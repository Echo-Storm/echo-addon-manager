#pragma once
#include <functional>
#include <windows.h>

namespace eam {

class AddonManager;

class GuiManager {
public:
    // Starts the window thread. `beforeAddons` runs on it first, before the addons load (work that must not run in DllMain).
    static void StartGuiThread(AddonManager* manager, std::function<void()> beforeAddons = {});

    // The manager window is hidden, not destroyed, when it is closed: it lives in the notification area (tray) and
    // comes back with a click on its icon or a hotkey (Ctrl+Shift + an F-key, F12 by default).
    static void ToggleWindow();
    static bool WindowVisible();
    static void ApplyHotkey();               // (re)registers the hotkey from the config; call after it changes
    static const char* HotkeyStatus();       // "registered", "off" or why it could not be registered
    static void FailNextFramesForTest(int n);   // the next n frames throw while drawing (the window test checks the window survives)
    static void RequestUserScale();          // the interface-size setting changed: re-apply the scale at the start of the next frame

private:
    static DWORD WINAPI GuiThread(LPVOID lpParam);
};

} // namespace eam
