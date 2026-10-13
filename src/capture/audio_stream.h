#pragma once
#include <QtGlobal>
#include <cstdint>
#include <vector>
struct SwrContext;

namespace clipsapp {
enum class AudioSampleFormat { Float32, Float64, Unsigned8, Signed16, Signed24, Signed32 };
struct AudioInputFormat {
    int sampleRate = 48000;
    int channels = 2;
    AudioSampleFormat samples = AudioSampleFormat::Float32;
    quint64 channelMask = 0;
};
struct AudioChunk { qint64 start = 0; std::vector<float> samples; };

// One instance per capture stream. Output is continuous, interleaved stereo at
// 48 kHz; QPC timestamps align streams but never split adjacent audio packets.
class AudioStream {
public:
    explicit AudioStream(const AudioInputFormat& format);
    ~AudioStream();
    AudioStream(const AudioStream&) = delete;
    AudioStream& operator=(const AudioStream&) = delete;
    AudioChunk convert(const uint8_t* data, int frames, qint64 timestampSample,
                       bool timestampValid = true, bool discontinuity = false);
private:
    AudioInputFormat m_format;
    SwrContext* m_context = nullptr;
    std::vector<uint8_t> m_silence;
    std::vector<int32_t> m_unpacked;
    qint64 m_next = 0;
    int m_sinceCorrection = 0;
    double m_clockError = 0;
    bool m_started = false;
};
}
