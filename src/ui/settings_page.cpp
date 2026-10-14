#include "settings_page.h"
#include "source_dialog.h"
#include "widgets.h"
#include <QDir>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeySequenceEdit>
#include <QLineEdit>
#include <QLocale>
#include <QPainter>
#include <QScrollArea>
#include <QSet>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <algorithm>

namespace clipsapp {
namespace {
// A level above zero holds that constant quality, and the bitrate becomes its ceiling.
struct Quality { const char* name; int width, height, fps, bitrate, level; };
const Quality qualities[]{{"Légère", 1920, 1080, 30, 12, 0}, {"Équilibrée", 1920, 1080, 60, 25, 0}, {"Haute", 2560, 1440, 60, 50, 0}, {"Maximale", 2560, 1440, 60, 200, 14}};
struct Level { int value; const char* name; };
const Level levels[]{{26, "Légère"}, {23, "Bonne"}, {20, "Haute"}, {17, "Très haute"}, {14, "Quasi sans perte"}};
QString help(const char* text) { return QStringLiteral("<qt>%1</qt>").arg(QString::fromUtf8(text).toHtmlEscaped()); }
QString grouped(int value) { return QLocale(QLocale::French, QLocale::France).toString(value); }
class Swatch final : public QWidget {
public:
    explicit Swatch(const QColor& color) : m_color(color) { setFixedSize(10, 10); }
protected:
    void paintEvent(QPaintEvent*) override { QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing); painter.setPen(Qt::NoPen); painter.setBrush(m_color); painter.drawRoundedRect(rect(), 3, 3); }
private:
    QColor m_color;
};
QVBoxLayout* field(const QString& title, QWidget* widget) { auto* layout = new QVBoxLayout; layout->setSpacing(6); layout->addWidget(label(title, "lbl")); layout->addWidget(widget); return layout; }
QFrame* section(const QString& title, QVBoxLayout*& body, Tag** tag = nullptr) {
    auto* frame = card(); auto* layout = new QVBoxLayout(frame); layout->setContentsMargins(16, 14, 16, 16); layout->setSpacing(14);
    auto* head = new QHBoxLayout; head->setSpacing(10); head->addWidget(label(title, "h2")); head->addStretch();
    if (tag) { *tag = new Tag(QStringLiteral("Modifié"), Tag::Warn); (*tag)->hide(); head->addWidget(*tag); }
    layout->addLayout(head); body = layout; return frame;
}
QWidget* scrolled(QWidget* content) { auto* scroll = new QScrollArea; scroll->setWidgetResizable(true); scroll->setWidget(content); scroll->setFrameShape(QFrame::NoFrame); scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); return scroll; }
QString sourceIcon(SourceKind kind) { return kind == SourceKind::Microphone ? QStringLiteral("mic") : kind == SourceKind::Application ? QStringLiteral("app") : kind == SourceKind::Output ? QStringLiteral("phones") : QStringLiteral("apps"); }
}
SettingsPage::SettingsPage(AppConfig& config, const QString& settingsPath, QWidget* parent) : QWidget(parent), m_config(config), m_saved(config) {
    auto* layout = new QVBoxLayout(this); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(0);
    auto* head = new QVBoxLayout; head->setContentsMargins(24, 20, 24, 0); head->setSpacing(10);
    auto* top = new QHBoxLayout; top->setSpacing(10); auto* titles = new QVBoxLayout; titles->setSpacing(2);
    titles->addWidget(label(QStringLiteral("Réglages"), "title")); titles->addWidget(label(QStringLiteral("Ils s'appliquent aux prochains clips."), "t2")); top->addLayout(titles, 1);
    top->addWidget(label(QStringLiteral("Profil"), "lbl")); m_profiles = new ChoiceBox; m_profiles->setObjectName("profiles"); m_profiles->setFixedWidth(180); top->addWidget(m_profiles);
    m_duplicate = new Button(QStringLiteral("copy"), QStringLiteral("Dupliquer")); m_duplicate->setToolTip(QStringLiteral("Créer un profil à partir de celui-ci")); top->addWidget(m_duplicate); head->addLayout(top);
    auto* tabRow = new QHBoxLayout; tabRow->setSpacing(26); m_tabs = new QStackedWidget;
    const QList<QPair<QString, QString>> tabs{{QStringLiteral("monitor"), QStringLiteral("Capture")}, {QStringLiteral("speaker"), QStringLiteral("Audio")}, {QStringLiteral("folder"), QStringLiteral("Fichiers et alertes")}};
    for (int index = 0; index < tabs.size(); ++index) {
        auto* tab = new TabButton(tabs[index].first, tabs[index].second); tab->setObjectName(QStringLiteral("settings-tab-%1").arg(index)); tab->setChecked(index == 0); tabRow->addWidget(tab); m_tabButtons.append(tab);
        connect(tab, &QPushButton::clicked, this, [this, index] { showTab(index); });
    }
    tabRow->addStretch(); head->addLayout(tabRow); layout->addLayout(head); layout->addWidget(separator());
    auto* lockHost = new QVBoxLayout; lockHost->setContentsMargins(24, 16, 24, 0);
    m_lock = new Banner(Banner::Info, QStringLiteral("lock")); m_lock->setObjectName("settings-locked"); m_lock->setTexts(QStringLiteral("Le replay utilise ce profil"), QStringLiteral("Les réglages restent en lecture seule jusqu'à l'arrêt du replay. Rien n'est perdu."));
    auto* stop = new Button(QStringLiteral("stop"), QStringLiteral("Arrêter le replay pour modifier")); m_lock->actions()->addWidget(stop); m_lock->hide(); lockHost->addWidget(m_lock); layout->addLayout(lockHost);
    m_tabs->addWidget(buildCaptureTab()); m_tabs->addWidget(buildAudioTab()); m_tabs->addWidget(buildFilesTab(settingsPath)); layout->addWidget(m_tabs, 1);

    m_saveBar = new QFrame; m_saveBar->setObjectName("savebar"); m_saveBar->setFixedHeight(60); m_saveBar->setStyleSheet(QStringLiteral("QFrame#savebar { background: #14181E; border: none; border-top: 1px solid #3B4553; }"));
    auto* bar = new QHBoxLayout(m_saveBar); bar->setContentsMargins(24, 0, 24, 0); bar->setSpacing(10);
    auto* dot = new IconBox(QString(), tk::warn, tk::warn, 8); dot->setRound(true); bar->addWidget(dot);
    m_dirtyTitle = label({}, "strong"); m_dirtyTitle->setObjectName("settings-dirty"); bar->addWidget(m_dirtyTitle); m_dirtyDetail = new ElideLabel({}, "t2"); bar->addWidget(m_dirtyDetail, 1);
    auto* revert = new Button(QStringLiteral("Annuler")); revert->setObjectName("settings-revert"); bar->addWidget(revert);
    m_save = new Button(QStringLiteral("check"), QStringLiteral("Enregistrer"), Button::Primary); m_save->setObjectName("settings-save"); bar->addWidget(m_save); m_saveBar->hide(); layout->addWidget(m_saveBar);

    connect(stop, &QPushButton::clicked, this, &SettingsPage::stopReplayRequested);
    connect(revert, &QPushButton::clicked, this, &SettingsPage::revertRequested);
    connect(m_save, &QPushButton::clicked, this, &SettingsPage::saveRequested);
    connect(m_duplicate, &QPushButton::clicked, this, [this] { duplicateProfile(); });
    connect(m_profiles, &QComboBox::currentIndexChanged, this, [this] {
        if (m_loading) return;
        collect(); m_config.activeProfileId = m_profiles->currentData().toString(); load(); emit edited();
    });
}
RecordingProfile& SettingsPage::profile() {
    if (auto* active = m_config.activeProfile()) return *active;
    return m_config.profiles.first();
}
QWidget* SettingsPage::buildCaptureTab() {
    auto* content = new QWidget; auto* columns = new QHBoxLayout(content); columns->setContentsMargins(24, 18, 24, 18); columns->setSpacing(16);
    auto* left = new QVBoxLayout; left->setSpacing(16); auto* right = new QVBoxLayout; right->setSpacing(16); columns->addLayout(left, 1); columns->addLayout(right, 1);

    QVBoxLayout* body; left->addWidget(section(QStringLiteral("Ce qui est capturé"), body, &m_captureTag));
    m_videoTarget = new ChoiceBox; m_videoTarget->setObjectName("video-target"); m_videoTarget->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon); m_videoTarget->setMinimumContentsLength(18); body->addLayout(field(QStringLiteral("Écran ou fenêtre"), m_videoTarget));
    m_gameName = new QLineEdit; m_gameName->setObjectName("recording-game"); m_gameName->setMaxLength(80); body->addLayout(field(QStringLiteral("Nom du jeu"), m_gameName));
    m_detectGame = new Toggle(QStringLiteral("Reconnaître le jeu capturé quand le nom est vide")); body->addWidget(m_detectGame);

    left->addWidget(section(QStringLiteral("Qualité"), body, &m_qualityTag));
    auto* presets = new QGridLayout; presets->setSpacing(8);
    for (const auto& quality : qualities) {
        const auto weight = quality.level ? QStringLiteral("Qualité constante") : QStringLiteral("%1 Mbit/s").arg(quality.bitrate);
        auto* tile = new Tile(QString::fromUtf8(quality.name), QStringLiteral("%1p · %2 FPS\n%3").arg(quality.height).arg(quality.fps).arg(weight)); tile->setObjectName(QStringLiteral("quality-%1").arg(m_presets.size()));
        presets->addWidget(tile, int(m_presets.size() / 2), int(m_presets.size() % 2)); m_presets.append(tile);
        // A preset only fills in the settings that already exist below.
        connect(tile, &QPushButton::clicked, this, [this, quality] {
            m_resolution->setCurrentIndex(m_resolution->findData(QStringLiteral("%1x%2").arg(quality.width).arg(quality.height))); m_fps->setCurrentIndex(m_fps->findData(quality.fps)); m_bitrate->setValue(quality.bitrate);
            m_rateControl->setCurrentIndex(m_rateControl->findData(quality.level ? "quality" : "bitrate")); if (quality.level) m_quality->setCurrentIndex(m_quality->findData(quality.level)); updateQuality();
        });
    }
    m_presets.last()->setToolTip(help("Très proche de l'écran, pour garder des clips à retravailler. Les clips sont lourds et le replay demande plus de mémoire ; vous pourrez toujours les compresser au moment du partage."));
    body->addLayout(presets);
    m_advancedToggle = new Button(QStringLiteral("next"), QStringLiteral("Réglages avancés"), Button::Ghost, Button::Small); m_advancedToggle->setObjectName("advanced-toggle"); m_advancedToggle->setPadding(4); body->addWidget(m_advancedToggle, 0, Qt::AlignLeft);
    m_advanced = new QWidget; auto* grid = new QGridLayout(m_advanced); grid->setContentsMargins(0, 0, 0, 0); grid->setHorizontalSpacing(12); grid->setVerticalSpacing(12);
    m_resolution = new ChoiceBox; m_resolution->addItem(QStringLiteral("1080p · 1920 × 1080"), "1920x1080"); m_resolution->addItem(QStringLiteral("1440p · 2560 × 1440"), "2560x1440"); m_resolution->addItem(QStringLiteral("4K · 3840 × 2160"), "3840x2160");
    m_fps = new ChoiceBox; m_fps->setObjectName("fps"); for (const int rate : {30, 60, 120, 144}) m_fps->addItem(QStringLiteral("%1 FPS").arg(rate), rate);
    m_codec = new ChoiceBox; m_codec->addItem(QStringLiteral("H.264 · compatible"), "h264"); m_codec->addItem(QStringLiteral("HEVC · plus compact"), "hevc");
    m_encoder = new ChoiceBox; m_encoder->addItem(QStringLiteral("Automatique"), "auto"); m_encoder->addItem("NVIDIA NVENC", "nvenc");
#ifdef Q_OS_WIN
    m_encoder->addItem("AMD AMF", "amf");
#else
    m_encoder->addItem("VAAPI · AMD / Intel", "vaapi");
#endif
    m_encoder->addItem("Intel Quick Sync", "qsv");
    m_rateControl = new ChoiceBox; m_rateControl->setObjectName("rate-control"); m_rateControl->addItem(QStringLiteral("Débit cible"), "bitrate"); m_rateControl->addItem(QStringLiteral("Qualité constante"), "quality");
    m_bitrate = new QSpinBox; m_bitrate->setObjectName("bitrate"); m_bitrate->setRange(5, 300); m_bitrate->setSuffix(QStringLiteral(" Mbit/s")); m_bitrate->setButtonSymbols(QAbstractSpinBox::NoButtons); m_bitrate->setFont(monoFont(13));
    m_quality = new ChoiceBox; m_quality->setObjectName("quality-level"); for (const auto& level : levels) m_quality->addItem(QString::fromUtf8(level.name), level.value);
    m_effort = new ChoiceBox; m_effort->setObjectName("effort"); m_effort->addItem(QStringLiteral("Rapide · léger pour le GPU"), "fast"); m_effort->addItem(QStringLiteral("Soigné · compresse mieux"), "thorough");
    m_cursor = new Toggle(QStringLiteral("Masqué dans les clips")); auto* cursorHost = new QWidget; cursorHost->setFixedHeight(32); auto* cursorLayout = new QHBoxLayout(cursorHost); cursorLayout->setContentsMargins(0, 0, 0, 0); cursorLayout->addWidget(m_cursor);
    // Every advanced setting explains itself where the pointer rests, on its name as on its field.
    const auto place = [grid](int row, int column, const QString& title, QWidget* widget, const char* text) {
        auto* name = label(title, "lbl"); name->setToolTip(help(text)); widget->setToolTip(help(text));
        auto* layout = new QVBoxLayout; layout->setSpacing(6); layout->addWidget(name); layout->addWidget(widget); grid->addLayout(layout, row, column); return name;
    };
    place(0, 0, QStringLiteral("Résolution"), m_resolution, "Taille de l'image enregistrée. Un écran plus grand ou plus petit est mis à cette taille par la carte graphique.");
    place(0, 1, QStringLiteral("Fréquence"), m_fps, "Nombre d'images par seconde. Plus il y en a, plus le mouvement est fluide : 120 ou 144 FPS suivent un écran rapide. À débit égal, chaque image reçoit moins de données, et la carte graphique travaille davantage ; si elle ne suit pas, la barre du replay le signale.");
    place(1, 0, QStringLiteral("Codec"), m_codec, "H.264 se lit partout. HEVC garde la même qualité avec moins de données, mais certains logiciels et navigateurs ne le lisent pas.");
    place(1, 1, QStringLiteral("Encodeur"), m_encoder, "Puce qui compresse la vidéo pendant le jeu. « Automatique » prend celle de votre carte graphique.");
    place(2, 0, QStringLiteral("Mode de débit"), m_rateControl, "Débit cible : le poids des clips et la mémoire du replay sont prévisibles. Qualité constante : chaque scène reçoit ce qu'il lui faut ; une scène chargée pèse plus, une scène calme moins.");
    m_bitrateLabel = place(2, 1, QStringLiteral("Débit vidéo"), m_bitrate, "Quantité de données par seconde de vidéo. Plus haut, l'image est plus fidèle, les clips plus lourds et le replay demande plus de mémoire. En qualité constante, c'est le plafond des scènes les plus chargées.");
    place(3, 0, QStringLiteral("Effort d'encodage"), m_effort, "Rapide : sollicite le moins la carte graphique pendant le jeu. Soigné : compresse mieux à débit égal, ce qui se voit surtout aux débits modestes ; la carte graphique travaille davantage.");
    m_qualityLabel = place(3, 1, QStringLiteral("Niveau de qualité"), m_quality, "Fidélité visée pour chaque image, en qualité constante. « Quasi sans perte » reste très proche de l'écran et donne des clips lourds, que vous pourrez toujours compresser au moment du partage.");
    place(4, 0, QStringLiteral("Curseur"), cursorHost, "Affiche ou masque le pointeur de la souris dans les clips.");
    grid->setColumnStretch(0, 1); grid->setColumnStretch(1, 1); body->addWidget(m_advanced); m_advanced->hide(); left->addStretch();

    right->addWidget(section(QStringLiteral("Replay"), body, &m_replayTag));
    auto* durationRow = new QHBoxLayout; durationRow->setSpacing(12); m_durationSlider = new QSlider(Qt::Horizontal); m_durationSlider->setRange(15, 600); m_durationSlider->setPageStep(15);
    m_duration = new QSpinBox; m_duration->setObjectName("replay-duration"); m_duration->setRange(15, 600); m_duration->setSuffix(QStringLiteral(" s")); m_duration->setFixedWidth(96); m_duration->setButtonSymbols(QAbstractSpinBox::NoButtons); m_duration->setFont(monoFont(13));
    durationRow->addWidget(m_durationSlider, 1); durationRow->addWidget(m_duration);
    auto* durationGroup = new QVBoxLayout; durationGroup->setSpacing(6); durationGroup->addWidget(label(QStringLiteral("Secondes gardées en mémoire"), "lbl")); durationGroup->addLayout(durationRow);
    durationGroup->addWidget(label(QStringLiteral("De 15 s à 10 min. C'est aussi la durée d'un clip sauvegardé."), "hint", true)); body->addLayout(durationGroup);
    m_memory = new ChoiceBox; m_memory->setObjectName("replay-memory"); m_memory->setFixedWidth(200); for (const int value : {256, 512, 1024, 2048, 4096, 8192}) m_memory->addItem(grouped(value) + QStringLiteral(" Mio"), value);
    body->addLayout(field(QStringLiteral("Plafond de mémoire"), m_memory));
    m_estimateCalm = new QWidget; auto* calm = new QVBoxLayout(m_estimateCalm); calm->setContentsMargins(0, 0, 0, 0); calm->setSpacing(6);
    m_estimateCalmText = label({}, "hint", true); m_estimateCalmText->setObjectName("memory-estimate"); m_estimateCalmBar = new Bar; calm->addWidget(m_estimateCalmText); calm->addWidget(m_estimateCalmBar); body->addWidget(m_estimateCalm);
    auto* warn = new QFrame; m_estimateWarn = warn; warn->setObjectName("memory-warning"); warn->setProperty("banner", "warn"); auto* warnLayout = new QVBoxLayout(warn); warnLayout->setContentsMargins(12, 12, 12, 12); warnLayout->setSpacing(8);
    auto* warnHead = new QHBoxLayout; warnHead->setSpacing(8); warnHead->addWidget(new IconLabel(QStringLiteral("warn"), tk::warn));
    m_estimateWarnTitle = label({}); m_estimateWarnTitle->setStyleSheet(QStringLiteral("color: #F2B35E; font-weight: 600;")); warnHead->addWidget(m_estimateWarnTitle, 1); m_estimateWarnLimit = monoLabel({}, 12, tk::text2); warnHead->addWidget(m_estimateWarnLimit);
    warnLayout->addLayout(warnHead); m_estimateWarnBar = new Bar(nullptr, 12); m_estimateWarnBar->setColors(tk::warn, QColor(0x3A, 0x2C, 0x16)); warnLayout->addWidget(m_estimateWarnBar);
    m_estimateWarnText = label({}, "warnText", true); warnLayout->addWidget(m_estimateWarnText); body->addWidget(warn);
    auto* hotkeyRow = new QHBoxLayout; hotkeyRow->setSpacing(12); m_hotkeyCaps = new Keycaps; hotkeyRow->addWidget(m_hotkeyCaps);
    m_hotkey = new QKeySequenceEdit; m_hotkey->setObjectName("replay-hotkey"); m_hotkey->setMaximumSequenceLength(1); m_hotkey->hide(); hotkeyRow->addWidget(m_hotkey);
    m_hotkeyChange = new Button(QStringLiteral("Changer"), Button::Secondary, Button::Small); hotkeyRow->addWidget(m_hotkeyChange); hotkeyRow->addStretch();
    auto* hotkeyGroup = new QVBoxLayout; hotkeyGroup->setSpacing(6); hotkeyGroup->addWidget(label(QStringLiteral("Raccourci pour sauvegarder"), "lbl")); hotkeyGroup->addLayout(hotkeyRow);
    m_hotkeyHint = label(QStringLiteral("Fonctionne même quand le jeu est au premier plan."), "hint", true); hotkeyGroup->addWidget(m_hotkeyHint); body->addLayout(hotkeyGroup); right->addStretch();

    connect(m_advancedToggle, &QPushButton::clicked, this, [this] { m_advanced->setVisible(!m_advanced->isVisible()); m_advancedToggle->setIconName(m_advanced->isVisible() ? QStringLiteral("down") : QStringLiteral("next")); });
    connect(m_gameName, &QLineEdit::textChanged, this, [this] { changed(); });
    for (auto* toggle : {m_detectGame, m_cursor}) connect(toggle, &QCheckBox::toggled, this, [this] { m_cursor->setText(m_cursor->isChecked() ? QStringLiteral("Visible dans les clips") : QStringLiteral("Masqué dans les clips")); changed(); });
    for (auto* combo : {m_videoTarget, m_resolution, m_fps, m_codec, m_encoder, m_rateControl, m_quality, m_effort, m_memory}) connect(combo, &QComboBox::currentIndexChanged, this, [this] { changed(); });
    connect(m_bitrate, &QSpinBox::valueChanged, this, [this] { changed(); });
    connect(m_duration, &QSpinBox::valueChanged, this, [this](int value) { QSignalBlocker blocker(m_durationSlider); m_durationSlider->setValue(value); changed(); });
    connect(m_durationSlider, &QSlider::valueChanged, m_duration, &QSpinBox::setValue);
    connect(m_hotkeyChange, &QPushButton::clicked, this, [this] {
        const bool editing = m_hotkey->isVisible();
        m_hotkey->setVisible(!editing); m_hotkeyCaps->setVisible(editing); m_hotkeyChange->setText(editing ? QStringLiteral("Changer") : QStringLiteral("Terminer"));
        m_hotkeyHint->setText(editing ? QStringLiteral("Fonctionne même quand le jeu est au premier plan.") : QStringLiteral("Appuyez sur la nouvelle combinaison de touches."));
        if (!editing) m_hotkey->setFocus();
    });
    connect(m_hotkey, &QKeySequenceEdit::keySequenceChanged, this, [this] { updateHotkey(); changed(); });
    return scrolled(content);
}
QWidget* SettingsPage::buildAudioTab() {
    auto* content = new QWidget; auto* layout = new QVBoxLayout(content); layout->setContentsMargins(24, 16, 24, 16); layout->setSpacing(10);
    auto* top = new QHBoxLayout; top->setSpacing(10);
    auto* intro = new QLabel(QStringLiteral("Chaque <b style='color:#E9EDF3'>piste</b> devient une ligne réglable dans l'éditeur. Les <b style='color:#E9EDF3'>sources</b> rangées dans une même piste sont mélangées pour de bon.")); intro->setProperty("role", "t2"); intro->setWordWrap(true); top->addWidget(intro, 1);
    intro->setText(intro->text() + QStringLiteral(" Le curseur d'une piste est son <b style='color:#E9EDF3'>volume habituel</b> dans les clips ; l'enregistrement, lui, garde tout à plein niveau."));
    auto* refresh = new Button(QStringLiteral("refresh"), QStringLiteral("Actualiser")); auto* add = new Button(QStringLiteral("plus"), QStringLiteral("Ajouter une source"), Button::Primary); add->setObjectName("add-source"); top->addWidget(refresh, 0, Qt::AlignTop); top->addWidget(add, 0, Qt::AlignTop);
    layout->addLayout(top); layout->addSpacing(4);
    m_lanes = new QVBoxLayout; m_lanes->setSpacing(10); layout->addLayout(m_lanes);
    auto* dashed = new DashedButton(QStringLiteral("plus"), QStringLiteral("Ajouter une source ou une piste")); layout->addWidget(dashed);
    auto* notes = new QHBoxLayout; notes->setSpacing(8); notes->addWidget(new IconLabel(QStringLiteral("info"), tk::text2, 14), 0, Qt::AlignTop); m_sourceNotes = label({}, "hint", true); notes->addWidget(m_sourceNotes, 1); layout->addLayout(notes); layout->addStretch();
    connect(refresh, &QPushButton::clicked, this, [this] { collect(); refreshCatalog(); populateSources(); emit notice(QStringLiteral("Sources audio actualisées"), {}); });
    connect(add, &QPushButton::clicked, this, [this] { editSource(-1); }); connect(dashed, &QPushButton::clicked, this, [this] { editSource(-1); });
    return scrolled(content);
}
QWidget* SettingsPage::buildFilesTab(const QString& settingsPath) {
    auto* content = new QWidget; auto* outer = new QHBoxLayout(content); outer->setContentsMargins(24, 16, 24, 16);
    auto* column = new QWidget; column->setMaximumWidth(820); auto* layout = new QVBoxLayout(column); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(16); outer->addWidget(column, 1); outer->addStretch(0);
    QVBoxLayout* body; layout->addWidget(section(QStringLiteral("Dossier des clips"), body));
    auto* row = new QHBoxLayout; row->setSpacing(12); m_folder = new QLineEdit; m_folder->setObjectName("libraryPath"); m_folder->addAction(icon(QStringLiteral("folder")), QLineEdit::LeadingPosition); row->addWidget(m_folder, 1);
    auto* browse = new Button(QStringLiteral("Parcourir…")); row->addWidget(browse); body->addLayout(row);
    body->addWidget(label(QStringLiteral("Un sous-dossier par jeu suffit pour classer. Les copies Discord vont dans « Partage Discord ». Formats lus : MP4, MKV, WebM, MOV, AVI."), "hint", true));
    layout->addWidget(section(QStringLiteral("Après chaque sauvegarde"), body)); body->setSpacing(12);
    m_savedSound = new Toggle(QStringLiteral("Jouer un son bref")); m_savedSound->setObjectName("clip-saved-sound"); body->addWidget(m_savedSound);
#ifdef Q_OS_WIN
    m_savedNotify = new Toggle(QStringLiteral("Afficher une notification Windows"));
#else
    m_savedNotify = new Toggle(QStringLiteral("Afficher une notification du système"));
#endif
    m_savedNotify->setObjectName("clip-saved-notification"); body->addWidget(m_savedNotify);
    layout->addWidget(section(QStringLiteral("Fenêtre et lancement"), body)); body->setSpacing(12);
    m_closeToTray = new Toggle(QStringLiteral("Garder Clipsapp dans la zone de notification à la fermeture de la fenêtre")); m_closeToTray->setObjectName("close-to-tray"); body->addWidget(m_closeToTray);
    body->addWidget(label(QStringLiteral("Le replay continue de tourner, sans fenêtre ouverte. Un clic sur l'icône, près de l'horloge, rouvre Clipsapp ; un clic droit propose de sauvegarder un clip ou de quitter."), "hint", true));
    // Shown only where the application manages its shortcut: a build under test has none.
    m_startMenu = new Toggle(QStringLiteral("Afficher Clipsapp dans le menu Démarrer")); m_startMenu->setObjectName("start-menu"); m_startMenu->hide(); body->addWidget(m_startMenu);
    // Shown only in a build that follows a repository: without one there is nothing to check.
    m_updateSection = section(QStringLiteral("Mises à jour"), body); body->setSpacing(10); m_updateSection->hide(); layout->addWidget(m_updateSection);
    m_checkUpdates = new Toggle(QStringLiteral("Vérifier au lancement si une nouvelle version existe")); m_checkUpdates->setObjectName("check-updates"); body->addWidget(m_checkUpdates);
    body->addWidget(label(QStringLiteral("Clipsapp demande à GitHub le numéro de la dernière version, et rien d'autre. Une mise à jour n'est jamais installée sans votre accord."), "hint", true));
    auto* versionRow = new QHBoxLayout; versionRow->setSpacing(12); m_version = label({}, "small"); m_version->setObjectName("current-version"); versionRow->addWidget(m_version, 1);
    auto* checkNow = new Button(QStringLiteral("refresh"), QStringLiteral("Vérifier maintenant"), Button::Secondary, Button::Small); checkNow->setObjectName("check-updates-now"); versionRow->addWidget(checkNow); body->addLayout(versionRow);
    connect(checkNow, &QPushButton::clicked, this, &SettingsPage::checkUpdatesRequested);
    layout->addWidget(section(QStringLiteral("Sur ce PC"), body)); body->setSpacing(10);
    body->addWidget(label(QStringLiteral("Clipsapp fonctionne sans compte, sans publicité et sans télémétrie. Vos clips et vos réglages ne quittent pas cet ordinateur."), nullptr, true));
    auto* pathRow = new QHBoxLayout; pathRow->setSpacing(12); auto* path = new ElideLabel(QDir::toNativeSeparators(settingsPath), "small", Qt::ElideMiddle); path->setFont(monoFont(12)); pathRow->addWidget(path, 1);
    auto* openFolder = new Button(QStringLiteral("folder"), QStringLiteral("Ouvrir le dossier"), Button::Secondary, Button::Small); pathRow->addWidget(openFolder); body->addLayout(pathRow); layout->addStretch();
    connect(browse, &QPushButton::clicked, this, &SettingsPage::chooseFolderRequested); connect(openFolder, &QPushButton::clicked, this, &SettingsPage::openSettingsFolderRequested);
    connect(m_folder, &QLineEdit::textChanged, this, [this] { changed(); });
    for (auto* toggle : {m_savedSound, m_savedNotify, m_checkUpdates, m_closeToTray, m_startMenu}) connect(toggle, &QCheckBox::toggled, this, [this] { changed(); });
    return scrolled(content);
}
void SettingsPage::setStartMenu(bool managed) { m_startMenu->setVisible(managed); }
void SettingsPage::setUpdates(bool followed, const QString& version) { m_updateSection->setVisible(followed); m_version->setText(QStringLiteral("Version installée : %1").arg(version)); }
void SettingsPage::showTab(int index) { if (index < 0 || index >= m_tabs->count()) return; m_tabs->setCurrentIndex(index); for (int tab = 0; tab < m_tabButtons.size(); ++tab) static_cast<QPushButton*>(m_tabButtons[tab])->setChecked(tab == index); }
int SettingsPage::currentTab() const { return m_tabs->currentIndex(); }
QKeySequence SettingsPage::hotkey() const { return m_hotkey->keySequence(); }
bool SettingsPage::setFps(int fps) { const int index = m_fps->findData(fps); if (index < 0) return false; m_fps->setCurrentIndex(index); return true; }
void SettingsPage::setFolder(const QString& path) { m_folder->setText(QDir::toNativeSeparators(path)); }
void SettingsPage::setSaved(const AppConfig& saved) { m_saved = saved; refreshDirty(); }
void SettingsPage::setLocked(bool locked) {
    if (locked == m_locked) return;
    m_locked = locked; m_lock->setVisible(locked); m_tabs->setEnabled(!locked); m_profiles->setEnabled(!locked); m_duplicate->setEnabled(!locked); refreshDirty();
}
void SettingsPage::changed() {
    if (m_loading) return;
    collect(); updateEstimate(); updateQuality(); refreshDirty(); emit edited();
}
void SettingsPage::refreshCatalog() {
    m_catalog = SourceCatalog::discover(); populateVideoTargets();
    m_sourceNotes->setText(m_catalog.notes.isEmpty() ? QStringLiteral("Seules les applications qui jouent du son apparaissent dans la liste. Une application arrêtée sera enregistrée dès qu'elle émet du son.") : m_catalog.notes.join('\n'));
}
void SettingsPage::populateVideoTargets() {
    QSignalBlocker blocker(m_videoTarget); const auto& video = profile().video;
    m_videoTarget->clear(); m_videoTarget->addItem(QStringLiteral("Choisir un écran ou une fenêtre…"), QString());
    for (const auto& target : m_catalog.videoTargets) {
        m_videoTarget->addItem(icon(target.id.startsWith(QLatin1String("screen:")) ? QStringLiteral("monitor") : QStringLiteral("window")), target.label, target.id);
        m_videoTarget->setItemData(m_videoTarget->count() - 1, target.label, Qt::UserRole + 1);
    }
    if (!video.targetId.isEmpty() && m_videoTarget->findData(video.targetId) < 0) {
        m_videoTarget->addItem(video.targetLabel + QStringLiteral(" (indisponible actuellement)"), video.targetId);
        m_videoTarget->setItemData(m_videoTarget->count() - 1, video.targetLabel, Qt::UserRole + 1);
    }
    const int index = m_videoTarget->findData(video.targetId); m_videoTarget->setCurrentIndex(index >= 0 ? index : 0);
}
void SettingsPage::load() {
    m_loading = true;
    { QSignalBlocker blocker(m_profiles); m_profiles->clear(); for (const auto& p : m_config.profiles) m_profiles->addItem(p.name, p.id); m_profiles->setCurrentIndex(std::max(0, m_profiles->findData(profile().id))); }
    const auto& p = profile(); populateVideoTargets();
    m_gameName->setText(p.gameName); m_gameName->setPlaceholderText(QStringLiteral("Détecté automatiquement, sinon « %1 »").arg(p.name)); m_detectGame->setChecked(p.detectGame);
    m_resolution->setCurrentIndex(m_resolution->findData(QString("%1x%2").arg(p.video.width).arg(p.video.height)));
    m_fps->setCurrentIndex(m_fps->findData(p.video.fps)); m_codec->setCurrentIndex(m_codec->findData(p.video.codec));
    int encoderIndex = m_encoder->findData(p.video.encoder);
    if (encoderIndex < 0) { m_encoder->addItem(p.video.encoder + QStringLiteral(" (autre système)"), p.video.encoder); encoderIndex = m_encoder->count() - 1; }
    m_encoder->setCurrentIndex(encoderIndex); m_bitrate->setValue(p.video.bitrateMbps); m_cursor->setChecked(p.video.captureCursor);
    m_rateControl->setCurrentIndex(m_rateControl->findData(p.video.rateControl)); m_effort->setCurrentIndex(m_effort->findData(p.video.effort));
    // A level written by hand in the settings file keeps its place between the named ones.
    int levelIndex = m_quality->findData(p.video.quality);
    if (levelIndex < 0) { levelIndex = 0; while (levelIndex < m_quality->count() && m_quality->itemData(levelIndex).toInt() > p.video.quality) ++levelIndex; m_quality->insertItem(levelIndex, QStringLiteral("Personnalisé · %1").arg(p.video.quality), p.video.quality); }
    m_quality->setCurrentIndex(levelIndex);
    m_cursor->setText(p.video.captureCursor ? QStringLiteral("Visible dans les clips") : QStringLiteral("Masqué dans les clips"));
    m_duration->setValue(p.replay.durationSeconds); m_durationSlider->setValue(p.replay.durationSeconds);
    int memoryIndex = m_memory->findData(p.replay.memoryLimitMiB);
    if (memoryIndex < 0) { memoryIndex = 0; while (memoryIndex < m_memory->count() && m_memory->itemData(memoryIndex).toInt() < p.replay.memoryLimitMiB) ++memoryIndex; m_memory->insertItem(memoryIndex, grouped(p.replay.memoryLimitMiB) + QStringLiteral(" Mio"), p.replay.memoryLimitMiB); }
    m_memory->setCurrentIndex(memoryIndex); m_hotkey->setKeySequence(QKeySequence::fromString(p.replay.hotkey, QKeySequence::PortableText)); updateHotkey();
    setFolder(m_config.libraryPath); m_savedSound->setChecked(m_config.clipSavedSound); m_savedNotify->setChecked(m_config.clipSavedNotification); m_checkUpdates->setChecked(m_config.checkUpdates);
    m_closeToTray->setChecked(m_config.closeToTray); m_startMenu->setChecked(m_config.startMenuShortcut);
    populateSources(); m_loading = false; updateEstimate(); updateQuality(); refreshDirty();
    bool preset = false; for (auto* tile : m_presets) preset |= tile->isChecked();
    if (!preset && !m_advanced->isVisible()) { m_advanced->show(); m_advancedToggle->setIconName(QStringLiteral("down")); }
}
void SettingsPage::collect() {
    auto& p = profile(); const auto dimensions = m_resolution->currentData().toString().split('x');
    if (dimensions.size() == 2) { p.video.width = dimensions[0].toInt(); p.video.height = dimensions[1].toInt(); }
    p.video.targetId = m_videoTarget->currentData().toString(); p.video.targetLabel = p.video.targetId.isEmpty() ? QString() : m_videoTarget->currentData(Qt::UserRole + 1).toString();
    p.video.fps = m_fps->currentData().toInt(); p.video.codec = m_codec->currentData().toString(); p.video.encoder = m_encoder->currentData().toString();
    p.video.bitrateMbps = m_bitrate->value(); p.video.captureCursor = m_cursor->isChecked();
    p.video.rateControl = m_rateControl->currentData().toString(); p.video.quality = m_quality->currentData().toInt(); p.video.effort = m_effort->currentData().toString();
    p.gameName = m_gameName->text().trimmed(); p.detectGame = m_detectGame->isChecked();
    p.replay.durationSeconds = m_duration->value(); p.replay.memoryLimitMiB = m_memory->currentData().toInt(); p.replay.hotkey = m_hotkey->keySequence().toString(QKeySequence::PortableText);
    m_config.libraryPath = QDir::fromNativeSeparators(m_folder->text().trimmed());
    m_config.clipSavedSound = m_savedSound->isChecked(); m_config.clipSavedNotification = m_savedNotify->isChecked(); m_config.checkUpdates = m_checkUpdates->isChecked();
    m_config.closeToTray = m_closeToTray->isChecked(); m_config.startMenuShortcut = m_startMenu->isChecked();
}
void SettingsPage::updateHotkey() { m_hotkeyCaps->setSequence(m_hotkey->keySequence()); if (m_hotkey->isVisible() && !m_hotkey->keySequence().isEmpty()) m_hotkeyChange->click(); }
void SettingsPage::updateQuality() {
    const auto& video = profile().video;
    const bool constant = video.rateControl == "quality";
    for (int index = 0; index < m_presets.size(); ++index) { const auto& quality = qualities[index]; m_presets[index]->setChecked(video.width == quality.width && video.height == quality.height && video.fps == quality.fps && video.bitrateMbps == quality.bitrate && (constant ? video.quality : 0) == quality.level); }
    // The level only means something in constant quality, where the bitrate turns into a ceiling.
    m_bitrateLabel->setText(constant ? QStringLiteral("Débit maximal") : QStringLiteral("Débit vidéo")); m_qualityLabel->setVisible(constant); m_quality->setVisible(constant);
}
void SettingsPage::updateEstimate() {
    if (m_loading) return;
    const auto& p = profile(); const double estimated = estimatedBufferMiB(p); const int limit = p.replay.memoryLimitMiB; const bool over = estimated > limit;
    m_estimateCalm->setVisible(!over); m_estimateWarn->setVisible(over);
    // A constant quality has no fixed weight: the page gives what it usually fills, from a calm
    // game to a busy one, and what its ceiling would fill at most.
    const bool constant = p.video.rateControl == "quality"; const auto usual = usualBufferMiB(p); const int low = qRound(usual.low), high = qRound(usual.high);
    const auto amount = constant && low != high ? QStringLiteral("≈ %1 à %2 Mio").arg(grouped(low), grouped(high)) : QStringLiteral("≈ %1 Mio").arg(grouped(constant ? high : qRound(estimated)));
    if (!over) { m_estimateCalmText->setText(constant ? QStringLiteral("Mémoire estimée : %1 selon le jeu, %2 au plus, sur un plafond de %3.").arg(amount, grouped(qRound(estimated)), grouped(limit)) : QStringLiteral("Mémoire estimée : %1 sur un plafond de %2. Le GPU et le moteur en utilisent un peu plus.").arg(amount, grouped(limit))); m_estimateCalmBar->setValue((constant ? usual.high : estimated) / limit); return; }
    int next = 0; for (int index = 0; index < m_memory->count(); ++index) if (!next && m_memory->itemData(index).toInt() >= estimated) next = m_memory->itemData(index).toInt();
    m_estimateWarnTitle->setText(QStringLiteral("Mémoire estimée : %1").arg(amount)); m_estimateWarnLimit->setText(QStringLiteral("plafond %1").arg(grouped(limit)));
    m_estimateWarnBar->setValue(1); m_estimateWarnBar->setMarker(limit / estimated);
    m_estimateWarnText->setText((constant ? QStringLiteral("Selon le jeu, et jusqu'à %1 Mio si les scènes chargées durent. Au-delà du plafond, les secondes les plus anciennes sont perdues. Réduisez la durée ou le débit maximal").arg(grouped(qRound(estimated))) : QStringLiteral("Au-delà du plafond, les secondes les plus anciennes sont perdues. Réduisez la durée ou le débit")) + (next ? QStringLiteral(", ou montez le plafond à %1 Mio.").arg(grouped(next)) : QStringLiteral(".")));
}
void SettingsPage::populateSources() {
    while (auto* child = m_lanes->takeAt(0)) { if (child->widget()) { child->widget()->hide(); child->widget()->deleteLater(); } delete child; }
    const auto& p = profile();
    for (int track = 0; track < p.tracks.size(); ++track) {
        QList<int> rows; for (int row = 0; row < p.sources.size(); ++row) if (p.sources[row].trackId == p.tracks[track].id) rows.append(row);
        auto* lane = card(); lane->setObjectName(QStringLiteral("audio-lane-%1").arg(track)); auto* laneLayout = new QHBoxLayout(lane); laneLayout->setContentsMargins(1, 1, 1, 1); laneLayout->setSpacing(0);
        auto* head = new QFrame; head->setObjectName("laneHead"); head->setFixedWidth(196); head->setStyleSheet(QStringLiteral("QFrame#laneHead { background: #111419; border: none; border-right: 1px solid #242A34; border-top-left-radius: 7px; border-bottom-left-radius: 7px; }"));
        auto* headLayout = new QVBoxLayout(head); headLayout->setContentsMargins(16, 12, 16, 12); headLayout->setSpacing(3); auto* number = caption(QStringLiteral("Piste %1").arg(track + 1)); headLayout->addWidget(number);
        auto* nameRow = new QHBoxLayout; nameRow->setSpacing(8); nameRow->addWidget(new Swatch(tk::track(track))); nameRow->addWidget(new ElideLabel(p.tracks[track].name, "h2"), 1); headLayout->addLayout(nameRow);
        headLayout->addWidget(label(rows.size() > 1 ? QStringLiteral("%1 sources mélangées").arg(rows.size()) : QStringLiteral("1 source"), "hint")); headLayout->addStretch();
        // The usual level of the track in the clips: applied when they are played, edited or shared.
        // The recording keeps every track at full level, so nothing chosen here is ever lost.
        const auto trackName = p.tracks[track].name;
        auto* levelRow = new QHBoxLayout; levelRow->setSpacing(8); auto* level = new QSlider(Qt::Horizontal); level->setObjectName(QStringLiteral("track-mix-%1").arg(track)); level->setRange(0, 200); level->setPageStep(10);
        level->setValue(qRound(mixLevel(m_config.defaultMix, trackName) * 100)); level->setAccessibleName(QStringLiteral("Volume habituel de la piste ") + trackName);
        auto* percent = monoLabel(QStringLiteral("%1 %").arg(level->value()), 11, tk::text2); percent->setFixedWidth(40); percent->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        const auto tip = help("Volume habituel de cette piste dans vos clips. Il s'applique à la lecture, dans l'éditeur et dans les copies de partage ; l'enregistrement garde la piste à son niveau d'origine, et chaque clip reste réglable à part.");
        level->setToolTip(tip); percent->setToolTip(tip); levelRow->addWidget(level, 1); levelRow->addWidget(percent); headLayout->addSpacing(4); headLayout->addLayout(levelRow);
        connect(level, &QSlider::valueChanged, this, [this, level, percent, trackName](int value) {
            // Close to the recorded level, the slider settles on it.
            if (level->isSliderDown() && value != 100 && std::abs(value - 100) <= 3) { level->setValue(100); return; }
            percent->setText(QStringLiteral("%1 %").arg(value));
            if (value == 100) m_config.defaultMix.remove(mixKey(trackName)); else m_config.defaultMix.insert(mixKey(trackName), value / 100.0);
            changed();
        });
        laneLayout->addWidget(head);
        auto* sources = new QVBoxLayout; sources->setContentsMargins(0, 0, 0, 0); sources->setSpacing(0);
        for (int position = 0; position < rows.size(); ++position) {
            const int row = rows[position]; const auto& source = p.sources[row];
            if (position) { auto* line = separator(); line->setStyleSheet(QStringLiteral("background: #1E242C;")); sources->addWidget(line); }
            QString state = QStringLiteral("À choisir"); Tag::Variant variant = Tag::Warn;
            if (source.kind == SourceKind::ApplicationMix) { state = QStringLiteral("Automatique"); variant = Tag::Ok; }
            else if (source.targetId == "@default") { const bool present = !m_catalog.audioTargets(source.kind).isEmpty(); state = present ? QStringLiteral("Par défaut") : QStringLiteral("Périphérique absent"); variant = present ? Tag::Info : Tag::Err; }
            else if (!source.targetId.isEmpty()) {
                bool available = false;
                for (const auto& target : m_catalog.audioTargets(source.kind)) if (QDir::toNativeSeparators(target.id).compare(QDir::toNativeSeparators(source.targetId), Qt::CaseInsensitive) == 0) available = true;
                state = available ? QStringLiteral("Disponible") : source.kind == SourceKind::Application ? QStringLiteral("Application arrêtée") : QStringLiteral("Périphérique absent");
                variant = available ? Tag::Ok : source.kind == SourceKind::Application ? Tag::Warn : Tag::Err;
            }
            QString target = source.targetLabel.isEmpty() ? sourceKindLabel(source.kind) + QStringLiteral(" à choisir") : source.targetLabel;
            if (source.kind == SourceKind::ApplicationMix) {
                const auto exclusions = applicationExclusions(source, p); QStringList names; for (const auto& path : exclusions) names.append(QFileInfo(path).fileName());
                target = exclusions.isEmpty() ? QStringLiteral("Toutes les applications") : QStringLiteral("Toutes les applications sauf %1 : %2").arg(exclusions.size()).arg(names.join(QStringLiteral(", ")));
            } else if (source.targetId == "@default" && source.kind == SourceKind::Microphone) target = QStringLiteral("Microphone par défaut · suit le système");
            auto* line = new QWidget; line->setFixedHeight(54); auto* lineLayout = new QHBoxLayout(line); lineLayout->setContentsMargins(14, 0, 12, 0); lineLayout->setSpacing(12);
            lineLayout->addWidget(new IconBox(sourceIcon(source.kind)));
            auto* texts = new QVBoxLayout; texts->setSpacing(0); texts->addStretch(); auto* name = new ElideLabel(source.name, "strong"); if (!source.enabled) name->setStyleSheet(QStringLiteral("color: #7C8696; font-weight: 600;"));
            texts->addWidget(name); texts->addWidget(new ElideLabel(target, "small")); texts->addStretch(); lineLayout->addLayout(texts, 1);
            auto* tagHost = new QWidget; tagHost->setFixedWidth(150); auto* tagLayout = new QHBoxLayout(tagHost); tagLayout->setContentsMargins(0, 0, 0, 0); tagLayout->addWidget(new Tag(state, variant), 0, Qt::AlignCenter); lineLayout->addWidget(tagHost);
            auto* enabled = new Toggle; enabled->setObjectName(QStringLiteral("source-enabled-%1").arg(row)); enabled->setChecked(source.enabled); enabled->setToolTip(QStringLiteral("Inclure %1 dans les clips").arg(source.name)); enabled->setAccessibleName(enabled->toolTip()); lineLayout->addWidget(enabled);
            auto* edit = new Button(QStringLiteral("Modifier"), Button::Secondary, Button::Small); edit->setObjectName(QStringLiteral("source-edit-%1").arg(row)); lineLayout->addWidget(edit);
            auto* remove = new Button(QStringLiteral("x"), QString(), Button::Ghost, Button::Small); remove->setToolTip(QStringLiteral("Retirer %1").arg(source.name)); remove->setAccessibleName(remove->toolTip()); lineLayout->addWidget(remove);
            const auto sourceId = source.id;
            connect(enabled, &QCheckBox::toggled, this, [this, sourceId](bool checked) { for (auto& s : profile().sources) if (s.id == sourceId) s.enabled = checked; populateSources(); changed(); });
            connect(edit, &QPushButton::clicked, this, [this, row] { editSource(row); }); connect(remove, &QPushButton::clicked, this, [this, row] { removeSource(row); });
            sources->addWidget(line);
        }
        laneLayout->addLayout(sources, 1); m_lanes->addWidget(lane);
    }
}
void SettingsPage::editSource(int index) {
    collect(); const auto& p = profile();
    SourceDialog dialog(m_catalog, p, index >= 0 ? &p.sources[index] : nullptr, this);
    if (dialog.exec() != QDialog::Accepted) return;
    const auto track = dialog.newTrack(); if (!track.id.isEmpty()) profile().tracks.append(track);
    if (index < 0) profile().sources.append(dialog.source()); else profile().sources[index] = dialog.source();
    pruneTracks(); populateSources(); changed();
}
void SettingsPage::removeSource(int index) { if (index < 0 || index >= profile().sources.size()) return; profile().sources.removeAt(index); pruneTracks(); populateSources(); changed(); }
void SettingsPage::pruneTracks() {
    auto& p = profile(); QSet<QString> used; for (const auto& source : p.sources) used.insert(source.trackId);
    p.tracks.erase(std::remove_if(p.tracks.begin(), p.tracks.end(), [&used](const auto& track) { return !used.contains(track.id); }), p.tracks.end());
}
void SettingsPage::duplicateProfile() {
    QString name = QStringLiteral("%1 (copie)").arg(profile().name);
    if (!askText(this, QStringLiteral("Dupliquer le profil"), QStringLiteral("Le nouveau profil reprend tous les réglages de « %1 ».").arg(profile().name), QStringLiteral("Nom du profil"), name, QStringLiteral("Créer le profil"))) return;
    collect(); auto copy = profile(); copy.id = newId(); copy.name = name; m_config.profiles.append(copy); m_config.activeProfileId = copy.id; load(); emit edited();
}
void SettingsPage::refreshDirty() {
    if (m_loading) return;
    QStringList groups; int count = 0; const auto note = [&](bool differs, const QString& group) { if (!differs) return; ++count; if (!groups.contains(group)) groups.append(group); };
    const auto& p = profile(); const RecordingProfile* saved = nullptr; for (const auto& candidate : m_saved.profiles) if (candidate.id == p.id) saved = &candidate;
    bool capture = false, quality = false, replay = false;
    if (!saved) { note(true, QStringLiteral("nouveau profil")); capture = quality = replay = true; }
    else {
        const auto json = [](const RecordingProfile& value) { AppConfig holder; holder.profiles = {value}; return QJsonDocument(holder.toJson().value("profiles").toArray().first().toObject()); };
        capture = p.video.targetId != saved->video.targetId || p.gameName != saved->gameName || p.detectGame != saved->detectGame;
        note(p.video.targetId != saved->video.targetId, QStringLiteral("cible")); note(p.gameName != saved->gameName || p.detectGame != saved->detectGame, QStringLiteral("nom du jeu"));
        const bool size = p.video.width != saved->video.width || p.video.height != saved->video.height;
        for (const bool differs : {size, p.video.fps != saved->video.fps, p.video.codec != saved->video.codec, p.video.encoder != saved->video.encoder, p.video.bitrateMbps != saved->video.bitrateMbps, p.video.rateControl != saved->video.rateControl, p.video.quality != saved->video.quality, p.video.effort != saved->video.effort, p.video.captureCursor != saved->video.captureCursor}) { quality |= differs; note(differs, QStringLiteral("qualité")); }
        note(p.replay.durationSeconds != saved->replay.durationSeconds, QStringLiteral("durée du replay")); note(p.replay.memoryLimitMiB != saved->replay.memoryLimitMiB, QStringLiteral("plafond de mémoire")); note(p.replay.hotkey != saved->replay.hotkey, QStringLiteral("raccourci"));
        replay = p.replay.durationSeconds != saved->replay.durationSeconds || p.replay.memoryLimitMiB != saved->replay.memoryLimitMiB || p.replay.hotkey != saved->replay.hotkey;
        auto current = p, before = *saved; current.video = before.video; current.replay = before.replay; current.gameName = before.gameName; current.detectGame = before.detectGame;
        note(json(current) != json(before), QStringLiteral("audio"));
    }
    note(m_config.activeProfileId != m_saved.activeProfileId && saved, QStringLiteral("profil actif"));
    note(m_config.libraryPath != m_saved.libraryPath, QStringLiteral("dossier des clips"));
    note(m_config.clipSavedSound != m_saved.clipSavedSound || m_config.clipSavedNotification != m_saved.clipSavedNotification, QStringLiteral("alertes"));
    note(m_config.checkUpdates != m_saved.checkUpdates, QStringLiteral("mises à jour"));
    note(!sameMix(m_config.defaultMix, m_saved.defaultMix), QStringLiteral("volumes habituels"));
    note(m_config.closeToTray != m_saved.closeToTray, QStringLiteral("fermeture de la fenêtre")); note(m_config.startMenuShortcut != m_saved.startMenuShortcut, QStringLiteral("menu Démarrer"));
    if (!count && m_config.toJson() != m_saved.toJson()) { count = 1; groups.append(QStringLiteral("réglages")); }
    m_captureTag->setVisible(capture); m_qualityTag->setVisible(quality); m_replayTag->setVisible(replay);
    m_dirtyTitle->setText(count > 1 ? QStringLiteral("%1 modifications non enregistrées").arg(count) : QStringLiteral("1 modification non enregistrée"));
    static_cast<ElideLabel*>(m_dirtyDetail)->setFullText(QStringLiteral("· ") + groups.join(QStringLiteral(", "))); m_saveBar->setVisible(count > 0 && !m_locked);
}
}
