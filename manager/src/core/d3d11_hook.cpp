#include "d3d11_hook.h"
#include "iat_patcher.h"
#include "../host/host_impl.h"
#include "../event/event_system.h"
#include "../log/logger.h"
#include <d3d11.h>
#include <cstdint>

namespace D3D11Hook {

static lsproxy::HostImpl* g_host = nullptr;
static bool g_installed = false;

// Original D3D11CreateDevice function pointer
typedef HRESULT(WINAPI* PFN_D3D11CreateDevice)(
    IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT,
    const D3D_FEATURE_LEVEL*, UINT, UINT,
    ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);

static PFN_D3D11CreateDevice g_origCreateDevice = nullptr;

// Original vtable function pointers
typedef void(STDMETHODCALLTYPE* PFN_Dispatch)(
    ID3D11DeviceContext*, UINT, UINT, UINT);
typedef void(STDMETHODCALLTYPE* PFN_CSSetShader)(
    ID3D11DeviceContext*, ID3D11ComputeShader*, ID3D11ClassInstance* const*, UINT);
typedef void(STDMETHODCALLTYPE* PFN_CSSetShaderResources)(
    ID3D11DeviceContext*, UINT, UINT, ID3D11ShaderResourceView* const*);
typedef void(STDMETHODCALLTYPE* PFN_CSSetUnorderedAccessViews)(
    ID3D11DeviceContext*, UINT, UINT, ID3D11UnorderedAccessView* const*, const UINT*);

static PFN_Dispatch g_origDispatch = nullptr;
static PFN_CSSetShader g_origCSSetShader = nullptr;
static PFN_CSSetShaderResources g_origCSSetSRVs = nullptr;
static PFN_CSSetUnorderedAccessViews g_origCSSetUAVs = nullptr;

// Track currently bound compute shader for dispatch identification
static ID3D11ComputeShader* g_currentCS = nullptr;
static uint32_t g_dispatchCount = 0;
static void** g_hookedVtable = nullptr;

static void STDMETHODCALLTYPE HookedCSSetShader(
    ID3D11DeviceContext* ctx, ID3D11ComputeShader* shader,
    ID3D11ClassInstance* const* classInstances, UINT numClassInstances)
{
    g_currentCS = shader;
    if (g_origCSSetShader) {
        g_origCSSetShader(ctx, shader, classInstances, numClassInstances);
    }
}

static void STDMETHODCALLTYPE HookedCSSetSRVs(
    ID3D11DeviceContext* ctx, UINT startSlot, UINT numViews,
    ID3D11ShaderResourceView* const* views)
{
    if (g_origCSSetSRVs) {
        g_origCSSetSRVs(ctx, startSlot, numViews, views);
    }
}

static void STDMETHODCALLTYPE HookedCSSetUAVs(
    ID3D11DeviceContext* ctx, UINT startSlot, UINT numUAVs,
    ID3D11UnorderedAccessView* const* views, const UINT* initialCounts)
{
    if (g_origCSSetUAVs) {
        g_origCSSetUAVs(ctx, startSlot, numUAVs, views, initialCounts);
    }
}

// Hooked Dispatch — called every time Lossless.dll dispatches a compute shader
static void STDMETHODCALLTYPE HookedDispatch(
    ID3D11DeviceContext* ctx, UINT x, UINT y, UINT z)
{
    g_dispatchCount++;

    if (g_host) {
        bool skip = g_host->InvokePreDispatch(x, y, z);
        if (skip) return;
    }

    if (g_origDispatch) {
        g_origDispatch(ctx, x, y, z);
    }

    if (g_host) {
        g_host->InvokePostDispatch(x, y, z);
    }
}

static bool HookVtableSlot(void** vtable, UINT slot, void* hook, void** origOut) {
    *origOut = vtable[slot];
    DWORD oldProtect;
    if (VirtualProtect(&vtable[slot], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect)) {
        vtable[slot] = hook;
        VirtualProtect(&vtable[slot], sizeof(void*), oldProtect, &oldProtect);
        return true;
    }
    return false;
}

static void InstallDispatchHook(ID3D11DeviceContext* ctx) {
    void** vtable = *(void***)ctx;

    // Reset shader tracking — previous device's shader pointers are now stale
    g_currentCS = nullptr;

    // Don't re-hook same vtable: g_origDispatch would be overwritten with
    // HookedDispatch itself, causing infinite recursion on next Dispatch call.
    if (vtable == g_hookedVtable) {
        LOG_INFO("D3D11Hook", "Vtable %p already hooked, skipping re-hook", vtable);
        return;
    }

    if (HookVtableSlot(vtable, 41, (void*)&HookedDispatch, (void**)&g_origDispatch))
        LOG_INFO("D3D11Hook", "Dispatch hook installed (slot 41)");

    if (HookVtableSlot(vtable, 39, (void*)&HookedCSSetShader, (void**)&g_origCSSetShader))
        LOG_INFO("D3D11Hook", "CSSetShader hook installed (slot 39)");

    if (HookVtableSlot(vtable, 37, (void*)&HookedCSSetSRVs, (void**)&g_origCSSetSRVs))
        LOG_INFO("D3D11Hook", "CSSetShaderResources hook installed (slot 37)");

    if (HookVtableSlot(vtable, 38, (void*)&HookedCSSetUAVs, (void**)&g_origCSSetUAVs))
        LOG_INFO("D3D11Hook", "CSSetUnorderedAccessViews hook installed (slot 38)");

    g_hookedVtable = vtable;
}

// Hooked D3D11CreateDevice — captures device and context
static HRESULT WINAPI HookedD3D11CreateDevice(
    IDXGIAdapter* pAdapter,
    D3D_DRIVER_TYPE DriverType,
    HMODULE Software,
    UINT Flags,
    const D3D_FEATURE_LEVEL* pFeatureLevels,
    UINT FeatureLevels,
    UINT SDKVersion,
    ID3D11Device** ppDevice,
    D3D_FEATURE_LEVEL* pFeatureLevel,
    ID3D11DeviceContext** ppImmediateContext)
{
    HRESULT hr = g_origCreateDevice(
        pAdapter, DriverType, Software, Flags,
        pFeatureLevels, FeatureLevels, SDKVersion,
        ppDevice, pFeatureLevel, ppImmediateContext);

    if (SUCCEEDED(hr) && g_host) {
        ID3D11Device* device = ppDevice ? *ppDevice : nullptr;
        ID3D11DeviceContext* context = ppImmediateContext ? *ppImmediateContext : nullptr;

        if (device && context) {
            bool deviceChanged = (g_host->GetD3D11Device() != nullptr &&
                                  g_host->GetD3D11Device() != device);

            g_host->SetD3D11Device(device, context);
            g_dispatchCount = 0;

            D3D_FEATURE_LEVEL fl = pFeatureLevel ? *pFeatureLevel : D3D_FEATURE_LEVEL_11_0;
            LOG_INFO("D3D11Hook", "Captured D3D11 device (%p) and context (%p), FL %x",
                     device, context, fl);

            InstallDispatchHook(context);

            if (deviceChanged) {
                lsproxy::EventBus::Instance().Publish(LSPROXY_EVENT_D3D11_DEVICE_CHANGED);
            }
            lsproxy::EventBus::Instance().Publish(LSPROXY_EVENT_D3D11_DEVICE_READY);
        }
    }
    return hr;
}

void Initialize(lsproxy::HostImpl* host) {
    g_host = host;
    if (g_installed) return;

    HMODULE hLosslessOriginal = GetModuleHandleW(L"Lossless_original.dll");
    if (!hLosslessOriginal) {
        LOG_ERROR("D3D11Hook", "Lossless_original.dll not loaded");
        return;
    }

    // Get original D3D11CreateDevice from d3d11.dll
    HMODULE hD3D11 = GetModuleHandleW(L"d3d11.dll");
    if (!hD3D11) hD3D11 = LoadLibraryW(L"d3d11.dll");
    if (!hD3D11) {
        LOG_ERROR("D3D11Hook", "Failed to load d3d11.dll");
        return;
    }
    g_origCreateDevice = (PFN_D3D11CreateDevice)GetProcAddress(hD3D11, "D3D11CreateDevice");

    // D3D11CreateDevice is a delay import in Lossless.dll.
    // We always need the real function pointer from d3d11.dll first,
    // then patch the delay IAT thunk to point to our hook.
    IatPatcher::PatchDelayIat(
        hLosslessOriginal, "d3d11.dll", "D3D11CreateDevice", &HookedD3D11CreateDevice);
    LOG_INFO("D3D11Hook", "D3D11CreateDevice delay IAT hook installed");

    g_installed = true;
}

void Shutdown() {
    g_host = nullptr;
    g_installed = false;
    g_hookedVtable = nullptr;
    g_currentCS = nullptr;
    g_dispatchCount = 0;
}

void* GetCurrentComputeShader() {
    return g_currentCS;
}

uint32_t GetDispatchCount() {
    return g_dispatchCount;
}

} // namespace D3D11Hook
