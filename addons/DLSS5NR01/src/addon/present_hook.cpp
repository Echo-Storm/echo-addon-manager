#include "addon/present_hook.h"
#include <windows.h>
#include <cstdio>

#pragma comment(lib, "dxgi.lib")

// Why a vtable patch and not a code hook: something else in the process (the NVIDIA overlay/driver hooks Present
// lazily at the first present) patches dxgi!Present after us and chains to a pristine copy of the code, which
// silently removes an inline hook installed before it (seen in the offline host: our JMP replaced, hits stuck at 1).
// The swap chain's vtable is a static table in dxgi.dll shared by every swap chain of that class and it is not
// refreshed per object (checked across presents), so a patched slot keeps working and calls whatever code hook
// sits on the function.
namespace {
using PFN_Present  = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using PFN_Present1 = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
PFN_Present g_origPresent = nullptr; PFN_Present1 g_origPresent1 = nullptr;
void** g_table = nullptr;   // the patched vtable
PresentHook::Callback g_cb = nullptr; void* g_user = nullptr;
PresentHook::LogFn g_log;
thread_local int t_depth = 0;
unsigned g_hits = 0;

HRESULT STDMETHODCALLTYPE HookPresent(IDXGISwapChain* sc, UINT sync, UINT flags) {
    g_hits++;
    PresentHook::Callback cb = g_cb;   // Uninstall() can null it from another thread between the test and the call
    if (t_depth++ == 0 && cb && !(flags & DXGI_PRESENT_TEST)) cb(sc, g_user);
    PFN_Present orig = g_origPresent;
    HRESULT hr = orig ? orig(sc, sync, flags) : S_OK;
    --t_depth; return hr;
}
HRESULT STDMETHODCALLTYPE HookPresent1(IDXGISwapChain1* sc, UINT sync, UINT flags, const DXGI_PRESENT_PARAMETERS* pp) {
    g_hits++;
    PresentHook::Callback cb = g_cb;
    if (t_depth++ == 0 && cb && !(flags & DXGI_PRESENT_TEST)) cb(sc, g_user);
    PFN_Present1 orig = g_origPresent1;
    HRESULT hr = orig ? orig(sc, sync, flags, pp) : S_OK;
    --t_depth; return hr;
}
bool PatchSlot(void** table, int slot, void* detour, void** orig) {
    DWORD old = 0;
    if (!VirtualProtect(table + slot, sizeof(void*), PAGE_READWRITE, &old)) return false;
    *orig = table[slot]; table[slot] = detour;
    VirtualProtect(table + slot, sizeof(void*), old, &old);
    return true;
}
} // namespace

bool PresentHook::Install(ID3D11Device* dev, Callback cb, void* user, LogFn log) {
    if (g_table) return true;
    IDXGIDevice* dx = nullptr; IDXGIAdapter* ad = nullptr; IDXGIFactory2* fac = nullptr; IDXGISwapChain1* sc = nullptr; HWND hwnd = nullptr;
    if (FAILED(dev->QueryInterface(IID_PPV_ARGS(&dx))) || !dx) { log("PresentHook: IDXGIDevice missing"); return false; }
    HRESULT hr = dx->GetAdapter(&ad); dx->Release();
    if (FAILED(hr) || !ad) { log("PresentHook: adapter missing"); return false; }
    hr = ad->GetParent(IID_PPV_ARGS(&fac)); ad->Release();
    if (FAILED(hr) || !fac) { log("PresentHook: IDXGIFactory2 missing"); return false; }
    // A throwaway swap chain of the same class LS uses (a window swap chain): it hands us the shared vtable.
    WNDCLASSEXW wc{ sizeof wc }; wc.lpfnWndProc = DefWindowProcW; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"NrPresentProbe";
    RegisterClassExW(&wc);
    hwnd = CreateWindowExW(0, wc.lpszClassName, L"", WS_POPUP, 0, 0, 16, 16, nullptr, nullptr, wc.hInstance, nullptr);
    DXGI_SWAP_CHAIN_DESC1 d{}; d.Width = 16; d.Height = 16; d.Format = DXGI_FORMAT_B8G8R8A8_UNORM; d.SampleDesc.Count = 1;
    d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; d.BufferCount = 2; d.Scaling = DXGI_SCALING_NONE; d.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL; d.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    hr = hwnd ? fac->CreateSwapChainForHwnd(dev, hwnd, &d, nullptr, nullptr, &sc) : E_FAIL;
    if (FAILED(hr)) {
        d.Scaling = DXGI_SCALING_STRETCH; d.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
        hr = fac->CreateSwapChainForComposition(dev, &d, nullptr, &sc);
    }
    fac->Release();
    if (FAILED(hr) || !sc) { char b[96]; snprintf(b, sizeof b, "PresentHook: probe swap chain failed 0x%08x", (unsigned)hr); log(b); if (hwnd) DestroyWindow(hwnd); return false; }
    void** table = *(void***)sc;
    g_cb = cb; g_user = user; g_log = log;
    if (!PatchSlot(table, 8, (void*)&HookPresent, (void**)&g_origPresent)) { log("PresentHook: VirtualProtect on the swap chain vtable failed"); sc->Release(); if (hwnd) DestroyWindow(hwnd); g_cb = nullptr; return false; }
    if (!PatchSlot(table, 22, (void*)&HookPresent1, (void**)&g_origPresent1)) g_origPresent1 = nullptr;
    g_table = table;
    sc->Release(); if (hwnd) DestroyWindow(hwnd);
    uintptr_t base = (uintptr_t)GetModuleHandleW(L"dxgi.dll");
    char b[200]; snprintf(b, sizeof b, "PresentHook: patched swap chain vtable %p: Present (dxgi+0x%llx) and Present1 (dxgi+0x%llx)", (void*)table, (unsigned long long)((uintptr_t)g_origPresent - base), (unsigned long long)((uintptr_t)g_origPresent1 - base)); log(b);
    return true;
}

void PresentHook::Uninstall() {
    if (!g_table) return;
    void* cur = nullptr;
    if (g_table[8] == (void*)&HookPresent) PatchSlot(g_table, 8, (void*)g_origPresent, &cur);     // only undo our own patch
    if (g_origPresent1 && g_table[22] == (void*)&HookPresent1) PatchSlot(g_table, 22, (void*)g_origPresent1, &cur);
    // Keep g_origPresent/g_origPresent1: something installed after us (the NVIDIA overlay, ReShade) may still call
    // through our detour, and it must find the real function, not null. Only the callback is turned off.
    g_table = nullptr; g_cb = nullptr; g_user = nullptr;
}
bool PresentHook::Installed() { return g_table != nullptr; }
unsigned PresentHook::Hits() { return g_hits; }
void PresentHook::DumpState(LogFn log) {
    if (!g_table) { log("PresentHook: not installed"); return; }
    char t[200]; snprintf(t, sizeof t, "PresentHook: hits %u; vtable %p slot 8 %s, slot 22 %s", g_hits, (void*)g_table, g_table[8] == (void*)&HookPresent ? "ours" : "REPLACED", g_table[22] == (void*)&HookPresent1 ? "ours" : "replaced/none");
    log(t);
}
