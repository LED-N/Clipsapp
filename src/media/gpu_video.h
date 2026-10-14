#pragma once
#include <QColor>
#include <QImage>
#include <QRect>
#include <memory>
struct AVBufferRef;
struct AVFrame;
namespace clipsapp {
// An image as the graphics card decoded it: it never leaves the card.
using GpuFrame = std::shared_ptr<AVFrame>;

// Decoding a clip with the graphics card and then copying each image back to memory to draw it
// makes the picture wait behind whatever else keeps the card busy, a game first of all. Here the
// decoder and the window share one device: the image is scaled, cropped and converted by the
// card, and goes to the screen without that journey.
class GpuVideo {
public:
    // The device of the application, or nothing where it cannot be had: the callers then keep
    // drawing images from memory, as before.
    static GpuVideo* shared();
    // What a decoder is given so that its images can be shown here. Borrowed: take a reference.
    AVBufferRef* decodingDevice() const;
    struct Impl;
    Impl* impl() const { return m_impl.get(); }
    ~GpuVideo();
private:
    GpuVideo();
    std::unique_ptr<Impl> m_impl;
};

// What one window shows: the last image it was given, in the place it was told, and above it an
// optional picture drawn by the interface. Every method may be called from any thread.
class GpuOutput {
public:
    // window: the native handle (HWND) the picture is presented in.
    GpuOutput(GpuVideo* video, void* window);
    ~GpuOutput();
    bool valid() const;
    // In pixels of the screen.
    void resize(QSize size);
    // The part of the image that is shown, in its own pixels, and where it goes in the window.
    void setView(const QRect& source, const QRect& place, const QColor& ground);
    // Premultiplied, the size of the window: drawn over the image. A null image removes it.
    void setOverlay(const QImage& overlay);
    // Darkens everything, as the interface does around a dialog: 0 leaves the picture alone.
    void setDim(const QColor& veil);
    // Shows this image from now on. An empty one leaves the ground alone.
    void show(const GpuFrame& frame);
    // Draws again what is shown, after the window was uncovered or changed.
    void refresh();
    // How many images were handed to the screen, and what the window holds: for measurements.
    quint64 presented() const;
    QImage grab();
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
}
