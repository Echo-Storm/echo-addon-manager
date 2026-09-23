#pragma once
#include "../../addon/addon_info.h"
#include "imgui.h"

namespace eam {
namespace widgets {

// Renders a single addon card. Returns true if the card was clicked (selected).
// If the enable switch on the card was flipped this frame, *toggled is set to true and
// addon.enabled already holds the new state; the caller must apply it via
// AddonManager::ToggleAddon (the card itself never loads, unloads or saves anything).
bool AddonCard(AddonInfo& addon, int index, bool isSelected, bool* toggled = nullptr);

} // namespace widgets
} // namespace eam
