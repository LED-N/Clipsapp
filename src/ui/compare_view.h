#pragma once
#include "media/clip_player.h"
#include "media/clip_reader.h"
#include <QPointer>
#include <QTimer>
#include <QWidget>
#include <memory>
class QBoxLayout;
class QLabel;
class QSlider;
namespace clipsapp {
class Button;
class Segmented;
// One clip in two versions on the same picture: left of the separation the original, right of it
// the compressed file. The separation follows the mouse. The original plays, with its sound; the
// compressed file follows its clock image for image, so that both sides always show the same moment
// at the same scale. At 100 % or 200 % the picture is shown in its own pixels, where a loss would show;
// a drag of the right button moves it. The comparison also opens on the whole screen.
class CompareView final : public QWidget {
    Q_OBJECT
public:
    explicit CompareView(QWidget* parent = nullptr);
    ~CompareView() override;
    void open(const QString& before, const QString& after, qint64 beforeBytes, qint64 afterBytes);
    // Lets go of both files: they can then be moved or deleted.
    void close();
    double split() const { return m_split; }
    void setSplit(double split);
    bool playing() const;
    void setPlaying(bool playing);
    // Listening level in percent, squared on its way to the speakers as in the full-screen player.
    void setVolume(int percent);
    int volume() const { return m_volume; }
    void setFullScreen(bool full);
    bool fullScreen() const { return m_window != nullptr; }
    ClipPlayer* before() { return &m_before; }
    // The images on screen, once both sides show the same moment at the same scale.
    QImage shownBefore() const { return m_shownBefore; }
    QImage shownAfter() const { return m_shownAfter; }
signals:
    void volumeChanged(int percent);
protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
private:
    class Follower;
    void paintPicture(QPainter& painter);
    void present();
    void reframe();
    void refresh();
    QSize deviceSize() const;
    ClipPlayer m_before; std::unique_ptr<Follower> m_after;
    QWidget* m_picture; Button* m_play; QLabel* m_time; QLabel* m_total; QSlider* m_seek; Segmented* m_zoom; Button* m_mute; QSlider* m_volumeSlider; Button* m_expand;
    QTimer m_resize, m_pan; qint64 m_beforeBytes = 0, m_afterBytes = 0; int m_volume = 100;
    // What both sides are asked for; a change waits for both before the picture changes.
    FrameOutput m_output; quint64 m_generation = 0; qint64 m_staleKey = 0; QImage m_shownBefore, m_shownAfter;
    double m_split = 0.5; QPointF m_focus{-1, -1}; QPoint m_panFrom; bool m_dragging = false, m_panning = false, m_framed = false;
    // On the whole screen: the window it is moved to, and where it goes back.
    QPointer<QWidget> m_window, m_homeParent; QBoxLayout* m_home = nullptr; int m_homeIndex = -1;
};
}
