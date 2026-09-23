// Offline test of the sample addon (examples/SampleAddon): the addon that docs/addon-authors.md points people to must keep working against the manager
// as it is now. It is copied into a throw-away addons folder, found, loaded and started by the real AddonManager, its settings panel is drawn for a frame
// and its settings are checked: what it publishes (status, a metric), what it reads from the settings file and what it writes back.
//   eam_sampletest.exe        (SampleAddon.dll must sit beside it; the build puts it there; addon.json comes from examples/SampleAddon)
#include "src/addon/addon_manager.h"
#include "src/config/config_manager.h"
#include "src/host/host_impl.h"
#include "src/host/metrics.h"
#include "src/log/logger.h"
#include "imgui.h"
#include <windows.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using namespace eam;

static int g_failed = 0;
static void Check(const char* what, bool ok, const std::string& detail = "") {
    printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : "  (", detail.empty() ? "" : (detail + ")").c_str());
    if (!ok) ++g_failed;
}

static fs::path ExeDir() {
    wchar_t b[MAX_PATH];
    GetModuleFileNameW(nullptr, b, MAX_PATH);
    return fs::path(b).parent_path();
}

static AddonInfo* Find(AddonManager& m, const std::string& id) {
    for (auto& a : m.GetAddons()) if (a.id == id) return &a;
    return nullptr;
}

// Draw one frame of ImGui with no window and no GPU, calling the addon's panel inside it
static void DrawPanel(AddonManager& mgr, int index) {
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;   // no imgui.ini in the folder the test happens to run from
    io.LogFilename = nullptr;
    io.DisplaySize = ImVec2(900, 700);
    io.DeltaTime = 1.0f / 60.0f;
    unsigned char* px = nullptr;
    int w = 0, h = 0;
    io.Fonts->GetTexDataAsRGBA32(&px, &w, &h);
    ImGui::NewFrame();
    ImGui::SetNextWindowSize(ImVec2(800, 600));
    ImGui::Begin("panel");
    mgr.RenderAddonSettings(index);
    ImGui::End();
    ImGui::EndFrame();
}

int main() {
    const fs::path dll = ExeDir() / "SampleAddon.dll";
    const fs::path manifest = fs::path(SAMPLE_ADDON_DIR) / "addon.json";
    if (!fs::exists(dll) || !fs::exists(manifest)) { printf("FAIL  SampleAddon.dll or its addon.json is missing (%ls, %ls)\n", dll.c_str(), manifest.c_str()); return 1; }

    const fs::path T = fs::temp_directory_path() / ("eam_sampletest_" + std::to_string(GetCurrentProcessId()));
    std::error_code ec;
    fs::remove_all(T, ec);
    const fs::path A = T / "addons";
    fs::create_directories(A / "SampleAddon");
    fs::copy_file(dll, A / "SampleAddon" / "SampleAddon.dll");
    fs::copy_file(manifest, A / "SampleAddon" / "addon.json");
    Logger::Instance().Init((T / "test.log").wstring());
    // settings from an earlier session: a changed greeting, a volume, and a counter
    { std::ofstream f(A / "config.json", std::ios::binary); f << R"({"addons":{"SampleAddon":{"greeting":"Howdy","volume":"0.25","clicks":"7"}},"global":{"security_level":0}})"; }
    ConfigManager::Instance().Load((A / "config.json").wstring());
    Metrics::Instance().Clear();

    {
        HostImpl host;
        AddonManager mgr(&host, A.wstring());
        ImGuiContext* ctx = ImGui::CreateContext();
        ImGui::SetCurrentContext(ctx);

        mgr.ScanAddons();
        AddonInfo* a = Find(mgr, "SampleAddon");
        Check("the manager finds the sample addon", a != nullptr);
        if (!a) return 1;
        Check("its addon.json is read (name, version, author, tags)", a->manifest.parsed && a->manifest.name == "Sample Addon" && a->manifest.version == "1.0.0" &&
              a->manifest.author == "Echo Addon Manager project" && a->manifest.tags.size() == 1 && a->manifest.tags[0] == "example");
        Check("it asks for API 1.0.0, which this manager provides", a->manifest.minHostVersion == "1.0.0" || a->manifest.minHostVersion.empty(), a->manifest.minHostVersion);
        mgr.LoadAddons();
        Check("it loads", a->IsLoaded(), a->errorMessage);
        mgr.InitializeAddons(ctx);
        Check("it starts without a fault", !a->faulted && a->errorMessage.empty(), a->errorMessage);
        Check("it reports having a settings panel and needing no restart", a->exports.renderSettings != nullptr && (a->capabilities & EAM_CAP_HAS_SETTINGS) != 0 && !a->RequiresRestart());

        const Metrics::Status st = Metrics::Instance().GetStatus("SampleAddon");
        Check("it publishes a green status built from the settings it read (greeting, volume, counter)", st.text == "Howdy: volume 25%, clicked 7 times" && st.level == 1, st.text);

        DrawPanel(mgr, 0);
        const Metrics::Series clicks = Metrics::Instance().Get("SampleAddon", "clicks", 30.0);
        Check("drawing its settings panel works and publishes the counter as a metric", !clicks.samples.empty() && clicks.last() == 7.0f && clicks.unit == "clicks", std::to_string(clicks.samples.size()));
        Check("...without changing anything by itself", std::string(host.GetConfig("SampleAddon", "clicks", "?")) == "7" && std::string(host.GetConfig("SampleAddon", "greeting", "?")) == "Howdy");

        mgr.UnloadAddons();
        Check("shutting it down clears its status", Metrics::Instance().GetStatus("SampleAddon").text.empty());
        ImGui::DestroyContext(ctx);
    }
    Check("its settings are still in the settings file after it was shut down", [&] {
        std::ifstream f(A / "config.json", std::ios::binary);
        const std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        return s.find("\"greeting\"") != std::string::npos && s.find("Howdy") != std::string::npos && s.find("\"clicks\"") != std::string::npos;
    }());

    {   // a first run, with no settings at all: the defaults
        fs::remove(A / "config.json");
        { std::ofstream f(A / "config.json", std::ios::binary); f << R"({"addons":{},"global":{"security_level":0}})"; }
        ConfigManager::Instance().Load((A / "config.json").wstring());
        HostImpl host;
        AddonManager mgr(&host, A.wstring());
        ImGuiContext* ctx = ImGui::CreateContext();
        ImGui::SetCurrentContext(ctx);
        mgr.ScanAddons();
        mgr.LoadAddons();
        mgr.InitializeAddons(ctx);
        Check("with no saved settings it starts with its defaults", Metrics::Instance().GetStatus("SampleAddon").text == "Hello: volume 50%, clicked 0 times", Metrics::Instance().GetStatus("SampleAddon").text);
        mgr.UnloadAddons();
        ImGui::DestroyContext(ctx);
    }

    Logger::Instance().Shutdown();
    fs::remove_all(T, ec);
    printf("\n%s (%d failed)\n", g_failed ? "SAMPLE ADDON TEST FAILED" : "SAMPLE ADDON TEST PASSED", g_failed);
    return g_failed ? 1 : 0;
}
