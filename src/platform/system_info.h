#pragma once
#include <QStringList>
namespace clipsapp {
// What a diagnostic says about the machine: the system, the graphics cards with their driver, the media libraries.
QString systemDescription();
QStringList graphicsAdapters();
QString mediaLibraries();
}
