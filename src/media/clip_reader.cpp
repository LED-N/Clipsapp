#include "clip_reader.h"
#include <QFileInfo>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
#include <stdexcept>
#include <thread>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixdesc.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}
namespace clipsapp {
namespace {
void check(int code, const char* action) {
    if (code < 0) { char text[AV_ERROR_MAX_STRING_SIZE]; av_strerror(code, text, sizeof(text)); throw std::runtime_error(std::string(action) + " : " + text); }
}
struct Decoder {
    AVCodecContext* context = nullptr;
    SwrContext* resampler = nullptr;
    AVPixelFormat hardwareFormat = AV_PIX_FMT_NONE;
    qint64 nextAudio = AV_NOPTS_VALUE;
    qint64 decodedMs = -1, endMs = 0;
    bool flushed = false, enabled = true;
    std::deque<AVPacket*> pending;
    void clearPackets() { for (auto* packet : pending) av_packet_free(&packet); pending.clear(); }
    ~Decoder() { clearPackets(); swr_free(&resampler); avcodec_free_context(&context); }
};
struct VideoFrame { qint64 ms; QImage image; };
struct Samples { qint64 start; std::vector<float> data; };
AVPixelFormat hardwareFormat(AVCodecContext* context, const AVPixelFormat* formats) {
    const auto expected = *static_cast<AVPixelFormat*>(context->opaque);
    for (auto* format = formats; *format != AV_PIX_FMT_NONE; ++format) if (*format == expected) return expected;
    return avcodec_default_get_format(context, formats);
}
}
struct ClipReader::Impl {
    ClipInfo info;
    AVFormatContext* file = nullptr;
    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    AVFrame* cpu = av_frame_alloc();
    SwsContext* scaler = nullptr;
    SwsContext* fitScaler = nullptr;
    AVFrame* view = av_frame_alloc();
    AVFrame* target = av_frame_alloc();
    std::vector<std::unique_ptr<Decoder>> decoders;
    std::vector<std::deque<Samples>> audio;
    std::deque<VideoFrame> video;
    std::vector<QImage> spare;
    QImage lastImage;
    void recycle(QImage&& image) { if (!image.isNull() && output.fit && spare.size() < 4) spare.push_back(std::move(image)); }
    qint64 originUs = 0, readMs = -1, targetMs = 0, presentMs = -1;
    qint64 pendingBytes = 0;
    bool eof = false, audioOnly = false;
    const std::atomic_bool* stop = nullptr;
    FrameOutput output;
    QSize outputSize(QSize region) const {
        if (!output.fit) return region.width() <= output.box.width() && region.height() <= output.box.height() ? region : region.scaled(output.box, Qt::KeepAspectRatio).expandedTo({2, 2});
        return region.scaled(output.box, Qt::KeepAspectRatio).expandedTo({2, 2});
    }
    ~Impl() {
        decoders.clear(); sws_freeContext(scaler); sws_freeContext(fitScaler); av_frame_free(&view); av_frame_free(&target); av_frame_free(&frame); av_frame_free(&cpu); av_packet_free(&packet); avformat_close_input(&file);
    }
    void open(const QString& path, bool hardware) {
        const QFileInfo source(path);
        if (!source.isFile()) throw std::runtime_error("Le clip n'est plus disponible.");
        info.path = source.absoluteFilePath();
        file = avformat_alloc_context(); if (!file || !packet || !frame || !cpu) throw std::bad_alloc();
        file->interrupt_callback = {[](void* opaque) { const auto* self = static_cast<Impl*>(opaque); return self->stop && self->stop->load() ? 1 : 0; }, this};
        AVDictionary* options = nullptr; av_dict_set(&options, "protocol_whitelist", "file", 0);
        const int opened = avformat_open_input(&file, info.path.toUtf8().constData(), nullptr, &options); av_dict_free(&options); check(opened, "Ouverture du clip");
        check(avformat_find_stream_info(file, nullptr), "Lecture des pistes");
        info.videoStream = av_find_best_stream(file, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0); check(info.videoStream, "Piste vidéo absente");
        originUs = file->start_time == AV_NOPTS_VALUE ? 0 : file->start_time;
        info.durationMs = file->duration == AV_NOPTS_VALUE ? 0 : file->duration / 1000;
        auto* stream = file->streams[info.videoStream]; info.size = {stream->codecpar->width, stream->codecpar->height};
        info.videoBitrate = stream->codecpar->bit_rate;
        info.videoCodec = QString::fromUtf8(avcodec_get_name(stream->codecpar->codec_id));
        const auto rate = av_guess_frame_rate(file, stream, nullptr); if (rate.den && rate.num) info.fps = av_q2d(rate);
        if (info.durationMs <= 0 && stream->duration != AV_NOPTS_VALUE) info.durationMs = av_rescale_q(stream->duration, stream->time_base, {1, 1000});
        if (info.durationMs <= 0 || info.size.width() < 2 || info.size.height() < 2) throw std::runtime_error("Durée ou dimensions du clip invalides.");
        if (file->nb_streams > 128) throw std::runtime_error("Ce fichier contient trop de pistes.");
        decoders.resize(file->nb_streams); audio.resize(file->nb_streams);
        for (unsigned index = 0; index < file->nb_streams; ++index) {
            auto* input = file->streams[index]; const bool isVideo = int(index) == info.videoStream;
            if (isVideo && audioOnly) continue;
            if (!isVideo && (input->codecpar->codec_type != AVMEDIA_TYPE_AUDIO || info.audio.size() >= 16)) continue;
            const auto* codec = avcodec_find_decoder(input->codecpar->codec_id); if (!codec) throw std::runtime_error("Codec de lecture indisponible.");
            auto decoder = std::make_unique<Decoder>(); decoder->context = avcodec_alloc_context3(codec); if (!decoder->context) throw std::bad_alloc();
            auto* context = decoder->context; check(avcodec_parameters_to_context(context, input->codecpar), "Paramètres du décodeur");
            context->pkt_timebase = input->time_base; context->thread_count = 2;
            decoder->endMs = input->duration == AV_NOPTS_VALUE ? info.durationMs : av_rescale_q(input->duration, input->time_base, {1, 1000}) + (input->start_time == AV_NOPTS_VALUE ? 0 : av_rescale_q(input->start_time, input->time_base, {1, 1000})) - originUs / 1000;
            if (isVideo && hardware) {
#ifdef Q_OS_WIN
                constexpr auto deviceType = AV_HWDEVICE_TYPE_D3D11VA;
#else
                constexpr auto deviceType = AV_HWDEVICE_TYPE_CUDA;
#endif
                for (int configIndex = 0; const auto* config = avcodec_get_hw_config(codec, configIndex); ++configIndex) {
                    if (config->device_type != deviceType || !(config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX)) continue;
                    if (av_hwdevice_ctx_create(&context->hw_device_ctx, deviceType, nullptr, nullptr, 0) >= 0) {
                        decoder->hardwareFormat = config->pix_fmt; context->opaque = &decoder->hardwareFormat; context->get_format = hardwareFormat;
                        info.decoder = QString::fromUtf8(av_hwdevice_get_type_name(deviceType));
                    }
                    break;
                }
            }
            check(avcodec_open2(context, codec, nullptr), "Initialisation du décodeur");
            if (!isVideo) {
                const auto* title = av_dict_get(input->metadata, "handler_name", nullptr, 0);
                if (!title || !strcmp(title->value, "SoundHandler")) title = av_dict_get(input->metadata, "title", nullptr, 0);
                if (!title) title = av_dict_get(input->metadata, "name", nullptr, 0);
                info.audio.append({int(index), title ? QString::fromUtf8(title->value) : QStringLiteral("Piste %1").arg(info.audio.size() + 1), QString::fromUtf8(avcodec_get_name(input->codecpar->codec_id))});
            }
            decoders[index] = std::move(decoder);
        }
        if (info.decoder.isEmpty()) info.decoder = QStringLiteral("Logiciel");
    }
    void receive(int index) {
        auto& decoder = *decoders[size_t(index)]; auto* stream = file->streams[index];
        int result;
        while ((result = avcodec_receive_frame(decoder.context, frame)) >= 0) {
            const qint64 pts = frame->best_effort_timestamp;
            if (index == info.videoStream) {
                const qint64 ms = pts == AV_NOPTS_VALUE ? std::max<qint64>(0, readMs) : av_rescale_q(pts, stream->time_base, {1, 1000}) - originUs / 1000;
                const qint64 duration = frame->duration > 0 ? av_rescale_q(frame->duration, stream->time_base, {1, 1000}) : qint64(std::ceil(1000.0 / info.fps));
                decoder.decodedMs = std::max(decoder.decodedMs, ms + std::max<qint64>(1, duration));
                if (ms >= targetMs - 1000.0 / info.fps && (presentMs < 0 || ms + std::max<qint64>(1, duration) > presentMs)) {
                    AVFrame* image = frame;
                    if (frame->hw_frames_ctx) {
                        // The memory that receives the GPU picture is kept from one image to the next:
                        // allocating a dozen megabytes sixty times per second costs more than filling them.
                        const auto software = reinterpret_cast<AVHWFramesContext*>(frame->hw_frames_ctx->data)->sw_format;
                        if (cpu->buf[0] && (cpu->width != frame->width || cpu->height != frame->height || cpu->format != software || !av_frame_is_writable(cpu))) av_frame_unref(cpu);
                        if (!cpu->buf[0]) { cpu->format = software; cpu->width = frame->width; cpu->height = frame->height; if (av_frame_get_buffer(cpu, 0) < 0) av_frame_unref(cpu); }
                        if (av_hwframe_transfer_data(cpu, frame, 0) < 0) { av_frame_unref(cpu); check(av_hwframe_transfer_data(cpu, frame, 0), "Lecture de l'image GPU"); }
                        cpu->colorspace = frame->colorspace; cpu->color_range = frame->color_range; image = cpu;
                    }
                    // A cropped presentation reads its region in place: each plane starts at the region's corner.
                    const auto* layout = av_pix_fmt_desc_get(AVPixelFormat(image->format));
                    QRect region(0, 0, image->width, image->height);
                    if (output.crop.isValid() && layout && !(layout->flags & (AV_PIX_FMT_FLAG_PAL | AV_PIX_FMT_FLAG_BITSTREAM | AV_PIX_FMT_FLAG_HWACCEL))) {
                        const auto wanted = output.crop.intersected(region);
                        const QRect even(wanted.x() & ~1, wanted.y() & ~1, wanted.width() & ~1, wanted.height() & ~1);
                        if (even.width() >= 2 && even.height() >= 2) region = even;
                    }
                    const uint8_t* planes[4]{}; int planeStrides[4]{};
                    for (int plane = 0; plane < 4 && image->data[plane]; ++plane) {
                        const bool chroma = layout && layout->nb_components >= 3 && !(layout->flags & AV_PIX_FMT_FLAG_RGB) && (plane == 1 || plane == 2);
                        int step = 1; if (layout) for (int component = 0; component < layout->nb_components; ++component) if (layout->comp[component].plane == plane) step = layout->comp[component].step;
                        planes[plane] = image->data[plane] + (region.y() >> (chroma ? layout->log2_chroma_h : 0)) * image->linesize[plane] + (region.x() >> (chroma ? layout->log2_chroma_w : 0)) * step;
                        planeStrides[plane] = image->linesize[plane];
                    }
                    const QSize preview = outputSize(region.size());
                    // Opaque pixels: drawn unscaled, such an image is copied to the window as it is.
                    // Likewise for the images handed out: one that nobody shows any more is written over.
                    QImage pixels;
                    for (auto it = spare.begin(); it != spare.end(); ++it) if (it->size() == preview && it->isDetached()) { pixels = std::move(*it); spare.erase(it); break; }
                    if (pixels.isNull()) pixels = QImage(preview, QImage::Format_RGB32);
                    if (pixels.isNull()) throw std::bad_alloc();
                    const bool resized = preview != region.size(), hd = image->colorspace == AVCOL_SPC_BT709;
                    // A screen-sized image is several times the work of a preview: its conversion is shared
                    // between a few threads. Should that path refuse the frame, the single-threaded one takes over.
                    bool converted = false;
                    if (output.fit && view && target) {
                        if (!fitScaler && (fitScaler = sws_alloc_context())) fitScaler->threads = int(std::clamp(std::thread::hardware_concurrency() / 2, 2u, 8u));
                        av_frame_unref(view); av_frame_unref(target);
                        if (fitScaler && av_frame_ref(view, image) >= 0) {
                            fitScaler->flags = resized ? SWS_BILINEAR : SWS_FAST_BILINEAR;
                            for (int plane = 0; plane < 4; ++plane) view->data[plane] = const_cast<uint8_t*>(planes[plane]);
                            view->width = region.width(); view->height = region.height();
                            view->colorspace = hd ? AVCOL_SPC_BT709 : AVCOL_SPC_SMPTE170M; if (view->color_range == AVCOL_RANGE_UNSPECIFIED) view->color_range = AVCOL_RANGE_MPEG;
                            target->format = AV_PIX_FMT_BGRA; target->width = preview.width(); target->height = preview.height();
                            target->data[0] = pixels.bits(); target->linesize[0] = int(pixels.bytesPerLine()); target->colorspace = AVCOL_SPC_RGB; target->color_range = AVCOL_RANGE_JPEG;
                            target->buf[0] = av_buffer_create(pixels.bits(), size_t(pixels.sizeInBytes()), [](void*, uint8_t*) {}, nullptr, 0);
                            converted = target->buf[0] && sws_scale_frame(fitScaler, target, view) >= 0;
                        }
                        av_frame_unref(view); av_frame_unref(target);
                    }
                    if (!converted) {
                        scaler = sws_getCachedContext(scaler, region.width(), region.height(), AVPixelFormat(image->format), preview.width(), preview.height(), AV_PIX_FMT_BGRA, output.fit && resized ? SWS_BILINEAR : SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
                        if (!scaler) throw std::bad_alloc();
                        const auto* coefficients = sws_getCoefficients(hd ? SWS_CS_ITU709 : SWS_CS_DEFAULT);
                        sws_setColorspaceDetails(scaler, coefficients, image->color_range == AVCOL_RANGE_JPEG, coefficients, 1, 0, 1 << 16, 1 << 16);
                        uint8_t* data[]{pixels.bits(), nullptr, nullptr, nullptr}; const int strides[]{int(pixels.bytesPerLine()), 0, 0, 0};
                        check(sws_scale(scaler, planes, planeStrides, 0, region.height(), data, strides), "Conversion de l'aperçu");
                    }
                    // Never more decoded images in memory than about 128 Mio of them.
                    const size_t limit = size_t(std::clamp<qint64>(128LL * 1024 * 1024 / std::max<qint64>(1, pixels.sizeInBytes()), 3, 24));
                    video.push_back({ms, std::move(pixels)}); while (video.size() > limit) { recycle(std::move(video.front().image)); video.pop_front(); }
                }
            } else {
                const qint64 delay = decoder.resampler ? swr_get_delay(decoder.resampler, 48000) : 0;
                qint64 start = pts == AV_NOPTS_VALUE ? (decoder.nextAudio == AV_NOPTS_VALUE ? targetMs * 48 : decoder.nextAudio) : av_rescale_q(pts, stream->time_base, {1, 48000}) - originUs * 48 / 1000 - delay;
                if (decoder.nextAudio != AV_NOPTS_VALUE && std::abs(start - decoder.nextAudio) <= 2) start = decoder.nextAudio;
                if (!decoder.resampler) {
                    AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
                    check(swr_alloc_set_opts2(&decoder.resampler, &stereo, AV_SAMPLE_FMT_FLT, 48000, &frame->ch_layout, AVSampleFormat(frame->format), frame->sample_rate, 0, nullptr), "Format audio de lecture");
                    if (frame->ch_layout.nb_channels == 1) { const double matrix[]{1, 1}; check(swr_set_matrix(decoder.resampler, matrix, 1), "Lecture du micro mono"); }
                    check(swr_init(decoder.resampler), "Conversion des pistes audio");
                }
                const int capacity = swr_get_out_samples(decoder.resampler, frame->nb_samples); check(capacity, "Taille audio");
                Samples samples{start, std::vector<float>(size_t(capacity) * 2)}; uint8_t* destination = reinterpret_cast<uint8_t*>(samples.data.data());
                const int count = swr_convert(decoder.resampler, &destination, capacity, const_cast<const uint8_t**>(frame->extended_data), frame->nb_samples); check(count, "Décodage audio");
                samples.data.resize(size_t(count) * 2); decoder.nextAudio = start + count;
                decoder.decodedMs = decoder.nextAudio / 48;
                if (start + count > targetMs * 48) audio[size_t(index)].push_back(std::move(samples));
                while (audio[size_t(index)].size() > 100) audio[size_t(index)].pop_front();
            }
            av_frame_unref(frame);
        }
        if (result != AVERROR(EAGAIN) && result != AVERROR_EOF) check(result, "Décodage du clip");
    }
};
ClipReader::ClipReader(const QString& path, bool hardware, bool audioOnly) : m_impl(std::make_unique<Impl>()) { m_impl->audioOnly = audioOnly; m_impl->open(path, hardware); }
ClipReader::~ClipReader() = default;
ClipInfo ClipReader::info() const { return m_impl->info; }
bool ClipReader::setAudioTracks(const QList<TrackEdit>& tracks) {
    auto& reader = *m_impl; bool changed = false;
    for (const auto& audio : reader.info.audio) {
        auto& decoder = reader.decoders[size_t(audio.stream)];
        const bool enabled = std::any_of(tracks.cbegin(), tracks.cend(), [&](const auto& track) { return track.stream == audio.stream; });
        if (decoder->enabled == enabled) continue;
        changed = true; decoder->enabled = enabled;
        for (const auto* packet : decoder->pending) reader.pendingBytes -= packet->size;
        decoder->clearPackets(); reader.audio[size_t(audio.stream)].clear();
        avcodec_flush_buffers(decoder->context); swr_free(&decoder->resampler);
        decoder->nextAudio = AV_NOPTS_VALUE; decoder->decodedMs = -1; decoder->flushed = false;
    }
    return changed;
}
void ClipReader::seek(qint64 ms) {
    auto& reader = *m_impl; const qint64 timestamp = ms * 1000 + reader.originUs;
    check(avformat_seek_file(reader.file, -1, INT64_MIN, timestamp, timestamp, 0), "Déplacement dans le clip");
    for (auto& decoder : reader.decoders) if (decoder) { avcodec_flush_buffers(decoder->context); swr_free(&decoder->resampler); decoder->nextAudio = AV_NOPTS_VALUE; decoder->decodedMs = -1; decoder->flushed = false; decoder->clearPackets(); }
    reader.pendingBytes = 0;
    for (auto& queue : reader.audio) queue.clear(); reader.video.clear(); reader.lastImage = {};
    reader.readMs = -1; reader.targetMs = ms; reader.eof = false; av_packet_unref(reader.packet);
}
bool ClipReader::setFrameOutput(const FrameOutput& output) {
    if (m_impl->output == output || output.box.width() < 2 || output.box.height() < 2) return false;
    m_impl->output = output; return true;
}
QSize ClipReader::frameSize() const {
    const QRect whole(QPoint(), m_impl->info.size); const auto region = m_impl->output.crop.isValid() ? m_impl->output.crop.intersected(whole) : whole;
    return m_impl->outputSize(region.isEmpty() ? whole.size() : region.size());
}
void ClipReader::readUntil(qint64 endMs, const std::atomic_bool& stop, bool videoOnly, qint64 videoEndMs, qint64 presentMs) {
    auto& reader = *m_impl; reader.stop = &stop; reader.presentMs = presentMs;
    // Container packet order is not necessarily timestamp order. In particular,
    // GSR MP4 files may deliver video a second ahead of the other tracks.
    // Wait for each decoded stream and defer ahead-of-time packets, so neither
    // audio gets replaced by silence nor the current video frame gets evicted.
    const Decoder* videoDecoder = reader.info.videoStream >= 0 ? reader.decoders[size_t(reader.info.videoStream)].get() : nullptr;
    const auto needs = [endMs, videoEndMs, videoDecoder](const Decoder& decoder) {
        const qint64 end = videoEndMs >= 0 && &decoder == videoDecoder ? std::min(endMs, videoEndMs) : endMs;
        return !decoder.flushed && decoder.decodedMs < std::min(end, decoder.endMs);
    };
    for (int budget = 0; budget < 16384 && !stop; ++budget) {
        bool missing = false;
        for (size_t index = 0; index < reader.decoders.size(); ++index) {
            auto& decoder = reader.decoders[index]; if (!decoder || !decoder->enabled || (videoOnly && int(index) != reader.info.videoStream)) continue;
            while (!stop && needs(*decoder) && !decoder->pending.empty()) {
                auto* packet = decoder->pending.front(); decoder->pending.pop_front(); reader.pendingBytes -= packet->size;
                const int sent = avcodec_send_packet(decoder->context, packet); av_packet_free(&packet); check(sent, "Paquet différé"); reader.receive(int(index));
            }
            if (reader.eof && needs(*decoder) && decoder->pending.empty() && !decoder->flushed) {
                check(avcodec_send_packet(decoder->context, nullptr), "Fin de lecture"); reader.receive(int(index)); decoder->flushed = true;
            }
            missing |= needs(*decoder);
        }
        if (!missing || reader.eof || stop) break;
        const int result = av_read_frame(reader.file, reader.packet);
        if (result == AVERROR_EXIT && stop) return;
        if (result == AVERROR_EOF) {
            reader.eof = true;
            continue;
        }
        check(result, "Lecture du clip");
        const int index = reader.packet->stream_index;
        if (reader.packet->dts != AV_NOPTS_VALUE) reader.readMs = std::max(reader.readMs, av_rescale_q(reader.packet->dts, reader.file->streams[index]->time_base, {1, 1000}) - reader.originUs / 1000);
        if (reader.decoders[size_t(index)] && reader.decoders[size_t(index)]->enabled) {
            auto& decoder = *reader.decoders[size_t(index)];
            if (needs(decoder) && (!videoOnly || index == reader.info.videoStream)) { check(avcodec_send_packet(decoder.context, reader.packet), "Paquet de lecture"); reader.receive(index); }
            else {
                auto* copy = av_packet_clone(reader.packet); if (!copy) throw std::bad_alloc();
                decoder.pending.push_back(copy); reader.pendingBytes += copy->size;
                if (reader.pendingBytes > 64 * 1024 * 1024) throw std::runtime_error("Les pistes de ce fichier sont trop décalées pour l'aperçu en mémoire.");
            }
        }
        av_packet_unref(reader.packet);
    }
}
QImage ClipReader::frameAt(qint64 positionMs) {
    auto& reader = *m_impl;
    while (!reader.video.empty() && (reader.video.front().ms <= positionMs || reader.lastImage.isNull())) {
        reader.recycle(std::move(reader.lastImage)); reader.lastImage = std::move(reader.video.front().image); reader.video.pop_front();
    }
    return reader.lastImage;
}
void ClipReader::mix(qint64 first, int count, const QList<TrackEdit>& tracks, float* output) {
    std::fill_n(output, size_t(count) * 2, 0.0f);
    for (const auto& track : tracks) {
        if (track.stream < 0 || size_t(track.stream) >= m_impl->audio.size()) continue;
        auto& queue = m_impl->audio[size_t(track.stream)];
        for (auto it = queue.begin(); it != queue.end();) {
            const qint64 end = it->start + qint64(it->data.size() / 2);
            if (!track.muted) for (qint64 sample = std::max(first, it->start); sample < std::min(first + count, end); ++sample) {
                const size_t input = size_t(sample - it->start) * 2, destination = size_t(sample - first) * 2;
                output[destination] += float(it->data[input] * track.volume); output[destination + 1] += float(it->data[input + 1] * track.volume);
            }
            if (end <= first + count) it = queue.erase(it); else ++it;
        }
    }
    for (int i = 0; i < count * 2; ++i) output[i] = std::isfinite(output[i]) ? std::clamp(output[i], -1.0f, 1.0f) : 0;
}
}
