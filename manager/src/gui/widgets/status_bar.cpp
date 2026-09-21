#include "status_bar.h"
#include "../gui_scale.h"
#include "imgui.h"
#include "lsproxy/lsp_widgets.h"
#include <windows.h>
#include <shellapi.h>

namespace lsproxy {
namespace widgets {

void OpenKofi() { ShellExecuteA(nullptr, "open", kKofiUrl, nullptr, nullptr, SW_SHOWNORMAL); }

float StatusBarHeight() { return ImGui::GetFrameHeight() * 1.05f; }

void StatusBar(const std::string& left) {
    const float h = StatusBarHeight();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddLine(ImVec2(origin.x, origin.y), ImVec2(origin.x + w, origin.y), lsp::theme::U(lsp::theme::kBorder), 1.0f);

    // right: donate <heart> ko-fi
    const char* a = "donate";
    const char* b = "ko-fi";
    const float ico = ImGui::GetFontSize() * 0.95f, gap = ImGui::GetFontSize() * 0.4f;
    const float cw = ImGui::CalcTextSize(a).x + gap + ico + gap + ImGui::CalcTextSize(b).x;
    const float padX = ImGui::GetStyle().FramePadding.x;
    const ImVec2 bp(origin.x + w - cw - padX * 2.0f, origin.y + 1.0f);
    ImGui::SetCursorScreenPos(bp);
    const bool clicked = ImGui::InvisibleButton("##kofi", ImVec2(cw + padX * 2.0f, h - 1.0f));
    const bool hovered = ImGui::IsItemHovered();
    if (clicked) OpenKofi();
    if (hovered) ImGui::SetTooltip("Support development on Ko-fi (opens your browser)");
    const ImU32 txt = lsp::theme::U(hovered ? lsp::theme::kText : lsp::theme::kMuted);
    float x = bp.x + padX;
    const float ty = bp.y + (h - 1.0f - ImGui::GetTextLineHeight()) * 0.5f;
    dl->AddText(ImVec2(x, ty), txt, a);
    x += ImGui::CalcTextSize(a).x + gap;
    const float* heart = hovered ? lsp::theme::kAccentHot : lsp::theme::kAccent;   // the theme's green, like the other apps' accents
    lsp::svg::Draw(dl, lsp::icons::kHeart, ImVec2(x, bp.y + (h - 1.0f - ico) * 0.5f), ico, lsp::theme::U(heart), 0.0f, lsp::theme::U(heart));
    x += ico + gap;
    dl->AddText(ImVec2(x, ty), txt, b);

    // left: status text
    dl->AddText(ImVec2(origin.x + 2.0f, ty), lsp::theme::U(lsp::theme::kMuted), left.c_str());
    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y));
    ImGui::Dummy(ImVec2(w, h));   // an item after the cursor moves, so the window boundary is extended properly
}

} // namespace widgets
} // namespace lsproxy
