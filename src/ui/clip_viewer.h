#pragma once
#include "media/clip_player.h"
#include <QTimer>
#include <QWidget>
#include <functional>
#include <optional>
class QLabel;
class QSlider;
namespace clipsapp {
class Button;
class ElideLabel;
class VideoSurface;
// Shows a clip on the whole screen, to watch it or to show it to someone without sending it.
// Images come from the player at the size of the screen, so each pixel drawn is a decoded one.
class ClipViewer final : public QWidget {
    Q_OBJECT
public:
    explicit ClipViewer(QWidget* owner);
    ~ClipViewer() override;
    // Plays playlist[index]. An edit, given by the editor, shows that clip as it would be exported.
    void open(const QStringList& playlist, int index, std::optional<ClipEdit> edit = {}, qint64 startMs = -1);
    void finish();
    QString currentPath() const { return m_playlist.value(m_index); }
    int index() const { return m_index; }
    QImage image() const { return m_image; }
    // Listening level in percent; it is the viewer's own and leaves the clips as they are.
    int volume() const { return m_volume; }
    bool muted() const { return m_muted; }
    void setVolume(int percent);
    // The usual level of each track, for the clips shown as they are.
    void setDefaultMix(const TrackMix& mix) { m_mix = mix; }
    // The usual levels of each clip, those of its game: asked as each clip of the playlist opens.
    void setMixLookup(std::function<TrackMix(const QString&)> lookup) { m_mixFor = std::move(lookup); }
signals:
    void volumeChanged(int percent);
    void viewed(const QString& path);
    void closed(const QString& path, qint64 positionMs);
protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    bool event(QEvent* event) override;
private:
    QSize deviceSize() const;
    void keepAwake(bool awake);
    void load(int index);
    void jump(int index);
    void updatePlayer();
    void wake();
    void setControlsVisible(bool visible);
    void step(int frames);
    void updateStepKeys();
    void toggleMute();
    void applyVolume();
    void placeSurface();
    ClipPlayer m_player;
    QWidget* m_owner; QStringList m_playlist; int m_index = -1;
    std::optional<ClipEdit> m_edit; ClipEdit m_current; qint64 m_start = -1; bool m_ready = false, m_open = false, m_awake = false;
    QImage m_image; qint64 m_imageKey = 0; QString m_message;
    QTimer m_hide; int m_volume = 100, m_wheel = 0; bool m_muted = false; TrackMix m_mix; std::function<TrackMix(const QString&)> m_mixFor;
    // Where the graphics card shows the clip itself, under the two bars; nothing where it cannot.
    VideoSurface* m_surface = nullptr; bool m_direct = false; QSize m_frame;
    Button* m_stepBack; Button* m_stepForward; Button* m_mute; QSlider* m_volumeSlider;
    QWidget* m_top; QWidget* m_bottom; ElideLabel* m_title; QLabel* m_count; Button* m_previous; Button* m_next; Button* m_play; QLabel* m_time; QLabel* m_total; QSlider* m_seek;
};
}
