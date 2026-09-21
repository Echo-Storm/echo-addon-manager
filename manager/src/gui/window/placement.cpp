#include "placement.h"

namespace lsproxy {
namespace window {

Placement InitialPlacement(const nlohmann::json& saved, float displayScale, const OnScreenFn& onScreen) {
    Placement p;
    p.w = static_cast<int>(kDefaultLogicalW * displayScale);
    p.h = static_cast<int>(kDefaultLogicalH * displayScale);

    if (saved.is_object()) {
        const int lw = saved.value("w", 0), lh = saved.value("h", 0);
        if (lw >= kMinLogicalW && lh >= kMinLogicalH) {
            p.w = static_cast<int>(lw * displayScale);
            p.h = static_cast<int>(lh * displayScale);
            p.x = saved.value("x", p.x);
            p.y = saved.value("y", p.y);
            p.maximized = saved.value("maximized", false);
        }
    }

    const RECT want = { p.x, p.y, p.x + p.w, p.y + p.h };
    if (onScreen && !onScreen(want)) { p.x = 100; p.y = 100; }
    return p;
}

SavedPlacement CapturePlacement(HWND hwnd, float displayScale) {
    SavedPlacement s;
    WINDOWPLACEMENT wp = { sizeof(wp) };
    if (!GetWindowPlacement(hwnd, &wp) || displayScale <= 0.0f) return s;
    const RECT& r = wp.rcNormalPosition;   // the restored rectangle, so a maximised window keeps the size it returns to
    s.x = r.left;
    s.y = r.top;
    s.w = static_cast<int>((r.right - r.left) / displayScale + 0.5f);
    s.h = static_cast<int>((r.bottom - r.top) / displayScale + 0.5f);
    s.maximized = wp.showCmd == SW_SHOWMAXIMIZED;
    s.valid = true;
    return s;
}

nlohmann::json ToJson(const SavedPlacement& p) {
    return nlohmann::json{ {"x", p.x}, {"y", p.y}, {"w", p.w}, {"h", p.h}, {"maximized", p.maximized} };
}

} // namespace window
} // namespace lsproxy
