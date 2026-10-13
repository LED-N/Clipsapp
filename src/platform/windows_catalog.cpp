#include "source_catalog.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <QGuiApplication>
#include <QScreen>
#include <QSet>
#include <windows.h>
#include <initguid.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <functiondiscoverykeys_devpkey.h>
#include <propsys.h>
#include <propvarutil.h>
#include <algorithm>

namespace clipsapp {
namespace {
template<typename T> struct ComObject {
    T* value = nullptr;
    ~ComObject() { if (value) value->Release(); }
    T** address() { return &value; }
    T* operator->() const { return value; }
};
QString processPath(DWORD pid) {
    HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!handle) return {};
    wchar_t buffer[32768]; DWORD size = 32768;
    const bool ok = QueryFullProcessImageNameW(handle, 0, buffer, &size);
    CloseHandle(handle);
    return ok ? QString::fromWCharArray(buffer, static_cast<int>(size)) : QString{};
}
SourceTarget deviceTarget(IMMDevice* device) {
    LPWSTR id = nullptr;
    if (FAILED(device->GetId(&id))) return {};
    SourceTarget target {QString::fromWCharArray(id), {}};
    CoTaskMemFree(id);
    ComObject<IPropertyStore> properties;
    if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, properties.address()))) {
        PROPVARIANT name; PropVariantInit(&name);
        if (SUCCEEDED(properties->GetValue(PKEY_Device_FriendlyName, &name)) && name.vt == VT_LPWSTR) target.label = QString::fromWCharArray(name.pwszVal);
        PropVariantClear(&name);
    }
    if (target.label.isEmpty()) target.label = target.id;
    return target;
}
BOOL CALLBACK collectWindow(HWND hwnd, LPARAM argument) {
    if (!IsWindowVisible(hwnd) || GetWindowTextLengthW(hwnd) <= 0) return TRUE;
    DWORD pid = 0; GetWindowThreadProcessId(hwnd, &pid);
    if (pid == static_cast<DWORD>(QCoreApplication::applicationPid())) return TRUE;
    RECT rect; if (!GetWindowRect(hwnd, &rect) || rect.right - rect.left < 64 || rect.bottom - rect.top < 64) return TRUE;
    const auto path = processPath(pid);
    if (path.isEmpty()) return TRUE;
    wchar_t title[1024]; const int length = GetWindowTextW(hwnd, title, 1024);
    if (length == 0) return TRUE;
    const auto name = QString::fromWCharArray(title, length);
    auto* list = reinterpret_cast<QList<SourceTarget>*>(argument);
    list->append({"window:" + path.toCaseFolded() + "|" + name, name + " · " + QFileInfo(path).fileName()});
    return TRUE;
}
}
SourceCatalog SourceCatalog::discover() {
    SourceCatalog catalog;
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR monitor, HDC, LPRECT, LPARAM data) -> BOOL {
        auto& targets = *reinterpret_cast<QList<SourceTarget>*>(data); MONITORINFOEXW info{}; info.cbSize = sizeof(info);
        if (GetMonitorInfoW(monitor, &info)) {
            const auto name = QString::fromWCharArray(info.szDevice);
            targets.append({"screen:" + name, QStringLiteral("Écran %1 · %2 × %3%4").arg(name).arg(info.rcMonitor.right - info.rcMonitor.left).arg(info.rcMonitor.bottom - info.rcMonitor.top).arg(info.dwFlags & MONITORINFOF_PRIMARY ? QStringLiteral(" · principal") : QString())});
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&catalog.videoTargets));
    EnumWindows(collectWindow, reinterpret_cast<LPARAM>(&catalog.videoTargets));
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool uninitialize = SUCCEEDED(init);
    if (FAILED(init) && init != RPC_E_CHANGED_MODE) { catalog.notes << QStringLiteral("Les périphériques audio ne peuvent pas être interrogés."); return catalog; }
    {
        ComObject<IMMDeviceEnumerator> enumerator;
        if (FAILED(CoCreateInstance(CLSID_MMDeviceEnumerator, nullptr, CLSCTX_ALL, IID_IMMDeviceEnumerator, reinterpret_cast<void**>(enumerator.address())))) {
            catalog.notes << QStringLiteral("La liste des périphériques audio est indisponible.");
        } else {
            QSet<QString> applications;
            for (const EDataFlow flow : {eCapture, eRender}) {
                ComObject<IMMDeviceCollection> devices;
                if (FAILED(enumerator->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, devices.address()))) continue;
                UINT count = 0; devices->GetCount(&count);
                for (UINT index = 0; index < count; ++index) {
                    ComObject<IMMDevice> device;
                    if (FAILED(devices->Item(index, device.address()))) continue;
                    const auto target = deviceTarget(device.value);
                    if (target.id.isEmpty()) continue;
                    if (flow == eCapture) { catalog.microphones.append(target); continue; }
                    catalog.outputs.append(target);
                    ComObject<IAudioSessionManager2> manager;
                    if (FAILED(device->Activate(IID_IAudioSessionManager2, CLSCTX_ALL, nullptr, reinterpret_cast<void**>(manager.address())))) continue;
                    ComObject<IAudioSessionEnumerator> sessions;
                    if (FAILED(manager->GetSessionEnumerator(sessions.address()))) continue;
                    int sessionCount = 0; sessions->GetCount(&sessionCount);
                    for (int sessionIndex = 0; sessionIndex < sessionCount; ++sessionIndex) {
                        ComObject<IAudioSessionControl> control;
                        if (FAILED(sessions->GetSession(sessionIndex, control.address()))) continue;
                        ComObject<IAudioSessionControl2> control2;
                        if (FAILED(control->QueryInterface(IID_IAudioSessionControl2, reinterpret_cast<void**>(control2.address())))) continue;
                        DWORD pid = 0;
                        if (FAILED(control2->GetProcessId(&pid)) || pid == 0 || pid == static_cast<DWORD>(QCoreApplication::applicationPid())) continue;
                        const auto path = processPath(pid); const auto identity = path.toCaseFolded();
                        if (path.isEmpty() || applications.contains(identity)) continue;
                        applications.insert(identity);
                        catalog.applications.append({path, QFileInfo(path).fileName()});
                    }
                }
            }
        }
    }
    if (uninitialize) CoUninitialize();
    std::sort(catalog.applications.begin(), catalog.applications.end(), [](const auto& a, const auto& b) { return a.label.localeAwareCompare(b.label) < 0; });
    return catalog;
}
}
