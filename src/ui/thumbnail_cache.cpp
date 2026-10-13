#include "thumbnail_cache.h"
#include "media/clip_reader.h"
#include <QFileInfo>
#include <QtConcurrent/QtConcurrentRun>
namespace clipsapp {
ThumbnailCache::ThumbnailCache(QObject* parent) : QObject(parent) {
    connect(&m_worker, &QFutureWatcher<ClipPreview>::finished, this, [this] {
        m_busy = false;
        const auto result = m_worker.result(); m_pending.remove(result.key);
        if (m_stop && !m_stop->load()) {
            if (m_meta.size() >= 20000) m_meta.clear();
            m_meta.insert(result.key, {result.info.durationMs, result.info.size, result.info.fps, int(result.info.audio.size()), result.image.isNull()});
            const auto cost = std::max(1, int(result.image.sizeInBytes() / 1024)); m_cache.insert(result.key, new ClipPreview(result), cost);
            emit ready(result.key);
        }
        startNext();
    });
}
ThumbnailCache::~ThumbnailCache() { if (m_stop) *m_stop = true; disconnect(&m_worker, nullptr, this, nullptr); }
QString ThumbnailCache::keyFor(const QString& path, qint64 size, const QDateTime& modified) { return path + QString("|%1|%2").arg(size).arg(modified.toMSecsSinceEpoch()); }
QString ThumbnailCache::keyFor(const QString& path) { const QFileInfo file(path); return keyFor(path, file.size(), file.lastModified()); }
void ThumbnailCache::setActive(bool active) {
    m_active = active;
    if (!active) { if (m_stop) *m_stop = true; m_queue.clear(); m_pending.clear(); }
    else startNext();
}
void ThumbnailCache::request(const QString& key, const QString& path) {
    if (!m_active || m_cache.contains(key) || m_pending.contains(key) || m_pending.size() >= 32) return;
    if (const auto* known = meta(key); known && known->failed) return;
    m_pending.insert(key); m_queue.enqueue({key, path}); startNext();
}
void ThumbnailCache::startNext() {
    if (!m_active || m_busy || m_queue.isEmpty()) return;
    m_busy = true;
    const auto job = m_queue.dequeue(); m_stop = std::make_shared<std::atomic_bool>(false); const auto stop = m_stop;
    m_worker.setFuture(QtConcurrent::run([job, stop] {
        ClipPreview result; result.key = job.first;
        try {
            // Short, infrequent previews do not allocate another GPU decoder
            // while the replay is using the GPU. Only one job runs at a time.
            ClipReader reader(job.second, false); result.info = reader.info();
            const auto time = std::min<qint64>(500, result.info.durationMs / 10); reader.seek(time); reader.readUntil(time + 1, *stop, true);
            if (!*stop) result.image = reader.frameAt(time).scaled({128, 72}, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        } catch (const std::exception& error) { result.error = QString::fromUtf8(error.what()); }
        return result;
    }));
}
}
