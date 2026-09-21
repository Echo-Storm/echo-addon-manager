#include "addon_card.h"
#include "toggle_switch.h"
#include "tooltip.h"
#include "../gui_scale.h"
#include "../../host/metrics.h"
#include "lsproxy/lsp_widgets.h"
#include <cstdio>

namespace lsproxy {
namespace widgets {

// Status dot color
static ImU32 GetStatusColor(const AddonInfo& addon) {
    if (addon.faulted) return IM_COL32(208, 80, 80, 255);   // Red
    if (addon.security == SecurityVerdict::Tampered) return IM_COL32(208, 80, 80, 255);
    if (addon.security == SecurityVerdict::Unknown && addon.enabled) return lsp::theme::U(lsp::theme::kWarn); // Amber
    if (addon.IsLoaded()) return lsp::theme::U(lsp::theme::kAccent); // Accent green
    return IM_COL32(102, 102, 102, 255);                      // Grey
}

// Fallback icon when an addon ships none: a package glyph on a dark tile (vector, so it stays crisp at any scale)
static void DrawLetterIcon(ImDrawList* drawList, ImVec2 pos, float size, const std::string&) {
    using namespace lsp::theme;
    drawList->AddRectFilled(pos, ImVec2(pos.x + size, pos.y + size), U(kPanelAlt), S(4.0f));
    drawList->AddRect(pos, ImVec2(pos.x + size, pos.y + size), U(kBorderBright), S(4.0f), 0, 1.0f);
    const float in = size * 0.60f, off = (size - in) * 0.5f;
    lsp::svg::Draw(drawList, lsp::icons::kPackage, ImVec2(pos.x + off, pos.y + off), in, U(kAccent), 1.7f);
}

// Draw addon icon (texture or letter fallback)
static void DrawIcon(ImDrawList* drawList, ImVec2 pos, float size, const AddonInfo& addon) {
    if (addon.iconTexture) {
        // Draw the texture with rounded corners via clip rect + image
        ImVec2 p1(pos.x + size, pos.y + size);
        drawList->AddImageRounded(
            (ImTextureID)addon.iconTexture,
            pos, p1,
            ImVec2(0, 0), ImVec2(1, 1),
            IM_COL32(255, 255, 255, 255),
            S(4.0f));
    } else {
        DrawLetterIcon(drawList, pos, size, addon.GetDisplayName());
    }
}

bool AddonCard(AddonInfo& addon, int index, bool isSelected, bool* toggled) {
    bool clicked = false;
    if (toggled) *toggled = false;
    ImGui::PushID(index);

    ImGui::PushStyleColor(ImGuiCol_ChildBg, lsp::theme::V(isSelected ? lsp::theme::kRowHover : lsp::theme::kPanel));
    ImGui::PushStyleColor(ImGuiCol_Border, lsp::theme::V(isSelected ? lsp::theme::kAccentDim : lsp::theme::kBorder));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, S(4.0f));

    const Metrics::Status live = Metrics::Instance().GetStatus(addon.id);   // the addon's own one-line status, when it reports one
    float cardHeight = S(70.0f) + (live.text.empty() ? 0.0f : S(20.0f));
    ImGui::BeginChild("Card", ImVec2(-1, cardHeight), true);

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 cursorPos = ImGui::GetCursorScreenPos();

    // Icon (texture or letter fallback)
    float iconSize = S(40.0f);
    DrawIcon(drawList, cursorPos, iconSize, addon);

    // Status dot
    ImVec2 dotPos(cursorPos.x + iconSize - S(4), cursorPos.y + iconSize - S(4));
    drawList->AddCircleFilled(dotPos, S(5.0f), GetStatusColor(addon));
    drawList->AddCircle(dotPos, S(5.0f), IM_COL32(0, 0, 0, 150), 12, 1.0f);

    // Text area
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + iconSize + S(10));
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + S(4));

    float startY = ImGui::GetCursorPosY();
    ImGui::Text("%s", addon.GetDisplayName().c_str());
    {   // a chip only for states that need attention (the switch already says on / off)
        const char* chip = nullptr; ImU32 col = 0;
        if (addon.faulted || addon.security == SecurityVerdict::Tampered) { chip = addon.faulted ? "ERROR" : "TAMPERED"; col = IM_COL32(208, 128, 128, 255); }
        else if (addon.enabled && !addon.IsLoaded() && addon.RequiresRestart()) { chip = "RESTART TO APPLY"; col = lsp::theme::U(lsp::theme::kWarn); }
        else if (addon.enabled && !addon.IsLoaded()) { chip = "NOT LOADED"; col = lsp::theme::U(lsp::theme::kWarn); }
        if (chip) {
            ImGui::SameLine(0, S(8));
            const ImVec2 cp = ImGui::GetCursorScreenPos(), ts = ImGui::CalcTextSize(chip);
            const float px = S(6), py = S(1);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRect(ImVec2(cp.x, cp.y), ImVec2(cp.x + ts.x + px * 2, cp.y + ts.y + py * 2), col, S(3.0f), 0, 1.0f);
            dl->AddText(ImVec2(cp.x + px, cp.y + py), col, chip);
            ImGui::Dummy(ImVec2(ts.x + px * 2, ts.y + py * 2));
        }
    }
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + iconSize + S(10));
    ImGui::TextDisabled("v%s  |  %s", addon.GetDisplayVersion().c_str(), addon.GetDisplayAuthor().c_str());
    if (!live.text.empty()) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + iconSize + S(10));
        const float* c = live.level == 1 ? lsp::theme::kAccent : live.level == 2 ? lsp::theme::kWarn : live.level == 3 ? lsp::theme::kDanger : lsp::theme::kMuted;
        ImGui::PushStyleColor(ImGuiCol_Text, lsp::theme::V(c));
        ImGui::TextUnformatted(live.text.c_str());
        ImGui::PopStyleColor();
    }

    // Toggle switch on the right
    float toggleWidth = ImGui::GetFrameHeight() * 1.8f * 0.8f;
    ImGui::SameLine(ImGui::GetWindowWidth() - toggleWidth - S(15));
    ImGui::SetCursorPosY(startY + S(5));

    char toggleId[32];
    snprintf(toggleId, sizeof(toggleId), "##toggle_%d", index);
    bool enabled = addon.enabled;
    if (ToggleSwitch(toggleId, &enabled)) {
        addon.enabled = enabled;
        if (toggled) *toggled = true;
        clicked = true;
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled)) {
        std::string tip = addon.GetDisplayName() + "\n\n";
        tip += addon.manifest.description.empty() ? std::string("No description.") : addon.manifest.description;
        tip += "\n\n";
        tip += addon.enabled ? "On: switch it off to stop using this addon." : "Off: switch it on to use this addon.";
        if (addon.RequiresRestart()) tip += "\nChanging this takes effect the next time Lossless Scaling starts.";
        Tip(tip.c_str());
    }

    // Detect card click (not on toggle)
    if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0)) {
        clicked = true;
    }

    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
    ImGui::PopID();

    return clicked;
}

} // namespace widgets
} // namespace lsproxy
