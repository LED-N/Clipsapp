#include "clip_exporter.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <algorithm>
#include <cmath>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
namespace clipsapp {
namespace {
QString seconds(qint64 ms) { return QString::number(ms / 1000.0, 'f', 3); }
bool canCopyVideo(const ClipInfo& info, const ClipEdit& edit) {
    return QStringList{"h264", "hevc", "av1"}.contains(info.videoCodec) && edit.startMs == 0 && edit.endMs == info.durationMs && edit.crop == QRect(QPoint(), info.size);
}
QSize sizedOutput(const QRect& crop, qint64 videoBitrate, double fps, const QString& codec) {
    const double density = codec == "h264" ? 0.045 : 0.03;
    const double pixels = videoBitrate / (std::max(1.0, fps) * density);
    const double scale = std::min(1.0, std::sqrt(pixels / (double(crop.width()) * crop.height())));
    return {std::max(2, int(crop.width() * scale) & ~1), std::max(2, int(crop.height() * scale) & ~1)};
}
}
SizedExportPlan planSizedExport(const ClipInfo& info, const ClipEdit& edit, qint64 sourceBytes, const ClipExportOptions& options) {
    SizedExportPlan plan; plan.outputSize = edit.crop.size();
    const double duration = std::max(0.05, (edit.endMs - edit.startMs) / 1000.0);
    const qint64 totalRate = qint64((options.maxBytes * 0.985 - 16384) * 8 / duration);
    if (totalRate < 1000000) plan.audioBitrate = 128000;
    if (totalRate < 240000) plan.audioBitrate = 64000;
    const qint64 audio = audioExportTrackCount(edit) ? plan.audioBitrate : 0;
    plan.videoBitrate = totalRate - audio; plan.feasible = plan.videoBitrate >= 48000;
    plan.copyVideo = options.tryCopyVideo && canCopyVideo(info, edit) && sourceBytes <= options.maxBytes;
    if (plan.copyVideo) { plan.estimatedBytes = sourceBytes; return plan; }
    if (options.autoResize && plan.feasible) plan.outputSize = sizedOutput(edit.crop, plan.videoBitrate, info.fps, options.codec);
    plan.estimatedBytes = qint64((plan.videoBitrate + audio) * duration / 8) + 16384;
    return plan;
}
ClipExporter::ClipExporter(QObject* parent, QString executable) : QObject(parent), m_executable(std::move(executable)) {
    if (m_executable.isEmpty()) {
#ifdef Q_OS_WIN
        m_executable = QDir(QCoreApplication::applicationDirPath()).filePath("ffmpeg.exe");
#else
        m_executable = QStandardPaths::findExecutable("ffmpeg");
#endif
    }
#ifdef Q_OS_WIN
    m_process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) { args->flags |= CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS; });
#endif
    connect(&m_process, &QProcess::readyReadStandardError, this, [this] { m_log = (m_log + m_process.readAllStandardError()).right(32768); });
    connect(&m_process, &QProcess::readyReadStandardOutput, this, [this] {
        m_progress += m_process.readAllStandardOutput();
        int newline;
        while ((newline = m_progress.indexOf('\n')) >= 0) {
            const auto line = m_progress.left(newline).trimmed(); m_progress.remove(0, newline + 1);
            if (line.startsWith("out_time_us=")) {
                const qint64 elapsed = line.mid(12).toLongLong();
                const int value = int(std::clamp<qint64>(elapsed / std::max<qint64>(1, (m_edit.endMs - m_edit.startMs) * 10), 0, 99));
                emit progress(m_pass == 1 ? value / 2 : m_pass == 2 ? 50 + value / 2 : value);
            }
        }
        if (m_progress.size() > 4096) m_progress.clear();
    });
    connect(&m_process, &QProcess::finished, this, &ClipExporter::finish);
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || !m_running) return;
        m_running = false; m_temporary.reset(); m_passDirectory.reset(); emit failed(QStringLiteral("Impossible de démarrer l'export : ") + m_process.errorString());
    });
}
ClipExporter::~ClipExporter() { if (m_running) { m_cancelled = true; m_process.kill(); m_process.waitForFinished(3000); } }
bool ClipExporter::start(const ClipInfo& info, const ClipEdit& edit, const QString& destination, QString& error, QStringList encoders, ClipExportOptions options) {
    if (m_running) { error = QStringLiteral("Un export est déjà en cours."); return false; }
    error = validateEdit(info, edit); if (!error.isEmpty()) return false;
    if (options.maxBytes < 0 || (options.maxBytes && options.maxBytes < 65536) || !QStringList{"h264", "hevc", "av1"}.contains(options.codec)) { error = QStringLiteral("Plafond ou format de compression invalide."); return false; }
    const QFileInfo source(info.path), target(destination);
    if (!source.isFile()) { error = QStringLiteral("Le clip d'origine n'est plus disponible."); return false; }
    if (target.absoluteFilePath().compare(source.absoluteFilePath(), Qt::CaseInsensitive) == 0 || target.exists()) {
        error = QStringLiteral("Choisissez un nouveau nom de fichier pour l'export."); return false;
    }
    if (target.suffix().compare("mp4", Qt::CaseInsensitive) || !QDir(target.absolutePath()).exists()) {
        error = QStringLiteral("Choisissez un fichier MP4 dans un dossier existant."); return false;
    }
    if (m_executable.isEmpty() || !QFileInfo(m_executable).isExecutable() || encoders.isEmpty()) { error = QStringLiteral("Le moteur d'export FFmpeg est indisponible."); return false; }
    m_temporary = std::make_unique<QTemporaryFile>(QDir(target.absolutePath()).filePath(".clipsapp-export-XXXXXX.mp4.part"));
    if (!m_temporary->open()) { error = m_temporary->errorString(); m_temporary.reset(); return false; }
    m_temporary->close(); m_info = info; m_edit = edit; m_destination = target.absoluteFilePath(); m_encoders = std::move(encoders);
    m_options = std::move(options); m_pass = 0; m_passDirectory.reset(); m_sizeAttempts = 0; m_encoder.clear(); m_copyVideo = false; m_outputSize = edit.crop.size();
    m_audioBitrate = 192000;
    m_videoBitrate = m_options.highQuality ? std::max<qint64>(20000000, info.videoBitrate) : 20000000;
    if (m_options.highQuality && m_options.tryCopyVideo && canCopyVideo(info, edit)) m_copyVideo = true;
    if (m_options.maxBytes) {
        const auto plan = planSizedExport(info, edit, source.size(), m_options);
        m_audioBitrate = plan.audioBitrate; m_videoBitrate = plan.videoBitrate;
        if (!plan.feasible) { m_temporary.reset(); error = QStringLiteral("Ce plafond est trop bas pour la durée du clip. Augmentez-le ou réduisez la sélection."); return false; }
        m_edit.separateTracks = false;
        m_copyVideo = plan.copyVideo;
        m_passDirectory = std::make_unique<QTemporaryDir>(QDir(target.absolutePath()).filePath(".clipsapp-pass-XXXXXX"));
        if (!m_passDirectory->isValid()) { m_temporary.reset(); error = QStringLiteral("Impossible de préparer l'analyse de compression."); return false; }
    }
    m_copyAudio = m_copyVideo && info.audio.size() == 1 && info.audio.first().codec == "aac" && edit.tracks.size() == 1 && !edit.tracks.first().muted && std::abs(edit.tracks.first().volume - 1) < 0.000001;
    m_cancelled = false; m_running = true; launch(); return true;
}
QStringList ClipExporter::arguments(const QString& encoder) const {
    QStringList args{"-nostdin", "-hide_banner", "-loglevel", "error", "-y", "-threads", "2", "-filter_complex_threads", "1", "-protocol_whitelist", "file,pipe"};
    if (m_edit.startMs > 0) args << "-ss" << seconds(m_edit.startMs);
    args << "-i" << m_info.path;
    QStringList graph;
    if (!m_copyVideo) graph.append(QString("[0:%1]crop=%2:%3:%4:%5,scale=%6:%7:flags=lanczos,setsar=1[v]").arg(m_info.videoStream).arg(m_edit.crop.width()).arg(m_edit.crop.height()).arg(m_edit.crop.x()).arg(m_edit.crop.y()).arg(m_outputSize.width()).arg(m_outputSize.height()));
    QStringList active; QStringList names;
    for (const auto& track : m_edit.tracks) if (!track.muted && m_pass != 1 && !m_copyAudio) {
        const QString name = QString("a%1").arg(active.size());
        graph.append(QString("[0:%1]aresample=48000:async=1:first_pts=0,aformat=channel_layouts=stereo,volume=%2,apad=whole_dur=%3[%4]")
            .arg(track.stream).arg(track.volume, 0, 'f', 6).arg(seconds(m_edit.endMs - m_edit.startMs), name));
        active.append(name); names.append(track.name);
    }
    if (!m_edit.separateTracks && !active.isEmpty()) {
        QString inputs; for (const auto& name : active) inputs += '[' + name + ']';
        graph.append(inputs + QString("amix=inputs=%1:normalize=0:duration=longest,aeval=exprs='clip(val(0),-1,1)|clip(val(1),-1,1)',aformat=channel_layouts=stereo[mix]").arg(active.size()));
        const auto title = active.size() == 1 ? names.first() : QStringLiteral("Mixage");
        active = {"mix"}; names = {title};
    }
    if (!graph.isEmpty()) args << "-filter_complex" << graph.join(';');
    args << "-map" << (m_copyVideo ? QString("0:%1").arg(m_info.videoStream) : "[v]");
    if (m_copyAudio) args << "-map" << QString("0:%1").arg(m_edit.tracks.first().stream);
    for (const auto& name : active) args << "-map" << '[' + name + ']';
    if (!(m_copyVideo && m_copyAudio)) args << "-t" << seconds(m_edit.endMs - m_edit.startMs);
    args << "-c:v" << (m_copyVideo ? "copy" : encoder);
    if (!m_copyVideo) {
        args << "-pix_fmt" << (encoder == "libsvtav1" ? "yuv420p10le" : "yuv420p") << "-b:v" << QString::number(m_videoBitrate);
        if (encoder.endsWith("nvenc")) {
            args << "-preset" << (m_options.maxBytes || m_options.highQuality ? "p7" : "p4");
            if (m_options.maxBytes || m_options.highQuality) args << "-tune" << "hq" << "-rc" << "vbr" << "-multipass" << "fullres" << "-rc-lookahead" << "32" << "-spatial-aq" << "1" << "-temporal-aq" << "1" << "-bf" << "3";
            if (m_options.highQuality) args << "-b:v" << "0" << "-cq" << "16";
        }
        if (encoder == "libsvtav1") args << "-preset" << "6" << "-svtav1-params" << "lp=2:tune=0";
        if (encoder == "libkvazaar") args << "-kvazaar-params" << "threads=2";
        if (m_pass) args << "-pass" << QString::number(m_pass) << "-passlogfile" << QDir(m_passDirectory->path()).filePath("video");
        if (m_options.maxBytes && encoder != "libsvtav1") args << "-maxrate:v" << QString::number(m_videoBitrate * 2) << "-bufsize:v" << QString::number(std::min<qint64>(2000000000, m_videoBitrate * 4));
        if (encoder.startsWith("hevc")) args << "-tag:v" << "hvc1";
    }
    if (m_copyAudio) args << "-c:a" << "copy" << "-metadata:s:a:0" << "handler_name=" + m_edit.tracks.first().name;
    else if (!active.isEmpty()) {
        args << "-c:a" << "aac" << "-b:a" << QString::number(m_audioBitrate) << "-ar" << "48000";
        for (int i = 0; i < active.size(); ++i) args << QString("-metadata:s:a:%1").arg(i) << "handler_name=" + names[i];
    } else args << "-an";
    // The file says its sound was mixed here: opened again, it is not given the usual levels a second time.
    args << "-map_metadata" << "-1" << "-metadata" << QStringLiteral("comment=") + QString::fromUtf8(mixedMark) << "-progress" << "pipe:1" << "-nostats";
    if (m_pass == 1) args << "-f" << "null" << "-";
    else args << "-movflags" << "+faststart" << "-f" << "mp4" << m_temporary->fileName();
    return args;
}
void ClipExporter::launch() {
    if (m_encoder.isEmpty()) m_encoder = m_encoders.takeFirst();
    if (m_options.maxBytes && !m_copyVideo && m_options.autoResize) m_outputSize = sizedOutput(m_edit.crop, m_videoBitrate, m_info.fps, m_options.codec);
    if (m_options.maxBytes && m_encoder == "libsvtav1" && !m_copyVideo && !m_pass) m_pass = 1;
    emit stageChanged(m_options.highQuality ? QStringLiteral("Export de la version haute qualité") : m_copyVideo ? QStringLiteral("Préparation du partage sans réencoder la vidéo") : m_pass == 1 ? QStringLiteral("Analyse du clip pour la compression") : m_sizeAttempts ? QStringLiteral("Ajustement pour respecter le plafond") : QStringLiteral("Compression du clip"));
    m_progress.clear(); m_log.clear(); m_process.start(m_executable, arguments(m_encoder)); emit progress(m_pass == 2 ? 50 : 0);
}
void ClipExporter::cancel() { if (m_running) { m_cancelled = true; m_process.kill(); } }
void ClipExporter::finish(int code, QProcess::ExitStatus status) {
    if (!m_running) return;
    m_log = (m_log + m_process.readAllStandardError()).right(32768);
    if (!m_cancelled && (code != 0 || status != QProcess::NormalExit) && (!m_encoders.isEmpty() || m_copyVideo)) {
        if (m_copyVideo) { m_copyVideo = false; m_copyAudio = false; } else m_encoder.clear();
        m_pass = 0; launch(); return;
    }
    if (!m_cancelled && code == 0 && status == QProcess::NormalExit && m_pass == 1) { m_pass = 2; launch(); return; }
    if (!m_cancelled && code == 0 && status == QProcess::NormalExit && m_options.maxBytes && QFileInfo(m_temporary->fileName()).size() > m_options.maxBytes) {
        const auto actual = QFileInfo(m_temporary->fileName()).size();
        if (m_copyVideo) { m_copyVideo = false; m_copyAudio = false; }
        else m_videoBitrate = qint64(m_videoBitrate * std::min(0.94, m_options.maxBytes / double(actual) * 0.97));
        if (++m_sizeAttempts <= 4 && m_videoBitrate >= 48000) { m_pass = 0; launch(); return; }
        m_running = false; m_temporary.reset(); m_passDirectory.reset(); emit failed(QStringLiteral("Le plafond n'a pas pu être respecté. Augmentez la taille ou réduisez la durée.")); return;
    }
    m_running = false;
    if (m_cancelled) { m_temporary.reset(); m_passDirectory.reset(); emit cancelled(); return; }
    if (code != 0 || status != QProcess::NormalExit) {
        m_temporary.reset(); m_passDirectory.reset(); emit failed(QStringLiteral("L'export a échoué.\n") + QString::fromUtf8(m_log).right(4000)); return;
    }
    if (!m_temporary->rename(m_destination)) { const auto error = m_temporary->errorString(); m_temporary.reset(); m_passDirectory.reset(); emit failed(QStringLiteral("Impossible de finaliser le fichier exporté : ") + error); return; }
    m_temporary->setAutoRemove(false); m_temporary.reset(); m_passDirectory.reset(); emit progress(100); emit completed(m_destination);
}
}
