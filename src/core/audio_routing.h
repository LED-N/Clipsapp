#pragma once
#include <QList>
#include <QStringList>
namespace clipsapp {
struct AudioProcess { quint32 pid = 0, parent = 0; QString executable; };
// What names an application across its updates: its path, whatever the case or the separators,
// with the version numbers of its folders set aside. Discord moves from "app-1.0.9261" to
// "app-1.0.9262" when it updates, and stays the same application.
QString applicationIdentity(const QString& executable);
// WASAPI captures a process tree. Select disjoint trees, and never select a
// tree containing an excluded process (including an excluded descendant).
QList<quint32> includedAudioRoots(const QList<AudioProcess>& processes,
                                const QList<quint32>& sessions,
                                const QStringList& exclusions, quint32 ownPid);
}
