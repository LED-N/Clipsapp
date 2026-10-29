#pragma once
#include <QByteArray>
#include <QList>
#include <QString>
namespace clipsapp {
struct SavedSound { QString id, name; };
// The sounds a saved clip can make, the first being the one of earlier versions.
QList<SavedSound> clipSavedSounds();
// That sound as a WAV file in memory, 16-bit mono. The volume goes from 0 to 100 and follows the
// ear rather than the amplitude: 50 is the level of earlier versions. An unknown sound is the first one.
// "times" plays it again after a short pause: a recording sounds once as it starts, twice as it ends.
QByteArray clipSavedSoundWave(const QString& id, int volume, int times = 1);
// Played without waiting; a sound still playing is cut by the next one.
void playClipSavedSound(const QString& id = {}, int volume = 50, int times = 1);
}
