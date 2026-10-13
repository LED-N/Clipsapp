#pragma once
#include <QList>
#include <QStringList>
namespace clipsapp {
struct AudioProcess { quint32 pid = 0, parent = 0; QString executable; };
// WASAPI captures a process tree. Select disjoint trees, and never select a
// tree containing an excluded process (including an excluded descendant).
QList<quint32> includedAudioRoots(const QList<AudioProcess>& processes,
                                const QList<quint32>& sessions,
                                const QStringList& exclusions, quint32 ownPid);
}
