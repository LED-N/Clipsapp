#include <QtTest>
#include <QTemporaryDir>
#include <QtConcurrent/QtConcurrentRun>
#include "capture/replay_engine.h"
#include "platform/windows_audio.h"
#include <algorithm>
#include <stdexcept>
#include <thread>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}
namespace clipsapp { std::shared_ptr<CaptureBackend> createSyntheticCaptureBackend(const RecordingProfile&); }
using namespace clipsapp;
struct Run {
    std::shared_ptr<CaptureBackend> backend;
    std::atomic_bool stop{false};
    QFuture<QString> future;
    explicit Run(const RecordingProfile& profile) : backend(createSyntheticCaptureBackend(profile)) {
        future = QtConcurrent::run([this] { return backend->run(stop, [](QString) {}); });
    }
    ~Run() { stop = true; future.waitForFinished(); }
};
class FailedStart final : public CaptureBackend {
public:
    explicit FailedStart(const RecordingProfile& profile) : m_backend(createSyntheticCaptureBackend(profile)) {}
    QString run(std::atomic_bool& stop, const std::function<void(QString)>&) override {
        return m_backend->run(stop, [](QString) { throw std::runtime_error("Erreur de capture simulée."); });
    }
    CaptureStats stats() const override { return m_backend->stats(); }
    ExportTask prepareSave(const QString& path) const override { return m_backend->prepareSave(path); }
private:
    std::shared_ptr<CaptureBackend> m_backend;
};
struct Inspection { QString error; int videoFrames = 0; QList<int> audioFrames, crossings; QList<double> peak; QStringList titles; double duration = 0; };
Inspection inspect(const QString& path) {
    Inspection result; AVFormatContext* format = nullptr;
    if (avformat_open_input(&format, path.toUtf8().constData(), nullptr, nullptr) < 0) { result.error = "Cannot open clip"; return result; }
    avformat_find_stream_info(format, nullptr); result.duration = format->duration / double(AV_TIME_BASE);
    std::vector<AVCodecContext*> decoders(format->nb_streams, nullptr);
    result.audioFrames.fill(0, int(format->nb_streams));
    result.crossings.fill(0, int(format->nb_streams)); result.peak.fill(0, int(format->nb_streams)); QList<float> previous; previous.fill(0, int(format->nb_streams));
    for (unsigned i = 0; i < format->nb_streams; ++i) {
        const auto* codec = avcodec_find_decoder(format->streams[i]->codecpar->codec_id);
        decoders[i] = avcodec_alloc_context3(codec); avcodec_parameters_to_context(decoders[i], format->streams[i]->codecpar);
        if (avcodec_open2(decoders[i], codec, nullptr) < 0) result.error = "Cannot open decoder";
        const auto* title = av_dict_get(format->streams[i]->metadata, "handler_name", nullptr, 0); result.titles.append(title ? QString::fromUtf8(title->value) : QString());
    }
    auto* packet = av_packet_alloc(); auto* frame = av_frame_alloc();
    auto receive = [&](unsigned stream) {
        int code;
        while ((code = avcodec_receive_frame(decoders[stream], frame)) >= 0) {
            if (decoders[stream]->codec_type == AVMEDIA_TYPE_VIDEO) ++result.videoFrames;
            else {
                result.audioFrames[int(stream)] += frame->nb_samples;
                if (frame->format != AV_SAMPLE_FMT_FLTP) result.error = "Unexpected decoded audio format";
                else for (int sample = 0; sample < frame->nb_samples; ++sample) {
                    const float value = reinterpret_cast<float*>(frame->data[0])[sample];
                    result.peak[int(stream)] = std::max(result.peak[int(stream)], double(qAbs(value)));
                    if (previous[int(stream)] < 0 && value >= 0) ++result.crossings[int(stream)]; previous[int(stream)] = value;
                }
            }
            av_frame_unref(frame);
        }
        if (code != AVERROR(EAGAIN) && code != AVERROR_EOF) result.error = "Decoder rejected a frame";
    };
    while (av_read_frame(format, packet) >= 0) {
        const auto stream = unsigned(packet->stream_index);
        if (avcodec_send_packet(decoders[stream], packet) < 0) result.error = "Decoder rejected a packet";
        receive(stream); av_packet_unref(packet);
    }
    for (unsigned i = 0; i < decoders.size(); ++i) { avcodec_send_packet(decoders[i], nullptr); receive(i); avcodec_free_context(&decoders[i]); }
    av_frame_free(&frame); av_packet_free(&packet); avformat_close_input(&format); return result;
}
class MediaTests final : public QObject {
    Q_OBJECT
private slots:
    void savesDecodableVideoAndThreeNamedAudioTracksWithoutStopping() {
        auto profile = AppConfig::defaults().profiles.first(); profile.video.width = 320; profile.video.height = 192; profile.video.fps = 30; profile.video.bitrateMbps = 5; profile.replay.durationSeconds = 2;
        for (auto& source : profile.sources) source.enabled = true;
        Run capture(profile); QTemporaryDir directory; QVERIFY(directory.isValid());
        QTRY_VERIFY_WITH_TIMEOUT(capture.backend->stats().videoFrames >= 150 || capture.future.isFinished(), 10000);
        QVERIFY2(!capture.future.isFinished(), qPrintable(capture.future.isFinished() ? capture.future.result() : QString()));
        const auto before = capture.backend->stats().videoFrames;
        const auto path = directory.filePath(QStringLiteral("Clip avec espaces et été.mp4"));
        const auto saved = capture.backend->save(path); QVERIFY2(saved.error.isEmpty(), qPrintable(saved.error));
        const auto inspected = inspect(path); QVERIFY2(inspected.error.isEmpty(), qPrintable(inspected.error));
        QCOMPARE(inspected.titles, QStringList({QStringLiteral("Vidéo"), "Micro", "Jeu", "Discord"}));
        QVERIFY(inspected.videoFrames >= 50); QVERIFY(inspected.duration >= 2.0 && inspected.duration <= 4.2);
        for (int stream = 1; stream < 4; ++stream) {
            QVERIFY(inspected.audioFrames[stream] > 48000); QVERIFY(inspected.peak[stream] > 0.05);
            const double frequency = inspected.crossings[stream] * 48000.0 / inspected.audioFrames[stream]; QVERIFY(qAbs(frequency - (220 + stream * 110)) < 10);
        }
        QTRY_VERIFY_WITH_TIMEOUT(capture.backend->stats().videoFrames > before + 10, 2000);
        const auto duplicate = capture.backend->save(path); QVERIFY(!duplicate.error.isEmpty()); QVERIFY(QFile::exists(path));
        // Prepare a requested export, then stop before executing it. It must use
        // its own snapshot even though the capture buffer has been released.
        const auto pending = capture.backend->prepareSave(directory.filePath("requested-before-stop.mp4"));
        capture.stop = true; capture.future.waitForFinished(); QVERIFY2(capture.future.result().isEmpty(), qPrintable(capture.future.result()));
        QCOMPARE(capture.backend->stats().bytes, qint64(0)); QCOMPARE(capture.backend->stats().seconds, 0.0);
        QCOMPARE(capture.backend->stats().videoFrames, quint64(0)); QVERIFY(capture.backend->stats().encoder.isEmpty());
        const auto afterStop = capture.backend->save(directory.filePath("after-stop.mp4")); QVERIFY(!afterStop.error.isEmpty());
        QVERIFY(!QFile::exists(afterStop.path)); QVERIFY(QFile::exists(path));
        const auto requested = pending(); QVERIFY2(requested.error.isEmpty(), qPrintable(requested.error));
        QVERIFY(inspect(requested.path).error.isEmpty());
    }
    void recordsAt144ImagesPerSecondWithPictureAndSoundInStep() {
        auto profile = AppConfig::defaults().profiles.first(); profile.video.width = 320; profile.video.height = 192; profile.video.fps = 144; profile.video.bitrateMbps = 5; profile.replay.durationSeconds = 2;
        Run capture(profile); QTemporaryDir directory;
        QTRY_VERIFY_WITH_TIMEOUT(capture.backend->stats().videoFrames >= 3 * 144 || capture.future.isFinished(), 10000);
        QVERIFY2(!capture.future.isFinished(), qPrintable(capture.future.isFinished() ? capture.future.result() : QString()));
        const auto saved = capture.backend->save(directory.filePath("fast.mp4")); QVERIFY2(saved.error.isEmpty(), qPrintable(saved.error));
        const auto inspected = inspect(saved.path); QVERIFY2(inspected.error.isEmpty(), qPrintable(inspected.error));
        // Whatever the encoder had time for, the clip lasts what was recorded: an image given up is not time slowed down.
        QVERIFY(inspected.duration >= 2.0 && inspected.duration <= 4.2); QVERIFY(inspected.videoFrames >= 144); QVERIFY(inspected.videoFrames <= int(inspected.duration * 144) + 2);
        QVERIFY(qAbs(inspected.audioFrames[1] / 48000.0 - inspected.duration) < 0.4);
    }
    void groupsSourcesOnOneTrackAndOmitsDisabledSources() {
        auto profile = AppConfig::defaults().profiles.first(); profile.video.width = 320; profile.video.height = 192; profile.video.fps = 60; profile.video.bitrateMbps = 5;
        profile.sources[0].enabled = false; profile.sources[1].enabled = profile.sources[2].enabled = true; profile.sources[2].trackId = profile.sources[1].trackId;
        Run capture(profile); QTemporaryDir directory;
        QTRY_VERIFY_WITH_TIMEOUT(capture.backend->stats().videoFrames >= 90 || capture.future.isFinished(), 8000);
        QVERIFY2(!capture.future.isFinished(), qPrintable(capture.future.isFinished() ? capture.future.result() : QString()));
        const auto saved = capture.backend->save(directory.filePath("grouped.mp4")); QVERIFY2(saved.error.isEmpty(), qPrintable(saved.error));
        const auto inspected = inspect(saved.path); QCOMPARE(inspected.titles, QStringList({QStringLiteral("Vidéo"), "Jeu"})); QVERIFY2(inspected.error.isEmpty(), qPrintable(inspected.error));
    }
    void engineClearsOnStopFinishesRequestedExportAndRestartsEmpty() {
        auto profile = AppConfig::defaults().profiles.first();
        profile.gameName = "ARC Raiders";
        profile.video.width = 320; profile.video.height = 192; profile.video.fps = 60; profile.video.bitrateMbps = 5; profile.video.targetId = "synthetic";
        QTemporaryDir directory; ReplayEngine engine(nullptr, createSyntheticCaptureBackend);
        QSignalSpy saved(&engine, &ReplayEngine::saved), failed(&engine, &ReplayEngine::failed);
        QString error;
        QVERIFY(!engine.save(error));
        QVERIFY2(engine.start(profile, directory.path(), error), qPrintable(error));
        QTRY_VERIFY_WITH_TIMEOUT(engine.state() == ReplayEngine::State::Running && engine.stats().videoFrames >= 30, 5000);
        QVERIFY(engine.stats().bytes > 0);
        QVERIFY2(engine.save(error), qPrintable(error));
        engine.stop(); QCOMPARE(engine.state(), ReplayEngine::State::Stopping);
        QVERIFY(!engine.save(error));
        QTRY_COMPARE_WITH_TIMEOUT(engine.state(), ReplayEngine::State::Idle, 5000);
        QCOMPARE(engine.stats().bytes, qint64(0)); QCOMPARE(engine.stats().seconds, 0.0);
        QVERIFY(!engine.save(error));
        QTRY_COMPARE_WITH_TIMEOUT(saved.size(), 1, 5000); QCOMPARE(failed.size(), 0);
        const auto path = saved.first().first().toString(); QVERIFY(QFile::exists(path)); QVERIFY(inspect(path).error.isEmpty());
        QVERIFY(QFileInfo(path).fileName().startsWith("ARC Raiders-")); QCOMPARE(QFileInfo(path).dir().dirName(), QString("ARC Raiders"));
        QVERIFY2(engine.start(profile, directory.path(), error), qPrintable(error));
        QCOMPARE(engine.state(), ReplayEngine::State::Starting);
        QTRY_VERIFY_WITH_TIMEOUT(engine.state() == ReplayEngine::State::Running && engine.stats().videoFrames >= 10, 5000);
        QVERIFY(engine.stats().seconds < 0.5);
        engine.stop(); QTRY_COMPARE_WITH_TIMEOUT(engine.state(), ReplayEngine::State::Idle, 5000);
        QCOMPARE(engine.stats().bytes, qint64(0)); QCOMPARE(saved.size(), 1);
    }
    void engineClearsWhenStartupIsCancelledOrFails() {
        auto profile = AppConfig::defaults().profiles.first(); profile.video.width = 320; profile.video.height = 192; profile.video.targetId = "synthetic";
        QTemporaryDir directory; QString error;
        ReplayEngine cancelled(nullptr, createSyntheticCaptureBackend);
        QVERIFY2(cancelled.start(profile, directory.path(), error), qPrintable(error)); cancelled.stop();
        QTRY_COMPARE_WITH_TIMEOUT(cancelled.state(), ReplayEngine::State::Idle, 5000);
        QCOMPARE(cancelled.stats().bytes, qint64(0)); QCOMPARE(cancelled.stats().seconds, 0.0); QVERIFY(!cancelled.save(error));
        std::shared_ptr<CaptureBackend> backend;
        ReplayEngine broken(nullptr, [&](const RecordingProfile& config) { backend = std::make_shared<FailedStart>(config); return backend; });
        QSignalSpy failed(&broken, &ReplayEngine::failed);
        QVERIFY2(broken.start(profile, directory.path(), error), qPrintable(error));
        QTRY_COMPARE_WITH_TIMEOUT(broken.state(), ReplayEngine::State::Error, 5000);
        QCOMPARE(failed.size(), 1); QCOMPARE(broken.stats().bytes, qint64(0)); QCOMPARE(broken.stats().seconds, 0.0);
        QVERIFY(!broken.save(error));
        QCOMPARE(backend->stats().bytes, qint64(0)); QVERIFY(backend->stats().encoder.isEmpty());
        const auto rejected = backend->save(directory.filePath("after-error.mp4")); QVERIFY(!rejected.error.isEmpty()); QVERIFY(!QFile::exists(rejected.path));
    }
    void anAbsentMicrophoneSilencesItsTrackWithoutStoppingAnything() {
        // A device that does not exist is what an unplugged one looks like: nothing is opened, nothing is heard.
        AudioSource source; source.id = "absent"; source.name = "Micro USB"; source.kind = SourceKind::Microphone; source.targetId = "{0.0.1.00000000}.{00000000-0000-0000-0000-00000000c11a}";
        WindowsAudio audio(source); audio.start(windowsClock100ns());
        QVERIFY(audio.error().isEmpty()); QTRY_VERIFY_WITH_TIMEOUT(!audio.unavailable().isEmpty(), 2000); QCOMPARE(audio.source().name, QString("Micro USB"));
        std::vector<float> left(1024, 0.0f), right(1024, 0.0f); audio.mix(0, 1024, left.data(), right.data());
        QVERIFY(std::all_of(left.begin(), left.end(), [](float sample) { return sample == 0.0f; }));
        // Tried again every second, it stays missing without ever becoming an error.
        QTest::qWait(1300); QVERIFY(audio.error().isEmpty()); QVERIFY(!audio.unavailable().isEmpty());
    }
};
QTEST_GUILESS_MAIN(MediaTests)
#include "media_tests.moc"
