#pragma once
#include <QtGlobal>
#include <memory>
namespace clipsapp {
// Called only by the playback worker; buffers contain stereo float at 48 kHz.
class AudioOutput {
public:
    AudioOutput();
    ~AudioOutput();
    int writableFrames() const;
    qint64 playedFrames() const;
    void write(const float* samples, int frames);
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
}
