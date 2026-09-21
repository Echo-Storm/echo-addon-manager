#pragma once
#include <windows.h>
#include <functional>
#include "../../../third_party/nlohmann/json.hpp"

namespace lsproxy {
namespace window {

// Sizes are "logical" pixels (96 dpi); the window is created at logical * the display scale.
constexpr int kDefaultLogicalW = 1100, kDefaultLogicalH = 720;
constexpr int kMinLogicalW = 760, kMinLogicalH = 480;

// Where and how big the window opens, in physical pixels.
struct Placement {
    int x = 100, y = 100;
    int w = 0, h = 0;
    bool maximized = false;
};

// What is kept in the settings file (ui.window): position in pixels, size in logical pixels, and whether it was maximised.
struct SavedPlacement {
    int x = 0, y = 0;
    int w = 0, h = 0;
    bool maximized = false;
    bool valid = false;
};

using OnScreenFn = std::function<bool(const RECT&)>;   // does this rectangle touch a monitor that exists now?

// The opening placement: the saved one when it is usable (both sizes at least the minimum), otherwise the default size at (100, 100).
// A saved position that is no longer on any monitor (a display was unplugged) falls back to (100, 100), keeping the size.
Placement InitialPlacement(const nlohmann::json& saved, float displayScale, const OnScreenFn& onScreen);

// Reads the placement of a window in logical pixels (dividing by `displayScale`, the monitor's dpi / 96 and nothing else: the user's
// interface-size setting scales what is inside the window, not the window).
SavedPlacement CapturePlacement(HWND hwnd, float displayScale);

nlohmann::json ToJson(const SavedPlacement& p);

} // namespace window
} // namespace lsproxy
