#pragma once
#include "media/gpu_video.h"
#include <QWidget>
#include <memory>
namespace clipsapp {
// The place of a window where the graphics card shows a clip itself. It is a window of its own,
// which Windows composes: the interface cannot draw over it, so what belongs above the image
// (a crop frame, a veil) is handed to it as a picture, and the widgets that must stay above
// it are given a window of their own with raiseAbove().
class VideoSurface final : public QWidget {
    Q_OBJECT
public:
    explicit VideoSurface(QWidget* parent);
    ~VideoSurface() override;
    // A window really on screen, on a machine whose card can decode and show a clip this way.
    static bool usable(const QWidget* host);
    // Whether the card gave it what it needs: a surface without it shows nothing and is let go.
    bool ready() const { return bool(m_output); }
    // Whether the card still shows what it is given. A driver that was replaced or that failed takes
    // the surface with it: the clip is then opened again and drawn from memory, here and from then on.
    bool lost() const;
    static void giveUp();
    // Keeps an ordinary widget visible above the surfaces of its window.
    static void raiseAbove(QWidget* widget);
    // From the thread of the player: the image that is due.
    void present(const GpuFrame& frame);
    // The part of the image that is shown, in its own pixels, and its place in pixels of the screen.
    void setView(const QRect& source, const QRect& place, const QColor& ground);
    // Everything the interface draws over the image, the size of the surface in pixels of the screen.
    void setOverlay(const QImage& overlay);
    // Under a dialog the rest of the window is darkened: so is the image.
    void setDimmed(bool dimmed);
    void clear();
    // For measurements: the images handed to the screen, and what the surface holds.
    quint64 presented() const;
    QImage grabFrame();
    QPaintEngine* paintEngine() const override { return nullptr; }
protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    bool event(QEvent* event) override;
private:
    QSize deviceSize() const;
    std::unique_ptr<GpuOutput> m_output;
};
}
