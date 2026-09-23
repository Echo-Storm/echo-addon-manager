#include "engine/nr_engine.h"
#include "engine/nr_shaders.h"
#include <d3dcompiler.h>
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include "nvsdk_ngx.h"

namespace {

constexpr unsigned long long kAppId = 0x24480451ull;   // the application id NGX is started with (the same as the forwarder's)
constexpr DWORD kSlotWaitMs = 2000, kIdleWaitMs = 5000;
constexpr int kFloatGetterSlot = 14;                   // the parameter block's float getter (see nr_forwarder.cpp)
enum Pass { kShrink = 0, kMotion = 1, kDeltaPass = 2 };

// Root constants b0, twelve dwords, as the shaders declare them (nr_shaders.h).
struct PassConstants { uint32_t dstW, dstH, srcW, srcH; uint32_t flags; float flowScale; float smoothAmount; uint32_t pad[5]; };

const char* NgxResultName(int r) {
    switch (static_cast<NVSDK_NGX_Result>(r)) {
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

template <class T> void SafeRelease(T*& p) { if (p) { p->Release(); p = nullptr; } }

// NGX's log callback has no user pointer: it reaches the engine through this.
NrEngine* g_logTarget = nullptr;
void NVSDK_CONV OnNgxLog(const char* message, NVSDK_NGX_Logging_Level, NVSDK_NGX_Feature) {
    if (!g_logTarget || !message) return;
    std::string line(message);
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
    if (line.find("NGXLoadConfig") != std::string::npos || line.find("NGXLoadFromPath") != std::string::npos) return;   // noise on every start
    if (line.find("error") != std::string::npos || line.find("warning") != std::string::npos || line.find("dlssnr") != std::string::npos)
        g_logTarget->Log("[ngx] %s", line.c_str());
}

} // namespace

void NrEngine::Log(const char* fmt, ...) {
    if (!m_log) return;
    char text[1024]; va_list args; va_start(args, fmt); vsnprintf(text, sizeof text, fmt, args); va_end(args);
    m_log(text);
}

void NrEngine::Fail(const char* fmt, ...) {
    va_list args; va_start(args, fmt); vsnprintf(m_stats.lastError, sizeof m_stats.lastError, fmt, args); va_end(args);
    m_failed = true; m_ready = false;
    Log("NrEngine FAILED: %s", m_stats.lastError);
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Starting and stopping
// ---------------------------------------------------------------------------------------------------------------------------------

bool NrEngine::Init(const LUID& luid, const std::wstring& forwarderPath, const std::wstring& snippetPath, const std::wstring& dataPath,
                    const std::wstring& lsDir, LogFn log) {
    m_log = std::move(log); m_forwarderPath = forwarderPath; m_snippetPath = snippetPath; m_dataPath = dataPath; m_lsDir = lsDir;
    m_failed = m_ready = false;
    m_stats = NrStats{};
    g_logTarget = this;
    if (!CreateQueue(luid) || !StartNgx() || !StartForwarder() || !CreatePipelines()) return false;
    m_ready = true;
    Log("NrEngine ready (float slot %d)", m_stats.floatSlot);
    return true;
}

void NrEngine::Shutdown() {
    const bool idle = !m_queue || WaitIdle();
    ReleaseScratch();
    if (m_caps) { NVSDK_NGX_D3D12_Shutdown1(m_dev); m_caps = nullptr; }
    if (m_forwarder) { FreeLibrary(m_forwarder); m_forwarder = nullptr; }
    if (idle) { for (ID3D12Resource* r : m_uploads) r->Release(); m_uploads.clear(); }   // otherwise the GPU may still read them: leak rather than crash
    for (ID3D12PipelineState** p : { &m_psoShrink, &m_psoMotion, &m_psoDelta, &m_psoDeltaSmooth }) SafeRelease(*p);
    SafeRelease(m_rootSig); SafeRelease(m_heap); SafeRelease(m_timestamps); SafeRelease(m_timestampReadback); SafeRelease(m_list);
    for (ID3D12CommandAllocator*& a : m_alloc) SafeRelease(a);
    for (uint64_t& v : m_slotDone) v = 0;
    SafeRelease(m_fence);
    if (m_fenceEvent) { CloseHandle(m_fenceEvent); m_fenceEvent = nullptr; }
    SafeRelease(m_queue); SafeRelease(m_dev);
    m_fenceValue = 0; m_nextSlot = 0; m_flow = nullptr; m_flowW = m_flowH = 0;
    m_ready = false;
    if (g_logTarget == this) g_logTarget = nullptr;
}

bool NrEngine::CreateQueue(const LUID& luid) {
    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) { Fail("CreateDXGIFactory1"); return false; }
    IDXGIAdapter1* adapter = nullptr;
    for (UINT i = 0; !adapter; ++i) {
        IDXGIAdapter1* a = nullptr;
        if (factory->EnumAdapters1(i, &a) == DXGI_ERROR_NOT_FOUND) break;
        DXGI_ADAPTER_DESC1 desc; a->GetDesc1(&desc);
        if (desc.AdapterLuid.LowPart == luid.LowPart && desc.AdapterLuid.HighPart == luid.HighPart) adapter = a; else a->Release();
    }
    factory->Release();
    if (!adapter) { Fail("no graphics card with LUID %08x:%08x", luid.HighPart, luid.LowPart); return false; }
    const HRESULT hr = D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_dev));
    adapter->Release();
    if (FAILED(hr)) { Fail("D3D12CreateDevice 0x%08x", (unsigned)hr); return false; }

    D3D12_COMMAND_QUEUE_DESC queue{}; queue.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (FAILED(m_dev->CreateCommandQueue(&queue, IID_PPV_ARGS(&m_queue)))) { Fail("CreateCommandQueue"); return false; }
    for (ID3D12CommandAllocator*& a : m_alloc)
        if (FAILED(m_dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&a)))) { Fail("CreateCommandAllocator"); return false; }
    if (FAILED(m_dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_alloc[0], nullptr, IID_PPV_ARGS(&m_list)))) { Fail("CreateCommandList"); return false; }
    m_list->Close();
    if (FAILED(m_dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)))) { Fail("CreateFence"); return false; }
    m_fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);

    // four timestamps a run (start, model start, model end, end), read back from 32 bytes a slot
    D3D12_QUERY_HEAP_DESC queries{}; queries.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP; queries.Count = 4 * kSlots;
    m_dev->CreateQueryHeap(&queries, IID_PPV_ARGS(&m_timestamps));
    D3D12_HEAP_PROPERTIES readback{}; readback.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC buffer{}; buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; buffer.Width = 32 * kSlots; buffer.Height = 1;
    buffer.DepthOrArraySize = 1; buffer.MipLevels = 1; buffer.SampleDesc.Count = 1; buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    m_dev->CreateCommittedResource(&readback, D3D12_HEAP_FLAG_NONE, &buffer, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_timestampReadback));
    m_queue->GetTimestampFrequency(&m_timestampFreq);
    return true;
}

bool NrEngine::StartNgx() {
    const wchar_t* searchPaths[] = { m_lsDir.c_str(), m_dataPath.c_str() };
    NVSDK_NGX_FeatureCommonInfo info{};
    info.PathListInfo.Path = searchPaths; info.PathListInfo.Length = 2;
    info.LoggingInfo.LoggingCallback = OnNgxLog; info.LoggingInfo.MinimumLoggingLevel = NVSDK_NGX_LOGGING_LEVEL_ON;
    info.LoggingInfo.DisableOtherLoggingSinks = false;
    NVSDK_NGX_Result r = NVSDK_NGX_D3D12_Init(kAppId, m_dataPath.c_str(), m_dev, &info, NVSDK_NGX_Version_API);
    if (NVSDK_NGX_FAILED(r)) { Fail("NGX core Init: %s", NgxResultName(r)); return false; }
    NVSDK_NGX_Parameter* caps = nullptr;
    r = NVSDK_NGX_D3D12_GetCapabilityParameters(&caps);
    if (NVSDK_NGX_FAILED(r) || !caps) { Fail("GetCapabilityParameters: %s", NgxResultName(r)); return false; }
    m_caps = caps;
    return true;
}

bool NrEngine::StartForwarder() {
    m_forwarder = LoadLibraryW(m_forwarderPath.c_str());
    if (!m_forwarder) { Fail("forwarder LoadLibrary %lu (%ls)", GetLastError(), m_forwarderPath.c_str()); return false; }
    struct Export { const char* name; void** fn; };
    const Export exports[] = {
        { "nrfwd_probe", (void**)&m_probe }, { "nrfwd_init", (void**)&m_init }, { "nrfwd_set_float_slot", (void**)&m_setFloatSlot },
        { "nrfwd_probe_float", (void**)&m_probeFloat }, { "nrfwd_get_float", (void**)&m_getFloat }, { "nrfwd_create", (void**)&m_create },
        { "nrfwd_evaluate", (void**)&m_evaluate }, { "nrfwd_release", (void**)&m_release }, { "nrfwd_last_result", (void**)&m_lastResult },
    };
    for (const Export& e : exports)
        if (!(*e.fn = reinterpret_cast<void*>(GetProcAddress(m_forwarder, e.name)))) { Fail("forwarder export %s missing", e.name); return false; }
    const int found = m_probe(m_snippetPath.c_str());
    if ((found & 0xF) != 0xF) { Fail("snippet probe 0x%x (%ls)", found, m_snippetPath.c_str()); return false; }
    if (!FindFloatSlot()) return false;
    const int r = m_init(m_snippetPath.c_str(), m_dataPath.c_str(), m_dev, m_caps);
    if (r != 1) { Fail("snippet Init_Ext: %s", NgxResultName(r)); return false; }
    return true;
}

// The model reads floats through getter slot 14: the right setter is the one whose value comes back there.
bool NrEngine::FindFloatSlot() {
    for (const int slot : { 6, 5, 1, 2, 4, 7 }) {
        m_probeFloat(m_caps, "NR.Probe", 1.5f, slot);
        alignas(8) uint8_t bytes[8] = {};
        const int got = m_getFloat(m_caps, "NR.Probe", bytes, kFloatGetterSlot);
        m_probeFloat(m_caps, "NR.Probe", 0.0f, slot);
        float value; memcpy(&value, bytes, sizeof value);
        if (got == 1 && std::fabs(value - 1.5f) < 1e-6f) { m_setFloatSlot(slot); m_stats.floatSlot = slot; return true; }
    }
    Fail("no float setter slot round-trips through getter 14");
    return false;
}

// One root signature for the passes of our own: a table t0..t3, a table u0..u1, twelve root constants (b0) and a linear clamp sampler (s0).
bool NrEngine::CreatePipelines() {
    D3D12_DESCRIPTOR_RANGE srvs{}; srvs.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; srvs.NumDescriptors = 4;
    D3D12_DESCRIPTOR_RANGE uavs{}; uavs.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV; uavs.NumDescriptors = 2;
    D3D12_ROOT_PARAMETER params[3]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; params[0].DescriptorTable = { 1, &srvs };
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; params[1].DescriptorTable = { 1, &uavs };
    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS; params[2].Constants.Num32BitValues = sizeof(PassConstants) / 4;
    for (D3D12_ROOT_PARAMETER& p : params) p.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT; sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    D3D12_ROOT_SIGNATURE_DESC desc{ 3, params, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_NONE };
    ID3DBlob* blob = nullptr, * error = nullptr;
    if (FAILED(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error))) {
        Fail("root signature: %s", error ? static_cast<const char*>(error->GetBufferPointer()) : "?"); SafeRelease(error); return false;
    }
    HRESULT hr = m_dev->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&m_rootSig));
    blob->Release();
    if (FAILED(hr)) { Fail("CreateRootSignature 0x%08x", (unsigned)hr); return false; }

    auto build = [&](const char* source, const char* entry, ID3D12PipelineState** pso) {
        ID3DBlob* code = nullptr, * err = nullptr;
        if (FAILED(D3DCompile(source, strlen(source), "nr_model", nullptr, nullptr, entry, "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &err))) {
            Fail("HLSL %s: %s", entry, err ? static_cast<const char*>(err->GetBufferPointer()) : "?"); SafeRelease(err); return false;
        }
        D3D12_COMPUTE_PIPELINE_STATE_DESC pd{}; pd.pRootSignature = m_rootSig; pd.CS = { code->GetBufferPointer(), code->GetBufferSize() };
        const HRESULT h = m_dev->CreateComputePipelineState(&pd, IID_PPV_ARGS(pso));
        code->Release();
        if (FAILED(h)) { Fail("PSO %s 0x%08x", entry, (unsigned)h); return false; }
        return true;
    };
    if (!build(kNrModelHlsl, "CSDown", &m_psoShrink) || !build(kNrModelHlsl, "CSFlowToMvec", &m_psoMotion) ||
        !build(kNrModelHlsl, "CSDelta", &m_psoDelta) || !build(kNrSmoothHlsl, "CSDeltaSmooth", &m_psoDeltaSmooth)) return false;

    // a block of descriptors per allocator slot, so a list still on the GPU never sees its descriptors rewritten
    D3D12_DESCRIPTOR_HEAP_DESC heap{};
    heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; heap.NumDescriptors = kDescriptorsPerSlot * kSlots; heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(m_dev->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&m_heap)))) { Fail("CreateDescriptorHeap"); return false; }
    m_descriptorSize = m_dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------------------------------------------------------------

ID3D12Resource* NrEngine::MakeTexture(uint32_t w, uint32_t h, DXGI_FORMAT fmt, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES state) {
    D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; desc.Width = w; desc.Height = h; desc.DepthOrArraySize = 1; desc.MipLevels = 1;
    desc.Format = fmt; desc.SampleDesc.Count = 1; desc.Flags = flags;
    ID3D12Resource* texture = nullptr;
    const HRESULT hr = m_dev->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr, IID_PPV_ARGS(&texture));
    if (FAILED(hr)) Log("a %ux%u texture (format %d) could not be made: 0x%08x", w, h, (int)fmt, (unsigned)hr);
    return texture;
}

void NrEngine::Transition(ID3D12Resource* r, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to) {
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition = { r, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, from, to };
    m_list->ResourceBarrier(1, &b);
}

void NrEngine::Upload(ID3D12Resource* texture, uint32_t bytesPerPixel, const void* pixels) {
    const D3D12_RESOURCE_DESC desc = texture->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout{}; UINT rows = 0; UINT64 rowBytes = 0, total = 0;
    m_dev->GetCopyableFootprints(&desc, 0, 1, 0, &layout, &rows, &rowBytes, &total);
    D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC buffer{}; buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; buffer.Width = total; buffer.Height = 1;
    buffer.DepthOrArraySize = 1; buffer.MipLevels = 1; buffer.SampleDesc.Count = 1; buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ID3D12Resource* staging = nullptr;
    if (FAILED(m_dev->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&staging)))) return;
    uint8_t* mapped = nullptr;
    staging->Map(0, nullptr, reinterpret_cast<void**>(&mapped));
    const size_t srcPitch = static_cast<size_t>(desc.Width) * bytesPerPixel;
    for (UINT y = 0; y < rows; ++y) memcpy(mapped + layout.Offset + y * layout.Footprint.RowPitch, static_cast<const uint8_t*>(pixels) + y * srcPitch, srcPitch);
    staging->Unmap(0, nullptr);
    D3D12_TEXTURE_COPY_LOCATION to{}; to.pResource = texture; to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION from{}; from.pResource = staging; from.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; from.PlacedFootprint = layout;
    m_list->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
    Transition(texture, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    m_uploads.push_back(staging);
}

bool NrEngine::WaitIdle() {
    if (!m_queue || !m_fence) return true;
    m_queue->Signal(m_fence, ++m_fenceValue);
    if (m_fence->GetCompletedValue() < m_fenceValue) { m_fence->SetEventOnCompletion(m_fenceValue, m_fenceEvent); WaitForSingleObject(m_fenceEvent, kIdleWaitMs); }
    if (m_fence->GetCompletedValue() < m_fenceValue) { Log("NrEngine: the GPU did not finish within %lu ms", kIdleWaitMs); return false; }
    for (ID3D12Resource* r : m_uploads) r->Release();
    m_uploads.clear();
    return true;
}

int NrEngine::TakeSlot() {
    const int slot = m_nextSlot;
    if (m_slotDone[slot] && m_fence->GetCompletedValue() < m_slotDone[slot]) {
        m_fence->SetEventOnCompletion(m_slotDone[slot], m_fenceEvent);
        WaitForSingleObject(m_fenceEvent, kSlotWaitMs);
        // still busy: resetting the allocator now would pull its commands from under the GPU, so this frame is not run
        if (m_fence->GetCompletedValue() < m_slotDone[slot]) return -1;
    }
    m_nextSlot = (m_nextSlot + 1) % kSlots;
    m_alloc[slot]->Reset();
    return slot;
}

// The times of this slot's previous run (finished: TakeSlot waited for it), and where it sat on the CPU's clock relative to its submission.
void NrEngine::ReadTimes(int slot) {
    if (!m_slotDone[slot]) return;
    const D3D12_RANGE range{ static_cast<SIZE_T>(slot) * 32, static_cast<SIZE_T>(slot) * 32 + 32 };
    uint64_t* mapped = nullptr;
    if (FAILED(m_timestampReadback->Map(0, &range, reinterpret_cast<void**>(&mapped)))) return;
    const uint64_t* t = mapped + slot * 4;
    const double msPerTick = 1000.0 / static_cast<double>(m_timestampFreq);
    m_stats.nrMs = static_cast<double>(t[2] - t[1]) * msPerTick;
    m_stats.totalMs = static_cast<double>(t[3] - t[0]) * msPerTick;
    uint64_t gpuNow = 0, cpuNow = 0; LARGE_INTEGER qpcFreq; QueryPerformanceFrequency(&qpcFreq);
    if (m_slotSubmitQpc[slot] && SUCCEEDED(m_queue->GetClockCalibration(&gpuNow, &cpuNow))) {
        const double sinceSubmitMs = static_cast<double>(static_cast<int64_t>(cpuNow) - m_slotSubmitQpc[slot]) * 1000.0 / static_cast<double>(qpcFreq.QuadPart);
        auto onCpuClock = [&](uint64_t gpu) { return sinceSubmitMs + (static_cast<double>(gpu) - static_cast<double>(gpuNow)) * msPerTick; };
        m_stats.startMs = onCpuClock(t[0]); m_stats.doneMs = onCpuClock(t[3]);
    }
    const D3D12_RANGE nothingWritten{ 0, 0 };
    m_timestampReadback->Unmap(0, &nothingWritten);
}

D3D12_CPU_DESCRIPTOR_HANDLE NrEngine::CpuDescriptor(int slot, int pass, int i) const {
    D3D12_CPU_DESCRIPTOR_HANDLE h = m_heap->GetCPUDescriptorHandleForHeapStart();
    h.ptr += static_cast<SIZE_T>(slot * kDescriptorsPerSlot + pass * kPassDescriptors + i) * m_descriptorSize;
    return h;
}
D3D12_GPU_DESCRIPTOR_HANDLE NrEngine::GpuDescriptor(int slot, int pass, int i) const {
    D3D12_GPU_DESCRIPTOR_HANDLE h = m_heap->GetGPUDescriptorHandleForHeapStart();
    h.ptr += static_cast<UINT64>(slot * kDescriptorsPerSlot + pass * kPassDescriptors + i) * m_descriptorSize;
    return h;
}

ID3D12Resource* NrEngine::OpenSharedTexture(HANDLE h) {
    ID3D12Resource* r = nullptr;
    const HRESULT hr = m_dev->OpenSharedHandle(h, IID_PPV_ARGS(&r));
    if (FAILED(hr)) Log("NrEngine: a shared texture could not be opened: 0x%08x", (unsigned)hr);
    return r;
}
ID3D12Fence* NrEngine::OpenSharedFence(HANDLE h) {
    ID3D12Fence* f = nullptr;
    const HRESULT hr = m_dev->OpenSharedHandle(h, IID_PPV_ARGS(&f));
    if (FAILED(hr)) Log("NrEngine: a shared fence could not be opened: 0x%08x", (unsigned)hr);
    return f;
}

void NrEngine::SetFlowInput(ID3D12Resource* flow, uint32_t w, uint32_t h) {
    const bool changed = (flow != nullptr) != (m_flow != nullptr) || (flow && (w != m_flowW || h != m_flowH));
    if (changed) { if (flow) Log("flow input: LSFG flow %ux%u -> model motion vectors", w, h); else Log("flow input: none (zero motion vectors)"); }
    m_flow = flow; m_flowW = flow ? w : 0; m_flowH = flow ? h : 0;
    m_stats.hasFlow = flow != nullptr; m_stats.flowW = m_flowW; m_stats.flowH = m_flowH;
}

void NrEngine::ReleaseScratch() {
    if (m_feature) { WaitIdle(); m_release(m_feature); m_feature = nullptr; }
    for (ID3D12Resource** t : { &m_proxy, &m_out[0], &m_out[1], &m_depth, &m_mvec, &m_history[0], &m_history[1] }) SafeRelease(*t);
    m_historyValid = false; m_historyRead = 0;
    m_w = m_h = m_ww = m_wh = 0;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Prepare: the feature and the scratch textures
// ---------------------------------------------------------------------------------------------------------------------------------

bool NrEngine::Prepare(uint32_t w, uint32_t h, DXGI_FORMAT fmt, const NrParams& p) {
    if (!m_ready) return false;
    // the working size: the frame scaled by 0.25..1, rounded up to a multiple of 8, at least 64 (or the frame) and at most the frame
    const float scale = std::clamp(p.workingScale, 0.25f, 1.0f);
    auto workSize = [&](uint32_t full) {
        uint32_t s = (static_cast<uint32_t>(full * scale) + 7) & ~7u;
        s = std::min(s, full);
        return std::max(s, std::min(full, 64u));
    };
    const uint32_t ww = workSize(w), wh = workSize(h);
    if (m_feature && w == m_w && h == m_h && fmt == m_fmt && ww == m_ww && wh == m_wh) { m_params = p; return true; }   // the rest applies on the next run

    Log("Prepare: frame %ux%u (%s) -> model input %ux%u (%.2f MP), style %u intensity %.2f", w, h,
        fmt == DXGI_FORMAT_B8G8R8A8_UNORM ? "BGRA8" : fmt == DXGI_FORMAT_R8G8B8A8_UNORM ? "RGBA8" : "fmt?", ww, wh, ww * wh / 1e6, p.style, p.intensity);
    if (!WaitIdle()) { Fail("the GPU did not finish the earlier work"); return false; }
    ReleaseScratch();
    m_w = w; m_h = h; m_fmt = fmt; m_ww = ww; m_wh = wh; m_params = p;
    m_stats.workW = ww; m_stats.workH = wh;

    const auto writable = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    m_proxy = MakeTexture(ww, wh, DXGI_FORMAT_R8G8B8A8_UNORM, writable, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    for (ID3D12Resource*& out : m_out) out = MakeTexture(ww, wh, DXGI_FORMAT_R8G8B8A8_UNORM, writable, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    for (ID3D12Resource*& hist : m_history) hist = MakeTexture(ww, wh, DXGI_FORMAT_R16G16B16A16_FLOAT, writable, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    m_depth = MakeTexture(ww, wh, DXGI_FORMAT_R32_FLOAT, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
    m_mvec = MakeTexture(ww, wh, DXGI_FORMAT_R16G16_FLOAT, writable, D3D12_RESOURCE_STATE_COPY_DEST);
    if (!m_proxy || !m_out[0] || !m_out[1] || !m_history[0] || !m_history[1] || !m_depth || !m_mvec) { Fail("scratch allocation"); return false; }

    // flat depth (0.5; the model ignores it) and zero motion, uploaded once; then the feature, which records its own setup on the same list
    m_alloc[0]->Reset();
    m_list->Reset(m_alloc[0], nullptr);
    const std::vector<float> depth(static_cast<size_t>(ww) * wh, 0.5f);
    const std::vector<uint16_t> motion(static_cast<size_t>(ww) * wh * 2, 0);
    Upload(m_depth, 4, depth.data());
    Upload(m_mvec, 4, motion.data());
    NrCreateParams create{}; create.width = ww; create.height = wh; create.preset = 0; create.scalingRatio = 1.0f; create.tuning = p.Tuning();
    m_feature = m_create(m_list, m_caps, &create);
    m_list->Close();
    ID3D12CommandList* lists[] = { m_list };
    m_queue->ExecuteCommandLists(1, lists);
    WaitIdle();
    if (!m_feature) { Fail("CreateFeature(18): %s", NgxResultName(m_lastResult(1))); return false; }
    m_resetHistory = true;
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// One run
// ---------------------------------------------------------------------------------------------------------------------------------
//
// The descriptors of each pass, per allocator slot (t0..t3, then u0, u1; null where the pass reads or writes nothing):
//   shrink   t0 the frame                                                   u0 the proxy
//   motion   t3 LSFG's flow (null without)                                  u1 the motion vectors
//   delta    t0 the last smoothed delta (smoothing only), t1 the proxy,     u0 the shared delta, u1 the new smoothed delta (smoothing only)
//            t2 the model's last output, t3 the motion vectors

bool NrEngine::Run(ID3D12Resource* sharedIn, ID3D12Resource* sharedDelta, ID3D12Fence* waitFence, uint64_t waitValue, ID3D12Fence* usedFence,
                   uint64_t usedValue, ID3D12Fence* signalFence, uint64_t signalValue, bool reset) {
    if (!m_ready || !m_feature) return false;
    const int slot = TakeSlot();
    if (slot < 0) { Log("NrEngine: the GPU is still busy with a run from %d frames ago: this frame is skipped", kSlots); return false; }
    ReadTimes(slot);

    const int passes = std::clamp(static_cast<int>(m_params.passes), 1, 4);
    const bool smooth = m_params.deltaSmooth > 0.001f;
    const bool historyUsable = m_historyValid && !reset && !m_resetHistory;
    ID3D12Resource* const lastOut = m_out[(passes - 1) % 2];
    ID3D12Resource* const historyIn = m_history[m_historyRead];
    ID3D12Resource* const historyOut = m_history[1 - m_historyRead];

    // descriptors
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{}; srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D; srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels = 1;
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav{}; uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    auto readable = [&](int pass, int t, ID3D12Resource* r, DXGI_FORMAT f) { srv.Format = f; m_dev->CreateShaderResourceView(r, &srv, CpuDescriptor(slot, pass, t)); };
    auto writable = [&](int pass, int u, ID3D12Resource* r, DXGI_FORMAT f) { uav.Format = f; m_dev->CreateUnorderedAccessView(r, nullptr, &uav, CpuDescriptor(slot, pass, 4 + u)); };
    const DXGI_FORMAT rgba8 = DXGI_FORMAT_R8G8B8A8_UNORM, rgba16f = DXGI_FORMAT_R16G16B16A16_FLOAT, rg16f = DXGI_FORMAT_R16G16_FLOAT;
    readable(kShrink, 0, sharedIn, m_fmt); readable(kShrink, 1, nullptr, rgba8); readable(kShrink, 2, nullptr, rgba8); readable(kShrink, 3, nullptr, rgba16f);
    writable(kShrink, 0, m_proxy, rgba8); writable(kShrink, 1, nullptr, rg16f);
    readable(kMotion, 0, nullptr, rgba8); readable(kMotion, 1, nullptr, rgba8); readable(kMotion, 2, nullptr, rgba8); readable(kMotion, 3, m_flow, rgba16f);
    writable(kMotion, 0, nullptr, rgba16f); writable(kMotion, 1, m_mvec, rg16f);
    readable(kDeltaPass, 0, smooth ? historyIn : nullptr, rgba16f); readable(kDeltaPass, 1, m_proxy, rgba8); readable(kDeltaPass, 2, lastOut, rgba8);
    readable(kDeltaPass, 3, m_mvec, rg16f);
    writable(kDeltaPass, 0, sharedDelta, rgba16f); writable(kDeltaPass, 1, smooth ? historyOut : nullptr, rgba16f);

    m_list->Reset(m_alloc[slot], nullptr);
    const UINT q = slot * 4;
    m_list->EndQuery(m_timestamps, D3D12_QUERY_TYPE_TIMESTAMP, q);
    ID3D12DescriptorHeap* heaps[] = { m_heap };
    auto bindOurs = [&] { m_list->SetDescriptorHeaps(1, heaps); m_list->SetComputeRootSignature(m_rootSig); };
    auto dispatch = [&](ID3D12PipelineState* pso, int pass, const PassConstants& c) {
        m_list->SetPipelineState(pso);
        m_list->SetComputeRootDescriptorTable(0, GpuDescriptor(slot, pass, 0));
        m_list->SetComputeRootDescriptorTable(1, GpuDescriptor(slot, pass, 4));
        m_list->SetComputeRoot32BitConstants(2, sizeof c / 4, &c, 0);
        m_list->Dispatch((c.dstW + 7) / 8, (c.dstH + 7) / 8, 1);
    };
    bindOurs();

    // the shared textures come in COMMON
    Transition(sharedIn, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(sharedDelta, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    // 1. the frame, shrunk to the proxy
    Transition(m_proxy, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    dispatch(m_psoShrink, kShrink, PassConstants{ m_ww, m_wh, m_w, m_h });
    Transition(m_proxy, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    // 2. LSFG's flow as motion vectors in working-size pixels (zero without flow). One flow unit is W / (flowUnit * flow width) frame pixels
    //    (measured: units of a texture flowUnit times the flow's size), and a frame pixel is ww / W working pixels.
    {
        const bool withFlow = m_flow && m_params.useFlow && m_flowW && m_flowH;
        const float unit = m_params.flowUnit > 0.1f ? m_params.flowUnit : 2.0f;
        PassConstants c{ m_ww, m_wh, m_flowW, m_flowH, withFlow ? 1u : 0u };
        c.flowScale = withFlow ? static_cast<float>(m_ww) / (unit * static_cast<float>(m_flowW)) : 0.0f;
        Transition(m_mvec, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        dispatch(m_psoMotion, kMotion, c);
        Transition(m_mvec, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }

    // 3. the model: the proxy (with the motion vectors and flat depth) into m_out[0]; each further pass reads the last result and writes the
    //    other buffer. Both rest writable; the one a pass reads is made readable first.
    m_list->EndQuery(m_timestamps, D3D12_QUERY_TYPE_TIMESTAMP, q + 1);
    NrEvalParams e{};
    e.depth = m_depth; e.mvec = m_mvec; e.width = e.guideWidth = m_ww; e.height = e.guideHeight = m_wh;
    e.mvScaleX = e.mvScaleY = 1.0f; e.scalingRatio = 1.0f; e.tuning = m_params.Tuning();
    D3D12_RESOURCE_STATES outState[2] = { D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS };
    auto setOutState = [&](int i, D3D12_RESOURCE_STATES want) { if (outState[i] != want) { Transition(m_out[i], outState[i], want); outState[i] = want; } };
    int evalResult = 1;
    for (int k = 0; k < passes; ++k) {
        const int dst = k % 2;
        if (k > 0) setOutState(1 - dst, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        setOutState(dst, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        e.color = k == 0 ? m_proxy : m_out[1 - dst];
        e.output = m_out[dst];
        e.reset = k == 0 && (reset || m_resetHistory) ? 1u : 0u;   // the model's history starts afresh once, not at every pass
        const int r = m_evaluate(m_list, m_feature, m_caps, &e);
        if (r != 1 && evalResult == 1) evalResult = r;
    }
    m_resetHistory = false;
    m_list->EndQuery(m_timestamps, D3D12_QUERY_TYPE_TIMESTAMP, q + 2);
    bindOurs();   // the model leaves its own heap and root signature bound
    setOutState((passes - 1) % 2, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    // 4. the delta (model - proxy) into the shared texture; with smoothing, blended with the last one moved along the motion
    PassConstants c{ m_ww, m_wh, m_ww, m_wh };
    if (smooth) {
        c.flags = historyUsable ? 2u : 0u;
        c.smoothAmount = std::clamp(m_params.deltaSmooth, 0.0f, 0.95f);
        Transition(historyOut, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        dispatch(m_psoDeltaSmooth, kDeltaPass, c);
        Transition(historyOut, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        m_historyRead = 1 - m_historyRead; m_historyValid = true;
    } else {
        dispatch(m_psoDelta, kDeltaPass, c);
        m_historyValid = false;   // no history is kept while smoothing is off
    }

    setOutState(0, D3D12_RESOURCE_STATE_UNORDERED_ACCESS); setOutState(1, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Transition(sharedIn, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
    Transition(sharedDelta, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
    m_list->EndQuery(m_timestamps, D3D12_QUERY_TYPE_TIMESTAMP, q + 3);
    m_list->ResolveQueryData(m_timestamps, D3D12_QUERY_TYPE_TIMESTAMP, q, 4, m_timestampReadback, static_cast<UINT64>(slot) * 32);
    m_list->Close();

    if (evalResult != 1) { ++m_stats.fails; snprintf(m_stats.lastError, sizeof m_stats.lastError, "evaluate: %s", NgxResultName(evalResult)); }
    m_queue->Wait(waitFence, waitValue);
    if (usedFence) m_queue->Wait(usedFence, usedValue);
    ID3D12CommandList* lists[] = { m_list };
    m_queue->ExecuteCommandLists(1, lists);
    m_queue->Signal(signalFence, signalValue);
    m_queue->Signal(m_fence, ++m_fenceValue);
    m_slotDone[slot] = m_fenceValue;
    LARGE_INTEGER now; QueryPerformanceCounter(&now); m_slotSubmitQpc[slot] = now.QuadPart;
    ++m_stats.frames;
    return evalResult == 1;
}
