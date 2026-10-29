#pragma once
#include <QColor>
#include <QList>
#include <QPointF>
#include <QPointer>
#include <QWidget>
class QPainter;
namespace clipsapp {
class VideoCanvas;
// A stroke or a shape drawn on a picture, in the pixels of that picture: it lands the same on the
// full picture as on the view it was drawn in.
struct Mark {
    enum Kind { Pen, Arrow, Rectangle, Ellipse };
    Kind kind = Pen; QColor color = Qt::red; double width = 6;
    // The points of a stroke; for a shape, where the drag began and where it ended.
    QList<QPointF> points;
};
void paintMarks(QPainter& painter, const QList<Mark>& marks);
// Laid over a canvas: shows the marks where the picture is drawn, and while drawing, takes the
// mouse to add new ones. Otherwise the mouse goes through it, to the crop.
class MarkupLayer final : public QWidget {
    Q_OBJECT
public:
    explicit MarkupLayer(VideoCanvas* canvas);
    const QList<Mark>& marks() const { return m_marks; }
    void setMarks(const QList<Mark>& marks) { m_marks = marks; update(); }
    void setDrawing(bool drawing);
    bool drawing() const { return m_drawing; }
    void setTool(Mark::Kind tool) { m_tool = tool; }
    void setColor(const QColor& color) { m_color = color; }
    // The thickness, in pixels of the picture.
    void setStrokeWidth(double width) { m_width = width; }
    // What must stay above the drawing, such as the buttons on the picture: it keeps under them.
    void keepUnder(QWidget* widget) { m_under = widget; place(); }
    void undo();
    void clear();
signals:
    void changed();
protected:
    void place();
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
private:
    QPointF toImage(QPointF point) const;
    VideoCanvas* m_canvas; QPointer<QWidget> m_under; QList<Mark> m_marks; bool m_drawing = false, m_dragging = false;
    Mark::Kind m_tool = Mark::Pen; QColor m_color = Qt::red; double m_width = 6;
};
}
