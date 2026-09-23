#include "tab_about.h"
#include "../gui_scale.h"
#include "../widgets/status_bar.h"
#include "../../update/update_check.h"
#include "../../../sdk/include/eam/version.h"
#include "imgui.h"
#include "eam/widgets.h"
#include <cstdio>
#include <windows.h>
#include <shellapi.h>

namespace eam {

static void OpenUrl(const char* url) { ShellExecuteA(nullptr, "open", url, nullptr, nullptr, SW_SHOWNORMAL); }

static void CenteredText(const char* text, bool muted = false) {
    const float w = ImGui::CalcTextSize(text).x;
    ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - w) * 0.5f + ImGui::GetCursorPosX());
    if (muted) ImGui::TextDisabled("%s", text); else ImGui::TextUnformatted(text);
}

void RenderTabAbout() {
    ImGui::Dummy(ImVec2(0, S(12)));

    // logo: the Echo mark on a dark tile, drawn as vectors (crisp at any scale)
    {
        const float s = ImGui::GetFontSize() * 4.2f;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (ImGui::GetContentRegionAvail().x - s) * 0.5f);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + s, p.y + s), eam::ui::theme::U(eam::ui::theme::kPanel), s * 0.22f);
        dl->AddRect(p, ImVec2(p.x + s, p.y + s), eam::ui::theme::U(eam::ui::theme::kAccentDim), s * 0.22f, 0, 1.5f);
        const float m = s * 0.06f;
        const float in = s - 2 * m;
        eam::ui::svg::Draw(dl, eam::ui::icons::kEchoBack, ImVec2(p.x + m, p.y + m), in, eam::ui::theme::U(eam::ui::theme::kAccentDim), 1.8f);
        eam::ui::svg::Draw(dl, eam::ui::icons::kEchoMid, ImVec2(p.x + m, p.y + m), in, eam::ui::theme::U(eam::ui::theme::kAccent), 1.8f);
        eam::ui::svg::Draw(dl, eam::ui::icons::kEchoFront, ImVec2(p.x + m, p.y + m), in, eam::ui::theme::U(eam::ui::theme::kAccentHot), 1.8f, eam::ui::theme::U(eam::ui::theme::kAccent));
        ImGui::Dummy(ImVec2(s, s));
        ImGui::Dummy(ImVec2(0, S(6)));
    }

    // title block
    CenteredText(EAM_PRODUCT_NAME);
    char versionStr[64];
    snprintf(versionStr, sizeof(versionStr), "v%s", EAM_VERSION_STRING);
    CenteredText(versionStr, true);
    {   // is there a newer one? (the daily check, or the button)
        const update::Status st = update::Current();
        const std::string line = update::DescribeStatus(st);
        if (st.state == update::State::Available) {
            ImGui::PushStyleColor(ImGuiCol_Text, eam::ui::theme::V(eam::ui::theme::kAccent));
            CenteredText(line.c_str());
            ImGui::PopStyleColor();
            const float bw2 = ImGui::CalcTextSize("Open the download page").x + ImGui::GetFontSize() * 4.0f;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (ImGui::GetContentRegionAvail().x - bw2) * 0.5f);
            if (eam::ui::Button("Open the download page", eam::ui::icons::kExternal, eam::ui::ButtonKind::Primary, ImVec2(bw2, 0))) OpenUrl(st.url.c_str());
        } else {
            if (st.state != update::State::Idle) CenteredText(line.c_str(), true);
            const bool checking = st.state == update::State::Checking;
            const float bw2 = ImGui::CalcTextSize("Check for updates").x + ImGui::GetFontSize() * 3.0f;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (ImGui::GetContentRegionAvail().x - bw2) * 0.5f);
            if (checking) ImGui::BeginDisabled();
            if (eam::ui::Button("Check for updates", eam::ui::icons::kCheck, eam::ui::ButtonKind::Flat, ImVec2(bw2, 0))) update::StartCheckAsync();
            if (checking) ImGui::EndDisabled();
        }
    }
    ImGui::Dummy(ImVec2(0, S(6)));
    CenteredText("An addon manager for Lossless Scaling: shaders, behaviour, and anything else an addon adds.", true);

    // support
    ImGui::Dummy(ImVec2(0, S(16)));
    const float bw = ImGui::CalcTextSize("Support on Ko-fi").x + ImGui::GetFontSize() * 4.0f;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (ImGui::GetContentRegionAvail().x - bw) * 0.5f);
    if (eam::ui::Button("Support on Ko-fi", eam::ui::icons::kHeart, eam::ui::ButtonKind::Primary, ImVec2(bw, ImGui::GetFrameHeight() * 1.25f)))
        widgets::OpenKofi();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("If this is useful, a Ko-fi helps a lot. Opens your browser.");

    ImGui::Dummy(ImVec2(0, S(18)));
    eam::ui::SectionLabel("Build");
    ImGui::TextDisabled("Addon API");   ImGui::SameLine(S(120)); ImGui::Text("v%s", EAM_API_VERSION_STRING);
    ImGui::TextDisabled("ImGui");        ImGui::SameLine(S(120)); ImGui::Text("%s", ImGui::GetVersion());
    ImGui::TextDisabled("Built");        ImGui::SameLine(S(120)); ImGui::Text("%s %s", __DATE__, __TIME__);

    ImGui::Dummy(ImVec2(0, S(14)));
    eam::ui::SectionLabel("Good to know");
    ImGui::TextWrapped("Closed this window? It is still running in the notification area (tray). Click the icon, or use the hotkey set in "
                       "Settings, to open it again.");
    ImGui::Dummy(ImVec2(0, S(4)));
    ImGui::TextWrapped("Hold Ctrl and scroll over any slider to fine-tune it; double-click a slider to put it back to its default.");

    ImGui::Dummy(ImVec2(0, S(14)));
    eam::ui::SectionLabel("Thanks");
    ImGui::TextWrapped("This began as LosslessProxy by FrankBarretta (MIT), and we are grateful for it. Just under a third of this program's code "
                       "is still theirs: mainly the part that loads into Lossless Scaling and hooks DirectX 11 and its shaders, plus the "
                       "ReShade and Windowed features, which began as addons of theirs and are built in now. Everything else was written or rewritten since: how "
                       "addons are found, checked, loaded and switched, the settings file, the event system, the window, tabs, "
                       "Performance, backup, install and remove, the tray and the shared look.");
    ImGui::Dummy(ImVec2(0, S(4)));
    if (eam::ui::Button("The original project", eam::ui::icons::kExternal, eam::ui::ButtonKind::Flat)) OpenUrl("https://github.com/FrankBarretta/LosslessProxy");
    ImGui::Dummy(ImVec2(0, S(10)));
    ImGui::TextDisabled("Also: Dear ImGui and nlohmann/json (MIT), stb_image (public domain), MinHook (BSD, used by Windowed mode).");
    ImGui::TextDisabled("Icon shapes are drawn in the manner of the Lucide set (ISC).");
    ImGui::TextDisabled("Lossless Scaling belongs to its author; this is an unofficial add-on for it.");

    ImGui::Dummy(ImVec2(0, S(14)));
    eam::ui::SectionLabel("For addon authors");
    ImGui::TextDisabled("Include the SDK headers (addon_sdk.h). widgets.h gives an addon this same look: the palette,");
    ImGui::TextDisabled("sliders with defaults, section headers, buttons and SVG icons.");
}

} // namespace eam
