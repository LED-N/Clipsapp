#pragma once
#include "core/track_mix.h"
#include <QList>
#include <QRect>
#include <QString>
namespace clipsapp {
// What an export writes in the comment of its file, and what tells the reader the sound was mixed.
inline constexpr char mixedMark[] = "Mixé par Clipsapp";
// What a clip compressed to save room says of itself: it is not offered the same compression twice.
inline constexpr char compressedMark[] = "Compressé par Clipsapp";
struct ClipAudioInfo { int stream = -1; QString name; QString codec; };
struct ClipInfo {
    QString path, decoder;
    QSize size;
    qint64 durationMs = 0;
    double fps = 30;
    int videoStream = -1;
    QList<ClipAudioInfo> audio;
    qint64 videoBitrate = 0;
    QString videoCodec;
    // Written by an export of the application: its sound already carries the levels that were chosen.
    bool mixed = false;
    // Written by the compressor of the application, to save room.
    bool compressed = false;
};
struct TrackEdit { int stream = -1; QString name; double volume = 1; bool muted = false; };
struct ClipEdit {
    QRect crop;
    qint64 startMs = 0, endMs = 0;
    QList<TrackEdit> tracks;
    bool separateTracks = false;
};
QRect boundedCrop(QRect requested, QSize source);
// The clip as it is, each track at its usual level. A clip that was already mixed is left as it sounds.
ClipEdit defaultEdit(const ClipInfo& info, const TrackMix& mix = {});
// The levels an edit gives to the tracks it keeps audible, for the mix to learn from.
TrackMix editLevels(const ClipEdit& edit);
QString validateEdit(const ClipInfo& info, const ClipEdit& edit);
int audioExportTrackCount(const ClipEdit& edit);
bool sameClipEdits(const ClipEdit& left, const ClipEdit& right);
}
