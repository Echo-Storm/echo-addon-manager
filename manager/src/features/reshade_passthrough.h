#pragma once
#include <string>

namespace lsproxy {
namespace features {
namespace reshade {

// ReShade input passthrough: while it is on, the mouse and keyboard reach a ReShade overlay (its menu) instead of being swallowed by
// Lossless Scaling's full-screen window. A hotkey turns it on and off; it turns itself off when the game or Lossless Scaling loses focus.

inline constexpr const char* kId = "LSP-ReShade";   // the settings section, and the id the on/off switch has always had

void Start();                       // begin watching the hotkey (safe to switch on while Lossless Scaling runs)
void Stop();                        // stop, and put every window back as it was
void RenderOptions();               // the few settings, drawn in the manager's window
std::string Status();               // "Passthrough ON" while it is on, otherwise empty

void ForcePassthroughForTest(bool on);   // the offline test only: on without the hotkey or a focused game window

} // namespace reshade
} // namespace features
} // namespace lsproxy
