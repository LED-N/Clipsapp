#include "discord_batch.h"
#include "media/clip_reader.h"
#include "core/game_identity.h"
#include <QDir>
#include <QFileInfo>
#include <QUuid>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
namespace clipsapp {
DiscordBatch::DiscordBatch(QObject* parent) : QObject(parent), m_exporter(this) {
    connect(&m_exporter, &ClipExporter::progress, this, [this](int value) { if (m_index >= 0 && m_index < m_jobs.size()) { m_jobs[m_index].progress = value; emit changed(); } });
    connect(&m_exporter, &ClipExporter::stageChanged, this, [this](const QString& stage) { if (m_index >= 0 && m_index < m_jobs.size()) { m_jobs[m_index].stage = stage; emit changed(); } });
    connect(&m_exporter, &ClipExporter::failed, this, &DiscordBatch::failCurrent);
    connect(&m_exporter, &ClipExporter::cancelled, this, &DiscordBatch::finishBatch);
    connect(&m_exporter, &ClipExporter::completed, this, [this](const QString& path) {
        auto& job = m_jobs[m_index];
        if (m_highQuality) {
            emit highQualityReady(path, m_info.path, m_edit);
            if (m_cancelled) { finishBatch(); return; }
            if (QFileInfo(path).size() <= m_options.maxBytes) prepare(path, true);
            else compress(m_info, m_edit); // Encode from the original edit, avoiding a second video generation.
        } else {
            job.state = ShareJob::Ready; job.output = path; job.bytes = QFileInfo(path).size(); job.progress = 100;
            emit shareReady(path, job.source, m_options.maxBytes); next();
        }
    });
    connect(&m_prepare, &QFutureWatcher<DiscordPrepared>::finished, this, [this] {
        if (m_cancelled) { finishBatch(); return; }
        const auto prepared = m_prepare.result(); if (!prepared.error.isEmpty()) { failCurrent(prepared.error); return; }
        if (m_preparingHighQuality) compress(prepared.info, defaultEdit(prepared.info, m_options.mixOf(prepared.info.path)));
        else { m_info = prepared.info; m_edit = defaultEdit(m_info, m_options.mixOf(m_info.path)); compress(m_info, m_edit); }
    });
}
DiscordBatch::~DiscordBatch() { disconnect(&m_exporter, nullptr, this, nullptr); disconnect(&m_prepare, nullptr, this, nullptr); }
QString DiscordBatch::uniquePath(const QString& directory, const QString& base, const QString& suffix) const {
    return QDir(directory).filePath(safeGameFolder(base) + '-' + QUuid::createUuid().toString(QUuid::Id128).left(10) + suffix);
}
int DiscordBatch::count(ShareJob::State state, bool thisBatchOnly) const {
    return int(std::count_if(m_jobs.begin(), m_jobs.end(), [&](const ShareJob& job) { return job.state == state && (!thisBatchOnly || job.batch == m_batch); }));
}
int DiscordBatch::batchSize() const { return int(std::count_if(m_jobs.begin(), m_jobs.end(), [this](const ShareJob& job) { return job.batch == m_batch; })); }
int DiscordBatch::percent() const {
    int total = 0, size = 0;
    for (const auto& job : m_jobs) if (job.batch == m_batch) { ++size; total += job.state == ShareJob::Pending ? 0 : job.state == ShareJob::Running ? job.progress : 100; }
    return size ? total / size : 0;
}
int DiscordBatch::remainingSeconds() const {
    if (!m_running || m_index < 0 || m_index >= m_jobs.size() || m_jobs[m_index].progress < 5) return -1;
    return int(m_clock.elapsed() * (100 - m_jobs[m_index].progress) / m_jobs[m_index].progress / 1000);
}
void DiscordBatch::start(const QStringList& paths, const ShareOptions& options, std::optional<ClipInfo> editedInfo, std::optional<ClipEdit> edited) {
    if (m_running || paths.isEmpty()) return;
    // A failed copy stays listed until it is retried or dismissed; finished ones give way.
    m_jobs.erase(std::remove_if(m_jobs.begin(), m_jobs.end(), [](const ShareJob& job) { return job.state != ShareJob::Failed; }), m_jobs.end());
    m_options = options; ++m_batch;
    for (const auto& path : paths) {
        ShareJob job; job.source = path; job.name = QFileInfo(path).completeBaseName(); job.batch = m_batch;
        if (editedInfo && edited) { job.info = editedInfo; job.edit = edited; }
        m_jobs.append(job);
    }
    begin();
}
void DiscordBatch::retry(int index) {
    if (index < 0 || index >= m_jobs.size() || m_jobs[index].state != ShareJob::Failed) return;
    auto& job = m_jobs[index]; job.state = ShareJob::Pending; job.error.clear(); job.progress = 0; job.stage.clear();
    if (m_running) { job.batch = m_batch; emit changed(); return; }
    job.batch = ++m_batch; begin();
}
void DiscordBatch::remove(int index) {
    if (index < 0 || index >= m_jobs.size() || m_jobs[index].state == ShareJob::Running || m_jobs[index].state == ShareJob::Pending) return;
    m_jobs.removeAt(index); if (index < m_index) --m_index; emit changed();
}
void DiscordBatch::begin() {
    if (!QDir().mkpath(m_options.directory)) {
        for (auto& job : m_jobs) if (job.state == ShareJob::Pending) { job.state = ShareJob::Failed; job.error = QStringLiteral("Impossible de créer le dossier de partage."); }
        emit changed(); emit finished(0, count(ShareJob::Failed), false); return;
    }
    m_running = true; m_cancelled = false; m_index = -1; emit runningChanged(true); next();
}
void DiscordBatch::next() {
    m_index = -1;
    if (!m_cancelled) for (int index = 0; index < m_jobs.size(); ++index) if (m_jobs[index].state == ShareJob::Pending) { m_index = index; break; }
    if (m_index < 0) { finishBatch(); return; }
    auto& job = m_jobs[m_index]; job.state = ShareJob::Running; job.progress = 0; job.stage = QStringLiteral("Lecture du clip"); m_clock.restart(); emit changed();
    if (job.info && job.edit) {
        m_info = *job.info; m_edit = *job.edit; m_highQuality = true;
        ClipExportOptions options; options.highQuality = true;
        const QFileInfo original(m_info.path); const auto path = uniquePath(original.absolutePath(), original.completeBaseName(), "-haute-qualité.mp4");
        QString error; if (!m_exporter.start(m_info, m_edit, path, error, {"h264_nvenc", "h264_amf", "libopenh264"}, options)) failCurrent(error);
    } else prepare(job.source);
}
void DiscordBatch::prepare(const QString& path, bool highQuality) {
    m_preparingHighQuality = highQuality;
    m_prepare.setFuture(QtConcurrent::run([path] { DiscordPrepared result; try { ClipReader reader(path, false, true); result.info = reader.info(); } catch (const std::exception& error) { result.error = QString::fromUtf8(error.what()); } return result; }));
}
void DiscordBatch::compress(const ClipInfo& info, ClipEdit edit) {
    m_highQuality = false; edit.separateTracks = false;
    ClipExportOptions options; options.maxBytes = m_options.maxBytes; options.codec = m_options.codec; options.autoResize = m_options.autoResize;
    const QStringList encoders = options.codec == "av1" ? QStringList{"libsvtav1"} : options.codec == "hevc" ? QStringList{"hevc_nvenc", "hevc_amf", "libkvazaar"} : QStringList{"h264_nvenc", "h264_amf", "libopenh264"};
    auto& job = m_jobs[m_index]; const auto plan = planSizedExport(info, edit, QFileInfo(info.path).size(), options);
    job.copied = plan.copyVideo; job.detail = plan.copyVideo ? QString() : QStringLiteral("%1p %2 FPS").arg(plan.outputSize.height()).arg(qRound(info.fps));
    QString error; const auto path = uniquePath(m_options.directory, QFileInfo(job.source).completeBaseName(), ".discord.mp4");
    if (!m_exporter.start(info, edit, path, error, encoders, options)) failCurrent(error);
}
void DiscordBatch::failCurrent(const QString& error) {
    if (m_index >= 0 && m_index < m_jobs.size()) { m_jobs[m_index].state = ShareJob::Failed; m_jobs[m_index].error = error; }
    next();
}
void DiscordBatch::finishBatch() {
    for (auto& job : m_jobs) if (job.batch == m_batch && (job.state == ShareJob::Pending || job.state == ShareJob::Running)) job.state = ShareJob::Cancelled;
    const bool wasRunning = m_running; m_running = false; m_index = -1; emit changed();
    if (wasRunning) { emit runningChanged(false); emit finished(count(ShareJob::Ready), count(ShareJob::Failed), m_cancelled); }
}
void DiscordBatch::cancel() {
    if (!m_running) return;
    m_cancelled = true;
    // The exporter answers with cancelled(); a clip still being read is caught when its reading ends.
    if (m_exporter.running()) m_exporter.cancel(); else if (!m_prepare.isRunning()) finishBatch();
    emit changed();
}
}
