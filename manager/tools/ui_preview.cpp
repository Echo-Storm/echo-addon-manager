// Offscreen preview of the manager's look: builds the real theme (gui_style.cpp), the real toggle switch, addon card, status bar
// and About tab, the shared lsp_widgets.h controls (sliders with defaults, section headers, buttons, SVG icons), and renders them to
// BMP files. No window is created and nothing is drawn on anyone's screen.
//   ui_preview.exe [outDir] [scale] [addon.dll ...]
// Each addon DLL is loaded against a fake host and its settings panel is rendered to preview_<dll name>.bmp, so a panel can
// be looked at (and proven to load against this ImGui) without running Lossless Scaling.
#include <windows.h>
#include <cstdio>
#include <string>
#include <map>
#include <filesystem>
#include <vector>
#include "imgui.h"
#include "tools/ui_shot.h"
#include "src/gui/gui_style.h"
#include "src/gui/gui_scale.h"
#include "src/gui/widgets/toggle_switch.h"
#include "src/gui/widgets/addon_card.h"
#include "src/gui/widgets/toast.h"
#include "src/gui/widgets/status_bar.h"
#include "src/gui/widgets/empty_state.h"
#include "src/gui/window/status_text.h"
#include "src/gui/tabs/tab_about.h"
#include "src/gui/tabs/tab_features.h"
#include "src/config/config_manager.h"
#include "src/gui/tabs/tab_logs.h"
#include "src/log/logger.h"
#include "lsproxy/version.h"
#include "src/gui/tabs/tab_performance.h"
#include "src/gui/tabs/tab_settings.h"
#include "src/gui/gui_manager.h"
#include "src/addon/addon_manager.h"
#include "src/host/metrics.h"
#include "src/host/gpu_stats.h"
#include "lsproxy/lsp_widgets.h"
#include "lsproxy/addon_sdk.h"

using namespace lsproxy;

// The preview does not link the real manager window or addon manager: the few things the Settings tab touches are stubbed.
void GuiManager::ApplyHotkey() {}
const char* GuiManager::HotkeyStatus() { return "registered"; }
void GuiManager::RequestUserScale() {}
std::vector<AddonInfo>& AddonManager::GetAddons() { static std::vector<AddonInfo> v; return v; }

static void Shell(const char* active, const std::string& status, const std::function<void()>& body) {
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("##Main", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
    ImGui::BeginChild("##content", ImVec2(0, -widgets::StatusBarHeight() - 2.0f), false, ImGuiWindowFlags_NoBackground);
    if (ImGui::BeginTabBar("##MainTabs")) {
        for (const char* t : { "Addons", "Features", "Performance", "Settings", "Logs", "About" }) {
            ImGuiTabItemFlags f = (std::string(t) == active) ? ImGuiTabItemFlags_SetSelected : 0;
            if (ImGui::BeginTabItem(t, nullptr, f)) { if (std::string(t) == active) body(); ImGui::EndTabItem(); }
        }
        ImGui::EndTabBar();
    }
    ImGui::EndChild();
    widgets::StatusBar(status);
    ImGui::End();
    widgets::ToastRender();
}

struct FakeHost : IHost {
    std::map<std::string, std::string> cfg;
    void Log(LsProxyLogLevel, const char* m) override { printf("[addon] %s\n", m); }
    const char* GetConfig(const char* id, const char* k, const char* d) override { auto it = cfg.find(std::string(id) + "/" + k); return it == cfg.end() ? d : it->second.c_str(); }
    void SetConfig(const char* id, const char* k, const char* v) override { cfg[std::string(id) + "/" + k] = v; }
    void SaveConfig() override {}
    uint32_t GetHostVersion() override { return 0x10000; }
    void SubscribeEvent(uint32_t, LsProxyEventCallback, void*) override {}
    void UnsubscribeEvent(uint32_t, LsProxyEventCallback) override {}
    void PublishEvent(uint32_t, const void*, uint32_t) override {}
    void* GetD3D11Device() override { return nullptr; }
    void* GetD3D11DeviceContext() override { return nullptr; }
    void SetPreDispatchCallback(LsProxyPreDispatchCallback, void*) override {}
    void SetPostDispatchCallback(LsProxyPostDispatchCallback, void*) override {}
    void* GetCurrentComputeShader() override { return nullptr; }
    uint32_t GetDispatchCount() override { return 0; }
    void SetStatus(const char*, const char*, int) override {}
    void PublishMetric(const char*, const char*, double, const char*) override {}
};

int main(int argc, char** argv) {
    std::string out = argc > 1 ? argv[1] : ".";
    const float scale = argc > 2 ? (float)atof(argv[2]) : 1.0f;
    ImGui::CreateContext();
    LoadUiFonts("", "");
    ApplyUiScale(scale);

    UiShot shot;
    const int W = (int)(620 * scale), H = (int)(1000 * scale);
    if (!shot.Init(W, H)) { printf("device init failed\n"); return 1; }

    // LSP_PREVIEW_CLEAN=1 draws the tidy scene used for the README screenshots: no toast, only the first addon on, no error chips.
    char* cleanEnv = nullptr; size_t cleanLen = 0; _dupenv_s(&cleanEnv, &cleanLen, "LSP_PREVIEW_CLEAN");
    const bool clean = cleanEnv != nullptr; free(cleanEnv);
    AddonInfo a, b, c;
    a.id = "DLSS5NR01"; a.manifest.name = "DLSS 5 Neural Rendering"; a.manifest.version = "0.7.0"; a.manifest.author = "andreiday / Echo-Storm"; a.hModule = (HMODULE)1; a.enabled = true;
    b.id = "sample-a"; b.manifest.name = "Sample addon A"; b.manifest.version = "1.0.0"; b.manifest.author = "Someone"; b.enabled = !clean; b.capabilities = clean ? 0 : LSPROXY_CAP_REQUIRES_RESTART;   // enabled, needs a restart
    c.id = "sample-b"; c.manifest.name = "Sample addon B"; c.manifest.version = "1.0.0"; c.manifest.author = "Someone else"; c.enabled = !clean; c.faulted = !clean;   // shows the ERROR chip

    float model = 0.5f, sharpen = 0.0f, vib = 1.2f, blend = 0.72f, gamma = 1.0f; int passes = 1, grain = 2; bool sw = true, sw2 = false; int sel = 0;
    const float dModel = 0.35f, dSharpen = 0.0f, dVib = 0.0f, dBlend = 1.0f, dGamma = 1.0f; const int dPasses = 1, dGrain = 1;
    if (!clean) widgets::ToastShow("Installed 'Cool Addon' (switched off). Turn it on with its switch.", widgets::ToastType::Success, 1000.0f);
    const std::string status = window::StatusCounts(clean ? 1 : 3, 1);   // the tidy scene has just Neural Rendering, as a real install does

    // live data for the cards and the Performance tab: 20 s of a game near 60 fps with a few hitches, a model at ~6.6 ms, a GPU at its cap
    {
        Metrics& M = Metrics::Instance();
        const double now = M.Now();
        unsigned seed = 12345;
        auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return (float)(seed >> 8) / (float)(1 << 24); };
        for (double t = now - 20.0; t <= now; t += 1.0 / 60.0) {
            float ms = 16.7f + (rnd() - 0.5f) * 1.6f;
            if (rnd() > 0.985f) ms += 9.0f + rnd() * 14.0f;   // a hitch now and then
            M.PublishAt("DLSS5NR01", "frame_ms", ms, "ms", t);
        }
        for (double t = now - 20.0; t <= now; t += 0.2) {
            M.PublishAt("DLSS5NR01", "model_ms", 6.6f + (rnd() - 0.5f) * 0.7f, "ms", t);
            M.PublishAt("DLSS5NR01", "keepup_pct", 98.0f + rnd() * 2.0f, "%", t);
        }
        for (double t = now - 20.0; t <= now; t += 0.5) {
            const float u = 90.0f + (rnd() - 0.3f) * 9.0f;
            M.PublishAt("system", "gpu_util", u > 100 ? 100 : u, "%", t);
            M.PublishAt("system", "gpu_power_w", 281.0f + (rnd() - 0.5f) * 4.0f, "W", t);
        }
        M.SetStatus("DLSS5NR01", "Running, model 6.6 ms, keeps up 99%", 1);
        GpuStats::Snapshot g; g.name = "NVIDIA GeForce RTX 4070 Ti SUPER"; g.driver = "616.92"; g.deviceCount = 1; g.utilGpu = 97; g.utilMem = 44;
        g.clockGraphics = 2610; g.clockMem = 10501; g.tempC = 68; g.powerW = 283.4; g.powerLimitW = 285.0; g.vramUsedMB = 13132; g.vramTotalMB = 16376; g.throttle = 0x4;
        GpuStats::Instance().InjectForPreview(g);
    }

    // 1. Addons tab
    shot.Frame([&] {
        Shell("Addons", status, [&] {
            ImGui::Dummy(ImVec2(0, 5));
            lsp::Button("Install addon", lsp::icons::kDownload, lsp::ButtonKind::Primary); ImGui::SameLine();
            lsp::Button("Open addons folder", lsp::icons::kFolder);
            ImGui::Dummy(ImVec2(0, 6));
            if (widgets::AddonCard(a, 0, sel == 0)) sel = 0;
            if (!clean && widgets::AddonCard(b, 1, sel == 1)) sel = 1;
            if (!clean && widgets::AddonCard(c, 2, sel == 2)) sel = 2;
        });
    }, 12);
    shot.Save((out + "/preview_addons.bmp").c_str());

    // 1b. The Remove confirmation over the Addons tab, and the empty state
    shot.Frame([&] {
        Shell("Addons", status, [&] {
            ImGui::Dummy(ImVec2(0, 5));
            lsp::Button("Install addon", lsp::icons::kDownload, lsp::ButtonKind::Primary); ImGui::SameLine();
            lsp::Button("Open addons folder", lsp::icons::kFolder);
            ImGui::Dummy(ImVec2(0, 6));
            widgets::AddonCard(a, 0, true); widgets::AddonCard(b, 1, false);
            static bool opened = false;
            if (!opened) { ImGui::OpenPopup("Remove addon"); opened = true; }
            if (ImGui::BeginPopupModal("Remove addon", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 32.0f);
                ImGui::TextWrapped("Remove %s?", a.manifest.name.c_str());
                ImGui::Dummy(ImVec2(0, 4));
                ImGui::TextWrapped("It is switched off and its folder is moved to addons/.removed (nothing is erased; move the folder back to restore it). Its settings are kept, so installing it again brings them back.");
                ImGui::PopTextWrapPos();
                ImGui::Dummy(ImVec2(0, 6));
                lsp::Button("Remove", lsp::icons::kTrash, lsp::ButtonKind::Danger); ImGui::SameLine(); lsp::Button("Cancel", lsp::icons::kClose);
                ImGui::EndPopup();
            }
        });
    }, 14);
    shot.Save((out + "/preview_remove.bmp").c_str());
    shot.Frame([&] {
        Shell("Addons", status, [&] {
            ImGui::Dummy(ImVec2(0, 5));
            lsp::Button("Install addon", lsp::icons::kDownload, lsp::ButtonKind::Primary); ImGui::SameLine();
            lsp::Button("Open addons folder", lsp::icons::kFolder);
            ImGui::Dummy(ImVec2(0, 6));
            ImGui::BeginChild("AddonList", ImVec2(260 * ImGui::GetStyle().FontScaleDpi, -1), false);
            widgets::EmptyState(260 * ImGui::GetStyle().FontScaleDpi, "No addons installed yet", "Use Install addon above, or drop a folder, .zip or .dll onto this window.");
            ImGui::EndChild();
        });
    }, 12);
    shot.Save((out + "/preview_empty.bmp").c_str());

    // 2. Controls: section headers, sliders (default tick, modified ring, hover), buttons, icons
    ImVec2 hoverAt(-1, -1);
    auto controls = [&] {
        Shell("Settings", status, [&] {
            ImGui::Dummy(ImVec2(0, 5));
            if (lsp::SectionHeader("Quality and performance", true)) {
                lsp::SliderFloat("Model resolution", &model, 0.25f, 1.0f, "%.2f x the frame", 0, &dModel);
                lsp::SliderInt("Model passes", &passes, 1, 4, "%d", 0, &dPasses);
                lsp::SliderFloat("Blend amount", &blend, 0.0f, 2.0f, "%.2f", 0, &dBlend);
                hoverAt = ImVec2(ImGui::GetItemRectMin().x + 160.0f * ImGui::GetStyle().FontScaleDpi, ImGui::GetItemRectMin().y + ImGui::GetFrameHeight() * 0.5f);
                lsp::SliderFloat("Sharpen", &sharpen, 0.0f, 1.0f, sharpen <= 0.001f ? "off" : "%.2f", 0, &dSharpen);
            }
            if (lsp::SectionHeader("Picture (sharpness, tone, colour, grain)", true)) {
                lsp::SliderFloat("Vibrance", &vib, 0.0f, 2.0f, "%.2f", 0, &dVib);
                lsp::SliderFloat("Gamma", &gamma, 0.5f, 2.0f, fabsf(gamma - 1.0f) < 0.002f ? "unchanged" : "%.2f", 0, &dGamma);
                lsp::SliderInt("Grain size", &grain, 1, 4, "%d px", 0, &dGrain);
            }
            lsp::SectionHeader("Keep the HUD untouched");
            lsp::SectionHeader("Games (a look per program)");
            ImGui::Dummy(ImVec2(0, 8));
            lsp::SectionLabel("Buttons and icons");
            lsp::Button("Save preset", lsp::icons::kCheck, lsp::ButtonKind::Primary); ImGui::SameLine();
            lsp::Button("Restore defaults", lsp::icons::kReset); ImGui::SameLine();
            lsp::Button("Remove", lsp::icons::kClose, lsp::ButtonKind::Danger); ImGui::SameLine();
            lsp::Button("Learn more", lsp::icons::kExternal, lsp::ButtonKind::Flat);
            ImGui::Dummy(ImVec2(0, 4));
            for (const char* ic : { lsp::icons::kHeart, lsp::icons::kPlus, lsp::icons::kDownload, lsp::icons::kFolder, lsp::icons::kReset, lsp::icons::kCheck,
                                    lsp::icons::kClose, lsp::icons::kExternal, lsp::icons::kInfo, lsp::icons::kCoffee, lsp::icons::kPower, lsp::icons::kPackage,
                                    lsp::icons::kSliders, lsp::icons::kEye }) {
                ImGui::PushID(ic); lsp::IconButton("##i", ic, ImGui::GetFrameHeight() * 1.2f); ImGui::PopID(); ImGui::SameLine();
            }
            ImGui::NewLine();
            {   // filled hearts at three sizes (the Ko-fi heart is a filled path)
                const ImVec2 hp = ImGui::GetCursorScreenPos();
                for (int i = 0; i < 3; ++i) {
                    const float sz = i == 0 ? 15.0f : (i == 1 ? 32.0f : 64.0f) ;
                    lsp::svg::Draw(ImGui::GetWindowDrawList(), lsp::icons::kHeart, ImVec2(hp.x + i * 90.0f, hp.y), sz * ImGui::GetStyle().FontScaleDpi, lsp::theme::U(lsp::theme::kAccent), 0.0f, lsp::theme::U(lsp::theme::kAccent));
                }
                ImGui::Dummy(ImVec2(10, 70.0f * ImGui::GetStyle().FontScaleDpi));
            }
            widgets::ToggleSwitch("##t1", &sw); ImGui::SameLine(); ImGui::TextUnformatted("Enabled");
            widgets::ToggleSwitch("##t2", &sw2); ImGui::SameLine(); ImGui::TextUnformatted("Compare view");
        });
    };
    shot.Frame(controls, 12);
    shot.Frame(controls, 6, hoverAt.x, hoverAt.y);   // second pass: the pointer rests on "Blend amount"
    shot.Save((out + "/preview_settings.bmp").c_str());

    // 2b. Performance tab (the real one, fed the synthetic data above)
    {
        ImGuiStyle& st = ImGui::GetStyle(); (void)st;
        GpuStats::Snapshot g = GpuStats::Instance().Get(); GpuStats::Instance().InjectForPreview(g);   // keep it fresh
        shot.Frame([&] { GpuStats::Instance().InjectForPreview(g); Shell("Performance", status, [&] { RenderTabPerformance(); }); }, 12);
        shot.Save((out + "/preview_performance.bmp").c_str());
    }

    // 2c. Settings tab (the real one; the config is empty, so everything shows its defaults)
    shot.Frame([&] { Shell("Settings", status, [&] { RenderTabSettings(nullptr); }); }, 12);
    shot.Save((out + "/preview_settings_tab.bmp").c_str());

    // 3. Features tab (the real one): ReShade passthrough on, so its options show; the tidy scene leaves Windowed off, the other one turns it on
    //    too, which shows the "restart to apply" label (the hooks are not in, in a preview)
    {
        ConfigManager& cfg = ConfigManager::Instance();
        cfg.Load((std::filesystem::path(out) / "preview_config.json").wstring());
        cfg.SetAddonEnabled("LSP-ReShade", true);
        cfg.SetAddonEnabled("LSP-Windowed", !clean);
        shot.Frame([&] { Shell("Features", status, [&] { RenderTabFeatures(); }); }, 12);
        shot.Save((out + "/preview_features.bmp").c_str());
    }

    // 3b. About tab (the real one)
    shot.Frame([&] { Shell("About", status, [&] { RenderTabAbout(); }); }, 12);
    shot.Save((out + "/preview_about.bmp").c_str());

    // 4. Logs tab, with a few typical lines (sample data, like the rest of the preview)
    {
        LOG_INFO("Core", "%s v%s starting...", LSPROXY_PRODUCT_NAME, LSPROXY_VERSION_STRING);
        LOG_INFO("GUI", "Tray icon added");
        LOG_INFO("AddonManager", "Found 1 addons in addons");
        LOG_INFO("AddonManager", "Loaded 'DLSS5NR01' %s", "0.7.0");
        LOG_INFO("Features", "ReShade input passthrough is on");
        LOG_INFO("GUI", "Hotkey Ctrl+Shift+F12 registered");
        LOG_INFO("DLSS5NR01", "Engine started on the LSFG device");
        shot.Frame([&] { Shell("Logs", status, [&] { RenderTabLogs(); }); }, 12);
        shot.Save((out + "/preview_logs.bmp").c_str());
    }

    for (int i = 3; i < argc; ++i) {   // addon panels
        HMODULE h = LoadLibraryA(argv[i]);
        if (!h) { printf("cannot load %s (error %lu)\n", argv[i], GetLastError()); continue; }
        using Init_t = void (*)(IHost*, ImGuiContext*, void*, void*, void*); using Void_t = void (*)(); using Str_t = const char* (*)();
        auto init = (Init_t)GetProcAddress(h, "AddonInitialize"); auto render = (Void_t)GetProcAddress(h, "AddonRenderSettings"); auto name = (Str_t)GetProcAddress(h, "GetAddonName");
        auto shut = (Void_t)GetProcAddress(h, "AddonShutdown");
        if (!init || !render) { printf("%s has no panel exports\n", argv[i]); continue; }
        static FakeHost host;
        ImGuiMemAllocFunc af; ImGuiMemFreeFunc ff; void* ud; ImGui::GetAllocatorFunctions(&af, &ff, &ud);
        init(&host, ImGui::GetCurrentContext(), (void*)af, (void*)ff, ud);
        const std::string title = name ? name() : "addon";
        shot.Frame([&] {
            ImGui::SetNextWindowPos(ImVec2(0, 0)); ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
            ImGui::Begin("##addon", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
            lsp::SectionLabel(title.c_str()); render(); ImGui::End();
        }, 12);
        std::string base = argv[i]; const size_t sl = base.find_last_of("/\\"); if (sl != std::string::npos) base = base.substr(sl + 1);
        const std::string file = out + "/preview_" + base + ".bmp";
        printf("%s -> %s: %s\n", title.c_str(), file.c_str(), shot.Save(file.c_str()) ? "written" : "FAILED");
        if (shut) shut();
    }
    shot.Shutdown();
    printf("wrote previews to %s\n", out.c_str());
    return 0;
}
