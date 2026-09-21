#pragma once
#include <windows.h>
#include <string>

namespace lsproxy {
namespace window {
namespace hotkey {

constexpr int kId = 0x4C50;   // the id the window registers its hotkey under (WM_HOTKEY's wParam)

// The hotkey is Ctrl+Shift plus one of F1..F12 (ui.hotkey_vk, F12 by default; anything else falls back to F12).
int NormalizeVk(int vk);
std::wstring Label(int vk);           // "Ctrl+Shift+F12"
std::wstring LabelFromConfig();

// (Re)registers it from the settings on `hwnd` (ui.hotkey_enabled turns it off). Safe to call again after the settings change.
void Apply(HWND hwnd);
void Remove(HWND hwnd);

bool Registered();
const char* Status();                 // "registered", "off" or why it could not be registered

} // namespace hotkey
} // namespace window
} // namespace lsproxy
