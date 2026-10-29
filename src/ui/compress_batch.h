#pragma once
#include "core/clip_library.h"
#include "media/clip_compressor.h"
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QObject>
#include <functional>
namespace clipsapp {
struct CompressJob {
    // Review: compressed and checked, waiting for the user to compare it and choose. Replaced: the
    // compressed file took the original's place. Kept: the original stays, by choice or for lack of gain.
    enum State { Pending, Running, Review, Replaced, Kept, Skipped, Failed, Cancelled };
    QString source, name, output, path; State state = Pending; int progress = 0; bool permanent = false;
    qint64 before = 0, after = 0; QString stage, note, error;
};
// "review" keeps each compressed file beside its original until replace() or discard(); otherwise it
// takes the original's place as soon as it is checked. "permanent" deletes the originals instead of
// sending them to the trash. "again" compresses also the clips Clipsapp already compressed, which are
// otherwise passed: chosen one by one, after a warning, rather than with a whole library.
struct CompressOptions { CompressSettings settings; bool review = false, permanent = false, again = false; };
struct CompressReplacement { QString path, error; };
// The compressed file takes the place of the original, which goes to the trash (or is deleted): same
// folder, same name unless its container changed, same date, so that it keeps its place in the
// library. "trash" is how the original goes when it is not deleted for good.
CompressReplacement replaceWithCompressed(const QString& original, const QString& compressed, bool permanent, const ClipTrashOperation& trash);
// Compresses clips one after the other, to save room. It belongs to the window: a whole library goes
// on in the background while the window is used for something else.
class CompressBatch final : public QObject {
    Q_OBJECT
public:
    explicit CompressBatch(QObject* parent = nullptr, ClipTrashOperation trash = {});
    ~CompressBatch() override;
    void start(const QStringList& paths, const CompressOptions& options);
    // The clip under way is abandoned, the ones after it are not started; files already replaced stay so.
    void cancel();
    void replace(int index, bool permanent);
    void discard(int index);
    // Called before an original is replaced, on the thread of the window: what still reads it lets go.
    void setRelease(std::function<void(const QString&)> release) { m_release = std::move(release); }
    bool running() const { return m_running; }
    bool busy() const { return m_running || m_replacing.isRunning(); }
    bool replacing() const { return m_replacing.isRunning(); }
    const QList<CompressJob>& jobs() const { return m_jobs; }
    const CompressOptions& options() const { return m_options; }
    int current() const { return m_index; }
    int count(CompressJob::State state) const;
    int percent() const;
    int remainingSeconds() const;
    // What the replaced files gave back, against their originals.
    qint64 savedBytes() const;
signals:
    void changed();
    void runningChanged(bool running);
    void replaced(const QString& path, const QString& original);
    void finished(int replaced, int failed, bool cancelled);
private:
    void next();
    void compress(const ClipInfo& info);
    void checked(const ClipInfo& info, const QString& error);
    void replaceNow(int index, bool permanent);
    void fail(const QString& error);
    void finishBatch();
    QList<CompressJob> m_jobs; CompressOptions m_options; ClipTrashOperation m_trash; std::function<void(const QString&)> m_release;
    ClipCompressor m_compressor;
    QFutureWatcher<std::pair<ClipInfo, QString>> m_probe, m_check;
    QFutureWatcher<CompressReplacement> m_replacing; int m_replacingIndex = -1; ClipInfo m_sourceInfo;
    QElapsedTimer m_clock;
    bool m_running = false, m_cancelled = false;
    int m_index = -1;
};
}
