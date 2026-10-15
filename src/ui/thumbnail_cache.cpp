#include "thumbnail_cache.h"
#include "media/clip_reader.h"
#include "core/clip_library.h"
#include <QImageReader>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QtConcurrent/QtConcurrentRun>
namespace clipsapp {
namespace {
QString indexPath(const QString& storage) { return QDir(storage).filePath(QStringLiteral("index.json")); }
QString imageName(const QString& key) { return QString::fromLatin1(QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha1).toHex()) + QStringLiteral(".jpg"); }
// The clip a key stands for, with the size and the date it had when it was read.
bool stillTheSame(const QString& key) {
    const int last = key.lastIndexOf('|'), before = last > 0 ? key.lastIndexOf('|', last - 1) : -1; if (before < 0) return false;
    const QFileInfo file(key.left(before));
    return file.isFile() && file.size() == key.mid(before + 1, last - before - 1).toLongLong() && file.lastModified().toMSecsSinceEpoch() == key.mid(last + 1).toLongLong();
}
}
ThumbnailCache::ThumbnailCache(QObject* parent) : QObject(parent) {
    m_saveLater.setSingleShot(true); m_saveLater.setInterval(3000); connect(&m_saveLater, &QTimer::timeout, this, &ThumbnailCache::save);
    connect(&m_worker, &QFutureWatcher<ClipPreview>::finished, this, [this] {
        m_busy = false;
        const auto result = m_worker.result(); m_pending.remove(result.key);
        if (m_stop && !m_stop->load()) {
            if (m_meta.size() >= 20000) { m_meta.clear(); m_stored.clear(); }
            m_meta.insert(result.key, {result.info.durationMs, result.info.size, result.info.fps, int(result.info.audio.size()), result.image.isNull()});
            const auto cost = std::max(1, int(result.image.sizeInBytes() / 1024)); m_cache.insert(result.key, new ClipPreview(result), cost);
            if (result.stored) m_stored.insert(result.key);
            // A clip that could not be read is tried again at the next launch: it may have been still on its way.
            if (!result.image.isNull() && !m_storage.isEmpty()) { m_unsaved = true; m_saveLater.start(); }
            emit ready(result.key);
        }
        startNext();
    });
    connect(&m_prune, &QFutureWatcher<QStringList>::finished, this, [this] {
        const auto gone = m_prune.result();
        for (const auto& key : gone) { m_meta.remove(key); m_stored.remove(key); m_cache.remove(key); }
        if (!gone.isEmpty()) { m_unsaved = true; m_saveLater.start(); }
    });
}
ThumbnailCache::~ThumbnailCache() { save(); if (m_stop) *m_stop = true; disconnect(&m_worker, nullptr, this, nullptr); }
QString ThumbnailCache::keyFor(const QString& path, qint64 size, const QDateTime& modified) { return path + QString("|%1|%2").arg(size).arg(modified.toMSecsSinceEpoch()); }
QString ThumbnailCache::keyFor(const QString& path) { const QFileInfo file(path); return keyFor(path, file.size(), file.lastModified()); }
QString ThumbnailCache::imagePath(const QString& key) const { return QDir(m_storage).filePath(imageName(key)); }
void ThumbnailCache::setStorage(const QString& directory) {
    m_storage = directory;
    if (m_storage.isEmpty() || !QDir().mkpath(m_storage)) { m_storage.clear(); return; }
    QFile file(indexPath(m_storage));
    if (file.open(QIODevice::ReadOnly) && file.size() < 64 * 1024 * 1024) {
        const auto document = QJsonDocument::fromJson(file.readAll());
        const auto clips = document.object().value("version").toInt() == 1 ? document.object().value("clips").toObject() : QJsonObject();
        for (auto it = clips.begin(); it != clips.end() && m_meta.size() < 20000; ++it) {
            const auto entry = it.value().toObject(); if (m_meta.contains(it.key())) continue;
            m_meta.insert(it.key(), {entry.value("duration").toInteger(), {entry.value("width").toInt(), entry.value("height").toInt()}, entry.value("fps").toDouble(), entry.value("tracks").toInt(), false});
            if (entry.value("image").toBool()) m_stored.insert(it.key());
        }
    }
    // What belongs to a clip deleted, renamed or changed since is forgotten, away from the interface.
    // A preview written after this point is never taken for a leftover.
    const auto keys = m_meta.keys(); const auto storage = m_storage; const auto since = QDateTime::currentDateTime();
    m_prune.setFuture(QtConcurrent::run([keys, storage, since] {
        QStringList gone; QSet<QString> kept;
        for (const auto& key : keys) { if (stillTheSame(key)) kept.insert(imageName(key)); else { gone.append(key); QFile::remove(QDir(storage).filePath(imageName(key))); } }
        for (const auto& image : QDir(storage).entryInfoList({QStringLiteral("*.jpg")}, QDir::Files))
            if (!kept.contains(image.fileName()) && image.lastModified() < since) QFile::remove(image.absoluteFilePath());
        return gone;
    }));
}
const ClipPreview* ThumbnailCache::preview(const QString& key) {
    if (const auto* known = m_cache.object(key)) return known;
    if (!m_stored.contains(key)) return nullptr;
    const QImage image(imagePath(key));
    if (image.isNull()) { m_stored.remove(key); m_unsaved = true; m_saveLater.start(); return nullptr; }
    m_cache.insert(key, new ClipPreview{key, image, {}, {}, true}, std::max(1, int(image.sizeInBytes() / 1024)));
    return m_cache.object(key);
}
void ThumbnailCache::save() {
    m_saveLater.stop();
    if (!m_unsaved || m_storage.isEmpty()) return;
    QJsonObject clips;
    for (auto it = m_meta.cbegin(); it != m_meta.cend(); ++it) {
        if (it.value().failed) continue;
        const auto& meta = it.value();
        clips.insert(it.key(), QJsonObject{{"duration", meta.durationMs}, {"width", meta.size.width()}, {"height", meta.size.height()}, {"fps", meta.fps}, {"tracks", meta.tracks}, {"image", m_stored.contains(it.key())}});
    }
    QSaveFile file(indexPath(m_storage)); const auto bytes = QJsonDocument(QJsonObject{{"version", 1}, {"clips", clips}}).toJson(QJsonDocument::Compact);
    if (file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit()) m_unsaved = false;
    else qWarning("Miniatures : index non enregistré (%s).", qUtf8Printable(file.errorString()));
}
void ThumbnailCache::setActive(bool active) {
    m_active = active;
    if (!active) { if (m_stop) *m_stop = true; m_queue.clear(); m_pending.clear(); }
    else startNext();
}
void ThumbnailCache::request(const QString& key, const QString& path) {
    if (!m_active || m_cache.contains(key) || m_stored.contains(key) || m_pending.contains(key) || m_pending.size() >= 32) return;
    if (const auto* known = meta(key); known && known->failed) return;
    m_pending.insert(key); m_queue.enqueue({key, path}); startNext();
}
void ThumbnailCache::startNext() {
    if (!m_active || m_busy || m_queue.isEmpty()) return;
    m_busy = true;
    const auto job = m_queue.dequeue(); m_stop = std::make_shared<std::atomic_bool>(false); const auto stop = m_stop;
    const auto file = m_storage.isEmpty() ? QString() : imagePath(job.first);
    m_worker.setFuture(QtConcurrent::run([job, stop, file] {
        ClipPreview result; result.key = job.first;
        if (isImageFile(job.second)) {
            // A picture is read straight at the size of its preview; its own size is what the row tells.
            QImageReader reader(job.second); reader.setAutoTransform(true); result.info.path = job.second; result.info.size = reader.size();
            if (result.info.size.isValid()) reader.setScaledSize(result.info.size.scaled(128, 72, Qt::KeepAspectRatio).expandedTo({1, 1}));
            result.image = reader.read(); if (result.image.isNull()) result.error = reader.errorString();
        } else try {
            // Short, infrequent previews do not allocate another GPU decoder
            // while the replay is using the GPU. Only one job runs at a time.
            ClipReader reader(job.second, false); result.info = reader.info();
            const auto time = std::min<qint64>(500, result.info.durationMs / 10); reader.seek(time); reader.readUntil(time + 1, *stop, true);
            if (!*stop) result.image = reader.frameAt(time).scaled({128, 72}, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        } catch (const std::exception& error) { result.error = QString::fromUtf8(error.what()); }
        // Written here, away from the interface; the index follows once the result is back.
        if (!file.isEmpty() && !result.image.isNull()) result.stored = result.image.save(file, "JPG", 90);
        return result;
    }));
}
}
