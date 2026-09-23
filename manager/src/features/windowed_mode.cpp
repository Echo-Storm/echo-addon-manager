#include "windowed_mode.h"
#include "windowed_dxgi.h"
#include "windowed_state.h"
#include "../config/config_manager.h"
#include "../log/logger.h"
#include "imgui.h"
#include "lsproxy/lsp_widgets.h"
#include <MinHook.h>
#include <d3d11.h>
#include <dxgi.h>
#include <dxgi1_2.h>
#include <cstdlib>
#include <string>

namespace lsproxy {
namespace features {
namespace windowed {

namespace {

// The handle the virtual display's outputs report as their monitor; Windows' own APIs are made to recognise it.
const HMONITOR kVirtualMonitor = (HMONITOR)0xBADF00D;

ConfigManager& Cfg() { return ConfigManager::Instance(); }

// ---------------------------------------------------------------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------------------------------------------------------------

// A setting from this feature's section, or from the section the old standalone addon used, or the default.
std::string Setting(const char* key, const char* dflt) {
    static const std::string kAbsent = "<absent>";   // a value no real setting has
    std::string v = Cfg().Get(kId, key, kAbsent);
    if (v == kAbsent) v = Cfg().Get("LSP-Windowed-Mode", key, dflt);
    return v;
}

void LoadSettings() {
    Settings& s = GetState().settings;
    s.splitMode = Setting("split_mode", "0") == "1";
    s.splitType = std::atoi(Setting("split_type", "0").c_str());
    s.positionMode = Setting("position_mode", "0") == "1";
    s.positionSide = std::atoi(Setting("position_side", "1").c_str());
    LOG_DEBUG("Windowed", "Settings loaded: split=%d/%d position=%d/%d", s.splitMode, s.splitType, s.positionMode, s.positionSide);
}

void SaveSettings() {
    const Settings& s = GetState().settings;
    Cfg().Set(kId, "split_mode", s.splitMode ? "1" : "0");
    Cfg().Set(kId, "split_type", std::to_string(s.splitType));
    Cfg().Set(kId, "position_mode", s.positionMode ? "1" : "0");
    Cfg().Set(kId, "position_side", std::to_string(s.positionSide));
    Cfg().Save();
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Where the game window is, and where Lossless Scaling's window belongs
// ---------------------------------------------------------------------------------------------------------------------------------

RECT PositionBesideTarget(HWND target, RECT lsRect) {
    RECT result = lsRect;
    if (!target || !IsWindow(target)) return result;

    RECT window;
    if (!GetWindowRect(target, &window)) return result;
    const int w = lsRect.right - lsRect.left, h = lsRect.bottom - lsRect.top;
    if (w <= 0 || h <= 0) return result;

    const int targetWidth = window.right - window.left;
    RECT r = { 0, 0, w, h };
    switch (GetState().settings.positionSide) {
        case 0: r.left = window.left - w; r.top = lsRect.top; break;                                   // left of the game window
        case 1: r.left = window.right; r.top = lsRect.top; break;                                      // right of it
        case 2: r.left = window.left + targetWidth / 2 - w / 2; r.top = window.top - h; break;          // above it
        default: r.left = window.left + targetWidth / 2 - w / 2; r.top = window.bottom; break;          // below it
    }
    r.right = r.left + w;
    r.bottom = r.top + h;
    return r;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Hooks: what Lossless Scaling sees when it asks Windows and DXGI for displays
// ---------------------------------------------------------------------------------------------------------------------------------

using EnumDisplayMonitors_t = BOOL(WINAPI*)(HDC, LPCRECT, MONITORENUMPROC, LPARAM);
using GetMonitorInfoW_t = BOOL(WINAPI*)(HMONITOR, LPMONITORINFO);
using CreateDXGIFactory1_t = HRESULT(WINAPI*)(REFIID, void**);

EnumDisplayMonitors_t g_realEnumDisplayMonitors = nullptr;
GetMonitorInfoW_t g_realGetMonitorInfoW = nullptr;
CreateDXGIFactory1_t g_realCreateDXGIFactory1 = nullptr;

// Once wrapped DXGI objects exist, Lossless Scaling may hold them for the rest of the process, and their code lives here: never unload.
void PinThisModule() {
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN, (LPCWSTR)&PinThisModule, &self);
}

BOOL WINAPI EnumDisplayMonitorsHook(HDC hdc, LPCRECT clip, MONITORENUMPROC callback, LPARAM data) {
    const BOOL result = g_realEnumDisplayMonitors(hdc, clip, callback, data);
    if (result && GetState().settings.active) {
        __try { callback(kVirtualMonitor, hdc, nullptr, data); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    return result;
}

BOOL WINAPI GetMonitorInfoHook(HMONITOR monitor, LPMONITORINFO info) {
    if (monitor != kVirtualMonitor) return g_realGetMonitorInfoW(monitor, info);
    if (!info) return FALSE;

    UpdateTargetRect();
    State& st = GetState();
    std::lock_guard<std::mutex> lock(st.mutex);
    info->rcMonitor = st.settings.positionMode ? st.lsRect : st.targetRect;
    info->rcWork = info->rcMonitor;
    info->dwFlags = 0;
    if (info->cbSize >= sizeof(MONITORINFOEXW)) wcsncpy_s(((LPMONITORINFOEXW)info)->szDevice, L"\\\\.\\DISPLAY_WINDOWED", CCHDEVICENAME);
    return TRUE;
}

HRESULT WINAPI CreateDXGIFactory1Hook(REFIID riid, void** factory) {
    const HRESULT hr = g_realCreateDXGIFactory1(riid, factory);
    if (FAILED(hr) || !factory || !*factory) return hr;

    const bool wanted = riid == __uuidof(IDXGIFactory) || riid == __uuidof(IDXGIFactory1) || riid == __uuidof(IDXGIFactory2) ||
                        riid == __uuidof(IDXGIFactory3) || riid == __uuidof(IDXGIFactory4) || riid == __uuidof(IDXGIFactory5) ||
                        riid == __uuidof(IDXGIFactory6);
    if (!wanted) return hr;

    IUnknown* real = (IUnknown*)*factory;
    g_FactoryAlive = true;
    PinThisModule();

    IDXGIFactory6* factory6 = nullptr;
    if (SUCCEEDED(real->QueryInterface(__uuidof(IDXGIFactory6), (void**)&factory6))) {
        *factory = new ProxyDXGIFactory(factory6);
        real->Release();
        LOG_DEBUG("Windowed", "Factory wrapped (IDXGIFactory6)");
        return hr;
    }
    // Every supported Windows has IDXGIFactory6. Wrapping an older factory as if it were one would call methods it does not have: leave it as it is.
    LOG_WARN("Windowed", "This Windows has no IDXGIFactory6; the factory is left unwrapped and Windowed mode does not apply");
    return hr;
}

bool g_started = false;           // Start() has run: the hooks are installed or about to be
bool g_hooksUp = false;
HANDLE g_initThread = nullptr;
HANDLE g_watcherThread = nullptr;

void InstallHooks() {
    LOG_INFO("Windowed", "Installing hooks...");
    if (MH_Initialize() != MH_OK) {
        LOG_ERROR("Windowed", "Failed to initialize MinHook");
        return;
    }
    MH_CreateHookApi(L"user32.dll", "EnumDisplayMonitors", &EnumDisplayMonitorsHook, (LPVOID*)&g_realEnumDisplayMonitors);
    MH_CreateHookApi(L"user32.dll", "GetMonitorInfoW", &GetMonitorInfoHook, (LPVOID*)&g_realGetMonitorInfoW);
    MH_CreateHookApi(L"dxgi.dll", "CreateDXGIFactory1", &CreateDXGIFactory1Hook, (LPVOID*)&g_realCreateDXGIFactory1);
    if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK) {
        LOG_ERROR("Windowed", "Failed to enable hooks");
        return;
    }
    g_hooksUp = true;
    LOG_INFO("Windowed", "Hooks installed");
}

DWORD WINAPI InstallHooksThread(LPVOID) { InstallHooks(); return 0; }   // on its own thread: not while the loader lock is held

void RemoveHooks() {
    GetState().running = false;   // the watcher must not be inside our code when the hooks come out
    if (g_initThread) { WaitForSingleObject(g_initThread, 5000); CloseHandle(g_initThread); g_initThread = nullptr; }
    if (g_watcherThread) { WaitForSingleObject(g_watcherThread, 2000); CloseHandle(g_watcherThread); g_watcherThread = nullptr; }
    if (!g_hooksUp) return;
    g_hooksUp = false;
    MH_DisableHook(MH_ALL_HOOKS);
    MH_Uninitialize();
    LOG_INFO("Windowed", "Hooks removed");
}

// ---------------------------------------------------------------------------------------------------------------------------------
// The watcher: keeps Lossless Scaling's window over the right part of the screen
// ---------------------------------------------------------------------------------------------------------------------------------

BOOL CALLBACK FindOverlay(HWND hwnd, LPARAM) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != GetCurrentProcessId() || !IsWindowVisible(hwnd)) return TRUE;

    RECT rc;
    GetWindowRect(hwnd, &rc);
    State& st = GetState();
    auto same = [&](const RECT& r) { return rc.left == r.left && rc.top == r.top && rc.right == r.right && rc.bottom == r.bottom; };
    if (same(st.targetRect) || same(st.lsRect)) {
        st.overlay = hwnd;
        return FALSE;
    }
    return TRUE;
}

// In split mode Lossless Scaling's window is clipped to one half of the game window.
void ApplySplitRegion() {
    State& st = GetState();
    HWND target;
    RECT targetRect;
    {
        std::lock_guard<std::mutex> lock(st.mutex);
        target = st.targetWindow;
        targetRect = st.targetRect;
    }
    if (!target) return;

    st.overlay = nullptr;
    EnumWindows(FindOverlay, 0);

    // SetWindowRgn redraws the window, so only call it when the region actually changes (it used to run every 200 ms).
    static HWND lastOverlay = nullptr;
    static int lastKind = -1, lastW = 0, lastH = 0;
    const int wantKind = (st.settings.active && st.settings.splitMode) ? st.settings.splitType : -1;
    const int w = targetRect.right - targetRect.left, h = targetRect.bottom - targetRect.top;
    if (st.overlay && st.overlay == lastOverlay && wantKind == lastKind && (wantKind < 0 || (w == lastW && h == lastH))) return;
    if (st.overlay) { lastOverlay = st.overlay; lastKind = wantKind; lastW = w; lastH = h; }

    if (st.overlay && st.settings.active && st.settings.splitMode) {
        HRGN region = nullptr;
        switch (st.settings.splitType) {
            case 0: region = CreateRectRgn(0, 0, w / 2, h); break;
            case 1: region = CreateRectRgn(w / 2, 0, w, h); break;
            case 2: region = CreateRectRgn(0, 0, w, h / 2); break;
            case 3: region = CreateRectRgn(0, h / 2, w, h); break;
        }
        if (region) SetWindowRgn(st.overlay, region, TRUE);
    } else if (st.overlay) {
        SetWindowRgn(st.overlay, nullptr, TRUE);
    }
}

// In positioning mode Lossless Scaling's window is moved beside the game window.
void MoveBesideTarget() {
    State& st = GetState();
    if (!st.settings.active) return;
    if (!st.settings.positionMode) {
        std::lock_guard<std::mutex> lock(st.mutex);
        st.lsRect = st.targetRect;
        return;
    }

    HWND target;
    RECT targetRect;
    {
        std::lock_guard<std::mutex> lock(st.mutex);
        target = st.targetWindow;
        targetRect = st.targetRect;
    }
    if (!target || !IsWindow(target)) return;

    const RECT wanted = PositionBesideTarget(target, targetRect);
    {
        std::lock_guard<std::mutex> lock(st.mutex);
        st.lsRect = wanted;
    }
    if (st.overlay && IsWindow(st.overlay)) {
        RECT now;
        GetWindowRect(st.overlay, &now);
        if (abs(now.left - wanted.left) > 2 || abs(now.top - wanted.top) > 2)
            SetWindowPos(st.overlay, nullptr, wanted.left, wanted.top, wanted.right - wanted.left, wanted.bottom - wanted.top, SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

DWORD WINAPI WatcherThread(LPVOID) {
    while (GetState().running) {
        ApplySplitRegion();
        MoveBesideTarget();
        Sleep(200);
    }
    return 0;
}

const char* const kSides[] = { "Left", "Right", "Top", "Bottom" };

} // namespace

// ---------------------------------------------------------------------------------------------------------------------------------
// Used by the virtual display (windowed_dxgi.cpp) and the hooks
// ---------------------------------------------------------------------------------------------------------------------------------

void UpdateTargetRect() {
    const HWND foreground = GetForegroundWindow();
    if (!foreground) return;

    DWORD pid = 0;
    GetWindowThreadProcessId(foreground, &pid);
    if (pid == GetCurrentProcessId()) return;

    RECT client;
    if (!GetClientRect(foreground, &client)) return;
    POINT topLeft = { client.left, client.top }, bottomRight = { client.right, client.bottom };
    ClientToScreen(foreground, &topLeft);
    ClientToScreen(foreground, &bottomRight);
    const RECT onScreen = { topLeft.x, topLeft.y, bottomRight.x, bottomRight.y };
    if (onScreen.right <= onScreen.left || onScreen.bottom <= onScreen.top) return;

    State& st = GetState();
    const bool positioning = st.settings.positionMode;
    const RECT beside = positioning ? PositionBesideTarget(foreground, onScreen) : onScreen;

    std::lock_guard<std::mutex> lock(st.mutex);
    st.targetRect = onScreen;
    st.targetWindow = foreground;
    if (positioning) st.lsRect = beside;
    else if (st.lsRect.right == 0) st.lsRect = st.targetRect;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// The feature
// ---------------------------------------------------------------------------------------------------------------------------------

void Start() {
    if (g_started) return;
    g_started = true;
    LoadSettings();
    GetState().settings.active = true;
    GetState().running = true;
    g_initThread = CreateThread(nullptr, 0, InstallHooksThread, nullptr, 0, nullptr);
    g_watcherThread = CreateThread(nullptr, 0, WatcherThread, nullptr, 0, nullptr);
    LOG_INFO("Windowed", "Windowed mode is on");
}

void Stop() {
    if (!g_started) return;
    RemoveHooks();
    g_started = false;
}

bool Started() { return g_started; }

void SetActive(bool on) {
    GetState().settings.active = on;
    LOG_INFO("Windowed", "Windowed mode %s", on ? "on" : "off");
}

std::string Status() {
    if (!g_started || !GetState().settings.active) return {};
    const Settings& s = GetState().settings;
    if (s.splitMode) return std::string("Split: ") + kSides[s.splitType & 3];
    if (s.positionMode) return std::string("Beside the game: ") + kSides[s.positionSide & 3];
    return "Virtual display on";
}

void RenderOptions() {
    Settings& s = GetState().settings;
    bool changed = false;

    if (s.positionMode) ImGui::BeginDisabled();
    if (ImGui::Checkbox("Split screen", &s.splitMode)) changed = true;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Lossless Scaling covers only one half of the game window, so the other half stays as it is.\n"
                          "Not available together with Window positioning.");
    if (s.positionMode) ImGui::EndDisabled();
    if (s.splitMode) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7.0f);
        if (ImGui::Combo("##windowed_split", &s.splitType, kSides, 4)) changed = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Which half of the game window Lossless Scaling covers.");
    }

    if (s.splitMode) ImGui::BeginDisabled();
    if (ImGui::Checkbox("Window positioning", &s.positionMode)) changed = true;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Lossless Scaling's window is put beside the game window (for a second monitor or a split desktop)\n"
                          "instead of on top of it. Not available together with Split screen.");
    if (s.splitMode) ImGui::EndDisabled();
    if (s.positionMode) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7.0f);
        if (ImGui::Combo("##windowed_side", &s.positionSide, kSides, 4)) changed = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Which side of the game window Lossless Scaling's window sits on.");
    }

    if (changed) SaveSettings();
}

} // namespace windowed
} // namespace features
} // namespace lsproxy
