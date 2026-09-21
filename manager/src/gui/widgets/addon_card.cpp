#include "addon_card.h"
#include "toggle_switch.h"
#include "tooltip.h"
#include "../gui_scale.h"
#include "../../host/metrics.h"
#include "lsproxy/lsp_widgets.h"
#include <cstdio>
#include <string>

namespace lsproxy {
namespace widgets {

namespace {

const ImU32 kRed = IM_COL32(208, 80, 80, 255);
const ImU32 kGrey = IM_COL32(102, 102, 102, 255);
const ImU32 kChipRed = IM_COL32(208, 128, 128, 255);

// The dot on the icon's corner: red for trouble, amber when it is on but unchecked, green when it is running, grey otherwise.
ImU32 DotColour(const AddonInfo& a) {
    using namespace lsp::theme;
    if (a.faulted || a.security == SecurityVerdict::Tampered) return kRed;
    if (a.security == SecurityVerdict::Unknown && a.enabled) return U(kWarn);
    if (a.IsLoaded()) return U(kAccent);
    return kGrey;
}

// The small outlined label beside the name, only for states that need attention (the switch already says on or off).
struct Chip { const char* text = nullptr; ImU32 colour = 0; };

Chip ChipFor(const AddonInfo& a) {
    using namespace lsp::theme;
    if (a.faulted) return { "ERROR", kChipRed };
    if (a.security == SecurityVerdict::Tampered) return { "TAMPERED", kChipRed };
    if (a.enabled && !a.IsLoaded()) return { a.RequiresRestart() ? "RESTART TO APPLY" : "NOT LOADED", U(kWarn) };
    return {};
}

const float* LiveColour(int level) {
    using namespace lsp::theme;
    switch (level) {
        case 1: return kAccent;
        case 2: return kWarn;
        case 3: return kDanger;
        default: return kMuted;
    }
}

// The addon's own icon, or a package glyph on a dark tile when it ships none (vector, so it stays crisp at any scale).
void DrawIcon(ImDrawList* draw, ImVec2 at, float size, const AddonInfo& a) {
    using namespace lsp::theme;
    const ImVec2 bottomRight(at.x + size, at.y + size);
    if (a.iconTexture) {
        draw->AddImageRounded((ImTextureID)a.iconTexture, at, bottomRight, ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 255), S(4.0f));
        return;
    }
    draw->AddRectFilled(at, bottomRight, U(kPanelAlt), S(4.0f));
    draw->AddRect(at, bottomRight, U(kBorderBright), S(4.0f), 0, 1.0f);
    const float glyph = size * 0.60f, inset = (size - glyph) * 0.5f;
    lsp::svg::Draw(draw, lsp::icons::kPackage, ImVec2(at.x + inset, at.y + inset), glyph, U(kAccent), 1.7f);
}

void DrawChip(const Chip& chip) {
    ImGui::SameLine(0, S(8));
    const ImVec2 at = ImGui::GetCursorScreenPos(), text = ImGui::CalcTextSize(chip.text);
    const float padX = S(6), padY = S(1);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRect(at, ImVec2(at.x + text.x + padX * 2, at.y + text.y + padY * 2), chip.colour, S(3.0f), 0, 1.0f);
    draw->AddText(ImVec2(at.x + padX, at.y + padY), chip.colour, chip.text);
    ImGui::Dummy(ImVec2(text.x + padX * 2, text.y + padY * 2));
}

std::string SwitchTooltip(const AddonInfo& a) {
    std::string tip = a.GetDisplayName() + "\n\n";
    tip += a.manifest.description.empty() ? std::string("No description.") : a.manifest.description;
    tip += "\n\n";
    tip += a.enabled ? "On: switch it off to stop using this addon." : "Off: switch it on to use this addon.";
    if (a.RequiresRestart()) tip += "\nChanging this takes effect the next time Lossless Scaling starts.";
    return tip;
}

} // namespace

bool AddonCard(AddonInfo& addon, int index, bool isSelected, bool* toggled) {
    using namespace lsp::theme;
    bool clicked = false;
    if (toggled) *toggled = false;
    ImGui::PushID(index);

    ImGui::PushStyleColor(ImGuiCol_ChildBg, V(isSelected ? kRowHover : kPanel));
    ImGui::PushStyleColor(ImGuiCol_Border, V(isSelected ? kAccentDim : kBorder));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, S(4.0f));

    const Metrics::Status live = Metrics::Instance().GetStatus(addon.id);   // the addon's own one-line status, if it reports one
    const float cardHeight = S(70.0f) + (live.text.empty() ? 0.0f : S(20.0f));
    ImGui::BeginChild("Card", ImVec2(-1, cardHeight), true);

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 corner = ImGui::GetCursorScreenPos();
    const float iconSize = S(40.0f);
    const float textIndent = iconSize + S(10);

    DrawIcon(draw, corner, iconSize, addon);
    const ImVec2 dot(corner.x + iconSize - S(4), corner.y + iconSize - S(4));
    draw->AddCircleFilled(dot, S(5.0f), DotColour(addon));
    draw->AddCircle(dot, S(5.0f), IM_COL32(0, 0, 0, 150), 12, 1.0f);

    // name, chip, version and author, then the live status, all to the right of the icon
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + textIndent);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + S(4));
    const float rowTop = ImGui::GetCursorPosY();
    ImGui::Text("%s", addon.GetDisplayName().c_str());
    if (const Chip chip = ChipFor(addon); chip.text) DrawChip(chip);

    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + textIndent);
    ImGui::TextDisabled("v%s  |  %s", addon.GetDisplayVersion().c_str(), addon.GetDisplayAuthor().c_str());
    if (!live.text.empty()) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + textIndent);
        ImGui::PushStyleColor(ImGuiCol_Text, V(LiveColour(live.level)));
        ImGui::TextUnformatted(live.text.c_str());
        ImGui::PopStyleColor();
    }

    // the switch, at the right edge
    const float switchWidth = ImGui::GetFrameHeight() * 1.8f * 0.8f;
    ImGui::SameLine(ImGui::GetWindowWidth() - switchWidth - S(15));
    ImGui::SetCursorPosY(rowTop + S(5));
    char switchId[32];
    snprintf(switchId, sizeof switchId, "##toggle_%d", index);
    bool on = addon.enabled;
    if (ToggleSwitch(switchId, &on)) {
        addon.enabled = on;
        if (toggled) *toggled = true;
        clicked = true;
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled)) Tip(SwitchTooltip(addon).c_str());

    if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0)) clicked = true;   // anywhere on the card selects it

    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
    ImGui::PopID();
    return clicked;
}

} // namespace widgets
} // namespace lsproxy
