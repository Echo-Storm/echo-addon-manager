#pragma once
#include "../../addon/addon_info.h"
#include "imgui.h"

namespace eam {
namespace widgets {

// Renders one addon's row in the sidebar. Returns true if the row was clicked (selected).
// If the enable switch on the card was flipped this frame, *toggled is set to true and
// addon.enabled already holds the new state; the caller must apply it via
// AddonManager::ToggleAddon (the card itself never loads, unloads or saves anything).
bool AddonCard(AddonInfo& addon, int index, bool isSelected, bool* toggled = nullptr);

// The addon's icon on a dark tile, `size` square at `at` (lit: in the accent colour, for an addon that is on). Also used by the detail pane.
void DrawAddonIcon(ImDrawList* draw, ImVec2 at, float size, const AddonInfo& addon, bool lit);

} // namespace widgets
} // namespace eam
