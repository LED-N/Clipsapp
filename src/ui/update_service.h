#pragma once
#include "core/update.h"
#include <QCryptographicHash>
#include <QFile>
#include <QObject>
#include <QPointer>
class QNetworkAccessManager;
class QNetworkReply;
class QProcess;
namespace clipsapp {
// Asks GitHub for the latest release, and on request downloads its archive, checks it and
// unpacks it next to the application. Nothing is replaced before install() is called.
// Its only requests are those two; neither carries anything about the user or the clips.
class UpdateService final : public QObject {
    Q_OBJECT
public:
    enum class State { Idle, Checking, Available, Downloading, Unpacking, Ready, Failed };
    UpdateService(QString currentVersion, QString installDirectory, QObject* parent = nullptr);
    ~UpdateService() override;
    // The repository whose releases are followed, "owner/name". Without one the service stays silent.
    void setRepository(const QString& repository);
    // For tests: a release description and an archive read from local files.
    void setFeed(const QUrl& feed);
    bool enabled() const { return m_feed.isValid(); }
    State state() const { return m_state; }
    const UpdateInfo& info() const { return m_info; }
    QString currentVersion() const { return m_current.toString(); }
    QString error() const { return m_error; }
    double progress() const { return m_info.bytes > 0 ? double(m_received) / double(m_info.bytes) : 0; }
    qint64 receivedBytes() const { return m_received; }
    bool busy() const { return m_state == State::Checking || m_state == State::Downloading || m_state == State::Unpacking; }
    void check(bool manual = false);
    void download();
    void cancel();
    // Puts the unpacked version in place of the current one. The caller then restarts the application.
    bool install(QString& error);
    QString executablePath() const;
signals:
    void changed();
    // A check ended: a newer version (announced once per version unless asked by hand), none, or no answer.
    void available(bool manual);
    void upToDate(bool manual);
    void checkFailed(const QString& error, bool manual);
private:
    void setState(State state, const QString& error = {});
    void finishDownload();
    void unpack();
    void fail(const QString& error);
    QNetworkAccessManager* m_network;
    QPointer<QNetworkReply> m_reply;
    QProcess* m_unpacker = nullptr;
    QVersionNumber m_current, m_announced;
    QString m_install, m_repository, m_error, m_archive, m_staged;
    QUrl m_feed; bool m_local = false, m_fallbackTried = false;
    State m_state = State::Idle;
    UpdateInfo m_info;
    QFile m_file; QCryptographicHash m_hash{QCryptographicHash::Sha256}; qint64 m_received = 0;
};
}
