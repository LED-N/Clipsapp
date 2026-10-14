#pragma once
#include <QString>
namespace clipsapp {
// The application has no installer: it adds itself to the Start menu, as one shortcut named
// after it in the folder where Windows lists the user's programs.
QString startMenuShortcut(const QString& programsDirectory);
// Whether that shortcut is there and still leads to a program that exists.
bool startMenuShortcutWorks(const QString& programsDirectory);
// Adds the shortcut to this executable, or removes it. Nothing else in the folder is touched.
bool setStartMenuShortcut(const QString& programsDirectory, const QString& executable, bool present, QString& error);
}
