#include "hotkey.h"
#include "../../config/config_manager.h"
#include "../../log/logger.h"
#include <atomic>
#include <cstdio>
#include <mutex>

namespace eam {
namespace window {
namespace hotkey {

namespace {
std::mutex g_mu;                        // Status() is read by the Settings tab while the GUI thread may be re-registering
std::atomic<bool> g_registered{ false };
char g_status[96] = "off";
}

int NormalizeVk(int vk) { return (vk < VK_F1 || vk > VK_F12) ? VK_F12 : vk; }

std::wstring Label(int vk) {
    wchar_t b[32];
    swprintf(b, 32, L"Ctrl+Shift+F%d", NormalizeVk(vk) - VK_F1 + 1);
    return b;
}

std::wstring LabelFromConfig() { return Label(ConfigManager::Instance().GlobalGetOr<int>("ui", "hotkey_vk", VK_F12)); }

void Apply(HWND hwnd) {
    if (!hwnd) return;
    UnregisterHotKey(hwnd, kId);
    g_registered = false;

    std::lock_guard<std::mutex> lk(g_mu);
    auto& cfg = ConfigManager::Instance();
    if (!cfg.GlobalGetOr<bool>("ui", "hotkey_enabled", true)) {
        snprintf(g_status, sizeof g_status, "off");
        return;
    }
    const int vk = NormalizeVk(cfg.GlobalGetOr<int>("ui", "hotkey_vk", VK_F12));
    if (RegisterHotKey(hwnd, kId, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, static_cast<UINT>(vk))) {
        g_registered = true;
        snprintf(g_status, sizeof g_status, "registered");
    } else {
        snprintf(g_status, sizeof g_status, "could not register: another program already uses that combination");
        LOG_WARN("GUI", "Manager hotkey Ctrl+Shift+F%d could not be registered (error %lu)", vk - VK_F1 + 1, GetLastError());
    }
}

void Remove(HWND hwnd) {
    if (hwnd) UnregisterHotKey(hwnd, kId);
    g_registered = false;
}

bool Registered() { return g_registered; }

const char* Status() { return g_status; }

} // namespace hotkey
} // namespace window
} // namespace eam
