#include "windows_audio.h"
#include "capture/audio_stream.h"
#include "core/audio_routing.h"
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
struct EndpointSessions {
    Com<IAudioSessionManager2> manager; SessionNotification* notification = nullptr;
    ~EndpointSessions() { if (notification) manager->UnregisterSessionNotification(notification); }
};
class AutomaticSessions {
public:
    AutomaticSessions() {
        check(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(enumerator.out())), "Suivi des applications audio");
        notification.p = new SessionNotification; refreshDevices();
    }
    void refreshDevices() {
        Com<IMMDeviceCollection> devices;
        check(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, devices.out()), "Sorties audio des applications");
        UINT count = 0; check(devices->GetCount(&count), "Nombre de sorties audio"); QSet<QString> current;
        for (UINT index = 0; index < count; ++index) {
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
    void wait() { WaitForSingleObject(notification->event, 1000); }
private:
    Com<IMMDeviceEnumerator> enumerator; Com<SessionNotification> notification;
    std::map<QString, std::unique_ptr<EndpointSessions>> endpoints;
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
    QSet<DWORD> candidates; const auto filename = QFileInfo(QString(executable).replace('\\', '/')).fileName();
    if (Process32FirstW(snapshot, &entry)) do {
        parents.insert(entry.th32ProcessID, entry.th32ParentProcessID);
        if (QString::fromWCharArray(entry.szExeFile).compare(filename, Qt::CaseInsensitive) == 0) candidates.insert(entry.th32ProcessID);
    } while (Process32NextW(snapshot, &entry));
    CloseHandle(snapshot);
    QList<DWORD> matching;
    for (auto it = parents.cbegin(); it != parents.cend(); ++it)
        if (candidates.contains(it.key()) && it.key() != GetCurrentProcessId() && processPath(it.key()).compare(QDir::toNativeSeparators(executable), Qt::CaseInsensitive) == 0) matching.append(it.key());
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
        struct Search { QString name; HMONITOR handle = nullptr; } search{targetId.mid(7), nullptr};
        EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR monitor, HDC, LPRECT, LPARAM data) -> BOOL {
            auto& query = *reinterpret_cast<Search*>(data); MONITORINFOEXW info{}; info.cbSize = sizeof(info);
            if (GetMonitorInfoW(monitor, &info) && QString::fromWCharArray(info.szDevice) == query.name) { query.handle = monitor; return FALSE; }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&search));
        if (search.handle) return QString("hmonitor=%1").arg(quintptr(search.handle));
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
    std::vector<std::thread> threads;
    QMutex mutex;
    std::deque<AudioChunk> queue;
    QString failure;
    std::atomic<int> initialized{0};
    void run(DWORD pid, qint64 epoch, const std::atomic_bool* localStop = nullptr, HANDLE process = nullptr) {
        bool ready = false;
        try {
            Apartment apartment;
            Com<IAudioClient> client;
            const bool application = source.kind == SourceKind::Application || source.kind == SourceKind::ApplicationMix;
            if (application) client.p = openApplication(pid);
            else {
                Com<IMMDeviceEnumerator> enumerator; Com<IMMDevice> device;
                check(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(enumerator.out())), "Liste des périphériques audio");
                if (source.targetId == "@default") check(enumerator->GetDefaultAudioEndpoint(source.kind == SourceKind::Microphone ? eCapture : eRender, eConsole, device.out()), "Périphérique audio par défaut");
                else check(enumerator->GetDevice(reinterpret_cast<LPCWSTR>(source.targetId.utf16()), device.out()), "Périphérique audio choisi");
                check(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(client.out())), "Ouverture du périphérique audio");
            }
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
            check(client->Start(), "Démarrage audio"); ready = true; if (!localStop) ++initialized;
            UINT64 nextPosition = 0; bool hasPosition = false;
            std::vector<uint8_t> copied;
            while (!stop && !(localStop && localStop->load())) {
                UINT32 available = 0; check(capture->GetNextPacketSize(&available), "Lecture audio");
                while (available && !stop && !(localStop && localStop->load())) {
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
        } catch (const std::exception& error) {
            if (!stop && !(localStop && localStop->load()) && !(process && WaitForSingleObject(process, 0) == WAIT_OBJECT_0)) {
                QMutexLocker lock(&mutex); failure = source.name + ": " + QString::fromUtf8(error.what());
            }
        }
        if (!ready && !localStop) ++initialized;
    }
    void monitor(qint64 epoch) {
        struct Worker {
            DWORD pid = 0; HANDLE process = nullptr; std::atomic_bool stop{false}; std::thread thread;
            ~Worker() { stop = true; if (thread.joinable()) thread.join(); if (process) CloseHandle(process); }
        };
        bool ready = false;
        try {
            Apartment apartment; AutomaticSessions sessions;
            std::vector<std::unique_ptr<Worker>> workers;
            auto nextDevices = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (!stop) {
                if (std::chrono::steady_clock::now() >= nextDevices) { sessions.refreshDevices(); nextDevices = std::chrono::steady_clock::now() + std::chrono::seconds(5); }
                QList<quint32> roots;
                if (source.kind == SourceKind::Application) { for (auto pid : applicationProcesses(source.targetId)) roots.append(pid); }
                else roots = sessions.roots(source.excludedApplications);
                if (roots.size() > 32) throw std::runtime_error("Plus de 32 applications audio actives : ajoutez des exclusions.");
                for (auto it = workers.begin(); it != workers.end();) {
                    if (!roots.contains((*it)->pid) || WaitForSingleObject((*it)->process, 0) == WAIT_OBJECT_0) it = workers.erase(it); else ++it;
                }
                for (auto pid : roots) {
                    if (stop) break;
                    if (std::any_of(workers.begin(), workers.end(), [pid](const auto& worker) { return worker->pid == pid; })) continue;
                    auto worker = std::make_unique<Worker>(); worker->pid = pid;
                    worker->process = OpenProcess(SYNCHRONIZE, FALSE, pid); if (!worker->process) continue;
                    auto* state = worker.get(); worker->thread = std::thread([this, state, epoch] { run(state->pid, epoch, &state->stop, state->process); });
                    workers.push_back(std::move(worker));
                }
                if (!ready) { ready = true; ++initialized; }
                sessions.wait();
            }
        } catch (const std::exception& error) { if (!stop) { QMutexLocker lock(&mutex); failure = source.name + ": " + QString::fromUtf8(error.what()); } }
        if (!ready) ++initialized;
    }
};
WindowsAudio::WindowsAudio(const AudioSource& source) : m_impl(std::make_unique<Impl>()) { m_impl->source = source; }
WindowsAudio::~WindowsAudio() { m_impl->stop = true; for (auto& thread : m_impl->threads) if (thread.joinable()) thread.join(); }
void WindowsAudio::start(qint64 epoch) {
    if (m_impl->source.kind == SourceKind::ApplicationMix || m_impl->source.kind == SourceKind::Application) {
        m_impl->threads.emplace_back([this, epoch] { m_impl->monitor(epoch); });
    } else {
    std::vector<unsigned long> pids{0};
    for (const auto pid : pids) m_impl->threads.emplace_back([this, pid, epoch] { m_impl->run(pid, epoch); });
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(7);
    while (m_impl->initialized < int(m_impl->threads.size()) && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    if (m_impl->initialized < int(m_impl->threads.size())) throw std::runtime_error("Délai d'initialisation audio dépassé.");
    const QString failure = error(); if (!failure.isEmpty()) throw std::runtime_error(failure.toStdString());
}
QString WindowsAudio::error() const { QMutexLocker lock(&m_impl->mutex); return m_impl->failure; }
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
