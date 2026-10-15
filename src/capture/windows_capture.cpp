#include "replay_engine.h"
#include "core/replay_buffer.h"
#include "platform/fine_wait.h"
#include "platform/windows_audio.h"
#include "platform/windows_video.h"
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QSaveFile>
#include <QScopeGuard>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
#include <stdexcept>
#include <thread>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
}

namespace clipsapp {
namespace {
constexpr AVRational microseconds{1, 1000000};
void check(int result, const QString& operation) {
    if (result < 0) { char message[AV_ERROR_MAX_STRING_SIZE]; av_strerror(result, message, sizeof(message)); throw std::runtime_error((operation + ": " + QString::fromUtf8(message)).toStdString()); }
}
struct Frame { AVFrame* p = av_frame_alloc(); ~Frame() { av_frame_free(&p); } };
struct Packet { AVPacket* p = av_packet_alloc(); ~Packet() { av_packet_free(&p); } };
struct Codec { AVCodecContext* p = nullptr; ~Codec() { avcodec_free_context(&p); } };
using Parameters = std::shared_ptr<AVCodecParameters>;
struct Stream { Parameters parameters; AVRational timeBase; QString title; };
Parameters parameters(AVCodecContext* codec) {
    Parameters result(avcodec_parameters_alloc(), [](auto* item) { avcodec_parameters_free(&item); });
    if (!result) throw std::bad_alloc();
    check(avcodec_parameters_from_context(result.get(), codec), QStringLiteral("Paramètres de la piste")); return result;
}
struct AudioEncoder {
    Codec codec; Frame frame;
    QString id, name;
    std::vector<std::unique_ptr<WindowsAudio>> sources;
    int stream = 0;
};
void testFrame(AVFrame* frame, const VideoSettings& video, qint64 position) {
    frame->format = AV_PIX_FMT_YUV420P; frame->width = video.width; frame->height = video.height;
    check(av_frame_get_buffer(frame, 0), QStringLiteral("Image de test"));
    for (int y = 0; y < video.height; ++y) for (int x = 0; x < video.width; ++x)
        frame->data[0][y * frame->linesize[0] + x] = ((x / 24 + y / 24 + position / 15) % 2) ? 190 : 64;
    for (int plane = 1; plane < 3; ++plane) for (int y = 0; y < video.height / 2; ++y)
        std::fill_n(frame->data[plane] + y * frame->linesize[plane], video.width / 2, plane == 1 ? 96 : 160);
}
int writeFile(void* opaque, const uint8_t* data, int size) {
    auto* file = static_cast<QSaveFile*>(opaque); const auto written = file->write(reinterpret_cast<const char*>(data), size);
    return written == size ? size : AVERROR(EIO);
}
int64_t seekFile(void* opaque, int64_t offset, int whence) {
    auto* file = static_cast<QSaveFile*>(opaque);
    if (whence == AVSEEK_SIZE) return file->size();
    const int mode = whence & ~AVSEEK_FORCE;
    qint64 position = offset;
    if (mode == SEEK_CUR) position += file->pos(); else if (mode == SEEK_END) position += file->size(); else if (mode != SEEK_SET) return AVERROR(EINVAL);
    return file->seek(position) ? position : AVERROR(EIO);
}
ExportResult writeSnapshot(const ReplaySnapshot& snapshot, const QList<Stream>& streams, const QString& path) {
    ExportResult result{path, {}, snapshot.seconds()};
    if (snapshot.packets.isEmpty()) { result.error = QStringLiteral("Le buffer ne contient pas encore de clip décodable."); return result; }
    QSaveFile file(path); file.setDirectWriteFallback(false);
    AVFormatContext* format = nullptr; AVIOContext* io = nullptr;
    try {
        if (QFile::exists(path)) throw std::runtime_error("Le fichier de destination existe déjà.");
        if (!file.open(QIODevice::WriteOnly)) throw std::runtime_error(file.errorString().toStdString());
        check(avformat_alloc_output_context2(&format, nullptr, "mp4", nullptr), QStringLiteral("Création du clip MP4"));
        auto* buffer = static_cast<unsigned char*>(av_malloc(65536));
        if (!buffer) throw std::bad_alloc();
        io = avio_alloc_context(buffer, 65536, 1, &file, nullptr, writeFile, seekFile);
        if (!io) { av_free(buffer); throw std::bad_alloc(); }
        format->pb = io; format->flags |= AVFMT_FLAG_CUSTOM_IO;
        for (const auto& stream : streams) {
            auto* output = avformat_new_stream(format, nullptr); if (!output) throw std::bad_alloc();
            check(avcodec_parameters_copy(output->codecpar, stream.parameters.get()), QStringLiteral("Copie de la piste"));
            output->time_base = stream.timeBase;
            av_dict_set(&output->metadata, "handler_name", stream.title.toUtf8().constData(), 0);
            if (output->codecpar->codec_id == AV_CODEC_ID_HEVC) output->codecpar->codec_tag = MKTAG('h', 'v', 'c', '1');
            output->disposition = stream.parameters->codec_type == AVMEDIA_TYPE_VIDEO ? AV_DISPOSITION_DEFAULT : 0;
        }
        check(avformat_write_header(format, nullptr), QStringLiteral("En-tête du clip"));
        Packet packet;
        for (const auto& item : snapshot.packets) {
            check(av_new_packet(packet.p, int(item.data.size())), QStringLiteral("Paquet du clip"));
            std::memcpy(packet.p->data, item.data.constData(), size_t(item.data.size()));
            packet.p->stream_index = item.stream;
            const auto timeBase = format->streams[item.stream]->time_base;
            packet.p->pts = av_rescale_q(item.ptsUs - snapshot.startUs, microseconds, timeBase);
            packet.p->dts = av_rescale_q(item.dtsUs - snapshot.startUs, microseconds, timeBase);
            packet.p->duration = av_rescale_q(item.durationUs, microseconds, timeBase);
            if (item.keyframe) packet.p->flags |= AV_PKT_FLAG_KEY;
            check(av_interleaved_write_frame(format, packet.p), QStringLiteral("Écriture du clip")); av_packet_unref(packet.p);
        }
        check(av_write_trailer(format), QStringLiteral("Finalisation du clip")); avio_flush(io);
        check(io->error, QStringLiteral("Écriture du fichier"));
        if (!file.commit()) throw std::runtime_error(file.errorString().toStdString());
    } catch (const std::exception& error) { file.cancelWriting(); result.error = QStringLiteral("Sauvegarde impossible : ") + QString::fromUtf8(error.what()); }
    if (io) { av_freep(&io->buffer); avio_context_free(&io); }
    if (format) avformat_free_context(format);
    return result;
}
class WindowsCapture final : public CaptureBackend {
public:
    WindowsCapture(RecordingProfile profile, bool synthetic = false)
        : m_profile(std::move(profile)), m_synthetic(synthetic), m_buffer(qint64(m_profile.replay.durationSeconds) * 1000000, qint64(m_profile.replay.memoryLimitMiB) * 1048576) {}
    CaptureStats stats() const override {
        QMutexLocker lock(&m_mutex); return {m_buffer.seconds(), m_buffer.bytes(), m_frames, m_encoder, m_warning, m_lost, m_silent};
    }
    ExportTask prepareSave(const QString& path) const override {
        ReplaySnapshot snapshot; QList<Stream> streams;
        { QMutexLocker lock(&m_mutex); snapshot = m_buffer.snapshot(); streams = m_streams; }
        return [snapshot = std::move(snapshot), streams = std::move(streams), path] { return writeSnapshot(snapshot, streams, path); };
    }
    QString run(std::atomic_bool& stop, const std::function<void(QString)>& ready) override {
        const auto cleanup = qScopeGuard([this] {
            QMutexLocker lock(&m_mutex);
            m_buffer.clear(); m_streams.clear(); m_frames = 0;
            m_encoder.clear(); m_warning.clear(); m_lost = 0; m_silent.clear();
        });
        try {
            const auto& video = m_profile.video;
            std::unique_ptr<WindowsVideo> nativeVideo;
            Frame frame;
            if (m_synthetic) {
                testFrame(frame.p, video, 0);
            } else {
                nativeVideo = std::make_unique<WindowsVideo>(video, stop);
                { QMutexLocker lock(&m_mutex); m_warning = nativeVideo->warning(); }
                const auto deadline = windowsClock100ns() + 50000000;
                while (!stop && !nativeVideo->read(frame.p)) {
                    if (windowsClock100ns() >= deadline) throw std::runtime_error("Aucune image reçue. Affichez la fenêtre choisie avant d'activer le replay.");
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
                if (stop) return {};
            }
            const qint64 audioEpoch = windowsClock100ns();
            Codec encoder; openVideo(encoder, frame.p);
            std::vector<std::unique_ptr<AudioEncoder>> audio;
            int streamIndex = 1;
            for (const auto& track : m_profile.tracks) {
                QList<AudioSource> selected;
                for (const auto& sourceConfig : m_profile.sources) if (sourceConfig.enabled && sourceConfig.trackId == track.id) selected.append(sourceConfig);
                if (selected.isEmpty()) continue;
                if (streamIndex > 16) throw std::runtime_error("Cette version prend en charge jusqu'à 16 pistes audio actives.");
                auto item = std::make_unique<AudioEncoder>(); item->id = track.id; item->name = track.name; item->stream = streamIndex++;
                openAudio(*item);
                if (!m_synthetic) for (const auto& sourceConfig : selected) {
                    if (stop) return {};
                    auto configured = sourceConfig;
                    if (configured.kind == SourceKind::ApplicationMix) configured.excludedApplications = applicationExclusions(configured, m_profile);
                    auto capture = std::make_unique<WindowsAudio>(configured); capture->start(audioEpoch); item->sources.push_back(std::move(capture));
                }
                audio.push_back(std::move(item));
            }
            { QMutexLocker lock(&m_mutex); m_streams.append({parameters(encoder.p), encoder.p->time_base, QStringLiteral("Vidéo")});
              for (const auto& item : audio) m_streams.append({parameters(item->codec.p), item->codec.p->time_base, item->name}); }
            if (stop) return {};
            if (nativeVideo) for (int i = 0; i < 3; ++i) { av_frame_unref(frame.p); nativeVideo->read(frame.p); }
            const qint64 epoch = windowsClock100ns();
            const qint64 audioOffset = (epoch - audioEpoch) * 48000 / 10000000;
            // An image taken late is still stamped on time: the wait must not add its own jitter.
            const FineWait pause(true);
            // Never more than a millisecond at a time, so that stopping is immediate.
            const auto waitUntil = [&](qint64 deadline) { for (qint64 left; !stop && (left = deadline - (windowsClock100ns() - epoch)) > 0;) pause.waitTicks(std::min<qint64>(left, 10000)); };
            ready(m_encoder);
            qint64 audioPosition = 0;
            qint64 videoPosition = 0;
            // Images encoded and images given up, second by second, over the last five.
            std::deque<std::pair<int, int>> seconds{{0, 0}}; qint64 second = 0;
            auto encodeAudioUntil = [&](qint64 elapsed100ns) {
                // Leave 120 ms for input callbacks and driver buffering; missing input is silence.
                const qint64 available = std::max<qint64>(0, (elapsed100ns - 1200000) * 48000 / 10000000);
                while (!audio.empty() && audioPosition + 1024 <= available && !stop) {
                    for (auto& item : audio) {
                        check(av_frame_make_writable(item->frame.p), QStringLiteral("Buffer audio"));
                        auto* left = reinterpret_cast<float*>(item->frame.p->data[0]); auto* right = reinterpret_cast<float*>(item->frame.p->data[1]);
                        std::fill_n(left, 1024, 0.0f); std::fill_n(right, 1024, 0.0f);
                        if (m_synthetic) for (int sample = 0; sample < 1024; ++sample) {
                            const float value = float(0.12 * std::sin(2 * 3.141592653589793 * (220 + item->stream * 110) * double(audioPosition + sample) / 48000)); left[sample] = value; right[sample] = value;
                        }
                        else for (auto& input : item->sources) {
                            const auto error = input->error(); if (!error.isEmpty()) throw std::runtime_error(error.toStdString());
                            input->mix(audioPosition + audioOffset, 1024, left, right);
                        }
                        for (int sample = 0; sample < 1024; ++sample) { left[sample] = std::clamp(left[sample], -1.0f, 1.0f); right[sample] = std::clamp(right[sample], -1.0f, 1.0f); }
                        item->frame.p->pts = audioPosition; encode(item->codec.p, item->frame.p, item->stream);
                    }
                    audioPosition += 1024;
                }
            };
            while (!stop) {
                const qint64 pts = videoPosition;
                const qint64 deadline = av_rescale_q(pts, encoder.p->time_base, AVRational{1, 10000000});
                waitUntil(deadline);
                if (stop) break;
                frame.p->pts = pts; encode(encoder.p, frame.p, 0);
                { QMutexLocker lock(&m_mutex); ++m_frames; }
                encodeAudioUntil(windowsClock100ns() - epoch);
                av_frame_unref(frame.p);
                ++videoPosition; ++seconds.back().first;
                // An encoder that does not keep up must not slow time down: the images it had no
                // time for are given up, and the picture stays in step with the sound.
                const qint64 due = av_rescale_q(windowsClock100ns() - epoch, AVRational{1, 10000000}, encoder.p->time_base);
                if (due > videoPosition) { seconds.back().second += int(due - videoPosition); videoPosition = due; }
                if (const qint64 now = (windowsClock100ns() - epoch) / 10000000; now != second) {
                    second = now; int encoded = 0, lost = 0; for (const auto& [kept, given] : seconds) { encoded += kept; lost += given; }
                    QStringList silent; for (const auto& item : audio) for (const auto& input : item->sources) if (!input->unavailable().isEmpty()) silent.append(input->source().name);
                    { QMutexLocker lock(&m_mutex); m_lost = encoded + lost ? double(lost) / (encoded + lost) : 0; m_silent = silent; }
                    seconds.emplace_back(0, 0); while (seconds.size() > 5) seconds.pop_front();
                }
                if (nativeVideo) {
                    // Read at the next output deadline, keeping the GPU queue bounded.
                    const auto nextDeadline = av_rescale_q(videoPosition, encoder.p->time_base, AVRational{1, 10000000});
                    waitUntil(nextDeadline);
                    if (stop) break;
                    nativeVideo->read(frame.p); continue;
                }
                testFrame(frame.p, video, videoPosition);
            }
            return {};
        } catch (const std::exception& error) { return QString::fromUtf8(error.what()); }
    }
private:
    void openVideo(Codec& result, AVFrame* first) {
        const auto& video = m_profile.video;
        QStringList candidates;
        if (m_synthetic) candidates = {"libopenh264"};
        else if (video.encoder == "auto") candidates = {video.codec + "_nvenc", video.codec + "_amf"};
        else if (video.encoder == "nvenc" || video.encoder == "amf") candidates = {video.codec + '_' + video.encoder};
        else throw std::runtime_error("Encodeur inconnu : choisissez Automatique, NVIDIA NVENC ou AMD AMF dans les réglages.");
        const bool constantQuality = video.rateControl == "quality", thorough = video.effort == "thorough";
        const qint64 bitrate = qint64(video.bitrateMbps) * 1000000;
        const QByteArray quality = QByteArray::number(video.quality);
        QStringList errors;
        for (const auto& name : candidates) {
            const auto* codec = avcodec_find_encoder_by_name(name.toUtf8().constData()); if (!codec) { errors.append(name + ": absent"); continue; }
            const bool nvenc = name.endsWith("nvenc"), amf = name.endsWith("amf");
            // Each step asks a little less of the GPU, down to what every card of the family accepts:
            // B-frames used as references, then plain B-frames, then none. The buffer lives in memory,
            // so nothing here is tuned for latency.
            for (int step = nvenc && thorough ? 2 : amf ? 1 : 0; step >= 0; --step) {
                avcodec_free_context(&result.p); result.p = avcodec_alloc_context3(codec); if (!result.p) throw std::bad_alloc();
                auto* context = result.p; context->width = video.width; context->height = video.height;
                context->time_base = {1, video.fps}; context->framerate = {video.fps, 1}; context->gop_size = video.fps * 2;
                context->max_b_frames = 0; context->pix_fmt = AVPixelFormat(first->format);
                context->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
                context->sample_aspect_ratio = {1, 1}; context->color_primaries = AVCOL_PRI_BT709; context->color_trc = AVCOL_TRC_BT709; context->colorspace = AVCOL_SPC_BT709;
                if (first->hw_frames_ctx) context->hw_frames_ctx = av_buffer_ref(first->hw_frames_ctx);
                // The last resort on AMD is a plain constant bitrate, which every driver accepts.
                const bool legacy = amf && step == 0, tuned = (nvenc || amf) && !legacy;
                // A target lets hard scenes borrow from calm ones; a constant quality only knows its ceiling.
                context->bit_rate = tuned && constantQuality ? 0 : bitrate;
                context->rc_max_rate = tuned && !constantQuality ? bitrate * 3 / 2 : bitrate;
                context->rc_buffer_size = int(tuned ? bitrate * 2 : bitrate);
                AVDictionary* options = nullptr;
                const auto set = [&options](const char* key, const char* value) { av_dict_set(&options, key, value, 0); };
                if (nvenc) {
                    set("preset", thorough ? "p5" : "p4"); set("tune", "hq"); set("rc", "vbr"); set("delay", "0");
                    if (constantQuality) set("cq", quality.constData());
                    if (step >= 1) context->max_b_frames = 2;
                    if (step == 2) set("b_ref_mode", "middle");
                } else if (legacy) {
                    set("usage", "lowlatency"); set("rc", "cbr");
                } else if (amf) {
                    set("usage", "transcoding"); set("quality", thorough ? "quality" : "speed");
                    if (constantQuality) { set("rc", "cqp"); for (const char* key : {"qp_i", "qp_p", "qp_b"}) set(key, quality.constData()); }
                    else set("rc", "vbr_peak");
                }
                const int status = avcodec_open2(context, codec, &options); av_dict_free(&options);
                if (status >= 0) { QMutexLocker lock(&m_mutex); m_encoder = name; return; }
                char message[AV_ERROR_MAX_STRING_SIZE]; av_strerror(status, message, sizeof(message)); errors.append(name + ": " + QString::fromUtf8(message));
            }
        }
        throw std::runtime_error((QStringLiteral("Aucun encodeur matériel disponible. Vérifiez le pilote GPU et le codec choisi.\n") + errors.join('\n')).toStdString());
    }
    void openAudio(AudioEncoder& item) {
        const auto* codec = avcodec_find_encoder(AV_CODEC_ID_AAC); if (!codec) throw std::runtime_error("Encodeur AAC indisponible.");
        item.codec.p = avcodec_alloc_context3(codec); if (!item.codec.p) throw std::bad_alloc();
        auto* context = item.codec.p; context->sample_rate = 48000; context->sample_fmt = AV_SAMPLE_FMT_FLTP;
        av_channel_layout_default(&context->ch_layout, 2); context->time_base = {1, 48000}; context->bit_rate = 192000; context->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        check(avcodec_open2(context, codec, nullptr), QStringLiteral("Encodeur audio AAC"));
        item.frame.p->nb_samples = context->frame_size; item.frame.p->format = context->sample_fmt; item.frame.p->sample_rate = context->sample_rate;
        check(av_channel_layout_copy(&item.frame.p->ch_layout, &context->ch_layout), QStringLiteral("Canaux audio"));
        if (context->frame_size != 1024) throw std::runtime_error("Taille de trame AAC inattendue.");
        check(av_frame_get_buffer(item.frame.p, 0), QStringLiteral("Allocation audio"));
    }
    void encode(AVCodecContext* encoder, AVFrame* frame, int stream) {
        check(avcodec_send_frame(encoder, frame), QStringLiteral("Encodage de la piste"));
        Packet packet;
        int status;
        while ((status = avcodec_receive_packet(encoder, packet.p)) >= 0) {
            EncodedPacket item; item.stream = stream;
            item.ptsUs = av_rescale_q(packet.p->pts, encoder->time_base, microseconds); item.dtsUs = av_rescale_q(packet.p->dts, encoder->time_base, microseconds);
            const auto duration = packet.p->duration > 0 ? packet.p->duration : (stream == 0 ? 1 : 1024);
            item.durationUs = av_rescale_q(duration, encoder->time_base, microseconds); item.keyframe = packet.p->flags & AV_PKT_FLAG_KEY;
            item.data = QByteArray(reinterpret_cast<const char*>(packet.p->data), packet.p->size);
            { QMutexLocker lock(&m_mutex); m_buffer.push(std::move(item)); }
            av_packet_unref(packet.p);
        }
        if (status != AVERROR(EAGAIN) && status != AVERROR_EOF) check(status, QStringLiteral("Sortie de l'encodeur"));
    }
    RecordingProfile m_profile;
    bool m_synthetic;
    mutable QMutex m_mutex;
    ReplayBuffer m_buffer;
    QList<Stream> m_streams;
    QString m_encoder, m_warning;
    quint64 m_frames = 0;
    double m_lost = 0;
    QStringList m_silent;
};
}
std::shared_ptr<CaptureBackend> createCaptureBackend(const RecordingProfile& profile) { return std::make_shared<WindowsCapture>(profile); }
// Test-only factory: generated video/audio never reads a user's screen or microphone.
std::shared_ptr<CaptureBackend> createSyntheticCaptureBackend(const RecordingProfile& profile) { return std::make_shared<WindowsCapture>(profile, true); }
}
