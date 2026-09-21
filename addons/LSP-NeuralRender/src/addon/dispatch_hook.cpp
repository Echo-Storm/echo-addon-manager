#include "addon/dispatch_hook.h"
#include <MinHook.h>
#include <d3d11_4.h>
#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <vector>
#include <algorithm>

#pragma comment(lib, "d3d11.lib")

namespace {
using PFN_Dispatch = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, UINT);
constexpr int kMax = 8;
PFN_Dispatch g_orig[kMax] = {};
void* g_target[kMax] = {};
int g_count = 0;
DispatchHook::Callback g_cb = nullptr;
void* g_user = nullptr;
thread_local int t_depth = 0;

unsigned g_variantHits[kMax] = {};
template <int N> void STDMETHODCALLTYPE Detour(ID3D11DeviceContext* ctx, UINT x, UINT y, UINT z) {
    g_variantHits[N]++;
    // Only the outermost entry invokes the callback: a refresh stub tail-jumps into the resolved
    // implementation (also hooked), so the guard must stay raised across the original call.
    bool skip = false;
    if (t_depth++ == 0 && g_cb) skip = g_cb(ctx, x, y, z, g_user);
    if (!skip && g_orig[N]) g_orig[N](ctx, x, y, z);
    --t_depth;
}
void* DetourFor(int i) {
    switch (i) {
    case 0: return (void*)&Detour<0>; case 1: return (void*)&Detour<1>; case 2: return (void*)&Detour<2>; case 3: return (void*)&Detour<3>;
    case 4: return (void*)&Detour<4>; case 5: return (void*)&Detour<5>; case 6: return (void*)&Detour<6>; default: return (void*)&Detour<7>;
    }
}

void Grab(std::vector<void*>& out, ID3D11DeviceContext* c) {
    if (!c) return;
    void* p = (*(void***)c)[41];   // ID3D11DeviceContext::Dispatch
    if (p && std::find(out.begin(), out.end(), p) == out.end()) out.push_back(p);
}

// Create throwaway devices and walk every state that swaps the context's function table.
void Collect(std::vector<void*>& out, const DispatchHook::LogFn& log) {
    const D3D_DRIVER_TYPE types[] = { D3D_DRIVER_TYPE_WARP, D3D_DRIVER_TYPE_HARDWARE };
    const UINT flagSets[] = { 0u, (UINT)D3D11_CREATE_DEVICE_SINGLETHREADED };
    for (D3D_DRIVER_TYPE t : types) {
        for (UINT flags : flagSets) {
            ID3D11Device* dev = nullptr; ID3D11DeviceContext* ctx = nullptr;
            HRESULT hr = D3D11CreateDevice(nullptr, t, nullptr, flags, nullptr, 0, D3D11_SDK_VERSION, &dev, nullptr, &ctx);
            if (FAILED(hr)) { char b[96]; snprintf(b, sizeof b, "DispatchHook: probe device (type %d flags %u) failed 0x%08x", (int)t, flags, (unsigned)hr); log(b); continue; }
            // After SetMultithreadProtected() the table holds "refresh" stubs; the first call through any
            // slot rewrites the whole table with the real variant. Grab the stub, poke, grab the resolved entry.
            // (a state-setting call is needed; plain getters do not refresh the table)
            auto grabResolved = [&](ID3D11DeviceContext* c) { Grab(out, c); c->CSSetShader(nullptr, nullptr, 0); Grab(out, c); };
            grabResolved(ctx);
            ID3D11Multithread* mt = nullptr;
            if (SUCCEEDED(ctx->QueryInterface(IID_PPV_ARGS(&mt))) && mt) {
                mt->SetMultithreadProtected(TRUE);  grabResolved(ctx);
                mt->SetMultithreadProtected(FALSE); grabResolved(ctx);
                mt->SetMultithreadProtected(TRUE);  grabResolved(ctx);
                mt->Release();
            }
            ID3D11DeviceContext* def = nullptr;
            if (SUCCEEDED(dev->CreateDeferredContext(0, &def)) && def) { grabResolved(def); def->Release(); }
            ctx->Release(); dev->Release();
        }
        if (!out.empty()) break;   // the runtime's tables are driver-independent; no need to touch the hardware
    }
}
} // namespace

int DispatchHook::Install(Callback cb, void* user, LogFn log) {
    if (g_count) return g_count;
    std::vector<void*> targets; Collect(targets, log);
    if (targets.empty()) { log("DispatchHook: no Dispatch entry points found"); return 0; }
    MH_STATUS st = MH_Initialize();
    if (st != MH_OK && st != MH_ERROR_ALREADY_INITIALIZED) { log("DispatchHook: MH_Initialize failed"); return 0; }
    g_cb = cb; g_user = user;
    for (void* t : targets) {
        if (g_count >= kMax) break;
        int i = g_count;
        st = MH_CreateHook(t, DetourFor(i), (void**)&g_orig[i]);
        if (st != MH_OK) { char b[96]; snprintf(b, sizeof b, "DispatchHook: MH_CreateHook(%p) -> %d", t, (int)st); log(b); continue; }
        g_target[i] = t; g_count++;
    }
    if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK) { log("DispatchHook: MH_EnableHook failed"); Uninstall(); return 0; }
    uintptr_t base = (uintptr_t)GetModuleHandleW(L"d3d11.dll");
    for (int i = 0; i < g_count; ++i) { char b[128]; snprintf(b, sizeof b, "DispatchHook: hooked d3d11!Dispatch variant %d at +0x%llx", i, (unsigned long long)((uintptr_t)g_target[i] - base)); log(b); }
    return g_count;
}

void DispatchHook::Uninstall() {
    if (!g_count) return;
    MH_DisableHook(MH_ALL_HOOKS);
    for (int i = 0; i < g_count; ++i) { MH_RemoveHook(g_target[i]); g_target[i] = nullptr; g_orig[i] = nullptr; }
    g_count = 0; g_cb = nullptr; g_user = nullptr;
    MH_Uninitialize();
}

int DispatchHook::Count() { return g_count; }
unsigned DispatchHook::VariantHits(int i) { return (i >= 0 && i < kMax) ? g_variantHits[i] : 0; }
