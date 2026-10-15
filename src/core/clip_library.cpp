#include "clip_library.h"
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QRegularExpression>
#include <QSaveFile>
#include <QFile>
#include <QSet>
#include <QThread>
#include <QUuid>
#include <algorithm>

namespace clipsapp {
namespace {
QString pathKey(const QString& path) {
    auto key = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
#ifdef Q_OS_WIN
    key = key.toCaseFolded();
#endif
    return key;
}
}
LibraryLabels::LibraryLabels(QString path) : m_path(std::move(path)) {}
bool LibraryLabels::load(QString& error) {
    QFile file(m_path); error.clear(); m_state = {}; m_readable = true;
    if (!file.exists()) return true;
    if (!file.open(QIODevice::ReadOnly) || file.size() > 4 * 1024 * 1024) { error = QStringLiteral("Classement des clips illisible."); m_readable = false; return false; }
    QJsonParseError parse; const auto document = QJsonDocument::fromJson(file.readAll(), &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject() || document.object().value("version").toInt() != 1) { error = QStringLiteral("Classement des clips illisible, fichier conservé."); m_readable = false; return false; }
    const auto games = document.object().value("games").toObject(); const auto favorites = document.object().value("favorites").toArray(); const auto shares = document.object().value("shares").toObject();
    if (games.size() > 10000 || favorites.size() > 10000 || shares.size() > 10000) { error = QStringLiteral("Trop de clips classés manuellement."); m_readable = false; return false; }
    for (auto it = games.begin(); it != games.end(); ++it) if (!it.value().toString().isEmpty()) m_state.games.insert(pathKey(it.key()), it.value().toString().left(80));
    for (const auto& favorite : favorites) if (!favorite.toString().isEmpty()) m_state.favorites.insert(pathKey(favorite.toString()));
    for (auto it = shares.begin(); it != shares.end(); ++it) {
        const auto link = it.value().toObject(); if (link.value("source").toString().isEmpty()) continue;
        m_state.shares.insert(pathKey(it.key()), {link.value("source").toString(), link.value("limitBytes").toInteger()});
    }
    return true;
}
QString LibraryLabels::gameFor(const QString& clip, const QString& fallback) const { return m_state.games.value(pathKey(clip), fallback); }
bool LibraryLabels::setGame(const QString& clip, const QString& game, QString& error) {
    return setGames({clip}, game, error);
}
QStringList LibraryLabels::gameNames() const {
    auto names = m_state.games.values(); names.removeDuplicates(); names.sort(Qt::CaseInsensitive); return names;
}
bool LibraryLabels::write(State state, QString& error) {
    error.clear();
    if (!m_readable) { error = QStringLiteral("Le fichier de classement est illisible. Il est conservé sans modification."); return false; }
    if (state.games.size() > 10000 || state.favorites.size() > 10000 || state.shares.size() > 10000) { error = QStringLiteral("Le classement manuel est limité à 10 000 clips."); return false; }
    if (!QDir().mkpath(QFileInfo(m_path).absolutePath())) { error = QStringLiteral("Impossible de créer le dossier de classement."); return false; }
    QJsonObject games, shares; for (auto it = state.games.cbegin(); it != state.games.cend(); ++it) games.insert(it.key(), it.value());
    auto favorites = state.favorites.values(); favorites.sort();
    for (auto it = state.shares.cbegin(); it != state.shares.cend(); ++it) shares.insert(it.key(), QJsonObject{{"source", it.value().source}, {"limitBytes", it.value().limitBytes}});
    QSaveFile file(m_path); const auto bytes = QJsonDocument(QJsonObject{{"version", 1}, {"games", games}, {"favorites", QJsonArray::fromStringList(favorites)}, {"shares", shares}}).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) { error = file.errorString(); return false; }
    m_state = std::move(state); return true;
}
bool LibraryLabels::setGames(const QStringList& clips, const QString& game, QString& error) {
    auto state = m_state; const auto value = game.trimmed().left(80);
    for (const auto& clip : clips) {
        const auto key = pathKey(clip);
        if (value.isEmpty()) state.games.remove(key); else state.games.insert(key, value);
    }
    return write(std::move(state), error);
}
bool LibraryLabels::isFavorite(const QString& clip) const { return m_state.favorites.contains(pathKey(clip)); }
bool LibraryLabels::setFavorites(const QStringList& clips, bool favorite, QString& error) {
    auto state = m_state;
    for (const auto& clip : clips) { if (favorite) state.favorites.insert(pathKey(clip)); else state.favorites.remove(pathKey(clip)); }
    return write(std::move(state), error);
}
ShareLink LibraryLabels::shareLink(const QString& copy) const { return m_state.shares.value(pathKey(copy)); }
bool LibraryLabels::setShareLink(const QString& copy, const QString& source, qint64 limitBytes, QString& error) {
    auto state = m_state; state.shares.insert(pathKey(copy), {QFileInfo(source).absoluteFilePath(), limitBytes});
    return write(std::move(state), error);
}
QStringList LibraryLabels::shareCopies(const QString& source) const {
    QStringList copies; const auto key = pathKey(source);
    for (auto it = m_state.shares.cbegin(); it != m_state.shares.cend(); ++it) if (pathKey(it.value().source) == key) copies.append(it.key());
    return copies;
}
bool LibraryLabels::moveClip(const QString& from, const QString& to, QString& error) {
    auto state = m_state; const auto old = pathKey(from), now = pathKey(to);
    if (state.games.contains(old)) state.games.insert(now, state.games.take(old));
    if (state.favorites.remove(old)) state.favorites.insert(now);
    for (auto& link : state.shares) if (pathKey(link.source) == old) link.source = QFileInfo(to).absoluteFilePath();
    return write(std::move(state), error);
}
bool LibraryLabels::forget(const QStringList& clips, QString& error) {
    auto state = m_state;
    for (const auto& clip : clips) { const auto key = pathKey(clip); state.games.remove(key); state.favorites.remove(key); state.shares.remove(key); }
    return write(std::move(state), error);
}
QString formatBytes(qint64 bytes) {
    const QLocale french(QLocale::French, QLocale::France);
    if (bytes >= 1000000000) return french.toString(bytes / 1000000000.0, 'f', 1) + QStringLiteral(" Go");
    return french.toString(bytes / 1000000.0, 'f', bytes >= 100000000 ? 0 : 1) + QStringLiteral(" Mo");
}
QString formatLimit(qint64 bytes) {
    if (bytes >= 1000000000 && bytes % 1000000000 == 0) return QStringLiteral("%1 Go").arg(bytes / 1000000000);
    if (bytes % 1000000 == 0) return QStringLiteral("%1 Mo").arg(bytes / 1000000);
    return formatBytes(bytes);
}
QString dateGroupTitle(const QDate& day, const QDate& today) {
    if (day == today) return QStringLiteral("Aujourd’hui");
    const QLocale french(QLocale::French, QLocale::France);
    auto title = french.toString(day, day.year() == today.year() ? QStringLiteral("dddd d MMMM") : QStringLiteral("dddd d MMMM yyyy"));
    if (day == today.addDays(-1)) return QStringLiteral("Hier · ") + title;
    if (!title.isEmpty()) title[0] = title[0].toUpper();
    return title;
}
ClipRename renameClipFile(const QString& path, const QString& name) {
    const QFileInfo file(path); const auto wanted = name.trimmed();
    if (!file.isFile() || file.isSymLink()) return {{}, QStringLiteral("Le clip n'est plus disponible.")};
    static const QRegularExpression forbidden(QStringLiteral("[<>:\"/\\\\|?*\\x00-\\x1f]"));
    if (wanted.isEmpty() || wanted.size() > 120 || wanted.contains(forbidden) || wanted.endsWith('.') || wanted.startsWith('.'))
        return {{}, QStringLiteral("Ce nom ne peut pas servir de nom de fichier.")};
    if (wanted.endsWith(QStringLiteral(".discord"), Qt::CaseInsensitive)) return {{}, QStringLiteral("Ce nom est réservé aux copies Discord.")};
    const auto target = file.absoluteDir().filePath(wanted + '.' + file.suffix());
    if (target == file.absoluteFilePath()) return {target, {}};
    // A change of letter case only is the same file on Windows, and still a valid rename.
    if (QFileInfo::exists(target) && pathKey(target) != pathKey(path)) return {{}, QStringLiteral("Un autre clip porte déjà ce nom.")};
    QString error;
    for (int attempt = 0; attempt < 4; ++attempt) {
        QFile source(file.absoluteFilePath());
        if (source.rename(target)) return {target, {}};
        error = source.errorString();
        if (attempt < 3) QThread::msleep(75);
    }
    return {{}, QStringLiteral("Impossible de renommer le clip : ") + error};
}
QString screenshotsDirectory(const QString& libraryPath) { return QDir(libraryPath).filePath(QStringLiteral("Screenshots")); }
bool isImageFile(const QString& path) { return QStringList{"png", "jpg", "jpeg", "webp", "bmp"}.contains(QFileInfo(path).suffix().toLower()); }
QString capturedFileName(const QString& gameFolder, const QString& suffix) {
    return QStringLiteral("%1-%2-%3.%4").arg(gameFolder, QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss-zzz")), QUuid::createUuid().toString(QUuid::Id128).left(8), suffix);
}
namespace {
LibraryScan scanFiles(const QString& directory, const std::function<bool(const QFileInfo&, const QStringList&)>& wanted) {
    LibraryScan result;
    const QDir root(directory);
    if (!root.exists()) return result;
    QDirIterator it(directory, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
    int visited = 0;
    while (it.hasNext()) {
        it.next();
        if (++visited > 50000 || result.clips.size() >= 10000) { result.warning = QStringLiteral("La bibliothèque est volumineuse. Affichage limité aux premiers fichiers trouvés."); break; }
        const QFileInfo info = it.fileInfo();
        const auto relative = root.relativeFilePath(info.absoluteFilePath());
        const auto parts = relative.split('/');
        if (!wanted(info, parts)) continue;
        const QString game = parts.size() > 1 ? parts.first() : QStringLiteral("Non classé");
        result.clips.append({info.absoluteFilePath(), info.completeBaseName(), game, info.suffix().toUpper(), info.size(), info.lastModified()});
    }
    std::sort(result.clips.begin(), result.clips.end(), [](const auto& left, const auto& right) { return left.modified > right.modified; });
    return result;
}
}
LibraryScan scanLibrary(const QString& directory, bool sharingOnly) {
    static const QStringList extensions{"mp4", "mkv", "webm", "mov", "avi"};
    return scanFiles(directory, [sharingOnly](const QFileInfo& info, const QStringList& parts) {
        if (!sharingOnly && parts.size() > 1 && parts.first().compare(QStringLiteral("Screenshots"), Qt::CaseInsensitive) == 0) return false;
        return info.fileName().endsWith(".discord.mp4", Qt::CaseInsensitive) == sharingOnly && extensions.contains(info.suffix().toLower());
    });
}
LibraryScan scanImages(const QString& directory) { return scanFiles(directory, [](const QFileInfo& info, const QStringList&) { return isImageFile(info.fileName()); }); }
ClipTrashResult trashClips(const QStringList& paths, ClipTrashOperation operation) {
    ClipTrashResult result; QSet<QString> seen;
    if (!operation) operation = [](const QString& path, QString& error) {
        // A cancelled preview can still be closing its reader. Retry briefly
        // on the worker thread; never fall back to permanent deletion.
        for (int attempt = 0; attempt < 4; ++attempt) {
            QFile file(path);
            if (file.moveToTrash()) return true;
            error = file.errorString();
            if (!QFileInfo::exists(path)) break;
            if (attempt < 3) QThread::msleep(75);
        }
        return false;
    };
    for (const auto& path : paths) {
        const QFileInfo file(path); const auto key = pathKey(path);
        if (seen.contains(key)) continue;
        seen.insert(key);
        QString error;
        if (path.isEmpty() || !file.isFile() || file.isSymLink()) error = QStringLiteral("Le clip n'est plus disponible ou n'est pas un fichier vidéo local.");
        else if (operation(file.absoluteFilePath(), error)) { result.removed.append(file.absoluteFilePath()); continue; }
        result.errors.append(file.fileName() + " : " + (error.isEmpty() ? QStringLiteral("Impossible de déplacer le clip dans la corbeille.") : error));
    }
    return result;
}
}

