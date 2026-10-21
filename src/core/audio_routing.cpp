#include "audio_routing.h"
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
namespace clipsapp {
QString applicationIdentity(const QString& executable) {
    static const QRegularExpression version(QStringLiteral("\\d+(?:\\.\\d+)+"));
    auto parts = QString(executable).replace('\\', '/').toCaseFolded().split('/');
    for (int index = 0; index + 1 < parts.size(); ++index) parts[index].replace(version, QStringLiteral("#"));
    return parts.join('/');
}
QList<quint32> includedAudioRoots(const QList<AudioProcess>& processes, const QList<quint32>& sessions,
                                const QStringList& exclusions, quint32 ownPid) {
    QHash<quint32, AudioProcess> graph; QSet<QString> paths;
    for (const auto& path : exclusions) paths.insert(applicationIdentity(path));
    QSet<quint32> blocked, hasBlockedChild;
    for (const auto& process : processes) {
        graph.insert(process.pid, process);
        if (process.pid == ownPid || paths.contains(applicationIdentity(process.executable))) blocked.insert(process.pid);
    }
    auto ancestors = [&](quint32 pid) {
        QList<quint32> result; QSet<quint32> seen;
        for (int budget = 0; pid && budget < 256 && !seen.contains(pid); ++budget) {
            seen.insert(pid); result.append(pid); pid = graph.value(pid).parent;
        }
        return result;
    };
    for (auto pid : blocked) for (auto ancestor : ancestors(pid)) hasBlockedChild.insert(ancestor);
    QSet<quint32> candidates;
    for (auto pid : sessions) {
        if (!graph.contains(pid) || graph.value(pid).executable.isEmpty() || hasBlockedChild.contains(pid)) continue;
        bool excluded = false; for (auto ancestor : ancestors(pid)) if (blocked.contains(ancestor)) { excluded = true; break; }
        if (!excluded) candidates.insert(pid);
    }
    QList<quint32> roots;
    for (auto pid : candidates) {
        bool covered = false; for (auto ancestor : ancestors(pid)) if (ancestor != pid && candidates.contains(ancestor)) { covered = true; break; }
        if (!covered) roots.append(pid);
    }
    std::sort(roots.begin(), roots.end()); return roots;
}
}
