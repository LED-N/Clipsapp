#include "clip_feedback.h"
#include <QApplication>
#include <algorithm>
#include <cstring>
#ifdef Q_OS_WIN
#include <windows.h>
#include <mmsystem.h>
#include <QByteArray>
#include <QtEndian>
#include <cmath>
namespace {
QByteArray makeCue() {
    constexpr int rate = 24000, frames = 4320;
    QByteArray wave(44 + frames * 2, 0); auto* data = reinterpret_cast<uchar*>(wave.data());
    std::memcpy(data, "RIFF", 4); qToLittleEndian<quint32>(wave.size() - 8, data + 4); std::memcpy(data + 8, "WAVEfmt ", 8);
    qToLittleEndian<quint32>(16, data + 16); qToLittleEndian<quint16>(1, data + 20); qToLittleEndian<quint16>(1, data + 22);
    qToLittleEndian<quint32>(rate, data + 24); qToLittleEndian<quint32>(rate * 2, data + 28); qToLittleEndian<quint16>(2, data + 32); qToLittleEndian<quint16>(16, data + 34);
    std::memcpy(data + 36, "data", 4); qToLittleEndian<quint32>(frames * 2, data + 40);
    for (int frame = 0; frame < frames; ++frame) {
        const int local = frame % (frames / 2); const double envelope = std::min(1.0, local / 240.0) * std::min(1.0, (frames / 2 - local) / 600.0);
        const double hz = frame < frames / 2 ? 880 : 1175;
        const auto sample = qint16(4200 * envelope * std::sin(6.283185307179586 * hz * frame / rate)); qToLittleEndian<quint16>(quint16(sample), data + 44 + frame * 2);
    }
    return wave;
}
}
#endif
namespace clipsapp {
void playClipSavedSound() {
#ifdef Q_OS_WIN
    static const auto cue = makeCue(); PlaySoundW(reinterpret_cast<LPCWSTR>(cue.constData()), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
#else
    QApplication::beep();
#endif
}
}
