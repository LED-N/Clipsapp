#include "windows_audio.h"
#include "capture/audio_stream.h"
#include "core/audio_routing.h"
#include "platform/windows_monitors.h"
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <audiopolicy.h>
#include <tlhelp32.h>
#include <QMutex>
#include <QMutexLocker>
#include <QHash>
#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <deque>
#include <functional>
#include <map>
#include <stdexcept>
#include <thread>

namespace clipsapp {
namespace {
void check(HRESULT result, const char* operation) {
    if (FAILED(result)) throw std::runtime_error(QString("%1 (Windows 0x%2)").arg(operation).arg(quint32(result), 8, 16, QLatin1Char('0')).toStdString());
}
template<class T> struct Com {
    T* p = nullptr;
    ~Com() { if (p) p->Release(); }
    T** out() { return &p; }
    T* operator->() const { return p; }
};
struct Apartment {
    Apartment() { check(CoInitializeEx(nullptr, COINIT_MULTITHREADED), "Initialisation audio COM"); }
    ~Apartment() { CoUninitialize(); }
};
QString processPath(DWORD pid) {
    const HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!handle) return {};
    wchar_t path[32768]; DWORD size = 32768;
    const bool ok = QueryFullProcessImageNameW(handle, 0, path, &size); CloseHandle(handle);
    return ok ? QString::fromWCharArray(path, int(size)) : QString();
}
class Activation final : public IActivateAudioInterfaceCompletionHandler {
public:
    Activation() : event(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}
    ~Activation() { if (client) client->Release(); CloseHandle(event); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        *object = nullptr;
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IActivateAudioInterfaceCompletionHandler) || iid == IID_IAgileObject) {
            *object = static_cast<IActivateAudioInterfaceCompletionHandler*>(this); AddRef(); return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { const auto count = --refs; if (!count) delete this; return count; }
    HRESULT STDMETHODCALLTYPE ActivateCompleted(IActivateAudioInterfaceAsyncOperation* operation) override {
        Com<IUnknown> unknown; HRESULT activation = E_FAIL;
        result = operation->GetActivateResult(&activation, unknown.out());
        if (SUCCEEDED(result)) result = activation;
        if (SUCCEEDED(result)) result = unknown->QueryInterface(__uuidof(IAudioClient), reinterpret_cast<void**>(&client));
        SetEvent(event); return S_OK;
    }
    HANDLE event; IAudioClient* client = nullptr; HRESULT result = E_FAIL;
private:
    std::atomic<ULONG> refs{1};
};
// ABI from audioclientactivationparams.h, absent from the portable MinGW SDK.
struct ProcessActivation { int type = 1; struct { DWORD pid; int mode = 0; } process; };
IAudioClient* openApplication(DWORD pid) {
    ProcessActivation params; params.process.pid = pid;
    PROPVARIANT variant{}; variant.vt = VT_BLOB; variant.blob.cbSize = sizeof(params); variant.blob.pBlobData = reinterpret_cast<BYTE*>(&params);
    auto* callback = new Activation;
    Com<IActivateAudioInterfaceAsyncOperation> operation;
    HRESULT result = ActivateAudioInterfaceAsync(L"VAD\\Process_Loopback", __uuidof(IAudioClient), &variant, callback, operation.out());
    if (SUCCEEDED(result)) {
        if (WaitForSingleObject(callback->event, 5000) != WAIT_OBJECT_0) result = HRESULT_FROM_WIN32(ERROR_TIMEOUT);
        else result = callback->result;
    }
    IAudioClient* client = nullptr;
    if (SUCCEEDED(result)) { client = callback->client; client->AddRef(); }
    callback->Release(); check(result, "Activation de l'audio de l'application"); return client;
}
struct MixFormat {
    WAVEFORMATEX* p = nullptr;
    ~MixFormat() { CoTaskMemFree(p); }
};
AudioInputFormat inputFormat(const WAVEFORMATEX& wave) {
    constexpr GUID pcm{WAVE_FORMAT_PCM, 0, 0x0010, {0x80,0x00,0x00,0xaa,0x00,0x38,0x9b,0x71}};
    constexpr GUID floating{WAVE_FORMAT_IEEE_FLOAT, 0, 0x0010, {0x80,0x00,0x00,0xaa,0x00,0x38,0x9b,0x71}};
    AudioInputFormat result{int(wave.nSamplesPerSec), int(wave.nChannels)};
    WORD tag = wave.wFormatTag;
    if (tag == WAVE_FORMAT_EXTENSIBLE && wave.cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) {
        const auto& extended = reinterpret_cast<const WAVEFORMATEXTENSIBLE&>(wave);
        result.channelMask = extended.dwChannelMask;
        if (extended.SubFormat == floating) tag = WAVE_FORMAT_IEEE_FLOAT;
        else if (extended.SubFormat == pcm) tag = WAVE_FORMAT_PCM;
    }
    if (tag == WAVE_FORMAT_IEEE_FLOAT && wave.wBitsPerSample == 32) result.samples = AudioSampleFormat::Float32;
    else if (tag == WAVE_FORMAT_IEEE_FLOAT && wave.wBitsPerSample == 64) result.samples = AudioSampleFormat::Float64;
    else if (tag == WAVE_FORMAT_PCM && wave.wBitsPerSample == 8) result.samples = AudioSampleFormat::Unsigned8;
    else if (tag == WAVE_FORMAT_PCM && wave.wBitsPerSample == 16) result.samples = AudioSampleFormat::Signed16;
    else if (tag == WAVE_FORMAT_PCM && wave.wBitsPerSample == 24) result.samples = AudioSampleFormat::Signed24;
    else if (tag == WAVE_FORMAT_PCM && wave.wBitsPerSample == 32) result.samples = AudioSampleFormat::Signed32;
    else throw std::runtime_error("Format natif du périphérique audio non pris en charge.");
    if (wave.nBlockAlign != wave.nChannels * (wave.wBitsPerSample / 8)) throw std::runtime_error("Alignement audio non pris en charge.");
    return result;
}
class SessionNotification final : public IAudioSessionNotification {
public:
    SessionNotification() : event(CreateEventW(nullptr, FALSE, FALSE, nullptr)) {
        if (!event) throw std::runtime_error("Impossible de suivre les nouvelles applications audio.");
    }
    ~SessionNotification() { for (auto* session : sessions) session->Release(); CloseHandle(event); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        *object = nullptr;
        if (iid != __uuidof(IUnknown) && iid != __uuidof(IAudioSessionNotification)) return E_NOINTERFACE;
        *object = static_cast<IAudioSessionNotification*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { const auto count = --refs; if (!count) delete this; return count; }
    HRESULT STDMETHODCALLTYPE OnSessionCreated(IAudioSessionControl* session) override {
        if (!session) return E_POINTER;
        { QMutexLocker lock(&mutex); if (sessions.size() < 4096) { session->AddRef(); sessions.append(session); } else overflow = true; }
        SetEvent(event); return S_OK;
    }
    QList<DWORD> processes() {
        QMutexLocker lock(&mutex);
        if (overflow) throw std::runtime_error("Trop de sessions audio pour le mode automatique.");
        QSet<DWORD> pids;
        for (auto it = sessions.begin(); it != sessions.end();) {
            AudioSessionState state;
            if (FAILED((*it)->GetState(&state)) || state == AudioSessionStateExpired) { (*it)->Release(); it = sessions.erase(it); continue; }
            Com<IAudioSessionControl2> control; DWORD pid = 0;
            if (SUCCEEDED((*it)->QueryInterface(__uuidof(IAudioSessionControl2), reinterpret_cast<void**>(control.out()))) && SUCCEEDED(control->GetProcessId(&pid)) && pid) pids.insert(pid);
            ++it;
        }
        return pids.values();
    }
    HANDLE event;
private:
    std::atomic<ULONG> refs{1}; QMutex mutex; QList<IAudioSessionControl*> sessions; bool overflow = false;
};
// Windows says when a device comes, goes, changes state or becomes the default one: the sources
// wait for that rather than looking again every second. Owned by its creator, who unregisters it
// before it goes; its reference count is therefore not what keeps it.
class DeviceWatch final : public IMMNotificationClient {
public:
    explicit DeviceWatch(IMMDeviceEnumerator* enumerator) : m_enumerator(enumerator) {
        event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!event) throw std::runtime_error("Impossible de suivre les périphériques audio.");
        const HRESULT result = m_enumerator->RegisterEndpointNotificationCallback(this);
        if (FAILED(result)) { CloseHandle(event); check(result, "Suivi des périphériques audio"); }
    }
    ~DeviceWatch() { m_enumerator->UnregisterEndpointNotificationCallback(this); CloseHandle(event); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        *object = nullptr;
        if (iid != __uuidof(IUnknown) && iid != __uuidof(IMMNotificationClient)) return E_NOINTERFACE;
        *object = static_cast<IMMNotificationClient*>(this); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) override { SetEvent(event); return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override { SetEvent(event); return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override { SetEvent(event); return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow, ERole, LPCWSTR) override { SetEvent(event); return S_OK; }
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override { return S_OK; }
    // Set by any of these changes; taken by the wait that sees it.
    HANDLE event = nullptr;
private:
    IMMDeviceEnumerator* m_enumerator;
};
struct EndpointSessions {
    Com<IAudioSessionManager2> manager; SessionNotification* notification = nullptr;
    ~EndpointSessions() { if (notification) manager->UnregisterSessionNotification(notification); }
};
class AutomaticSessions {
public:
    AutomaticSessions() {
        check(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(enumerator.out())), "Suivi des applications audio");
        notification.p = new SessionNotification; refreshDevices(); devices = std::make_unique<DeviceWatch>(enumerator.p);
    }
    ~AutomaticSessions() { devices.reset(); }
    // A new application plays sound; an output came, went or changed.
    HANDLE sessionEvent() const { return notification->event; }
    HANDLE deviceEvent() const { return devices->event; }
    void refreshDevices() {
        Com<IMMDeviceCollection> devices;
        check(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, devices.out()), "Sorties audio des applications");
        UINT count = 0; check(devices->GetCount(&count), "Nombre de sorties audio"); QSet<QString> current;
        for (UINT index = 0; index < count; ++index) {
            // A headset unplugged while the list is read fails on its own: the other outputs are still followed.
            try {
                Com<IMMDevice> device; check(devices->Item(index, device.out()), "Sortie audio");
                LPWSTR id = nullptr; check(device->GetId(&id), "Identifiant de la sortie audio"); const auto key = QString::fromWCharArray(id); CoTaskMemFree(id); current.insert(key);
                if (endpoints.count(key)) continue;
                auto endpoint = std::make_unique<EndpointSessions>();
                check(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(endpoint->manager.out())), "Sessions audio de la sortie");
                check(endpoint->manager->RegisterSessionNotification(notification.p), "Notifications des applications audio"); endpoint->notification = notification.p;
                Com<IAudioSessionEnumerator> sessions; check(endpoint->manager->GetSessionEnumerator(sessions.out()), "Sessions audio existantes");
                int size = 0; check(sessions->GetCount(&size), "Nombre de sessions audio");
                for (int item = 0; item < size; ++item) { Com<IAudioSessionControl> session; if (SUCCEEDED(sessions->GetSession(item, session.out()))) notification->OnSessionCreated(session.p); }
                endpoints.emplace(key, std::move(endpoint));
            } catch (const std::exception& error) { qWarning("Audio : une sortie n'a pas pu être suivie (%s).", error.what()); }
        }
        for (auto it = endpoints.begin(); it != endpoints.end();) if (!current.contains(it->first)) it = endpoints.erase(it); else ++it;
    }
    QList<quint32> roots(const QStringList& exclusions) {
        const auto sessions = notification->processes(); QSet<QString> names;
        for (auto path : exclusions) names.insert(QFileInfo(path.replace('\\', '/')).fileName().toCaseFolded());
        QList<AudioProcess> processes;
        const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE) throw std::runtime_error("Liste des processus audio indisponible.");
        PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry);
        if (Process32FirstW(snapshot, &entry)) do {
            QString path;
            const auto name = QString::fromWCharArray(entry.szExeFile).toCaseFolded();
            if (sessions.contains(entry.th32ProcessID) || names.contains(name)) path = processPath(entry.th32ProcessID);
            // If an excluded executable cannot be inspected, omit its whole
            // tree conservatively rather than letting it leak into the mix.
            if (path.isEmpty() && names.contains(name)) for (auto excluded : exclusions)
                if (QFileInfo(excluded.replace('\\', '/')).fileName().toCaseFolded() == name) { path = excluded; break; }
            processes.append({entry.th32ProcessID, entry.th32ParentProcessID, path});
        } while (Process32NextW(snapshot, &entry));
        CloseHandle(snapshot);
        QList<quint32> ids; for (auto pid : sessions) ids.append(pid);
        return includedAudioRoots(processes, ids, exclusions, GetCurrentProcessId());
    }
private:
    Com<IMMDeviceEnumerator> enumerator; Com<SessionNotification> notification;
    std::map<QString, std::unique_ptr<EndpointSessions>> endpoints;
    std::unique_ptr<DeviceWatch> devices;
};
struct Event {
    HANDLE handle;
    explicit Event(bool manual) : handle(CreateEventW(nullptr, manual, FALSE, nullptr)) { if (!handle) throw std::runtime_error("Événement audio impossible à créer."); }
    ~Event() { CloseHandle(handle); }
};
}
qint64 windowsClock100ns() {
    LARGE_INTEGER counter, frequency; QueryPerformanceCounter(&counter); QueryPerformanceFrequency(&frequency);
    return qint64((static_cast<long double>(counter.QuadPart) * 10000000.0L) / frequency.QuadPart);
}
std::vector<unsigned long> applicationProcesses(const QString& executable) {
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return {};
    PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry);
    QHash<DWORD, DWORD> parents;
    QSet<DWORD> candidates; const auto filename = QFileInfo(QString(executable).replace('\\', '/')).fileName(), identity = applicationIdentity(executable);
    if (Process32FirstW(snapshot, &entry)) do {
        parents.insert(entry.th32ProcessID, entry.th32ParentProcessID);
        if (QString::fromWCharArray(entry.szExeFile).compare(filename, Qt::CaseInsensitive) == 0) candidates.insert(entry.th32ProcessID);
    } while (Process32NextW(snapshot, &entry));
    CloseHandle(snapshot);
    QList<DWORD> matching;
    for (auto it = parents.cbegin(); it != parents.cend(); ++it)
        if (candidates.contains(it.key()) && it.key() != GetCurrentProcessId() && applicationIdentity(processPath(it.key())) == identity) matching.append(it.key());
    std::vector<unsigned long> roots;
    for (DWORD pid : matching) {
        DWORD ancestor = parents.value(pid); bool child = false;
        for (int guard = 0; guard < 64 && ancestor; ++guard) {
            if (matching.contains(ancestor)) { child = true; break; }
            const DWORD next = parents.value(ancestor); if (next == ancestor) break; ancestor = next;
        }
        if (!child) roots.push_back(pid);
    }
    return roots;
}
QString windowsVideoOptions(const QString& targetId) {
    if (targetId.startsWith("screen:")) {
        // The screen itself, wherever Windows put it: its device may have changed since it was chosen.
        if (const auto monitor = findMonitor(targetId)) return QString("hmonitor=%1").arg(quintptr(monitor->handle));
    } else if (targetId.startsWith("window:")) {
        const int separator = targetId.indexOf('|', 7);
        struct Search { QString path, title; HWND handle = nullptr; } search{targetId.mid(7, separator - 7), targetId.mid(separator + 1), nullptr};
        if (separator >= 0) EnumWindows([](HWND window, LPARAM data) -> BOOL {
            auto& query = *reinterpret_cast<Search*>(data);
            if (!IsWindowVisible(window)) return TRUE;
            const int length = GetWindowTextLengthW(window); std::vector<wchar_t> title(size_t(length + 1)); GetWindowTextW(window, title.data(), length + 1);
            DWORD pid; GetWindowThreadProcessId(window, &pid);
            if (QString::fromWCharArray(title.data()) == query.title && processPath(pid).compare(QDir::toNativeSeparators(query.path), Qt::CaseInsensitive) == 0) { query.handle = window; return FALSE; }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&search));
        if (search.handle) return QString("hwnd=%1").arg(quintptr(search.handle));
    }
    throw std::runtime_error("L'écran ou la fenêtre choisi n'est plus disponible. Actualisez les sources.");
}
struct WindowsAudio::Impl {
    AudioSource source;
    std::atomic<bool> stop{false};
    // Set with stop: wakes the threads that wait for a device or an application.
    Event stopped{true};
    std::vector<std::thread> threads;
    QMutex mutex;
    std::deque<AudioChunk> queue;
    QString failure, unavailable;
    std::atomic<int> initialized{0};
    QByteArray label() const { return source.name.toUtf8(); }
    // Reads one stream until keepGoing says otherwise. Its errors are thrown: the caller decides
    // whether they end the capture or only silence the source until it can be opened again.
    void pump(IAudioClient* client, bool application, qint64 epoch, const std::function<bool()>& keepGoing, const std::function<void()>& started) {
        // Endpoints use their shared-engine format: a USB microphone can be
        // mono/44.1 kHz/PCM rather than the previous forced stereo float.
        // Process loopback requires an explicit format (no endpoint mix).
        WAVEFORMATEX processFormat{}; processFormat.wFormatTag = WAVE_FORMAT_IEEE_FLOAT; processFormat.nChannels = 2;
        processFormat.nSamplesPerSec = 48000; processFormat.wBitsPerSample = 32; processFormat.nBlockAlign = 8; processFormat.nAvgBytesPerSec = 384000;
        MixFormat native;
        if (!application) check(client->GetMixFormat(&native.p), "Format natif du périphérique audio");
        const WAVEFORMATEX* format = native.p ? native.p : &processFormat;
        AudioStream stream(inputFormat(*format));
        DWORD flags = source.kind == SourceKind::Microphone ? 0 : AUDCLNT_STREAMFLAGS_LOOPBACK;
        if (application) flags |= AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
        check(client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, 2000000, 0, format, nullptr), "Initialisation du flux audio");
        Com<IAudioCaptureClient> capture; check(client->GetService(__uuidof(IAudioCaptureClient), reinterpret_cast<void**>(capture.out())), "Capture audio");
        check(client->Start(), "Démarrage audio"); started();
        UINT64 nextPosition = 0; bool hasPosition = false;
        std::vector<uint8_t> copied;
        while (keepGoing()) {
            UINT32 available = 0; check(capture->GetNextPacketSize(&available), "Lecture audio");
            while (available && keepGoing()) {
                BYTE* data; UINT32 frames; DWORD packetFlags; UINT64 position, qpc;
                check(capture->GetBuffer(&data, &frames, &packetFlags, &position, &qpc), "Lecture du paquet audio");
                if (!frames) break;
                const bool validTimestamp = !(packetFlags & AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR);
                const qint64 timestamp = validTimestamp ? qint64(qpc) : windowsClock100ns() - qint64(frames) * 10000000 / format->nSamplesPerSec;
                const bool discontinuity = hasPosition && ((packetFlags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) ||
                    (!application && validTimestamp && position != nextPosition));
                nextPosition = position + frames; hasPosition = true;
                const bool silent = (packetFlags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
                copied.resize(silent ? 0 : size_t(frames) * format->nBlockAlign);
                if (!silent) std::memcpy(copied.data(), data, copied.size());
                // Release the Windows packet before conversion, keeping the
                // capture thread from holding up the next hardware period.
                check(capture->ReleaseBuffer(frames), "Libération du paquet audio");
                auto chunk = stream.convert(silent ? nullptr : copied.data(), int(frames),
                    qint64(std::llround((timestamp - epoch) * (48000.0L / 10000000))), validTimestamp, discontinuity);
                if (!chunk.samples.empty()) { QMutexLocker lock(&mutex); queue.push_back(std::move(chunk));
                  const qint64 now = (windowsClock100ns() - epoch) * 48000 / 10000000;
                  while (!queue.empty() && (queue.front().start < now - 96000 || queue.size() > 400)) queue.pop_front(); }
                check(capture->GetNextPacketSize(&available), "Lecture audio");
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        client->Stop();
    }
    // A microphone or an output. While its device is missing, unplugged or refused, its track stays
    // silent and the replay goes on, in step, without it; the source is tried again when Windows says
    // a device came or changed. A source that follows the default device of the system also follows
    // it when it changes. Nothing is looked at while all is well.
    void runDevice(qint64 epoch) {
        bool counted = false; const auto count = [&] { if (!counted) { counted = true; ++initialized; } };
        try {
            Apartment apartment;
            Com<IMMDeviceEnumerator> enumerator;
            check(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(enumerator.out())), "Liste des périphériques audio");
            DeviceWatch devices(enumerator.p);
            const bool followsDefault = source.targetId == "@default"; const EDataFlow flow = source.kind == SourceKind::Microphone ? eCapture : eRender;
            const auto defaultDevice = [&] { Com<IMMDevice> device; return SUCCEEDED(enumerator->GetDefaultAudioEndpoint(flow, eConsole, device.out())) ? deviceId(device.p) : QString(); };
            QString problem;
            while (!stop) {
                bool switched = false;
                try {
                    Com<IMMDevice> device;
                    if (followsDefault) check(enumerator->GetDefaultAudioEndpoint(flow, eConsole, device.out()), "Aucun périphérique audio par défaut");
                    else check(enumerator->GetDevice(reinterpret_cast<LPCWSTR>(source.targetId.utf16()), device.out()), "Périphérique audio introuvable");
                    const auto id = deviceId(device.p);
                    Com<IAudioClient> client; check(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(client.out())), "Ouverture du périphérique audio");
                    pump(client.p, false, epoch, [&] {
                        if (stop) return false;
                        // The default device is asked for only when Windows says something changed.
                        if (!followsDefault || WaitForSingleObject(devices.event, 0) != WAIT_OBJECT_0) return true;
                        switched = defaultDevice() != id; return !switched;
                    }, [&] {
                        { QMutexLocker lock(&mutex); if (!unavailable.isEmpty()) { unavailable.clear(); qInfo("Audio : « %s » est de nouveau disponible.", label().constData()); } }
                        problem.clear(); count();
                    });
                    if (switched) qInfo("Audio : « %s » suit le nouveau périphérique par défaut.", label().constData());
                } catch (const std::exception& error) {
                    if (stop) break;
                    const auto detail = QString::fromUtf8(error.what());
                    if (detail != problem) qWarning("Audio : « %s » indisponible, sa piste reste silencieuse jusqu'à son retour (%s).", label().constData(), error.what());
                    problem = detail; { QMutexLocker lock(&mutex); unavailable = detail; }
                }
                count();
                if (switched || stop) continue;
                // Missing or refused: back when a device comes or changes. One taken by another program
                // says nothing when it is freed, hence a new try every 10 s all the same.
                const HANDLE waits[]{stopped.handle, devices.event}; WaitForMultipleObjects(2, waits, FALSE, 10000);
            }
        } catch (const std::exception& error) { if (!stop) { QMutexLocker lock(&mutex); failure = source.name + ": " + QString::fromUtf8(error.what()); } }
        count();
    }
    // One process of an application source. When it ends, or its sound cannot be followed, the
    // worker ends too; the monitor decides whether to try that process again.
    void runApplication(DWORD pid, qint64 epoch, const std::atomic_bool& localStop, HANDLE process) {
        try {
            Apartment apartment;
            Com<IAudioClient> client; client.p = openApplication(pid);
            pump(client.p, true, epoch, [&] { return !stop && !localStop; }, [] {});
        } catch (const std::exception& error) {
            if (!stop && !localStop && WaitForSingleObject(process, 0) != WAIT_OBJECT_0)
                qWarning("Audio : le son du processus %lu n'a pas pu être suivi pour « %s » (%s). Nouvel essai dans 5 secondes.", pid, label().constData(), error.what());
        }
    }
    void monitor(qint64 epoch) {
        struct Worker {
            DWORD pid = 0; HANDLE process = nullptr; std::atomic_bool stop{false}, done{false}; std::thread thread;
            ~Worker() { stop = true; if (thread.joinable()) thread.join(); if (process) CloseHandle(process); }
        };
        bool ready = false;
        try {
            Apartment apartment; AutomaticSessions sessions;
            // Set by a worker that ends: the monitor looks at once whether to try its process again.
            Event ended(false);
            std::vector<std::unique_ptr<Worker>> workers;
            // A process whose sound could not be followed waits a little before the next try.
            QHash<DWORD, std::chrono::steady_clock::time_point> retry;
            while (!stop) {
                const auto now = std::chrono::steady_clock::now();
                QList<quint32> roots;
                if (source.kind == SourceKind::Application) { for (auto pid : applicationProcesses(source.targetId)) roots.append(pid); }
                else roots = sessions.roots(source.excludedApplications);
                if (roots.size() > 32) throw std::runtime_error("Plus de 32 applications audio actives : ajoutez des exclusions.");
                for (auto it = workers.begin(); it != workers.end();) {
                    const auto& worker = **it;
                    if (worker.done && WaitForSingleObject(worker.process, 0) != WAIT_OBJECT_0 && roots.contains(worker.pid)) retry.insert(worker.pid, now + std::chrono::seconds(5));
                    if (worker.done || !roots.contains(worker.pid) || WaitForSingleObject(worker.process, 0) == WAIT_OBJECT_0) it = workers.erase(it); else ++it;
                }
                for (auto it = retry.begin(); it != retry.end();) if (!roots.contains(it.key())) it = retry.erase(it); else ++it;
                for (auto pid : roots) {
                    if (stop) break;
                    if (std::any_of(workers.begin(), workers.end(), [pid](const auto& worker) { return worker->pid == pid; })) continue;
                    if (retry.contains(pid) && now < retry.value(pid)) continue;
                    auto worker = std::make_unique<Worker>(); worker->pid = pid;
                    worker->process = OpenProcess(SYNCHRONIZE, FALSE, pid); if (!worker->process) continue;
                    auto* state = worker.get(); const HANDLE wake = ended.handle;
                    worker->thread = std::thread([this, state, epoch, wake] { runApplication(state->pid, epoch, state->stop, state->process); state->done = true; SetEvent(wake); });
                    workers.push_back(std::move(worker));
                }
                if (!ready) { ready = true; ++initialized; }
                // Nothing is looked at again until something happens: an application starts playing
                // sound, a followed process ends, a worker stops, an output comes or goes, or a process
                // that failed may be tried again. Every 30 s all the same, should Windows have said nothing.
                std::vector<HANDLE> waits{stopped.handle, sessions.deviceEvent(), sessions.sessionEvent(), ended.handle};
                for (const auto& worker : workers) waits.push_back(worker->process);
                auto timeout = std::chrono::milliseconds(30000);
                for (auto it = retry.cbegin(); it != retry.cend(); ++it)
                    timeout = std::min(timeout, std::max(std::chrono::milliseconds(0), std::chrono::duration_cast<std::chrono::milliseconds>(it.value() - now) + std::chrono::milliseconds(10)));
                const DWORD woken = WaitForMultipleObjects(DWORD(waits.size()), waits.data(), FALSE, DWORD(timeout.count()));
                if (woken == WAIT_OBJECT_0 + 1) sessions.refreshDevices();
            }
        } catch (const std::exception& error) { if (!stop) { QMutexLocker lock(&mutex); failure = source.name + ": " + QString::fromUtf8(error.what()); } }
        if (!ready) ++initialized;
    }
    static QString deviceId(IMMDevice* device) {
        LPWSTR id = nullptr; if (FAILED(device->GetId(&id))) return {};
        const auto result = QString::fromWCharArray(id); CoTaskMemFree(id); return result;
    }
};
WindowsAudio::WindowsAudio(const AudioSource& source) : m_impl(std::make_unique<Impl>()) { m_impl->source = source; }
WindowsAudio::~WindowsAudio() { m_impl->stop = true; SetEvent(m_impl->stopped.handle); for (auto& thread : m_impl->threads) if (thread.joinable()) thread.join(); }
void WindowsAudio::start(qint64 epoch) {
    if (m_impl->source.kind == SourceKind::ApplicationMix || m_impl->source.kind == SourceKind::Application) m_impl->threads.emplace_back([this, epoch] { m_impl->monitor(epoch); });
    else m_impl->threads.emplace_back([this, epoch] { m_impl->runDevice(epoch); });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(7);
    while (m_impl->initialized < int(m_impl->threads.size()) && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    if (m_impl->initialized < int(m_impl->threads.size())) throw std::runtime_error("Délai d'initialisation audio dépassé.");
    const QString failure = error(); if (!failure.isEmpty()) throw std::runtime_error(failure.toStdString());
}
QString WindowsAudio::error() const { QMutexLocker lock(&m_impl->mutex); return m_impl->failure; }
QString WindowsAudio::unavailable() const { QMutexLocker lock(&m_impl->mutex); return m_impl->unavailable; }
const AudioSource& WindowsAudio::source() const { return m_impl->source; }
void WindowsAudio::mix(qint64 first, int count, float* left, float* right) {
    QMutexLocker lock(&m_impl->mutex);
    for (auto it = m_impl->queue.begin(); it != m_impl->queue.end();) {
        const qint64 end = it->start + qint64(it->samples.size() / 2);
        const qint64 beginOverlap = std::max(first, it->start), endOverlap = std::min(first + count, end);
        for (qint64 sample = beginOverlap; sample < endOverlap; ++sample) {
            const auto offset = size_t(sample - it->start) * 2;
            left[sample - first] += it->samples[offset]; right[sample - first] += it->samples[offset + 1];
        }
        if (end <= first + count) it = m_impl->queue.erase(it); else ++it;
    }
}
}
