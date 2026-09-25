#include "addon/scaler11.h"
#include "addon/bridge.h"
#include "addon/product.h"
#include "engine/sr_engine.h"
#include <d3dcompiler.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>

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

// The pass's own bindings (and its shader: the grab pass sets ours) are taken off while our work runs (its output is bound as a UAV, which a
// copy must not write under) and put back after, so the NIS dispatch and the passes that follow find what they left.
struct SavedBindings {
    static const UINT kSrvs = 8, kUavs = 4;
    ID3D11DeviceContext* ctx;
    ID3D11ShaderResourceView* srvs[kSrvs] = {}; ID3D11UnorderedAccessView* uavs[kUavs] = {};
    ID3D11ComputeShader* shader = nullptr;
    explicit SavedBindings(ID3D11DeviceContext* c) : ctx(c) {
        ctx->CSGetShaderResources(0, kSrvs, srvs); ctx->CSGetUnorderedAccessViews(0, kUavs, uavs); ctx->CSGetShader(&shader, nullptr, nullptr);
        ID3D11ShaderResourceView* noSrvs[kSrvs] = {}; ID3D11UnorderedAccessView* noUavs[kUavs] = {};
        ctx->CSSetShaderResources(0, kSrvs, noSrvs); ctx->CSSetUnorderedAccessViews(0, kUavs, noUavs, nullptr);
    }
    ~SavedBindings() {
        ctx->CSSetShader(shader, nullptr, 0);
        ctx->CSSetShaderResources(0, kSrvs, srvs); ctx->CSSetUnorderedAccessViews(0, kUavs, uavs, nullptr);
        for (auto*& v : srvs) SafeRelease(v);
        for (auto*& v : uavs) SafeRelease(v);
        SafeRelease(shader);
    }
};

// The grab pass: the frame, read the way NIS reads it (through the pass's t0 view), written into the shared frame texture.
const char* const kGrabHlsl = R"HLSL(
Texture2D<float4>   tFrame : register(t0);
RWTexture2D<float4> uOut   : register(u0);
[numthreads(8, 8, 1)]
void CSGrab(uint3 id : SV_DispatchThreadID) {
    uint w, h; uOut.GetDimensions(w, h);
    if (id.x < w && id.y < h) uOut[id.xy] = tFrame.Load(int3(id.xy, 0));
}
)HLSL";

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
                    coefficients(c1) && coefficients(c2) && o.Width >= in.Width && o.Height >= in.Height &&   // 1:1 too (DLSS then runs as DLAA)
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
    if (FAILED(m_dev->CreateFence(0, D3D11_FENCE_FLAG_SHARED, IID_PPV_ARGS(&f.d3d11)))) { Log("%s upscaler: the %s fence could not be made", kUpscalerName, name); return false; }
    HANDLE handle = nullptr;
    if (FAILED(f.d3d11->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr, &handle))) { Log("%s upscaler: the %s fence could not be shared", kUpscalerName, name); f.Release(); return false; }
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
    if (FAILED(hr)) { Log("%s upscaler: the shared %s (%ux%u, format %d) could not be made: 0x%08x", kUpscalerName, name, w, h, (int)fmt, (unsigned)hr); t.Release(); return false; }
    t.w = w; t.h = h; t.fmt = fmt;
    Log("%s upscaler: shared %s %ux%u, format %d", kUpscalerName, name, w, h, (int)fmt);
    return true;
}

bool ScalerLink::Init(ID3D11Device* dev, ID3D11DeviceContext* ctx, SrEngine* engine, LogFn log) {
    m_log = std::move(log); m_engine = engine; m_ctx = ctx; m_frame = 0; for (auto& h : m_holds) h = 0; m_described = false;
    m_count = Counters(); m_lastShown = 0;
    m_atPresent = m_copiedAtPresent = 0; m_probeState = 0; m_pendingReset = false;
    if (FAILED(dev->QueryInterface(IID_PPV_ARGS(&m_dev))) || FAILED(ctx->QueryInterface(IID_PPV_ARGS(&m_ctx4)))) {
        Log("%s upscaler: Lossless Scaling's device has no shared fences (D3D11.4 is needed)", kUpscalerName); Shutdown(); return false;
    }
    if (!MakeFence(m_copied, "copied") || !MakeFence(m_done, "done") || !MakeGrabShader()) { Shutdown(); return false; }
    return true;
}

bool ScalerLink::MakeGrabShader() {
    ID3DBlob* code = nullptr, * error = nullptr;
    if (FAILED(D3DCompile(kGrabHlsl, strlen(kGrabHlsl), "scaler_grab", nullptr, nullptr, "CSGrab", "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &error))) {
        Log("%s upscaler: the grab shader: %s", kUpscalerName, error ? static_cast<const char*>(error->GetBufferPointer()) : "?"); SafeRelease(error); return false;
    }
    const HRESULT hr = m_dev->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &m_grab);
    code->Release();
    if (FAILED(hr)) { Log("%s upscaler: the grab shader could not be made: 0x%08x", kUpscalerName, (unsigned)hr); return false; }
    return true;
}

void ScalerLink::Unblock() {
    if (m_copied.d3d12 && m_copied.d3d12->GetCompletedValue() < m_frame) {
        Log("%s upscaler: the frame-copied signal had reached %llu of %llu; signalled from the CPU so the engine can finish", kUpscalerName,
            (unsigned long long)m_copied.d3d12->GetCompletedValue(), (unsigned long long)m_frame);
        m_copied.d3d12->Signal(m_frame);
    }
}

void ScalerLink::Shutdown() {
    Unblock();
    if (m_engine && (m_in[0].d3d12 || m_copied.d3d12)) m_engine->Drain();
    for (auto*& v : m_inUav) SafeRelease(v);
    SafeRelease(m_grab);
    for (auto& t : m_in) t.Release();
    for (auto& t : m_out) t.Release();
    for (auto& t : m_flow) t.Release();
    for (auto& h : m_holds) h = 0;
    m_atPresent = 0;
    SafeRelease(m_probe[0]); SafeRelease(m_probe[1]);
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
    Log("%s upscaler: Lossless Scaling's device changed; the old one: 0x%08x %s. Frames handed over %llu, 'copied' reached %llu, 'done' reached %llu", kUpscalerName,
        (unsigned)reason, name, (unsigned long long)m_frame, (unsigned long long)copied, (unsigned long long)done);
}

// Once per link: what the pass reads and writes, so the log shows whether its output is the swap chain's own buffer.
void ScalerLink::DescribeTargets(const NisPass& pass) {
    if (m_described) return;
    m_described = true;
    auto describe = [&](const char* name, ID3D11Resource* r) {
        D3D11_TEXTURE2D_DESC d{}; if (!Texture2D(r, d)) return;
        DXGI_USAGE usage = 0; IDXGIResource* dxgi = nullptr;
        if (SUCCEEDED(r->QueryInterface(IID_PPV_ARGS(&dxgi)))) { dxgi->GetUsage(&usage); dxgi->Release(); }
        Log("%s upscaler: the NIS pass's %s: %ux%u format %d, bind 0x%x, misc 0x%x, usage 0x%x%s", kUpscalerName, name, d.Width, d.Height, (int)d.Format,
            d.BindFlags, d.MiscFlags, (unsigned)usage, (usage & DXGI_USAGE_BACK_BUFFER) ? " (the swap chain's back buffer)" : "");
    };
    describe("input", pass.in);
    describe("output", pass.out);
    ID3D11ShaderResourceView* view = nullptr; m_ctx->CSGetShaderResources(0, 1, &view);   // how NIS reads the frame (an sRGB view would linearise it)
    if (view) { D3D11_SHADER_RESOURCE_VIEW_DESC vd{}; view->GetDesc(&vd); Log("%s upscaler: the NIS pass reads the frame as format %d", kUpscalerName, (int)vd.Format); view->Release(); }
}

bool ScalerLink::Upscale(const NisPass& pass, ID3D11Resource* flow, uint32_t flowW, uint32_t flowH, float flowUnit, float motionFraction, bool estimate, unsigned preset,
                         float sharpen, bool reset, Handoff handoff, float waitMs) {
    if (!IsReady() || !m_engine || !m_engine->IsReady()) return false;
    DescribeTargets(pass);
    if (handoff != m_handoff) {
        Log("%s upscaler: handoff %s", kUpscalerName, handoff == Handoff::Late ? "the newest finished picture (nothing waits)" : handoff == Handoff::Wait ? "GPU wait"
                                         : handoff == Handoff::Observe ? "observe only (NIS stays)" : "NIS runs, the picture copied over it at Present");
        Unblock(); m_engine->Drain();   // the variants keep their frames differently: start from an idle engine
        m_handoff = handoff; for (auto& h : m_holds) h = 0;
    }
    // the shared textures: the frame as RGBA8 (the grab pass reads BGRA as RGBA), the picture in the output's format, which the upscaler
    // writes through a UAV
    const DXGI_FORMAT inFmt = DXGI_FORMAT_R8G8B8A8_UNORM, outFmt = Bridge::ViewFormat(pass.outFmt);
    if (!Bridge::FormatSupported(pass.inFmt) || (outFmt != DXGI_FORMAT_R8G8B8A8_UNORM && outFmt != DXGI_FORMAT_R10G10B10A2_UNORM && outFmt != DXGI_FORMAT_R16G16B16A16_FLOAT)) {
        if (!m_loggedFormat) { Log("%s upscaler: frame format %d -> %d is not one the upscaler can take here; NIS stays", kUpscalerName, (int)pass.inFmt, (int)pass.outFmt); m_loggedFormat = true; }
        return false;
    }
    static const char* const inNames[kIn] = { "frame", "second frame" };
    static const char* const outNames[kOut] = { "picture", "second picture", "third picture" };
    bool refit = true;
    for (int i = 0; i < kIn && refit; ++i) {
        ID3D11Texture2D* const before = m_in[i].d3d11;
        refit = Fit(m_in[i], pass.inW, pass.inH, inFmt, true, inNames[i]);
        if (refit && (m_in[i].d3d11 != before || !m_inUav[i])) {
            SafeRelease(m_inUav[i]);
            if (FAILED(m_dev->CreateUnorderedAccessView(m_in[i].d3d11, nullptr, &m_inUav[i]))) { Log("%s upscaler: the frame's UAV could not be made", kUpscalerName); return false; }
        }
    }
    for (int i = 0; i < kOut && refit; ++i) {
        ID3D11Texture2D* const before = m_out[i].d3d11;
        refit = Fit(m_out[i], pass.outW, pass.outH, outFmt, true, outNames[i]);
        if (refit && m_out[i].d3d11 != before) m_holds[i] = 0;   // a new texture holds nothing yet
    }
    if (!refit) return false;
    ++m_count.passes;

    // A new frame goes to the engine while fewer than kIn are with it (its queue finishes them in order, so "done" says how many are left).
    // With Wait, Lossless Scaling's own queue waited for the one before, so there is room.
    const uint64_t finished = m_done.d3d11->GetCompletedValue();
    const bool room = m_handoff == Handoff::Wait || m_frame < finished + kIn;
    m_pendingReset = m_pendingReset || reset;   // not lost when a frame is not handed over
    const SavedBindings saved(m_ctx);
    if (room) {
        const uint64_t n = ++m_frame;
        const int in = static_cast<int>(n % kIn), out = static_cast<int>(n % kOut);
        ID3D11Texture2D* flowTex = nullptr;
        if (!estimate && flow && flowW && flowH && Fit(m_flow[in], flowW, flowH, DXGI_FORMAT_R16G16B16A16_FLOAT, false, in ? "second flow" : "flow")) flowTex = m_flow[in].d3d11;
        ID3D11ShaderResourceView* frame = saved.srvs[0];   // the NIS pass's own view of the frame
        m_ctx->CSSetShader(m_grab, nullptr, 0);
        m_ctx->CSSetShaderResources(0, 1, &frame);
        m_ctx->CSSetUnorderedAccessViews(0, 1, &m_inUav[in], nullptr);
        m_ctx->Dispatch((pass.inW + 7) / 8, (pass.inH + 7) / 8, 1);
        ID3D11ShaderResourceView* noSrv = nullptr; ID3D11UnorderedAccessView* noUav = nullptr;
        m_ctx->CSSetShaderResources(0, 1, &noSrv); m_ctx->CSSetUnorderedAccessViews(0, 1, &noUav, nullptr);
        if (flowTex) m_ctx->CopyResource(flowTex, flow);
        m_ctx4->Signal(m_copied.d3d11, n);
        m_ctx->Flush();   // the engine's queue waits for this signal: hand it to the GPU now
        // The engine signals "done" = n on its own queue in every case (after the frames before it), also when it could not run this one.
        const bool ran = m_engine->Run(m_in[in].d3d12, pass.inW, pass.inH, inFmt, m_out[out].d3d12, pass.outW, pass.outH, outFmt,
                                       flowTex ? m_flow[in].d3d12 : nullptr, m_flow[in].w, m_flow[in].h, flowUnit, motionFraction, estimate, preset, sharpen,
                                       m_pendingReset, m_copied.d3d12, n, m_done.d3d12, n);
        m_holds[out] = ran ? n : 0;
        if (ran) m_pendingReset = false;
        if (m_handoff == Handoff::Wait) {
            if (!ran) return false;
            m_ctx4->Wait(m_done.d3d11, n);   // a GPU wait: Lossless Scaling's queue holds until the picture is written
            m_ctx->CopyResource(pass.out, m_out[out].d3d11);
            return true;
        }
    } else {
        ++m_count.skipped;
    }
    // When the engine has finished nothing newer than the picture shown last (two passes close together, as adaptive frame generation makes
    // them, or a busy GPU), this pass would show it again: wait a little for the next one instead. Only then: a newer picture that is already
    // finished is shown at once (waiting for every picture's successor cost Lossless Scaling's thread time on most passes, 2026-09-25).
    // A spin on the fence, not a timed wait: Windows can round a short timeout up to its 15.6 ms timer tick.
    const uint64_t want = m_lastShown + 1;
    if (m_handoff == Handoff::Late && waitMs > 0.0f && m_lastShown && want <= m_frame && m_done.d3d11->GetCompletedValue() < want) {
        LARGE_INTEGER f, t0, t; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&t0);
        const LONGLONG limit = static_cast<LONGLONG>(f.QuadPart * (waitMs / 1000.0));
        bool got = false;
        do { YieldProcessor(); QueryPerformanceCounter(&t); got = m_done.d3d11->GetCompletedValue() >= want; } while (!got && t.QuadPart - t0.QuadPart < limit);
        ++m_count.waits; if (got) ++m_count.waitHits;
        m_count.waitedMs += (t.QuadPart - t0.QuadPart) * 1000.0 / f.QuadPart;
    }
    // The newest picture the engine has finished (never one it is writing: those are the kIn at most after it, in other textures).
    const uint64_t newest = m_done.d3d11->GetCompletedValue();
    Probe(newest, room);
    if ((m_count.passes % 3000) == 0)
        Log("%s upscaler: %llu passes, %llu frames not handed over (the engine had %d already), %llu pictures shown twice; waited for a picture "
            "%llu times (got it %llu times, %.2f ms on average)", kUpscalerName, (unsigned long long)m_count.passes, (unsigned long long)m_count.skipped, kIn,
            (unsigned long long)m_count.repeats, (unsigned long long)m_count.waits, (unsigned long long)m_count.waitHits,
            m_count.waits ? m_count.waitedMs / m_count.waits : 0.0);
    if (m_handoff == Handoff::Observe) return false;
    const bool ready = newest && m_holds[newest % kOut] == newest;
    if (m_handoff == Handoff::AtPresent) { m_atPresent = ready ? newest : 0; return false; }   // NIS runs; the picture goes over it at Present
    if (!ready) return false;   // nothing finished yet: NIS this once
    if (newest == m_lastShown) ++m_count.repeats;
    m_lastShown = newest;
    m_ctx->CopyResource(pass.out, m_out[newest % kOut].d3d11);
    return true;
}

void ScalerLink::PresentCopy(IDXGISwapChain* sc) {
    if (!m_atPresent || !m_dev || m_handoff != Handoff::AtPresent) return;
    ID3D11Texture2D* back = nullptr;
    if (FAILED(sc->GetBuffer(0, IID_PPV_ARGS(&back)))) return;
    ID3D11Device* dev = nullptr; back->GetDevice(&dev);
    D3D11_TEXTURE2D_DESC d{}; back->GetDesc(&d);
    const Shared& picture = m_out[m_atPresent % kOut];
    if (dev == m_dev && d.Width == picture.w && d.Height == picture.h && m_holds[m_atPresent % kOut] == m_atPresent) {
        m_ctx->CopyResource(back, picture.d3d11);
        if (++m_copiedAtPresent == 1) Log("%s upscaler: first picture copied into the back buffer at Present", kUpscalerName);
        m_atPresent = 0;
    }
    if (dev) dev->Release();
    back->Release();
}

// Once per link, a while in: the average brightness (0-255) and the share of pure black pixels of the frame DLSS was given and the picture it
// made, read back without waiting (the copies are mapped only once the GPU has finished them).
void ScalerLink::Probe(uint64_t shown, bool inFresh) {
    if (m_probeState == 2 || !shown || m_holds[shown % kOut] != shown) return;
    auto bytes8 = [](DXGI_FORMAT f) { return f == DXGI_FORMAT_R8G8B8A8_UNORM || f == DXGI_FORMAT_B8G8R8A8_UNORM || f == DXGI_FORMAT_B8G8R8X8_UNORM; };
    if (m_probeState == 0) {
        if (m_frame < 120 || !inFresh) return;
        const Shared* src[2] = { &m_in[m_frame % kIn], &m_out[shown % kOut] };
        for (int i = 0; i < 2; ++i) {
            D3D11_TEXTURE2D_DESC d{}; src[i]->d3d11->GetDesc(&d);
            d.Usage = D3D11_USAGE_STAGING; d.BindFlags = 0; d.MiscFlags = 0; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            if (FAILED(m_dev->CreateTexture2D(&d, nullptr, &m_probe[i]))) { SafeRelease(m_probe[0]); SafeRelease(m_probe[1]); m_probeState = 2; return; }
            m_ctx->CopyResource(m_probe[i], src[i]->d3d11);
        }
        m_probeState = 1;
        return;
    }
    const char* names[2] = { "frame given to the upscaler", "picture the upscaler made" };
    D3D11_MAPPED_SUBRESOURCE maps[2] = {};
    for (int i = 0; i < 2; ++i) {
        if (m_ctx->Map(m_probe[i], 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &maps[i]) != S_OK) { if (i) m_ctx->Unmap(m_probe[0], 0); return; }   // not finished yet
    }
    for (int i = 0; i < 2; ++i) {
        D3D11_TEXTURE2D_DESC d{}; m_probe[i]->GetDesc(&d);
        if (!bytes8(d.Format)) { Log("%s upscaler: probe: the %s is format %d (not read)", kUpscalerName, names[i], (int)d.Format); continue; }
        double sum = 0; uint64_t n = 0, black = 0;
        for (uint32_t y = 0; y < d.Height; y += 16) {
            const uint8_t* row = static_cast<const uint8_t*>(maps[i].pData) + static_cast<size_t>(y) * maps[i].RowPitch;
            for (uint32_t x = 0; x < d.Width; x += 16) {
                const uint8_t* px = row + x * 4;
                const int v = px[0] + px[1] + px[2];
                sum += v / 3.0; ++n; if (v == 0) ++black;
            }
        }
        Log("%s upscaler: probe: the %s (%ux%u) averages %.1f of 255, %.1f%% of it pure black", kUpscalerName, names[i], d.Width, d.Height, n ? sum / n : 0.0, n ? 100.0 * black / n : 0.0);
    }
    for (int i = 0; i < 2; ++i) m_ctx->Unmap(m_probe[i], 0);
    SafeRelease(m_probe[0]); SafeRelease(m_probe[1]);
    m_probeState = 2;
}

} // namespace nr
