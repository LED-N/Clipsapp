#pragma once
#include <QImage>
#include <QWidget>
namespace clipsapp {
class VideoCanvas final : public QWidget {
    Q_OBJECT
public:
    explicit VideoCanvas(QWidget* parent = nullptr);
    void setFrame(QImage image, QSize original);
    void setCrop(QRect crop);
    void setResultPreview(bool enabled);
    // "Cadrer": the whole frame with the crop drawn on it, to draw, move or resize.
    void setCropTool(bool enabled);
    // Width over height the crop must keep; 0 leaves it free.
    void setAspect(double ratio) { m_aspect = ratio; }
    // "Libre" on the whole image: show the frame and its corners, so there is something to pull.
    void setFreeFrame(bool shown) { m_freeFrame = shown; update(); }
    void setCropLabel(const QString& label) { m_label = label; update(); }
    void setNotice(const QString& notice) { m_notice = notice; update(); }
    void setOverlay(QWidget* overlay);
signals:
    void cropSelected(QRect crop);
protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
private:
    enum class Drag { None, Draw, Move, TopLeft, TopRight, BottomLeft, BottomRight };
    QRectF displayRect() const;
    QRectF frameRect(const QRect& crop) const;
    QPoint sourcePoint(QPointF point) const;
    Drag hit(QPointF point) const;
    QRect dragged() const;
    QImage m_image;
    QSize m_original;
    QRect m_crop, m_start;
    QPoint m_anchor, m_current;
    QString m_label, m_notice;
    QWidget* m_overlay = nullptr;
    double m_aspect = 0;
    Drag m_drag = Drag::None;
    bool m_preview = true, m_tool = false, m_freeFrame = false;
};
}
