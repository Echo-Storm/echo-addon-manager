#include "gui_manager.h"
#include "gui_scale.h"
#include "gui_style.h"
#include "icon_loader.h"
#include "tabs/tab_addons.h"
#include "widgets/toast.h"
#include "window/chrome.h"
#include "window/dpi.h"
#include "window/hotkey.h"
#include "window/main_frame.h"
#include "window/placement.h"
#include "window/tray.h"
#include "window/window_device.h"
#include "../addon/addon_manager.h"
#include "../config/config_manager.h"
#include "../host/gpu_stats.h"
#include "../log/logger.h"
#include "../../sdk/include/lsproxy/version.h"
#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include <shellapi.h>
#include <atomic>
#include <cmath>
#include <string>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace lsproxy {

namespace {

constexpr const wchar_t* kWindowClass = L"EchoAddonManagerClass";

// Everything the window and its thread share. One window per process. It is allocated once and never destroyed: static destructors run
// under the loader lock when the DLL unloads, which is no place to release D3D objects.
struct Shell {
    AddonManager* manager = nullptr;
    HWND hwnd = nullptr;                    // set once the window and its device exist
    std::atomic<bool> hidden{ false };      // closed to the tray: the window exists but is not shown, and nothing is drawn
    std::atomic<bool> scaleDirty{ false };  // the interface-size setting changed: apply it before the next frame
    bool minimized = false;
    bool bringAddonsForward = false;        // set by a drop: show the Addons tab so the confirmation is seen
    float displayScale = 1.0f;              // what Windows asks for (monitor dpi / 96)
    UINT taskbarCreated = 0;                // broadcast when Explorer restarts and the tray icons have to be added again
    HICON trayIcon = nullptr;
    window::WindowDevice device;
    window::SavedPlacement saved;           // captured when the window is closed, written to the settings when the thread winds down
};
Shell& g = *new Shell;

void ShowManager(bool show) {
    if (!g.hwnd) return;
    if (show) {
        ShowWindow(g.hwnd, IsIconic(g.hwnd) ? SW_RESTORE : SW_SHOW);
        SetForegroundWindow(g.hwnd);
        g.hidden = false;
    } else {
        ShowWindow(g.hwnd, SW_HIDE);
        g.hidden = true;
    }
}

void PersistPlacement() {
    if (!g.saved.valid) return;
    auto& cfg = ConfigManager::Instance();
    cfg.GlobalSet("ui", "window", window::ToJson(g.saved));
    cfg.Save();
}

float TotalScale() { return g.displayScale * window::UserScale(); }

std::wstring WindowTitle() {
    std::wstring title = LSPROXY_PRODUCT_NAME_W L" v";
    for (const char* c = LSPROXY_VERSION_STRING; *c; ++c) title += static_cast<wchar_t>(*c);
    return title;
}

// Closing hides the window instead of ending the GUI thread: the addons keep their ImGui context, and the manager can be reopened from
// the tray icon or the hotkey. The first time, a balloon says so.
void OnClose(HWND hwnd) {
    const window::SavedPlacement now = window::CapturePlacement(hwnd, g.displayScale);
    if (now.valid) g.saved = now;
    PersistPlacement();
    ShowManager(false);

    auto& cfg = ConfigManager::Instance();
    if (cfg.GlobalGetOr<bool>("ui", "hide_hint_shown", false)) return;
    std::wstring text = L"Click its icon in the notification area";
    if (window::hotkey::Registered()) text += L" or press " + window::hotkey::LabelFromConfig();
    text += L" to open it again.";
    window::tray::Balloon(L"The addon manager is still running", text.c_str());
    cfg.GlobalSet("ui", "hide_hint_shown", true);
    cfg.Save();
}

LRESULT WINAPI WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam)) return true;

    if (g.taskbarCreated && msg == g.taskbarCreated) {   // Explorer restarted: its tray forgot our icon
        window::tray::ReAdd(hwnd, g.trayIcon, window::hotkey::Registered() ? window::hotkey::LabelFromConfig() : L"");
        return 0;
    }

    switch (msg) {
    case WM_SIZE:
        g.minimized = (wParam == SIZE_MINIMIZED);
        if (!g.minimized) g.device.Resize(LOWORD(lParam), HIWORD(lParam));
        return 0;

    case WM_DROPFILES: {   // an addon (folder, .zip or .dll) dropped on the window: ask before installing it
        HDROP drop = reinterpret_cast<HDROP>(wParam);
        wchar_t path[2048];
        if (DragQueryFileW(drop, 0, path, 2048)) { RequestInstallFromPath(path); g.bringAddonsForward = true; }
        DragFinish(drop);
        return 0;
    }

    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
        info->ptMinTrackSize.x = static_cast<LONG>(S(static_cast<float>(window::kMinLogicalW)));
        info->ptMinTrackSize.y = static_cast<LONG>(S(static_cast<float>(window::kMinLogicalH)));
        return 0;
    }

    case WM_DPICHANGED: {
        // Moved to a monitor with a different scale: rebuild the style at the new scale (fonts are rasterised at the final size, so no
        // atlas rebuild is needed) and adopt the rectangle Windows suggests so the window keeps its apparent size.
        g.displayScale = HIWORD(wParam) / 96.0f;
        ApplyUiScale(TotalScale());
        const RECT* r = reinterpret_cast<const RECT*>(lParam);
        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }

    case WM_CLOSE:
        OnClose(hwnd);
        return 0;

    case window::tray::kMessage:
        if (LOWORD(lParam) == WM_LBUTTONUP) {
            ShowManager(g.hidden);
        } else if (LOWORD(lParam) == WM_RBUTTONUP) {
            bool toggle = false;
            window::tray::ShowMenu(hwnd, g.hidden, toggle);
            if (toggle) ShowManager(g.hidden);
        }
        return 0;

    case WM_HOTKEY:
        if (static_cast<int>(wParam) == window::hotkey::kId) ShowManager(g.hidden);
        return 0;

    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) return 0;   // Alt alone does not open the (absent) system menu
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool OnScreen(const RECT& r) { return MonitorFromRect(&r, MONITOR_DEFAULTTONULL) != nullptr; }

} // namespace

void GuiManager::RequestUserScale() { g.scaleDirty = true; }

void GuiManager::ApplyHotkey() {
    if (!g.hwnd) return;
    window::hotkey::Apply(g.hwnd);
    window::tray::SetTip(window::hotkey::Registered() ? window::hotkey::LabelFromConfig() : L"");
}

const char* GuiManager::HotkeyStatus() { return window::hotkey::Status(); }
void GuiManager::ToggleWindow() { ShowManager(g.hidden); }
bool GuiManager::WindowVisible() { return !g.hidden; }

void GuiManager::StartGuiThread(AddonManager* manager) {
    g.manager = manager;
    if (HANDLE thread = CreateThread(nullptr, 0, GuiThread, nullptr, 0, nullptr)) CloseHandle(thread);
}

DWORD WINAPI GuiManager::GuiThread(LPVOID /*lpParam*/) {
    auto& cfg = ConfigManager::Instance();

    // Load addons in the GUI thread to avoid the loader lock. Settings > "Auto-load addons on startup": when off, addons wait for "Load now".
    if (g.manager) {
        if (cfg.GlobalGetOr<bool>(nullptr, "auto_load", true)) g.manager->LoadAddons();
        else LOG_INFO("GUI", "Auto-load is off; addons stay unloaded until loaded from the manager");
    }

    // Pairs with the DPI-awareness call in Core::Init. The window is created at the system scale and corrected below once we know which
    // monitor it actually landed on.
    const float systemScale = window::DisplayScale(nullptr);
    const window::Placement place = window::InitialPlacement(cfg.GlobalGet("ui", "window"), systemScale, OnScreen);

    const window::AppIcons icons = window::LoadAppIcons();

    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandleW(nullptr), icons.big, nullptr, nullptr, nullptr, kWindowClass, icons.little };
    RegisterClassExW(&wc);
    const std::wstring title = WindowTitle();
    HWND hwnd = CreateWindowW(kWindowClass, title.c_str(), WS_OVERLAPPEDWINDOW, place.x, place.y, place.w, place.h, nullptr, nullptr, wc.hInstance, nullptr);
    window::ApplyDarkTitleBar(hwnd);

    g.displayScale = window::DisplayScale(hwnd);
    if (std::abs(g.displayScale - systemScale) > 0.01f) {
        SetWindowPos(hwnd, nullptr, place.x, place.y, static_cast<int>(place.w * g.displayScale / systemScale),
                     static_cast<int>(place.h * g.displayScale / systemScale), SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (icons.big) {   // in case the class icons did not apply
        SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(icons.big));
        SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icons.little));
    }

    if (!g.device.Create(hwnd)) {
        LOG_ERROR("GUI", "Could not create the manager window's D3D11 device; the manager UI is unavailable");
        DestroyWindow(hwnd);
        UnregisterClassW(kWindowClass, wc.hInstance);
        return 1;
    }

    g.hwnd = hwnd;
    DragAcceptFiles(hwnd, TRUE);   // drop an addon on the window to install it
    g.trayIcon = icons.little;
    g.taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    window::tray::Add(hwnd, g.trayIcon, L"");
    GuiManager::ApplyHotkey();     // registers the hotkey and puts it in the tray tip
    if (cfg.GlobalGetOr<bool>("ui", "open_on_start", true)) {
        ShowWindow(hwnd, place.maximized ? SW_SHOWMAXIMIZED : SW_SHOWDEFAULT);
        UpdateWindow(hwnd);
    } else {
        g.hidden = true;   // starts in the tray; the icon and the hotkey open it
        LOG_INFO("GUI", "Manager window starts hidden (Settings > Manager window)");
    }

    // Addon icons need the window's D3D11 device.
    IconLoader_SetDevice(g.device.Device());
    if (g.manager) g.manager->LoadAddonIcons();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    if (g.manager) g.manager->InitializeAddons(ImGui::GetCurrentContext());   // addons share this context

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;   // layout is ours; do not litter the Lossless Scaling folder with imgui.ini

    // Fonts can be overridden in the addon-manager config: "ui": { "font": "...ttf", "mono_font": "...ttf" }
    LoadUiFonts(cfg.GlobalGetOr<std::string>("ui", "font", ""), cfg.GlobalGetOr<std::string>("ui", "mono_font", ""));
    ApplyUiScale(TotalScale());

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g.device.Device(), g.device.Context());

    const float clearColor[4] = { 0.094f, 0.094f, 0.094f, 1.0f };
    LOG_INFO("GUI", "Addon Manager window ready");

    for (bool done = false; !done;) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;

        // Present does not wait for vsync while minimised, so block for the next message instead of spinning.
        if (g.minimized || g.hidden) { WaitMessage(); continue; }

        if (g.scaleDirty.exchange(false)) ApplyUiScale(TotalScale());   // between frames: the style is not in use
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        window::RenderMainFrame(g.manager, g.bringAddonsForward);
        widgets::ToastRender();
        ImGui::Render();

        ID3D11RenderTargetView* target = g.device.BackBuffer();
        g.device.Context()->OMSetRenderTargets(1, &target, nullptr);
        g.device.Context()->ClearRenderTargetView(target, clearColor);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        const HRESULT presentHr = g.device.SwapChain()->Present(1, 0);

        // This window sits next to a running game. Redrawing it at the display rate while nobody is looking wastes GPU and CPU the game
        // wants (and, when the window is covered, Present returns at once and the loop would spin). Drop to ~10 fps unless it has focus.
        const bool occluded = (presentHr == DXGI_STATUS_OCCLUDED);
        if (occluded || GetForegroundWindow() != hwnd) MsgWaitForMultipleObjects(0, nullptr, FALSE, occluded ? 250 : 100, QS_ALLINPUT);
    }

    GpuStats::Instance().Shutdown();
    PersistPlacement();
    window::hotkey::Remove(hwnd);
    window::tray::Remove();

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    g.device.Destroy();
    DestroyWindow(hwnd);
    UnregisterClassW(kWindowClass, wc.hInstance);
    return 0;
}

} // namespace lsproxy
