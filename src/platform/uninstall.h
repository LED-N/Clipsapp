#pragma once
#include <QString>
#include <QStringList>
namespace clipsapp {
// What a copy of the application leaves outside its own folder, and where.
struct UninstallPlan {
    QString installDirectory, executable;
    // Settings, classification of the clips, previews and journal.
    QString configDirectory;
    // Where the Start menu shortcut and the session start entry are; empty, they are not looked for.
    QString programsDirectory, loginLocation;
    bool keepSettings = false;
};
// How "Clipsapp.exe --uninstall" ends, as the uninstall script reads it.
enum UninstallCode { UninstallDone = 0, UninstallStillRunning = 2, UninstallClipsInside = 3, UninstallIncomplete = 4 };
struct UninstallResult { int code = UninstallDone; QStringList problems; };
// The folder of the clips when it lies inside the folder about to go, or nothing. Asked first:
// such an uninstall stops before anything is closed or removed.
QString clipsInside(const UninstallPlan& plan);
// Removes the Start menu shortcut and the session start entry when they lead to this executable
// (those of another copy stay), then the settings unless they are kept. Touches nothing when the
// clips are kept inside the folder about to go. Deleting that folder is left to the script.
UninstallResult removeTraces(const UninstallPlan& plan);
}
