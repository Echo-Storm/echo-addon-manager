#pragma once
#include <windows.h>

namespace lsproxy {
namespace window {

// Dark title bar in the manager's colours. Windows 11 honours the caption, text and border colours; Windows 10 only the dark-mode flag.
// dwmapi is bound at run time, so nothing new is linked and older systems simply ignore it.
void ApplyDarkTitleBar(HWND hwnd);

struct AppIcons {
    HICON big = nullptr;      // taskbar, Alt+Tab
    HICON little = nullptr;   // title bar, tray (never null when `big` is set); not "small": Windows headers #define that
};

// The window, taskbar and tray icons: LP-icon.ico (a multi-size icon, so each size is a proper frame) beside the program or beside this
// DLL, otherwise LP-icon.png in the same two places. Both empty when none is found.
AppIcons LoadAppIcons();

} // namespace window
} // namespace lsproxy
