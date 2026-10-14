#include "replay_strip.h"
#include "widgets.h"
#include <QHBoxLayout>
#include <QLocale>
#include <QPainter>
#include <QResizeEvent>
#include <cmath>

namespace clipsapp {
namespace {
class Brand final : public QWidget {
public:
    explicit Brand(QWidget* parent = nullptr) : QWidget(parent) { setFixedSize(186, 40); }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this); paintLogo(painter, QRectF(0, 7, 26, 26));
        auto font = uiFont(16, QFont::Bold); font.setLetterSpacing(QFont::PercentageSpacing, 99); painter.setFont(font); painter.setPen(tk::text);
        painter.drawText(QRectF(36, 0, width() - 36, height()), Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("Clipsapp"));
    }
};
// Title over one line of detail; both give way with an ellipsis when the window narrows.
class TwoLines final : public QWidget {
public:
    explicit TwoLines(QWidget* parent = nullptr) : QWidget(parent) { setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed); }
    void set(const QString& title, const QString& detail) { if (title == m_title && detail == m_detail) return; m_title = title; m_detail = detail; updateGeometry(); update(); }
    QSize sizeHint() const override { return {std::max(QFontMetrics(uiFont(13, QFont::Bold)).horizontalAdvance(m_title), QFontMetrics(uiFont(12)).horizontalAdvance(m_detail)) + 2, 36}; }
    QSize minimumSizeHint() const override { return {QFontMetrics(uiFont(13, QFont::Bold)).horizontalAdvance(m_title.section(QStringLiteral(" · "), 0, 0)) + 2, 36}; }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this); const auto titleFont = uiFont(13, QFont::Bold); painter.setFont(titleFont); painter.setPen(tk::text);
        const bool two = !m_detail.isEmpty();
        painter.drawText(QRectF(0, two ? 1 : 0, width(), two ? 17 : height()), Qt::AlignVCenter | Qt::AlignLeft, QFontMetrics(titleFont).elidedText(m_title, Qt::ElideRight, width()));
        if (!two) return;
        const auto detailFont = uiFont(12); painter.setFont(detailFont); painter.setPen(tk::text2);
        painter.drawText(QRectF(0, 18, width(), 16), Qt::AlignVCenter | Qt::AlignLeft, QFontMetrics(detailFont).elidedText(m_detail, Qt::ElideRight, width()));
    }
private:
    QString m_title, m_detail;
};
class StatusBox final : public QWidget {
public:
    using QWidget::QWidget;
    void set(const QColor& background, const QColor& border, const QColor& dot, bool ring) { m_background = background; m_border = border; m_dot = dot; m_ring = ring; update(); }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing); painter.setPen(QPen(m_border, 1)); painter.setBrush(m_background);
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 8, 8);
        const QRectF dot(14, (height() - 10) / 2.0, 10, 10);
        if (m_ring) { painter.setPen(QPen(m_dot, 2)); painter.setBrush(Qt::NoBrush); painter.drawEllipse(dot.adjusted(1, 1, -1, -1)); }
        else { painter.setPen(Qt::NoPen); painter.setBrush(m_dot); painter.drawEllipse(dot); }
    }
private:
    QColor m_background = tk::surface, m_border = tk::line, m_dot = tk::text3; bool m_ring = true;
};
QString mebibytes(qint64 bytes) {
    const QLocale french(QLocale::French, QLocale::France); const double value = bytes / 1048576.0;
    return (value >= 1024 ? french.toString(value / 1024, 'f', 2) + QStringLiteral(" Gio") : french.toString(value, 'f', value >= 100 ? 0 : 1) + QStringLiteral(" Mio"));
}
}
ReplayStrip::ReplayStrip(QWidget* parent) : QFrame(parent) {
    setObjectName("strip"); setFixedHeight(60); setStyleSheet(QStringLiteral("QFrame#strip { background: #101318; border: none; border-bottom: 1px solid #242A34; }"));
    auto* layout = new QHBoxLayout(this); layout->setContentsMargins(18, 0, 16, 1); layout->setSpacing(12); layout->addWidget(new Brand);
    auto* status = new StatusBox; m_status = status; status->setObjectName("replay-status"); status->setFixedHeight(40);
    auto* inner = new QHBoxLayout(status); inner->setContentsMargins(36, 0, 14, 0); inner->setSpacing(10);
    m_texts = new TwoLines; inner->addWidget(m_texts);
    m_fix = new Button(QStringLiteral("Capturer l'écran entier"), Button::Secondary, Button::Small); m_fix->setObjectName("replay-capture-screen"); inner->addWidget(m_fix); inner->addStretch();
    m_gauge = new Bar; m_gauge->setFixedWidth(160); inner->addWidget(m_gauge);
    m_seconds = monoLabel({}, 12, QColor(0xC9, 0xF3, 0xE1)); m_seconds->setObjectName("replay-seconds"); inner->addWidget(m_seconds);
    m_bytes = monoLabel({}, 12, tk::text2); inner->addWidget(m_bytes); layout->addWidget(status, 1);
    m_primary = new Button(QString(), Button::Primary); m_primary->setObjectName("replay-primary"); layout->addWidget(m_primary);
    m_secondary = new Button(QString(), Button::Secondary); m_secondary->setObjectName("replay-secondary"); layout->addWidget(m_secondary);
    connect(m_fix, &QPushButton::clicked, this, &ReplayStrip::fullScreenRequested);
    connect(m_primary, &QPushButton::clicked, this, [this] {
        if (m_view.state == ReplayView::Running || m_view.state == ReplayView::Starting) emit saveRequested(); else emit startRequested();
    });
    connect(m_secondary, &QPushButton::clicked, this, [this] { if (m_view.state == ReplayView::Error) emit settingsRequested(); else emit stopRequested(); });
    refresh();
}
void ReplayStrip::apply(const ReplayView& view) { m_view = view; refresh(); }
void ReplayStrip::resizeEvent(QResizeEvent* event) {
    QFrame::resizeEvent(event);
    const bool compact = event->size().width() < 1100; if (compact != m_compact) { m_compact = compact; refresh(); }
}
void ReplayStrip::refresh() {
    const auto& v = m_view; auto* status = static_cast<StatusBox*>(m_status); auto* texts = static_cast<TwoLines*>(m_texts);
    const bool live = v.state == ReplayView::Running, warned = live && !v.warning.isEmpty();
    QString title, detail;
    switch (v.state) {
    case ReplayView::Idle: title = QStringLiteral("Replay arrêté"); detail = v.blockedReason.isEmpty() ? v.profileSummary : v.blockedReason; status->set(tk::surface, tk::line, QColor(0x6E, 0x77, 0x87), true); break;
    case ReplayView::Starting: title = QStringLiteral("Démarrage du replay…"); detail = v.liveSummary; status->set(tk::surface, QColor(0x2A, 0x31, 0x3C), tk::warn, true); break;
    case ReplayView::Stopping: title = QStringLiteral("Arrêt du replay…"); detail = QStringLiteral("Libération de la mémoire"); status->set(tk::surface, QColor(0x2A, 0x31, 0x3C), tk::text3, true); break;
    case ReplayView::Error: title = QStringLiteral("Le replay s'est arrêté"); detail = v.error; status->set(tk::dangerBg, tk::dangerLine, tk::danger, false); break;
    case ReplayView::Running:
        if (warned) { title = QStringLiteral("Replay actif · ") + v.warningTitle; detail = v.warning; status->set(tk::warnBg, tk::warnLine, tk::warn, false); }
        else { title = QStringLiteral("Replay actif"); detail = v.saving ? QStringLiteral("Écriture du clip sur le disque…") : v.liveSummary; status->set(tk::liveBg, tk::liveLine, tk::accent, false); }
        break;
    }
    texts->set(title, m_compact ? QString() : detail); m_status->setToolTip(m_compact || v.state == ReplayView::Error ? detail : QString());
    m_fix->setVisible(warned && v.canFixWarning && !m_compact);
    // A warning needs the room: its sentence and its remedy come before the size of the buffer.
    const bool brief = m_compact || warned;
    m_gauge->setVisible(live); m_seconds->setVisible(live); m_bytes->setVisible(live && !brief); m_gauge->setFixedWidth(brief ? 72 : 160);
    if (warned) m_status->setToolTip(detail);
    if (live) {
        const double capacity = std::max(1.0, v.capacity); const bool full = v.seconds >= capacity - 0.75; const int seconds = int(std::min(capacity, std::floor(v.seconds + (full ? 0.75 : 0))));
        m_gauge->setValue(full ? 1 : v.seconds / capacity); m_gauge->setColors(warned ? tk::warn : tk::accent);
        m_seconds->setText(brief ? QStringLiteral("%1 s").arg(seconds) : full ? QStringLiteral("%1 s prêtes").arg(seconds) : QStringLiteral("%1 s sur %2 s").arg(seconds).arg(int(capacity)));
        m_bytes->setText(mebibytes(v.bytes));
    }
    const bool saveRole = v.state == ReplayView::Running || v.state == ReplayView::Starting;
    if (saveRole) {
        m_primary->setIconName(QStringLiteral("save")); m_primary->setText(v.saving ? QStringLiteral("Sauvegarde…") : m_compact ? QStringLiteral("Sauvegarder") : QStringLiteral("Sauvegarder le clip"));
        m_primary->setKbd(m_compact ? QString() : QString(v.hotkey).replace('+', ' ')); m_primary->setEnabled(v.canSave && !v.saving);
        m_primary->setToolTip(v.state == ReplayView::Starting ? QStringLiteral("Le replay démarre") : v.saving ? QStringLiteral("Un clip est en cours d'écriture") : !v.canSave ? QStringLiteral("Le buffer attend sa première image") : QString());
    } else {
        m_primary->setKbd({}); m_primary->setIconName(v.state == ReplayView::Error ? QStringLiteral("refresh") : QStringLiteral("rec"));
        m_primary->setText(v.state == ReplayView::Error ? QStringLiteral("Réessayer") : QStringLiteral("Démarrer le replay"));
        m_primary->setEnabled(v.canStart && v.state != ReplayView::Stopping); m_primary->setToolTip(v.canStart ? QString() : v.blockedReason);
    }
    m_secondary->setVisible(saveRole || v.state == ReplayView::Error);
    if (v.state == ReplayView::Error) { m_secondary->setIconName({}); m_secondary->setText(QStringLiteral("Voir les réglages")); }
    else if (v.state == ReplayView::Starting) { m_secondary->setIconName({}); m_secondary->setText(QStringLiteral("Annuler")); }
    else { m_secondary->setIconName(QStringLiteral("stop")); m_secondary->setText(m_compact ? QString() : QStringLiteral("Arrêter")); m_secondary->setToolTip(QStringLiteral("Arrêter le replay")); }
    m_primary->updateGeometry(); m_secondary->updateGeometry();
}
}
