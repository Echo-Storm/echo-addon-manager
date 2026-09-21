#include "toggle_switch.h"
#include "imgui_internal.h"
#include "lsproxy/lsp_widgets.h"
#include <unordered_map>

namespace lsproxy {
namespace widgets {

namespace {

// How far each switch's knob has travelled, 0 (off end) to 1 (on end), kept by the switch's id so it animates across frames.
std::unordered_map<ImGuiID, float> g_knobTravel;

float MoveToward(float from, float to, float step) {
    if (from < to) return from + step > to ? to : from + step;
    if (from > to) return from - step < to ? to : from - step;
    return from;
}

} // namespace

bool ToggleSwitch(const char* id, bool* value) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    ImGuiContext& g = *GImGui;
    const ImGuiID key = window->GetID(id);

    // The pill is 1.8 times as wide as it is tall, and as tall as most of a text row, so it follows the display scale.
    const float height = ImGui::GetFrameHeight() * 0.8f;
    const float width = height * 1.8f;
    const float radius = height * 0.5f;

    const ImVec2 origin = window->DC.CursorPos;
    const ImRect bounds(origin, ImVec2(origin.x + width, origin.y + height));
    ImGui::ItemSize(bounds, g.Style.FramePadding.y);
    if (!ImGui::ItemAdd(bounds, key)) return false;

    bool hovered = false, held = false;
    const bool flipped = ImGui::ButtonBehavior(bounds, key, &hovered, &held);
    if (flipped) *value = !*value;

    float& travel = g_knobTravel[key];
    travel = MoveToward(travel, *value ? 1.0f : 0.0f, g.IO.DeltaTime * 8.0f);

    using namespace lsp::theme;
    const ImU32 fill = *value ? U(hovered ? kAccent : kAccentDim) : U(hovered ? kBorderBright : kButtonHover);
    const ImU32 outline = *value ? U(kAccent) : U(kBorderBright);
    const ImU32 knobColour = *value ? U(kText) : U(kMuted);

    ImDrawList* draw = window->DrawList;
    draw->AddRectFilled(bounds.Min, bounds.Max, fill, radius);
    draw->AddRect(bounds.Min, bounds.Max, outline, radius, 0, 1.0f);

    const float knobRadius = radius - height * 0.1f;
    const ImVec2 knobCentre(bounds.Min.x + radius + travel * (width - height), bounds.Min.y + radius);
    draw->AddCircleFilled(knobCentre, knobRadius, knobColour);

    return flipped;
}

} // namespace widgets
} // namespace lsproxy
