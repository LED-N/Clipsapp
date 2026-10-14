#include "clip_editor.h"
#include "video_canvas.h"
#include "video_surface.h"
#include "clip_timeline.h"
#include "layout_keys.h"
#include "widgets.h"
#include "core/clip_library.h"
#include <QDir>
#include <QGuiApplication>
#include <QInputMethod>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QKeyEvent>
#include <QLineEdit>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QShortcut>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <cmath>
#include <limits>
namespace clipsapp {
namespace {
const double ratios[]{0, 16.0 / 9, 9.0 / 16, 1, 0};
const char* const ratioNames[]{"Entier", "16:9", "9:16", "1:1", "Libre"};
QVBoxLayout* section(QVBoxLayout* parent, const QString& title, const QString& iconName) {
    auto* layout = new QVBoxLayout; layout->setSpacing(10); auto* head = new QHBoxLayout; head->addWidget(caption(title)); head->addStretch();
    if (!iconName.isEmpty()) head->addWidget(new IconLabel(iconName, tk::text3));
    layout->addLayout(head); parent->addLayout(layout); return layout;
}
QString joined(const QStringList& names) { return names.size() > 1 ? names.mid(0, names.size() - 1).join(QStringLiteral(", ")) + QStringLiteral(" et ") + names.last() : names.join(QString()); }
}
ClipEditor::ClipEditor(QWidget* parent) : QWidget(parent), m_player(this), m_exporter(this) {
    m_analysisPool.setMaxThreadCount(1);
    setObjectName("clipEditor");
    setStyleSheet(QStringLiteral(R"(
        QFrame#timelineFrame { background: #0F1217; border: 1px solid #242A34; border-radius: 8px; }
        QFrame#zoomBar { background: #111419; border: none; border-top: 1px solid #242A34; border-bottom-left-radius: 7px; border-bottom-right-radius: 7px; }
        QWidget#inspector { background: #101318; }
        QFrame#exportBox { background: #14181E; border: 1px solid #2A313C; border-radius: 8px; }
    )"));
    auto* root = new QVBoxLayout(this); root->setContentsMargins(0, 0, 0, 0); root->setSpacing(0);
    root->addWidget(buildHeader()); root->addWidget(separator());
    auto* workspace = new QHBoxLayout; workspace->setSpacing(0);
    auto* center = new QVBoxLayout; m_center = center; center->setContentsMargins(16, 16, 16, 16); center->setSpacing(12);
    m_problem = new Banner(Banner::Warn, QStringLiteral("warn")); m_problem->setObjectName("editor-problem"); m_problem->hide(); center->addWidget(m_problem);
    m_banner = new Banner(Banner::Ok, QStringLiteral("ok")); m_banner->setObjectName("export-banner"); m_banner->hide(); center->addWidget(m_banner);
    auto* openExported = new Button(QStringLiteral("Ouvrir le nouveau clip"), Button::Secondary, Button::Small); openExported->setObjectName("open-exported");
    auto* trashOriginal = new Button(QStringLiteral("Mettre l'original à la corbeille…"), Button::Ghost, Button::Small); trashOriginal->setObjectName("trash-original");
    auto* closeBanner = new Button(QStringLiteral("x"), QString(), Button::Ghost, Button::Small); closeBanner->setAccessibleName(QStringLiteral("Fermer"));
    for (auto* button : {openExported, trashOriginal, closeBanner}) m_banner->actions()->addWidget(button);
    m_canvas = new VideoCanvas; center->addWidget(m_canvas, 1);
    m_mode = new Segmented; m_mode->setObjectName("crop-mode"); m_mode->setDark(true); m_mode->addOption(QStringLiteral("Cadrer"), QStringLiteral("crop")); m_mode->addOption(QStringLiteral("Résultat"), QStringLiteral("eye"));
    auto* overlay = new QWidget; auto* overlayLayout = new QHBoxLayout(overlay); overlayLayout->setContentsMargins(0, 0, 0, 0); overlayLayout->setSpacing(6); overlayLayout->addWidget(m_mode);
    m_fullScreen = new Button(QStringLiteral("expand"), QString(), Button::Secondary); m_fullScreen->setObjectName("editor-full-screen"); m_fullScreen->setFixedSize(34, 34);
    m_fullScreen->setToolTip(QStringLiteral("Voir le résultat en plein écran (F)")); m_fullScreen->setAccessibleName(QStringLiteral("Voir le résultat en plein écran")); overlayLayout->addWidget(m_fullScreen);
    m_canvas->setOverlay(overlay);
    m_canvas->setCropTool(true); m_canvas->setResultPreview(true);
    // The preview is made at the size it is drawn at: showing an image is then copying it, at the
    // pace of the clip. A view that is being resized asks many times; its last word is waited for.
    m_previewSize.setSingleShot(true); m_previewSize.setInterval(60);
    connect(m_canvas, &VideoCanvas::sizeWanted, this, [this] { m_previewSize.start(); });
    connect(&m_previewSize, &QTimer::timeout, this, [this] { m_player.setPresentation(previewBox(), true); });

    m_transport = new QWidget; m_transport->setFixedHeight(40); auto* transport = new QHBoxLayout(m_transport); transport->setContentsMargins(0, 0, 0, 0); transport->setSpacing(8);
    auto* toStart = new Button(QStringLiteral("start"), QString(), Button::Ghost); toStart->setToolTip(QStringLiteral("Aller au début de la sélection (Origine)")); toStart->setAccessibleName(toStart->toolTip());
    auto* previous = new Button(QStringLiteral("prev"), QString(), Button::Ghost); previous->setObjectName("frame-previous"); previous->setAccessibleName(QStringLiteral("Image précédente"));
    m_play = new Button(QStringLiteral("play"), QStringLiteral("Lire")); m_play->setObjectName("play-pause"); m_play->setEnabled(false); m_play->setFixedWidth(92);
    auto* next = new Button(QStringLiteral("next"), QString(), Button::Ghost); next->setObjectName("frame-next"); next->setAccessibleName(QStringLiteral("Image suivante"));
    m_time = monoLabel(QStringLiteral("00:00.000"), 14, tk::text, QFont::DemiBold); m_time->setObjectName("editor-time"); m_time->setContentsMargins(8, 0, 0, 0); m_total = monoLabel(QStringLiteral("/ 00:00.000"), 13, tk::text2);
    m_startHere = new Button(QStringLiteral("Début ici"), Button::Secondary, Button::Small); m_startHere->setObjectName("trim-start-here"); m_startHere->setKbd(QStringLiteral("I"));
    m_endHere = new Button(QStringLiteral("Fin ici"), Button::Secondary, Button::Small); m_endHere->setObjectName("trim-end-here"); m_endHere->setKbd(QStringLiteral("O"));
    m_selection = monoLabel(QStringLiteral("00:00.000"), 13, tk::accent, QFont::DemiBold); auto* divider = separator(true); divider->setFixedHeight(24);
    for (auto* widget : std::initializer_list<QWidget*>{toStart, previous, m_play, next, m_time, m_total}) transport->addWidget(widget);
    transport->addStretch(); transport->addWidget(m_startHere); transport->addWidget(m_endHere); transport->addSpacing(6); transport->addWidget(divider); transport->addSpacing(6);
    m_selectionCaption = label(QStringLiteral("Sélection"), "small"); transport->addWidget(m_selectionCaption); transport->addWidget(m_selection); center->addWidget(m_transport);

    auto* frame = new QFrame; frame->setObjectName("timelineFrame"); auto* frameLayout = new QVBoxLayout(frame); frameLayout->setContentsMargins(1, 1, 1, 1); frameLayout->setSpacing(0);
    m_timeline = new ClipTimeline; m_timeline->setEnabled(false); m_timeline->setFixedHeight(m_timeline->preferredHeight()); frameLayout->addWidget(m_timeline);
    auto* zoomBar = new QFrame; zoomBar->setObjectName("zoomBar"); zoomBar->setFixedHeight(40); m_zoomControls = zoomBar; auto* zoomRow = new QHBoxLayout(zoomBar); zoomRow->setContentsMargins(10, 0, 10, 0); zoomRow->setSpacing(6);
    auto* less = new Button(QStringLiteral("minus"), QString(), Button::Ghost, Button::Small); less->setAccessibleName(QStringLiteral("Dézoomer")); auto* more = new Button(QStringLiteral("plus"), QString(), Button::Ghost, Button::Small); more->setAccessibleName(QStringLiteral("Zoomer"));
    m_zoom = new QSlider(Qt::Horizontal); m_zoom->setObjectName("timeline-zoom"); m_zoom->setRange(0, 7); m_zoom->setFixedWidth(96);
    m_zoomLabel = monoLabel(QStringLiteral("1×"), 12, tk::text2); m_zoomLabel->setFixedWidth(34);
    auto* fit = new Button(QStringLiteral("Tout le clip"), Button::Secondary, Button::Small); fit->setObjectName("timeline-fit");
    m_pan = new QScrollBar(Qt::Horizontal); m_pan->setObjectName("timeline-pan");
    m_zoomHint = label(QStringLiteral("Ctrl + molette : zoom · Maj + molette : défiler"), "small3"); m_zoomHint->setFont(uiFont(11));
    zoomRow->addWidget(less); zoomRow->addWidget(m_zoom); zoomRow->addWidget(more); zoomRow->addWidget(m_zoomLabel); zoomRow->addWidget(fit); zoomRow->addSpacing(8); zoomRow->addWidget(m_pan, 1); zoomRow->addSpacing(8); zoomRow->addWidget(m_zoomHint);
    frameLayout->addWidget(zoomBar); center->addWidget(frame);
    workspace->addLayout(center, 1); workspace->addWidget(separator(true)); workspace->addWidget(buildInspector()); root->addLayout(workspace, 1);

    connect(less, &QPushButton::clicked, this, [this] { m_zoom->setValue(m_zoom->value() - 1); }); connect(more, &QPushButton::clicked, this, [this] { m_zoom->setValue(m_zoom->value() + 1); });
    connect(fit, &QPushButton::clicked, this, [this] { m_timeline->setZoom(1); });
    connect(m_zoom, &QSlider::valueChanged, this, [this](int value) { m_timeline->setZoom(std::pow(2.0, value)); });
    connect(m_pan, &QScrollBar::valueChanged, m_timeline, &ClipTimeline::setViewStart);
    connect(m_timeline, &ClipTimeline::viewChanged, this, [this] {
        QSignalBlocker zoomBlock(m_zoom), panBlock(m_pan);
        m_zoom->setValue(qRound(std::log2(m_timeline->zoomFactor()))); m_zoomLabel->setText(QString("%1×").arg(m_timeline->zoomFactor(), 0, 'f', 0));
        m_pan->setRange(0, int(std::min<qint64>(std::numeric_limits<int>::max(), std::max<qint64>(0, m_info.durationMs - m_timeline->visibleDuration()))));
        m_pan->setPageStep(int(m_timeline->visibleDuration())); m_pan->setSingleStep(std::max(1, int(m_timeline->visibleDuration() / 8))); m_pan->setValue(int(m_timeline->viewStart()));
    });
    connect(m_mode, &Segmented::currentChanged, this, [this](int index) { m_canvas->setCropTool(index == 0); m_canvas->setResultPreview(index == 1); });
    connect(m_back, &QPushButton::clicked, this, [this] { suspend(); emit backRequested(); });
    connect(&m_player, &ClipPlayer::changed, this, &ClipEditor::updatePlayer);
    connect(m_play, &QPushButton::clicked, this, [this] { m_player.setPlaying(!m_player.snapshot().playing); });
    connect(toStart, &QPushButton::clicked, this, [this] { m_player.seek(m_edit.startMs); });
    connect(previous, &QPushButton::clicked, this, [this] { stepFrames(-1); }); connect(next, &QPushButton::clicked, this, [this] { stepFrames(1); });
    const auto shortcut = [this](const QKeySequence& keys, auto&& action) { auto* result = new QShortcut(keys, this); result->setContext(Qt::WidgetWithChildrenShortcut); connect(result, &QShortcut::activated, this, action); };
    shortcut(Qt::Key_Space, [this] { if (m_play->isEnabled() && m_transport->isEnabled()) m_play->click(); });
    shortcut(Qt::Key_I, [this] { if (m_transport->isEnabled()) m_startHere->click(); }); shortcut(Qt::Key_O, [this] { if (m_transport->isEnabled()) m_endHere->click(); });
    // The two step keys are the ones around the left hand's forward key: A and E in AZERTY, Q and E in QWERTY.
    auto* stepBack = new QShortcut(this); auto* stepForward = new QShortcut(this);
    for (auto* step : {stepBack, stepForward}) { step->setContext(Qt::WidgetWithChildrenShortcut); connect(step, &QShortcut::activated, this, [this, step, stepBack] { stepFrames(step == stepBack ? -1 : 1); }); }
    const auto nameStepKeys = [stepBack, stepForward, previous, next] {
        const auto keys = stepKeys(); stepBack->setKey(keys.back); stepForward->setKey(keys.forward);
        previous->setToolTip(QStringLiteral("Image précédente (%1)").arg(keys.backLabel)); next->setToolTip(QStringLiteral("Image suivante (%1)").arg(keys.forwardLabel));
    };
    nameStepKeys(); connect(QGuiApplication::inputMethod(), &QInputMethod::localeChanged, this, nameStepKeys);
    shortcut(Qt::Key_Home, [this] { if (m_transport->isEnabled()) m_player.seek(m_edit.startMs); });
    shortcut(Qt::Key_F2, [this] { if (m_rename->isEnabled()) m_rename->click(); });
    shortcut(Qt::Key_F, [this] { if (m_fullScreen->isEnabled()) m_fullScreen->click(); });
    connect(m_fullScreen, &QPushButton::clicked, this, [this] { if (m_initialized && !m_exporter.running()) emit fullScreenRequested(); });
    connect(m_timeline, &ClipTimeline::seekRequested, &m_player, &ClipPlayer::seek);
    connect(m_timeline, &ClipTimeline::audioChanged, this, &ClipEditor::applyEdits);
    connect(m_timeline, &ClipTimeline::removeTrackRequested, this, &ClipEditor::removeTrack);
    connect(m_restoreTracks, &QPushButton::clicked, this, &ClipEditor::restoreTracks);
    connect(m_delete, &QPushButton::clicked, this, [this] { if (!m_exporter.running()) emit deleteRequested(m_path, false); });
    connect(m_discord, &QPushButton::clicked, this, [this] { if (m_initialized && !m_sharingBusy && !m_exporter.running()) emit discordRequested(m_info, m_edit); });
    connect(m_timeline, &ClipTimeline::trimChanged, this, [this](qint64 start, qint64 end) {
        m_updating = true; m_start->setRange(0, (end - 50) / 1000.0); m_end->setRange((start + 50) / 1000.0, m_info.durationMs / 1000.0);
        m_start->setValue(start / 1000.0); m_end->setValue(end / 1000.0); m_updating = false; applyEdits();
    });
    for (auto* field : {m_x, m_y, m_width, m_height}) connect(field, &QSpinBox::valueChanged, this, [this] { if (!m_updating) updateCrop({m_x->value(), m_y->value(), m_width->value(), m_height->value()}); });
    connect(m_canvas, &VideoCanvas::cropSelected, this, &ClipEditor::updateCrop);
    for (auto* field : {m_start, m_end}) connect(field, &QDoubleSpinBox::valueChanged, this, &ClipEditor::applyEdits);
    connect(m_startHere, &QPushButton::clicked, this, [this] { m_start->setValue(m_player.snapshot().positionMs / 1000.0); }); connect(m_endHere, &QPushButton::clicked, this, [this] { m_end->setValue(m_player.snapshot().positionMs / 1000.0); });
    connect(m_audioMode, &Segmented::currentChanged, this, [this] { applyEdits(); }); connect(m_export, &QPushButton::clicked, this, &ClipEditor::exportClip);
    connect(m_cancelExport, &QPushButton::clicked, this, [this] { m_exporter.cancel(); });
    connect(&m_exporter, &ClipExporter::progress, this, [this](int value) {
        m_progress->setValue(value / 100.0); m_exportLabel->setText(QStringLiteral("Export MP4 · %1 %").arg(value));
        m_exportEta->setText(value >= 5 && value < 100 ? QStringLiteral("encore ≈ %1 s").arg(std::max<qint64>(1, m_exportClock.elapsed() * (100 - value) / value / 1000)) : QString());
    });
    connect(&m_exporter, &ClipExporter::completed, this, [this](const QString& path) {
        m_baseline = m_exportEdit;
        if (m_exports.size() >= 128 && !m_exports.contains(m_exportSource)) m_exports.erase(m_exports.begin());
        m_exports.insert(m_exportSource, m_baseline); m_exportedPath = path;
        // Nothing is removed here: the banner only offers to open the copy or to send the original to the trash.
        const QFileInfo file(path); const bool beside = file.absolutePath() == QFileInfo(m_exportSource).absolutePath();
        m_banner->setTexts(QStringLiteral("Exporté : %1 · %2").arg(file.fileName(), formatBytes(file.size())),
            !beside ? QDir::toNativeSeparators(file.absolutePath()) : m_game.isEmpty() ? QStringLiteral("Rangé à côté de l'original.") : QStringLiteral("Rangé dans %1, à côté de l'original.").arg(m_game));
        m_banner->show(); updateActions();
        const auto original = m_exportSource; emit exported(path, original);
    });
    connect(&m_exporter, &ClipExporter::failed, this, [this](const QString& error) { updateActions(); emit failed(QStringLiteral("L'export a échoué"), error); });
    connect(&m_exporter, &ClipExporter::cancelled, this, [this] { updateActions(); });
    connect(openExported, &QPushButton::clicked, this, [this] { if (!m_exportedPath.isEmpty()) emit openRequested(m_exportedPath); });
    connect(trashOriginal, &QPushButton::clicked, this, [this] { if (!m_exporter.running() && !m_exportSource.isEmpty()) emit deleteRequested(m_exportSource, false); });
    connect(closeBanner, &QPushButton::clicked, m_banner, &QWidget::hide);
    updateActions();
}
ClipEditor::~ClipEditor() { disconnect(&m_player, nullptr, this, nullptr); disconnect(&m_exporter, nullptr, this, nullptr); stopAnalysis(); m_player.close(); }
QWidget* ClipEditor::buildHeader() {
    m_header = new QWidget; m_header->setFixedHeight(52); auto* layout = new QHBoxLayout(m_header); layout->setContentsMargins(16, 0, 16, 0); layout->setSpacing(10);
    m_back = new Button(QStringLiteral("back"), QStringLiteral("Clips"), Button::Ghost); m_back->setObjectName("editor-back"); m_back->setPadding(10); layout->addWidget(m_back);
    auto* divider = separator(true); divider->setFixedHeight(24); layout->addWidget(divider);
    m_title = new ElideLabel(QStringLiteral("Éditeur"), "clip", Qt::ElideMiddle); m_title->setObjectName("editor-title"); m_title->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed); m_title->setMaximumWidth(360); layout->addWidget(m_title);
    m_titleEdit = new QLineEdit; m_titleEdit->setObjectName("editor-rename"); m_titleEdit->setMaxLength(120); m_titleEdit->setFixedWidth(300); m_titleEdit->hide(); m_titleEdit->installEventFilter(this); layout->addWidget(m_titleEdit);
    m_rename = new Button(QStringLiteral("edit"), QString(), Button::Ghost, Button::Small); m_rename->setObjectName("editor-rename-button"); m_rename->setToolTip(QStringLiteral("Renommer le clip (F2)")); m_rename->setAccessibleName(QStringLiteral("Renommer le clip")); layout->addWidget(m_rename);
    m_gameTag = new Tag; m_gameTag->setIconName(QStringLiteral("tag")); m_gameTag->hide(); layout->addWidget(m_gameTag);
    m_stateTag = new Tag(QStringLiteral("Modifié"), Tag::Warn); m_stateTag->setObjectName("editor-state"); m_stateTag->hide(); layout->addWidget(m_stateTag);
    m_hint = new ElideLabel({}, "small"); m_hint->setObjectName("editor-hint"); layout->addWidget(m_hint, 1);
    auto* box = new QFrame; box->setObjectName("exportBox"); box->setFixedHeight(36); m_exportBox = box; auto* boxLayout = new QHBoxLayout(box); boxLayout->setContentsMargins(12, 0, 4, 0); boxLayout->setSpacing(12);
    auto* figures = new QVBoxLayout; figures->setSpacing(4); figures->setContentsMargins(0, 5, 0, 6); auto* line = new QHBoxLayout; m_exportLabel = label(QStringLiteral("Export MP4"), "strong"); m_exportLabel->setFont(uiFont(12, QFont::DemiBold)); m_exportEta = label({}, "small");
    line->addWidget(m_exportLabel); line->addStretch(); line->addWidget(m_exportEta); figures->addLayout(line); m_progress = new Bar; m_progress->setFixedWidth(220); figures->addWidget(m_progress); boxLayout->addLayout(figures);
    m_cancelExport = new Button(QStringLiteral("Annuler l'export"), Button::Secondary, Button::Small); m_cancelExport->setObjectName("cancel-export"); boxLayout->addWidget(m_cancelExport); box->hide(); layout->addWidget(box);
    m_discord = new Button(QStringLiteral("share"), QStringLiteral("Préparer pour Discord")); m_discord->setObjectName("compress-discord-editor"); m_discord->setEnabled(false); layout->addWidget(m_discord);
    m_export = new Button(QStringLiteral("save"), QStringLiteral("Exporter le clip"), Button::Primary); m_export->setObjectName("export-clip"); m_export->setEnabled(false); layout->addWidget(m_export);
    connect(m_rename, &QPushButton::clicked, this, [this] {
        if (!m_initialized || m_exporter.running() || m_sharingBusy) return;
        m_titleEdit->setText(QFileInfo(m_path).completeBaseName()); m_title->hide(); m_rename->hide(); m_titleEdit->show(); m_titleEdit->selectAll(); m_titleEdit->setFocus();
    });
    connect(m_titleEdit, &QLineEdit::returnPressed, this, [this] { finishRename(true); });
    return m_header;
}
QWidget* ClipEditor::buildInspector() {
    auto* scroll = new QScrollArea; scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame); scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); scroll->setFixedWidth(288); scroll->setStyleSheet(QStringLiteral("QScrollArea { background: #101318; }")); m_inspector = scroll;
    m_controls = new QWidget; m_controls->setObjectName("inspector"); m_controls->setEnabled(false); auto* layout = new QVBoxLayout(m_controls); layout->setContentsMargins(18, 16, 18, 16); layout->setSpacing(18);

    auto* trim = section(layout, QStringLiteral("Découpe"), QStringLiteral("cut")); m_sections.append(trim);
    m_start = new TimeField; m_end = new TimeField; m_start->setObjectName("trim-start"); m_end->setObjectName("trim-end");
    auto* trimGrid = new QHBoxLayout; trimGrid->setSpacing(10);
    for (auto* field : {m_start, m_end}) { auto* group = new QVBoxLayout; group->setSpacing(5); group->addWidget(label(field == m_start ? QStringLiteral("Début") : QStringLiteral("Fin"), "lbl")); group->addWidget(field); trimGrid->addLayout(group, 1); }
    trim->addLayout(trimGrid); m_kept = label({}, "hint"); m_kept->setTextFormat(Qt::RichText); trim->addWidget(m_kept); layout->addWidget(separator());

    auto* crop = section(layout, QStringLiteral("Cadrage"), QStringLiteral("crop")); m_sections.append(crop);
    auto* ratioRow = new QHBoxLayout; m_ratioRow = ratioRow; ratioRow->setSpacing(6);
    const QSizeF shapes[]{{26, 15}, {24, 14}, {11, 19}, {16, 16}, {22, 15}}; const Tile::Shape kinds[]{Tile::Filled, Tile::Outline, Tile::Outline, Tile::Outline, Tile::Dashed};
    for (int index = 0; index < 5; ++index) {
        auto* tile = new Tile(QString::fromLatin1(ratioNames[index])); tile->setObjectName(QStringLiteral("crop-ratio-%1").arg(index)); tile->setCentered(true); tile->setRaised(true); tile->setShape(shapes[index], kinds[index]); tile->setFixedTileHeight(58);
        tile->setChecked(index == 0); ratioRow->addWidget(tile); m_ratios.append(tile); connect(tile, &QPushButton::clicked, this, [this, index] { chooseRatio(index); });
    }
    crop->addLayout(ratioRow); crop->addWidget(label(QStringLiteral("Glissez le cadre dans l'aperçu pour le placer."), "hint", true));
    auto* cropActions = new QHBoxLayout; m_preciseToggle = new Button(QStringLiteral("next"), QStringLiteral("Valeurs précises"), Button::Ghost, Button::Small); m_preciseToggle->setObjectName("crop-precise"); m_preciseToggle->setPadding(6);
    auto* resetCrop = new Button(QStringLiteral("undo"), QStringLiteral("Image entière"), Button::Ghost, Button::Small); resetCrop->setObjectName("crop-reset"); resetCrop->setPadding(6); cropActions->addWidget(m_preciseToggle); cropActions->addStretch(); cropActions->addWidget(resetCrop); crop->addLayout(cropActions);
    m_precise = new QWidget; auto* cropGrid = new QGridLayout(m_precise); cropGrid->setContentsMargins(0, 0, 0, 0); cropGrid->setHorizontalSpacing(10); cropGrid->setVerticalSpacing(5);
    m_x = new QSpinBox; m_y = new QSpinBox; m_width = new QSpinBox; m_height = new QSpinBox;
    m_x->setObjectName("crop-x"); m_y->setObjectName("crop-y"); m_width->setObjectName("crop-width"); m_height->setObjectName("crop-height");
    const QList<QSpinBox*> fields{m_x, m_y, m_width, m_height}; const QStringList labels{"X", "Y", QStringLiteral("Largeur"), QStringLiteral("Hauteur")};
    for (int index = 0; index < 4; ++index) { fields[index]->setSingleStep(2); fields[index]->setButtonSymbols(QAbstractSpinBox::NoButtons); fields[index]->setFont(monoFont(13)); cropGrid->addWidget(label(labels[index], "lbl"), index / 2 * 2, index % 2); cropGrid->addWidget(fields[index], index / 2 * 2 + 1, index % 2); }
    m_precise->hide(); crop->addWidget(m_precise); layout->addWidget(separator());
    connect(m_preciseToggle, &QPushButton::clicked, this, [this] { m_precise->setVisible(!m_precise->isVisible()); m_preciseToggle->setIconName(m_precise->isVisible() ? QStringLiteral("down") : QStringLiteral("next")); });
    connect(resetCrop, &QPushButton::clicked, this, [this] { chooseRatio(0); });

    auto* sound = section(layout, QStringLiteral("Son exporté"), QStringLiteral("speaker")); m_sections.append(sound);
    m_audioMode = new Segmented; m_audioMode->setObjectName("export-audio-mode"); m_audioMode->addOption(QStringLiteral("Une piste mixée")); m_audioMode->addOption(QStringLiteral("Pistes séparées")); sound->addWidget(m_audioMode);
    m_audioSummary = label({}, "hint", true); m_audioSummary->setObjectName("export-audio-summary"); sound->addWidget(m_audioSummary);
    m_restoreTracks = new Button(QStringLiteral("undo"), QStringLiteral("Rétablir les pistes retirées"), Button::Ghost, Button::Small); m_restoreTracks->setObjectName("restore-audio-tracks"); m_restoreTracks->setPadding(6); m_restoreTracks->hide(); sound->addWidget(m_restoreTracks, 0, Qt::AlignLeft);
    m_keepMix = new Button(QStringLiteral("check"), QStringLiteral("Garder ces volumes par défaut"), Button::Ghost, Button::Small); m_keepMix->setObjectName("keep-mix"); m_keepMix->setPadding(6); m_keepMix->hide(); sound->addWidget(m_keepMix, 0, Qt::AlignLeft);
    m_keepMix->setToolTip(QStringLiteral("<qt>Les prochains clips s'ouvriront avec ces volumes, dans l'éditeur, en plein écran et dans les copies de partage. Les fichiers enregistrés ne sont pas modifiés.</qt>"));
    connect(m_keepMix, &QPushButton::clicked, this, [this] { if (m_initialized) emit mixChosen(chosenMix()); });
    layout->addWidget(separator());

    // In a small window the facts fold behind one line, so the trash button stays in view.
    auto* origin = new QVBoxLayout; origin->setSpacing(10); layout->addLayout(origin);
    m_originCaption = caption(QStringLiteral("Clip d'origine")); origin->addWidget(m_originCaption);
    m_detailsToggle = new Button(QStringLiteral("Détails du clip"), Button::Ghost, Button::Small); m_detailsToggle->setObjectName("clip-details"); m_detailsToggle->setTrailingIcon(QStringLiteral("down")); m_detailsToggle->setSpread(true); m_detailsToggle->setPadding(4); m_detailsToggle->hide(); origin->addWidget(m_detailsToggle);
    m_facts = new QWidget; auto* facts = new QGridLayout(m_facts); facts->setContentsMargins(0, 0, 0, 0); facts->setHorizontalSpacing(10); facts->setVerticalSpacing(6); facts->setColumnMinimumWidth(0, 86); facts->setColumnStretch(1, 1);
    connect(m_detailsToggle, &QPushButton::clicked, this, [this] { m_facts->setVisible(!m_facts->isVisible()); m_detailsToggle->setTrailingIcon(m_facts->isVisible() ? QStringLiteral("up") : QStringLiteral("down")); });
    const QStringList names{QStringLiteral("Image"), QStringLiteral("Durée"), QStringLiteral("Pistes"), QStringLiteral("Taille"), QStringLiteral("Fichier")};
    for (int index = 0; index < names.size(); ++index) {
        auto* name = label(names[index], "small"); facts->addWidget(name, index, 0);
        QLabel* value; if (index == 4) { m_fileName = new ElideLabel({}, nullptr, Qt::ElideMiddle); value = m_fileName; } else value = label({});
        value->setFont(index == 1 ? monoFont(12) : uiFont(12)); facts->addWidget(value, index, 1); m_details.append(value);
    }
    origin->addWidget(m_facts); layout->addStretch();
    m_delete = new Button(QStringLiteral("trash"), QStringLiteral("Mettre à la corbeille"), Button::Danger); m_delete->setObjectName("delete-editor-clip"); m_delete->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed); m_delete->setEnabled(false); layout->addWidget(m_delete);
    scroll->setWidget(m_controls); return scroll;
}
void ClipEditor::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    const bool compact = width() < 1100; if (compact == m_compact) return;
    m_compact = compact; m_inspector->setFixedWidth(compact ? 248 : 288); m_header->setFixedHeight(compact ? 48 : 52); m_zoomHint->setVisible(!compact); m_transport->setFixedHeight(compact ? 36 : 40);
    m_zoomControls->setFixedHeight(compact ? 34 : 40); m_export->setText(compact ? QStringLiteral("Exporter") : QStringLiteral("Exporter le clip")); m_audioMode->setOptionText(0, compact ? QStringLiteral("Mixé") : QStringLiteral("Une piste mixée")); m_audioMode->setOptionText(1, compact ? QStringLiteral("Séparé") : QStringLiteral("Pistes séparées"));
    m_startHere->setKbd(compact ? QString() : QStringLiteral("I")); m_endHere->setKbd(compact ? QString() : QStringLiteral("O"));
    // At the minimum window width every pixel of the transport row counts.
    m_selectionCaption->setVisible(!compact); m_play->setFixedWidth(compact ? 84 : 92); m_time->setFont(monoFont(compact ? 13 : 14, QFont::DemiBold)); m_total->setFont(monoFont(compact ? 12 : 13));
    m_controls->layout()->setContentsMargins(compact ? 14 : 18, compact ? 14 : 16, compact ? 14 : 18, compact ? 14 : 16); m_controls->layout()->setSpacing(compact ? 14 : 18);
    for (auto* part : m_sections) part->setSpacing(compact ? 8 : 10);
    m_originCaption->setVisible(!compact); m_detailsToggle->setVisible(compact); m_facts->setVisible(!compact); m_detailsToggle->setTrailingIcon(QStringLiteral("down"));
    m_ratioRow->setSpacing(compact ? 4 : 6); for (auto* tile : m_ratios) tile->setFixedTileHeight(compact ? 50 : 58); m_preciseToggle->setText(compact ? QStringLiteral("Valeurs") : QStringLiteral("Valeurs précises"));
    m_transport->layout()->setSpacing(compact ? 6 : 8); m_center->setContentsMargins(compact ? 12 : 16, compact ? 12 : 16, compact ? 12 : 16, compact ? 12 : 16); m_center->setSpacing(compact ? 10 : 12);
    m_timeline->setCompact(compact); m_volumes = m_timeline->volumes(); m_mutes = m_timeline->mutes(); m_timeline->setFixedHeight(m_timeline->preferredHeight());
}
bool ClipEditor::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_titleEdit && m_titleEdit->isVisible()) {
        if (event->type() == QEvent::KeyPress && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) { finishRename(false); return true; }
        if (event->type() == QEvent::FocusOut) finishRename(true);
    }
    return QWidget::eventFilter(watched, event);
}
void ClipEditor::finishRename(bool commit) {
    if (!m_titleEdit->isVisible()) return;
    const auto name = m_titleEdit->text().trimmed(); m_titleEdit->hide(); m_title->show(); m_rename->show();
    if (commit && !name.isEmpty() && name != QFileInfo(m_path).completeBaseName()) emit renameRequested(m_path, name);
}
void ClipEditor::setGame(const QString& game) { m_game = game; m_gameTag->setText(game); m_gameTag->setVisible(!game.isEmpty()); }
void ClipEditor::stopAnalysis() { if (m_analysisStop) *m_analysisStop = true; if (m_analysis) { delete m_analysis; m_analysis = nullptr; } }
void ClipEditor::beginAnalysis() {
    m_analysisTasks.erase(std::remove_if(m_analysisTasks.begin(), m_analysisTasks.end(), [](const auto& task) { return task.isFinished(); }), m_analysisTasks.end());
    const QFileInfo file(m_path); m_analysisKey = m_path + QString("|%1|%2").arg(file.size()).arg(file.lastModified().toMSecsSinceEpoch());
    QList<int> streams; for (const auto& track : m_edit.tracks) { streams.append(track.stream); m_analysisKey += QString("|a%1").arg(track.stream); }
    if (m_analysisCache.contains(m_analysisKey)) { m_timeline->setAnalysis(m_analysisCache.value(m_analysisKey)); return; }
    m_analysisStop = std::make_shared<std::atomic_bool>(false); const auto stop = m_analysisStop; const auto path = m_path;
    m_analysis = new QFutureWatcher<ClipAnalysis>(this); auto* worker = m_analysis;
    connect(worker, &QFutureWatcher<ClipAnalysis>::finished, this, [this, worker, stop] {
        if (!stop->load() && m_initialized && m_analysis == worker) {
            const auto analysis = worker->result(); m_timeline->setAnalysis(analysis);
            if (analysis.error.isEmpty()) { if (m_analysisCache.size() >= 4) m_analysisCache.erase(m_analysisCache.begin()); m_analysisCache.insert(m_analysisKey, analysis); }
            else { m_problem->setTexts(QStringLiteral("Aperçu de timeline incomplet"), analysis.error); m_problem->show(); }
        }
        if (m_analysis == worker) m_analysis = nullptr;
        worker->deleteLater();
    });
    const auto task = QtConcurrent::run(&m_analysisPool, [path, stop, streams] { if (*stop) return ClipAnalysis{}; return analyzeClip(path, *stop, streams); });
    m_analysisTasks.append(task); worker->setFuture(task);
}
void ClipEditor::openClip(const QString& path) {
    if (m_exporter.running()) return;
    stopAnalysis(); m_initialized = false; m_path = QFileInfo(path).absoluteFilePath(); m_controls->setEnabled(false); m_title->setFullText(QFileInfo(path).completeBaseName());
    m_banner->hide(); m_problem->hide(); m_exportedPath.clear(); finishRename(false); updateActions(); m_player.setPresentation(previewBox(), true);
    // Where the card can show the clip itself, the images go straight to their place in the view.
    auto* surface = m_canvas->prepareSurface();
    m_player.setFrameSink(surface ? [surface](const GpuFrame& frame) { surface->present(frame); } : std::function<void(const GpuFrame&)>());
    m_player.load(m_path);
}
// Until the view knows the clip, a size that suits most of them.
QSize ClipEditor::previewBox() const { const auto wanted = m_canvas->wantedSize(); return wanted.isEmpty() ? QSize(1280, 720) : wanted; }
void ClipEditor::suspend() { stopAnalysis(); m_player.close(); }
QList<QFuture<ClipAnalysis>> ClipEditor::pendingAnalyses() const {
    QList<QFuture<ClipAnalysis>> pending;
    for (const auto& task : m_analysisTasks) if (!task.isFinished()) pending.append(task);
    return pending;
}
void ClipEditor::forgetClip(const QString& path) {
    m_drafts.remove(path); m_exports.remove(path);
    const auto prefix = path + '|'; for (auto it = m_analysisCache.begin(); it != m_analysisCache.end();) if (it.key().startsWith(prefix)) it = m_analysisCache.erase(it); else ++it;
    if (path == m_exportSource) { m_exportSource.clear(); m_banner->hide(); }
    if (path == m_path) { suspend(); m_path.clear(); m_initialized = false; }
}
void ClipEditor::moveClip(const QString& from, const QString& to) {
    if (m_drafts.contains(from)) m_drafts.insert(to, m_drafts.take(from));
    if (m_exports.contains(from)) m_exports.insert(to, m_exports.take(from));
    if (m_exportSource == from) m_exportSource = to;
    if (m_path == from) { m_path = to; m_title->setFullText(QFileInfo(to).completeBaseName()); }
}
void ClipEditor::stepFrames(int frames) {
    const auto state = m_player.snapshot(); if (!state.loaded || !m_initialized || !m_transport->isEnabled()) return;
    if (state.playing) m_player.setPlaying(false);
    const double frame = 1000.0 / std::max(1.0, m_info.fps);
    m_player.seek(std::clamp<qint64>(qRound64(state.positionMs + frames * frame), m_edit.startMs, std::max(m_edit.startMs, m_edit.endMs - 1)));
}
void ClipEditor::updatePlayer() {
    const auto state = m_player.snapshot();
    if (state.loaded && !m_initialized) {
        m_updating = true; m_info = state.info; m_usualEdit = defaultEdit(m_info, m_mix); m_edit = m_drafts.value(m_path, m_usualEdit); if (!validateEdit(m_info, m_edit).isEmpty()) m_edit = m_usualEdit;
        m_originalEdit = defaultEdit(m_info); m_baseline = m_exports.value(m_path, m_originalEdit);
        m_start->setRange(0, m_info.durationMs / 1000.0 - 0.05); m_end->setRange(0.05, m_info.durationMs / 1000.0);
        m_start->setValue(m_edit.startMs / 1000.0); m_end->setValue(m_edit.endMs / 1000.0); m_audioMode->setCurrentIndex(m_edit.separateTracks ? 1 : 0);
        for (auto* field : {m_x, m_width}) field->setMaximum(m_info.size.width());
        for (auto* field : {m_y, m_height}) field->setMaximum(m_info.size.height());
        m_width->setMinimum(2); m_height->setMinimum(2);
        m_timeline->setClip(m_info, m_edit); m_volumes = m_timeline->volumes(); m_mutes = m_timeline->mutes();
        m_timeline->setFixedHeight(m_timeline->preferredHeight());
        const QFileInfo file(m_path);
        m_details[0]->setText(QStringLiteral("%1 × %2 · %3 FPS").arg(m_info.size.width()).arg(m_info.size.height()).arg(m_info.fps, 0, 'f', 0)); m_details[1]->setText(timeText(m_info.durationMs));
        m_details[3]->setText(formatBytes(file.size())); m_fileName->setFullText(file.fileName()); m_fileName->setToolTip(QDir::toNativeSeparators(m_path));
        m_ratio = m_edit.crop == QRect(QPoint(), m_info.size) ? 0 : 4; m_canvas->setAspect(0);
        m_updating = false; m_initialized = true; updateCrop(m_edit.crop); applyEdits(); beginAnalysis();
    }
    // The card stopped showing the clip: it is opened again, drawn from memory this time.
    if (state.direct && m_canvas->surface() && m_canvas->surface()->lost()) { VideoSurface::giveUp(); m_canvas->setDirect(false); m_initialized = false; const auto path = m_path; QMetaObject::invokeMethod(this, [this, path] { if (path == m_path) openClip(path); }, Qt::QueuedConnection); return; }
    m_canvas->setFrame(state.image, state.info.size); m_canvas->setDirect(state.direct);
    // While the clip plays the picture follows every image, and the rest of the page the pace of a
    // screen: the buttons, the time and the timeline have nothing to say two hundred times a second.
    const bool settled = state.playing && m_seenPlaying && state.loaded == m_seenLoaded && state.error.isEmpty() && state.warning.isEmpty();
    if (settled && m_refresh.isValid() && m_refresh.elapsed() < 15) return;
    m_refresh.start(); m_seenPlaying = state.playing; m_seenLoaded = state.loaded;
    m_play->setEnabled(state.loaded); updateActions();
    m_play->setText(state.playing ? QStringLiteral("Pause") : QStringLiteral("Lire")); m_play->setIconName(state.playing ? QStringLiteral("pause") : QStringLiteral("play"));
    m_timeline->setPosition(state.positionMs); m_time->setText(timeText(state.positionMs)); m_total->setText(QStringLiteral("/ ") + timeText(state.info.durationMs));
    if (state.playing && (state.positionMs < m_timeline->viewStart() || state.positionMs > m_timeline->viewStart() + m_timeline->visibleDuration())) m_timeline->setViewStart(state.positionMs - m_timeline->visibleDuration() / 8);
    if (!state.error.isEmpty()) { m_problem->setTexts(QStringLiteral("Lecture impossible"), state.error); m_problem->show(); }
    else if (!state.warning.isEmpty()) { m_problem->setTexts(state.warning, {}); m_problem->show(); }
}
void ClipEditor::chooseRatio(int index) {
    if (!m_initialized) { for (int tile = 0; tile < m_ratios.size(); ++tile) m_ratios[tile]->setChecked(tile == m_ratio); return; }
    m_ratio = index; m_canvas->setAspect(ratios[index]);
    if (index == 0) updateCrop({QPoint(), m_info.size});
    else if (index < 4) {
        const double ratio = ratios[index];
        int width = m_info.size.width(), height = qRound(width / ratio); if (height > m_info.size.height()) { height = m_info.size.height(); width = qRound(height * ratio); }
        // Keep the place the user already chose when the new shape still fits around it.
        const auto centre = m_edit.crop == QRect(QPoint(), m_info.size) ? QPoint(m_info.size.width() / 2, m_info.size.height() / 2) : m_edit.crop.center();
        updateCrop({std::clamp(centre.x() - width / 2, 0, m_info.size.width() - width), std::clamp(centre.y() - height / 2, 0, m_info.size.height() - height), width, height});
        m_mode->setCurrentIndex(0);
    } else { updateCrop(m_edit.crop); m_mode->setCurrentIndex(0); }
}
void ClipEditor::updateCrop(QRect crop) {
    if (!m_initialized) return;
    m_edit.crop = boundedCrop(crop, m_info.size); m_updating = true;
    m_x->setValue(m_edit.crop.x()); m_y->setValue(m_edit.crop.y()); m_width->setValue(m_edit.crop.width()); m_height->setValue(m_edit.crop.height()); m_updating = false;
    // The tiles follow the crop: the whole image, a kept ratio, or a free shape.
    const bool whole = m_edit.crop == QRect(QPoint(), m_info.size); const double shape = m_edit.crop.width() / double(std::max(1, m_edit.crop.height()));
    if (whole && m_ratio != 4) m_ratio = 0;
    else if (!whole && (m_ratio == 0 || (m_ratio < 4 && std::abs(shape - ratios[m_ratio]) > 0.02))) m_ratio = 4;
    m_canvas->setAspect(ratios[m_ratio]); m_canvas->setFreeFrame(m_ratio == 4);
    for (int tile = 0; tile < m_ratios.size(); ++tile) m_ratios[tile]->setChecked(tile == m_ratio);
    m_canvas->setCropLabel(QStringLiteral("%1 · %2 × %3").arg(QString::fromLatin1(ratioNames[m_ratio])).arg(m_edit.crop.width()).arg(m_edit.crop.height()));
    m_canvas->setCrop(m_edit.crop); applyEdits();
}
void ClipEditor::applyEdits() {
    if (m_updating || !m_initialized) return;
    m_edit.startMs = qRound64(m_start->value() * 1000); m_edit.endMs = qRound64(m_end->value() * 1000);
    for (int track = 0; track < m_edit.tracks.size(); ++track) { m_edit.tracks[track].volume = m_volumes[track]->value() / 100.0; m_edit.tracks[track].muted = m_mutes[track]->isChecked(); }
    m_edit.separateTracks = audioExportTrackCount(m_edit) > 1 && m_audioMode->currentIndex() == 1;
    m_updating = true; m_start->setMaximum(m_end->value() - 0.05); m_end->setMinimum(m_start->value() + 0.05); m_updating = false;
    m_selection->setText(timeText(m_edit.endMs - m_edit.startMs)); m_timeline->setEdit(m_edit);
    m_kept->setText(QStringLiteral("Gardé : <span style=\"font-family:'Cascadia Mono','Consolas','Noto Sans Mono',monospace; color:#E9EDF3\">%1</span> sur %2").arg(timeText(m_edit.endMs - m_edit.startMs), timeText(m_info.durationMs)));
    if (m_drafts.size() >= 128 && !m_drafts.contains(m_path)) m_drafts.erase(m_drafts.begin());
    m_drafts.insert(m_path, m_edit); m_player.setEdit(m_edit); updateActions();
}
void ClipEditor::updateSummary() {
    const bool loaded = m_initialized && m_player.snapshot().loaded; const int audioCount = loaded ? audioExportTrackCount(m_edit) : 0;
    QStringList kept, silent; for (const auto& track : m_edit.tracks) (track.muted ? silent : kept).append(track.name);
    QString text = audioCount == 0 ? QStringLiteral("Export sans audio") : audioCount == 1 ? QStringLiteral("Une piste audio conservée : %1.").arg(joined(kept))
        : m_edit.separateTracks ? QStringLiteral("%1, chacune sur sa piste.").arg(joined(kept)) : QStringLiteral("%1, mélangés en une piste.").arg(joined(kept));
    if (loaded && !silent.isEmpty()) text += silent.size() > 1 ? QStringLiteral(" %1 sont muets et ne seront pas exportés.").arg(joined(silent)) : QStringLiteral(" %1 est muet et ne sera pas exporté.").arg(silent.first());
    m_audioSummary->setText(text); m_audioMode->setVisible(loaded && audioCount > 1);
    QStringList removed; for (const auto& audio : m_info.audio) { bool present = false; for (const auto& track : m_edit.tracks) present |= track.stream == audio.stream; if (!present) removed.append(audio.name); }
    m_restoreTracks->setVisible(loaded && !removed.isEmpty());
    // Offered once the levels of this clip are not the usual ones. A clip mixed by an export says nothing of the habit.
    m_keepMix->setVisible(loaded && !m_info.mixed && !m_edit.tracks.isEmpty() && !sameMix(chosenMix(), m_mix));
    m_restoreTracks->setText(removed.size() == 1 ? QStringLiteral("Rétablir la piste « %1 »").arg(removed.first()) : QStringLiteral("Rétablir les %1 pistes retirées").arg(removed.size()));
    if (loaded) m_details[2]->setText(removed.isEmpty() ? QString::number(m_info.audio.size()) : QStringLiteral("%1, dont %2 retirée%3").arg(m_info.audio.size()).arg(removed.size()).arg(removed.size() > 1 ? "s" : ""));
}
void ClipEditor::updateActions() {
    const bool loaded = m_initialized && m_player.snapshot().loaded;
    const bool running = m_exporter.running(); const bool editable = loaded && !running;
    m_controls->setEnabled(editable); m_timeline->setEnabled(editable); m_zoomControls->setEnabled(editable); m_canvas->setEnabled(editable); m_mode->setEnabled(editable); m_fullScreen->setEnabled(editable); m_transport->setEnabled(editable);
    m_back->setEnabled(!running); m_rename->setEnabled(editable && !m_sharingBusy); m_delete->setEnabled(editable && !m_sharingBusy);
    m_canvas->setNotice(running ? QStringLiteral("Édition verrouillée pendant l'export") : QString());
    m_discord->setEnabled(editable && !m_sharingBusy); m_discord->setText(m_sharingBusy ? QStringLiteral("Partage en cours…") : QStringLiteral("Préparer pour Discord"));
    updateSummary();
    const bool changed = hasExportChanges(), valid = loaded && validateEdit(m_info, m_edit).isEmpty();
    m_export->setEnabled(editable && !m_sharingBusy && changed && valid); m_export->setVisible(!running); m_discord->setVisible(!running); m_exportBox->setVisible(running);
    // A disabled export always says why, next to the button rather than in a tooltip.
    const QString reason = !loaded ? (m_path.isEmpty() ? QStringLiteral("Ouvrez un clip depuis la bibliothèque.") : QStringLiteral("Chargement du clip…")) : m_sharingBusy ? QStringLiteral("Attendez la fin du partage Discord pour exporter.")
        : !changed ? QStringLiteral("Rien à exporter : coupez, recadrez ou réglez une piste.") : !valid ? validateEdit(m_info, m_edit) : QString();
    // A clip that only carries the usual levels can be exported, but nobody modified it yet.
    const bool touched = changed && !sameClipEdits(m_edit, m_usualEdit);
    m_stateTag->setVisible(running || (loaded && touched));
    if (running) m_stateTag->set(QStringLiteral("Export en cours"), Tag::Info); else m_stateTag->set(QStringLiteral("Modifié"), Tag::Warn);
    auto* hint = static_cast<ElideLabel*>(m_hint);
    hint->setAlignment((reason.isEmpty() || running ? Qt::AlignLeft : Qt::AlignRight) | Qt::AlignVCenter);
    hint->setFullText(running ? QString() : !reason.isEmpty() ? reason : touched ? QStringLiteral("Exportez pour garder vos changements après la fermeture.") : QStringLiteral("Vos volumes habituels sont appliqués. Exportez pour les garder dans un fichier."));
    m_export->setToolTip(reason);
}
bool ClipEditor::hasExportChanges() const {
    return m_initialized && !sameClipEdits(m_edit, m_originalEdit) && !sameClipEdits(m_edit, m_baseline);
}
void ClipEditor::setSharingBusy(bool busy) { m_sharingBusy = busy; updateActions(); }
// The clip being edited keeps the levels it shows: the new mix is where the next ones start.
void ClipEditor::setDefaultMix(const TrackMix& mix) { m_mix = mix; if (m_initialized) m_usualEdit = defaultEdit(m_info, m_mix); updateActions(); }
TrackMix ClipEditor::chosenMix() const {
    auto mix = m_mix; const auto levels = editLevels(m_edit);
    for (auto track = levels.cbegin(); track != levels.cend(); ++track) mix.insert(track.key(), track.value());
    return mix;
}
void ClipEditor::noteHighQualityExport(const QString& original, const ClipEdit& edit) {
    if (m_exports.size() >= 128 && !m_exports.contains(original)) m_exports.erase(m_exports.begin());
    m_exports.insert(original, edit); if (m_path == original) { m_baseline = edit; updateActions(); }
}
void ClipEditor::removeTrack(int stream) {
    if (!m_initialized || m_exporter.running()) return;
    for (int i = 0; i < m_edit.tracks.size(); ++i) if (m_edit.tracks[i].stream == stream) { m_edit.tracks.removeAt(i); break; }
    m_timeline->setEdit(m_edit); m_volumes = m_timeline->volumes(); m_mutes = m_timeline->mutes();
    m_timeline->setFixedHeight(m_timeline->preferredHeight());
    applyEdits(); stopAnalysis(); beginAnalysis();
}
void ClipEditor::restoreTracks() {
    auto restored = defaultEdit(m_info, m_mix).tracks;
    for (auto& track : restored) for (const auto& kept : m_edit.tracks) if (kept.stream == track.stream) track = kept;
    m_edit.tracks = std::move(restored); m_timeline->setEdit(m_edit); m_volumes = m_timeline->volumes(); m_mutes = m_timeline->mutes();
    m_timeline->setFixedHeight(m_timeline->preferredHeight());
    applyEdits(); stopAnalysis(); beginAnalysis();
}
void ClipEditor::exportClip() {
    if (m_exporter.running()) return;
    const QFileInfo original(m_path); const auto path = QFileDialog::getSaveFileName(this, QStringLiteral("Exporter le clip modifié"), original.dir().filePath(original.completeBaseName() + QStringLiteral(" (édité).mp4")), QStringLiteral("Vidéo MP4 (*.mp4)"), nullptr, QFileDialog::DontConfirmOverwrite); if (path.isEmpty()) return;
    QString error; if (!exportTo(path.endsWith(".mp4", Qt::CaseInsensitive) ? path : path + ".mp4", error)) emit failed(QStringLiteral("Export impossible"), error);
}
bool ClipEditor::exportTo(const QString& path, QString& error) {
    if (m_sharingBusy) { error = QStringLiteral("Attendez la fin du partage Discord avant un autre export."); return false; }
    if (!hasExportChanges() || !m_player.snapshot().loaded) { error = QStringLiteral("Modifiez le clip avant de l'exporter."); return false; }
    if (!m_exporter.start(m_info, m_edit, path, error)) return false;
    m_exportSource = m_path; m_exportEdit = m_edit; m_exportClock.restart(); m_banner->hide();
    m_progress->setValue(0); m_exportLabel->setText(QStringLiteral("Export MP4 · 0 %")); m_exportEta->clear();
    updateActions(); return true;
}
}
