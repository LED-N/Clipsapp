#pragma once
#include "clip_edit.h"
#include "gpu_video.h"
#include <functional>
#include <QImage>
#include <QMutex>
#include <QObject>
#include <QTimer>
#include <QWaitCondition>
#include <algorithm>
#include <atomic>
#include <thread>
namespace clipsapp {
// Brings stereo samples from the level they were at to the one asked for, in a ramp across
// the block: a change of volume is heard without a click. Returns the level reached.
float applyGain(float* stereo, int frames, float from, float to);
struct PlayerSnapshot {
    ClipInfo info;
    QImage image;
    qint64 positionMs = 0;
    bool loaded = false, loading = false, playing = false;
    // The images go straight from the graphics card to a window: there is none in memory to draw.
    bool direct = false;
    QString error, warning;
};
class ClipPlayer final : public QObject {
    Q_OBJECT
public:
    explicit ClipPlayer(QObject* parent = nullptr, bool audioEnabled = true);
    ~ClipPlayer() override;
    // Presentation: images come out as the edited result (cropped) fitted to this box, pixel for
    // pixel, instead of the light 720p editing preview. Chosen before load(); the box may then change while the clip is shown.
    // The editor asks for the whole image, on which it draws the crop itself.
    void setPresentation(QSize box, bool whole = false);
    // Chosen before load(): each image that is due is handed over as the card decoded it, from the
    // thread of the player. A clip the card cannot decode still comes out as images in memory.
    void setFrameSink(std::function<void(const GpuFrame&)> sink);
    void load(const QString& path);
    void close();
    void setPlaying(bool playing);
    void seek(qint64 milliseconds);
    void setEdit(const ClipEdit& edit);
    // Listening level, 0 to 1, applied to the mix on its way to the speakers. The edit keeps its own volumes.
    void setVolume(double gain) { m_volume = float(std::clamp(gain, 0.0, 1.0)); }
    double volume() const { return m_volume; }
    PlayerSnapshot snapshot() const;
signals:
    void changed();
private:
    void run(QString path);
    mutable QMutex m_mutex;
    QWaitCondition m_wake;
    QTimer m_timer;
    std::thread m_thread;
    std::atomic_bool m_stop{false};
    std::atomic_bool m_decodeCancel{false};
    std::atomic<float> m_volume{1.f};
    PlayerSnapshot m_snapshot;
    ClipEdit m_edit;
    QSize m_presentation; bool m_whole = false;
    std::function<void(const GpuFrame&)> m_sink;
    qint64 m_requestedMs = 0;
    quint64 m_seekSerial = 1, m_revision = 0;
    bool m_playing = false, m_audioEnabled;
};
}
