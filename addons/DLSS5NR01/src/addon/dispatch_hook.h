// DispatchHook: a code hook on every ID3D11DeviceContext::Dispatch implementation in d3d11.dll, so the addon sees each of Lossless Scaling's
// compute passes before it runs.
//
// Why not a vtable patch (the manager's own dispatch callback): each context has its own copy of its function table, and
// ID3D11Multithread::SetMultithreadProtected() swaps that copy for one with other entry points, dropping any patched slot. Lossless Scaling turns
// multithread protection on (through Windows.Graphics.Capture), so a vtable patch never fires. Hooking the code itself covers every variant, on
// every device and context in the process.
#pragma once
#include <d3d11.h>
#include <functional>

namespace DispatchHook {
    using LogFn = std::function<void(const char*)>;
    // Called before the original Dispatch, on the calling thread. Return true to skip it (the addon never does).
    using Callback = bool (*)(ID3D11DeviceContext* ctx, UINT x, UINT y, UINT z);
    // Hooks every Dispatch implementation found. Returns how many (0 = none, the hook is not in place).
    int Install(Callback cb, LogFn log);
    void Uninstall();
}
