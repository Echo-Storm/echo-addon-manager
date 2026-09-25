#include "engine/sr_engine.h"
#include <d3dcompiler.h>
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <vector>
#include "nvsdk_ngx.h"
#include "nvsdk_ngx_defs.h"
#include "nvsdk_ngx_params.h"
#include "ffx_api/ffx_api.h"
#include "ffx_api/ffx_upscale.h"
#include "ffx_api/ffx_api_loader.h"
#include "ffx_api/dx12/ffx_api_dx12.h"

struct SrEngine::FfxState { HMODULE module = nullptr; ffxFunctions fn{}; ffxContext context = nullptr; };

namespace {

constexpr unsigned long long kAppId = 0x24480452ull;   // the upscaler's NGX application id (Neural Rendering's is ...451)
constexpr DWORD kSlotWaitMs = 500, kIdleWaitMs = 5000;

template <class T> void SafeRelease(T*& p) { if (p) { p->Release(); p = nullptr; } }

// Frame generation's flow (xy: from this frame to the one before, in units of 1/flowUnit of a flow pixel) as DLSS motion vectors in pixels of
// the game's frame: where each pixel was in the presented frame before, a fraction of a real frame ago. Zero without flow.
const char* kMotionHlsl = R"(
Texture2D<float4> tFlow : register(t0);
RWTexture2D<float2> uMotion : register(u0);
SamplerState sLinear : register(s0);
cbuffer C : register(b0) { uint2 size; float scale; uint hasFlow; };
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= size.x || id.y >= size.y) return;
    const float2 uv = (float2(id.xy) + 0.5) / float2(size);
    uMotion[id.xy] = hasFlow != 0 ? tFlow.SampleLevel(sLinear, uv, 0).xy * scale : float2(0, 0);
}
)";
struct MotionConstants { uint32_t w, h; float scale; uint32_t hasFlow; };

// Contrast-adaptive sharpening of DLSS's picture (the AMD FidelityFX CAS formula, MIT; as Neural Rendering's compose uses it): the weight
// shrinks where a channel is already near 0 or 1, so detail is sharpened without clipping.
const char* kSharpenHlsl = R"(
Texture2D<float4> tIn : register(t0);
RWTexture2D<float4> uOut : register(u0);
cbuffer C : register(b0) { uint2 size; float amount; uint unused; };
float3 Sharpen(float3 n, float3 w, float3 c, float3 e, float3 s, float a) {
    const float3 lo = min(min(min(w, c), min(e, n)), s);
    const float3 hi = max(max(max(w, c), max(e, n)), s);
    const float3 room = sqrt(saturate(min(lo, 1.0 - hi) / max(hi, 1e-4)));
    const float3 k = room * (-1.0 / lerp(8.0, 5.0, a));
    return saturate((n * k + w * k + e * k + s * k + c) / (1.0 + 4.0 * k));
}
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= size.x || id.y >= size.y) return;
    const int2 p = int2(id.xy), last = int2(size) - 1;
    const float4 c = tIn[p];
    uOut[p] = float4(Sharpen(tIn[clamp(p + int2(0, -1), 0, last)].rgb, tIn[clamp(p + int2(-1, 0), 0, last)].rgb, c.rgb,
                             tIn[clamp(p + int2(1, 0), 0, last)].rgb, tIn[clamp(p + int2(0, 1), 0, last)].rgb, saturate(amount)), c.a);
}
)";
struct SharpenConstants { uint32_t w, h; float amount; uint32_t unused; };

const char* ResultName(NVSDK_NGX_Result r) {
    switch (r) {
    case NVSDK_NGX_Result_Success: return "Success";
    case NVSDK_NGX_Result_FAIL_FeatureNotSupported: return "FeatureNotSupported";
    case NVSDK_NGX_Result_FAIL_PlatformError: return "PlatformError";
    case NVSDK_NGX_Result_FAIL_FeatureNotFound: return "FeatureNotFound (nvngx_dlss.dll missing?)";
    case NVSDK_NGX_Result_FAIL_InvalidParameter: return "InvalidParameter";
    case NVSDK_NGX_Result_FAIL_NotInitialized: return "NotInitialized";
    case NVSDK_NGX_Result_FAIL_UnsupportedInputFormat: return "UnsupportedInputFormat";
    case NVSDK_NGX_Result_FAIL_RWFlagMissing: return "RWFlagMissing";
    case NVSDK_NGX_Result_FAIL_OutOfDate: return "OutOfDate (update the NVIDIA driver)";
    default: return "error";
    }
}

// AMD's runtime reports problems through a callback without a user pointer: the engine that loaded it sets where they go.
std::function<void(const char*)> g_ffxLog;
void FfxMessage(uint32_t type, const wchar_t* message) {
    if (!g_ffxLog || !message) return;
    char text[512]; WideCharToMultiByte(CP_UTF8, 0, message, -1, text, sizeof text, nullptr, nullptr);
    char line[560]; snprintf(line, sizeof line, "FSR 3 %s: %s", type == FFX_API_MESSAGE_TYPE_ERROR ? "error" : "warning", text);
    g_ffxLog(line);
}

} // namespace

bool SrEngine::HasFeature() const { return m_backend == Backend::Fsr ? (m_ffx && m_ffx->context) : m_feature != nullptr; }

void SrEngine::Log(const char* fmt, ...) {
    if (!m_log) return;
    char text[512]; va_list a; va_start(a, fmt); vsnprintf(text, sizeof text, fmt, a); va_end(a);
    m_log(text);
}

void SrEngine::Fail(const char* fmt, ...) {
    char text[256]; va_list a; va_start(a, fmt); vsnprintf(text, sizeof text, fmt, a); va_end(a);
    m_error = text; m_failed = true; m_ready = false;
    Log("%s upscaler FAILED: %s", Name(), text);
}

// ---- starting and stopping

bool SrEngine::Init(const LUID& card, const std::wstring& dataPath, const std::wstring& runtimeDir, LogFn log, Backend backend) {
    m_log = std::move(log); m_failed = false; m_error.clear(); m_backend = backend;
    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) { Fail("CreateDXGIFactory1"); return false; }
    IDXGIAdapter1* adapter = nullptr;
    for (UINT i = 0; !adapter; ++i) {
        IDXGIAdapter1* a = nullptr;
        if (factory->EnumAdapters1(i, &a) == DXGI_ERROR_NOT_FOUND) break;
        DXGI_ADAPTER_DESC1 desc; a->GetDesc1(&desc);
        if (desc.AdapterLuid.LowPart == card.LowPart && desc.AdapterLuid.HighPart == card.HighPart) adapter = a; else a->Release();
    }
    factory->Release();
    if (!adapter) { Fail("no graphics card with LUID %08x:%08x", card.HighPart, card.LowPart); return false; }
    const HRESULT hr = D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_dev));
    adapter->Release();
    if (FAILED(hr)) { Fail("D3D12CreateDevice 0x%08x", (unsigned)hr); return false; }

    D3D12_COMMAND_QUEUE_DESC queue{}; queue.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (FAILED(m_dev->CreateCommandQueue(&queue, IID_PPV_ARGS(&m_queue)))) { Fail("CreateCommandQueue"); return false; }
    for (auto*& a : m_alloc) if (FAILED(m_dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&a)))) { Fail("CreateCommandAllocator"); return false; }
    if (FAILED(m_dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_alloc[0], nullptr, IID_PPV_ARGS(&m_list)))) { Fail("CreateCommandList"); return false; }
    m_list->Close();
    if (FAILED(m_dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence))) || !(m_event = CreateEventW(nullptr, FALSE, FALSE, nullptr))) { Fail("CreateFence"); return false; }
    D3D12_QUERY_HEAP_DESC queries{}; queries.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP; queries.Count = 3 * kSlots;   // start, motion done, end
    m_dev->CreateQueryHeap(&queries, IID_PPV_ARGS(&m_timestamps));
    D3D12_HEAP_PROPERTIES readback{}; readback.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC buffer{}; buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; buffer.Width = 24 * kSlots; buffer.Height = 1; buffer.DepthOrArraySize = 1;
    buffer.MipLevels = 1; buffer.SampleDesc.Count = 1; buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    m_dev->CreateCommittedResource(&readback, D3D12_HEAP_FLAG_NONE, &buffer, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_timestampReadback));
    m_queue->GetTimestampFrequency(&m_timestampFreq);

    // the motion pass: t0 the flow, u0 the motion vectors, four constants, a linear sampler
    D3D12_DESCRIPTOR_RANGE srv{}; srv.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; srv.NumDescriptors = 1;
    D3D12_DESCRIPTOR_RANGE uav{}; uav.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV; uav.NumDescriptors = 1;
    D3D12_ROOT_PARAMETER params[3]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; params[0].DescriptorTable = { 1, &srv };
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; params[1].DescriptorTable = { 1, &uav };
    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS; params[2].Constants.Num32BitValues = 4;
    for (auto& p : params) p.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_STATIC_SAMPLER_DESC sampler{}; sampler.Filter = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT; sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    const D3D12_ROOT_SIGNATURE_DESC rootDesc{ 3, params, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_NONE };
    ID3DBlob* blob = nullptr; ID3DBlob* error = nullptr;
    if (FAILED(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error)) ||
        FAILED(m_dev->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&m_rootSig)))) {
        SafeRelease(blob); SafeRelease(error); Fail("the motion pass's root signature"); return false;
    }
    SafeRelease(blob);
    auto build = [&](const char* source, const char* name, ID3D12PipelineState** out) {
        ID3DBlob* code = nullptr; ID3DBlob* err = nullptr;
        if (FAILED(D3DCompile(source, strlen(source), name, nullptr, nullptr, "main", "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &err))) {
            Log("%s: %s", name, err ? static_cast<const char*>(err->GetBufferPointer()) : "?"); SafeRelease(err); Fail("the %s shader did not compile", name); return false;
        }
        D3D12_COMPUTE_PIPELINE_STATE_DESC pso{}; pso.pRootSignature = m_rootSig; pso.CS = { code->GetBufferPointer(), code->GetBufferSize() };
        const HRESULT h = m_dev->CreateComputePipelineState(&pso, IID_PPV_ARGS(out));
        SafeRelease(code);
        if (FAILED(h)) { Fail("the %s pipeline 0x%08x", name, (unsigned)h); return false; }
        return true;
    };
    if (!build(kMotionHlsl, "sr_motion", &m_motionPso) || !build(kSharpenHlsl, "sr_sharpen", &m_sharpenPso)) return false;
    static_assert(FlowEstimator::kSlots == kSlots, "the estimator reads its statistics back per engine slot");
    if (!m_estimator.Init(m_dev, [this](const char* m) { Log("%s", m); })) Log("%s upscaler: the motion estimator could not start; motion comes from frame generation only", Name());
    m_estimator.SetTimestampFrequency(m_timestampFreq);
    // per allocator slot: the flow's view, the motion vectors', and the sharpening pass's input and output
    D3D12_DESCRIPTOR_HEAP_DESC heap{}; heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; heap.NumDescriptors = 4 * kSlots; heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(m_dev->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&m_heap)))) { Fail("CreateDescriptorHeap"); return false; }
    m_descriptorSize = m_dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    if (m_backend == Backend::Fsr) {   // AMD's runtime from the addon's fsr folder (signed by AMD; loaded by its full path only)
        m_ffx = new FfxState;
        const std::wstring path = runtimeDir + L"\\amd_fidelityfx_dx12.dll";
        m_ffx->module = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!m_ffx->module) { Fail("AMD's FSR 3 runtime could not be loaded from %ls (error %lu)", path.c_str(), GetLastError()); return false; }
        ffxLoadFunctions(&m_ffx->fn, m_ffx->module);
        if (!m_ffx->fn.CreateContext || !m_ffx->fn.DestroyContext || !m_ffx->fn.Dispatch) { Fail("AMD's FSR 3 runtime lacks the FidelityFX API"); return false; }
        g_ffxLog = [this](const char* m) { Log("%s", m); };
        m_ready = true;
        Log("FSR 3 upscaler ready on its own D3D12 device (runtime from %ls)", runtimeDir.c_str());
        return true;
    }

    // NGX, with NVIDIA's runtime from the addon's dlss folder
    const wchar_t* paths[] = { runtimeDir.c_str() };
    NVSDK_NGX_FeatureCommonInfo info{}; info.PathListInfo.Path = paths; info.PathListInfo.Length = 1;
    NVSDK_NGX_Result r = NVSDK_NGX_D3D12_Init(kAppId, dataPath.c_str(), m_dev, &info);
    if (NVSDK_NGX_FAILED(r)) { Fail("NGX Init: %s", ResultName(r)); return false; }
    NVSDK_NGX_Parameter* p = nullptr;
    if (NVSDK_NGX_FAILED(NVSDK_NGX_D3D12_AllocateParameters(&p)) || !p) { Fail("NGX parameters"); return false; }
    m_params = p;
    int available = 0;
    NVSDK_NGX_Parameter* caps = nullptr;
    if (NVSDK_NGX_SUCCEED(NVSDK_NGX_D3D12_GetCapabilityParameters(&caps)) && caps) { caps->Get(NVSDK_NGX_Parameter_SuperSampling_Available, &available); NVSDK_NGX_D3D12_DestroyParameters(caps); }
    if (!available) { Fail("DLSS Super Resolution is not available on this graphics card or driver"); return false; }
    m_ready = true;
    Log("DLSS upscaler ready on its own D3D12 device (runtime from %ls)", runtimeDir.c_str());
    return true;
}

void SrEngine::Shutdown() {
    if (m_queue) WaitIdle();
    if (m_ffx) {
        if (m_ffx->context) m_ffx->fn.DestroyContext(&m_ffx->context, nullptr);
        if (m_ffx->module) FreeLibrary(m_ffx->module);
        delete m_ffx; m_ffx = nullptr;
        g_ffxLog = nullptr;
    }
    if (m_feature) { NVSDK_NGX_D3D12_ReleaseFeature(static_cast<NVSDK_NGX_Handle*>(m_feature)); m_feature = nullptr; }
    if (m_params) { NVSDK_NGX_D3D12_DestroyParameters(static_cast<NVSDK_NGX_Parameter*>(m_params)); m_params = nullptr; }
    if (m_dev && m_backend == Backend::Dlss) NVSDK_NGX_D3D12_Shutdown1(m_dev);
    m_estimator.Shutdown(); m_estimatedLast = false; m_estimates = 0;
    SafeRelease(m_motion); SafeRelease(m_distrust); SafeRelease(m_depth); SafeRelease(m_depthUpload);
    SafeRelease(m_motionPso); SafeRelease(m_sharpenPso); SafeRelease(m_unsharpened); m_unsharpenedW = m_unsharpenedH = 0; SafeRelease(m_rootSig); SafeRelease(m_heap);
    SafeRelease(m_timestamps); SafeRelease(m_timestampReadback);
    SafeRelease(m_list); for (auto*& a : m_alloc) SafeRelease(a);
    SafeRelease(m_fence); if (m_event) { CloseHandle(m_event); m_event = nullptr; }
    SafeRelease(m_queue); SafeRelease(m_dev);
    for (auto& v : m_slotDone) v = 0;
    m_fenceValue = 0; m_nextSlot = 0; m_inW = m_inH = m_outW = m_outH = 0; m_preset = ~0u; m_ready = false;
}

ID3D12Resource* SrEngine::OpenSharedTexture(HANDLE h) {
    ID3D12Resource* r = nullptr;
    if (FAILED(m_dev->OpenSharedHandle(h, IID_PPV_ARGS(&r)))) Log("%s upscaler: a shared texture could not be opened", Name());
    return r;
}
ID3D12Fence* SrEngine::OpenSharedFence(HANDLE h) {
    ID3D12Fence* f = nullptr;
    if (FAILED(m_dev->OpenSharedHandle(h, IID_PPV_ARGS(&f)))) Log("%s upscaler: a shared fence could not be opened", Name());
    return f;
}

bool SrEngine::WaitIdle() {
    if (!m_queue || !m_fence) return true;
    m_queue->Signal(m_fence, ++m_fenceValue);
    if (m_fence->GetCompletedValue() < m_fenceValue) { m_fence->SetEventOnCompletion(m_fenceValue, m_event); WaitForSingleObject(m_event, kIdleWaitMs); }
    return m_fence->GetCompletedValue() >= m_fenceValue;
}
void SrEngine::Drain() { WaitIdle(); }

void SrEngine::Transition(ID3D12Resource* r, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to) {
    D3D12_RESOURCE_BARRIER b{}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition = { r, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, from, to };
    m_list->ResourceBarrier(1, &b);
}

int SrEngine::TakeSlot() {
    const int slot = m_nextSlot;
    if (m_slotDone[slot] && m_fence->GetCompletedValue() < m_slotDone[slot]) {
        m_fence->SetEventOnCompletion(m_slotDone[slot], m_event);
        WaitForSingleObject(m_event, kSlotWaitMs);
        if (m_fence->GetCompletedValue() < m_slotDone[slot]) return -1;   // still on the GPU: this frame is left to NIS
    }
    m_nextSlot = (slot + 1) % kSlots;
    m_alloc[slot]->Reset();
    return slot;
}

void SrEngine::ReadTime(int slot) {
    if (!m_slotDone[slot] || !m_timestampReadback) return;
    const D3D12_RANGE range{ static_cast<SIZE_T>(slot) * 24, static_cast<SIZE_T>(slot) * 24 + 24 };
    uint64_t* t = nullptr;
    if (FAILED(m_timestampReadback->Map(0, &range, reinterpret_cast<void**>(&t)))) return;
    const uint64_t t0 = t[slot * 3], t1 = t[slot * 3 + 1], t2 = t[slot * 3 + 2];
    const D3D12_RANGE none{ 0, 0 };
    m_timestampReadback->Unmap(0, &none);
    auto smooth = [](double& avg, double ms) { avg = avg == 0 ? ms : avg * 0.9 + ms * 0.1; };
    if (t2 > t0 && m_timestampFreq) smooth(m_gpuMs, (t2 - t0) * 1000.0 / m_timestampFreq);
    if (t1 >= t0 && t2 >= t1 && m_timestampFreq) smooth(m_motionMs, (t1 - t0) * 1000.0 / m_timestampFreq);
}

// ---- the feature and its inputs (on the caller's thread; our own device only)

bool SrEngine::EnsureInputs(uint32_t w, uint32_t h) {
    if (m_motion && m_inW == w && m_inH == h) return true;
    SafeRelease(m_motion); SafeRelease(m_distrust); SafeRelease(m_depth); SafeRelease(m_depthUpload);
    D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC d{}; d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; d.Width = w; d.Height = h; d.DepthOrArraySize = 1; d.MipLevels = 1; d.SampleDesc.Count = 1;
    d.Format = DXGI_FORMAT_R16G16_FLOAT; d.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    if (FAILED(m_dev->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&m_motion)))) return false;
    d.Format = DXGI_FORMAT_R8_UNORM;
    if (FAILED(m_dev->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&m_distrust)))) return false;
    d.Format = DXGI_FORMAT_R32_FLOAT; d.Flags = D3D12_RESOURCE_FLAG_NONE;
    if (FAILED(m_dev->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_depth)))) return false;
    // flat depth (DLSS reads it; Lossless Scaling has none), uploaded once
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout{}; UINT rows = 0; UINT64 rowBytes = 0, total = 0;
    m_dev->GetCopyableFootprints(&d, 0, 1, 0, &layout, &rows, &rowBytes, &total);
    D3D12_HEAP_PROPERTIES upload{}; upload.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC buf{}; buf.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; buf.Width = total; buf.Height = 1; buf.DepthOrArraySize = 1; buf.MipLevels = 1;
    buf.SampleDesc.Count = 1; buf.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(m_dev->CreateCommittedResource(&upload, D3D12_HEAP_FLAG_NONE, &buf, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_depthUpload)))) return false;
    uint8_t* mapped = nullptr;
    m_depthUpload->Map(0, nullptr, reinterpret_cast<void**>(&mapped));
    const std::vector<float> row(w, 0.5f);
    for (UINT y = 0; y < rows; ++y) memcpy(mapped + layout.Offset + y * layout.Footprint.RowPitch, row.data(), w * 4);
    m_depthUpload->Unmap(0, nullptr);
    D3D12_TEXTURE_COPY_LOCATION to{}; to.pResource = m_depth; to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION from{}; from.pResource = m_depthUpload; from.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; from.PlacedFootprint = layout;
    m_list->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
    Transition(m_depth, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    return true;
}

bool SrEngine::EnsureSharpenTarget(uint32_t w, uint32_t h, DXGI_FORMAT fmt) {
    if (m_unsharpened && m_unsharpenedW == w && m_unsharpenedH == h && m_unsharpenedFmt == fmt) return true;
    if (m_unsharpened) { WaitIdle(); SafeRelease(m_unsharpened); }
    D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC d{}; d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; d.Width = w; d.Height = h; d.DepthOrArraySize = 1; d.MipLevels = 1; d.SampleDesc.Count = 1;
    d.Format = fmt; d.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    if (FAILED(m_dev->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&m_unsharpened)))) return false;
    m_unsharpenedW = w; m_unsharpenedH = h; m_unsharpenedFmt = fmt;
    return true;
}

bool SrEngine::EnsureFeature(uint32_t inW, uint32_t inH, uint32_t outW, uint32_t outH, unsigned preset) {
    if (m_backend == Backend::Fsr) preset = 0;   // FSR has no models to choose
    if (HasFeature() && inW == m_inW && inH == m_inH && outW == m_outW && outH == m_outH && preset == m_preset) return true;
    LARGE_INTEGER f, a, b; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&a);
    if (!WaitIdle()) { Fail("the GPU did not finish the earlier work"); return false; }
    for (auto& v : m_slotDone) v = 0;
    if (m_feature) { NVSDK_NGX_D3D12_ReleaseFeature(static_cast<NVSDK_NGX_Handle*>(m_feature)); m_feature = nullptr; }
    if (m_ffx && m_ffx->context) { m_ffx->fn.DestroyContext(&m_ffx->context, nullptr); m_ffx->context = nullptr; }
    m_alloc[0]->Reset();
    m_list->Reset(m_alloc[0], nullptr);
    if (!EnsureInputs(inW, inH)) { m_list->Close(); Fail("the motion-vector and depth textures could not be made"); return false; }
    const float ratio = std::max(static_cast<float>(outW) / inW, static_cast<float>(outH) / inH);
    if (m_backend == Backend::Fsr) {   // the depth upload runs first; FSR's context needs no command list
        m_list->Close();
        ID3D12CommandList* upload[] = { m_list };
        m_queue->ExecuteCommandLists(1, upload);
        WaitIdle();
        SafeRelease(m_depthUpload);
        ffxCreateBackendDX12Desc backendDesc{}; backendDesc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12; backendDesc.device = m_dev;
        ffxCreateContextDescUpscale desc{}; desc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE; desc.header.pNext = &backendDesc.header;
        desc.flags = 0;   // LDR colour; motion at the game's size; no jitter, no inverted or infinite depth
        desc.maxRenderSize = { inW, inH }; desc.maxUpscaleSize = { outW, outH }; desc.fpMessage = FfxMessage;
        const ffxReturnCode_t rc = m_ffx->fn.CreateContext(&m_ffx->context, &desc.header, nullptr);
        QueryPerformanceCounter(&b);
        if (rc != FFX_API_RETURN_OK || !m_ffx->context) { m_ffx->context = nullptr; Fail("FSR 3 could not make its upscaling context (code %u)", rc); return false; }
        m_inW = inW; m_inH = inH; m_outW = outW; m_outH = outH; m_preset = preset;
        m_buildMs = (b.QuadPart - a.QuadPart) * 1000.0 / f.QuadPart;
        Log("FSR 3 upscaler: %ux%u -> %ux%u (x%.2f), made in %.0f ms", inW, inH, outW, outH, ratio, m_buildMs);
        return true;
    }
    auto* p = static_cast<NVSDK_NGX_Parameter*>(m_params);
    const NVSDK_NGX_PerfQuality_Value quality = ratio <= 1.01f ? NVSDK_NGX_PerfQuality_Value_DLAA   // the game at the screen's size: anti-aliasing only
                                              : ratio <= 1.55f ? NVSDK_NGX_PerfQuality_Value_MaxQuality : ratio <= 1.75f ? NVSDK_NGX_PerfQuality_Value_Balanced
                                              : ratio <= 2.2f ? NVSDK_NGX_PerfQuality_Value_MaxPerf : NVSDK_NGX_PerfQuality_Value_UltraPerformance;
    p->Set(NVSDK_NGX_Parameter_CreationNodeMask, 1u); p->Set(NVSDK_NGX_Parameter_VisibilityNodeMask, 1u);
    p->Set(NVSDK_NGX_Parameter_Width, inW); p->Set(NVSDK_NGX_Parameter_Height, inH);
    p->Set(NVSDK_NGX_Parameter_OutWidth, outW); p->Set(NVSDK_NGX_Parameter_OutHeight, outH);
    p->Set(NVSDK_NGX_Parameter_PerfQualityValue, static_cast<int>(quality));
    p->Set(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags, static_cast<int>(NVSDK_NGX_DLSS_Feature_Flags_MVLowRes));   // motion at the game's size
    p->Set(NVSDK_NGX_Parameter_DLSS_Enable_Output_Subrects, 0);
    for (const char* key : { NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_DLAA, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Quality,
                             NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Balanced, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Performance,
                             NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraPerformance, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraQuality })
        p->Set(key, preset);
    NVSDK_NGX_Handle* handle = nullptr;
    const NVSDK_NGX_Result r = NVSDK_NGX_D3D12_CreateFeature(m_list, NVSDK_NGX_Feature_SuperSampling, p, &handle);
    m_list->Close();
    ID3D12CommandList* lists[] = { m_list };
    m_queue->ExecuteCommandLists(1, lists);
    WaitIdle();
    SafeRelease(m_depthUpload);
    QueryPerformanceCounter(&b);
    if (NVSDK_NGX_FAILED(r) || !handle) { Fail("CreateFeature(DLSS): %s", ResultName(r)); return false; }
    m_feature = handle; m_inW = inW; m_inH = inH; m_outW = outW; m_outH = outH; m_preset = preset;
    m_buildMs = (b.QuadPart - a.QuadPart) * 1000.0 / f.QuadPart;
    Log("%s upscaler: %ux%u -> %ux%u (x%.2f), preset %u, made in %.0f ms", Name(), inW, inH, outW, outH, ratio, preset, m_buildMs);
    return true;
}

// ---- one frame

bool SrEngine::Run(ID3D12Resource* in, uint32_t inW, uint32_t inH, DXGI_FORMAT inFormat, ID3D12Resource* out, uint32_t outW, uint32_t outH, DXGI_FORMAT outFormat,
                   ID3D12Resource* flow, uint32_t flowW, uint32_t flowH, float flowUnit, float motionFraction, bool estimate, unsigned preset, float sharpen, bool reset,
                   ID3D12Fence* copied, uint64_t copiedValue, ID3D12Fence* done, uint64_t doneValue) {
    if (!m_ready) return false;
    const bool fsr = m_backend == Backend::Fsr;
    const bool fresh = !HasFeature() || inW != m_inW || inH != m_inH || outW != m_outW || outH != m_outH || (!fsr && preset != m_preset);
    if (!EnsureFeature(inW, inH, outW, outH, preset)) return false;
    // DLSS writes into a texture of ours and the sharpening pass from there into out; FSR sharpens by itself (its RCAS)
    const bool sharpening = !fsr && sharpen > 0.001f && EnsureSharpenTarget(outW, outH, outFormat);
    LARGE_INTEGER qpcNow, qpcFreq; QueryPerformanceCounter(&qpcNow); QueryPerformanceFrequency(&qpcFreq);
    const float frameMs = m_lastRunQpc ? std::clamp(static_cast<float>((qpcNow.QuadPart - m_lastRunQpc) * 1000.0 / qpcFreq.QuadPart), 1.0f, 100.0f) : 16.7f;
    m_lastRunQpc = qpcNow.QuadPart;
    bool estimating = estimate && m_estimator.IsReady();
    if (estimating && m_estimator.NeedsResize(inW, inH)) { WaitIdle(); estimating = m_estimator.Ensure(inW, inH); }
    if (estimating && !m_estimatedLast) m_estimator.Forget();   // its frame before is not the one before this
    m_estimatedLast = estimating;
    const int slot = TakeSlot();
    if (slot < 0) return false;
    ReadTime(slot);
    m_estimator.ReadStats(slot);

    // descriptors: the flow (or a null view) and the motion vectors
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = m_heap->GetCPUDescriptorHandleForHeapStart(); cpu.ptr += static_cast<SIZE_T>(slot) * 4 * m_descriptorSize;
    D3D12_GPU_DESCRIPTOR_HANDLE gpu = m_heap->GetGPUDescriptorHandleForHeapStart(); gpu.ptr += static_cast<UINT64>(slot) * 4 * m_descriptorSize;
    D3D12_SHADER_RESOURCE_VIEW_DESC sv{}; sv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT; sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; sv.Texture2D.MipLevels = 1;
    m_dev->CreateShaderResourceView(flow, &sv, cpu);
    D3D12_CPU_DESCRIPTOR_HANDLE cpuUav = cpu; cpuUav.ptr += m_descriptorSize;
    D3D12_UNORDERED_ACCESS_VIEW_DESC uv{}; uv.Format = DXGI_FORMAT_R16G16_FLOAT; uv.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    m_dev->CreateUnorderedAccessView(m_motion, nullptr, &uv, cpuUav);
    if (sharpening) {   // the sharpening pass: DLSS's picture in, the shared output out
        D3D12_CPU_DESCRIPTOR_HANDLE cpuIn = cpu; cpuIn.ptr += 2 * m_descriptorSize;
        D3D12_SHADER_RESOURCE_VIEW_DESC si{}; si.Format = outFormat; si.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        si.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; si.Texture2D.MipLevels = 1;
        m_dev->CreateShaderResourceView(m_unsharpened, &si, cpuIn);
        D3D12_CPU_DESCRIPTOR_HANDLE cpuOut = cpu; cpuOut.ptr += 3 * m_descriptorSize;
        D3D12_UNORDERED_ACCESS_VIEW_DESC uo{}; uo.Format = outFormat; uo.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        m_dev->CreateUnorderedAccessView(out, nullptr, &uo, cpuOut);
    }

    m_list->Reset(m_alloc[slot], nullptr);
    m_list->EndQuery(m_timestamps, D3D12_QUERY_TYPE_TIMESTAMP, slot * 3);
    // the shared textures come in COMMON
    Transition(in, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    if (flow) Transition(flow, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(out, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    // 1. motion vectors: measured from the frames, or frame generation's flow (or none)
    Transition(m_motion, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    ID3D12DescriptorHeap* heaps[] = { m_heap };
    if (estimating) {
        Transition(m_distrust, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        m_estimator.Record(m_list, slot, in, inFormat == DXGI_FORMAT_UNKNOWN ? DXGI_FORMAT_R8G8B8A8_UNORM : inFormat, m_motion, m_distrust);
        Transition(m_distrust, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    } else {
        m_list->SetDescriptorHeaps(1, heaps);
        m_list->SetComputeRootSignature(m_rootSig);
        m_list->SetPipelineState(m_motionPso);
        m_list->SetComputeRootDescriptorTable(0, gpu);
        D3D12_GPU_DESCRIPTOR_HANDLE gpuUav = gpu; gpuUav.ptr += m_descriptorSize;
        m_list->SetComputeRootDescriptorTable(1, gpuUav);
        const float unit = flowUnit > 0.1f ? flowUnit : 2.0f;
        const MotionConstants c{ inW, inH, flow && flowW ? static_cast<float>(inW) / (unit * flowW) * motionFraction : 0.0f, flow && flowW && flowH ? 1u : 0u };
        m_list->SetComputeRoot32BitConstants(2, 4, &c, 0);
        m_list->Dispatch((inW + 7) / 8, (inH + 7) / 8, 1);
    }
    Transition(m_motion, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    m_list->EndQuery(m_timestamps, D3D12_QUERY_TYPE_TIMESTAMP, slot * 3 + 1);

    // 2. the upscaler: FSR 3, or DLSS
    bool evaluated = true; char evalError[96] = {};
    if (fsr) {
        ffxDispatchDescUpscale d{}; d.header.type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE;
        d.commandList = m_list;
        d.color = ffxApiGetResourceDX12(in, FFX_API_RESOURCE_STATE_COMPUTE_READ);
        d.depth = ffxApiGetResourceDX12(m_depth, FFX_API_RESOURCE_STATE_COMPUTE_READ);
        d.motionVectors = ffxApiGetResourceDX12(m_motion, FFX_API_RESOURCE_STATE_COMPUTE_READ);
        d.reactive = ffxApiGetResourceDX12(estimating ? m_distrust : nullptr, FFX_API_RESOURCE_STATE_COMPUTE_READ);   // where the motion cannot be trusted
        d.output = ffxApiGetResourceDX12(out, FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
        d.jitterOffset = { 0.0f, 0.0f }; d.motionVectorScale = { 1.0f, 1.0f };   // vectors in the game's pixels
        d.renderSize = { inW, inH }; d.upscaleSize = { outW, outH };
        d.enableSharpening = sharpen > 0.001f; d.sharpness = std::clamp(sharpen, 0.0f, 1.0f);
        d.frameTimeDelta = frameMs; d.preExposure = 1.0f; d.reset = reset || fresh;
        d.cameraNear = 0.1f; d.cameraFar = 1000.0f; d.cameraFovAngleVertical = 1.0f; d.viewSpaceToMetersFactor = 1.0f;   // the depth is flat anyway
        d.flags = FFX_UPSCALE_FLAG_NON_LINEAR_COLOR_SRGB;   // the frame as the game shows it (gamma-encoded)
        const ffxReturnCode_t rc = m_ffx->fn.Dispatch(&m_ffx->context, &d.header);
        if (rc != FFX_API_RETURN_OK) { evaluated = false; snprintf(evalError, sizeof evalError, "FSR 3 dispatch failed (code %u)", rc); }
    }
    auto* p = static_cast<NVSDK_NGX_Parameter*>(m_params);
    if (!fsr) {
    p->Set(NVSDK_NGX_Parameter_Color, in);
    p->Set(NVSDK_NGX_Parameter_Output, sharpening ? m_unsharpened : out);
    p->Set(NVSDK_NGX_Parameter_Depth, m_depth);
    p->Set(NVSDK_NGX_Parameter_MotionVectors, m_motion);
    // where the measured motion cannot be trusted, DLSS leans on this frame instead of its history (none with frame generation's flow)
    p->Set(NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_Mask, estimating ? m_distrust : static_cast<ID3D12Resource*>(nullptr));
    p->Set(NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_SubrectBase_X, 0u); p->Set(NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_SubrectBase_Y, 0u);
    p->Set(NVSDK_NGX_Parameter_Jitter_Offset_X, 0.0f); p->Set(NVSDK_NGX_Parameter_Jitter_Offset_Y, 0.0f);
    p->Set(NVSDK_NGX_Parameter_MV_Scale_X, 1.0f); p->Set(NVSDK_NGX_Parameter_MV_Scale_Y, 1.0f);
    p->Set(NVSDK_NGX_Parameter_Reset, (reset || fresh) ? 1 : 0);
    p->Set(NVSDK_NGX_Parameter_Sharpness, 0.0f);
    p->Set(NVSDK_NGX_Parameter_DLSS_Pre_Exposure, 1.0f);
    p->Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width, inW);
    p->Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height, inH);
    const NVSDK_NGX_Result r = NVSDK_NGX_D3D12_EvaluateFeature_C(m_list, static_cast<NVSDK_NGX_Handle*>(m_feature), p, nullptr);
    if (NVSDK_NGX_FAILED(r)) { evaluated = false; snprintf(evalError, sizeof evalError, "EvaluateFeature(DLSS): %s", ResultName(r)); }
    }

    // 3. sharpening, from DLSS's picture into the shared output (DLSS leaves its own heap and root signature bound)
    if (sharpening) {
        Transition(m_unsharpened, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        m_list->SetDescriptorHeaps(1, heaps);
        m_list->SetComputeRootSignature(m_rootSig);
        m_list->SetPipelineState(m_sharpenPso);
        D3D12_GPU_DESCRIPTOR_HANDLE gpuIn = gpu; gpuIn.ptr += 2 * m_descriptorSize;
        D3D12_GPU_DESCRIPTOR_HANDLE gpuOut = gpu; gpuOut.ptr += 3 * m_descriptorSize;
        m_list->SetComputeRootDescriptorTable(0, gpuIn);
        m_list->SetComputeRootDescriptorTable(1, gpuOut);
        const SharpenConstants sc{ outW, outH, sharpen, 0 };
        m_list->SetComputeRoot32BitConstants(2, 4, &sc, 0);
        m_list->Dispatch((outW + 7) / 8, (outH + 7) / 8, 1);
        Transition(m_unsharpened, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }

    Transition(in, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
    if (flow) Transition(flow, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
    Transition(out, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
    m_list->EndQuery(m_timestamps, D3D12_QUERY_TYPE_TIMESTAMP, slot * 3 + 2);
    m_list->ResolveQueryData(m_timestamps, D3D12_QUERY_TYPE_TIMESTAMP, slot * 3, 3, m_timestampReadback, static_cast<UINT64>(slot) * 24);
    m_list->Close();

    // submitted even when DLSS failed, so "done" is always signalled for a queued run
    m_queue->Wait(copied, copiedValue);
    ID3D12CommandList* lists[] = { m_list };
    m_queue->ExecuteCommandLists(1, lists);
    m_queue->Signal(done, doneValue);
    m_queue->Signal(m_fence, ++m_fenceValue);
    m_slotDone[slot] = m_fenceValue;
    if (!evaluated) { Fail("%s", evalError); return false; }
    ++m_runs;
    if (estimating && (++m_estimates == 60 || m_estimates % 1200 == 0)) {   // what the estimate found (a check that it follows the picture)
        double x = 0, y = 0, length = 0, cost = 0, distrust = 0; uint64_t frames = 0;
        if (m_estimator.TakeAverages(x, y, length, cost, distrust, frames))
            Log("motion estimator: over %llu frames, average vector (%.2f, %.2f) px, average length %.2f px, match cost %.4f; motion %.2f ms of %.2f ms; "
                "the upscaler told to lean on the current frame over %.1f%% of the picture", (unsigned long long)frames, x, y, length, cost, m_motionMs, m_gpuMs, distrust * 100.0);
        double stage[4];
        if (m_estimator.TakeStageTimes(stage))
            Log("motion estimator: stages %.3f ms pyramid, %.3f search, %.3f median, %.3f every pixel", stage[0], stage[1], stage[2], stage[3]);
    }
    return true;
}
