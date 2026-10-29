#include "ui/main_window.h"
#include "ui/clip_editor.h"
#include "ui/clip_viewer.h"
#include "ui/theme.h"
#include "core/config_store.h"
#include "platform/source_catalog.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QPushButton>
#include <QAbstractButton>
#include <QFileInfo>
#include <cstdio>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QElapsedTimer>
#include <QThread>
#include <windows.h>
#include <psapi.h>
#include <algorithm>
using namespace clipsapp;
int main(int argc, char** argv) {
    QApplication app(argc, argv); applyTheme(app); QCommandLineParser parser; parser.addHelpOption();
    parser.addOption({"output", "Isolated configuration and numerical measurements", "directory"});
    parser.addOption({"seconds", "Replay observation duration", "seconds", "185"});
    parser.addOption({"resolution", "Capture resolution", "widthxheight", "2560x1440"});
    // A profile in constant quality, as the settings offer it: its level, and the bitrate it never goes over.
    parser.addOption({"quality", "Constant quality level (10 to 30) instead of a target bitrate", "level"});
    parser.addOption({"bitrate", "Video bitrate, or its ceiling in constant quality", "mbps"});
    // Opening a clip in the editor and leaving it again, instead of the replay: what stays once it is closed.
    parser.addOption({"clip", "Clip opened in the editor, played, then closed, several times", "file"});
    parser.addOption({"cycles", "Times the clip is opened and closed", "count", "3"}); parser.process(app);
    if (!parser.isSet("output")) return 2;
    const auto directory = QDir(parser.value("output")).absolutePath(); const auto configDirectory = QDir(directory).filePath("config");
    auto config = AppConfig::defaults(); config.libraryPath = QDir(directory).filePath("empty-library");
    for (const auto& target : SourceCatalog::discover().videoTargets) if (target.id.startsWith("screen:")) { config.profiles[0].video.targetId = target.id; break; }
    const auto dimensions = parser.value("resolution").split('x');
    if (dimensions.size() != 2) return 2;
    config.profiles[0].video.width = dimensions[0].toInt(); config.profiles[0].video.height = dimensions[1].toInt();
    if (parser.isSet("quality")) { config.profiles[0].video.rateControl = QStringLiteral("quality"); config.profiles[0].video.quality = parser.value("quality").toInt(); }
    if (parser.isSet("bitrate")) config.profiles[0].video.bitrateMbps = parser.value("bitrate").toInt();
    // Set by hand: no preset decides the bitrate any more.
    if (parser.isSet("quality") || parser.isSet("bitrate")) config.profiles[0].video.preset.clear();
    config.profiles[0].replay.memoryLimitMiB = 2048;
    if (parser.isSet("clip")) {
        // The clip alone in its library, so that only its own thumbnail is ever made.
        const auto library = QDir(directory).filePath("clip-library"); QDir().mkpath(library);
        const auto copy = QDir(library).filePath(QFileInfo(parser.value("clip")).fileName());
        if (!QFile::exists(copy) && !QFile::copy(parser.value("clip"), copy)) return 2;
        config.libraryPath = library;
    }
    ConfigStore store(configDirectory); QString error; if (!store.save(config, error)) return 2;
    MainWindow window(configDirectory);
    if (parser.isSet("clip")) {
        // The window is really shown, out of sight, for the images to go the way they go for the player.
        window.move(-6000, -6000); window.show();
        auto* editor = window.findChild<ClipEditor*>(); if (!editor) return 2;
        const auto copy = QDir(config.libraryPath).filePath(QFileInfo(parser.value("clip")).fileName());
        const auto wait = [&app](int ms) { QElapsedTimer waited; waited.start(); while (waited.elapsed() < ms) { app.processEvents(); QThread::msleep(5); } };
        QJsonArray samples; QElapsedTimer elapsed; elapsed.start();
        const auto measure = [&](const QString& stage) {
            PROCESS_MEMORY_COUNTERS_EX memory{}; memory.cb = sizeof(memory);
            GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory));
            DWORD handles = 0; GetProcessHandleCount(GetCurrentProcess(), &handles);
            samples.append(QJsonObject{{"stage", stage}, {"elapsedMs", elapsed.elapsed()}, {"privateBytes", qint64(memory.PrivateUsage)}, {"workingSet", qint64(memory.WorkingSetSize)}, {"handles", int(handles)}});
            std::printf("%-10s %6lld ms  private %5lld Mo  working set %5lld Mo  handles %lu\n", qPrintable(stage), elapsed.elapsed(), qint64(memory.PrivateUsage) >> 20, qint64(memory.WorkingSetSize) >> 20, handles); std::fflush(stdout);
        };
        wait(8000); measure("idle");
        const int cycles = std::clamp(parser.value("cycles").toInt(), 1, 50);
        for (int cycle = 0; cycle < cycles; ++cycle) {
            window.setPage(MainWindow::Editor); editor->openClip(copy); wait(4000); measure("opened");
            if (auto* play = editor->findChild<QAbstractButton*>("play-pause"); play && play->isEnabled()) play->click();
            wait(15000); measure("played");
            window.setPage(MainWindow::Library); wait(6000); measure("closed");
        }
        // The full screen player, in a window out of sight.
        ClipViewer viewer(&window); viewer.setProperty("windowed", true);
        for (int cycle = 0; cycle < cycles; ++cycle) {
            viewer.open({copy}, 0); viewer.move(-6000, -6000); wait(15000); measure("viewing");
            viewer.finish(); wait(6000); measure("viewed");
        }
        wait(10000); measure("settled");
        QFile file(QDir(directory).filePath("clip-report.json")); if (file.open(QIODevice::WriteOnly)) file.write(QJsonDocument(QJsonObject{{"samples", samples}}).toJson(QJsonDocument::Indented));
        window.close(); return 0;
    }
    window.setAttribute(Qt::WA_DontShowOnScreen); window.show();
    auto* replay = window.findChild<ReplayEngine*>(); if (!replay) return 2;
    auto* activate = window.findChild<QPushButton*>("replay-primary");
    if (!activate) return 2;
    const auto sample = [replay](qint64 ms) {
        PROCESS_MEMORY_COUNTERS_EX memory{}; memory.cb = sizeof(memory);
        GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory));
        DWORD handles = 0; GetProcessHandleCount(GetCurrentProcess(), &handles);
        const auto stats = replay->stats();
        return QJsonObject{{"elapsedMs", ms}, {"privateBytes", qint64(memory.PrivateUsage)}, {"workingSet", qint64(memory.WorkingSetSize)}, {"bufferBytes", stats.bytes}, {"bufferSeconds", stats.seconds}, {"videoFrames", qint64(stats.videoFrames)}, {"handles", int(handles)}, {"gdiObjects", int(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS))}, {"userObjects", int(GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS))}};
    };
    QString captureError; QObject::connect(replay, &ReplayEngine::failed, &window, [&](const QString& value) { captureError = value; });
    QJsonArray samples; QElapsedTimer elapsed; elapsed.start(); samples.append(sample(0)); activate->click(); qint64 next = 5000;
    const int seconds = std::clamp(parser.value("seconds").toInt(), 75, 86400);
    while (elapsed.elapsed() < (seconds + 15) * 1000 && captureError.isEmpty() && replay->stats().videoFrames < quint64(60 * seconds)) {
        app.processEvents();
        if (elapsed.elapsed() >= next) {
            samples.append(sample(elapsed.elapsed())); next += 5000;
            // A numerical checkpoint makes long runs observable without
            // retaining images or audio on disk, or changing live settings.
            QFile progress(QDir(directory).filePath("progress.json"));
            if (progress.open(QIODevice::WriteOnly)) progress.write(QJsonDocument(QJsonObject{{"samples", samples}}).toJson(QJsonDocument::Compact));
        }
        QThread::msleep(5);
    }
    const auto before = replay->stats(); replay->stop(); QElapsedTimer stopping; stopping.start();
    while (replay->state() == ReplayEngine::State::Stopping && stopping.elapsed() < 15000) { app.processEvents(); QThread::msleep(5); }
    QJsonObject report{{"samples", samples}, {"afterStop", sample(elapsed.elapsed())}, {"captureError", captureError}, {"frames", qint64(before.videoFrames)}, {"encoder", before.encoder}, {"width", config.profiles[0].video.width}, {"height", config.profiles[0].video.height}, {"fps", 60}, {"durationSeconds", seconds}};
    qint64 low = INT64_MAX, high = 0;
    for (const auto& entry : samples) { const auto object = entry.toObject(); if (object["elapsedMs"].toInteger() >= 80000) { const auto value = object["privateBytes"].toInteger(); low = std::min(low, value); high = std::max(high, value); } }
    const bool passed = captureError.isEmpty() && before.videoFrames >= quint64(60 * seconds) && replay->stats().bytes == 0 && (high == 0 || high - low < 16 * 1024 * 1024);
    report["passed"] = passed; report["variationAfter80sBytes"] = high ? high - low : 0;
    QFile file(QDir(directory).filePath("report.json")); if (file.open(QIODevice::WriteOnly)) file.write(QJsonDocument(report).toJson(QJsonDocument::Indented));
    window.close(); return passed ? 0 : 1;
}
