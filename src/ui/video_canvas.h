#pragma once
#include <QImage>
#include <QWidget>
class QCursor;
class QPainter;
namespace clipsapp {
class VideoSurface;
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
    void setFreeFrame(bool shown) { m_freeFrame = shown; refresh(); }
    void setCropLabel(const QString& label) { m_label = label; refresh(); }
    void setNotice(const QString& notice) { if (notice == m_notice) return; m_notice = notice; refresh(); }
    void setOverlay(QWidget* overlay);
    // The size, in pixels of the screen, at which an image of the whole frame is drawn without being
    // scaled: what the view shows of it then fills its place pixel for pixel. Never larger than the frame.
    QSize wantedSize() const;
    // Where the graphics card can show the clip itself, the place it does so in this view; nothing
    // elsewhere. The view then draws what belongs over the image, and no longer the image.
    VideoSurface* prepareSurface();
    VideoSurface* surface() const { return m_surface; }
    void setDirect(bool direct);
    // The image being shown, for what measures how often it changes.
    qint64 frameKey() const { return m_image.cacheKey(); }
    // Where the picture is drawn in this view, and which part of it: the crop when its result is shown.
    QRectF imageArea() const { return displayRect(); }
    QRect shownPart() const { return m_preview && !m_tool && m_crop.isValid() ? m_crop : QRect(QPoint(), m_original); }
signals:
    void cropSelected(QRect crop);
    void sizeWanted(QSize size);
protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void changeEvent(QEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
private:
    enum class Drag { None, Draw, Move, TopLeft, TopRight, BottomLeft, BottomRight };
    // A frame to crop: drawn here, or shown by the graphics card, which leaves this view no image of it.
    bool hasPicture() const { return !m_original.isEmpty() && (m_direct || !m_image.isNull()); }
    QRectF displayRect() const;
    QRectF frameRect(const QRect& crop) const;
    QPoint sourcePoint(QPointF point) const;
    Drag hit(QPointF point) const;
    QRect dragged() const;
    void announce();
    void paintOver(QPainter& painter, const QRectF& display, bool result);
    void point(const QCursor& cursor);
    void placeSurface();
    // Windows shows the surface above this view, which is then never asked to draw itself: what
    // lies over the image is drawn again whenever something of it changes.
    void refresh();
    void layOver();
    bool m_stale = false;
    VideoSurface* m_surface = nullptr; bool m_direct = false;
    QImage m_image;
    QSize m_original, m_wanted;
    QRect m_crop, m_start;
    QPoint m_anchor, m_current;
    QString m_label, m_notice;
    QWidget* m_overlay = nullptr;
    double m_aspect = 0;
    Drag m_drag = Drag::None;
    bool m_preview = true, m_tool = false, m_freeFrame = false;
};
}
