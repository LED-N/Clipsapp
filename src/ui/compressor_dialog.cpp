#include "compressor_dialog.h"
#include "delete_prompt.h"
#include "clip_list.h"
#include "compare_view.h"
#include "thumbnail_cache.h"
#include "core/clip_library.h"
#include "media/clip_reader.h"
#include <QApplication>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollArea>
#include <QTemporaryDir>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>

namespace clipsapp {
namespace {
struct Limit { int megabytes; const char* name; };
const Limit limits[]{{20, "Sans Nitro"}, {50, "Nitro Basic, Classic"}, {50, "Serveur niveau 2"}, {100, "Serveur niveau 3"}, {1000, "Nitro"}};
const char* const codecs[]{"h264", "hevc", "av1"};
const char* const levelHints[]{"Aucune différence visible : le meilleur choix pour garder vos clips.", "Une légère perte dans les scènes très chargées, pour bien plus de place.", "Le plus léger : la perte se voit de près."};
QString firstLine(const QString& text) { return text.section('\n', 0, 0).trimmed(); }
QString roughDuration(qint64 ms) { const auto seconds = (ms + 500) / 1000; return seconds >= 90 ? QStringLiteral("%1 min").arg((seconds + 30) / 60) : QStringLiteral("%1 s").arg(seconds); }
QString remaining(qint64 seconds) { seconds = std::max<qint64>(1, seconds); return seconds >= 3600 ? QStringLiteral("%1 h %2 min").arg(seconds / 3600).arg(seconds / 60 % 60, 2, 10, QLatin1Char('0')) : seconds >= 60 ? QStringLiteral("%1 min %2 s").arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0')) : QStringLiteral("%1 s").arg(seconds); }
QString gain(qint64 before, qint64 after) { return before > 0 ? QStringLiteral("−%1 %").arg(qRound(100.0 * (before - after) / before)) : QString(); }
// A line between the rows of a list, in the colors of the theme.
QFrame* rowLine() { auto* line = separator(); setThemedStyleSheet(line, QStringLiteral("background: {lineSoft};")); return line; }
QScrollArea* listWell(QWidget* rows, QWidget*& well) {
    well = card("well"); auto* layout = new QVBoxLayout(well); layout->setContentsMargins(1, 1, 1, 1);
    auto* scroll = new QScrollArea; scroll->setWidgetResizable(true); scroll->setWidget(rows); scroll->setFrameShape(QFrame::NoFrame); scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); layout->addWidget(scroll);
    return scroll;
}
class ThumbView final : public QWidget {
public:
    ThumbView(ThumbnailCache* cache, const QString& path) : m_cache(cache), m_path(path), m_key(ThumbnailCache::keyFor(path)) { setFixedSize(48, 27); connect(cache, &ThumbnailCache::ready, this, qOverload<>(&QWidget::update)); }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing); QPainterPath rounded; rounded.addRoundedRect(rect(), 4, 4); painter.setClipPath(rounded); painter.fillRect(rect(), tk::raised);
        const auto* preview = m_cache->preview(m_key); const auto* meta = m_cache->meta(m_key);
        if (!preview) m_cache->request(m_key, m_path);
        if (preview && !preview->image.isNull()) { painter.setRenderHint(QPainter::SmoothPixmapTransform); painter.drawImage(rect(), preview->image); }
    }
private:
    ThumbnailCache* m_cache; QString m_path, m_key;
};
// A copy for Discord, under way or ready: once ready, the row itself is dragged into a conversation.
class JobRow final : public QWidget {
public:
    JobRow(DiscordBatch* batch, std::function<void(const QString&)> copy) : m_batch(batch) {
        setMinimumHeight(64); setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed); auto* layout = new QHBoxLayout(this); layout->setContentsMargins(14, 12, 14, 12); layout->setSpacing(12);
        m_mark = new IconBox(QStringLiteral("share"), tk::info, tk::infoSoft, 28); m_mark->setRound(true); layout->addWidget(m_mark);
        auto* texts = new QVBoxLayout; texts->setSpacing(4); auto* head = new QHBoxLayout; m_name = new ElideLabel({}, "strong"); head->addWidget(m_name, 1); m_percent = monoLabel({}, 12, tk::text2); head->addWidget(m_percent);
        texts->addLayout(head); m_bar = new Bar; texts->addWidget(m_bar); m_status = new ElideLabel({}, "small"); texts->addWidget(m_status); layout->addLayout(texts, 1);
        m_dragHint = new QWidget; auto* hint = new QHBoxLayout(m_dragHint); hint->setContentsMargins(0, 0, 0, 0); hint->setSpacing(4); hint->addWidget(new IconLabel(QStringLiteral("grip"), tk::text3, 14)); hint->addWidget(label(QStringLiteral("Glisser"), "small3")); layout->addWidget(m_dragHint);
        m_copy = new Button(QStringLiteral("copy"), QStringLiteral("Copier"), Button::Secondary, Button::Small); layout->addWidget(m_copy);
        m_retry = new Button(QStringLiteral("refresh"), QStringLiteral("Réessayer"), Button::Secondary, Button::Small); layout->addWidget(m_retry);
        connect(m_copy, &QPushButton::clicked, this, [this, copy] { if (!m_output.isEmpty()) copy(m_output); });
        connect(m_retry, &QPushButton::clicked, this, [this] { m_batch->retry(m_index); });
    }
    void apply(int index, const ShareJob& job, int left) {
        m_index = index; m_output = job.state == ShareJob::Ready ? job.output : QString(); m_name->setFullText(job.name);
        const bool running = job.state == ShareJob::Running, ready = job.state == ShareJob::Ready, failed = job.state == ShareJob::Failed;
        m_bar->setVisible(running); m_percent->setVisible(running); m_copy->setVisible(ready); m_dragHint->setVisible(ready); m_retry->setVisible(failed); setCursor(ready ? Qt::OpenHandCursor : Qt::ArrowCursor);
        if (ready) m_mark->set(QStringLiteral("check"), tk::accent, tk::accentSoft); else if (failed) m_mark->set(QStringLiteral("x"), tk::danger, tk::dangerSoft); else m_mark->set(QStringLiteral("share"), running ? tk::info : tk::text3, running ? tk::infoSoft : tk::raised);
        setStyleProperty(m_status, "role", failed ? "danger" : "small");
        if (running) {
            m_bar->setValue(job.progress / 100.0); m_percent->setText(QStringLiteral("%1 %").arg(job.progress)); QStringList parts{job.stage}; if (!job.detail.isEmpty()) parts << job.detail; if (left >= 0) parts << QStringLiteral("encore ≈ %1").arg(remaining(left));
            m_status->setFullText(parts.join(QStringLiteral(" · ")));
        } else if (ready) m_status->setFullText(QStringLiteral("Prête · %1 · %2").arg(formatBytes(job.bytes), job.copied ? QStringLiteral("copiée telle quelle") : job.detail.isEmpty() ? QStringLiteral("réencodée") : job.detail));
        else if (failed) m_status->setFullText(QStringLiteral("Échec : ") + firstLine(job.error));
        else m_status->setFullText(job.state == ShareJob::Cancelled ? QStringLiteral("Annulée") : QStringLiteral("En attente"));
    }
    QString output() const { return m_output; }
protected:
    void mousePressEvent(QMouseEvent* event) override { m_press = event->position().toPoint(); }
    void mouseMoveEvent(QMouseEvent* event) override {
        if (m_output.isEmpty() || !(event->buttons() & Qt::LeftButton) || (event->position().toPoint() - m_press).manhattanLength() < QApplication::startDragDistance()) return;
        startFileDrag(this, existingFileUrls({m_output}));
    }
private:
    DiscordBatch* m_batch; int m_index = -1; QString m_output; QPoint m_press;
    IconBox* m_mark; ElideLabel* m_name; ElideLabel* m_status; QLabel* m_percent; Bar* m_bar; QWidget* m_dragHint; Button* m_copy; Button* m_retry;
};
// Every ready copy at once, dragged from one handle.
class DragAll final : public QWidget {
public:
    explicit DragAll(std::function<QStringList()> files) : m_files(std::move(files)) { setCursor(Qt::OpenHandCursor); setAttribute(Qt::WA_Hover); setFixedHeight(32); }
    void setCount(int count) { m_count = count; updateGeometry(); update(); }
    QSize sizeHint() const override { return {QFontMetrics(uiFont(12, QFont::DemiBold)).horizontalAdvance(text()) + 16 + 8 + 24, 32}; }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing); const bool over = underMouse();
        QPen pen(over ? tk::accent : tk::lineHover, 1, Qt::DashLine); painter.setPen(pen); painter.setBrush(over ? tk::accentSoft : QColor(Qt::transparent)); painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 6, 6);
        paintIcon(painter, QStringLiteral("grip"), QRectF(12, (height() - 16) / 2.0, 16, 16), over ? tk::accent : tk::text2);
        painter.setFont(uiFont(12, QFont::DemiBold)); painter.setPen(over ? tk::accent : tk::text); painter.drawText(QRectF(36, 0, width() - 44, height()), Qt::AlignVCenter | Qt::AlignLeft, text());
    }
    void mousePressEvent(QMouseEvent* event) override { m_press = event->position().toPoint(); }
    void mouseMoveEvent(QMouseEvent* event) override {
        if (!(event->buttons() & Qt::LeftButton) || (event->position().toPoint() - m_press).manhattanLength() < QApplication::startDragDistance()) return;
        startFileDrag(this, existingFileUrls(m_files()));
    }
private:
    QString text() const { return QStringLiteral("Glisser les %1 fichiers").arg(m_count); }
    std::function<QStringList()> m_files; int m_count = 0; QPoint m_press;
};
}
CompressorDialog::CompressorDialog(DiscordBatch* discord, CompressBatch* space, ThumbnailCache* cache, const Setup& setup, const ShareOptions& shareDefaults, const CompressSettings& spaceDefaults, QWidget* parent)
    : ModalDialog(parent, 640), m_discord(discord), m_space(space), m_cache(cache), m_setup(setup), m_shareDefaults(shareDefaults), m_spaceDefaults(spaceDefaults) {
    setObjectName("compressor-dialog"); m_workMark = new IconBox(QStringLiteral("compress"), tk::accent, tk::accentSoft); setHeading(QStringLiteral("Compresser"), {}, m_workMark, false);
    m_overall = new Bar; m_overall->setObjectName("compressor-overall"); m_overall->hide(); headerLayout()->addWidget(m_overall);
    // One face at a time; the hidden ones take no room, so the dialog is always the size of the one shown.
    auto* stages = new QWidget; m_stages = new QVBoxLayout(stages); m_stages->setContentsMargins(0, 0, 0, 0); m_stages->setSpacing(0); body()->addWidget(stages);
    buildOptions(); buildDiscordWork(); buildSpaceWork(); buildCompare(); buildBatch();
    m_dragAll = new DragAll([this] { QStringList files; for (auto* row : m_jobRows) if (const auto output = static_cast<JobRow*>(row)->output(); !output.isEmpty()) files << output; return files; });
    m_dragAll->setObjectName("compressor-drag-all"); m_dragAll->hide(); footerLeft()->addWidget(m_dragAll);
    m_cancel = new Button(QStringLiteral("Annuler")); m_cancel->setObjectName("compressor-cancel");
    m_start = new Button(QStringLiteral("compress"), QStringLiteral("Compresser"), Button::Primary); m_start->setObjectName("compressor-start"); m_start->setDefault(true);
    m_stop = new Button(QStringLiteral("stop"), QStringLiteral("Arrêter"), Button::Ghost); m_stop->setObjectName("compressor-stop"); m_stop->setTextColor(tk::danger);
    m_background = new Button(QStringLiteral("Continuer en arrière-plan")); m_background->setObjectName("compressor-background");
    m_close = new Button(QStringLiteral("Fermer"), Button::Primary); m_close->setObjectName("compressor-close");
    m_details = new Button(QStringLiteral("info"), QStringLiteral("Détails"), Button::Ghost); m_details->setObjectName("compressor-details"); m_details->hide(); footerLeft()->addWidget(m_details);
    connect(m_details, &QPushButton::clicked, this, [this] { showCompressionReport(this, m_space->jobs(), m_space->options().permanent); });
    for (auto* button : {m_stop, m_cancel, m_background, m_close, m_start}) footer()->addWidget(button);
    connect(m_cancel, &QPushButton::clicked, this, &CompressorDialog::reject);
    connect(m_start, &QPushButton::clicked, this, [this] { begin(); });
    connect(m_stop, &QPushButton::clicked, m_space, &CompressBatch::cancel);
    connect(m_background, &QPushButton::clicked, this, [this] { QDialog::reject(); });
    connect(m_close, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_discord, &DiscordBatch::changed, this, [this] { if (m_stage == DiscordWork) refreshDiscord(); });
    connect(m_discord, &DiscordBatch::runningChanged, this, [this] { if (m_stage == DiscordWork) refreshDiscord(); });
    connect(m_space, &CompressBatch::changed, this, [this] { if (m_stage == SpaceWork || m_stage == Compare) refreshSpace(); else if (m_stage == SpaceBatch) refreshBatch(); });
    connect(&m_probe, &QFutureWatcher<QList<DiscordPrepared>>::finished, this, [this] { m_infos = m_probe.result(); updateEstimates(); updateAgain(); });
    // Opened on a library that compresses in the background, or did: it shows where it is, or what it gave.
    if (m_setup.paths.isEmpty() && !m_space->jobs().isEmpty() && !m_space->options().review) { m_goal = Space; showStage(SpaceBatch); return; }
    showStage(Options);
    if (m_setup.editedInfo && m_setup.edited) { m_infos = {{*m_setup.editedInfo, {}}}; updateEstimates(); updateAgain(); }
    else { const auto sources = m_setup.library ? QStringList() : m_setup.paths; m_probe.setFuture(QtConcurrent::run([sources] {
        QList<DiscordPrepared> infos;
        for (const auto& path : sources) { DiscordPrepared result; try { ClipReader reader(path, false, true); result.info = reader.info(); } catch (const std::exception& error) { result.error = QString::fromUtf8(error.what()); } infos.append(result); }
        return infos;
    })); updateEstimates(); }
    chooseGoal(m_setup.library ? Space : m_setup.goal);
}
CompressorDialog::~CompressorDialog() {
    disconnect(&m_probe, nullptr, this, nullptr); m_probe.waitForFinished();
    if (m_compare) m_compare->close();
    // A copy for Discord does not outlive its dialog: what is still being written goes with it once stopped.
    if (m_temporary) {
        if (m_discord && m_discord->running()) { m_discord->cancel(); auto temporary = m_temporary; DiscordBatch* batch = m_discord; auto connection = std::make_shared<QMetaObject::Connection>();
            *connection = connect(batch, &DiscordBatch::runningChanged, batch, [temporary, connection](bool running) mutable { if (running) return; temporary.reset(); QObject::disconnect(*connection); }); }
        m_temporary.reset();
    }
    // A comparison left without an answer keeps the original.
    if (m_space) for (int index = 0; index < m_space->jobs().size(); ++index) if (m_space->jobs()[index].state == CompressJob::Review) m_space->discard(index);
}
void CompressorDialog::buildOptions() {
    auto* page = new QWidget; auto* layout = new QVBoxLayout(page); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(16);
    auto* goals = new QHBoxLayout; goals->setSpacing(10);
    m_spaceTile = new Tile(QStringLiteral("Gagner de la place"), QStringLiteral("La qualité est préservée,\nl'original est remplacé")); m_spaceTile->setIconName(QStringLiteral("compress")); m_spaceTile->setObjectName("compressor-goal-space");
    m_discordTile = new Tile(QStringLiteral("Envoyer sur Discord"), QStringLiteral("À la taille de votre limite,\nun fichier à glisser")); m_discordTile->setIconName(QStringLiteral("share")); m_discordTile->setObjectName("compressor-goal-discord");
    for (auto* tile : {m_spaceTile, m_discordTile}) { tile->setRaised(true); goals->addWidget(tile, 1); }
    if (m_setup.library) { m_discordTile->setEnabled(false); m_discordTile->setToolTip(QStringLiteral("Pour toute la bibliothèque, seul le gain de place est proposé.")); }
    layout->addLayout(goals);
    // Said before anything starts: the capture may give up images for as long as the compression lasts.
    if (m_setup.capturing) {
        auto* busy = new Banner(Banner::Warn, QStringLiteral("warn")); busy->setObjectName("compressor-capture-warning");
        busy->setTexts(QStringLiteral("Un replay ou un enregistrement est en cours"), QStringLiteral("La compression utilise aussi la carte graphique et le processeur : le temps qu'elle dure, la capture peut perdre des images ou saccader. Mieux vaut compresser une fois la partie finie."));
        layout->addWidget(busy);
    }
    connect(m_spaceTile, &QPushButton::clicked, this, [this] { chooseGoal(Space); }); connect(m_discordTile, &QPushButton::clicked, this, [this] { chooseGoal(Discord); });
    m_goalHint = new QWidget; auto* hint = new QHBoxLayout(m_goalHint); hint->setContentsMargins(0, 4, 0, 4); hint->setSpacing(8);
    hint->addWidget(new IconLabel(QStringLiteral("info"), tk::text2, 14), 0, Qt::AlignTop); hint->addWidget(label(QStringLiteral("Choisissez ce que vous voulez en faire : les réglages suivent."), "hint", true), 1);
    m_goalPages = {m_goalHint, buildSpaceOptions(), buildDiscordOptions()};
    for (auto* goalPage : m_goalPages) { goalPage->hide(); layout->addWidget(goalPage); }
    page->hide(); m_stages->addWidget(page);
}
QWidget* CompressorDialog::buildSpaceOptions() {
    auto* page = new QWidget; auto* layout = new QVBoxLayout(page); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(16);
    const int count = int(m_setup.paths.size()); const bool several = count > 1 || m_setup.library;
    auto* quality = new QVBoxLayout; quality->setSpacing(8); quality->addWidget(caption(QStringLiteral("Qualité")));
    m_level = new Segmented; m_level->setObjectName("compress-level"); for (const auto& name : {QStringLiteral("Fidèle"), QStringLiteral("Équilibrée"), QStringLiteral("Compacte")}) m_level->addOption(name);
    m_level->setCurrentIndex(std::clamp(m_spaceDefaults.level, 0, 2)); quality->addWidget(m_level);
    m_levelHint = label(QString::fromUtf8(levelHints[m_level->currentIndex()]), "hint", true); quality->addWidget(m_levelHint); layout->addLayout(quality);
    connect(m_level, &Segmented::currentChanged, this, [this](int index) { m_levelHint->setText(QString::fromUtf8(levelHints[index])); });
    auto* engine = new QVBoxLayout; engine->setSpacing(4); engine->addWidget(caption(QStringLiteral("Moteur"))); engine->addSpacing(4);
    auto* engineHost = new QWidget; auto* engines = new QVBoxLayout(engineHost); engines->setContentsMargins(0, 0, 0, 0); engines->setSpacing(4);
    const QStringList names{QStringLiteral("Carte graphique"), QStringLiteral("Processeur")}, details{QStringLiteral("HEVC, rapide, la machine reste disponible"), QStringLiteral("AV1, plus compact, bien plus lent")};
    for (int index = 0; index < 2; ++index) { auto* row = new RadioRow(names[index], details[index], engineHost); row->setObjectName(index ? "compress-engine-processor" : "compress-engine-gpu"); row->setChecked(m_spaceDefaults.processor == (index == 1)); engines->addWidget(row); m_engines.append(row); }
    engine->addWidget(engineHost);
    engine->addSpacing(4); engine->addWidget(label(QStringLiteral("Le HEVC et l'AV1 gagnent le plus de place, mais Discord ne les lit pas sur mobile ni sans accélération matérielle : pour envoyer un clip, « Compresser pour Discord » en fait une copie en H.264."), "hint", true));
    layout->addLayout(engine);
    // What is compressed: the clips and their weight; a whole library is only counted.
    auto* what = new QVBoxLayout; what->setSpacing(8); qint64 total = 0; for (const auto& path : m_setup.paths) total += QFileInfo(path).size();
    what->addWidget(caption(m_setup.library ? QStringLiteral("Toute la bibliothèque") : several ? QStringLiteral("%1 clips").arg(count) : QStringLiteral("Le clip")));
    if (!m_setup.library) {
        auto* rows = new QWidget; auto* list = new QVBoxLayout(rows); list->setContentsMargins(0, 0, 0, 0); list->setSpacing(0);
        for (int index = 0; index < count; ++index) {
            if (index) list->addWidget(rowLine());
            auto* row = new QWidget; row->setFixedHeight(42); auto* line = new QHBoxLayout(row); line->setContentsMargins(12, 0, 12, 0); line->setSpacing(12); line->addWidget(new ThumbView(m_cache, m_setup.paths[index]));
            line->addWidget(new ElideLabel(QFileInfo(m_setup.paths[index]).completeBaseName(), "strong", Qt::ElideMiddle), 1);
            auto* again = new Tag(QStringLiteral("Déjà compressé"), Tag::Warn); again->setObjectName(QStringLiteral("compress-again-%1").arg(index)); again->hide(); line->addWidget(again); m_againTags.append(again);
            line->addWidget(monoLabel(formatBytes(QFileInfo(m_setup.paths[index]).size()), 12, tk::text2)); list->addWidget(row);
        }
        QWidget* well; auto* scroll = listWell(rows, well); scroll->setFixedHeight(int(std::min(3, std::max(1, count))) * 43); what->addWidget(well);
    } else what->addWidget(label(QStringLiteral("%1 clips · %2. Ceux déjà compressés par Clipsapp sont passés ; un clip qui ne gagne rien garde son original.").arg(count).arg(formatBytes(total)), "hint", true));
    layout->addLayout(what);
    // Said, not refused: a clip compressed again loses a little more, for little room.
    m_againWarning = new Banner(Banner::Warn, QStringLiteral("warn")); m_againWarning->setObjectName("compress-again-warning"); m_againWarning->hide(); layout->addWidget(m_againWarning);
    // In a batch, those already compressed are left as they are unless this is ticked.
    if (several && !m_setup.library) {
        m_again = new Toggle(QStringLiteral("Re-compresser les clips déjà compressés"), Toggle::Box); m_again->setObjectName("compress-again-toggle"); m_again->hide(); layout->addWidget(m_again);
        connect(m_again, &QCheckBox::toggled, this, [this] { updateAgain(); });
    }
    auto* note = new QHBoxLayout; note->setSpacing(8); note->addWidget(new IconLabel(QStringLiteral("info"), tk::text2, 14), 0, Qt::AlignTop);
    note->addWidget(label(several ? QStringLiteral("Le son, les pistes et leurs noms sont gardés tels quels, comme la date du clip. Chaque clip vérifié remplace son original, qui part à la corbeille.")
                                   : QStringLiteral("Le son, les pistes et leurs noms sont gardés tels quels, comme la date du clip. Vous comparez les deux versions avant de remplacer l'original."), "hint", true), 1);
    layout->addLayout(note);
    if (m_setup.editedChanges) { auto* edited = label(QStringLiteral("Vos modifications non exportées ne sont pas comprises : c'est le fichier du clip qui est compressé."), "warn", true); edited->setObjectName("compress-edited-warning"); layout->addWidget(edited); }
    if (several) {
        m_permanent = new Toggle(QStringLiteral("Supprimer les originaux définitivement, sans passer par la corbeille"), Toggle::Box); m_permanent->setObjectName("compress-permanent"); layout->addWidget(m_permanent);
        m_permanentWarning = label(QStringLiteral("Irréversible : les originaux ne pourront pas être récupérés."), "danger", true); m_permanentWarning->setContentsMargins(26, 0, 0, 0); m_permanentWarning->hide(); layout->addWidget(m_permanentWarning);
        connect(m_permanent, &QCheckBox::toggled, this, [this](bool on) { m_permanentWarning->setVisible(on); refit(); });
    }
    return page;
}
QWidget* CompressorDialog::buildDiscordOptions() {
    auto* page = new QWidget; auto* layout = new QVBoxLayout(page); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(18);
    auto* limitGroup = new QVBoxLayout; limitGroup->setSpacing(8); limitGroup->addWidget(caption(QStringLiteral("Taille visée par fichier")));
    auto* grid = new QGridLayout; grid->setSpacing(8); int chosen = -1;
    for (int index = 0; index < 6; ++index) {
        auto* tile = index < 5 ? new Tile(formatLimit(qint64(limits[index].megabytes) * 1000000), QString::fromUtf8(limits[index].name)) : new Tile(QStringLiteral("… Mo"), QStringLiteral("Personnalisé"));
        tile->setTitlePixels(15); tile->setObjectName(QStringLiteral("discord-limit-%1").arg(index)); if (index == 5) tile->setMutedTitle(true);
        if (index < 5 && chosen < 0 && qint64(limits[index].megabytes) * 1000000 == m_shareDefaults.maxBytes) chosen = index;
        grid->addWidget(tile, index / 3, index % 3); m_limits.append(tile);
        connect(tile, &QPushButton::clicked, this, [this, index] {
            for (int other = 0; other < m_limits.size(); ++other) m_limits[other]->setChecked(other == index);
            if (index < 5) { QSignalBlocker blocker(m_limit); m_limit->setValue(limits[index].megabytes); }
            m_customRow->setVisible(index == 5); if (index == 5) { m_limit->setFocus(); m_limit->selectAll(); } updateEstimates(); refit();
        });
    }
    if (chosen < 0) chosen = 5;
    m_limits[chosen]->setChecked(true); limitGroup->addLayout(grid);
    m_customRow = new QWidget; auto* custom = new QHBoxLayout(m_customRow); custom->setContentsMargins(0, 0, 0, 0); custom->setSpacing(10); custom->addWidget(label(QStringLiteral("Taille maximale par fichier"), "lbl"));
    m_limit = new QDoubleSpinBox; m_limit->setObjectName("discord-limit-mb"); m_limit->setRange(0.1, 10000); m_limit->setDecimals(1); m_limit->setSuffix(QStringLiteral(" Mo")); m_limit->setButtonSymbols(QAbstractSpinBox::NoButtons); m_limit->setFixedWidth(120); m_limit->setFont(monoFont(13));
    m_limit->setValue(m_shareDefaults.maxBytes / 1000000.0); custom->addWidget(m_limit); custom->addStretch(); m_customRow->setVisible(chosen == 5); limitGroup->addWidget(m_customRow);
    limitGroup->addWidget(label(QStringLiteral("Les limites des serveurs boostés ne valent que dans ces serveurs. 1 Mo = 1 000 000 octets."), "hint", true)); layout->addLayout(limitGroup);
    connect(m_limit, &QDoubleSpinBox::valueChanged, this, [this] {
        // A typed size that is no preset becomes the custom limit.
        int match = 5; for (int index = 0; index < 5; ++index) if (m_limits[index]->isChecked() && qAbs(limits[index].megabytes - m_limit->value()) < 0.01) match = index;
        for (int index = 0; index < m_limits.size(); ++index) m_limits[index]->setChecked(index == match);
        m_customRow->setVisible(match == 5); updateEstimates();
    });
    auto* estimateGroup = new QVBoxLayout; estimateGroup->setSpacing(8); estimateGroup->addWidget(caption(QStringLiteral("Résultat estimé")));
    auto* rows = new QWidget; auto* estimates = new QVBoxLayout(rows); estimates->setContentsMargins(0, 0, 0, 0); estimates->setSpacing(0);
    for (int index = 0; index < (m_setup.library ? 0 : int(m_setup.paths.size())); ++index) {
        if (index) estimates->addWidget(rowLine());
        auto* row = new QWidget; row->setFixedHeight(46); auto* rowLayout = new QHBoxLayout(row); rowLayout->setContentsMargins(12, 0, 12, 0); rowLayout->setSpacing(12); rowLayout->addWidget(new ThumbView(m_cache, m_setup.paths[index]));
        auto* texts = new QVBoxLayout; texts->setSpacing(0); texts->addStretch(); texts->addWidget(new ElideLabel(QFileInfo(m_setup.paths[index]).completeBaseName(), "strong", Qt::ElideMiddle));
        auto* figures = new ElideLabel({}, "small"); figures->setFont(monoFont(11)); texts->addWidget(figures); texts->addStretch(); rowLayout->addLayout(texts, 1);
        auto* tag = new Tag(QStringLiteral("Analyse…")); rowLayout->addWidget(tag); m_estimateTexts.append(figures); m_estimateTags.append(tag); estimates->addWidget(row);
    }
    QWidget* well; auto* scroll = listWell(rows, well); scroll->setFixedHeight(int(std::min<qsizetype>(3, std::max<qsizetype>(1, m_estimateTexts.size())) * 47)); estimateGroup->addWidget(well);
    m_estimateWarning = label({}, "warn", true); m_estimateWarning->setObjectName("discord-estimate-warning"); m_estimateWarning->hide(); estimateGroup->addWidget(m_estimateWarning); layout->addLayout(estimateGroup);
    auto* codecGroup = new QVBoxLayout; codecGroup->setSpacing(4); codecGroup->addWidget(caption(QStringLiteral("Format"))); codecGroup->addSpacing(4);
    const QStringList names{QStringLiteral("H.264"), QStringLiteral("HEVC"), QStringLiteral("AV1")}, details{QStringLiteral("compatible partout, rapide"), QStringLiteral("plus compact, utilise le GPU"), QStringLiteral("meilleure image, plus lent")};
    auto* codecHost = new QWidget; auto* codecLayout = new QVBoxLayout(codecHost); codecLayout->setContentsMargins(0, 0, 0, 0); codecLayout->setSpacing(4);
    for (int index = 0; index < 3; ++index) {
        auto* row = new RadioRow(names[index], details[index], codecHost); row->setObjectName(QStringLiteral("discord-codec-%1").arg(codecs[index])); row->setChecked(m_shareDefaults.codec == QLatin1String(codecs[index])); codecLayout->addWidget(row); m_codecs.append(row);
        connect(row, &QPushButton::clicked, this, [this] { updateEstimates(); });
    }
    if (std::none_of(m_codecs.begin(), m_codecs.end(), [](const QPushButton* row) { return row->isChecked(); })) m_codecs.first()->setChecked(true);
    codecGroup->addWidget(codecHost); layout->addLayout(codecGroup);
    auto* resizeGroup = new QVBoxLayout; resizeGroup->setSpacing(2); m_resize = new Toggle(QStringLiteral("Réduire la résolution si nécessaire")); m_resize->setObjectName("discord-auto-resize"); m_resize->setFont(uiFont(13, QFont::DemiBold)); m_resize->setChecked(m_shareDefaults.autoResize);
    auto* resizeHint = label(QStringLiteral("Seulement quand la limite l'exige, et juste assez pour garder une image nette. Les FPS sont conservés ; le son est réuni en une piste."), "hint"); resizeHint->setContentsMargins(46, 0, 0, 0); resizeGroup->addWidget(m_resize); resizeGroup->addWidget(resizeHint); layout->addLayout(resizeGroup);
    connect(m_resize, &QCheckBox::toggled, this, [this] { updateEstimates(); });
    auto* temporary = new QHBoxLayout; temporary->setSpacing(8); temporary->addWidget(new IconLabel(QStringLiteral("info"), tk::text2, 14), 0, Qt::AlignTop);
    temporary->addWidget(label(m_setup.edited ? QStringLiteral("L'édition est d'abord enregistrée en haute qualité à côté de l'original. La copie pour Discord, elle, est temporaire : glissez-la avant de fermer cette fenêtre.")
                                              : QStringLiteral("La copie est temporaire : glissez-la dans Discord avant de fermer cette fenêtre, rien ne s'accumule sur le disque. L'original ne change pas."), "hint", true), 1);
    layout->addLayout(temporary);
    return page;
}
void CompressorDialog::buildDiscordWork() {
    auto* page = new QWidget; auto* layout = new QVBoxLayout(page); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(12);
    auto* rows = new QWidget; m_jobs = new QVBoxLayout(rows); m_jobs->setContentsMargins(0, 0, 0, 0); m_jobs->setSpacing(0); m_jobs->addStretch();
    QWidget* well; auto* scroll = listWell(rows, well); scroll->setFixedHeight(65); m_jobScroll = scroll; layout->addWidget(well, 1);
    auto* hint = new QHBoxLayout; hint->setSpacing(8); hint->addWidget(new IconLabel(QStringLiteral("info"), tk::text2, 14), 0, Qt::AlignTop);
    hint->addWidget(label(QStringLiteral("Glissez une copie prête dans une conversation Discord, ou copiez-la puis collez-la avec Ctrl V. Les copies sont effacées à la fermeture de cette fenêtre."), "hint", true), 1); layout->addLayout(hint);
    page->hide(); m_stages->addWidget(page);
}
void CompressorDialog::buildSpaceWork() {
    auto* page = new QWidget; auto* layout = new QVBoxLayout(page); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(10);
    m_workProgress = new QWidget; auto* progress = new QVBoxLayout(m_workProgress); progress->setContentsMargins(0, 0, 0, 0); progress->setSpacing(10);
    auto* figures = new QHBoxLayout; m_workPercent = monoLabel(QStringLiteral("0 %"), 26, tk::text, QFont::Bold); m_workPercent->setObjectName("compress-percent"); figures->addWidget(m_workPercent, 0, Qt::AlignBottom); figures->addStretch();
    m_workEta = label({}, "small"); figures->addWidget(m_workEta, 0, Qt::AlignBottom); progress->addLayout(figures);
    m_workBar = new Bar(nullptr, 8); m_workBar->setObjectName("compress-progress"); progress->addWidget(m_workBar); layout->addWidget(m_workProgress);
    m_workStage = label({}, "hint", true); m_workStage->setObjectName("compress-stage"); layout->addWidget(m_workStage);
    page->hide(); m_stages->addWidget(page);
}
void CompressorDialog::buildCompare() {
    auto* page = new QWidget; auto* layout = new QVBoxLayout(page); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(10);
    m_compare = new CompareView; m_compare->setObjectName("compress-compare"); m_compare->setVolume(m_setup.volume); layout->addWidget(m_compare, 1);
    connect(m_compare, &CompareView::volumeChanged, this, &CompressorDialog::volumeChanged);
    auto* hint = new QHBoxLayout; hint->setSpacing(8); hint->addWidget(new IconLabel(QStringLiteral("info"), tk::text2, 14), 0, Qt::AlignTop);
    hint->addWidget(label(QStringLiteral("Glissez la séparation sur l'image : à gauche l'original, à droite la version compressée. À 100 % et 200 %, chaque pixel du clip est montré tel quel."), "hint", true), 1); layout->addLayout(hint);
    m_keep = new Button(QStringLiteral("Garder l'original")); m_keep->setObjectName("compress-keep"); m_keep->hide();
    m_replace = new Button(QStringLiteral("check"), QStringLiteral("Remplacer l'original"), Button::Primary); m_replace->setObjectName("compress-replace"); m_replace->setAutoDefault(false); m_replace->hide();
    footer()->addWidget(m_keep); footer()->addWidget(m_replace);
    connect(m_keep, &QPushButton::clicked, this, [this] { reject(); });
    connect(m_replace, &QPushButton::clicked, this, [this] {
        int index = -1; for (int at = 0; at < m_space->jobs().size(); ++at) if (m_space->jobs()[at].state == CompressJob::Review) index = at;
        if (index < 0 || m_replacing) return;
        // The original goes the way every deletion goes: asked, to the trash or, ticked on purpose, for good.
        const auto answer = confirmTrash(this, {m_space->jobs()[index].source}, false, QStringLiteral("La version compressée prend sa place, sous le même nom."));
        if (answer == Deletion::Kept) return;
        // Both files are let go before the original leaves its place.
        m_replacing = true; m_compare->close(); m_keep->setEnabled(false); m_replace->setEnabled(false); m_replace->setText(QStringLiteral("Remplacement…"));
        m_space->replace(index, answer == Deletion::Permanent);
    });
    page->hide(); m_stages->addWidget(page);
}
void CompressorDialog::buildBatch() {
    auto* page = new QWidget; auto* layout = new QVBoxLayout(page); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(12);
    auto* current = card(); auto* currentLayout = new QVBoxLayout(current); currentLayout->setContentsMargins(14, 12, 14, 12); currentLayout->setSpacing(6);
    auto* head = new QHBoxLayout; m_batchName = new ElideLabel({}, "strong", Qt::ElideMiddle); m_batchName->setObjectName("compress-batch-name"); head->addWidget(m_batchName, 1); m_batchFigures = monoLabel({}, 12, tk::text2); head->addWidget(m_batchFigures); currentLayout->addLayout(head);
    m_batchBar = new Bar; currentLayout->addWidget(m_batchBar); m_batchStage = new ElideLabel({}, "small"); currentLayout->addWidget(m_batchStage); layout->addWidget(current);
    auto* rows = new QWidget; m_batchRows = new QVBoxLayout(rows); m_batchRows->setContentsMargins(0, 0, 0, 0); m_batchRows->setSpacing(0); m_batchRows->addStretch();
    QWidget* well; auto* scroll = listWell(rows, well); scroll->setFixedHeight(5 * 41); m_batchScroll = well; well->hide(); layout->addWidget(well);
    auto* hint = new QHBoxLayout; hint->setSpacing(8); hint->addWidget(new IconLabel(QStringLiteral("info"), tk::text2, 14), 0, Qt::AlignTop);
    hint->addWidget(label(QStringLiteral("La compression continue en arrière-plan si vous fermez cette fenêtre ; le Compresseur montre où elle en est. Les originaux remplacés sont dans la corbeille."), "hint", true), 1); layout->addLayout(hint);
    page->hide(); m_stages->addWidget(page);
}
void CompressorDialog::chooseGoal(Goal goal) {
    m_goal = goal; m_spaceTile->setChecked(goal == Space); m_discordTile->setChecked(goal == Discord);
    // The settings of the goal chosen; before a choice, a word on what follows.
    for (int index = 0; index < m_goalPages.size(); ++index) m_goalPages[index]->setVisible(index == (goal == Space ? 1 : goal == Discord ? 2 : 0));
    // What will really be compressed: in a batch, without the clips already compressed unless asked.
    const auto paths = goal == Space ? spacePaths() : m_setup.paths; const int count = int(paths.size()); m_start->setEnabled(goal != Choose && count > 0);
    m_start->setText(goal == Discord ? (count > 1 ? QStringLiteral("Compresser %1 copies").arg(count) : QStringLiteral("Compresser pour Discord")) : m_setup.library ? QStringLiteral("Compresser la bibliothèque") : count > 1 ? QStringLiteral("Compresser %1 clips").arg(count) : QStringLiteral("Compresser le clip"));
    m_start->setIconName(goal == Discord ? QStringLiteral("share") : QStringLiteral("compress"));
    qint64 total = 0; for (const auto& path : paths) total += QFileInfo(path).size();
    setHeadingTexts(m_setup.library ? QStringLiteral("Compresser la bibliothèque") : goal == Discord ? QStringLiteral("Compresser pour Discord") : goal == Space ? QStringLiteral("Gagner de la place") : QStringLiteral("Compresser"),
        count > 1 || m_setup.library ? QStringLiteral("%1 clips · %2 au total").arg(count).arg(formatBytes(total)) : count == 1 ? QStringLiteral("%1 · %2").arg(QFileInfo(paths.first()).completeBaseName(), formatBytes(total)) : QString());
    m_workMark->set(goal == Discord ? QStringLiteral("share") : QStringLiteral("compress"), goal == Discord ? tk::info : tk::accent, goal == Discord ? tk::infoSoft : tk::accentSoft);
    if (goal == Discord) updateEstimates();
    refit();
}
void CompressorDialog::showStage(Stage stage) {
    m_stage = stage;
    for (int index = 0; index < m_stages->count(); ++index) m_stages->itemAt(index)->widget()->setVisible(index == int(stage));
    // The comparison is large only while it is shown.
    m_compare->setMinimumHeight(stage == Compare ? 440 : 0);
    setFixedWidth(stage == Compare ? 980 : stage == SpaceWork ? 520 : 640);
    m_start->setVisible(stage == Options); m_overall->setVisible(stage == DiscordWork || stage == SpaceBatch);
    m_cancel->setVisible(stage == Options); m_stop->hide(); m_background->hide(); m_close->hide(); m_dragAll->hide(); m_details->hide();
    m_keep->setVisible(stage == Compare); m_replace->setVisible(stage == Compare);
    if (stage == DiscordWork) refreshDiscord(); else if (stage == SpaceWork || stage == Compare) refreshSpace(); else if (stage == SpaceBatch) refreshBatch();
    refit();
}
qint64 CompressorDialog::limitBytes() const { for (int index = 0; index < 5; ++index) if (m_limits[index]->isChecked()) return qint64(limits[index].megabytes) * 1000000; return qRound64(m_limit->value() * 1000000); }
QString CompressorDialog::codec() const { for (int index = 0; index < 3; ++index) if (m_codecs[index]->isChecked()) return QString::fromLatin1(codecs[index]); return QStringLiteral("h264"); }
CompressSettings CompressorDialog::spaceSettings() const { CompressSettings settings; settings.level = m_level->currentIndex(); settings.processor = m_engines.value(1) && m_engines[1]->isChecked(); return settings; }
QStringList CompressorDialog::spacePaths() const {
    if (!m_again || m_again->isChecked()) return m_setup.paths;
    QStringList paths;
    for (int index = 0; index < m_setup.paths.size(); ++index) if (!(index < m_infos.size() && m_infos[index].error.isEmpty() && m_infos[index].info.compressed)) paths.append(m_setup.paths[index]);
    return paths;
}
// The clips Clipsapp already compressed, once their files are read: marked in the list, and said above it.
void CompressorDialog::updateAgain() {
    if (m_setup.library) return;
    int again = 0;
    for (int index = 0; index < m_infos.size() && index < m_againTags.size(); ++index) { const bool done = m_infos[index].error.isEmpty() && m_infos[index].info.compressed; m_againTags[index]->setVisible(done); again += done; }
    const int count = int(m_setup.paths.size());
    if (count == 1) m_againWarning->setTexts(QStringLiteral("Ce clip a déjà été compressé par Clipsapp"), QStringLiteral("Le compresser à nouveau lui fait perdre un peu plus de qualité, pour peu de place gagnée. Vous pouvez tout de même continuer, et comparer avant de remplacer l'original."));
    else {
        const bool redo = m_again && m_again->isChecked(); const bool one = again == 1;
        m_againWarning->setTexts(again == count ? QStringLiteral("Ces clips ont déjà été compressés par Clipsapp") : again > 1 ? QStringLiteral("%1 de ces clips ont déjà été compressés par Clipsapp").arg(again) : QStringLiteral("Un de ces clips a déjà été compressé par Clipsapp"),
            redo ? (one ? QStringLiteral("Il sera compressé à nouveau : il y perdra un peu plus de qualité, pour peu de place gagnée.") : QStringLiteral("Ils seront compressés à nouveau : ils y perdront un peu plus de qualité, pour peu de place gagnée."))
                 : (one ? QStringLiteral("Il est laissé tel quel : le compresser à nouveau lui ferait perdre un peu plus de qualité, pour peu de place gagnée.") : QStringLiteral("Ils sont laissés tels quels : les compresser à nouveau leur ferait perdre un peu plus de qualité, pour peu de place gagnée.")));
    }
    m_againWarning->setVisible(again > 0); if (m_again) m_again->setVisible(again > 0);
    chooseGoal(m_goal); refit();
}
void CompressorDialog::updateEstimates() {
    ClipExportOptions options; options.maxBytes = limitBytes(); options.codec = codec(); options.autoResize = m_resize->isChecked(); QString warning; double worst = 1;
    for (int index = 0; index < m_estimateTexts.size(); ++index) {
        auto* figures = static_cast<ElideLabel*>(m_estimateTexts[index]); auto* tag = m_estimateTags[index]; const qint64 bytes = QFileInfo(m_setup.paths[index]).size();
        if (index >= m_infos.size()) { figures->setFullText(formatBytes(bytes)); tag->set(QStringLiteral("Analyse…"), Tag::Neutral); continue; }
        if (!m_infos[index].error.isEmpty()) { figures->setFullText(firstLine(m_infos[index].error)); tag->set(QStringLiteral("Illisible"), Tag::Err); tag->setIconName({}); continue; }
        const auto& info = m_infos[index].info; auto edit = m_setup.edited && m_setup.editedInfo ? *m_setup.edited : defaultEdit(info, m_shareDefaults.mixOf(info.path)); edit.separateTracks = false;
        // An edited clip is exported in high quality first; only that step decides whether a plain copy will do.
        const auto plan = planSizedExport(info, edit, m_setup.edited ? options.maxBytes + 1 : bytes, options); tag->setIconName({});
        if (plan.copyVideo) { figures->setFullText(formatBytes(bytes)); tag->set(QStringLiteral("Copié tel quel"), Tag::Ok); continue; }
        if (!plan.feasible) { figures->setFullText(QStringLiteral("%1 · trop long pour cette limite").arg(formatBytes(bytes))); tag->set(QStringLiteral("Impossible"), Tag::Err); continue; }
        const double scale = plan.outputSize.height() / double(std::max(1, edit.crop.height())), density = plan.videoBitrate / (std::max(1.0, info.fps) * plan.outputSize.width() * plan.outputSize.height());
        const bool degraded = scale < 0.5 || density < (options.codec == "h264" ? 0.0225 : 0.015);
        figures->setFullText(QStringLiteral("%1 → ≈ %2 · %3p %4 FPS").arg(formatBytes(bytes), formatBytes(plan.estimatedBytes)).arg(plan.outputSize.height()).arg(qRound(info.fps)));
        if (degraded) { tag->set(QStringLiteral("Image dégradée"), Tag::Warn); tag->setIconName(QStringLiteral("warn")); } else tag->set(QStringLiteral("Réencodé"), Tag::Neutral);
        if (degraded && scale <= worst) {
            worst = scale; qint64 bigger = 0; for (const auto& limit : limits) if (!bigger && qint64(limit.megabytes) * 1000000 > options.maxBytes) bigger = qint64(limit.megabytes) * 1000000;
            // Said as what will happen: the copy is made all the same, smaller or less sharp.
            const auto better = bigger ? QStringLiteral("Coupez le clip ou visez %1 pour une image plus nette.").arg(formatLimit(bigger)) : QStringLiteral("Coupez le clip pour une image plus nette.");
            warning = options.autoResize ? QStringLiteral("%1 dans %2 : la copie est faite, mais en %3p et moins nette. %4").arg(roughDuration(edit.endMs - edit.startMs), formatLimit(options.maxBytes)).arg(plan.outputSize.height()).arg(better)
                                         : QStringLiteral("%1 dans %2 : la copie est faite, mais l'image sera très dégradée. Cochez « Réduire la résolution si nécessaire », ou %3").arg(roughDuration(edit.endMs - edit.startMs), formatLimit(options.maxBytes), better.left(1).toLower() + better.mid(1));
        }
    }
    m_estimateWarning->setText(warning); m_estimateWarning->setVisible(!warning.isEmpty());
}
void CompressorDialog::begin() {
    if (m_setup.paths.isEmpty()) return;
    if (m_goal == Discord) {
        if (m_discord->running()) return;
        ShareOptions options = m_shareDefaults; options.maxBytes = limitBytes(); options.codec = codec(); options.autoResize = m_resize->isChecked();
        // The copies live as long as the dialog, in a folder of their own that goes with it.
        m_temporary = std::make_shared<QTemporaryDir>(QDir(QDir::tempPath()).filePath(QStringLiteral("Clipsapp-discord-XXXXXX")));
        if (!m_temporary->isValid()) { m_temporary.reset(); setHeadingTexts(QStringLiteral("Compresser pour Discord"), QStringLiteral("Le dossier temporaire n'a pas pu être créé.")); return; }
        options.directory = m_temporary->path();
        emit preferencesChosen(options.maxBytes, options.codec, options.autoResize);
        m_discord->start(m_setup.paths, options, m_setup.editedInfo, m_setup.edited); showStage(DiscordWork); return;
    }
    if (m_goal != Space || m_space->busy()) return;
    CompressOptions options; options.settings = spaceSettings(); options.review = m_setup.paths.size() == 1 && !m_setup.library; options.permanent = m_permanent && m_permanent->isChecked();
    // Clips chosen one by one are compressed even if they were already, after the warning; a whole library passes them.
    options.again = !m_setup.library;
    emit spaceSettingsChosen(options.settings);
    m_space->start(spacePaths(), options); showStage(options.review ? SpaceWork : SpaceBatch);
}
void CompressorDialog::refreshDiscord() {
    const auto& jobs = m_discord->jobs(); QList<int> shown; for (int index = 0; index < jobs.size(); ++index) if (jobs[index].batch == m_discord->batch()) shown.append(index);
    if (m_jobBatch != m_discord->batch() || m_jobRows.size() != shown.size()) {
        m_jobBatch = m_discord->batch(); for (auto* row : m_jobRows) { row->hide(); row->deleteLater(); } m_jobRows.clear();
        while (m_jobs->count() > 1) { auto* child = m_jobs->takeAt(0); if (child->widget()) child->widget()->deleteLater(); delete child; }
        for (int position = 0; position < shown.size(); ++position) {
            if (position) m_jobs->insertWidget(m_jobs->count() - 1, rowLine());
            auto* row = new JobRow(m_discord, [this](const QString& path) { emit copyRequested({path}); }); m_jobs->insertWidget(m_jobs->count() - 1, row); m_jobRows.append(row);
        }
        m_jobScroll->setFixedHeight(int(std::clamp<qsizetype>(shown.size(), 1, 5)) * 65); refit();
    }
    const int current = m_discord->current(), left = m_discord->remainingSeconds();
    for (int position = 0; position < shown.size(); ++position) static_cast<JobRow*>(m_jobRows[position])->apply(shown[position], jobs[shown[position]], shown[position] == current ? left : -1);
    const int ready = m_discord->count(ShareJob::Ready), failed = m_discord->count(ShareJob::Failed), size = int(shown.size()); const bool running = m_discord->running();
    m_overall->setValue(m_discord->percent() / 100.0); m_cancel->setVisible(running); m_close->setVisible(!running);
    static_cast<DragAll*>(m_dragAll)->setCount(ready); m_dragAll->setVisible(!running && ready > 1);
    if (running) m_workMark->set(QStringLiteral("share"), tk::info, tk::infoSoft);
    else if (ready) m_workMark->set(QStringLiteral("ok"), tk::accent, tk::accentSoft);
    else m_workMark->set(QStringLiteral("warn"), tk::warn, tk::warnSoft);
    if (running) {
        int position = 0; for (const int index : shown) if (index <= current) ++position;
        const auto name = current >= 0 && current < jobs.size() ? jobs[current].name : QString(); const auto stage = current >= 0 && current < jobs.size() ? jobs[current].stage : QString();
        setHeadingTexts(size > 1 ? QStringLiteral("Préparation de %1 copies").arg(size) : QStringLiteral("Préparation de la copie"), QStringLiteral("Clip %1 sur %2 · %3%4").arg(std::max(1, position)).arg(size).arg(name, stage.isEmpty() ? QString() : QStringLiteral(" · ") + stage));
    } else {
        qint64 bytes = 0; for (const int index : shown) if (jobs[index].state == ShareJob::Ready) bytes += jobs[index].bytes;
        const auto summary = failed ? QStringLiteral("%1 prête%2, %3 échec%4").arg(ready).arg(ready > 1 ? "s" : "").arg(failed).arg(failed > 1 ? "s" : "") : ready > 1 ? QStringLiteral("%1 copies prêtes · %2").arg(ready).arg(formatBytes(bytes)) : ready == 1 ? QStringLiteral("Copie prête · %1").arg(formatBytes(bytes)) : QStringLiteral("Aucune copie préparée");
        setHeadingTexts(summary, ready > 1 ? QStringLiteral("Glissez-les dans Discord, ensemble ou une à une. Elles sont effacées à la fermeture.") : ready ? QStringLiteral("Glissez-la dans Discord. Elle est effacée à la fermeture.") : QStringLiteral("Le lot s'est arrêté avant de produire une copie."));
    }
}
void CompressorDialog::refreshSpace() {
    const auto& jobs = m_space->jobs(); if (jobs.isEmpty()) return; const auto& job = jobs.first();
    if (job.state == CompressJob::Review && m_stage == SpaceWork) {
        showStage(Compare); m_workMark->set(QStringLiteral("compress"), tk::accent, tk::accentSoft);
        setHeadingTexts(QStringLiteral("Comparez avant de remplacer"), QStringLiteral("%1 → %2 · %3 gagnés (%4)").arg(formatBytes(job.before), formatBytes(job.after), formatBytes(job.before - job.after), gain(job.before, job.after)));
        m_compare->open(job.source, job.output, job.before, job.after); m_keep->setDefault(true); return;
    }
    if (m_stage == Compare) {
        // The comparison already showed what was gained: the dialog leaves, and the window says it in a notification.
        if (job.state == CompressJob::Replaced && !m_replaced) { m_replaced = true; m_replacing = false; accept(); }
        else if (job.state == CompressJob::Failed && m_replacing) {
            m_replacing = false; showStage(SpaceWork); m_workProgress->hide(); m_close->show(); m_cancel->hide(); m_workMark->set(QStringLiteral("warn"), tk::warn, tk::warnSoft);
            setHeadingTexts(QStringLiteral("L'original n'a pas été remplacé"), job.name); m_workStage->setText(job.error);
        }
        return;
    }
    // Under way, or ended without anything to compare.
    if (m_replaced) return;
    const bool running = job.state == CompressJob::Running;
    m_workProgress->setVisible(running); m_cancel->setVisible(running); m_cancel->setText(QStringLiteral("Annuler")); m_close->setVisible(!running);
    if (running) {
        m_workBar->setValue(job.progress / 100.0); m_workPercent->setText(QStringLiteral("%1 %").arg(job.progress)); const int left = m_space->remainingSeconds();
        m_workEta->setText(left >= 0 ? QStringLiteral("encore ≈ %1").arg(remaining(left)) : QString()); m_workStage->setText(job.stage);
        setHeadingTexts(QStringLiteral("Compression en cours"), QStringLiteral("%1 · %2").arg(job.name, formatBytes(job.before)));
        return;
    }
    const bool fine = job.state == CompressJob::Kept || job.state == CompressJob::Skipped;
    m_workMark->set(fine ? QStringLiteral("info") : QStringLiteral("warn"), fine ? tk::info : tk::warn, fine ? tk::infoSoft : tk::warnSoft);
    if (job.state == CompressJob::Cancelled) { setHeadingTexts(QStringLiteral("Compression annulée"), job.name); m_workStage->setText(QStringLiteral("L'original n'a pas changé.")); }
    else if (fine) { setHeadingTexts(job.note, job.name); m_workStage->setText(job.state == CompressJob::Skipped ? QStringLiteral("Ce clip a déjà été compressé par Clipsapp : une seconde fois, il perdrait un peu plus pour peu de place.") : QStringLiteral("La version compressée ne gagnait presque rien : l'original est gardé tel quel.")); }
    else { setHeadingTexts(QStringLiteral("La compression a échoué"), job.name); m_workStage->setText(firstLine(job.error)); m_workStage->setToolTip(job.error); }
    refit();
}
void CompressorDialog::refreshBatch() {
    const auto& jobs = m_space->jobs(); const bool running = m_space->running(); const int index = m_space->current(), size = int(jobs.size());
    const int replaced = m_space->count(CompressJob::Replaced), failed = m_space->count(CompressJob::Failed); const qint64 saved = m_space->savedBytes();
    int done = 0; for (const auto& job : jobs) done += job.state != CompressJob::Pending && job.state != CompressJob::Running;
    m_overall->setValue(m_space->percent() / 100.0); m_stop->setVisible(running); m_background->setVisible(running); m_close->setVisible(!running); m_cancel->hide(); m_details->setVisible(!running && size > 0);
    if (running) m_background->setDefault(true); else m_close->setDefault(true);
    if (running) m_workMark->set(QStringLiteral("compress"), tk::accent, tk::accentSoft);
    else if (failed) m_workMark->set(QStringLiteral("warn"), tk::warn, tk::warnSoft);
    else m_workMark->set(QStringLiteral("ok"), tk::accent, tk::accentSoft);
    if (running && index >= 0 && index < size) {
        const auto& job = jobs[index]; m_batchName->setFullText(job.name); m_batchBar->setValue(job.progress / 100.0); const int left = m_space->remainingSeconds();
        m_batchFigures->setText(left >= 0 ? QStringLiteral("%1 % · ≈ %2").arg(job.progress).arg(remaining(left)) : QStringLiteral("%1 %").arg(job.progress)); m_batchStage->setFullText(job.stage);
        setHeadingTexts(QStringLiteral("Compression de %1 clips").arg(size), QStringLiteral("Clip %1 sur %2 · %3 gagnés").arg(done + 1).arg(size).arg(formatBytes(saved)));
    } else {
        m_batchName->setFullText(QStringLiteral("%1 clip%2 remplacé%2").arg(replaced).arg(replaced > 1 ? "s" : "")); m_batchBar->setValue(1); m_batchFigures->setText(formatBytes(saved));
        m_batchStage->setFullText(failed ? QStringLiteral("%1 échec%2 : leur original est gardé.").arg(failed).arg(failed > 1 ? "s" : "") : QStringLiteral("Les autres originaux sont gardés tels quels."));
        setHeadingTexts(saved > 0 ? QStringLiteral("%1 gagnés").arg(formatBytes(saved)) : QStringLiteral("Compression terminée"), QStringLiteral("%1 clip%2 traité%2 sur %3").arg(done).arg(done > 1 ? "s" : "").arg(size));
    }
    // The clips done so far, newest at the top; rows are only added.
    QList<int> finished; for (int at = 0; at < size; ++at) if (jobs[at].state != CompressJob::Pending && jobs[at].state != CompressJob::Running) finished.append(at);
    for (int at = m_batchShown; at < finished.size(); ++at) {
        const auto& job = jobs[finished[at]]; auto* row = new QWidget; row->setFixedHeight(40); auto* line = new QHBoxLayout(row); line->setContentsMargins(12, 0, 12, 0); line->setSpacing(10);
        line->addWidget(new ElideLabel(job.name, nullptr, Qt::ElideMiddle), 1);
        Tag* tag = nullptr;
        switch (job.state) {
        case CompressJob::Replaced: line->addWidget(monoLabel(QStringLiteral("%1 → %2").arg(formatBytes(job.before), formatBytes(job.after)), 12, tk::text2)); tag = new Tag(gain(job.before, job.after), Tag::Ok); break;
        case CompressJob::Failed: tag = new Tag(QStringLiteral("Échec"), Tag::Err); tag->setToolTip(job.error); break;
        case CompressJob::Cancelled: tag = new Tag(QStringLiteral("Arrêté"), Tag::Neutral); break;
        default: tag = new Tag(job.note.isEmpty() ? QStringLiteral("Gardé") : job.note, Tag::Neutral); break;
        }
        line->addWidget(tag); m_batchRows->insertWidget(0, row); if (m_batchRows->count() > 2) m_batchRows->insertWidget(1, rowLine());
    }
    if (finished.size() != m_batchShown) { m_batchShown = int(finished.size()); m_batchScroll->setVisible(m_batchShown > 0); refit(); }
}
void CompressorDialog::reject() {
    switch (m_stage) {
    case DiscordWork: if (m_discord->running()) m_discord->cancel(); break;
    case SpaceWork: if (m_space->running() && m_space->options().review) m_space->cancel(); break;
    case Compare: if (m_replacing) return; break;
    // A library goes on behind the window.
    default: break;
    }
    QDialog::reject();
}
}
namespace clipsapp {
void showCompressionReport(QWidget* parent, const QList<CompressJob>& jobs, bool permanent) {
    ModalDialog dialog(parent, 660); dialog.setObjectName("compression-report");
    qint64 before = 0, after = 0; int replaced = 0, failed = 0;
    for (const auto& job : jobs) { if (job.state == CompressJob::Replaced) { before += job.before; after += job.after; ++replaced; } else if (job.state == CompressJob::Failed) ++failed; }
    const qint64 saved = before - after;
    dialog.setHeading(saved > 0 ? QStringLiteral("%1 gagnés").arg(formatBytes(saved)) : QStringLiteral("Aucune place gagnée"),
        replaced ? QStringLiteral("%1 clip%2 compressé%2 sur %3 · %4 → %5 (%6)").arg(replaced).arg(replaced > 1 ? "s" : "").arg(jobs.size()).arg(formatBytes(before), formatBytes(after), gain(before, after))
                 : QStringLiteral("%1 clip%2 examiné%2, aucun remplacé").arg(jobs.size()).arg(jobs.size() > 1 ? "s" : ""),
        new IconBox(failed ? QStringLiteral("warn") : QStringLiteral("ok"), failed ? tk::warn : tk::accent, failed ? tk::warnSoft : tk::accentSoft));
    auto* rows = new QWidget; auto* list = new QVBoxLayout(rows); list->setContentsMargins(0, 0, 0, 0); list->setSpacing(0);
    for (int index = 0; index < jobs.size(); ++index) {
        const auto& job = jobs[index]; if (index) list->addWidget(rowLine());
        auto* row = new QWidget; row->setObjectName(QStringLiteral("report-row-%1").arg(index)); row->setFixedHeight(52); auto* line = new QHBoxLayout(row); line->setContentsMargins(14, 0, 14, 0); line->setSpacing(12);
        auto* texts = new QVBoxLayout; texts->setSpacing(1); texts->addStretch(); texts->addWidget(new ElideLabel(job.name, "strong", Qt::ElideMiddle));
        QString detail; Tag* tag = nullptr;
        switch (job.state) {
        case CompressJob::Replaced: detail = job.note.isEmpty() ? (permanent ? QStringLiteral("Remplacé, original supprimé") : QStringLiteral("Remplacé, original à la corbeille")) : job.note; tag = new Tag(gain(job.before, job.after), Tag::Ok); break;
        case CompressJob::Failed: detail = firstLine(job.error); tag = new Tag(QStringLiteral("Échec"), Tag::Err); break;
        case CompressJob::Cancelled: detail = QStringLiteral("Arrêté avant d'être compressé : l'original n'a pas changé"); tag = new Tag(QStringLiteral("Arrêté"), Tag::Neutral); break;
        case CompressJob::Pending: case CompressJob::Running: detail = QStringLiteral("En cours"); tag = new Tag(QStringLiteral("En cours"), Tag::Info); break;
        default: detail = job.note.isEmpty() ? QStringLiteral("Original gardé") : job.note; tag = new Tag(QStringLiteral("Gardé"), Tag::Neutral); break;
        }
        auto* small = new ElideLabel(detail, job.state == CompressJob::Failed ? "danger" : "small"); small->setToolTip(job.state == CompressJob::Failed ? job.error : detail); texts->addWidget(small); texts->addStretch(); line->addLayout(texts, 1);
        line->addWidget(monoLabel(job.state == CompressJob::Replaced ? QStringLiteral("%1 → %2").arg(formatBytes(job.before), formatBytes(job.after)) : formatBytes(job.before), 12, job.state == CompressJob::Replaced ? tk::text : tk::text2));
        line->addWidget(tag); list->addWidget(row);
    }
    QWidget* well; auto* scroll = listWell(rows, well); scroll->setFixedHeight(int(std::clamp<qsizetype>(jobs.size(), 1, 6)) * 53); dialog.body()->addWidget(well);
    auto* note = new QHBoxLayout; note->setSpacing(8); note->addWidget(new IconLabel(QStringLiteral("info"), tk::text2, 14), 0, Qt::AlignTop);
    note->addWidget(label((permanent ? QStringLiteral("Les originaux remplacés sont supprimés définitivement.") : QStringLiteral("Les originaux remplacés sont dans la corbeille du système, d'où ils se récupèrent."))
        + (failed ? QStringLiteral(" Un clip en échec garde son original.") : QString()), "hint", true), 1);
    dialog.body()->addLayout(note);
    auto* close = new Button(QStringLiteral("Fermer"), Button::Primary); close->setObjectName("report-close"); close->setDefault(true); dialog.footer()->addWidget(close);
    QObject::connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);
    dialog.exec();
}
}
