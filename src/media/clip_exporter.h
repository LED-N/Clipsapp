#pragma once
#include "clip_edit.h"
#include <QObject>
#include <QProcess>
#include <QTemporaryFile>
#include <QTemporaryDir>
#include <memory>
namespace clipsapp {
struct ClipExportOptions {
    bool highQuality = false;
    qint64 maxBytes = 0;
    QString codec = "h264";
    bool autoResize = true;
    bool tryCopyVideo = true;
};
// What a size-capped export will do, decided the same way the exporter decides it,
// so the interface can announce the result before any encoding starts.
struct SizedExportPlan {
    bool feasible = true, copyVideo = false;
    qint64 videoBitrate = 0, audioBitrate = 192000, estimatedBytes = 0;
    QSize outputSize;
};
SizedExportPlan planSizedExport(const ClipInfo& info, const ClipEdit& edit, qint64 sourceBytes, const ClipExportOptions& options);
class ClipExporter final : public QObject {
    Q_OBJECT
public:
    explicit ClipExporter(QObject* parent = nullptr, QString executable = {});
    ~ClipExporter() override;
    bool start(const ClipInfo& info, const ClipEdit& edit, const QString& destination, QString& error,
               QStringList encoders = {"h264_nvenc", "h264_amf", "libopenh264"}, ClipExportOptions options = {});
    void cancel();
    bool running() const { return m_running; }
signals:
    void progress(int percent);
    void completed(const QString& path);
    void failed(const QString& error);
    void cancelled();
    void stageChanged(const QString& text);
private:
    void launch();
    QStringList arguments(const QString& encoder) const;
    void finish(int code, QProcess::ExitStatus status);
    QProcess m_process;
    QString m_executable, m_destination;
    ClipInfo m_info;
    ClipEdit m_edit;
    QStringList m_encoders;
    QByteArray m_progress, m_log;
    std::unique_ptr<QTemporaryFile> m_temporary;
    std::unique_ptr<QTemporaryDir> m_passDirectory;
    ClipExportOptions m_options;
    QString m_encoder;
    qint64 m_videoBitrate = 20000000, m_audioBitrate = 192000;
    QSize m_outputSize;
    int m_pass = 0, m_sizeAttempts = 0;
    bool m_copyVideo = false;
    bool m_copyAudio = false;
    bool m_running = false, m_cancelled = false;
};
}
