#pragma once
#include <windows.h>

namespace eam {
namespace features {
namespace reshade {

// Synthesised input, the same as if the user had used the mouse and keyboard.
void SendKey(WORD vk, bool down);
void Click();                                                   // one left click at the pointer
void PressCombo(int vk, bool ctrl, bool alt, bool shift);       // holds the modifiers, taps the key, lets go

} // namespace reshade
} // namespace features
} // namespace eam
