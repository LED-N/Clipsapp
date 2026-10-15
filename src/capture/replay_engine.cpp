#include "replay_engine.h"
#include "core/game_identity.h"
#include "platform/game_detection.h"
#include <QDateTime>
#include <QDir>
#include <QRegularExpression>
#include <QUuid>
#include <QtConcurrent/QtConcurrentRun>

namespace clipsapp {
ReplayEngine::ReplayEngine(QObject* parent, CaptureBackendFactory factory) : QObject(parent), m_factory(std::move(factory)) {
    m_timer.setInterval(500); connect(&m_timer, &QTimer::timeout, this, [this] {
        if (m_detectGame && m_state == State::Running) { const auto name = detectCapturedGame(m_videoTarget); if (!name.isEmpty()) m_gameName = name; }
        emit changed();
    });
    connect(&m_capture, &QFutureWatcher<QString>::finished, this, [this] {
        const auto error = m_capture.result(); m_timer.stop();
        if (error.isEmpty()) qInfo("Replay : arrêté."); else qWarning("Replay : arrêté sur une erreur : %s", qUtf8Printable(error));
        m_backend.reset();
        setState(error.isEmpty() ? State::Idle : State::Error);
        if (!error.isEmpty()) emit failed(error);
    });
    connect(&m_export, &QFutureWatcher<ExportResult>::finished, this, [this] {
        const auto result = m_export.result(); emit changed();
        if (result.error.isEmpty()) qInfo("Replay : clip de %.1f s sauvegardé dans %s.", result.seconds, qUtf8Printable(result.path)); else qWarning("Replay : sauvegarde impossible : %s", qUtf8Printable(result.error));
        if (result.error.isEmpty()) emit saved(result.path, result.seconds); else emit failed(result.error);
    });
}
ReplayEngine::~ReplayEngine() { m_stop = true; m_capture.waitForFinished(); m_export.waitForFinished(); }
bool ReplayEngine::supported() {
#ifdef Q_OS_WIN
    return true;
#else
    return false;
#endif
}
void ReplayEngine::setState(State state) { m_state = state; emit changed(); }
CaptureStats ReplayEngine::stats() const { return m_backend ? m_backend->stats() : CaptureStats{}; }
bool ReplayEngine::start(const RecordingProfile& profile, const QString& directory, QString& error) {
    if (!supported()) { error = QStringLiteral("La capture Linux sera ajoutée avec PipeWire et le portail KDE."); return false; }
    if (m_state == State::Starting || m_state == State::Running || m_state == State::Stopping || m_capture.isRunning() || m_export.isRunning()) { error = QStringLiteral("Une capture ou une sauvegarde est encore en cours."); return false; }
    if (profile.video.targetId.isEmpty()) { error = QStringLiteral("Choisissez un écran ou une fenêtre dans la configuration."); return false; }
    for (const auto& source : profile.sources)
        if (source.enabled && source.targetId.isEmpty()) { error = QStringLiteral("Choisissez une cible pour la source audio : ") + source.name; return false; }
    if (!QDir::isAbsolutePath(directory) || !QDir().mkpath(directory)) { error = QStringLiteral("Impossible de créer le dossier des clips."); return false; }
    m_directory = directory; m_profileName = profile.name; m_stop = false;
    m_gameName = profile.gameName.trimmed(); m_detectGame = profile.detectGame && m_gameName.isEmpty(); m_videoTarget = profile.video.targetId;
    if (m_detectGame) m_gameName = detectCapturedGame(m_videoTarget);
    m_backend = m_factory(profile);
    if (!m_backend) { error = QStringLiteral("Moteur de capture indisponible sur ce système."); return false; }
    const auto& video = profile.video; QStringList sources; for (const auto& source : profile.sources) if (source.enabled) sources.append(QStringLiteral("%1 (%2)").arg(source.name, sourceKindKey(source.kind)));
    qInfo("Replay : démarrage du profil « %s » sur %s, %dx%d à %d FPS, %s, effort %s, %s %d, encodeur %s, %d s gardées, plafond %d Mio. Sources : %s.", qUtf8Printable(profile.name), qUtf8Printable(video.targetId.section('|', 0, 0)), video.width, video.height, video.fps,
        qUtf8Printable(video.codec), qUtf8Printable(video.effort), qUtf8Printable(video.rateControl), video.rateControl == QLatin1String("quality") ? video.quality : video.bitrateMbps, qUtf8Printable(video.encoder), profile.replay.durationSeconds, profile.replay.memoryLimitMiB, qUtf8Printable(sources.join(QStringLiteral(", "))));
    setState(State::Starting); m_timer.start();
    const auto backend = m_backend;
    m_capture.setFuture(QtConcurrent::run([this, backend] {
        return backend->run(m_stop, [this](QString encoder) {
            qInfo("Replay : actif, encodeur %s.", qUtf8Printable(encoder));
            QMetaObject::invokeMethod(this, [this] { if (m_state == State::Starting) setState(State::Running); }, Qt::QueuedConnection);
        });
    }));
    return true;
}
void ReplayEngine::stop() {
    if (m_state == State::Starting || m_state == State::Running) { m_stop = true; setState(State::Stopping); }
}
bool ReplayEngine::save(QString& error) {
    if (m_state != State::Running) { error = QStringLiteral("Activez le replay avant de sauvegarder un clip."); return false; }
    if (m_export.isRunning()) { error = QStringLiteral("Un clip est déjà en cours de sauvegarde."); return false; }
    if (!m_backend || stats().seconds <= 0) { error = QStringLiteral("Le buffer attend encore sa première image clé. Réessayez dans quelques secondes."); return false; }
    if (m_detectGame) { const auto name = detectCapturedGame(m_videoTarget); if (!name.isEmpty()) m_gameName = name; }
    const auto folder = safeGameFolder(m_gameName.isEmpty() ? m_profileName : m_gameName);
    const auto destination = QDir(m_directory).filePath(folder);
    if (!QDir().mkpath(destination)) { error = QStringLiteral("Impossible de créer le sous-dossier des clips."); return false; }
    const auto filename = QString("%1-%2-%3.mp4").arg(folder, QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm-ss-zzz"), QUuid::createUuid().toString(QUuid::Id128).left(8));
    const auto path = QDir(destination).filePath(filename);
    const auto task = m_backend->prepareSave(path);
    m_export.setFuture(QtConcurrent::run(task)); emit changed(); return true;
}
#ifndef Q_OS_WIN
std::shared_ptr<CaptureBackend> createCaptureBackend(const RecordingProfile&) { return {}; }
#endif
}
