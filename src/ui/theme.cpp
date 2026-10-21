#include "theme.h"
#include "core/config.h"
#include <QBuffer>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QRegularExpression>
#include <QApplication>
#include <QHash>
#include <QLinearGradient>
#include <QLineF>
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
    {"toggle", "M5.5 4.5h5a3.5 3.5 0 0 1 0 7h-5a3.5 3.5 0 0 1 0-7z M10.5 6a2 2 0 1 0 0 4a2 2 0 1 0 0-4z"},
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
    {"spark", "M8 1.5l1.6 4.9 4.9 1.6-4.9 1.6L8 14.5 6.4 9.6 1.5 8l4.9-1.6z"},
    {"star","M8 2.2l1.8 3.7 4 .6-2.9 2.8.7 4L8 11.4l-3.6 1.9.7-4L2.2 6.5l4-.6z"},
    {"tag", "M2.5 2.5h5l6 6-5 5-6-6z M5.5 5.5h.01"},
    {"trash", "M2.5 4.5h11 M6 4.5V2.5h4v2 M4 4.5l.7 9h6.6l.7-9"},
    {"folder", "M2 4.5v8h12v-6.5H8L6.5 4.5z"},
    {"crop", "M4.5 1.5v10h10 M1.5 4.5h10v10"},
    {"image", "M2.5 3h11v10h-11z M2.5 11l3.5-3.5 3 3 2-2 2.5 2.5 M10.5 5.5a1 1 0 1 0 0 2a1 1 0 1 0 0-2z"},
    {"arrow", "M3.5 12.5l8.5-8.5 M6.5 3.5h5.5v5.5"},
    {"rect", "M2.5 4h11v8h-11z"},
    {"circle", "M8 2.5a5.5 5.5 0 1 0 0 11a5.5 5.5 0 1 0 0-11z"},
    {"camera", "M2 5h3l1.5-2h3L11 5h3v8H2z M8 6.5a2.5 2.5 0 1 0 0 5a2.5 2.5 0 1 0 0-5z"},
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
    // In the colors of the theme, its gradient crossing the logo when it has one.
    QLinearGradient gradient(1, 1, 25, 25); gradient.setColorAt(0, tk::accent); gradient.setColorAt(1, tk::accentGradient ? tk::accentEnd : tk::accent);
    painter.setPen(Qt::NoPen); painter.setBrush(gradient); painter.drawRoundedRect(QRectF(1, 1, 24, 24), 7, 7);
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
QColor tk::mix(const QColor& from, const QColor& to, double amount) {
    const auto blend = [amount](double a, double b) { return int(std::lround(a + (b - a) * amount)); };
    return QColor(blend(from.red(), to.red()), blend(from.green(), to.green()), blend(from.blue(), to.blue()));
}
bool ThemeColors::operator==(const ThemeColors& other) const {
    return background == other.background && accent == other.accent && text == other.text && windowGradient == other.windowGradient && windowEnd == other.windowEnd
        && accentGradient == other.accentGradient && accentEnd == other.accentEnd && angle == other.angle;
}
QList<ThemePreset> themePresets() {
    const auto theme = [](unsigned background, unsigned accent, unsigned text, unsigned windowEnd = 0, unsigned accentEnd = 0) {
        ThemeColors colors; colors.background = tk::rgb(background); colors.accent = tk::rgb(accent); colors.text = tk::rgb(text);
        colors.windowGradient = windowEnd != 0; colors.windowEnd = windowEnd ? tk::rgb(windowEnd) : colors.background;
        colors.accentGradient = accentEnd != 0; colors.accentEnd = accentEnd ? tk::rgb(accentEnd) : colors.accent;
        return colors;
    };
    return {
        {QStringLiteral("clipsapp"), QStringLiteral("Clipsapp"), ThemeColors{}},
        {QStringLiteral("violet"), QStringLiteral("Violet"), theme(0x0F0B17, 0xA98BFF, 0xEEEAF7, 0x1C1030, 0xD48BFF)},
        {QStringLiteral("night"), QStringLiteral("Bleu nuit"), theme(0x090E18, 0x5EA8FF, 0xE8EEF8, 0x0E1A30)},
        {QStringLiteral("amber"), QStringLiteral("Ambre"), theme(0x100D0A, 0xF5B24A, 0xF3EEE7, 0x1E150B, 0xF57E4A)},
        {QStringLiteral("rose"), QStringLiteral("Rose"), theme(0x120B10, 0xFF7EB6, 0xF5EBF1, 0x22101C)},
    };
}
namespace {
// Every color from the three of the theme. The default theme keeps the values it was drawn with.
void derive(const ThemeColors& colors) {
    using namespace tk;
    const QColor b = colors.background, a = colors.accent, t = colors.text;
    if (b == ThemeColors{}.background && a == ThemeColors{}.accent && t == ThemeColors{}.text) {
        window = rgb(0x0C0E12); rail = rgb(0x101318); surface = rgb(0x14181E); raised = rgb(0x1A1F27); hover = rgb(0x212732); press = rgb(0x282F3B); field = rgb(0x0F1217); header = rgb(0x111419);
        line = rgb(0x242A34); lineSoft = rgb(0x1E242C); lineStrong = rgb(0x323A47); lineHover = rgb(0x3B4553); control = rgb(0x4A5464); chip = rgb(0x1F242D); selected = rgb(0x262D38);
        text = rgb(0xE9EDF3); text2 = rgb(0xA1AABA); text3 = rgb(0x8A94A5); textOff = rgb(0x566070); muted = rgb(0x7C8696);
        accent = rgb(0x6FE3B4); accentHover = rgb(0x8BEBC4); accentPress = rgb(0x55CFA0); onAccent = rgb(0x05261A); accentSoft = rgb(0x12302A); accentLine = rgb(0x2F7A5F); liveBg = rgb(0x11251F); liveLine = rgb(0x1D3F33);
        accentOff = rgb(0x173229); onAccentOff = rgb(0x4F7A69); highlight = rgb(0x1D4A3B);
        warnSoft = rgb(0x352812); warnBg = rgb(0x241D10); warnLine = rgb(0x4D3B1C); warnText = rgb(0xE9D3B0);
        dangerSoft = rgb(0x3A1C1F); dangerBg = rgb(0x2A171A); dangerLine = rgb(0x5A2A30); infoSoft = rgb(0x1A2640); infoLine = rgb(0x2B3D63);
    } else {
        // Surfaces and lines rise from the background toward the text: they keep its hue, light or dark.
        const auto up = [&](double amount) { return mix(b, t, amount); };
        window = b; rail = up(0.018); surface = up(0.040); raised = up(0.066); hover = up(0.098); press = up(0.130); field = up(0.013); header = up(0.024);
        line = up(0.110); lineSoft = up(0.083); lineStrong = up(0.172); lineHover = up(0.213); control = up(0.282); chip = up(0.088); selected = up(0.120);
        text = t; text2 = mix(t, b, 0.33); text3 = mix(t, b, 0.42); textOff = mix(t, b, 0.66); muted = mix(t, b, 0.48);
        // The accent and what is written on it, dark on a light accent, light on a dark one.
        const bool light = a.lightnessF() > 0.5;
        accent = a; accentHover = mix(a, light ? QColor(Qt::white) : t, 0.2); accentPress = mix(a, b, 0.14); onAccent = light ? mix(a, QColor(Qt::black), 0.86) : mix(a, QColor(Qt::white), 0.92);
        accentSoft = mix(b, a, 0.14); accentLine = mix(b, a, 0.40); liveBg = mix(b, a, 0.07); liveLine = mix(b, a, 0.17);
        accentOff = mix(b, a, 0.12); onAccentOff = mix(b, a, 0.42); highlight = mix(b, a, 0.24);
        // Warnings, errors and information keep their color, on a ground taken from the background.
        warnSoft = mix(b, warn, 0.16); warnBg = mix(b, warn, 0.10); warnLine = mix(b, warn, 0.27); warnText = mix(t, warn, 0.30);
        dangerSoft = mix(b, danger, 0.16); dangerBg = mix(b, danger, 0.11); dangerLine = mix(b, danger, 0.28); infoSoft = mix(b, info, 0.11); infoLine = mix(b, info, 0.24);
    }
    windowGradient = colors.windowGradient; windowEnd = colors.windowGradient ? colors.windowEnd : window;
    accentGradient = colors.accentGradient; accentEnd = colors.accentGradient ? colors.accentEnd : accent; gradientAngle = colors.angle;
}
// The two ends of a gradient at this angle (0: left to right, 90: top to bottom), in the box's own units.
QLineF gradientLine(const QRectF& box, int angle) {
    const double radians = angle * 3.141592653589793 / 180, dx = std::cos(radians) / 2, dy = std::sin(radians) / 2;
    return {box.center().x() - dx * box.width(), box.center().y() - dy * box.height(), box.center().x() + dx * box.width(), box.center().y() + dy * box.height()};
}
QString styleSheet() {
    using namespace tk;
    const auto axis = gradientLine(QRectF(0, 0, 1, 1), gradientAngle);
    const auto ground = windowGradient ? QStringLiteral("qlineargradient(x1:%1, y1:%2, x2:%3, y2:%4, stop:0 %5, stop:1 %6)").arg(axis.x1()).arg(axis.y1()).arg(axis.x2()).arg(axis.y2()).arg(window.name(), windowEnd.name()) : window.name();
    QString sheet = QStringLiteral(R"(
        QWidget { color: {text}; }
        QWidget:disabled { color: {textOff}; }
        QMainWindow, QDialog { background: {ground}; }
        QToolTip { background: {raised}; color: {text}; border: 1px solid {lineStrong}; padding: 7px 10px; }
        QLabel { background: transparent; }
        QLabel[role="t2"] { color: {text2}; }
        QLabel[role="t3"] { color: {text3}; }
        QLabel[role="small"], QLabel[role="hint"], QLabel[role="lbl"] { color: {text2}; }
        QLabel[role="small3"], QLabel[role="cap"] { color: {text3}; }
        QLabel[role="danger"] { color: {danger}; }
        QLabel[role="warn"] { color: {warn}; }
        QLabel[role="warnText"] { color: {warnText}; }
        QLabel:disabled { color: {textOff}; }
        QFrame[card="true"] { background: {surface}; border: 1px solid {line}; border-radius: 8px; }
        QFrame[card="well"] { background: {field}; border: 1px solid {line}; border-radius: 8px; }
        QFrame[card="panel"] { background: {header}; border: 1px solid {line}; border-radius: 8px; }
        QFrame[card="bar"] { background: {raised}; border: 1px solid {lineHover}; border-radius: 10px; }
        QFrame[banner="ok"] { background: {liveBg}; border: 1px solid {liveLine}; border-radius: 8px; }
        QFrame[banner="info"] { background: {infoSoft}; border: 1px solid {infoLine}; border-radius: 8px; }
        QFrame[banner="warn"] { background: {warnBg}; border: 1px solid {warnLine}; border-radius: 8px; }
        QFrame[banner="err"] { background: {dangerBg}; border: 1px solid {dangerLine}; border-radius: 8px; }
        QFrame[line="h"] { background: {line}; border: none; max-height: 1px; min-height: 1px; }
        QFrame[line="v"] { background: {line}; border: none; max-width: 1px; min-width: 1px; }
        QFrame#strip { background: {rail}; border: none; border-bottom: 1px solid {line}; }
        QFrame#sidebar { background: {rail}; border: none; border-right: 1px solid {line}; }
        QFrame#savebar { background: {surface}; border: none; border-top: 1px solid {lineHover}; }
        QLineEdit, QComboBox, QAbstractSpinBox { background: {field}; border: 1px solid {lineStrong}; border-radius: 6px; padding: 0 10px; min-height: 30px; selection-background-color: {highlight}; selection-color: {text}; }
        QLineEdit:hover, QComboBox:hover, QAbstractSpinBox:hover { border-color: {control}; }
        QLineEdit:focus, QComboBox:focus, QAbstractSpinBox:focus { border: 2px solid {accent}; padding: 0 9px; }
        QLineEdit:disabled, QComboBox:disabled, QAbstractSpinBox:disabled { background: {header}; border-color: {lineSoft}; color: {textOff}; }
        QLineEdit[bare="true"], QLineEdit[bare="true"]:focus, QLineEdit[bare="true"]:hover { background: transparent; border: none; padding: 0; min-height: 0; }
        QKeySequenceEdit > QLineEdit { min-width: 140px; }
        QComboBox::drop-down { width: 28px; border: none; }
        QComboBox::down-arrow { image: none; }
        QComboBox QAbstractItemView { background: {raised}; border: 1px solid {lineStrong}; padding: 4px; outline: 0; selection-background-color: {selected}; selection-color: {text}; }
        QComboBox QAbstractItemView::item { min-height: 30px; padding: 0 8px; border-radius: 5px; }
        QComboBox QAbstractItemView::item:selected { background: {selected}; }
        QMenu { background: {raised}; border: 1px solid {lineStrong}; border-radius: 8px; padding: 4px; }
        QMenu::item { padding: 6px 28px 6px 10px; border-radius: 5px; background: transparent; }
        QMenu::item:selected { background: {selected}; }
        QMenu::item:disabled { color: {textOff}; }
        QMenu::item[danger="true"] { color: {danger}; }
        QMenu::separator { height: 1px; background: {line}; margin: 4px 6px; }
        QMenu::icon { padding-left: 8px; }
        QMenu::indicator { width: 0; }
        QScrollArea { background: transparent; border: none; }
        QScrollArea > QWidget > QWidget { background: transparent; }
        QAbstractItemView { background: transparent; border: none; outline: 0; }
        QScrollBar:vertical { background: transparent; width: 10px; margin: 2px 3px 2px 3px; }
        QScrollBar::handle:vertical { background: {lineStrong}; min-height: 30px; border-radius: 2px; }
        QScrollBar::handle:vertical:hover { background: {lineHover}; }
        QScrollBar:horizontal { background: {raised}; height: 8px; margin: 0; border-radius: 4px; }
        QScrollBar::handle:horizontal { background: {lineStrong}; min-width: 30px; border-radius: 3px; margin: 1px; }
        QScrollBar::handle:horizontal:hover { background: {lineHover}; }
        QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
        QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
        QSlider { min-height: 16px; }
        QSlider::groove:horizontal { height: 4px; background: {lineStrong}; border-radius: 2px; }
        QSlider::sub-page:horizontal { background: {accent}; border-radius: 2px; }
        QSlider::handle:horizontal { width: 10px; height: 10px; margin: -5px 0; border-radius: 7px; background: {text}; border: 2px solid {window}; }
        QSlider::handle:horizontal:focus { border-color: {info}; }
        QSlider::sub-page:horizontal:disabled, QSlider::handle:horizontal:disabled { background: {textOff}; }
    )");
    const std::pair<const char*, QColor> tokens[]{
        {"text2", text2}, {"text3", text3}, {"textOff", textOff}, {"text", text}, {"window", window}, {"raised", raised}, {"surface", surface}, {"field", field}, {"header", header},
        {"rail", rail}, {"lineStrong", lineStrong}, {"lineHover", lineHover}, {"lineSoft", lineSoft}, {"line", line}, {"control", control}, {"selected", selected}, {"highlight", highlight},
        {"accent", accent}, {"liveBg", liveBg}, {"liveLine", liveLine}, {"warnText", warnText}, {"warnBg", warnBg}, {"warnLine", warnLine}, {"warn", warn},
        {"dangerBg", dangerBg}, {"dangerLine", dangerLine}, {"danger", danger}, {"infoSoft", infoSoft}, {"infoLine", infoLine}, {"info", info}};
    sheet.replace(QStringLiteral("{ground}"), ground);
    for (const auto& [name, color] : tokens) sheet.replace(QStringLiteral("{%1}").arg(QLatin1String(name)), color.name());
    return sheet;
}
void applyPalette(QApplication& application) {
    QPalette palette;
    palette.setColor(QPalette::Window, tk::window); palette.setColor(QPalette::WindowText, tk::text);
    palette.setColor(QPalette::Base, tk::field); palette.setColor(QPalette::AlternateBase, tk::surface);
    palette.setColor(QPalette::Text, tk::text); palette.setColor(QPalette::Button, tk::raised);
    palette.setColor(QPalette::ButtonText, tk::text); palette.setColor(QPalette::Highlight, tk::highlight);
    palette.setColor(QPalette::HighlightedText, tk::text); palette.setColor(QPalette::PlaceholderText, tk::text3);
    palette.setColor(QPalette::ToolTipBase, tk::raised); palette.setColor(QPalette::ToolTipText, tk::text);
    palette.setColor(QPalette::Disabled, QPalette::Text, tk::textOff); palette.setColor(QPalette::Disabled, QPalette::WindowText, tk::textOff);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, tk::textOff);
    application.setPalette(palette);
}
}
QBrush accentBrush(const QRectF& box) {
    if (!tk::accentGradient) return tk::accent;
    const auto line = gradientLine(box, 0); QLinearGradient gradient(line.p1(), line.p2());
    gradient.setColorAt(0, tk::accent); gradient.setColorAt(1, tk::accentEnd); return gradient;
}
ThemeColors darkTheme(ThemeColors colors) {
    const auto atMost = [](QColor color, double lightness) { return color.lightnessF() > lightness ? QColor::fromHslF(color.hslHueF(), color.hslSaturationF(), lightness).toRgb() : color; };
    const auto atLeast = [](QColor color, double lightness) { return color.lightnessF() < lightness ? QColor::fromHslF(color.hslHueF(), color.hslSaturationF(), lightness).toRgb() : color; };
    colors.background = atMost(colors.background, 0.22); colors.windowEnd = atMost(colors.windowEnd, 0.22); colors.text = atLeast(colors.text, 0.72);
    colors.angle = ((colors.angle % 360) + 360) % 360;
    return colors;
}
ThemeColors themeFromSettings(const ThemeSettings& settings) {
    ThemeColors colors; const auto read = [](const QString& name, const QColor& fallback) { const QColor color(name); return name.isEmpty() || !color.isValid() ? fallback : color; };
    colors.background = read(settings.background, colors.background); colors.accent = read(settings.accent, colors.accent); colors.text = read(settings.text, colors.text);
    colors.windowEnd = read(settings.windowEnd, colors.background); colors.accentEnd = read(settings.accentEnd, colors.accent);
    colors.windowGradient = settings.windowGradient; colors.accentGradient = settings.accentGradient; colors.angle = settings.angle;
    return darkTheme(colors);
}
ThemeSettings themeSettings(const ThemeColors& colors) {
    ThemeSettings settings; if (colors == ThemeColors{}) return settings;
    const auto name = [](const QColor& color) { return color.name().toUpper(); };
    settings.background = name(colors.background); settings.accent = name(colors.accent); settings.text = name(colors.text);
    settings.windowGradient = colors.windowGradient; settings.accentGradient = colors.accentGradient; settings.angle = colors.angle;
    if (colors.windowGradient) settings.windowEnd = name(colors.windowEnd);
    if (colors.accentGradient) settings.accentEnd = name(colors.accentEnd);
    return settings;
}
// clipsapp-theme-0F0B17-A98BFF-EEEAF7-1C1030-D48BFF-135: background, accent, text, the second color of
// each gradient ("0" without one), and the angle of the background's. One word, which survives a chat.
QString themeCode(const ThemeColors& colors) {
    const auto hex = [](const QColor& color) { return color.name().mid(1).toUpper(); };
    return QStringLiteral("clipsapp-theme-%1-%2-%3-%4-%5-%6").arg(hex(colors.background), hex(colors.accent), hex(colors.text),
        colors.windowGradient ? hex(colors.windowEnd) : QStringLiteral("0"), colors.accentGradient ? hex(colors.accentEnd) : QStringLiteral("0")).arg(colors.angle);
}
bool readThemeCode(const QString& text, ThemeColors& colors) {
    static const QRegularExpression code(QStringLiteral("clipsapp-theme-([0-9a-f]{6})-([0-9a-f]{6})-([0-9a-f]{6})-([0-9a-f]{6}|0)-([0-9a-f]{6}|0)-(\\d{1,3})"), QRegularExpression::CaseInsensitiveOption);
    const auto match = code.match(text); if (!match.hasMatch()) return false;
    ThemeColors read; const auto color = [&match](int index) { return QColor(QLatin1Char('#') + match.captured(index)); };
    read.background = color(1); read.accent = color(2); read.text = color(3);
    read.windowGradient = match.captured(4) != QLatin1String("0"); read.windowEnd = read.windowGradient ? color(4) : read.background;
    read.accentGradient = match.captured(5) != QLatin1String("0"); read.accentEnd = read.accentGradient ? color(5) : read.accent;
    read.angle = match.captured(6).toInt() % 360;
    colors = darkTheme(read); return true;
}
QString writeThemeIcon(const QString& directory) {
    QDir folder(directory);
    for (const auto& old : folder.entryList({QStringLiteral("Clipsapp-*.ico")}, QDir::Files)) folder.remove(old);
    if (tk::accent == ThemeColors{}.accent && !tk::accentGradient) return {};
    // An .ico made of PNG images, one per size Windows asks for, from the taskbar to large icons.
    QList<QByteArray> images; const QList<int> sizes{16, 20, 24, 32, 40, 48, 64, 256};
    for (const int size : sizes) {
        QImage image(size, size, QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent);
        { QPainter painter(&image); paintLogo(painter, QRectF(0, 0, size, size)); }
        QByteArray png; QBuffer buffer(&png); buffer.open(QIODevice::WriteOnly); image.save(&buffer, "PNG"); images.append(png);
    }
    QByteArray ico; QDataStream out(&ico, QIODevice::WriteOnly); out.setByteOrder(QDataStream::LittleEndian);
    out << quint16(0) << quint16(1) << quint16(images.size());
    quint32 offset = 6 + 16 * quint32(images.size());
    for (int index = 0; index < images.size(); ++index) {
        const int size = sizes[index];
        out << quint8(size >= 256 ? 0 : size) << quint8(size >= 256 ? 0 : size) << quint8(0) << quint8(0) << quint16(1) << quint16(32) << quint32(images[index].size()) << offset;
        offset += quint32(images[index].size());
    }
    for (const auto& image : images) out.writeRawData(image.constData(), int(image.size()));
    // Named after its colors: Windows keeps icons by file, a new name shows the new one at once.
    const auto path = folder.filePath(QStringLiteral("Clipsapp-%1.ico").arg(QString::fromLatin1(QCryptographicHash::hash(themeCode(ThemeColors{tk::window, tk::accent, tk::text, tk::windowGradient, tk::windowEnd, tk::accentGradient, tk::accentEnd, tk::gradientAngle}).toUtf8(), QCryptographicHash::Sha1).toHex().left(10))));
    QFile file(path); if (!folder.mkpath(QStringLiteral(".")) || !file.open(QIODevice::WriteOnly) || file.write(ico) != ico.size()) return {};
    return path;
}
void applyColors(const ThemeColors& colors) {
    // Writing the style sheet again draws every window again: only when the theme changes.
    static ThemeColors applied; static bool any = false;
    auto* application = qobject_cast<QApplication*>(QCoreApplication::instance());
    if (any && applied == colors && application && !application->styleSheet().isEmpty()) return;
    applied = colors; any = true;
    derive(colors);
    if (!application) return;
    applyPalette(*application); application->setStyleSheet(styleSheet());
    // What draws itself reads the colors when it paints: it only needs to be painted again.
    for (auto* widget : QApplication::allWidgets()) widget->update();
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
    applyColors(ThemeColors{});
}
}
