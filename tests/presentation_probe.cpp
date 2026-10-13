#include <QGuiApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QTimer>
#include "media/clip_player.h"
#include <algorithm>
#include <vector>
#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#endif
using namespace clipsapp;

// Manual hardware check of full-screen playback: plays a local clip, every track muted,
// with images fitted to a box as large as a screen, and measures how regularly a new image
// arrives. It opens no window and captures nothing; drawing is timed into a memory image.
int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    QCommandLineParser parser; parser.addHelpOption();
    parser.addOption({"input", "Local clip to play", "file"});
    parser.addOption({"output-dir", "Directory for the report", "directory"});
    parser.addOption({"box", "Size of the screen to fill", "widthxheight", "2560x1440"});
    parser.addOption({"seconds", "Playback duration to observe", "seconds", "8"}); parser.process(app);
    if (!parser.isSet("input") || !parser.isSet("output-dir")) return 2;
    const QDir directory(parser.value("output-dir")); if (!QDir().mkpath(directory.path())) return 2;
    const auto dimensions = parser.value("box").split('x'); if (dimensions.size() != 2) return 2;
    const QSize box(dimensions[0].toInt(), dimensions[1].toInt()); const int seconds = std::clamp(parser.value("seconds").toInt(), 2, 120);
    ClipPlayer player; player.setPresentation(box);
    QImage screen(box, QImage::Format_RGB32); screen.fill(Qt::black);
    QJsonObject report; bool started = false, finished = false; qint64 lastKey = 0, firstMs = -1, lastPosition = 0; qint64 peakBytes = 0;
    std::vector<double> gaps, paints; QElapsedTimer sinceImage, playback;
    const auto finish = [&](const QString& error) {
        if (finished) return; finished = true; player.close();
        std::sort(gaps.begin(), gaps.end()); std::sort(paints.begin(), paints.end());
        const auto at = [](const std::vector<double>& values, double share) { return values.empty() ? 0.0 : values[std::min(values.size() - 1, size_t(values.size() * share))]; };
        const double span = (lastPosition - firstMs) / 1000.0, fps = report.value("fps").toDouble();
        report.insert("images", int(gaps.size()) + 1); report.insert("playedSeconds", span); report.insert("imagesPerSecond", span > 0 ? (gaps.size() + 1) / span : 0);
        report.insert("shareOfFrames", span > 0 && fps > 0 ? (gaps.size() + 1) / (span * fps) : 0);
        report.insert("gapMedianMs", at(gaps, 0.5)); report.insert("gapP95Ms", at(gaps, 0.95)); report.insert("gapP99Ms", at(gaps, 0.99)); report.insert("gapMaxMs", gaps.empty() ? 0 : gaps.back());
        report.insert("paintMedianMs", at(paints, 0.5)); report.insert("paintP95Ms", at(paints, 0.95)); report.insert("peakPrivateMiB", double(peakBytes) / 1048576.0);
        report.insert("passed", error.isEmpty()); report.insert("error", error);
        QFile file(directory.filePath("report.json"));
        if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(report).toJson()) < 0) app.exit(2); else app.exit(error.isEmpty() ? 0 : 1);
    };
    QObject::connect(&player, &ClipPlayer::changed, &app, [&] {
        if (finished) return;
        const auto state = player.snapshot();
        if (!state.error.isEmpty()) { finish(state.error); return; }
        if (!state.loaded) return;
        if (!started) {
            if (state.loading) return;
            started = true; auto edit = defaultEdit(state.info); for (auto& track : edit.tracks) track.muted = true;
            report.insert("decoder", state.info.decoder); report.insert("width", state.info.size.width()); report.insert("height", state.info.size.height()); report.insert("fps", state.info.fps);
            report.insert("imageWidth", state.image.width()); report.insert("imageHeight", state.image.height());
            player.setEdit(edit); player.setPlaying(true); playback.start(); return;
        }
        if (state.image.cacheKey() != lastKey && state.playing) {
            if (lastKey) gaps.push_back(sinceImage.nsecsElapsed() / 1e6);
            sinceImage.start(); lastKey = state.image.cacheKey(); if (firstMs < 0) firstMs = state.positionMs; lastPosition = state.positionMs;
            // What a window does with the image: one unscaled copy into its backing store.
            QElapsedTimer paint; paint.start(); { QPainter painter(&screen); painter.drawImage(QPoint((box.width() - state.image.width()) / 2, (box.height() - state.image.height()) / 2), state.image); }
            paints.push_back(paint.nsecsElapsed() / 1e6);
#ifdef Q_OS_WIN
            PROCESS_MEMORY_COUNTERS_EX memory{}; memory.cb = sizeof(memory);
            if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory))) peakBytes = std::max<qint64>(peakBytes, qint64(memory.PrivateUsage));
#endif
        }
        if (playback.elapsed() >= seconds * 1000 || (!state.playing && !state.loading && playback.elapsed() > 1000)) finish({});
    });
    QTimer::singleShot((seconds + 20) * 1000, &app, [&] { finish("Presentation playback timed out"); });
    player.load(parser.value("input")); return app.exec();
}
