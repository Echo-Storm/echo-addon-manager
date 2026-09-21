// Model-side shaders, compiled at runtime with D3DCompile (cs_5_0). One root signature, three PSOs.
//   t0..t3  SRVs      u0,u1 UAVs      b0 root constants (12 dwords)      s0 static linear-clamp sampler
//
// Per model run:  CSDown (frame -> proxy)  ->  CSFlowToMvec (LSFG flow -> mvec)  ->  [model]  ->
//                 CSDelta (model - proxy -> the shared work-res delta)
// The delta is applied to LS's presented frames on the D3D11 side (addon/compose11.cpp).
#pragma once

static const char* kNrModelHlsl = R"HLSL(
SamplerState sLin : register(s0);
Texture2D<float4>   tOrig  : register(t0);   // full-res frame
Texture2D<float4>   tProxy : register(t1);   // work-res proxy: the frame as the model saw it
Texture2D<float4>   tNr    : register(t2);   // CSDelta: model output
Texture2D<float4>   tAux   : register(t3);   // CSFlowToMvec: LSFG flow (RGBA16F)
RWTexture2D<float4> uOut   : register(u0);
RWTexture2D<float2> uMv    : register(u1);   // CSFlowToMvec only

cbuffer CB : register(b0) {
    uint2 dstSize;    // size of the texture being written
    uint2 srcSize;    // size of the texture being read
    uint  flags;      // bit0: a flow texture is bound
    float flowScale;  // CSFlowToMvec: work-res pixels per flow unit
    float smoothAmt;  // (used by kNrSmoothHlsl only)
    uint  pad0; uint4 pad1;
};

// Frame -> proxy. Four bilinear taps spread over the work texel's footprint: a plain bilinear fetch aliases past
// a 2x reduction, and aliasing in the model's input becomes noise in its output.
[numthreads(8, 8, 1)]
void CSDown(uint3 id : SV_DispatchThreadID) {
    if (id.x >= dstSize.x || id.y >= dstSize.y) return;
    float2 step = 1.0 / float2(dstSize);
    float2 uv = (float2(id.xy) + 0.5) * step;
    float2 q = step * 0.25;
    float4 c = tOrig.SampleLevel(sLin, uv + float2(-q.x, -q.y), 0) + tOrig.SampleLevel(sLin, uv + float2( q.x, -q.y), 0) +
               tOrig.SampleLevel(sLin, uv + float2(-q.x,  q.y), 0) + tOrig.SampleLevel(sLin, uv + float2( q.x,  q.y), 0);
    uOut[id.xy] = c * 0.25;
}

// LSFG flow (xy = current -> previous frame) -> work-res motion vectors in work-res pixels. Zeros without flow.
[numthreads(8, 8, 1)]
void CSFlowToMvec(uint3 id : SV_DispatchThreadID) {
    if (id.x >= dstSize.x || id.y >= dstSize.y) return;
    float2 uv = (float2(id.xy) + 0.5) / float2(dstSize);
    uMv[id.xy] = (flags & 1u) ? tAux.SampleLevel(sLin, uv, 0).xy * flowScale : float2(0, 0);
}

// Work-res delta = model - proxy (signed, RGBA16F).
[numthreads(8, 8, 1)]
void CSDelta(uint3 id : SV_DispatchThreadID) {
    if (id.x >= dstSize.x || id.y >= dstSize.y) return;
    uOut[id.xy] = float4(tNr[id.xy].rgb - tProxy[id.xy].rgb, 0);
}
)HLSL";

// Delta with temporal smoothing: the new delta (model - proxy) is blended with the previous smoothed delta, sampled
// where this pixel was one frame ago (the work-res motion vectors, current -> previous), and written both to the shared
// delta and to the history buffer. t0 = previous delta, t1 = proxy, t2 = model output, t3 = motion vectors.
// flags bit1: the history is valid (not right after a reset or a rebuild).
static const char* kNrSmoothHlsl = R"HLSL(
SamplerState sLin : register(s0);
Texture2D<float4>   tPrev  : register(t0);
Texture2D<float4>   tProxy : register(t1);
Texture2D<float4>   tNr    : register(t2);
Texture2D<float4>   tMv    : register(t3);
RWTexture2D<float4> uOut   : register(u0);   // the shared delta
RWTexture2D<float4> uHist  : register(u1);   // the new history

cbuffer CB : register(b0) {
    uint2 dstSize; uint2 srcSize;
    uint  flags; float flowScale; float smoothAmt; uint pad0; uint4 pad1;
};

[numthreads(8, 8, 1)]
void CSDeltaSmooth(uint3 id : SV_DispatchThreadID) {
    if (id.x >= dstSize.x || id.y >= dstSize.y) return;
    float3 d = tNr[id.xy].rgb - tProxy[id.xy].rgb;
    if (flags & 2u) {
        const float2 mv = tMv[id.xy].xy;
        const float2 puv = (float2(id.xy) + 0.5 + mv) / float2(dstSize);
        const float3 pd = tPrev.SampleLevel(sLin, puv, 0).rgb;
        d = lerp(d, pd, saturate(smoothAmt));
    }
    uOut[id.xy] = float4(d, 0);
    uHist[id.xy] = float4(d, 0);
}
)HLSL";
