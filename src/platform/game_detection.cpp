#include "game_detection.h"
#include "core/game_identity.h"
#include <QHash>
#include <QDir>
#include <QFileInfo>
#ifdef Q_OS_WIN
#include <windows.h>
#include <vector>
namespace {
QString productName(const QString& path) {
    DWORD unused = 0; const auto* name = reinterpret_cast<LPCWSTR>(path.utf16()); const auto size = GetFileVersionInfoSizeW(name, &unused);
    if (!size || size > 1024 * 1024) return {};
    std::vector<char> data(size); if (!GetFileVersionInfoW(name, 0, size, data.data())) return {};
    struct Translation { WORD language, page; }; Translation* translations = nullptr; UINT bytes = 0;
    if (!VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void**>(&translations), &bytes)) return {};
    for (UINT i = 0; i < bytes / sizeof(Translation); ++i) {
        const auto key = QString("\\StringFileInfo\\%1%2\\ProductName").arg(translations[i].language, 4, 16, QLatin1Char('0')).arg(translations[i].page, 4, 16, QLatin1Char('0'));
        wchar_t* value = nullptr; UINT length = 0;
        if (VerQueryValueW(data.data(), reinterpret_cast<LPCWSTR>(key.utf16()), reinterpret_cast<void**>(&value), &length) && length > 1) return QString::fromWCharArray(value, int(length) - 1);
    }
    return {};
}
}
#endif
namespace clipsapp {
QString detectCapturedGame(const QString& videoTarget) {
#ifdef Q_OS_WIN
    QString path, title;
    if (videoTarget.startsWith("window:")) {
        const int split = videoTarget.indexOf('|', 7); if (split < 0) return {};
        path = videoTarget.mid(7, split - 7); title = videoTarget.mid(split + 1);
    } else if (videoTarget.startsWith("screen:")) {
        const auto window = GetForegroundWindow(); if (!window || !IsWindowVisible(window) || IsIconic(window)) return {};
        DWORD pid = 0; GetWindowThreadProcessId(window, &pid); if (pid == GetCurrentProcessId()) return {};
        MONITORINFOEXW monitor{}; monitor.cbSize = sizeof(monitor);
        if (!GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONULL), &monitor) || QString::fromWCharArray(monitor.szDevice) != videoTarget.mid(7)) return {};
        RECT rect{}; if (!GetWindowRect(window, &rect)) return {};
        if (rect.right - rect.left < (monitor.rcMonitor.right - monitor.rcMonitor.left) * .6 || rect.bottom - rect.top < (monitor.rcMonitor.bottom - monitor.rcMonitor.top) * .6) return {};
        const auto process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid); if (!process) return {};
        wchar_t buffer[32768]; DWORD length = 32768; const bool found = QueryFullProcessImageNameW(process, 0, buffer, &length); CloseHandle(process); if (!found) return {};
        path = QString::fromWCharArray(buffer, int(length)); wchar_t caption[1024]; const int count = GetWindowTextW(window, caption, 1024); title = QString::fromWCharArray(caption, count);
    } else return {};
    static QHash<QString, QString> products;
    const auto key = path.toCaseFolded();
    if (!products.contains(key)) { if (products.size() >= 64) products.clear(); products.insert(key, productName(path)); }
    return gameNameFromApplication(path, title, products.value(key));
#else
    Q_UNUSED(videoTarget); return {};
#endif
}
}
