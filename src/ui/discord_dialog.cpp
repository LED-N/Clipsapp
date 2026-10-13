#include "discord_dialog.h"
#include "clip_list.h"
#include "thumbnail_cache.h"
#include "core/clip_library.h"
#include "media/clip_reader.h"
#include <QApplication>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollArea>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>

namespace clipsapp {
namespace {
struct Limit { int megabytes; const char* name; };
const Limit limits[]{{20, "Sans Nitro"}, {50, "Nitro Basic, Classic"}, {50, "Serveur niveau 2"}, {100, "Serveur niveau 3"}, {1000, "Nitro"}};
const char* const codecs[]{"h264", "hevc", "av1"};
QString firstLine(const QString& text) { return text.section('\n', 0, 0).trimmed(); }
QString roughDuration(qint64 ms) { const auto seconds = (ms + 500) / 1000; return seconds >= 90 ? QStringLiteral("%1 min").arg((seconds + 30) / 60) : QStringLiteral("%1 s").arg(seconds); }
class ThumbView final : public QWidget {
public:
    ThumbView(ThumbnailCache* cache, const QString& path) : m_cache(cache), m_path(path), m_key(ThumbnailCache::keyFor(path)) { setFixedSize(48, 27); connect(cache, &ThumbnailCache::ready, this, qOverload<>(&QWidget::update)); }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing); QPainterPath rounded; rounded.addRoundedRect(rect(), 4, 4); painter.setClipPath(rounded); painter.fillRect(rect(), tk::raised);
        const auto* preview = m_cache->preview(m_key); const auto* meta = m_cache->meta(m_key);
        if (!preview && !(meta && meta->failed)) m_cache->request(m_key, m_path);
        if (preview && !preview->image.isNull()) { painter.setRenderHint(QPainter::SmoothPixmapTransform); painter.drawImage(rect(), preview->image); }
    }
private:
    ThumbnailCache* m_cache; QString m_path, m_key;
};
class JobRow final : public QWidget {
public:
    JobRow(DiscordBatch* batch, std::function<void(const QString&)> copy) : m_batch(batch) {
        setMinimumHeight(64); setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed); auto* layout = new QHBoxLayout(this); layout->setContentsMargins(14, 12, 14, 12); layout->setSpacing(12);
        m_mark = new IconBox(QStringLiteral("share"), tk::info, tk::infoSoft, 28); m_mark->setRound(true); layout->addWidget(m_mark);
        auto* texts = new QVBoxLayout; texts->setSpacing(4); auto* head = new QHBoxLayout; m_name = new ElideLabel({}, "strong"); head->addWidget(m_name, 1); m_percent = monoLabel({}, 12, tk::text2); head->addWidget(m_percent);
        texts->addLayout(head); m_bar = new Bar; m_bar->setColors(tk::info); texts->addWidget(m_bar); m_status = new ElideLabel({}, "small"); texts->addWidget(m_status); layout->addLayout(texts, 1);
        m_dragHint = new QWidget; auto* hint = new QHBoxLayout(m_dragHint); hint->setContentsMargins(0, 0, 0, 0); hint->setSpacing(4); hint->addWidget(new IconLabel(QStringLiteral("grip"), tk::text3, 14)); hint->addWidget(label(QStringLiteral("Glisser"), "small3")); layout->addWidget(m_dragHint);
        m_copy = new Button(QStringLiteral("copy"), QStringLiteral("Copier"), Button::Secondary, Button::Small); layout->addWidget(m_copy);
        m_retry = new Button(QStringLiteral("refresh"), QStringLiteral("Réessayer"), Button::Secondary, Button::Small); layout->addWidget(m_retry);
        connect(m_copy, &QPushButton::clicked, this, [this, copy] { if (!m_output.isEmpty()) copy(m_output); });
        connect(m_retry, &QPushButton::clicked, this, [this] { m_batch->retry(m_index); });
    }
    void apply(int index, const ShareJob& job, int remaining) {
        m_index = index; m_output = job.state == ShareJob::Ready ? job.output : QString(); m_name->setFullText(job.name);
        const bool running = job.state == ShareJob::Running, ready = job.state == ShareJob::Ready, failed = job.state == ShareJob::Failed;
        m_bar->setVisible(running); m_percent->setVisible(running); m_copy->setVisible(ready); m_dragHint->setVisible(ready); m_retry->setVisible(failed); setCursor(ready ? Qt::OpenHandCursor : Qt::ArrowCursor);
        if (ready) m_mark->set(QStringLiteral("check"), tk::accent, tk::accentSoft); else if (failed) m_mark->set(QStringLiteral("x"), tk::danger, tk::dangerSoft); else m_mark->set(QStringLiteral("share"), running ? tk::info : tk::text3, running ? tk::infoSoft : tk::raised);
        m_status->setStyleSheet(failed ? QStringLiteral("color: #F2777C;") : QString());
        if (running) {
            m_bar->setValue(job.progress / 100.0); m_percent->setText(QStringLiteral("%1 %").arg(job.progress)); QStringList parts{job.stage}; if (!job.detail.isEmpty()) parts << job.detail; if (remaining >= 0) parts << QStringLiteral("encore ≈ %1 s").arg(remaining);
            m_status->setFullText(parts.join(QStringLiteral(" · ")));
        } else if (ready) m_status->setFullText(QStringLiteral("Prête · %1 · %2").arg(formatBytes(job.bytes), job.copied ? QStringLiteral("copiée telle quelle") : job.detail.isEmpty() ? QStringLiteral("réencodée") : job.detail));
        else if (failed) m_status->setFullText(QStringLiteral("Échec : ") + firstLine(job.error));
        else m_status->setFullText(job.state == ShareJob::Cancelled ? QStringLiteral("Annulée") : QStringLiteral("En attente"));
    }
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
}
DiscordDialog::DiscordDialog(DiscordBatch* batch, ThumbnailCache* cache, QStringList paths, const ShareOptions& defaults, QWidget* parent, std::optional<ClipInfo> editedInfo, std::optional<ClipEdit> edited)
    : ModalDialog(parent, 560), m_batch(batch), m_cache(cache), m_paths(std::move(paths)), m_defaults(defaults), m_editedInfo(std::move(editedInfo)), m_edited(std::move(edited)) {
    setObjectName("discord-export-dialog"); setHeading(QStringLiteral("Préparer pour Discord"));
    m_overall = new Bar; headerLayout()->addWidget(m_overall);
    m_stages = new QStackedWidget; body()->addWidget(m_stages);
    buildOptions(); buildProgress();
    auto* note = new QWidget; m_footerNote = note; auto* noteLayout = new QHBoxLayout(note); noteLayout->setContentsMargins(0, 0, 0, 0); noteLayout->setSpacing(6);
    noteLayout->addWidget(new IconLabel(QStringLiteral("folder"), tk::text2, 14)); noteLayout->addWidget(label(QStringLiteral("Rangées dans « Partage Discord »"), "small")); footerLeft()->addWidget(note);
    m_cancelBatch = new Button(QStringLiteral("Annuler le lot"), Button::Ghost); m_cancelBatch->setObjectName("discord-cancel-batch"); m_cancelBatch->setTextColor(tk::danger); footerLeft()->addWidget(m_cancelBatch);
    m_cancel = new Button(QStringLiteral("Annuler")); m_start = new Button(QStringLiteral("share"), QString(), Button::Primary); m_start->setObjectName("discord-start"); m_start->setDefault(true);
    m_background = new Button(QStringLiteral("Continuer en arrière-plan")); m_background->setObjectName("discord-background"); m_openSharing = new Button(QStringLiteral("share"), QStringLiteral("Ouvrir Partage Discord"), Button::Primary); m_openSharing->setObjectName("discord-open-sharing");
    for (auto* button : {m_cancel, m_start, m_background, m_openSharing}) footer()->addWidget(button);
    connect(m_cancel, &QPushButton::clicked, this, &QDialog::reject); connect(m_background, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_start, &QPushButton::clicked, this, [this] { begin(); });
    connect(m_cancelBatch, &QPushButton::clicked, m_batch, &DiscordBatch::cancel);
    connect(m_openSharing, &QPushButton::clicked, this, [this] { emit showSharingRequested(); accept(); });
    connect(m_batch, &DiscordBatch::changed, this, [this] { if (m_progressStage) refreshJobs(); });
    connect(m_batch, &DiscordBatch::runningChanged, this, [this] { if (m_progressStage) refreshJobs(); });
    connect(&m_probe, &QFutureWatcher<QList<DiscordPrepared>>::finished, this, [this] { m_infos = m_probe.result(); updateEstimates(); });
    if (m_paths.isEmpty() || m_batch->running()) { showStage(true); return; }
    showStage(false);
    if (m_editedInfo && m_edited) { m_infos = {{*m_editedInfo, {}}}; updateEstimates(); }
    else { const auto sources = m_paths; m_probe.setFuture(QtConcurrent::run([sources] {
        QList<DiscordPrepared> infos;
        for (const auto& path : sources) { DiscordPrepared result; try { ClipReader reader(path, false, true); result.info = reader.info(); } catch (const std::exception& error) { result.error = QString::fromUtf8(error.what()); } infos.append(result); }
        return infos;
    })); updateEstimates(); }
}
DiscordDialog::~DiscordDialog() { disconnect(&m_probe, nullptr, this, nullptr); }
void DiscordDialog::buildOptions() {
    auto* page = new QWidget; auto* layout = new QVBoxLayout(page); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(18);
    auto* limitGroup = new QVBoxLayout; limitGroup->setSpacing(8); limitGroup->addWidget(caption(QStringLiteral("Limite par fichier")));
    auto* grid = new QGridLayout; grid->setSpacing(8); int chosen = -1;
    for (int index = 0; index < 6; ++index) {
        auto* tile = index < 5 ? new Tile(formatLimit(qint64(limits[index].megabytes) * 1000000), QString::fromUtf8(limits[index].name)) : new Tile(QStringLiteral("… Mo"), QStringLiteral("Personnalisé"));
        tile->setTitlePixels(15); tile->setObjectName(QStringLiteral("discord-limit-%1").arg(index)); if (index == 5) tile->setMutedTitle(true);
        if (index < 5 && chosen < 0 && qint64(limits[index].megabytes) * 1000000 == m_defaults.maxBytes) chosen = index;
        grid->addWidget(tile, index / 3, index % 3); m_limits.append(tile);
        connect(tile, &QPushButton::clicked, this, [this, index] {
            for (int other = 0; other < m_limits.size(); ++other) m_limits[other]->setChecked(other == index);
            if (index < 5) { QSignalBlocker blocker(m_limit); m_limit->setValue(limits[index].megabytes); }
            m_customRow->setVisible(index == 5); if (index == 5) { m_limit->setFocus(); m_limit->selectAll(); } updateEstimates();
        });
    }
    if (chosen < 0) chosen = 5;
    m_limits[chosen]->setChecked(true); limitGroup->addLayout(grid);
    m_customRow = new QWidget; auto* custom = new QHBoxLayout(m_customRow); custom->setContentsMargins(0, 0, 0, 0); custom->setSpacing(10); custom->addWidget(label(QStringLiteral("Taille maximale par fichier"), "lbl"));
    m_limit = new QDoubleSpinBox; m_limit->setObjectName("discord-limit-mb"); m_limit->setRange(0.1, 10000); m_limit->setDecimals(1); m_limit->setSuffix(QStringLiteral(" Mo")); m_limit->setButtonSymbols(QAbstractSpinBox::NoButtons); m_limit->setFixedWidth(120); m_limit->setFont(monoFont(13));
    m_limit->setValue(m_defaults.maxBytes / 1000000.0); custom->addWidget(m_limit); custom->addStretch(); m_customRow->setVisible(chosen == 5); limitGroup->addWidget(m_customRow);
    limitGroup->addWidget(label(QStringLiteral("Les limites des serveurs boostés ne valent que dans ces serveurs. 1 Mo = 1 000 000 octets."), "hint", true)); layout->addLayout(limitGroup);
    connect(m_limit, &QDoubleSpinBox::valueChanged, this, [this] {
        // A typed size that is no preset becomes the custom limit.
        int match = 5; for (int index = 0; index < 5; ++index) if (m_limits[index]->isChecked() && qAbs(limits[index].megabytes - m_limit->value()) < 0.01) match = index;
        for (int index = 0; index < m_limits.size(); ++index) m_limits[index]->setChecked(index == match);
        m_customRow->setVisible(match == 5); updateEstimates();
    });

    auto* estimateGroup = new QVBoxLayout; estimateGroup->setSpacing(8); estimateGroup->addWidget(caption(QStringLiteral("Résultat estimé")));
    auto* well = card("well"); auto* wellLayout = new QVBoxLayout(well); wellLayout->setContentsMargins(1, 1, 1, 1);
    auto* rows = new QWidget; m_estimates = new QVBoxLayout(rows); m_estimates->setContentsMargins(0, 0, 0, 0); m_estimates->setSpacing(0);
    for (int index = 0; index < m_paths.size(); ++index) {
        if (index) { auto* line = separator(); line->setStyleSheet(QStringLiteral("background: #1E242C;")); m_estimates->addWidget(line); }
        auto* row = new QWidget; row->setFixedHeight(46); auto* rowLayout = new QHBoxLayout(row); rowLayout->setContentsMargins(12, 0, 12, 0); rowLayout->setSpacing(12); rowLayout->addWidget(new ThumbView(m_cache, m_paths[index]));
        auto* texts = new QVBoxLayout; texts->setSpacing(0); texts->addStretch(); texts->addWidget(new ElideLabel(QFileInfo(m_paths[index]).completeBaseName(), "strong", Qt::ElideMiddle));
        auto* figures = new ElideLabel({}, "small"); figures->setFont(monoFont(11)); texts->addWidget(figures); texts->addStretch(); rowLayout->addLayout(texts, 1);
        auto* tag = new Tag(QStringLiteral("Analyse…")); rowLayout->addWidget(tag); m_estimateTexts.append(figures); m_estimateTags.append(tag); m_estimates->addWidget(row);
    }
    auto* scroll = new QScrollArea; scroll->setWidgetResizable(true); scroll->setWidget(rows); scroll->setFrameShape(QFrame::NoFrame); scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFixedHeight(int(std::min<qsizetype>(3, std::max<qsizetype>(1, m_paths.size())) * 47)); wellLayout->addWidget(scroll); estimateGroup->addWidget(well);
    m_estimateWarning = label({}, "warn", true); m_estimateWarning->setObjectName("discord-estimate-warning"); m_estimateWarning->hide(); estimateGroup->addWidget(m_estimateWarning); layout->addLayout(estimateGroup);

    auto* codecGroup = new QVBoxLayout; codecGroup->setSpacing(4); codecGroup->addWidget(caption(QStringLiteral("Compression"))); codecGroup->addSpacing(4);
    const QStringList names{QStringLiteral("H.264"), QStringLiteral("HEVC"), QStringLiteral("AV1")}, details{QStringLiteral("compatible partout, rapide"), QStringLiteral("plus compact, utilise le GPU"), QStringLiteral("meilleure image, plus lent")};
    auto* codecHost = new QWidget; auto* codecLayout = new QVBoxLayout(codecHost); codecLayout->setContentsMargins(0, 0, 0, 0); codecLayout->setSpacing(4);
    for (int index = 0; index < 3; ++index) {
        auto* row = new RadioRow(names[index], details[index], codecHost); row->setObjectName(QStringLiteral("discord-codec-%1").arg(codecs[index])); row->setChecked(m_defaults.codec == QLatin1String(codecs[index])); codecLayout->addWidget(row); m_codecs.append(row);
        connect(row, &QPushButton::clicked, this, [this] { updateEstimates(); });
    }
    if (std::none_of(m_codecs.begin(), m_codecs.end(), [](const QPushButton* row) { return row->isChecked(); })) m_codecs.first()->setChecked(true);
    codecGroup->addWidget(codecHost); layout->addLayout(codecGroup);
    auto* resizeGroup = new QVBoxLayout; resizeGroup->setSpacing(2); m_resize = new Toggle(QStringLiteral("Réduire la résolution si nécessaire")); m_resize->setObjectName("discord-auto-resize"); m_resize->setFont(uiFont(13, QFont::DemiBold)); m_resize->setChecked(m_defaults.autoResize);
    auto* resizeHint = label(QStringLiteral("Les FPS sont conservés. Le son est réuni en une piste."), "hint"); resizeHint->setContentsMargins(46, 0, 0, 0); resizeGroup->addWidget(m_resize); resizeGroup->addWidget(resizeHint); layout->addLayout(resizeGroup);
    connect(m_resize, &QCheckBox::toggled, this, [this] { updateEstimates(); });
    m_stages->addWidget(page);
}
void DiscordDialog::buildProgress() {
    auto* page = new QWidget; auto* layout = new QVBoxLayout(page); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(12);
    auto* well = card("well"); auto* wellLayout = new QVBoxLayout(well); wellLayout->setContentsMargins(1, 1, 1, 1);
    auto* rows = new QWidget; m_jobs = new QVBoxLayout(rows); m_jobs->setContentsMargins(0, 0, 0, 0); m_jobs->setSpacing(0); m_jobs->addStretch();
    auto* scroll = new QScrollArea; scroll->setWidgetResizable(true); scroll->setWidget(rows); scroll->setFrameShape(QFrame::NoFrame); scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); scroll->setFixedHeight(65); m_jobScroll = scroll;
    wellLayout->addWidget(scroll); layout->addWidget(well, 1);
    auto* hint = new QHBoxLayout; hint->setSpacing(8); hint->addWidget(new IconLabel(QStringLiteral("info"), tk::text2, 14)); hint->addWidget(label(QStringLiteral("Une copie prête se glisse déjà vers Discord depuis cette liste."), "hint"), 1); layout->addLayout(hint);
    m_stages->addWidget(page);
}
void DiscordDialog::showStage(bool progress) {
    m_progressStage = progress; m_stages->setCurrentIndex(progress ? 1 : 0); m_overall->setVisible(progress);
    m_footerNote->setVisible(!progress); m_cancel->setVisible(!progress); m_start->setVisible(!progress); m_cancelBatch->setVisible(progress); m_background->setVisible(progress); m_openSharing->setVisible(progress);
    // Each face has its own height; the hidden one must not hold the dialog open.
    m_stages->widget(0)->setSizePolicy(progress ? QSizePolicy::Ignored : QSizePolicy::Preferred, progress ? QSizePolicy::Ignored : QSizePolicy::Preferred);
    m_stages->widget(1)->setSizePolicy(progress ? QSizePolicy::Preferred : QSizePolicy::Ignored, progress ? QSizePolicy::Preferred : QSizePolicy::Ignored);
    if (progress) refreshJobs();
    else {
        qint64 total = 0; for (const auto& path : m_paths) total += QFileInfo(path).size();
        setHeadingTexts(QStringLiteral("Préparer pour Discord"), m_edited ? QStringLiteral("L'édition est d'abord enregistrée en haute qualité à côté de l'original, puis préparée pour Discord.")
            : QStringLiteral("%1 clip%2 · %3 au total. Les originaux ne sont pas modifiés.").arg(m_paths.size()).arg(m_paths.size() > 1 ? "s" : "").arg(formatBytes(total)));
        m_start->setText(m_paths.size() > 1 ? QStringLiteral("Préparer %1 copies").arg(m_paths.size()) : QStringLiteral("Préparer la copie"));
    }
    refit();
}
qint64 DiscordDialog::limitBytes() const { for (int index = 0; index < 5; ++index) if (m_limits[index]->isChecked()) return qint64(limits[index].megabytes) * 1000000; return qRound64(m_limit->value() * 1000000); }
QString DiscordDialog::codec() const { for (int index = 0; index < 3; ++index) if (m_codecs[index]->isChecked()) return QString::fromLatin1(codecs[index]); return QStringLiteral("h264"); }
void DiscordDialog::updateEstimates() {
    ClipExportOptions options; options.maxBytes = limitBytes(); options.codec = codec(); options.autoResize = m_resize->isChecked(); QString warning; double worst = 1;
    for (int index = 0; index < m_estimateTexts.size(); ++index) {
        auto* figures = static_cast<ElideLabel*>(m_estimateTexts[index]); auto* tag = m_estimateTags[index]; const qint64 bytes = QFileInfo(m_paths[index]).size();
        if (index >= m_infos.size()) { figures->setFullText(formatBytes(bytes)); tag->set(QStringLiteral("Analyse…"), Tag::Neutral); continue; }
        if (!m_infos[index].error.isEmpty()) { figures->setFullText(firstLine(m_infos[index].error)); tag->set(QStringLiteral("Illisible"), Tag::Err); tag->setIconName({}); continue; }
        const auto& info = m_infos[index].info; auto edit = m_edited && m_editedInfo ? *m_edited : defaultEdit(info); edit.separateTracks = false;
        // An edited clip is exported in high quality first; only that step decides whether a plain copy will do.
        const auto plan = planSizedExport(info, edit, m_edited ? options.maxBytes + 1 : bytes, options); tag->setIconName({});
        if (plan.copyVideo) { figures->setFullText(formatBytes(bytes)); tag->set(QStringLiteral("Copié tel quel"), Tag::Ok); continue; }
        if (!plan.feasible) { figures->setFullText(QStringLiteral("%1 · trop long pour cette limite").arg(formatBytes(bytes))); tag->set(QStringLiteral("Impossible"), Tag::Err); continue; }
        const double scale = plan.outputSize.height() / double(std::max(1, edit.crop.height())), density = plan.videoBitrate / (std::max(1.0, info.fps) * plan.outputSize.width() * plan.outputSize.height());
        const bool degraded = scale < 0.5 || density < (options.codec == "h264" ? 0.0225 : 0.015);
        figures->setFullText(QStringLiteral("%1 → ≈ %2 · %3p %4 FPS").arg(formatBytes(bytes), formatBytes(plan.estimatedBytes)).arg(plan.outputSize.height()).arg(qRound(info.fps)));
        if (degraded) { tag->set(QStringLiteral("Image dégradée"), Tag::Warn); tag->setIconName(QStringLiteral("warn")); } else tag->set(QStringLiteral("Réencodé"), Tag::Neutral);
        if (degraded && scale <= worst) {
            worst = scale; qint64 bigger = 0; for (const auto& limit : limits) if (!bigger && qint64(limit.megabytes) * 1000000 > options.maxBytes) bigger = qint64(limit.megabytes) * 1000000;
            warning = QStringLiteral("%1 tiennent mal dans %2 : coupez le clip%3.").arg(roughDuration(edit.endMs - edit.startMs), formatLimit(options.maxBytes), bigger ? QStringLiteral(" ou visez %1").arg(formatLimit(bigger)) : QString());
        }
    }
    m_estimateWarning->setText(warning); m_estimateWarning->setVisible(!warning.isEmpty());
}
void DiscordDialog::begin() {
    if (m_batch->running() || m_paths.isEmpty()) return;
    ShareOptions options = m_defaults; options.maxBytes = limitBytes(); options.codec = codec(); options.autoResize = m_resize->isChecked();
    emit preferencesChosen(options.maxBytes, options.codec, options.autoResize);
    m_batch->start(m_paths, options, m_editedInfo, m_edited); showStage(true);
}
void DiscordDialog::refreshJobs() {
    const auto& jobs = m_batch->jobs(); QList<int> shown; for (int index = 0; index < jobs.size(); ++index) if (jobs[index].batch == m_batch->batch()) shown.append(index);
    if (m_jobBatch != m_batch->batch() || m_jobRows.size() != shown.size()) {
        m_jobBatch = m_batch->batch(); for (auto* row : m_jobRows) { row->hide(); row->deleteLater(); } m_jobRows.clear();
        while (m_jobs->count() > 1) { auto* child = m_jobs->takeAt(0); if (child->widget()) child->widget()->deleteLater(); delete child; }
        for (int position = 0; position < shown.size(); ++position) {
            if (position) { auto* line = separator(); line->setStyleSheet(QStringLiteral("background: #1E242C;")); m_jobs->insertWidget(m_jobs->count() - 1, line); }
            auto* row = new JobRow(m_batch, [this](const QString& path) { emit copyRequested({path}); }); m_jobs->insertWidget(m_jobs->count() - 1, row); m_jobRows.append(row);
        }
        m_jobScroll->setFixedHeight(int(std::clamp<qsizetype>(shown.size(), 1, 5)) * 65); refit();
    }
    const int current = m_batch->current(), remaining = m_batch->remainingSeconds();
    for (int position = 0; position < shown.size(); ++position) static_cast<JobRow*>(m_jobRows[position])->apply(shown[position], jobs[shown[position]], shown[position] == current ? remaining : -1);
    const int ready = m_batch->count(ShareJob::Ready), failed = m_batch->count(ShareJob::Failed), size = int(shown.size()); const bool running = m_batch->running();
    m_overall->setValue(m_batch->percent() / 100.0); m_cancelBatch->setVisible(running); m_background->setText(running ? QStringLiteral("Continuer en arrière-plan") : QStringLiteral("Fermer"));
    if (running) {
        int position = 0; for (const int index : shown) if (index <= current) ++position;
        const auto name = current >= 0 && current < jobs.size() ? jobs[current].name : QString(); const auto stage = current >= 0 && current < jobs.size() ? jobs[current].stage : QString();
        setHeadingTexts(size > 1 ? QStringLiteral("Préparation de %1 copies").arg(size) : QStringLiteral("Préparation de la copie"), QStringLiteral("Clip %1 sur %2 · %3%4").arg(std::max(1, position)).arg(size).arg(name, stage.isEmpty() ? QString() : QStringLiteral(" · ") + stage));
    } else {
        const auto summary = failed ? QStringLiteral("%1 prête%2, %3 échec%4").arg(ready).arg(ready > 1 ? "s" : "").arg(failed).arg(failed > 1 ? "s" : "") : ready > 1 ? QStringLiteral("%1 copies prêtes").arg(ready) : ready == 1 ? QStringLiteral("Copie prête") : QStringLiteral("Aucune copie préparée");
        setHeadingTexts(summary, ready > 1 ? QStringLiteral("Glissez-les vers Discord depuis cette liste ou depuis Partage Discord.") : ready ? QStringLiteral("Glissez-la vers Discord depuis cette liste ou depuis Partage Discord.") : QStringLiteral("Le lot s'est arrêté avant de produire une copie."));
    }
}
}
