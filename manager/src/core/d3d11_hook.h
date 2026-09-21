#pragma once
#include <cstdint>

namespace lsproxy {
class HostImpl;
}

namespace D3D11Hook {

/// Install IAT hook on D3D11CreateDevice in Lossless_original.dll.
/// When the device is created, captures pointers and sets them on the host.
/// Also installs a vtable hook on ID3D11DeviceContext::Dispatch for
/// pre/post dispatch callbacks.
void Initialize(lsproxy::HostImpl* host);
void Shutdown();

void* GetCurrentComputeShader();
uint32_t GetDispatchCount();

} // namespace D3D11Hook
