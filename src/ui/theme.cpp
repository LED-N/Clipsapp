#include "theme.h"
#include <QApplication>
#include <QHash>
#include <QPainter>
#include <QPalette>
#include <QStyle>
#include <QVariant>
#include <QWidget>
#include <cctype>
#include <cmath>

namespace clipsapp {
namespace {
struct IconData { const char* name; const char* path; };
const IconData icons[] = {
    {"clips", "M2.5 3.5h11v9h-11z M2.5 6.5h11 M6 3.5l-1.5 3 M9.5 3.5L8 6.5 M13 3.5l-1.5 3"},
    {"share", "M8 10V2.5 M5 5.5l3-3 3 3 M3 9.5v4h10v-4"},
    {"settings", "M2.5 4.5h6 M11.5 4.5h2 M2.5 11.5h2 M7.5 11.5h6 M10 3v3 M6 10v3"},
    {"save", "M8 2.5v7 M5 7l3 3 3-3 M2.5 10.5v3h11v-3"},
    {"stop", "M4.5 4.5h7v7h-7z"},
    {"rec", "M8 4.5a3.5 3.5 0 1 0 0 7 3.5 3.5 0 0 0 0-7z"},
    {"play", "M5 3.2v9.6l7.5-4.8z"},
    {"pause", "M5.5 3.5v9 M10.5 3.5v9"},
    {"start", "M4 3.5v9 M12.5 3.5v9L6.5 8z"},
    {"end", "M12 3.5v9 M3.5 3.5v9L9.5 8z"},
    {"prev", "M10 4L6 8l4 4"},
    {"next", "M6 4l4 4-4 4"},
    {"search", "M7 11.5a4.5 4.5 0 1 0 0-9 4.5 4.5 0 0 0 0 9z M10.5 10.5l3 3"},
    {"sort", "M5 3v10 M2.5 10.5L5 13l2.5-2.5 M11 13V3 M8.5 5.5L11 3l2.5 2.5"},
    {"down", "M4 6l4 4 4-4"},
    {"up", "M4 10l4-4 4 4"},
    {"check", "M3.5 8.5l3 3 6-6.5"},
    {"cut", "M4.5 6a1.75 1.75 0 1 0 0-3.5 1.75 1.75 0 0 0 0 3.5z M4.5 13.5a1.75 1.75 0 1 0 0-3.5 1.75 1.75 0 0 0 0 3.5z M6 5.2l7.5 7.3 M6 10.8l7.5-7.3"},
    {"more", "M3.5 8h.01 M8 8h.01 M12.5 8h.01"},
    {"x", "M4 4l8 8 M12 4l-8 8"},
    {"ok", "M8 14a6 6 0 1 0 0-12 6 6 0 0 0 0 12z M5.5 8.2l1.8 1.8 3.3-3.6"},
    {"info", "M8 14a6 6 0 1 0 0-12 6 6 0 0 0 0 12z M8 7.5V11 M8 5h.01"},
    {"err", "M8 14a6 6 0 1 0 0-12 6 6 0 0 0 0 12z M6 6l4 4 M10 6l-4 4"},
    {"warn", "M8 2.2l6.3 11.3H1.7z M8 6.5v3 M8 11.6h.01"},
    {"lock", "M4 7.5h8v6H4z M5.5 7.5V5a2.5 2.5 0 0 1 5 0v2.5"},
    {"star", "M8 2.2l1.8 3.7 4 .6-2.9 2.8.7 4L8 11.4l-3.6 1.9.7-4L2.2 6.5l4-.6z"},
    {"tag", "M2.5 2.5h5l6 6-5 5-6-6z M5.5 5.5h.01"},
    {"trash", "M2.5 4.5h11 M6 4.5V2.5h4v2 M4 4.5l.7 9h6.6l.7-9"},
    {"folder", "M2 4.5v8h12v-6.5H8L6.5 4.5z"},
    {"crop", "M4.5 1.5v10h10 M1.5 4.5h10v10"},
    {"mic", "M8 2a2 2 0 0 0-2 2v3.5a2 2 0 0 0 4 0V4a2 2 0 0 0-2-2z M4 7.5a4 4 0 0 0 8 0 M8 11.5v2.5"},
    {"app", "M2 3.5h12v9H2z M2 6h12"},
    {"apps", "M2.5 2.5h4.5v4.5H2.5z M9 2.5h4.5v4.5H9z M2.5 9h4.5v4.5H2.5z M9 9h4.5v4.5H9z"},
    {"speaker", "M2.5 6v4h2.5l3.5 3V3L5 6z M11 5.5a3.5 3.5 0 0 1 0 5"},
    {"muted", "M2.5 6v4h2.5l3.5 3V3L5 6z M11 6l3 4 M14 6l-3 4"},
    {"phones", "M2.5 10V8a5.5 5.5 0 0 1 11 0v2 M2.5 10h2.5v3.5H2.5z M11 10h2.5v3.5H11z"},
    {"monitor", "M2 2.5h12v8.5H2z M5.5 14h5 M8 11v3"},
    {"window", "M2 3h12v10H2z M2 5.5h12 M4 4.25h.01 M5.5 4.25h.01"},
    {"grip", "M6 4h.01 M10 4h.01 M6 8h.01 M10 8h.01 M6 12h.01 M10 12h.01"},
    {"copy", "M5.5 5.5h8v8h-8z M10.5 5.5v-3h-8v8h3"},
    {"back", "M13 8H3 M7 4L3 8l4 4"},
    {"refresh", "M13.2 8A5.2 5.2 0 1 1 11.7 4.3 M13.2 2.5v2.8h-2.8"},
    {"plus", "M8 3v10 M3 8h10"},
    {"minus", "M3 8h10"},
    {"undo", "M3 5.5h6.5a3.5 3.5 0 0 1 0 7H6 M5.5 3L3 5.5 5.5 8"},
    {"film", "M2.5 2.5h11v11h-11z M5 2.5v11 M11 2.5v11 M2.5 5.5H5 M2.5 10.5H5 M11 5.5h2.5 M11 10.5h2.5"},
    {"edit", "M10.5 2.5l3 3L6 13H3v-3z"},
    {"open", "M9 2.5h4.5V7 M13.5 2.5L7.5 8.5 M11.5 9.5v4h-9v-9h4"},
    {"expand", "M2.5 6V2.5H6 M10 2.5h3.5V6 M13.5 10v3.5H10 M6 13.5H2.5V10"},
    {"eye","M1.5 8s2.5-4.5 6.5-4.5S14.5 8 14.5 8 12 12.5 8 12.5 1.5 8 1.5 8z M8 10a2 2 0 1 0 0-4 2 2 0 0 0 0 4z"},
};
bool bold(const QString& name) { return name == QLatin1String("more") || name == QLatin1String("grip"); }
bool filled(const QString& name) { return name == QLatin1String("play") || name == QLatin1String("starfill"); }

// SVG endpoint arc, approximated by one cubic per quarter turn at most.
void arcTo(QPainterPath& path, QPointF from, double rx, double ry, double rotation, bool large, bool sweep, QPointF to) {
    rx = std::abs(rx); ry = std::abs(ry);
    if (rx == 0 || ry == 0 || from == to) { path.lineTo(to); return; }
    const double pi = 3.141592653589793, phi = rotation * pi / 180, cosPhi = std::cos(phi), sinPhi = std::sin(phi);
    const double dx = (from.x() - to.x()) / 2, dy = (from.y() - to.y()) / 2;
    const double x1 = cosPhi * dx + sinPhi * dy, y1 = -sinPhi * dx + cosPhi * dy;
    const double lambda = x1 * x1 / (rx * rx) + y1 * y1 / (ry * ry);
    if (lambda > 1) { rx *= std::sqrt(lambda); ry *= std::sqrt(lambda); }
    const double numerator = rx * rx * ry * ry - rx * rx * y1 * y1 - ry * ry * x1 * x1, denominator = rx * rx * y1 * y1 + ry * ry * x1 * x1;
    const double factor = (large != sweep ? 1 : -1) * std::sqrt(std::max(0.0, denominator == 0 ? 0.0 : numerator / denominator));
    const double cxp = factor * rx * y1 / ry, cyp = -factor * ry * x1 / rx;
    const double cx = cosPhi * cxp - sinPhi * cyp + (from.x() + to.x()) / 2, cy = sinPhi * cxp + cosPhi * cyp + (from.y() + to.y()) / 2;
    const double start = std::atan2((y1 - cyp) / ry, (x1 - cxp) / rx);
    double sweepAngle = std::atan2((-y1 - cyp) / ry, (-x1 - cxp) / rx) - start;
    if (sweep && sweepAngle < 0) sweepAngle += 2 * pi;
    if (!sweep && sweepAngle > 0) sweepAngle -= 2 * pi;
    const int segments = std::max(1, int(std::ceil(std::abs(sweepAngle) / (pi / 2) - 1e-9)));
    const double delta = sweepAngle / segments, handle = 4.0 / 3 * std::tan(delta / 4);
    const auto map = [&](double x, double y) { return QPointF(cx + cosPhi * rx * x - sinPhi * ry * y, cy + sinPhi * rx * x + cosPhi * ry * y); };
    for (int segment = 0; segment < segments; ++segment) {
        const double a = start + segment * delta, b = a + delta;
        path.cubicTo(map(std::cos(a) - handle * std::sin(a), std::sin(a) + handle * std::cos(a)), map(std::cos(b) + handle * std::sin(b), std::sin(b) - handle * std::cos(b)), map(std::cos(b), std::sin(b)));
    }
}
// The subset of SVG path syntax the icon set uses: M L H V C S A Z, absolute and relative.
QPainterPath parsePath(const char* data) {
    QPainterPath path; const QByteArray d(data); int i = 0; char command = 0; QPointF current, subpath, control; bool hadCurve = false;
    const auto skip = [&] { while (i < d.size() && (d[i] == ' ' || d[i] == ',')) ++i; };
    const auto number = [&] {
        skip(); const int first = i; bool dot = false;
        if (i < d.size() && (d[i] == '-' || d[i] == '+')) ++i;
        while (i < d.size() && (std::isdigit(static_cast<unsigned char>(d[i])) || (d[i] == '.' && !dot))) { if (d[i] == '.') dot = true; ++i; }
        return d.mid(first, i - first).toDouble();
    };
    const auto point = [&](bool relative) { const double x = number(), y = number(); return relative ? current + QPointF(x, y) : QPointF(x, y); };
    while (true) {
        skip(); if (i >= d.size()) break;
        if (std::isalpha(static_cast<unsigned char>(d[i]))) command = d[i++];
        else if (command == 'M') command = 'L';
        else if (command == 'm') command = 'l';
        const bool relative = std::islower(static_cast<unsigned char>(command)); bool curve = false;
        switch (std::toupper(static_cast<unsigned char>(command))) {
        case 'M': current = subpath = point(relative); path.moveTo(current); break;
        case 'L': current = point(relative); path.lineTo(current); break;
        case 'H': { const double x = number(); current.setX(relative ? current.x() + x : x); path.lineTo(current); break; }
        case 'V': { const double y = number(); current.setY(relative ? current.y() + y : y); path.lineTo(current); break; }
        case 'C': { const auto c1 = point(relative), c2 = point(relative), end = point(relative); path.cubicTo(c1, c2, end); control = c2; current = end; curve = true; break; }
        case 'S': { const auto c1 = hadCurve ? current * 2 - control : current; const auto c2 = point(relative), end = point(relative); path.cubicTo(c1, c2, end); control = c2; current = end; curve = true; break; }
        case 'A': { const double rx = number(), ry = number(), rotation = number(); const bool large = number() != 0, sweep = number() != 0; const auto end = point(relative); arcTo(path, current, rx, ry, rotation, large, sweep, end); current = end; break; }
        case 'Z': path.closeSubpath(); current = subpath; break;
        default: return path;
        }
        hadCurve = curve;
    }
    return path;
}
const QPainterPath& iconPath(const QString& name) {
    static QHash<QString, QPainterPath> cache;
    auto it = cache.find(name);
    if (it == cache.end()) {
        QPainterPath path;
        const auto lookup = name == QLatin1String("starfill") ? QStringLiteral("star") : name;
        for (const auto& entry : icons) if (lookup == QLatin1String(entry.name)) path = parsePath(entry.path);
        it = cache.insert(name, path);
    }
    return it.value();
}
}
QColor tk::track(int index) {
    static constexpr QColor colors[]{rgb(0x6FE3B4), rgb(0x86A8F5), rgb(0xCFA2F0), rgb(0xF0B97A), rgb(0xF28FB3), rgb(0x7BD4E6)};
    return colors[((index % 6) + 6) % 6];
}
QFont uiFont(int pixels, int weight) {
    QFont font = QApplication::font(); font.setPixelSize(pixels); font.setWeight(QFont::Weight(weight)); return font;
}
QFont monoFont(int pixels, int weight) {
    QFont font; font.setFamilies({QStringLiteral("Cascadia Mono"), QStringLiteral("Consolas"), QStringLiteral("Noto Sans Mono"), QStringLiteral("monospace")});
    font.setStyleHint(QFont::Monospace); font.setPixelSize(pixels); font.setWeight(QFont::Weight(weight)); return font;
}
void paintIcon(QPainter& painter, const QString& name, const QRectF& box, const QColor& color) {
    const auto& path = iconPath(name); if (path.isEmpty()) return;
    painter.save(); painter.setRenderHint(QPainter::Antialiasing);
    const double scale = box.width() / 16.0; painter.translate(box.topLeft()); painter.scale(scale, scale);
    if (filled(name)) { painter.setPen(Qt::NoPen); painter.setBrush(color); }
    else { painter.setPen(QPen(color, bold(name) ? 2.4 : 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin)); painter.setBrush(Qt::NoBrush); }
    painter.drawPath(path); painter.restore();
}
QPixmap iconPixmap(const QString& name, const QColor& color, int size, qreal ratio) {
    QPixmap pixmap(QSize(size, size) * ratio); pixmap.setDevicePixelRatio(ratio); pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap); paintIcon(painter, name, QRectF(0, 0, size, size), color); return pixmap;
}
QIcon icon(const QString& name, const QColor& color) {
    QIcon result;
    for (const qreal ratio : {1.0, 2.0}) {
        result.addPixmap(iconPixmap(name, color, 16, ratio), QIcon::Normal); result.addPixmap(iconPixmap(name, color, 16, ratio), QIcon::Active);
        result.addPixmap(iconPixmap(name, tk::textOff, 16, ratio), QIcon::Disabled);
    }
    return result;
}
void paintLogo(QPainter& painter, const QRectF& box) {
    painter.save(); painter.setRenderHint(QPainter::Antialiasing); painter.translate(box.topLeft()); painter.scale(box.width() / 26.0, box.height() / 26.0);
    painter.setPen(Qt::NoPen); painter.setBrush(tk::accent); painter.drawRoundedRect(QRectF(1, 1, 24, 24), 7, 7);
    painter.setBrush(tk::onAccent); painter.drawRoundedRect(QRectF(7.5, 7.5, 2.4, 11), 1.2, 1.2);
    painter.drawPolygon(QPolygonF{{12, 7.6}, {12, 18.4}, {19.4, 13}}); painter.restore();
}
QIcon appIcon() {
    QIcon result;
    for (const int size : {16, 24, 32, 48, 64, 128}) {
        QPixmap pixmap(size, size); pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap); paintLogo(painter, QRectF(0, 0, size, size)); painter.end(); result.addPixmap(pixmap);
    }
    return result;
}
void setStyleProperty(QWidget* widget, const char* name, const QVariant& value) {
    if (widget->property(name) == value) return;
    widget->setProperty(name, value); widget->style()->unpolish(widget); widget->style()->polish(widget); widget->update();
}
void applyTheme(QApplication& application) {
    application.setStyle("Fusion");
    QFont font;
#ifdef Q_OS_WIN
    font.setFamilies({QStringLiteral("Segoe UI Variable Text"), QStringLiteral("Segoe UI")});
#else
    font.setFamilies({QStringLiteral("Noto Sans"), QStringLiteral("sans-serif")});
#endif
    font.setPixelSize(13); application.setFont(font);
    QPalette palette;
    palette.setColor(QPalette::Window, tk::window); palette.setColor(QPalette::WindowText, tk::text);
    palette.setColor(QPalette::Base, tk::field); palette.setColor(QPalette::AlternateBase, tk::surface);
    palette.setColor(QPalette::Text, tk::text); palette.setColor(QPalette::Button, tk::raised);
    palette.setColor(QPalette::ButtonText, tk::text); palette.setColor(QPalette::Highlight, QColor(0x1D, 0x4A, 0x3B));
    palette.setColor(QPalette::HighlightedText, tk::text); palette.setColor(QPalette::PlaceholderText, tk::text3);
    palette.setColor(QPalette::ToolTipBase, tk::raised); palette.setColor(QPalette::ToolTipText, tk::text);
    palette.setColor(QPalette::Disabled, QPalette::Text, tk::textOff); palette.setColor(QPalette::Disabled, QPalette::WindowText, tk::textOff);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, tk::textOff);
    application.setPalette(palette);
    application.setStyleSheet(QStringLiteral(R"(
        QWidget { color: #E9EDF3; }
        QWidget:disabled { color: #566070; }
        QMainWindow, QDialog { background: #0C0E12; }
        QToolTip { background: #1A1F27; color: #E9EDF3; border: 1px solid #323A47; padding: 5px 8px; }
        QLabel { background: transparent; }
        QLabel[role="t2"] { color: #A1AABA; }
        QLabel[role="t3"] { color: #8A94A5; }
        QLabel[role="small"], QLabel[role="hint"], QLabel[role="lbl"] { color: #A1AABA; }
        QLabel[role="small3"], QLabel[role="cap"] { color: #8A94A5; }
        QLabel[role="danger"] { color: #F2777C; }
        QLabel[role="warn"] { color: #F2B35E; }
        QLabel[role="warnText"] { color: #E9D3B0; }
        QLabel:disabled { color: #566070; }
        QFrame[card="true"] { background: #14181E; border: 1px solid #242A34; border-radius: 8px; }
        QFrame[card="well"] { background: #0F1217; border: 1px solid #242A34; border-radius: 8px; }
        QFrame[card="panel"] { background: #111419; border: 1px solid #242A34; border-radius: 8px; }
        QFrame[card="bar"] { background: #1A1F27; border: 1px solid #3B4553; border-radius: 10px; }
        QFrame[banner="ok"] { background: #11251F; border: 1px solid #1D3F33; border-radius: 8px; }
        QFrame[banner="info"] { background: #1A2640; border: 1px solid #2B3D63; border-radius: 8px; }
        QFrame[banner="warn"] { background: #241D10; border: 1px solid #4D3B1C; border-radius: 8px; }
        QFrame[banner="err"] { background: #2A171A; border: 1px solid #5A2A30; border-radius: 8px; }
        QFrame[line="h"] { background: #242A34; border: none; max-height: 1px; min-height: 1px; }
        QFrame[line="v"] { background: #242A34; border: none; max-width: 1px; min-width: 1px; }
        QLineEdit, QComboBox, QAbstractSpinBox { background: #0F1217; border: 1px solid #323A47; border-radius: 6px; padding: 0 10px; min-height: 30px; selection-background-color: #1D4A3B; selection-color: #E9EDF3; }
        QLineEdit:hover, QComboBox:hover, QAbstractSpinBox:hover { border-color: #4A5464; }
        QLineEdit:focus, QComboBox:focus, QAbstractSpinBox:focus { border: 2px solid #6FE3B4; padding: 0 9px; }
        QLineEdit:disabled, QComboBox:disabled, QAbstractSpinBox:disabled { background: #111419; border-color: #1E242C; color: #566070; }
        QLineEdit[bare="true"], QLineEdit[bare="true"]:focus, QLineEdit[bare="true"]:hover { background: transparent; border: none; padding: 0; min-height: 0; }
        QKeySequenceEdit > QLineEdit { min-width: 140px; }
        QComboBox::drop-down { width: 28px; border: none; }
        QComboBox::down-arrow { image: none; }
        QComboBox QAbstractItemView { background: #1A1F27; border: 1px solid #323A47; padding: 4px; outline: 0; selection-background-color: #262D38; selection-color: #E9EDF3; }
        QComboBox QAbstractItemView::item { min-height: 30px; padding: 0 8px; border-radius: 5px; }
        QComboBox QAbstractItemView::item:selected { background: #262D38; }
        QMenu { background: #1A1F27; border: 1px solid #323A47; border-radius: 8px; padding: 4px; }
        QMenu::item { padding: 6px 28px 6px 10px; border-radius: 5px; background: transparent; }
        QMenu::item:selected { background: #262D38; }
        QMenu::item:disabled { color: #566070; }
        QMenu::item[danger="true"] { color: #F2777C; }
        QMenu::separator { height: 1px; background: #2A313C; margin: 4px 6px; }
        QMenu::icon { padding-left: 8px; }
        QMenu::indicator { width: 0; }
        QScrollArea { background: transparent; border: none; }
        QScrollArea > QWidget > QWidget { background: transparent; }
        QAbstractItemView { background: transparent; border: none; outline: 0; }
        QScrollBar:vertical { background: transparent; width: 10px; margin: 2px 3px 2px 3px; }
        QScrollBar::handle:vertical { background: #2E3542; min-height: 30px; border-radius: 2px; }
        QScrollBar::handle:vertical:hover { background: #3B4553; }
        QScrollBar:horizontal { background: #1A1F27; height: 8px; margin: 0; border-radius: 4px; }
        QScrollBar::handle:horizontal { background: #323A47; min-width: 30px; border-radius: 3px; margin: 1px; }
        QScrollBar::handle:horizontal:hover { background: #3B4553; }
        QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
        QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
        QSlider { min-height: 16px; }
        QSlider::groove:horizontal { height: 4px; background: #323A47; border-radius: 2px; }
        QSlider::sub-page:horizontal { background: #6FE3B4; border-radius: 2px; }
        QSlider::handle:horizontal { width: 10px; height: 10px; margin: -5px 0; border-radius: 7px; background: #E9EDF3; border: 2px solid #0C0E12; }
        QSlider::handle:horizontal:focus { border-color: #8FB8FF; }
        QSlider::sub-page:horizontal:disabled, QSlider::handle:horizontal:disabled { background: #566070; }
    )"));
}
}
