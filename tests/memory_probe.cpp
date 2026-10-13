#include "ui/main_window.h"
#include "ui/theme.h"
#include "core/config_store.h"
#include "platform/source_catalog.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QPushButton>
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
    parser.addOption({"resolution", "Capture resolution", "widthxheight", "2560x1440"}); parser.process(app);
    if (!parser.isSet("output")) return 2;
    const auto directory = QDir(parser.value("output")).absolutePath(); const auto configDirectory = QDir(directory).filePath("config");
    auto config = AppConfig::defaults(); config.libraryPath = QDir(directory).filePath("empty-library");
    for (const auto& target : SourceCatalog::discover().videoTargets) if (target.id.startsWith("screen:")) { config.profiles[0].video.targetId = target.id; break; }
    const auto dimensions = parser.value("resolution").split('x');
    if (dimensions.size() != 2) return 2;
    config.profiles[0].video.width = dimensions[0].toInt(); config.profiles[0].video.height = dimensions[1].toInt();
    ConfigStore store(configDirectory); QString error; if (!store.save(config, error)) return 2;
    MainWindow window(configDirectory); window.setAttribute(Qt::WA_DontShowOnScreen); window.show();
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
