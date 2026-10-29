#include "clip_compressor.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <algorithm>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
namespace clipsapp {
namespace {
// HEVC on the graphics card first, NVIDIA then AMD; the processor's AV1 when neither is there.
QStringList encodersFor(const CompressSettings& settings) { return settings.processor ? QStringList{"libsvtav1"} : QStringList{"hevc_nvenc", "hevc_amf", "libsvtav1"}; }
}
QString compressedSuffix(const QString& path) {
    const auto suffix = QFileInfo(path).suffix().toLower();
    // WebM holds neither HEVC nor the sound of every recorder, AVI none of them well: those become MP4.
    return suffix == QLatin1String("mkv") || suffix == QLatin1String("mov") ? suffix : QStringLiteral("mp4");
}
ClipCompressor::ClipCompressor(QObject* parent, QString executable) : QObject(parent), m_executable(std::move(executable)) {
    if (m_executable.isEmpty()) {
#ifdef Q_OS_WIN
        const QDir folder(QCoreApplication::applicationDirPath());
        m_executable = folder.exists(QStringLiteral("runtime/ffmpeg.exe")) ? folder.filePath(QStringLiteral("runtime/ffmpeg.exe")) : folder.filePath(QStringLiteral("ffmpeg.exe"));
#else
        m_executable = QStandardPaths::findExecutable("ffmpeg");
#endif
    }
#ifdef Q_OS_WIN
    // Below the games: a long compression never takes the processor from what is being played.
    m_process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) { args->flags |= CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS; });
#endif
    connect(&m_process, &QProcess::readyReadStandardError, this, [this] { m_log = (m_log + m_process.readAllStandardError()).right(32768); });
    connect(&m_process, &QProcess::readyReadStandardOutput, this, [this] {
        m_progress += m_process.readAllStandardOutput();
        int newline;
        while ((newline = m_progress.indexOf('\n')) >= 0) {
            const auto line = m_progress.left(newline).trimmed(); m_progress.remove(0, newline + 1);
            if (line.startsWith("out_time_us=")) emit progress(int(std::clamp<qint64>(line.mid(12).toLongLong() / std::max<qint64>(1, m_info.durationMs * 10), 0, 99)));
        }
        if (m_progress.size() > 4096) m_progress.clear();
    });
    connect(&m_process, &QProcess::finished, this, &ClipCompressor::finish);
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || !m_running) return;
        m_running = false; m_temporary.reset(); emit failed(QStringLiteral("Impossible de démarrer la compression : ") + m_process.errorString());
    });
}
ClipCompressor::~ClipCompressor() { if (m_running) { m_cancelled = true; m_process.kill(); m_process.waitForFinished(3000); } }
bool ClipCompressor::start(const ClipInfo& info, const QString& destination, const CompressSettings& settings, QString& error) {
    if (m_running) { error = QStringLiteral("Une compression est déjà en cours."); return false; }
    const QFileInfo source(info.path), target(destination);
    if (!source.isFile() || info.videoStream < 0 || info.durationMs <= 0) { error = QStringLiteral("Le clip n'est plus disponible."); return false; }
    if (target.exists() || target.absoluteFilePath().compare(source.absoluteFilePath(), Qt::CaseInsensitive) == 0 || !QDir(target.absolutePath()).exists()) { error = QStringLiteral("Le fichier compressé n'a pas d'emplacement libre."); return false; }
    if (m_executable.isEmpty() || !QFileInfo(m_executable).isExecutable()) { error = QStringLiteral("Le moteur de compression FFmpeg est indisponible."); return false; }
    // Written under a name of its own beside the original, on the same drive: taking its place is then a rename.
    m_temporary = std::make_unique<QTemporaryFile>(QDir(target.absolutePath()).filePath(QStringLiteral(".clipsapp-compress-XXXXXX.part")));
    if (!m_temporary->open()) { error = m_temporary->errorString(); m_temporary.reset(); return false; }
    m_temporary->close(); m_info = info; m_destination = target.absoluteFilePath(); m_settings = settings; m_format = compressedSuffix(info.path);
    m_encoders = encodersFor(settings); m_encoder.clear(); m_copyAudio = true; m_cancelled = false; m_running = true; launch(); return true;
}
QStringList ClipCompressor::arguments() const {
    const bool nvenc = m_encoder.endsWith(QLatin1String("nvenc")), amf = m_encoder.endsWith(QLatin1String("amf")), av1 = m_encoder == QLatin1String("libsvtav1");
    const int level = std::clamp(m_settings.level, 0, 2); const auto& suffix = m_format;
    QStringList args{"-nostdin", "-hide_banner", "-loglevel", "error", "-y", "-protocol_whitelist", "file,pipe", "-i", m_info.path,
                     "-map", QStringLiteral("0:%1").arg(m_info.videoStream), "-map", "0:a?", "-c:v", m_encoder, "-fps_mode", "passthrough"};
    // A picture a seek can start on every two seconds, as the replay writes them: the editor moves through it as quickly.
    args << "-g" << QString::number(std::max(1, qRound(m_info.fps * 2)));
    if (nvenc) args << "-preset" << "p6" << "-tune" << "hq" << "-rc" << "vbr" << "-cq" << QStringList{"24", "28", "32"}[level] << "-b:v" << "0" << "-spatial-aq" << "1" << "-temporal-aq" << "1" << "-rc-lookahead" << "32" << "-bf" << "3" << "-pix_fmt" << "yuv420p";
    else if (amf) args << "-usage" << "transcoding" << "-quality" << "quality" << "-rc" << "cqp" << "-qp_i" << QStringList{"22", "26", "30"}[level] << "-qp_p" << QStringList{"24", "28", "32"}[level] << "-pix_fmt" << "yuv420p";
    else if (av1) args << "-preset" << "7" << "-crf" << QStringList{"30", "35", "40"}[level] << "-svtav1-params" << "tune=0" << "-pix_fmt" << "yuv420p10le";
    // Never heavier than the original: the graphics card is held under what the original spent.
    if (!av1 && m_info.videoBitrate > 0) args << "-maxrate:v" << QString::number(qint64(m_info.videoBitrate * 0.9)) << "-bufsize:v" << QString::number(qint64(m_info.videoBitrate * 1.8));
    if (m_encoder.startsWith(QLatin1String("hevc")) && suffix != QLatin1String("mkv")) args << "-tag:v" << "hvc1";
    // The sound is the original's, track by track; only a container that refuses it has it encoded again.
    if (m_copyAudio) args << "-c:a" << "copy"; else args << "-c:a" << "aac" << "-b:a" << "256k";
    const QString comment = m_info.mixed ? QStringLiteral("%1 · %2").arg(QString::fromUtf8(mixedMark), QString::fromUtf8(compressedMark)) : QString::fromUtf8(compressedMark);
    args << "-map_metadata" << "0" << "-metadata" << QStringLiteral("comment=") + comment;
    if (suffix == QLatin1String("mkv")) args << "-f" << "matroska"; else args << "-movflags" << "+faststart" << "-f" << (suffix == QLatin1String("mov") ? "mov" : "mp4");
    args << "-progress" << "pipe:1" << "-nostats" << m_temporary->fileName();
    return args;
}
void ClipCompressor::launch() {
    if (m_encoder.isEmpty()) m_encoder = m_encoders.takeFirst();
    const auto where = m_encoder == QLatin1String("libsvtav1") ? QStringLiteral("AV1 sur le processeur") : m_encoder.endsWith(QLatin1String("nvenc")) ? QStringLiteral("HEVC sur la carte graphique NVIDIA") : QStringLiteral("HEVC sur la carte graphique AMD");
    emit stageChanged(QStringLiteral("Compression %1%2").arg(where, m_copyAudio ? QString() : QStringLiteral(", son réencodé")));
    m_progress.clear(); m_log.clear(); m_process.start(m_executable, arguments()); emit progress(0);
}
void ClipCompressor::cancel() { if (m_running) { m_cancelled = true; m_process.kill(); } }
void ClipCompressor::finish(int code, QProcess::ExitStatus status) {
    if (!m_running) return;
    m_log = (m_log + m_process.readAllStandardError()).right(32768);
    const bool ok = code == 0 && status == QProcess::NormalExit;
    if (!m_cancelled && !ok) {
        // The next encoder; once none is left, every one again with the sound encoded too.
        if (!m_encoders.isEmpty()) { m_encoder.clear(); launch(); return; }
        if (m_copyAudio) { m_copyAudio = false; m_encoders = encodersFor(m_settings); m_encoder.clear(); launch(); return; }
    }
    m_running = false;
    if (m_cancelled) { m_temporary.reset(); emit cancelled(); return; }
    if (!ok) { m_temporary.reset(); emit failed(QStringLiteral("La compression a échoué.\n") + QString::fromUtf8(m_log).right(4000)); return; }
    if (!m_temporary->rename(m_destination)) { const auto error = m_temporary->errorString(); m_temporary.reset(); emit failed(QStringLiteral("Impossible de finaliser le fichier compressé : ") + error); return; }
    m_temporary->setAutoRemove(false); m_temporary.reset(); emit progress(100); emit completed(m_destination);
}
}
