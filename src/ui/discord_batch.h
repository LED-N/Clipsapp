#pragma once
#include "media/clip_exporter.h"
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QObject>
#include <functional>
#include <optional>
namespace clipsapp {
struct DiscordPrepared { ClipInfo info; QString error; };
// The mix is the usual level of each track: a clip shared whole is given it, as in the editor.
// The usual levels of a clip come from its game: "clipMix" answers for each one, "mix" without it.
struct ShareOptions {
    QString directory; qint64 maxBytes = 20000000; QString codec = "h264"; bool autoResize = true; TrackMix mix; std::function<TrackMix(const QString&)> clipMix;
    TrackMix mixOf(const QString& path) const { return clipMix ? clipMix(path) : mix; }
};
struct ShareJob {
    enum State { Pending, Running, Ready, Failed, Cancelled };
    QString source, name; State state = Pending; int progress = 0, batch = 0;
    QString stage, detail, output, error; qint64 bytes = 0; bool copied = false;
    // Set when the job carries the editor's work rather than a whole clip.
    std::optional<ClipInfo> info; std::optional<ClipEdit> edit;
};
// Prepares Discord copies one clip at a time. It belongs to the window, not to the
// dialog that started it, so closing the dialog leaves the batch running.
class DiscordBatch final : public QObject {
    Q_OBJECT
public:
    explicit DiscordBatch(QObject* parent = nullptr);
    ~DiscordBatch() override;
    void start(const QStringList& paths, const ShareOptions& options, std::optional<ClipInfo> editedInfo = {}, std::optional<ClipEdit> edited = {});
    void retry(int index);
    void remove(int index);
    void cancel();
    bool running() const { return m_running; }
    const QList<ShareJob>& jobs() const { return m_jobs; }
    const ShareOptions& options() const { return m_options; }
    int current() const { return m_index; }
    int batch() const { return m_batch; }
    int count(ShareJob::State state, bool thisBatchOnly = true) const;
    int batchSize() const;
    int percent() const;
    int remainingSeconds() const;
signals:
    void changed();
    void runningChanged(bool running);
    void shareReady(const QString& path, const QString& source, qint64 limitBytes);
    void highQualityReady(const QString& path, const QString& original, const ClipEdit& edit);
    void finished(int ready, int failed, bool cancelled);
private:
    void begin();
    void next();
    void prepare(const QString& path, bool highQuality = false);
    void compress(const ClipInfo& info, ClipEdit edit);
    void failCurrent(const QString& error);
    void finishBatch();
    QString uniquePath(const QString& directory, const QString& base, const QString& suffix) const;
    QList<ShareJob> m_jobs; ShareOptions m_options;
    ClipInfo m_info; ClipEdit m_edit;
    ClipExporter m_exporter;
    QFutureWatcher<DiscordPrepared> m_prepare;
    QElapsedTimer m_clock;
    bool m_running = false, m_cancelled = false, m_highQuality = false, m_preparingHighQuality = false;
    int m_index = -1, m_batch = 0;
};
}
