#include "compare_view.h"
#include "widgets.h"
#include "core/clip_library.h"
#include <QBoxLayout>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QSignalBlocker>
#include <QSlider>
#include <QStyle>
#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <thread>
namespace clipsapp {
namespace {
QString clock(qint64 ms) { const auto seconds = std::max<qint64>(0, ms) / 1000; return QString("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0')); }
// A click anywhere on the bar goes there at once.
class SeekBar final : public QSlider {
public:
    SeekBar() : QSlider(Qt::Horizontal) { setCursor(Qt::PointingHandCursor); }
protected:
    void mousePressEvent(QMouseEvent* event) override { if (event->button() != Qt::LeftButton) return; setSliderDown(true); follow(event); }
    void mouseMoveEvent(QMouseEvent* event) override { if (isSliderDown()) follow(event); }
    void mouseReleaseEvent(QMouseEvent* event) override { if (event->button() == Qt::LeftButton) setSliderDown(false); }
private:
    void follow(QMouseEvent* event) {
        const int value = QStyle::sliderValueFromPosition(minimum(), maximum(), int(event->position().x()) - 7, std::max(1, width() - 14));
        if (value != sliderPosition()) { setSliderPosition(value); emit sliderMoved(value); }
    }
};
}
// The compressed side: a reader of its own, on a thread of its own, asked for the moment the original
// shows. Played, it reads forward from where it is, which costs one image at a time; only a jump or
// another scale makes it seek. Each image says what it was asked for, so that it is only shown beside
// the original's image of the same moment, at the same scale.
class CompareView::Follower {
public:
    struct Frame { QImage image; qint64 frameMs = -1, position = -1; quint64 generation = 0; QString error; };
    Follower(QString path, std::function<void()> ready) : m_path(std::move(path)), m_ready(std::move(ready)) { m_thread = std::thread([this] { run(); }); }
    ~Follower() { { std::lock_guard lock(m_mutex); m_stop = true; } m_cancel = true; m_wake.notify_all(); m_thread.join(); }
    void want(qint64 position, const FrameOutput& output, quint64 generation) {
        {
            std::lock_guard lock(m_mutex);
            if (m_request.valid && m_request.position == position && m_request.generation == generation) return;
            // A jump back, a jump ahead or another scale: the reading under way is of no use any more.
            if (m_request.valid && (m_request.generation != generation || position < m_request.position || position > m_request.position + 1500)) m_cancel = true;
            m_request = {true, position, output, generation};
        }
        m_wake.notify_all();
    }
    Frame frame() const { std::lock_guard lock(m_mutex); return m_frame; }
private:
    struct Request { bool valid = false; qint64 position = 0; FrameOutput output; quint64 generation = 0; };
    void run() {
        std::unique_ptr<ClipReader> reader;
        try { reader = std::make_unique<ClipReader>(m_path, true); }
        catch (const std::exception& error) { { std::lock_guard lock(m_mutex); m_frame.error = QString::fromUtf8(error.what()); } m_ready(); return; }
        FrameOutput current; bool seek = true; qint64 read = -1; Request done;
        for (;;) {
            Request request;
            {
                std::unique_lock lock(m_mutex);
                m_wake.wait(lock, [&] { return m_stop || (m_request.valid && (m_request.position != done.position || m_request.generation != done.generation || !done.valid)); });
                if (m_stop) return;
                request = m_request; m_cancel = false;
            }
            try {
                if (!(request.output == current)) { reader->setFrameOutput(request.output); current = request.output; seek = true; }
                if (seek || request.position < read || request.position > read + 1500) { reader->seek(request.position); seek = false; }
                reader->readUntil(request.position + 1, m_cancel, true, -1, request.position);
                // Called off: the next request starts from a seek.
                if (m_cancel) { seek = true; continue; }
                read = request.position; const auto image = reader->frameAt(request.position);
                // Asked for a moment before its first image (many clips start a frame or three late): that image stands for it.
                const qint64 shown = std::min(reader->frameTime(), request.position);
                { std::lock_guard lock(m_mutex); m_frame = {image, shown, request.position, request.generation, {}}; }
            } catch (const std::exception& error) { std::lock_guard lock(m_mutex); m_frame.error = QString::fromUtf8(error.what()); seek = true; }
            done = request; m_ready();
        }
    }
    QString m_path; std::function<void()> m_ready;
    mutable std::mutex m_mutex; std::condition_variable m_wake; std::atomic_bool m_cancel{false}; bool m_stop = false;
    Request m_request; Frame m_frame; std::thread m_thread;
};
CompareView::CompareView(QWidget* parent) : QWidget(parent), m_before(this, true) {
    setFocusPolicy(Qt::StrongFocus);
    auto* layout = new QVBoxLayout(this); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(10);
    m_picture = new QWidget; m_picture->setObjectName("compare-picture"); m_picture->setMinimumSize(480, 270); m_picture->setMouseTracking(true); m_picture->setCursor(Qt::SplitHCursor);
    m_picture->setAttribute(Qt::WA_OpaquePaintEvent); m_picture->installEventFilter(this); layout->addWidget(m_picture, 1);
    auto* row = new QHBoxLayout; row->setSpacing(10);
    m_play = new Button(QStringLiteral("play"), QStringLiteral("Lire")); m_play->setObjectName("compare-play"); m_play->setFixedWidth(92); row->addWidget(m_play);
    m_time = monoLabel(QStringLiteral("0:00"), 13, tk::text, QFont::DemiBold); row->addWidget(m_time);
    m_seek = new SeekBar; m_seek->setObjectName("compare-seek"); row->addWidget(m_seek, 1);
    m_total = monoLabel(QStringLiteral("0:00"), 12, tk::text2); row->addWidget(m_total); row->addSpacing(6);
    m_mute = new Button(QStringLiteral("speaker"), QString(), Button::Ghost); m_mute->setObjectName("compare-mute"); row->addWidget(m_mute);
    m_volumeSlider = new QSlider(Qt::Horizontal); m_volumeSlider->setObjectName("compare-volume"); m_volumeSlider->setRange(0, 100); m_volumeSlider->setFixedWidth(88); m_volumeSlider->setAccessibleName(QStringLiteral("Volume")); row->addWidget(m_volumeSlider); row->addSpacing(6);
    m_zoom = new Segmented; m_zoom->setObjectName("compare-zoom"); for (const auto& name : {QStringLiteral("Original"), QStringLiteral("100 %"), QStringLiteral("200 %")}) m_zoom->addOption(name);
    m_zoom->setToolTip(QStringLiteral("<qt>« Original » montre l'image entière. À 100 % et 200 %, l'image est montrée dans ses propres pixels : c'est là qu'une perte se verrait. Clic droit glissé pour se déplacer dans l'image.</qt>")); row->addWidget(m_zoom);
    m_expand = new Button(QStringLiteral("expand"), QString(), Button::Secondary); m_expand->setObjectName("compare-full-screen"); m_expand->setToolTip(QStringLiteral("Comparer en plein écran (double clic sur l'image)")); m_expand->setAccessibleName(m_expand->toolTip()); row->addWidget(m_expand);
    layout->addLayout(row);
    m_resize.setSingleShot(true); m_resize.setInterval(60); m_pan.setSingleShot(true); m_pan.setInterval(30);
    connect(&m_resize, &QTimer::timeout, this, [this] { present(); });
    connect(&m_pan, &QTimer::timeout, this, [this] { reframe(); });
    connect(&m_before, &ClipPlayer::changed, this, [this] { refresh(); });
    connect(m_play, &QPushButton::clicked, this, [this] { setPlaying(!playing()); });
    connect(m_seek, &QSlider::sliderMoved, this, [this](int value) { m_before.seek(value); });
    connect(m_zoom, &Segmented::currentChanged, this, [this] { reframe(); });
    connect(m_volumeSlider, &QSlider::valueChanged, this, [this](int value) { setVolume(value); emit volumeChanged(value); });
    connect(m_mute, &QPushButton::clicked, this, [this] { m_volumeSlider->setValue(m_volume > 0 ? 0 : 50); });
    connect(m_expand, &QPushButton::clicked, this, [this] { setFullScreen(!fullScreen()); });
    setVolume(100);
}
CompareView::~CompareView() { disconnect(&m_before, nullptr, this, nullptr); close(); }
QSize CompareView::deviceSize() const { const auto ratio = m_picture->devicePixelRatioF(); return {std::max(2, qRound(m_picture->width() * ratio)), std::max(2, qRound(m_picture->height() * ratio))}; }
void CompareView::open(const QString& before, const QString& after, qint64 beforeBytes, qint64 afterBytes) {
    close();
    m_beforeBytes = beforeBytes; m_afterBytes = afterBytes; m_focus = {-1, -1}; m_shownBefore = {}; m_shownAfter = {}; m_framed = false;
    // Both sides start on the whole picture at the size of the view; the scale chosen follows once the original is read.
    m_output = {}; m_output.box = deviceSize(); m_output.fit = true; ++m_generation; m_staleKey = 0;
    m_before.setPresentation(m_output.box); m_before.load(before);
    QPointer<CompareView> self(this);
    m_after = std::make_unique<Follower>(after, [self] { QMetaObject::invokeMethod(self, [self] { if (self) self->refresh(); }, Qt::QueuedConnection); });
    update();
}
void CompareView::close() { setFullScreen(false); m_before.close(); m_after.reset(); }
void CompareView::setSplit(double split) { m_split = std::clamp(split, 0.0, 1.0); m_picture->update(); }
bool CompareView::playing() const { return m_before.snapshot().playing; }
void CompareView::setPlaying(bool playing) { m_before.setPlaying(playing); }
void CompareView::setVolume(int percent) {
    m_volume = std::clamp(percent, 0, 100); const double level = m_volume / 100.0; m_before.setVolume(level * level);
    { QSignalBlocker blocker(m_volumeSlider); m_volumeSlider->setValue(m_volume); }
    m_mute->setIconName(m_volume == 0 ? QStringLiteral("muted") : QStringLiteral("speaker")); m_mute->setToolTip(m_volume == 0 ? QStringLiteral("Rétablir le son") : QStringLiteral("Couper le son"));
    m_volumeSlider->setToolTip(QStringLiteral("Volume : %1 %").arg(m_volume));
}
void CompareView::present() { reframe(); }
void CompareView::reframe() {
    const auto info = m_before.snapshot().info; if (info.size.isEmpty()) return;
    const int level = m_zoom->currentIndex(); FrameOutput output; output.box = deviceSize(); output.fit = true; auto edit = defaultEdit(info);
    if (level > 0) {
        // The part of the picture that fills the view at this scale, around the place looked at.
        const double factor = level == 2 ? 2.0 : 1.0;
        const QSize part(std::min(info.size.width(), std::max(16, int(output.box.width() / factor)) & ~1), std::min(info.size.height(), std::max(16, int(output.box.height() / factor)) & ~1));
        if (m_focus.x() < 0) m_focus = QPointF(info.size.width() / 2.0, info.size.height() / 2.0);
        m_focus.setX(std::clamp(m_focus.x(), part.width() / 2.0, info.size.width() - part.width() / 2.0)); m_focus.setY(std::clamp(m_focus.y(), part.height() / 2.0, info.size.height() - part.height() / 2.0));
        const auto crop = boundedCrop(QRect(QPoint(int(m_focus.x() - part.width() / 2.0) & ~1, int(m_focus.y() - part.height() / 2.0) & ~1), part), info.size);
        // The whole picture is no crop at all, for both sides alike.
        if (crop != QRect(QPoint(), info.size)) { edit.crop = crop; output.crop = crop; }
    }
    if (output == m_output) return;
    // The picture on screen stays until both sides have the new one: never one zoomed beside the other.
    m_output = output; ++m_generation; m_staleKey = m_before.snapshot().image.cacheKey();
    m_before.setPresentation(output.box); m_before.setEdit(edit); refresh();
}
void CompareView::refresh() {
    const auto state = m_before.snapshot();
    if (state.loaded && !m_framed) { m_framed = true; { QSignalBlocker blocker(m_seek); m_seek->setRange(0, int(std::max<qint64>(1, state.info.durationMs - 1))); } m_total->setText(clock(state.info.durationMs)); reframe(); }
    if (!m_seek->isSliderDown()) { QSignalBlocker blocker(m_seek); m_seek->setValue(int(state.positionMs)); }
    if (const auto text = clock(state.positionMs); m_time->text() != text) m_time->setText(text);
    if (const auto text = state.playing ? QStringLiteral("Pause") : QStringLiteral("Lire"); m_play->text() != text) { m_play->setText(text); m_play->setIconName(state.playing ? QStringLiteral("pause") : QStringLiteral("play")); }
    if (!m_after || !state.loaded) { m_picture->update(); return; }
    m_after->want(state.positionMs, m_output, m_generation);
    // The two images go on screen together, and only when they show the same moment at the same scale.
    const auto frame = m_after->frame(); const double duration = 1000.0 / std::max(1.0, state.info.fps);
    const bool leaderReady = !state.loading && !state.image.isNull() && (m_staleKey == 0 || state.image.cacheKey() != m_staleKey);
    const bool sameMoment = frame.frameMs >= 0 && frame.frameMs <= state.positionMs + 1 && state.positionMs < frame.frameMs + duration + 1;
    if (leaderReady && frame.generation == m_generation && !frame.image.isNull() && frame.image.size() == state.image.size() && sameMoment) {
        if (state.image.cacheKey() != m_shownBefore.cacheKey() || frame.image.cacheKey() != m_shownAfter.cacheKey()) { m_shownBefore = state.image; m_shownAfter = frame.image; m_staleKey = 0; m_picture->update(); }
    } else if (!frame.error.isEmpty() || !state.error.isEmpty()) m_picture->update();
}
void CompareView::setFullScreen(bool full) {
    if (full == fullScreen()) return;
    if (full) {
        auto* screen = this->window()->screen();
        m_homeParent = parentWidget(); m_home = parentWidget() ? qobject_cast<QBoxLayout*>(parentWidget()->layout()) : nullptr; m_homeIndex = m_home ? m_home->indexOf(this) : -1;
        // A window of its own, above the dialog it comes from (never inside the view it is about to hold).
        auto* window = new QWidget(parentWidget() ? parentWidget()->window() : nullptr, Qt::Window | Qt::FramelessWindowHint); m_window = window; window->setObjectName("compare-window"); window->setAttribute(Qt::WA_StyledBackground); followVisibility(window, this);
        setThemedStyleSheet(window, QStringLiteral("QWidget#compare-window { background: {window}; }"));
        auto* layout = new QVBoxLayout(window); layout->setContentsMargins(16, 10, 16, 14); layout->setSpacing(10);
        auto* bar = new QHBoxLayout; bar->setSpacing(12); bar->addWidget(label(QStringLiteral("Avant · après"), "strong"));
        bar->addWidget(label(QStringLiteral("Glissez la séparation sur l'image : à gauche l'original, à droite la version compressée."), "small"), 1);
        bar->addWidget(label(QStringLiteral("Échap pour quitter"), "small3"));
        auto* leave = new Button(QStringLiteral("x"), QString(), Button::Ghost); leave->setObjectName("compare-leave"); leave->setToolTip(QStringLiteral("Quitter le plein écran (Échap)")); leave->setAccessibleName(leave->toolTip()); bar->addWidget(leave);
        layout->addLayout(bar); layout->addWidget(this, 1);
        connect(leave, &QPushButton::clicked, this, [this] { setFullScreen(false); });
        window->installEventFilter(this);
        // The screen the comparison was on, whole.
        if (screen) window->setGeometry(screen->geometry());
        if (window->testAttribute(Qt::WA_DontShowOnScreen)) window->show(); else window->showFullScreen();
        show(); m_expand->setIconName(QStringLiteral("compress")); m_expand->setToolTip(QStringLiteral("Quitter le plein écran (Échap)"));
    } else {
        auto* window = m_window.data(); m_window = nullptr;
        if (m_home && m_homeParent) m_home->insertWidget(std::max(0, m_homeIndex), this, 1); else setParent(m_homeParent);
        show(); window->hide(); window->deleteLater(); m_home = nullptr;
        m_expand->setIconName(QStringLiteral("expand")); m_expand->setToolTip(QStringLiteral("Comparer en plein écran (double clic sur l'image)"));
    }
    setFocus(); m_resize.start();
}
void CompareView::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape && fullScreen()) { setFullScreen(false); return; }
    if (event->key() == Qt::Key_Space) { setPlaying(!playing()); return; }
    if (event->key() == Qt::Key_Left || event->key() == Qt::Key_Right) { setSplit(m_split + (event->key() == Qt::Key_Left ? -0.05 : 0.05)); return; }
    QWidget::keyPressEvent(event);
}
bool CompareView::eventFilter(QObject* watched, QEvent* event) {
    // Closed by the system on the whole screen: back into the dialog rather than gone.
    if (watched == m_window && event->type() == QEvent::Close) { event->ignore(); QMetaObject::invokeMethod(this, [this] { setFullScreen(false); }, Qt::QueuedConnection); return true; }
    if (watched != m_picture) return QWidget::eventFilter(watched, event);
    switch (event->type()) {
    case QEvent::Paint: { QPainter painter(m_picture); paintPicture(painter); return true; }
    case QEvent::Resize: m_resize.start(); break;
    case QEvent::MouseButtonDblClick: if (static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton) { setFullScreen(!fullScreen()); return true; } break;
    case QEvent::MouseButtonPress: {
        auto* mouse = static_cast<QMouseEvent*>(event); setFocus(Qt::MouseFocusReason);
        if (mouse->button() == Qt::LeftButton) { m_dragging = true; setSplit(mouse->position().x() / std::max(1, m_picture->width())); }
        else if (mouse->button() == Qt::RightButton && m_zoom->currentIndex() > 0) { m_panning = true; m_panFrom = mouse->position().toPoint(); m_picture->setCursor(Qt::ClosedHandCursor); }
        return true;
    }
    case QEvent::MouseMove: {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (m_dragging) setSplit(mouse->position().x() / std::max(1, m_picture->width()));
        else if (m_panning) {
            const auto delta = mouse->position().toPoint() - m_panFrom; m_panFrom = mouse->position().toPoint();
            // One pixel of the view is half a pixel of the clip at 200 %, one at 100 %.
            const double scale = m_picture->devicePixelRatioF() / (m_zoom->currentIndex() == 2 ? 2.0 : 1.0);
            m_focus -= QPointF(delta.x() * scale, delta.y() * scale); if (!m_pan.isActive()) m_pan.start();
        }
        return true;
    }
    case QEvent::MouseButtonRelease: m_dragging = false; if (m_panning) { m_panning = false; m_picture->setCursor(Qt::SplitHCursor); reframe(); } return true;
    default: break;
    }
    return QWidget::eventFilter(watched, event);
}
void CompareView::paintPicture(QPainter& painter) {
    painter.fillRect(m_picture->rect(), Qt::black); painter.setRenderHint(QPainter::Antialiasing);
    if (m_shownBefore.isNull() || m_shownAfter.isNull()) {
        auto error = m_before.snapshot().error; if (error.isEmpty() && m_after) error = m_after->frame().error;
        painter.setPen(error.isEmpty() ? tk::text3 : tk::danger); painter.setFont(uiFont(13)); painter.drawText(m_picture->rect().adjusted(24, 24, -24, -24), Qt::AlignCenter | Qt::TextWordWrap, error.isEmpty() ? QStringLiteral("Chargement des deux versions…") : error);
        return;
    }
    // Both images were made at the size of the view, in its pixels: they are laid one over the other.
    const double ratio = m_picture->devicePixelRatioF(); const QSizeF shown = QSizeF(m_shownBefore.size()) / ratio;
    const QRectF picture((m_picture->width() - shown.width()) / 2, (m_picture->height() - shown.height()) / 2, shown.width(), shown.height());
    const double x = std::clamp(m_split * m_picture->width(), picture.left(), picture.right());
    painter.save(); painter.setClipRect(QRectF(picture.left(), picture.top(), x - picture.left(), picture.height())); painter.drawImage(picture, m_shownBefore); painter.restore();
    painter.save(); painter.setClipRect(QRectF(x, picture.top(), picture.right() - x, picture.height())); painter.drawImage(picture, m_shownAfter); painter.restore();
    // The separation and its handle, in the colors of the theme.
    painter.setPen(Qt::NoPen); painter.setBrush(tk::accent); painter.drawRect(QRectF(x - 1, picture.top(), 2, picture.height()));
    const QRectF handle(x - 17, picture.center().y() - 17, 34, 34); painter.setBrush(accentBrush(handle)); painter.drawEllipse(handle);
    paintIcon(painter, QStringLiteral("prev"), QRectF(handle.left() + 2, handle.center().y() - 6, 12, 12), tk::onAccent); paintIcon(painter, QStringLiteral("next"), QRectF(handle.right() - 14, handle.center().y() - 6, 12, 12), tk::onAccent);
    const auto beforeText = QStringLiteral("Avant · %1").arg(formatBytes(m_beforeBytes));
    const auto afterText = m_beforeBytes > 0 ? QStringLiteral("Après · %1 · −%2 %").arg(formatBytes(m_afterBytes)).arg(qRound(100.0 * (m_beforeBytes - m_afterBytes) / m_beforeBytes)) : QStringLiteral("Après · %1").arg(formatBytes(m_afterBytes));
    const auto left = Tag::measure(beforeText), right = Tag::measure(afterText);
    Tag::paint(painter, QRectF(picture.left() + 10, picture.top() + 10, left.width(), left.height()), beforeText, Tag::Dark);
    Tag::paint(painter, QRectF(picture.right() - 10 - right.width(), picture.top() + 10, right.width(), right.height()), afterText, Tag::Dark);
}
}
