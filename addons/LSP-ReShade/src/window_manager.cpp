#include "window_manager.hpp"
#include <map>
#include <mutex>
#include <vector>

struct WindowState {
    WNDPROC originalProc = nullptr;
    LONG_PTR originalStyle = 0;
    LONG_PTR originalExStyle = 0;
    bool stylesModified = false;
    bool activated = false;
};

static std::map<HWND, WindowState> s_windows;
static std::mutex s_mutex;

static LRESULT CALLBACK HookProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    // Force cursor visibility when passthrough is active
    if (g_inputPassthrough) {
        if (uMsg == WM_SETCURSOR || uMsg == WM_MOUSEMOVE) {
            SetCursor(LoadCursor(NULL, IDC_ARROW));
            ClipCursor(NULL);
            if (uMsg == WM_SETCURSOR) return TRUE;
        }
    }

    // Get original WndProc
    WNDPROC oldProc = nullptr;
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        auto it = s_windows.find(hwnd);
        if (it != s_windows.end()) oldProc = it->second.originalProc;
    }

    if (!oldProc) {
        oldProc = (WNDPROC)GetWindowLongPtr(hwnd, GWLP_WNDPROC);
        if (oldProc == HookProc) return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }

    LRESULT ret = CallWindowProc(oldProc, hwnd, uMsg, wParam, lParam);

    // Fix click-through: convert HTTRANSPARENT to HTCLIENT
    if (g_inputPassthrough && uMsg == WM_NCHITTEST && ret == HTTRANSPARENT) {
        return HTCLIENT;
    }

    return ret;
}

namespace WindowMgr {

void ProcessWindow(HWND hwnd) {
    // Subclass if not already done
    WNDPROC currentProc = (WNDPROC)GetWindowLongPtr(hwnd, GWLP_WNDPROC);
    if (currentProc != HookProc) {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_windows[hwnd].originalProc = currentProc;
        SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LONG_PTR)HookProc);
    }

    if (!g_inputPassthrough) return;

    LONG_PTR exStyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
    LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);

    bool isNew = false;
    bool isOverlay = false;
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        auto& state = s_windows[hwnd];
        if (!state.stylesModified) {
            state.originalExStyle = exStyle;
            state.originalStyle = style;
            state.stylesModified = true;
        }
        if (!state.activated) {
            isNew = true;
            state.activated = true;
        }
        isOverlay = (state.originalExStyle & (WS_EX_TRANSPARENT | WS_EX_LAYERED | WS_EX_NOACTIVATE)) != 0;
    }

    LONG_PTR newExStyle = exStyle & ~(WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_LAYERED);
    LONG_PTR newStyle = style & ~WS_DISABLED;
    bool changed = (newExStyle != exStyle || newStyle != style);

    if (changed) {
        SetWindowLongPtr(hwnd, GWL_EXSTYLE, newExStyle);
        SetWindowLongPtr(hwnd, GWL_STYLE, newStyle);
    }

    if (changed || isNew) {
        if (isOverlay) {
            SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED);

            DWORD foreThread = GetWindowThreadProcessId(GetForegroundWindow(), NULL);
            DWORD curThread = GetCurrentThreadId();
            if (foreThread != curThread) {
                AttachThreadInput(foreThread, curThread, TRUE);
                BringWindowToTop(hwnd);
                SetForegroundWindow(hwnd);
                AttachThreadInput(foreThread, curThread, FALSE);
            } else {
                SetForegroundWindow(hwnd);
            }
        } else if (changed) {
            SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED | SWP_NOZORDER);
        }
    }
}

bool StillHooked() {
    // after RestoreAll only the windows we could not restore are left in the table
    std::lock_guard<std::mutex> lock(s_mutex);
    for (const auto& kv : s_windows) if (IsWindow(kv.first)) return true;
    return false;
}

void RestoreAll() {
    std::vector<std::pair<HWND, WindowState>> snapshot;
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        snapshot.assign(s_windows.begin(), s_windows.end());
    }

    std::vector<HWND> restored;
    for (const auto& [hwnd, state] : snapshot) {
        if (!IsWindow(hwnd)) { restored.push_back(hwnd); continue; }

        if (state.stylesModified) {
            SetWindowLongPtr(hwnd, GWL_EXSTYLE, state.originalExStyle);
            SetWindowLongPtr(hwnd, GWL_STYLE, state.originalStyle);
            SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
        }

        if (GetWindowLongPtr(hwnd, GWLP_WNDPROC) == (LONG_PTR)HookProc) {
            SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LONG_PTR)state.originalProc);
            restored.push_back(hwnd);
        }
        // else: another subclass sits on top of ours. Our procedure stays in that chain, so the entry (with the original
        // procedure HookProc forwards to) is kept and the addon reports StillHooked() so its DLL is not unloaded.
    }

    std::lock_guard<std::mutex> lock(s_mutex);
    for (HWND h : restored) s_windows.erase(h);
}

void CleanupDeadWindows() {
    std::lock_guard<std::mutex> lock(s_mutex);
    for (auto it = s_windows.begin(); it != s_windows.end();) {
        if (!IsWindow(it->first)) it = s_windows.erase(it);
        else ++it;
    }
}

} // namespace WindowMgr
