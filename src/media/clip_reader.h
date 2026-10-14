#pragma once
#include "clip_edit.h"
#include "gpu_video.h"
#include <QImage>
#include <atomic>
#include <memory>
namespace clipsapp {
// What the reader hands out as images. By default the whole frame, never larger than
// 1280 x 720: enough for an editing preview, and light. A presentation asks instead for
// a region of the frame fitted to a box, so that every pixel of the screen comes
// straight from the decoded picture and nothing is rescaled when it is drawn.
struct FrameOutput {
    QSize box{1280, 720};
    QRect crop;
    bool fit = false;
    bool operator==(const FrameOutput& other) const { return box == other.box && crop == other.crop && fit == other.fit; }
};
class ClipReader {
public:
    // presentable: the images stay on the graphics card, for a window that shows them from there.
    // Where the card cannot decode the clip, they come out of memory as for any other reader.
    explicit ClipReader(const QString& path, bool hardware = true, bool audioOnly = false, bool presentable = false);
    ~ClipReader();
    ClipInfo info() const;
    void seek(qint64 positionMs);
    bool setAudioTracks(const QList<TrackEdit>& tracks);
    // Returns true when the output changed; images already decoded keep their former size.
    bool setFrameOutput(const FrameOutput& output);
    QSize frameSize() const;
    // Video may stop short of the other tracks (videoEndMs): large images are costly to keep,
    // compressed packets are not. Frames that end before presentMs are decoded but not converted;
    // with the sound being read too the clip is playing, and presentMs moves on with the clock meanwhile.
    void readUntil(qint64 endMs, const std::atomic_bool& stop, bool videoOnly = false, qint64 videoEndMs = -1, qint64 presentMs = -1);
    QImage frameAt(qint64 positionMs);
    // The same for a presentable reader: the image due at that time, still on the card. Nothing
    // when this clip is decoded in memory, which direct() tells before the first image.
    GpuFrame gpuFrameAt(qint64 positionMs);
    bool direct() const;
    void mix(qint64 firstSample, int count, const QList<TrackEdit>& tracks, float* stereo);
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
}
