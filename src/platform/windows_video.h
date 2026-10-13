#pragma once
#include "core/config.h"
#include <memory>
#include <atomic>
struct AVFrame;
namespace clipsapp {
class WindowsVideo {
public:
    WindowsVideo(const VideoSettings& settings, const std::atomic_bool& stop);
    ~WindowsVideo();
    // Polls the frame pool without waiting. Duplicates the last image when idle.
    bool read(AVFrame* frame);
    QString warning() const;
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
}
