#include "tab_about.h"
#include "../gui_scale.h"
#include "../widgets/status_bar.h"
#include "../../../sdk/include/lsproxy/version.h"
#include "imgui.h"
#include "lsproxy/lsp_widgets.h"
#include <cstdio>
#include <windows.h>
#include <shellapi.h>

namespace lsproxy {

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
        dl->AddRectFilled(p, ImVec2(p.x + s, p.y + s), lsp::theme::U(lsp::theme::kPanel), s * 0.22f);
        dl->AddRect(p, ImVec2(p.x + s, p.y + s), lsp::theme::U(lsp::theme::kAccentDim), s * 0.22f, 0, 1.5f);
        const float m = s * 0.06f;
        const float in = s - 2 * m;
        lsp::svg::Draw(dl, lsp::icons::kEchoBack, ImVec2(p.x + m, p.y + m), in, lsp::theme::U(lsp::theme::kAccentDim), 1.8f);
        lsp::svg::Draw(dl, lsp::icons::kEchoMid, ImVec2(p.x + m, p.y + m), in, lsp::theme::U(lsp::theme::kAccent), 1.8f);
        lsp::svg::Draw(dl, lsp::icons::kEchoFront, ImVec2(p.x + m, p.y + m), in, lsp::theme::U(lsp::theme::kAccentHot), 1.8f, lsp::theme::U(lsp::theme::kAccent));
        ImGui::Dummy(ImVec2(s, s));
        ImGui::Dummy(ImVec2(0, S(6)));
    }

    // title block
    CenteredText(LSPROXY_PRODUCT_NAME);
    char versionStr[64];
    snprintf(versionStr, sizeof(versionStr), "v%s", LSPROXY_VERSION_STRING);
    CenteredText(versionStr, true);
    ImGui::Dummy(ImVec2(0, S(6)));
    CenteredText("An addon manager for Lossless Scaling: shaders, behaviour, and anything else an addon adds.", true);

    // support
    ImGui::Dummy(ImVec2(0, S(16)));
    const float bw = ImGui::CalcTextSize("Support on Ko-fi").x + ImGui::GetFontSize() * 4.0f;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (ImGui::GetContentRegionAvail().x - bw) * 0.5f);
    if (lsp::Button("Support on Ko-fi", lsp::icons::kHeart, lsp::ButtonKind::Primary, ImVec2(bw, ImGui::GetFrameHeight() * 1.25f)))
        widgets::OpenKofi();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("If this is useful, a Ko-fi helps a lot. Opens your browser.");

    ImGui::Dummy(ImVec2(0, S(18)));
    lsp::SectionLabel("Build");
    ImGui::TextDisabled("Addon API");   ImGui::SameLine(S(120)); ImGui::Text("v%s", LSPROXY_API_VERSION_STRING);
    ImGui::TextDisabled("ImGui");        ImGui::SameLine(S(120)); ImGui::Text("%s", ImGui::GetVersion());
    ImGui::TextDisabled("Built");        ImGui::SameLine(S(120)); ImGui::Text("%s %s", __DATE__, __TIME__);

    ImGui::Dummy(ImVec2(0, S(14)));
    lsp::SectionLabel("Good to know");
    ImGui::TextWrapped("Closed this window? It is still running in the notification area (tray). Click the icon, or use the hotkey set in "
                       "Settings, to open it again.");
    ImGui::Dummy(ImVec2(0, S(4)));
    ImGui::TextWrapped("Hold Ctrl and scroll over any slider to fine-tune it; double-click a slider to put it back to its default.");

    ImGui::Dummy(ImVec2(0, S(14)));
    lsp::SectionLabel("Thanks");
    ImGui::TextWrapped("This began as LosslessProxy by FrankBarretta (MIT), and we are grateful for it. About a third of this program's code "
                       "is still theirs: mainly the part that loads into Lossless Scaling and hooks DirectX 11 and its shaders, plus the "
                       "ReShade and Windowed addons, which we fixed and reworked. Everything else was written or rewritten since: how "
                       "addons are found, checked, loaded and switched, the settings file, the event system, the window, tabs, "
                       "Performance, backup, install and remove, the tray and the shared look.");
    ImGui::Dummy(ImVec2(0, S(4)));
    if (lsp::Button("The original project", lsp::icons::kExternal, lsp::ButtonKind::Flat)) OpenUrl("https://github.com/FrankBarretta/LosslessProxy");
    ImGui::Dummy(ImVec2(0, S(10)));
    ImGui::TextDisabled("Also: Dear ImGui and nlohmann/json (MIT), stb_image (public domain), MinHook (BSD, in the Windowed addon).");
    ImGui::TextDisabled("Icon shapes are drawn in the manner of the Lucide set (ISC).");
    ImGui::TextDisabled("Lossless Scaling belongs to its author; this is an unofficial add-on for it.");

    ImGui::Dummy(ImVec2(0, S(14)));
    lsp::SectionLabel("For addon authors");
    ImGui::TextDisabled("Include the SDK headers (addon_sdk.h). lsp_widgets.h gives an addon this same look: the palette,");
    ImGui::TextDisabled("sliders with defaults, section headers, buttons and SVG icons.");
}

} // namespace lsproxy
