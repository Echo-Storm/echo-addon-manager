#include "tray.h"
#include "../../log/logger.h"
#include "../../../sdk/include/lsproxy/version.h"
#include <shellapi.h>
#include <filesystem>

namespace lsproxy {
namespace window {
namespace tray {

namespace {
NOTIFYICONDATAW g_nid = {};
bool g_added = false;
int g_failNext = 0;

enum MenuId { kToggle = 1, kOpenAddons, kOpenLog, kOpenLogs };

void Open(const std::wstring& target) { ShellExecuteW(nullptr, L"open", target.c_str(), nullptr, nullptr, SW_SHOWNORMAL); }
}

std::wstring TipText(const std::wstring& hotkeyLabel) {
    std::wstring tip = LSPROXY_PRODUCT_NAME_W L": click to open or close";
    if (!hotkeyLabel.empty()) tip += L" (" + hotkeyLabel + L")";
    return tip;
}

void SetTip(const std::wstring& hotkeyLabel) {
    if (!g_added) return;
    wcsncpy_s(g_nid.szTip, TipText(hotkeyLabel).c_str(), _TRUNCATE);
    g_nid.uFlags = NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

bool Add(HWND hwnd, HICON icon, const std::wstring& hotkeyLabel) {
    if (!hwnd) return false;
    g_nid = {};
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_nid.uCallbackMessage = kMessage;
    g_nid.hIcon = icon ? icon : LoadIconW(nullptr, IDI_APPLICATION);
    wcsncpy_s(g_nid.szTip, TipText(hotkeyLabel).c_str(), _TRUNCATE);
    if (g_failNext > 0) {
        --g_failNext;
        g_added = false;
    } else {
        g_added = Shell_NotifyIconW(NIM_ADD, &g_nid) != FALSE;
        // Explorer can time out an add that it did carry out; the next add then fails because the icon is already there. A modify tells the two apart.
        if (!g_added) { g_nid.uFlags = NIF_TIP; g_added = Shell_NotifyIconW(NIM_MODIFY, &g_nid) != FALSE; g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP; }
    }
    if (g_added) LOG_INFO("GUI", "Tray icon added");
    else LOG_WARN("GUI", "Could not add the tray icon; the manager can only be reopened with its hotkey");
    return g_added;
}

void Remove() {
    if (g_added) Shell_NotifyIconW(NIM_DELETE, &g_nid);
    g_added = false;
}

void Forget() { g_added = false; }   // the taskbar is new: our old registration is gone with it

bool Added() { return g_added; }

void FailNextAddsForTest(int n) { g_failNext = n; }

void Balloon(const wchar_t* title, const wchar_t* text) {
    if (!g_added) return;
    NOTIFYICONDATAW n = g_nid;
    n.uFlags = NIF_INFO;
    n.dwInfoFlags = NIIF_INFO | NIIF_NOSOUND;
    wcsncpy_s(n.szInfoTitle, title, _TRUNCATE);
    wcsncpy_s(n.szInfo, text, _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &n);
}

void ShowMenu(HWND hwnd, bool managerHidden, bool& toggle) {
    toggle = false;
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    AppendMenuW(menu, MF_STRING, kToggle, managerHidden ? L"Open the addon manager" : L"Hide the addon manager");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kOpenAddons, L"Open the addons folder");
    AppendMenuW(menu, MF_STRING, kOpenLog, L"Open the log file");
    AppendMenuW(menu, MF_STRING, kOpenLogs, L"Open the logs folder");

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd);   // without this the menu does not close when the user clicks elsewhere
    const int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
    PostMessageW(hwnd, WM_NULL, 0, 0);
    DestroyMenu(menu);

    wchar_t exe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    const std::wstring dir = std::filesystem::path(exe).parent_path().wstring();
    switch (cmd) {
    case kToggle:     toggle = true; break;
    case kOpenAddons: Open(dir + L"\\addons"); break;
    case kOpenLog:    Open(dir + L"\\logs\\" + std::wstring(LSPROXY_PRODUCT_LOGFILE_W)); break;
    case kOpenLogs:   Open(dir + L"\\logs"); break;
    }
}

} // namespace tray
} // namespace window
} // namespace lsproxy
