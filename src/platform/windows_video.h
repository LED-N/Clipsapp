#pragma once
#include "core/config.h"
#include <QImage>
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
    // The screen captured is gone for now: the images are black until it is back.
    bool lost() const;
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
// One image of a screen or a window, pixel for pixel as it is on screen: nothing is scaled or
// converted, unlike the replay. Taken beside a replay that runs, without disturbing it.
QImage captureScreenImage(const QString& targetId, bool cursor, QString& error);
}
