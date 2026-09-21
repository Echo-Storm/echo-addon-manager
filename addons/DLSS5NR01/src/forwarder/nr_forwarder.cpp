// nvngx.dll_dlss5nr01.dll — the only module that calls nvngx_dlssnr.dll.
//
// The snippet resolves the module owning its return address and rejects any caller whose module
// path does not contain "nvngx.dll" with FAIL_PlatformError before it reads a single argument
// (the driver core is _nvngx.dll). This DLL's name satisfies that check; nothing else here does.
//
// Every snippet result is stored in a volatile before return: a tail call becomes a jmp, the snippet
// then resolves the return address of *our* caller, and the check fails again.
//
// The parameter block is the driver core's capability block, driven through its vtable. The public
// header's Set overload order is ULL, float, double, uint, int, ID3D11Resource*, ID3D12Resource*,
// void* (slots 0-7), Get in the same order (8-15). In the live block resources only land through
// the 64-bit setter (slot 0) and the float setter is not reliably at slot 1, so the host probes it.
#include <windows.h>
#include <d3d12.h>
#include <cstring>
#include "forwarder/nr_api.h"

namespace {

using PFN_SetULL = void(__thiscall*)(void*, const char*, unsigned long long);
using PFN_SetF   = void(__thiscall*)(void*, const char*, float);
using PFN_SetUI  = void(__thiscall*)(void*, const char*, unsigned int);
using PFN_GetAny = int(__thiscall*)(void*, const char*, void*);

constexpr int VT_SET_ULL = 0;
constexpr int VT_SET_UI  = 3;
int g_floatSlot = 1;

inline void** VT(void* block) { return *reinterpret_cast<void***>(block); }
void setUI (void* b, const char* n, unsigned v)      { reinterpret_cast<PFN_SetUI >(VT(b)[VT_SET_UI])(b, n, v); }
void setF  (void* b, const char* n, float v)         { reinterpret_cast<PFN_SetF  >(VT(b)[g_floatSlot])(b, n, v); }
void setPtr(void* b, const char* n, const void* v)   { reinterpret_cast<PFN_SetULL>(VT(b)[VT_SET_ULL])(b, n, (unsigned long long)(uintptr_t)v); }

using PFN_InitExt  = int(__cdecl*)(unsigned long long, const wchar_t*, ID3D12Device*, int, const void*);
using PFN_Create   = int(__cdecl*)(ID3D12GraphicsCommandList*, int, const void*, void**);
using PFN_Evaluate = int(__cdecl*)(ID3D12GraphicsCommandList*, const void*, const void*, void*);
using PFN_Release  = int(__cdecl*)(void*);
using PFN_Populate = int(__cdecl*)(void*);   // NVSDK_NGX_D3D12_PopulateParameters_Impl: the snippet plants its callbacks in the block

struct Snippet {
    HMODULE      module   = nullptr;
    PFN_InitExt  init     = nullptr;
    PFN_Create   create   = nullptr;
    PFN_Evaluate evaluate = nullptr;
    PFN_Release  release  = nullptr;
    PFN_Populate populate = nullptr;
    bool         inited   = false;
} g;

int g_last[4] = { 0, 0, 0, 0 };   // init, create, evaluate, populate
using PFN_RatioCb = int(__cdecl*)(void*);

bool Load(const wchar_t* path) {
    if (g.module) return g.create != nullptr;
    g.module = LoadLibraryExW(path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!g.module) return false;
    g.init     = (PFN_InitExt) GetProcAddress(g.module, "NVSDK_NGX_D3D12_Init_Ext");
    g.create   = (PFN_Create)  GetProcAddress(g.module, "NVSDK_NGX_D3D12_CreateFeature");
    g.evaluate = (PFN_Evaluate)GetProcAddress(g.module, "NVSDK_NGX_D3D12_EvaluateFeature");
    g.release  = (PFN_Release) GetProcAddress(g.module, "NVSDK_NGX_D3D12_ReleaseFeature");
    g.populate = (PFN_Populate)GetProcAddress(g.module, "NVSDK_NGX_D3D12_PopulateParameters_Impl");
    return g.create && g.evaluate;
}

constexpr unsigned long long kAppId      = 0x24480451ull;
constexpr int                kApiVersion = 0x0000015;
constexpr int                kFeatureNR  = 18;

} // namespace

extern "C" {

__declspec(dllexport) int __cdecl nrfwd_probe(const wchar_t* snippetPath) {
    if (!Load(snippetPath)) return 0;
    return (g.init ? 1 : 0) | (g.create ? 2 : 0) | (g.evaluate ? 4 : 0) | (g.release ? 8 : 0);
}

__declspec(dllexport) void __cdecl nrfwd_set_float_slot(int slot) {
    if (slot >= 0 && slot < 8) g_floatSlot = slot;
}

__declspec(dllexport) void __cdecl nrfwd_probe_float(void* block, const char* key, float value, int setterSlot) {
    if (!block || setterSlot < 0 || setterSlot >= 8) return;
    reinterpret_cast<PFN_SetF>(VT(block)[setterSlot])(block, key, value);
}

__declspec(dllexport) int __cdecl nrfwd_get_float(void* block, const char* key, void* out8, int getterSlot) {
    if (!block || getterSlot < 8 || getterSlot >= 16) return -1;
    volatile int r = reinterpret_cast<PFN_GetAny>(VT(block)[getterSlot])(block, key, out8);
    return r;
}

__declspec(dllexport) int __cdecl nrfwd_init(const wchar_t* snippetPath, const wchar_t* dataPath, ID3D12Device* device, void* block) {
    if (!Load(snippetPath) || !g.init) return -1;
    if (g.inited) return 1;
    volatile int r = g.init(kAppId, dataPath, device, kApiVersion, block);
    g_last[0] = r;
    g.inited = (r == 1);
    if (g.inited && g.populate) { volatile int pr = g.populate(block); g_last[3] = pr; }   // plants DLSSNRComputeScalingRatioCallback / DLSSNRGetStatsCallback
    return r;
}

static void setTuning(void* block, const NrTuning& t) {
    setF (block, "DLSSNR.Intensity",               t.intensity);
    setUI(block, "DLSSNR.Style",                   t.style);
    setF (block, "DLSSNR.LocalStructureStrength",  t.localStructure);
    setF (block, "DLSSNR.LocalToneStrength",       t.localTone);
    setF (block, "DLSSNR.SkinStructureStrength",   t.skinStructure);
    setUI(block, "DLSSNR.UseAutoMask",  t.useAutoMask);
    setUI(block, "DLSSNR.UICorrection", t.uiCorrection);
}

__declspec(dllexport) void* __cdecl nrfwd_create(ID3D12GraphicsCommandList* cmd, void* block, const NrCreateParams* c) {
    if (!g.create || !g.inited || !cmd || !block || !c) return nullptr;
    setUI(block, "DLSSNR.Enabled", 1u);
    setUI(block, "DLSSNR.Width",  c->width);
    setUI(block, "DLSSNR.Height", c->height);
    setF (block, "DLSSNR.ScalingRatio", c->scalingRatio);
    setUI(block, "CreationNodeMask",   1u);
    setUI(block, "VisibilityNodeMask", 1u);
    // Written unconditionally, zero included: the block outlives the feature and would otherwise
    // keep whatever the previous create left in it.
    setUI(block, "DLSSNR.Hint.Render.Preset", c->preset);
    setTuning(block, c->tuning);
    void* handle = nullptr;
    volatile int r = g.create(cmd, kFeatureNR, block, &handle);
    g_last[1] = r;
    return (r == 1) ? handle : nullptr;
}

__declspec(dllexport) int __cdecl nrfwd_evaluate(ID3D12GraphicsCommandList* cmd, void* feature, void* block, const NrEvalParams* e) {
    if (!g.evaluate || !feature || !cmd || !block || !e) return -1;
    setPtr(block, "DLSSNR.Color",  e->color);
    setPtr(block, "DLSSNR.Depth",  e->depth);
    setPtr(block, "DLSSNR.MVec",   e->mvec);
    setPtr(block, "DLSSNR.Output", e->output);
    setUI(block, "DLSSNR.Enabled", 1u);
    setUI(block, "DLSSNR.Width",  e->width);
    setUI(block, "DLSSNR.Height", e->height);
    setUI(block, "DLSSNR.DepthInverted", e->depthInverted);
    setUI(block, "DLSSNR.Reset", e->reset);
    setUI(block, "DLSSNR.ColorSubrectBaseX", 0u);  setUI(block, "DLSSNR.ColorSubrectBaseY", 0u);
    setUI(block, "DLSSNR.ColorSubrectWidth", e->width);  setUI(block, "DLSSNR.ColorSubrectHeight", e->height);
    setUI(block, "DLSSNR.OutputSubrectBaseX", 0u); setUI(block, "DLSSNR.OutputSubrectBaseY", 0u);
    setUI(block, "DLSSNR.OutputSubrectWidth", e->width); setUI(block, "DLSSNR.OutputSubrectHeight", e->height);
    setUI(block, "DLSSNR.DepthSubrectBaseX", 0u);  setUI(block, "DLSSNR.DepthSubrectBaseY", 0u);
    setUI(block, "DLSSNR.DepthSubrectWidth", e->guideWidth);  setUI(block, "DLSSNR.DepthSubrectHeight", e->guideHeight);
    setUI(block, "DLSSNR.MVecSubrectBaseX", 0u);   setUI(block, "DLSSNR.MVecSubrectBaseY", 0u);
    setUI(block, "DLSSNR.MVecSubrectWidth", e->guideWidth);   setUI(block, "DLSSNR.MVecSubrectHeight", e->guideHeight);
    setF(block, "DLSSNR.MVecScaleX", e->mvScaleX);
    setF(block, "DLSSNR.MVecScaleY", e->mvScaleY);
    setF(block, "DLSSNR.ScalingRatio", e->scalingRatio);
    // Always written (null included): the block outlives every texture we ever handed it.
    setPtr(block, "DLSSNR.ControlMask", e->controlMask);
    setUI(block, "DLSSNR.ControlMaskSubrectBaseX", 0u); setUI(block, "DLSSNR.ControlMaskSubrectBaseY", 0u);
    setUI(block, "DLSSNR.ControlMaskSubrectWidth",  e->controlMask ? e->width  : 0u);
    setUI(block, "DLSSNR.ControlMaskSubrectHeight", e->controlMask ? e->height : 0u);
    setTuning(block, e->tuning);
    volatile int r = g.evaluate(cmd, feature, block, nullptr);
    g_last[2] = r;
    return r;
}

__declspec(dllexport) void __cdecl nrfwd_release(void* feature) {
    if (g.release && feature) { volatile int r = g.release(feature); (void)r; }
}

__declspec(dllexport) int __cdecl nrfwd_scaling_ratio(void* block, unsigned perfQuality, float* outRatio) {
    if (!block) return -1;
    setUI(block, "PerfQualityValue", perfQuality);
    void* cb = nullptr;
    int gr = reinterpret_cast<PFN_GetAny>(VT(block)[8])(block, "DLSSNRComputeScalingRatioCallback", &cb);   // Get(void**)
    if (gr != 1 || !cb) return -2;
    volatile int r = reinterpret_cast<PFN_RatioCb>(cb)(block);
    if (outRatio) { alignas(8) unsigned char b[8] = {}; if (reinterpret_cast<PFN_GetAny>(VT(block)[14])(block, "DLSSNR.ScalingRatio", b) == 1) memcpy(outRatio, b, 4); }
    return r;
}

__declspec(dllexport) int __cdecl nrfwd_last_result(int which) {
    return (which >= 0 && which < 4) ? g_last[which] : 0;
}

} // extern "C"

BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }
