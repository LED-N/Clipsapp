#include "video_canvas.h"
#include "video_surface.h"
#include "widgets.h"
#include <QEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>
namespace clipsapp {
namespace {
const QColor ground(0x07, 0x08, 0x0A);
}
VideoCanvas::VideoCanvas(QWidget* parent) : QWidget(parent) { setMinimumSize(280, 180); setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding); setMouseTracking(true); }
void VideoCanvas::setFrame(QImage image, QSize original) { if (m_image.cacheKey() == image.cacheKey() && m_original == original) return; m_image = std::move(image); m_original = original; refresh(); announce(); }
void VideoCanvas::setCrop(QRect crop) { m_crop = crop; refresh(); announce(); }
void VideoCanvas::setResultPreview(bool enabled) { m_preview = enabled; refresh(); announce(); }
void VideoCanvas::setCropTool(bool enabled) { m_tool = enabled; m_drag = Drag::None; point(enabled ? Qt::CrossCursor : Qt::ArrowCursor); refresh(); announce(); }
void VideoCanvas::setOverlay(QWidget* overlay) { m_overlay = overlay; overlay->setParent(this); overlay->adjustSize(); overlay->move(width() - overlay->width() - 12, 12); overlay->show(); }
void VideoCanvas::resizeEvent(QResizeEvent*) { if (m_overlay) { m_overlay->adjustSize(); m_overlay->move(width() - m_overlay->width() - 12, 12); } announce(); refresh(); }
QSize VideoCanvas::wantedSize() const {
    const auto display = displayRect(); if (display.isEmpty() || m_original.isEmpty()) return {};
    // What fills the view: the crop when its result is shown, the whole frame otherwise.
    const double across = m_preview && !m_tool && m_crop.isValid() ? m_crop.width() : m_original.width();
    const double scale = std::min(1.0, display.width() * devicePixelRatioF() / across);
    return QSize(qRound(m_original.width() * scale), qRound(m_original.height() * scale)).expandedTo({2, 2});
}
void VideoCanvas::announce() { if (m_direct) placeSurface(); const auto wanted = wantedSize(); if (wanted == m_wanted) return; m_wanted = wanted; emit sizeWanted(wanted); }
QRectF VideoCanvas::displayRect() const {
    const QSizeF source = m_preview && !m_tool && m_crop.isValid() ? QSizeF(m_crop.size()) : QSizeF(m_original);
    if (source.isEmpty()) return {};
    const auto size = source.scaled(QSizeF(std::max(1, width() - 16), std::max(1, height() - 16)), Qt::KeepAspectRatio);
    return {(width() - size.width()) / 2, (height() - size.height()) / 2, size.width(), size.height()};
}
QRectF VideoCanvas::frameRect(const QRect& crop) const {
    const auto display = displayRect();
    return {display.x() + crop.x() * display.width() / m_original.width(), display.y() + crop.y() * display.height() / m_original.height(), crop.width() * display.width() / m_original.width(), crop.height() * display.height() / m_original.height()};
}
VideoSurface* VideoCanvas::prepareSurface() {
    if (!VideoSurface::usable(this)) return nullptr;
    if (m_surface) return m_surface;
    m_surface = new VideoSurface(this); m_surface->setGeometry(rect()); m_surface->setCursor(cursor()); m_surface->hide();
    // The buttons that sit on the image keep their place above it.
    VideoSurface::raiseAbove(m_overlay);
    return m_surface;
}
void VideoCanvas::setDirect(bool direct) {
    direct = direct && m_surface; if (direct == m_direct) return;
    m_direct = direct;
    if (direct) placeSurface(); else m_surface->clear();
    m_surface->setVisible(direct); if (direct && m_overlay) m_overlay->raise();
    refresh();
}
// The card is told which part of the image this view shows, and where.
void VideoCanvas::placeSurface() {
    if (!m_surface) return;
    m_surface->setGeometry(rect());
    const auto display = displayRect(); const double ratio = devicePixelRatioF();
    const QRect place = QRectF(display.x() * ratio, display.y() * ratio, display.width() * ratio, display.height() * ratio).toRect();
    m_surface->setView(m_preview && !m_tool && m_crop.isValid() ? m_crop : QRect(QPoint(), m_original), place, ground);
}
void VideoCanvas::point(const QCursor& cursor) { setCursor(cursor); if (m_surface) m_surface->setCursor(cursor); }
void VideoCanvas::changeEvent(QEvent* event) { if (event->type() == QEvent::EnabledChange) refresh(); QWidget::changeEvent(event); }
// Several changes in a row, a crop being dragged for instance, make one picture.
void VideoCanvas::refresh() {
    update(); if (!m_direct || m_stale) return;
    m_stale = true; QMetaObject::invokeMethod(this, [this] { m_stale = false; layOver(); }, Qt::QueuedConnection);
}
// The image is on the card. What this view draws over it becomes a picture laid on the image
// there, and the corners the surface cannot round are given the colour around the view.
void VideoCanvas::layOver() {
    if (!m_direct || !m_surface || m_original.isEmpty()) return;
    const auto display = displayRect(); const double ratio = devicePixelRatioF();
    QImage layer(qRound(width() * ratio), qRound(height() * ratio), QImage::Format_ARGB32_Premultiplied); layer.setDevicePixelRatio(ratio); layer.fill(Qt::transparent);
    {
        QPainter painter(&layer); painter.setRenderHint(QPainter::Antialiasing);
        QPainterPath corners; corners.addRect(rect()); corners.addRoundedRect(rect(), 8, 8); painter.fillPath(corners, tk::window);
        if (!isEnabled()) painter.fillRect(display, QColor(ground.red(), ground.green(), ground.blue(), 115));
        paintOver(painter, display, m_preview && !m_tool && m_crop.isValid());
    }
    m_surface->setOverlay(layer);
}
void VideoCanvas::paintEvent(QPaintEvent*) {
    const auto display = displayRect(); const bool result = m_preview && !m_tool && m_crop.isValid();
    QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing);
    // Under the surface of the card there is only the ground to draw.
    if (m_direct) { painter.setPen(Qt::NoPen); painter.setBrush(ground); painter.drawRoundedRect(rect(), 8, 8); return; }
    // The rounded ground is drawn, not cut out: the picture stays eight pixels inside it, and a
    // clip around every image would cost more than the image.
    painter.setPen(Qt::NoPen); painter.setBrush(ground); painter.drawRoundedRect(rect(), 8, 8); painter.setBrush(Qt::NoBrush);
    if (m_image.isNull() || m_original.isEmpty()) { painter.setPen(tk::text2); painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("Aperçu vidéo")); return; }
    const double sx = m_image.width() / double(m_original.width()), sy = m_image.height() / double(m_original.height());
    const QRectF source = result ? QRectF(m_crop.x() * sx, m_crop.y() * sy, m_crop.width() * sx, m_crop.height() * sy) : QRectF(m_image.rect());
    if (!isEnabled()) painter.setOpacity(0.55);
    painter.setRenderHint(QPainter::Antialiasing, false);
    // An image made for this view goes to the screen pixel for pixel, which is a copy. Anything else,
    // a view changing size or a crop too small to fill it, is scaled, which is many times the work.
    const double ratio = devicePixelRatioF(); const QRectF place(display.x() * ratio, display.y() * ratio, display.width() * ratio, display.height() * ratio);
    if (std::abs(source.width() - place.width()) <= 2 && std::abs(source.height() - place.height()) <= 2) {
        painter.save(); painter.scale(1 / ratio, 1 / ratio);
        const QRect part = source.toRect(); painter.drawImage(QPoint(qRound(place.x() + (place.width() - part.width()) / 2), qRound(place.y() + (place.height() - part.height()) / 2)), m_image, part);
        painter.restore();
    } else { painter.setRenderHint(QPainter::SmoothPixmapTransform); painter.drawImage(display, m_image, source); }
    painter.setOpacity(1);
    paintOver(painter, display, result);
}
// What lies over the image: the crop and its handles, its name, the notice of a locked edit.
void VideoCanvas::paintOver(QPainter& painter, const QRectF& display, bool result) {
    painter.setRenderHint(QPainter::Antialiasing, false);
    if (!isEnabled()) painter.setOpacity(0.55);
    if (!result) {
        const QRect selected = m_drag != Drag::None ? dragged() : m_crop; const bool whole = selected == QRect(QPoint(), m_original) && m_drag == Drag::None;
        const QRectF frame = frameRect(selected);
        if (!whole) {
            QPainterPath shade; shade.addRect(display); shade.addRect(frame); painter.fillPath(shade, QColor(4, 5, 7, 173));
            painter.setPen(QPen(QColor(233, 237, 243, 56), 1));
            for (int third = 1; third < 3; ++third) { const double x = std::round(frame.x() + frame.width() * third / 3) + 0.5, y = std::round(frame.y() + frame.height() * third / 3) + 0.5; painter.drawLine(QPointF(x, frame.top()), QPointF(x, frame.bottom())); painter.drawLine(QPointF(frame.left(), y), QPointF(frame.right(), y)); }
        }
        const bool framed = !whole || (m_tool && m_freeFrame);
        if (framed) { painter.setPen(QPen(tk::text, 1)); painter.setBrush(Qt::NoBrush); painter.drawRect(frame.adjusted(0.5, 0.5, -0.5, -0.5)); }
        if (framed && m_tool && isEnabled()) {
            painter.setPen(QPen(tk::window, 2)); painter.setBrush(tk::text);
            for (const auto& corner : {frame.topLeft(), frame.topRight(), frame.bottomLeft(), frame.bottomRight()}) painter.drawRect(QRectF(corner.x() - 5, corner.y() - 5, 10, 10));
        }
        if (!whole && !m_label.isEmpty() && frame.width() > 90 && frame.height() > 40) {
            const auto text = m_drag != Drag::None ? QStringLiteral("%1 × %2").arg(selected.width()).arg(selected.height()) : m_label; const auto size = Tag::measure(text);
            Tag::paint(painter, QRectF(frame.left() + 8, frame.top() + 8, size.width(), size.height()), text, Tag::Dark);
        }
    }
    painter.setOpacity(1);
    if (!m_notice.isEmpty()) { const auto size = Tag::measure(m_notice, true); Tag::paint(painter, QRectF(12, 12, size.width() + 6, 26), m_notice, Tag::Info, QStringLiteral("lock")); }
}
QPoint VideoCanvas::sourcePoint(QPointF point) const {
    const auto display = displayRect();
    return {std::clamp(int((point.x() - display.x()) * m_original.width() / display.width()), 0, m_original.width() - 1),
            std::clamp(int((point.y() - display.y()) * m_original.height() / display.height()), 0, m_original.height() - 1)};
}
VideoCanvas::Drag VideoCanvas::hit(QPointF point) const {
    if (!m_crop.isValid() || m_original.isEmpty()) return Drag::Draw;
    // The whole image shows no frame, hence no corner to pull, unless a free crop was asked for.
    if (m_crop == QRect(QPoint(), m_original) && !m_freeFrame) return Drag::Draw;
    const auto frame = frameRect(m_crop); const auto grabs = [&](QPointF corner) { return std::abs(point.x() - corner.x()) <= 9 && std::abs(point.y() - corner.y()) <= 9; };
    if (grabs(frame.topLeft())) return Drag::TopLeft;
    if (grabs(frame.topRight())) return Drag::TopRight;
    if (grabs(frame.bottomLeft())) return Drag::BottomLeft;
    if (grabs(frame.bottomRight())) return Drag::BottomRight;
    // The whole frame is nothing to move: a drag on it draws a new crop.
    if (m_crop != QRect(QPoint(), m_original) && frame.contains(point)) return Drag::Move;
    return Drag::Draw;
}
QRect VideoCanvas::dragged() const {
    const QRect bounds(QPoint(), m_original);
    if (m_drag == Drag::Move) {
        QRect moved = m_start.translated(m_current - m_anchor);
        moved.moveLeft(std::clamp(moved.left(), 0, bounds.width() - moved.width())); moved.moveTop(std::clamp(moved.top(), 0, bounds.height() - moved.height())); return moved;
    }
    // A corner drag keeps the opposite corner; a new crop grows from where the press landed.
    QPoint fixed = m_anchor;
    if (m_drag == Drag::TopLeft) fixed = m_start.bottomRight(); else if (m_drag == Drag::TopRight) fixed = m_start.bottomLeft();
    else if (m_drag == Drag::BottomLeft) fixed = m_start.topRight(); else if (m_drag == Drag::BottomRight) fixed = m_start.topLeft();
    QRect rect = QRect(fixed, m_current).normalized();
    if (m_aspect > 0) {
        int width = rect.width(), height = qRound(width / m_aspect);
        const int room = m_current.y() >= fixed.y() ? bounds.height() - fixed.y() : fixed.y() + 1;
        if (height > room) { height = room; width = qRound(height * m_aspect); }
        rect = QRect(m_current.x() >= fixed.x() ? fixed.x() : fixed.x() - width + 1, m_current.y() >= fixed.y() ? fixed.y() : fixed.y() - height + 1, width, height);
    }
    return rect.intersected(bounds);
}
void VideoCanvas::mousePressEvent(QMouseEvent* event) {
    if (!m_tool || !isEnabled() || m_image.isNull() || event->button() != Qt::LeftButton) return;
    const auto kind = hit(event->position());
    if (kind == Drag::Draw && !displayRect().contains(event->position())) return;
    m_drag = kind; m_start = m_crop; m_anchor = m_current = sourcePoint(event->position()); refresh();
}
void VideoCanvas::mouseMoveEvent(QMouseEvent* event) {
    if (m_drag != Drag::None) { m_current = sourcePoint(event->position()); refresh(); return; }
    if (!m_tool || !isEnabled() || m_image.isNull()) return;
    const auto kind = hit(event->position());
    point(kind == Drag::Move ? Qt::SizeAllCursor : kind == Drag::TopLeft || kind == Drag::BottomRight ? Qt::SizeFDiagCursor : kind == Drag::TopRight || kind == Drag::BottomLeft ? Qt::SizeBDiagCursor : Qt::CrossCursor);
}
void VideoCanvas::mouseReleaseEvent(QMouseEvent* event) {
    if (m_drag == Drag::None || event->button() != Qt::LeftButton) return;
    m_current = sourcePoint(event->position()); const auto selection = dragged(); const bool moved = m_drag == Drag::Move; m_drag = Drag::None;
    // An accidental click must not replace the current crop.
    if (moved ? selection != m_start : selection.width() >= 8 && selection.height() >= 8) emit cropSelected(selection);
    refresh();
}
}
