#include "window_device.h"
#include <string>

namespace eam {
namespace window {

namespace {

template <class T> void SafeRelease(T*& p) { if (p) { p->Release(); p = nullptr; } }

using CreateDXGIFactory1Fn = HRESULT(WINAPI*)(REFIID, void**);
using D3D11CreateDeviceFn = HRESULT(WINAPI*)(IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL*, UINT, UINT,
                                             ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);

}

bool WindowDevice::Create(HWND hwnd) {
    // System copies, by full path: a dxgi.dll or d3d11.dll next to the program (ReShade) would otherwise be picked up here.
    wchar_t systemDir[MAX_PATH] = {};
    if (!GetSystemDirectoryW(systemDir, MAX_PATH)) return false;
    const std::wstring dir = systemDir;
    HMODULE dxgi = LoadLibraryW((dir + L"\\dxgi.dll").c_str());
    HMODULE d3d11 = LoadLibraryW((dir + L"\\d3d11.dll").c_str());
    if (!dxgi || !d3d11) return false;
    const auto createFactory = reinterpret_cast<CreateDXGIFactory1Fn>(GetProcAddress(dxgi, "CreateDXGIFactory1"));
    const auto createDevice = reinterpret_cast<D3D11CreateDeviceFn>(GetProcAddress(d3d11, "D3D11CreateDevice"));
    if (!createFactory || !createDevice) return false;

    IDXGIFactory1* factory = nullptr;
    if (FAILED(createFactory(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory)))) return false;

    const D3D_FEATURE_LEVEL levels[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    D3D_FEATURE_LEVEL got;
    bool ok = SUCCEEDED(createDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2, D3D11_SDK_VERSION, &m_device, &got, &m_context));

    if (ok) {
        DXGI_SWAP_CHAIN_DESC sd = {};
        sd.BufferCount = 2;
        sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferDesc.RefreshRate.Numerator = 60;
        sd.BufferDesc.RefreshRate.Denominator = 1;
        sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow = hwnd;
        sd.SampleDesc.Count = 1;
        sd.Windowed = TRUE;
        sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        ok = SUCCEEDED(factory->CreateSwapChain(m_device, &sd, &m_swapChain));
    }
    factory->Release();

    if (!ok) { Destroy(); return false; }
    CreateBackBufferView();
    return m_backBuffer != nullptr;
}

void WindowDevice::CreateBackBufferView() {
    ID3D11Texture2D* buffer = nullptr;
    if (FAILED(m_swapChain->GetBuffer(0, IID_PPV_ARGS(&buffer)))) return;
    m_device->CreateRenderTargetView(buffer, nullptr, &m_backBuffer);
    buffer->Release();
}

void WindowDevice::ReleaseBackBufferView() { SafeRelease(m_backBuffer); }

void WindowDevice::Resize(UINT width, UINT height) {
    if (!m_device || !m_swapChain) return;
    ReleaseBackBufferView();
    m_swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    CreateBackBufferView();
}

void WindowDevice::Destroy() {
    ReleaseBackBufferView();
    SafeRelease(m_swapChain);
    SafeRelease(m_context);
    SafeRelease(m_device);
}

} // namespace window
} // namespace eam
