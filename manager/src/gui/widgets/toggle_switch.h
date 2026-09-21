#pragma once
#include "imgui.h"

namespace lsproxy {
namespace widgets {

// Animated pill-shaped toggle switch. Returns true if state changed.
bool ToggleSwitch(const char* id, bool* value);

} // namespace widgets
} // namespace lsproxy
