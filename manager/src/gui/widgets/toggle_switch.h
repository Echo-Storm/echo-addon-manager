#pragma once
#include "imgui.h"

namespace eam {
namespace widgets {

// Animated pill-shaped toggle switch. Returns true if state changed. size: 1 is the standard height (most of a text row).
bool ToggleSwitch(const char* id, bool* value, float size = 1.0f);

} // namespace widgets
} // namespace eam
