#include "capture/replay_engine.h"
#include "platform/source_catalog.h"
#include <QGuiApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtConcurrent/QtConcurrentRun>
#include <thread>
#include <cstdio>
#include <windows.h>
#include <psapi.h>
#include <QJsonArray>

using namespace clipsapp;
int main(int argc, char** argv) {
    QGuiApplication application(argc, argv);
    QCommandLineParser parser; parser.addHelpOption();
    parser.addOption({"output", "Explicit directory for local test clips", "directory"});
    parser.addOption({"app", "Optional application executable to capture separately", "path"});
    parser.addOption({"microphone", "Include the default microphone"});
    parser.addOption({"window-app", "Capture a visible window of this executable instead of the screen", "path"});
    parser.addOption({"fps", "Frame rate", "fps", "30"});
    parser.addOption({"hevc", "Use HEVC"});
    parser.addOption({"4k", "Use 4K resolution"});
    parser.addOption({"1440p", "Use 1440p resolution"});
    parser.addOption({"bitrate", "Target bitrate, or ceiling of the constant quality", "Mbit/s", "25"});
    parser.addOption({"quality", "Hold this constant quality instead of a bitrate", "level"});
    parser.addOption({"thorough", "Ask the encoder for its thorough effort"});
    parser.addOption({"seconds", "Observation duration", "seconds", "5"});
    parser.addOption({"buffer", "Replay duration", "seconds", "3"});
    parser.addOption({"automatic-audio", "Capture applications automatically"});
    parser.addOption({"no-save", "Only measure memory, without saving screen content"}); parser.process(application);
    if (!parser.isSet("output")) return 2;
    const auto directory = QDir(parser.value("output")).absolutePath(); if (!QDir().mkpath(directory)) return 2;
    auto profile = AppConfig::defaults().profiles.first();
    const auto catalog = SourceCatalog::discover();
    QString screen;
    for (const auto& target : catalog.videoTargets) if (target.id.startsWith("screen:")) { screen = target.id; break; }
    if (screen.isEmpty()) return 3;
    profile.video.targetId = screen; profile.video.fps = parser.value("fps").toInt(); profile.replay.durationSeconds = parser.value("buffer").toInt();
    if (parser.isSet("window-app")) {
        const auto prefix = "window:" + parser.value("window-app").toCaseFolded() + '|';
        profile.video.targetId.clear();
        for (const auto& target : catalog.videoTargets) if (target.id.startsWith(prefix, Qt::CaseInsensitive)) { profile.video.targetId = target.id; break; }
        if (profile.video.targetId.isEmpty()) return 4;
    }
    if (parser.isSet("hevc")) profile.video.codec = "hevc";
    if (parser.isSet("4k")) { profile.video.width = 3840; profile.video.height = 2160; }
    if (parser.isSet("1440p")) { profile.video.width = 2560; profile.video.height = 1440; }
    profile.video.bitrateMbps = parser.value("bitrate").toInt();
    if (parser.isSet("quality")) { profile.video.rateControl = "quality"; profile.video.quality = parser.value("quality").toInt(); }
    if (parser.isSet("thorough")) profile.video.effort = "thorough";
    profile.sources.clear();
    profile.sources.append({"output", "Son système", parser.isSet("automatic-audio") ? SourceKind::ApplicationMix : SourceKind::Output, "@default", "Par défaut", profile.tracks[1].id, true});
    if (parser.isSet("microphone")) profile.sources.append({"microphone", "Micro", SourceKind::Microphone, "@default", "Par défaut", profile.tracks[0].id, true});
    if (parser.isSet("app")) profile.sources.append({"app", "Application", SourceKind::Application, QDir::toNativeSeparators(QFileInfo(parser.value("app")).absoluteFilePath()), "Application", profile.tracks[2].id, true});
    const auto backend = createCaptureBackend(profile); std::atomic_bool stop{false}; std::atomic_bool ready{false};
    auto future = QtConcurrent::run([&] { return backend->run(stop, [&](QString) { ready = true; }); });
    const int observation = std::clamp(parser.value("seconds").toInt(), 5, 600);
    const auto started = std::chrono::steady_clock::now();
    const auto deadline = started + std::chrono::seconds(observation + 15);
    QJsonArray memory; qint64 nextSample = 0;
    while (!future.isFinished() && (!ready || backend->stats().videoFrames < quint64(profile.video.fps * observation)) && std::chrono::steady_clock::now() < deadline) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
        if (elapsed >= nextSample) {
            PROCESS_MEMORY_COUNTERS_EX counters{}; counters.cb = sizeof(counters);
            GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters));
            const auto state = backend->stats();
            memory.append(QJsonObject{{"elapsedMs", qint64(elapsed)}, {"privateBytes", qint64(counters.PrivateUsage)}, {"workingSet", qint64(counters.WorkingSetSize)}, {"bufferBytes", state.bytes}, {"bufferSeconds", state.seconds}});
            nextSample += 5000;
        }
        QCoreApplication::processEvents(); std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ExportResult saved;
    if (!parser.isSet("no-save") && ready && backend->stats().videoFrames) saved = backend->save(QDir(directory).filePath("native.mp4"));
    const auto stats = backend->stats();
    stop = true; future.waitForFinished();
    const auto stoppedStats = backend->stats();
    QJsonObject report{{"targetId", profile.video.targetId}, {"captureError", future.result()}, {"exportError", saved.error}, {"warning", stats.warning}, {"path", saved.path}, {"encoder", stats.encoder}, {"frames", qint64(stats.videoFrames)}, {"lost", stats.lost}, {"bufferBytes", stats.bytes}, {"seconds", stats.seconds}, {"bufferBytesAfterStop", stoppedStats.bytes}, {"secondsAfterStop", stoppedStats.seconds}, {"fps", profile.video.fps}, {"width", profile.video.width}, {"height", profile.video.height}};
    report.insert("memory", memory);
    const auto json = QJsonDocument(report).toJson(QJsonDocument::Indented); QFile file(QDir(directory).filePath("report.json")); if (file.open(QIODevice::WriteOnly)) file.write(json);
    std::fwrite(json.constData(), 1, size_t(json.size()), stdout);
    return future.result().isEmpty() && saved.error.isEmpty() && (parser.isSet("no-save") || !saved.path.isEmpty()) && stats.videoFrames >= quint64(profile.video.fps * observation) && stoppedStats.bytes == 0 && stoppedStats.seconds == 0 ? 0 : 1;
}
