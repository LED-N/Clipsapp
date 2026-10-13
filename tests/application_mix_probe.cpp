#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include "platform/windows_audio.h"
#include "media/audio_output.h"
#include <algorithm>
#include <cmath>
#include <thread>
#include <vector>
using namespace clipsapp;
namespace {
double amplitude(const std::vector<float>& values, int startMs, int endMs, double frequency) {
    double sine = 0, cosine = 0; const int start = startMs * 48, end = std::min(int(values.size()), endMs * 48);
    for (int sample = start; sample < end; ++sample) { const auto angle = 2 * 3.141592653589793 * frequency * sample / 48000; sine += values[size_t(sample)] * std::sin(angle); cosine += values[size_t(sample)] * std::cos(angle); }
    return 2 * std::sqrt(sine * sine + cosine * cosine) / std::max(1, end - start);
}
}
// This is a manual test only. Launch the two tone helpers as sibling processes,
// not children of the recorder: the recorder's entire tree is always excluded.
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv); QCommandLineParser parser; parser.addHelpOption();
    parser.addOption({"tone", "Play a quiet generated tone for a manual audio test", "frequency"});
    parser.addOption({"duration", "Tone duration in milliseconds", "milliseconds", "3000"});
    parser.addOption({"excluded", "Executable of the helper on a separate track", "path"});
    parser.addOption({"output-dir", "Directory for the numeric report only", "directory"}); parser.process(app);
    try {
        if (parser.isSet("tone")) {
            AudioOutput output; const double frequency = parser.value("tone").toDouble(); const qint64 limit = parser.value("duration").toLongLong() * 48;
            qint64 submitted = 0; std::vector<float> samples(960);
            while (output.playedFrames() < limit) {
                if (submitted < limit && output.writableFrames() >= 480) {
                    const int count = int(std::min<qint64>(480, limit - submitted));
                    for (int frame = 0; frame < count; ++frame) samples[size_t(frame) * 2] = samples[size_t(frame) * 2 + 1] = float(0.015 * std::sin(2 * 3.141592653589793 * frequency * (submitted + frame) / 48000));
                    output.write(samples.data(), count); submitted += count;
                } else std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            return 0;
        }
        if (!parser.isSet("excluded") || !parser.isSet("output-dir")) return 2;
        QDir directory(parser.value("output-dir")); if (!QDir().mkpath(directory.path())) return 2;
        auto profile = AppConfig::defaults().profiles.first(); auto game = profile.sources[1]; auto separate = profile.sources[2];
        separate.enabled = true; separate.targetId = parser.value("excluded"); profile.sources[2] = separate;
        game.excludedApplications = applicationExclusions(game, profile);
        const auto epoch = windowsClock100ns(); WindowsAudio all(game), isolated(separate); all.start(epoch); isolated.start(epoch);
        QFile ready(directory.filePath("ready")); if (!ready.open(QIODevice::WriteOnly)) return 2; ready.write("ready"); ready.close();
        std::vector<float> gameSamples, separateSamples; gameSamples.reserve(8 * 48000); separateSamples.reserve(8 * 48000);
        qint64 cursor = 0; float left[480], right[480]; QString error;
        while (cursor < 8 * 48000) {
            QCoreApplication::processEvents(); error = all.error() + isolated.error(); if (!error.isEmpty()) break;
            const qint64 available = (windowsClock100ns() - epoch - 1500000) * 48000 / 10000000;
            if (cursor + 480 <= available) {
                std::fill_n(left, 480, 0); std::fill_n(right, 480, 0); all.mix(cursor, 480, left, right); gameSamples.insert(gameSamples.end(), left, left + 480);
                std::fill_n(left, 480, 0); std::fill_n(right, 480, 0); isolated.mix(cursor, 480, left, right); separateSamples.insert(separateSamples.end(), left, left + 480); cursor += 480;
            } else std::this_thread::sleep_for(std::chrono::milliseconds(3));
        }
        const double first = amplitude(gameSamples, 1600, 2600, 330), restarted = amplitude(gameSamples, 5400, 7400, 330);
        const double excluded = amplitude(gameSamples, 2000, 7000, 550), kept = amplitude(separateSamples, 2000, 7000, 550), wrong = amplitude(separateSamples, 2000, 7000, 330);
        const bool passed = error.isEmpty() && first > 0.007 && restarted > 0.007 && excluded < 0.0005 && kept > 0.007 && wrong < 0.0005;
        QJsonObject report{{"passed", passed}, {"error", error}, {"gameStartedAfterReplay", first}, {"gameRestarted", restarted}, {"excludedToneInGame", excluded}, {"excludedToneOnSeparateTrack", kept}, {"gameToneOnSeparateTrack", wrong}};
        QFile file(directory.filePath("report.json")); if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(report).toJson()) < 0) return 2;
        return passed ? 0 : 1;
    } catch (const std::exception& error) {
        if (parser.isSet("output-dir")) { QFile file(QDir(parser.value("output-dir")).filePath("report.json")); if (file.open(QIODevice::WriteOnly)) file.write(QJsonDocument(QJsonObject{{"passed", false}, {"error", QString::fromUtf8(error.what())}}).toJson()); }
        return 1;
    }
}
