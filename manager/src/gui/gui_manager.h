#pragma once
#include <windows.h>

namespace lsproxy {

class AddonManager;

class GuiManager {
public:
    static void StartGuiThread(AddonManager* manager);

    // The manager window is hidden, not destroyed, when it is closed: it lives in the notification area (tray) and
    // comes back with a click on its icon or a hotkey (Ctrl+Shift + an F-key, F12 by default).
    static void ToggleWindow();
    static bool WindowVisible();
    static void ApplyHotkey();               // (re)registers the hotkey from the config; call after it changes
    static const char* HotkeyStatus();       // "registered", "off" or why it could not be registered
    static void RequestUserScale();          // the interface-size setting changed: re-apply the scale at the start of the next frame

private:
    static DWORD WINAPI GuiThread(LPVOID lpParam);
};

} // namespace lsproxy
