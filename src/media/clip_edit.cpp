#include "clip_edit.h"
#include <QSet>
#include <algorithm>
#include <cmath>
namespace clipsapp {
QRect boundedCrop(QRect crop, QSize source) {
    const int width = source.width() & ~1, height = source.height() & ~1;
    if (width < 2 || height < 2) return {};
    const int x = std::clamp(crop.x(), 0, width - 2) & ~1, y = std::clamp(crop.y(), 0, height - 2) & ~1;
    return {x, y, std::clamp(crop.width(), 2, width - x) & ~1, std::clamp(crop.height(), 2, height - y) & ~1};
}
ClipEdit defaultEdit(const ClipInfo& info) {
    ClipEdit edit; edit.crop = boundedCrop({QPoint(), info.size}, info.size); edit.endMs = info.durationMs;
    for (const auto& track : info.audio) edit.tracks.append({track.stream, track.name, 1, false});
    return edit;
}
int audioExportTrackCount(const ClipEdit& edit) {
    return int(std::count_if(edit.tracks.cbegin(), edit.tracks.cend(), [](const auto& track) { return !track.muted; }));
}
bool sameClipEdits(const ClipEdit& left, const ClipEdit& right) {
    if (left.crop != right.crop || left.startMs != right.startMs || left.endMs != right.endMs || left.tracks.size() != right.tracks.size()) return false;
    for (int i = 0; i < left.tracks.size(); ++i) {
        const auto& a = left.tracks[i]; const auto& b = right.tracks[i];
        if (a.stream != b.stream || a.name != b.name || a.muted != b.muted || std::abs(a.volume - b.volume) > 0.000001) return false;
    }
    return audioExportTrackCount(left) <= 1 || left.separateTracks == right.separateTracks;
}
QString validateEdit(const ClipInfo& info, const ClipEdit& edit) {
    if (info.videoStream < 0 || info.durationMs <= 0 || info.size.isEmpty()) return QStringLiteral("Ce clip ne contient pas de vidéo exploitable.");
    if (edit.startMs < 0 || edit.endMs > info.durationMs || edit.endMs - edit.startMs < 50) return QStringLiteral("Choisissez au moins 50 ms entre le début et la fin du clip.");
    if (edit.crop != boundedCrop(edit.crop, info.size)) return QStringLiteral("Le recadrage doit rester dans l'image, avec des dimensions paires.");
    QSet<int> streams, seen; for (const auto& track : info.audio) streams.insert(track.stream);
    for (const auto& track : edit.tracks) {
        if (!streams.contains(track.stream) || seen.contains(track.stream) || !std::isfinite(track.volume) || track.volume < 0 || track.volume > 2)
            return QStringLiteral("Réglage de piste audio invalide.");
        seen.insert(track.stream);
    }
    return {};
}
}
