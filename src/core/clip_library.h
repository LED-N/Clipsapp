#pragma once
#include <QDateTime>
#include <QList>
#include <QString>
#include <QHash>
#include <QSet>
#include <QStringList>
#include <functional>

namespace clipsapp {
struct ClipEntry { QString path; QString name; QString game; QString format; qint64 size = 0; QDateTime modified; };
struct LibraryScan { QList<ClipEntry> clips; QString warning; };
// The clips under a folder, the folder of the screenshots left aside.
LibraryScan scanLibrary(const QString& directory, bool sharingOnly = false);
// Screenshots live in their own folder of the library, one sub-folder per game as the clips do.
QString screenshotsDirectory(const QString& libraryPath);
bool isImageFile(const QString& path);
// The pictures under the folder of the screenshots: PNG, JPEG, WebP and BMP.
LibraryScan scanImages(const QString& directory);
// The name of a file the application saves: the game, the moment, and a short mark that keeps two names apart.
QString capturedFileName(const QString& gameFolder, const QString& suffix);
struct ClipTrashResult { QStringList removed, errors; };
using ClipTrashOperation = std::function<bool(const QString&, QString&)>;
ClipTrashResult trashClips(const QStringList& paths, ClipTrashOperation operation = {});
// Renames the file in place: same folder, same extension, never over another file.
struct ClipRename { QString path, error; };
ClipRename renameClipFile(const QString& path, const QString& name);
// Decimal units, as Discord counts them: 1 Mo = 1 000 000 octets.
QString formatBytes(qint64 bytes);
// A limit reads as a round number: « 20 Mo », « 1 Go ».
QString formatLimit(qint64 bytes);
QString dateGroupTitle(const QDate& day, const QDate& today);
// A Discord copy remembers the clip it was made from and the limit it was made for.
struct ShareLink { QString source; qint64 limitBytes = 0; };
class LibraryLabels {
public:
    explicit LibraryLabels(QString path);
    bool load(QString& error);
    bool setGame(const QString& clip, const QString& game, QString& error);
    bool setGames(const QStringList& clips, const QString& game, QString& error);
    QStringList gameNames() const;
    QString gameFor(const QString& clip, const QString& fallback) const;
    bool isFavorite(const QString& clip) const;
    bool setFavorites(const QStringList& clips, bool favorite, QString& error);
    ShareLink shareLink(const QString& copy) const;
    bool setShareLink(const QString& copy, const QString& source, qint64 limitBytes, QString& error);
    QStringList shareCopies(const QString& source) const;
    // Carries every mark of a renamed clip over to its new path.
    bool moveClip(const QString& from, const QString& to, QString& error);
    bool forget(const QStringList& clips, QString& error);
private:
    struct State { QHash<QString, QString> games; QSet<QString> favorites; QHash<QString, ShareLink> shares; };
    bool write(State state, QString& error);
    QString m_path;
    State m_state;
    bool m_readable = true;
};
}
