#include "addon/bridge.h"
#include <algorithm>
#include <cstdarg>
#include <cstdio>

namespace {
template <class T> void SafeRelease(T*& p) { if (p) { p->Release(); p = nullptr; } }

double Smooth(double avg, double sample) { return avg == 0 ? sample : avg * 0.9 + sample * 0.1; }
double Ms(int64_t ticks, int64_t freq) { return (double)ticks * 1000.0 / (double)freq; }
int64_t Now() { LARGE_INTEGER q; QueryPerformanceCounter(&q); return q.QuadPart; }

// Keys for LogOnce: what failed, and for which size and format.
uint64_t FailureKey(char what, uint32_t w, uint32_t h, uint32_t fmt) { return ((uint64_t)(uint8_t)what << 56) ^ ((uint64_t)w << 36) ^ ((uint64_t)h << 16) ^ fmt; }
}

void Bridge::SharedTexture::Release() { SafeRelease(view); SafeRelease(d3d12); SafeRelease(d3d11); w = h = 0; }
void Bridge::SharedFence::Release() { SafeRelease(d3d12); SafeRelease(d3d11); }

void Bridge::Log(const char* fmt, ...) {
    if (!m_log) return;
    char text[512]; va_list args; va_start(args, fmt); vsnprintf(text, sizeof text, fmt, args); va_end(args);
    m_log(text);
}
void Bridge::LogOnce(uint64_t key, const char* fmt, ...) {
    if (key == m_lastFailure || !m_log) return;
    m_lastFailure = key;
    char text[512]; va_list args; va_start(args, fmt); vsnprintf(text, sizeof text, fmt, args); va_end(args);
    m_log(text);
}

DXGI_FORMAT Bridge::ViewFormat(DXGI_FORMAT f) {
    switch (f) {
    case DXGI_FORMAT_B8G8R8A8_TYPELESS: case DXGI_FORMAT_B8G8R8A8_UNORM: case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        return DXGI_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_R8G8B8A8_TYPELESS: case DXGI_FORMAT_R8G8B8A8_UNORM: case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS: case DXGI_FORMAT_R10G10B10A2_UNORM:
        return DXGI_FORMAT_R10G10B10A2_UNORM;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS: case DXGI_FORMAT_R16G16B16A16_FLOAT:
        return DXGI_FORMAT_R16G16B16A16_FLOAT;
    default:
        return DXGI_FORMAT_UNKNOWN;
    }
}
// The model is fed 8-bit display-referred frames only (no HDR yet).
bool Bridge::FormatSupported(DXGI_FORMAT f) {
    const DXGI_FORMAT view = ViewFormat(f);
    return view == DXGI_FORMAT_R8G8B8A8_UNORM || view == DXGI_FORMAT_B8G8R8A8_UNORM;
}

bool Bridge::MakeFence(SharedFence& f, const char* name) {
    if (FAILED(m_dev->CreateFence(0, D3D11_FENCE_FLAG_SHARED, IID_PPV_ARGS(&f.d3d11)))) { Log("Bridge: the %s fence could not be created", name); return false; }
    HANDLE handle = nullptr;
    if (FAILED(f.d3d11->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr, &handle))) { Log("Bridge: the %s fence could not be shared", name); f.Release(); return false; }
    f.d3d12 = m_engine->OpenSharedFence(handle);
    CloseHandle(handle);
    if (!f.d3d12) { f.Release(); return false; }
    return true;
}

// Every shared texture is one mip, no multisampling, shared through an NT handle (which D3D11 wants paired with SHARED). The model only reads the
// input and flow copies; it writes the result slots, which Lossless Scaling's side then reads through a view.
bool Bridge::MakeTexture(SharedTexture& t, uint32_t w, uint32_t h, DXGI_FORMAT fmt, bool modelWrites, const char* name) {
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = w; desc.Height = h; desc.MipLevels = 1; desc.ArraySize = 1; desc.Format = fmt; desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | (modelWrites ? D3D11_BIND_UNORDERED_ACCESS : 0u);
    desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
    HRESULT hr = m_dev->CreateTexture2D(&desc, nullptr, &t.d3d11);
    if (SUCCEEDED(hr)) {
        IDXGIResource1* dxgi = nullptr;
        hr = t.d3d11->QueryInterface(IID_PPV_ARGS(&dxgi));
        HANDLE handle = nullptr;
        if (SUCCEEDED(hr)) { hr = dxgi->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr, &handle); dxgi->Release(); }
        if (SUCCEEDED(hr)) { t.d3d12 = m_engine->OpenSharedTexture(handle); CloseHandle(handle); if (!t.d3d12) hr = E_FAIL; }
    }
    if (SUCCEEDED(hr) && modelWrites) hr = m_dev->CreateShaderResourceView(t.d3d11, nullptr, &t.view);
    if (FAILED(hr)) {
        LogOnce(FailureKey(name[0], w, h, (uint32_t)fmt), "Bridge: the shared %s (%ux%u, format %d) could not be made: 0x%08x", name, w, h, (int)fmt, (unsigned)hr);
        t.Release();
        return false;
    }
    t.w = w; t.h = h;
    return true;
}

bool Bridge::Init(ID3D11Device* dev, ID3D11DeviceContext* ctx, NrEngine* engine, LogFn log) {
    Shutdown();
    m_log = std::move(log); m_engine = engine; m_ctx = ctx;
    if (FAILED(dev->QueryInterface(IID_PPV_ARGS(&m_dev)))) { Log("Bridge: Lossless Scaling's device has no ID3D11Device5"); Shutdown(); return false; }
    if (FAILED(ctx->QueryInterface(IID_PPV_ARGS(&m_ctx4)))) { Log("Bridge: Lossless Scaling's context has no ID3D11DeviceContext4"); Shutdown(); return false; }
    if (!MakeFence(m_copied, "copied") || !MakeFence(m_finished, "finished") || !MakeFence(m_released, "released")) { Shutdown(); return false; }
    m_inFlight = 0; m_newestSlot = -1; m_releaseCount = 0; m_runs = m_skipped = 0;
    m_prevFrameQpc = 0; m_intervalMs = m_lastIntervalMs = m_cpuMs = 0; m_frameTimeCount = 0;
    Log("Bridge: shared fences up");
    return true;
}

void Bridge::Unblock() {
    if (m_copied.d3d12 && m_copied.d3d12->GetCompletedValue() < m_inFlight) {
        Log("Bridge: the frame-copied signal had reached %llu of %llu; signalled from the CPU", (unsigned long long)m_copied.d3d12->GetCompletedValue(),
            (unsigned long long)m_inFlight);
        m_copied.d3d12->Signal(m_inFlight);
    }
    if (m_released.d3d12 && m_released.d3d12->GetCompletedValue() < m_releaseCount) m_released.d3d12->Signal(m_releaseCount);
}

void Bridge::Shutdown() {
    // Runs still queued on the model's side may use the shared textures and fences: release their waits, then let them finish.
    Unblock();
    if (m_engine && (m_input.d3d12 || m_copied.d3d12)) m_engine->Drain();
    // a compose on Lossless Scaling's side may wait on the GPU for a result the model did not finish: release it
    if (m_finished.d3d12 && m_finished.d3d12->GetCompletedValue() < m_inFlight) {
        Log("Bridge: the model had finished %llu of %llu; released Lossless Scaling's wait for it", (unsigned long long)m_finished.d3d12->GetCompletedValue(),
            (unsigned long long)m_inFlight);
        m_finished.d3d12->Signal(m_inFlight);
    }
    DropInput(); DropFlow(); DropSlots();
    m_copied.Release(); m_finished.Release(); m_released.Release();
    if (m_lsPriorityApplied && m_lsPriority != 0) SetLsGpuPriority(0);   // leave Lossless Scaling's device as it was
    SafeRelease(m_ctx4); SafeRelease(m_dev); m_ctx = nullptr;
}

void Bridge::SetLsGpuPriority(int p) {
    if (!m_dev || (m_lsPriorityApplied && p == m_lsPriority)) return;
    IDXGIDevice* dxgi = nullptr;
    if (FAILED(m_dev->QueryInterface(IID_PPV_ARGS(&dxgi)))) return;
    const HRESULT hr = dxgi->SetGPUThreadPriority(p);
    INT now = 0; dxgi->GetGPUThreadPriority(&now);
    dxgi->Release();
    Log("Bridge: Lossless Scaling's GPU thread priority set to %d: 0x%08x (now %d)", p, (unsigned)hr, now);
    m_lsPriority = p; m_lsPriorityApplied = SUCCEEDED(hr);
}

void Bridge::DropInput() { m_input.Release(); m_fmt = DXGI_FORMAT_UNKNOWN; }
void Bridge::DropFlow() { if (m_engine) m_engine->SetFlowInput(nullptr, 0, 0); m_flow.Release(); }
void Bridge::DropSlots() { for (Slot& s : m_slots) { s.delta.Release(); s.frame = 0; s.releasedAt = 0; } m_newestSlot = -1; }

bool Bridge::Ensure(uint32_t w, uint32_t h, DXGI_FORMAT fmt) {
    const DXGI_FORMAT view = ViewFormat(fmt);
    if (m_input.d3d11 && m_input.w == w && m_input.h == h && m_fmt == view) return true;
    if (!FormatSupported(fmt)) { LogOnce(FailureKey('F', 0, 0, (uint32_t)fmt), "Bridge: frames of format %d cannot be fed to the model", (int)fmt); return false; }
    if (m_input.d3d12) m_engine->Drain();
    DropInput(); DropFlow(); DropSlots();
    if (!MakeTexture(m_input, w, h, view, false, "input")) return false;
    m_fmt = view;
    Log("Bridge: shared input %ux%u, format %d", w, h, (int)view);
    return true;
}

bool Bridge::FitFlow(uint32_t w, uint32_t h) {
    if (m_flow.d3d11 && m_flow.w == w && m_flow.h == h) return true;
    if (m_flow.d3d11) { m_engine->Drain(); DropFlow(); }
    if (!MakeTexture(m_flow, w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, false, "flow")) return false;
    Log("Bridge: shared flow %ux%u, RGBA16F", w, h);
    return true;
}

bool Bridge::FitSlots(uint32_t w, uint32_t h) {
    if (m_slots[0].delta.d3d11 && m_slots[0].delta.w == w && m_slots[0].delta.h == h) return true;
    m_engine->Drain();
    DropSlots();
    for (Slot& s : m_slots)
        if (!MakeTexture(s.delta, w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, true, "result")) { DropSlots(); return false; }
    Log("Bridge: %d shared result slots %ux%u, RGBA16F", kSlots, w, h);
    return true;
}

int Bridge::FindSlot(uint64_t frame) const {
    for (int i = 0; i < kSlots; ++i) if (m_slots[i].frame == frame) return i;
    return -1;
}

void Bridge::NoteFrameTime(int64_t now, int64_t freq) {
    if (m_prevFrameQpc) {
        const double ms = Ms(now - m_prevFrameQpc, freq);
        if (ms < 500.0) {   // longer is a pause (loading, alt-tab), not a frame time
            m_lastIntervalMs = ms;
            m_intervalMs = Smooth(m_intervalMs, ms);
            if (m_frameTimeCount < kFrameTimeCap) m_frameTimes[m_frameTimeCount++] = (float)ms;
        }
    }
    m_prevFrameQpc = now;
}

// Everything here is recorded on Lossless Scaling's immediate context, in order. The CPU never blocks. Lossless Scaling's queue waits only with
// orderOnGpu (frame generation off), for the model's run before; otherwise the only waits are the model queue's.
bool Bridge::Submit(ID3D11Texture2D* frame, ID3D11Texture2D* flow, uint32_t flowW, uint32_t flowH, const NrParams& params, bool reset, uint64_t frameIndex,
                    bool orderOnGpu) {
    if (!m_input.d3d11 || !m_copied.d3d11 || !m_engine || !m_engine->IsReady()) return false;
    LARGE_INTEGER freq; QueryPerformanceFrequency(&freq);
    const int64_t start = Now();
    NoteFrameTime(start, freq.QuadPart);
    struct CpuTime { Bridge& b; int64_t start, freq; ~CpuTime() { b.m_cpuMs = Smooth(b.m_cpuMs, Ms(Now() - start, freq)); } } cpuTime{ *this, start, freq.QuadPart };

    if (!m_engine->Prepare(m_input.w, m_input.h, m_fmt, params)) return false;
    if (!FitSlots(m_engine->Stats().workW, m_engine->Stats().workH)) return false;
    // The model is still busy with an earlier frame: skip this one.
    if (m_inFlight && m_finished.d3d12->GetCompletedValue() < m_inFlight) {
        if (!orderOnGpu) { ++m_skipped; return false; }
        m_ctx4->Wait(m_finished.d3d11, m_inFlight);   // the copy below waits on the GPU until the model has read the frame before
    }
    const bool withFlow = flow && params.useFlow && flowW && flowH && FitFlow(flowW, flowH);
    m_engine->SetFlowInput(withFlow ? m_flow.d3d12 : nullptr, m_flow.w, m_flow.h);

    // The slots take turns. The newest holds the result the presents use now; the one before may still be read by a compose already recorded,
    // which is what the run waits on "released" for. So the next in turn is always free to write.
    const int s = (m_newestSlot + 1) % kSlots;
    m_ctx->CopyResource(m_input.d3d11, frame);
    if (withFlow) m_ctx->CopyResource(m_flow.d3d11, flow);
    m_ctx4->Signal(m_copied.d3d11, frameIndex);
    const uint64_t queuedBefore = m_engine->Stats().frames;
    const bool ok = m_engine->Run(m_input.d3d12, m_slots[s].delta.d3d12, m_copied.d3d12, frameIndex, m_released.d3d12, m_slots[s].releasedAt,
                                  m_finished.d3d12, frameIndex, reset);
    // A run that was never queued never signals "finished": waiting for it would skip every frame from now on.
    if (m_engine->Stats().frames == queuedBefore) return false;
    m_inFlight = frameIndex; m_newestSlot = s;
    m_slots[s].frame = ok ? frameIndex : 0;   // a run whose model evaluation failed leaves no usable result
    if (ok) ++m_runs;
    return ok;
}

bool Bridge::TakeFrameTimeWindow(float& p50, float& p95, float& p99, float& worst, int& n, int& over20, int& over33) {
    n = m_frameTimeCount;
    if (n < 30) return false;
    float sorted[kFrameTimeCap];
    std::copy(m_frameTimes, m_frameTimes + n, sorted);
    std::sort(sorted, sorted + n);
    auto at = [&](double q) { const int i = (int)(q * n); return sorted[i < n ? i : n - 1]; };
    p50 = at(0.50); p95 = at(0.95); p99 = at(0.99); worst = sorted[n - 1];
    over20 = (int)std::count_if(sorted, sorted + n, [](float ms) { return ms > 20.0f; });
    over33 = (int)std::count_if(sorted, sorted + n, [](float ms) { return ms > 33.0f; });
    m_frameTimeCount = 0;
    return true;
}

uint64_t Bridge::QueuedDelta(ID3D11ShaderResourceView** srv, uint32_t* ww, uint32_t* wh) {
    if (m_newestSlot < 0 || !m_inFlight || m_slots[m_newestSlot].frame != m_inFlight) return 0;   // none, or the newest run failed
    if (srv) *srv = m_slots[m_newestSlot].delta.view;
    if (ww) *ww = m_slots[m_newestSlot].delta.w;
    if (wh) *wh = m_slots[m_newestSlot].delta.h;
    return m_inFlight;
}

uint64_t Bridge::NewestDelta(ID3D11ShaderResourceView** srv, uint32_t* ww, uint32_t* wh) {
    if (!m_finished.d3d12 || !m_slots[0].delta.d3d11) return 0;
    const uint64_t finished = m_finished.d3d12->GetCompletedValue();
    int newest = -1;
    for (int i = 0; i < kSlots; ++i) {
        const uint64_t f = m_slots[i].frame;
        if (f && f <= finished && (newest < 0 || f > m_slots[newest].frame)) newest = i;
    }
    if (newest < 0) return 0;
    if (srv) *srv = m_slots[newest].delta.view;
    if (ww) *ww = m_slots[newest].delta.w;
    if (wh) *wh = m_slots[newest].delta.h;
    return m_slots[newest].frame;
}

// The result is already finished when a compose reads it, so this wait passes at once; it is there to make the model's writes visible to D3D11.
void Bridge::BeginDeltaUse(uint64_t d) { if (m_ctx4 && m_finished.d3d11) m_ctx4->Wait(m_finished.d3d11, d); }
void Bridge::EndDeltaUse(uint64_t d) {
    const int s = FindSlot(d);
    if (s < 0 || !m_ctx4 || !m_released.d3d11) return;
    m_ctx4->Signal(m_released.d3d11, ++m_releaseCount);
    m_slots[s].releasedAt = m_releaseCount;
}
