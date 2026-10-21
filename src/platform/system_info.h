#pragma once
#include <QStringList>
namespace clipsapp {
// What a diagnostic says about the machine: the system, the graphics cards with their driver, the media libraries.
QString systemDescription();
QStringList graphicsAdapters();
QString mediaLibraries();
// The memory installed in the PC, in bytes; 0 if unknown.
qint64 physicalMemoryBytes();
// Whether the drive holding this folder is a hard drive (1), not one (0), or cannot be told (-1).
int rotationalDrive(const QString& folder);
}
