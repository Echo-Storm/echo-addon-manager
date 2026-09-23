// What the forwarder (nvngx.dll_dlss5nr01.dll) exports, for the engine and the self-test that load it. The forwarder is the only module that calls
// the model file (nvngx_dlssnr.dll, "the snippet"); see nr_forwarder.cpp for why it has to be a module of its own.
#pragma once
#include <cstdint>

struct ID3D12Device;
struct ID3D12GraphicsCommandList;
struct ID3D12Resource;

// What the model reads at every evaluate (measured one by one, docs/dlssnr-knobs.md). Changing any of it never needs the feature made
// again; the model only restarts its history.
struct NrTuning {
    uint32_t style;           // DLSSNR.Style: 0 standard, 1 natural, 2 cinematic (more is taken as 2)
    uint32_t useAutoMask;     // DLSSNR.UseAutoMask
    uint32_t uiCorrection;    // DLSSNR.UICorrection (does nothing without a UI texture, which is never given)
    float intensity;          // DLSSNR.Intensity, which the model keeps to 0..1
    float localStructure;     // DLSSNR.LocalStructureStrength, not limited (negative works, above 10 is garbage)
    float localTone;          // DLSSNR.LocalToneStrength, not limited
    float skinStructure;      // DLSSNR.SkinStructureStrength, -1 = the same as local structure
};

// What the model reads only when the feature is made. The tuning is written too, so the first evaluate already has it.
struct NrCreateParams {
    uint32_t width, height;
    uint32_t preset;          // DLSSNR.Hint.Render.Preset: the 310.8 model has one set of weights, so 0..3 are the same
    float scalingRatio;       // DLSSNR.ScalingRatio: read but without effect in 310.8; 1
    NrTuning tuning;
};

// Written in full at every evaluate: the parameter block is shared, so nothing may be left over from an earlier call.
struct NrEvalParams {
    ID3D12Resource* color;    // NON_PIXEL_SHADER_RESOURCE
    ID3D12Resource* depth;    // NON_PIXEL_SHADER_RESOURCE, R32_FLOAT, at the guide size
    ID3D12Resource* mvec;     // NON_PIXEL_SHADER_RESOURCE, R16G16_FLOAT, at the guide size
    ID3D12Resource* output;   // UNORDERED_ACCESS, the size of color
    uint32_t width, height;
    uint32_t guideWidth, guideHeight;
    uint32_t depthInverted;
    uint32_t reset;           // 1: the model starts its history afresh
    float mvScaleX, mvScaleY;
    float scalingRatio;       // DLSSNR.ScalingRatio
    // DLSSNR.ControlMask, optional: RGBA8 (or R8 / R32F) at the colour size, NON_PIXEL_SHADER_RESOURCE. Black pixels come back exactly as they
    // went in, white ones get the model; one non-zero channel alone counts as black, so the value goes into R, G and B. It replaces the auto
    // mask. Null for none.
    ID3D12Resource* controlMask;
    NrTuning tuning;
};

extern "C" {
// Loads the model file; returns which of its entry points were found: 1 Init_Ext, 2 CreateFeature, 4 EvaluateFeature, 8 ReleaseFeature.
typedef int   (__cdecl* PFN_nrfwd_probe)(const wchar_t* snippetPath);
// Starts the model on the device with the driver's capability block; 1 = success, otherwise an NGX result.
typedef int   (__cdecl* PFN_nrfwd_init)(const wchar_t* snippetPath, const wchar_t* dataPath, ID3D12Device* device, void* capsBlock);
// Which of the block's setter slots takes a float (the engine finds it with the two probe calls below).
typedef void  (__cdecl* PFN_nrfwd_set_float_slot)(int setterSlot);
typedef void  (__cdecl* PFN_nrfwd_probe_float)(void* capsBlock, const char* key, float value, int setterSlot);
typedef int   (__cdecl* PFN_nrfwd_get_float)(void* capsBlock, const char* key, void* out8Bytes, int getterSlot);
// Makes the model feature, recording its setup on `cmd`; null on failure (nrfwd_last_result(1) says why).
typedef void* (__cdecl* PFN_nrfwd_create)(ID3D12GraphicsCommandList* cmd, void* capsBlock, const NrCreateParams* p);
// Records one run of the model on `cmd`; 1 = success, otherwise an NGX result.
typedef int   (__cdecl* PFN_nrfwd_evaluate)(ID3D12GraphicsCommandList* cmd, void* feature, void* capsBlock, const NrEvalParams* p);
typedef void  (__cdecl* PFN_nrfwd_release)(void* feature);
// The last result of: 0 init, 1 create, 2 evaluate, 3 the parameter population that follows init.
typedef int   (__cdecl* PFN_nrfwd_last_result)(int which);
}

#define NR_FORWARDER_FILENAME L"nvngx.dll_dlss5nr01.dll"
