#include "gui_style.h"
#include "gui_scale.h"
#include "imgui.h"
#include "eam/widgets.h"
#include <windows.h>
#include <string>

namespace eam {

void SetupModernStyle() {
    eam::ui::theme::ApplyStyle(ImGui::GetStyle());
}

// ---------------------------------------------------------------------------------------
// Display scale and fonts
// ---------------------------------------------------------------------------------------

static float g_uiScale = 1.0f;
static ImFont* g_monoFont = nullptr;
static ImFont* g_titleFont = nullptr;   // Segoe UI Semibold: the header's title and the addon names

float UiScale() { return g_uiScale; }
void SetUiScale(float scale) {
    g_uiScale = scale < 0.5f ? 0.5f : (scale > 4.0f ? 4.0f : scale);
}

void ApplyUiScale(float dpiScale) {
    SetUiScale(dpiScale);
    ImGuiStyle& style = ImGui::GetStyle();
    style = ImGuiStyle();
    SetupModernStyle();
    style.ScaleAllSizes(g_uiScale);
    // Fonts are rasterised at FontSizeBase * FontScaleDpi, so text stays sharp at any scale.
    style.FontSizeBase = kUiFontSize;
    style.FontScaleDpi = g_uiScale;
}

static ImFont* TryAddFont(ImGuiIO& io, const std::string& path) {
    if (path.empty()) return nullptr;
    DWORD attrs = GetFileAttributesA(path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY)) return nullptr;
    return io.Fonts->AddFontFromFileTTF(path.c_str());
}

void LoadUiFonts(const std::string& uiFontPath, const std::string& monoFontPath) {
    ImGuiIO& io = ImGui::GetIO();

    char winDir[MAX_PATH] = {};
    const std::string fontsDir = GetWindowsDirectoryA(winDir, MAX_PATH)
        ? std::string(winDir) + "\\Fonts\\" : std::string("C:\\Windows\\Fonts\\");

    ImFont* ui = TryAddFont(io, uiFontPath);
    for (const char* name : { "segoeui.ttf", "tahoma.ttf", "arial.ttf" }) {
        if (ui) break;
        ui = TryAddFont(io, fontsDir + name);
    }
    if (!ui) ui = io.Fonts->AddFontDefault();
    io.FontDefault = ui;

    g_titleFont = TryAddFont(io, fontsDir + "seguisb.ttf");
    if (!g_titleFont) g_titleFont = ui;

    g_monoFont = TryAddFont(io, monoFontPath);
    for (const char* name : { "CascadiaMono.ttf", "consola.ttf", "cour.ttf" }) {
        if (g_monoFont) break;
        g_monoFont = TryAddFont(io, fontsDir + name);
    }
}

ImFont* MonoFont() { return g_monoFont; }
ImFont* TitleFont() { return g_titleFont; }

} // namespace eam
