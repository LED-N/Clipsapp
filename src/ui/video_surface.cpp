#include "video_surface.h"
#include <QCoreApplication>
#include <QEvent>
#include <QGuiApplication>

namespace clipsapp {
VideoSurface::VideoSurface(QWidget* parent) : QWidget(parent) {
    // Only this widget and those raised above it get a window of their own: the rest of the
    // interface keeps being drawn as one picture.
    QCoreApplication::setAttribute(Qt::AA_DontCreateNativeWidgetSiblings);
    setAttribute(Qt::WA_NativeWindow); setAttribute(Qt::WA_DontCreateNativeAncestors);
    // Qt draws nothing here and copies nothing over it: the card does.
    setAttribute(Qt::WA_PaintOnScreen); setAttribute(Qt::WA_NoSystemBackground); setAttribute(Qt::WA_OpaquePaintEvent);
    // Clicks and moves are not handled: they go on to the view the surface lies in.
    setMouseTracking(true); setFocusPolicy(Qt::NoFocus);
    if (auto* top = window()) top->setProperty("directVideo", true);
    if (auto* video = GpuVideo::shared()) m_output = std::make_unique<GpuOutput>(video, reinterpret_cast<void*>(winId()));
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
    return !abandoned && top->isVisible() && !top->testAttribute(Qt::WA_DontShowOnScreen) && GpuVideo::shared();
}
void VideoSurface::raiseAbove(QWidget* widget) {
    if (!widget || !widget->window()->property("directVideo").toBool()) return;
    widget->setAttribute(Qt::WA_NativeWindow); widget->setAttribute(Qt::WA_DontCreateNativeAncestors); widget->raise();
}
QSize VideoSurface::deviceSize() const { const auto ratio = devicePixelRatioF(); return QSize(qRound(width() * ratio), qRound(height() * ratio)).expandedTo({1, 1}); }
void VideoSurface::present(const GpuFrame& frame) { if (m_output) m_output->show(frame); }
void VideoSurface::setView(const QRect& source, const QRect& place, const QColor& ground) { if (m_output) m_output->setView(source, place, ground); }
void VideoSurface::setOverlay(const QImage& overlay) { if (m_output) m_output->setOverlay(overlay); }
void VideoSurface::setDimmed(bool dimmed) { if (m_output) m_output->setDim(dimmed ? QColor(6, 8, 10, 184) : QColor(0, 0, 0, 0)); }
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
