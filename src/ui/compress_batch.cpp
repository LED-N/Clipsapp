#include "compress_batch.h"
#include "media/clip_reader.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QThread>
#include <QUuid>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
namespace clipsapp {
namespace {
std::pair<ClipInfo, QString> probe(const QString& path) {
    try { ClipReader reader(path, false, true); return {reader.info(), {}}; } catch (const std::exception& error) { return {{}, QString::fromUtf8(error.what())}; }
}
}
CompressReplacement replaceWithCompressed(const QString& original, const QString& compressed, bool permanent, const ClipTrashOperation& trash) {
    const QFileInfo source(original); const auto modified = source.lastModified(), born = source.birthTime();
    const auto target = source.absoluteDir().filePath(source.completeBaseName() + QLatin1Char('.') + compressedSuffix(original));
    // A clip whose container changes (WebM, AVI) never takes the name of another file.
    if (target.compare(source.absoluteFilePath(), Qt::CaseInsensitive) != 0 && QFileInfo::exists(target)) {
        QFile::remove(compressed); return {{}, QStringLiteral("Un autre fichier s'appelle déjà « %1 » : l'original est gardé.").arg(QFileInfo(target).fileName())};
    }
    // The original goes first, so that its place is free; the trash remembers where it was.
    const auto removed = trashClips({source.absoluteFilePath()}, permanent ? permanentDeletion() : trash);
    if (removed.removed.isEmpty()) {
        QFile::remove(compressed);
        return {{}, (permanent ? QStringLiteral("L'original n'a pas pu être supprimé, il est gardé tel quel. ") : QStringLiteral("L'original n'a pas pu être mis à la corbeille, il est gardé tel quel. ")) + removed.errors.value(0)};
    }
    const auto keepDate = [&](const QString& path) {
        QFile file(path); if (!file.open(QIODevice::ReadWrite)) return;
        file.setFileTime(modified, QFileDevice::FileModificationTime); if (born.isValid()) file.setFileTime(born, QFileDevice::FileBirthTime);
    };
    for (int attempt = 0; attempt < 6; ++attempt) {
        if (QFile::rename(compressed, target)) { keepDate(target); return {target, {}}; }
        QThread::msleep(100);
    }
    // Its place stays taken: the compressed file is kept under a name beside it rather than lost.
    QString fallback; for (int index = 1; index < 100 && (fallback.isEmpty() || QFileInfo::exists(fallback)); ++index)
        fallback = source.absoluteDir().filePath(QStringLiteral("%1 (compressé%2).%3").arg(source.completeBaseName(), index > 1 ? QStringLiteral(" %1").arg(index) : QString(), compressedSuffix(original)));
    if (QFile::rename(compressed, fallback)) { keepDate(fallback); return {fallback, QStringLiteral("Le clip compressé n'a pas pu prendre le nom de l'original : il s'appelle « %1 ».").arg(QFileInfo(fallback).fileName())}; }
    return {{}, QStringLiteral("Le clip compressé n'a pas pu être mis en place ; il reste dans le dossier sous le nom « %1 ».").arg(QFileInfo(compressed).fileName())};
}
CompressBatch::CompressBatch(QObject* parent, ClipTrashOperation trash) : QObject(parent), m_trash(std::move(trash)), m_compressor(this) {
    connect(&m_compressor, &ClipCompressor::progress, this, [this](int value) { if (m_index >= 0 && m_index < m_jobs.size()) { m_jobs[m_index].progress = value; emit changed(); } });
    connect(&m_compressor, &ClipCompressor::stageChanged, this, [this](const QString& stage) { if (m_index >= 0 && m_index < m_jobs.size()) { m_jobs[m_index].stage = stage; emit changed(); } });
    connect(&m_compressor, &ClipCompressor::failed, this, &CompressBatch::fail);
    connect(&m_compressor, &ClipCompressor::cancelled, this, &CompressBatch::finishBatch);
    connect(&m_compressor, &ClipCompressor::completed, this, [this](const QString& path) {
        auto& job = m_jobs[m_index]; job.output = path; job.stage = QStringLiteral("Vérification du clip compressé"); emit changed();
        m_check.setFuture(QtConcurrent::run([path] { return probe(path); }));
    });
    connect(&m_probe, &QFutureWatcher<std::pair<ClipInfo, QString>>::finished, this, [this] {
        if (m_cancelled) { finishBatch(); return; }
        const auto [info, error] = m_probe.result(); auto& job = m_jobs[m_index];
        if (!error.isEmpty()) { fail(error); return; }
        // Compressed once already: a second pass would lose a little more for little room, unless it was asked for.
        if (info.compressed && !m_options.again) { job.state = CompressJob::Skipped; job.note = QStringLiteral("Déjà compressé par Clipsapp"); next(); return; }
        m_sourceInfo = info; compress(info);
    });
    connect(&m_check, &QFutureWatcher<std::pair<ClipInfo, QString>>::finished, this, [this] { const auto [info, error] = m_check.result(); checked(info, error); });
    connect(&m_replacing, &QFutureWatcher<CompressReplacement>::finished, this, [this] {
        const auto result = m_replacing.result(); const int index = std::exchange(m_replacingIndex, -1);
        if (index >= 0 && index < m_jobs.size()) {
            auto& job = m_jobs[index];
            if (result.path.isEmpty()) { job.state = CompressJob::Failed; job.error = result.error; job.output.clear(); }
            else { job.state = CompressJob::Replaced; job.path = result.path; job.output.clear(); job.after = QFileInfo(result.path).size(); job.note = result.error; emit replaced(result.path, job.source); }
        }
        emit changed();
        if (m_running) next();
    });
}
CompressBatch::~CompressBatch() {
    disconnect(&m_compressor, nullptr, this, nullptr); m_probe.waitForFinished(); m_check.waitForFinished(); m_replacing.waitForFinished();
    // A compressed file nobody chose stays out of the library.
    for (const auto& job : m_jobs) if (job.state == CompressJob::Review && !job.output.isEmpty()) QFile::remove(job.output);
}
int CompressBatch::count(CompressJob::State state) const { return int(std::count_if(m_jobs.begin(), m_jobs.end(), [state](const CompressJob& job) { return job.state == state; })); }
int CompressBatch::percent() const {
    if (m_jobs.isEmpty()) return 0;
    int total = 0; for (const auto& job : m_jobs) total += job.state == CompressJob::Pending ? 0 : job.state == CompressJob::Running ? job.progress : 100;
    return total / int(m_jobs.size());
}
int CompressBatch::remainingSeconds() const {
    if (!m_running || m_index < 0 || m_index >= m_jobs.size() || m_jobs[m_index].progress < 3) return -1;
    return int(m_clock.elapsed() * (100 - m_jobs[m_index].progress) / m_jobs[m_index].progress / 1000);
}
qint64 CompressBatch::savedBytes() const { qint64 saved = 0; for (const auto& job : m_jobs) if (job.state == CompressJob::Replaced) saved += job.before - job.after; return saved; }
void CompressBatch::start(const QStringList& paths, const CompressOptions& options) {
    if (busy() || paths.isEmpty()) return;
    for (const auto& job : m_jobs) if (job.state == CompressJob::Review && !job.output.isEmpty()) QFile::remove(job.output);
    m_jobs.clear(); m_options = options;
    for (const auto& path : paths) { CompressJob job; job.source = QFileInfo(path).absoluteFilePath(); job.name = QFileInfo(path).completeBaseName(); job.before = QFileInfo(path).size(); m_jobs.append(job); }
    m_running = true; m_cancelled = false; m_index = -1; emit runningChanged(true); next();
}
void CompressBatch::next() {
    m_index = -1;
    if (!m_cancelled) for (int index = 0; index < m_jobs.size(); ++index) if (m_jobs[index].state == CompressJob::Pending) { m_index = index; break; }
    if (m_index < 0) { finishBatch(); return; }
    auto& job = m_jobs[m_index]; job.state = CompressJob::Running; job.progress = 0; job.stage = QStringLiteral("Lecture du clip"); job.before = QFileInfo(job.source).size(); m_clock.restart(); emit changed();
    const auto path = job.source; m_probe.setFuture(QtConcurrent::run([path] { return probe(path); }));
}
void CompressBatch::compress(const ClipInfo& info) {
    const auto destination = QFileInfo(info.path).absoluteDir().filePath(QStringLiteral(".clipsapp-review-%1.part").arg(QUuid::createUuid().toString(QUuid::Id128).left(12)));
    QString error; if (!m_compressor.start(info, destination, m_options.settings, error)) fail(error);
}
void CompressBatch::checked(const ClipInfo& info, const QString& error) {
    auto& job = m_jobs[m_index];
    if (m_cancelled) { QFile::remove(job.output); job.output.clear(); finishBatch(); return; }
    const auto drop = [&](CompressJob::State state, const QString& why) { QFile::remove(job.output); job.output.clear(); job.state = state; (state == CompressJob::Failed ? job.error : job.note) = why; next(); };
    if (!error.isEmpty()) { drop(CompressJob::Failed, QStringLiteral("Le clip compressé est illisible : ") + error); return; }
    // The same length and every track, or the original stays: nothing is lost on the way.
    const auto& before = m_sourceInfo; const qint64 tolerance = std::max<qint64>(500, before.durationMs / 50);
    if (std::abs(info.durationMs - before.durationMs) > tolerance || info.audio.size() != before.audio.size() || info.size != before.size) { drop(CompressJob::Failed, QStringLiteral("Le clip compressé ne correspond pas à l'original : il est écarté.")); return; }
    job.after = QFileInfo(job.output).size();
    // Below a twentieth of gain, the original is already compact: it stays as it is.
    if (job.after > job.before * 0.95) { drop(CompressJob::Kept, QStringLiteral("Déjà compact : rien à gagner")); return; }
    if (m_options.review) { job.state = CompressJob::Review; job.progress = 100; emit changed(); next(); return; }
    replaceNow(m_index, m_options.permanent);
}
void CompressBatch::replace(int index, bool permanent) { if (index >= 0 && index < m_jobs.size() && m_jobs[index].state == CompressJob::Review && !m_replacing.isRunning()) replaceNow(index, permanent); }
void CompressBatch::discard(int index) {
    if (index < 0 || index >= m_jobs.size() || m_jobs[index].state != CompressJob::Review) return;
    auto& job = m_jobs[index]; QFile::remove(job.output); job.output.clear(); job.state = CompressJob::Kept; job.note = QStringLiteral("Original gardé"); emit changed();
}
void CompressBatch::replaceNow(int index, bool permanent) {
    auto& job = m_jobs[index]; job.stage = QStringLiteral("Remplacement de l'original"); job.permanent = permanent; emit changed();
    if (m_release) m_release(job.source);
    m_replacingIndex = index; const auto source = job.source, output = job.output; const auto trash = m_trash;
    m_replacing.setFuture(QtConcurrent::run([source, output, permanent, trash] { return replaceWithCompressed(source, output, permanent, trash); }));
}
void CompressBatch::fail(const QString& error) {
    if (m_index >= 0 && m_index < m_jobs.size()) { auto& job = m_jobs[m_index]; job.state = CompressJob::Failed; job.error = error; if (!job.output.isEmpty()) QFile::remove(job.output); job.output.clear(); }
    next();
}
void CompressBatch::finishBatch() {
    for (auto& job : m_jobs) if (job.state == CompressJob::Pending || job.state == CompressJob::Running) job.state = CompressJob::Cancelled;
    const bool was = m_running; m_running = false; m_index = -1; emit changed();
    if (was) { emit runningChanged(false); emit finished(count(CompressJob::Replaced), count(CompressJob::Failed), m_cancelled); }
}
void CompressBatch::cancel() {
    if (!m_running) return;
    m_cancelled = true;
    // The compressor answers with cancelled(); a reading or a check is caught as it ends, a file being replaced is finished first.
    if (m_compressor.running()) m_compressor.cancel();
    else if (!m_probe.isRunning() && !m_check.isRunning() && !m_replacing.isRunning()) finishBatch();
    emit changed();
}
}
