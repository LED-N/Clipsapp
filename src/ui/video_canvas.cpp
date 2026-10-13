#include "video_canvas.h"
#include "widgets.h"
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>
namespace clipsapp {
VideoCanvas::VideoCanvas(QWidget* parent) : QWidget(parent) { setMinimumSize(280, 180); setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding); setMouseTracking(true); }
void VideoCanvas::setFrame(QImage image, QSize original) { if (m_image.cacheKey() == image.cacheKey() && m_original == original) return; m_image = std::move(image); m_original = original; update(); }
void VideoCanvas::setCrop(QRect crop) { m_crop = crop; update(); }
void VideoCanvas::setResultPreview(bool enabled) { m_preview = enabled; update(); }
void VideoCanvas::setCropTool(bool enabled) { m_tool = enabled; m_drag = Drag::None; setCursor(enabled ? Qt::CrossCursor : Qt::ArrowCursor); update(); }
void VideoCanvas::setOverlay(QWidget* overlay) { m_overlay = overlay; overlay->setParent(this); overlay->adjustSize(); overlay->move(width() - overlay->width() - 12, 12); overlay->show(); }
void VideoCanvas::resizeEvent(QResizeEvent*) { if (m_overlay) { m_overlay->adjustSize(); m_overlay->move(width() - m_overlay->width() - 12, 12); } }
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
void VideoCanvas::paintEvent(QPaintEvent*) {
    QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing);
    QPainterPath rounded; rounded.addRoundedRect(rect(), 8, 8); painter.setClipPath(rounded); painter.fillRect(rect(), QColor(0x07, 0x08, 0x0A));
    if (m_image.isNull() || m_original.isEmpty()) { painter.setPen(tk::text2); painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("Aperçu vidéo")); return; }
    const auto display = displayRect();
    const bool result = m_preview && !m_tool && m_crop.isValid();
    const double sx = m_image.width() / double(m_original.width()), sy = m_image.height() / double(m_original.height());
    const QRectF source = result ? QRectF(m_crop.x() * sx, m_crop.y() * sy, m_crop.width() * sx, m_crop.height() * sy) : QRectF(m_image.rect());
    if (!isEnabled()) painter.setOpacity(0.55);
    painter.setRenderHint(QPainter::Antialiasing, false); painter.setRenderHint(QPainter::SmoothPixmapTransform); painter.drawImage(display, m_image, source);
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
    m_drag = kind; m_start = m_crop; m_anchor = m_current = sourcePoint(event->position()); update();
}
void VideoCanvas::mouseMoveEvent(QMouseEvent* event) {
    if (m_drag != Drag::None) { m_current = sourcePoint(event->position()); update(); return; }
    if (!m_tool || !isEnabled() || m_image.isNull()) return;
    const auto kind = hit(event->position());
    setCursor(kind == Drag::Move ? Qt::SizeAllCursor : kind == Drag::TopLeft || kind == Drag::BottomRight ? Qt::SizeFDiagCursor : kind == Drag::TopRight || kind == Drag::BottomLeft ? Qt::SizeBDiagCursor : Qt::CrossCursor);
}
void VideoCanvas::mouseReleaseEvent(QMouseEvent* event) {
    if (m_drag == Drag::None || event->button() != Qt::LeftButton) return;
    m_current = sourcePoint(event->position()); const auto selection = dragged(); const bool moved = m_drag == Drag::Move; m_drag = Drag::None;
    // An accidental click must not replace the current crop.
    if (moved ? selection != m_start : selection.width() >= 8 && selection.height() >= 8) emit cropSelected(selection);
    update();
}
}
