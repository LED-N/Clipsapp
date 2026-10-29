#include "settings_page.h"
#include "quality_tiles.h"
#include "source_dialog.h"
#include "theme_editor.h"
#include "clip_feedback.h"
#include "widgets.h"
#include "core/audio_routing.h"
#include "core/clip_library.h"
#include "core/game_identity.h"
#include "platform/system_info.h"
#include <QCursor>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMenu>
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
#include <QStorageInfo>
#include <QVBoxLayout>
#include <algorithm>

namespace clipsapp {
namespace {
struct Level { int value; const char* name; };
const Level levels[]{{26, "Légère"}, {23, "Bonne"}, {20, "Haute"}, {17, "Très haute"}, {14, "Quasi sans perte"}};
QString help(const char* text) { return QStringLiteral("<qt>%1</qt>").arg(QString::fromUtf8(text).toHtmlEscaped()); }
// A setting whose choices each do something of their own: what it is, then each choice on its
// line, its name in bold, and what is worth knowing beyond them.
struct Choice { const char* name; const char* effect; };
QString choices(const char* intro, std::initializer_list<Choice> items, const char* outro = nullptr) {
    const auto text = [](const char* value) { return QString::fromUtf8(value).toHtmlEscaped(); };
    QString html = QStringLiteral("<qt><p style='margin:0'>%1</p>").arg(text(intro));
    for (const auto& item : items) html += QStringLiteral("<p style='margin:6px 0 0 0'><b>%1</b><br>%2</p>").arg(text(item.name), text(item.effect));
    if (outro) html += QStringLiteral("<p style='margin:6px 0 0 0; color:{text2}'>%1</p>").arg(text(outro));
    return html + QStringLiteral("</qt>");
}
QString grouped(int value) { return QLocale(QLocale::French, QLocale::France).toString(value); }
class Swatch final : public QWidget {
public:
    explicit Swatch(const QColor& color) : m_color(color) { setFixedSize(10, 10); }
protected:
    void paintEvent(QPaintEvent*) override { QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing); painter.setPen(Qt::NoPen); painter.setBrush(m_color); painter.drawRoundedRect(rect(), 3, 3); }
private:
    QColor m_color;
};
QVBoxLayout* field(const QString& title, QWidget* widget, QLayout* inner = nullptr) {
    auto* layout = new QVBoxLayout; layout->setSpacing(6); layout->addWidget(label(title, "lbl")); if (widget) layout->addWidget(widget); if (inner) layout->addLayout(inner); return layout;
}
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
    const QList<QPair<QString, QString>> tabs{{QStringLiteral("monitor"), QStringLiteral("Capture")}, {QStringLiteral("speaker"), QStringLiteral("Audio")}, {QStringLiteral("toggle"), QStringLiteral("Général")}};
    for (int index = 0; index < tabs.size(); ++index) {
        auto* tab = new TabButton(tabs[index].first, tabs[index].second); tab->setObjectName(QStringLiteral("settings-tab-%1").arg(index)); tab->setChecked(index == 0); tabRow->addWidget(tab); m_tabButtons.append(tab);
        connect(tab, &QPushButton::clicked, this, [this, index] { showTab(index); });
    }
    tabRow->addStretch(); head->addLayout(tabRow); layout->addLayout(head); layout->addWidget(separator());
    auto* lockHost = new QVBoxLayout; lockHost->setContentsMargins(24, 16, 24, 0);
    m_lock = new Banner(Banner::Info, QStringLiteral("lock")); m_lock->setObjectName("settings-locked"); m_lock->setTexts(QStringLiteral("Certains réglages attendent l'arrêt de la capture"), QStringLiteral("Ce qui est capturé, la qualité, le buffer du replay et les sources audio se modifient une fois le replay et l'enregistrement arrêtés. Le reste s'applique tout de suite."));
    auto* stop = new Button(QStringLiteral("stop"), QStringLiteral("Tout arrêter pour modifier")); m_lock->actions()->addWidget(stop); m_lock->hide(); lockHost->addWidget(m_lock); layout->addLayout(lockHost);
    m_tabs->addWidget(buildCaptureTab()); m_tabs->addWidget(buildAudioTab()); m_tabs->addWidget(buildGeneralTab(settingsPath)); layout->addWidget(m_tabs, 1);

    m_saveBar = new QFrame; m_saveBar->setObjectName("savebar"); m_saveBar->setFixedHeight(60);
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

    QVBoxLayout* body; left->addWidget(m_targetSection = section(QStringLiteral("Ce qui est capturé"), body, &m_captureTag));
    m_videoTarget = new ChoiceBox; m_videoTarget->setObjectName("video-target"); m_videoTarget->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon); m_videoTarget->setMinimumContentsLength(18); body->addLayout(field(QStringLiteral("Écran ou fenêtre"), m_videoTarget));
    // The game is the one captured, recognised by itself; failing that, the clips take the name of the profile.
    body->addWidget(label(QStringLiteral("Les clips sont rangés sous le nom du jeu capturé, reconnu de lui-même, ou à défaut sous le nom du profil."), "hint", true));

    left->addWidget(m_qualitySection = section(QStringLiteral("Qualité"), body, &m_qualityTag));
    // Three choices, each in a word: the size, the frequency, then how much each image is given.
    auto* sizes = new QHBoxLayout; sizes->setSpacing(8);
    for (const auto& size : videoSizes) {
        auto* tile = new QualityTile(QString::fromUtf8(size.name), 58, size.screen); tile->setObjectName(QStringLiteral("resolution-%1").arg(size.height)); tile->setLines(QString::fromUtf8(size.kind), {});
        setThemedToolTip(tile, help("Taille de l'image enregistrée. Un écran plus grand ou plus petit est mis à cette taille par la carte graphique."));
        sizes->addWidget(tile); m_sizes.append(tile);
        connect(tile, &QPushButton::clicked, this, [this, tile] { for (auto* other : m_sizes) other->setChecked(other == tile); followPreset(); changed(); });
    }
    body->addLayout(field(QStringLiteral("Résolution"), nullptr, sizes));
    m_rates = new Segmented; m_rates->setObjectName("fps"); for (const int rate : frameRates) m_rates->addOption(QStringLiteral("%1 FPS").arg(rate));
    setThemedToolTip(m_rates, help("Nombre d'images par seconde. Plus il y en a, plus le mouvement est fluide : 120 ou 144 FPS suivent un écran rapide. La carte graphique travaille davantage ; si elle ne suit pas, la barre du replay le signale."));
    body->addLayout(field(QStringLiteral("Fréquence"), m_rates));
    auto* presetRow = new QHBoxLayout; presetRow->setSpacing(8);
    for (int index = 0; index < int(std::size(qualityPresets)); ++index) {
        auto* tile = new QualityTile(QString::fromUtf8(qualityPresets[index].name), 74, {}, index + 1); tile->setObjectName(QStringLiteral("quality-%1").arg(index)); presetRow->addWidget(tile); m_presets.append(tile);
        connect(tile, &QPushButton::clicked, this, [this, index] { m_preset = index; followPreset(); changed(); });
    }
    body->addLayout(field(QStringLiteral("Préréglage"), nullptr, presetRow));
    m_presetText = label({}, "hint", true); m_presetText->setObjectName("quality-summary"); body->addWidget(m_presetText);
    m_presetWeight = label({}, "small", true); m_presetWeight->setObjectName("quality-weight"); body->addWidget(m_presetWeight);
    body->addWidget(separator());
    m_advancedToggle = new Toggle(QStringLiteral("Réglages avancés")); m_advancedToggle->setObjectName("advanced-toggle"); body->addWidget(m_advancedToggle);
    m_advanced = new QWidget; auto* grid = new QGridLayout(m_advanced); grid->setContentsMargins(0, 0, 0, 0); grid->setHorizontalSpacing(12); grid->setVerticalSpacing(12);
    m_codec = new ChoiceBox; m_codec->addItem(QStringLiteral("H.264 · compatible"), "h264"); m_codec->addItem(QStringLiteral("HEVC · plus compact"), "hevc");
    m_encoder = new ChoiceBox; m_encoder->setObjectName("encoder"); m_encoder->addItem(QStringLiteral("Automatique"), "auto"); m_encoder->addItem("NVIDIA NVENC", "nvenc");
#ifdef Q_OS_WIN
    m_encoder->addItem("AMD AMF", "amf");
    m_encoder->addItem(QStringLiteral("Windows · toutes les cartes"), "mf");
#else
    m_encoder->addItem("VAAPI · AMD / Intel", "vaapi");
#endif
    m_rateControl = new ChoiceBox; m_rateControl->setObjectName("rate-control"); m_rateControl->addItem(QStringLiteral("Débit cible"), "bitrate"); m_rateControl->addItem(QStringLiteral("Qualité constante"), "quality");
    m_bitrate = new QSpinBox; m_bitrate->setObjectName("bitrate"); m_bitrate->setRange(5, 300); m_bitrate->setSuffix(QStringLiteral(" Mbit/s")); m_bitrate->setButtonSymbols(QAbstractSpinBox::NoButtons); m_bitrate->setFont(monoFont(13));
    m_quality = new ChoiceBox; m_quality->setObjectName("quality-level"); for (const auto& level : levels) m_quality->addItem(QString::fromUtf8(level.name), level.value);
    m_effort = new ChoiceBox; m_effort->setObjectName("effort"); m_effort->addItem(QStringLiteral("Rapide · léger pour le GPU"), "fast"); m_effort->addItem(QStringLiteral("Soigné · compresse mieux"), "thorough");
    m_cursor = new Toggle(QStringLiteral("Masqué dans les clips")); auto* cursorHost = new QWidget; cursorHost->setFixedHeight(32); auto* cursorLayout = new QHBoxLayout(cursorHost); cursorLayout->setContentsMargins(0, 0, 0, 0); cursorLayout->addWidget(m_cursor);
    // Every advanced setting explains itself where the pointer rests, on its name as on its field.
    const auto place = [grid](int row, int column, const QString& title, QWidget* widget, const QString& tip) {
        auto* name = label(title, "lbl"); setThemedToolTip(name, tip); setThemedToolTip(widget, tip);
        auto* layout = new QVBoxLayout; layout->setSpacing(6); layout->addWidget(name); layout->addWidget(widget); grid->addLayout(layout, row, column); return name;
    };
    place(0, 0, QStringLiteral("Codec"), m_codec, choices("Format de la vidéo enregistrée.", {
        {"H.264 · compatible", "Se lit partout : navigateurs, téléphones, Discord, logiciels de montage."},
        {"HEVC · plus compact", "Même qualité avec moins de données. Certains logiciels et navigateurs ne le lisent pas."}}));
    place(0, 1, QStringLiteral("Encodeur"), m_encoder, choices("Puce de la carte graphique qui compresse la vidéo pendant le jeu.", {
        {"Automatique", "Celle de votre carte graphique, NVIDIA ou AMD."},
        {"NVIDIA NVENC", "Cartes GeForce et RTX."},
        {"AMD AMF", "Cartes Radeon."},
        {"Windows", "L'encodeur de la carte tel que Windows le propose, pour une carte dont le pilote est trop ancien pour NVENC (GTX 900 et 1000, par exemple). En Automatique, il prend le relais de lui-même."}}));
    place(1, 0, QStringLiteral("Mode de débit"), m_rateControl, choices("Comment la quantité de données est répartie dans la vidéo.", {
        {"Débit cible", "Le poids des clips et la mémoire du replay sont prévisibles."},
        {"Qualité constante", "Chaque scène reçoit ce qu'il lui faut : une scène chargée pèse plus, une scène calme moins."}}));
    m_bitrateLabel = place(1, 1, QStringLiteral("Débit vidéo"), m_bitrate, choices("Quantité de données par seconde de vidéo. Plus haut, l'image est plus fidèle, les clips plus lourds et le replay demande plus de mémoire.", {
        {"En débit cible", "La vidéo vise ce débit d'un bout à l'autre."},
        {"En qualité constante", "Un plafond : seules les scènes les plus chargées l'atteignent, les autres pèsent moins."}}));
    place(2, 0, QStringLiteral("Effort d'encodage"), m_effort, choices("Travail que la carte graphique consacre à compresser chaque image.", {
        {"Rapide", "Sollicite le moins la carte graphique pendant le jeu."},
        {"Soigné", "Compresse mieux à débit égal, ce qui se voit surtout aux débits modestes. La carte graphique travaille davantage."}}));
    m_qualityLabel = place(2, 1, QStringLiteral("Niveau de qualité"), m_quality, choices("Fidélité visée pour chaque image, en qualité constante.", {
        {"Légère, Bonne", "Clips légers : les détails fins sont adoucis."},
        {"Haute, Très haute", "Un bon équilibre pour garder ses clips."},
        {"Quasi sans perte", "Très proche de l'écran, pour retravailler les clips. Ils sont lourds : vous pourrez les compresser au moment du partage."}}));
    place(3, 0, QStringLiteral("Curseur"), cursorHost, help("Affiche ou masque le pointeur de la souris dans les clips."));
    grid->setColumnStretch(0, 1); grid->setColumnStretch(1, 1); body->addWidget(m_advanced); m_advanced->hide(); left->addStretch();

    right->addWidget(section(QStringLiteral("Replay"), body, &m_replayTag));
    auto* durationRow = new QHBoxLayout; durationRow->setSpacing(12); m_durationSlider = new QSlider(Qt::Horizontal); m_durationSlider->setRange(15, 600); m_durationSlider->setPageStep(15);
    m_duration = new QSpinBox; m_duration->setObjectName("replay-duration"); m_duration->setRange(15, 600); m_duration->setSuffix(QStringLiteral(" s")); m_duration->setFixedWidth(96); m_duration->setButtonSymbols(QAbstractSpinBox::NoButtons); m_duration->setFont(monoFont(13));
    durationRow->addWidget(m_durationSlider, 1); durationRow->addWidget(m_duration);
    auto* durationGroup = new QVBoxLayout; durationGroup->setSpacing(6); durationGroup->addWidget(label(QStringLiteral("Secondes gardées"), "lbl")); durationGroup->addLayout(durationRow);
    durationGroup->addWidget(label(QStringLiteral("De 15 s à 10 min. C'est aussi la durée d'un clip sauvegardé."), "hint", true)); body->addLayout(durationGroup);
    // Where the replay keeps its last seconds. Past a share of the memory of the PC, the disk is proposed.
    m_storage = new ChoiceBox; m_storage->setObjectName("replay-storage"); m_storage->setFixedWidth(200);
    m_storage->addItem(QStringLiteral("Mémoire vive"), "memory"); m_storage->addItem(QStringLiteral("Disque · SSD conseillé"), "disk");
    setThemedToolTip(m_storage, choices("Où le replay garde ses dernières secondes.", {
        {"Mémoire vive", "Le plus simple, pour un replay de quelques minutes à un débit raisonnable."},
        {"Disque", "Pour un long replay ou un débit élevé : la mémoire reste au jeu. Un SSD est conseillé ; les fichiers sont effacés à l'arrêt du replay."}}));
    body->addLayout(field(QStringLiteral("Buffer du replay"), m_storage));
    auto* proposal = new QFrame; m_diskProposal = proposal; proposal->setObjectName("disk-proposal"); proposal->setProperty("banner", "info"); auto* proposalLayout = new QVBoxLayout(proposal); proposalLayout->setContentsMargins(12, 12, 12, 12); proposalLayout->setSpacing(8);
    auto* proposalHead = new QHBoxLayout; proposalHead->setSpacing(8); proposalHead->addWidget(new IconLabel(QStringLiteral("info"), tk::info)); m_diskProposalTitle = label({}, "strong"); proposalHead->addWidget(m_diskProposalTitle, 1); proposalLayout->addLayout(proposalHead);
    m_diskProposalText = label({}, "small", true); proposalLayout->addWidget(m_diskProposalText);
    auto* proposalActions = new QHBoxLayout; proposalActions->setSpacing(8);
    auto* useDisk = new Button(QStringLiteral("Passer au disque"), Button::Primary, Button::Small); useDisk->setObjectName("use-disk-buffer");
    auto* keepMemory = new Button(QStringLiteral("Rester en mémoire"), Button::Ghost, Button::Small); keepMemory->setObjectName("keep-memory-buffer");
    proposalActions->addWidget(useDisk); proposalActions->addWidget(keepMemory); proposalActions->addStretch(); proposalLayout->addLayout(proposalActions); body->addWidget(proposal); proposal->hide();
    m_memory = new ChoiceBox; m_memory->setObjectName("replay-memory"); m_memory->setFixedWidth(200); for (const int value : {256, 512, 1024, 2048, 4096, 8192}) m_memory->addItem(grouped(value) + QStringLiteral(" Mio"), value);
    m_memoryField = new QWidget; auto* memoryLayout = field(QStringLiteral("Plafond de mémoire"), m_memory); memoryLayout->setContentsMargins(0, 0, 0, 0); m_memoryField->setLayout(memoryLayout); body->addWidget(m_memoryField);
    m_diskBox = new QWidget; m_diskBox->setObjectName("disk-buffer"); auto* diskLayout = new QVBoxLayout(m_diskBox); diskLayout->setContentsMargins(0, 0, 0, 0); diskLayout->setSpacing(12);
    m_diskLimit = new ChoiceBox; m_diskLimit->setObjectName("replay-disk-limit"); m_diskLimit->setFixedWidth(200); for (const int gib : {4, 8, 16, 32, 64}) m_diskLimit->addItem(QStringLiteral("%1 Gio").arg(gib), gib * 1024);
    diskLayout->addLayout(field(QStringLiteral("Plafond sur le disque"), m_diskLimit));
    auto* diskFolderRow = new QHBoxLayout; diskFolderRow->setSpacing(12); m_diskFolder = new QLineEdit; m_diskFolder->setObjectName("replay-disk-folder"); m_diskFolder->addAction(icon(QStringLiteral("folder")), QLineEdit::LeadingPosition);
    m_diskFolder->setPlaceholderText(QDir::toNativeSeparators(replayDiskFolder({}))); diskFolderRow->addWidget(m_diskFolder, 1);
    auto* diskBrowse = new Button(QStringLiteral("Parcourir…")); diskBrowse->setObjectName("replay-disk-browse"); diskFolderRow->addWidget(diskBrowse);
    auto* diskFolderGroup = new QVBoxLayout; diskFolderGroup->setSpacing(6); diskFolderGroup->addWidget(label(QStringLiteral("Dossier du buffer"), "lbl")); diskFolderGroup->addLayout(diskFolderRow);
    m_diskSpace = label({}, "hint", true); m_diskSpace->setObjectName("disk-space"); diskFolderGroup->addWidget(m_diskSpace);
    diskFolderGroup->addWidget(label(QStringLiteral("Un SSD est préférable : un disque dur peut ralentir les chargements du jeu et peiner aux débits élevés. Laissé vide, c'est le dossier « Buffer du replay » à côté de Clipsapp. Les fichiers sont effacés dès l'arrêt du replay, même après un plantage."), "hint", true));
    diskLayout->addLayout(diskFolderGroup); body->addWidget(m_diskBox); m_diskBox->hide();
    connect(useDisk, &QPushButton::clicked, this, [this] { m_storage->setCurrentIndex(m_storage->findData("disk")); });
    connect(keepMemory, &QPushButton::clicked, this, [this] { profile().replay.keepMemory = true; changed(); });
    connect(diskBrowse, &QPushButton::clicked, this, [this] {
        const auto start = m_diskFolder->text().trimmed().isEmpty() ? replayDiskFolder({}) : m_diskFolder->text().trimmed();
        const auto directory = QFileDialog::getExistingDirectory(this, QStringLiteral("Dossier du buffer du replay"), QDir(start).exists() ? start : QFileInfo(start).absolutePath());
        if (!directory.isEmpty()) m_diskFolder->setText(QDir::toNativeSeparators(directory));
    });
    connect(m_diskFolder, &QLineEdit::textChanged, this, [this] { changed(); });
    m_estimateCalm = new QWidget; auto* calm = new QVBoxLayout(m_estimateCalm); calm->setContentsMargins(0, 0, 0, 0); calm->setSpacing(6);
    m_estimateCalmText = label({}, "hint", true); m_estimateCalmText->setObjectName("memory-estimate"); m_estimateCalmBar = new Bar; calm->addWidget(m_estimateCalmText); calm->addWidget(m_estimateCalmBar); body->addWidget(m_estimateCalm);
    auto* warn = new QFrame; m_estimateWarn = warn; warn->setObjectName("memory-warning"); warn->setProperty("banner", "warn"); auto* warnLayout = new QVBoxLayout(warn); warnLayout->setContentsMargins(12, 12, 12, 12); warnLayout->setSpacing(8);
    auto* warnHead = new QHBoxLayout; warnHead->setSpacing(8); warnHead->addWidget(new IconLabel(QStringLiteral("warn"), tk::warn));
    m_estimateWarnTitle = label({}); setThemedStyleSheet(m_estimateWarnTitle, QStringLiteral("color: {warn}; font-weight: 600;")); warnHead->addWidget(m_estimateWarnTitle, 1); m_estimateWarnLimit = monoLabel({}, 12, tk::text2); warnHead->addWidget(m_estimateWarnLimit);
    warnLayout->addLayout(warnHead); m_estimateWarnBar = new Bar(nullptr, 12); m_estimateWarnBar->setColors(tk::warn, tk::warnSoft); warnLayout->addWidget(m_estimateWarnBar);
    m_estimateWarnText = label({}, "warnText", true); warnLayout->addWidget(m_estimateWarnText); body->addWidget(warn);
    // Each shortcut can also be left out: the action then only has its button.
    const auto withdraw = [](const char* name) { auto* button = new Button(QStringLiteral("x"), QStringLiteral("Retirer"), Button::Ghost, Button::Small); button->setObjectName(name); button->setToolTip(QStringLiteral("Ne plus utiliser de raccourci")); return button; };
    const auto noKey = QStringLiteral("Aucun raccourci");
    auto* hotkeyRow = new QHBoxLayout; hotkeyRow->setSpacing(12); m_hotkeyCaps = new Keycaps; hotkeyRow->addWidget(m_hotkeyCaps);
    m_hotkey = new QKeySequenceEdit; m_hotkey->setObjectName("replay-hotkey"); m_hotkey->setMaximumSequenceLength(1); m_hotkey->hide(); hotkeyRow->addWidget(m_hotkey);
    m_hotkeyChange = new Button(QStringLiteral("Changer"), Button::Secondary, Button::Small); hotkeyRow->addWidget(m_hotkeyChange); auto* hotkeyClear = withdraw("replay-hotkey-clear"); hotkeyRow->addWidget(hotkeyClear); hotkeyRow->addStretch(); m_hotkeyCaps->setEmptyText(noKey);
    auto* hotkeyGroup = new QVBoxLayout; hotkeyGroup->setSpacing(6); hotkeyGroup->addWidget(label(QStringLiteral("Raccourci pour sauvegarder"), "lbl")); hotkeyGroup->addLayout(hotkeyRow);
    m_hotkeyHint = label(QStringLiteral("Fonctionne même quand le jeu est au premier plan."), "hint", true); hotkeyGroup->addWidget(m_hotkeyHint); body->addLayout(hotkeyGroup);
    // Screenshots take what the replay captures, with a shortcut of their own.
    right->addWidget(section(QStringLiteral("Screenshots"), body));
    auto* shotRow = new QHBoxLayout; shotRow->setSpacing(12); m_shotCaps = new Keycaps; shotRow->addWidget(m_shotCaps);
    m_shotKey = new QKeySequenceEdit; m_shotKey->setObjectName("screenshot-hotkey"); m_shotKey->setMaximumSequenceLength(1); m_shotKey->hide(); shotRow->addWidget(m_shotKey);
    m_shotChange = new Button(QStringLiteral("Changer"), Button::Secondary, Button::Small); m_shotChange->setObjectName("screenshot-hotkey-change"); shotRow->addWidget(m_shotChange); auto* shotClear = withdraw("screenshot-hotkey-clear"); shotRow->addWidget(shotClear); shotRow->addStretch(); m_shotCaps->setEmptyText(noKey);
    auto* shotGroup = new QVBoxLayout; shotGroup->setSpacing(6); shotGroup->addWidget(label(QStringLiteral("Raccourci pour un screenshot"), "lbl")); shotGroup->addLayout(shotRow);
    m_shotHint = label({}, "hint", true); shotGroup->addWidget(m_shotHint); body->addLayout(shotGroup);
    const auto shotHint = [this](bool editing) { m_shotHint->setText(editing ? QStringLiteral("Appuyez sur la nouvelle combinaison de touches.") : QStringLiteral("L'écran ou la fenêtre choisis plus haut, en qualité native, dans le dossier « Screenshots ». Fonctionne que le replay tourne ou non.")); };
    shotHint(false);
    connect(m_shotChange, &QPushButton::clicked, this, [this, shotHint] {
        const bool editing = m_shotKey->isVisible();
        m_shotKey->setVisible(!editing); m_shotCaps->setVisible(editing); m_shotChange->setText(editing ? QStringLiteral("Changer") : QStringLiteral("Terminer")); shotHint(!editing);
        if (!editing) m_shotKey->setFocus();
    });
    connect(shotClear, &QPushButton::clicked, this, [this] { if (m_shotKey->isVisible()) m_shotChange->click(); m_shotKey->clear(); });
    connect(m_shotKey, &QKeySequenceEdit::keySequenceChanged, shotClear, [this, shotClear] { shotClear->setVisible(!m_shotKey->keySequence().isEmpty()); });
    connect(m_shotKey, &QKeySequenceEdit::keySequenceChanged, this, [this] { m_shotCaps->setSequence(m_shotKey->keySequence()); if (m_shotKey->isVisible() && !m_shotKey->keySequence().isEmpty()) m_shotChange->click(); changed(); });
    // A recording rides on the replay, with the same quality; one key starts it and stops it.
    right->addWidget(section(QStringLiteral("Enregistrement"), body));
    auto* recordRow = new QHBoxLayout; recordRow->setSpacing(12); m_recordCaps = new Keycaps; recordRow->addWidget(m_recordCaps);
    m_recordKey = new QKeySequenceEdit; m_recordKey->setObjectName("record-hotkey"); m_recordKey->setMaximumSequenceLength(1); m_recordKey->hide(); recordRow->addWidget(m_recordKey);
    m_recordChange = new Button(QStringLiteral("Changer"), Button::Secondary, Button::Small); m_recordChange->setObjectName("record-hotkey-change"); recordRow->addWidget(m_recordChange); auto* recordClear = withdraw("record-hotkey-clear"); recordRow->addWidget(recordClear); recordRow->addStretch(); m_recordCaps->setEmptyText(noKey);
    auto* recordGroup = new QVBoxLayout; recordGroup->setSpacing(6); recordGroup->addWidget(label(QStringLiteral("Raccourci pour démarrer et arrêter"), "lbl")); recordGroup->addLayout(recordRow);
    m_recordHint = label({}, "hint", true); recordGroup->addWidget(m_recordHint); body->addLayout(recordGroup);
    const auto recordHint = [this](bool editing) { m_recordHint->setText(editing ? QStringLiteral("Appuyez sur la nouvelle combinaison de touches.") : QStringLiteral("Enregistre tout, sans limite de durée, jusqu'à ce que vous l'arrêtiez, dans le dossier du jeu. Même qualité que le replay. L'enregistrement tourne seul, ou à côté du replay : chacun a sa barre en haut, et le replay se démarre ou s'arrête sans couper l'enregistrement.")); };
    recordHint(false);
    connect(m_recordChange, &QPushButton::clicked, this, [this, recordHint] {
        const bool editing = m_recordKey->isVisible();
        m_recordKey->setVisible(!editing); m_recordCaps->setVisible(editing); m_recordChange->setText(editing ? QStringLiteral("Changer") : QStringLiteral("Terminer")); recordHint(!editing);
        if (!editing) m_recordKey->setFocus();
    });
    connect(recordClear, &QPushButton::clicked, this, [this] { if (m_recordKey->isVisible()) m_recordChange->click(); m_recordKey->clear(); });
    connect(m_recordKey, &QKeySequenceEdit::keySequenceChanged, recordClear, [this, recordClear] { recordClear->setVisible(!m_recordKey->keySequence().isEmpty()); });
    connect(m_recordKey, &QKeySequenceEdit::keySequenceChanged, this, [this] { m_recordCaps->setSequence(m_recordKey->keySequence()); if (m_recordKey->isVisible() && !m_recordKey->keySequence().isEmpty()) m_recordChange->click(); changed(); });
    // Where a saved clip goes ends what the replay does: it sits under it.
    right->addWidget(section(QStringLiteral("Dossier des clips et captures d'écran"), body));
    auto* folderRow = new QHBoxLayout; folderRow->setSpacing(12); m_folder = new QLineEdit; m_folder->setObjectName("libraryPath"); m_folder->addAction(icon(QStringLiteral("folder")), QLineEdit::LeadingPosition); folderRow->addWidget(m_folder, 1);
    auto* browse = new Button(QStringLiteral("Parcourir…")); browse->setObjectName("library-browse"); folderRow->addWidget(browse); body->addLayout(folderRow);
    body->addWidget(label(QStringLiteral("Un sous-dossier par jeu suffit pour classer. Les captures d'écran vont dans « Screenshots ». Formats lus : MP4, MKV, WebM, MOV, AVI ; PNG, JPEG, WebP, BMP."), "hint", true));
    connect(browse, &QPushButton::clicked, this, &SettingsPage::chooseFolderRequested);
    connect(m_folder, &QLineEdit::textChanged, this, [this] { changed(); });
    right->addStretch();

    connect(m_advancedToggle, &QCheckBox::toggled, m_advanced, &QWidget::setVisible);
    connect(m_cursor, &QCheckBox::toggled, this, [this] { m_cursor->setText(m_cursor->isChecked() ? QStringLiteral("Visible dans les clips") : QStringLiteral("Masqué dans les clips")); changed(); });
    for (auto* combo : {m_videoTarget, m_encoder, m_effort, m_memory, m_storage, m_diskLimit}) connect(combo, &QComboBox::currentIndexChanged, this, [this] { changed(); });
    // The frequency and the codec change what a preset gives; the bitrate, its mode and level set by hand leave the presets.
    connect(m_rates, &Segmented::currentChanged, this, [this] { followPreset(); changed(); });
    connect(m_codec, &QComboBox::currentIndexChanged, this, [this] { followPreset(); changed(); });
    for (auto* combo : {m_rateControl, m_quality}) connect(combo, &QComboBox::currentIndexChanged, this, [this] { leavePreset(); changed(); });
    connect(m_bitrate, &QSpinBox::valueChanged, this, [this] { leavePreset(); changed(); });
    connect(m_duration, &QSpinBox::valueChanged, this, [this](int value) { QSignalBlocker blocker(m_durationSlider); m_durationSlider->setValue(value); changed(); });
    connect(m_durationSlider, &QSlider::valueChanged, m_duration, &QSpinBox::setValue);
    connect(m_hotkeyChange, &QPushButton::clicked, this, [this] {
        const bool editing = m_hotkey->isVisible();
        m_hotkey->setVisible(!editing); m_hotkeyCaps->setVisible(editing); m_hotkeyChange->setText(editing ? QStringLiteral("Changer") : QStringLiteral("Terminer"));
        m_hotkeyHint->setText(editing ? QStringLiteral("Fonctionne même quand le jeu est au premier plan.") : QStringLiteral("Appuyez sur la nouvelle combinaison de touches."));
        if (!editing) m_hotkey->setFocus();
    });
    connect(hotkeyClear, &QPushButton::clicked, this, [this] { if (m_hotkey->isVisible()) m_hotkeyChange->click(); m_hotkey->clear(); });
    connect(m_hotkey, &QKeySequenceEdit::keySequenceChanged, hotkeyClear, [this, hotkeyClear] { hotkeyClear->setVisible(!m_hotkey->keySequence().isEmpty()); });
    connect(m_hotkey, &QKeySequenceEdit::keySequenceChanged, this, [this] { updateHotkey(); changed(); });
    return scrolled(content);
}
QWidget* SettingsPage::buildAudioTab() {
    auto* content = new QWidget; auto* layout = new QVBoxLayout(content); layout->setContentsMargins(24, 16, 24, 16); layout->setSpacing(10);
    auto* top = new QHBoxLayout; top->setSpacing(10);
    auto* intro = new QLabel; intro->setProperty("role", "t2"); intro->setWordWrap(true); top->addWidget(intro, 1);
    setThemedText(intro, QStringLiteral("Chaque <b style='color:{text}'>piste</b> devient une ligne réglable dans l'éditeur. Les <b style='color:{text}'>sources</b> rangées dans une même piste sont mélangées pour de bon."
        " Le curseur d'une piste est son <b style='color:{text}'>volume habituel</b> dans les clips ; l'enregistrement, lui, garde tout à plein niveau."));
    auto* refresh = new Button(QStringLiteral("refresh"), QStringLiteral("Actualiser")); auto* add = new Button(QStringLiteral("plus"), QStringLiteral("Ajouter une source"), Button::Primary); add->setObjectName("add-source"); m_addSource = add; top->addWidget(refresh, 0, Qt::AlignTop); top->addWidget(add, 0, Qt::AlignTop);
    layout->addLayout(top); layout->addSpacing(4);
    m_lanes = new QVBoxLayout; m_lanes->setSpacing(10); layout->addLayout(m_lanes);
    auto* dashed = new DashedButton(QStringLiteral("plus"), QStringLiteral("Ajouter une source ou une piste")); m_addLane = dashed; layout->addWidget(dashed);
    auto* notes = new QHBoxLayout; notes->setSpacing(8); notes->addWidget(new IconLabel(QStringLiteral("info"), tk::text2, 14), 0, Qt::AlignTop); m_sourceNotes = label({}, "hint", true); notes->addWidget(m_sourceNotes, 1); layout->addLayout(notes); layout->addStretch();
    // The levels of a game's clips, learnt from its exports or kept from the editor: listed, to be forgotten.
    m_gameMixHost = new QWidget; auto* gameMixLayout = new QVBoxLayout(m_gameMixHost); gameMixLayout->setContentsMargins(0, 8, 0, 0); gameMixLayout->setSpacing(8);
    gameMixLayout->addWidget(label(QStringLiteral("Volumes par jeu"), "h2"));
    gameMixLayout->addWidget(label(QStringLiteral("Les clips de ces jeux s'ouvrent avec leurs propres volumes ; les pistes qui n'y figurent pas gardent leur volume habituel. Oublier un jeu le ramène aux volumes habituels."), "hint", true));
    auto* gameMixWell = card("well"); gameMixWell->setObjectName("game-mixes"); m_gameMixes = new QVBoxLayout(gameMixWell); m_gameMixes->setContentsMargins(1, 4, 1, 4); m_gameMixes->setSpacing(0); gameMixLayout->addWidget(gameMixWell);
    layout->insertWidget(layout->count() - 1, m_gameMixHost); m_gameMixHost->hide();
    connect(refresh, &QPushButton::clicked, this, [this] { refreshSources(); emit notice(QStringLiteral("Sources audio actualisées"), {}); });
    connect(add, &QPushButton::clicked, this, [this] { editSource(-1); }); connect(dashed, &QPushButton::clicked, this, [this] { editSource(-1); });
    return scrolled(content);
}
QWidget* SettingsPage::buildGeneralTab(const QString& settingsPath) {
    auto* content = new QWidget; auto* outer = new QHBoxLayout(content); outer->setContentsMargins(24, 16, 24, 16);
    auto* column = new QWidget; column->setMaximumWidth(820); auto* layout = new QVBoxLayout(column); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(16); outer->addWidget(column, 1); outer->addStretch(0);
    QVBoxLayout* body;
    // The colors of the application, shown at once in the whole window while they are chosen.
    layout->addWidget(section(QStringLiteral("Thèmes"), body)); m_themeEditor = new ThemeEditor; body->addWidget(m_themeEditor);
    connect(m_themeEditor, &ThemeEditor::changed, this, [this](const ThemeColors& colors) { m_config.theme = themeSettings(colors); applyColors(colors); emit themePreviewed(); changed(); });
    connect(m_themeEditor, &ThemeEditor::notice, this, &SettingsPage::notice);
    layout->addWidget(section(QStringLiteral("Après chaque sauvegarde"), body)); body->setSpacing(12);
    // A sound for each kind of saving, each with its level, heard as chosen: the game usually covers
    // it, so it is tried here first.
    const auto soundRow = [this, body](const QString& text, const QString& name, QCheckBox*& toggle, QWidget*& options, QComboBox*& choice, QSlider*& volume, QLabel*& percent) {
        toggle = new Toggle(text); toggle->setObjectName(name); body->addWidget(toggle);
        options = new QWidget; auto* row = new QHBoxLayout(options); row->setContentsMargins(46, 0, 0, 0); row->setSpacing(12);
        choice = new ChoiceBox; choice->setObjectName(name + QStringLiteral("-choice")); choice->setFixedWidth(200);
        for (const auto& cue : clipSavedSounds()) choice->addItem(cue.name, cue.id);
        row->addWidget(choice);
        auto* quiet = new IconLabel(QStringLiteral("speaker"), tk::text2, 14); quiet->setToolTip(QStringLiteral("Volume du son")); row->addWidget(quiet);
        volume = new QSlider(Qt::Horizontal); volume->setObjectName(name + QStringLiteral("-volume")); volume->setRange(0, 100); volume->setPageStep(10); volume->setMinimumWidth(140); volume->setAccessibleName(QStringLiteral("Volume : ") + text);
        row->addWidget(volume, 1);
        percent = monoLabel(QStringLiteral("50 %"), 12, tk::text2); percent->setFixedWidth(44); percent->setAlignment(Qt::AlignRight | Qt::AlignVCenter); row->addWidget(percent);
        auto* listen = new Button(QStringLiteral("play"), QStringLiteral("Écouter"), Button::Secondary, Button::Small); listen->setObjectName(name + QStringLiteral("-preview")); row->addWidget(listen);
        body->addWidget(options);
        auto* sound = choice; auto* level = volume; auto* shown = percent;
        const auto preview = [sound, level] { playClipSavedSound(sound->currentData().toString(), level->value()); };
        connect(listen, &QPushButton::clicked, this, preview);
        // A sound picked, or a level let go of, is heard at once.
        connect(sound, &QComboBox::activated, this, preview);
        connect(level, &QSlider::sliderReleased, this, preview);
        connect(level, &QSlider::valueChanged, this, [this, shown](int value) { shown->setText(QStringLiteral("%1 %").arg(value)); changed(); });
        connect(sound, &QComboBox::currentIndexChanged, this, [this] { changed(); });
        connect(toggle, &QCheckBox::toggled, options, &QWidget::setEnabled);
    };
    soundRow(QStringLiteral("Jouer un son pour un clip"), QStringLiteral("clip-saved-sound"), m_savedSound, m_soundOptions, m_sound, m_soundVolume, m_soundPercent);
    soundRow(QStringLiteral("Jouer un son pour un screenshot"), QStringLiteral("screenshot-sound"), m_shotSound, m_shotSoundOptions, m_shotSoundChoice, m_shotVolume, m_shotPercent);
    soundRow(QStringLiteral("Jouer un son pour un enregistrement"), QStringLiteral("record-sound"), m_recordSound, m_recordSoundOptions, m_recordSoundChoice, m_recordVolume, m_recordPercent);
    auto* recordSoundHint = label(QStringLiteral("Une fois au démarrage, deux fois à l'arrêt."), "hint"); recordSoundHint->setContentsMargins(46, 0, 0, 0); body->addWidget(recordSoundHint);
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
    // Nothing here starts by itself unless the user asks: both settings are off at first.
    layout->addWidget(section(QStringLiteral("Démarrage automatique"), body)); body->setSpacing(12);
#ifdef Q_OS_WIN
    m_loginStart = new Toggle(QStringLiteral("Lancer Clipsapp à l'ouverture de la session Windows"));
#else
    m_loginStart = new Toggle(QStringLiteral("Lancer Clipsapp à l'ouverture de la session"));
#endif
    m_loginStart->setObjectName("login-start"); body->addWidget(m_loginStart);
    // How it shows itself then: behind its icon, out of the way, or its window in front.
    m_loginWindow = new QWidget; auto* loginWindowLayout = new QVBoxLayout(m_loginWindow); loginWindowLayout->setContentsMargins(46, 0, 0, 0); loginWindowLayout->setSpacing(6);
    m_loginShow = new ChoiceBox; m_loginShow->setObjectName("login-window"); m_loginShow->setFixedWidth(320);
    m_loginShow->addItem(QStringLiteral("Réduit dans la zone de notification"), false); m_loginShow->addItem(QStringLiteral("Fenêtre au premier plan"), true); loginWindowLayout->addWidget(m_loginShow);
    m_loginHint = label({}, "hint", true); loginWindowLayout->addWidget(m_loginHint); body->addWidget(m_loginWindow);
    m_loginStart->hide(); m_loginWindow->hide();
    connect(m_loginShow, &QComboBox::currentIndexChanged, this, [this] { updateLoginHint(); changed(); });
    connect(m_loginStart, &QCheckBox::toggled, this, &SettingsPage::updateLoginHint); connect(m_closeToTray, &QCheckBox::toggled, this, &SettingsPage::updateLoginHint);
    m_autoReplay = new ChoiceBox; m_autoReplay->setObjectName("auto-replay"); m_autoReplay->setFixedWidth(320);
    m_autoReplay->addItem(QStringLiteral("Désactivé · je le démarre moi-même"), "off"); m_autoReplay->addItem(QStringLiteral("Dès le lancement de Clipsapp"), "launch"); m_autoReplay->addItem(QStringLiteral("Quand un de mes jeux est ouvert"), "games");
    body->addLayout(field(QStringLiteral("Replay automatique"), m_autoReplay));
    m_autoHint = label({}, "hint", true); m_autoHint->setObjectName("auto-replay-hint"); body->addWidget(m_autoHint);
    m_gamesHost = new QWidget; auto* gamesLayout = new QVBoxLayout(m_gamesHost); gamesLayout->setContentsMargins(0, 0, 0, 0); gamesLayout->setSpacing(8);
    auto* gamesWell = card("well"); gamesWell->setObjectName("replay-games"); m_games = new QVBoxLayout(gamesWell); m_games->setContentsMargins(1, 4, 1, 4); m_games->setSpacing(0); gamesLayout->addWidget(gamesWell);
    m_gamesEmpty = label(QStringLiteral("Aucun jeu choisi : ajoutez ceux dont l'ouverture démarre le replay."), "hint", true); m_gamesEmpty->setContentsMargins(12, 8, 12, 8); m_games->addWidget(m_gamesEmpty);
    auto* addGame = new Button(QStringLiteral("plus"), QStringLiteral("Ajouter un jeu"), Button::Secondary, Button::Small); addGame->setObjectName("add-replay-game"); gamesLayout->addWidget(addGame, 0, Qt::AlignLeft);
    body->addWidget(m_gamesHost); m_gamesHost->hide();
    connect(addGame, &QPushButton::clicked, this, [this] { chooseGame(); });
    connect(m_autoReplay, &QComboBox::currentIndexChanged, this, [this] { changed(); populateGames(); });
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
    auto* openFolder = new Button(QStringLiteral("folder"), QStringLiteral("Ouvrir le dossier"), Button::Secondary, Button::Small); pathRow->addWidget(openFolder); body->addLayout(pathRow);
    // The journal stays here too. A diagnostic is what a report of a problem needs, copied on purpose.
    auto* journalRow = new QHBoxLayout; journalRow->setSpacing(12);
    journalRow->addWidget(label(QStringLiteral("Un journal de ce que fait Clipsapp est gardé dans le sous-dossier « logs », 2 Mo au plus. Rien n'est envoyé : « Copier le diagnostic » le met dans le presse-papiers, sans le nom de votre dossier personnel, pour le joindre à un signalement."), "hint", true), 1);
    auto* diagnostic = new Button(QStringLiteral("copy"), QStringLiteral("Copier le diagnostic"), Button::Secondary, Button::Small); diagnostic->setObjectName("copy-diagnostic"); journalRow->addWidget(diagnostic, 0, Qt::AlignTop); body->addLayout(journalRow); layout->addStretch();
    connect(openFolder, &QPushButton::clicked, this, &SettingsPage::openSettingsFolderRequested);
    connect(diagnostic, &QPushButton::clicked, this, &SettingsPage::copyDiagnosticRequested);
    for (auto* toggle : {m_savedSound, m_shotSound, m_recordSound, m_savedNotify, m_checkUpdates, m_closeToTray, m_startMenu, m_loginStart}) connect(toggle, &QCheckBox::toggled, this, [this] { changed(); });
    return scrolled(content);
}
void SettingsPage::setStartMenu(bool managed) { m_startMenu->setVisible(managed); }
void SettingsPage::updateLoginHint() {
    const bool visible = m_loginShow->currentData().toBool();
    m_loginHint->setText(visible ? QStringLiteral("La fenêtre de Clipsapp s'ouvre au premier plan dès l'ouverture de la session.")
        : m_closeToTray->isChecked() ? QStringLiteral("Clipsapp s'ouvre discrètement, près de l'horloge : un clic sur son icône affiche la fenêtre.") : QStringLiteral("La zone de notification n'est pas utilisée (voir plus haut) : la fenêtre s'ouvre au premier plan."));
    m_loginShow->setEnabled(m_loginStart->isChecked());
}
void SettingsPage::setLoginStart(bool managed) { m_loginStart->setVisible(managed); m_loginWindow->setVisible(managed); }
void SettingsPage::addReplayGame(const QString& executable) {
    const auto path = QDir::fromNativeSeparators(executable.trimmed());
    if (path.isEmpty() || m_config.replayGames.contains(path, Qt::CaseInsensitive) || m_config.replayGames.size() >= 64) return;
    m_config.replayGames.append(path); populateGames(); changed();
}
void SettingsPage::chooseGame() {
    // The programs that have a window open now come first: a game is usually running when it is added.
    QMenu menu(this); menu.setObjectName("replay-game-menu"); QSet<QString> offered;
    for (const auto& target : SourceCatalog::discover().videoTargets) {
        if (!target.id.startsWith(QLatin1String("window:"))) continue;
        const auto path = target.id.mid(7).section('|', 0, 0); const auto identity = path.toCaseFolded();
        if (offered.contains(identity) || m_config.replayGames.contains(path, Qt::CaseInsensitive)) continue;
        offered.insert(identity);
        const auto title = target.id.section('|', 1); const auto name = gameNameFromApplication(path, title);
        auto* action = menu.addAction(icon(QStringLiteral("window")), QStringLiteral("%1 · %2").arg(name.isEmpty() ? title : name, QFileInfo(path).fileName()));
        connect(action, &QAction::triggered, this, [this, path] { addReplayGame(path); });
    }
    if (offered.isEmpty()) menu.addAction(QStringLiteral("Aucune autre application ouverte"))->setEnabled(false);
    menu.addSeparator();
    connect(menu.addAction(icon(QStringLiteral("folder")), QStringLiteral("Choisir le programme du jeu…")), &QAction::triggered, this, [this] {
#ifdef Q_OS_WIN
        const auto path = QFileDialog::getOpenFileName(this, QStringLiteral("Programme du jeu"), QString(), QStringLiteral("Programmes (*.exe)"));
#else
        const auto path = QFileDialog::getOpenFileName(this, QStringLiteral("Programme du jeu"));
#endif
        addReplayGame(path);
    });
    auto* button = findChild<QWidget*>("add-replay-game"); menu.exec(button ? button->mapToGlobal(QPoint(0, button->height() + 4)) : QCursor::pos());
}
void SettingsPage::populateGameMixes() {
    while (auto* child = m_gameMixes->takeAt(0)) { if (child->widget()) { child->widget()->hide(); child->widget()->deleteLater(); } delete child; }
    m_gameMixHost->setVisible(!m_config.gameMixes.isEmpty());
    int index = 0;
    for (auto game = m_config.gameMixes.cbegin(); game != m_config.gameMixes.cend(); ++game, ++index) {
        // Tracks are kept by their folded name: the profile gives back the way it is written.
        QStringList levels;
        for (auto track = game->cbegin(); track != game->cend(); ++track) {
            QString name = track.key(); for (const auto& known : m_config.profiles) for (const auto& candidate : known.tracks) if (mixKey(candidate.name) == track.key()) name = candidate.name;
            levels << QStringLiteral("%1 %2 %").arg(name).arg(qRound(track.value() * 100));
        }
        auto* row = new QWidget; row->setObjectName(QStringLiteral("game-mix-%1").arg(index)); row->setFixedHeight(48); auto* rowLayout = new QHBoxLayout(row); rowLayout->setContentsMargins(12, 0, 8, 0); rowLayout->setSpacing(10);
        rowLayout->addWidget(new IconBox(QStringLiteral("speaker")));
        auto* texts = new QVBoxLayout; texts->setSpacing(0); texts->addStretch(); texts->addWidget(new ElideLabel(game.key(), "strong")); texts->addWidget(new ElideLabel(levels.join(QStringLiteral(" · ")), "small")); texts->addStretch(); rowLayout->addLayout(texts, 1);
        auto* forget = new Button(QStringLiteral("undo"), QStringLiteral("Oublier"), Button::Ghost, Button::Small); forget->setObjectName(QStringLiteral("forget-game-mix-%1").arg(index)); forget->setToolTip(QStringLiteral("Les clips %1 reprennent les volumes habituels").arg(game.key())); rowLayout->addWidget(forget);
        const auto name = game.key(); connect(forget, &QPushButton::clicked, this, [this, name] { m_config.gameMixes.remove(name); m_config.gameMixHistory.remove(name); populateGameMixes(); changed(); });
        m_gameMixes->addWidget(row);
    }
}
void SettingsPage::populateGames() {
    const auto mode = m_autoReplay->currentData().toString();
    m_autoHint->setText(mode == QLatin1String("launch") ? QStringLiteral("Le replay démarre avec Clipsapp. Il enregistre l'écran choisi dans Capture, ou l'écran principal si c'est une fenêtre qui y est choisie.")
        : mode == QLatin1String("games") ? QStringLiteral("Le replay démarre à l'ouverture d'un de ces jeux et s'arrête 30 secondes après sa fermeture. Il enregistre l'écran choisi dans Capture, ou l'écran principal si c'est une fenêtre qui y est choisie.")
        : QStringLiteral("Le replay démarre quand vous le lancez, depuis la barre du haut ou l'icône près de l'horloge."));
    m_gamesHost->setVisible(mode == QLatin1String("games"));
    while (m_games->count() > 1) { auto* child = m_games->takeAt(1); if (child->widget()) { child->widget()->hide(); child->widget()->deleteLater(); } delete child; }
    m_gamesEmpty->setVisible(m_config.replayGames.isEmpty());
    for (int index = 0; index < m_config.replayGames.size(); ++index) {
        const auto path = m_config.replayGames[index]; const auto name = gameNameFromApplication(path, {});
        auto* row = new QWidget; row->setObjectName(QStringLiteral("replay-game-%1").arg(index)); row->setFixedHeight(48); auto* rowLayout = new QHBoxLayout(row); rowLayout->setContentsMargins(12, 0, 8, 0); rowLayout->setSpacing(10);
        rowLayout->addWidget(new IconBox(QStringLiteral("app")));
        auto* texts = new QVBoxLayout; texts->setSpacing(0); texts->addStretch(); texts->addWidget(new ElideLabel(name.isEmpty() ? QFileInfo(path).completeBaseName() : name, "strong"));
        texts->addWidget(new ElideLabel(QDir::toNativeSeparators(path), "small", Qt::ElideMiddle)); texts->addStretch(); rowLayout->addLayout(texts, 1);
        auto* remove = new Button(QStringLiteral("x"), QString(), Button::Ghost, Button::Small); remove->setObjectName(QStringLiteral("remove-replay-game-%1").arg(index)); remove->setToolTip(QStringLiteral("Ne plus démarrer le replay avec ce jeu")); remove->setAccessibleName(remove->toolTip()); rowLayout->addWidget(remove);
        connect(remove, &QPushButton::clicked, this, [this, path] { m_config.replayGames.removeAll(path); populateGames(); changed(); });
        m_games->addWidget(row);
    }
}
void SettingsPage::setUpdates(bool followed, const QString& version) { m_updateSection->setVisible(followed); m_version->setText(QStringLiteral("Version installée : %1").arg(version)); }
void SettingsPage::showTab(int index) {
    if (index < 0 || index >= m_tabs->count()) return;
    m_tabs->setCurrentIndex(index); for (int tab = 0; tab < m_tabButtons.size(); ++tab) static_cast<QPushButton*>(m_tabButtons[tab])->setChecked(tab == index);
    // The applications that play sound change all the time: the audio tab shows those of the moment.
    if (index == 1) refreshSources();
}
int SettingsPage::currentTab() const { return m_tabs->currentIndex(); }
QKeySequence SettingsPage::hotkey() const { return m_hotkey->keySequence(); }
bool SettingsPage::setFps(int fps) {
    const auto found = std::find(std::begin(frameRates), std::end(frameRates), fps); if (found == std::end(frameRates)) return false;
    m_rates->setCurrentIndex(int(found - std::begin(frameRates))); return true;
}
// The size shown as chosen; the first one if none is, as for a profile written by hand.
int SettingsPage::selectedSize() const { for (int index = 0; index < m_sizes.size(); ++index) if (m_sizes[index]->isChecked()) return index; return 0; }
// The settings of the preset chosen, at the size, frequency and codec shown.
void SettingsPage::followPreset() {
    if (m_preset < 0) return;
    VideoSettings video; video.preset = QString::fromLatin1(qualityPresets[m_preset].key); video.width = videoSizes[selectedSize()].width; video.height = videoSizes[selectedSize()].height;
    video.fps = frameRates[std::max(0, m_rates->currentIndex())]; video.codec = m_codec->currentData().toString(); applyQualityPreset(video);
    m_applying = true; m_rateControl->setCurrentIndex(m_rateControl->findData(video.rateControl)); m_bitrate->setValue(video.bitrateMbps); m_applying = false;
}
void SettingsPage::leavePreset() { if (!m_applying && !m_loading) m_preset = -1; }
void SettingsPage::setFolder(const QString& path) { m_folder->setText(QDir::toNativeSeparators(path)); }
void SettingsPage::setSaved(const AppConfig& saved) { m_saved = saved; refreshDirty(); }
// While the replay or a recording runs, only what the capture uses waits for it: what is captured,
// its quality, the buffer of the replay, the audio sources and tracks, and the profile itself. The
// rest (shortcuts, folder, usual volumes, the whole General tab) applies at once, without restarting it.
void SettingsPage::setLocked(bool locked) {
    if (locked == m_locked) return;
    m_locked = locked; m_lock->setVisible(locked); m_profiles->setEnabled(!locked); m_duplicate->setEnabled(!locked);
    for (auto* used : std::initializer_list<QWidget*>{m_targetSection, m_qualitySection, m_durationSlider, m_duration, m_storage, m_diskProposal, m_memoryField, m_diskBox, m_addSource, m_addLane}) used->setEnabled(!locked);
    populateSources(); refreshDirty();
}
void SettingsPage::changed() {
    if (m_loading) return;
    collect(); updateEstimate(); updateQuality(); refreshDirty(); emit edited();
}
void SettingsPage::refreshCatalog() {
    m_catalog = SourceCatalog::discover(); populateVideoTargets();
    m_sourceNotes->setText(m_catalog.notes.isEmpty() ? QStringLiteral("Seules les applications qui jouent du son apparaissent dans la liste. Une application arrêtée sera enregistrée dès qu'elle émet du son.") : m_catalog.notes.join('\n'));
}
void SettingsPage::refreshSources() { if (m_loading) return; collect(); refreshCatalog(); populateSources(); }
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
    for (int index = 0; index < m_sizes.size(); ++index) m_sizes[index]->setChecked(videoSizes[index].width == p.video.width && videoSizes[index].height == p.video.height);
    setFps(p.video.fps); m_codec->setCurrentIndex(m_codec->findData(p.video.codec));
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
    m_memory->setCurrentIndex(memoryIndex);
    m_storage->setCurrentIndex(std::max(0, m_storage->findData(p.replay.storage)));
    int diskIndex = m_diskLimit->findData(p.replay.diskLimitMiB);
    if (diskIndex < 0) { diskIndex = 0; while (diskIndex < m_diskLimit->count() && m_diskLimit->itemData(diskIndex).toInt() < p.replay.diskLimitMiB) ++diskIndex; m_diskLimit->insertItem(diskIndex, QStringLiteral("%1 Gio").arg(QLocale(QLocale::French, QLocale::France).toString(p.replay.diskLimitMiB / 1024.0, 'g', 4)), p.replay.diskLimitMiB); }
    m_diskLimit->setCurrentIndex(diskIndex); m_diskFolder->setText(QDir::toNativeSeparators(p.replay.diskFolder));
    m_hotkey->setKeySequence(QKeySequence::fromString(p.replay.hotkey, QKeySequence::PortableText)); updateHotkey();
    setFolder(m_config.libraryPath); m_savedSound->setChecked(m_config.clipSavedSound); m_savedNotify->setChecked(m_config.clipSavedNotification); m_checkUpdates->setChecked(m_config.checkUpdates);
    m_sound->setCurrentIndex(std::max(0, m_sound->findData(m_config.clipSavedSoundId))); m_soundVolume->setValue(m_config.clipSavedSoundVolume); m_soundPercent->setText(QStringLiteral("%1 %").arg(m_config.clipSavedSoundVolume)); m_soundOptions->setEnabled(m_config.clipSavedSound);
    m_shotSound->setChecked(m_config.screenshotSound); m_shotSoundChoice->setCurrentIndex(std::max(0, m_shotSoundChoice->findData(m_config.screenshotSoundId))); m_shotVolume->setValue(m_config.screenshotSoundVolume); m_shotPercent->setText(QStringLiteral("%1 %").arg(m_config.screenshotSoundVolume)); m_shotSoundOptions->setEnabled(m_config.screenshotSound);
    m_shotKey->setKeySequence(QKeySequence::fromString(m_config.screenshotHotkey, QKeySequence::PortableText)); m_shotCaps->setSequence(m_shotKey->keySequence()); populateGameMixes();
    m_recordKey->setKeySequence(QKeySequence::fromString(m_config.recordHotkey, QKeySequence::PortableText)); m_recordCaps->setSequence(m_recordKey->keySequence());
    m_recordSound->setChecked(m_config.recordSound); m_recordSoundChoice->setCurrentIndex(std::max(0, m_recordSoundChoice->findData(m_config.recordSoundId))); m_recordVolume->setValue(m_config.recordSoundVolume); m_recordPercent->setText(QStringLiteral("%1 %").arg(m_config.recordSoundVolume)); m_recordSoundOptions->setEnabled(m_config.recordSound);
    // Given back or loaded, the theme is shown again as it is written.
    { const auto colors = themeFromSettings(m_config.theme); m_themeEditor->setTheme(colors); applyColors(colors); emit themePreviewed(); }
    m_closeToTray->setChecked(m_config.closeToTray); m_startMenu->setChecked(m_config.startMenuShortcut);
    m_loginStart->setChecked(m_config.launchAtLogin); m_loginShow->setCurrentIndex(m_config.launchVisible ? 1 : 0); updateLoginHint(); m_autoReplay->setCurrentIndex(std::max(0, m_autoReplay->findData(m_config.autoReplay))); populateGames();
    m_preset = -1; for (int index = 0; index < int(std::size(qualityPresets)); ++index) if (p.video.preset == QLatin1String(qualityPresets[index].key)) m_preset = index;
    populateSources(); m_loading = false; updateEstimate(); updateQuality(); refreshDirty();
    // A quality set by hand is shown where it was set.
    if (m_preset < 0) m_advancedToggle->setChecked(true);
}
void SettingsPage::collect() {
    auto& p = profile(); const auto& size = videoSizes[selectedSize()]; p.video.width = size.width; p.video.height = size.height;
    p.video.targetId = m_videoTarget->currentData().toString(); p.video.targetLabel = p.video.targetId.isEmpty() ? QString() : m_videoTarget->currentData(Qt::UserRole + 1).toString();
    p.video.fps = frameRates[std::max(0, m_rates->currentIndex())]; p.video.preset = m_preset >= 0 ? QString::fromLatin1(qualityPresets[m_preset].key) : QString(); p.video.codec = m_codec->currentData().toString(); p.video.encoder = m_encoder->currentData().toString();
    p.video.bitrateMbps = m_bitrate->value(); p.video.captureCursor = m_cursor->isChecked();
    p.video.rateControl = m_rateControl->currentData().toString(); p.video.quality = m_quality->currentData().toInt(); p.video.effort = m_effort->currentData().toString();
    p.gameName.clear(); p.detectGame = true;
    p.replay.durationSeconds = m_duration->value(); p.replay.memoryLimitMiB = m_memory->currentData().toInt(); p.replay.hotkey = m_hotkey->keySequence().toString(QKeySequence::PortableText);
    p.replay.storage = m_storage->currentData().toString(); p.replay.diskLimitMiB = m_diskLimit->currentData().toInt(); p.replay.diskFolder = QDir::fromNativeSeparators(m_diskFolder->text().trimmed());
    // Chosen once, the disk answers the proposal: it comes back if the memory is chosen again.
    if (p.replay.storage == QLatin1String("disk")) p.replay.keepMemory = false;
    m_config.libraryPath = QDir::fromNativeSeparators(m_folder->text().trimmed());
    m_config.clipSavedSound = m_savedSound->isChecked(); m_config.clipSavedNotification = m_savedNotify->isChecked(); m_config.checkUpdates = m_checkUpdates->isChecked();
    m_config.clipSavedSoundId = m_sound->currentData().toString(); m_config.clipSavedSoundVolume = m_soundVolume->value();
    m_config.screenshotSound = m_shotSound->isChecked(); m_config.screenshotSoundId = m_shotSoundChoice->currentData().toString(); m_config.screenshotSoundVolume = m_shotVolume->value();
    m_config.screenshotHotkey = m_shotKey->keySequence().toString(QKeySequence::PortableText);
    m_config.recordHotkey = m_recordKey->keySequence().toString(QKeySequence::PortableText);
    m_config.recordSound = m_recordSound->isChecked(); m_config.recordSoundId = m_recordSoundChoice->currentData().toString(); m_config.recordSoundVolume = m_recordVolume->value();
    m_config.closeToTray = m_closeToTray->isChecked(); m_config.startMenuShortcut = m_startMenu->isChecked();
    m_config.launchAtLogin = m_loginStart->isChecked(); m_config.launchVisible = m_loginShow->currentData().toBool(); m_config.autoReplay = m_autoReplay->currentData().toString();
}
void SettingsPage::updateHotkey() { m_hotkeyCaps->setSequence(m_hotkey->keySequence()); if (m_hotkey->isVisible() && !m_hotkey->keySequence().isEmpty()) m_hotkeyChange->click(); }
void SettingsPage::updateQuality() {
    const auto& video = profile().video;
    const bool constant = video.rateControl == "quality";
    // Each preset says what it gives at the size, the frequency and the codec chosen.
    const auto perMinute = [](int mbps) { return formatBytes(qint64(mbps) * 1000000 * 60 / 8); };
    for (int index = 0; index < m_presets.size(); ++index) {
        const int mbps = presetBitrate(QString::fromLatin1(qualityPresets[index].key), video.width, video.height, video.fps, video.codec);
        m_presets[index]->setLines(QStringLiteral("%1 Mbit/s").arg(mbps), QStringLiteral("≈ %1 / min").arg(perMinute(mbps))); m_presets[index]->setChecked(index == m_preset);
    }
    m_presetText->setText(m_preset >= 0 ? QString::fromUtf8(qualityPresets[m_preset].text) : QStringLiteral("Personnalisé : la qualité est réglée à la main, dans les réglages avancés. Choisissez un préréglage pour revenir à un réglage simple."));
    const int seconds = profile().replay.durationSeconds; const auto clip = formatBytes(qint64(video.bitrateMbps) * 1000000 * seconds / 8);
    m_presetWeight->setText(constant ? QStringLiteral("Un clip de %1 s pèse au plus %2, souvent bien moins.").arg(seconds).arg(clip) : QStringLiteral("Un clip de %1 s pèse environ %2.").arg(seconds).arg(clip));
    // The level only means something in constant quality, where the bitrate turns into a ceiling.
    m_bitrateLabel->setText(constant ? QStringLiteral("Débit maximal") : QStringLiteral("Débit vidéo")); m_qualityLabel->setVisible(constant); m_quality->setVisible(constant);
}
void SettingsPage::updateEstimate() {
    if (m_loading) return;
    const auto& p = profile(); const bool disk = p.replay.storage == QLatin1String("disk");
    const double estimated = estimatedBufferMiB(p); const int limit = disk ? p.replay.diskLimitMiB : p.replay.memoryLimitMiB; const bool over = estimated > limit;
    m_estimateCalm->setVisible(!over); m_estimateWarn->setVisible(over); m_memoryField->setVisible(!disk); m_diskBox->setVisible(disk);
    // A constant quality has no fixed weight: the page gives what it usually fills, from a calm
    // game to a busy one, and what its ceiling would fill at most.
    const bool constant = p.video.rateControl == "quality"; const auto usual = usualBufferMiB(p); const int low = qRound(usual.low), high = qRound(usual.high);
    const QLocale french(QLocale::French, QLocale::France); const auto gib = [&french](double mib) { return french.toString(mib / 1024, 'f', 1) + QStringLiteral(" Gio"); };
    // A buffer that would take a good share of the memory of the PC: the disk is proposed, once.
    const qint64 installed = physicalMemoryBytes();
    const bool propose = !disk && !p.replay.keepMemory && suggestsDiskBuffer(p, installed); m_diskProposal->setVisible(propose);
    if (propose) {
        m_diskProposalTitle->setText(QStringLiteral("Un replay de ≈ %1 en mémoire").arg(gib(usual.high)));
        m_diskProposalText->setText(QStringLiteral("C'est %1 % de la mémoire de ce PC (%2). Gardé sur le disque, il laisse cette mémoire au jeu et au système : moins de risque de saccades ou de plantage quand la mémoire vient à manquer. Un SSD est conseillé.")
            .arg(qRound(usual.high * 1048576.0 * 100 / double(installed))).arg(gib(installed / 1048576.0)));
    }
    if (disk) {
        // The folder may not exist yet: the space is that of the drive that will hold it.
        QString folder = replayDiskFolder(p.replay); while (!QDir(folder).exists() && QFileInfo(folder).absolutePath() != folder) folder = QFileInfo(folder).absolutePath();
        const QStorageInfo volume(folder); QStringList notes;
        if (volume.isValid()) {
            notes << QStringLiteral("%1 libres sur ce disque.").arg(gib(volume.bytesAvailable() / 1048576.0));
            if (volume.bytesAvailable() / 1048576.0 < std::min<double>(estimated, limit) + 256) notes << QStringLiteral("Pas assez de place pour ce replay : libérez-en ou choisissez un autre dossier.");
        }
        if (rotationalDrive(folder) == 1) notes << QStringLiteral("Ce dossier est sur un disque dur : un SSD est préférable.");
        m_diskSpace->setText(notes.join(' ')); m_diskSpace->setVisible(!notes.isEmpty());
        const auto amount = constant && low != high ? QStringLiteral("≈ %1 à %2").arg(french.toString(usual.low / 1024, 'f', 1), gib(usual.high)) : QStringLiteral("≈ %1").arg(gib(constant ? usual.high : estimated));
        if (!over) {
            m_estimateCalmText->setText(constant ? QStringLiteral("Espace estimé sur le disque : %1 selon le jeu, %2 au plus, sur un plafond de %3.").arg(amount, gib(estimated), gib(limit)) : QStringLiteral("Espace estimé sur le disque : %1 sur un plafond de %2.").arg(amount, gib(limit)));
            m_estimateCalmBar->setValue((constant ? usual.high : estimated) / limit); return;
        }
        int next = 0; for (int index = 0; index < m_diskLimit->count(); ++index) if (!next && m_diskLimit->itemData(index).toInt() >= estimated) next = m_diskLimit->itemData(index).toInt();
        m_estimateWarnTitle->setText(QStringLiteral("Espace estimé sur le disque : %1").arg(amount)); m_estimateWarnLimit->setText(QStringLiteral("plafond %1").arg(gib(limit)));
        m_estimateWarnBar->setValue(1); m_estimateWarnBar->setMarker(limit / estimated);
        m_estimateWarnText->setText(QStringLiteral("Au-delà du plafond, les secondes les plus anciennes sont perdues. Réduisez la durée ou le débit") + (next ? QStringLiteral(", ou montez le plafond à %1.").arg(gib(next)) : QStringLiteral(".")));
        return;
    }
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
        auto* head = new QFrame; head->setObjectName("laneHead"); head->setFixedWidth(196); setThemedStyleSheet(head, QStringLiteral("QFrame#laneHead { background: {header}; border: none; border-right: 1px solid {line}; border-top-left-radius: 7px; border-bottom-left-radius: 7px; }"));
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
            if (position) { auto* line = separator(); setThemedStyleSheet(line, QStringLiteral("background: {lineSoft};")); sources->addWidget(line); }
            QString state = QStringLiteral("À choisir"); Tag::Variant variant = Tag::Warn;
            if (source.kind == SourceKind::ApplicationMix) { state = QStringLiteral("Automatique"); variant = Tag::Ok; }
            else if (source.targetId == "@default") { const bool present = !m_catalog.audioTargets(source.kind).isEmpty(); state = present ? QStringLiteral("Par défaut") : QStringLiteral("Périphérique absent"); variant = present ? Tag::Info : Tag::Err; }
            else if (!source.targetId.isEmpty()) {
                // An application is available while it is open, even silent.
                bool available = source.kind == SourceKind::Application && !m_catalog.runningApplication(source.targetId).id.isEmpty();
                for (const auto& target : m_catalog.audioTargets(source.kind))
                    if (source.kind != SourceKind::Application && QDir::toNativeSeparators(target.id).compare(QDir::toNativeSeparators(source.targetId), Qt::CaseInsensitive) == 0) available = true;
                state = available ? QStringLiteral("Disponible") : source.kind == SourceKind::Application ? QStringLiteral("Application arrêtée") : QStringLiteral("Périphérique absent");
                variant = available ? Tag::Ok : source.kind == SourceKind::Application ? Tag::Warn : Tag::Err;
            }
            QString target = source.targetLabel.isEmpty() ? sourceKindLabel(source.kind) + QStringLiteral(" à choisir") : source.targetLabel;
            if (source.kind == SourceKind::ApplicationMix) {
                const auto exclusions = applicationExclusions(source, p); QStringList names; for (const auto& path : exclusions) names.append(QFileInfo(path).fileName());
                target = exclusions.isEmpty() ? QStringLiteral("Toutes les applications") : QStringLiteral("Toutes les applications sauf %1 : %2").arg(exclusions.size()).arg(names.join(QStringLiteral(", ")));
            } else if (source.targetId == "@default" && source.kind == SourceKind::Microphone) target = QStringLiteral("Microphone par défaut · suit le système");
            // During a capture, the sources wait for it: the row is shown, not changed.
            auto* line = new QWidget; line->setFixedHeight(54); line->setEnabled(!m_locked); auto* lineLayout = new QHBoxLayout(line); lineLayout->setContentsMargins(14, 0, 12, 0); lineLayout->setSpacing(12);
            lineLayout->addWidget(new IconBox(sourceIcon(source.kind)));
            auto* texts = new QVBoxLayout; texts->setSpacing(0); texts->addStretch(); auto* name = new ElideLabel(source.name, "strong"); if (!source.enabled) setThemedStyleSheet(name, QStringLiteral("color: {muted}; font-weight: 600;"));
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
    // The list offered is that of the moment, not the one read when Clipsapp started.
    collect(); refreshCatalog(); const auto& p = profile();
    SourceDialog dialog(m_catalog, p, index >= 0 ? &p.sources[index] : nullptr, this);
    if (dialog.exec() != QDialog::Accepted) { populateSources(); return; }
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
        // A preset chosen whose settings happen to be those already saved is a change all the same, counted once.
        if (!quality && p.video.preset != saved->video.preset) { quality = true; note(true, QStringLiteral("qualité")); }
        note(p.replay.durationSeconds != saved->replay.durationSeconds, QStringLiteral("durée du replay")); note(p.replay.memoryLimitMiB != saved->replay.memoryLimitMiB, QStringLiteral("plafond de mémoire")); note(p.replay.hotkey != saved->replay.hotkey, QStringLiteral("raccourci"));
        const bool buffer = p.replay.storage != saved->replay.storage || p.replay.diskLimitMiB != saved->replay.diskLimitMiB || p.replay.diskFolder != saved->replay.diskFolder || p.replay.keepMemory != saved->replay.keepMemory;
        note(buffer, QStringLiteral("buffer du replay"));
        replay = buffer || p.replay.durationSeconds != saved->replay.durationSeconds || p.replay.memoryLimitMiB != saved->replay.memoryLimitMiB || p.replay.hotkey != saved->replay.hotkey;
        auto current = p, before = *saved; current.video = before.video; current.replay = before.replay; current.gameName = before.gameName; current.detectGame = before.detectGame;
        note(json(current) != json(before), QStringLiteral("audio"));
    }
    note(m_config.activeProfileId != m_saved.activeProfileId && saved, QStringLiteral("profil actif"));
    note(m_config.libraryPath != m_saved.libraryPath, QStringLiteral("dossier des clips"));
    note(m_config.clipSavedSound != m_saved.clipSavedSound || m_config.clipSavedNotification != m_saved.clipSavedNotification, QStringLiteral("alertes"));
    note(m_config.clipSavedSoundId != m_saved.clipSavedSoundId || m_config.clipSavedSoundVolume != m_saved.clipSavedSoundVolume, QStringLiteral("son de sauvegarde"));
    note(m_config.screenshotSound != m_saved.screenshotSound || m_config.screenshotSoundId != m_saved.screenshotSoundId || m_config.screenshotSoundVolume != m_saved.screenshotSoundVolume, QStringLiteral("son des screenshots"));
    note(m_config.recordSound != m_saved.recordSound || m_config.recordSoundId != m_saved.recordSoundId || m_config.recordSoundVolume != m_saved.recordSoundVolume, QStringLiteral("son de l'enregistrement"));
    note(m_config.screenshotHotkey != m_saved.screenshotHotkey, QStringLiteral("raccourci des screenshots"));
    note(m_config.recordHotkey != m_saved.recordHotkey, QStringLiteral("raccourci de l'enregistrement"));
    note(m_config.theme != m_saved.theme, QStringLiteral("thème"));
    note(m_config.gameMixes != m_saved.gameMixes, QStringLiteral("volumes par jeu"));
    note(m_config.checkUpdates != m_saved.checkUpdates, QStringLiteral("mises à jour"));
    note(!sameMix(m_config.defaultMix, m_saved.defaultMix), QStringLiteral("volumes habituels"));
    note(m_config.closeToTray != m_saved.closeToTray, QStringLiteral("fermeture de la fenêtre")); note(m_config.startMenuShortcut != m_saved.startMenuShortcut, QStringLiteral("menu Démarrer"));
    note(m_config.launchAtLogin != m_saved.launchAtLogin || m_config.launchVisible != m_saved.launchVisible, QStringLiteral("lancement avec la session")); note(m_config.autoReplay != m_saved.autoReplay || m_config.replayGames != m_saved.replayGames, QStringLiteral("replay automatique"));
    if (!count && m_config.toJson() != m_saved.toJson()) { count = 1; groups.append(QStringLiteral("réglages")); }
    m_captureTag->setVisible(capture); m_qualityTag->setVisible(quality); m_replayTag->setVisible(replay);
    m_dirtyTitle->setText(count > 1 ? QStringLiteral("%1 modifications non enregistrées").arg(count) : QStringLiteral("1 modification non enregistrée"));
    static_cast<ElideLabel*>(m_dirtyDetail)->setFullText(QStringLiteral("· ") + groups.join(QStringLiteral(", "))); m_saveBar->setVisible(count > 0);
}
}
