#pragma once
#include <windows.h>

namespace lsproxy {
namespace window {

// Display scale of the monitor a window is on (1.0 = 96 dpi); the system dpi when there is no window or the call is missing.
float DisplayScale(HWND hwnd);

// The interface-size setting (Settings > Interface size, ui.scale_percent) as a factor, kept between 75 % and 200 %.
int ClampScalePercent(int percent);
float UserScale();

} // namespace window
} // namespace lsproxy
