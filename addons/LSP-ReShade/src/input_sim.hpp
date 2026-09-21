#pragma once
#include <windows.h>

namespace InputSim {

void SendKey(WORD vk, bool down);
void Click();
void PressCombo(int vk, bool ctrl, bool alt, bool shift);

} // namespace InputSim
