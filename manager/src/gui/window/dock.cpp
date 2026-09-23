#include "dock.h"
#include "../widgets/toast.h"
#include "../../config/config_manager.h"
#include "../../log/logger.h"
#include <algorithm>
#include <cwchar>
#include <string>

namespace eam::window::dock {

namespace {

HWND g_manager = nullptr;
HWND g_ls = nullptr;                   // Lossless Scaling's window, once found
Side g_side = Side::Off;
HWINEVENTHOOK g_hooks[4] = {};
bool g_draggedByHand = false;          // the manager was moved (not only resized) since the person took hold of its frame
bool g_minimisedWithLs = false;        // the manager went down because Lossless Scaling's window did

// The frame as seen: Windows 10 and 11 give a window invisible resize borders, which GetWindowRect includes.
RECT VisibleFrame(HWND w) {
    RECT r{};
    using GetAttributeFn = HRESULT(WINAPI*)(HWND, DWORD, PVOID, DWORD);
    static const auto getAttribute = reinterpret_cast<GetAttributeFn>(GetProcAddress(LoadLibraryW(L"dwmapi.dll"), "DwmGetWindowAttribute"));
    constexpr DWORD kExtendedFrameBounds = 9;   // DWMWA_EXTENDED_FRAME_BOUNDS
    if (!getAttribute || FAILED(getAttribute(w, kExtendedFrameBounds, &r, sizeof r))) GetWindowRect(w, &r);
    return r;
}

// Lossless Scaling's own window: a visible, top-level, un-owned window of this process whose title starts with "Lossless Scaling", and not
// the manager, a tool window or the click-through overlay it scales into.
BOOL CALLBACK Consider(HWND w, LPARAM found) {
    DWORD pid = 0;
    GetWindowThreadProcessId(w, &pid);
    if (pid != GetCurrentProcessId() || w == g_manager || !IsWindowVisible(w) || GetWindow(w, GW_OWNER)) return TRUE;
    const LONG_PTR ex = GetWindowLongPtrW(w, GWL_EXSTYLE);
    if (ex & (WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE)) return TRUE;
    wchar_t title[64] = {};
    GetWindowTextW(w, title, 64);
    if (wcsncmp(title, L"Lossless Scaling", 16) != 0) return TRUE;
    *reinterpret_cast<HWND*>(found) = w;
    return FALSE;
}
HWND FindLossless() {
    HWND found = nullptr;
    EnumWindows(Consider, reinterpret_cast<LPARAM>(&found));
    return found;
}

// Every rectangle here in physical pixels: DWM's frame bounds always are, and the rest (window rectangles, the work area, SetWindowPos) are
// only while the calling thread is per-monitor DPI aware. The process may not be (Lossless Scaling's own manifest decides), and at any display
// scaling but 100% the two would not match.
struct PhysicalPixels {
    using SetContextFn = DPI_AWARENESS_CONTEXT(WINAPI*)(DPI_AWARENESS_CONTEXT);
    SetContextFn set = reinterpret_cast<SetContextFn>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetThreadDpiAwarenessContext"));
    DPI_AWARENESS_CONTEXT before = set ? set(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) : nullptr;
    ~PhysicalPixels() { if (set && before) set(before); }
};

void Place() {
    if (!g_manager || !g_ls || g_side == Side::Off || !IsWindowVisible(g_manager) || IsIconic(g_ls) || IsIconic(g_manager)) return;
    const PhysicalPixels physical;
    if (IsZoomed(g_manager)) ShowWindow(g_manager, SW_RESTORE);
    MONITORINFO monitor{ sizeof monitor };
    GetMonitorInfoW(MonitorFromWindow(g_ls, MONITOR_DEFAULTTONEAREST), &monitor);
    const RECT want = Beside(VisibleFrame(g_ls), VisibleFrame(g_manager), g_side, monitor.rcWork);
    RECT window{}, frame = VisibleFrame(g_manager);
    GetWindowRect(g_manager, &window);
    const RECT border{ frame.left - window.left, frame.top - window.top, window.right - frame.right, window.bottom - frame.bottom };
    const BOOL moved = SetWindowPos(g_manager, nullptr, want.left - border.left, want.top - border.top, (want.right - want.left) + border.left + border.right,
                 (want.bottom - want.top) + border.top + border.bottom, SWP_NOZORDER | SWP_NOACTIVATE);
    const RECT ls = VisibleFrame(g_ls), after = VisibleFrame(g_manager);
    LOG_DEBUG("Dock", "ls %ld,%ld-%ld,%ld work %ld,%ld-%ld,%ld want %ld,%ld-%ld,%ld border %ld,%ld,%ld,%ld moved %d -> %ld,%ld-%ld,%ld", ls.left, ls.top, ls.right, ls.bottom,
              monitor.rcWork.left, monitor.rcWork.top, monitor.rcWork.right, monitor.rcWork.bottom, want.left, want.top, want.right, want.bottom,
              border.left, border.top, border.right, border.bottom, moved, after.left, after.top, after.right, after.bottom);
}

void Attach(HWND ls) {
    g_ls = ls;
    LOG_INFO("Dock", "Docked %s of Lossless Scaling's window", g_side == Side::Left ? "left" : "right");
    Place();
}

void CALLBACK OnEvent(HWINEVENTHOOK, DWORD event, HWND w, LONG object, LONG child, DWORD, DWORD) {
    if (g_side == Side::Off || !g_ls || w != g_ls || object != OBJID_WINDOW || child != CHILDID_SELF) return;
    if (event != EVENT_OBJECT_LOCATIONCHANGE) LOG_DEBUG("Dock", "event 0x%04lx (manager %s)", event, IsIconic(g_manager) ? "minimised" : "shown");
    switch (event) {
    case EVENT_OBJECT_LOCATIONCHANGE:
        Place();
        break;
    case EVENT_SYSTEM_FOREGROUND:   // it came forward: the manager goes just behind it, so the two show together
        if (IsWindowVisible(g_manager)) SetWindowPos(g_manager, g_ls, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        break;
    case EVENT_SYSTEM_MINIMIZESTART:
    case EVENT_OBJECT_HIDE:         // minimised to the taskbar, or to the notification area
        if (IsWindowVisible(g_manager) && !IsIconic(g_manager)) { g_minimisedWithLs = true; ShowWindow(g_manager, SW_SHOWMINNOACTIVE); }
        break;
    case EVENT_SYSTEM_MINIMIZEEND:
    case EVENT_OBJECT_SHOW:
        if (g_minimisedWithLs) { g_minimisedWithLs = false; ShowWindow(g_manager, SW_SHOWNOACTIVATE); }
        Place();
        break;
    case EVENT_OBJECT_DESTROY:      // it will be looked for again
        g_ls = nullptr;
        break;
    }
}

} // namespace

const char* SideName(Side side) { return side == Side::Left ? "left" : side == Side::Right ? "right" : "off"; }

Side SideFromConfig() {
    const std::string s = ConfigManager::Instance().GlobalGetOr<std::string>("ui", "dock", "off");
    return s == "left" ? Side::Left : s == "right" ? Side::Right : Side::Off;
}

void SetSide(Side side) {
    auto& cfg = ConfigManager::Instance();
    cfg.GlobalSet("ui", "dock", SideName(side));
    cfg.Save();
    g_side = side;
    g_minimisedWithLs = false;
    if (side == Side::Off || !g_manager) return;
    if (g_ls && !IsWindow(g_ls)) g_ls = nullptr;
    if (!g_ls) { if (HWND ls = FindLossless()) Attach(ls); } else Place();
}

void Start(HWND manager) {
    g_manager = manager;
    g_side = SideFromConfig();
    // Out of context, so the calls arrive on this thread through its message loop; only this process's windows.
    const DWORD pid = GetCurrentProcessId();
    const DWORD ranges[4][2] = { { EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND }, { EVENT_SYSTEM_MINIMIZESTART, EVENT_SYSTEM_MINIMIZEEND },
                                 { EVENT_OBJECT_DESTROY, EVENT_OBJECT_HIDE }, { EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE } };
    for (int i = 0; i < 4; ++i) g_hooks[i] = SetWinEventHook(ranges[i][0], ranges[i][1], nullptr, OnEvent, pid, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNTHREAD);
    SetTimer(manager, kTimer, 1000, nullptr);
}

void Stop() {
    for (HWINEVENTHOOK& h : g_hooks) { if (h) UnhookWinEvent(h); h = nullptr; }
    if (g_manager) KillTimer(g_manager, kTimer);
    g_manager = nullptr; g_ls = nullptr;
}

void OnTimer() {
    if (g_side == Side::Off || !g_manager) return;
    if (g_ls && !IsWindow(g_ls)) g_ls = nullptr;
    if (!g_ls) if (HWND ls = FindLossless()) Attach(ls);
}

bool Docked() { return g_side != Side::Off && g_ls != nullptr; }
void Refresh() { if (Docked()) Place(); }

void OnManagerMessage(HWND, UINT msg, WPARAM, LPARAM) {
    switch (msg) {
    case WM_ENTERSIZEMOVE: g_draggedByHand = false; break;
    case WM_MOVING: g_draggedByHand = true; break;
    case WM_EXITSIZEMOVE:
        if (!Docked()) break;
        if (g_draggedByHand) {   // moved away by hand: that is undocking
            SetSide(Side::Off);
            widgets::ToastShow("Undocked from Lossless Scaling. Settings > Interface docks it again.", widgets::ToastType::Info, 5.0f);
        } else {
            Place();              // resized: the width is kept, the height and position follow Lossless Scaling's window again
        }
        break;
    }
}

RECT Beside(const RECT& ls, const RECT& manager, Side side, const RECT& work) {
    const LONG width = std::min<LONG>(manager.right - manager.left, work.right - work.left);
    const LONG roomLeft = ls.left - work.left, roomRight = work.right - ls.right;
    bool left = side == Side::Left;
    if (left && roomLeft < width && roomRight >= width) left = false;
    else if (!left && roomRight < width && roomLeft >= width) left = true;
    else if (roomLeft < width && roomRight < width) left = roomLeft > roomRight;   // no room on either side: the roomier one, overlapping it
    RECT r{};
    r.top = ls.top; r.bottom = ls.bottom;
    if (left) { r.right = ls.left; r.left = r.right - width; if (r.left < work.left) { r.left = work.left; r.right = r.left + width; } }
    else { r.left = ls.right; r.right = r.left + width; if (r.right > work.right) { r.right = work.right; r.left = r.right - width; } }
    return r;
}

} // namespace eam::window::dock
