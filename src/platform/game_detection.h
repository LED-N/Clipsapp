#pragma once
#include <QString>
#include <QStringList>
namespace clipsapp {
// Selected windows use their executable. Screen capture observes only large
// foreground windows on that monitor, never changing the capture target.
QString detectCapturedGame(const QString& videoTarget);
// The executables among these that run now, compared by full path, regardless of case and separators.
QStringList runningPrograms(const QStringList& executables);
// The identifiers of the processes that run this executable, compared the same way.
QList<qint64> programProcesses(const QString& executable);
}
