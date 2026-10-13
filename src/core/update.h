#pragma once
#include <QByteArray>
#include <QString>
#include <QUrl>
#include <QVersionNumber>
#include <optional>
namespace clipsapp {
// A version published on GitHub, and the archive that carries it for this system.
struct UpdateInfo {
    QVersionNumber version;
    QString title, notes;
    QUrl page, download;
    QString fileName;
    qint64 bytes = 0;
    // Hexadecimal SHA-256 announced by GitHub for the archive; empty when it gives none.
    QByteArray sha256;
};
// The end of the name of the archive made for this system, as scripts/release-windows.ps1 writes it.
QString updateAssetSuffix();
// Where GitHub tells which release is the latest, for a repository written "owner/name". Empty when there is none.
QUrl latestReleaseUrl(const QString& repository);
// "v0.8.1" and "0.8.1" are the same version.
QVersionNumber versionOfTag(const QString& tag);
// 0.8 and 0.8.0 are the same version: neither is newer than the other.
bool newerVersion(const QVersionNumber& candidate, const QVersionNumber& current);
// Reads GitHub's answer. Drafts, pre-releases and releases without an archive for this system are not updates.
std::optional<UpdateInfo> parseRelease(const QByteArray& json, const QString& assetSuffix, QString& error);
// Only an archive of the repository's own releases, over HTTPS, is ever downloaded.
bool trustedDownload(const QUrl& download, const QString& repository);

// The application is a folder of files, without an installer: updating it is replacing them.
// Windows lets a running program be renamed but not overwritten, so the files in use are set
// aside in a folder of the installation, the new ones take their names, and the old ones are
// removed at the next start. If anything fails halfway, every file goes back to its place.
QString updateWorkDirectory(const QString& installDirectory);
bool canUpdateInPlace(const QString& installDirectory);
// The folder of an unpacked archive that holds the application: the folder itself, or its only sub-folder.
QString stagedRoot(const QString& unpackedDirectory, const QString& executableName);
bool applyUpdate(const QString& stagedDirectory, const QString& installDirectory, const QString& executableName, QString& error);
// Removes what an update set aside. Files still held by the version that is closing stay until the next call.
void clearUpdateLeftovers(const QString& installDirectory);
}
