#pragma once
#include "clip_edit.h"
#include <QObject>
#include <QProcess>
#include <QTemporaryFile>
#include <memory>
namespace clipsapp {
// To save room while keeping the picture: the video is encoded again at a constant quality, the
// sound, every track and their names are kept as they are, and so are the size, the pace and the
// length. "level" goes from 0, where nothing shows, to 2, the most compact. "processor" uses AV1 on
// the processor, more compact and much slower, rather than HEVC on the graphics card.
struct CompressSettings { int level = 0; bool processor = false; };
// The container the compressed file keeps: the original's, unless it cannot hold the new video.
QString compressedSuffix(const QString& path);
class ClipCompressor final : public QObject {
    Q_OBJECT
public:
    explicit ClipCompressor(QObject* parent = nullptr, QString executable = {});
    ~ClipCompressor() override;
    // Writes "destination", which must not exist; the original is only read. The file is in the
    // container compressedSuffix() names for the original, whatever the name it is given.
    bool start(const ClipInfo& info, const QString& destination, const CompressSettings& settings, QString& error);
    void cancel();
    bool running() const { return m_running; }
    // The encoder that wrote the last file, as FFmpeg names it.
    QString encoder() const { return m_encoder; }
signals:
    void progress(int percent);
    void stageChanged(const QString& text);
    void completed(const QString& path);
    void failed(const QString& error);
    void cancelled();
private:
    void launch();
    QStringList arguments() const;
    void finish(int code, QProcess::ExitStatus status);
    QProcess m_process;
    QString m_executable, m_destination, m_encoder, m_format;
    ClipInfo m_info;
    CompressSettings m_settings;
    QStringList m_encoders;
    QByteArray m_progress, m_log;
    std::unique_ptr<QTemporaryFile> m_temporary;
    bool m_copyAudio = true, m_running = false, m_cancelled = false;
};
}
