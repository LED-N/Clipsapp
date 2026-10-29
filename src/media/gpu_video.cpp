#include "gpu_video.h"
#include <QtGlobal>
#ifdef Q_OS_WIN
#include <windows.h>
#include <d3d11.h>
#include <d3d11_4.h>
#include <dxgi1_2.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_d3d11va.h>
}

namespace clipsapp {
namespace {
template<class T> struct Com {
    T* p = nullptr;
    Com() = default;
    Com(const Com&) = delete;
    Com& operator=(const Com&) = delete;
    ~Com() { reset(); }
    void reset() { if (p) p->Release(); p = nullptr; }
    T** out() { reset(); return &p; }
    T* operator->() const { return p; }
};
// One triangle that covers the window, and the picture of the interface laid over the image.
const char shaders[] =
    "struct Corner { float4 position : SV_Position; float2 at : TEXCOORD0; };\n"
    "Corner vertex(uint index : SV_VertexID) { Corner corner; corner.at = float2((index << 1) & 2, index & 2);\n"
    "    corner.position = float4(corner.at * float2(2, -2) + float2(-1, 1), 0, 1); return corner; }\n"
    "Texture2D picture : register(t0); SamplerState smooth : register(s0);\n"
    "float4 pixel(Corner corner) : SV_Target { return picture.Sample(smooth, corner.at); }\n";
using Compile = HRESULT (WINAPI*)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob**);
}

struct GpuVideo::Impl {
    // The decoder and every window draw through one context, which only one thread may use at a time.
    // The lock belongs to the decoding device: the images still held after the last window closed
    // keep it, and the device, until they go.
    std::recursive_mutex* lock = nullptr;
    Com<ID3D11Device> device; Com<ID3D11DeviceContext> context; Com<ID3D11VideoDevice> videoDevice; Com<ID3D11VideoContext> videoContext;
    Com<ID3D11VertexShader> vertex; Com<ID3D11PixelShader> pixel; Com<ID3D11SamplerState> sampler; Com<ID3D11BlendState> blend; Com<ID3D11RasterizerState> raster;
    AVBufferRef* hardware = nullptr;
    ~Impl() { av_buffer_unref(&hardware); }
    static void take(void* self) { static_cast<std::recursive_mutex*>(self)->lock(); }
    static void give(void* self) { static_cast<std::recursive_mutex*>(self)->unlock(); }
    bool create() {
        if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, device.out(), nullptr, context.out()))) return false;
        if (FAILED(device->QueryInterface(__uuidof(ID3D11VideoDevice), reinterpret_cast<void**>(videoDevice.out()))) || FAILED(context->QueryInterface(__uuidof(ID3D11VideoContext), reinterpret_cast<void**>(videoContext.out())))) return false;
        Com<ID3D11Multithread> threads; if (SUCCEEDED(context->QueryInterface(__uuidof(ID3D11Multithread), reinterpret_cast<void**>(threads.out())))) threads->SetMultithreadProtected(TRUE);
        // The compiler of Windows itself, loaded on demand: without it there is no overlay, hence no direct display.
        const HMODULE library = LoadLibraryW(L"d3dcompiler_47.dll"); const auto compile = library ? reinterpret_cast<Compile>(reinterpret_cast<void*>(GetProcAddress(library, "D3DCompile"))) : nullptr;
        if (!compile) return false;
        Com<ID3DBlob> vertexCode, pixelCode;
        if (FAILED(compile(shaders, sizeof(shaders) - 1, nullptr, nullptr, nullptr, "vertex", "vs_4_0", 0, 0, vertexCode.out(), nullptr)) || FAILED(compile(shaders, sizeof(shaders) - 1, nullptr, nullptr, nullptr, "pixel", "ps_4_0", 0, 0, pixelCode.out(), nullptr))) return false;
        if (FAILED(device->CreateVertexShader(vertexCode->GetBufferPointer(), vertexCode->GetBufferSize(), nullptr, vertex.out())) || FAILED(device->CreatePixelShader(pixelCode->GetBufferPointer(), pixelCode->GetBufferSize(), nullptr, pixel.out()))) return false;
        D3D11_SAMPLER_DESC sampling{}; sampling.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR; sampling.AddressU = sampling.AddressV = sampling.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP; sampling.MaxLOD = D3D11_FLOAT32_MAX;
        // What the interface draws is premultiplied: it is added over what it does not cover.
        D3D11_BLEND_DESC blending{}; auto& over = blending.RenderTarget[0]; over.BlendEnable = TRUE; over.SrcBlend = over.SrcBlendAlpha = D3D11_BLEND_ONE; over.DestBlend = over.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        over.BlendOp = over.BlendOpAlpha = D3D11_BLEND_OP_ADD; over.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        D3D11_RASTERIZER_DESC filling{}; filling.FillMode = D3D11_FILL_SOLID; filling.CullMode = D3D11_CULL_NONE; filling.DepthClipEnable = TRUE;
        if (FAILED(device->CreateSamplerState(&sampling, sampler.out())) || FAILED(device->CreateBlendState(&blending, blend.out())) || FAILED(device->CreateRasterizerState(&filling, raster.out()))) return false;
        hardware = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_D3D11VA); if (!hardware) return false;
        auto* context = reinterpret_cast<AVHWDeviceContext*>(hardware->data); lock = new std::recursive_mutex; context->user_opaque = lock;
        context->free = [](AVHWDeviceContext* self) { delete static_cast<std::recursive_mutex*>(self->user_opaque); };
        auto* shared = static_cast<AVD3D11VADeviceContext*>(context->hwctx);
        device->AddRef(); shared->device = device.p; shared->lock = take; shared->unlock = give; shared->lock_ctx = lock;
        return av_hwdevice_ctx_init(hardware) >= 0;
    }
};
GpuVideo::GpuVideo() : m_impl(std::make_unique<Impl>()) {}
GpuVideo::~GpuVideo() = default;
AVBufferRef* GpuVideo::decodingDevice() const { return m_impl->hardware; }
namespace {
std::mutex sharing; std::weak_ptr<GpuVideo> current; bool refused = false;
}
std::shared_ptr<GpuVideo> GpuVideo::acquire() {
    std::lock_guard guard(sharing);
    if (auto video = current.lock()) return video;
    if (!available()) return {};
    std::shared_ptr<GpuVideo> candidate(new GpuVideo);
    // A card or a driver that refuses once is not asked again.
    if (!candidate->m_impl->create()) { refused = true; return {}; }
    current = candidate; return candidate;
}
// A way back to the images drawn from memory, for a card or a driver that would misbehave.
bool GpuVideo::available() { return !refused && !qEnvironmentVariableIsSet("CLIPSAPP_NO_GPU_VIDEO"); }

struct GpuOutput::Impl {
    std::shared_ptr<GpuVideo> video; GpuVideo::Impl* gpu = nullptr; HWND window = nullptr;
    Com<IDXGISwapChain1> chain; Com<ID3D11Texture2D> back; Com<ID3D11RenderTargetView> target; Com<ID3D11VideoProcessorOutputView> outputView;
    Com<ID3D11VideoProcessorEnumerator> enumerator; Com<ID3D11VideoProcessor> processor; QSize processorInput, processorOutput;
    Com<ID3D11Texture2D> overlay; Com<ID3D11ShaderResourceView> overlayView; QSize overlaySize;
    Com<ID3D11Texture2D> veil; Com<ID3D11ShaderResourceView> veilView;
    GpuFrame frame; QRect source, place; QColor ground{Qt::black}; QSize size;
    std::atomic<quint64> presented{0}; bool failed = false;
    bool create() {
        Com<IDXGIDevice> device; Com<IDXGIAdapter> adapter; Com<IDXGIFactory2> factory;
        if (FAILED(gpu->device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(device.out()))) || FAILED(device->GetAdapter(adapter.out())) || FAILED(adapter->GetParent(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(factory.out())))) return false;
        // Two buffers that Windows flips to the screen: the model its compositor shows without copying.
        DXGI_SWAP_CHAIN_DESC1 description{}; description.Width = UINT(size.width()); description.Height = UINT(size.height()); description.Format = DXGI_FORMAT_B8G8R8A8_UNORM; description.SampleDesc.Count = 1;
        description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; description.BufferCount = 2; description.Scaling = DXGI_SCALING_STRETCH; description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; description.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        if (FAILED(factory->CreateSwapChainForHwnd(gpu->device.p, window, &description, nullptr, nullptr, chain.out()))) {
            description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
            if (FAILED(factory->CreateSwapChainForHwnd(gpu->device.p, window, &description, nullptr, nullptr, chain.out()))) return false;
        }
        factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER); return true;
    }
    bool buffers() {
        if (back.p && target.p) return true;
        return SUCCEEDED(chain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(back.out()))) && SUCCEEDED(gpu->device->CreateRenderTargetView(back.p, nullptr, target.out()));
    }
    // The card scales the part of the image that is shown into its place, and turns its colours into those of the screen.
    bool drawFrame() {
        if (!frame || frame->format != AV_PIX_FMT_D3D11 || !frame->data[0]) return false;
        // An image decoded on a device that has since gone cannot be shown on this one.
        if (!frame->hw_frames_ctx || reinterpret_cast<AVHWFramesContext*>(frame->hw_frames_ctx->data)->device_ref->data != gpu->hardware->data) return false;
        const QRect window(QPoint(), size), to = place.intersected(window), from = source.intersected(QRect(0, 0, frame->width, frame->height));
        if (to.isEmpty() || from.isEmpty()) return false;
        auto* texture = reinterpret_cast<ID3D11Texture2D*>(frame->data[0]); D3D11_TEXTURE2D_DESC description; texture->GetDesc(&description);
        const QSize input(int(description.Width), int(description.Height));
        if (!processor.p || input != processorInput || size != processorOutput) {
            outputView.reset(); processor.reset(); enumerator.reset();
            D3D11_VIDEO_PROCESSOR_CONTENT_DESC content{}; content.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE; content.InputWidth = description.Width; content.InputHeight = description.Height;
            content.OutputWidth = UINT(size.width()); content.OutputHeight = UINT(size.height()); content.InputFrameRate = content.OutputFrameRate = {60, 1}; content.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;
            if (FAILED(gpu->videoDevice->CreateVideoProcessorEnumerator(&content, enumerator.out())) || FAILED(gpu->videoDevice->CreateVideoProcessor(enumerator.p, 0, processor.out()))) { processor.reset(); return false; }
            processorInput = input; processorOutput = size;
        }
        if (!outputView.p) {
            D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC view{}; view.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
            if (FAILED(gpu->videoDevice->CreateVideoProcessorOutputView(back.p, enumerator.p, &view, outputView.out()))) return false;
        }
        // The decoder keeps its images as the slices of one texture: the second pointer of the frame says which.
        D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC slice{}; slice.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D; slice.Texture2D.ArraySlice = UINT(quintptr(frame->data[1]));
        Com<ID3D11VideoProcessorInputView> inputView; if (FAILED(gpu->videoDevice->CreateVideoProcessorInputView(texture, enumerator.p, &slice, inputView.out()))) return false;
        const LONG left = LONG(frame->crop_left), top = LONG(frame->crop_top);
        const RECT part{left + from.left(), top + from.top(), left + from.left() + from.width(), top + from.top() + from.height()};
        const RECT spot{to.left(), to.top(), to.left() + to.width(), to.top() + to.height()}, all{0, 0, size.width(), size.height()};
        auto* video = gpu->videoContext.p;
        video->VideoProcessorSetStreamFrameFormat(processor.p, 0, D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);
        video->VideoProcessorSetStreamSourceRect(processor.p, 0, TRUE, &part); video->VideoProcessorSetStreamDestRect(processor.p, 0, TRUE, &spot); video->VideoProcessorSetOutputTargetRect(processor.p, TRUE, &all);
        D3D11_VIDEO_COLOR background{}; background.RGBA.R = ground.redF(); background.RGBA.G = ground.greenF(); background.RGBA.B = ground.blueF(); background.RGBA.A = 1;
        video->VideoProcessorSetOutputBackgroundColor(processor.p, FALSE, &background);
        // As when the image was converted in memory: BT.709 when the clip says so, BT.601 otherwise, in the range it gives.
        D3D11_VIDEO_PROCESSOR_COLOR_SPACE recorded{}, shown{}; recorded.YCbCr_Matrix = frame->colorspace == AVCOL_SPC_BT709 ? 1 : 0; recorded.Nominal_Range = frame->color_range == AVCOL_RANGE_JPEG ? 2 : 1; shown.RGB_Range = 0;
        video->VideoProcessorSetStreamColorSpace(processor.p, 0, &recorded); video->VideoProcessorSetOutputColorSpace(processor.p, &shown); video->VideoProcessorSetStreamAutoProcessingMode(processor.p, 0, FALSE);
        D3D11_VIDEO_PROCESSOR_STREAM stream{}; stream.Enable = TRUE; stream.pInputSurface = inputView.p;
        return SUCCEEDED(video->VideoProcessorBlt(processor.p, outputView.p, 0, 1, &stream));
    }
    bool draw() {
        if (!chain.p || failed || size.isEmpty() || !buffers()) return false;
        auto* context = gpu->context.p;
        if (!drawFrame()) { const float colour[4]{float(ground.redF()), float(ground.greenF()), float(ground.blueF()), 1}; context->ClearRenderTargetView(target.p, colour); }
        if (overlayView.p || veilView.p) {
            const D3D11_VIEWPORT viewport{0, 0, float(size.width()), float(size.height()), 0, 1}; const float factor[4]{};
            context->OMSetRenderTargets(1, &target.p, nullptr); context->RSSetViewports(1, &viewport); context->RSSetState(gpu->raster.p); context->OMSetBlendState(gpu->blend.p, factor, 0xffffffff);
            context->IASetInputLayout(nullptr); context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            context->VSSetShader(gpu->vertex.p, nullptr, 0); context->PSSetShader(gpu->pixel.p, nullptr, 0); context->PSSetSamplers(0, 1, &gpu->sampler.p);
            for (auto* layer : {overlayView.p, veilView.p}) if (layer) { context->PSSetShaderResources(0, 1, &layer); context->Draw(3, 0); }
            // Nothing stays bound to the buffer: the next image is written to it by the video processor.
            ID3D11ShaderResourceView* nothing = nullptr; ID3D11RenderTargetView* nowhere = nullptr; context->PSSetShaderResources(0, 1, &nothing); context->OMSetRenderTargets(1, &nowhere, nullptr);
        }
        return true;
    }
    void render() {
        if (!draw()) return;
        if (FAILED(chain->Present(0, 0))) failed = true; else ++presented;
    }
    bool texture(Com<ID3D11Texture2D>& result, Com<ID3D11ShaderResourceView>& view, QSize extent, bool lasting, const void* pixels, int stride) {
        D3D11_TEXTURE2D_DESC description{}; description.Width = UINT(extent.width()); description.Height = UINT(extent.height()); description.MipLevels = description.ArraySize = 1; description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        description.SampleDesc.Count = 1; description.BindFlags = D3D11_BIND_SHADER_RESOURCE; description.Usage = lasting ? D3D11_USAGE_IMMUTABLE : D3D11_USAGE_DYNAMIC; description.CPUAccessFlags = lasting ? 0 : D3D11_CPU_ACCESS_WRITE;
        D3D11_SUBRESOURCE_DATA data{}; data.pSysMem = pixels; data.SysMemPitch = UINT(stride);
        view.reset();
        return SUCCEEDED(gpu->device->CreateTexture2D(&description, pixels ? &data : nullptr, result.out())) && SUCCEEDED(gpu->device->CreateShaderResourceView(result.p, nullptr, view.out()));
    }
};
GpuOutput::GpuOutput(std::shared_ptr<GpuVideo> video, void* window) : m_impl(std::make_unique<Impl>()) { m_impl->gpu = video->impl(); m_impl->video = std::move(video); m_impl->window = static_cast<HWND>(window); }
// The device outlives the lock taken here: the last window may be the last to hold it.
GpuOutput::~GpuOutput() { const auto video = m_impl->video; std::lock_guard guard(*m_impl->gpu->lock); m_impl->frame.reset(); m_impl.reset(); }
bool GpuOutput::valid() const { std::lock_guard guard(*m_impl->gpu->lock); return m_impl->chain.p && !m_impl->failed; }
quint64 GpuOutput::presented() const { return m_impl->presented; }
void GpuOutput::resize(QSize size) {
    auto& output = *m_impl; std::lock_guard guard(*output.gpu->lock);
    size = size.expandedTo({1, 1}); if (size == output.size && output.chain.p) return;
    output.size = size; output.back.reset(); output.target.reset(); output.outputView.reset();
    if (!output.chain.p) { if (!output.create()) { output.failed = true; return; } }
    else if (FAILED(output.chain->ResizeBuffers(0, UINT(size.width()), UINT(size.height()), DXGI_FORMAT_UNKNOWN, 0))) { output.failed = true; return; }
    output.render();
}
void GpuOutput::setView(const QRect& source, const QRect& place, const QColor& ground) {
    auto& output = *m_impl; std::lock_guard guard(*output.gpu->lock);
    if (source == output.source && place == output.place && ground == output.ground) return;
    output.source = source; output.place = place; output.ground = ground; output.render();
}
void GpuOutput::setOverlay(const QImage& overlay) {
    auto& output = *m_impl; std::lock_guard guard(*output.gpu->lock);
    if (overlay.isNull()) { if (!output.overlayView.p) return; output.overlayView.reset(); output.overlay.reset(); output.overlaySize = {}; output.render(); return; }
    const QImage pixels = overlay.format() == QImage::Format_ARGB32_Premultiplied ? overlay : overlay.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (pixels.size() != output.overlaySize || !output.overlay.p) {
        if (!output.texture(output.overlay, output.overlayView, pixels.size(), false, nullptr, 0)) { output.overlayView.reset(); output.overlay.reset(); output.overlaySize = {}; return; }
        output.overlaySize = pixels.size();
    }
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (SUCCEEDED(output.gpu->context->Map(output.overlay.p, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        for (int row = 0; row < pixels.height(); ++row) std::memcpy(static_cast<uchar*>(mapped.pData) + size_t(row) * mapped.RowPitch, pixels.constScanLine(row), size_t(pixels.width()) * 4);
        output.gpu->context->Unmap(output.overlay.p, 0);
    }
    output.render();
}
void GpuOutput::setDim(const QColor& veil) {
    auto& output = *m_impl; std::lock_guard guard(*output.gpu->lock);
    if (veil.alpha() == 0) { if (!output.veilView.p) return; output.veilView.reset(); output.veil.reset(); output.render(); return; }
    const QRgb pixel = qPremultiply(veil.rgba());
    if (output.texture(output.veil, output.veilView, {1, 1}, true, &pixel, 4)) output.render();
}
void GpuOutput::show(const GpuFrame& frame) { auto& output = *m_impl; std::lock_guard guard(*output.gpu->lock); output.frame = frame; output.render(); }
void GpuOutput::refresh() { auto& output = *m_impl; std::lock_guard guard(*output.gpu->lock); output.render(); }
QImage GpuOutput::grab() {
    auto& output = *m_impl; std::lock_guard guard(*output.gpu->lock);
    if (!output.draw()) return {};
    D3D11_TEXTURE2D_DESC description; output.back->GetDesc(&description); description.Usage = D3D11_USAGE_STAGING; description.BindFlags = 0; description.CPUAccessFlags = D3D11_CPU_ACCESS_READ; description.MiscFlags = 0;
    Com<ID3D11Texture2D> copy; QImage result;
    if (SUCCEEDED(output.gpu->device->CreateTexture2D(&description, nullptr, copy.out()))) {
        output.gpu->context->CopyResource(copy.p, output.back.p); D3D11_MAPPED_SUBRESOURCE mapped{};
        if (SUCCEEDED(output.gpu->context->Map(copy.p, 0, D3D11_MAP_READ, 0, &mapped))) {
            result = QImage(int(description.Width), int(description.Height), QImage::Format_RGB32);
            for (int row = 0; row < result.height(); ++row) std::memcpy(result.scanLine(row), static_cast<const uchar*>(mapped.pData) + size_t(row) * mapped.RowPitch, size_t(result.width()) * 4);
            output.gpu->context->Unmap(copy.p, 0);
        }
    }
    if (FAILED(output.chain->Present(0, 0))) output.failed = true; else ++output.presented;
    return result;
}
}
#else
namespace clipsapp {
// Elsewhere than on Windows, images are still drawn from memory.
struct GpuVideo::Impl {};
struct GpuOutput::Impl {};
GpuVideo::GpuVideo() = default;
GpuVideo::~GpuVideo() = default;
std::shared_ptr<GpuVideo> GpuVideo::acquire() { return {}; }
bool GpuVideo::available() { return false; }
AVBufferRef* GpuVideo::decodingDevice() const { return nullptr; }
GpuOutput::GpuOutput(std::shared_ptr<GpuVideo>, void*) {}
GpuOutput::~GpuOutput() = default;
bool GpuOutput::valid() const { return false; }
void GpuOutput::resize(QSize) {}
void GpuOutput::setView(const QRect&, const QRect&, const QColor&) {}
void GpuOutput::setOverlay(const QImage&) {}
void GpuOutput::setDim(const QColor&) {}
void GpuOutput::show(const GpuFrame&) {}
void GpuOutput::refresh() {}
quint64 GpuOutput::presented() const { return 0; }
QImage GpuOutput::grab() { return {}; }
}
#endif
