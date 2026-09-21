#include "features.h"
#include "reshade_passthrough.h"
#include "windowed_mode.h"
#include "../config/config_manager.h"
#include "../log/logger.h"
#include <cstring>

namespace lsproxy {
namespace features {

namespace {

enum Which { kReShade = 0, kWindowed = 1, kCount = 2 };

const Info kInfo[kCount] = {
    { reshade::kId,
      "ReShade input passthrough",
      "Use the mouse and keyboard on a ReShade overlay while Lossless Scaling is scaling the game.",
      "Lossless Scaling's window normally swallows the mouse and keyboard, so a ReShade menu cannot be clicked or typed into. With this on, "
      "the hotkey below turns passthrough on and off: while it is on, your input goes to the ReShade overlay. It turns itself off when the "
      "game or Lossless Scaling loses focus.\nIt can be switched on and off at any time." },
    { windowed::kId,
      "Windowed mode and second monitor",
      "Let Lossless Scaling work with a windowed game, or with a second monitor.",
      "Adds a virtual display the size of your game window, so Lossless Scaling can scale a window instead of needing full screen. It can also "
      "cover just one half of that window (split screen) or sit beside it (for a second monitor).\nIt has to be in place before Lossless "
      "Scaling starts, so switching it on needs Lossless Scaling to be restarted. Switching it off takes effect at once." },
};

ConfigManager& Cfg() { return ConfigManager::Instance(); }

} // namespace

int Count() { return kCount; }

const Info& At(int index) { return kInfo[index]; }

bool IsOn(int index) { return Cfg().IsAddonEnabled(kInfo[index].id, false); }

void SetOn(int index, bool on) {
    Cfg().SetAddonEnabled(kInfo[index].id, on);
    Cfg().Save();
    LOG_INFO("Features", "%s is now %s", kInfo[index].title, on ? "on" : "off");

    switch (index) {
        case kReShade:
            if (on) reshade::Start(); else reshade::Stop();
            break;
        case kWindowed:
            if (windowed::Started()) windowed::SetActive(on);   // hooks are in: the switch is live. Otherwise it waits for a restart.
            break;
    }
}

bool NeedsRestart(int index) { return index == kWindowed && IsOn(index) && !windowed::Started(); }

std::string Status(int index) {
    switch (index) {
        case kReShade: return IsOn(index) ? reshade::Status() : std::string();
        case kWindowed: return IsOn(index) ? windowed::Status() : std::string();
    }
    return {};
}

void RenderOptions(int index) {
    switch (index) {
        case kReShade: reshade::RenderOptions(); break;
        case kWindowed: windowed::RenderOptions(); break;
    }
}

void Start() {
    if (IsOn(kReShade)) reshade::Start();
    if (IsOn(kWindowed)) windowed::Start();
}

void Stop() {
    reshade::Stop();
    windowed::Stop();
}

bool IsRetiredAddonId(const std::string& folderName) {
    for (const Info& f : kInfo)
        if (folderName == f.id) return true;
    return false;
}

} // namespace features
} // namespace lsproxy
