#include "gui_manager.h"
#include "gui_style.h"
#include "gui_scale.h"
#include "icon_loader.h"
#include "../../third_party/stb_image.h"
#include "tabs/tab_addons.h"
#include "tabs/tab_settings.h"
#include "tabs/tab_logs.h"
#include "tabs/tab_about.h"
#include "widgets/toast.h"
#include "widgets/status_bar.h"
#include "tabs/tab_performance.h"
#include "../host/metrics.h"
#include "../host/gpu_stats.h"
#include "../addon/addon_manager.h"
#include "../host/host_impl.h"
#include "../log/logger.h"
#include "../../sdk/include/lsproxy/version.h"
#include "../config/config_manager.h"
#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include <d3d11.h>
#include <dxgi.h>
#include <shellapi.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")

// Forward declare
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace lsproxy {

// Dark title bar in the manager's colours (Windows 11 honours the caption/text/border colours; Windows 10 only the
// dark mode flag). dwmapi is bound at run time so nothing new is linked and older systems simply ignore it.
static void ApplyDarkTitleBar(HWND hwnd) {
    HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
    if (!dwm) return;
    using SetAttr = HRESULT (WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
    auto set = (SetAttr)GetProcAddress(dwm, "DwmSetWindowAttribute");
    if (set) {
        const BOOL dark = TRUE;
        set(hwnd, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof dark);
        const COLORREF caption = RGB(0x18, 0x18, 0x18), text = RGB(0xe8, 0xe8, 0xe8), border = RGB(0x2a, 0x2a, 0x2a);
        set(hwnd, 35 /*DWMWA_CAPTION_COLOR*/, &caption, sizeof caption);
        set(hwnd, 36 /*DWMWA_TEXT_COLOR*/, &text, sizeof text);
        set(hwnd, 34 /*DWMWA_BORDER_COLOR*/, &border, sizeof border);
    }
    FreeLibrary(dwm);
}

// Load a multi-size .ico at the size the caller wants (Windows picks the closest frame): 16 px for the tray, 32 for the taskbar.
static HICON LoadIcoSized(const std::wstring& path, int cx, int cy) {
    return (HICON)LoadImageW(nullptr, path.c_str(), IMAGE_ICON, cx, cy, LR_LOADFROMFILE);
}

// Load a PNG file as a Win32 HICON
static HICON LoadIconFromPng(const std::wstring& path) {
    // Convert wstring to UTF-8 for stb_image
    int sz = WideCharToMultiByte(CP_UTF8, 0, path.c_str(), (int)path.size(), nullptr, 0, nullptr, nullptr);
    std::string utf8(sz, 0);
    WideCharToMultiByte(CP_UTF8, 0, path.c_str(), (int)path.size(), &utf8[0], sz, nullptr, nullptr);

    int w, h, channels;
    unsigned char* rgba = stbi_load(utf8.c_str(), &w, &h, &channels, 4);
    if (!rgba) return nullptr;

    // Convert RGBA to BGRA (Windows HICON format)
    for (int i = 0; i < w * h; i++) {
        unsigned char tmp = rgba[i * 4 + 0];
        rgba[i * 4 + 0] = rgba[i * 4 + 2];
        rgba[i * 4 + 2] = tmp;
    }

    HBITMAP hColor = CreateBitmap(w, h, 1, 32, rgba);
    stbi_image_free(rgba);
    if (!hColor) return nullptr;

    HDC screenDc = GetDC(NULL);
    HBITMAP hMask = CreateCompatibleBitmap(screenDc, w, h);
    ReleaseDC(NULL, screenDc);
    if (!hMask) { DeleteObject(hColor); return nullptr; }

    ICONINFO ii = {};
    ii.fIcon = TRUE;
    ii.hbmMask = hMask;
    ii.hbmColor = hColor;
    HICON icon = CreateIconIndirect(&ii);

    DeleteObject(hColor);
    DeleteObject(hMask);
    return icon;
}

static float g_dpiScale = 1.0f;                 // what Windows asks for (monitor DPI / 96)
static std::atomic<bool> g_scaleDirty{ false };  // the interface-size setting changed: apply it before the next frame
static float UserScaleFromConfig() {
    int pct = ConfigManager::Instance().GlobalGetOr<int>("ui", "scale_percent", 100);
    if (pct < 75) pct = 75; if (pct > 200) pct = 200;
    return pct / 100.0f;
}
void GuiManager::RequestUserScale() { g_scaleDirty = true; }
static bool g_selectAddonsTab = false;   // set by a drop: bring the Addons tab forward so the confirmation is visible

// D3D11 state
static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;
static AddonManager* g_manager = nullptr;
static bool g_minimized = false;
static bool g_hidden = false;           // closed to the tray: the window exists but is not shown, and nothing is drawn
static HWND g_hwnd = nullptr;
static HICON g_trayIcon = nullptr;
static NOTIFYICONDATAW g_nid = {};
static bool g_trayAdded = false;
static UINT g_wmTaskbarCreated = 0;     // broadcast when Explorer restarts and the tray icons have to be added again
static bool g_hotkeyOk = false;
static char g_hotkeyStatus[96] = "off";
static constexpr UINT WM_TRAYICON = WM_APP + 1;
static constexpr int kHotkeyId = 0x4C50;

// Display scale of the monitor a window is on (1.0 = 96 DPI). Falls back to the system DPI.
static float QueryDpiScale(HWND hwnd) {
    UINT dpi = 0;
    if (HMODULE user32 = GetModuleHandleW(L"user32.dll")) {
        typedef UINT(WINAPI* PFN_GetDpiForWindow)(HWND);
        typedef UINT(WINAPI* PFN_GetDpiForSystem)();
        auto forWindow = (PFN_GetDpiForWindow)GetProcAddress(user32, "GetDpiForWindow");
        auto forSystem = (PFN_GetDpiForSystem)GetProcAddress(user32, "GetDpiForSystem");
        if (forWindow && hwnd) dpi = forWindow(hwnd);
        if (!dpi && forSystem) dpi = forSystem();
    }
    return dpi ? dpi / 96.0f : 1.0f;
}

// Window placement captured on WM_CLOSE (size in logical pixels so it survives a DPI change)
// and written to the config once the GUI thread winds down.
struct SavedWindow { int x = 0, y = 0, w = 0, h = 0; bool maximized = false; bool valid = false; };
static SavedWindow g_savedWindow;

static std::wstring HotkeyText() {
    const int vk = ConfigManager::Instance().GlobalGetOr<int>("ui", "hotkey_vk", VK_F12);
    wchar_t b[32]; swprintf(b, 32, L"Ctrl+Shift+F%d", vk - VK_F1 + 1);
    return b;
}

static void UpdateTrayTip() {
    if (!g_trayAdded) return;
    std::wstring tip = LSPROXY_PRODUCT_NAME_W L": click to open or close";
    if (g_hotkeyOk) tip += L" (" + HotkeyText() + L")";
    wcsncpy_s(g_nid.szTip, tip.c_str(), _TRUNCATE);
    g_nid.uFlags = NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

static void AddTrayIcon() {
    if (!g_hwnd) return;
    g_nid = {};
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = g_hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = g_trayIcon ? g_trayIcon : LoadIconW(nullptr, IDI_APPLICATION);
    wcsncpy_s(g_nid.szTip, LSPROXY_PRODUCT_NAME_W L": click to open or close", _TRUNCATE);
    g_trayAdded = Shell_NotifyIconW(NIM_ADD, &g_nid) != FALSE;
    if (!g_trayAdded) LOG_WARN("GUI", "Could not add the tray icon; the manager can only be reopened with its hotkey");
    else LOG_INFO("GUI", "Tray icon added");
    UpdateTrayTip();
}

static void RemoveTrayIcon() {
    if (g_trayAdded) { Shell_NotifyIconW(NIM_DELETE, &g_nid); g_trayAdded = false; }
}

static void ShowManager(bool show) {
    if (!g_hwnd) return;
    if (show) {
        ShowWindow(g_hwnd, IsIconic(g_hwnd) ? SW_RESTORE : SW_SHOW);
        SetForegroundWindow(g_hwnd);
        g_hidden = false;
    } else {
        ShowWindow(g_hwnd, SW_HIDE);
        g_hidden = true;
    }
}

static void TrayBalloon(const wchar_t* title, const wchar_t* text) {
    if (!g_trayAdded) return;
    NOTIFYICONDATAW n = g_nid;
    n.uFlags = NIF_INFO;
    n.dwInfoFlags = NIIF_INFO | NIIF_NOSOUND;
    wcsncpy_s(n.szInfoTitle, title, _TRUNCATE);
    wcsncpy_s(n.szInfo, text, _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &n);
}

static void ShowTrayMenu() {
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    AppendMenuW(menu, MF_STRING, 1, g_hidden ? L"Open the addon manager" : L"Hide the addon manager");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 2, L"Open the addons folder");
    AppendMenuW(menu, MF_STRING, 3, L"Open the log file");
    AppendMenuW(menu, MF_STRING, 4, L"Open the logs folder");
    POINT pt; GetCursorPos(&pt);
    SetForegroundWindow(g_hwnd);   // without this the menu does not close when the user clicks elsewhere
    const int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_hwnd, nullptr);
    PostMessageW(g_hwnd, WM_NULL, 0, 0);
    DestroyMenu(menu);

    wchar_t exe[MAX_PATH]; GetModuleFileNameW(nullptr, exe, MAX_PATH);
    const std::wstring dir = std::filesystem::path(exe).parent_path().wstring();
    if (cmd == 1) ShowManager(g_hidden);
    else if (cmd == 2) ShellExecuteW(nullptr, L"open", (dir + L"\\addons").c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    else if (cmd == 3) ShellExecuteW(nullptr, L"open", (dir + L"\\logs\\" + std::wstring(LSPROXY_PRODUCT_LOGFILE_W)).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    else if (cmd == 4) ShellExecuteW(nullptr, L"open", (dir + L"\\logs").c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

// Writes the window placement captured on close (and at shutdown) to the config.
static void PersistWindow() {
    if (!g_savedWindow.valid) return;
    auto& cfg = ConfigManager::Instance();
    cfg.GlobalSet("ui", "window", nlohmann::json{
        {"x", g_savedWindow.x}, {"y", g_savedWindow.y},
        {"w", g_savedWindow.w}, {"h", g_savedWindow.h},
        {"maximized", g_savedWindow.maximized}});
    cfg.Save();
}

void GuiManager::ApplyHotkey() {
    if (!g_hwnd) return;
    UnregisterHotKey(g_hwnd, kHotkeyId);
    g_hotkeyOk = false;
    auto& cfg = ConfigManager::Instance();
    if (!cfg.GlobalGetOr<bool>("ui", "hotkey_enabled", true)) {
        snprintf(g_hotkeyStatus, sizeof(g_hotkeyStatus), "off");
    } else {
        int vk = cfg.GlobalGetOr<int>("ui", "hotkey_vk", VK_F12);
        if (vk < VK_F1 || vk > VK_F12) vk = VK_F12;
        if (RegisterHotKey(g_hwnd, kHotkeyId, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, (UINT)vk)) {
            g_hotkeyOk = true;
            snprintf(g_hotkeyStatus, sizeof(g_hotkeyStatus), "registered");
        } else {
            snprintf(g_hotkeyStatus, sizeof(g_hotkeyStatus), "could not register: another program already uses that combination");
            LOG_WARN("GUI", "Manager hotkey Ctrl+Shift+F%d could not be registered (error %lu)", vk - VK_F1 + 1, GetLastError());
        }
    }
    UpdateTrayTip();
}
const char* GuiManager::HotkeyStatus() { return g_hotkeyStatus; }
void GuiManager::ToggleWindow() { ShowManager(g_hidden); }
bool GuiManager::WindowVisible() { return !g_hidden; }

static void CreateRenderTarget() {
    ID3D11Texture2D* pBackBuffer;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, NULL, &g_mainRenderTargetView);
    pBackBuffer->Release();
}

static void CleanupRenderTarget() {
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

static void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

static bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };

    // Load system DLLs to bypass ReShade
    wchar_t systemPath[MAX_PATH];
    if (!GetSystemDirectoryW(systemPath, MAX_PATH)) return false;
    std::wstring sysDir = systemPath;

    HMODULE hDXGI = LoadLibraryW((sysDir + L"\\dxgi.dll").c_str());
    if (!hDXGI) return false;

    typedef HRESULT(WINAPI* CreateDXGIFactory1Func)(REFIID, void**);
    auto createFactoryFunc = (CreateDXGIFactory1Func)GetProcAddress(hDXGI, "CreateDXGIFactory1");
    if (!createFactoryFunc) return false;

    IDXGIFactory1* pFactory = nullptr;
    if (FAILED(createFactoryFunc(__uuidof(IDXGIFactory1), (void**)&pFactory))) return false;

    HMODULE hD3D11 = LoadLibraryW((sysDir + L"\\d3d11.dll").c_str());
    if (!hD3D11) { pFactory->Release(); return false; }

    typedef HRESULT(WINAPI* D3D11CreateDeviceFunc)(
        IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL*,
        UINT, UINT, ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);
    auto createDeviceFunc = (D3D11CreateDeviceFunc)GetProcAddress(hD3D11, "D3D11CreateDevice");
    if (!createDeviceFunc) { pFactory->Release(); return false; }

    if (FAILED(createDeviceFunc(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, createDeviceFlags,
                                featureLevelArray, 2, D3D11_SDK_VERSION,
                                &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext))) {
        pFactory->Release();
        return false;
    }

    if (FAILED(pFactory->CreateSwapChain(g_pd3dDevice, &sd, &g_pSwapChain))) {
        // Null the globals: the caller's CleanupDeviceD3D() releases whatever is still set.
        g_pd3dDevice->Release(); g_pd3dDevice = nullptr;
        g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr;
        pFactory->Release();
        return false;
    }

    pFactory->Release();
    CreateRenderTarget();
    return true;
}

static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    if (g_wmTaskbarCreated && msg == g_wmTaskbarCreated) {   // Explorer restarted: its tray forgot our icon
        g_trayAdded = false;
        AddTrayIcon();
        return 0;
    }

    switch (msg) {
    case WM_SIZE:
        g_minimized = (wParam == SIZE_MINIMIZED);
        if (g_pd3dDevice != nullptr && !g_minimized) {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
        }
        return 0;
    case WM_DROPFILES: {   // an addon (folder, .zip or .dll) dropped on the window: ask before installing it
        HDROP drop = (HDROP)wParam;
        wchar_t dropped[2048];
        if (DragQueryFileW(drop, 0, dropped, 2048)) { RequestInstallFromPath(dropped); g_selectAddonsTab = true; }
        DragFinish(drop);
        return 0;
    }
    case WM_GETMINMAXINFO: {
        MINMAXINFO* mmi = (MINMAXINFO*)lParam;
        mmi->ptMinTrackSize.x = (LONG)S(760.0f);
        mmi->ptMinTrackSize.y = (LONG)S(480.0f);
        return 0;
    }
    case WM_DPICHANGED: {
        // Moved to a monitor with a different scale: rebuild the style at the new scale (fonts
        // are rasterised at the final size, so no atlas rebuild is needed) and adopt the
        // rectangle Windows suggests so the window keeps its apparent size.
        g_dpiScale = HIWORD(wParam) / 96.0f;
        ApplyUiScale(g_dpiScale * UserScaleFromConfig());
        const RECT* r = (const RECT*)lParam;
        SetWindowPos(hWnd, NULL, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_CLOSE: {
        WINDOWPLACEMENT wp = { sizeof(wp) };
        if (GetWindowPlacement(hWnd, &wp)) {
            const float s = UiScale();
            g_savedWindow.x = wp.rcNormalPosition.left;
            g_savedWindow.y = wp.rcNormalPosition.top;
            g_savedWindow.w = (int)((wp.rcNormalPosition.right - wp.rcNormalPosition.left) / s + 0.5f);
            g_savedWindow.h = (int)((wp.rcNormalPosition.bottom - wp.rcNormalPosition.top) / s + 0.5f);
            g_savedWindow.maximized = (wp.showCmd == SW_SHOWMAXIMIZED);
            g_savedWindow.valid = true;
        }
        // Closing hides the window instead of ending the GUI thread: the addons keep their ImGui context, and the
        // manager can be reopened from the tray icon or the hotkey.
        PersistWindow();
        ShowManager(false);
        auto& cfg = ConfigManager::Instance();
        if (!cfg.GlobalGetOr<bool>("ui", "hide_hint_shown", false)) {
            std::wstring text = L"Click its icon in the notification area";
            if (g_hotkeyOk) text += L" or press " + HotkeyText();
            text += L" to open it again.";
            TrayBalloon(L"The addon manager is still running", text.c_str());
            cfg.GlobalSet("ui", "hide_hint_shown", true);
            cfg.Save();
        }
        return 0;
    }
    case WM_TRAYICON:
        if (LOWORD(lParam) == WM_LBUTTONUP) ShowManager(g_hidden);
        else if (LOWORD(lParam) == WM_RBUTTONUP) ShowTrayMenu();
        return 0;
    case WM_HOTKEY:
        if ((int)wParam == kHotkeyId) ShowManager(g_hidden);
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) return 0;
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

void GuiManager::StartGuiThread(AddonManager* manager) {
    g_manager = manager;
    if (HANDLE thread = CreateThread(NULL, 0, GuiThread, NULL, 0, NULL)) CloseHandle(thread);
}

DWORD WINAPI GuiManager::GuiThread(LPVOID lpParam) {
    // Load addons in GUI thread to avoid loader lock
    // Settings > "Auto-load addons on startup": when off, addons wait for "Load now" in the manager.
    if (g_manager) {
        if (ConfigManager::Instance().GlobalGetOr<bool>(nullptr, "auto_load", true))
            g_manager->LoadAddons();
        else
            LOG_INFO("GUI", "Auto-load is off; addons stay unloaded until loaded from the manager");
    }

    // Pairs with the DPI-awareness call in Core::Init. The window is created at the system scale
    // and corrected below once we know which monitor it actually landed on.
    const float sysScale = QueryDpiScale(nullptr);
    int winX = 100, winY = 100;
    int winW = (int)(1100 * sysScale), winH = (int)(720 * sysScale);
    bool startMaximized = false;
    {
        const nlohmann::json w = ConfigManager::Instance().GlobalGet("ui", "window");
        if (w.is_object()) {
            const int lw = w.value("w", 0), lh = w.value("h", 0);
            if (lw >= 760 && lh >= 480) {
                winW = (int)(lw * sysScale);
                winH = (int)(lh * sysScale);
                winX = w.value("x", winX);
                winY = w.value("y", winY);
                startMaximized = w.value("maximized", false);
            }
        }
    }
    {   // Keep the saved position on a monitor that still exists.
        RECT want = { winX, winY, winX + winW, winY + winH };
        if (!MonitorFromRect(&want, MONITOR_DEFAULTTONULL)) { winX = 100; winY = 100; }
    }

    // The window / taskbar / tray icon: LP-icon.ico (several sizes) next to the exe or the proxy DLL, else LP-icon.png
    HICON hAppIcon = nullptr, hSmallIcon = nullptr;
    {
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(NULL, exePath, MAX_PATH);
        std::wstring iconPath = std::filesystem::path(exePath).parent_path() / L"addons" / L".." / L"LP-icon.png";
        // Normalize: try next to the exe first, then next to the proxy DLL
        std::wstring iconPath1 = (std::filesystem::path(exePath).parent_path() / L"LP-icon.png").wstring();
        HMODULE hSelf = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCWSTR)&GuiManager::GuiThread, &hSelf);
        wchar_t dllPath[MAX_PATH];
        GetModuleFileNameW(hSelf, dllPath, MAX_PATH);
        std::wstring iconPath2 = (std::filesystem::path(dllPath).parent_path() / L"LP-icon.png").wstring();

        const std::wstring icoExePath = (std::filesystem::path(exePath).parent_path() / L"LP-icon.ico").wstring();
        const std::wstring icoDllPath = (std::filesystem::path(dllPath).parent_path() / L"LP-icon.ico").wstring();
        const std::wstring* icoPath = std::filesystem::exists(icoExePath) ? &icoExePath : (std::filesystem::exists(icoDllPath) ? &icoDllPath : nullptr);
        if (icoPath) {
            hAppIcon = LoadIcoSized(*icoPath, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON));
            hSmallIcon = LoadIcoSized(*icoPath, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON));
        }
        if (!hAppIcon) {
            if (std::filesystem::exists(iconPath1)) hAppIcon = LoadIconFromPng(iconPath1);
            else if (std::filesystem::exists(iconPath2)) hAppIcon = LoadIconFromPng(iconPath2);
        }
        if (!hSmallIcon) hSmallIcon = hAppIcon;
    }

    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L,
                       GetModuleHandle(NULL), hAppIcon, NULL, NULL, NULL,
                       L"EchoAddonManagerClass", hSmallIcon };
    RegisterClassExW(&wc);
    std::wstring title = LSPROXY_PRODUCT_NAME_W L" v";
    for (const char* c = LSPROXY_VERSION_STRING; *c; ++c) title += (wchar_t)*c;
    HWND hwnd = CreateWindowW(wc.lpszClassName, title.c_str(),
                              WS_OVERLAPPEDWINDOW, winX, winY, winW, winH,
                              NULL, NULL, wc.hInstance, NULL);

    ApplyDarkTitleBar(hwnd);

    const float dpiScale = QueryDpiScale(hwnd);
    if (std::fabs(dpiScale - sysScale) > 0.01f) {
        SetWindowPos(hwnd, NULL, winX, winY, (int)(winW * dpiScale / sysScale),
                     (int)(winH * dpiScale / sysScale), SWP_NOZORDER | SWP_NOACTIVATE);
    }

    // Set icon on window (in case wc didn't apply it)
    if (hAppIcon) {
        SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hAppIcon);
        SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hSmallIcon);
    }

    if (!CreateDeviceD3D(hwnd)) {
        LOG_ERROR("GUI", "Could not create the manager window's D3D11 device; the manager UI is unavailable");
        CleanupDeviceD3D();
        DestroyWindow(hwnd);
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    g_hwnd = hwnd;
    DragAcceptFiles(hwnd, TRUE);   // drop an addon on the window to install it
    g_trayIcon = hSmallIcon;
    g_wmTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    AddTrayIcon();
    GuiManager::ApplyHotkey();
    if (ConfigManager::Instance().GlobalGetOr<bool>("ui", "open_on_start", true)) {
        ShowWindow(hwnd, startMaximized ? SW_SHOWMAXIMIZED : SW_SHOWDEFAULT);
        UpdateWindow(hwnd);
    } else {
        g_hidden = true;   // starts in the tray; the icon and the hotkey open it
        LOG_INFO("GUI", "Manager window starts hidden (Settings > Manager window)");
    }

    // Set up icon loader with D3D11 device and load addon icons
    IconLoader_SetDevice(g_pd3dDevice);
    if (g_manager) {
        g_manager->LoadAddonIcons();
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    // Initialize addons with ImGui context
    if (g_manager) {
        g_manager->InitializeAddons(ImGui::GetCurrentContext());
    }

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr; // layout is ours; do not litter the Lossless Scaling folder with imgui.ini

    {   // Fonts can be overridden in the addon-manager config: "ui": { "font": "...ttf", "mono_font": "...ttf" }
        auto& cfg = ConfigManager::Instance();
        LoadUiFonts(cfg.GlobalGetOr<std::string>("ui", "font", ""),
                    cfg.GlobalGetOr<std::string>("ui", "mono_font", ""));
    }
    g_dpiScale = dpiScale;
    ApplyUiScale(g_dpiScale * UserScaleFromConfig());

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    ImVec4 clearColor = ImVec4(0.094f, 0.094f, 0.094f, 1.0f);
    bool done = false;

    LOG_INFO("GUI", "Addon Manager window ready");

    while (!done) {
        MSG msg;
        while (PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;

        // Present doesn't wait for vsync while minimized, so block for the next message instead of spinning.
        if (g_minimized || g_hidden) {
            WaitMessage();
            continue;
        }

        if (g_scaleDirty.exchange(false)) ApplyUiScale(g_dpiScale * UserScaleFromConfig());   // between frames: the style is not in use
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // Fullscreen main window
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("##Main", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);

        // Content (everything but the status bar along the bottom)
        ImGui::BeginChild("##content", ImVec2(0, -widgets::StatusBarHeight() - 2.0f), false, ImGuiWindowFlags_NoBackground);

        // Tab bar
        if (ImGui::BeginTabBar("##MainTabs", ImGuiTabBarFlags_None)) {
            if (ImGui::BeginTabItem("Addons", nullptr, g_selectAddonsTab ? ImGuiTabItemFlags_SetSelected : 0)) {
                g_selectAddonsTab = false;
                ImGui::Dummy(ImVec2(0, 5));
                RenderTabAddons(g_manager);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Performance")) {
                RenderTabPerformance();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Settings")) {
                RenderTabSettings(g_manager);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Logs")) {
                ImGui::Dummy(ImVec2(0, 5));
                RenderTabLogs();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("About")) {
                RenderTabAbout();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::EndChild();

        {   // status bar: version and how many addons are on, Ko-fi at the right
            int total = 0, on = 0;
            if (g_manager) for (const auto& ad : g_manager->GetAddons()) { ++total; if (ad.enabled) ++on; }
            char left[128];
            snprintf(left, sizeof left, "%s %s   |   %d addon%s, %d on", LSPROXY_PRODUCT_NAME, LSPROXY_VERSION_STRING, total, total == 1 ? "" : "s", on);
            std::string leftText = left;
            const Metrics::Status live = Metrics::Instance().BestStatus();   // the most relevant live status of any addon
            if (!live.text.empty()) {
                std::string who = live.addon;
                if (g_manager) for (const auto& ad : g_manager->GetAddons()) if (ad.id == live.addon) who = ad.GetDisplayName();
                leftText += "   |   " + who + ": " + live.text;
            }
            widgets::StatusBar(leftText);
        }

        ImGui::End();

        // Toast overlay
        widgets::ToastRender();

        // Render
        ImGui::Render();
        const float cc[4] = { clearColor.x, clearColor.y, clearColor.z, clearColor.w };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, NULL);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, cc);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        const HRESULT presentHr = g_pSwapChain->Present(1, 0);

        // This window sits next to a running game. Redrawing it at the display rate while nobody is
        // looking wastes GPU and CPU the game wants (and, when the window is covered, Present returns
        // at once and the loop would spin). Drop to ~10 fps unless it has focus.
        const bool occluded = (presentHr == DXGI_STATUS_OCCLUDED);
        if (occluded || GetForegroundWindow() != hwnd)
            MsgWaitForMultipleObjects(0, nullptr, FALSE, occluded ? 250 : 100, QS_ALLINPUT);
    }

    GpuStats::Instance().Shutdown();
    PersistWindow();
    UnregisterHotKey(hwnd, kHotkeyId);
    RemoveTrayIcon();

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDeviceD3D();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);

    return 0;
}

} // namespace lsproxy
