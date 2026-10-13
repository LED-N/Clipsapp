#pragma once
#include <QWidget>
#include "media/clip_edit.h"
#include "media/clip_analysis.h"
#include <algorithm>
class QAbstractButton;
class QSlider;
namespace clipsapp {
// Drawn to measure. Waveforms and the frame strip are rendered once into an image;
// during playback only the playhead is painted over it.
class ClipTimeline final : public QWidget {
    Q_OBJECT
public:
    explicit ClipTimeline(QWidget* parent = nullptr);
    void setClip(const ClipInfo& info, const ClipEdit& edit);
    void setEdit(const ClipEdit& edit);
    void setAnalysis(ClipAnalysis analysis);
    void setPosition(qint64 position);
    void setZoom(double factor, qint64 anchorMs = -1, double anchorRatio = 0.5);
    void setViewStart(qint64 startMs);
    // Narrow windows: smaller header, ruler, frame strip and tracks.
    void setCompact(bool compact);
    // Ruler, frame strip and up to three tracks; further tracks scroll.
    int preferredHeight() const;
    double zoomFactor() const { return m_zoom; }
    qint64 viewStart() const { return m_viewStart; }
    qint64 visibleDuration() const;
    bool hasAnalysis() const { return !m_analysis.thumbnails.isEmpty(); }
    const QList<QSlider*>& volumes() const { return m_volumes; }
    const QList<QAbstractButton*>& mutes() const { return m_mutes; }
signals:
    void seekRequested(qint64 position);
    void trimChanged(qint64 start, qint64 end);
    void audioChanged();
    void removeTrackRequested(int stream);
    void viewChanged();
protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
private:
    struct Metrics { int header, ruler, video, track; };
    Metrics metrics() const { return m_compact ? Metrics{168, 22, 40, 44} : Metrics{208, 24, 52, 52}; }
    int laneLeft() const { return metrics().header + 12; }
    int laneSpan() const { return std::max(1, width() - 13 - laneLeft()); }
    int scrollLimit() const;
    double xAt(qint64 time) const;
    qint64 timeAt(double x) const;
    void drag(double x);
    void paintContent(QPainter& painter);
    void rebuildRows();
    void placeRows();
    QImage m_background;
    bool m_backgroundDirty = true, m_compact = false;
    ClipInfo m_info; ClipEdit m_edit; ClipAnalysis m_analysis; qint64 m_position = 0;
    double m_zoom = 1, m_grab = 0; qint64 m_viewStart = 0; int m_scroll = 0;
    QWidget* m_headers; QList<QWidget*> m_rows; QList<QSlider*> m_volumes; QList<QAbstractButton*> m_mutes;
    enum class Drag { None, Playhead, Start, End }; Drag m_drag = Drag::None;
};
}
