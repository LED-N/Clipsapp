#pragma once
#include <QColor>
#include <QFont>
#include <QIcon>
#include <QPainterPath>
#include <QString>
class QApplication;
class QPainter;
class QWidget;
namespace clipsapp {
void applyTheme(QApplication& application);

// Design tokens. One dark theme; no shadow, blur or stacked transparency.
namespace tk {
constexpr QColor rgb(unsigned value, int alpha = 255) { return QColor(int(value >> 16 & 255), int(value >> 8 & 255), int(value & 255), alpha); }
inline constexpr QColor window = rgb(0x0C0E12), rail = rgb(0x101318), surface = rgb(0x14181E), raised = rgb(0x1A1F27), hover = rgb(0x212732), press = rgb(0x282F3B), field = rgb(0x0F1217), header = rgb(0x111419);
inline constexpr QColor line = rgb(0x242A34), lineSoft = rgb(0x1E242C), lineStrong = rgb(0x323A47), lineHover = rgb(0x3B4553), control = rgb(0x4A5464), chip = rgb(0x1F242D), selected = rgb(0x262D38);
inline constexpr QColor text = rgb(0xE9EDF3), text2 = rgb(0xA1AABA), text3 = rgb(0x8A94A5), textOff = rgb(0x566070);
inline constexpr QColor accent = rgb(0x6FE3B4), accentHover = rgb(0x8BEBC4), accentPress = rgb(0x55CFA0), onAccent = rgb(0x05261A), accentSoft = rgb(0x12302A), accentLine = rgb(0x2F7A5F), liveBg = rgb(0x11251F), liveLine = rgb(0x1D3F33);
inline constexpr QColor warn = rgb(0xF2B35E), warnSoft = rgb(0x352812), warnBg = rgb(0x241D10), warnLine = rgb(0x4D3B1C);
inline constexpr QColor danger = rgb(0xF2777C), dangerSoft = rgb(0x3A1C1F), dangerBg = rgb(0x2A171A), dangerLine = rgb(0x5A2A30);
inline constexpr QColor info = rgb(0x8FB8FF), infoSoft = rgb(0x1A2640), infoLine = rgb(0x2B3D63);
QColor track(int index);
}

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
}
