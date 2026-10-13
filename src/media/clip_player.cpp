#include "clip_player.h"
#include "clip_reader.h"
#include "audio_output.h"
#include <QMutexLocker>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>
#include <vector>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
namespace clipsapp {
namespace {
// Windows wakes a sleeping thread every 15.6 ms by default: too coarse to hand out an image
// every 16.7 ms. A presentation waits on a high-resolution timer instead, which holds its
// precision whatever the rest of the system asks for, and costs nothing once released.
class FineWait {
public:
    explicit FineWait(bool wanted) {
#ifdef Q_OS_WIN
        // CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, Windows 10 1803 and later.
        if (wanted) m_timer = CreateWaitableTimerExW(nullptr, nullptr, 0x00000002, TIMER_ALL_ACCESS);
#else
        Q_UNUSED(wanted);
#endif
    }
    ~FineWait() {
#ifdef Q_OS_WIN
        if (m_timer) CloseHandle(m_timer);
#endif
    }
    void wait(int milliseconds) const {
#ifdef Q_OS_WIN
        LARGE_INTEGER due; due.QuadPart = -10000LL * milliseconds;
        if (m_timer && SetWaitableTimer(m_timer, &due, 0, nullptr, nullptr, FALSE)) { WaitForSingleObject(m_timer, 100); return; }
#endif
        std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
    }
private:
#ifdef Q_OS_WIN
    HANDLE m_timer = nullptr;
#endif
};
}
float applyGain(float* stereo, int frames, float from, float to) {
    if (frames <= 0 || (from == 1.f && to == 1.f)) return to;
    const float step = (to - from) / float(frames); float gain = from;
    for (int frame = 0; frame < frames; ++frame) { gain += step; stereo[2 * frame] *= gain; stereo[2 * frame + 1] *= gain; }
    return to;
}
ClipPlayer::ClipPlayer(QObject* parent, bool audioEnabled) : QObject(parent), m_audioEnabled(audioEnabled) {
    m_timer.setInterval(16);
    connect(&m_timer, &QTimer::timeout, this, [this] {
        emit changed(); const auto state = snapshot();
        if (!state.loading && !state.playing) m_timer.stop();
    });
}
ClipPlayer::~ClipPlayer() { close(); }
PlayerSnapshot ClipPlayer::snapshot() const { QMutexLocker lock(&m_mutex); return m_snapshot; }
void ClipPlayer::setPresentation(QSize box) {
    { QMutexLocker lock(&m_mutex); if (m_presentation == box) return; const bool live = !m_presentation.isEmpty() && !box.isEmpty() && m_snapshot.loaded; m_presentation = box; if (!live) return;
      // Another screen, another scale: the clip being shown is redrawn at the new size, where it is.
      ++m_revision; m_snapshot.loading = true; }
    m_wake.wakeAll(); m_timer.start();
}
void ClipPlayer::close() {
    { QMutexLocker lock(&m_mutex); m_stop = true; m_decodeCancel = true; m_wake.wakeAll(); }
    if (m_thread.joinable()) m_thread.join(); m_timer.stop();
    { QMutexLocker lock(&m_mutex); m_snapshot = {}; m_edit = {}; m_playing = false; }
    emit changed();
}
void ClipPlayer::load(const QString& path) {
    close(); bool presenting;
    { QMutexLocker lock(&m_mutex); m_stop = false; m_decodeCancel = false; m_snapshot.loading = true; m_requestedMs = 0; m_seekSerial = 1; m_revision = 0; presenting = !m_presentation.isEmpty(); }
    // A presentation looks for its next image four times per display refresh; an editing preview once.
    m_timer.setTimerType(presenting ? Qt::PreciseTimer : Qt::CoarseTimer); m_timer.setInterval(presenting ? 4 : 16);
    m_thread = std::thread([this, path] { run(path); }); m_timer.start(); emit changed();
}
void ClipPlayer::setPlaying(bool playing) {
    { QMutexLocker lock(&m_mutex);
      if (!m_snapshot.loaded) return;
      if (playing && m_snapshot.positionMs >= m_edit.endMs - 1) { m_requestedMs = m_edit.startMs; ++m_seekSerial; m_snapshot.loading = true; }
      m_playing = playing; m_snapshot.loading = true; ++m_revision; }
    m_wake.wakeAll(); m_timer.start();
}
void ClipPlayer::seek(qint64 milliseconds) {
    { QMutexLocker lock(&m_mutex); if (!m_snapshot.loaded) return;
      m_requestedMs = std::clamp(milliseconds, m_edit.startMs, std::max(m_edit.startMs, m_edit.endMs - 1));
      m_snapshot.positionMs = m_requestedMs;
      ++m_seekSerial; ++m_revision; m_snapshot.loading = true; }
    m_decodeCancel = true;
    m_wake.wakeAll(); m_timer.start();
}
void ClipPlayer::setEdit(const ClipEdit& edit) {
    { QMutexLocker lock(&m_mutex); m_edit = edit; ++m_revision;
      if (m_snapshot.loaded && (m_snapshot.positionMs < edit.startMs || m_snapshot.positionMs >= edit.endMs)) {
          m_requestedMs = edit.startMs; ++m_seekSerial; m_snapshot.loading = true;
      } }
    m_wake.wakeAll(); m_timer.start();
}
void ClipPlayer::run(QString path) {
    try {
        ClipReader reader(path); const auto info = reader.info(); QSize presentation;
        { QMutexLocker lock(&m_mutex); m_snapshot.info = info; m_edit = defaultEdit(info); presentation = m_presentation; }
        const FineWait pause(!presentation.isEmpty());
        std::unique_ptr<AudioOutput> output;
        bool playing = false, prepared = false, anchored = false;
        quint64 serial = 0;
        qint64 position = 0, base = 0, audioCursor = 0;
        double offset = 0; float gain = m_volume;
        auto wall = std::chrono::steady_clock::now();
        std::vector<float> mixed(480 * 2);
        // The audio device reports what it has played in steps of about 10 ms. Choosing images
        // with that clock makes 60 FPS stutter, so the position follows the steady clock, held
        // on the audio by a slowly corrected offset. Sound and picture stay together, images flow.
        const auto clock = [&] {
            const qint64 elapsed = base + std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - wall).count();
            if (!output) return elapsed;
            const qint64 played = output->playedFrames(); if (played <= 0) return position;
            const double measured = double(base + played / 48 - elapsed);
            if (!anchored) { offset = measured; anchored = true; } else offset += (measured - offset) * 0.05;
            return std::max(position, elapsed + qRound64(offset));
        };
        while (!m_stop) {
            ClipEdit edit; bool requestedPlaying; quint64 requestedSerial, revision; qint64 requested;
            { QMutexLocker lock(&m_mutex); edit = m_edit; requestedPlaying = m_playing; requestedSerial = m_seekSerial; requested = m_requestedMs; revision = m_revision; if (!presentation.isEmpty() && !m_presentation.isEmpty()) presentation = m_presentation; }
            if (prepared && playing) position = clock();
            const bool audioSelectionChanged = reader.setAudioTracks(edit.tracks);
            FrameOutput frames;
            if (!presentation.isEmpty()) { frames.box = presentation; frames.fit = true; if (edit.crop.isValid() && edit.crop != QRect(QPoint(), info.size)) frames.crop = edit.crop; }
            const bool outputChanged = reader.setFrameOutput(frames);
            if (requestedSerial != serial || requestedPlaying != playing || audioSelectionChanged || outputChanged) {
                if (requestedSerial != serial) position = requested;
                position = std::clamp(position, edit.startMs, std::max(edit.startMs, edit.endMs - 1));
                output.reset(); reader.seek(position); prepared = false; anchored = false; serial = requestedSerial; playing = requestedPlaying;
                base = position; audioCursor = position * 48;
            }
            { QMutexLocker lock(&m_mutex); if (m_seekSerial != serial || m_stop) continue; m_decodeCancel = false; }
            // The image that is due goes out first: converting the next ones must not hold it back.
            if (prepared && playing) { const auto due = reader.frameAt(position); QMutexLocker lock(&m_mutex); if (m_seekSerial == serial) { m_snapshot.image = due; m_snapshot.positionMs = position; } }
            // Paused scrubbing needs one accurate image, without priming all
            // audio tracks or decoding another 150 ms of video. While playing, images are
            // decoded only as far ahead as about 96 Mio of them allow: nine at 720p, three in 4K.
            const auto size = reader.frameSize(); const qint64 frameBytes = std::max<qint64>(1, qint64(size.width()) * size.height() * 4);
            const qint64 ahead = std::min<qint64>(150, qint64(std::ceil(std::clamp<qint64>(96LL * 1024 * 1024 / frameBytes, 3, 9) * 1000.0 / std::max(1.0, info.fps))));
            reader.readUntil(std::min(info.durationMs, position + (playing ? 150 : 1)), m_decodeCancel, !playing, playing ? std::min(info.durationMs, position + ahead) : -1, position);
            if (m_stop) break;
            if (m_decodeCancel) continue;
            if (!prepared) {
                if (playing && m_audioEnabled && !info.audio.isEmpty()) {
                    try { output = std::make_unique<AudioOutput>(); }
                    catch (const std::exception& error) { QMutexLocker lock(&m_mutex); m_snapshot.warning = QString::fromUtf8(error.what()) + QStringLiteral(" · aperçu sans son"); }
                }
                wall = std::chrono::steady_clock::now(); prepared = true;
            }
            if (playing && output) {
                for (int budget = 0; budget < 5 && output->writableFrames() >= 480 && audioCursor < edit.endMs * 48; ++budget) {
                    const int count = int(std::min<qint64>(480, edit.endMs * 48 - audioCursor));
                    reader.mix(audioCursor, count, edit.tracks, mixed.data()); gain = applyGain(mixed.data(), count, gain, m_volume);
                    output->write(mixed.data(), count); audioCursor += count;
                }
            }
            if (playing) position = clock();
            if (position >= edit.endMs) {
                position = edit.endMs; playing = false; output.reset();
                QMutexLocker lock(&m_mutex);
                if (m_seekSerial == serial && m_playing == requestedPlaying) m_playing = false;
            }
            const auto image = reader.frameAt(position);
            { QMutexLocker lock(&m_mutex); if (m_seekSerial != serial) continue; m_snapshot.image = image; m_snapshot.positionMs = position;
              m_snapshot.loaded = true; m_snapshot.loading = m_seekSerial != serial || m_playing != playing; m_snapshot.playing = playing; }
            if (!playing) {
                QMutexLocker lock(&m_mutex); if (!m_stop && m_revision == revision) m_wake.wait(&m_mutex);
            } else pause.wait(presentation.isEmpty() ? 5 : 1);
        }
    } catch (const std::exception& error) {
        if (!m_stop) { QMutexLocker lock(&m_mutex); m_snapshot.error = QString::fromUtf8(error.what()); m_snapshot.loaded = false; m_snapshot.loading = false; m_snapshot.playing = false; m_playing = false; }
    }
}
}
