// PresentHook — inline-hooks dxgi.dll's IDXGISwapChain::Present / Present1 so the addon sees every frame Lossless
// Scaling presents (real and generated) right before it goes to the screen. The entry points are taken from a
// throwaway swap chain on the same device: every swap chain of the same DXGI runtime shares them.
#pragma once
#include <d3d11.h>
#include <dxgi1_2.h>
#include <functional>

namespace PresentHook {
    using LogFn = std::function<void(const char*)>;
    // Called before the original Present, on the presenting thread. Not called for DXGI_PRESENT_TEST.
    using Callback = void(*)(IDXGISwapChain* sc, void* user);
    bool Install(ID3D11Device* dev, Callback cb, void* user, LogFn log);   // MinHook must already be initialised
    void Uninstall();
    bool Installed();
    unsigned Hits();
    void DumpState(LogFn log);   // diagnostics: hit count and the first bytes at the hooked entry points
}
