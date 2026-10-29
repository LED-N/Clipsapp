#include "quality_tiles.h"
#include "theme.h"
#include <QPainter>
#include <utility>
namespace clipsapp {
int closestVideoSize(int screenHeight) { int chosen = 0; for (int index = 0; index < int(std::size(videoSizes)); ++index) if (videoSizes[index].height <= screenHeight) chosen = index; return chosen; }
QualityTile::QualityTile(const QString& title, int height, QSizeF screen, int bars) : m_title(title), m_screen(screen), m_bars(bars) {
    setCheckable(true); setAttribute(Qt::WA_Hover); setCursor(Qt::PointingHandCursor); setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed); setFixedHeight(height); setAccessibleName(title);
}
void QualityTile::setLines(const QString& first, const QString& second) { m_first = first; m_second = second; update(); }
void QualityTile::paintEvent(QPaintEvent*) {
    QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing); if (!isEnabled()) painter.setOpacity(0.45);
    const bool on = isChecked(), over = underMouse() && isEnabled();
    painter.setPen(QPen(on ? tk::accent : over ? tk::lineHover : tk::line, 1)); painter.setBrush(on ? tk::accentSoft : over ? tk::surface : tk::field);
    painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 8, 8);
    const QColor mark = on ? tk::accent : over ? tk::text2 : tk::text3; const double right = width() - 12; double used = 0;
    if (!m_screen.isEmpty()) {
        used = m_screen.width(); const QRectF screen(right - m_screen.width(), (height() - m_screen.height()) / 2.0 - 2, m_screen.width(), m_screen.height());
        painter.setPen(QPen(mark, 1.5)); painter.setBrush(QColor(mark.red(), mark.green(), mark.blue(), on ? 50 : 18)); painter.drawRoundedRect(screen, 2, 2);
        painter.drawLine(QPointF(screen.center().x() - 4, screen.bottom() + 3.5), QPointF(screen.center().x() + 4, screen.bottom() + 3.5));
    }
    if (m_bars) {
        used = 3 * 5 + 2 * 3; painter.setPen(Qt::NoPen);
        for (int index = 0; index < 3; ++index) { const double tall = 7 + index * 5; painter.setBrush(index < m_bars ? mark : tk::line); painter.drawRoundedRect(QRectF(right - used + index * 8, height() / 2.0 + 8 - tall, 5, tall), 1.5, 1.5); }
    }
    const double left = 12, room = right - used - 10 - left; double y = 10;
    const auto titleFont = uiFont(14, QFont::Bold); const QFontMetrics titleMetrics(titleFont); painter.setFont(titleFont); painter.setPen(on ? tk::accent : tk::text);
    painter.drawText(QRectF(left, y, room, titleMetrics.height()), Qt::AlignLeft | Qt::AlignVCenter, titleMetrics.elidedText(m_title, Qt::ElideRight, int(room))); y += titleMetrics.height() + 2;
    const auto font = uiFont(12); const QFontMetrics metrics(font); painter.setFont(font);
    for (const auto& [line, color] : {std::pair{m_first, tk::text2}, std::pair{m_second, tk::text3}}) {
        if (line.isEmpty()) continue;
        painter.setPen(color); painter.drawText(QRectF(left, y, room, metrics.height()), Qt::AlignLeft | Qt::AlignVCenter, metrics.elidedText(line, Qt::ElideRight, int(room))); y += metrics.height();
    }
}
}
