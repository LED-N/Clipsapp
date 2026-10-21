#pragma once
#include "core/config.h"
#include <QObject>
#include <QFutureWatcher>
#include <QTimer>
#include <atomic>
#include <functional>
#include <memory>

namespace clipsapp {
// "lost" is the share of images the encoder had no time for, over the last seconds. "silent" names
// the audio sources whose device is missing: their tracks are silent until it comes back.
// "recording" says a recording is being written: its length and its weight so far.
struct CaptureStats {
    double seconds = 0; qint64 bytes = 0; quint64 videoFrames = 0; QString encoder, warning; double lost = 0; QStringList silent;
    bool recording = false; double recordingSeconds = 0; qint64 recordingBytes = 0;
    // Why the replay keeps nothing although it was asked to: its buffer on disk could not be set up.
    QString bufferError;
};
struct ExportResult { QString path, error; double seconds = 0; };
// A recording that ended. Its file is kept, readable, even when an error stopped it; "path" is
// empty when none ended.
struct RecordingResult { QString path, error; double seconds = 0; bool kept = false; };
using ExportTask = std::function<ExportResult()>;
class CaptureBackend {
public:
    virtual ~CaptureBackend() = default;
    virtual QString run(std::atomic_bool& stop, const std::function<void(QString)>& ready) = 0;
    virtual CaptureStats stats() const = 0;
    // Capture a snapshot now; execute its disk export independently of capture.
    virtual ExportTask prepareSave(const QString& path) const = 0;
    ExportResult save(const QString& path) const { return prepareSave(path)(); }
    // Everything encoded from now on goes into this file too, until stopped. Returns why it cannot.
    virtual QString startRecording(const QString&) { return QStringLiteral("L'enregistrement n'est pas disponible ici."); }
    // The file is completed in the background; takeRecording says when it is.
    virtual void stopRecording() {}
    virtual RecordingResult takeRecording() { return {}; }
    // Whether the replay keeps its last seconds. A capture that only records keeps none: its buffer
    // is emptied and takes no memory. Called before run, it says how the capture starts.
    virtual void setBuffering(bool) {}
};
std::shared_ptr<CaptureBackend> createCaptureBackend(const RecordingProfile& profile);
using CaptureBackendFactory = std::function<std::shared_ptr<CaptureBackend>(const RecordingProfile&)>;

class ReplayEngine final : public QObject {
    Q_OBJECT
public:
    enum class State { Idle, Starting, Running, Stopping, Error };
    explicit ReplayEngine(QObject* parent = nullptr, CaptureBackendFactory factory = createCaptureBackend);
    ~ReplayEngine() override;
    // The capture serves the replay, a recording, or both. Started for a recording alone, it keeps no
    // replay and stops when the recording ends; the replay may be started meanwhile, and stopped again.
    enum class Purpose { Replay, Recording };
    bool start(const RecordingProfile& profile, const QString& directory, QString& error, Purpose purpose = Purpose::Replay);
    // Stops the replay; a recording under way goes on, the capture with it.
    void stopReplay();
    // Stops the capture, the replay and a recording alike.
    void stop();
    // The replay of a capture started for a recording.
    void startReplay();
    bool replaying() const { return m_keepReplay && (m_state == State::Starting || m_state == State::Running); }
    bool save(QString& error);
    // Where the next clips and recordings go, changed while the capture runs; a recording under way stays in its file.
    void setDirectory(const QString& directory);
    // A recording of everything until it is stopped, filed beside the clips of the game. Asked while
    // the capture starts, it waits for it.
    bool startRecording(QString& error);
    void recordWhenRunning();
    void stopRecording();
    bool recording() const { return m_recordPending || stats().recording; }
    // State of the capture, whatever it serves.
    State state() const { return m_state; }
    bool exporting() const { return m_export.isRunning(); }
    CaptureStats stats() const;
    QString clipGameName() const { return m_gameName.isEmpty() ? m_profileName : m_gameName; }
    static bool supported();
signals:
    void changed();
    void failed(const QString& error);
    void saved(const QString& path, double seconds);
    void recordingStarted(const QString& path);
    // The file is kept when "kept"; "error" says what stopped it before it was asked to.
    void recorded(const QString& path, double seconds, bool kept, const QString& error);
    void recordingFailed(const QString& error);
    // The replay could not keep its seconds (its buffer on disk could not be set up); the capture goes on.
    void replayFailed(const QString& error);
private:
    void setState(State state);
    // The folder of the game being captured, created if needed; empty with an error otherwise.
    QString gameDirectory(QString& folder, QString& error);
    // The recording that ended, said; without a replay, the capture stops with it unless it is ending already.
    void collectRecording(bool stopWithIt = true);
    bool m_recordPending = false, m_keepReplay = true;
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
