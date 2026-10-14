#pragma once
#include "core/config.h"
#include <QObject>
#include <QFutureWatcher>
#include <QTimer>
#include <atomic>
#include <functional>
#include <memory>

namespace clipsapp {
// "lost" is the share of images the encoder had no time for, over the last seconds.
struct CaptureStats { double seconds = 0; qint64 bytes = 0; quint64 videoFrames = 0; QString encoder, warning; double lost = 0; };
struct ExportResult { QString path, error; double seconds = 0; };
using ExportTask = std::function<ExportResult()>;
class CaptureBackend {
public:
    virtual ~CaptureBackend() = default;
    virtual QString run(std::atomic_bool& stop, const std::function<void(QString)>& ready) = 0;
    virtual CaptureStats stats() const = 0;
    // Capture a snapshot now; execute its disk export independently of capture.
    virtual ExportTask prepareSave(const QString& path) const = 0;
    ExportResult save(const QString& path) const { return prepareSave(path)(); }
};
std::shared_ptr<CaptureBackend> createCaptureBackend(const RecordingProfile& profile);
using CaptureBackendFactory = std::function<std::shared_ptr<CaptureBackend>(const RecordingProfile&)>;

class ReplayEngine final : public QObject {
    Q_OBJECT
public:
    enum class State { Idle, Starting, Running, Stopping, Error };
    explicit ReplayEngine(QObject* parent = nullptr, CaptureBackendFactory factory = createCaptureBackend);
    ~ReplayEngine() override;
    bool start(const RecordingProfile& profile, const QString& directory, QString& error);
    void stop();
    bool save(QString& error);
    State state() const { return m_state; }
    bool exporting() const { return m_export.isRunning(); }
    CaptureStats stats() const;
    QString clipGameName() const { return m_gameName.isEmpty() ? m_profileName : m_gameName; }
    static bool supported();
signals:
    void changed();
    void failed(const QString& error);
    void saved(const QString& path, double seconds);
private:
    void setState(State state);
    State m_state = State::Idle;
    QString m_directory, m_profileName, m_gameName, m_videoTarget;
    bool m_detectGame = false;
    std::atomic_bool m_stop{false};
    CaptureBackendFactory m_factory;
    std::shared_ptr<CaptureBackend> m_backend;
    QFutureWatcher<QString> m_capture;
    QFutureWatcher<ExportResult> m_export;
    QTimer m_timer;
};
}
