#include "input_sim.hpp"
#include <thread>
#include <chrono>

namespace InputSim {

void SendKey(WORD vk, bool down) {
    INPUT input = {};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = vk;
    input.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
    SendInput(1, &input, sizeof(INPUT));
}

void Click() {
    INPUT inputs[2] = {};
    inputs[0].type = INPUT_MOUSE;
    inputs[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    inputs[1].type = INPUT_MOUSE;
    inputs[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    SendInput(1, &inputs[0], sizeof(INPUT));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    SendInput(1, &inputs[1], sizeof(INPUT));
}

void PressCombo(int vk, bool ctrl, bool alt, bool shift) {
    if (ctrl)  SendKey(VK_CONTROL, true);
    if (alt)   SendKey(VK_MENU, true);
    if (shift) SendKey(VK_SHIFT, true);

    SendKey((WORD)vk, true);
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    SendKey((WORD)vk, false);

    if (shift) SendKey(VK_SHIFT, false);
    if (alt)   SendKey(VK_MENU, false);
    if (ctrl)  SendKey(VK_CONTROL, false);
}

} // namespace InputSim
