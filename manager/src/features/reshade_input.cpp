#include "reshade_input.h"
#include <chrono>
#include <thread>

namespace lsproxy {
namespace features {
namespace reshade {

namespace {

void Pause(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

void Mouse(DWORD flag) {
    INPUT in = {};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = flag;
    SendInput(1, &in, sizeof in);
}

} // namespace

void SendKey(WORD vk, bool down) {
    INPUT in = {};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    in.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
    SendInput(1, &in, sizeof in);
}

void Click() {
    Mouse(MOUSEEVENTF_LEFTDOWN);
    Pause(50);
    Mouse(MOUSEEVENTF_LEFTUP);
}

void PressCombo(int vk, bool ctrl, bool alt, bool shift) {
    if (ctrl) SendKey(VK_CONTROL, true);
    if (alt) SendKey(VK_MENU, true);
    if (shift) SendKey(VK_SHIFT, true);

    SendKey((WORD)vk, true);
    Pause(150);
    SendKey((WORD)vk, false);

    if (shift) SendKey(VK_SHIFT, false);
    if (alt) SendKey(VK_MENU, false);
    if (ctrl) SendKey(VK_CONTROL, false);
}

} // namespace reshade
} // namespace features
} // namespace lsproxy
