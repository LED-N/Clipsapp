#pragma once
#include <QString>
namespace clipsapp {
// Where the system keeps the programs it starts with the user's session: a registry key on
// Windows, the autostart folder on Linux.
QString loginStartLocation();
// What is written there for this executable. Started that way, the application waits behind its icon.
QString loginStartCommand(const QString& executable);
// Whether this location starts a program that still exists.
bool loginStartWorks(const QString& location);
// The program the entry starts, or nothing when there is no entry.
QString loginStartProgram(const QString& location);
// Adds the entry for this executable, or removes it. Nothing else in the location is touched.
bool setLoginStart(const QString& location, const QString& executable, bool present, QString& error);
}
