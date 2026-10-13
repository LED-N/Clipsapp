#include "audio_stream.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
extern "C" {
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
}
namespace clipsapp {
namespace {
void check(int result) {
    if (result < 0) {
        char message[AV_ERROR_MAX_STRING_SIZE]; av_strerror(result, message, sizeof(message));
        throw std::runtime_error(std::string("Conversion audio : ") + message);
    }
}
AVSampleFormat sampleFormat(AudioSampleFormat format) {
    switch (format) {
    case AudioSampleFormat::Float32: return AV_SAMPLE_FMT_FLT;
    case AudioSampleFormat::Float64: return AV_SAMPLE_FMT_DBL;
    case AudioSampleFormat::Unsigned8: return AV_SAMPLE_FMT_U8;
    case AudioSampleFormat::Signed16: return AV_SAMPLE_FMT_S16;
    case AudioSampleFormat::Signed24: case AudioSampleFormat::Signed32: return AV_SAMPLE_FMT_S32;
    }
    throw std::runtime_error("Format audio non pris en charge.");
}
}
AudioStream::AudioStream(const AudioInputFormat& format) : m_format(format) {
    if (format.sampleRate < 8000 || format.sampleRate > 384000 || format.channels < 1 || format.channels > 32)
        throw std::runtime_error("Fréquence ou nombre de canaux audio non pris en charge.");
    AVChannelLayout input{}, output = AV_CHANNEL_LAYOUT_STEREO;
    if (format.channelMask) {
        check(av_channel_layout_from_mask(&input, format.channelMask));
        if (input.nb_channels != format.channels) { av_channel_layout_uninit(&input); throw std::runtime_error("Canaux audio incohérents."); }
    } else av_channel_layout_default(&input, format.channels);
    const int result = swr_alloc_set_opts2(&m_context, &output, AV_SAMPLE_FMT_FLT, 48000,
                                         &input, sampleFormat(format.samples), format.sampleRate, 0, nullptr);
    av_channel_layout_uninit(&input);
    try {
        check(result);
        // Enable the filter from the beginning, including 48 -> 48 kHz, so clock
        // drift compensation does not reinitialize it in the middle of speech.
        check(av_opt_set_int(m_context, "flags", SWR_FLAG_RESAMPLE, 0));
        if (format.channels == 1) {
            const double matrix[]{1.0, 1.0}; check(swr_set_matrix(m_context, matrix, 1));
        }
        check(swr_init(m_context));
    } catch (...) { swr_free(&m_context); throw; }
}
AudioStream::~AudioStream() { swr_free(&m_context); }
AudioChunk AudioStream::convert(const uint8_t* data, int frames, qint64 timestamp, bool valid, bool discontinuity) {
    if (frames <= 0) return {m_next, {}};
    const auto delay = swr_get_delay(m_context, 48000);
    if (!m_started) { m_next = timestamp; m_started = true; }
    else if (discontinuity || (valid && std::abs(timestamp - m_next - delay) > 4800)) {
        // A genuine lost packet retains its gap. Rounding/jitter of otherwise
        // contiguous packets must not introduce silence or double their samples.
        swr_close(m_context); check(swr_init(m_context));
        m_next = std::max(m_next, valid ? timestamp : m_next + delay);
        m_clockError = 0; m_sinceCorrection = 0;
    }
    if (valid) m_clockError = 0.95 * m_clockError + 0.05 * double(timestamp - m_next - swr_get_delay(m_context, 48000));
    m_sinceCorrection += frames;
    if (m_sinceCorrection >= m_format.sampleRate) {
        // Spread a small clock correction over a second instead of inserting or
        // dropping samples at a packet boundary (audible clicks).
        const int correction = std::clamp(int(std::lround(m_clockError)), -48, 48);
        check(swr_set_compensation(m_context, correction, 48000));
        m_sinceCorrection %= m_format.sampleRate;
    }
    const size_t count = size_t(frames) * size_t(m_format.channels);
    if (!data) {
        const int bytes = m_format.samples == AudioSampleFormat::Signed24 ? 3 : av_get_bytes_per_sample(sampleFormat(m_format.samples));
        m_silence.assign(count * size_t(bytes), m_format.samples == AudioSampleFormat::Unsigned8 ? 128 : 0);
        data = m_silence.data();
    }
    if (m_format.samples == AudioSampleFormat::Signed24) {
        m_unpacked.resize(count);
        for (size_t i = 0; i < count; ++i)
            m_unpacked[i] = int32_t(uint32_t(data[3 * i]) << 8 | uint32_t(data[3 * i + 1]) << 16 | uint32_t(data[3 * i + 2]) << 24);
        data = reinterpret_cast<const uint8_t*>(m_unpacked.data());
    }
    const int capacity = swr_get_out_samples(m_context, frames); check(capacity);
    AudioChunk output{m_next, std::vector<float>(size_t(capacity) * 2)};
    uint8_t* destination = reinterpret_cast<uint8_t*>(output.samples.data());
    const int converted = swr_convert(m_context, &destination, capacity, &data, frames); check(converted);
    output.samples.resize(size_t(converted) * 2); m_next += converted;
    for (auto& sample : output.samples) if (!std::isfinite(sample)) sample = 0;
    return output;
}
}
