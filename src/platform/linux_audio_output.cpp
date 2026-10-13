#include "media/audio_output.h"
#include <QElapsedTimer>
#include <QProcess>
#include <algorithm>
#include <stdexcept>
namespace clipsapp {
struct AudioOutput::Impl {
    QProcess process;
    QElapsedTimer clock;
    qint64 submitted = 0;
    ~Impl() { process.kill(); process.waitForFinished(1000); }
};
AudioOutput::AudioOutput() : m_impl(std::make_unique<Impl>()) {
    m_impl->process.start("pw-cat", {"--playback", "--rate=48000", "--channels=2", "--format=f32", "--latency=50ms", "-"});
    if (!m_impl->process.waitForStarted(2000)) throw std::runtime_error("PipeWire : pw-cat est indisponible.");
}
AudioOutput::~AudioOutput() = default;
int AudioOutput::writableFrames() const {
    if (m_impl->process.state() != QProcess::Running) throw std::runtime_error("La sortie PipeWire s'est arrêtée.");
    m_impl->process.waitForBytesWritten(0);
    return int(std::clamp<qint64>(2400 - (m_impl->submitted - playedFrames()), 0, 2400));
}
qint64 AudioOutput::playedFrames() const {
    if (!m_impl->clock.isValid()) return 0;
    return std::min(m_impl->submitted, std::max<qint64>(0, m_impl->clock.elapsed() - 50) * 48);
}
void AudioOutput::write(const float* samples, int frames) {
    const qint64 bytes = qint64(frames) * 2 * sizeof(float);
    if (m_impl->process.write(reinterpret_cast<const char*>(samples), bytes) != bytes) throw std::runtime_error("Écriture audio PipeWire impossible.");
    if (!m_impl->clock.isValid()) m_impl->clock.start();
    m_impl->submitted += frames;
}
}
