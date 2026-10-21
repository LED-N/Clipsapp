#include "widgets.h"
#include "video_surface.h"
#include <QEvent>
#include <QFocusEvent>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace clipsapp {
namespace {
// The stylesheet colours a role; the widget's own font carries its size and weight,
// so that layouts measure the text with the font it is drawn with.
void applyRole(QLabel* target, const char* role) {
    struct RoleFont { const char* role; int pixels; QFont::Weight weight; };
    static const RoleFont fonts[]{{"small", 12, QFont::Normal}, {"hint", 12, QFont::Normal}, {"small3", 12, QFont::Normal}, {"danger", 12, QFont::Normal}, {"warn", 12, QFont::Normal}, {"warnText", 12, QFont::Normal},
        {"lbl", 12, QFont::DemiBold}, {"cap", 11, QFont::Bold}, {"strong", 13, QFont::DemiBold}, {"bold", 13, QFont::Bold}, {"h2", 14, QFont::Bold}, {"clip", 15, QFont::Bold}, {"dialog", 17, QFont::Bold}, {"title", 22, QFont::Bold}, {"display", 26, QFont::Bold}};
    target->setProperty("role", role);
    for (const auto& entry : fonts) if (qstrcmp(entry.role, role) == 0) target->setFont(uiFont(entry.pixels, entry.weight));
}
}
QLabel* label(const QString& text, const char* role, bool wrap) {
    auto* result = new QLabel(text); result->setTextFormat(Qt::PlainText); result->setWordWrap(wrap);
    if (role) applyRole(result, role);
    return result;
}
QLabel* caption(const QString& text) {
    auto* result = label(text.toUpper(), "cap"); auto font = uiFont(11, QFont::Bold); font.setLetterSpacing(QFont::PercentageSpacing, 107); result->setFont(font); return result;
}
QLabel* monoLabel(const QString& text, int pixels, const QColor& color, int weight) {
    // The colors of the theme go by role, so that they follow it when it changes.
    auto* result = label(text, color == tk::text2 ? "t2" : color == tk::text3 ? "t3" : nullptr); result->setFont(monoFont(pixels, weight));
    if (color != tk::text && color != tk::text2 && color != tk::text3) result->setStyleSheet(QStringLiteral("color: %1;").arg(color.name()));
    return result;
}
QFrame* card(const char* kind) { auto* frame = new QFrame; frame->setProperty("card", kind); return frame; }
QFrame* separator(bool vertical) { auto* line = new QFrame; line->setProperty("line", vertical ? "v" : "h"); if (vertical) line->setFixedWidth(1); else line->setFixedHeight(1); return line; }
QHBoxLayout* centered(QWidget* widget) { auto* row = new QHBoxLayout; row->setContentsMargins(0, 0, 0, 0); row->addStretch(); row->addWidget(widget); row->addStretch(); return row; }
void followVisibility(QWidget* child, const QWidget* parent) {
    if (parent && parent->window()->testAttribute(Qt::WA_DontShowOnScreen)) child->setAttribute(Qt::WA_DontShowOnScreen, true);
}
QMenu* popupMenu(QWidget* parent) {
    auto* menu = new QMenu(parent); menu->setWindowFlags(menu->windowFlags() | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    menu->setAttribute(Qt::WA_TranslucentBackground); followVisibility(menu, parent); return menu;
}
QString timeText(qint64 ms) { return QString("%1:%2.%3").arg(ms / 60000, 2, 10, QLatin1Char('0')).arg(ms / 1000 % 60, 2, 10, QLatin1Char('0')).arg(ms % 1000, 3, 10, QLatin1Char('0')); }

QSize kbdSize(const QString& keys) { return {QFontMetrics(uiFont(11, QFont::DemiBold)).horizontalAdvance(keys) + 12, 18}; }
void paintKbd(QPainter& painter, const QRectF& rect, const QString& keys, const QColor& color) {
    painter.save(); painter.setRenderHint(QPainter::Antialiasing); painter.setOpacity(painter.opacity() * 0.72);
    painter.setPen(QPen(color, 1)); painter.setBrush(Qt::NoBrush); painter.drawRoundedRect(rect.adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
    painter.setFont(uiFont(11, QFont::DemiBold)); painter.drawText(rect, Qt::AlignCenter, keys); painter.restore();
}

Button::Button(const QString& text, Variant variant, Size size, QWidget* parent) : QPushButton(text, parent), m_variant(variant), m_size(size) {
    setAttribute(Qt::WA_Hover); setCursor(Qt::PointingHandCursor); setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    if (variant == Segment || variant == Tool) setCheckable(true);
    if (variant == Tool) setFixedSize(26, 26);
}
Button::Button(const QString& iconName, const QString& text, Variant variant, Size size, QWidget* parent) : Button(text, variant, size, parent) { m_icon = iconName; }
QFont Button::textFont() const { return uiFont(m_size == Small ? 12 : m_size == Large ? 14 : 13, QFont::DemiBold); }
QSize Button::sizeHint() const {
    if (m_variant == Tool) return {26, 26};
    const int height = m_variant == Segment ? 28 : heightOf(m_size);
    if (text().isEmpty() && m_kbd.isEmpty() && m_trailing.isEmpty()) return {height, height};
    const int gap = m_size == Small || m_variant == Segment ? 6 : 8, padding = m_padding >= 0 ? m_padding : m_variant == Segment ? 12 : m_size == Small ? 10 : m_size == Large ? 18 : 14;
    int width = QFontMetrics(textFont()).horizontalAdvance(text());
    if (!m_icon.isEmpty()) width += 16 + gap;
    if (!m_kbd.isEmpty()) width += kbdSize(m_kbd).width() + gap;
    if (!m_trailing.isEmpty()) width += 16 + gap;
    return {width + padding * 2 + 2, height};
}
QSize Button::minimumSizeHint() const { return m_spread ? QSize(heightOf(m_size) * 2, heightOf(m_size)) : sizeHint(); }
void Button::focusInEvent(QFocusEvent* event) {
    m_keyboardFocus = event->reason() == Qt::TabFocusReason || event->reason() == Qt::BacktabFocusReason || event->reason() == Qt::ShortcutFocusReason;
    QPushButton::focusInEvent(event);
}
bool Button::event(QEvent* event) {
    if (event->type() == QEvent::EnabledChange) setCursor(isEnabled() ? Qt::PointingHandCursor : Qt::ArrowCursor);
    return QPushButton::event(event);
}
void Button::paintEvent(QPaintEvent*) {
    QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing);
    const bool off = !isEnabled(), on = isChecked(), down = isDown() || (on && m_variant != Segment && m_variant != Tool), over = underMouse() && !off;
    QColor background = Qt::transparent, border = Qt::transparent, foreground = tk::text;
    switch (m_variant) {
    case Secondary:
        background = off ? tk::mix(tk::window, tk::raised, 0.4) : down ? tk::press : over ? tk::hover : tk::raised; border = off ? tk::lineSoft : over ? tk::lineHover : tk::lineStrong; break;
    case Primary:
        background = border = off ? tk::accentOff : down ? tk::accentPress : over ? tk::accentHover : tk::accent; foreground = off ? tk::onAccentOff : tk::onAccent; break;
    case Ghost:
        if (!off) background = down ? tk::hover : over ? tk::raised : QColor(Qt::transparent);
        foreground = over || down ? tk::text : tk::text2; break;
    case Danger:
        background = off ? tk::mix(tk::window, tk::raised, 0.4) : down ? tk::mix(tk::dangerSoft, tk::danger, 0.1) : over ? tk::dangerSoft : tk::dangerBg; border = off ? tk::lineSoft : over ? tk::mix(tk::dangerLine, tk::danger, 0.15) : tk::dangerLine; foreground = tk::danger; break;
    case DangerSolid:
        background = border = off ? tk::dangerSoft : over ? QColor(0xEE, 0x82, 0x87) : QColor(0xE5, 0x66, 0x6D); foreground = QColor(0x2A, 0x08, 0x0B); break;
    case Segment:
        if (on) background = tk::selected;
        foreground = on || over ? tk::text : tk::text2; break;
    case Tool:
        if (on) { background = tk::warnSoft; border = tk::mix(tk::warnLine, tk::warn, 0.1); foreground = tk::warn; }
        else { if (over) background = tk::chip; foreground = over ? tk::text : tk::text2; }
        break;
    }
    if (m_color.isValid() && m_variant != Primary) foreground = m_color;
    if (off && m_variant != Primary) foreground = tk::textOff;
    const double radius = m_variant == Segment || m_variant == Tool ? 5 : 6;
    painter.setPen(border.alpha() ? QPen(border, 1) : QPen(Qt::NoPen)); painter.setBrush(background);
    // A theme with a gradient gives it to its main buttons; hovered or pressed, they take its first color.
    if (m_variant == Primary && tk::accentGradient && !off && !down && !over) { painter.setPen(Qt::NoPen); painter.setBrush(accentBrush(rect())); }
    painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
    if (m_keyboardFocus && hasFocus()) { painter.setPen(QPen(tk::info, 2)); painter.setBrush(Qt::NoBrush); painter.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), radius, radius); }

    const int gap = m_size == Small || m_variant == Segment ? 6 : 8; const auto font = textFont(); const QFontMetrics metrics(font);
    if (text().isEmpty() && m_kbd.isEmpty() && m_trailing.isEmpty()) { paintIcon(painter, m_icon, QRectF((width() - 16) / 2.0, (height() - 16) / 2.0, 16, 16), foreground); return; }
    const int padding = m_padding >= 0 ? m_padding : m_variant == Segment ? 12 : m_size == Small ? 10 : m_size == Large ? 18 : 14;
    const QSize kbd = m_kbd.isEmpty() ? QSize() : kbdSize(m_kbd);
    int fixed = 0;
    if (!m_icon.isEmpty()) fixed += 16 + gap;
    if (!m_kbd.isEmpty()) fixed += kbd.width() + gap;
    if (!m_trailing.isEmpty()) fixed += 16 + gap;
    const int room = std::max(0, width() - padding * 2 - fixed);
    const auto shown = metrics.elidedText(text(), Qt::ElideRight, room); const int textWidth = std::min(room, metrics.horizontalAdvance(shown));
    double x = m_spread ? padding : (width() - (fixed + textWidth)) / 2.0;
    if (!m_icon.isEmpty()) { paintIcon(painter, m_icon, QRectF(x, (height() - 16) / 2.0, 16, 16), m_spread && m_variant != Primary && !off ? tk::text2 : foreground); x += 16 + gap; }
    painter.setFont(font); painter.setPen(foreground); painter.drawText(QRectF(x, 0, textWidth + 1, height()), Qt::AlignVCenter | Qt::AlignLeft, shown); x += textWidth;
    if (!m_kbd.isEmpty()) { x += gap; paintKbd(painter, QRectF(x, (height() - kbd.height()) / 2.0, kbd.width(), kbd.height()), m_kbd, foreground); x += kbd.width(); }
    if (!m_trailing.isEmpty()) paintIcon(painter, m_trailing, QRectF(m_spread ? width() - padding - 16 : x + gap, (height() - 16) / 2.0, 16, 16), m_spread && !off ? tk::text2 : foreground);
}

Tag::Tag(const QString& text, Variant variant, QWidget* parent) : QWidget(parent), m_text(text), m_variant(variant) { setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed); }
QSize Tag::measure(const QString& text, bool icon) { return {QFontMetrics(uiFont(11, QFont::DemiBold)).horizontalAdvance(text) + 14 + (icon ? 17 : 0), 20}; }
void Tag::paint(QPainter& painter, const QRectF& rect, const QString& text, Variant variant, const QString& iconName) {
    const QColor backgrounds[]{tk::chip, tk::accentSoft, tk::warnSoft, tk::dangerSoft, tk::infoSoft, tk::window};
    const QColor foregrounds[]{tk::text2, tk::accent, tk::warn, tk::danger, tk::info, tk::text};
    painter.save(); painter.setRenderHint(QPainter::Antialiasing); painter.setPen(Qt::NoPen); painter.setBrush(backgrounds[variant]); painter.drawRoundedRect(rect, 4, 4);
    double x = rect.x() + 7;
    if (!iconName.isEmpty()) { paintIcon(painter, iconName, QRectF(x, rect.y() + (rect.height() - 12) / 2, 12, 12), foregrounds[variant]); x += 17; }
    painter.setFont(uiFont(11, QFont::DemiBold)); painter.setPen(foregrounds[variant]); painter.drawText(QRectF(x, rect.y(), rect.right() - x, rect.height()), Qt::AlignVCenter | Qt::AlignLeft, text); painter.restore();
}
void Tag::paintEvent(QPaintEvent*) { QPainter painter(this); paint(painter, rect(), m_text, m_variant, m_icon); }

Toggle::Toggle(const QString& text, Kind kind, QWidget* parent) : QCheckBox(text, parent), m_kind(kind) { setCursor(Qt::PointingHandCursor); setAttribute(Qt::WA_Hover); }
QSize Toggle::sizeHint() const {
    const int indicator = m_kind == Switch ? 34 : 18, gap = m_kind == Switch ? 12 : 8;
    return {indicator + (text().isEmpty() ? 0 : gap + QFontMetrics(font()).horizontalAdvance(text()) + 2), 22};
}
void Toggle::paintBox(QPainter& painter, const QRectF& rect, bool checked, bool partial) {
    painter.save(); painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(checked ? tk::accent : tk::control, 1.5)); painter.setBrush(checked ? tk::accent : tk::field); painter.drawRoundedRect(rect.adjusted(0.75, 0.75, -0.75, -0.75), 4, 4);
    if (checked) {
        painter.setPen(QPen(tk::onAccent, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin)); const double s = rect.width() / 18.0; const auto at = [&](double x, double y) { return QPointF(rect.x() + (1 + x) * s, rect.y() + (1 + y) * s); };
        if (partial) painter.drawLine(at(3, 8), at(13, 8)); else painter.drawPolyline(QPolygonF{at(3.5, 8.5), at(6.5, 11.5), at(12.5, 5)});
    }
    painter.restore();
}
void Toggle::paintEvent(QPaintEvent*) {
    QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing); if (!isEnabled()) painter.setOpacity(0.4);
    const bool on = isChecked(); double x = 0;
    if (m_kind == Switch) {
        const QRectF track(0.75, (height() - 20) / 2.0 + 0.75, 32.5, 18.5);
        painter.setPen(QPen(on ? tk::accent : tk::control, 1.5)); painter.setBrush(on ? tk::accent : tk::field); painter.drawRoundedRect(track, 9.25, 9.25);
        painter.setPen(Qt::NoPen); painter.setBrush(on ? tk::onAccent : tk::text2); painter.drawEllipse(QRectF(on ? 18.5 : 4.5, (height() - 11) / 2.0, 11, 11)); x = 34 + 12;
    } else { paintBox(painter, QRectF(0, (height() - 18) / 2.0, 18, 18), on); x = 18 + 8; }
    if (hasFocus() && (focusPolicy() & Qt::TabFocus) && property("keyboardFocus").toBool()) { painter.setPen(QPen(tk::info, 2)); painter.setBrush(Qt::NoBrush); painter.drawRoundedRect(QRectF(0, (height() - 20) / 2.0, x - 8, 20).adjusted(-1, -1, 1, 1), 6, 6); }
    painter.setPen(tk::text); painter.setFont(font()); painter.drawText(QRectF(x, 0, width() - x, height()), Qt::AlignVCenter | Qt::AlignLeft, QFontMetrics(font()).elidedText(text(), Qt::ElideRight, int(width() - x)));
}
RadioRow::RadioRow(const QString& title, const QString& detail, QWidget* parent) : QPushButton(parent), m_title(title), m_detail(detail) {
    setCheckable(true); setAutoExclusive(true); setAttribute(Qt::WA_Hover); setCursor(Qt::PointingHandCursor); setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed); setAccessibleName(title);
}
void RadioRow::paintEvent(QPaintEvent*) {
    QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing); const bool on = isChecked();
    if (on || underMouse()) { painter.setPen(on ? QPen(tk::line, 1) : QPen(Qt::NoPen)); painter.setBrush(tk::surface); painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 8, 8); }
    const QRectF radio(12.75, (height() - 18) / 2.0 + 0.75, 16.5, 16.5);
    painter.setPen(QPen(on ? tk::accent : tk::control, 1.5)); painter.setBrush(Qt::NoBrush); painter.drawEllipse(radio);
    if (on) { painter.setPen(Qt::NoPen); painter.setBrush(tk::accent); painter.drawEllipse(radio.center(), 4, 4); }
    const auto bold = uiFont(13, QFont::DemiBold); painter.setFont(bold); painter.setPen(tk::text); const double x = 42;
    painter.drawText(QRectF(x, 0, width() - x, height()), Qt::AlignVCenter | Qt::AlignLeft, m_title);
    const double after = x + QFontMetrics(bold).horizontalAdvance(m_title) + 5; painter.setFont(uiFont(13)); painter.setPen(tk::text2);
    painter.drawText(QRectF(after, 0, width() - after - 8, height()), Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("· ") + m_detail);
}

Tile::Tile(const QString& title, const QString& subtitle, QWidget* parent) : QPushButton(parent), m_title(title), m_subtitle(subtitle) {
    setCheckable(true); setAttribute(Qt::WA_Hover); setCursor(Qt::PointingHandCursor); setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed); setAccessibleName(title);
}
QSize Tile::sizeHint() const {
    if (m_height) return {60, m_height};
    int height = 20;
    if (m_picture) height += 64 + 8;
    if (!m_icon.isEmpty()) height += 16 + 6;
    height += QFontMetrics(uiFont(m_titlePixels, QFont::Bold)).height();
    if (!m_subtitle.isEmpty()) height += 3 + QFontMetrics(uiFont(12)).height() * int(m_subtitle.count('\n') + 1);
    const int width = std::max(QFontMetrics(uiFont(m_titlePixels, QFont::Bold)).horizontalAdvance(m_title), QFontMetrics(uiFont(12)).horizontalAdvance(m_subtitle.section('\n', 0, 0))) + 26;
    return {width, height};
}
void Tile::paintEvent(QPaintEvent*) {
    QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing); if (!isEnabled()) painter.setOpacity(0.45);
    const bool on = isChecked(), over = underMouse() && isEnabled();
    const QColor background = on ? tk::accentSoft : m_raised ? (over ? tk::raised : tk::surface) : (over ? tk::surface : tk::field);
    painter.setPen(QPen(on ? tk::accent : over ? tk::lineHover : tk::line, 1)); painter.setBrush(background);
    painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), m_centered ? 6 : 8, m_centered ? 6 : 8);
    if (hasFocus() && property("keyboardFocus").toBool()) { painter.setPen(QPen(tk::info, 2)); painter.setBrush(Qt::NoBrush); painter.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 7, 7); }
    if (m_centered) {
        const QColor color = on ? tk::accent : over ? tk::text : tk::text2; const auto font = uiFont(11, QFont::DemiBold); const int textHeight = QFontMetrics(font).height();
        // Above the name, a shape or, for a tool, its icon.
        const QSizeF mark = m_icon.isEmpty() ? m_shapeSize : QSizeF(18, 18);
        const double total = mark.height() + 5 + textHeight, top = (height() - total) / 2.0;
        const QRectF shape((width() - mark.width()) / 2.0, top, mark.width(), mark.height());
        if (!m_icon.isEmpty()) paintIcon(painter, m_icon, shape, color);
        else {
            QPen pen(color, 1.5); if (m_shape == Dashed) pen.setStyle(Qt::DashLine);
            painter.setPen(pen); painter.setBrush(m_shape == Filled ? QColor(color.red(), color.green(), color.blue(), 46) : QColor(Qt::transparent)); painter.drawRoundedRect(shape, 2, 2);
        }
        painter.setFont(font); painter.setPen(color); painter.drawText(QRectF(0, top + mark.height() + 5, width(), textHeight), Qt::AlignCenter, m_title);
        return;
    }
    const double left = m_picture ? 10 : 12; double y = 10;
    if (m_picture) {
        const QRectF picture(left + 0.5, y + 0.5, width() - left * 2 - 1, 63); QPen pen(tk::line, 1); if (m_pictureDashed) pen.setStyle(Qt::DashLine);
        QLinearGradient gradient(picture.topLeft(), picture.bottomRight()); gradient.setColorAt(0, QColor(0x1F, 0x35, 0x40)); gradient.setColorAt(0.5, QColor(0x12, 0x20, 0x27)); gradient.setColorAt(1, QColor(0x33, 0x40, 0x5A));
        painter.setPen(pen); painter.setBrush(m_pictureDashed ? QBrush(Qt::NoBrush) : QBrush(gradient)); painter.drawRoundedRect(picture, 5, 5);
        if (!m_pictureIcon.isEmpty()) paintIcon(painter, m_pictureIcon, QRectF(picture.center().x() - 8, picture.center().y() - 8, 16, 16), tk::text3);
        y += 64 + 8;
    }
    if (!m_icon.isEmpty()) { paintIcon(painter, m_icon, QRectF(left, y, 16, 16), on ? tk::accent : over ? tk::text : tk::text2); y += 16 + 6; }
    const auto titleFont = uiFont(m_titlePixels, m_picture || !m_icon.isEmpty() ? QFont::DemiBold : QFont::Bold); const QFontMetrics titleMetrics(titleFont);
    painter.setFont(titleFont); painter.setPen(on && !m_picture ? tk::accent : m_mutedTitle ? tk::text2 : tk::text);
    painter.drawText(QRectF(left, y, width() - left * 2, titleMetrics.height()), Qt::AlignVCenter | Qt::AlignLeft, titleMetrics.elidedText(m_title, Qt::ElideRight, int(width() - left * 2)));
    y += titleMetrics.height() + 3;
    if (!m_subtitle.isEmpty()) {
        const auto font = uiFont(12); const QFontMetrics metrics(font); painter.setFont(font); painter.setPen(tk::text2);
        for (const auto& line : m_subtitle.split('\n')) { painter.drawText(QRectF(left, y, width() - left * 2, metrics.height()), Qt::AlignVCenter | Qt::AlignLeft, metrics.elidedText(line, Qt::ElideRight, int(width() - left * 2))); y += metrics.height(); }
    }
}

Segmented::Segmented(QWidget* parent) : QWidget(parent), m_layout(new QHBoxLayout(this)) { m_layout->setContentsMargins(3, 3, 3, 3); m_layout->setSpacing(2); setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed); }
void Segmented::addOption(const QString& text, const QString& iconName) {
    auto* button = new Button(iconName, text, Button::Segment); button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    const int index = int(m_buttons.size()); m_buttons.append(button); m_layout->addWidget(button, 1);
    connect(button, &QPushButton::clicked, this, [this, index] { setCurrentIndex(index); });
    if (m_current < 0) { m_current = 0; button->setChecked(true); }
}
void Segmented::setOptionText(int index, const QString& text) { if (index >= 0 && index < m_buttons.size()) m_buttons[index]->setText(text); }
void Segmented::setCurrentIndex(int index) {
    if (index < 0 || index >= m_buttons.size()) return;
    const bool changed = index != m_current; m_current = index;
    for (int i = 0; i < m_buttons.size(); ++i) m_buttons[i]->setChecked(i == index);
    if (changed) emit currentChanged(index);
}
void Segmented::paintEvent(QPaintEvent*) {
    QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing); painter.setPen(QPen(tk::line, 1)); painter.setBrush(m_dark ? tk::window : tk::field);
    painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 7, 7);
}

Bar::Bar(QWidget* parent, int height) : QWidget(parent) { setFixedHeight(height); setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed); }
void Bar::setValue(double value) { value = std::clamp(value, 0.0, 1.0); if (std::abs(value - m_value) < 0.0005) return; m_value = value; update(); }
void Bar::paintEvent(QPaintEvent*) {
    QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing); painter.setPen(Qt::NoPen); if (!isEnabled()) painter.setOpacity(0.4);
    const double thickness = m_marker >= 0 ? height() - 6 : height(), top = (height() - thickness) / 2.0, radius = thickness / 2;
    painter.setBrush(m_track.isValid() ? m_track : tk::selected); painter.drawRoundedRect(QRectF(0, top, width(), thickness), radius, radius);
    if (m_value > 0) { const QRectF fill(0, top, std::max(thickness, width() * m_value), thickness); painter.setBrush(m_fill.isValid() ? QBrush(m_fill) : accentBrush(QRectF(0, top, width(), thickness))); painter.drawRoundedRect(fill, radius, radius); }
    if (m_marker >= 0) { painter.setBrush(tk::text); painter.drawRect(QRectF(std::clamp(width() * m_marker, 0.0, width() - 2.0), 0, 2, height())); }
}

Keycaps::Keycaps(QWidget* parent, bool large) : QWidget(parent), m_large(large) { setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed); }
QStringList Keycaps::keyNames(const QKeySequence& sequence) {
    const auto textForm = sequence.toString(QKeySequence::PortableText); auto names = textForm.split('+', Qt::SkipEmptyParts);
    if (textForm.endsWith('+')) names.append(QStringLiteral("+"));
    for (auto& name : names) if (name == QLatin1String("Meta")) name = QStringLiteral("Win");
    return names;
}
void Keycaps::setSequence(const QKeySequence& sequence) { m_keys = keyNames(sequence); updateGeometry(); update(); }
QSize Keycaps::sizeHint() const {
    const QFontMetrics metrics(uiFont(m_large ? 18 : 12, QFont::Bold)); int width = 0;
    for (const auto& key : m_keys) width += std::max(m_large ? 64 : 34, metrics.horizontalAdvance(key) + (m_large ? 34 : 18));
    const int between = m_large ? 10 + metrics.horizontalAdvance(QLatin1Char('+')) + 10 : 6;
    return {std::max(1, width + between * std::max(0, int(m_keys.size()) - 1)), m_large ? 56 : 28};
}
void Keycaps::paintEvent(QPaintEvent*) {
    QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing); const auto font = uiFont(m_large ? 18 : 12, QFont::Bold); const QFontMetrics metrics(font); painter.setFont(font);
    double x = 0; const double radius = m_large ? 8 : 5, lip = m_large ? 4 : 3;
    for (int index = 0; index < m_keys.size(); ++index) {
        const bool last = m_large && index == m_keys.size() - 1; const double width = std::max(m_large ? 64 : 34, metrics.horizontalAdvance(m_keys[index]) + (m_large ? 34 : 18));
        const QRectF cap(x + 0.5, 0.5, width - 1, height() - 1); const QColor border = last ? tk::accentLine : tk::lineHover;
        painter.setPen(Qt::NoPen); painter.setBrush(border); painter.drawRoundedRect(cap, radius, radius);
        painter.setBrush(tk::raised); painter.drawRoundedRect(cap.adjusted(1, 1, -1, -lip), radius - 1, radius - 1);
        painter.setPen(last ? tk::accent : tk::text); painter.drawText(cap.adjusted(0, 0, 0, -lip + 1), Qt::AlignCenter, m_keys[index]); x += width;
        if (index + 1 < m_keys.size()) {
            if (m_large) { const double plus = 10 + metrics.horizontalAdvance(QLatin1Char('+')) + 10; painter.setPen(tk::text3); painter.drawText(QRectF(x, 0, plus, height()), Qt::AlignCenter, QStringLiteral("+")); x += plus; }
            else x += 6;
        }
    }
}

IconBox::IconBox(const QString& iconName, const QColor& color, const QColor& background, int size, QWidget* parent) : QWidget(parent), m_icon(iconName), m_color(color), m_background(background) { setFixedSize(size, size); }
void IconBox::paintEvent(QPaintEvent*) {
    QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing); painter.setPen(Qt::NoPen); painter.setBrush(m_background);
    const double radius = m_round ? width() / 2.0 : 8; painter.drawRoundedRect(rect(), radius, radius);
    paintIcon(painter, m_icon, QRectF((width() - 16) / 2.0, (height() - 16) / 2.0, 16, 16), m_color);
}
IconLabel::IconLabel(const QString& iconName, const QColor& color, int size, QWidget* parent) : QWidget(parent), m_icon(iconName), m_color(color) { setFixedSize(size, size); }
void IconLabel::paintEvent(QPaintEvent*) { QPainter painter(this); paintIcon(painter, m_icon, rect(), isEnabled() ? m_color : tk::textOff); }

ElideLabel::ElideLabel(const QString& text, const char* role, Qt::TextElideMode mode, QWidget* parent) : QLabel(parent), m_full(text), m_mode(mode) {
    setTextFormat(Qt::PlainText); if (role) applyRole(this, role); setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed); setToolTip(text);
}
QSize ElideLabel::sizeHint() const { const QFontMetrics metrics(font()); return {metrics.horizontalAdvance(m_full) + 2, metrics.height() + 2}; }
void ElideLabel::paintEvent(QPaintEvent*) {
    QPainter painter(this); painter.setFont(font()); painter.setPen(palette().color(isEnabled() ? QPalette::Normal : QPalette::Disabled, QPalette::WindowText));
    painter.drawText(rect(), int(alignment()) | Qt::TextSingleLine, QFontMetrics(font()).elidedText(m_full, m_mode, width()));
}
DashedButton::DashedButton(const QString& iconName, const QString& text, QWidget* parent) : QPushButton(text, parent), m_icon(iconName) { setAttribute(Qt::WA_Hover); setCursor(Qt::PointingHandCursor); setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed); }
void DashedButton::paintEvent(QPaintEvent*) {
    QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing); const QColor color = !isEnabled() ? tk::textOff : underMouse() ? tk::accent : tk::text2;
    QPen pen(!isEnabled() ? tk::lineSoft : underMouse() ? tk::accent : tk::lineHover, 1, Qt::DashLine); painter.setPen(pen); painter.setBrush(Qt::NoBrush); painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 8, 8);
    const auto font = uiFont(13, QFont::DemiBold); const double content = 16 + 8 + QFontMetrics(font).horizontalAdvance(text()), x = (width() - content) / 2;
    paintIcon(painter, m_icon, QRectF(x, (height() - 16) / 2.0, 16, 16), color); painter.setFont(font); painter.setPen(color); painter.drawText(QRectF(x + 24, 0, content, height()), Qt::AlignVCenter | Qt::AlignLeft, text());
}
TabButton::TabButton(const QString& iconName, const QString& text, QWidget* parent) : QPushButton(text, parent), m_icon(iconName) { setCheckable(true); setAutoExclusive(true); setAttribute(Qt::WA_Hover); setCursor(Qt::PointingHandCursor); }
QSize TabButton::sizeHint() const { return {QFontMetrics(uiFont(13, QFont::DemiBold)).horizontalAdvance(text()) + 16 + 8 + 4, 40}; }
void TabButton::paintEvent(QPaintEvent*) {
    QPainter painter(this); const QColor color = isChecked() || underMouse() ? tk::text : tk::text2;
    paintIcon(painter, m_icon, QRectF(2, (height() - 2 - 16) / 2.0, 16, 16), color); painter.setFont(uiFont(13, QFont::DemiBold)); painter.setPen(color);
    painter.drawText(QRectF(26, 0, width() - 26, height() - 2), Qt::AlignVCenter | Qt::AlignLeft, text());
    if (isChecked()) painter.fillRect(QRectF(0, height() - 2, width(), 2), tk::accent);
    if (hasFocus() && property("keyboardFocus").toBool()) { painter.setPen(QPen(tk::info, 2)); painter.drawRect(rect().adjusted(1, 1, -1, -1)); }
}

ChoiceBox::ChoiceBox(QWidget* parent) : QComboBox(parent) { setCursor(Qt::PointingHandCursor); setFocusPolicy(Qt::StrongFocus); setMaxVisibleItems(14); }
void ChoiceBox::paintEvent(QPaintEvent* event) {
    QComboBox::paintEvent(event);
    QPainter painter(this); paintIcon(painter, QStringLiteral("down"), QRectF(width() - 26, (height() - 16) / 2.0, 16, 16), isEnabled() ? tk::text2 : tk::textOff);
}
// The settings scroll with the wheel; a list under the pointer must not change on the way.
void ChoiceBox::wheelEvent(QWheelEvent* event) { event->ignore(); }

TimeField::TimeField(QWidget* parent) : QDoubleSpinBox(parent) { setDecimals(3); setSingleStep(0.1); setButtonSymbols(QAbstractSpinBox::NoButtons); setFont(monoFont(13)); setKeyboardTracking(false); }
QString TimeField::textFromValue(double value) const { return timeText(qRound64(value * 1000)); }
double TimeField::valueFromText(const QString& text) const {
    auto normalized = text.trimmed(); normalized.replace(',', '.');
    const auto parts = normalized.split(':');
    return parts.size() == 2 ? parts[0].toDouble() * 60 + parts[1].toDouble() : normalized.toDouble();
}
QValidator::State TimeField::validate(QString& input, int&) const {
    static const QRegularExpression pattern(QStringLiteral("^\\d{0,3}(:\\d{0,2})?([.,]\\d{0,3})?$"));
    return pattern.match(input.trimmed()).hasMatch() ? QValidator::Acceptable : QValidator::Invalid;
}

Banner::Banner(Kind kind, const QString& iconName, QWidget* parent) : QFrame(parent) {
    static const char* names[]{"ok", "info", "warn", "err"}; const QColor colors[]{tk::accent, tk::info, tk::warn, tk::danger};
    setProperty("banner", names[kind]); auto* layout = new QHBoxLayout(this); layout->setContentsMargins(14, 10, 10, 10); layout->setSpacing(12);
    layout->addWidget(new IconLabel(iconName, colors[kind]), 0, Qt::AlignVCenter);
    auto* texts = new QVBoxLayout; texts->setSpacing(1); m_title = label({}, "strong"); m_detail = label({}, "small", true); texts->addWidget(m_title); texts->addWidget(m_detail); layout->addLayout(texts, 1);
    m_actions = new QHBoxLayout; m_actions->setSpacing(6); layout->addLayout(m_actions);
}
void Banner::setTexts(const QString& title, const QString& detail) { m_title->setText(title); m_detail->setText(detail); m_detail->setVisible(!detail.isEmpty()); }

namespace {
class Scrim final : public QWidget {
public:
    using QWidget::QWidget;
protected:
    void paintEvent(QPaintEvent*) override { QPainter painter(this); painter.fillRect(rect(), QColor(6, 8, 10, 184)); }
};
}
ModalDialog::ModalDialog(QWidget* parent, int width) : QDialog(parent, Qt::Dialog | Qt::FramelessWindowHint) {
    setAttribute(Qt::WA_TranslucentBackground); setModal(true); setFixedWidth(width); followVisibility(this, parent);
    auto* root = new QVBoxLayout(this); root->setContentsMargins(1, 1, 1, 1); root->setSpacing(0);
    auto* top = new QWidget; m_header = new QVBoxLayout(top); m_header->setContentsMargins(22, 20, 22, 0); m_header->setSpacing(12);
    m_headingRow = new QHBoxLayout; m_headingRow->setSpacing(12); m_header->addLayout(m_headingRow); root->addWidget(top);
    auto* middle = new QWidget; m_body = new QVBoxLayout(middle); m_body->setContentsMargins(22, 16, 22, 16); m_body->setSpacing(16); root->addWidget(middle, 1);
    root->addWidget(separator());
    auto* bottom = new QWidget; auto* bottomLayout = new QHBoxLayout(bottom); bottomLayout->setContentsMargins(22, 14, 22, 14); bottomLayout->setSpacing(8);
    m_footerLeft = new QHBoxLayout; m_footerLeft->setSpacing(8); bottomLayout->addLayout(m_footerLeft); bottomLayout->addStretch();
    m_footer = new QHBoxLayout; m_footer->setSpacing(8); bottomLayout->addLayout(m_footer); root->addWidget(bottom);
}
void ModalDialog::setHeading(const QString& title, const QString& subtitle, QWidget* leading, bool closeButton) {
    if (leading) m_headingRow->addWidget(leading, 0, Qt::AlignTop);
    auto* texts = new QVBoxLayout; texts->setSpacing(2); m_title = label(title, "dialog", true); m_subtitle = label(subtitle, "t2", true);
    texts->addWidget(m_title); texts->addWidget(m_subtitle); m_subtitle->setVisible(!subtitle.isEmpty()); m_headingRow->addLayout(texts, 1);
    if (closeButton) {
        auto* close = new Button(QStringLiteral("x"), QString(), Button::Ghost); close->setObjectName("dialog-close"); close->setToolTip(QStringLiteral("Fermer")); close->setAccessibleName(QStringLiteral("Fermer")); close->setFocusPolicy(Qt::NoFocus);
        m_headingRow->addWidget(close, 0, Qt::AlignTop); connect(close, &QPushButton::clicked, this, &QDialog::reject);
    }
}
void ModalDialog::setHeadingTexts(const QString& title, const QString& subtitle) {
    if (!m_title) return;
    m_title->setText(title); m_subtitle->setText(subtitle); m_subtitle->setVisible(!subtitle.isEmpty());
}
void ModalDialog::refit() {
    adjustSize();
    if (auto* owner = parentWidget(); owner && isVisible()) { auto* top = owner->window(); move(top->mapToGlobal(top->rect().center()) - QPoint(width() / 2, height() / 2)); }
}
void ModalDialog::paintEvent(QPaintEvent*) {
    QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing); painter.setPen(QPen(tk::lineStrong, 1)); painter.setBrush(tk::surface);
    painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 12, 12);
}
void ModalDialog::showEvent(QShowEvent* event) {
    if (auto* owner = parentWidget()) {
        auto* top = owner->window(); adjustSize();
        if (m_aside) { move(top->mapToGlobal(QPoint(top->width() - width() - 24, 72))); QDialog::showEvent(event); return; }
        if (!m_scrim) { m_scrim = new Scrim(top); m_scrim->setGeometry(top->rect()); m_scrim->show(); m_scrim->raise(); for (auto* surface : top->findChildren<VideoSurface*>()) surface->setDimmed(true); }
        move(top->mapToGlobal(top->rect().center()) - QPoint(width() / 2, height() / 2));
    }
    QDialog::showEvent(event);
}
void ModalDialog::hideEvent(QHideEvent* event) {
    // The image the card shows is not under the veil: it is darkened there, and given back with it.
    if (m_scrim) { for (auto* surface : m_scrim->window()->findChildren<VideoSurface*>()) surface->setDimmed(false); m_scrim->hide(); m_scrim->deleteLater(); m_scrim = nullptr; }
    QDialog::hideEvent(event);
}

bool askText(QWidget* parent, const QString& title, const QString& subtitle, const QString& fieldLabel, QString& value, const QString& acceptText) {
    ModalDialog dialog(parent, 420); dialog.setObjectName("text-prompt"); dialog.setHeading(title, subtitle);
    auto* group = new QVBoxLayout; group->setSpacing(6); group->addWidget(label(fieldLabel, "lbl"));
    auto* field = new QLineEdit(value); field->setMaxLength(80); group->addWidget(field); dialog.body()->addLayout(group);
    auto* cancel = new Button(QStringLiteral("Annuler")); auto* accept = new Button(acceptText, Button::Primary); accept->setDefault(true); dialog.footer()->addWidget(cancel); dialog.footer()->addWidget(accept);
    QObject::connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject); QObject::connect(accept, &QPushButton::clicked, &dialog, &QDialog::accept); QObject::connect(field, &QLineEdit::returnPressed, &dialog, &QDialog::accept);
    field->selectAll(); field->setFocus();
    if (dialog.exec() != QDialog::Accepted || field->text().trimmed().isEmpty()) return false;
    value = field->text().trimmed(); return true;
}

namespace {
class Toast final : public QFrame {
public:
    Toast(ToastHost::Kind kind, const QString& title, const QString& text, QList<ToastAction> actions, const QString& iconName, QWidget* parent) : QFrame(parent) {
        setObjectName("toast"); setFixedWidth(360);
        setStyleSheet(QStringLiteral("QFrame#toast { background: #1A1F27; border: 1px solid %1; border-radius: 8px; }").arg(kind == ToastHost::Error ? "#5A2A30" : kind == ToastHost::Warn ? "#4D3B1C" : "#323A47"));
        static const char* names[]{"ok", "share", "warn", "err"}; const QColor colors[]{tk::accent, tk::info, tk::warn, tk::danger};
        auto* layout = new QHBoxLayout(this); layout->setContentsMargins(14, 12, 10, 12); layout->setSpacing(12);
        layout->addWidget(new IconLabel(iconName.isEmpty() ? QString::fromLatin1(names[kind]) : iconName, colors[kind]), 0, Qt::AlignTop);
        auto* texts = new QVBoxLayout; texts->setSpacing(2); auto* heading = label(title, "bold", true); heading->setObjectName("toast-title"); texts->addWidget(heading);
        if (!text.isEmpty()) { auto* detail = label(text, "small", true); detail->setObjectName("toast-text"); texts->addWidget(detail); }
        if (!actions.isEmpty()) {
            auto* row = new QHBoxLayout; row->setSpacing(6); row->setContentsMargins(0, 8, 0, 0);
            for (int index = 0; index < actions.size(); ++index) {
                auto* button = new Button(actions[index].text, index ? Button::Ghost : Button::Secondary, Button::Small); row->addWidget(button);
                connect(button, &QPushButton::clicked, this, [this, run = actions[index].run] { const auto action = run; close(); if (action) action(); });
            }
            row->addStretch(); texts->addLayout(row);
        }
        layout->addLayout(texts, 1);
        auto* dismiss = new Button(QStringLiteral("x"), QString(), Button::Ghost, Button::Small); dismiss->setAccessibleName(QStringLiteral("Fermer")); dismiss->setFocusPolicy(Qt::NoFocus);
        layout->addWidget(dismiss, 0, Qt::AlignTop); connect(dismiss, &QPushButton::clicked, this, &QWidget::close);
        setAttribute(Qt::WA_DeleteOnClose);
        // Success and information leave by themselves; a warning or an error waits for the user.
        if (kind == ToastHost::Ok || kind == ToastHost::Info) { m_timer = new QTimer(this); m_timer->setSingleShot(true); m_timer->setInterval(6000); connect(m_timer, &QTimer::timeout, this, &QWidget::close); m_timer->start(); }
    }
protected:
    void enterEvent(QEnterEvent*) override { if (m_timer) m_timer->stop(); }
    void leaveEvent(QEvent*) override { if (m_timer) m_timer->start(); }
private:
    QTimer* m_timer = nullptr;
};
}
ToastHost::ToastHost(QWidget* area) : QObject(area), m_area(area) { area->installEventFilter(this); }
void ToastHost::show(Kind kind, const QString& title, const QString& text, QList<ToastAction> actions, const QString& iconName) {
    auto* toast = new Toast(kind, title, text, std::move(actions), iconName, m_area);
    connect(toast, &QObject::destroyed, this, [this] { layout(); });
    m_toasts.append(toast);
    m_toasts.erase(std::remove_if(m_toasts.begin(), m_toasts.end(), [](const auto& entry) { return entry.isNull(); }), m_toasts.end());
    while (m_toasts.size() > 3) { if (auto* oldest = m_toasts.takeFirst().data()) { oldest->hide(); oldest->deleteLater(); } }
    // Over an image the graphics card shows itself, a notification needs a window of its own.
    VideoSurface::raiseAbove(toast);
    toast->show(); toast->raise(); layout();
}
void ToastHost::layout() {
    int y = m_area->height() - m_inset;
    for (int index = int(m_toasts.size()) - 1; index >= 0; --index) {
        auto* toast = m_toasts[index].data(); if (!toast || toast->isHidden()) continue;
        // Wrapped labels overstate their size hint; the height the layout needs at this width is the right one.
        const int height = toast->layout()->hasHeightForWidth() ? toast->layout()->totalHeightForWidth(360) : toast->sizeHint().height(); y -= height;
        toast->setGeometry(m_area->width() - 20 - 360, y, 360, height); toast->raise(); y -= 10;
    }
}
bool ToastHost::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_area && event->type() == QEvent::Resize) layout();
    return QObject::eventFilter(watched, event);
}
}
