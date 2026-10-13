#include <QtTest>
#include "capture/audio_stream.h"
#include <cmath>
#include <cstring>
using namespace clipsapp;
namespace {
std::vector<uint8_t> tone(int rate, int channels, AudioSampleFormat format, int start, int count) {
    const int bytes = format == AudioSampleFormat::Signed16 ? 2 : format == AudioSampleFormat::Signed24 ? 3 : 4;
    std::vector<uint8_t> output(size_t(count * channels * bytes));
    for (int frame = 0; frame < count; ++frame) for (int channel = 0; channel < channels; ++channel) {
        const double value = 0.3 * std::sin(2 * 3.141592653589793 * (440 + channel * 220) * (start + frame) / rate);
        auto* destination = output.data() + (frame * channels + channel) * bytes;
        if (format == AudioSampleFormat::Float32) { const float sample = float(value); std::memcpy(destination, &sample, 4); }
        else if (format == AudioSampleFormat::Signed16) { const int16_t sample = int16_t(value * 32767); std::memcpy(destination, &sample, 2); }
        else { const int32_t sample = int32_t(value * 8388607); destination[0] = uint8_t(sample); destination[1] = uint8_t(sample >> 8); destination[2] = uint8_t(sample >> 16); }
    }
    return output;
}
}
class AudioTests final : public QObject {
    Q_OBJECT
private slots:
    void convertsNativeMicrophoneFormats_data() {
        QTest::addColumn<int>("rate"); QTest::addColumn<int>("channels"); QTest::addColumn<int>("format");
        for (int rate : {44100, 48000, 96000}) for (int channels : {1, 2})
            for (const auto format : {AudioSampleFormat::Float32, AudioSampleFormat::Signed16, AudioSampleFormat::Signed24})
                QTest::newRow(qPrintable(QString("%1Hz-%2ch-%3").arg(rate).arg(channels).arg(int(format)))) << rate << channels << int(format);
    }
    void convertsNativeMicrophoneFormats() {
        QFETCH(int, rate); QFETCH(int, channels); QFETCH(int, format);
        AudioStream stream({rate, channels, AudioSampleFormat(format)});
        std::vector<float> samples; qint64 next = 1700;
        const int packetSize = rate / 100;
        for (int position = 0; position < rate; position += packetSize) {
            const int count = std::min(packetSize, rate - position);
            const auto input = tone(rate, channels, AudioSampleFormat(format), position, count);
            // Reproduce QPC jitter at packet boundaries (the previous placement
            // created gaps or overlapping samples from these tiny offsets).
            const qint64 jitter = position == 0 ? 0 : ((position / packetSize) % 3 - 1) * 4;
            const auto chunk = stream.convert(input.data(), count, 1700 + qint64(std::llround(position * 48000.0 / rate)) + jitter);
            QCOMPARE(chunk.start, next); next += qint64(chunk.samples.size() / 2);
            samples.insert(samples.end(), chunk.samples.begin(), chunk.samples.end());
        }
        QVERIFY(qAbs(qint64(samples.size() / 2) - 48000) < 100);
        for (int channel = 0; channel < 2; ++channel) {
            int crossings = 0; double peak = 0, biggestStep = 0;
            for (size_t frame = 64; frame < samples.size() / 2; ++frame) {
                const auto current = samples[frame * 2 + channel], previous = samples[(frame - 1) * 2 + channel];
                if (previous < 0 && current >= 0) ++crossings;
                peak = std::max(peak, double(std::abs(current))); biggestStep = std::max(biggestStep, double(std::abs(current - previous)));
                if (channels == 1) QCOMPARE(samples[frame * 2], samples[frame * 2 + 1]);
            }
            const double frequency = crossings * 48000.0 / (samples.size() / 2 - 64);
            QVERIFY(qAbs(frequency - (440 + (channels == 1 ? 0 : channel * 220))) < 3);
            QVERIFY(peak > 0.29 && peak < 0.31); QVERIFY(biggestStep < 0.035);
        }
    }
    void timestampsAndSilentPacketsKeepAContinuousStream() {
        AudioStream stream({48000, 1, AudioSampleFormat::Float32});
        std::vector<float> input(480, 0.25f); qint64 next = 0;
        for (int packet = 0; packet < 200; ++packet) {
            const auto chunk = stream.convert(reinterpret_cast<const uint8_t*>(input.data()), 480,
                packet * 480 + (packet % 2 ? 3 : 0), packet % 7 != 0);
            QCOMPARE(chunk.start, next); next += qint64(chunk.samples.size() / 2);
            // A constant signal must have no clicks between hardware packets.
            for (float sample : chunk.samples) QVERIFY(std::abs(sample - 0.25f) < 0.001f);
        }
        const auto silent = stream.convert(nullptr, 480, 96000);
        QCOMPARE(silent.start, next); QVERIFY(silent.samples.size() > 800);
        for (size_t i = 100; i < silent.samples.size(); ++i) QVERIFY(std::abs(silent.samples[i]) < 0.001f);
        next += qint64(silent.samples.size() / 2);
        const auto resumed = stream.convert(reinterpret_cast<const uint8_t*>(input.data()), 480, 96480);
        QCOMPARE(resumed.start, next);
    }
    void compensatesSlowClockDriftWithoutBreakingPacketContinuity() {
        AudioStream stream({48000, 1, AudioSampleFormat::Float32});
        std::vector<float> input(480, 0.25f); qint64 next = 0;
        for (int packet = 0; packet < 5000; ++packet) {
            // Simulate an input clock running 200 ppm slower than the video clock.
            const auto chunk = stream.convert(reinterpret_cast<const uint8_t*>(input.data()), 480,
                qint64(std::llround(packet * 480.096)));
            QCOMPARE(chunk.start, next); next += qint64(chunk.samples.size() / 2);
            for (float sample : chunk.samples) QVERIFY(std::abs(sample - 0.25f) < 0.001f);
        }
        QVERIFY(qAbs(next - qint64(std::llround(5000 * 480.096))) < 80);
    }
    void realDiscontinuityRetainsItsGapAndStartsCleanly() {
        AudioStream stream({48000, 1, AudioSampleFormat::Float32});
        std::vector<float> input(480, 0.25f);
        const auto first = stream.convert(reinterpret_cast<const uint8_t*>(input.data()), 480, 0);
        const auto resumed = stream.convert(reinterpret_cast<const uint8_t*>(input.data()), 480, 2000, true, true);
        QCOMPARE(resumed.start, qint64(2000)); QVERIFY(first.samples.size() / 2 < size_t(resumed.start));
        for (float sample : resumed.samples) QVERIFY(std::abs(sample - 0.25f) < 0.001f);
    }
};
QTEST_GUILESS_MAIN(AudioTests)
#include "audio_tests.moc"
