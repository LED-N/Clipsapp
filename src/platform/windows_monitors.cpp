#include "windows_monitors.h"
#include <QHash>
#include <QRegularExpression>
#include <windows.h>
#include <vector>
namespace clipsapp {
QList<WindowsMonitor> windowsMonitors() {
    // The path and the name of each screen come from the display configuration, by the device it is on now.
    QHash<QString, std::pair<QString, QString>> byDevice;
    UINT32 pathCount = 0, modeCount = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) == ERROR_SUCCESS && pathCount > 0) {
        std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount); std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
        if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), nullptr) == ERROR_SUCCESS) {
            for (UINT32 index = 0; index < pathCount; ++index) {
                DISPLAYCONFIG_SOURCE_DEVICE_NAME source{}; source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME; source.header.size = sizeof(source);
                source.header.adapterId = paths[index].sourceInfo.adapterId; source.header.id = paths[index].sourceInfo.id;
                DISPLAYCONFIG_TARGET_DEVICE_NAME target{}; target.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME; target.header.size = sizeof(target);
                target.header.adapterId = paths[index].targetInfo.adapterId; target.header.id = paths[index].targetInfo.id;
                if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS || DisplayConfigGetDeviceInfo(&target.header) != ERROR_SUCCESS) continue;
                // A screen duplicated on another keeps the first of them.
                const auto device = QString::fromWCharArray(source.viewGdiDeviceName);
                if (!byDevice.contains(device)) byDevice.insert(device, {QString::fromWCharArray(target.monitorDevicePath), QString::fromWCharArray(target.monitorFriendlyDeviceName).trimmed()});
            }
        }
    }
    QList<WindowsMonitor> monitors;
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR handle, HDC, LPRECT, LPARAM data) -> BOOL {
        MONITORINFOEXW info{}; info.cbSize = sizeof(info);
        if (GetMonitorInfoW(handle, &info)) {
            WindowsMonitor monitor; monitor.handle = handle; monitor.device = QString::fromWCharArray(info.szDevice); monitor.primary = info.dwFlags & MONITORINFOF_PRIMARY;
            monitor.area = QRect(info.rcMonitor.left, info.rcMonitor.top, info.rcMonitor.right - info.rcMonitor.left, info.rcMonitor.bottom - info.rcMonitor.top);
            reinterpret_cast<QList<WindowsMonitor>*>(data)->append(monitor);
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&monitors));
    static const QRegularExpression digits(QStringLiteral("(\\d+)\\D*$"));
    for (auto& monitor : monitors) {
        const auto found = byDevice.value(monitor.device); monitor.path = found.first; monitor.name = found.second;
        // Without the display configuration, the interface of the screen on its device names it as well.
        if (monitor.path.isEmpty()) { DISPLAY_DEVICEW display{}; display.cb = sizeof(display); if (EnumDisplayDevicesW(reinterpret_cast<LPCWSTR>(monitor.device.utf16()), 0, &display, EDD_GET_DEVICE_INTERFACE_NAME)) monitor.path = QString::fromWCharArray(display.DeviceID); }
        // The number Windows gives the screen in its own settings.
        const auto match = digits.match(monitor.device); monitor.number = match.hasMatch() ? match.captured(1).toInt() : 0;
    }
    return monitors;
}
QString monitorTargetId(const WindowsMonitor& monitor) { return QStringLiteral("screen:") + (monitor.path.isEmpty() ? monitor.device : monitor.path); }
QString monitorLabel(const WindowsMonitor& monitor) {
    QStringList parts{monitor.number ? QStringLiteral("Écran %1").arg(monitor.number) : QStringLiteral("Écran")};
    if (!monitor.name.isEmpty()) parts << monitor.name;
    parts << QStringLiteral("%1 × %2").arg(monitor.area.width()).arg(monitor.area.height());
    if (monitor.primary) parts << QStringLiteral("principal");
    return parts.join(QStringLiteral(" · "));
}
std::optional<WindowsMonitor> findMonitor(const QString& targetId, const QList<WindowsMonitor>& monitors) {
    if (!targetId.startsWith(QLatin1String("screen:"))) return {};
    const auto key = targetId.mid(7);
    for (const auto& monitor : monitors) if (!monitor.path.isEmpty() && monitor.path.compare(key, Qt::CaseInsensitive) == 0) return monitor;
    // A screen without a path, or a target written before paths: by its device.
    for (const auto& monitor : monitors) if (monitor.device.compare(key, Qt::CaseInsensitive) == 0) return monitor;
    return {};
}
std::optional<WindowsMonitor> findMonitor(const QString& targetId) { return findMonitor(targetId, windowsMonitors()); }
QString stableMonitorTarget(const QString& targetId) {
    if (!targetId.startsWith(QLatin1String("screen:\\\\.\\"))) return targetId;
    const auto monitor = findMonitor(targetId);
    return monitor ? monitorTargetId(*monitor) : targetId;
}
}
