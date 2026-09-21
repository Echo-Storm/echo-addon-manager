#include "windowed_dxgi.h"
#include "windowed_state.h"
#include <map>
#include <string>

namespace lsproxy {
namespace features {
namespace windowed {

bool g_FactoryAlive = false;


struct LUIDComparator {
    bool operator()(const LUID& a, const LUID& b) const {
        if (a.HighPart != b.HighPart) return a.HighPart < b.HighPart;
        return a.LowPart < b.LowPart;
    }
};

static std::map<LUID, ProxyDXGIAdapter*, LUIDComparator> g_AdapterCache;
static std::mutex g_AdapterMu;     // DXGI may be enumerated from several threads
static std::mutex g_FakeMu;
static ProxyDXGIOutput* g_FakeOutput = nullptr;

// --- ProxyDXGIFactory ---

ProxyDXGIFactory::ProxyDXGIFactory(IDXGIFactory6* pFactory) : m_pFactory(pFactory), m_refCount(1) {}

ProxyDXGIFactory::~ProxyDXGIFactory() {
    g_FactoryAlive = false;
    {
        std::lock_guard<std::mutex> lk(g_AdapterMu);
        for (auto& pair : g_AdapterCache) {
            if (pair.second) pair.second->Release();
        }
        g_AdapterCache.clear();
    }
    { std::lock_guard<std::mutex> lk(g_FakeMu); if (g_FakeOutput) { g_FakeOutput->Release(); g_FakeOutput = nullptr; } }
    if (m_pFactory) m_pFactory->Release();
}

HRESULT ProxyDXGIFactory::QueryInterface(REFIID riid, void** ppvObject) {
    if (!ppvObject) return E_POINTER;
    if (riid == __uuidof(IUnknown) || riid == __uuidof(IDXGIObject) || riid == __uuidof(IDXGIFactory) ||
        riid == __uuidof(IDXGIFactory1) || riid == __uuidof(IDXGIFactory2) || riid == __uuidof(IDXGIFactory3) ||
        riid == __uuidof(IDXGIFactory4) || riid == __uuidof(IDXGIFactory5) || riid == __uuidof(IDXGIFactory6)) {
        *ppvObject = this; AddRef(); return S_OK;
    }
    return m_pFactory->QueryInterface(riid, ppvObject);
}

ULONG ProxyDXGIFactory::AddRef() { return InterlockedIncrement(&m_refCount); }
ULONG ProxyDXGIFactory::Release() { ULONG r = InterlockedDecrement(&m_refCount); if (r == 0) delete this; return r; }

HRESULT ProxyDXGIFactory::SetPrivateData(REFGUID Name, UINT DataSize, const void* pData) { return m_pFactory->SetPrivateData(Name, DataSize, pData); }
HRESULT ProxyDXGIFactory::SetPrivateDataInterface(REFGUID Name, const IUnknown* pUnknown) { return m_pFactory->SetPrivateDataInterface(Name, pUnknown); }
HRESULT ProxyDXGIFactory::GetPrivateData(REFGUID Name, UINT* pDataSize, void* pData) { return m_pFactory->GetPrivateData(Name, pDataSize, pData); }
HRESULT ProxyDXGIFactory::GetParent(REFIID riid, void** ppParent) { return m_pFactory->GetParent(riid, ppParent); }

HRESULT ProxyDXGIFactory::EnumAdapters(UINT Adapter, IDXGIAdapter** ppAdapter) {
    IDXGIAdapter* pRealAdapter = nullptr;
    HRESULT hr = m_pFactory->EnumAdapters(Adapter, &pRealAdapter);
    if (SUCCEEDED(hr)) {
        IDXGIAdapter4* pAdapter4 = nullptr;
        if (SUCCEEDED(pRealAdapter->QueryInterface(__uuidof(IDXGIAdapter4), (void**)&pAdapter4))) {
            DXGI_ADAPTER_DESC2 desc;
            pAdapter4->GetDesc2(&desc);
            LUID luid = desc.AdapterLuid;
            std::lock_guard<std::mutex> lk(g_AdapterMu);
            auto it = g_AdapterCache.find(luid);
            if (it != g_AdapterCache.end()) {
                *ppAdapter = it->second; it->second->AddRef();
                pAdapter4->Release(); pRealAdapter->Release();
                return S_OK;
            }
            auto* proxy = new ProxyDXGIAdapter(pAdapter4, Adapter);
            proxy->AddRef(); // cache ref
            g_AdapterCache[luid] = proxy;
            *ppAdapter = proxy; proxy->AddRef(); // caller ref
            pRealAdapter->Release();
            return S_OK;
        }
        *ppAdapter = pRealAdapter;
    }
    return hr;
}

HRESULT ProxyDXGIFactory::MakeWindowAssociation(HWND h, UINT f) { return m_pFactory->MakeWindowAssociation(h, f); }
HRESULT ProxyDXGIFactory::GetWindowAssociation(HWND* h) { return m_pFactory->GetWindowAssociation(h); }
HRESULT ProxyDXGIFactory::CreateSwapChain(IUnknown* d, DXGI_SWAP_CHAIN_DESC* p, IDXGISwapChain** s) { return m_pFactory->CreateSwapChain(d, p, s); }
HRESULT ProxyDXGIFactory::CreateSoftwareAdapter(HMODULE m, IDXGIAdapter** a) { return m_pFactory->CreateSoftwareAdapter(m, a); }

HRESULT ProxyDXGIFactory::EnumAdapters1(UINT Adapter, IDXGIAdapter1** ppAdapter) {
    IDXGIAdapter* base = nullptr;
    HRESULT hr = this->EnumAdapters(Adapter, &base);
    if (SUCCEEDED(hr)) { hr = base->QueryInterface(__uuidof(IDXGIAdapter1), (void**)ppAdapter); base->Release(); }
    return hr;
}

BOOL ProxyDXGIFactory::IsCurrent() { return m_pFactory->IsCurrent(); }
BOOL ProxyDXGIFactory::IsWindowedStereoEnabled() { return m_pFactory->IsWindowedStereoEnabled(); }
HRESULT ProxyDXGIFactory::CreateSwapChainForHwnd(IUnknown* d, HWND h, const DXGI_SWAP_CHAIN_DESC1* p, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* f, IDXGIOutput* o, IDXGISwapChain1** s) { return m_pFactory->CreateSwapChainForHwnd(d, h, p, f, o, s); }
HRESULT ProxyDXGIFactory::CreateSwapChainForCoreWindow(IUnknown* d, IUnknown* w, const DXGI_SWAP_CHAIN_DESC1* p, IDXGIOutput* o, IDXGISwapChain1** s) { return m_pFactory->CreateSwapChainForCoreWindow(d, w, p, o, s); }
HRESULT ProxyDXGIFactory::GetSharedResourceAdapterLuid(HANDLE h, LUID* l) { return m_pFactory->GetSharedResourceAdapterLuid(h, l); }
HRESULT ProxyDXGIFactory::RegisterStereoStatusWindow(HWND h, UINT m, DWORD* c) { return m_pFactory->RegisterStereoStatusWindow(h, m, c); }
HRESULT ProxyDXGIFactory::RegisterStereoStatusEvent(HANDLE h, DWORD* c) { return m_pFactory->RegisterStereoStatusEvent(h, c); }
void ProxyDXGIFactory::UnregisterStereoStatus(DWORD c) { m_pFactory->UnregisterStereoStatus(c); }
HRESULT ProxyDXGIFactory::RegisterOcclusionStatusWindow(HWND h, UINT m, DWORD* c) { return m_pFactory->RegisterOcclusionStatusWindow(h, m, c); }
HRESULT ProxyDXGIFactory::RegisterOcclusionStatusEvent(HANDLE h, DWORD* c) { return m_pFactory->RegisterOcclusionStatusEvent(h, c); }
void ProxyDXGIFactory::UnregisterOcclusionStatus(DWORD c) { m_pFactory->UnregisterOcclusionStatus(c); }
HRESULT ProxyDXGIFactory::CreateSwapChainForComposition(IUnknown* d, const DXGI_SWAP_CHAIN_DESC1* p, IDXGIOutput* o, IDXGISwapChain1** s) { return m_pFactory->CreateSwapChainForComposition(d, p, o, s); }
UINT ProxyDXGIFactory::GetCreationFlags() { return m_pFactory->GetCreationFlags(); }
HRESULT ProxyDXGIFactory::EnumAdapterByLuid(LUID l, REFIID r, void** a) { return m_pFactory->EnumAdapterByLuid(l, r, a); }
HRESULT ProxyDXGIFactory::EnumWarpAdapter(REFIID r, void** a) { return m_pFactory->EnumWarpAdapter(r, a); }
HRESULT ProxyDXGIFactory::CheckFeatureSupport(DXGI_FEATURE f, void* d, UINT s) { return m_pFactory->CheckFeatureSupport(f, d, s); }
HRESULT ProxyDXGIFactory::EnumAdapterByGpuPreference(UINT a, DXGI_GPU_PREFERENCE g, REFIID r, void** v) { return m_pFactory->EnumAdapterByGpuPreference(a, g, r, v); }

// --- ProxyDXGIAdapter ---

ProxyDXGIAdapter::ProxyDXGIAdapter(IDXGIAdapter4* pAdapter, UINT index) : m_pAdapter(pAdapter), m_refCount(1), m_adapterIndex(index) {}
ProxyDXGIAdapter::~ProxyDXGIAdapter() { if (m_pAdapter) m_pAdapter->Release(); }

HRESULT ProxyDXGIAdapter::QueryInterface(REFIID riid, void** ppvObject) {
    if (!ppvObject) return E_POINTER;
    if (riid == __uuidof(IUnknown) || riid == __uuidof(IDXGIObject) || riid == __uuidof(IDXGIAdapter) ||
        riid == __uuidof(IDXGIAdapter1) || riid == __uuidof(IDXGIAdapter2) || riid == __uuidof(IDXGIAdapter3) ||
        riid == __uuidof(IDXGIAdapter4)) {
        *ppvObject = this; AddRef(); return S_OK;
    }
    return m_pAdapter->QueryInterface(riid, ppvObject);
}

ULONG ProxyDXGIAdapter::AddRef() { return InterlockedIncrement(&m_refCount); }
ULONG ProxyDXGIAdapter::Release() { ULONG r = InterlockedDecrement(&m_refCount); if (r == 0) delete this; return r; }

HRESULT ProxyDXGIAdapter::SetPrivateData(REFGUID N, UINT S, const void* D) { return m_pAdapter->SetPrivateData(N, S, D); }
HRESULT ProxyDXGIAdapter::SetPrivateDataInterface(REFGUID N, const IUnknown* U) { return m_pAdapter->SetPrivateDataInterface(N, U); }
HRESULT ProxyDXGIAdapter::GetPrivateData(REFGUID N, UINT* S, void* D) { return m_pAdapter->GetPrivateData(N, S, D); }
HRESULT ProxyDXGIAdapter::GetParent(REFIID r, void** p) { return m_pAdapter->GetParent(r, p); }

HRESULT ProxyDXGIAdapter::EnumOutputs(UINT Output, IDXGIOutput** ppOutput) {
    IDXGIOutput* pRealOutput = nullptr;
    HRESULT hr = m_pAdapter->EnumOutputs(Output, &pRealOutput);

    if (hr == DXGI_ERROR_NOT_FOUND && m_adapterIndex == 0 && GetState().settings.active) {
        bool isNextSlot = false;
        if (Output == 0) {
            isNextSlot = true;
        } else {
            IDXGIOutput* pPrev = nullptr;
            if (SUCCEEDED(m_pAdapter->EnumOutputs(Output - 1, &pPrev))) {
                pPrev->Release();
                isNextSlot = true;
            }
        }
        if (isNextSlot) {
            std::lock_guard<std::mutex> lk(g_FakeMu);
            if (!g_FakeOutput) {
                g_FakeOutput = new ProxyDXGIOutput(nullptr, true);
                g_FakeOutput->AddRef();
            }
            *ppOutput = g_FakeOutput;
            g_FakeOutput->AddRef();
            return S_OK;
        }
        return hr;
    }

    if (SUCCEEDED(hr)) {
        IDXGIOutput6* pOutput6 = nullptr;
        if (SUCCEEDED(pRealOutput->QueryInterface(__uuidof(IDXGIOutput6), (void**)&pOutput6))) {
            *ppOutput = new ProxyDXGIOutput(pOutput6, false);
            pRealOutput->Release();
        } else {
            *ppOutput = pRealOutput;
        }
    }
    return hr;
}

HRESULT ProxyDXGIAdapter::GetDesc(DXGI_ADAPTER_DESC* p) { return m_pAdapter->GetDesc(p); }
HRESULT ProxyDXGIAdapter::CheckInterfaceSupport(REFGUID i, LARGE_INTEGER* v) { return m_pAdapter->CheckInterfaceSupport(i, v); }
HRESULT ProxyDXGIAdapter::GetDesc1(DXGI_ADAPTER_DESC1* p) { return m_pAdapter->GetDesc1(p); }
HRESULT ProxyDXGIAdapter::GetDesc2(DXGI_ADAPTER_DESC2* p) { return m_pAdapter->GetDesc2(p); }
HRESULT ProxyDXGIAdapter::RegisterHardwareContentProtectionTeardownStatusEvent(HANDLE h, DWORD* c) { return m_pAdapter->RegisterHardwareContentProtectionTeardownStatusEvent(h, c); }
void ProxyDXGIAdapter::UnregisterHardwareContentProtectionTeardownStatus(DWORD c) { m_pAdapter->UnregisterHardwareContentProtectionTeardownStatus(c); }
HRESULT ProxyDXGIAdapter::QueryVideoMemoryInfo(UINT n, DXGI_MEMORY_SEGMENT_GROUP g, DXGI_QUERY_VIDEO_MEMORY_INFO* i) { return m_pAdapter->QueryVideoMemoryInfo(n, g, i); }
HRESULT ProxyDXGIAdapter::SetVideoMemoryReservation(UINT n, DXGI_MEMORY_SEGMENT_GROUP g, UINT64 r) { return m_pAdapter->SetVideoMemoryReservation(n, g, r); }
HRESULT ProxyDXGIAdapter::RegisterVideoMemoryBudgetChangeNotificationEvent(HANDLE h, DWORD* c) { return m_pAdapter->RegisterVideoMemoryBudgetChangeNotificationEvent(h, c); }
void ProxyDXGIAdapter::UnregisterVideoMemoryBudgetChangeNotification(DWORD c) { m_pAdapter->UnregisterVideoMemoryBudgetChangeNotification(c); }
HRESULT ProxyDXGIAdapter::GetDesc3(DXGI_ADAPTER_DESC3* p) { return m_pAdapter->GetDesc3(p); }

// --- ProxyDXGIOutput ---

ProxyDXGIOutput::ProxyDXGIOutput(IDXGIOutput6* pOutput, bool isFake) : m_pOutput(pOutput), m_refCount(1), m_isFake(isFake) {}
ProxyDXGIOutput::~ProxyDXGIOutput() { if (m_pOutput) m_pOutput->Release(); }

HRESULT ProxyDXGIOutput::QueryInterface(REFIID riid, void** ppvObject) {
    if (!ppvObject) return E_POINTER;
    if (riid == __uuidof(IUnknown) || riid == __uuidof(IDXGIObject) || riid == __uuidof(IDXGIOutput) ||
        riid == __uuidof(IDXGIOutput1) || riid == __uuidof(IDXGIOutput2) || riid == __uuidof(IDXGIOutput3) ||
        riid == __uuidof(IDXGIOutput4) || riid == __uuidof(IDXGIOutput5) || riid == __uuidof(IDXGIOutput6)) {
        *ppvObject = this; AddRef(); return S_OK;
    }
    if (m_isFake) return E_NOINTERFACE;
    if (m_pOutput) return m_pOutput->QueryInterface(riid, ppvObject);
    return E_NOINTERFACE;
}

ULONG ProxyDXGIOutput::AddRef() { return InterlockedIncrement(&m_refCount); }
ULONG ProxyDXGIOutput::Release() { ULONG r = InterlockedDecrement(&m_refCount); if (r == 0) delete this; return r; }

HRESULT ProxyDXGIOutput::SetPrivateData(REFGUID N, UINT S, const void* D) { if (m_isFake) return S_OK; return m_pOutput->SetPrivateData(N, S, D); }
HRESULT ProxyDXGIOutput::SetPrivateDataInterface(REFGUID N, const IUnknown* U) { if (m_isFake) return S_OK; return m_pOutput->SetPrivateDataInterface(N, U); }
HRESULT ProxyDXGIOutput::GetPrivateData(REFGUID N, UINT* S, void* D) { if (m_isFake) return DXGI_ERROR_NOT_FOUND; return m_pOutput->GetPrivateData(N, S, D); }
HRESULT ProxyDXGIOutput::GetParent(REFIID r, void** p) { if (m_isFake) return E_NOTIMPL; return m_pOutput->GetParent(r, p); }

HRESULT ProxyDXGIOutput::GetDesc(DXGI_OUTPUT_DESC* pDesc) {
    if (m_isFake) {
        if (!pDesc) return E_INVALIDARG;
        wcscpy_s(pDesc->DeviceName, 32, L"\\\\.\\DISPLAY_VIRTUAL");
        UpdateTargetRect();
        auto& st = GetState();
        std::lock_guard<std::mutex> lock(st.mutex);
        pDesc->DesktopCoordinates = st.settings.positionMode ? st.lsRect : st.targetRect;
        pDesc->AttachedToDesktop = TRUE;
        pDesc->Rotation = DXGI_MODE_ROTATION_IDENTITY;
        pDesc->Monitor = (HMONITOR)0xBADF00D;
        return S_OK;
    }
    return m_pOutput->GetDesc(pDesc);
}

HRESULT ProxyDXGIOutput::GetDisplayModeList(DXGI_FORMAT EnumFormat, UINT Flags, UINT* pNumModes, DXGI_MODE_DESC* pDesc) {
    if (m_isFake) {
        if (!pNumModes) return E_INVALIDARG;
        if (!pDesc) { *pNumModes = 1; return S_OK; }
        if (*pNumModes < 1) return DXGI_ERROR_MORE_DATA;
        UpdateTargetRect();
        auto& st = GetState();
        std::lock_guard<std::mutex> lock(st.mutex);
        pDesc[0].Width = st.targetRect.right - st.targetRect.left;
        pDesc[0].Height = st.targetRect.bottom - st.targetRect.top;
        pDesc[0].RefreshRate = {60, 1};
        pDesc[0].Format = EnumFormat;
        pDesc[0].ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_PROGRESSIVE;
        pDesc[0].Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
        *pNumModes = 1;
        return S_OK;
    }
    return m_pOutput->GetDisplayModeList(EnumFormat, Flags, pNumModes, pDesc);
}

HRESULT ProxyDXGIOutput::FindClosestMatchingMode(const DXGI_MODE_DESC* pModeToMatch, DXGI_MODE_DESC* pClosestMatch, IUnknown* pConcernedDevice) {
    if (m_isFake) { if (!pClosestMatch) return E_INVALIDARG; if (pModeToMatch) *pClosestMatch = *pModeToMatch; return S_OK; }
    return m_pOutput->FindClosestMatchingMode(pModeToMatch, pClosestMatch, pConcernedDevice);
}

HRESULT ProxyDXGIOutput::WaitForVBlank() { if (m_isFake) { Sleep(16); return S_OK; } return m_pOutput->WaitForVBlank(); }
HRESULT ProxyDXGIOutput::TakeOwnership(IUnknown* d, BOOL e) { if (m_isFake) return S_OK; return m_pOutput->TakeOwnership(d, e); }
void ProxyDXGIOutput::ReleaseOwnership() { if (m_isFake) return; m_pOutput->ReleaseOwnership(); }
HRESULT ProxyDXGIOutput::GetGammaControlCapabilities(DXGI_GAMMA_CONTROL_CAPABILITIES* p) { if (m_isFake) return E_NOTIMPL; return m_pOutput->GetGammaControlCapabilities(p); }
HRESULT ProxyDXGIOutput::SetGammaControl(const DXGI_GAMMA_CONTROL* p) { if (m_isFake) return S_OK; return m_pOutput->SetGammaControl(p); }
HRESULT ProxyDXGIOutput::GetGammaControl(DXGI_GAMMA_CONTROL* p) { if (m_isFake) return E_NOTIMPL; return m_pOutput->GetGammaControl(p); }
HRESULT ProxyDXGIOutput::SetDisplaySurface(IDXGISurface* s) { if (m_isFake) return S_OK; return m_pOutput->SetDisplaySurface(s); }
HRESULT ProxyDXGIOutput::GetDisplaySurfaceData(IDXGISurface* d) { if (m_isFake) return S_OK; return m_pOutput->GetDisplaySurfaceData(d); }
HRESULT ProxyDXGIOutput::GetFrameStatistics(DXGI_FRAME_STATISTICS* s) { if (m_isFake) return E_NOTIMPL; return m_pOutput->GetFrameStatistics(s); }

HRESULT ProxyDXGIOutput::GetDisplayModeList1(DXGI_FORMAT EnumFormat, UINT Flags, UINT* pNumModes, DXGI_MODE_DESC1* pDesc) {
    if (m_isFake) {
        if (!pNumModes) return E_INVALIDARG;
        if (!pDesc) { *pNumModes = 1; return S_OK; }
        if (*pNumModes < 1) return DXGI_ERROR_MORE_DATA;
        UpdateTargetRect();
        auto& st = GetState();
        std::lock_guard<std::mutex> lock(st.mutex);
        pDesc[0].Width = st.targetRect.right - st.targetRect.left;
        pDesc[0].Height = st.targetRect.bottom - st.targetRect.top;
        pDesc[0].RefreshRate = {60, 1};
        pDesc[0].Format = EnumFormat;
        pDesc[0].ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_PROGRESSIVE;
        pDesc[0].Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
        pDesc[0].Stereo = FALSE;
        *pNumModes = 1;
        return S_OK;
    }
    return m_pOutput->GetDisplayModeList1(EnumFormat, Flags, pNumModes, pDesc);
}

HRESULT ProxyDXGIOutput::FindClosestMatchingMode1(const DXGI_MODE_DESC1* m, DXGI_MODE_DESC1* c, IUnknown* d) {
    if (m_isFake) { if (!c) return E_INVALIDARG; if (m) *c = *m; return S_OK; }
    return m_pOutput->FindClosestMatchingMode1(m, c, d);
}

HRESULT ProxyDXGIOutput::GetDisplaySurfaceData1(IDXGIResource* d) { if (m_isFake) return S_OK; return m_pOutput->GetDisplaySurfaceData1(d); }
HRESULT ProxyDXGIOutput::DuplicateOutput(IUnknown* d, IDXGIOutputDuplication** o) { if (m_isFake) return DXGI_ERROR_UNSUPPORTED; return m_pOutput->DuplicateOutput(d, o); }
BOOL ProxyDXGIOutput::SupportsOverlays() { if (m_isFake) return FALSE; return m_pOutput->SupportsOverlays(); }
HRESULT ProxyDXGIOutput::CheckOverlaySupport(DXGI_FORMAT f, IUnknown* d, UINT* p) { if (m_isFake) return E_NOTIMPL; return m_pOutput->CheckOverlaySupport(f, d, p); }
HRESULT ProxyDXGIOutput::CheckOverlayColorSpaceSupport(DXGI_FORMAT f, DXGI_COLOR_SPACE_TYPE c, IUnknown* d, UINT* p) { if (m_isFake) return E_NOTIMPL; return m_pOutput->CheckOverlayColorSpaceSupport(f, c, d, p); }
HRESULT ProxyDXGIOutput::DuplicateOutput1(IUnknown* d, UINT f, UINT c, const DXGI_FORMAT* fmt, IDXGIOutputDuplication** o) { if (m_isFake) return DXGI_ERROR_UNSUPPORTED; return m_pOutput->DuplicateOutput1(d, f, c, fmt, o); }

HRESULT ProxyDXGIOutput::GetDesc1(DXGI_OUTPUT_DESC1* pDesc) {
    if (m_isFake) {
        if (!pDesc) return E_INVALIDARG;
        wcscpy_s(pDesc->DeviceName, 32, L"\\\\.\\DISPLAY_VIRTUAL");
        UpdateTargetRect();
        auto& st = GetState();
        std::lock_guard<std::mutex> lock(st.mutex);
        pDesc->DesktopCoordinates = st.settings.positionMode ? st.lsRect : st.targetRect;
        pDesc->AttachedToDesktop = TRUE;
        pDesc->Rotation = DXGI_MODE_ROTATION_IDENTITY;
        pDesc->Monitor = (HMONITOR)0xBADF00D;
        pDesc->BitsPerColor = 8;
        pDesc->ColorSpace = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
        pDesc->RedPrimary[0] = 0.64f; pDesc->RedPrimary[1] = 0.33f;
        pDesc->GreenPrimary[0] = 0.30f; pDesc->GreenPrimary[1] = 0.60f;
        pDesc->BluePrimary[0] = 0.15f; pDesc->BluePrimary[1] = 0.06f;
        pDesc->WhitePoint[0] = 0.3127f; pDesc->WhitePoint[1] = 0.3290f;
        pDesc->MinLuminance = 0.0f;
        pDesc->MaxLuminance = 100.0f;
        pDesc->MaxFullFrameLuminance = 100.0f;
        return S_OK;
    }
    return m_pOutput->GetDesc1(pDesc);
}

HRESULT ProxyDXGIOutput::CheckHardwareCompositionSupport(UINT* p) { if (m_isFake) return E_NOTIMPL; return m_pOutput->CheckHardwareCompositionSupport(p); }

} // namespace windowed
} // namespace features
} // namespace lsproxy
