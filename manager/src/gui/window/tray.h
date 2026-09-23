#pragma once
#include <windows.h>
#include <string>

namespace eam {
namespace window {
namespace tray {

constexpr UINT kMessage = WM_APP + 1;   // the callback message the tray icon sends to the window

// The tooltip: "<product>: click to open or close", plus the hotkey in brackets when there is one.
std::wstring TipText(const std::wstring& hotkeyLabel);

bool Add(HWND hwnd, HICON icon, const std::wstring& hotkeyLabel);   // false: the icon could not be added (the hotkey still works)
void Remove();
void Forget();                 // Explorer restarted and its tray forgot the icon: the next Add starts from nothing
bool Added();                  // is the icon in the tray right now?
void FailNextAddsForTest(int n);   // the next n calls of Add fail without asking Windows (the window test uses it to try the retry)
void SetTip(const std::wstring& hotkeyLabel);
void Balloon(const wchar_t* title, const wchar_t* text);

// The right-click menu. Runs its own modal loop and does what was picked: `toggle` is set when "open / hide the manager" was chosen.
void ShowMenu(HWND hwnd, bool managerHidden, bool& toggle);

} // namespace tray
} // namespace window
} // namespace eam
