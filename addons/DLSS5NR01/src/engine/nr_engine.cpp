#include "engine/nr_engine.h"
#include "engine/nr_shaders.h"
#include <d3dcompiler.h>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cmath>
#include "nvsdk_ngx.h"

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

static const char* NgxName(int r) {
    switch ((NVSDK_NGX_Result)r) {
    case NVSDK_NGX_Result_Success: return "Success";
    case NVSDK_NGX_Result_FAIL_FeatureNotSupported: return "FeatureNotSupported";
    case NVSDK_NGX_Result_FAIL_PlatformError: return "PlatformError";
    case NVSDK_NGX_Result_FAIL_FeatureNotFound: return "FeatureNotFound";
    case NVSDK_NGX_Result_FAIL_InvalidParameter: return "InvalidParameter";
    case NVSDK_NGX_Result_FAIL_NotInitialized: return "NotInitialized";
    case NVSDK_NGX_Result_FAIL_UnsupportedInputFormat: return "UnsupportedInputFormat";
    case NVSDK_NGX_Result_FAIL_RWFlagMissing: return "RWFlagMissing";
    case NVSDK_NGX_Result_FAIL_MissingInput: return "MissingInput";
    case NVSDK_NGX_Result_FAIL_UnableToInitializeFeature: return "UnableToInitializeFeature";
    case NVSDK_NGX_Result_FAIL_OutOfDate: return "OutOfDate";
    case NVSDK_NGX_Result_FAIL_OutOfGPUMemory: return "OutOfGPUMemory";
    case NVSDK_NGX_Result_FAIL_UnsupportedFormat: return "UnsupportedFormat";
    case NVSDK_NGX_Result_FAIL_Denied: return "Denied";
    default: return "Fail";
    }
}

// root constants b0 (12 dwords), see nr_shaders.h
struct ModelCB { uint32_t dstW, dstH, srcW, srcH; uint32_t flags; float flowScale; float smoothAmt; uint32_t pad[5]; };

void NrEngine::Log(const char* fmt, ...) {
    char b[1024]; va_list a; va_start(a, fmt); vsnprintf(b, sizeof b, fmt, a); va_end(a);
    if (m_log) m_log(b);
}
void NrEngine::Fail(const char* fmt, ...) {
    va_list a; va_start(a, fmt); vsnprintf(m_stats.lastError, sizeof m_stats.lastError, fmt, a); va_end(a);
    m_failed = true; m_ready = false;
    Log("NrEngine FAILED: %s", m_stats.lastError);
}

static void NVSDK_CONV NgxLogCb(const char* msg, NVSDK_NGX_Logging_Level, NVSDK_NGX_Feature) {
    // routed through a static so the callback (C ABI, no user data) can reach the engine's logger
    extern NrEngine* g_nrEngineForLog; if (!g_nrEngineForLog || !msg) return;
    std::string s(msg); while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    if (s.find("NGXLoadConfig") != std::string::npos || s.find("NGXLoadFromPath") != std::string::npos) return;
    if (s.find("error") != std::string::npos || s.find("warning") != std::string::npos || s.find("dlssnr") != std::string::npos)
        g_nrEngineForLog->Log("[ngx] %s", s.c_str());
}
NrEngine* g_nrEngineForLog = nullptr;

// ------------------------------------------------------------------ lifecycle
bool NrEngine::Init(const LUID& luid, const std::wstring& forwarderPath, const std::wstring& snippetPath,
                    const std::wstring& dataPath, const std::wstring& lsDir, LogFn log) {
    m_log = log; m_forwarderPath = forwarderPath; m_snippetPath = snippetPath; m_dataPath = dataPath; m_lsDir = lsDir;
    m_failed = false; m_ready = false; memset(&m_stats, 0, sizeof m_stats); m_stats.floatSlot = -1;
    g_nrEngineForLog = this;
    if (!InitD3D12(luid)) return false;
    if (!InitNgx()) return false;
    if (!InitForwarder()) return false;
    if (!InitComposer()) return false;
    m_ready = true;
    Log("NrEngine ready (float slot %d)", m_stats.floatSlot);
    return true;
}

void NrEngine::Shutdown() {
    if (m_queue) WaitIdle();
    ReleaseFeatureAndScratch();
    if (m_caps) { NVSDK_NGX_D3D12_Shutdown1(m_dev); m_caps = nullptr; }
    if (m_fwd) { FreeLibrary(m_fwd); m_fwd = nullptr; }
    for (auto* g : m_garbage) g->Release(); m_garbage.clear();
    for (auto** p : { &m_psoDown, &m_psoFlow, &m_psoDelta, &m_psoDeltaSm }) { if (*p) (*p)->Release(); *p = nullptr; }
    if (m_rootSig) m_rootSig->Release(); if (m_heap) m_heap->Release(); m_rootSig = nullptr; m_heap = nullptr;
    if (m_qheap) m_qheap->Release(); if (m_qread) m_qread->Release(); m_qheap = nullptr; m_qread = nullptr;
    if (m_list) m_list->Release(); m_list = nullptr;
    for (auto& a : m_alloc) { if (a) a->Release(); a = nullptr; }
    for (auto& f : m_allocFence) f = 0;
    if (m_ownFence) m_ownFence->Release(); m_ownFence = nullptr;
    if (m_ownEvent) CloseHandle(m_ownEvent); m_ownEvent = nullptr;
    if (m_queue) m_queue->Release(); m_queue = nullptr;
    if (m_dev) m_dev->Release(); m_dev = nullptr;
    m_ownValue = 0; m_frameIndex = 0; m_flowIn = nullptr; m_flowW = m_flowH = 0;
    m_ready = false; if (g_nrEngineForLog == this) g_nrEngineForLog = nullptr;
}

bool NrEngine::InitD3D12(const LUID& luid) {
    IDXGIFactory1* fac = nullptr; if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&fac)))) { Fail("CreateDXGIFactory1"); return false; }
    IDXGIAdapter1* adapter = nullptr;
    for (UINT i = 0;; ++i) { IDXGIAdapter1* a = nullptr; if (fac->EnumAdapters1(i, &a) == DXGI_ERROR_NOT_FOUND) break;
        DXGI_ADAPTER_DESC1 d; a->GetDesc1(&d); if (d.AdapterLuid.LowPart == luid.LowPart && d.AdapterLuid.HighPart == luid.HighPart) { adapter = a; break; } a->Release(); }
    fac->Release();
    if (!adapter) { Fail("adapter LUID %08x:%08x not found", luid.HighPart, luid.LowPart); return false; }
    HRESULT hr = D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_dev)); adapter->Release();
    if (FAILED(hr)) { Fail("D3D12CreateDevice 0x%08x", hr); return false; }
    D3D12_COMMAND_QUEUE_DESC q{}; q.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (FAILED(m_dev->CreateCommandQueue(&q, IID_PPV_ARGS(&m_queue)))) { Fail("CreateCommandQueue"); return false; }
    for (int i = 0; i < kFrames; ++i)
        if (FAILED(m_dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_alloc[i])))) { Fail("CreateCommandAllocator"); return false; }
    if (FAILED(m_dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_alloc[0], nullptr, IID_PPV_ARGS(&m_list)))) { Fail("CreateCommandList"); return false; }
    m_list->Close();
    if (FAILED(m_dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_ownFence)))) { Fail("CreateFence"); return false; }
    m_ownEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    D3D12_QUERY_HEAP_DESC qd{}; qd.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP; qd.Count = 4 * kFrames;
    m_dev->CreateQueryHeap(&qd, IID_PPV_ARGS(&m_qheap));
    D3D12_HEAP_PROPERTIES hp{}; hp.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC bd{}; bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; bd.Width = 32 * kFrames; bd.Height = 1; bd.DepthOrArraySize = 1; bd.MipLevels = 1; bd.SampleDesc.Count = 1; bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    m_dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_qread));
    m_queue->GetTimestampFrequency(&m_tsFreq);
    return true;
}

bool NrEngine::InitNgx() {
    const wchar_t* paths[] = { m_lsDir.c_str(), m_dataPath.c_str() };
    NVSDK_NGX_FeatureCommonInfo ci{}; ci.PathListInfo.Path = paths; ci.PathListInfo.Length = 2;
    ci.LoggingInfo.LoggingCallback = NgxLogCb; ci.LoggingInfo.MinimumLoggingLevel = NVSDK_NGX_LOGGING_LEVEL_ON; ci.LoggingInfo.DisableOtherLoggingSinks = false;
    NVSDK_NGX_Result r = NVSDK_NGX_D3D12_Init(0x24480451ull, m_dataPath.c_str(), m_dev, &ci, NVSDK_NGX_Version_API);
    if (NVSDK_NGX_FAILED(r)) { Fail("NGX core Init: %s", NgxName(r)); return false; }
    NVSDK_NGX_Parameter* caps = nullptr; r = NVSDK_NGX_D3D12_GetCapabilityParameters(&caps);
    if (NVSDK_NGX_FAILED(r) || !caps) { Fail("GetCapabilityParameters: %s", NgxName(r)); return false; }
    m_caps = caps;
    return true;
}

bool NrEngine::InitForwarder() {
    m_fwd = LoadLibraryW(m_forwarderPath.c_str());
    if (!m_fwd) { Fail("forwarder LoadLibrary %lu (%ls)", GetLastError(), m_forwarderPath.c_str()); return false; }
#define GP(n, v) v = (decltype(v))GetProcAddress(m_fwd, n); if (!v) { Fail("forwarder export %s missing", n); return false; }
    GP("lspnr_probe", m_pProbe) GP("lspnr_init", m_pInit) GP("lspnr_set_float_slot", m_pSetFloatSlot) GP("lspnr_probe_float", m_pProbeFloat)
    GP("lspnr_get_float", m_pGetFloat) GP("lspnr_create", m_pCreate) GP("lspnr_evaluate", m_pEvaluate) GP("lspnr_release", m_pRelease) GP("lspnr_last_result", m_pLast)
#undef GP
    int bits = m_pProbe(m_snippetPath.c_str());
    if ((bits & 0xF) != 0xF) { Fail("snippet probe 0x%x (%ls)", bits, m_snippetPath.c_str()); return false; }
    if (!ProbeFloatSlot()) return false;
    int ir = m_pInit(m_snippetPath.c_str(), m_dataPath.c_str(), m_dev, m_caps);
    if (ir != 1) { Fail("snippet Init_Ext: %s", NgxName(ir)); return false; }
    return true;
}

// The snippet reads its floats through getter slot 14; only a setter whose value comes back there is right.
bool NrEngine::ProbeFloatSlot() {
    const int kGetter = 14;
    for (int s : { 6, 5, 1, 2, 4, 7 }) {
        m_pProbeFloat(m_caps, "LSPNR.Probe", 1.5f, s);
        alignas(8) uint8_t b[8] = {}; int g = m_pGetFloat(m_caps, "LSPNR.Probe", b, kGetter);
        float v; memcpy(&v, b, 4);
        m_pProbeFloat(m_caps, "LSPNR.Probe", 0.0f, s);
        if (g == 1 && fabsf(v - 1.5f) < 1e-6f) { m_pSetFloatSlot(s); m_stats.floatSlot = s; return true; }
    }
    Fail("no float setter slot round-trips through getter 14"); return false;
}

bool NrEngine::InitComposer() {
    // root signature: [0] SRV table t0-t3, [1] UAV table u0-u1, [2] root constants b0 (12 dwords), static sampler s0
    D3D12_DESCRIPTOR_RANGE srvRange{}; srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; srvRange.NumDescriptors = 4; srvRange.BaseShaderRegister = 0;
    D3D12_DESCRIPTOR_RANGE uavRange{}; uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV; uavRange.NumDescriptors = 2; uavRange.BaseShaderRegister = 0;
    D3D12_ROOT_PARAMETER rp[3]{};
    rp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; rp[0].DescriptorTable.NumDescriptorRanges = 1; rp[0].DescriptorTable.pDescriptorRanges = &srvRange; rp[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; rp[1].DescriptorTable.NumDescriptorRanges = 1; rp[1].DescriptorTable.pDescriptorRanges = &uavRange; rp[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rp[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS; rp[2].Constants.Num32BitValues = 12; rp[2].Constants.ShaderRegister = 0; rp[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_STATIC_SAMPLER_DESC ss{}; ss.Filter = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT; ss.AddressU = ss.AddressV = ss.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP; ss.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_ROOT_SIGNATURE_DESC rs{}; rs.NumParameters = 3; rs.pParameters = rp; rs.NumStaticSamplers = 1; rs.pStaticSamplers = &ss;
    ID3DBlob* sig = nullptr, * err = nullptr;
    if (FAILED(D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err))) { Fail("root signature: %s", err ? (char*)err->GetBufferPointer() : "?"); return false; }
    HRESULT hr = m_dev->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(&m_rootSig)); sig->Release();
    if (FAILED(hr)) { Fail("CreateRootSignature 0x%08x", hr); return false; }

    auto make = [&](const char* entry, ID3D12PipelineState** out, const char* src = kNrModelHlsl) -> bool {
        ID3DBlob* cs = nullptr, * e = nullptr;
        if (FAILED(D3DCompile(src, strlen(src), "nr_model", nullptr, nullptr, entry, "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &cs, &e))) {
            Fail("HLSL %s: %s", entry, e ? (char*)e->GetBufferPointer() : "?"); return false; }
        D3D12_COMPUTE_PIPELINE_STATE_DESC pd{}; pd.pRootSignature = m_rootSig; pd.CS = { cs->GetBufferPointer(), cs->GetBufferSize() };
        HRESULT h = m_dev->CreateComputePipelineState(&pd, IID_PPV_ARGS(out)); cs->Release();
        if (FAILED(h)) { Fail("PSO %s 0x%08x", entry, h); return false; }
        return true;
    };
    if (!make("CSDown", &m_psoDown) || !make("CSFlowToMvec", &m_psoFlow) || !make("CSDelta", &m_psoDelta) || !make("CSDeltaSmooth", &m_psoDeltaSm, kNrSmoothHlsl)) return false;

    // one descriptor block per allocator slot, so lists still in flight never see their descriptors overwritten
    D3D12_DESCRIPTOR_HEAP_DESC hd{}; hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; hd.NumDescriptors = kDescPerSlot * kFrames; hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(m_dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&m_heap)))) { Fail("CreateDescriptorHeap"); return false; }
    m_descSize = m_dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    return true;
}

// ------------------------------------------------------------------ helpers
ID3D12Resource* NrEngine::MakeTex(uint32_t w, uint32_t h, DXGI_FORMAT fmt, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES state) {
    D3D12_HEAP_PROPERTIES hp{}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC d{}; d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; d.Width = w; d.Height = h; d.DepthOrArraySize = 1; d.MipLevels = 1;
    d.Format = fmt; d.SampleDesc.Count = 1; d.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN; d.Flags = flags;
    ID3D12Resource* r = nullptr; HRESULT hr = m_dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, state, nullptr, IID_PPV_ARGS(&r));
    if (FAILED(hr)) Log("CreateCommittedResource %ux%u fmt %d failed 0x%08x", w, h, (int)fmt, hr);
    return r;
}
void NrEngine::Barrier(ID3D12GraphicsCommandList* list, ID3D12Resource* r, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER b{}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; b.Transition.pResource = r; b.Transition.StateBefore = before; b.Transition.StateAfter = after; b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    list->ResourceBarrier(1, &b);
}
int NrEngine::AcquireSlot() {
    int slot = m_frameIndex; m_frameIndex = (m_frameIndex + 1) % kFrames;
    if (m_allocFence[slot] && m_ownFence->GetCompletedValue() < m_allocFence[slot]) { m_ownFence->SetEventOnCompletion(m_allocFence[slot], m_ownEvent); WaitForSingleObject(m_ownEvent, 2000); }
    m_alloc[slot]->Reset();
    return slot;
}
void NrEngine::WaitIdle() {
    if (!m_queue || !m_ownFence) return;
    m_queue->Signal(m_ownFence, ++m_ownValue);
    if (m_ownFence->GetCompletedValue() < m_ownValue) { m_ownFence->SetEventOnCompletion(m_ownValue, m_ownEvent); WaitForSingleObject(m_ownEvent, 5000); }
    for (auto* g : m_garbage) g->Release(); m_garbage.clear();
}
void NrEngine::ReadTimestamps(int slot, double& aMs, double& bMs) {
    aMs = bMs = 0; if (!m_allocFence[slot]) return;   // this slot has not completed a pass yet
    uint64_t* ts = nullptr; D3D12_RANGE rr{ (SIZE_T)slot * 32, (SIZE_T)slot * 32 + 32 };
    if (SUCCEEDED(m_qread->Map(0, &rr, (void**)&ts))) {
        const uint64_t* t = ts + slot * 4;
        aMs = (double)(t[2] - t[1]) / m_tsFreq * 1000.0; bMs = (double)(t[3] - t[0]) / m_tsFreq * 1000.0;
        // GPU timestamps -> QPC through the queue's clock calibration, relative to when this slot was submitted
        uint64_t gpuNow = 0, cpuNow = 0; LARGE_INTEGER qf; QueryPerformanceFrequency(&qf);
        if (m_submitQpc[slot] && SUCCEEDED(m_queue->GetClockCalibration(&gpuNow, &cpuNow))) {
            auto toMs = [&](uint64_t g) { double dGpuMs = ((double)g - (double)gpuNow) / (double)m_tsFreq * 1000.0; double cpuMs = ((double)cpuNow - (double)m_submitQpc[slot]) / (double)qf.QuadPart * 1000.0; return cpuMs + dGpuMs; };
            m_stats.startMs = toMs(t[0]); m_stats.doneMs = toMs(t[3]);
        }
        D3D12_RANGE wr{ 0, 0 }; m_qread->Unmap(0, &wr);
    }
}
D3D12_CPU_DESCRIPTOR_HANDLE NrEngine::Cpu(int slot, int i) { auto h = m_heap->GetCPUDescriptorHandleForHeapStart(); h.ptr += (SIZE_T)(slot * kDescPerSlot + i) * m_descSize; return h; }
D3D12_GPU_DESCRIPTOR_HANDLE NrEngine::Gpu(int slot, int i) { auto h = m_heap->GetGPUDescriptorHandleForHeapStart(); h.ptr += (UINT64)(slot * kDescPerSlot + i) * m_descSize; return h; }
ID3D12Resource* NrEngine::OpenSharedTexture(HANDLE h) { ID3D12Resource* r = nullptr; HRESULT hr = m_dev->OpenSharedHandle(h, IID_PPV_ARGS(&r)); if (FAILED(hr)) Log("OpenSharedHandle(texture) 0x%08x", hr); return r; }
ID3D12Fence* NrEngine::OpenSharedFence(HANDLE h) { ID3D12Fence* f = nullptr; HRESULT hr = m_dev->OpenSharedHandle(h, IID_PPV_ARGS(&f)); if (FAILED(hr)) Log("OpenSharedHandle(fence) 0x%08x", hr); return f; }

void NrEngine::SetFlowInput(ID3D12Resource* flow, uint32_t w, uint32_t h) {
    if ((flow != nullptr) != (m_flowIn != nullptr) || (flow && (w != m_flowW || h != m_flowH))) {
        if (flow) Log("flow input: LSFG flow %ux%u -> model motion vectors", w, h); else Log("flow input: none (zero motion vectors)");
    }
    m_flowIn = flow; m_flowW = flow ? w : 0; m_flowH = flow ? h : 0;
    m_stats.hasFlow = flow != nullptr; m_stats.flowW = m_flowW; m_stats.flowH = m_flowH;
}

void NrEngine::ReleaseFeatureAndScratch() {
    if (m_feature) { WaitIdle(); m_pRelease(m_feature); m_feature = nullptr; }
    for (auto** t : { &m_proxy, &m_nrOut, &m_nrOut2, &m_depth, &m_mvec, &m_prev[0], &m_prev[1] }) { if (*t) { (*t)->Release(); *t = nullptr; } }
    m_prevValid = false; m_prevIdx = 0;
    m_w = m_h = m_ww = m_wh = 0;
}

void NrEngine::UploadTexture(ID3D12Resource* tex, uint32_t bpp, const void* data) {
    const uint32_t ww = m_ww;
    D3D12_RESOURCE_DESC d = tex->GetDesc(); D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{}; UINT rows = 0; UINT64 rowSize = 0, total = 0;
    m_dev->GetCopyableFootprints(&d, 0, 1, 0, &fp, &rows, &rowSize, &total);
    D3D12_HEAP_PROPERTIES hp{}; hp.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC bd{}; bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; bd.Width = total; bd.Height = 1; bd.DepthOrArraySize = 1; bd.MipLevels = 1; bd.SampleDesc.Count = 1; bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ID3D12Resource* up = nullptr; m_dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&up));
    if (!up) return;
    uint8_t* dst = nullptr; up->Map(0, nullptr, (void**)&dst);
    for (UINT y = 0; y < rows; ++y) memcpy(dst + fp.Offset + (size_t)y * fp.Footprint.RowPitch, (const uint8_t*)data + (size_t)y * ww * bpp, (size_t)ww * bpp);
    up->Unmap(0, nullptr);
    D3D12_TEXTURE_COPY_LOCATION dl{}; dl.pResource = tex; dl.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION sl{}; sl.pResource = up; sl.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; sl.PlacedFootprint = fp;
    m_list->CopyTextureRegion(&dl, 0, 0, 0, &sl, nullptr);
    Barrier(m_list, tex, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    m_garbage.push_back(up);
}

// ------------------------------------------------------------------ prepare
// Scratch "rest" states: proxy and mvec rest in NON_PIXEL_SHADER_RESOURCE (readers need no barrier, writers
// go UAV and back); nrOut rests in UNORDERED_ACCESS (the model writes it).
bool NrEngine::Prepare(uint32_t w, uint32_t h, DXGI_FORMAT fmt, const NrParams& p) {
    if (!m_ready) return false;
    float ws = p.workingScale < 0.25f ? 0.25f : p.workingScale > 1.0f ? 1.0f : p.workingScale;
    uint32_t ww = ((uint32_t)(w * ws) + 7) & ~7u, wh = ((uint32_t)(h * ws) + 7) & ~7u;
    if (ww > w) ww = w; if (wh > h) wh = h;
    if (ww < 64) ww = w < 64 ? w : 64; if (wh < 64) wh = h < 64 ? h : 64;
    if (w == m_w && h == m_h && fmt == m_fmt && ww == m_ww && wh == m_wh && m_feature) { m_params = p; return true; }   // tuning and composer settings apply on the next pass
    Log("Prepare: frame %ux%u (%s) -> model input %ux%u (%.2f MP), style %u intensity %.2f",
        w, h, fmt == DXGI_FORMAT_B8G8R8A8_UNORM ? "BGRA8" : fmt == DXGI_FORMAT_R8G8B8A8_UNORM ? "RGBA8" : "fmt?", ww, wh, ww * wh / 1e6, p.style, p.intensity);
    WaitIdle();
    ReleaseFeatureAndScratch();
    m_w = w; m_h = h; m_fmt = fmt; m_ww = ww; m_wh = wh; m_params = p;
    m_stats.workW = ww; m_stats.workH = wh;

    const DXGI_FORMAT kModelFmt = DXGI_FORMAT_R8G8B8A8_UNORM;
    m_proxy = MakeTex(ww, wh, kModelFmt, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    m_nrOut = MakeTex(ww, wh, kModelFmt, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    m_nrOut2 = MakeTex(ww, wh, kModelFmt, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    m_depth = MakeTex(ww, wh, DXGI_FORMAT_R32_FLOAT, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
    m_mvec  = MakeTex(ww, wh, DXGI_FORMAT_R16G16_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
    for (auto& pv : m_prev) pv = MakeTex(ww, wh, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    if (!m_proxy || !m_nrOut || !m_nrOut2 || !m_depth || !m_mvec || !m_prev[0] || !m_prev[1]) { Fail("scratch allocation"); return false; }

    // upload flat depth (0.5; the model ignores depth anyway) and zero motion vectors once
    m_alloc[0]->Reset(); m_list->Reset(m_alloc[0], nullptr);
    std::vector<float> depth((size_t)ww * wh, 0.5f); std::vector<uint16_t> mv((size_t)ww * wh * 2, 0);
    UploadTexture(m_depth, 4, depth.data()); UploadTexture(m_mvec, 4, mv.data());
    // model feature (records GPU work into this list)
    LspnrCreateParams cp{}; cp.width = ww; cp.height = wh; cp.preset = 0; cp.scalingRatio = 1.0f; cp.tuning = p.Tuning();
    m_feature = m_pCreate(m_list, m_caps, &cp);
    m_list->Close(); ID3D12CommandList* l[] = { m_list }; m_queue->ExecuteCommandLists(1, l);
    WaitIdle();
    if (!m_feature) { Fail("CreateFeature(18): %s", NgxName(m_pLast(1))); return false; }
    m_needReset = true;
    return true;
}

// ------------------------------------------------------------------ the run
// Descriptor block per slot:
//   [0] t0 frame  [1] t1 proxy  [2] t2 null   [3] t3 flow (null without)      [4] u0 proxy  [5] u0 delta(shared)  [6] u0 null  [7] u1 mvec
//   [8] t0 frame  [9] t1 proxy  [10] t2 nr    [11] t3 mvec                    [12] u0 null  [13] u1 null
bool NrEngine::Run(ID3D12Resource* sharedIn, ID3D12Resource* sharedDelta, ID3D12Fence* waitFence, uint64_t waitValue,
                   ID3D12Fence* usedFence, uint64_t usedValue, ID3D12Fence* signalFence, uint64_t signalValue, bool reset) {
    if (!m_ready || !m_feature) return false;
    const int passes = m_params.passes < 1 ? 1 : m_params.passes > 4 ? 4 : (int)m_params.passes;
    const bool smooth = m_params.deltaSmooth > 0.001f && m_prev[0] && m_prev[1];
    const bool histValid = m_prevValid && !reset && !m_needReset;   // the history restarts with the model's own
    ID3D12Resource* const finalRes = (passes % 2 == 1) ? m_nrOut : m_nrOut2;   // where the last pass writes
    int s = AcquireSlot();
    ReadTimestamps(s, m_stats.nrMs, m_stats.totalMs);
    m_list->Reset(m_alloc[s], nullptr);
    const int q0 = s * 4;
    m_list->EndQuery(m_qheap, D3D12_QUERY_TYPE_TIMESTAMP, q0 + 0);

    D3D12_SHADER_RESOURCE_VIEW_DESC sv{}; sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D; sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; sv.Texture2D.MipLevels = 1;
    D3D12_UNORDERED_ACCESS_VIEW_DESC uv{}; uv.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    sv.Format = m_fmt;                          for (int i : { 0, 8 }) m_dev->CreateShaderResourceView(sharedIn, &sv, Cpu(s, i));
    sv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;     for (int i : { 1, 9 }) m_dev->CreateShaderResourceView(m_proxy, &sv, Cpu(s, i));
                                                 m_dev->CreateShaderResourceView(finalRes, &sv, Cpu(s, 10));
                                                 m_dev->CreateShaderResourceView(nullptr, &sv, Cpu(s, 2));
    sv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT; m_dev->CreateShaderResourceView(m_flowIn, &sv, Cpu(s, 3));   // null descriptor when there is no flow
    sv.Format = DXGI_FORMAT_R16G16_FLOAT;       m_dev->CreateShaderResourceView(m_mvec, &sv, Cpu(s, 11));
    uv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;     m_dev->CreateUnorderedAccessView(m_proxy, nullptr, &uv, Cpu(s, 4));
    uv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT; m_dev->CreateUnorderedAccessView(sharedDelta, nullptr, &uv, Cpu(s, 5));
    uv.Format = DXGI_FORMAT_R16G16_FLOAT;       m_dev->CreateUnorderedAccessView(nullptr, nullptr, &uv, Cpu(s, 6));
                                                 m_dev->CreateUnorderedAccessView(m_mvec, nullptr, &uv, Cpu(s, 7));
                                                 m_dev->CreateUnorderedAccessView(nullptr, nullptr, &uv, Cpu(s, 12));
                                                 m_dev->CreateUnorderedAccessView(nullptr, nullptr, &uv, Cpu(s, 13));
    ID3D12Resource* const prevRead = m_prev[m_prevIdx], * const prevWrite = m_prev[1 - m_prevIdx];
    if (smooth) {   // the delta group swaps two descriptors: t0 = last delta (was the unused frame), u1 = the new history (was null)
        sv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT; m_dev->CreateShaderResourceView(prevRead, &sv, Cpu(s, 8));
        uv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT; m_dev->CreateUnorderedAccessView(prevWrite, nullptr, &uv, Cpu(s, 6));
    }

    ID3D12DescriptorHeap* heaps[] = { m_heap }; m_list->SetDescriptorHeaps(1, heaps);
    m_list->SetComputeRootSignature(m_rootSig);
    ModelCB cb{};
    auto dispatchWork = [&](ID3D12PipelineState* pso, int srvBase, int uavBase) {
        m_list->SetPipelineState(pso);
        m_list->SetComputeRootDescriptorTable(0, Gpu(s, srvBase)); m_list->SetComputeRootDescriptorTable(1, Gpu(s, uavBase)); m_list->SetComputeRoot32BitConstants(2, 12, &cb, 0);
        m_list->Dispatch((cb.dstW + 7) / 8, (cb.dstH + 7) / 8, 1);
    };

    // shared resources arrive in COMMON
    Barrier(m_list, sharedIn, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Barrier(m_list, sharedDelta, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    // 1. downsample frame -> proxy
    Barrier(m_list, m_proxy, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    cb = {}; cb.dstW = m_ww; cb.dstH = m_wh; cb.srcW = m_w; cb.srcH = m_h;
    dispatchWork(m_psoDown, 0, 4);
    Barrier(m_list, m_proxy, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    // 2. LSFG flow -> model motion vectors (zeros when there is none). 1 flow unit = W / (flowUnit * fw) frame px
    //    (measured: units of a texture flowUnit x the flow's size); work px = frame px * ww / W.
    {
        const bool have = m_flowIn && m_params.useFlow && m_flowW && m_flowH;
        const float fu = m_params.flowUnit > 0.1f ? m_params.flowUnit : 2.0f;
        Barrier(m_list, m_mvec, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        cb = {}; cb.dstW = m_ww; cb.dstH = m_wh; cb.srcW = m_flowW; cb.srcH = m_flowH; cb.flags = have ? 1u : 0u;
        cb.flowScale = have ? ((float)m_w / (fu * (float)m_flowW)) * ((float)m_ww / (float)m_w) : 0.0f;
        dispatchWork(m_psoFlow, 0, 6);
        Barrier(m_list, m_mvec, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }

    // 3. model: proxy (+ mvec, flat depth) -> nrOut. Extra passes feed each result back in as the colour input,
    //    ping-ponging between the two output buffers (both rest in UNORDERED_ACCESS; the colour input must be a
    //    shader resource, so the buffer that was just written is transitioned before it is read).
    m_list->EndQuery(m_qheap, D3D12_QUERY_TYPE_TIMESTAMP, q0 + 1);
    LspnrEvalParams ep{}; ep.depth = m_depth; ep.mvec = m_mvec;
    ep.width = m_ww; ep.height = m_wh; ep.guideWidth = m_ww; ep.guideHeight = m_wh; ep.depthInverted = 0;
    ep.mvScaleX = 1.0f; ep.mvScaleY = 1.0f; ep.scalingRatio = 1.0f; ep.controlMask = nullptr; ep.tuning = m_params.Tuning();
    int er = 1;
    D3D12_RESOURCE_STATES stA = D3D12_RESOURCE_STATE_UNORDERED_ACCESS, stB = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    auto setState = [&](ID3D12Resource* r, D3D12_RESOURCE_STATES want) {
        D3D12_RESOURCE_STATES& st = (r == m_nrOut) ? stA : stB;
        if (st != want) { Barrier(m_list, r, st, want); st = want; }
    };
    ID3D12Resource* src = m_proxy;
    for (int k = 0; k < passes; ++k) {
        ID3D12Resource* dst = (k % 2 == 0) ? m_nrOut : m_nrOut2;
        if (src != m_proxy) setState(src, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        setState(dst, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        ep.color = src; ep.output = dst;
        ep.reset = (k == 0 && (reset || m_needReset)) ? 1u : 0u;   // the model's history restarts once, not per pass
        const int r = m_pEvaluate(m_list, m_feature, m_caps, &ep);
        if (r != 1 && er == 1) er = r;
        src = dst;
    }
    m_needReset = false;
    m_list->EndQuery(m_qheap, D3D12_QUERY_TYPE_TIMESTAMP, q0 + 2);
    // the snippet restores nothing: re-bind our heap/root signature
    m_list->SetDescriptorHeaps(1, heaps); m_list->SetComputeRootSignature(m_rootSig);
    setState(finalRes, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    // 4. delta = model - proxy -> the shared work-res delta
    cb = {}; cb.dstW = m_ww; cb.dstH = m_wh; cb.srcW = m_ww; cb.srcH = m_wh;
    if (smooth) {
        cb.flags = histValid ? 2u : 0u; cb.smoothAmt = m_params.deltaSmooth < 0.0f ? 0.0f : m_params.deltaSmooth > 0.95f ? 0.95f : m_params.deltaSmooth;
        Barrier(m_list, prevWrite, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        dispatchWork(m_psoDeltaSm, 8, 5);
        Barrier(m_list, prevWrite, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        m_prevIdx = 1 - m_prevIdx; m_prevValid = true;
    } else {
        dispatchWork(m_psoDelta, 8, 5);
        m_prevValid = false;   // the history is not kept while smoothing is off
    }

    setState(m_nrOut, D3D12_RESOURCE_STATE_UNORDERED_ACCESS); setState(m_nrOut2, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);   // back to rest
    Barrier(m_list, sharedIn, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
    Barrier(m_list, sharedDelta, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
    m_list->EndQuery(m_qheap, D3D12_QUERY_TYPE_TIMESTAMP, q0 + 3);
    m_list->ResolveQueryData(m_qheap, D3D12_QUERY_TYPE_TIMESTAMP, q0, 4, m_qread, (UINT64)s * 32);
    m_list->Close();

    if (er != 1) { m_stats.fails++; snprintf(m_stats.lastError, sizeof m_stats.lastError, "evaluate: %s", NgxName(er)); }
    m_queue->Wait(waitFence, waitValue);
    if (usedFence) m_queue->Wait(usedFence, usedValue);
    ID3D12CommandList* l[] = { m_list }; m_queue->ExecuteCommandLists(1, l);
    m_queue->Signal(signalFence, signalValue);
    m_queue->Signal(m_ownFence, ++m_ownValue); m_allocFence[s] = m_ownValue;
    { LARGE_INTEGER q; QueryPerformanceCounter(&q); m_submitQpc[s] = q.QuadPart; }
    m_stats.frames++;
    return er == 1;
}
