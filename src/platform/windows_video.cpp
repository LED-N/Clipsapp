#include "windows_video.h"
#include "windows_audio.h"
#include "windows_capture_abi.h"
#include "windows_monitors.h"
#include <d3d11.h>
#include <roapi.h>
#include <winstring.h>
#include <QImage>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <thread>
extern "C" {
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_d3d11va.h>
HRESULT WINAPI CreateDirect3D11DeviceFromDXGIDevice(IDXGIDevice*, IInspectable**);
}
namespace clipsapp {
namespace {
void check(HRESULT code, const char* operation) {
    if (FAILED(code)) throw std::runtime_error(QString("%1 (Windows 0x%2)").arg(operation).arg(quint32(code), 8, 16, QLatin1Char('0')).toStdString());
}
template<class T> struct Com {
    T* p = nullptr;
    ~Com() { reset(); }
    void reset() { if (p) p->Release(); p = nullptr; }
    T** out() { reset(); return &p; }
    T* operator->() const { return p; }
};
void close(IUnknown* object) { if (object) { Com<wgc::Closable> closable; if (SUCCEEDED(object->QueryInterface(wgc::closableId, reinterpret_cast<void**>(closable.out())))) closable->Close(); } }
void factory(const wchar_t* name, REFIID id, void** output) {
    HSTRING string; check(WindowsCreateString(name, UINT32(wcslen(name)), &string), "Nom de la capture");
    const HRESULT result = RoGetActivationFactory(string, id, output); WindowsDeleteString(string); check(result, "API Windows Graphics Capture");
}
bool disableBorder(wgc::Session* session, const std::atomic_bool& stop) {
    try {
        Com<wgc::Border> border;
        check(session->QueryInterface(wgc::borderId, reinterpret_cast<void**>(border.out())), "Réglage de la bordure Windows");
        Com<wgc::AccessFactory> access;
        factory(L"Windows.Graphics.Capture.GraphicsCaptureAccess", wgc::accessFactoryId, reinterpret_cast<void**>(access.out()));
        Com<wgc::AccessOperation> operation;
        check(access->Request(0 /* Borderless */, operation.out()), "Autorisation de capture sans bordure");
        Com<wgc::AsyncInfo> info;
        check(operation->QueryInterface(wgc::asyncInfoId, reinterpret_cast<void**>(info.out())), "Autorisation Windows");
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        INT32 state = 0;
        do {
            check(info->Status(&state), "État de l'autorisation Windows");
            if (state != 0) break;
            if (stop || std::chrono::steady_clock::now() >= deadline) { info->Cancel(); return false; }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        } while (true);
        if (state != 1 /* Completed */) return false;
        INT32 permission = 0;
        check(operation->Results(&permission), "Résultat de l'autorisation Windows");
        check(border->Set(false), "Désactivation de la bordure Windows");
        boolean required = true; check(border->Get(&required), "État de la bordure Windows");
        return permission == 4 /* AppCapabilityAccessStatus.Allowed */ && !required;
    } catch (const std::exception&) { return false; }
}
}
struct WindowsVideo::Impl {
    VideoSettings settings;
    QString warning;
    bool apartment = false;
    HWND window = nullptr;
    HMONITOR monitor = nullptr;
    Com<ID3D11Device> device;
    Com<ID3D11DeviceContext> context;
    Com<ID3D11VideoDevice> videoDevice;
    Com<ID3D11VideoContext> videoContext;
    Com<IInspectable> winrtDevice;
    Com<wgc::Item> item;
    Com<wgc::Pool> pool;
    Com<wgc::Session> session;
    Com<ID3D11VideoProcessorEnumerator> enumerator;
    Com<ID3D11VideoProcessor> processor;
    AVBufferRef* hardware = nullptr;
    AVBufferRef* frames = nullptr;
    AVFrame* last = av_frame_alloc();
    // Shown while the screen captured is gone, until it comes back.
    AVFrame* black = av_frame_alloc();
    int inputWidth = 0, inputHeight = 0;
    wgc::Size poolSize{};
    const std::atomic_bool* stopping = nullptr;
    bool lost = false; QString topology; std::chrono::steady_clock::time_point nextCheck;
    ~Impl() {
        closeSession(); winrtDevice.reset();
        av_frame_free(&last); av_frame_free(&black); av_buffer_unref(&frames); av_buffer_unref(&hardware);
        processor.reset(); enumerator.reset(); videoContext.reset(); videoDevice.reset(); context.reset(); device.reset();
        if (apartment) RoUninitialize();
    }
    void setup(const std::atomic_bool& stop) {
        stopping = &stop;
        check(RoInitialize(RO_INIT_MULTITHREADED), "Initialisation de la capture"); apartment = true;
        const QString resolved = windowsVideoOptions(settings.targetId);
        if (resolved.startsWith("hwnd=")) window = reinterpret_cast<HWND>(quintptr(resolved.mid(5).toULongLong()));
        else monitor = reinterpret_cast<HMONITOR>(quintptr(resolved.mid(9).toULongLong()));
        check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT,
                               nullptr, 0, D3D11_SDK_VERSION, device.out(), nullptr, context.out()), "Périphérique graphique D3D11");
        check(device->QueryInterface(__uuidof(ID3D11VideoDevice), reinterpret_cast<void**>(videoDevice.out())), "Redimensionnement GPU");
        check(context->QueryInterface(__uuidof(ID3D11VideoContext), reinterpret_cast<void**>(videoContext.out())), "Traitement vidéo GPU");
        Com<IDXGIDevice> dxgi; check(device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(dxgi.out())), "Périphérique DXGI");
        check(CreateDirect3D11DeviceFromDXGIDevice(dxgi.p, winrtDevice.out()), "Périphérique de capture");
        openSession(stop);
        if (monitor) { topology = screens(windowsMonitors()); nextCheck = std::chrono::steady_clock::now() + std::chrono::seconds(1); }
        hardware = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_D3D11VA); if (!hardware) throw std::bad_alloc();
        auto* hardwareContext = reinterpret_cast<AVD3D11VADeviceContext*>(reinterpret_cast<AVHWDeviceContext*>(hardware->data)->hwctx);
        device->AddRef(); hardwareContext->device = device.p;
        if (av_hwdevice_ctx_init(hardware) < 0) throw std::runtime_error("Contexte d'encodage D3D11 indisponible.");
        frames = av_hwframe_ctx_alloc(hardware); if (!frames) throw std::bad_alloc();
        auto* frameContext = reinterpret_cast<AVHWFramesContext*>(frames->data);
        frameContext->format = AV_PIX_FMT_D3D11; frameContext->sw_format = AV_PIX_FMT_NV12;
        frameContext->width = settings.width; frameContext->height = settings.height;
        // Dynamic textures are reused by the AVBufferPool; no large fixed pool.
        frameContext->initial_pool_size = 0;
        reinterpret_cast<AVD3D11VAFramesContext*>(frameContext->hwctx)->BindFlags = D3D11_BIND_RENDER_TARGET;
        if (av_hwframe_ctx_init(frames) < 0) throw std::runtime_error("Buffer d'encodage GPU indisponible.");
        check(session->Start(), "Démarrage de Windows Graphics Capture");
    }
    // The source in Windows Graphics Capture: its item, its pool and its session. A screen that comes back
    // is given new ones on the same device: its images keep going to the same encoder.
    void openSession(const std::atomic_bool& stop) {
        Com<wgc::ItemFactory> itemFactory; factory(L"Windows.Graphics.Capture.GraphicsCaptureItem", wgc::itemFactoryId, reinterpret_cast<void**>(itemFactory.out()));
        if (window) check(itemFactory->ForWindow(window, wgc::itemId, reinterpret_cast<void**>(item.out())), "Capture de la fenêtre");
        else check(itemFactory->ForMonitor(monitor, wgc::itemId, reinterpret_cast<void**>(item.out())), "Capture du moniteur");
        check(item->GetSize(&poolSize), "Dimensions de la source");
        Com<wgc::PoolFactory> poolFactory; factory(L"Windows.Graphics.Capture.Direct3D11CaptureFramePool", wgc::poolFactoryId, reinterpret_cast<void**>(poolFactory.out()));
        check(poolFactory->Create(winrtDevice.p, 87 /* B8G8R8A8_UNORM */, 2, poolSize, pool.out()), "Buffer de capture GPU");
        check(pool->CreateSession(item.p, session.out()), "Session de capture");
        Com<wgc::Cursor> cursor;
        check(session->QueryInterface(wgc::cursorId, reinterpret_cast<void**>(cursor.out())), "Réglage du curseur");
        check(cursor->Set(settings.captureCursor), "Capture du curseur");
        warning.clear(); if (!disableBorder(session.p, stop)) warning = QStringLiteral("Windows n'a pas autorisé la capture sans bordure. La bordure jaune peut rester visible.");
    }
    void closeSession() { close(session.p); close(pool.p); session.reset(); pool.reset(); item.reset(); }
    // How the screens are laid out now: a screen put back may keep its handle, but not its session.
    static QString screens(const QList<WindowsMonitor>& monitors) {
        QStringList parts; for (const auto& screen : monitors) parts << QStringLiteral("%1|%2|%3,%4,%5,%6").arg(screen.device, screen.path).arg(screen.area.x()).arg(screen.area.y()).arg(screen.area.width()).arg(screen.area.height());
        return parts.join(QLatin1Char(';'));
    }
    // Once a second, the screen captured is looked for again. Gone, the capture goes on with a black image;
    // back, under its handle or another, it starts again from where it is now.
    void follow() {
        const auto now = std::chrono::steady_clock::now(); if (now < nextCheck) return; nextCheck = now + std::chrono::seconds(1);
        const auto monitors = windowsMonitors(); const auto arrangement = screens(monitors); const auto found = findMonitor(settings.targetId, monitors);
        if (!found) { if (!lost) { lost = true; closeSession(); } topology = arrangement; return; }
        const auto handle = static_cast<HMONITOR>(found->handle);
        if (!lost && handle == monitor && arrangement == topology) return;
        topology = arrangement; closeSession(); monitor = handle;
        try { const std::atomic_bool never{false}; openSession(stopping ? *stopping : never); check(session->Start(), "Démarrage de Windows Graphics Capture"); lost = false; }
        catch (const std::exception&) { closeSession(); lost = true; }
    }
    // Black, at the size the clip is encoded: the two planes of one image cleared once, then shared.
    bool blank(AVFrame* output) {
        if (!black->buf[0]) {
            if (av_hwframe_get_buffer(frames, black, 0) < 0) return false;
            auto* texture = reinterpret_cast<ID3D11Texture2D*>(black->data[0]); const UINT slice = UINT(quintptr(black->data[1])); D3D11_TEXTURE2D_DESC description; texture->GetDesc(&description);
            const auto clear = [&](DXGI_FORMAT format, const float* value) {
                D3D11_RENDER_TARGET_VIEW_DESC view{}; view.Format = format;
                if (description.ArraySize > 1) { view.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY; view.Texture2DArray.FirstArraySlice = slice; view.Texture2DArray.ArraySize = 1; }
                else view.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
                Com<ID3D11RenderTargetView> target; if (FAILED(device->CreateRenderTargetView(texture, &view, target.out()))) return false;
                context->ClearRenderTargetView(target.p, value); return true;
            };
            const float luma[4]{16 / 255.f, 0, 0, 1}, chroma[4]{128 / 255.f, 128 / 255.f, 0, 1};
            if (!clear(DXGI_FORMAT_R8_UNORM, luma) || !clear(DXGI_FORMAT_R8G8_UNORM, chroma)) { av_frame_unref(black); return false; }
            context->Flush();
            black->sample_aspect_ratio = {1, 1}; black->color_primaries = AVCOL_PRI_BT709; black->color_trc = AVCOL_TRC_BT709; black->colorspace = AVCOL_SPC_BT709; black->color_range = AVCOL_RANGE_MPEG;
        }
        return av_frame_ref(output, black) >= 0;
    }
    void scale(ID3D11Texture2D* input, wgc::Size content, AVFrame* output) {
        D3D11_TEXTURE2D_DESC inputDesc; input->GetDesc(&inputDesc);
        if (int(inputDesc.Width) != inputWidth || int(inputDesc.Height) != inputHeight) {
            inputWidth = int(inputDesc.Width); inputHeight = int(inputDesc.Height);
            D3D11_VIDEO_PROCESSOR_CONTENT_DESC description{};
            description.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE; description.InputWidth = inputDesc.Width; description.InputHeight = inputDesc.Height;
            description.OutputWidth = UINT(settings.width); description.OutputHeight = UINT(settings.height);
            description.InputFrameRate = description.OutputFrameRate = {UINT(settings.fps), 1}; description.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;
            check(videoDevice->CreateVideoProcessorEnumerator(&description, enumerator.out()), "Dimensions du traitement GPU");
            check(videoDevice->CreateVideoProcessor(enumerator.p, 0, processor.out()), "Processeur vidéo GPU");
        }
        D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC inputViewDesc{}; inputViewDesc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
        Com<ID3D11VideoProcessorInputView> inputView;
        check(videoDevice->CreateVideoProcessorInputView(input, enumerator.p, &inputViewDesc, inputView.out()), "Lecture de la texture capturée");
        if (av_hwframe_get_buffer(frames, output, 0) < 0) throw std::runtime_error("Allocation de l'image GPU impossible.");
        auto* texture = reinterpret_cast<ID3D11Texture2D*>(output->data[0]); D3D11_TEXTURE2D_DESC outputDesc; texture->GetDesc(&outputDesc);
        D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC outputViewDesc{};
        if (outputDesc.ArraySize > 1) { outputViewDesc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2DARRAY; outputViewDesc.Texture2DArray.ArraySize = 1; outputViewDesc.Texture2DArray.FirstArraySlice = UINT(quintptr(output->data[1])); }
        else outputViewDesc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
        Com<ID3D11VideoProcessorOutputView> outputView;
        check(videoDevice->CreateVideoProcessorOutputView(texture, enumerator.p, &outputViewDesc, outputView.out()), "Écriture de la texture d'encodage");
        const int width = std::clamp(content.width, 1, inputWidth), height = std::clamp(content.height, 1, inputHeight);
        const double ratio = std::min(settings.width / double(width), settings.height / double(height));
        const int outWidth = std::max(2, int(width * ratio) & ~1), outHeight = std::max(2, int(height * ratio) & ~1);
        const int x = ((settings.width - outWidth) / 2) & ~1, y = ((settings.height - outHeight) / 2) & ~1;
        const RECT sourceRect{0, 0, width, height}, destinationRect{x, y, x + outWidth, y + outHeight}, targetRect{0, 0, settings.width, settings.height};
        videoContext->VideoProcessorSetStreamFrameFormat(processor.p, 0, D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);
        videoContext->VideoProcessorSetStreamSourceRect(processor.p, 0, TRUE, &sourceRect);
        videoContext->VideoProcessorSetStreamDestRect(processor.p, 0, TRUE, &destinationRect);
        videoContext->VideoProcessorSetOutputTargetRect(processor.p, TRUE, &targetRect);
        D3D11_VIDEO_COLOR background{}; background.RGBA.A = 1; videoContext->VideoProcessorSetOutputBackgroundColor(processor.p, FALSE, &background);
        D3D11_VIDEO_PROCESSOR_COLOR_SPACE inputSpace{}, outputSpace{};
        inputSpace.RGB_Range = 0; outputSpace.YCbCr_Matrix = 1; outputSpace.Nominal_Range = 1; // BT.709, studio range.
        videoContext->VideoProcessorSetStreamColorSpace(processor.p, 0, &inputSpace);
        videoContext->VideoProcessorSetOutputColorSpace(processor.p, &outputSpace);
        videoContext->VideoProcessorSetStreamAutoProcessingMode(processor.p, 0, FALSE);
        D3D11_VIDEO_PROCESSOR_STREAM stream{}; stream.Enable = TRUE; stream.pInputSurface = inputView.p;
        check(videoContext->VideoProcessorBlt(processor.p, outputView.p, 0, 1, &stream), "Redimensionnement et conversion GPU");
        context->Flush();
        output->sample_aspect_ratio = {1, 1}; output->color_primaries = AVCOL_PRI_BT709; output->color_trc = AVCOL_TRC_BT709; output->colorspace = AVCOL_SPC_BT709; output->color_range = AVCOL_RANGE_MPEG;
    }
};
WindowsVideo::WindowsVideo(const VideoSettings& settings, const std::atomic_bool& stop) : m_impl(std::make_unique<Impl>()) { m_impl->settings = settings; m_impl->setup(stop); }
WindowsVideo::~WindowsVideo() = default;
QString WindowsVideo::warning() const { return m_impl->warning; }
bool WindowsVideo::lost() const { return m_impl->lost; }
bool WindowsVideo::read(AVFrame* output) {
    auto& capture = *m_impl;
    if (capture.window && !IsWindow(capture.window)) throw std::runtime_error("La fenêtre capturée a été fermée. La capture est arrêtée et le buffer vidé.");
    // A screen comes and goes: while it is gone the capture shows black, and it starts again when it is back.
    if (capture.monitor) capture.follow();
    Com<wgc::Frame> frame;
    if (const HRESULT next = capture.lost ? S_OK : capture.pool->Next(frame.out()); FAILED(next)) {
        if (!capture.monitor) check(next, "Lecture de la capture");
        capture.lost = true; capture.closeSession(); capture.nextCheck = {};
    }
    if (capture.lost) { av_frame_unref(output); if (capture.blank(output)) return true; }
    else if (frame.p) {
        Com<IInspectable> surface; Com<wgc::DxgiAccess> access; Com<ID3D11Texture2D> texture;
        wgc::Size size{}; check(frame->ContentSize(&size), "Dimensions de l'image");
        check(frame->Surface(surface.out()), "Surface capturée");
        check(surface->QueryInterface(wgc::dxgiAccessId, reinterpret_cast<void**>(access.out())), "Accès à la texture");
        check(access->GetInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(texture.out())), "Texture de capture");
        if (size.width > 0 && size.height > 0) {
            capture.scale(texture.p, size, output); av_frame_unref(capture.last);
            if (av_frame_ref(capture.last, output) < 0) throw std::bad_alloc();
        }
        close(frame.p); frame.reset();
        if (size.width > 0 && size.height > 0 && (size.width != capture.poolSize.width || size.height != capture.poolSize.height)) {
            check(capture.pool->Recreate(capture.winrtDevice.p, 87, 2, size), "Redimensionnement de la fenêtre"); capture.poolSize = size;
        }
    }
    if (!output->buf[0] && capture.last->buf[0]) { if (av_frame_ref(output, capture.last) < 0) throw std::bad_alloc(); }
    return output->buf[0] != nullptr;
}
QImage captureScreenImage(const QString& targetId, bool cursor, QString& error) {
    const HRESULT initialized = RoInitialize(RO_INIT_MULTITHREADED); const bool apartment = SUCCEEDED(initialized);
    QImage image;
    try {
        if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) check(initialized, "Initialisation de la capture");
        const QString resolved = windowsVideoOptions(targetId);
        HWND window = nullptr; HMONITOR monitor = nullptr;
        if (resolved.startsWith("hwnd=")) window = reinterpret_cast<HWND>(quintptr(resolved.mid(5).toULongLong())); else monitor = reinterpret_cast<HMONITOR>(quintptr(resolved.mid(9).toULongLong()));
        Com<ID3D11Device> device; Com<ID3D11DeviceContext> context;
        check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, device.out(), nullptr, context.out()), "Périphérique graphique D3D11");
        Com<IDXGIDevice> dxgi; check(device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(dxgi.out())), "Périphérique DXGI");
        Com<IInspectable> winrtDevice; check(CreateDirect3D11DeviceFromDXGIDevice(dxgi.p, winrtDevice.out()), "Périphérique de capture");
        Com<wgc::ItemFactory> itemFactory; factory(L"Windows.Graphics.Capture.GraphicsCaptureItem", wgc::itemFactoryId, reinterpret_cast<void**>(itemFactory.out()));
        Com<wgc::Item> item;
        if (window) check(itemFactory->ForWindow(window, wgc::itemId, reinterpret_cast<void**>(item.out())), "Capture de la fenêtre");
        else check(itemFactory->ForMonitor(monitor, wgc::itemId, reinterpret_cast<void**>(item.out())), "Capture du moniteur");
        wgc::Size size{}; check(item->GetSize(&size), "Dimensions de la source");
        Com<wgc::PoolFactory> poolFactory; factory(L"Windows.Graphics.Capture.Direct3D11CaptureFramePool", wgc::poolFactoryId, reinterpret_cast<void**>(poolFactory.out()));
        Com<wgc::Pool> pool; check(poolFactory->Create(winrtDevice.p, 87 /* B8G8R8A8_UNORM */, 1, size, pool.out()), "Buffer de capture GPU");
        Com<wgc::Session> session; check(pool->CreateSession(item.p, session.out()), "Session de capture");
        Com<wgc::Cursor> pointer; check(session->QueryInterface(wgc::cursorId, reinterpret_cast<void**>(pointer.out())), "Réglage du curseur"); check(pointer->Set(cursor), "Capture du curseur");
        const std::atomic_bool never{false}; disableBorder(session.p, never);
        check(session->Start(), "Démarrage de Windows Graphics Capture");
        // The first image comes within a frame or two of the screen.
        Com<wgc::Frame> frame; const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (!frame.p && std::chrono::steady_clock::now() < deadline) { check(pool->Next(frame.out()), "Lecture de la capture"); if (!frame.p) std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
        if (!frame.p) throw std::runtime_error("Windows n'a fourni aucune image de l'écran.");
        Com<IInspectable> surface; Com<wgc::DxgiAccess> access; Com<ID3D11Texture2D> texture; wgc::Size content{};
        check(frame->ContentSize(&content), "Dimensions de l'image"); check(frame->Surface(surface.out()), "Surface capturée");
        check(surface->QueryInterface(wgc::dxgiAccessId, reinterpret_cast<void**>(access.out())), "Accès à la texture");
        check(access->GetInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(texture.out())), "Texture de capture");
        D3D11_TEXTURE2D_DESC description; texture->GetDesc(&description);
        const UINT width = UINT(std::clamp<int>(content.width, 1, int(description.Width))), height = UINT(std::clamp<int>(content.height, 1, int(description.Height)));
        // Read back by the processor: a copy the card hands over, then mapped row by row.
        D3D11_TEXTURE2D_DESC staging{}; staging.Width = width; staging.Height = height; staging.MipLevels = 1; staging.ArraySize = 1; staging.Format = description.Format;
        staging.SampleDesc.Count = 1; staging.Usage = D3D11_USAGE_STAGING; staging.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        Com<ID3D11Texture2D> readable; check(device->CreateTexture2D(&staging, nullptr, readable.out()), "Copie de l'image");
        const D3D11_BOX box{0, 0, 0, width, height, 1}; context->CopySubresourceRegion(readable.p, 0, 0, 0, 0, texture.p, 0, &box);
        D3D11_MAPPED_SUBRESOURCE mapped{}; check(context->Map(readable.p, 0, D3D11_MAP_READ, 0, &mapped), "Lecture de l'image");
        image = QImage(int(width), int(height), QImage::Format_RGB32);
        for (UINT row = 0; row < height; ++row) std::memcpy(image.scanLine(int(row)), static_cast<const uchar*>(mapped.pData) + size_t(row) * mapped.RowPitch, size_t(width) * 4);
        context->Unmap(readable.p, 0);
        close(frame.p); close(session.p); close(pool.p);
    } catch (const std::exception& failure) { error = QString::fromUtf8(failure.what()); image = {}; }
    if (apartment) RoUninitialize();
    return image;
}
}
