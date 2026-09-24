#include "addon/scaler11.h"
#include "addon/bridge.h"
#include "engine/sr_engine.h"
#include <cstdarg>
#include <cstdio>

namespace nr {

namespace {

template <class T> void SafeRelease(T*& p) { if (p) { p->Release(); p = nullptr; } }

bool Texture2D(ID3D11Resource* r, D3D11_TEXTURE2D_DESC& desc) {
    if (!r) return false;
    D3D11_RESOURCE_DIMENSION dim = D3D11_RESOURCE_DIMENSION_UNKNOWN;
    r->GetType(&dim);
    if (dim != D3D11_RESOURCE_DIMENSION_TEXTURE2D) return false;
    static_cast<ID3D11Texture2D*>(r)->GetDesc(&desc);
    return true;
}

// The pass's own bindings are taken off while the copies run (its output is bound as a UAV, which a copy must not write under) and put back
// after, so the passes that follow find what they left.
struct SavedBindings {
    static const UINT kSrvs = 8, kUavs = 4;
    ID3D11DeviceContext* ctx;
    ID3D11ShaderResourceView* srvs[kSrvs] = {}; ID3D11UnorderedAccessView* uavs[kUavs] = {};
    explicit SavedBindings(ID3D11DeviceContext* c) : ctx(c) {
        ctx->CSGetShaderResources(0, kSrvs, srvs); ctx->CSGetUnorderedAccessViews(0, kUavs, uavs);
        ID3D11ShaderResourceView* noSrvs[kSrvs] = {}; ID3D11UnorderedAccessView* noUavs[kUavs] = {};
        ctx->CSSetShaderResources(0, kSrvs, noSrvs); ctx->CSSetUnorderedAccessViews(0, kUavs, noUavs, nullptr);
    }
    ~SavedBindings() {
        ctx->CSSetShaderResources(0, kSrvs, srvs); ctx->CSSetUnorderedAccessViews(0, kUavs, uavs, nullptr);
        for (auto*& v : srvs) SafeRelease(v);
        for (auto*& v : uavs) SafeRelease(v);
    }
};

} // namespace

// ---- recognising the NIS pass

bool FindNisPass(ID3D11DeviceContext* ctx, uint32_t x, uint32_t y, uint32_t z, NisPass& pass) {
    pass = {};
    if (z != 1) return false;
    ID3D11ShaderResourceView* srvs[3] = {}; ID3D11UnorderedAccessView* uav = nullptr;
    ctx->CSGetShaderResources(0, 3, srvs); ctx->CSGetUnorderedAccessViews(0, 1, &uav);
    ID3D11Resource* res[3] = {}; ID3D11Resource* out = nullptr;
    for (int i = 0; i < 3; ++i) if (srvs[i]) srvs[i]->GetResource(&res[i]);
    if (uav) uav->GetResource(&out);
    D3D11_TEXTURE2D_DESC in{}, c1{}, c2{}, o{};
    auto coefficients = [](const D3D11_TEXTURE2D_DESC& d) { return d.Width == 2 && d.Height == 64 && d.Format == DXGI_FORMAT_R32G32B32A32_FLOAT; };
    const bool ok = Texture2D(res[0], in) && Texture2D(res[1], c1) && Texture2D(res[2], c2) && Texture2D(out, o) &&
                    coefficients(c1) && coefficients(c2) && (o.Width > in.Width || o.Height > in.Height) &&
                    x == (o.Width + 31) / 32 && y == (o.Height + 23) / 24;
    for (auto*& v : srvs) SafeRelease(v);
    SafeRelease(uav); SafeRelease(res[1]); SafeRelease(res[2]);
    if (!ok) { SafeRelease(res[0]); SafeRelease(out); return false; }
    pass.in = res[0]; pass.out = out; pass.inW = in.Width; pass.inH = in.Height; pass.outW = o.Width; pass.outH = o.Height;
    pass.inFmt = in.Format; pass.outFmt = o.Format;
    return true;
}

void ReleaseNisPass(NisPass& pass) { SafeRelease(pass.in); SafeRelease(pass.out); pass = {}; }

// ---- the link to the engine

void ScalerLink::Shared::Release() { SafeRelease(d3d11); SafeRelease(d3d12); w = h = 0; fmt = DXGI_FORMAT_UNKNOWN; }
void ScalerLink::Fence::Release() { SafeRelease(d3d11); SafeRelease(d3d12); }

void ScalerLink::Log(const char* fmt, ...) {
    if (!m_log) return;
    char text[512]; va_list a; va_start(a, fmt); vsnprintf(text, sizeof text, fmt, a); va_end(a);
    m_log(text);
}

bool ScalerLink::MakeFence(Fence& f, const char* name) {
    if (FAILED(m_dev->CreateFence(0, D3D11_FENCE_FLAG_SHARED, IID_PPV_ARGS(&f.d3d11)))) { Log("DLSS upscaler: the %s fence could not be made", name); return false; }
    HANDLE handle = nullptr;
    if (FAILED(f.d3d11->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr, &handle))) { Log("DLSS upscaler: the %s fence could not be shared", name); f.Release(); return false; }
    f.d3d12 = m_engine->OpenSharedFence(handle);
    CloseHandle(handle);
    if (!f.d3d12) { f.Release(); return false; }
    return true;
}

// A texture on Lossless Scaling's device, opened on the engine's. The engine only reads the frame and flow copies; it writes the picture.
bool ScalerLink::Fit(Shared& t, uint32_t w, uint32_t h, DXGI_FORMAT fmt, bool engineWrites, const char* name) {
    if (t.d3d11 && t.w == w && t.h == h && t.fmt == fmt) return true;
    if (t.d3d11) { m_engine->Drain(); t.Release(); }   // the engine may still read or write the old one
    D3D11_TEXTURE2D_DESC d{}; d.Width = w; d.Height = h; d.MipLevels = 1; d.ArraySize = 1; d.Format = fmt; d.SampleDesc.Count = 1; d.Usage = D3D11_USAGE_DEFAULT;
    d.BindFlags = D3D11_BIND_SHADER_RESOURCE | (engineWrites ? D3D11_BIND_UNORDERED_ACCESS : 0u);
    d.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
    HRESULT hr = m_dev->CreateTexture2D(&d, nullptr, &t.d3d11);
    if (SUCCEEDED(hr)) {
        IDXGIResource1* dxgi = nullptr; HANDLE handle = nullptr;
        hr = t.d3d11->QueryInterface(IID_PPV_ARGS(&dxgi));
        if (SUCCEEDED(hr)) { hr = dxgi->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr, &handle); dxgi->Release(); }
        if (SUCCEEDED(hr)) { t.d3d12 = m_engine->OpenSharedTexture(handle); CloseHandle(handle); if (!t.d3d12) hr = E_FAIL; }
    }
    if (FAILED(hr)) { Log("DLSS upscaler: the shared %s (%ux%u, format %d) could not be made: 0x%08x", name, w, h, (int)fmt, (unsigned)hr); t.Release(); return false; }
    t.w = w; t.h = h; t.fmt = fmt;
    Log("DLSS upscaler: shared %s %ux%u, format %d", name, w, h, (int)fmt);
    return true;
}

bool ScalerLink::Init(ID3D11Device* dev, ID3D11DeviceContext* ctx, SrEngine* engine, LogFn log) {
    m_log = std::move(log); m_engine = engine; m_ctx = ctx; m_frame = 0;
    if (FAILED(dev->QueryInterface(IID_PPV_ARGS(&m_dev))) || FAILED(ctx->QueryInterface(IID_PPV_ARGS(&m_ctx4)))) {
        Log("DLSS upscaler: Lossless Scaling's device has no shared fences (D3D11.4 is needed)"); Shutdown(); return false;
    }
    if (!MakeFence(m_copied, "copied") || !MakeFence(m_done, "done")) { Shutdown(); return false; }
    return true;
}

void ScalerLink::Shutdown() {
    if (m_engine && (m_in.d3d12 || m_copied.d3d12)) m_engine->Drain();
    m_in.Release(); m_out.Release(); m_flow.Release();
    m_copied.Release(); m_done.Release();
    SafeRelease(m_ctx4); SafeRelease(m_dev); m_ctx = nullptr;
}

void ScalerLink::ReportDeviceChange() {
    if (!m_dev) return;
    const HRESULT reason = m_dev->GetDeviceRemovedReason();
    const char* name = reason == S_OK ? "not removed: Lossless Scaling replaced it itself"
                     : reason == DXGI_ERROR_DEVICE_HUNG ? "HUNG (its GPU work stopped making progress)"
                     : reason == DXGI_ERROR_DEVICE_RESET ? "RESET" : reason == DXGI_ERROR_DEVICE_REMOVED ? "REMOVED" : "other";
    const uint64_t copied = m_copied.d3d11 ? m_copied.d3d11->GetCompletedValue() : 0, done = m_done.d3d11 ? m_done.d3d11->GetCompletedValue() : 0;
    Log("DLSS upscaler: Lossless Scaling's device changed; the old one: 0x%08x %s. Frames handed over %llu, 'copied' reached %llu, 'done' reached %llu",
        (unsigned)reason, name, (unsigned long long)m_frame, (unsigned long long)copied, (unsigned long long)done);
}

bool ScalerLink::Upscale(const NisPass& pass, ID3D11Resource* flow, uint32_t flowW, uint32_t flowH, float flowUnit, float motionFraction, unsigned preset, float sharpen, bool reset) {
    if (!IsReady() || !m_engine || !m_engine->IsReady()) return false;
    // the shared copies: the frame in its own (view) format, the picture in the output's, which DLSS writes through a UAV
    const DXGI_FORMAT inFmt = Bridge::ViewFormat(pass.inFmt), outFmt = Bridge::ViewFormat(pass.outFmt);
    if (!Bridge::FormatSupported(pass.inFmt) || (outFmt != DXGI_FORMAT_R8G8B8A8_UNORM && outFmt != DXGI_FORMAT_R10G10B10A2_UNORM && outFmt != DXGI_FORMAT_R16G16B16A16_FLOAT)) {
        if (!m_loggedFormat) { Log("DLSS upscaler: frame format %d -> %d is not one DLSS can take here; NIS stays", (int)pass.inFmt, (int)pass.outFmt); m_loggedFormat = true; }
        return false;
    }
    if (!Fit(m_in, pass.inW, pass.inH, inFmt, false, "frame") || !Fit(m_out, pass.outW, pass.outH, outFmt, true, "picture")) return false;
    ID3D11Texture2D* flowTex = nullptr;
    if (flow && flowW && flowH && Fit(m_flow, flowW, flowH, DXGI_FORMAT_R16G16B16A16_FLOAT, false, "flow")) flowTex = m_flow.d3d11;

    const SavedBindings saved(m_ctx);
    m_ctx->CopyResource(m_in.d3d11, pass.in);
    if (flowTex) m_ctx->CopyResource(flowTex, flow);
    const uint64_t n = ++m_frame;
    m_ctx4->Signal(m_copied.d3d11, n);
    m_ctx->Flush();   // the engine's queue waits for this signal: hand it to the GPU now
    if (!m_engine->Run(m_in.d3d12, pass.inW, pass.inH, inFmt, m_out.d3d12, pass.outW, pass.outH, outFmt, flowTex ? m_flow.d3d12 : nullptr, m_flow.w, m_flow.h,
                       flowUnit, motionFraction, preset, sharpen, reset, m_copied.d3d12, n, m_done.d3d12, n))
        return false;   // nothing to wait for: NIS runs
    m_ctx4->Wait(m_done.d3d11, n);   // a GPU wait: Lossless Scaling's queue holds until DLSS has written the picture
    m_ctx->CopyResource(pass.out, m_out.d3d11);
    return true;
}

} // namespace nr
