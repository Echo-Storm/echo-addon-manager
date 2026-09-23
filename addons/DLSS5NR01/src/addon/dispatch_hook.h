// DispatchHook — inline-hooks d3d11.dll's ID3D11DeviceContext::Dispatch implementations.
// Why not patch the context vtable (what the manager's own callback does): on current Windows builds each context
// carries a private copy of its vtable, and ID3D11Multithread::SetMultithreadProtected() rewrites that
// copy with a different set of entry points, silently discarding any patched slot. Lossless Scaling
// (via Windows.Graphics.Capture) enables multithread protection, so vtable patches never fire.
// Hooking the code in d3d11.dll catches every variant, on every device and context in the process.
#pragma once
#include <d3d11.h>
#include <functional>

namespace DispatchHook {
    using LogFn = std::function<void(const char*)>;
    // Return true from the callback to skip the original Dispatch (we never do).
    using Callback = bool(*)(ID3D11DeviceContext* ctx, UINT x, UINT y, UINT z, void* user);
    // Returns the number of distinct d3d11 entry points hooked (0 = failure).
    int  Install(Callback cb, void* user, LogFn log);
    void Uninstall();
    int  Count();
}
