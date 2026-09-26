// HDR frames, as the passes see them: HLSL shared by the engine's passes (D3D12) and the compose (D3D11), put in front of their own source.
//
// The model, the look controls and our other passes work on display-referred pictures, 0..1 and sRGB-encoded. An HDR frame is either scRGB
// (16-bit float, linear, Rec.709 primaries, 1.0 = 80 nits, brighter above) or HDR10 (10-bit, the PQ curve, Rec.2020 primaries). ToSdr gives
// its SDR view: light relative to the SDR white (Windows' "SDR content brightness", `white` in nits), a curve that is the identity up to kKnee
// and rolls everything brighter smoothly into the rest of 0..1, then sRGB encoding. FromSdr is its exact inverse, so a change made in the SDR
// view goes back into the frame's own encoding, and what was not changed comes back as it was. Encoding 0 is SDR: both are the identity.
#pragma once

#define NR_HDR_HLSL R"HLSL(
static const float kKnee = 0.75;
static const float3x3 kRec2020To709 = { 1.6605, -0.5876, -0.0728, -0.1246, 1.1329, -0.0083, -0.0182, -0.1006, 1.1187 };
static const float3x3 kRec709To2020 = { 0.6274, 0.3293, 0.0433, 0.0691, 0.9195, 0.0114, 0.0164, 0.0880, 0.8956 };
float3 SrgbToLinear(float3 c) { return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4); }
float3 LinearToSrgb(float3 l) { l = saturate(l); return l <= 0.0031308 ? l * 12.92 : 1.055 * pow(l, 1.0 / 2.4) - 0.055; }
float3 PqToNits(float3 e) {
    const float m1 = 0.1593017578125, m2 = 78.84375, c1 = 0.8359375, c2 = 18.8515625, c3 = 18.6875;
    const float3 p = pow(saturate(e), 1.0 / m2);
    return 10000.0 * pow(max(p - c1, 0.0) / (c2 - c3 * p), 1.0 / m1);
}
float3 NitsToPq(float3 n) {
    const float m1 = 0.1593017578125, m2 = 78.84375, c1 = 0.8359375, c2 = 18.8515625, c3 = 18.6875;
    const float3 y = pow(saturate(n / 10000.0), m1);
    return pow((c1 + c2 * y) / (1.0 + c3 * y), m2);
}
float3 Compress(float3 x) { const float3 over = max(x - kKnee, 0.0); return min(x, kKnee) + (1.0 - kKnee) * (1.0 - exp(-over / (1.0 - kKnee))); }
float3 Expand(float3 y) { const float3 over = max(y - kKnee, 0.0); return min(y, kKnee) - (1.0 - kKnee) * log(max(1.0 - over / (1.0 - kKnee), 1e-5)); }
// encoding: 0 SDR, 1 scRGB, 2 HDR10 (PQ); white: the SDR white in nits
float3 ToSdr(float3 c, uint encoding, float white) {
    if (encoding == 0u) return c;
    const float3 lin = encoding == 1u ? c * (80.0 / white) : mul(kRec2020To709, PqToNits(c)) / white;   // 1 = the SDR white
    return LinearToSrgb(Compress(max(lin, 0.0)));
}
float3 FromSdr(float3 s, uint encoding, float white) {
    if (encoding == 0u) return s;
    const float3 lin = Expand(SrgbToLinear(saturate(s)));
    return encoding == 1u ? lin * (white / 80.0) : NitsToPq(mul(kRec709To2020, lin) * white);
}
)HLSL"
