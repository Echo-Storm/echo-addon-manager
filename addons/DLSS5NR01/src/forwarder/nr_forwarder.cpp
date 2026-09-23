// nvngx.dll_dlss5nr01.dll: the one module that calls the model file (nvngx_dlssnr.dll, "the snippet").
//
// Why a module of its own: the snippet looks up the module its caller's return address lies in, and refuses (FAIL_PlatformError, before it
// reads any argument) a caller whose module path does not contain "nvngx.dll". The driver's own core is _nvngx.dll; this DLL's name passes the
// same check. For the same reason every call into the snippet keeps its result in a volatile: otherwise the compiler may end the function with
// a jump into the snippet instead of a call, and the snippet then sees the return address of whoever called us.
//
// The parameter block is the driver core's capability block, used through its function table. In the public header the Set overloads come in
// the order unsigned long long, float, double, unsigned, int, ID3D11Resource*, ID3D12Resource*, void* (slots 0-7), and Get in the same order
// (8-15). In the block the driver really hands out, resources only arrive through the 64-bit setter (slot 0), and the float setter is not
// always slot 1: the engine finds it and tells us (nrfwd_set_float_slot).
#include <windows.h>
#include <d3d12.h>
#include <cstdio>
#include "forwarder/nr_api.h"

namespace {

constexpr unsigned long long kAppId = 0x24480451ull;   // the application id the model is started with
constexpr int kNgxApiVersion = 0x15;
constexpr int kNeuralRenderingFeature = 18;
constexpr int kSetU64Slot = 0, kSetUIntSlot = 3, kFirstGetSlot = 8, kSlots = 16;

int g_floatSlot = 1;
int g_lastResult[4] = {};   // init, create, evaluate, population

using SetU64Fn = void(__thiscall*)(void*, const char*, unsigned long long);
using SetFloatFn = void(__thiscall*)(void*, const char*, float);
using SetUIntFn = void(__thiscall*)(void*, const char*, unsigned int);
using GetFn = int(__thiscall*)(void*, const char*, void*);

void* Slot(void* block, int i) { return (*static_cast<void***>(block))[i]; }
void SetUInt(void* block, const char* key, unsigned value) { reinterpret_cast<SetUIntFn>(Slot(block, kSetUIntSlot))(block, key, value); }
void SetFloat(void* block, const char* key, float value) { reinterpret_cast<SetFloatFn>(Slot(block, g_floatSlot))(block, key, value); }
void SetResource(void* block, const char* key, const void* resource) {
    reinterpret_cast<SetU64Fn>(Slot(block, kSetU64Slot))(block, key, static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(resource)));
}
// A sub-rectangle at the origin: the four keys <name>SubrectBaseX/Y and <name>SubrectWidth/Height.
void SetSubrect(void* block, const char* name, unsigned w, unsigned h) {
    static const char* const suffix[4] = { "BaseX", "BaseY", "Width", "Height" };
    const unsigned value[4] = { 0u, 0u, w, h };
    char key[64];
    for (int i = 0; i < 4; ++i) {
        snprintf(key, sizeof key, "DLSSNR.%sSubrect%s", name, suffix[i]);
        SetUInt(block, key, value[i]);
    }
}
void SetTuning(void* block, const NrTuning& t) {
    SetFloat(block, "DLSSNR.Intensity", t.intensity);
    SetUInt(block, "DLSSNR.Style", t.style);
    SetFloat(block, "DLSSNR.LocalStructureStrength", t.localStructure);
    SetFloat(block, "DLSSNR.LocalToneStrength", t.localTone);
    SetFloat(block, "DLSSNR.SkinStructureStrength", t.skinStructure);
    SetUInt(block, "DLSSNR.UseAutoMask", t.useAutoMask);
    SetUInt(block, "DLSSNR.UICorrection", t.uiCorrection);
}

// The snippet's entry points.
struct Snippet {
    HMODULE module = nullptr;
    int (__cdecl* init)(unsigned long long appId, const wchar_t* dataPath, ID3D12Device*, int apiVersion, const void* block) = nullptr;
    int (__cdecl* create)(ID3D12GraphicsCommandList*, int feature, const void* block, void** handle) = nullptr;
    int (__cdecl* evaluate)(ID3D12GraphicsCommandList*, const void* handle, const void* block, void* progress) = nullptr;
    int (__cdecl* release)(void* handle) = nullptr;
    int (__cdecl* populate)(void* block) = nullptr;   // PopulateParameters_Impl: the snippet puts its callbacks in the block
    bool started = false;
} g_snippet;

template <class Fn> void Resolve(Fn& fn, const char* name) { fn = reinterpret_cast<Fn>(GetProcAddress(g_snippet.module, name)); }

bool Load(const wchar_t* path) {
    if (!g_snippet.module) {
        g_snippet.module = LoadLibraryExW(path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!g_snippet.module) return false;
        Resolve(g_snippet.init, "NVSDK_NGX_D3D12_Init_Ext");
        Resolve(g_snippet.create, "NVSDK_NGX_D3D12_CreateFeature");
        Resolve(g_snippet.evaluate, "NVSDK_NGX_D3D12_EvaluateFeature");
        Resolve(g_snippet.release, "NVSDK_NGX_D3D12_ReleaseFeature");
        Resolve(g_snippet.populate, "NVSDK_NGX_D3D12_PopulateParameters_Impl");
    }
    return g_snippet.create && g_snippet.evaluate;
}

} // namespace

extern "C" {

__declspec(dllexport) int __cdecl nrfwd_probe(const wchar_t* snippetPath) {
    if (!Load(snippetPath)) return 0;
    return (g_snippet.init ? 1 : 0) | (g_snippet.create ? 2 : 0) | (g_snippet.evaluate ? 4 : 0) | (g_snippet.release ? 8 : 0);
}

__declspec(dllexport) void __cdecl nrfwd_set_float_slot(int slot) {
    if (slot >= 0 && slot < kFirstGetSlot) g_floatSlot = slot;
}

__declspec(dllexport) void __cdecl nrfwd_probe_float(void* block, const char* key, float value, int setterSlot) {
    if (block && setterSlot >= 0 && setterSlot < kFirstGetSlot) reinterpret_cast<SetFloatFn>(Slot(block, setterSlot))(block, key, value);
}

__declspec(dllexport) int __cdecl nrfwd_get_float(void* block, const char* key, void* out8Bytes, int getterSlot) {
    if (!block || getterSlot < kFirstGetSlot || getterSlot >= kSlots) return -1;
    volatile int result = reinterpret_cast<GetFn>(Slot(block, getterSlot))(block, key, out8Bytes);
    return result;
}

__declspec(dllexport) int __cdecl nrfwd_init(const wchar_t* snippetPath, const wchar_t* dataPath, ID3D12Device* device, void* block) {
    if (!Load(snippetPath) || !g_snippet.init) return -1;
    if (g_snippet.started) return 1;
    volatile int result = g_snippet.init(kAppId, dataPath, device, kNgxApiVersion, block);
    g_lastResult[0] = result;
    g_snippet.started = result == 1;
    if (g_snippet.started && g_snippet.populate) { volatile int populated = g_snippet.populate(block); g_lastResult[3] = populated; }
    return result;
}

__declspec(dllexport) void* __cdecl nrfwd_create(ID3D12GraphicsCommandList* cmd, void* block, const NrCreateParams* p) {
    if (!g_snippet.create || !g_snippet.started || !cmd || !block || !p) return nullptr;
    SetUInt(block, "DLSSNR.Enabled", 1u);
    SetUInt(block, "DLSSNR.Width", p->width);
    SetUInt(block, "DLSSNR.Height", p->height);
    SetFloat(block, "DLSSNR.ScalingRatio", p->scalingRatio);
    SetUInt(block, "CreationNodeMask", 1u);
    SetUInt(block, "VisibilityNodeMask", 1u);
    SetUInt(block, "DLSSNR.Hint.Render.Preset", p->preset);   // written even when 0: the block outlives the feature and keeps old values
    SetTuning(block, p->tuning);
    void* feature = nullptr;
    volatile int result = g_snippet.create(cmd, kNeuralRenderingFeature, block, &feature);
    g_lastResult[1] = result;
    return result == 1 ? feature : nullptr;
}

__declspec(dllexport) int __cdecl nrfwd_evaluate(ID3D12GraphicsCommandList* cmd, void* feature, void* block, const NrEvalParams* p) {
    if (!g_snippet.evaluate || !feature || !cmd || !block || !p) return -1;
    SetResource(block, "DLSSNR.Color", p->color);
    SetResource(block, "DLSSNR.Depth", p->depth);
    SetResource(block, "DLSSNR.MVec", p->mvec);
    SetResource(block, "DLSSNR.Output", p->output);
    SetUInt(block, "DLSSNR.Enabled", 1u);
    SetUInt(block, "DLSSNR.Width", p->width);
    SetUInt(block, "DLSSNR.Height", p->height);
    SetUInt(block, "DLSSNR.DepthInverted", p->depthInverted);
    SetUInt(block, "DLSSNR.Reset", p->reset);
    SetSubrect(block, "Color", p->width, p->height);
    SetSubrect(block, "Output", p->width, p->height);
    SetSubrect(block, "Depth", p->guideWidth, p->guideHeight);
    SetSubrect(block, "MVec", p->guideWidth, p->guideHeight);
    SetFloat(block, "DLSSNR.MVecScaleX", p->mvScaleX);
    SetFloat(block, "DLSSNR.MVecScaleY", p->mvScaleY);
    SetFloat(block, "DLSSNR.ScalingRatio", p->scalingRatio);
    SetResource(block, "DLSSNR.ControlMask", p->controlMask);   // null too: the block outlives every texture it was ever given
    SetSubrect(block, "ControlMask", p->controlMask ? p->width : 0u, p->controlMask ? p->height : 0u);
    SetTuning(block, p->tuning);
    volatile int result = g_snippet.evaluate(cmd, feature, block, nullptr);
    g_lastResult[2] = result;
    return result;
}

__declspec(dllexport) void __cdecl nrfwd_release(void* feature) {
    if (g_snippet.release && feature) { volatile int result = g_snippet.release(feature); (void)result; }
}

__declspec(dllexport) int __cdecl nrfwd_last_result(int which) {
    return which >= 0 && which < 4 ? g_lastResult[which] : 0;
}

} // extern "C"

BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }
