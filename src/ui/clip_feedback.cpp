#include "clip_feedback.h"
#include <QApplication>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>
#ifdef Q_OS_WIN
#include <windows.h>
#include <mmsystem.h>
#endif
namespace clipsapp {
namespace {
constexpr int rate = 44100;
constexpr double tau = 6.283185307179586;
using Samples = std::vector<double>;
Samples silence(double seconds) { return Samples(size_t(seconds * rate), 0.0); }
// A note added at a moment of the sound: a sine with some of its octave above, a short attack and
// a decay. A sound whose notes have a decay rings like a bell; one that holds stops cleanly.
void note(Samples& out, double start, double hz, double length, double decay, double octave = 0, double attack = 0.006) {
    const int first = int(start * rate), count = int(length * rate), release = int(0.02 * rate);
    for (int i = 0; i < count && first + i < int(out.size()); ++i) {
        const double t = double(i) / rate;
        const double envelope = std::min(1.0, t / attack) * (decay > 0 ? std::exp(-t / decay) : 1.0) * std::min(1.0, double(count - i) / release);
        out[size_t(first + i)] += envelope * (std::sin(tau * hz * t) + octave * std::sin(2 * tau * hz * t));
    }
}
// A note whose pitch slides, for the drop and the bubble.
void glide(Samples& out, double from, double to, double length, double decay) {
    double phase = 0; const int count = int(length * rate);
    for (int i = 0; i < count && i < int(out.size()); ++i) {
        const double t = double(i) / rate, hz = from * std::pow(to / from, t / length); phase += tau * hz / rate;
        out[size_t(i)] += std::min(1.0, t / 0.004) * std::exp(-t / decay) * std::min(1.0, double(count - i) / (0.015 * rate)) * std::sin(phase);
    }
}
// A burst of noise, darkened, for the click of a shutter. The noise is always the same.
void clack(Samples& out, double start, double length, quint32 seed) {
    const int first = int(start * rate), count = int(length * rate); double low = 0;
    for (int i = 0; i < count && first + i < int(out.size()); ++i) {
        seed = seed * 1664525u + 1013904223u; const double white = double(seed >> 8) / double(1u << 24) * 2 - 1;
        low += 0.35 * (white - low); const double t = double(i) / rate;
        out[size_t(first + i)] += std::exp(-t / (length / 4)) * low;
    }
}
Samples render(const QString& id) {
    if (id == QLatin1String("chime")) { auto s = silence(0.75); note(s, 0, 1046.5, 0.7, 0.22, 0.25); note(s, 0.07, 1318.5, 0.65, 0.22, 0.25); note(s, 0.14, 1568.0, 0.6, 0.25, 0.25); return s; }
    if (id == QLatin1String("coin")) { auto s = silence(0.42); note(s, 0, 987.8, 0.07, 0, 0.3); note(s, 0.07, 1318.5, 0.35, 0.12, 0.3); return s; }
    if (id == QLatin1String("drop")) { auto s = silence(0.18); glide(s, 1800, 500, 0.18, 0.06); return s; }
    if (id == QLatin1String("bubble")) { auto s = silence(0.14); glide(s, 320, 950, 0.14, 0.08); return s; }
    if (id == QLatin1String("shutter")) { auto s = silence(0.16); clack(s, 0, 0.035, 7); clack(s, 0.085, 0.05, 11); return s; }
    if (id == QLatin1String("beep")) { auto s = silence(0.13); note(s, 0, 1000, 0.13, 0); return s; }
    // "duo", the sound of the earlier versions: two held notes, a fourth apart.
    auto s = silence(0.18); note(s, 0, 880, 0.09, 0, 0, 0.01); note(s, 0.09, 1175, 0.09, 0, 0, 0.01); return s;
}
}
QList<SavedSound> clipSavedSounds() {
    return {{"duo", QStringLiteral("Deux notes")}, {"chime", QStringLiteral("Carillon")}, {"coin", QStringLiteral("Pièce")}, {"drop", QStringLiteral("Goutte")},
            {"bubble", QStringLiteral("Bulle")}, {"shutter", QStringLiteral("Déclencheur photo")}, {"beep", QStringLiteral("Bip")}};
}
QByteArray clipSavedSoundWave(const QString& id, int volume, int times) {
    bool known = false; for (const auto& sound : clipSavedSounds()) known |= sound.id == id;
    const auto once = render(known ? id : clipSavedSounds().first().id); auto samples = once;
    for (int again = 1; again < std::clamp(times, 1, 3); ++again) { const auto gap = silence(0.07); samples.insert(samples.end(), gap.begin(), gap.end()); samples.insert(samples.end(), once.begin(), once.end()); }
    // Every sound reaches the same peak; the ear hears the square of the setting as an even step.
    double peak = 0; for (const double sample : samples) peak = std::max(peak, std::abs(sample));
    const double level = std::clamp(volume, 0, 100) / 100.0, gain = peak > 0 ? 0.513 * level * level * 32767 / peak : 0;
    const int frames = int(samples.size());
    QByteArray wave(44 + frames * 2, 0); auto* data = reinterpret_cast<uchar*>(wave.data());
    std::memcpy(data, "RIFF", 4); qToLittleEndian<quint32>(wave.size() - 8, data + 4); std::memcpy(data + 8, "WAVEfmt ", 8);
    qToLittleEndian<quint32>(16, data + 16); qToLittleEndian<quint16>(1, data + 20); qToLittleEndian<quint16>(1, data + 22);
    qToLittleEndian<quint32>(rate, data + 24); qToLittleEndian<quint32>(rate * 2, data + 28); qToLittleEndian<quint16>(2, data + 32); qToLittleEndian<quint16>(16, data + 34);
    std::memcpy(data + 36, "data", 4); qToLittleEndian<quint32>(quint32(frames * 2), data + 40);
    for (int frame = 0; frame < frames; ++frame) qToLittleEndian<qint16>(qint16(std::lround(std::clamp(samples[size_t(frame)] * gain, -32767.0, 32767.0))), data + 44 + frame * 2);
    return wave;
}
void playClipSavedSound(const QString& id, int volume, int times) {
#ifdef Q_OS_WIN
    // Windows reads the sound from memory while it plays: the one playing is stopped before its memory is replaced.
    static QByteArray playing;
    PlaySoundW(nullptr, nullptr, 0);
    playing = clipSavedSoundWave(id, volume, times);
    PlaySoundW(reinterpret_cast<LPCWSTR>(playing.constData()), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
#else
    Q_UNUSED(id); Q_UNUSED(times); if (volume > 0) QApplication::beep();
#endif
}
}
