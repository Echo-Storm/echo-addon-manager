#include "toggle_switch.h"
#include "imgui_internal.h"
#include "lsproxy/lsp_widgets.h"
#include <unordered_map>

namespace lsproxy {
namespace widgets {

static std::unordered_map<ImGuiID, float> s_animState;

bool ToggleSwitch(const char* id, bool* value) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID imId = window->GetID(id);

    const float height = ImGui::GetFrameHeight() * 0.8f;
    const float width = height * 1.8f;
    const float radius = height * 0.5f;

    ImVec2 pos = window->DC.CursorPos;
    ImVec2 size(width, height);
    ImRect bb(pos, ImVec2(pos.x + width, pos.y + height));
    ImGui::ItemSize(bb, style.FramePadding.y);
    if (!ImGui::ItemAdd(bb, imId)) return false;

    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(bb, imId, &hovered, &held);
    if (pressed) {
        *value = !(*value);
    }

    // Animation
    float& anim = s_animState[imId];
    float target = *value ? 1.0f : 0.0f;
    float speed = g.IO.DeltaTime * 8.0f;
    if (anim < target) anim = (anim + speed > target) ? target : anim + speed;
    else if (anim > target) anim = (anim - speed < target) ? target : anim - speed;

    // Colors
    using namespace lsp::theme;
    ImU32 bgColor = *value ? U(hovered ? kAccent : kAccentDim) : U(hovered ? kBorderBright : kButtonHover);
    ImU32 knobColor = *value ? U(kText) : U(kMuted);

    ImDrawList* drawList = window->DrawList;

    // Background pill
    drawList->AddRectFilled(bb.Min, bb.Max, bgColor, radius);
    if (*value) drawList->AddRect(bb.Min, bb.Max, U(kAccent), radius, 0, 1.0f);
    else drawList->AddRect(bb.Min, bb.Max, U(kBorderBright), radius, 0, 1.0f);

    // Knob
    float knobPadding = height * 0.1f;   // scales with the row height, so it follows the display scale
    float knobRadius = radius - knobPadding;
    float knobX = bb.Min.x + radius + anim * (width - height);
    float knobY = bb.Min.y + radius;
    drawList->AddCircleFilled(ImVec2(knobX, knobY), knobRadius, knobColor);

    return pressed;
}

} // namespace widgets
} // namespace lsproxy
