#include "addon/bridge.h"
#include <cstdio>
#include <cstdarg>
#include <algorithm>

#pragma comment(lib, "d3d11.lib")

void Bridge::Log(const char* fmt, ...) { char b[512]; va_list a; va_start(a, fmt); vsnprintf(b, sizeof b, fmt, a); va_end(a); if (m_log) m_log(b); }

DXGI_FORMAT Bridge::ViewFormat(DXGI_FORMAT f) {
    switch (f) {
    case DXGI_FORMAT_B8G8R8A8_UNORM: case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: case DXGI_FORMAT_B8G8R8A8_TYPELESS: return DXGI_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_R8G8B8A8_UNORM: case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: case DXGI_FORMAT_R8G8B8A8_TYPELESS: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_R10G10B10A2_UNORM: case DXGI_FORMAT_R10G10B10A2_TYPELESS: return DXGI_FORMAT_R10G10B10A2_UNORM;
    case DXGI_FORMAT_R16G16B16A16_FLOAT: case DXGI_FORMAT_R16G16B16A16_TYPELESS: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    default: return DXGI_FORMAT_UNKNOWN;
    }
}
bool Bridge::FormatSupported(DXGI_FORMAT f) {
    DXGI_FORMAT v = ViewFormat(f);
    return v == DXGI_FORMAT_B8G8R8A8_UNORM || v == DXGI_FORMAT_R8G8B8A8_UNORM;   // v1: 8-bit display-referred only
}

bool Bridge::MakeSharedFence(ID3D11Fence** f11, ID3D12Fence** f12, const char* what) {
    if (FAILED(m_dev5->CreateFence(0, D3D11_FENCE_FLAG_SHARED, IID_PPV_ARGS(f11)))) { Log("Bridge: CreateFence(%s) failed", what); return false; }
    HANDLE h = nullptr;
    if (FAILED((*f11)->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr, &h))) { Log("Bridge: fence CreateSharedHandle(%s) failed", what); return false; }
    *f12 = m_engine->OpenSharedFence(h); CloseHandle(h);
    return *f12 != nullptr;
}
bool Bridge::Share(ID3D11Texture2D* t, ID3D12Resource** out) {
    IDXGIResource1* r = nullptr; if (FAILED(t->QueryInterface(IID_PPV_ARGS(&r)))) return false;
    HANDLE hh = nullptr; HRESULT hr = r->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr, &hh); r->Release();
    if (FAILED(hr)) { Log("Bridge: texture CreateSharedHandle 0x%08x", hr); return false; }
    *out = m_engine->OpenSharedTexture(hh); CloseHandle(hh); return *out != nullptr;
}

bool Bridge::Init(ID3D11Device* dev, ID3D11DeviceContext* ctx, NrEngine* engine, LogFn log) {
    Shutdown();
    m_log = log; m_engine = engine; m_ctx = ctx;
    if (FAILED(dev->QueryInterface(IID_PPV_ARGS(&m_dev5)))) { Log("Bridge: ID3D11Device5 not available"); return false; }
    if (FAILED(ctx->QueryInterface(IID_PPV_ARGS(&m_ctx4)))) { Log("Bridge: ID3D11DeviceContext4 not available"); Shutdown(); return false; }
    if (!MakeSharedFence(&m_copyIn11, &m_copyIn12, "copy-in") || !MakeSharedFence(&m_done11, &m_done12, "done") || !MakeSharedFence(&m_used11, &m_used12, "used")) { Shutdown(); return false; }
    m_lastRun = 0; m_lastRunSlot = -1; m_useCounter = 0; m_runs = m_skipped = 0; m_lastQpc = 0; m_intervalMs = m_cpuMs = 0;
    Log("Bridge: shared fences up");
    return true;
}

void Bridge::SetLsGpuPriority(int p) {
    if (!m_dev5) return;
    if (m_lsPrioSet && p == m_lsPrio) return;
    IDXGIDevice* dx = nullptr;
    if (SUCCEEDED(m_dev5->QueryInterface(IID_PPV_ARGS(&dx))) && dx) {
        HRESULT hr = dx->SetGPUThreadPriority(p); INT now = 0; dx->GetGPUThreadPriority(&now); dx->Release();
        Log("Bridge: LS device GPU thread priority %d -> 0x%08x (now %d)", p, (unsigned)hr, now);
        m_lsPrio = p; m_lsPrioSet = SUCCEEDED(hr);
    }
}

void Bridge::ReleaseDeltas() {
    for (int i = 0; i < kSlots; ++i) {
        if (m_deltaSrv[i]) m_deltaSrv[i]->Release(); if (m_delta12[i]) m_delta12[i]->Release(); if (m_delta11[i]) m_delta11[i]->Release();
        m_deltaSrv[i] = nullptr; m_delta12[i] = nullptr; m_delta11[i] = nullptr; m_slotFrame[i] = 0; m_slotLastUse[i] = 0;
    }
    m_dw = m_dh = 0; m_lastRunSlot = -1;
}
void Bridge::ReleaseTextures() {
    if (m_engine) m_engine->SetFlowInput(nullptr, 0, 0);
    if (m_in12) m_in12->Release(); if (m_in11) m_in11->Release(); m_in12 = nullptr; m_in11 = nullptr;
    if (m_flow12) m_flow12->Release(); if (m_flow11) m_flow11->Release(); m_flow12 = nullptr; m_flow11 = nullptr; m_fw = m_fh = 0;
    ReleaseDeltas();
    m_w = m_h = 0; m_fmt = DXGI_FORMAT_UNKNOWN;
}

void Bridge::Shutdown() {
    if (m_engine && (m_in12 || m_copyIn12)) m_engine->Drain();   // queued GPU work may still reference our shared textures/fences
    ReleaseTextures();
    for (auto** f : { &m_copyIn12, &m_done12, &m_used12 }) { if (*f) (*f)->Release(); *f = nullptr; }
    for (auto** f : { &m_copyIn11, &m_done11, &m_used11 }) { if (*f) (*f)->Release(); *f = nullptr; }
    if (m_lsPrioSet && m_lsPrio != 0) SetLsGpuPriority(0);   // leave LS's device as we found it
    if (m_ctx4) m_ctx4->Release(); if (m_dev5) m_dev5->Release(); m_ctx4 = nullptr; m_dev5 = nullptr; m_ctx = nullptr;
}

bool Bridge::Ensure(uint32_t w, uint32_t h, DXGI_FORMAT fmt) {
    DXGI_FORMAT vf = ViewFormat(fmt);
    if (w == m_w && h == m_h && vf == m_fmt && m_in11) return true;
    if (!FormatSupported(fmt)) { uint64_t key = 0xF000000000000000ull | (uint32_t)fmt; if (key != m_lastFailKey) { m_lastFailKey = key; Log("Bridge: unsupported frame format %d", (int)fmt); } return false; }
    if (m_in12) m_engine->Drain();
    ReleaseTextures();
    D3D11_TEXTURE2D_DESC d{}; d.Width = w; d.Height = h; d.MipLevels = 1; d.ArraySize = 1; d.Format = vf; d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    d.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;   // NTHANDLE must be paired with SHARED (or KEYEDMUTEX)
    HRESULT hr = m_dev5->CreateTexture2D(&d, nullptr, &m_in11);
    if (FAILED(hr) || !Share(m_in11, &m_in12)) {
        uint64_t key = ((uint64_t)w << 40) | ((uint64_t)h << 16) | (uint32_t)vf;
        if (key != m_lastFailKey) { m_lastFailKey = key; Log("Bridge: shared input creation failed (%ux%u fmt %d) hr=0x%08x", w, h, (int)vf, (unsigned)hr); }
        ReleaseTextures(); return false;
    }
    m_w = w; m_h = h; m_fmt = vf;
    Log("Bridge: shared input %ux%u fmt %d", w, h, (int)vf);
    return true;
}

bool Bridge::EnsureFlow(uint32_t w, uint32_t h) {
    if (w == m_fw && h == m_fh && m_flow11) return true;
    if (m_flow11) { m_engine->Drain(); m_engine->SetFlowInput(nullptr, 0, 0); m_flow12->Release(); m_flow11->Release(); m_flow12 = nullptr; m_flow11 = nullptr; m_fw = m_fh = 0; }
    D3D11_TEXTURE2D_DESC d{}; d.Width = w; d.Height = h; d.MipLevels = 1; d.ArraySize = 1; d.Format = DXGI_FORMAT_R16G16B16A16_FLOAT; d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = D3D11_BIND_SHADER_RESOURCE; d.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
    HRESULT hr = m_dev5->CreateTexture2D(&d, nullptr, &m_flow11);
    if (FAILED(hr)) { uint64_t key = 0xE000000000000000ull | ((uint64_t)w << 20) | h; if (key != m_lastFailKey) { m_lastFailKey = key; Log("Bridge: shared flow texture failed (%ux%u) hr=0x%08x", w, h, (unsigned)hr); } m_flow11 = nullptr; return false; }
    if (!Share(m_flow11, &m_flow12)) { m_flow11->Release(); m_flow11 = nullptr; return false; }
    m_fw = w; m_fh = h;
    Log("Bridge: shared flow texture %ux%u RGBA16F", w, h);
    return true;
}

bool Bridge::EnsureDeltas(uint32_t ww, uint32_t wh) {
    if (ww == m_dw && wh == m_dh && m_delta11[0]) return true;
    m_engine->Drain();
    ReleaseDeltas();
    D3D11_TEXTURE2D_DESC d{}; d.Width = ww; d.Height = wh; d.MipLevels = 1; d.ArraySize = 1; d.Format = DXGI_FORMAT_R16G16B16A16_FLOAT; d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS; d.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
    for (int i = 0; i < kSlots; ++i) {
        HRESULT hr = m_dev5->CreateTexture2D(&d, nullptr, &m_delta11[i]);
        if (FAILED(hr) || !Share(m_delta11[i], &m_delta12[i]) || FAILED(m_dev5->CreateShaderResourceView(m_delta11[i], nullptr, &m_deltaSrv[i]))) {
            uint64_t key = 0xD000000000000000ull | ((uint64_t)ww << 20) | wh; if (key != m_lastFailKey) { m_lastFailKey = key; Log("Bridge: shared delta creation failed (%ux%u) hr=0x%08x", ww, wh, (unsigned)hr); }
            ReleaseDeltas(); return false;
        }
    }
    m_dw = ww; m_dh = wh;
    Log("Bridge: %d shared delta slots %ux%u RGBA16F", kSlots, ww, wh);
    return true;
}

// Everything below is recorded on LS's immediate context in order. The CPU never blocks here and LS's queue never
// waits: the only GPU-side waits are on the model's queue.
bool Bridge::Submit(ID3D11Texture2D* frame, ID3D11Texture2D* flow, uint32_t flowW, uint32_t flowH, const NrParams& params, bool reset, uint64_t frameIndex) {
    if (!m_in11 || !m_copyIn11 || !m_engine || !m_engine->IsReady()) return false;
    LARGE_INTEGER qf, q0; QueryPerformanceFrequency(&qf); QueryPerformanceCounter(&q0);
    if (m_lastQpc) {
        double ms = (double)(q0.QuadPart - m_lastQpc) * 1000.0 / (double)qf.QuadPart;
        if (ms < 500.0) { m_lastRawMs = ms; m_intervalMs = m_intervalMs == 0 ? ms : m_intervalMs * 0.9 + ms * 0.1; if (m_ivCount < kIvCap) m_ivRing[m_ivCount++] = (float)ms; }
    }
    m_lastQpc = q0.QuadPart;
    auto cpuDone = [&]() { LARGE_INTEGER q1; QueryPerformanceCounter(&q1); double ms = (double)(q1.QuadPart - q0.QuadPart) * 1000.0 / (double)qf.QuadPart; m_cpuMs = m_cpuMs == 0 ? ms : m_cpuMs * 0.9 + ms * 0.1; };
    if (!m_engine->Prepare(m_w, m_h, m_fmt, params)) { cpuDone(); return false; }
    if (!EnsureDeltas(m_engine->Stats().workW, m_engine->Stats().workH)) { cpuDone(); return false; }
    // The model is still on the previous frame: skip this one. The present side keeps warping the newest delta.
    if (m_lastRun && m_done12->GetCompletedValue() < m_lastRun) { m_skipped++; cpuDone(); return false; }
    const bool haveFlow = flow && params.useFlow && flowW && flowH && EnsureFlow(flowW, flowH);
    m_engine->SetFlowInput(haveFlow ? m_flow12 : nullptr, m_fw, m_fh);

    // Round-robin over the slots: the previous run's slot is the newest delta (presents read it), the one before
    // may still be read by a compose already recorded; the run waits on "used" for that before writing.
    const int slot = (m_lastRunSlot + 1) % kSlots;
    m_ctx->CopyResource(m_in11, frame);
    if (haveFlow) m_ctx->CopyResource(m_flow11, flow);
    m_ctx4->Signal(m_copyIn11, frameIndex);
    bool ok = m_engine->Run(m_in12, m_delta12[slot], m_copyIn12, frameIndex, m_used12, m_slotLastUse[slot], m_done12, frameIndex, reset);
    m_lastRun = frameIndex; m_lastRunSlot = slot;
    m_slotFrame[slot] = ok ? frameIndex : 0;   // a failed evaluate leaves garbage in the slot: never hand it out
    if (ok) m_runs++;
    cpuDone();
    return ok;
}

bool Bridge::TakeFrameTimeWindow(float& p50, float& p95, float& p99, float& worst, int& n, int& over20, int& over33) {
    n = m_ivCount; if (n < 30) return false;
    float v[kIvCap]; for (int i = 0; i < n; ++i) v[i] = m_ivRing[i];
    std::sort(v, v + n);
    auto pct = [&](double p) { return v[(int)std::min<double>(n - 1, p * n)]; };
    p50 = pct(0.50); p95 = pct(0.95); p99 = pct(0.99); worst = v[n - 1];
    over20 = over33 = 0; for (int i = 0; i < n; ++i) { if (v[i] > 20.0f) ++over20; if (v[i] > 33.0f) ++over33; }
    m_ivCount = 0;
    return true;
}

uint64_t Bridge::NewestDelta(ID3D11ShaderResourceView** srv, uint32_t* ww, uint32_t* wh) {
    if (!m_done12 || !m_delta11[0]) return 0;
    const uint64_t done = m_done12->GetCompletedValue();
    uint64_t best = 0; int bs = -1;
    for (int i = 0; i < kSlots; ++i) if (m_slotFrame[i] && m_slotFrame[i] <= done && m_slotFrame[i] > best) { best = m_slotFrame[i]; bs = i; }
    if (bs < 0) return 0;
    if (srv) *srv = m_deltaSrv[bs]; if (ww) *ww = m_dw; if (wh) *wh = m_dh;
    return best;
}
void Bridge::BeginDeltaUse(uint64_t d) { if (m_ctx4 && m_done11) m_ctx4->Wait(m_done11, d); }   // already signalled: instant, but makes the write visible
void Bridge::EndDeltaUse(uint64_t d) {
    int s = SlotOf(d); if (s < 0 || !m_ctx4 || !m_used11) return;
    m_ctx4->Signal(m_used11, ++m_useCounter); m_slotLastUse[s] = m_useCounter;
}
