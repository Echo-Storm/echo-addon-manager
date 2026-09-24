#include "addon/scaler11.h"
#include <d3dcompiler.h>
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <vector>
#include "nvsdk_ngx.h"
#include "nvsdk_ngx_defs.h"
#include "nvsdk_ngx_params.h"

namespace nr {

namespace {

constexpr unsigned long long kAppId = 0x24480452ull;   // this addon's NGX application id (Neural Rendering's is ...451)

template <class T> void SafeRelease(T*& p) { if (p) { p->Release(); p = nullptr; } }

// Frame generation's flow (xy: from this frame to the one before, in units of 1/flowUnit of a flow pixel) as DLSS motion vectors in pixels of
// the game's frame: where each pixel was in the presented frame before, a fraction of a real frame ago.
const char* kMotionHlsl = R"(
Texture2D<float4> tFlow : register(t0);
SamplerState sLinear : register(s0);
RWTexture2D<float2> uMotion : register(u0);
cbuffer C : register(b0) { uint2 size; float scale; uint hasFlow; };
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= size.x || id.y >= size.y) return;
    const float2 uv = (float2(id.xy) + 0.5) / float2(size);
    uMotion[id.xy] = hasFlow != 0 ? tFlow.SampleLevel(sLinear, uv, 0).xy * scale : float2(0, 0);
}
)";
struct MotionConstants { uint32_t w, h; float scale; uint32_t hasFlow; };

bool Texture2D(ID3D11Resource* r, D3D11_TEXTURE2D_DESC& desc) {
    if (!r) return false;
    D3D11_RESOURCE_DIMENSION dim = D3D11_RESOURCE_DIMENSION_UNKNOWN;
    r->GetType(&dim);
    if (dim != D3D11_RESOURCE_DIMENSION_TEXTURE2D) return false;
    static_cast<ID3D11Texture2D*>(r)->GetDesc(&desc);
    return true;
}

// The compute state Lossless Scaling had bound when its pass was dispatched: NVIDIA's call binds its own, and everything is put back after,
// so the passes that follow find what they left.
struct SavedCompute {
    static const UINT kSrvs = 16, kUavs = 8, kCbs = 14, kSamplers = 16;
    ID3D11DeviceContext* ctx;
    ID3D11ComputeShader* shader = nullptr;
    ID3D11ShaderResourceView* srvs[kSrvs] = {}; ID3D11UnorderedAccessView* uavs[kUavs] = {};
    ID3D11Buffer* cbs[kCbs] = {}; ID3D11SamplerState* samplers[kSamplers] = {};
    explicit SavedCompute(ID3D11DeviceContext* c) : ctx(c) {
        ctx->CSGetShader(&shader, nullptr, nullptr);
        ctx->CSGetShaderResources(0, kSrvs, srvs); ctx->CSGetUnorderedAccessViews(0, kUavs, uavs);
        ctx->CSGetConstantBuffers(0, kCbs, cbs); ctx->CSGetSamplers(0, kSamplers, samplers);
        ID3D11ShaderResourceView* noSrvs[kSrvs] = {}; ID3D11UnorderedAccessView* noUavs[kUavs] = {};
        ctx->CSSetShaderResources(0, kSrvs, noSrvs); ctx->CSSetUnorderedAccessViews(0, kUavs, noUavs, nullptr);   // the output was bound as its UAV
    }
    ~SavedCompute() {
        ctx->CSSetShader(shader, nullptr, 0);
        ctx->CSSetShaderResources(0, kSrvs, srvs); ctx->CSSetUnorderedAccessViews(0, kUavs, uavs, nullptr);
        ctx->CSSetConstantBuffers(0, kCbs, cbs); ctx->CSSetSamplers(0, kSamplers, samplers);
        SafeRelease(shader);
        for (auto*& v : srvs) SafeRelease(v); for (auto*& v : uavs) SafeRelease(v); for (auto*& v : cbs) SafeRelease(v); for (auto*& v : samplers) SafeRelease(v);
    }
};

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
    return true;
}

void ReleaseNisPass(NisPass& pass) { SafeRelease(pass.in); SafeRelease(pass.out); pass = {}; }

// ---- starting and stopping

void Scaler11::Log(const char* fmt, ...) {
    if (!m_log) return;
    char text[512]; va_list a; va_start(a, fmt); vsnprintf(text, sizeof text, fmt, a); va_end(a);
    m_log(text);
}

void Scaler11::Fail(const char* what) {
    m_error = what; m_failed = true; m_ready = false;
    Log("DLSS scaler FAILED: %s", what);
}

bool Scaler11::Init(ID3D11Device* dev, const std::wstring& dataPath, const std::wstring& runtimeDir, LogFn log) {
    m_log = std::move(log); m_failed = false; m_error.clear();
    m_dev = dev; m_dev->AddRef();
    const wchar_t* paths[] = { runtimeDir.c_str() };
    NVSDK_NGX_FeatureCommonInfo info{};
    info.PathListInfo.Path = paths; info.PathListInfo.Length = 1;
    NVSDK_NGX_Result r = NVSDK_NGX_D3D11_Init(kAppId, dataPath.c_str(), dev, &info);
    if (NVSDK_NGX_FAILED(r)) { char t[160]; snprintf(t, sizeof t, "NGX D3D11 Init: %s", ResultName(r)); Fail(t); return false; }
    NVSDK_NGX_Parameter* params = nullptr;
    r = NVSDK_NGX_D3D11_AllocateParameters(&params);
    if (NVSDK_NGX_FAILED(r) || !params) { Fail("NGX parameters"); return false; }
    m_params = params;
    int available = 0;
    NVSDK_NGX_Parameter* caps = nullptr;
    if (NVSDK_NGX_SUCCEED(NVSDK_NGX_D3D11_GetCapabilityParameters(&caps)) && caps) {
        caps->Get(NVSDK_NGX_Parameter_SuperSampling_Available, &available);
        NVSDK_NGX_D3D11_DestroyParameters(caps);
    }
    if (!available) { Fail("DLSS Super Resolution is not available on this graphics card or driver"); return false; }

    ID3DBlob* code = nullptr; ID3DBlob* errors = nullptr;
    if (FAILED(D3DCompile(kMotionHlsl, strlen(kMotionHlsl), "scaler_motion", nullptr, nullptr, "main", "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors))) {
        Log("motion shader: %s", errors ? static_cast<const char*>(errors->GetBufferPointer()) : "?");
        SafeRelease(errors); Fail("the motion-vector shader did not compile"); return false;
    }
    dev->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &m_motionShader);
    SafeRelease(code); SafeRelease(errors);
    D3D11_BUFFER_DESC cb{}; cb.ByteWidth = 16; cb.Usage = D3D11_USAGE_DYNAMIC; cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER; cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    dev->CreateBuffer(&cb, nullptr, &m_constants);
    D3D11_SAMPLER_DESC s{}; s.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR; s.AddressU = s.AddressV = s.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP; s.MaxLOD = D3D11_FLOAT32_MAX;
    dev->CreateSamplerState(&s, &m_sampler);
    for (int i = 0; i < kQueries; ++i) {
        D3D11_QUERY_DESC q{}; q.Query = D3D11_QUERY_TIMESTAMP_DISJOINT; dev->CreateQuery(&q, &m_disjoint[i]);
        q.Query = D3D11_QUERY_TIMESTAMP; dev->CreateQuery(&q, &m_begin[i]); dev->CreateQuery(&q, &m_end[i]);
    }
    if (!m_motionShader || !m_constants || !m_sampler) { Fail("the motion-vector pass could not be made"); return false; }
    m_ready = true;
    Log("DLSS scaler ready on Lossless Scaling's device (runtime from %ls)", runtimeDir.c_str());
    return true;
}

void Scaler11::Shutdown() {
    if (m_feature) { NVSDK_NGX_D3D11_ReleaseFeature(static_cast<NVSDK_NGX_Handle*>(m_feature)); m_feature = nullptr; }
    if (m_params) { NVSDK_NGX_D3D11_DestroyParameters(static_cast<NVSDK_NGX_Parameter*>(m_params)); m_params = nullptr; }
    if (m_dev) NVSDK_NGX_D3D11_Shutdown1(m_dev);
    SafeRelease(m_motionUav); SafeRelease(m_motion); SafeRelease(m_depth);
    SafeRelease(m_motionShader); SafeRelease(m_constants); SafeRelease(m_sampler);
    for (int i = 0; i < kQueries; ++i) { SafeRelease(m_disjoint[i]); SafeRelease(m_begin[i]); SafeRelease(m_end[i]); m_queryUsed[i] = false; }
    SafeRelease(m_dev);
    m_inW = m_inH = m_outW = m_outH = 0; m_preset = ~0u; m_ready = false;
}

// ---- the feature and its inputs

bool Scaler11::EnsureInputs(uint32_t w, uint32_t h) {
    if (m_motion && m_inW == w && m_inH == h) return true;
    SafeRelease(m_motionUav); SafeRelease(m_motion); SafeRelease(m_depth);
    D3D11_TEXTURE2D_DESC d{}; d.Width = w; d.Height = h; d.MipLevels = 1; d.ArraySize = 1; d.SampleDesc.Count = 1; d.Usage = D3D11_USAGE_DEFAULT;
    d.Format = DXGI_FORMAT_R16G16_FLOAT; d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    if (FAILED(m_dev->CreateTexture2D(&d, nullptr, &m_motion)) || FAILED(m_dev->CreateUnorderedAccessView(m_motion, nullptr, &m_motionUav))) return false;
    const std::vector<float> flat(static_cast<size_t>(w) * h, 0.5f);   // the model's depth, which Lossless Scaling does not have
    const D3D11_SUBRESOURCE_DATA data{ flat.data(), w * 4, 0 };
    d.Format = DXGI_FORMAT_R32_FLOAT; d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    return SUCCEEDED(m_dev->CreateTexture2D(&d, &data, &m_depth));
}

bool Scaler11::EnsureFeature(ID3D11DeviceContext* ctx, uint32_t inW, uint32_t inH, uint32_t outW, uint32_t outH, unsigned preset) {
    if (m_feature && inW == m_inW && inH == m_inH && outW == m_outW && outH == m_outH && preset == m_preset) return true;
    if (m_feature) { NVSDK_NGX_D3D11_ReleaseFeature(static_cast<NVSDK_NGX_Handle*>(m_feature)); m_feature = nullptr; }
    if (!EnsureInputs(inW, inH)) { Fail("the motion-vector and depth textures could not be made"); return false; }
    auto* p = static_cast<NVSDK_NGX_Parameter*>(m_params);
    // the quality mode only sets NVIDIA's default preset for the ratio; the sizes are what DLSS works with
    const float ratio = std::max(static_cast<float>(outW) / inW, static_cast<float>(outH) / inH);
    const NVSDK_NGX_PerfQuality_Value quality = ratio <= 1.55f ? NVSDK_NGX_PerfQuality_Value_MaxQuality : ratio <= 1.75f ? NVSDK_NGX_PerfQuality_Value_Balanced
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
    LARGE_INTEGER f, a, b; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&a);
    NVSDK_NGX_Handle* handle = nullptr;
    NVSDK_NGX_Result r;
    { const SavedCompute saved(ctx); r = NVSDK_NGX_D3D11_CreateFeature(ctx, NVSDK_NGX_Feature_SuperSampling, p, &handle); }
    QueryPerformanceCounter(&b);
    if (NVSDK_NGX_FAILED(r) || !handle) { char t[160]; snprintf(t, sizeof t, "CreateFeature(DLSS): %s", ResultName(r)); Fail(t); return false; }
    m_feature = handle; m_inW = inW; m_inH = inH; m_outW = outW; m_outH = outH; m_preset = preset; ++m_builds;
    Log("DLSS scaler: %ux%u -> %ux%u (x%.2f), preset %u, made in %.0f ms on Lossless Scaling's render thread", inW, inH, outW, outH, ratio, preset,
        (b.QuadPart - a.QuadPart) * 1000.0 / f.QuadPart);
    return true;
}

void Scaler11::MakeMotion(ID3D11DeviceContext* ctx, ID3D11Resource* flow, uint32_t flowW, uint32_t flowH, float flowUnit, float fraction) {
    ID3D11ShaderResourceView* flowView = nullptr;
    if (flow && flowW && flowH) m_dev->CreateShaderResourceView(flow, nullptr, &flowView);
    const float unit = flowUnit > 0.1f ? flowUnit : 2.0f;
    D3D11_MAPPED_SUBRESOURCE m{};
    if (SUCCEEDED(ctx->Map(m_constants, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
        const MotionConstants c{ m_inW, m_inH, flowView ? static_cast<float>(m_inW) / (unit * flowW) * fraction : 0.0f, flowView ? 1u : 0u };
        memcpy(m.pData, &c, sizeof c); ctx->Unmap(m_constants, 0);
    }
    ctx->CSSetShader(m_motionShader, nullptr, 0);
    ctx->CSSetShaderResources(0, 1, &flowView);
    ctx->CSSetUnorderedAccessViews(0, 1, &m_motionUav, nullptr);
    ctx->CSSetConstantBuffers(0, 1, &m_constants);
    ctx->CSSetSamplers(0, 1, &m_sampler);
    ctx->Dispatch((m_inW + 7) / 8, (m_inH + 7) / 8, 1);
    ID3D11ShaderResourceView* noSrv = nullptr; ID3D11UnorderedAccessView* noUav = nullptr;
    ctx->CSSetShaderResources(0, 1, &noSrv); ctx->CSSetUnorderedAccessViews(0, 1, &noUav, nullptr);
    SafeRelease(flowView);
}

void Scaler11::ReadTimes(ID3D11DeviceContext* ctx) {
    for (int i = 0; i < kQueries; ++i) {
        if (!m_queryUsed[i]) continue;
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj{}; UINT64 t0 = 0, t1 = 0;
        if (ctx->GetData(m_disjoint[i], &dj, sizeof dj, D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) continue;
        if (ctx->GetData(m_begin[i], &t0, sizeof t0, D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) continue;
        if (ctx->GetData(m_end[i], &t1, sizeof t1, D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) continue;
        m_queryUsed[i] = false;
        if (!dj.Disjoint && dj.Frequency && t1 > t0) {
            const double ms = (t1 - t0) * 1000.0 / dj.Frequency;
            m_gpuMs = m_gpuMs == 0 ? ms : m_gpuMs * 0.9 + ms * 0.1;
        }
    }
}

// ---- one scaled frame

bool Scaler11::Run(ID3D11DeviceContext* ctx, const NisPass& pass, ID3D11Resource* flow, uint32_t flowW, uint32_t flowH, float flowUnit,
                   float motionFraction, unsigned preset, bool reset) {
    if (!m_ready) return false;
    const bool fresh = !m_feature || pass.inW != m_inW || pass.inH != m_inH || pass.outW != m_outW || pass.outH != m_outH || preset != m_preset;
    if (!EnsureFeature(ctx, pass.inW, pass.inH, pass.outW, pass.outH, preset)) return false;
    ReadTimes(ctx);
    const int q = m_nextQuery;
    const bool timed = !m_queryUsed[q] && m_disjoint[q];
    const SavedCompute saved(ctx);
    if (timed) { ctx->Begin(m_disjoint[q]); ctx->End(m_begin[q]); }
    MakeMotion(ctx, flow, flowW, flowH, flowUnit, motionFraction);
    auto* p = static_cast<NVSDK_NGX_Parameter*>(m_params);
    p->Set(NVSDK_NGX_Parameter_Color, pass.in);
    p->Set(NVSDK_NGX_Parameter_Output, pass.out);
    p->Set(NVSDK_NGX_Parameter_Depth, static_cast<ID3D11Resource*>(m_depth));
    p->Set(NVSDK_NGX_Parameter_MotionVectors, static_cast<ID3D11Resource*>(m_motion));
    p->Set(NVSDK_NGX_Parameter_Jitter_Offset_X, 0.0f); p->Set(NVSDK_NGX_Parameter_Jitter_Offset_Y, 0.0f);
    p->Set(NVSDK_NGX_Parameter_MV_Scale_X, 1.0f); p->Set(NVSDK_NGX_Parameter_MV_Scale_Y, 1.0f);
    p->Set(NVSDK_NGX_Parameter_Reset, (reset || fresh) ? 1 : 0);
    p->Set(NVSDK_NGX_Parameter_Sharpness, 0.0f);
    p->Set(NVSDK_NGX_Parameter_DLSS_Pre_Exposure, 1.0f);
    p->Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width, pass.inW);
    p->Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height, pass.inH);
    const NVSDK_NGX_Result r = NVSDK_NGX_D3D11_EvaluateFeature(ctx, static_cast<NVSDK_NGX_Handle*>(m_feature), p);
    if (timed) { ctx->End(m_end[q]); ctx->End(m_disjoint[q]); m_queryUsed[q] = true; m_nextQuery = (q + 1) % kQueries; }
    if (NVSDK_NGX_FAILED(r)) {
        char t[160]; snprintf(t, sizeof t, "EvaluateFeature(DLSS): %s", ResultName(r));
        Fail(t);
        return false;
    }
    ++m_runs;
    return true;
}

} // namespace nr
