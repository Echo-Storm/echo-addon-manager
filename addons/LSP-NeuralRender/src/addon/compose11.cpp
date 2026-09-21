#include "addon/compose11.h"
#include "addon/bridge.h"
#include <d3dcompiler.h>
#include <windows.h>
#include <cstddef>
#include <cstdio>
#include <cstdarg>
#include <cstring>

#pragma comment(lib, "d3dcompiler.lib")

static const char* kComposeHlsl = R"HLSL(
SamplerState sLin : register(s0);
Texture2D<float4>   tSrc   : register(t0);   // the presented frame (a copy)
Texture2D<float4>   tDelta : register(t1);   // work-res delta of real frame d
Texture2D<float4>   tFlow  : register(t2);   // LSFG flow: xy = current -> previous, zw = previous -> current
RWTexture2D<float4> uOut   : register(u0);

cbuffer CB : register(b0) {
    uint2  dstSize;
    float  offset;       // presented frame - d, in real frames
    float  intensity;
    float  maxDelta;
    float  hiProtect;
    uint   debugView;    // 0 result, 1 original, 2 delta x4, 3 frame role, 4 flow
    uint   flags;        // bit0 flow bound, bit1 generated frame
    float2 uvPerUnit;    // one flow unit in uv
    float2 pad;
    float  sharpen;      // 0 = off
    uint   compare;      // 0 enhanced, 1 split (left half original), 2 original only
    float  splitPos;
    uint   marker;       // 0 none, 1 green, 2 red, 3 amber, 4 blue, 5 purple: a square in the top-left corner
    float  saturation;   // 1 = unchanged
    float  vibrance;     // 0 = off
    float  brightness;   // 0 = unchanged
    float  contrast;     // 1 = unchanged
    float  gamma;        // 1 = unchanged
    float  shadows;      // 0 = unchanged
    float  highlights;   // 0 = unchanged
    float  grain;        // 0 = off
    uint   grainSeed;
    float  grainSize;
    uint   hudCount;
    float  hudFeather;
    float4 hud[6];       // left, top, right, bottom (0..1)
};
static const float3 kLuma = float3(0.299, 0.587, 0.114);

// Contrast-adaptive sharpening (the AMD FidelityFX CAS formula, MIT): the weight shrinks where a channel is
// already near 0 or 1, so it sharpens detail without clipping. e = centre; b, d, f, h = up, left, right, down.
float3 CasSharpen(float3 b, float3 d, float3 e, float3 f, float3 h, float sharp) {
    float3 mn = min(min(min(d, e), min(f, b)), h);
    float3 mx = max(max(max(d, e), max(f, b)), h);
    float3 amp = sqrt(saturate(min(mn, 1.0 - mx) / max(mx, 1e-4)));
    float peak = -1.0 / lerp(8.0, 5.0, sharp);
    float3 w = amp * peak;
    return saturate((b * w + d * w + f * w + h * w + e) / (1.0 + 4.0 * w));
}

[numthreads(8, 8, 1)]
void CSCompose(uint3 id : SV_DispatchThreadID) {
    if (id.x >= dstSize.x || id.y >= dstSize.y) return;
    float4 o = tSrc[id.xy];
    float3 r = o.rgb;
    const float xn = ((float)id.x + 0.5) / (float)dstSize.x;
    // protected rectangles: 1 inside (softly bordered), the picture is left as LS made it there
    const float2 uv0 = (float2(id.xy) + 0.5) / float2(dstSize);
    float hudMask = 0.0;
    for (uint hi = 0u; hi < hudCount && hi < 6u; ++hi) {
        const float4 rc = hud[hi];
        const float2 dd = min(uv0 - rc.xy, rc.zw - uv0);
        hudMask = max(hudMask, saturate(min(dd.x, dd.y) / max(hudFeather, 1e-5) + 0.5));
    }
    const bool plainView = (debugView == 0 || debugView == 3);
    const bool showOriginal = (compare == 2) || (compare == 1 && xn < splitPos) || (hudMask > 0.999 && plainView);
    if (!showOriginal) {
        float2 uv = (float2(id.xy) + 0.5) / float2(dstSize);
        float4 fl = (flags & 1u) ? tFlow.SampleLevel(sLin, uv, 0) : float4(0, 0, 0, 0);
        // where this pixel's content was in frame d: forward along previous->current for a delta from before this
        // frame, back along current->previous for a delta from after it
        float2 suv = offset > 0.0 ? uv - offset * fl.zw * uvPerUnit : uv + offset * fl.xy * uvPerUnit;
        float3 d = tDelta.SampleLevel(sLin, suv, 0).rgb;
        float wh = hiProtect < 0.999 ? 1.0 - smoothstep(hiProtect, 1.0, dot(o.rgb, kLuma)) : 1.0;
        d = clamp(d * intensity, -maxDelta, maxDelta) * wh;
        r = saturate(o.rgb + d);
        if (sharpen > 0.001 && (debugView == 0 || debugView == 3)) {
            // The delta is low-frequency, so the neighbours get the centre's delta: no extra flow or delta taps.
            const int2 lim = int2(dstSize) - 1; const int2 p = int2(id.xy);
            float3 up = saturate(tSrc[clamp(p + int2(0, -1), 0, lim)].rgb + d);
            float3 dn = saturate(tSrc[clamp(p + int2(0, 1), 0, lim)].rgb + d);
            float3 lf = saturate(tSrc[clamp(p + int2(-1, 0), 0, lim)].rgb + d);
            float3 rt = saturate(tSrc[clamp(p + int2(1, 0), 0, lim)].rgb + d);
            r = CasSharpen(up, lf, r, rt, dn, saturate(sharpen));
        }
        if ((abs(brightness) > 0.0005 || abs(contrast - 1.0) > 0.002 || abs(gamma - 1.0) > 0.002) && (debugView == 0 || debugView == 3)) {
            // Tone on the encoded (display) values, as a monitor's own controls do: contrast around mid-grey, then
            // brightness, then the gamma curve (gamma above 1 brightens the mid-tones and leaves black and white alone).
            r = saturate((r - 0.5) * contrast + 0.5 + brightness);
            r = pow(max(r, 1e-5), 1.0 / max(gamma, 0.05));
        }
        if ((abs(shadows) > 0.005 || abs(highlights) > 0.005) && plainView) {
            // Tonal ranges: shadows acts on the dark tones and fades out by 0.55 luma, highlights on the bright ones from
            // 0.45 up. Both add the same amount to every channel, so colours keep their differences.
            const float l = dot(r, kLuma);
            const float ws = 1.0 - smoothstep(0.0, 0.55, l);
            const float wh = smoothstep(0.45, 1.0, l);
            r = saturate(r + (shadows * ws + highlights * wh) * 0.25);
        }
        if ((abs(saturation - 1.0) > 0.002 || vibrance > 0.002) && (debugView == 0 || debugView == 3)) {
            // Saturation scales each colour's distance from the pixel's own luma; vibrance adds to that scale only where
            // the pixel is muted (small max-min spread), so vivid colours and skin tones are not pushed further.
            const float lum = dot(r, kLuma);
            const float chroma = max(r.r, max(r.g, r.b)) - min(r.r, min(r.g, r.b));
            const float amount = saturation + vibrance * (1.0 - saturate(chroma));
            r = saturate(lum + (r - lum) * amount);
        }
        if (grain > 0.002 && plainView) {
            // Monochrome grain, strongest in the mid-tones, different at every presented frame (grainSeed).
            const uint2 gp = id.xy / max(1u, (uint)grainSize);
            uint hsh = gp.x * 1973u + gp.y * 9277u + grainSeed * 26699u + 1u;
            hsh = (hsh ^ 61u) ^ (hsh >> 16); hsh *= 9u; hsh ^= hsh >> 4; hsh *= 0x27d4eb2du; hsh ^= hsh >> 15;
            const float n = (float)(hsh & 0xFFFFu) / 32767.5 - 1.0;
            const float lg = dot(r, kLuma);
            r = saturate(r + n * grain * 0.10 * (0.35 + 0.65 * 4.0 * lg * (1.0 - lg)));
        }
        if (hudMask > 0.0 && plainView) r = lerp(r, o.rgb, hudMask);
        if      (debugView == 1) r = o.rgb;
        else if (debugView == 2) r = saturate(0.5 + d * 4.0);
        else if (debugView == 3) r = saturate(r + ((flags & 2u) ? float3(0.15, 0, 0) : float3(0, 0.15, 0)));
        else if (debugView == 4) r = saturate(float3(0.5 + fl.xy / 16.0, 0.5));
    }
    if ((flags & 4u) != 0u) {   // show the protected areas: a green tint and outline (display only)
        float inside = 0.0, edge = 0.0; const float px = 1.5 / (float)dstSize.x;
        for (uint hj = 0u; hj < hudCount && hj < 6u; ++hj) {
            const float4 rc = hud[hj]; const float2 dd = min(uv0 - rc.xy, rc.zw - uv0); const float dm = min(dd.x, dd.y);
            if (dm > 0.0) inside = 1.0;
            if (abs(dm) < px) edge = 1.0;
        }
        r = lerp(r, float3(0.49, 0.70, 0.26), inside * 0.22);
        if (edge > 0.5) r = float3(0.49, 0.70, 0.26);
    }
    if (compare == 1 && abs((float)id.x + 0.5 - splitPos * (float)dstSize.x) < max(1.0, (float)dstSize.x / 2000.0))
        r = float3(0.9, 0.9, 0.9);   // the split line
    if (marker != 0) {
        const uint ms = max(12u, dstSize.x / 150u);
        if (id.x < ms && id.y < ms)
            r = marker == 1 ? float3(0.10, 0.85, 0.20) : marker == 2 ? float3(0.90, 0.15, 0.15) : marker == 3 ? float3(0.95, 0.70, 0.10) : marker == 4 ? float3(0.15, 0.45, 0.95) : float3(0.65, 0.30, 0.90);
    }
    uOut[id.xy] = float4(r, o.a);
}
)HLSL";

struct ComposeCB { uint32_t dstW, dstH; float offset, intensity, maxDelta, hiProtect; uint32_t debugView, flags; float uvPerUnitX, uvPerUnitY, pad0, pad1; float sharpen; uint32_t compare; float splitPos; uint32_t marker; float saturation; float vibrance; float brightness; float contrast; float gamma; float shadows, highlights, grain; uint32_t grainSeed; float grainSize; uint32_t hudCount; float hudFeather; float hud[6][4]; };
// The HLSL cbuffer packs into 16-byte rows: hud[] starts at byte 112 and the whole buffer is a multiple of 16
// (CreateBuffer rejects any other size for a constant buffer).
static_assert(offsetof(ComposeCB, gamma) == 80 && offsetof(ComposeCB, hud) == 112 && sizeof(ComposeCB) == 208, "ComposeCB must match the HLSL cbuffer layout");

void Compose11::Log(const char* fmt, ...) { char b[512]; va_list a; va_start(a, fmt); vsnprintf(b, sizeof b, fmt, a); va_end(a); if (m_log) m_log(b); }

bool Compose11::Init(ID3D11Device* dev, LogFn log) {
    Shutdown();
    m_log = log; m_dev = dev; m_dev->AddRef();
    ID3DBlob* cs = nullptr, * err = nullptr;
    if (FAILED(D3DCompile(kComposeHlsl, strlen(kComposeHlsl), "compose11", nullptr, nullptr, "CSCompose", "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &cs, &err))) {
        Log("Compose11: HLSL: %s", err ? (char*)err->GetBufferPointer() : "?"); if (err) err->Release(); Shutdown(); return false; }
    HRESULT hr = m_dev->CreateComputeShader(cs->GetBufferPointer(), cs->GetBufferSize(), nullptr, &m_cs); cs->Release();
    if (FAILED(hr)) { Log("Compose11: CreateComputeShader 0x%08x", (unsigned)hr); Shutdown(); return false; }
    D3D11_BUFFER_DESC bd{}; bd.ByteWidth = sizeof(ComposeCB); bd.Usage = D3D11_USAGE_DYNAMIC; bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(m_dev->CreateBuffer(&bd, nullptr, &m_cb))) { Log("Compose11: constant buffer"); Shutdown(); return false; }
    D3D11_SAMPLER_DESC sd{}; sd.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT; sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP; sd.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(m_dev->CreateSamplerState(&sd, &m_samp))) { Log("Compose11: sampler"); Shutdown(); return false; }
    m_cpuMs = 0; m_runs = 0;
    Log("Compose11: ready");
    return true;
}

void Compose11::Shutdown() {
    for (auto** p : { (IUnknown**)&m_srcSrv, (IUnknown**)&m_src, (IUnknown**)&m_dstUav, (IUnknown**)&m_dst, (IUnknown**)&m_flowSrv, (IUnknown**)&m_flowRes,
                      (IUnknown**)&m_cs, (IUnknown**)&m_cb, (IUnknown**)&m_samp, (IUnknown**)&m_dev }) { if (*p) (*p)->Release(); *p = nullptr; }
    m_sw = m_sh = 0; m_sfmt = DXGI_FORMAT_UNKNOWN;
}

bool Compose11::EnsureScratch(const D3D11_TEXTURE2D_DESC& td, DXGI_FORMAT vf) {
    if (td.Width == m_sw && td.Height == m_sh && vf == m_sfmt && m_src) return true;
    for (auto** p : { (IUnknown**)&m_srcSrv, (IUnknown**)&m_src, (IUnknown**)&m_dstUav, (IUnknown**)&m_dst }) { if (*p) (*p)->Release(); *p = nullptr; }
    D3D11_TEXTURE2D_DESC d{}; d.Width = td.Width; d.Height = td.Height; d.MipLevels = 1; d.ArraySize = 1; d.Format = td.Format; d.SampleDesc.Count = 1; d.Usage = D3D11_USAGE_DEFAULT;
    d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SHADER_RESOURCE_VIEW_DESC sv{}; sv.Format = vf; sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; sv.Texture2D.MipLevels = 1;
    D3D11_UNORDERED_ACCESS_VIEW_DESC uv{}; uv.Format = vf; uv.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
    HRESULT hr = m_dev->CreateTexture2D(&d, nullptr, &m_src);
    if (SUCCEEDED(hr)) hr = m_dev->CreateShaderResourceView(m_src, &sv, &m_srcSrv);
    d.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    if (SUCCEEDED(hr)) hr = m_dev->CreateTexture2D(&d, nullptr, &m_dst);
    if (SUCCEEDED(hr)) hr = m_dev->CreateUnorderedAccessView(m_dst, &uv, &m_dstUav);
    if (FAILED(hr)) { Log("Compose11: scratch %ux%u fmt %d failed 0x%08x", td.Width, td.Height, (int)td.Format, (unsigned)hr); return false; }
    m_sw = td.Width; m_sh = td.Height; m_sfmt = vf;
    snprintf(m_targetInfo, sizeof m_targetInfo, "%ux%u fmt %d%s", td.Width, td.Height, (int)td.Format, (td.BindFlags & D3D11_BIND_UNORDERED_ACCESS) ? " (UAV, in place)" : " (copy back)");
    Log("Compose11: target %s", m_targetInfo);
    return true;
}

ID3D11ShaderResourceView* Compose11::FlowSrv(ID3D11Resource* flow) {
    if (!flow) return nullptr;
    if (flow == m_flowRes && m_flowSrv) return m_flowSrv;
    if (m_flowSrv) m_flowSrv->Release(); if (m_flowRes) m_flowRes->Release(); m_flowSrv = nullptr; m_flowRes = nullptr;
    D3D11_SHADER_RESOURCE_VIEW_DESC sv{}; sv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT; sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; sv.Texture2D.MipLevels = 1;
    if (FAILED(m_dev->CreateShaderResourceView(flow, &sv, &m_flowSrv))) { m_flowSrv = nullptr; return nullptr; }
    m_flowRes = flow; m_flowRes->AddRef();
    return m_flowSrv;
}

bool Compose11::Run(ID3D11DeviceContext* ctx, const Args& a) {
    if (!m_cs || !ctx || !a.target || !a.delta) return false;
    LARGE_INTEGER qf, q0; QueryPerformanceFrequency(&qf); QueryPerformanceCounter(&q0);
    D3D11_TEXTURE2D_DESC td; a.target->GetDesc(&td);
    const DXGI_FORMAT vf = Bridge::ViewFormat(td.Format);
    if (vf == DXGI_FORMAT_UNKNOWN || td.SampleDesc.Count != 1) {   // 8-bit, 10-bit and half-float back buffers; the delta is display-referred either way
        uint64_t key = 0xC000000000000000ull | (uint32_t)td.Format; if (key != m_lastFailKey) { m_lastFailKey = key; Log("Compose11: unsupported present format %d", (int)td.Format); }
        return false;
    }
    if (!EnsureScratch(td, vf)) return false;
    const bool direct = (td.BindFlags & D3D11_BIND_UNORDERED_ACCESS) != 0;
    ID3D11UnorderedAccessView* targetUav = nullptr;
    if (direct) { D3D11_UNORDERED_ACCESS_VIEW_DESC uv{}; uv.Format = vf; uv.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D; if (FAILED(m_dev->CreateUnorderedAccessView(a.target, &uv, &targetUav))) targetUav = nullptr; }

    ctx->CopyResource(m_src, a.target);
    ID3D11ShaderResourceView* flowSrv = FlowSrv(a.flow);

    // constants
    ComposeCB cb{}; cb.dstW = td.Width; cb.dstH = td.Height; cb.offset = a.offset; cb.intensity = a.intensity; cb.maxDelta = a.maxDelta; cb.hiProtect = a.hiProtect;
    cb.debugView = a.debugView; cb.flags = (flowSrv ? 1u : 0u) | (a.isGen ? 2u : 0u);
    cb.sharpen = a.sharpen; cb.compare = a.compare; cb.splitPos = a.splitPos; cb.marker = a.marker;
    cb.saturation = a.saturation; cb.vibrance = a.vibrance;
    cb.brightness = a.brightness; cb.contrast = a.contrast; cb.gamma = a.gamma;
    cb.shadows = a.shadows; cb.highlights = a.highlights; cb.grain = a.grain; cb.grainSeed = a.grainSeed; cb.grainSize = a.grainSize < 1.0f ? 1.0f : a.grainSize > 4.0f ? 4.0f : a.grainSize;
    cb.hudCount = a.hudCount > 6u ? 6u : a.hudCount; cb.hudFeather = a.hudFeather; memcpy(cb.hud, a.hud, sizeof cb.hud);
    if (a.hudShow) cb.flags |= 4u;
    const float fu = a.flowUnit > 0.1f ? a.flowUnit : 2.0f;
    cb.uvPerUnitX = (flowSrv && a.flowW) ? 1.0f / (fu * (float)a.flowW) : 0.0f; cb.uvPerUnitY = (flowSrv && a.flowH) ? 1.0f / (fu * (float)a.flowH) : 0.0f;
    D3D11_MAPPED_SUBRESOURCE m{}; if (SUCCEEDED(ctx->Map(m_cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) { memcpy(m.pData, &cb, sizeof cb); ctx->Unmap(m_cb, 0); }

    // save LS's compute state, run, restore
    ID3D11ComputeShader* savedCs = nullptr; ID3D11ShaderResourceView* savedSrv[3] = {}; ID3D11UnorderedAccessView* savedUav = nullptr; ID3D11Buffer* savedCb = nullptr; ID3D11SamplerState* savedSamp = nullptr;
    ctx->CSGetShader(&savedCs, nullptr, nullptr); ctx->CSGetShaderResources(0, 3, savedSrv); ctx->CSGetUnorderedAccessViews(0, 1, &savedUav); ctx->CSGetConstantBuffers(0, 1, &savedCb); ctx->CSGetSamplers(0, 1, &savedSamp);
    ID3D11ShaderResourceView* srvs[3] = { m_srcSrv, a.delta, flowSrv }; ID3D11UnorderedAccessView* uavs[1] = { targetUav ? targetUav : m_dstUav };
    ID3D11ShaderResourceView* nullSrv[3] = {}; ID3D11UnorderedAccessView* nullUav[1] = {};
    ctx->CSSetShader(m_cs, nullptr, 0); ctx->CSSetShaderResources(0, 3, srvs); ctx->CSSetUnorderedAccessViews(0, 1, uavs, nullptr); ctx->CSSetConstantBuffers(0, 1, &m_cb); ctx->CSSetSamplers(0, 1, &m_samp);
    ctx->Dispatch((td.Width + 7) / 8, (td.Height + 7) / 8, 1);
    ctx->CSSetShaderResources(0, 3, nullSrv); ctx->CSSetUnorderedAccessViews(0, 1, nullUav, nullptr);
    ctx->CSSetShader(savedCs, nullptr, 0); ctx->CSSetShaderResources(0, 3, savedSrv); ctx->CSSetUnorderedAccessViews(0, 1, &savedUav, nullptr); ctx->CSSetConstantBuffers(0, 1, &savedCb); ctx->CSSetSamplers(0, 1, &savedSamp);
    if (savedCs) savedCs->Release(); for (auto* s : savedSrv) if (s) s->Release(); if (savedUav) savedUav->Release(); if (savedCb) savedCb->Release(); if (savedSamp) savedSamp->Release();
    if (!targetUav) ctx->CopyResource(a.target, m_dst); else targetUav->Release();

    m_runs++;
    LARGE_INTEGER q1; QueryPerformanceCounter(&q1); double ms = (double)(q1.QuadPart - q0.QuadPart) * 1000.0 / (double)qf.QuadPart; m_cpuMs = m_cpuMs == 0 ? ms : m_cpuMs * 0.9 + ms * 0.1;
    return true;
}
