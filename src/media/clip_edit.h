#pragma once
#include <QList>
#include <QRect>
#include <QString>
namespace clipsapp {
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
};
struct TrackEdit { int stream = -1; QString name; double volume = 1; bool muted = false; };
struct ClipEdit {
    QRect crop;
    qint64 startMs = 0, endMs = 0;
    QList<TrackEdit> tracks;
    bool separateTracks = false;
};
QRect boundedCrop(QRect requested, QSize source);
ClipEdit defaultEdit(const ClipInfo& info);
QString validateEdit(const ClipInfo& info, const ClipEdit& edit);
int audioExportTrackCount(const ClipEdit& edit);
bool sameClipEdits(const ClipEdit& left, const ClipEdit& right);
}
