#include "onboarding_page.h"
#include "widgets.h"
#include <QDir>
#include <QHBoxLayout>
#include <QKeySequenceEdit>
#include <QPainter>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <algorithm>

namespace clipsapp {
namespace {
class StepFrame final : public QFrame {
public:
    explicit StepFrame(int number) : m_number(number) { setObjectName(QStringLiteral("onboarding-step-%1").arg(number)); }
    void setState(int state) { m_state = state; update(); }
    int state() const { return m_state; }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing); const bool current = m_state == 1, done = m_state == 2;
        painter.setPen(QPen(current ? tk::accentLine : tk::line, 1)); painter.setBrush(current ? QColor(0x12, 0x1A, 0x1A) : done ? tk::surface : QColor(Qt::transparent));
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 10, 10);
        const QRectF mark(16, 14, 26, 26);
        if (done) { painter.setPen(Qt::NoPen); painter.setBrush(tk::accent); painter.drawEllipse(mark); painter.setPen(QPen(tk::onAccent, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin)); painter.drawPolyline(QPolygonF{{24.5, 27.5}, {27.5, 30.5}, {33.5, 24}}); return; }
        painter.setPen(QPen(current ? tk::accent : tk::control, current ? 2 : 1.5)); painter.setBrush(Qt::NoBrush); painter.drawEllipse(mark.adjusted(1, 1, -1, -1));
        painter.setFont(uiFont(12, QFont::Bold)); painter.setPen(current ? tk::accent : tk::text3); painter.drawText(mark, Qt::AlignCenter, QString::number(m_number));
    }
private:
    int m_number, m_state = 0;
};
}
OnboardingPage::OnboardingPage(AppConfig& config, QWidget* parent) : QWidget(parent), m_config(config) {
    setObjectName("onboarding"); auto* outer = new QHBoxLayout(this); outer->setContentsMargins(24, 0, 24, 0); outer->addStretch();
    auto* column = new QWidget; column->setFixedWidth(660); outer->addWidget(column); outer->addStretch();
    auto* layout = new QVBoxLayout(column); layout->setContentsMargins(0, 36, 0, 24); layout->setSpacing(12);
    auto* intro = new QVBoxLayout; intro->setSpacing(6); m_progress = caption({}); m_progress->setStyleSheet(QStringLiteral("color: #6FE3B4;")); intro->addWidget(m_progress);
    intro->addWidget(label(QStringLiteral("Bienvenue dans Clipsapp"), "display")); auto* lead = label(QStringLiteral("Quatre choix, puis le replay peut tourner. Vos clips restent sur ce PC."), "t2"); lead->setFont(uiFont(14)); intro->addWidget(lead);
    layout->addLayout(intro); layout->addSpacing(10);
    const QStringList titles{QStringLiteral("Dossier des clips"), QStringLiteral("Ce que vous capturez"), QStringLiteral("Les sons"), QStringLiteral("Raccourci de sauvegarde")};
    QList<QVBoxLayout*> bodies; QList<QHBoxLayout*> heads;
    for (int index = 0; index < 4; ++index) {
        auto* frame = new StepFrame(index + 1); auto* frameLayout = new QVBoxLayout(frame); frameLayout->setContentsMargins(56, 14, 16, 14); frameLayout->setSpacing(12);
        auto* head = new QHBoxLayout; head->setSpacing(12); auto* texts = new QVBoxLayout; texts->setSpacing(1); texts->addWidget(label(titles[index], "bold"));
        auto* summary = new ElideLabel({}, "small", Qt::ElideMiddle); texts->addWidget(summary); head->addLayout(texts, 1);
        auto* change = new Button(QStringLiteral("Changer"), Button::Ghost, Button::Small); change->setObjectName(QStringLiteral("onboarding-change-%1").arg(index)); head->addWidget(change, 0, Qt::AlignTop); frameLayout->addLayout(head);
        auto* body = new QWidget; auto* bodyLayout = new QVBoxLayout(body); bodyLayout->setContentsMargins(0, 0, 0, 0); bodyLayout->setSpacing(12); frameLayout->addWidget(body);
        m_steps.append(frame); m_summaries.append(summary); m_bodies.append(body); m_changes.append(change); bodies.append(bodyLayout); heads.append(head); layout->addWidget(frame);
        connect(change, &QPushButton::clicked, this, [this, index] { go(index); });
    }
    const auto next = [this](QVBoxLayout* body, int index, Button** out = nullptr) {
        auto* row = new QHBoxLayout; row->addStretch(); auto* button = new Button(QStringLiteral("Continuer"), Button::Primary); button->setTrailingIcon(QStringLiteral("next")); button->setObjectName(QStringLiteral("onboarding-next-%1").arg(index));
        row->addWidget(button); body->addLayout(row); connect(button, &QPushButton::clicked, this, [this, index] { go(index + 1); }); if (out) *out = button;
    };
    // 1 · folder
    auto* folderRow = new QHBoxLayout; folderRow->setSpacing(12); m_folder = new ElideLabel({}, nullptr, Qt::ElideMiddle); m_folder->setFont(monoFont(12)); folderRow->addWidget(m_folder, 1);
    auto* chooseFolder = new Button(QStringLiteral("folder"), QStringLiteral("Choisir un autre dossier"), Button::Secondary, Button::Small); folderRow->addWidget(chooseFolder); bodies[0]->addLayout(folderRow); next(bodies[0], 0);
    connect(chooseFolder, &QPushButton::clicked, this, &OnboardingPage::chooseFolderRequested);
    // 2 · screen or window
    m_screens = new QHBoxLayout; m_screens->setSpacing(10); bodies[1]->addLayout(m_screens);
    m_windows = new ChoiceBox; m_windows->setObjectName("onboarding-windows"); m_windows->hide(); bodies[1]->addWidget(m_windows); next(bodies[1], 1, &m_targetNext);
    connect(m_windows, &QComboBox::currentIndexChanged, this, [this] {
        if (!m_windows->isVisible()) return;
        profile().video.targetId = m_windows->currentData().toString(); profile().video.targetLabel = profile().video.targetId.isEmpty() ? QString() : m_windows->currentText(); refresh();
    });
    // 3 · sounds
    bodies[2]->addWidget(label(QStringLiteral("Le micro suit le périphérique par défaut. « Jeu » enregistre tout ce qui joue du son, sauf les applications écoutées à part. Chaque piste se règle ensuite dans l'éditeur ; les sources se modifient dans Réglages › Audio."), "small", true)); next(bodies[2], 2);
    // 4 · hotkey
    m_capsSummary = new Keycaps; heads[3]->insertWidget(1, m_capsSummary, 0, Qt::AlignVCenter);
    auto* hotkeyRow = new QHBoxLayout; hotkeyRow->setSpacing(12); m_caps = new Keycaps; hotkeyRow->addWidget(m_caps);
    m_hotkey = new QKeySequenceEdit; m_hotkey->setMaximumSequenceLength(1); m_hotkey->hide(); hotkeyRow->addWidget(m_hotkey);
    auto* changeKeys = new Button(QStringLiteral("Changer"), Button::Secondary, Button::Small); hotkeyRow->addWidget(changeKeys); hotkeyRow->addStretch(); bodies[3]->addLayout(hotkeyRow);
    auto* done = new QHBoxLayout; done->addStretch(); auto* finishStep = new Button(QStringLiteral("check"), QStringLiteral("C'est bon"), Button::Primary); finishStep->setObjectName("onboarding-next-3"); done->addWidget(finishStep); bodies[3]->addLayout(done);
    connect(finishStep, &QPushButton::clicked, this, [this] { go(4); });
    connect(changeKeys, &QPushButton::clicked, this, [this] { m_caps->hide(); m_hotkey->show(); m_hotkey->setFocus(); });
    connect(m_hotkey, &QKeySequenceEdit::keySequenceChanged, this, [this](const QKeySequence& keys) {
        if (keys.isEmpty()) return;
        profile().replay.hotkey = keys.toString(QKeySequence::PortableText); m_hotkey->hide(); m_caps->show(); refresh();
    });
    // The application has no installer: this is where it offers to appear in the Start menu.
    m_startMenu = new Toggle(QStringLiteral("Ajouter Clipsapp au menu Démarrer, pour le retrouver rapidement")); m_startMenu->setObjectName("onboarding-start-menu"); m_startMenu->setChecked(true); m_startMenu->hide(); layout->addWidget(m_startMenu);
    layout->addSpacing(8);
    auto* bottom = new QHBoxLayout; auto* skip = new Button(QStringLiteral("Passer, je réglerai plus tard"), Button::Ghost); skip->setObjectName("onboarding-skip"); bottom->addWidget(skip); bottom->addStretch();
    m_start = new Button(QStringLiteral("rec"), QStringLiteral("Démarrer le replay"), Button::Primary, Button::Large); m_start->setObjectName("onboarding-start"); bottom->addWidget(m_start); layout->addLayout(bottom); layout->addStretch();
    connect(skip, &QPushButton::clicked, this, [this] { emit finished(false); }); connect(m_start, &QPushButton::clicked, this, [this] { emit finished(true); });
    refresh();
}
RecordingProfile& OnboardingPage::profile() { if (auto* active = m_config.activeProfile()) return *active; return m_config.profiles.first(); }
void OnboardingPage::setStartMenu(bool managed) { m_startMenu->setVisible(managed); }
bool OnboardingPage::startMenuWanted() const { return !m_startMenu->isHidden() && m_startMenu->isChecked(); }
void OnboardingPage::go(int step) { m_step = std::clamp(step, 0, 4); refresh(); }
void OnboardingPage::setCatalog(const SourceCatalog& catalog) {
    m_catalog = catalog;
    while (auto* child = m_screens->takeAt(0)) { if (child->widget()) child->widget()->deleteLater(); delete child; }
    m_targets.clear(); QSignalBlocker blocker(m_windows); m_windows->clear(); m_windows->addItem(QStringLiteral("Choisir une fenêtre…"), QString()); int screens = 0, windows = 0;
    for (const auto& target : catalog.videoTargets) {
        if (!target.id.startsWith(QLatin1String("screen:"))) { m_windows->addItem(target.label, target.id); ++windows; continue; }
        if (screens >= 3) continue;
        const auto parts = target.label.split(QStringLiteral(" · ")); const bool primary = target.label.endsWith(QStringLiteral("principal"));
        auto* tile = new Tile(QStringLiteral("Écran %1%2").arg(++screens).arg(primary ? QStringLiteral(" · principal") : QString()), parts.value(1)); tile->setPicture(QStringLiteral("monitor"), false); tile->setProperty("target", target.id); tile->setProperty("targetLabel", target.label);
        m_screens->addWidget(tile); m_targets.append(tile);
        connect(tile, &QPushButton::clicked, this, [this, tile] { profile().video.targetId = tile->property("target").toString(); profile().video.targetLabel = tile->property("targetLabel").toString(); m_windows->hide(); refresh(); });
    }
    auto* window = new Tile(QStringLiteral("Une fenêtre…"), windows > 1 ? QStringLiteral("%1 fenêtres ouvertes").arg(windows) : windows == 1 ? QStringLiteral("1 fenêtre ouverte") : QStringLiteral("Aucune fenêtre ouverte")); window->setPicture(QStringLiteral("window"), true); window->setEnabled(windows > 0);
    m_screens->addWidget(window); m_targets.append(window); connect(window, &QPushButton::clicked, this, [this] { m_windows->show(); m_windows->showPopup(); refresh(); });
    refresh();
}
void OnboardingPage::refresh() {
    const auto& p = profile(); const auto keys = QKeySequence::fromString(p.replay.hotkey, QKeySequence::PortableText);
    m_progress->setText(m_step < 4 ? QStringLiteral("ÉTAPE %1 SUR 4").arg(m_step + 1) : QStringLiteral("TOUT EST PRÊT"));
    for (int index = 0; index < 4; ++index) {
        const bool current = index == m_step, done = index < m_step; static_cast<StepFrame*>(m_steps[index])->setState(current ? 1 : done ? 2 : 0);
        m_bodies[index]->setVisible(current); m_changes[index]->setVisible(done);
    }
    m_folder->setText(QDir::toNativeSeparators(m_config.libraryPath)); static_cast<ElideLabel*>(m_folder)->setFullText(QDir::toNativeSeparators(m_config.libraryPath));
    QStringList names; for (const auto& source : p.sources) names.append(source.name);
    const auto sounds = names.size() > 1 ? names.mid(0, names.size() - 1).join(QStringLiteral(", ")) + QStringLiteral(" et ") + names.last() : names.join(QString());
    const QStringList summaries{m_step == 0 ? QStringLiteral("Vos clips y seront rangés, un sous-dossier par jeu.") : QDir::toNativeSeparators(m_config.libraryPath),
        m_step > 1 && !p.video.targetLabel.isEmpty() ? p.video.targetLabel : QStringLiteral("Capturer l'écran entier évite le cadre jaune ajouté par Windows."),
        QStringLiteral("%1 : %2 sur %3 piste%4 séparée%4").arg(m_step > 2 ? QStringLiteral("Retenu") : QStringLiteral("Proposé"), sounds).arg(p.tracks.size()).arg(p.tracks.size() > 1 ? "s" : ""),
        QStringLiteral("Garde les %1 dernières secondes").arg(p.replay.durationSeconds)};
    for (int index = 0; index < 4; ++index) static_cast<ElideLabel*>(m_summaries[index])->setFullText(summaries[index]);
    const bool windowTarget = p.video.targetId.startsWith(QLatin1String("window:"));
    for (auto* tile : m_targets) tile->setChecked(tile->property("target").isValid() ? tile->property("target").toString() == p.video.targetId : windowTarget || m_windows->isVisible());
    if (windowTarget) { QSignalBlocker blocker(m_windows); m_windows->setCurrentIndex(std::max(0, m_windows->findData(p.video.targetId))); m_windows->setVisible(true); }
    m_targetNext->setEnabled(!p.video.targetId.isEmpty()); m_caps->setSequence(keys); m_capsSummary->setSequence(keys); m_capsSummary->setVisible(m_step != 3);
    m_start->setEnabled(m_step >= 4 && !p.video.targetId.isEmpty()); m_start->setToolTip(m_start->isEnabled() ? QString() : QStringLiteral("Terminez les quatre étapes pour démarrer"));
}
}
