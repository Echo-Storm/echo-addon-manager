#pragma once
#include <atomic>
#include <windows.h>

namespace lsproxy {
namespace features {
namespace reshade {

// True while input passthrough is on. The window procedure installed by ProcessWindow reads it, so it lives here.
extern std::atomic<bool> g_passthroughOn;

// Makes one of Lossless Scaling's own windows let the mouse and keyboard through to what is behind it: it is subclassed (once), and while
// passthrough is on its click-through and no-activate styles are taken off and an overlay is brought to the front.
void ProcessWindow(HWND hwnd);

// Puts every window back the way it was, including its window procedure.
void RestoreAll();

// True when a window still has our procedure installed. That happens when something else subclassed it after us, so ours could not be taken
// out; our procedure then stays in that window's chain and must not be unloaded.
bool StillHooked();

// Forgets windows that no longer exist.
void CleanupDeadWindows();

} // namespace reshade
} // namespace features
} // namespace lsproxy
