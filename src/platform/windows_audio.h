#pragma once
#include "core/config.h"
#include <memory>
#include <vector>

namespace clipsapp {
// Bounded PCM queue, positioned on the Windows QPC clock at 48 kHz.
class WindowsAudio {
public:
    explicit WindowsAudio(const AudioSource& source);
    ~WindowsAudio();
    WindowsAudio(const WindowsAudio&) = delete;
    void start(qint64 epoch100ns);
    void mix(qint64 firstSample, int count, float* left, float* right);
    QString error() const;
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
qint64 windowsClock100ns();
std::vector<unsigned long> applicationProcesses(const QString& executable);
QString windowsVideoOptions(const QString& targetId);
}
