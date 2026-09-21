#pragma once
#include <d3d11.h>
#include <dxgi.h>

namespace lsproxy {
namespace window {

// The manager window's own D3D11 device, swap chain and back-buffer view. It is separate from Lossless Scaling's (which the game capture uses),
// and is created from the system's d3d11.dll and dxgi.dll directly so a ReShade sitting in the folder cannot get in between.
class WindowDevice {
public:
    WindowDevice() = default;
    WindowDevice(const WindowDevice&) = delete;
    WindowDevice& operator=(const WindowDevice&) = delete;
    ~WindowDevice() { Destroy(); }

    bool Create(HWND hwnd);
    void Destroy();
    void Resize(UINT width, UINT height);   // call when the window's client size changed (not while minimised)

    ID3D11Device* Device() const { return m_device; }
    ID3D11DeviceContext* Context() const { return m_context; }
    IDXGISwapChain* SwapChain() const { return m_swapChain; }
    ID3D11RenderTargetView* BackBuffer() const { return m_backBuffer; }

private:
    void CreateBackBufferView();
    void ReleaseBackBufferView();

    ID3D11Device* m_device = nullptr;
    ID3D11DeviceContext* m_context = nullptr;
    IDXGISwapChain* m_swapChain = nullptr;
    ID3D11RenderTargetView* m_backBuffer = nullptr;
};

} // namespace window
} // namespace lsproxy
