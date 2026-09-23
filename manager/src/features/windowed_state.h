#pragma once
#include <mutex>
#include <windows.h>

namespace eam {
namespace features {
namespace windowed {

struct Settings {
    bool active = true;         // the virtual display and window moves are on (the feature's switch; changes while running)
    bool splitMode = false;
    int splitType = 0;          // which half of the game window Lossless Scaling covers: 0 left, 1 right, 2 top, 3 bottom
    bool positionMode = false;
    int positionSide = 1;       // where Lossless Scaling's window sits beside the game window: 0 left, 1 right, 2 top, 3 bottom
};

// Shared between the DXGI wrappers, the hooks and the watcher thread.
struct State {
    std::mutex mutex;           // guards the rectangles and the two window handles below
    Settings settings;

    RECT targetRect = { 0, 0, 1920, 1080 };   // the game window's client area, in screen coordinates
    RECT lsRect = { 0, 0, 1920, 1080 };       // where Lossless Scaling's own window belongs
    HWND targetWindow = nullptr;
    HWND overlay = nullptr;                   // Lossless Scaling's window, once found

    bool running = true;        // the watcher thread keeps going while this is true
};

inline State& GetState() {
    static State state;
    return state;
}

// Reads the foreground window (when it is not ours) into the state as the game window.
void UpdateTargetRect();

} // namespace windowed
} // namespace features
} // namespace eam
