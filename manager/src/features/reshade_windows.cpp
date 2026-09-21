#include "reshade_windows.h"
#include <map>
#include <mutex>
#include <vector>

namespace lsproxy {
namespace features {
namespace reshade {

std::atomic<bool> g_passthroughOn{false};

namespace {

// What a window looked like before passthrough touched it.
struct Saved {
    WNDPROC proc = nullptr;
    LONG_PTR style = 0;
    LONG_PTR exStyle = 0;
    bool stylesTaken = false;   // style and exStyle above are valid
    bool raised = false;        // this window has been brought forward once already
};

std::mutex g_mutex;
std::map<HWND, Saved> g_saved;

LRESULT CALLBACK PassthroughProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (g_passthroughOn && (msg == WM_SETCURSOR || msg == WM_MOUSEMOVE)) {   // keep the pointer visible and free
        SetCursor(LoadCursor(nullptr, IDC_ARROW));
        ClipCursor(nullptr);
        if (msg == WM_SETCURSOR) return TRUE;
    }

    WNDPROC next = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        const auto it = g_saved.find(hwnd);
        if (it != g_saved.end()) next = it->second.proc;
    }
    if (!next) {
        next = (WNDPROC)GetWindowLongPtr(hwnd, GWLP_WNDPROC);
        if (next == PassthroughProc) return DefWindowProc(hwnd, msg, wp, lp);   // not one of ours to forward to
    }

    const LRESULT result = CallWindowProc(next, hwnd, msg, wp, lp);
    if (g_passthroughOn && msg == WM_NCHITTEST && result == HTTRANSPARENT) return HTCLIENT;   // click-through becomes a real hit
    return result;
}

void BringForward(HWND hwnd) {
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED);
    const DWORD foreground = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
    const DWORD ours = GetCurrentThreadId();
    if (foreground != ours) {   // Windows only lets the thread that owns the foreground window hand it over
        AttachThreadInput(foreground, ours, TRUE);
        BringWindowToTop(hwnd);
        SetForegroundWindow(hwnd);
        AttachThreadInput(foreground, ours, FALSE);
    } else {
        SetForegroundWindow(hwnd);
    }
}

} // namespace

void ProcessWindow(HWND hwnd) {
    const WNDPROC current = (WNDPROC)GetWindowLongPtr(hwnd, GWLP_WNDPROC);
    if (current != PassthroughProc) {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_saved[hwnd].proc = current;
        SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LONG_PTR)PassthroughProc);
    }
    if (!g_passthroughOn) return;

    const LONG_PTR exStyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
    const LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);

    bool firstTime = false, wasOverlay = false;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        Saved& s = g_saved[hwnd];
        if (!s.stylesTaken) {
            s.exStyle = exStyle;
            s.style = style;
            s.stylesTaken = true;
        }
        if (!s.raised) {
            firstTime = true;
            s.raised = true;
        }
        wasOverlay = (s.exStyle & (WS_EX_TRANSPARENT | WS_EX_LAYERED | WS_EX_NOACTIVATE)) != 0;
    }

    const LONG_PTR wantEx = exStyle & ~(WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_LAYERED);
    const LONG_PTR wantStyle = style & ~WS_DISABLED;
    const bool changed = wantEx != exStyle || wantStyle != style;
    if (changed) {
        SetWindowLongPtr(hwnd, GWL_EXSTYLE, wantEx);
        SetWindowLongPtr(hwnd, GWL_STYLE, wantStyle);
    }
    if (!changed && !firstTime) return;

    if (wasOverlay) BringForward(hwnd);
    else if (changed) SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED | SWP_NOZORDER);
}

bool StillHooked() {
    std::lock_guard<std::mutex> lock(g_mutex);   // after RestoreAll, only windows that could not be restored remain in the table
    for (const auto& entry : g_saved)
        if (IsWindow(entry.first)) return true;
    return false;
}

void RestoreAll() {
    std::vector<std::pair<HWND, Saved>> all;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        all.assign(g_saved.begin(), g_saved.end());
    }

    std::vector<HWND> done;
    for (const auto& [hwnd, s] : all) {
        if (!IsWindow(hwnd)) { done.push_back(hwnd); continue; }
        if (s.stylesTaken) {
            SetWindowLongPtr(hwnd, GWL_EXSTYLE, s.exStyle);
            SetWindowLongPtr(hwnd, GWL_STYLE, s.style);
            SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
        }
        // If another subclass sits on top of ours, ours stays in that chain: keep the entry, with the procedure ours forwards to.
        if (GetWindowLongPtr(hwnd, GWLP_WNDPROC) == (LONG_PTR)PassthroughProc) {
            SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LONG_PTR)s.proc);
            done.push_back(hwnd);
        }
    }

    std::lock_guard<std::mutex> lock(g_mutex);
    for (HWND h : done) g_saved.erase(h);
}

void CleanupDeadWindows() {
    std::lock_guard<std::mutex> lock(g_mutex);
    for (auto it = g_saved.begin(); it != g_saved.end();) it = IsWindow(it->first) ? std::next(it) : g_saved.erase(it);
}

} // namespace reshade
} // namespace features
} // namespace lsproxy
