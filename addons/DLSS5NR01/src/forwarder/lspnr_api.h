// Contract between the forwarder (nvngx.dll_lspnr.dll) and its hosts (harness, addon).
#pragma once
#include <cstdint>

struct ID3D12Device;
struct ID3D12GraphicsCommandList;
struct ID3D12Resource;

// Tuning the model reads at EVERY evaluate (verified with the harness --trace and by measurement; see
// docs/dlssnr-knobs.md). Changing these never needs a feature rebuild; the snippet just resets its
// temporal history when one changes.
struct LspnrTuning {
    uint32_t style;           // DLSSNR.Style: 0 standard, 1 natural, 2 cinematic (higher clamps to 2)
    uint32_t useAutoMask;     // DLSSNR.UseAutoMask
    uint32_t uiCorrection;    // DLSSNR.UICorrection (no effect unless a UI texture is supplied)
    float intensity;          // DLSSNR.Intensity, clamped by the model to 0..1
    float localStructure;     // DLSSNR.LocalStructureStrength, unclamped (negative accepted, >10 is garbage)
    float localTone;          // DLSSNR.LocalToneStrength, unclamped
    float skinStructure;      // DLSSNR.SkinStructureStrength, -1 = follow local structure
};

// Read by the model at CreateFeature only: size, preset, scaling ratio. Tuning is written too so the
// first evaluate already has it.
struct LspnrCreateParams {
    uint32_t width, height;
    uint32_t preset;          // DLSSNR.Hint.Render.Preset — the 310.8 snippet ships one weight set, 0..3 are identical
    float scalingRatio;       // DLSSNR.ScalingRatio — read but inert in 310.8; keep 1.0
    LspnrTuning tuning;
};

// Re-written in full at every EvaluateFeature (the block is shared, F8).
struct LspnrEvalParams {
    ID3D12Resource* color;    // NON_PIXEL_SHADER_RESOURCE
    ID3D12Resource* depth;    // NON_PIXEL_SHADER_RESOURCE, R32_FLOAT, guide size
    ID3D12Resource* mvec;     // NON_PIXEL_SHADER_RESOURCE, R16G16_FLOAT, guide size
    ID3D12Resource* output;   // UNORDERED_ACCESS, same size as color
    uint32_t width, height;
    uint32_t guideWidth, guideHeight;
    uint32_t depthInverted;
    uint32_t reset;
    float mvScaleX, mvScaleY;
    float scalingRatio;       // DLSSNR.ScalingRatio
    // Optional DLSSNR.ControlMask: RGBA8 (or R8/R32F), colour size, NON_PIXEL_SHADER_RESOURCE. Black pixels come
    // back bit-identical to the input, white pixels get the model; a single non-zero channel counts as black,
    // so write the value to R, G and B. Supplying one replaces the auto mask. nullptr = none.
    ID3D12Resource* controlMask;
    LspnrTuning tuning;
};

extern "C" {
// bitfield of resolved snippet entry points: 1 Init_Ext, 2 CreateFeature, 4 EvaluateFeature, 8 ReleaseFeature
typedef int   (__cdecl* PFN_lspnr_probe)(const wchar_t* snippetPath);
typedef int   (__cdecl* PFN_lspnr_init)(const wchar_t* snippetPath, const wchar_t* dataPath, ID3D12Device* device, void* capsBlock);
typedef void  (__cdecl* PFN_lspnr_set_float_slot)(int setterSlot);
typedef void  (__cdecl* PFN_lspnr_probe_float)(void* capsBlock, const char* key, float value, int setterSlot);
typedef int   (__cdecl* PFN_lspnr_get_float)(void* capsBlock, const char* key, void* out8Bytes, int getterSlot);
typedef void* (__cdecl* PFN_lspnr_create)(ID3D12GraphicsCommandList* cmd, void* capsBlock, const LspnrCreateParams* p);
typedef int   (__cdecl* PFN_lspnr_evaluate)(ID3D12GraphicsCommandList* cmd, void* feature, void* capsBlock, const LspnrEvalParams* p);
typedef void  (__cdecl* PFN_lspnr_release)(void* feature);
typedef int   (__cdecl* PFN_lspnr_last_result)(int which);   // 0 init, 1 create, 2 evaluate, 3 PopulateParameters_Impl
// The snippet plants "DLSSNRComputeScalingRatioCallback" in the block (PopulateParameters_Impl, called by
// lspnr_init); Streamline sets "PerfQualityValue" and calls it to get DLSSNR.ScalingRatio for a performance
// mode. Measured on 310.8: modes 0,1,2,4,5 all answer 1.0, 3 and 6 are unsupported, and an evaluate with
// Output larger than Color fails (InvalidParameter) — this build has no model-side upsampling.
// Returns the callback's result (1 = ok), -2 if the block holds no callback; *outRatio = DLSSNR.ScalingRatio.
typedef int   (__cdecl* PFN_lspnr_scaling_ratio)(void* capsBlock, unsigned perfQuality, float* outRatio);
}

#define LSPNR_FORWARDER_FILENAME L"nvngx.dll_lspnr.dll"
