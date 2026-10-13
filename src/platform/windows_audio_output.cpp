#include "media/audio_output.h"
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <QString>
#include <algorithm>
#include <cstring>
#include <stdexcept>
namespace clipsapp {
namespace {
void check(HRESULT code) {
    if (FAILED(code)) throw std::runtime_error(QStringLiteral("Lecture audio indisponible (Windows 0x%1)").arg(quint32(code), 8, 16, QLatin1Char('0')).toStdString());
}
template<class T> struct Com {
    T* p = nullptr;
    ~Com() { if (p) p->Release(); }
    T** out() { return &p; }
    T* operator->() const { return p; }
};
}
struct AudioOutput::Impl {
    bool apartment = false, started = false;
    Com<IMMDeviceEnumerator> enumerator;
    Com<IMMDevice> device;
    Com<IAudioClient> client;
    Com<IAudioRenderClient> render;
    UINT32 capacity = 0;
    qint64 submitted = 0;
    ~Impl() {
        if (client.p) client->Stop();
        if (render.p) { render.p->Release(); render.p = nullptr; }
        if (client.p) { client.p->Release(); client.p = nullptr; }
        if (device.p) { device.p->Release(); device.p = nullptr; }
        if (enumerator.p) { enumerator.p->Release(); enumerator.p = nullptr; }
        if (apartment) CoUninitialize();
    }
};
AudioOutput::AudioOutput() : m_impl(std::make_unique<Impl>()) {
    auto& audio = *m_impl;
    check(CoInitializeEx(nullptr, COINIT_MULTITHREADED)); audio.apartment = true;
    check(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(audio.enumerator.out())));
    check(audio.enumerator->GetDefaultAudioEndpoint(eRender, eConsole, audio.device.out()));
    check(audio.device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(audio.client.out())));
    WAVEFORMATEX format{}; format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT; format.nChannels = 2;
    format.nSamplesPerSec = 48000; format.wBitsPerSample = 32; format.nBlockAlign = 8; format.nAvgBytesPerSec = 384000;
    check(audio.client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY, 1000000, 0, &format, nullptr));
    check(audio.client->GetBufferSize(&audio.capacity));
    check(audio.client->GetService(__uuidof(IAudioRenderClient), reinterpret_cast<void**>(audio.render.out())));
}
AudioOutput::~AudioOutput() = default;
int AudioOutput::writableFrames() const {
    UINT32 padding = 0; check(m_impl->client->GetCurrentPadding(&padding));
    return std::max(0, int(std::min<UINT32>(m_impl->capacity, 2400)) - int(padding));
}
qint64 AudioOutput::playedFrames() const {
    UINT32 padding = 0; check(m_impl->client->GetCurrentPadding(&padding));
    return std::max<qint64>(0, m_impl->submitted - padding);
}
void AudioOutput::write(const float* samples, int frames) {
    BYTE* data = nullptr; check(m_impl->render->GetBuffer(UINT32(frames), &data));
    std::memcpy(data, samples, size_t(frames) * 2 * sizeof(float));
    check(m_impl->render->ReleaseBuffer(UINT32(frames), 0)); m_impl->submitted += frames;
    if (!m_impl->started) { check(m_impl->client->Start()); m_impl->started = true; }
}
}
