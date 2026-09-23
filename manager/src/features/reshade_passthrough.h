#pragma once
#include <string>

namespace eam {
namespace features {
namespace reshade {

// ReShade input passthrough: while it is on, the mouse and keyboard reach a ReShade overlay (its menu) instead of being swallowed by
// Lossless Scaling's full-screen window. A hotkey turns it on and off; it turns itself off when the game or Lossless Scaling loses focus.

inline constexpr const char* kId = "ReShadePassthrough";   // the settings section and the id of the on/off switch
inline constexpr const char* kOldId = "LSP-ReShade";       // the same, up to 0.7.4 (and the folder of the standalone addon this once was)

void Start();                       // begin watching the hotkey (safe to switch on while Lossless Scaling runs)
void Stop();                        // stop, and put every window back as it was
void RenderOptions();               // the few settings, drawn in the manager's window
std::string Status();               // "Passthrough ON" while it is on, otherwise empty

void ForcePassthroughForTest(bool on);   // the offline test only: on without the hotkey or a focused game window

} // namespace reshade
} // namespace features
} // namespace eam
