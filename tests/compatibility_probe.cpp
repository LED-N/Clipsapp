#include "media/clip_reader.h"
#include "media/clip_player.h"
#include <QGuiApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QDir>
#include <QProcess>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <QElapsedTimer>
#include <cmath>
#include <thread>
using namespace clipsapp;
int main(int argc, char** argv) {
    QGuiApplication app(argc, argv); QCommandLineParser parser; parser.addHelpOption();
    parser.addOption({"input", "Local clip", "file"}); parser.addOption({"output", "Numerical report directory", "directory"});
    parser.addOption({"seconds", "Audio comparison duration", "seconds", "12"}); parser.process(app);
    if (!parser.isSet("input") || !parser.isSet("output")) return 2;
    QDir directory(parser.value("output")); if (!QDir().mkpath(directory.absolutePath())) return 2;
    QJsonObject report; bool passed = true;
    try {
        ClipReader reader(parser.value("input"), false, true); const auto info = reader.info();
        const auto duration = std::min<qint64>(info.durationMs, parser.value("seconds").toLongLong() * 1000);
        std::vector<std::unique_ptr<QFile>> references; QJsonArray tracks;
        for (const auto& track : info.audio) {
            const auto path = directory.filePath(QString("reference-%1.f32").arg(track.stream));
            QProcess process; process.start(QCoreApplication::applicationDirPath() + "/ffmpeg.exe", {"-v", "error", "-y", "-i", info.path, "-map", QString("0:%1").arg(track.stream), "-t", QString::number(duration / 1000.0, 'f', 3), "-af", "aresample=48000:async=1:first_pts=0", "-ac", "2", "-ar", "48000", "-f", "f32le", path});
            if (!process.waitForFinished(30000) || process.exitCode()) throw std::runtime_error("Reference FFmpeg failed");
            auto file = std::make_unique<QFile>(path); if (!file->open(QIODevice::ReadOnly)) throw std::runtime_error("Reference inaccessible"); references.push_back(std::move(file));
            tracks.append(QJsonObject{{"name", track.name}, {"maxError", 0.0}, {"errorSamples", 0}, {"samples", 0}});
        }
        std::atomic_bool stop{false}; float samples[960]; std::vector<double> maximum(info.audio.size()); std::vector<qint64> errors(info.audio.size()), compared(info.audio.size()); std::vector<QJsonArray> badBlocks(info.audio.size());
        for (qint64 ms = 0; ms < duration; ms += 10) {
            reader.readUntil(ms + 150, stop);
            for (int index = 0; index < info.audio.size(); ++index) {
                reader.mix(ms * 48, 480, {{info.audio[index].stream, info.audio[index].name, 1, false}}, samples);
                auto reference = references[size_t(index)]->read(sizeof(samples)); if (reference.isEmpty()) continue;
                const int comparedCount = int(reference.size() / sizeof(float));
                const auto* expected = reinterpret_cast<const float*>(reference.constData());
                compared[size_t(index)] += comparedCount;
                double blockError = 0, energy = 0, referenceEnergy = 0;
                for (int i = 0; i < comparedCount; ++i) { const double error = std::abs(samples[i] - std::clamp(expected[i], -1.0f, 1.0f)); maximum[size_t(index)] = std::max(maximum[size_t(index)], error); blockError = std::max(blockError, error); energy += samples[i] * samples[i]; referenceEnergy += expected[i] * expected[i]; if (error > 0.00001) ++errors[size_t(index)]; }
                if (blockError > .00001 && badBlocks[size_t(index)].size() < 16) badBlocks[size_t(index)].append(QJsonObject{{"ms", ms}, {"rms", std::sqrt(energy/960)}, {"referenceRms", std::sqrt(referenceEnergy/960)}, {"maxError", blockError}});
            }
        }
        for (int index = 0; index < tracks.size(); ++index) { auto track = tracks[index].toObject(); track["maxError"] = maximum[size_t(index)]; track["samples"] = compared[size_t(index)]; track["errorSamples"] = errors[size_t(index)]; track["firstBadBlocks"] = badBlocks[size_t(index)]; tracks[index] = track; if (errors[size_t(index)] > 100 || compared[size_t(index)] < (duration - 50) * 96) passed = false; references[size_t(index)]->close(); QFile::remove(references[size_t(index)]->fileName()); }
        report["audio"] = tracks;
        ClipPlayer player(nullptr, false); player.load(info.path); QElapsedTimer time; time.start();
        while (time.elapsed() < 15000 && !player.snapshot().loaded && player.snapshot().error.isEmpty()) { app.processEvents(); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
        QJsonArray seeks;
        for (qint64 target : {15000, 62000, 100000, 32000, 75000}) {
            time.restart(); player.seek(target);
            while (time.elapsed() < 15000 && player.snapshot().loading) { app.processEvents(); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
            const auto state = player.snapshot(); seeks.append(QJsonObject{{"target", target}, {"latencyMs", time.elapsed()}, {"position", state.positionMs}, {"error", state.error}});
            if (!state.error.isEmpty() || state.loading || std::abs(state.positionMs - target) > 50) passed = false;
        }
        report["seeks"] = seeks;
        int eventTicks = 0; QTimer heartbeat; heartbeat.setInterval(5); QObject::connect(&heartbeat, &QTimer::timeout, &app, [&] { ++eventTicks; }); heartbeat.start();
        for (int i = 0; i < 40; ++i) { player.seek(15000 + i * 2100); app.processEvents(); std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
        player.seek(62000); time.restart(); while (time.elapsed() < 5000 && player.snapshot().loading) { app.processEvents(); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
        report["rapidSeek"] = QJsonObject{{"latencyMs", time.elapsed()}, {"position", player.snapshot().positionMs}, {"eventTicks", eventTicks}, {"error", player.snapshot().error}};
        if (player.snapshot().loading || !player.snapshot().error.isEmpty() || player.snapshot().positionMs != 62000 || eventTicks == 0) passed = false;
        player.close();
    } catch (const std::exception& error) { report["error"] = error.what(); passed = false; }
    report["passed"] = passed; QFile output(directory.filePath("report.json")); if (output.open(QIODevice::WriteOnly)) output.write(QJsonDocument(report).toJson(QJsonDocument::Indented)); return passed ? 0 : 1;
}
