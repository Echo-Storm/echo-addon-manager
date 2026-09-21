#include "dpi.h"
#include "../../config/config_manager.h"

namespace lsproxy {
namespace window {

float DisplayScale(HWND hwnd) {
    // Looked up at run time: the per-window call needs Windows 10 1607, and nothing new is linked for it.
    using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
    using GetDpiForSystemFn = UINT(WINAPI*)();
    UINT dpi = 0;
    if (HMODULE user32 = GetModuleHandleW(L"user32.dll")) {
        const auto forWindow = reinterpret_cast<GetDpiForWindowFn>(GetProcAddress(user32, "GetDpiForWindow"));
        const auto forSystem = reinterpret_cast<GetDpiForSystemFn>(GetProcAddress(user32, "GetDpiForSystem"));
        if (forWindow && hwnd) dpi = forWindow(hwnd);
        if (!dpi && forSystem) dpi = forSystem();
    }
    return dpi ? dpi / 96.0f : 1.0f;
}

int ClampScalePercent(int percent) {
    if (percent < 75) return 75;
    if (percent > 200) return 200;
    return percent;
}

float UserScale() {
    return ClampScalePercent(ConfigManager::Instance().GlobalGetOr<int>("ui", "scale_percent", 100)) / 100.0f;
}

} // namespace window
} // namespace lsproxy
