#pragma once
#include <QList>
#include <QRect>
#include <QString>
#include <optional>
namespace clipsapp {
// A screen as Windows shows it now. Its device (« \\.\DISPLAY1 ») may be given to another screen
// after a sleep or when a screen is plugged in; its path names the screen itself, on its port, and
// stays the same from one session to the next.
struct WindowsMonitor { void* handle = nullptr; QString device, path, name; int number = 0; QRect area; bool primary = false; };
QList<WindowsMonitor> windowsMonitors();
// How the settings name a screen: « screen: » and its path (its device for a screen without one).
QString monitorTargetId(const WindowsMonitor& monitor);
// What the settings show of it: « Écran 2 · DELL U2723QE · 2560 × 1440 · principal ».
QString monitorLabel(const WindowsMonitor& monitor);
// The screen a target names among these, whichever way it was written; none when it is not there.
std::optional<WindowsMonitor> findMonitor(const QString& targetId, const QList<WindowsMonitor>& monitors);
std::optional<WindowsMonitor> findMonitor(const QString& targetId);
// A target written before 0.8.5 named the device: it becomes the screen that device shows now.
// Any other target is given back as it is.
QString stableMonitorTarget(const QString& targetId);
}
