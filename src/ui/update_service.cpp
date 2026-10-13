#include "update_service.h"
#include <QDir>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>

namespace clipsapp {
namespace {
#ifdef Q_OS_WIN
const QString executableName = QStringLiteral("Clipsapp.exe");
#else
const QString executableName = QStringLiteral("clipsapp");
#endif
QNetworkRequest request(const QUrl& url, const QString& version, int quietMilliseconds) {
    QNetworkRequest result(url); result.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Clipsapp/%1").arg(version));
    result.setRawHeader("Accept", "application/vnd.github+json, application/octet-stream");
    result.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    // Gives up after that long without a byte, not after that long in total: a slow line still finishes.
    result.setTransferTimeout(quietMilliseconds); return result;
}
}
UpdateService::UpdateService(QString currentVersion, QString installDirectory, QObject* parent)
    : QObject(parent), m_network(new QNetworkAccessManager(this)), m_current(versionOfTag(currentVersion)), m_install(std::move(installDirectory)) {}
UpdateService::~UpdateService() {
    if (m_reply) { m_reply->disconnect(this); m_reply->abort(); }
    if (m_unpacker) { m_unpacker->disconnect(this); m_unpacker->kill(); m_unpacker->waitForFinished(2000); }
}
void UpdateService::setRepository(const QString& repository) { m_repository = repository.trimmed(); m_feed = latestReleaseUrl(m_repository); m_local = false; }
void UpdateService::setFeed(const QUrl& feed) { m_feed = feed; m_local = feed.isLocalFile(); }
QString UpdateService::executablePath() const { return QDir(m_install).filePath(executableName); }
void UpdateService::setState(State state, const QString& error) { m_state = state; m_error = error; emit changed(); }
void UpdateService::fail(const QString& error) {
    if (m_file.isOpen()) m_file.close();
    if (!m_archive.isEmpty()) QFile::remove(m_archive);
    setState(State::Failed, error);
}
void UpdateService::check(bool manual) {
    if (!enabled() || m_state == State::Checking) return;
    // A download under way, or a version waiting for its restart, already answers the question.
    if (m_state == State::Downloading || m_state == State::Unpacking || m_state == State::Ready) { if (manual) emit available(true); return; }
    const State before = m_state == State::Available || m_state == State::Failed ? m_state : State::Idle; const QString errorBefore = m_error;
    setState(State::Checking);
    auto* reply = m_network->get(request(m_feed, m_current.toString(), 15000)); m_reply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, manual, before, errorBefore] {
        reply->deleteLater(); if (m_state != State::Checking) return;
        const auto problem = reply->error(); const auto body = reply->readAll();
        // A repository without any release answers "not found": there is simply nothing newer.
        if (problem == QNetworkReply::ContentNotFoundError) { setState(State::Idle); emit upToDate(manual); return; }
        if (problem != QNetworkReply::NoError) { setState(before, errorBefore); emit checkFailed(QStringLiteral("GitHub n'a pas répondu (%1).").arg(reply->errorString()), manual); return; }
        QString error; const auto found = parseRelease(body, updateAssetSuffix(), error);
        if (!found) { setState(before, errorBefore); emit checkFailed(error, manual); return; }
        if (!newerVersion(found->version, m_current)) { m_info = {}; setState(State::Idle); emit upToDate(manual); return; }
        if (!m_local && !trustedDownload(found->download, m_repository)) { setState(before, errorBefore); emit checkFailed(QStringLiteral("L'archive annoncée ne vient pas des publications du projet."), manual); return; }
        const bool news = found->version != m_announced; m_info = *found; m_announced = found->version; setState(State::Available);
        if (news || manual) emit available(manual);
    });
}
void UpdateService::download() {
    if ((m_state != State::Available && m_state != State::Failed) || m_info.version.isNull()) return;
    if (!canUpdateInPlace(m_install)) { fail(QStringLiteral("Le dossier de Clipsapp n'est pas modifiable par votre compte. Déplacez-le, par exemple dans Documents, ou téléchargez la nouvelle version depuis la page du projet.")); return; }
    clearUpdateLeftovers(m_install);
    const QDir work(updateWorkDirectory(m_install)); m_archive = work.filePath(QStringLiteral("download/") + m_info.fileName); m_staged.clear();
    m_file.setFileName(m_archive); m_hash.reset(); m_received = 0; m_fallbackTried = false;
    if (!QDir().mkpath(QFileInfo(m_archive).absolutePath()) || !m_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) { fail(QStringLiteral("Impossible d'écrire le téléchargement dans le dossier de Clipsapp.")); return; }
    setState(State::Downloading);
    auto* reply = m_network->get(request(m_info.download, m_current.toString(), 30000)); m_reply = reply;
    const auto drain = [this, reply] {
        const auto data = reply->readAll(); if (data.isEmpty() || !m_file.isOpen()) return;
        const int before = int(progress() * 200); m_hash.addData(data); m_received += data.size();
        if (m_file.write(data) != data.size()) { reply->abort(); return; }
        if (int(progress() * 200) != before) emit changed();
    };
    connect(reply, &QNetworkReply::readyRead, this, drain);
    connect(reply, &QNetworkReply::finished, this, [this, reply, drain] {
        reply->deleteLater(); if (m_state != State::Downloading || m_reply != reply) return;
        if (reply->error() == QNetworkReply::NoError) drain();
        if (reply->error() != QNetworkReply::NoError) { fail(QStringLiteral("Le téléchargement s'est interrompu (%1).").arg(reply->errorString())); return; }
        finishDownload();
    });
}
void UpdateService::finishDownload() {
    const bool written = m_file.flush(); m_file.close();
    if (!written || m_received != m_info.bytes) { fail(QStringLiteral("Le téléchargement est incomplet.")); return; }
    // GitHub publishes the fingerprint of each archive: a file altered on the way is not unpacked.
    if (!m_info.sha256.isEmpty() && m_hash.result().toHex() != m_info.sha256) { fail(QStringLiteral("L'archive reçue n'est pas celle qui a été publiée. Rien n'a été installé.")); return; }
    unpack();
}
void UpdateService::unpack() {
    const QString target = QDir(updateWorkDirectory(m_install)).filePath(QStringLiteral("new"));
    QDir(target).removeRecursively();
    if (!QDir().mkpath(target)) { fail(QStringLiteral("Impossible de préparer le dossier de la nouvelle version.")); return; }
    if (m_state != State::Unpacking) setState(State::Unpacking);
    QString program = QStringLiteral("tar"); QStringList arguments{QStringLiteral("-xf"), QDir::toNativeSeparators(m_archive), QStringLiteral("-C"), QDir::toNativeSeparators(target)};
#ifdef Q_OS_WIN
    // Windows ships its own tar, which reads zip archives and refuses paths that leave the target folder.
    const QDir system(QDir(qEnvironmentVariable("SystemRoot", QStringLiteral("C:/Windows"))).filePath(QStringLiteral("System32")));
    program = system.filePath(QStringLiteral("tar.exe"));
    if (m_fallbackTried || !QFileInfo::exists(program)) {
        m_fallbackTried = true; const auto quoted = [](QString path) { return QStringLiteral("'") + path.replace(QStringLiteral("'"), QStringLiteral("''")) + QStringLiteral("'"); };
        program = system.filePath(QStringLiteral("WindowsPowerShell/v1.0/powershell.exe"));
        arguments = {QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"), QStringLiteral("-Command"), QStringLiteral("Expand-Archive -LiteralPath %1 -DestinationPath %2 -Force").arg(quoted(QDir::toNativeSeparators(m_archive)), quoted(QDir::toNativeSeparators(target)))};
    }
#endif
    auto* process = new QProcess(this); m_unpacker = process; process->setProcessChannelMode(QProcess::MergedChannels);
    const auto done = [this, process, target](bool succeeded) {
        if (m_unpacker != process) return;
        m_unpacker = nullptr; process->deleteLater(); if (m_state != State::Unpacking) return;
        const auto root = succeeded ? stagedRoot(target, executableName) : QString();
        if (!root.isEmpty()) { m_staged = root; QFile::remove(m_archive); setState(State::Ready); return; }
#ifdef Q_OS_WIN
        if (!m_fallbackTried) { m_fallbackTried = true; unpack(); return; }
#endif
        fail(QStringLiteral("L'archive n'a pas pu être décompressée."));
    };
    connect(process, &QProcess::finished, this, [done](int code, QProcess::ExitStatus status) { done(code == 0 && status == QProcess::NormalExit); });
    connect(process, &QProcess::errorOccurred, this, [done](QProcess::ProcessError error) { if (error == QProcess::FailedToStart) done(false); });
    process->start(program, arguments);
}
void UpdateService::cancel() {
    if (m_state == State::Downloading && m_reply) { auto* reply = m_reply.data(); m_reply = nullptr; reply->disconnect(this); reply->abort(); reply->deleteLater(); }
    else if (m_state == State::Unpacking && m_unpacker) { auto* process = m_unpacker; m_unpacker = nullptr; process->disconnect(this); process->kill(); process->waitForFinished(2000); process->deleteLater(); }
    else return;
    if (m_file.isOpen()) m_file.close();
    clearUpdateLeftovers(m_install); m_received = 0; setState(State::Available);
}
bool UpdateService::install(QString& error) {
    if (m_state != State::Ready) { error = QStringLiteral("Aucune mise à jour n'est prête."); return false; }
    if (!applyUpdate(m_staged, m_install, executableName, error)) return false;
    m_staged.clear(); m_info = {}; setState(State::Idle); return true;
}
}
