#pragma once
#include <string>

namespace lsproxy {
namespace features {
namespace windowed {

// Windowed mode: lets Lossless Scaling work with a windowed game, and with a second monitor. It adds a virtual display the size (and
// position) of the game window to what Lossless Scaling sees, optionally covering only half of that window (split) or sitting beside it.
//
// It works by hooking Windows' display enumeration and DXGI's factory, and those hooks must be in place before Lossless Scaling asks for its
// displays. So it is started when Lossless Scaling starts (if it is switched on then), and switching it on later needs a restart;
// switching it off while running just makes the hooks do nothing.

inline constexpr const char* kId = "LSP-Windowed";   // the settings section, and the id the on/off switch has always had

void Start();                       // load the settings and install the hooks (Lossless Scaling start-up, when it is switched on)
void Stop();                        // take the hooks out (shutdown)
bool Started();                     // the hooks are (being) installed
void SetActive(bool on);            // the live switch, when the hooks are already in
void RenderOptions();               // the few settings, drawn in the manager's window
std::string Status();               // a short line for the window's status, or empty

} // namespace windowed
} // namespace features
} // namespace lsproxy
