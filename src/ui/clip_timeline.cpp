#include "clip_timeline.h"
#include "widgets.h"
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QSlider>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
namespace clipsapp {
namespace {
QString timeLabel(qint64 ms) { return QString("%1:%2").arg(ms / 60000).arg(ms / 1000 % 60, 2, 10, QLatin1Char('0')); }
class Swatch final : public QWidget {
public:
    explicit Swatch(const QColor& color) : m_color(color) { setFixedSize(10, 10); }
protected:
    void paintEvent(QPaintEvent*) override { QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing); painter.setPen(Qt::NoPen); painter.setBrush(m_color); painter.drawRoundedRect(rect(), 3, 3); }
private:
    QColor m_color;
};
}
ClipTimeline::ClipTimeline(QWidget* parent) : QWidget(parent), m_headers(new QWidget(this)) { setMouseTracking(true); setMinimumWidth(420); setObjectName("clip-timeline"); }
void ClipTimeline::setClip(const ClipInfo& info, const ClipEdit& edit) {
    m_info = info; m_edit = edit; m_analysis = {}; m_position = 0; m_zoom = 1; m_viewStart = 0; m_scroll = 0;
    rebuildRows(); emit viewChanged();
}
void ClipTimeline::setCompact(bool compact) { if (compact == m_compact) return; m_compact = compact; rebuildRows(); }
int ClipTimeline::preferredHeight() const {
    const auto m = metrics(); const int tracks = int(m_edit.tracks.size());
    return m.ruler + m.video + (m_compact ? std::min(tracks * m.track, 110) : std::min(tracks, 3) * m.track);
}
int ClipTimeline::scrollLimit() const { const auto m = metrics(); return std::max(0, int(m_edit.tracks.size()) * m.track - (height() - m.ruler - m.video)); }
void ClipTimeline::rebuildRows() {
    // The sliders are read by the editor after this call; rows from the previous clip must be gone by then.
    for (auto* row : m_rows) { row->hide(); row->deleteLater(); }
    m_rows.clear(); m_volumes.clear(); m_mutes.clear();
    const auto& edit = m_edit;
    for (int track = 0; track < edit.tracks.size(); ++track) {
        const auto name = edit.tracks[track].name;
        auto* row = new QWidget(m_headers); row->setObjectName("timelineTrack"); auto* layout = new QVBoxLayout(row); layout->setContentsMargins(12, 0, 10, 0); layout->setSpacing(m_compact ? 0 : 4); layout->addStretch();
        auto* heading = new QHBoxLayout; heading->setSpacing(8); heading->addWidget(new Swatch(tk::track(track)));
        auto* title = new ElideLabel(name); title->setFont(uiFont(12, QFont::DemiBold)); heading->addWidget(title, 1);
        auto* mute = new Button(QStringLiteral("speaker"), QString(), Button::Tool); mute->setObjectName(QString("track-mute-%1").arg(track)); mute->setChecked(edit.tracks[track].muted); heading->addSpacing(-6); heading->addWidget(mute);
        auto* remove = new Button(QStringLiteral("x"), QString(), Button::Tool); remove->setCheckable(false); remove->setObjectName(QString("track-remove-%1").arg(track)); remove->setToolTip(QStringLiteral("Retirer la piste %1 du clip modifié").arg(name)); remove->setAccessibleName(QStringLiteral("Retirer la piste ") + name);
        heading->addSpacing(-6); heading->addWidget(remove); layout->addLayout(heading);
        auto* level = new QHBoxLayout; level->setSpacing(8); auto* volume = new QSlider(Qt::Horizontal); volume->setObjectName(QString("track-volume-%1").arg(track)); volume->setRange(0, 200); volume->setPageStep(10); volume->setValue(qRound(edit.tracks[track].volume * 100));
        volume->setAccessibleName(QStringLiteral("Volume de la piste ") + name); level->addWidget(volume, 1);
        auto* percent = monoLabel(QString("%1 %").arg(volume->value()), 11, tk::text2); percent->setFixedWidth(40); percent->setAlignment(Qt::AlignRight | Qt::AlignVCenter); level->addWidget(percent); layout->addLayout(level); layout->addStretch();
        const auto reflect = [mute, title, name](bool muted) {
            mute->setIconName(muted ? QStringLiteral("muted") : QStringLiteral("speaker")); mute->setToolTip((muted ? QStringLiteral("Rétablir le son de ") : QStringLiteral("Couper le son de ")) + name); mute->setAccessibleName(mute->toolTip());
            setThemedStyleSheet(title, muted ? QStringLiteral("color: {muted};") : QString());
        };
        reflect(mute->isChecked());
        connect(remove, &QPushButton::clicked, this, [this, stream = edit.tracks[track].stream] { emit removeTrackRequested(stream); });
        connect(volume, &QSlider::valueChanged, this, [this, volume, percent](int value) {
            // A notch at 100 %: the handle settles there when dragged close by.
            if (volume->isSliderDown() && value != 100 && std::abs(value - 100) <= 3) { volume->setValue(100); return; }
            percent->setText(QString("%1 %").arg(value)); emit audioChanged();
        });
        connect(mute, &QPushButton::toggled, this, [this, reflect](bool muted) { reflect(muted); emit audioChanged(); });
        m_rows.append(row); m_volumes.append(volume); m_mutes.append(mute);
        row->show();
    }
    m_scroll = std::min(m_scroll, scrollLimit()); placeRows(); m_backgroundDirty = true; update();
}
void ClipTimeline::placeRows() {
    const auto m = metrics(); m_headers->setGeometry(0, m.ruler + m.video, m.header - 1, std::max(0, height() - m.ruler - m.video));
    for (int row = 0; row < m_rows.size(); ++row) m_rows[row]->setGeometry(0, row * m.track - m_scroll + 1, m.header - 1, m.track - 1);
}
void ClipTimeline::setEdit(const ClipEdit& edit) {
    bool rowsChanged = m_edit.tracks.size() != edit.tracks.size();
    if (!rowsChanged) for (int i = 0; i < edit.tracks.size(); ++i) rowsChanged |= m_edit.tracks[i].stream != edit.tracks[i].stream;
    m_edit = edit; if (rowsChanged) rebuildRows(); m_backgroundDirty = true; update();
}
void ClipTimeline::setAnalysis(ClipAnalysis analysis) { m_analysis = std::move(analysis); m_backgroundDirty = true; update(); }
void ClipTimeline::setPosition(qint64 position) { if (position == m_position) return; const int old = qRound(xAt(m_position)); m_position = position; const int now = qRound(xAt(position)); update(QRect(old - 8, 0, 17, height())); update(QRect(now - 8, 0, 17, height())); }
qint64 ClipTimeline::visibleDuration() const { return std::max<qint64>(1, qRound64(m_info.durationMs / m_zoom)); }
void ClipTimeline::setViewStart(qint64 start) { m_viewStart = std::clamp<qint64>(start, 0, std::max<qint64>(0, m_info.durationMs - visibleDuration())); m_backgroundDirty = true; update(); emit viewChanged(); }
void ClipTimeline::setZoom(double factor, qint64 anchor, double ratio) {
    if (m_info.durationMs <= 0) return;
    m_zoom = std::clamp(factor, 1.0, 128.0);
    setViewStart(qRound64((anchor < 0 ? m_position : anchor) - visibleDuration() * std::clamp(ratio, 0.0, 1.0)));
}
double ClipTimeline::xAt(qint64 time) const { return laneLeft() + (time - m_viewStart) / double(visibleDuration()) * laneSpan(); }
qint64 ClipTimeline::timeAt(double x) const { return std::clamp<qint64>(m_viewStart + qRound64(std::clamp((x - laneLeft()) / laneSpan(), 0.0, 1.0) * visibleDuration()), 0, m_info.durationMs); }
void ClipTimeline::resizeEvent(QResizeEvent*) { m_backgroundDirty = true; m_scroll = std::min(m_scroll, scrollLimit()); placeRows(); }
void ClipTimeline::paintEvent(QPaintEvent*) {
    if (m_backgroundDirty || m_background.size() != size() * devicePixelRatioF()) {
        m_background = QImage(size() * devicePixelRatioF(), QImage::Format_ARGB32_Premultiplied); m_background.setDevicePixelRatio(devicePixelRatioF());
        QPainter background(&m_background); paintContent(background); m_backgroundDirty = false;
    }
    QPainter painter(this); painter.drawImage(QPoint(), m_background); if (m_info.durationMs <= 0) return;
    const auto m = metrics(); painter.setClipRect(QRect(m.header, 0, width() - m.header, height())); painter.setRenderHint(QPainter::Antialiasing);
    const double playhead = std::round(xAt(m_position)); painter.fillRect(QRectF(playhead - 1, 0, 2, height()), tk::text);
    painter.setBrush(tk::text); painter.setPen(Qt::NoPen); painter.drawPolygon(QPolygonF{{playhead - 6, 0}, {playhead + 6, 0}, {playhead + 6, 6}, {playhead, 10}, {playhead - 6, 6}});
}
void ClipTimeline::paintContent(QPainter& painter) {
    const auto m = metrics(); const int left = laneLeft(), span = laneSpan(), tracksTop = m.ruler + m.video;
    painter.fillRect(rect(), tk::field); painter.fillRect(QRect(0, 0, m.header, height()), tk::header); painter.fillRect(QRect(m.header, 0, width() - m.header, m.ruler), tk::header);
    painter.fillRect(QRect(m.header - 1, 0, 1, height()), tk::line); painter.fillRect(QRect(0, m.ruler - 1, width(), 1), tk::line);
    auto capFont = uiFont(10, QFont::Bold); capFont.setLetterSpacing(QFont::PercentageSpacing, 107); painter.setFont(capFont); painter.setPen(tk::text3);
    painter.drawText(QRect(12, 0, m.header - 24, m.ruler - 1), Qt::AlignVCenter, QStringLiteral("PISTES"));
    paintIcon(painter, QStringLiteral("film"), QRectF(12, m.ruler + (m.video - 16) / 2.0, 16, 16), tk::text2);
    painter.setFont(uiFont(12, QFont::DemiBold)); painter.setPen(tk::text); painter.drawText(QRect(36, m.ruler, m.header - 48, m.video), Qt::AlignVCenter, QStringLiteral("Vidéo"));
    if (m_info.durationMs <= 0) return;
    painter.setClipRect(QRect(m.header, 0, width() - m.header, height()));
    const double start = xAt(m_edit.startMs), end = xAt(m_edit.endMs);
    // Ruler: a short tick per step, a taller one with its time every fifth step. The step follows the zoom.
    const qint64 intervals[]{1, 2, 5, 10, 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 15000, 30000, 60000, 120000, 300000, 600000, 3600000};
    qint64 interval = intervals[19]; for (auto candidate : intervals) if (candidate * double(span) / visibleDuration() >= 13) { interval = candidate; break; }
    painter.setFont(monoFont(10));
    for (qint64 time = (m_viewStart / interval) * interval; time <= std::min(m_info.durationMs, m_viewStart + visibleDuration() + interval); time += interval) {
        const int x = qRound(xAt(time)); const bool major = (time / interval) % 5 == 0;
        painter.fillRect(QRect(x, m.ruler - 1 - (major ? 9 : 5), 1, major ? 9 : 5), major ? tk::control : tk::lineHover);
        if (!major) continue;
        const auto text = interval < 200 ? QString::number(time / 1000.0, 'f', interval < 20 ? 2 : 1).replace('.', ',') + QStringLiteral(" s") : timeLabel(time);
        if (x + 4 + painter.fontMetrics().horizontalAdvance(text) > width() - 2) continue;
        painter.setPen(tk::text3); painter.drawText(QRect(x + 4, 0, 80, m.ruler - 6), Qt::AlignLeft | Qt::AlignBottom, text);
    }
    // Frame strip: 16:9 tiles separated by one pixel of background.
    painter.fillRect(QRect(m.header, m.ruler, width() - m.header, m.video), tk::window);
    if (!m_analysis.thumbnails.isEmpty()) {
        const int tile = qRound(m.video * 16 / 9.0); painter.save(); painter.setClipRect(QRect(left, m.ruler, span, m.video)); painter.setOpacity(0.9);
        for (int pixel = 0; pixel < span; pixel += tile) {
            const int image = std::min(int(m_analysis.thumbnails.size()) - 1, int(timeAt(left + pixel + tile / 2) * m_analysis.thumbnails.size() / m_info.durationMs));
            painter.drawImage(QRect(left + pixel, m.ruler, tile - 1, m.video), m_analysis.thumbnails[image]);
        }
        painter.restore();
    } else { painter.setFont(uiFont(12)); painter.setPen(tk::text3); painter.drawText(QRect(left, m.ruler, span, m.video), Qt::AlignCenter, QStringLiteral("Chargement des miniatures…")); }
    painter.save(); painter.setClipRect(QRect(m.header, tracksTop, width() - m.header, height() - tracksTop));
    for (int track = 0; track < m_edit.tracks.size(); ++track) {
        const int top = tracksTop + track * m.track - m_scroll; if (top >= height() || top + m.track <= tracksTop) continue;
        const bool mute = m_edit.tracks[track].muted; painter.fillRect(QRect(m.header, top, width() - m.header, 1), tk::raised);
        const QColor color = mute ? tk::lineHover : tk::track(track); const double middle = top + m.track / 2.0;
        const int analysisTrack = m_analysis.streams.isEmpty() ? track : int(m_analysis.streams.indexOf(m_edit.tracks[track].stream));
        if (analysisTrack >= 0 && analysisTrack < m_analysis.peaks.size() && !m_analysis.peaks[analysisTrack].isEmpty()) {
            const auto& peaks = m_analysis.peaks[analysisTrack];
            for (int pixel = 2; pixel < span - 1; pixel += 3) {
                const int first = std::min(int(peaks.size()) - 1, int(timeAt(left + pixel - 1) * peaks.size() / m_info.durationMs));
                const int last = std::min(int(peaks.size()), std::max(first + 1, int(timeAt(left + pixel + 2) * peaks.size() / m_info.durationMs))); float peak = 0;
                for (int bin = first; bin < last; ++bin) peak = std::max(peak, peaks[bin]);
                const double level = std::sqrt(std::min(1.0, peak * m_edit.tracks[track].volume)); const double half = std::max(0.6, level * (m.track / 2.0 - 4));
                painter.fillRect(QRectF(left + pixel - 1, middle - half, 2, half * 2), color);
            }
        } else painter.fillRect(QRectF(left, middle - 0.5, span, 1), color);
    }
    painter.restore();
    // Outside the selection: a dark veil and diagonal hatching, over the frames and every track.
    for (const QRectF& out : {QRectF(m.header, m.ruler, start - m.header, height() - m.ruler), QRectF(end, m.ruler, width() - end, height() - m.ruler)}) {
        if (out.width() <= 0) continue;
        auto veil = tk::mix(tk::window, QColor(Qt::black), 0.4), hatch = tk::text; veil.setAlpha(168); hatch.setAlpha(20);
        painter.fillRect(out, veil); painter.fillRect(out, QBrush(hatch, Qt::BDiagPattern));
    }
    painter.setRenderHint(QPainter::Antialiasing); painter.fillRect(QRectF(start, m.ruler, end - start, 2), tk::accent); painter.fillRect(QRectF(start, m.ruler + m.video - 2, end - start, 2), tk::accent);
    for (const bool leading : {true, false}) {
        const QRectF handle(leading ? start - 12 : end, m.ruler, 12, m.video); QPainterPath shape; shape.setFillRule(Qt::WindingFill);
        shape.addRoundedRect(handle, 4, 4); shape.addRect(leading ? handle.adjusted(6, 0, 0, 0) : handle.adjusted(0, 0, -6, 0)); painter.fillPath(shape, tk::accent);
        for (const double x : {handle.center().x() - 2.5, handle.center().x() + 1}) painter.fillRect(QRectF(x, handle.center().y() - 8, 1.5, 16), tk::onAccent);
    }
    if (const int limit = scrollLimit(); limit > 0) {
        const double area = height() - tracksTop, bar = std::max(16.0, area * area / (area + limit)); painter.setPen(Qt::NoPen); painter.setBrush(tk::lineStrong);
        painter.drawRoundedRect(QRectF(width() - 5, tracksTop + (area - bar) * m_scroll / limit, 3, bar), 1.5, 1.5);
    }
}
void ClipTimeline::mousePressEvent(QMouseEvent* event) {
    const auto m = metrics();
    if (!isEnabled() || m_info.durationMs <= 0 || event->button() != Qt::LeftButton || event->position().x() < m.header) return;
    const double x = event->position().x(), y = event->position().y(), start = xAt(m_edit.startMs), end = xAt(m_edit.endMs);
    // Each handle is 12 px wide and sits outside the selection; its grip reaches 4 px further on both sides.
    const bool onStart = x >= start - 16 && x <= start + 4, onEnd = x >= end - 4 && x <= end + 16;
    if (m_info.durationMs >= 50 && y >= m.ruler - 4 && y <= m.ruler + m.video + 4 && (onStart || onEnd)) {
        m_drag = onStart && onEnd ? (std::abs(x - start) <= std::abs(x - end) ? Drag::Start : Drag::End) : onStart ? Drag::Start : Drag::End;
        // Grabbing the body of a handle must not move the cut by itself.
        m_grab = m_drag == Drag::Start ? std::min(0.0, x - start) : std::max(0.0, x - end);
    } else { m_drag = Drag::Playhead; m_grab = 0; }
    drag(x);
}
void ClipTimeline::drag(double x) {
    const auto time = timeAt(x - m_grab);
    if (m_drag == Drag::Start) { m_edit.startMs = std::clamp<qint64>(time, 0, m_edit.endMs - 50); emit trimChanged(m_edit.startMs, m_edit.endMs); }
    else if (m_drag == Drag::End) { m_edit.endMs = std::clamp<qint64>(time, m_edit.startMs + 50, m_info.durationMs); emit trimChanged(m_edit.startMs, m_edit.endMs); }
    else if (m_drag == Drag::Playhead) { const auto position = std::clamp(time, m_edit.startMs, m_edit.endMs - 1); setPosition(position); emit seekRequested(position); }
    if (m_drag == Drag::Start || m_drag == Drag::End) m_backgroundDirty = true;
    update();
}
void ClipTimeline::mouseMoveEvent(QMouseEvent* event) {
    if (m_drag != Drag::None) { drag(event->position().x()); return; }
    const auto m = metrics(); const double x = event->position().x(), y = event->position().y(), start = xAt(m_edit.startMs), end = xAt(m_edit.endMs);
    setCursor(x >= m.header && y >= m.ruler - 4 && y <= m.ruler + m.video + 4 && ((x >= start - 16 && x <= start + 4) || (x >= end - 4 && x <= end + 16)) ? Qt::SizeHorCursor : Qt::ArrowCursor);
}
void ClipTimeline::mouseReleaseEvent(QMouseEvent* event) { if (event->button() == Qt::LeftButton && m_drag != Drag::None) { drag(event->position().x()); m_drag = Drag::None; } }
void ClipTimeline::wheelEvent(QWheelEvent* event) {
    if (event->modifiers() & Qt::ControlModifier) {
        const double ratio = std::clamp((event->position().x() - laneLeft()) / laneSpan(), 0.0, 1.0);
        setZoom(m_zoom * (event->angleDelta().y() > 0 ? 2 : 0.5), timeAt(event->position().x()), ratio); event->accept();
    } else if (event->modifiers() & Qt::ShiftModifier) { setViewStart(m_viewStart - event->angleDelta().y() * visibleDuration() / 480); event->accept(); }
    else if (scrollLimit() > 0) { m_scroll = std::clamp(m_scroll - event->angleDelta().y() / 4, 0, scrollLimit()); placeRows(); m_backgroundDirty = true; update(); event->accept(); }
    else QWidget::wheelEvent(event);
}
}
