// Offline test of the manager's addon handling: finding addons in a folder, reading their manifests, loading, initialising, switching on and
// off, removing and installing them, the security levels, and a faulting addon. It runs against a throw-away folder in %TEMP% with copies of
// a small test addon (test_addon.cpp), and touches no game and no Lossless Scaling install.
//   lsproxy_coretest.exe        (lsproxy_testaddon.dll must sit beside it; the build puts it there)
#include "src/addon/addon_dependency.h"
#include "src/addon/addon_manager.h"
#include "src/addon/addon_security.h"
#include "src/config/config_manager.h"
#include "src/event/event_system.h"
#include "src/host/gpu_stats.h"
#include "src/host/host_impl.h"
#include "src/host/metrics.h"
#include "src/log/logger.h"
#include "lsproxy/version.h"
#include "imgui.h"
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
using namespace lsproxy;

static int g_failed = 0;
static void Check(const char* what, bool ok, const std::string& detail = "") {
    printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : "  (", detail.empty() ? "" : (detail + ")").c_str());
    if (!ok) ++g_failed;
}

static void WriteFile(const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary);
    f << text;
}

static fs::path ExeDir() {
    wchar_t b[MAX_PATH];
    GetModuleFileNameW(nullptr, b, MAX_PATH);
    return fs::path(b).parent_path();
}

// An addon folder holding a copy of the test addon under `dll`, an optional manifest and an optional mode.
static fs::path MakeAddon(const fs::path& root, const std::string& folder, const std::string& dll, const char* manifest = nullptr, const char* mode = nullptr) {
    const fs::path dir = root / folder;
    fs::create_directories(dir);
    fs::copy_file(ExeDir() / "lsproxy_testaddon.dll", dir / dll, fs::copy_options::overwrite_existing);
    if (manifest) WriteFile(dir / "addon.json", manifest);
    if (mode) WriteFile(dir / "mode.txt", mode);
    return dir;
}

static int Calls(const fs::path& dir, const std::string& what) {
    std::ifstream f(dir / "calls.txt");
    std::string ln;
    int n = 0;
    while (std::getline(f, ln)) if (ln == what) ++n;
    return n;
}

static int IndexOf(AddonManager& m, const std::string& id) {
    auto& a = m.GetAddons();
    for (size_t i = 0; i < a.size(); ++i) if (a[i].id == id) return (int)i;
    return -1;
}

static AddonInfo* Find(AddonManager& m, const std::string& id) {
    const int i = IndexOf(m, id);
    return i < 0 ? nullptr : &m.GetAddons()[i];
}

static bool EnabledInFile(const fs::path& cfg, const std::string& id, bool* present = nullptr) {
    std::ifstream f(cfg);
    nlohmann::json j = nlohmann::json::parse(f, nullptr, false);
    const bool has = !j.is_discarded() && j.contains("addons") && j["addons"].contains(id) && j["addons"][id].contains("_enabled");
    if (present) *present = has;
    return has && j["addons"][id]["_enabled"].get<bool>();
}

static nlohmann::json ReadJson(const fs::path& p) {
    std::ifstream f(p);
    return nlohmann::json::parse(f, nullptr, false);
}

// The settings file: reading, writing, the two kinds of value, the enabled flag, and what happens to a damaged file.
static void TestConfig(const fs::path& T) {
    using nlohmann::json;
    printf("== settings file\n");
    ConfigManager& C = ConfigManager::Instance();
    const fs::path base = T / "cfg";

    {   // a new file, written and read back
        const fs::path dir = base / "new";
        fs::create_directories(dir);
        const fs::path p = dir / "config.json";
        C.Load(p.wstring());
        Check("with no file, everything reads as its default", C.Get("x", "k", "dflt") == "dflt" && C.GlobalGet(nullptr, "k").is_null() && C.IsAddonEnabled("x", true) && !C.IsAddonEnabled("x", false));
        C.Set("x", "k", "v");
        C.SetAddonEnabled("x", false);
        C.GlobalSet("ui", "size", 125);
        C.GlobalSet(nullptr, "level", 2);
        C.Save();
        Check("Save writes the file", fs::exists(p));
        const json j = ReadJson(p);
        Check("an addon's values, its enabled flag and the host's settings land where documented",
              j["addons"]["x"]["k"] == "v" && j["addons"]["x"]["_enabled"] == false && j["global"]["ui"]["size"] == 125 && j["global"]["level"] == 2);
        Check("no temporary file is left beside it", !fs::exists(fs::path(p) += ".tmp"));
        fs::remove(p);
        C.Save();
        Check("saving settings that have not changed does not rewrite the file", !fs::exists(p));
        C.Set("x", "k", "w");
        C.Save();
        Check("a change is written", fs::exists(p) && ReadJson(p)["addons"]["x"]["k"] == "w");

        C.Load(p.wstring());
        Check("loading the file again gives the same values", C.Get("x", "k") == "w" && !C.IsAddonEnabled("x", true) && C.GlobalGetOr<int>("ui", "size", 0) == 125 && C.GlobalGetOr<int>(nullptr, "level", 0) == 2);
        Check("a host setting of the wrong type reads as the default", C.GlobalGetOr<std::string>("ui", "size", "none") == "none" && C.GlobalGetOr<int>("ui", "absent", 9) == 9);
    }

    {   // the kinds of value, and the enabled flag in its old and new places
        const fs::path dir = base / "types";
        fs::create_directories(dir);
        const fs::path p = dir / "config.json";
        WriteFile(p, R"({"addons":{"t":{"s":"text","b":true,"n":3.5,"i":7,"o":{"a":1},"_enabled":true,"enabled":"yes"},"l":{"enabled":false},"m":{"enabled":"yes"}}})");
        C.Load(p.wstring());
        Check("text, booleans and numbers all read back as text", C.Get("t", "s") == "text" && C.Get("t", "b") == "1" && C.Get("t", "n") == "3.5" && C.Get("t", "i") == "7");
        Check("an object reads as the default", C.Get("t", "o", "dflt") == "dflt");
        Check("an addon's own 'enabled' text is not the host's flag", C.Get("t", "enabled") == "yes" && C.IsAddonEnabled("t", false));
        Check("the old boolean 'enabled' is still honoured", !C.IsAddonEnabled("l", true));
        Check("a text 'enabled' is not taken as the flag", C.IsAddonEnabled("m", true) && !C.IsAddonEnabled("m", false));
        C.SetAddonEnabled("l", true);
        const json snap = C.Snapshot();
        Check("setting the flag moves it to _enabled and drops the old boolean", snap["addons"]["l"]["_enabled"] == true && !snap["addons"]["l"].contains("enabled"));
        C.SetAddonEnabled("m", false);
        Check("...but leaves an addon's own 'enabled' text alone", C.Snapshot()["addons"]["m"]["enabled"] == "yes");
    }

    for (const char* bad : { "{ this is not json", "[1, 2, 3]" }) {   // a damaged file is kept, not overwritten
        const fs::path dir = base / (std::string("bad") + (bad[0] == '[' ? "_array" : "_text"));
        fs::create_directories(dir);
        const fs::path p = dir / "config.json";
        WriteFile(p, bad);
        C.Load(p.wstring());
        std::ifstream kept(fs::path(p) += ".corrupt");
        const std::string keptText((std::istreambuf_iterator<char>(kept)), std::istreambuf_iterator<char>());
        Check("a damaged settings file is kept as config.json.corrupt", keptText == bad, bad);
        Check("...and the settings start empty", C.Get("x", "k", "dflt") == "dflt");
        const std::string mark = std::string("v") + (bad[0] == '[' ? "2" : "1");
        C.Set("x", "k", mark);
        C.Save();
        Check("...and saving then writes a valid file", ReadJson(p)["addons"]["x"]["k"] == mark);
    }

    {   // a second file loaded later starts from a clean slate
        const fs::path one = base / "first" / "config.json", two = base / "second" / "config.json";
        fs::create_directories(one.parent_path());
        fs::create_directories(two.parent_path());
        C.Load(one.wstring());
        C.Set("q", "k", "same");
        C.Save();
        C.Load(two.wstring());
        C.Set("q", "k", "same");
        C.Save();
        Check("a file loaded later is written even when it holds what the previous file did", fs::exists(two));
    }

    {   // a whole-file replacement, as settings restore does
        const fs::path dir = base / "replace";
        fs::create_directories(dir);
        const fs::path p = dir / "config.json";
        C.Load(p.wstring());
        C.Replace(json{ { "addons", { { "r", { { "_enabled", false } } } } }, { "global", { { "log_level", 3 } } } });
        Check("Replace swaps everything and saves at once", !C.IsAddonEnabled("r", true) && ReadJson(p)["global"]["log_level"] == 3);
        C.Replace(json::array({ 1, 2 }));
        Check("Replace with something that is not an object leaves an empty object", C.Snapshot().is_object() && C.Snapshot().empty());
    }

    {   // the very old settings file, addons_config.ini
        const fs::path dir = base / "migrate";
        fs::create_directories(dir / "One");
        fs::create_directories(dir / "Two");
        fs::create_directories(dir / "Three");
        WriteFile(dir / "addons_config.ini", "[Addons]\nOne=0\nTwo=1\n");
        const fs::path p = dir / "config.json";
        C.Load(p.wstring());
        Check("settings from the old .ini are carried over, one per addon folder", !C.IsAddonEnabled("One", true) && C.IsAddonEnabled("Two", false) && C.IsAddonEnabled("Three", false));
        Check("...and written to config.json straight away", fs::exists(p) && ReadJson(p)["addons"]["One"]["_enabled"] == false);
    }
}

static bool PreSkips(uint32_t, uint32_t, uint32_t, void* user) { ++*(int*)user; return true; }
static bool PreLetsThrough(uint32_t, uint32_t, uint32_t, void* user) { ++*(int*)user; return false; }
static void PostCounts(uint32_t, uint32_t, uint32_t, void* user) { ++*(int*)user; }
static void EventGot(uint32_t id, const void* data, uint32_t size, void* user) {
    auto* seen = (std::pair<uint32_t, std::string>*)user;
    seen->first = id;
    seen->second.assign((const char*)data, size);
}

// What an addon can ask of the host: settings, events, dispatch callbacks, status and metrics.
static void TestHost(const fs::path& T) {
    printf("== host interface\n");
    const fs::path dir = T / "host";
    fs::create_directories(dir);
    ConfigManager::Instance().Load((dir / "config.json").wstring());
    HostImpl h;

    Check("the host reports the addon API version", h.GetHostVersion() == (uint32_t)LSPROXY_API_VERSION_INT);
    h.SetConfig("a", "k", "v");
    Check("a setting written by an addon reads back", std::string(h.GetConfig("a", "k", "")) == "v");
    Check("a missing setting gives the default given", std::string(h.GetConfig("a", "nope", "dflt")) == "dflt");
    const char* nothing = h.GetConfig(nullptr, nullptr, nullptr);
    Check("null arguments are treated as empty text", nothing && *nothing == 0);

    const char* first = h.GetConfig("a", "k", "");
    for (int i = 0; i < 500; ++i) h.GetConfig("a", "k", "");
    Check("a returned setting stays valid for hundreds of later calls", std::string(first) == "v");

    int skip = 0, through = 0, post = 0;
    h.SetPreDispatchCallback(PreSkips, &skip);
    h.SetPreDispatchCallback(PreLetsThrough, &through);
    h.SetPostDispatchCallback(PostCounts, &post);
    Check("every pre-dispatch callback runs, and any of them can skip the dispatch", h.InvokePreDispatch(1, 2, 3) && skip == 1 && through == 1);
    h.InvokePostDispatch(1, 2, 3);
    Check("post-dispatch callbacks run", post == 1);
    h.SetPreDispatchCallback(PreLetsThrough, &skip);   // the same owner again replaces its callback
    Check("a callback set again by the same owner replaces the old one", !h.InvokePreDispatch(1, 2, 3) && skip == 2 && through == 2);
    h.SetPreDispatchCallback(nullptr, &skip);          // no callback removes the owner's entry
    h.InvokePreDispatch(1, 2, 3);
    Check("setting no callback removes that owner's entry", skip == 2 && through == 3);
    h.SetPostDispatchCallback(nullptr, &post);
    h.InvokePostDispatch(1, 2, 3);
    Check("...for post-dispatch as well", post == 1);

    h.SetD3D11Device((void*)0x10, (void*)0x20);
    Check("the device and context handed over by the hook are what addons get", h.GetD3D11Device() == (void*)0x10 && h.GetD3D11DeviceContext() == (void*)0x20);

    std::pair<uint32_t, std::string> seen{ 0, "" };
    const uint32_t evt = LSPROXY_EVENT_CUSTOM + 7;
    h.SubscribeEvent(evt, EventGot, &seen);
    h.PublishEvent(evt, "payload", 7);
    Check("an event published by one addon reaches a subscriber with its data", seen.first == evt && seen.second == "payload");
    seen = { 0, "" };
    h.UnsubscribeEvent(evt, EventGot);
    h.PublishEvent(evt, "again", 5);
    Check("after unsubscribing nothing arrives", seen.first == 0);

    h.SetStatus("hostTest", "Running", 1);
    h.PublishMetric("hostTest", "fps", 59.5, "fps");
    const Metrics::Status st = Metrics::Instance().GetStatus("hostTest");
    const Metrics::Series se = Metrics::Instance().Get("hostTest", "fps", 10.0);
    Check("a status set by an addon can be read back", st.text == "Running" && st.level == 1);
    Check("a metric published by an addon can be read back", !se.samples.empty() && se.samples.back().v > 59.4f && se.unit == "fps");
    h.SetStatus(nullptr, nullptr, 0);
    h.PublishMetric(nullptr, nullptr, 1.0, nullptr);
    Check("null arguments to status and metrics are tolerated", true);
}

static AddonInfo MakeInfo(const std::string& id, std::vector<std::string> deps = {}) {
    AddonInfo a;
    a.id = id;
    a.folderName = std::wstring(id.begin(), id.end());
    a.manifest.dependencies = std::move(deps);
    return a;
}

static std::string Ids(const std::vector<AddonInfo>& v) {
    std::string s;
    for (const AddonInfo& a : v) s += (s.empty() ? "" : ",") + a.id;
    return s;
}

// Putting addons in an order where each comes after the ones it needs.
static void TestDependencies() {
    printf("== dependency order\n");
    {
        std::vector<AddonInfo> v{ MakeInfo("c", { "b" }), MakeInfo("b", { "a" }), MakeInfo("a") };
        const bool ok = AddonDependency::Resolve(v);
        Check("a chain comes out with what is needed first", ok && Ids(v) == "a,b,c", Ids(v));
    }
    {
        std::vector<AddonInfo> v{ MakeInfo("d", { "b", "c" }), MakeInfo("b", { "a" }), MakeInfo("c", { "a" }), MakeInfo("a") };
        const bool ok = AddonDependency::Resolve(v);
        const std::string r = Ids(v);
        Check("a diamond has its root first and its tip last", ok && r.front() == 'a' && r.back() == 'd', r);
    }
    {
        std::vector<AddonInfo> v{ MakeInfo("p", { "ghost" }), MakeInfo("q") };
        Check("a dependency that is not installed does not block", AddonDependency::Resolve(v) && v.size() == 2);
    }
    {
        std::vector<AddonInfo> v{ MakeInfo("m", { "n" }), MakeInfo("n", { "m" }) };
        Check("a circular dependency is reported and the list is left as it was", !AddonDependency::Resolve(v) && Ids(v) == "m,n", Ids(v));
        std::vector<AddonInfo> self{ MakeInfo("s", { "s" }) };
        Check("an addon that needs itself counts as circular", !AddonDependency::Resolve(self));
    }
    {
        std::vector<AddonInfo> none;
        Check("an empty list is fine", AddonDependency::Resolve(none) && none.empty());
    }
    {
        std::vector<AddonInfo> v{ MakeInfo("a1"), MakeInfo("a2"), MakeInfo("a3") };
        AddonDependency::Resolve(v);
        Check("addons that need nothing keep their order when the list is sorted", Ids(v) == "a1,a2,a3", Ids(v));
        AddonDependency::Resolve(v);
        Check("...and when it is sorted again", Ids(v) == "a1,a2,a3", Ids(v));
    }
}

// The SHA-256 of a DLL and what trusted_addons.json says about it.
static void TestSecurity(const fs::path& T) {
    printf("== addon safety checks\n");
    const fs::path dir = T / "sec";
    fs::create_directories(dir);
    WriteFile(dir / "abc.bin", "abc");
    WriteFile(dir / "empty.bin", "");
    WriteFile(dir / "long.bin", std::string(20000, 'a'));   // more than one read buffer

    Check("SHA-256 of 'abc' is the published value", AddonSecurity::ComputeSHA256((dir / "abc.bin").wstring()) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    Check("SHA-256 of an empty file is the published value", AddonSecurity::ComputeSHA256((dir / "empty.bin").wstring()) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    Check("SHA-256 of a file longer than one read is right", AddonSecurity::ComputeSHA256((dir / "long.bin").wstring()) == "cc17faaad36649c4603dda4d8ff97cb149722af0bcac0746305a2134ad2d0b97");
    Check("a file that cannot be read gives no hash", AddonSecurity::ComputeSHA256((dir / "nope.bin").wstring()).empty());

    const std::string abc = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    const std::wstring dll = (dir / "abc.bin").wstring();
    WriteFile(dir / "trusted_addons.json", R"({"sec_ok":[")" + abc + R"("],"sec_bad":["00"],"sec_multi":["11",")" + abc + R"("],"sec_junk":"not a list"})");
    AddonSecurity::LoadTrustedHashes(dir.wstring());
    Check("a DLL whose hash is listed is trusted", AddonSecurity::VerifyDll(dll, "sec_ok") == SecurityVerdict::Trusted);
    Check("a listed addon whose DLL differs is tampered", AddonSecurity::VerifyDll(dll, "sec_bad") == SecurityVerdict::Tampered);
    Check("any one of several listed hashes will do", AddonSecurity::VerifyDll(dll, "sec_multi") == SecurityVerdict::Trusted);
    Check("an addon that is not listed is unknown", AddonSecurity::VerifyDll(dll, "sec_absent") == SecurityVerdict::Unknown);
    Check("an entry that is not a list is ignored", AddonSecurity::VerifyDll(dll, "sec_junk") == SecurityVerdict::Unknown);
    Check("a listed addon whose DLL cannot be read is unknown", AddonSecurity::VerifyDll((dir / "nope.bin").wstring(), "sec_ok") == SecurityVerdict::Unknown);

    const fs::path dir2 = T / "sec2";
    fs::create_directories(dir2);
    WriteFile(dir2 / "trusted_addons.json", R"({"sec_other":["00"]})");
    AddonSecurity::LoadTrustedHashes(dir2.wstring());
    Check("loading the list again replaces it", AddonSecurity::VerifyDll(dll, "sec_ok") == SecurityVerdict::Unknown);

    const fs::path dir4 = T / "sec4";
    fs::create_directories(dir4);
    std::string upper = abc;
    for (char& c : upper) c = (char)toupper((unsigned char)c);
    WriteFile(dir4 / "trusted_addons.json", R"({"sec_upper":[")" + upper + R"("]})");
    AddonSecurity::LoadTrustedHashes(dir4.wstring());
    Check("a listed hash may be written in capitals", AddonSecurity::VerifyDll(dll, "sec_upper") == SecurityVerdict::Trusted);

    const fs::path dir3 = T / "sec3";
    fs::create_directories(dir3);
    WriteFile(dir3 / "trusted_addons.json", "{ broken");
    AddonSecurity::LoadTrustedHashes(dir3.wstring());
    Check("a broken list is survived", true);
}

static void CountA(uint32_t, const void*, uint32_t, void* user) { ((std::string*)user)->push_back('A'); }
static void CountB(uint32_t, const void*, uint32_t, void* user) { ((std::string*)user)->push_back('B'); }
static void Boom(uint32_t, const void*, uint32_t, void*) { volatile int* p = nullptr; *p = 1; }
static void RemovesItself(uint32_t id, const void*, uint32_t, void* user) {
    ++*(int*)user;
    EventBus::Instance().Unsubscribe(id, RemovesItself);
}

// The event bus addons publish to and subscribe from.
static void TestEvents() {
    printf("== event bus\n");
    EventBus& bus = EventBus::Instance();
    const uint32_t base = LSPROXY_EVENT_CUSTOM + 100;
    {
        std::string seen;
        bus.Subscribe(base, CountA, &seen);
        bus.Subscribe(base, CountB, &seen);
        bus.Publish(base);
        Check("subscribers are called in the order they subscribed", seen == "AB", seen);
        bus.Publish(base + 1);
        Check("an event nobody subscribed to reaches nobody", seen == "AB");
        bus.Unsubscribe(base, CountA);
        seen.clear();
        bus.Publish(base);
        Check("an unsubscribed callback is not called again", seen == "B", seen);
        bus.Unsubscribe(base, CountB);
    }
    {
        std::string one, two;
        bus.Subscribe(base + 2, CountA, &one);
        bus.Subscribe(base + 2, CountA, &two);
        bus.Unsubscribe(base + 2, CountA);
        bus.Publish(base + 2);
        Check("unsubscribing a callback removes it for every owner", one.empty() && two.empty());
        bus.Subscribe(base + 2, nullptr, nullptr);
        bus.Publish(base + 2);
        Check("a null callback is ignored", true);
    }
    {
        std::string after;
        bus.Subscribe(base + 3, Boom, nullptr);
        bus.Subscribe(base + 3, CountA, &after);
        bus.Publish(base + 3);
        Check("a subscriber that faults does not stop the others", after == "A", after);
        bus.Unsubscribe(base + 3, Boom);
        bus.Unsubscribe(base + 3, CountA);
    }
    {
        int calls = 0;
        bus.Subscribe(base + 4, RemovesItself, &calls);
        bus.Publish(base + 4);
        bus.Publish(base + 4);
        Check("a subscriber may unsubscribe itself while it is being called", calls == 1, std::to_string(calls));
    }
}

static void OnEvent(uint32_t, const void*, uint32_t, void* user) { ++*(int*)user; }

int main(int argc, char** argv) {
    if (argc > 1 && !strcmp(argv[1], "abrupt-gpu")) {
        // The process ends with the Performance tab's sampler thread running and GpuStats::Shutdown never called, as when Lossless Scaling exits.
        // A std::thread still joinable when the singleton is destroyed calls std::terminate: the exit code would be a crash.
        GpuStats::Instance().Wanted();
        Sleep(300);
        printf("abrupt exit with the GPU sampler running\n"); fflush(stdout);
        exit(0);   // runs the static destructors, as a DLL's are run when the process ends
    }
    const fs::path T = fs::temp_directory_path() / ("lsp_coretest_" + std::to_string(GetCurrentProcessId()));
    std::error_code ec;
    fs::remove_all(T, ec);
    const fs::path A = T / "addons";
    fs::create_directories(A);
    if (!fs::exists(ExeDir() / "lsproxy_testaddon.dll")) { printf("FAIL  lsproxy_testaddon.dll is not beside the test program\n"); return 1; }

    // ---- a folder of addons in every shape the manager has to cope with
    const fs::path alpha = MakeAddon(A, "alpha", "alpha.dll", R"({"name":"Alpha","version":"1.2.3","author":"A","description":"first","tags":["x","y"]})");
    WriteFile(alpha / "icon.png", "not really a picture");
    WriteFile(alpha / "settings.ini", "[a]\n");
    const fs::path beta = MakeAddon(A, "beta", "custom.dll", R"({"dll":"custom.dll","icon":"pic.jpg"})");
    fs::copy_file(ExeDir() / "lsproxy_testaddon.dll", beta / "other.dll");
    WriteFile(beta / "pic.jpg", "x");
    const fs::path gamma = MakeAddon(A, "gamma", "zzz.dll");                                   // no manifest, DLL not named after the folder
    MakeAddon(A, "newer", "newer.dll", R"({"min_host_version":"9.0.0"})");                     // needs a much newer manager
    MakeAddon(A, "minok", "minok.dll", R"({"min_host_version":"1.0.0"})");
    MakeAddon(A, "mingarbage", "mingarbage.dll", R"({"min_host_version":"abc"})");             // unreadable: must not block
    const fs::path restart = MakeAddon(A, "restart", "restart.dll", nullptr, "restart");
    const fs::path crashy = MakeAddon(A, "crashy", "crashy.dll", nullptr, "crash_init");
    MakeAddon(A, "badjson", "badjson.dll", "{ this is not json");
    MakeAddon(A, "dep_b", "dep_b.dll", R"({"dependencies":["dep_a"]})");
    MakeAddon(A, "dep_a", "dep_a.dll");
    WriteFile(A / "nodll" / "readme.txt", "no DLL in here");
    MakeAddon(A, ".hidden", "hidden.dll");                                                     // dot folders are staging areas
    MakeAddon(A, "renamed_new", "renamed_new.dll", R"({"renamed_from":["renamed_old"]})");                // an addon that changed its folder name
    MakeAddon(A, "renamed_old", "renamed_old.dll");                                            // ...and the folder it used to have
    MakeAddon(A, "LSP-ReShade", "LSP_ReShade.dll");                                            // a retired standalone addon: built in now
    WriteFile(A / "config.json", R"({"addons":{"beta":{"_enabled":false},"renamed_old":{"_enabled":false,"keep":"me"}},"global":{"security_level":0}})");

    setvbuf(stdout, nullptr, _IONBF, 0);
    try { TestConfig(T); TestHost(T); TestDependencies(); TestSecurity(T); TestEvents(); } catch (const std::exception& e) { Check("the settings tests ran to the end", false, e.what()); }

    HostImpl host;
    int loadedEvents = 0, unloadedEvents = 0;
    host.SubscribeEvent(LSPROXY_EVENT_ADDON_LOADED, OnEvent, &loadedEvents);
    host.SubscribeEvent(LSPROXY_EVENT_ADDON_UNLOADED, OnEvent, &unloadedEvents);

    {
        AddonManager mgr(&host, A.wstring());
        ImGuiContext* ctx = ImGui::CreateContext();
        ImGui::SetCurrentContext(ctx);

        // ---- scanning
        printf("== scanning\n");
        mgr.ScanAddons();
        Check("finds the twelve usable addons (the renamed one counts once)", mgr.GetAddons().size() == 12, "found " + std::to_string(mgr.GetAddons().size()));
        Check("a folder with no DLL is not an addon", IndexOf(mgr, "nodll") < 0);
        Check("a dot folder is not an addon", IndexOf(mgr, ".hidden") < 0);
        Check("the folder of a retired standalone addon is ignored, since that feature is built in", IndexOf(mgr, "LSP-ReShade") < 0);
        Check("an addon that was renamed hides its old folder", IndexOf(mgr, "renamed_new") >= 0 && IndexOf(mgr, "renamed_old") < 0);
        Check("...and keeps the settings it had under the old name, including being switched off", ConfigManager::Instance().Get("renamed_new", "keep") == "me" && !Find(mgr, "renamed_new")->enabled);
        bool oldPresent = true;
        EnabledInFile(A / "config.json", "renamed_old", &oldPresent);
        Check("...moved, not copied, and saved", !oldPresent && ConfigManager::Instance().Get("renamed_old", "keep", "gone") == "gone");
        AddonInfo* a = Find(mgr, "alpha");
        Check("manifest fields are read", a && a->manifest.parsed && a->manifest.name == "Alpha" && a->manifest.version == "1.2.3" && a->manifest.author == "A" &&
              a->manifest.description == "first" && a->manifest.tags.size() == 2);
        Check("the DLL named after the folder is used", a && fs::path(a->dllPath).filename() == "alpha.dll");
        Check("icon.png is picked up", a && fs::path(a->iconPath).filename() == "icon.png");
        Check("an .ini file is picked up as the addon's config", a && fs::path(a->configPath).filename() == "settings.ini");
        AddonInfo* b = Find(mgr, "beta");
        Check("the manifest's DLL name wins over other DLLs", b && fs::path(b->dllPath).filename() == "custom.dll");
        Check("the manifest's icon name wins", b && fs::path(b->iconPath).filename() == "pic.jpg");
        Check("an addon switched off in config.json starts switched off", b && !b->enabled);
        Check("an addon with no entry in config.json starts switched on", a && a->enabled);
        AddonInfo* g = Find(mgr, "gamma");
        Check("with no manifest and no matching name, the first DLL found is used", g && fs::path(g->dllPath).filename() == "zzz.dll");
        Check("with no manifest the display name is the folder name", g && g->GetDisplayName() == "gamma" && !g->manifest.parsed);
        AddonInfo* bj = Find(mgr, "badjson");
        Check("a broken addon.json does not hide the addon", bj && !bj->manifest.parsed);
        Check("dependencies are listed before what needs them", IndexOf(mgr, "dep_a") >= 0 && IndexOf(mgr, "dep_a") < IndexOf(mgr, "dep_b"));

        // ---- loading
        printf("== loading\n");
        mgr.LoadAddons();
        Check("an enabled addon is loaded", Find(mgr, "alpha")->IsLoaded());
        Check("a disabled addon is not loaded", !Find(mgr, "beta")->IsLoaded());
        AddonInfo* nw = Find(mgr, "newer");
        Check("an addon that needs a newer API is refused, with a reason", !nw->IsLoaded() && nw->errorMessage.find("Needs a newer") != std::string::npos, nw->errorMessage);
        Check("a stated minimum API that is met does not block", Find(mgr, "minok")->IsLoaded());
        Check("an unreadable minimum does not block", Find(mgr, "mingarbage")->IsLoaded());
        Check("name and version fall back to what the DLL reports", Find(mgr, "gamma")->manifest.name == "Test Addon" && Find(mgr, "gamma")->manifest.version == "9.9.9");
        Check("the manifest is not overridden by the DLL", Find(mgr, "alpha")->manifest.name == "Alpha");
        Check("a restart-required addon reports it", Find(mgr, "restart")->RequiresRestart() && !Find(mgr, "alpha")->RequiresRestart());

        // ---- initialising
        printf("== initialising\n");
        mgr.InitializeAddons(ctx);
        Check("an addon's initialise is called once", Calls(alpha, "init") == 1);
        AddonInfo* cr = Find(mgr, "crashy");
        Check("an addon that faults in initialise is marked, not fatal", cr->faulted && cr->errorMessage == "Crashed during initialization");
        Check("'loaded' events are sent for the addons that started (eight of nine)", loadedEvents == 8, "events " + std::to_string(loadedEvents));

        // ---- switching on and off
        printf("== switching on and off\n");
        const fs::path cfg = A / "config.json";
        mgr.ToggleAddon(IndexOf(mgr, "alpha"), false);
        Check("switching off unloads the addon", !Find(mgr, "alpha")->IsLoaded() && Calls(alpha, "shutdown") == 1);
        Check("switching off sends an 'unloaded' event", unloadedEvents == 1);
        Check("switching off is saved to config.json", !EnabledInFile(cfg, "alpha"));
        mgr.ToggleAddon(IndexOf(mgr, "alpha"), true);
        Check("switching on loads and starts it again", Find(mgr, "alpha")->IsLoaded() && Calls(alpha, "init") == 2);
        Check("switching on is saved to config.json", EnabledInFile(cfg, "alpha"));
        mgr.ToggleAddon(IndexOf(mgr, "restart"), false);
        Check("a restart-required addon stays loaded when switched off", Find(mgr, "restart")->IsLoaded() && !Find(mgr, "restart")->enabled && Calls(restart, "shutdown") == 0);
        bool present = false;
        EnabledInFile(cfg, "restart", &present);
        Check("...but the choice is saved for the next start", present && !EnabledInFile(cfg, "restart"));
        mgr.ToggleAddon(IndexOf(mgr, "restart"), true);

        // ---- resources, settings panels
        printf("== resources and settings panels\n");
        const void* data = nullptr; uint32_t size = 0;
        Check("an addon can supply a resource", mgr.InterceptResource(L"test.shader", L"TEXT", &data, &size) && size == 5 && data && !memcmp(data, "HELLO", 5));
        Check("an unknown resource is not supplied", !mgr.InterceptResource(L"other", L"TEXT", &data, &size));
        mgr.RenderAddonSettings(IndexOf(mgr, "alpha"));
        Check("a loaded addon's settings panel is drawn", Calls(alpha, "settings") == 1);
        mgr.RenderAddonSettings(IndexOf(mgr, "beta"));
        Check("an unloaded addon's settings panel is not", Calls(beta, "settings") == 0);
        for (int i = 0; i < (int)mgr.GetAddons().size(); ++i) mgr.ToggleAddon(i, false);
        Check("switched-off addons are not asked for resources", !mgr.InterceptResource(L"test.shader", L"TEXT", &data, &size));
        mgr.ToggleAddon(IndexOf(mgr, "alpha"), true);
        Check("...and one that is switched on again is", mgr.InterceptResource(L"test.shader", L"TEXT", &data, &size));
        mgr.ToggleAddon(IndexOf(mgr, "gamma"), true);

        // ---- security level
        printf("== security level\n");
        ConfigManager::Instance().GlobalSet(nullptr, "security_level", 2);
        mgr.ToggleAddon(IndexOf(mgr, "gamma"), false);
        mgr.ToggleAddon(IndexOf(mgr, "gamma"), true);
        Check("level 2 blocks an addon that is not on the trusted list", !Find(mgr, "gamma")->IsLoaded() && Find(mgr, "gamma")->errorMessage.rfind("Blocked:", 0) == 0, Find(mgr, "gamma")->errorMessage);
        ConfigManager::Instance().GlobalSet(nullptr, "security_level", 1);
        mgr.LoadAddonNow(IndexOf(mgr, "gamma"));
        Check("level 1 warns but still loads it", Find(mgr, "gamma")->IsLoaded());
        ConfigManager::Instance().GlobalSet(nullptr, "security_level", 0);

        // ---- removing
        printf("== removing\n");
        const size_t before = mgr.GetAddons().size();
        auto rm = mgr.RemoveAddon(IndexOf(mgr, "gamma"));
        Check("removing an addon succeeds", rm.ok && rm.id == "gamma", rm.message);
        Check("its folder is gone from addons", !fs::exists(gamma));
        bool inRemoved = false;
        if (fs::exists(A / ".removed")) for (auto& e : fs::directory_iterator(A / ".removed")) if (e.path().filename().string().rfind("gamma", 0) == 0) inRemoved = true;
        Check("...and moved to addons/.removed, not deleted", inRemoved);
        Check("it leaves the list", mgr.GetAddons().size() == before - 1 && IndexOf(mgr, "gamma") < 0);
        Check("it is recorded as switched off", !EnabledInFile(cfg, "gamma"));
        auto rr = mgr.RemoveAddon(IndexOf(mgr, "restart"));
        Check("a loaded restart-required addon cannot be removed yet", !rr.ok && rr.message.find("restarted") != std::string::npos, rr.message);
        Check("removing something that is not in the list is refused", !mgr.RemoveAddon(999).ok);

        // ---- installing
        printf("== installing\n");
        const fs::path incoming = T / "incoming";
        MakeAddon(incoming, "newone", "newone.dll", R"({"name":"New One"})");
        auto in = mgr.InstallAddon((incoming / "newone").wstring());
        Check("installing a folder succeeds", in.ok && in.id == "newone", in.message);
        Check("...into the addons folder, switched off", fs::exists(A / "newone" / "newone.dll") && Find(mgr, "newone") && !Find(mgr, "newone")->enabled && !EnabledInFile(cfg, "newone"));
        WriteFile(incoming / "empty" / "readme.txt", "nothing loadable");
        auto bad = mgr.InstallAddon((incoming / "empty").wstring());
        Check("a folder with no DLL is refused and leaves nothing behind", !bad.ok && !fs::exists(A / "empty"), bad.message);

        ImGui::DestroyContext(ctx);
    }   // the manager unloads everything here, including the addon that faulted

    Check("shutting down unloads what was loaded", Calls(alpha, "shutdown") == 3, std::to_string(Calls(alpha, "shutdown")));
    fs::remove_all(T, ec);
    printf("\n%s\n", g_failed ? "CORE TEST FAILED" : "CORE TEST PASSED");
    return g_failed ? 1 : 0;
}
