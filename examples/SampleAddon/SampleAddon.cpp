// SampleAddon: the smallest addon that does everything an addon usually does. Copy this folder, rename things, and build on it.
//
//   * the required exports (and the optional ones for a name, a version and a settings panel)
//   * reading and writing its own settings, which the manager keeps in addons\config.json under this addon's id
//   * a settings panel drawn in the manager's own look (widgets.h)
//   * a live status line and a number for the Performance tab
//
// It needs no GPU access and starts no threads. See docs/addon-authors.md for the rules, and docs/api-compatibility.md for what stays stable.
#include <eam/addon_sdk.h>
#include <eam/widgets.h>
#include <eam/version.h>
#include <windows.h>
#include <cstdio>
#include <string>

namespace {

// The id the manager knows this addon by is the name of its folder under addons\ ; use the same text here, since settings and status are kept per id.
const char* const kId = "SampleAddon";

IHost* g_host = nullptr;

// Settings, with their defaults
struct Settings {
    float volume = 0.5f;          // a slider
    char greeting[64] = "Hello";  // a text box
    int clicks = 0;               // a counter that survives restarts
};
Settings g_s;
const float kDefaultVolume = 0.5f;

void Load() {
    // Values are stored as text. GetConfig returns a pointer that stays valid for a while; copy what you keep.
    g_s.volume = static_cast<float>(atof(g_host->GetConfig(kId, "volume", "0.5")));
    snprintf(g_s.greeting, sizeof g_s.greeting, "%s", g_host->GetConfig(kId, "greeting", "Hello"));
    g_s.clicks = atoi(g_host->GetConfig(kId, "clicks", "0"));
}

void Save() {
    char b[32];
    snprintf(b, sizeof b, "%.3f", g_s.volume);
    g_host->SetConfig(kId, "volume", b);
    g_host->SetConfig(kId, "greeting", g_s.greeting);
    snprintf(b, sizeof b, "%d", g_s.clicks);
    g_host->SetConfig(kId, "clicks", b);
    g_host->SaveConfig();   // written to config.json now (atomically); without this it is written at the next save
}

void ShowStatus() {
    // One line for the addon's card and the status bar. Refresh it while it is true: a status that is not refreshed for a few seconds is hidden.
    char b[96];
    snprintf(b, sizeof b, "%s: volume %d%%, clicked %d times", g_s.greeting, static_cast<int>(g_s.volume * 100.0f + 0.5f), g_s.clicks);
    g_host->SetStatus(kId, b, 1);   // 1 = green; 0 grey, 2 amber, 3 red
}

} // namespace

// ---- required exports -------------------------------------------------------------------------------------------------------------------------

EAM_EXPORT void AddonInitialize(IHost* host, ImGuiContext* ctx, void* allocFunc, void* freeFunc, void* userData) {
    // The manager and every addon share one Dear ImGui context: use it, and its allocator, instead of making your own.
    ImGui::SetCurrentContext(ctx);
    ImGui::SetAllocatorFunctions(reinterpret_cast<ImGuiMemAllocFunc>(allocFunc), reinterpret_cast<ImGuiMemFreeFunc>(freeFunc), userData);
    eam::ui::InitAddonImGui();   // needed because this DLL carries its own copy of ImGui's code (see the CMakeLists)
    g_host = host;
    Load();
    host->Log(EAM_LOG_INFO, "SampleAddon started");
    ShowStatus();
}

EAM_EXPORT void AddonShutdown() {
    if (g_host) { Save(); g_host->SetStatus(kId, "", 0); }   // an empty status clears the line
    g_host = nullptr;
}

EAM_EXPORT uint32_t GetAddonCapabilities() { return EAM_CAP_HAS_SETTINGS; }   // we have a settings panel; no GPU access, no restart needed

// ---- optional exports: what the manager shows on the card (addon.json can say the same; the manifest wins) --------------------------------------

EAM_EXPORT const char* GetAddonName() { return "Sample Addon"; }
EAM_EXPORT const char* GetAddonVersion() { return "1.0.0"; }
EAM_EXPORT const char* GetAddonAuthor() { return "Echo-Storm"; }
EAM_EXPORT const char* GetAddonDescription() { return "A small example addon: settings, a settings panel, a status line and a metric."; }

// ---- the settings panel: called by the manager, once per frame, while this addon's page is open -----------------------------------------------

EAM_EXPORT void AddonRenderSettings() {
    if (!g_host) return;
    bool changed = false;

    eam::ui::SectionLabel("Greeting");
    ImGui::SetNextItemWidth(220.0f);
    changed |= ImGui::InputText("##greeting", g_s.greeting, sizeof g_s.greeting);
    ImGui::SameLine();
    ImGui::TextDisabled("what the status line says");

    eam::ui::SectionLabel("Volume");
    // A slider with a default: it shows a tick where the default is, and a double-click puts the value back.
    changed |= eam::ui::SliderFloat("Volume", &g_s.volume, 0.0f, 1.0f, "%.2f", 0, &kDefaultVolume);

    eam::ui::SectionLabel("Counter");
    if (eam::ui::Button("Count a click", eam::ui::icons::kPlus, eam::ui::ButtonKind::Primary)) {
        ++g_s.clicks;
        changed = true;
        g_host->Log(EAM_LOG_INFO, "clicked");
    }
    ImGui::SameLine();
    if (eam::ui::Button("Reset", eam::ui::icons::kReset)) { g_s.clicks = 0; changed = true; }

    // A number for the Performance tab: one series per (addon, key). Cheap enough to publish every frame.
    g_host->PublishMetric(kId, "clicks", g_s.clicks, "clicks");

    if (changed) { Save(); ShowStatus(); }
}
