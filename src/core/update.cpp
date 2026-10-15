#include "update.h"
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <algorithm>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTemporaryFile>

namespace clipsapp {
namespace {
// Where a release lists its files: with its libraries since 0.8.3, beside the executable before.
const char* const manifestName = "runtime/manifest.txt";
const char* const earlierManifestName = "manifest.txt";
// A manifest lists the files of a release, one relative path per line. Anything else is not followed.
QSet<QString> readManifest(const QString& directory) {
    QSet<QString> files; QFile file(QDir(directory).filePath(manifestName));
    if (!file.exists()) file.setFileName(QDir(directory).filePath(earlierManifestName));
    if (!file.open(QIODevice::ReadOnly)) return files;
    for (const auto& raw : file.readAll().split('\n')) {
        const auto path = QDir::fromNativeSeparators(QString::fromUtf8(raw).trimmed());
        if (path.isEmpty() || QDir::isAbsolutePath(path) || path.contains(QLatin1Char(':')) || path.split('/').contains(QStringLiteral("..")) || path.startsWith(QStringLiteral(".update"))) continue;
        files.insert(path);
    }
    return files;
}
// The folders that held these files, deepest first, removed when nothing is left in them. A folder
// with anything else in it, the user's own files included, stays.
void removeEmptiedFolders(const QString& installDirectory, const QSet<QString>& files) {
    QSet<QString> folders;
    for (auto relative : files) while (relative.contains('/')) { relative = relative.section('/', 0, -2); folders.insert(relative); }
    auto ordered = folders.values(); std::sort(ordered.begin(), ordered.end(), [](const QString& left, const QString& right) { return left.count('/') > right.count('/'); });
    const QDir install(installDirectory); for (const auto& folder : ordered) install.rmdir(folder);
}
struct Move { QString from, to; };
bool move(const QString& from, const QString& to, QList<Move>& done) {
    if (!QDir().mkpath(QFileInfo(to).absolutePath()) || !QFile::rename(from, to)) return false;
    done.append({from, to}); return true;
}
}
QString updateAssetSuffix() {
#ifdef Q_OS_WIN
    return QStringLiteral("-windows-x64.zip");
#else
    return QStringLiteral("-linux-x64.tar.gz");
#endif
}
QUrl latestReleaseUrl(const QString& repository) {
    const auto parts = repository.trimmed().split('/'); if (parts.size() != 2 || parts[0].isEmpty() || parts[1].isEmpty()) return {};
    return QUrl(QStringLiteral("https://api.github.com/repos/%1/%2/releases/latest").arg(parts[0], parts[1]));
}
QVersionNumber versionOfTag(const QString& tag) {
    auto text = tag.trimmed(); if (text.startsWith('v', Qt::CaseInsensitive)) text.remove(0, 1);
    qsizetype suffix = 0; const auto version = QVersionNumber::fromString(text, &suffix);
    // "0.8.1-beta" is not 0.8.1: a tag with anything after its numbers is not a version to install.
    return suffix == text.size() ? version : QVersionNumber();
}
bool newerVersion(const QVersionNumber& candidate, const QVersionNumber& current) { return QVersionNumber::compare(candidate.normalized(), current.normalized()) > 0; }
std::optional<UpdateInfo> parseRelease(const QByteArray& json, const QString& assetSuffix, QString& error) {
    QJsonParseError problem; const auto document = QJsonDocument::fromJson(json, &problem);
    if (!document.isObject()) { error = QStringLiteral("Réponse de GitHub illisible."); return {}; }
    const auto release = document.object();
    if (release.value("draft").toBool() || release.value("prerelease").toBool()) { error = QStringLiteral("La dernière publication n'est pas une version finale."); return {}; }
    UpdateInfo info; info.version = versionOfTag(release.value("tag_name").toString());
    if (info.version.isNull()) { error = QStringLiteral("La dernière publication n'a pas de numéro de version."); return {}; }
    info.title = release.value("name").toString().trimmed(); info.notes = release.value("body").toString().trimmed(); info.page = QUrl(release.value("html_url").toString());
    for (const auto& value : release.value("assets").toArray()) {
        const auto asset = value.toObject(); const auto name = asset.value("name").toString();
        if (!name.endsWith(assetSuffix, Qt::CaseInsensitive) || asset.value("state").toString(QStringLiteral("uploaded")) != QStringLiteral("uploaded")) continue;
        info.fileName = QFileInfo(name).fileName(); info.download = QUrl(asset.value("browser_download_url").toString()); info.bytes = asset.value("size").toInteger();
        const auto digest = asset.value("digest").toString(); if (digest.startsWith(QStringLiteral("sha256:"))) info.sha256 = digest.mid(7).toLatin1().toLower();
        break;
    }
    if (!info.download.isValid() || info.fileName.isEmpty() || info.bytes <= 0) { error = QStringLiteral("La version %1 n'a pas d'archive pour ce système.").arg(info.version.toString()); return {}; }
    return info;
}
bool trustedDownload(const QUrl& download, const QString& repository) {
    return download.scheme() == QStringLiteral("https") && download.host().compare(QStringLiteral("github.com"), Qt::CaseInsensitive) == 0
        && !repository.isEmpty() && download.path().startsWith(QStringLiteral("/%1/releases/download/").arg(repository), Qt::CaseInsensitive);
}
QString updateWorkDirectory(const QString& installDirectory) { return QDir(installDirectory).filePath(QStringLiteral(".update")); }
bool canUpdateInPlace(const QString& installDirectory) {
    // Asking the file system is the only sure answer: a folder under Program Files says yes and then refuses.
    QTemporaryFile probe(QDir(installDirectory).filePath(QStringLiteral(".update-check-XXXXXX"))); return probe.open();
}
QString stagedRoot(const QString& unpackedDirectory, const QString& executableName) {
    const QDir directory(unpackedDirectory); if (directory.exists(executableName)) return directory.absolutePath();
    const auto inner = directory.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    if (inner.size() == 1 && QDir(directory.filePath(inner.first())).exists(executableName)) return directory.filePath(inner.first());
    return {};
}
bool applyUpdate(const QString& stagedDirectory, const QString& installDirectory, const QString& executableName, QString& error) {
    const QDir staged(stagedDirectory), install(installDirectory);
    if (stagedDirectory.isEmpty() || !staged.exists(executableName)) { error = QStringLiteral("L'archive ne contient pas %1.").arg(executableName); return false; }
    // What the version in place shipped, read before anything moves: the new list may take its place.
    const auto previous = readManifest(install.absolutePath());
    QStringList incoming; QDirIterator walk(staged.absolutePath(), QDir::Files | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
    while (walk.hasNext()) { const auto relative = staged.relativeFilePath(walk.next()); if (!relative.startsWith(QStringLiteral(".update"))) incoming.append(relative); }
    const QDir aside(QDir(updateWorkDirectory(installDirectory)).filePath(QStringLiteral("old-%1").arg(QDateTime::currentMSecsSinceEpoch())));
    QList<Move> done;
    const auto undo = [&](const QString& what) {
        for (auto step = done.crbegin(); step != done.crend(); ++step) QFile::rename(step->to, step->from);
        error = QStringLiteral("%1 n'a pas pu être remplacé. La version actuelle est restée en place.").arg(QDir::toNativeSeparators(what)); return false;
    };
    for (const auto& relative : incoming) {
        const auto target = install.filePath(relative);
        if (QFileInfo::exists(target) && !move(target, aside.filePath(relative), done)) return undo(relative);
        if (!move(staged.filePath(relative), target, done)) return undo(relative);
    }
    // Files the previous version shipped and this one no longer does are set aside too. Anything else in the folder is not ours to touch.
    QSet<QString> kept; for (const auto& relative : incoming) kept.insert(relative.toLower());
    for (const auto& relative : previous) {
        if (kept.contains(relative.toLower()) || !QFileInfo(install.filePath(relative)).isFile()) continue;
        if (!move(install.filePath(relative), aside.filePath(relative), done)) return undo(relative);
    }
    // A version that arranges its files otherwise leaves no empty folder behind.
    removeEmptiedFolders(install.absolutePath(), previous);
    return true;
}
void clearUpdateLeftovers(const QString& installDirectory) {
    QDir work(updateWorkDirectory(installDirectory)); if (!work.exists()) return;
    // An update made by an earlier version set its files aside without their folders: those it emptied go now.
    QSet<QString> previous; for (const auto& aside : work.entryList({QStringLiteral("old-*")}, QDir::Dirs)) previous.unite(readManifest(work.filePath(aside)));
    work.removeRecursively();
    removeEmptiedFolders(installDirectory, previous);
}
bool publishedCopy(const QString& installDirectory) { return QFile::exists(QDir(installDirectory).filePath(manifestName)) || QFile::exists(QDir(installDirectory).filePath(earlierManifestName)); }
}
