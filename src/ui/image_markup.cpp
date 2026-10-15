#include "image_markup.h"
#include "video_canvas.h"
#include <QEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QtMath>
#include <algorithm>
#include <cmath>
namespace clipsapp {
void paintMarks(QPainter& painter, const QList<Mark>& marks) {
    painter.save(); painter.setRenderHint(QPainter::Antialiasing);
    for (const auto& mark : marks) {
        if (mark.points.isEmpty()) continue;
        QPen pen(mark.color, mark.width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin); painter.setPen(pen); painter.setBrush(Qt::NoBrush);
        const auto first = mark.points.first(), last = mark.points.last();
        switch (mark.kind) {
        case Mark::Pen:
            if (mark.points.size() == 1) { painter.drawPoint(first); break; }
            painter.drawPolyline(mark.points.data(), int(mark.points.size())); break;
        case Mark::Rectangle: painter.drawRect(QRectF(first, last).normalized()); break;
        case Mark::Ellipse: painter.drawEllipse(QRectF(first, last).normalized()); break;
        case Mark::Arrow: {
            // A head that grows with the line, filled, so that it reads at any thickness.
            const double angle = std::atan2(last.y() - first.y(), last.x() - first.x()), head = mark.width * 3.5 + 10;
            const QPointF left = last - QPointF(std::cos(angle - 0.45), std::sin(angle - 0.45)) * head, right = last - QPointF(std::cos(angle + 0.45), std::sin(angle + 0.45)) * head;
            painter.drawLine(first, (left + right) / 2);
            painter.setBrush(mark.color); QPolygonF tip{last, left, right}; painter.drawPolygon(tip); break;
        }
        }
    }
    painter.restore();
}
MarkupLayer::MarkupLayer(VideoCanvas* canvas) : QWidget(canvas), m_canvas(canvas) {
    setObjectName("screenshot-markup"); setAttribute(Qt::WA_TransparentForMouseEvents); setAttribute(Qt::WA_NoSystemBackground); setGeometry(canvas->rect());
    canvas->installEventFilter(this); show();
}
void MarkupLayer::setDrawing(bool drawing) { m_drawing = drawing; setAttribute(Qt::WA_TransparentForMouseEvents, !drawing); setCursor(drawing ? Qt::CrossCursor : Qt::ArrowCursor); update(); }
void MarkupLayer::undo() { if (m_marks.isEmpty()) return; m_marks.removeLast(); update(); emit changed(); }
void MarkupLayer::clear() { if (m_marks.isEmpty()) return; m_marks.clear(); update(); emit changed(); }
// The canvas changes size: the layer follows it.
bool MarkupLayer::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_canvas && (event->type() == QEvent::Resize || event->type() == QEvent::Show)) { setGeometry(m_canvas->rect()); raise(); }
    return QWidget::eventFilter(watched, event);
}
QPointF MarkupLayer::toImage(QPointF point) const {
    const auto area = m_canvas->imageArea(); const QRectF part(m_canvas->shownPart()); if (area.isEmpty() || part.isEmpty()) return {};
    const QPointF image(part.x() + (point.x() - area.x()) * part.width() / area.width(), part.y() + (point.y() - area.y()) * part.height() / area.height());
    return {std::clamp(image.x(), part.left(), part.right()), std::clamp(image.y(), part.top(), part.bottom())};
}
void MarkupLayer::paintEvent(QPaintEvent*) {
    const auto area = m_canvas->imageArea(); const QRectF part(m_canvas->shownPart()); if (area.isEmpty() || part.isEmpty() || m_marks.isEmpty()) return;
    // Drawn in the pixels of the picture, then brought to where the view shows it; nothing spills outside it.
    QPainter painter(this); painter.setClipRect(area);
    painter.translate(area.topLeft()); painter.scale(area.width() / part.width(), area.height() / part.height()); painter.translate(-part.topLeft());
    paintMarks(painter, m_marks);
}
void MarkupLayer::mousePressEvent(QMouseEvent* event) {
    if (!m_drawing || event->button() != Qt::LeftButton || !m_canvas->imageArea().contains(event->position())) return;
    const auto point = toImage(event->position());
    Mark mark; mark.kind = m_tool; mark.color = m_color; mark.width = m_width; mark.points = {point, point};
    if (m_tool == Mark::Pen) mark.points = {point};
    m_marks.append(mark); m_dragging = true; update();
}
void MarkupLayer::mouseMoveEvent(QMouseEvent* event) {
    if (!m_dragging || m_marks.isEmpty()) return;
    auto& mark = m_marks.last(); const auto point = toImage(event->position());
    if (mark.kind == Mark::Pen) { if (QLineF(mark.points.last(), point).length() >= std::max(1.0, mark.width / 4)) mark.points.append(point); }
    else mark.points.last() = point;
    update();
}
void MarkupLayer::mouseReleaseEvent(QMouseEvent* event) {
    if (!m_dragging || event->button() != Qt::LeftButton) return;
    m_dragging = false; const auto& mark = m_marks.last();
    // A shape made by a mere click is no shape; a click of the brush is a dot.
    if (mark.kind != Mark::Pen && QLineF(mark.points.first(), mark.points.last()).length() < mark.width) m_marks.removeLast();
    update(); emit changed();
}
}
