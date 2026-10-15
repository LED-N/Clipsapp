#include "system_info.h"
#include <QSysInfo>
extern "C" {
#include <libavutil/avutil.h>
}
#ifdef Q_OS_WIN
#include <windows.h>
#include <dxgi.h>
#endif

namespace clipsapp {
QString systemDescription() {
    return QStringLiteral("%1 (%2 %3) · %4").arg(QSysInfo::prettyProductName(), QSysInfo::kernelType(), QSysInfo::kernelVersion(), QSysInfo::currentCpuArchitecture());
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
