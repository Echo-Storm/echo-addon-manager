// Docking: the manager's window sits against one side of Lossless Scaling's window, as tall as it, and goes where it goes: moved, resized,
// minimised and restored together, and brought forward together. Settings > Interface chooses the side ("ui.dock": "off", "left" or
// "right"). Dragging the manager away by hand undocks it; resizing it keeps it docked and changes only its width.
//
// Both windows are in this process. Lossless Scaling's window is found by looking (it is created after the manager starts, and again when it
// is recreated), and followed with a WinEvent hook whose calls arrive on the manager's own thread.
#pragma once
#include <windows.h>

namespace eam::window::dock {

enum class Side { Off, Left, Right };

Side SideFromConfig();
void SetSide(Side side);   // saves it, and docks or undocks at once
const char* SideName(Side side);

// On the manager's window thread.
void Start(HWND manager);
void Stop();
void OnTimer();                                            // every second while docking is on and Lossless Scaling's window is not found yet
void OnManagerMessage(HWND manager, UINT msg, WPARAM wParam, LPARAM lParam);   // moving and sizing by hand
bool Docked();                                             // attached to Lossless Scaling's window right now
void Refresh();                                            // put the manager back beside it (after it was shown from the tray)
constexpr UINT_PTR kTimer = 0x4C53;

// Where the manager goes (its visible frame), given Lossless Scaling's visible frame, the manager's visible frame now, the side, and the work
// area of Lossless Scaling's monitor: flush against that side, as tall as Lossless Scaling's window, as wide as the manager is (but no wider
// than the work area allows). When there is no room on that side, the other side.
RECT Beside(const RECT& ls, const RECT& manager, Side side, const RECT& workArea);

} // namespace eam::window::dock
