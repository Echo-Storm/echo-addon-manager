#pragma once
#include <windows.h>
#include <string>

namespace lsproxy {
namespace window {
namespace tray {

constexpr UINT kMessage = WM_APP + 1;   // the callback message the tray icon sends to the window

// The tooltip: "<product>: click to open or close", plus the hotkey in brackets when there is one.
std::wstring TipText(const std::wstring& hotkeyLabel);

bool Add(HWND hwnd, HICON icon, const std::wstring& hotkeyLabel);   // false: the icon could not be added (the hotkey still works)
void Remove();
void ReAdd(HWND hwnd, HICON icon, const std::wstring& hotkeyLabel);   // Explorer restarted and forgot the icon
void SetTip(const std::wstring& hotkeyLabel);
void Balloon(const wchar_t* title, const wchar_t* text);

// The right-click menu. Runs its own modal loop and does what was picked: `toggle` is set when "open / hide the manager" was chosen.
void ShowMenu(HWND hwnd, bool managerHidden, bool& toggle);

} // namespace tray
} // namespace window
} // namespace lsproxy
