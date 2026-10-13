#include "clip_analysis.h"
#include "clip_reader.h"
#include <algorithm>
#include <cmath>
namespace clipsapp {
ClipAnalysis analyzeClip(const QString& path, const std::atomic_bool& stop, std::optional<QList<int>> audioStreams) {
    ClipAnalysis result;
    try {
        ClipInfo info;
        {
            // Waveforms never decode video. Each track keeps at most 1,400
            // peaks; PCM is consumed in 100 ms blocks and then discarded.
            ClipReader audio(path, false, true); info = audio.info();
            if (audioStreams) {
                info.audio.erase(std::remove_if(info.audio.begin(), info.audio.end(), [&](const auto& track) { return !audioStreams->contains(track.stream); }), info.audio.end());
                audio.setAudioTracks(defaultEdit(info).tracks);
            }
            const int bins = int(std::min<qint64>(1400, std::max<qint64>(1, info.durationMs / 20)));
            for (const auto& track : info.audio) { result.peaks.append(QVector<float>(bins, 0)); result.streams.append(track.stream); }
            std::vector<float> samples(4800 * 2);
            for (qint64 ms = 0; !info.audio.isEmpty() && ms < info.durationMs && !stop; ms += 100) {
                const int count = int(std::min<qint64>(100, info.durationMs - ms) * 48);
                audio.readUntil(std::min(info.durationMs, ms + 200), stop);
                for (int track = 0; track < info.audio.size() && !stop; ++track) {
                    audio.mix(ms * 48, count, {{info.audio[track].stream, {}, 1, false}}, samples.data());
                    for (int frame = 0; frame < count; ++frame) {
                        const int bin = int(std::min<qint64>(bins - 1, (ms * 48 + frame) * bins / (info.durationMs * 48)));
                        result.peaks[track][bin] = std::max(result.peaks[track][bin], std::max(std::abs(samples[size_t(frame) * 2]), std::abs(samples[size_t(frame) * 2 + 1])));
                    }
                }
            }
        }
        if (!stop) {
            ClipReader video(path); const int count = int(std::min<qint64>(10, std::max<qint64>(1, info.durationMs / 250)));
            for (int image = 0; image < count && !stop; ++image) {
                const qint64 position = image * info.durationMs / count; video.seek(position); video.readUntil(std::min(info.durationMs, position + 100), stop, true);
                const auto frame = video.frameAt(position); if (!frame.isNull()) result.thumbnails.append(frame.scaled({144, 82}, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            }
        }
    } catch (const std::exception& error) { if (!stop) result.error = QString::fromUtf8(error.what()); }
    return result;
}
}
