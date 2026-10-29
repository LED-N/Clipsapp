#include "video_surface.h"
#include "theme.h"
#include <QCoreApplication>
#include <QEvent>
#include <QGuiApplication>
#include <QWindow>

namespace clipsapp {
namespace {
// A widget given a window of its own, without its parents getting one, is placed by Qt as if its
// parent were the whole window: the editor's buttons over the image landed in the replay bar. Its
// window is put where the layout wants it, and again whenever something above it moves or resizes.
class NativePlace final : public QObject {
public:
    static void keep(QWidget* widget) { if (widget->property("nativePlaceKept").toBool()) return; widget->setProperty("nativePlaceKept", true); new NativePlace(widget); }
protected:
    bool eventFilter(QObject*, QEvent* event) override {
        const auto type = event->type();
        if (type == QEvent::Move || type == QEvent::Resize || type == QEvent::Show || type == QEvent::ParentChange) later();
        return false;
    }
private:
    explicit NativePlace(QWidget* widget) : QObject(widget), m_widget(widget) {
        for (auto* level = widget; level && !level->isWindow(); level = level->parentWidget()) level->installEventFilter(this);
        place();
    }
    void later() { if (m_queued) return; m_queued = true; QMetaObject::invokeMethod(this, [this] { m_queued = false; place(); }, Qt::QueuedConnection); }
    void place() {
        auto* handle = m_widget->windowHandle(); auto* native = m_widget->nativeParentWidget(); if (!handle || !native) return;
        const QRect wanted(m_widget->mapTo(native, QPoint()), m_widget->size());
        if (handle->geometry() != wanted) handle->setGeometry(wanted);
    }
    QWidget* m_widget; bool m_queued = false;
};
}
VideoSurface::VideoSurface(QWidget* parent) : QWidget(parent) {
    // Only this widget and those raised above it get a window of their own: the rest of the
    // interface keeps being drawn as one picture.
    QCoreApplication::setAttribute(Qt::AA_DontCreateNativeWidgetSiblings);
    // In this order: once the widget has its window, its parents would get one too.
    setAttribute(Qt::WA_DontCreateNativeAncestors); setAttribute(Qt::WA_NativeWindow); NativePlace::keep(this);
    // Qt draws nothing here and copies nothing over it: the card does.
    setAttribute(Qt::WA_PaintOnScreen); setAttribute(Qt::WA_NoSystemBackground); setAttribute(Qt::WA_OpaquePaintEvent);
    // Clicks and moves are not handled: they go on to the view the surface lies in.
    setMouseTracking(true); setFocusPolicy(Qt::NoFocus);
    if (auto* top = window()) top->setProperty("directVideo", true);
    if (auto video = GpuVideo::acquire()) m_output = std::make_unique<GpuOutput>(std::move(video), reinterpret_cast<void*>(winId()));
    if (m_output) m_output->resize(deviceSize());
}
VideoSurface::~VideoSurface() = default;
namespace {
bool abandoned = false;
}
void VideoSurface::giveUp() { abandoned = true; }
bool VideoSurface::lost() const { return !m_output || !m_output->valid(); }
bool VideoSurface::usable(const QWidget* host) {
    if (!host || QGuiApplication::platformName() != QLatin1String("windows")) return false;
    const auto* top = host->window();
    return !abandoned && top->isVisible() && !top->testAttribute(Qt::WA_DontShowOnScreen) && GpuVideo::available();
}
void VideoSurface::raiseAbove(QWidget* widget) {
    if (!widget || !widget->window()->property("directVideo").toBool()) return;
    // A widget already on screen gets its window at once: its parents must be kept out of it first.
    widget->setAttribute(Qt::WA_DontCreateNativeAncestors); widget->setAttribute(Qt::WA_NativeWindow); NativePlace::keep(widget); widget->raise();
}
QSize VideoSurface::deviceSize() const { const auto ratio = devicePixelRatioF(); return QSize(qRound(width() * ratio), qRound(height() * ratio)).expandedTo({1, 1}); }
void VideoSurface::present(const GpuFrame& frame) { if (m_output) m_output->show(frame); }
void VideoSurface::setView(const QRect& source, const QRect& place, const QColor& ground) { if (m_output) m_output->setView(source, place, ground); }
void VideoSurface::setOverlay(const QImage& overlay) { if (m_output) m_output->setOverlay(overlay); }
// The same veil as the one over the rest of the window, in the colors of the theme.
void VideoSurface::setDimmed(bool dimmed) { auto veil = tk::mix(tk::window, QColor(Qt::black), 0.45); veil.setAlpha(184); if (m_output) m_output->setDim(dimmed ? veil : QColor(0, 0, 0, 0)); }
void VideoSurface::clear() { if (m_output) m_output->show({}); }
quint64 VideoSurface::presented() const { return m_output ? m_output->presented() : 0; }
QImage VideoSurface::grabFrame() { return m_output ? m_output->grab() : QImage(); }
void VideoSurface::paintEvent(QPaintEvent*) { if (m_output) m_output->refresh(); }
void VideoSurface::resizeEvent(QResizeEvent*) { if (m_output) m_output->resize(deviceSize()); }
bool VideoSurface::event(QEvent* event) {
    // Moved to a screen with another scale: the same place holds another number of pixels.
    if (event->type() == QEvent::DevicePixelRatioChange && m_output) m_output->resize(deviceSize());
    return QWidget::event(event);
}
}
