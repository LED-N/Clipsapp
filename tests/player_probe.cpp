#include <QGuiApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include "media/clip_player.h"
#include "media/clip_exporter.h"
#include "media/clip_reader.h"
using namespace clipsapp;

// Manual hardware check: local playback through the real output, with every
// track muted. It never captures a screen or microphone or opens a window.
int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    QCommandLineParser parser; parser.addHelpOption();
    parser.addOption({"input", "Local clip to decode", "file"});
    parser.addOption({"output-dir", "Isolated directory for the report and export", "directory"}); parser.process(app);
    if (!parser.isSet("input") || !parser.isSet("output-dir")) return 2;
    const QDir directory(parser.value("output-dir")); if (!QDir().mkpath(directory.path())) return 2;
    ClipPlayer player; ClipExporter exporter;
    ClipInfo info; ClipEdit edit;
    QJsonObject report; int phase = 0; bool finished = false;
    auto finish = [&](const QString& error) {
        if (finished) return; finished = true;
        player.close(); report.insert("passed", error.isEmpty()); report.insert("error", error);
        report.insert("released", player.snapshot().image.isNull() && !player.snapshot().loaded);
        QFile file(directory.filePath("report.json"));
        if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(report).toJson()) < 0) app.exit(2);
        else app.exit(error.isEmpty() ? 0 : 1);
    };
    QObject::connect(&player, &ClipPlayer::changed, &app, [&] {
        if (finished || phase == 4) return;
        const auto state = player.snapshot();
        if (!state.error.isEmpty() || !state.warning.isEmpty()) { finish(state.error + state.warning); return; }
        if (!state.loaded || state.loading) return;
        if (phase == 0) {
            info = state.info; edit = defaultEdit(info); for (auto& track : edit.tracks) track.muted = true;
            report.insert("decoder", info.decoder); report.insert("width", info.size.width()); report.insert("height", info.size.height());
            report.insert("previewWidth", state.image.width()); report.insert("previewHeight", state.image.height()); report.insert("audioTracks", info.audio.size());
            if (info.audio.isEmpty() || info.durationMs < 2000 || state.image.isNull()) { finish("Expected a clip of at least two seconds with audio and video"); return; }
            phase = 1; player.setEdit(edit); player.setPlaying(true);
        } else if (phase == 1 && state.playing && state.positionMs >= 600) {
            report.insert("playedMs", double(state.positionMs)); phase = 2; player.seek(1000);
        } else if (phase == 2 && state.positionMs >= 1000) {
            report.insert("seekMs", double(state.positionMs)); phase = 3; player.setPlaying(false);
        } else if (phase == 3 && !state.playing) {
            phase = 4; player.close();
            edit.startMs = 500; edit.endMs = 1500;
            edit.crop = boundedCrop({info.size.width() / 4, info.size.height() / 4, info.size.width() / 2, info.size.height() / 2}, info.size);
            QString error;
            if (!exporter.start(info, edit, directory.filePath("gpu-export.mp4"), error, {"h264_nvenc"})) finish(error);
        }
    });
    QObject::connect(&exporter, &ClipExporter::failed, &app, finish);
    QObject::connect(&exporter, &ClipExporter::completed, &app, [&](const QString& path) {
        try {
            ClipReader reader(path); std::atomic_bool stop{false}; reader.readUntil(200, stop);
            report.insert("exportWidth", reader.info().size.width()); report.insert("exportHeight", reader.info().size.height());
            report.insert("exportDurationMs", double(reader.info().durationMs));
            if (reader.info().size != edit.crop.size() || reader.frameAt(0).isNull() || !reader.info().audio.isEmpty()) finish("Invalid cropped video-only NVENC export");
            else finish({});
        } catch (const std::exception& error) { finish(QString::fromUtf8(error.what())); }
    });
    QTimer::singleShot(15000, &app, [&] { finish("Native playback/export timed out"); });
    player.load(parser.value("input")); return app.exec();
}
