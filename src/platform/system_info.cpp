#include "system_info.h"
#include <QDir>
#include <QSysInfo>
extern "C" {
#include <libavutil/avutil.h>
}
#ifdef Q_OS_WIN
#include <windows.h>
#include <winioctl.h>
#include <dxgi.h>
#endif

namespace clipsapp {
QString systemDescription() {
    return QStringLiteral("%1 (%2 %3) · %4").arg(QSysInfo::prettyProductName(), QSysInfo::kernelType(), QSysInfo::kernelVersion(), QSysInfo::currentCpuArchitecture());
}
qint64 physicalMemoryBytes() {
#ifdef Q_OS_WIN
    MEMORYSTATUSEX status{}; status.dwLength = sizeof(status);
    return GlobalMemoryStatusEx(&status) ? qint64(status.ullTotalPhys) : 0;
#else
    return 0;
#endif
}
int rotationalDrive(const QString& folder) {
#ifdef Q_OS_WIN
    // The volume is opened without any access right: asking how it seeks needs no administrator.
    wchar_t mount[MAX_PATH]; const auto path = QDir::toNativeSeparators(QDir::cleanPath(folder));
    if (!GetVolumePathNameW(reinterpret_cast<LPCWSTR>(path.utf16()), mount, MAX_PATH)) return -1;
    auto volume = QString::fromWCharArray(mount); if (!volume.endsWith('\\') || volume.size() != 3) return -1;
    volume = QStringLiteral("\\\\.\\") + volume.left(2);
    const HANDLE device = CreateFileW(reinterpret_cast<LPCWSTR>(volume.utf16()), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (device == INVALID_HANDLE_VALUE) return -1;
    STORAGE_PROPERTY_QUERY query{}; query.PropertyId = StorageDeviceSeekPenaltyProperty; query.QueryType = PropertyStandardQuery;
    DEVICE_SEEK_PENALTY_DESCRIPTOR answer{}; DWORD size = 0;
    const bool known = DeviceIoControl(device, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query), &answer, sizeof(answer), &size, nullptr) && size >= sizeof(answer);
    CloseHandle(device);
    return known ? (answer.IncursSeekPenalty ? 1 : 0) : -1;
#else
    Q_UNUSED(folder); return -1;
#endif
}
QStringList graphicsAdapters() {
    QStringList adapters;
#ifdef Q_OS_WIN
    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory)))) return adapters;
    IDXGIAdapter1* adapter = nullptr;
    for (UINT index = 0; factory->EnumAdapters1(index, &adapter) != DXGI_ERROR_NOT_FOUND; ++index) {
        DXGI_ADAPTER_DESC1 description{};
        if (SUCCEEDED(adapter->GetDesc1(&description)) && !(description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
            auto text = QString::fromWCharArray(description.Description);
            // The version of the user-mode driver, as Windows lists it for the card.
            LARGE_INTEGER driver{};
            if (SUCCEEDED(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &driver)))
                text += QStringLiteral(" · pilote %1.%2.%3.%4").arg(HIWORD(driver.HighPart)).arg(LOWORD(driver.HighPart)).arg(HIWORD(driver.LowPart)).arg(LOWORD(driver.LowPart));
            text += QStringLiteral(" · %1 Mio dédiés").arg(qulonglong(description.DedicatedVideoMemory / 1048576));
            if (!adapters.contains(text)) adapters.append(text);
        }
        adapter->Release();
    }
    factory->Release();
#endif
    return adapters;
}
QString mediaLibraries() { return QStringLiteral("Qt %1 · FFmpeg %2").arg(QString::fromLatin1(qVersion()), QString::fromLatin1(av_version_info())); }
}
