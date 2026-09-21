#include "dxgi_proxy.hpp"
#include "windowed_state.hpp"
#include <lsproxy/addon_sdk.h>
#include "imgui.h"
#include <lsproxy/lsp_widgets.h>
#include <MinHook.h>
#include <d3d11.h>
#include <dxgi.h>
#include <dxgi1_2.h>
#include <filesystem>
#include <cstdio>
#include <string>

// ============================================================================
// Host interface (provided by Echo Addon Manager)
// ============================================================================

static IHost* g_host = nullptr;
static const char* ADDON_ID = "LSP-Windowed-Mode";

// Helper logging through host
static void HostLog(LsProxyLogLevel level, const char* fmt, ...) {
    if (!g_host) return;
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    g_host->Log(level, buf);
}

// ============================================================================
// Fake Monitor
// ============================================================================

static const HMONITOR FAKE_VIRTUAL_MONITOR = (HMONITOR)0xBADF00D;
static void PinModule();

// ============================================================================
// Target window tracking
// ============================================================================

static RECT CalculatePositionedRect(HWND hTarget, RECT rcTargetClient) {
    RECT result = rcTargetClient;
    if (!hTarget || !IsWindow(hTarget)) return result;

    RECT targetWindowRect;
    if (!GetWindowRect(hTarget, &targetWindowRect)) return result;

    int lsW = rcTargetClient.right - rcTargetClient.left;
    int lsH = rcTargetClient.bottom - rcTargetClient.top;
    if (lsW <= 0 || lsH <= 0) return result;

    auto& st = GetState();
    int side = st.settings.positionSide;
    int tW = targetWindowRect.right - targetWindowRect.left;

    RECT r = {0, 0, lsW, lsH};
    if (side == 0) { // Left
        r.left = targetWindowRect.left - lsW;
        r.top = rcTargetClient.top;
    } else if (side == 1) { // Right
        r.left = targetWindowRect.right;
        r.top = rcTargetClient.top;
    } else if (side == 2) { // Top
        r.left = targetWindowRect.left + tW / 2 - lsW / 2;
        r.top = targetWindowRect.top - lsH;
    } else { // Bottom
        r.left = targetWindowRect.left + tW / 2 - lsW / 2;
        r.top = targetWindowRect.bottom;
    }
    r.right = r.left + lsW;
    r.bottom = r.top + lsH;
    return r;
}

void UpdateTargetRect() {
    HWND hFg = GetForegroundWindow();
    if (!hFg) return;

    DWORD pid;
    GetWindowThreadProcessId(hFg, &pid);
    if (pid == GetCurrentProcessId()) return;

    RECT rcClient;
    if (!GetClientRect(hFg, &rcClient)) return;

    POINT tl = {rcClient.left, rcClient.top};
    POINT br = {rcClient.right, rcClient.bottom};
    ClientToScreen(hFg, &tl);
    ClientToScreen(hFg, &br);
    RECT rcScreen = {tl.x, tl.y, br.x, br.y};

    if (rcScreen.right <= rcScreen.left || rcScreen.bottom <= rcScreen.top) return;

    RECT positionedRect = rcScreen;
    auto& st = GetState();
    if (st.settings.positionMode) {
        positionedRect = CalculatePositionedRect(hFg, rcScreen);
    }

    std::lock_guard<std::mutex> lock(st.mutex);
    st.targetRect = rcScreen;
    st.hTargetWindow = hFg;
    if (st.settings.positionMode) {
        st.lsRect = positionedRect;
    } else if (st.lsRect.right == 0) {
        st.lsRect = st.targetRect;
    }
}

// ============================================================================
// Hook function pointers
// ============================================================================

typedef BOOL(WINAPI* EnumDisplayMonitors_t)(HDC, LPCRECT, MONITORENUMPROC, LPARAM);
typedef BOOL(WINAPI* GetMonitorInfoW_t)(HMONITOR, LPMONITORINFO);
typedef HRESULT(WINAPI* CreateDXGIFactory1_t)(REFIID, void**);

static EnumDisplayMonitors_t fpEnumDisplayMonitors = nullptr;
static GetMonitorInfoW_t fpGetMonitorInfoW = nullptr;
static CreateDXGIFactory1_t fpCreateDXGIFactory1 = nullptr;

// ============================================================================
// Watcher thread
// ============================================================================

static BOOL CALLBACK FindOverlayProc(HWND hwnd, LPARAM) {
    DWORD pid;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != GetCurrentProcessId()) return TRUE;
    if (!IsWindowVisible(hwnd)) return TRUE;

    RECT rc;
    GetWindowRect(hwnd, &rc);

    auto& st = GetState();
    bool matchTarget = (rc.left == st.targetRect.left && rc.top == st.targetRect.top &&
                        rc.right == st.targetRect.right && rc.bottom == st.targetRect.bottom);
    bool matchLS = (rc.left == st.lsRect.left && rc.top == st.lsRect.top &&
                    rc.right == st.lsRect.right && rc.bottom == st.lsRect.bottom);

    if (matchTarget || matchLS) {
        st.hFoundOverlay = hwnd;
        return FALSE;
    }
    return TRUE;
}

static void ApplyWindowRegion() {
    auto& st = GetState();
    HWND targetWindow;
    {
        std::lock_guard<std::mutex> lock(st.mutex);
        targetWindow = st.hTargetWindow;
    }
    if (!targetWindow) return;

    st.hFoundOverlay = nullptr;
    EnumWindows(FindOverlayProc, 0);

    // SetWindowRgn redraws the window: only call it when the region actually changes (it used to run every 200 ms)
    static HWND s_lastOverlay = nullptr; static int s_lastKind = -1, s_lastW = 0, s_lastH = 0;
    RECT tr; { std::lock_guard<std::mutex> lock(st.mutex); tr = st.targetRect; }
    const int wantKind = (st.settings.enabled && st.settings.splitMode) ? st.settings.splitType : -1;
    if (st.hFoundOverlay && st.hFoundOverlay == s_lastOverlay && wantKind == s_lastKind && (wantKind < 0 || (tr.right - tr.left == s_lastW && tr.bottom - tr.top == s_lastH))) return;
    if (st.hFoundOverlay) { s_lastOverlay = st.hFoundOverlay; s_lastKind = wantKind; s_lastW = tr.right - tr.left; s_lastH = tr.bottom - tr.top; }

    if (st.hFoundOverlay && st.settings.enabled && st.settings.splitMode) {
        RECT targetRect;
        {
            std::lock_guard<std::mutex> lock(st.mutex);
            targetRect = st.targetRect;
        }
        int w = targetRect.right - targetRect.left;
        int h = targetRect.bottom - targetRect.top;
        HRGN hrgn = nullptr;
        switch (st.settings.splitType) {
            case 0: hrgn = CreateRectRgn(0, 0, w / 2, h); break;
            case 1: hrgn = CreateRectRgn(w / 2, 0, w, h); break;
            case 2: hrgn = CreateRectRgn(0, 0, w, h / 2); break;
            case 3: hrgn = CreateRectRgn(0, h / 2, w, h); break;
        }
        if (hrgn) SetWindowRgn(st.hFoundOverlay, hrgn, TRUE);
    } else if (st.hFoundOverlay) {
        SetWindowRgn(st.hFoundOverlay, NULL, TRUE);
    }
}

static void UpdateWindowPositions() {
    auto& st = GetState();
    if (!st.settings.enabled) return;
    if (!st.settings.positionMode) {
        std::lock_guard<std::mutex> lock(st.mutex);
        st.lsRect = st.targetRect;
        return;
    }

    HWND targetWindow;
    RECT targetRect;
    {
        std::lock_guard<std::mutex> lock(st.mutex);
        targetWindow = st.hTargetWindow;
        targetRect = st.targetRect;
    }
    if (!targetWindow || !IsWindow(targetWindow)) return;

    RECT newLSRect = CalculatePositionedRect(targetWindow, targetRect);
    {
        std::lock_guard<std::mutex> lock(st.mutex);
        st.lsRect = newLSRect;
    }

    if (st.hFoundOverlay && IsWindow(st.hFoundOverlay)) {
        RECT cur;
        GetWindowRect(st.hFoundOverlay, &cur);
        int w = newLSRect.right - newLSRect.left;
        int h = newLSRect.bottom - newLSRect.top;
        if (abs(cur.left - newLSRect.left) > 2 || abs(cur.top - newLSRect.top) > 2) {
            SetWindowPos(st.hFoundOverlay, NULL, newLSRect.left, newLSRect.top, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }
}

static DWORD WINAPI WatcherThread(LPVOID) {
    while (GetState().running) {
        ApplyWindowRegion();
        UpdateWindowPositions();
        Sleep(200);
    }
    return 0;
}

// ============================================================================
// Detour hooks
// ============================================================================

static BOOL WINAPI Detour_EnumDisplayMonitors(HDC hdc, LPCRECT lprcClip, MONITORENUMPROC lpfnEnum, LPARAM dwData) {
    BOOL result = fpEnumDisplayMonitors(hdc, lprcClip, lpfnEnum, dwData);
    if (result && GetState().settings.enabled) {
        __try { lpfnEnum(FAKE_VIRTUAL_MONITOR, hdc, nullptr, dwData); }
        __except(EXCEPTION_EXECUTE_HANDLER) {}
    }
    return result;
}

static BOOL WINAPI Detour_GetMonitorInfoW(HMONITOR hMonitor, LPMONITORINFO lpmi) {
    if (hMonitor == FAKE_VIRTUAL_MONITOR) {
        if (!lpmi) return FALSE;
        UpdateTargetRect();
        auto& st = GetState();
        std::lock_guard<std::mutex> lock(st.mutex);
        lpmi->rcMonitor = st.settings.positionMode ? st.lsRect : st.targetRect;
        lpmi->rcWork = lpmi->rcMonitor;
        lpmi->dwFlags = 0;
        if (lpmi->cbSize >= sizeof(MONITORINFOEXW)) {
            wcsncpy_s(((LPMONITORINFOEXW)lpmi)->szDevice, L"\\\\.\\DISPLAY_WINDOWED", CCHDEVICENAME);
        }
        return TRUE;
    }
    return fpGetMonitorInfoW(hMonitor, lpmi);
}

static HRESULT WINAPI Detour_CreateDXGIFactory1(REFIID riid, void** ppFactory) {
    HRESULT hr = fpCreateDXGIFactory1(riid, ppFactory);
    if (SUCCEEDED(hr) && ppFactory && *ppFactory) {
        bool shouldWrap = (riid == __uuidof(IDXGIFactory) || riid == __uuidof(IDXGIFactory1) ||
                           riid == __uuidof(IDXGIFactory2) || riid == __uuidof(IDXGIFactory3) ||
                           riid == __uuidof(IDXGIFactory4) || riid == __uuidof(IDXGIFactory5) ||
                           riid == __uuidof(IDXGIFactory6));
        if (shouldWrap) {
            IUnknown* pUnk = (IUnknown*)*ppFactory;
            g_FactoryAlive = true;
            PinModule();
            IDXGIFactory6* pReal6 = nullptr;
            if (SUCCEEDED(pUnk->QueryInterface(__uuidof(IDXGIFactory6), (void**)&pReal6))) {
                *ppFactory = new ProxyDXGIFactory(pReal6);
                pUnk->Release();
                HostLog(LSPROXY_LOG_DEBUG, "Factory wrapped (IDXGIFactory6)");
            } else {
                IDXGIFactory2* pReal2 = nullptr;
                if (SUCCEEDED(pUnk->QueryInterface(__uuidof(IDXGIFactory2), (void**)&pReal2))) {
                    *ppFactory = new ProxyDXGIFactory((IDXGIFactory6*)pReal2);
                    pUnk->Release();
                    HostLog(LSPROXY_LOG_WARN, "Factory wrapped (IDXGIFactory2 fallback)");
                }
            }
        }
    }
    return hr;
}

// ============================================================================
// Hook init/cleanup
// ============================================================================

// Once DXGI objects with vtables in this DLL exist, Lossless Scaling may hold them past the addon's shutdown: never unload.
static void PinModule() {
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN, (LPCWSTR)&PinModule, &self);
}
static bool g_hooksUp = false;
static HANDLE g_watcherThread = nullptr, g_initThread = nullptr;
static void InitHooks() {
    HostLog(LSPROXY_LOG_INFO, "Initializing MinHook...");
    if (MH_Initialize() != MH_OK) {
        HostLog(LSPROXY_LOG_ERROR, "Failed to initialize MinHook");
        return;
    }

    MH_CreateHookApi(L"user32.dll", "EnumDisplayMonitors", &Detour_EnumDisplayMonitors, (LPVOID*)&fpEnumDisplayMonitors);
    MH_CreateHookApi(L"user32.dll", "GetMonitorInfoW", &Detour_GetMonitorInfoW, (LPVOID*)&fpGetMonitorInfoW);
    MH_CreateHookApi(L"dxgi.dll", "CreateDXGIFactory1", &Detour_CreateDXGIFactory1, (LPVOID*)&fpCreateDXGIFactory1);

    if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK) {
        HostLog(LSPROXY_LOG_ERROR, "Failed to enable hooks");
        return;
    }
    g_hooksUp = true;
    HostLog(LSPROXY_LOG_INFO, "Hooks installed successfully");
}
static DWORD WINAPI InitHooksThread(LPVOID) { InitHooks(); return 0; }

static void RemoveHooks() {
    // Stop the watcher first (it must not be inside our code when the DLL goes away), then take the hooks out once.
    GetState().running = false;
    if (g_initThread) { WaitForSingleObject(g_initThread, 5000); CloseHandle(g_initThread); g_initThread = nullptr; }
    if (g_watcherThread) { WaitForSingleObject(g_watcherThread, 2000); CloseHandle(g_watcherThread); g_watcherThread = nullptr; }
    if (!g_hooksUp) return;
    g_hooksUp = false;
    MH_DisableHook(MH_ALL_HOOKS);
    MH_Uninitialize();
    HostLog(LSPROXY_LOG_INFO, "Hooks removed");
}

// ============================================================================
// Settings persistence via IHost config
// ============================================================================

static void LoadSettings() {
    if (!g_host) return;
    auto& s = GetState().settings;
    s.enabled = std::string(g_host->GetConfig(ADDON_ID, "enabled", "1")) != "0";
    s.splitMode = std::string(g_host->GetConfig(ADDON_ID, "split_mode", "0")) == "1";
    s.splitType = std::atoi(g_host->GetConfig(ADDON_ID, "split_type", "0"));
    s.positionMode = std::string(g_host->GetConfig(ADDON_ID, "position_mode", "0")) == "1";
    s.positionSide = std::atoi(g_host->GetConfig(ADDON_ID, "position_side", "1"));
    HostLog(LSPROXY_LOG_DEBUG, "Settings loaded: split=%d/%d pos=%d/%d",
            s.splitMode, s.splitType, s.positionMode, s.positionSide);
}

static void SaveSettings() {
    if (!g_host) return;
    auto& s = GetState().settings;
    g_host->SetConfig(ADDON_ID, "enabled", s.enabled ? "1" : "0");
    g_host->SetConfig(ADDON_ID, "split_mode", s.splitMode ? "1" : "0");
    g_host->SetConfig(ADDON_ID, "split_type", std::to_string(s.splitType).c_str());
    g_host->SetConfig(ADDON_ID, "position_mode", s.positionMode ? "1" : "0");
    g_host->SetConfig(ADDON_ID, "position_side", std::to_string(s.positionSide).c_str());
    g_host->SaveConfig();
}

// ============================================================================
// Addon exports
// ============================================================================

LSPROXY_EXPORT void AddonInitialize(IHost* host, ImGuiContext* ctx,
                                    void* allocFunc, void* freeFunc, void* userData) {
    ImGui::SetCurrentContext(ctx);
    ImGui::SetAllocatorFunctions((ImGuiMemAllocFunc)allocFunc, (ImGuiMemFreeFunc)freeFunc, userData);
    lsp::InitAddonImGui();

    g_host = host;
    LoadSettings();
    HostLog(LSPROXY_LOG_INFO, "Windowed Mode addon initialized");
}

LSPROXY_EXPORT void AddonShutdown() {
    HostLog(LSPROXY_LOG_INFO, "Windowed Mode addon shutting down");
    GetState().running = false;
    RemoveHooks();
    g_host = nullptr;
}

LSPROXY_EXPORT uint32_t GetAddonCapabilities() {
    return LSPROXY_CAP_HAS_SETTINGS | LSPROXY_CAP_REQUIRES_RESTART;
}

LSPROXY_EXPORT void AddonRenderSettings() {
    auto& s = GetState().settings;
    bool changed = false;

    ImGui::TextWrapped("Lets Lossless Scaling work with a windowed game, and with a second monitor: it adds a virtual display that matches "
                       "the size and position of the window you are playing in.");
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 5));
    if (ImGui::Checkbox("Enable the virtual display", &s.enabled)) {
        HostLog(LSPROXY_LOG_INFO, "Windowed mode: %s", s.enabled ? "ON" : "OFF");
        changed = true;
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("Master switch. On: Lossless Scaling sees an extra display the size of your game window, so it can scale a window\n"
                          "instead of needing full screen, and the two modes below become available. Off: Lossless Scaling sees only your real\n"
                          "monitors and nothing is moved. Takes effect the next time scaling starts. (The addon itself must have been enabled\n"
                          "before Lossless Scaling started; it cannot be loaded into a running Lossless Scaling.)");
    if (!s.enabled) ImGui::BeginDisabled();
    ImGui::Dummy(ImVec2(0, 3));
    lsp::SectionLabel("Split mode");

    // Split Mode
    if (s.positionMode) ImGui::BeginDisabled();
    if (ImGui::Checkbox("Enable Split Mode", &s.splitMode)) {
        HostLog(LSPROXY_LOG_INFO, "Split mode: %s", s.splitMode ? "ON" : "OFF");
        changed = true;
    }
    if (s.positionMode) ImGui::EndDisabled();

    if (s.splitMode) {
        const char* splitTypes[] = {"Left", "Right", "Top", "Bottom"};
        if (ImGui::Combo("Split Type", &s.splitType, splitTypes, 4)) {
            changed = true;
        }
    }

    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 3));
    lsp::SectionLabel("Window positioning");

    // Position Mode
    if (s.splitMode) ImGui::BeginDisabled();
    if (ImGui::Checkbox("Enable Window Positioning", &s.positionMode)) {
        HostLog(LSPROXY_LOG_INFO, "Position mode: %s", s.positionMode ? "ON" : "OFF");
        changed = true;
    }
    if (s.splitMode) ImGui::EndDisabled();

    if (s.positionMode) {
        const char* posTypes[] = {"Left", "Right", "Top", "Bottom"};
        if (ImGui::Combo("Position Side", &s.positionSide, posTypes, 4)) {
            changed = true;
        }
        ImGui::TextWrapped("Positions the LS overlay window adjacent to the target window.");
    }
    if (!s.enabled) ImGui::EndDisabled();

    if (changed) SaveSettings();
}

LSPROXY_EXPORT const char* GetAddonName() { return "Windowed Mode"; }
LSPROXY_EXPORT const char* GetAddonVersion() { return "0.1.0"; }
LSPROXY_EXPORT const char* GetAddonAuthor() { return "FrankBarretta / Echo-Storm"; }
LSPROXY_EXPORT const char* GetAddonDescription() {
    return "Lets Lossless Scaling work with a windowed game and with a second monitor: adds a virtual "
           "display the size of your game window, with split-screen and side-by-side positioning.";
}

// ============================================================================
// DLL Entry Point
// ============================================================================

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        // Start hooks and watcher in separate threads to avoid loader lock
        g_initThread = CreateThread(nullptr, 0, InitHooksThread, nullptr, 0, nullptr);
        g_watcherThread = CreateThread(nullptr, 0, WatcherThread, nullptr, 0, nullptr);
        break;
    case DLL_PROCESS_DETACH:
        // AddonShutdown normally ran RemoveHooks already; at process exit just tell the watcher to stop (waiting or
        // unhooking under the loader lock is not safe).
        GetState().running = false;
        break;
    }
    return TRUE;
}
