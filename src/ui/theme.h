#pragma once
#include <QBrush>
#include <QColor>
#include <QList>
#include <QFont>
#include <QIcon>
#include <QPainterPath>
#include <QString>
class QApplication;
class QLabel;
class QPainter;
class QWidget;
namespace clipsapp {
struct ThemeSettings;
void applyTheme(QApplication& application);

// Design tokens; no shadow, blur or stacked transparency. They follow the theme (applyColors):
// drawing reads them when it paints and the style sheet is written again from them, so a new
// theme shows at once. The values here are those of the default theme.
namespace tk {
constexpr QColor rgb(unsigned value, int alpha = 255) { return QColor(int(value >> 16 & 255), int(value >> 8 & 255), int(value & 255), alpha); }
inline QColor window = rgb(0x0C0E12), rail = rgb(0x101318), surface = rgb(0x14181E), raised = rgb(0x1A1F27), hover = rgb(0x212732), press = rgb(0x282F3B), field = rgb(0x0F1217), header = rgb(0x111419);
inline QColor line = rgb(0x242A34), lineSoft = rgb(0x1E242C), lineStrong = rgb(0x323A47), lineHover = rgb(0x3B4553), control = rgb(0x4A5464), chip = rgb(0x1F242D), selected = rgb(0x262D38);
inline QColor text = rgb(0xE9EDF3), text2 = rgb(0xA1AABA), text3 = rgb(0x8A94A5), textOff = rgb(0x566070), muted = rgb(0x7C8696);
inline QColor accent = rgb(0x6FE3B4), accentHover = rgb(0x8BEBC4), accentPress = rgb(0x55CFA0), onAccent = rgb(0x05261A), accentSoft = rgb(0x12302A), accentLine = rgb(0x2F7A5F), liveBg = rgb(0x11251F), liveLine = rgb(0x1D3F33);
inline QColor accentOff = rgb(0x173229), onAccentOff = rgb(0x4F7A69), highlight = rgb(0x1D4A3B);
inline QColor warn = rgb(0xF2B35E), warnSoft = rgb(0x352812), warnBg = rgb(0x241D10), warnLine = rgb(0x4D3B1C), warnText = rgb(0xE9D3B0);
inline QColor danger = rgb(0xF2777C), dangerSoft = rgb(0x3A1C1F), dangerBg = rgb(0x2A171A), dangerLine = rgb(0x5A2A30);
inline QColor info = rgb(0x8FB8FF), infoSoft = rgb(0x1A2640), infoLine = rgb(0x2B3D63);
// The second color of the gradients, when the theme has them: behind the window, and on the main buttons.
inline QColor windowEnd = window, accentEnd = accent;
inline int gradientAngle = 135; inline bool windowGradient = false, accentGradient = false;
// From one color to another, by "amount" (0 keeps the first).
QColor mix(const QColor& from, const QColor& to, double amount);
QColor track(int index);
}
// What a theme is made of: a few colors, from which every other is derived, and its gradients.
struct ThemeColors {
    QColor background = tk::rgb(0x0C0E12), accent = tk::rgb(0x6FE3B4), text = tk::rgb(0xE9EDF3);
    bool windowGradient = false; QColor windowEnd = tk::rgb(0x0C0E12);
    bool accentGradient = false; QColor accentEnd = tk::rgb(0x6FE3B4);
    int angle = 135;
    bool operator==(const ThemeColors& other) const;
};
struct ThemePreset { QString id, name; ThemeColors colors; };
// The themes offered as a start; the first is the default one.
QList<ThemePreset> themePresets();
// Every color of the interface from these, and the style sheet written again from them; what is
// open is drawn again.
void applyColors(const ThemeColors& colors);
// The style of a primary surface (main button, gauge): the accent, or its gradient.
QBrush accentBrush(const QRectF& box);
// Dark themes only, for now: a background too light is darkened, a text too dark is lightened.
ThemeColors darkTheme(ThemeColors colors);
// To and from the settings, where the default theme is written as no colors at all.
ThemeColors themeFromSettings(const ThemeSettings& settings);
ThemeSettings themeSettings(const ThemeColors& colors);
// A theme in one line of text, to send to a friend, and back from any text that holds one.
QString themeCode(const ThemeColors& colors);
bool readThemeCode(const QString& text, ThemeColors& colors);
// The logo in the colors of the theme, as a file Windows can show on a shortcut, written in this
// folder; older ones are removed. Empty for the default theme, whose icon is the program's own.
QString writeThemeIcon(const QString& directory);

QFont uiFont(int pixels, int weight = QFont::Normal);
QFont monoFont(int pixels, int weight = QFont::Normal);
// Monochrome 16 px line icons, drawn from the design's path data.
void paintIcon(QPainter& painter, const QString& name, const QRectF& box, const QColor& color);
QPixmap iconPixmap(const QString& name, const QColor& color, int size = 16, qreal ratio = 1);
QIcon icon(const QString& name, const QColor& color = tk::text2);
QIcon appIcon();
void paintLogo(QPainter& painter, const QRectF& box);
// Dynamic properties drive the stylesheet; the widget is polished again after a change.
void setStyleProperty(QWidget* widget, const char* name, const QVariant& value);
// A style sheet or a rich text written with the colors of the theme by name: {surface}, {line},
// {accent}, {text2}… {ground} is the background of the window, {veil} the same a little transparent.
QString themed(QString text);
// The same, kept by the widget: it is written again in the new colors whenever the theme changes.
void setThemedStyleSheet(QWidget* widget, const QString& sheet);
void setThemedText(QLabel* label, const QString& text);
void setThemedToolTip(QWidget* widget, const QString& tip);
}
