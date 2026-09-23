#include "addon/dispatch_hook.h"
#include <MinHook.h>
#include <d3d11_4.h>
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <utility>
#include <vector>

namespace {
using DispatchFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, UINT);
constexpr int kDispatchSlot = 41;   // ID3D11DeviceContext::Dispatch in the function table
constexpr int kMaxHooks = 8;        // room to spare: the runtime has a handful

struct Hooked { void* target = nullptr; DispatchFn original = nullptr; };
Hooked g_hooks[kMaxHooks];
int g_hooked = 0;
std::atomic<DispatchHook::Callback> g_callback{ nullptr };
thread_local int t_nesting = 0;

// One detour per hooked entry point, each calling on to its own original. Only the outermost call on a thread runs the callback: a "refresh"
// stub jumps on into the real implementation, which is hooked too, so one Dispatch can pass through two of these.
template <int I> void STDMETHODCALLTYPE Detour(ID3D11DeviceContext* ctx, UINT x, UINT y, UINT z) {
    bool skip = false;
    if (t_nesting++ == 0) {
        if (const DispatchHook::Callback cb = g_callback.load(std::memory_order_acquire)) skip = cb(ctx, x, y, z);
    }
    if (!skip && g_hooks[I].original) g_hooks[I].original(ctx, x, y, z);
    --t_nesting;
}
template <int... I> constexpr void* DetourAt(int i, std::integer_sequence<int, I...>) {
    void* const all[] = { (void*)&Detour<I>... };
    return all[i];
}
void* DetourAt(int i) { return DetourAt(i, std::make_integer_sequence<int, kMaxHooks>{}); }

void Note(std::vector<void*>& found, ID3D11DeviceContext* ctx) {
    void* const fn = (*(void***)ctx)[kDispatchSlot];
    if (fn && std::find(found.begin(), found.end(), fn) == found.end()) found.push_back(fn);
}

// Finds the Dispatch implementations with short-lived devices of our own, going through every state that swaps a context's function table: plain,
// multithread protected (whose table starts as refresh stubs that the first state-setting call resolves), and deferred.
std::vector<void*> FindDispatchImplementations(const DispatchHook::LogFn& log) {
    std::vector<void*> found;
    auto noteResolved = [&](ID3D11DeviceContext* ctx) { Note(found, ctx); ctx->CSSetShader(nullptr, nullptr, 0); Note(found, ctx); };
    for (const D3D_DRIVER_TYPE type : { D3D_DRIVER_TYPE_WARP, D3D_DRIVER_TYPE_HARDWARE }) {
        for (const UINT flags : { 0u, (UINT)D3D11_CREATE_DEVICE_SINGLETHREADED }) {
            ID3D11Device* dev = nullptr; ID3D11DeviceContext* ctx = nullptr;
            const HRESULT hr = D3D11CreateDevice(nullptr, type, nullptr, flags, nullptr, 0, D3D11_SDK_VERSION, &dev, nullptr, &ctx);
            if (FAILED(hr)) {
                char text[112]; snprintf(text, sizeof text, "DispatchHook: a probe device (type %d, flags %u) failed: 0x%08x", (int)type, flags, (unsigned)hr);
                log(text); continue;
            }
            noteResolved(ctx);
            ID3D11Multithread* mt = nullptr;
            if (SUCCEEDED(ctx->QueryInterface(IID_PPV_ARGS(&mt)))) {
                for (const BOOL on : { TRUE, FALSE, TRUE }) { mt->SetMultithreadProtected(on); noteResolved(ctx); }
                mt->Release();
            }
            ID3D11DeviceContext* deferred = nullptr;
            if (SUCCEEDED(dev->CreateDeferredContext(0, &deferred))) { noteResolved(deferred); deferred->Release(); }
            ctx->Release(); dev->Release();
        }
        if (!found.empty()) break;   // the tables belong to the runtime, not the driver: WARP is enough, the graphics card is left alone
    }
    return found;
}
} // namespace

int DispatchHook::Install(Callback cb, LogFn log) {
    if (g_hooked) return g_hooked;
    const std::vector<void*> targets = FindDispatchImplementations(log);
    if (targets.empty()) { log("DispatchHook: no Dispatch implementation found"); return 0; }
    const MH_STATUS init = MH_Initialize();
    if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) { log("DispatchHook: MinHook could not start"); return 0; }
    for (void* target : targets) {
        if (g_hooked == kMaxHooks) break;
        Hooked& h = g_hooks[g_hooked];
        const MH_STATUS st = MH_CreateHook(target, DetourAt(g_hooked), (void**)&h.original);
        if (st != MH_OK) { char text[96]; snprintf(text, sizeof text, "DispatchHook: %p could not be hooked (%d)", target, (int)st); log(text); continue; }
        h.target = target; ++g_hooked;
    }
    g_callback.store(cb, std::memory_order_release);
    if (!g_hooked || MH_EnableHook(MH_ALL_HOOKS) != MH_OK) {
        log("DispatchHook: the hooks could not be switched on");
        if (g_hooked) Uninstall(); else MH_Uninitialize();
        return 0;
    }
    const uintptr_t base = (uintptr_t)GetModuleHandleW(L"d3d11.dll");
    for (int i = 0; i < g_hooked; ++i) {
        char text[112]; snprintf(text, sizeof text, "DispatchHook: hooked d3d11 Dispatch implementation %d at +0x%llx", i, (unsigned long long)((uintptr_t)g_hooks[i].target - base));
        log(text);
    }
    return g_hooked;
}

void DispatchHook::Uninstall() {
    g_callback.store(nullptr, std::memory_order_release);
    if (!g_hooked) return;
    MH_DisableHook(MH_ALL_HOOKS);
    for (int i = 0; i < g_hooked; ++i) { MH_RemoveHook(g_hooks[i].target); g_hooks[i] = {}; }
    g_hooked = 0;
    MH_Uninitialize();
}
