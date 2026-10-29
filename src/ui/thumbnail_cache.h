#pragma once
#include "media/clip_edit.h"
#include <QCache>
#include <QDateTime>
#include <QFutureWatcher>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QQueue>
#include <QSet>
#include <QTimer>
#include <atomic>
#include <memory>
#include <optional>
namespace clipsapp {
struct ClipPreview { QString key; QImage image; ClipInfo info; QString error; bool stored = false; };
// A clip that could not be read is tried again a little later, a few times: it may still have been on its way.
struct ClipMeta {
    qint64 durationMs = 0; QSize size; double fps = 0; int tracks = 0; bool failed = false; int attempts = 0; qint64 failedAt = 0;
    // Unreadable for good, after its last try: only then is it said.
    bool gaveUp() const { return failed && attempts >= 3; }
};
// 128 x 72 previews, read one at a time and only for what is on screen. With a storage folder,
// each preview and what was read of its clip are kept there, and a clip is read only once.
class ThumbnailCache final : public QObject {
    Q_OBJECT
public:
    explicit ThumbnailCache(QObject* parent = nullptr);
    ~ThumbnailCache() override;
    static QString keyFor(const QString& path, qint64 size, const QDateTime& modified);
    static QString keyFor(const QString& path);
    // Loads what an earlier launch kept, then forgets in the background what belongs to clips that are gone or changed.
    void setStorage(const QString& directory);
    // A preview kept on disk is read here, at once: a row shows it the first time it is painted.
    const ClipPreview* preview(const QString& key);
    // Kept after the image leaves the cache, so a row never loses its text.
    const ClipMeta* meta(const QString& key) const { const auto it = m_meta.constFind(key); return it == m_meta.constEnd() ? nullptr : &it.value(); }
    void request(const QString& key, const QString& path);
    // Whether Clipsapp already compressed a clip, once it is known: kept on disk with the previews.
    std::optional<bool> compressed(const QString& key) const { const auto it = m_marks.constFind(key); return it == m_marks.constEnd() ? std::nullopt : std::optional<bool>(it.value()); }
    // Finds it out in the background for the clips (key, path) not known yet; marksReady() follows.
    void requestMarks(const QList<QPair<QString, QString>>& clips);
    void setActive(bool active);
    QFuture<ClipPreview> pending() const { return m_busy ? m_worker.future() : QFuture<ClipPreview>{}; }
    // Writes what is not on disk yet; otherwise done a few seconds after the last change.
    void save();
signals:
    void ready(const QString& key);
    void marksReady();
private:
    void startNext();
    void startMarks();
    QString imagePath(const QString& key) const;
    QCache<QString, ClipPreview> m_cache{4096}; // KiB
    QHash<QString, ClipMeta> m_meta;
    QQueue<QPair<QString, QString>> m_queue;
    QSet<QString> m_pending;
    QFutureWatcher<ClipPreview> m_worker;
    std::shared_ptr<std::atomic_bool> m_stop;
    bool m_active = true;
    bool m_busy = false;
    QString m_storage;
    // The keys whose preview is on disk, and the writing of the index, a little after the last change.
    QSet<QString> m_stored;
    QTimer m_saveLater;
    bool m_unsaved = false;
    QFutureWatcher<QStringList> m_prune;
    QHash<QString, bool> m_marks; QList<QPair<QString, QString>> m_markQueue; QSet<QString> m_markFlight;
    QFutureWatcher<QHash<QString, bool>> m_marking; std::shared_ptr<std::atomic_bool> m_markStop = std::make_shared<std::atomic_bool>(false);
};
}
