#pragma once
#include "imgui.h"

namespace lsproxy {
namespace widgets {

// Wrapped tooltip for the item just submitted, shown after a short hover delay.
inline void Tip(const char* text) {
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled)) return;
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

} // namespace widgets
} // namespace lsproxy
