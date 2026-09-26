#include "runtime_list.h"
#include "imgui.h"
#include <eam/icons.h>
#include <eam/widgets.h>
#include <string>

namespace eam {
namespace widgets {

namespace {
std::string Utf8(const std::wstring& w) {
    std::string s;
    for (wchar_t c : w) {   // paths: enough for the tooltip (BMP characters)
        if (c < 0x80) s += (char)c;
        else if (c < 0x800) { s += (char)(0xC0 | (c >> 6)); s += (char)(0x80 | (c & 0x3F)); }
        else { s += (char)(0xE0 | (c >> 12)); s += (char)(0x80 | ((c >> 6) & 0x3F)); s += (char)(0x80 | (c & 0x3F)); }
    }
    return s;
}
} // namespace

RuntimeAction RuntimeListAtBottom(const std::vector<RuntimeFile>& rows) {
    RuntimeAction action;
    if (rows.empty()) return action;
    namespace th = ui::theme;
    const float a = ImGui::GetStyle().Alpha;
    // its height as drawn the frame before (an estimate the first time): the room above it is filled so it ends at the window's bottom
    static float measured = 0.0f;
    const float need = measured > 0.0f ? measured : ImGui::GetFontSize() * 2.0f + rows.size() * (ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y);
    const float gap = ImGui::GetContentRegionAvail().y - need - ImGui::GetStyle().ItemSpacing.y;
    if (gap > 0.0f) ImGui::Dummy(ImVec2(0, gap));
    const float top = ImGui::GetCursorPosY();
    ImGui::PushStyleColor(ImGuiCol_Separator, th::V(th::kBorder)); ImGui::Separator(); ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0, ImGui::GetFontSize() * 0.2f));
    ui::SectionLabel("Runtimes");
    ImGui::Dummy(ImVec2(0, ImGui::GetFontSize() * 0.15f));
    for (int i = 0; i < (int)rows.size(); ++i) {
        const RuntimeFile& r = rows[i];
        ImGui::PushID(i);
        const float h = ImGui::GetFrameHeight(), w = ImGui::GetContentRegionAvail().x;
        const float buttons = h;
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();

        // the line itself: hovering it shows the details
        ImGui::InvisibleButton("##row", ImVec2(w - buttons, h));
        const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort);
        if (ImGui::IsItemHovered()) dl->AddRectFilled(p, ImVec2(p.x + w - buttons, p.y + h), th::U(th::kRowHover, a), 3.0f);
        const float ico = ImGui::GetFontSize() * 0.9f;
        // a tick: loaded now; a cross: not (the addon is off, has not started on it yet, or the file is not there)
        const char* glyph = r.loaded ? ui::icons::kCheck : ui::icons::kClose;
        const ImU32 tint = r.loaded ? th::U(th::kAccent, a) : th::U(th::kMuted, a);
        std::string note;
        if (!r.exists) note = "not found";
        else if (!r.read) note = "...";
        else if (r.signature == RuntimeFile::Signature::Broken) note = "modified";
        else if (r.signature != RuntimeFile::Signature::Signed) note = "unsigned";
        ui::svg::Draw(dl, glyph, ImVec2(p.x + 2.0f, p.y + (h - ico) * 0.5f), ico, tint, 2.0f);
        float x = p.x + ico + ImGui::GetFontSize() * 0.55f;
        const float ty = p.y + (h - ImGui::GetFontSize()) * 0.5f, right = p.x + w - buttons - 4.0f;
        const ImVec4 clip(p.x, p.y, right, p.y + h);
        dl->AddText(nullptr, 0.0f, ImVec2(x, ty), th::U(th::kText, a), r.label.c_str(), nullptr, 0.0f, &clip);
        x += ImGui::CalcTextSize(r.label.c_str()).x + ImGui::GetFontSize() * 0.4f;
        const std::string version = r.exists && r.read ? r.ShownVersion() : std::string();
        if (!version.empty()) {
            dl->AddText(nullptr, 0.0f, ImVec2(x, ty), th::U(th::kMuted, a), version.c_str(), nullptr, 0.0f, &clip);
            x += ImGui::CalcTextSize(version.c_str()).x + ImGui::GetFontSize() * 0.4f;
        }
        if (!note.empty()) dl->AddText(nullptr, 0.0f, ImVec2(x, ty), th::U(th::kText, a * 0.75f), note.c_str(), nullptr, 0.0f, &clip);

        if (hovered) {
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 32.0f);
            ImGui::Text("%s runtime, used by %s (%s)", r.label.c_str(), r.addonName.c_str(), r.addonOn ? "on" : "off");
            if (r.loaded) ImGui::TextColored(th::V(th::kAccent), "Loaded now");
            else ImGui::TextDisabled("Not loaded now");
            if (!r.exists) ImGui::Text("Not found: %s", Utf8(r.path).c_str());
            else if (!r.read) ImGui::TextDisabled("Reading the file...");
            else {
                if (!r.description.empty() || !r.version.empty()) ImGui::TextUnformatted((r.description + (r.description.empty() ? "" : "  ") + r.version).c_str());
                if (r.signature == RuntimeFile::Signature::Signed) ImGui::Text("Signed by %s", r.signer.empty() ? "its maker" : r.signer.c_str());
                else if (r.signature == RuntimeFile::Signature::Broken)
                    ImGui::Text("Modified: signed by %s, then changed, so the signature no longer matches the file", r.signer.empty() ? "its maker" : r.signer.c_str());
                else ImGui::Text("Not signed%s", r.company.empty() ? "" : (" (it says it is " + r.company + "'s)").c_str());
                if (r.shippedKnown) ImGui::TextDisabled(r.shipped ? "The file the addon ships with." : "Not the file the addon ships with.");
                ImGui::TextDisabled("%s", Utf8(r.path).c_str());
                if (!r.sha256.empty()) ImGui::TextDisabled("SHA-256 %s", r.sha256.c_str());
            }
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }

        // + : another file
        ImGui::SameLine(0, 0);
        if (ui::IconButton("##choose", ui::icons::kPlus, h)) action = { i, RuntimeAction::Choose };
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("Use another %s file...", r.label.c_str());
        ImGui::PopID();
    }
    measured = ImGui::GetCursorPosY() - top;
    return action;
}

} // namespace widgets
} // namespace eam
