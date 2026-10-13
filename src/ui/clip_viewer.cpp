#include "clip_viewer.h"
#include "layout_keys.h"
#include "widgets.h"
#include <QApplication>
#include <QInputMethod>
#include <QWheelEvent>
#include <QCloseEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QPainter>
#include <QScreen>
#include <QSignalBlocker>
#include <QSlider>
#include <QStyle>
#include <algorithm>
#include <cmath>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace clipsapp {
namespace {
QString clock(qint64 ms) { const auto seconds = std::max<qint64>(0, ms) / 1000; return QString("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0')); }
// A click anywhere on the bar goes there at once, and the handle follows the pointer.
// The wheel is left to the viewer: over any part of the screen it sets the volume.
class JumpSlider final : public QSlider {
public:
    JumpSlider() : QSlider(Qt::Horizontal) { setFocusPolicy(Qt::NoFocus); setCursor(Qt::PointingHandCursor); }
protected:
    void wheelEvent(QWheelEvent* event) override { event->ignore(); }
    void mousePressEvent(QMouseEvent* event) override { if (event->button() != Qt::LeftButton) return; setSliderDown(true); follow(event); }
    void mouseMoveEvent(QMouseEvent* event) override { if (isSliderDown()) follow(event); }
    void mouseReleaseEvent(QMouseEvent* event) override { if (event->button() == Qt::LeftButton) setSliderDown(false); }
private:
    void follow(QMouseEvent* event) {
        const int value = QStyle::sliderValueFromPosition(minimum(), maximum(), int(event->position().x()) - 7, std::max(1, width() - 14));
        if (value == sliderPosition()) return;
        setSliderPosition(value); emit sliderMoved(value);
    }
};
}
ClipViewer::ClipViewer(QWidget* owner) : QWidget(owner, Qt::Window | Qt::FramelessWindowHint), m_player(this), m_owner(owner) {
    setObjectName("clip-viewer"); setWindowTitle(QStringLiteral("Clipsapp")); setWindowModality(Qt::ApplicationModal); setAttribute(Qt::WA_OpaquePaintEvent);
    setFocusPolicy(Qt::StrongFocus); setMouseTracking(true); followVisibility(this, owner);
    setStyleSheet(QStringLiteral("QFrame#viewerBar { background: rgba(12, 14, 18, 224); border: none; }"));
    auto* top = new QFrame(this); top->setObjectName("viewerBar"); m_top = top; auto* topLayout = new QHBoxLayout(top); topLayout->setContentsMargins(20, 0, 12, 0); topLayout->setSpacing(12);
    m_title = new ElideLabel({}, "clip", Qt::ElideMiddle); m_title->setObjectName("viewer-title"); m_title->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed); topLayout->addWidget(m_title);
    m_count = label({}, "small"); m_count->setObjectName("viewer-count"); topLayout->addWidget(m_count); topLayout->addStretch();
    topLayout->addWidget(label(QStringLiteral("Échap pour quitter"), "small3"));
    auto* close = new Button(QStringLiteral("x"), QString(), Button::Ghost); close->setObjectName("viewer-close"); close->setFocusPolicy(Qt::NoFocus); close->setToolTip(QStringLiteral("Quitter le plein écran (Échap)")); close->setAccessibleName(QStringLiteral("Quitter le plein écran")); topLayout->addWidget(close);
    auto* bottom = new QFrame(this); bottom->setObjectName("viewerBar"); m_bottom = bottom; auto* bottomLayout = new QHBoxLayout(bottom); bottomLayout->setContentsMargins(16, 0, 20, 0); bottomLayout->setSpacing(10);
    // As in the editor, the chevrons move by one image; the barred arrows change clip.
    m_previous = new Button(QStringLiteral("start"), QString(), Button::Ghost); m_previous->setObjectName("viewer-previous"); m_previous->setToolTip(QStringLiteral("Clip précédent (Page précédente)")); m_previous->setAccessibleName(QStringLiteral("Clip précédent"));
    m_stepBack = new Button(QStringLiteral("prev"), QString(), Button::Ghost); m_stepBack->setObjectName("viewer-frame-previous"); m_stepBack->setAccessibleName(QStringLiteral("Image précédente"));
    m_play = new Button(QStringLiteral("pause"), QStringLiteral("Pause")); m_play->setObjectName("viewer-play"); m_play->setFixedWidth(92);
    m_stepForward = new Button(QStringLiteral("next"), QString(), Button::Ghost); m_stepForward->setObjectName("viewer-frame-next"); m_stepForward->setAccessibleName(QStringLiteral("Image suivante"));
    m_next = new Button(QStringLiteral("end"), QString(), Button::Ghost); m_next->setObjectName("viewer-next"); m_next->setToolTip(QStringLiteral("Clip suivant (Page suivante)")); m_next->setAccessibleName(QStringLiteral("Clip suivant"));
    m_time = monoLabel(QStringLiteral("0:00"), 14, tk::text, QFont::DemiBold); m_time->setObjectName("viewer-time"); m_total = monoLabel(QStringLiteral("0:00"), 13, tk::text2);
    m_seek = new JumpSlider; m_seek->setObjectName("viewer-seek");
    m_mute = new Button(QStringLiteral("speaker"), QString(), Button::Ghost); m_mute->setObjectName("viewer-mute"); m_mute->setFocusPolicy(Qt::NoFocus);
    m_volumeSlider = new JumpSlider; m_volumeSlider->setObjectName("viewer-volume"); m_volumeSlider->setRange(0, 100); m_volumeSlider->setFixedWidth(96); m_volumeSlider->setAccessibleName(QStringLiteral("Volume"));
    for (auto* button : {m_previous, m_stepBack, m_play, m_stepForward, m_next}) { button->setFocusPolicy(Qt::NoFocus); bottomLayout->addWidget(button); }
    // Held down, a step button walks through the images.
    for (auto* button : {m_stepBack, m_stepForward}) { button->setAutoRepeat(true); button->setAutoRepeatDelay(350); button->setAutoRepeatInterval(90); }
    bottomLayout->addSpacing(6); bottomLayout->addWidget(m_time); bottomLayout->addWidget(m_seek, 1); bottomLayout->addWidget(m_total);
    bottomLayout->addSpacing(10); bottomLayout->addWidget(m_mute); bottomLayout->addWidget(m_volumeSlider);
    m_hide.setSingleShot(true); m_hide.setInterval(2500);
    connect(&m_hide, &QTimer::timeout, this, [this] {
        // The controls step aside while the clip plays, unless the pointer is on them.
        if (m_player.snapshot().playing && !m_seek->isSliderDown() && !m_volumeSlider->isSliderDown() && !m_top->underMouse() && !m_bottom->underMouse()) setControlsVisible(false);
    });
    connect(&m_player, &ClipPlayer::changed, this, &ClipViewer::updatePlayer);
    connect(close, &QPushButton::clicked, this, [this] { finish(); });
    connect(m_play, &QPushButton::clicked, this, [this] { if (m_ready) m_player.setPlaying(!m_player.snapshot().playing); wake(); });
    connect(m_previous, &QPushButton::clicked, this, [this] { jump(m_index - 1); });
    connect(m_next, &QPushButton::clicked, this, [this] { jump(m_index + 1); });
    connect(m_seek, &QSlider::sliderMoved, this, [this](int value) { if (m_ready) m_player.seek(m_current.startMs + value); wake(); });
    connect(m_stepBack, &QPushButton::clicked, this, [this] { step(-1); wake(); }); connect(m_stepForward, &QPushButton::clicked, this, [this] { step(1); wake(); });
    connect(m_mute, &QPushButton::clicked, this, [this] { toggleMute(); wake(); });
    connect(m_volumeSlider, &QSlider::valueChanged, this, [this](int value) { setVolume(value); wake(); });
    // The names of the two step keys follow the keyboard, even when it is changed while the application runs.
    connect(QGuiApplication::inputMethod(), &QInputMethod::localeChanged, this, &ClipViewer::updateStepKeys);
    updateStepKeys(); applyVolume();
}
void ClipViewer::updateStepKeys() {
    const auto keys = stepKeys();
    m_stepBack->setToolTip(QStringLiteral("Image précédente (%1)").arg(keys.backLabel)); m_stepForward->setToolTip(QStringLiteral("Image suivante (%1)").arg(keys.forwardLabel));
}
void ClipViewer::setVolume(int percent) {
    percent = std::clamp(percent, 0, 100); const bool changed = percent != m_volume;
    m_volume = percent; m_muted = false; applyVolume(); if (changed) emit volumeChanged(percent);
}
// Muting keeps the level, so the sound comes back where it was. At zero there is nothing to come back to.
void ClipViewer::toggleMute() { if (m_volume == 0) { setVolume(50); return; } m_muted = !m_muted; applyVolume(); }
void ClipViewer::applyVolume() {
    const bool silent = m_muted || m_volume == 0; const double level = m_volume / 100.0;
    // Squared, so that equal steps of the slider sound like equal steps.
    m_player.setVolume(silent ? 0 : level * level);
    { QSignalBlocker blocker(m_volumeSlider); m_volumeSlider->setValue(m_volume); }
    m_mute->setIconName(silent ? QStringLiteral("muted") : QStringLiteral("speaker"));
    m_mute->setToolTip(silent ? QStringLiteral("Rétablir le son (M)") : QStringLiteral("Couper le son (M)")); m_mute->setAccessibleName(silent ? QStringLiteral("Rétablir le son") : QStringLiteral("Couper le son"));
    m_volumeSlider->setToolTip(QStringLiteral("Volume : %1 % (molette)").arg(m_volume));
}
ClipViewer::~ClipViewer() { disconnect(&m_player, nullptr, this, nullptr); keepAwake(false); m_player.close(); }
void ClipViewer::open(const QStringList& playlist, int index, std::optional<ClipEdit> edit, qint64 startMs) {
    if (playlist.isEmpty()) return;
    m_playlist = playlist; m_edit = std::move(edit); m_start = startMs; m_open = true;
    if (testAttribute(Qt::WA_DontShowOnScreen)) show();
    else {
        // The screen the application is on, whole: no frame, no title bar.
        auto* screen = m_owner ? m_owner->screen() : QGuiApplication::primaryScreen();
        if (screen) setGeometry(screen->geometry());
        showFullScreen();
    }
    raise(); activateWindow(); setFocus(); qApp->installEventFilter(this);
    load(std::clamp(index, 0, int(playlist.size()) - 1));
}
void ClipViewer::load(int index) {
    m_index = index; m_ready = false; m_image = {}; m_imageKey = 0; m_message.clear();
    const auto path = currentPath(); m_title->setFullText(QFileInfo(path).completeBaseName());
    m_count->setText(QStringLiteral("%1 sur %2").arg(index + 1).arg(m_playlist.size())); m_count->setVisible(m_playlist.size() > 1);
    m_previous->setVisible(m_playlist.size() > 1); m_next->setVisible(m_playlist.size() > 1); m_previous->setEnabled(index > 0); m_next->setEnabled(index + 1 < m_playlist.size());
    { QSignalBlocker blocker(m_seek); m_seek->setRange(0, 0); } m_time->setText(clock(0)); m_total->setText(clock(0));
    m_player.setPresentation(deviceSize()); m_player.load(path);
    update(); wake();
}
// The edit and the starting point belong to the clip the viewer was opened on, not to its neighbours.
void ClipViewer::jump(int index) { if (index < 0 || index >= m_playlist.size() || index == m_index) return; m_edit.reset(); m_start = -1; load(index); }
void ClipViewer::finish() {
    if (!m_open) return;
    m_open = false; const auto path = currentPath(); const auto position = m_player.snapshot().positionMs;
    qApp->removeEventFilter(this); m_hide.stop(); keepAwake(false); m_player.close(); m_image = {}; m_index = -1; hide(); setCursor(Qt::ArrowCursor);
    emit closed(path, position);
}
void ClipViewer::updatePlayer() {
    if (!m_open) return;
    const auto state = m_player.snapshot();
    if (!state.error.isEmpty()) { if (m_message != state.error) { m_message = state.error; m_image = {}; update(); setControlsVisible(true); } return; }
    if (state.loaded && !m_ready) {
        m_ready = true; m_current = m_edit && validateEdit(state.info, *m_edit).isEmpty() ? *m_edit : defaultEdit(state.info);
        m_player.setEdit(m_current); if (m_start > m_current.startMs) m_player.seek(m_start);
        { QSignalBlocker blocker(m_seek); m_seek->setRange(0, int(std::max<qint64>(1, m_current.endMs - m_current.startMs))); }
        m_total->setText(clock(m_current.endMs - m_current.startMs)); m_player.setPlaying(true); emit viewed(currentPath()); return;
    }
    if (!state.image.isNull() && state.image.cacheKey() != m_imageKey) { m_image = state.image; m_imageKey = state.image.cacheKey(); update(); }
    if (!m_ready) return;
    keepAwake(state.playing);
    const int position = int(std::clamp<qint64>(state.positionMs - m_current.startMs, 0, m_seek->maximum()));
    if (!m_seek->isSliderDown() && m_seek->value() != position) { QSignalBlocker blocker(m_seek); m_seek->setValue(position); }
    if (const auto text = clock(position); m_time->text() != text) m_time->setText(text);
    if (const auto text = state.playing ? QStringLiteral("Pause") : QStringLiteral("Lire"); m_play->text() != text) { m_play->setText(text); m_play->setIconName(state.playing ? QStringLiteral("pause") : QStringLiteral("play")); }
    if (!state.playing && !state.loading && m_top->isHidden()) setControlsVisible(true);
}
// A clip that plays keeps the screen on; Windows gets its say back as soon as it stops.
void ClipViewer::keepAwake(bool awake) {
    if (m_awake == awake) return;
    m_awake = awake;
#ifdef Q_OS_WIN
    SetThreadExecutionState(awake ? ES_CONTINUOUS | ES_DISPLAY_REQUIRED : ES_CONTINUOUS);
#endif
}
void ClipViewer::wake() { setControlsVisible(true); m_hide.start(); }
void ClipViewer::setControlsVisible(bool visible) {
    if (m_top->isVisible() == visible && m_bottom->isVisible() == visible) return;
    m_top->setVisible(visible); m_bottom->setVisible(visible); setCursor(visible ? Qt::ArrowCursor : Qt::BlankCursor);
}
void ClipViewer::step(int frames) {
    const auto state = m_player.snapshot(); if (!m_ready || !state.loaded) return;
    if (state.playing) m_player.setPlaying(false);
    m_player.seek(qRound64(state.positionMs + frames * 1000.0 / std::max(1.0, state.info.fps)));
}
QSize ClipViewer::deviceSize() const { const auto ratio = devicePixelRatioF(); return {qRound(width() * ratio), qRound(height() * ratio)}; }
void ClipViewer::resizeEvent(QResizeEvent*) {
    m_top->setGeometry(0, 0, width(), 56); m_bottom->setGeometry(0, height() - 64, width(), 64);
    if (m_open && m_index >= 0) m_player.setPresentation(deviceSize());
}
// Moved to a screen with another scale: the images are made again for its pixels.
bool ClipViewer::event(QEvent* event) {
    if (event->type() == QEvent::DevicePixelRatioChange && m_open && m_index >= 0) { m_player.setPresentation(deviceSize()); update(); }
    return QWidget::event(event);
}
void ClipViewer::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    if (m_image.isNull()) {
        painter.fillRect(rect(), Qt::black); painter.setPen(m_message.isEmpty() ? tk::text3 : tk::danger); painter.setFont(uiFont(14));
        painter.drawText(rect().adjusted(40, 40, -40, -40), Qt::AlignCenter | Qt::TextWordWrap, m_message.isEmpty() ? QStringLiteral("Chargement du clip…") : m_message); return;
    }
    // Drawn in device pixels, one for one: the image was made for this screen.
    const auto device = deviceSize(); const QSize shown = m_image.width() > device.width() || m_image.height() > device.height() ? m_image.size().scaled(device, Qt::KeepAspectRatio) : m_image.size();
    const QRect picture(QPoint((device.width() - shown.width()) / 2, (device.height() - shown.height()) / 2), shown);
    painter.scale(1 / devicePixelRatioF(), 1 / devicePixelRatioF());
    if (picture.left() > 0) { painter.fillRect(0, 0, picture.left(), device.height(), Qt::black); painter.fillRect(picture.right() + 1, 0, device.width() - picture.right() - 1, device.height(), Qt::black); }
    if (picture.top() > 0) { painter.fillRect(0, 0, device.width(), picture.top(), Qt::black); painter.fillRect(0, picture.bottom() + 1, device.width(), device.height() - picture.bottom() - 1, Qt::black); }
    // Only while the window changes size is an image not yet the size it is shown at.
    if (shown == m_image.size()) painter.drawImage(picture.topLeft(), m_image); else { painter.setRenderHint(QPainter::SmoothPixmapTransform); painter.drawImage(picture, m_image); }
}
void ClipViewer::keyPressEvent(QKeyEvent* event) {
    const auto state = m_player.snapshot(); wake();
    if (const int frames = frameStep(event)) { step(frames); return; }
    switch (event->key()) {
    case Qt::Key_Escape: case Qt::Key_F: case Qt::Key_F11: finish(); break;
    case Qt::Key_Space: case Qt::Key_K: if (m_ready) m_player.setPlaying(!state.playing); break;
    case Qt::Key_Left: if (m_ready) m_player.seek(state.positionMs - 5000); break;
    case Qt::Key_Right: if (m_ready) m_player.seek(state.positionMs + 5000); break;
    case Qt::Key_M: toggleMute(); break;
    case Qt::Key_Home: if (m_ready) m_player.seek(m_current.startMs); break;
    case Qt::Key_PageUp: case Qt::Key_Up: jump(m_index - 1); break;
    case Qt::Key_PageDown: case Qt::Key_Down: jump(m_index + 1); break;
    default: QWidget::keyPressEvent(event);
    }
}
// A click on the picture plays or pauses; on the bars it belongs to their controls.
void ClipViewer::mousePressEvent(QMouseEvent* event) { wake(); if (event->button() == Qt::LeftButton && m_ready && !childAt(event->position().toPoint())) m_player.setPlaying(!m_player.snapshot().playing); }
void ClipViewer::mouseDoubleClickEvent(QMouseEvent* event) { if (event->button() == Qt::LeftButton && !childAt(event->position().toPoint())) finish(); }
// The wheel sets the volume wherever the pointer is: up for louder, down for quieter, five points a notch.
void ClipViewer::wheelEvent(QWheelEvent* event) {
    m_wheel += event->angleDelta().y(); const int notches = m_wheel / 120; m_wheel -= notches * 120; event->accept(); wake();
    if (!notches) return;
    // Turned down while muted, the level drops and the sound stays off.
    const bool stayMuted = m_muted && notches < 0; setVolume(m_volume + notches * 5);
    if (stayMuted && m_volume > 0) { m_muted = true; applyVolume(); }
}
void ClipViewer::closeEvent(QCloseEvent* event) { finish(); event->accept(); }
bool ClipViewer::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::MouseMove && m_open) { auto* widget = qobject_cast<QWidget*>(watched); if (widget && widget->window() == this) wake(); }
    return QWidget::eventFilter(watched, event);
}
}
