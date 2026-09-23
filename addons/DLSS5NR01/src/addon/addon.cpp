// DLSS5NR01: an Echo Addon Manager addon (DLSS 5 Neural Rendering).
//
// Where it sits in Lossless Scaling's pipeline (all on LS's render thread, all on the display GPU):
//
//   capture k ─┬─ LSFG pyramid pass on frame k            <- TAP: copy frame k (+ LSFG flow) to the model's input,
//              │                                             start a model run on it (skipped if the previous run
//              │                                             is still on the GPU). LS's frame is never touched.
//              ├─ LSFG flow, interpolation composes ...
//              └─ Present (generated a, generated b, real k) <- PRESENT HOOK: add the newest finished delta to the
//                                                             frame about to be shown, moved by LSFG's own flow to
//                                                             where that content sits in this frame.
//
// LS's queue never waits for the model, so the model can only ever be late for a frame, never slow LS down.
// The delta for frame k is normally finished before frame k is shown; until then the previous delta is warped
// forward. Settings live in an ImGui panel inside Echo Addon Manager.
#include <lsproxy/addon_sdk.h>
#include "imgui.h"
#include <lsproxy/lsp_widgets.h>
#include "engine/nr_engine.h"
#include "addon/bridge.h"
#include "addon/frame_tap.h"
#include "addon/dispatch_hook.h"
#include "addon/present_hook.h"
#include "addon/compose11.h"
#include "addon/requirements.h"
#include <shellapi.h>
#include <commdlg.h>
#include <d3d11.h>
#include <dxgi.h>
#include <windows.h>
#include <atomic>
#include <mutex>
#include <thread>
#include <string>
#include <vector>
#include <algorithm>
#include <cstdio>
#include <cstdarg>
#include <csignal>
#include <exception>

static const char* kAddonId = "DLSS5NR01";

// ------------------------------------------------------------------ state
struct Config {
    bool enabled = true;
    NrParams p;
    int tapMode = 0;          // 0 auto, 1 manual
    int frameSlot = -1;       // -1 auto (highest large SRV slot)
    std::string tickSig, tapSig;
    float watchdogMs = 80.0f;
    std::string snippetPath;  // empty = <LS dir>\nvngx_dlssnr.dll
    bool freshFlow = true;    // run the model once LSFG has this frame's own flow (off: at the TAP, with the previous pair's flow, one frame late)
    bool lsFirst = true;      // raise LS's GPU thread priority so its work pre-empts the model on the shared GPU
    bool hotkeys = true;      // Ctrl+Shift + key, polled at every present (works while the game has focus)
    int keyAB = VK_F6, keySplit = VK_F7, keySharpDn = VK_F8, keySharpUp = VK_F9, keyPreset = VK_F10;
    bool gameAuto = true;     // switch to a game's saved look when that game takes focus
    std::vector<std::pair<std::string, std::string>> games;   // lower-case exe name -> preset name
};

// ------------------------------------------------------------------ presets
// A preset is the look: the model knobs and the compose knobs, as one "name" plus a "key=value;key=value" string kept in the
// host config (which has no key enumeration, so the names live in one "presetNames" list, joined by '|'). Applying a
// preset that changes the working scale rebuilds the model, which costs a short hitch.
struct Preset { std::string name, data; };
static std::vector<Preset> g_presets;              // under g_cfgMu
static int g_presetCursor = -1;                    // last preset applied by the hotkey
// The HUD areas as "l,t,r,b/l,t,r,b" (no ';' '=' '|', so they fit inside a preset and the config).
static std::string HudSerialize(const NrParams& p) {
    std::string s;
    for (uint32_t i = 0; i < p.hudCount && i < (uint32_t)NrParams::kMaxHud; ++i) {
        char b[96]; snprintf(b, sizeof b, "%s%g,%g,%g,%g", i ? "/" : "", p.hud[i][0], p.hud[i][1], p.hud[i][2], p.hud[i][3]); s += b;
    }
    return s;
}
static void HudParse(const std::string& s, NrParams& p) {
    p.hudCount = 0; size_t pos = 0;
    while (pos < s.size() && p.hudCount < (uint32_t)NrParams::kMaxHud) {
        size_t end = s.find('/', pos); if (end == std::string::npos) end = s.size();
        float v[4] = {};
        if (sscanf(s.substr(pos, end - pos).c_str(), "%f,%f,%f,%f", &v[0], &v[1], &v[2], &v[3]) == 4) {
            for (float& x : v) x = std::clamp(x, 0.0f, 1.0f);
            if (v[2] > v[0] + 0.001f && v[3] > v[1] + 0.001f) { memcpy(p.hud[p.hudCount], v, sizeof v); p.hudCount++; }
        }
        pos = end + 1;
    }
}
static std::string PresetSerialize(const NrParams& p) {
    char b[1024];
    snprintf(b, sizeof b, "passes=%u;style=%u;autoMask=%u;intensity=%g;localStructure=%g;localTone=%g;skinStructure=%g;useFlow=%d;workingScale=%g;composeIntensity=%g;maxDelta=%g;hiProtect=%g;sharpen=%g;saturation=%g;vibrance=%g;brightness=%g;contrast=%g;gamma=%g;shadows=%g;highlights=%g;grain=%g;grainSize=%g;deltaSmooth=%g;ghostGuard=%g;hudFeather=%g;hud=%s",
             p.passes, p.style, p.useAutoMask, p.intensity, p.localStructure, p.localTone, p.skinStructure, p.useFlow ? 1 : 0, p.workingScale, p.composeIntensity, p.maxDelta, p.hiProtect, p.sharpen, p.saturation, p.vibrance, p.brightness, p.contrast, p.gamma,
             p.shadows, p.highlights, p.grain, p.grainSize, p.deltaSmooth, p.ghostGuard, p.hudFeather, HudSerialize(p).c_str());
    return b;
}
static bool PresetApplyTo(const std::string& data, NrParams& p) {
    bool any = false; size_t pos = 0;
    while (pos < data.size()) {
        size_t end = data.find(';', pos); if (end == std::string::npos) end = data.size();
        const std::string tok = data.substr(pos, end - pos); pos = end + 1;
        const size_t eq = tok.find('='); if (eq == std::string::npos) continue;
        const std::string k = tok.substr(0, eq); const std::string vs = tok.substr(eq + 1); const float v = (float)atof(vs.c_str());
        if (k == "passes") p.passes = (uint32_t)std::clamp((int)v, 1, 4); else if (k == "style") p.style = (uint32_t)v; else if (k == "autoMask") p.useAutoMask = (uint32_t)v; else if (k == "intensity") p.intensity = v;
        else if (k == "localStructure") p.localStructure = v; else if (k == "localTone") p.localTone = v; else if (k == "skinStructure") p.skinStructure = v;
        else if (k == "useFlow") p.useFlow = v != 0.0f; else if (k == "workingScale") p.workingScale = std::clamp(v, 0.25f, 1.0f);
        else if (k == "composeIntensity") p.composeIntensity = v; else if (k == "maxDelta") p.maxDelta = v; else if (k == "hiProtect") p.hiProtect = v;
        else if (k == "sharpen") p.sharpen = std::clamp(v, 0.0f, 1.0f);
        else if (k == "saturation") p.saturation = std::clamp(v, 0.0f, 2.0f); else if (k == "vibrance") p.vibrance = std::clamp(v, 0.0f, 1.0f);
        else if (k == "brightness") p.brightness = std::clamp(v, -0.3f, 0.3f); else if (k == "contrast") p.contrast = std::clamp(v, 0.5f, 1.5f);
        else if (k == "gamma") p.gamma = std::clamp(v, 0.5f, 2.0f);
        else if (k == "shadows") p.shadows = std::clamp(v, -1.0f, 1.0f); else if (k == "highlights") p.highlights = std::clamp(v, -1.0f, 1.0f);
        else if (k == "grain") p.grain = std::clamp(v, 0.0f, 1.0f); else if (k == "grainSize") p.grainSize = std::clamp(v, 1.0f, 4.0f);
        else if (k == "ghostGuard") p.ghostGuard = std::clamp(v, 0.0f, 1.0f); else if (k == "deltaSmooth") p.deltaSmooth = std::clamp(v, 0.0f, 0.95f); else if (k == "hudFeather") p.hudFeather = std::clamp(v, 0.0f, 0.05f);
        else if (k == "hud") HudParse(vs, p); else continue;
        any = true;
    }
    return any;
}
static std::string PresetCleanName(std::string n) {   // names live in a '|'-joined list and a "key=value;..." store
    for (char& c : n) if (c == '|' || c == ';' || c == '=') c = '-';
    while (!n.empty() && n.back() == ' ') n.pop_back();
    if (n.size() > 40) n.resize(40);
    return n;
}
static IHost* g_host = nullptr;
static Config g_cfg; static std::mutex g_cfgMu;
static NrEngine g_engine; static Bridge g_bridge; static FrameTap g_tap; static Compose11 g_compose;
static std::atomic<bool> g_armed{ false }, g_killed{ false }, g_engineStarting{ false }, g_requestReset{ false };
static std::string g_status = "waiting for device", g_killReason; static std::mutex g_statusMu;
static ID3D11Device* g_dev = nullptr; static ID3D11DeviceContext* g_ctx = nullptr;
static LUID g_luid{}; static std::string g_adapterName; static bool g_hasDisplay = false; static bool g_engineLuidValid = false; static LUID g_engineLuid{};
static std::wstring g_lsDir, g_addonDir;
static FILE* g_logFile = nullptr; static std::mutex g_logMu;
static uint64_t g_nrRuns = 0; static double g_lastNrMs = 0, g_avgNrMs = 0, g_lastTotalMs = 0; static uint32_t g_watchdogHits = 0;
static std::string g_frameInfo;
// Everything below the dispatch/present hooks runs under g_tapMu: the tap, the compose, bridge/engine teardown, device bookkeeping.
static std::mutex g_tapMu;
static thread_local bool t_ownWork = false;         // our own compose dispatch is on LS's context: the dispatch hook must ignore it
struct SeenDevice { ID3D11Device* dev; bool ok; LUID luid; };
static std::vector<SeenDevice> g_seenDevs;          // devices that have dispatched, classified once each (not AddRef'd; reset on device events)
static ID3D11Device* g_tapDev = nullptr;            // the device the bridge is bound to
static LUID g_tapLuid{}; static bool g_tapLuidValid = false;   // adapter of the device that issues the LSFG tap; the engine follows it
static LUID g_pendingLuid{}; static uint32_t g_pendingTaps = 0; // taps seen on an adapter other than the engine's (switch after kSwitchTaps)
static const uint32_t kSwitchTaps = 20;
static LUID g_failedLuid{}; static bool g_failedLuidValid = false; // adapter the engine last failed on (no automatic retry there)
static bool SameLuid(const LUID& a, const LUID& b) { return a.LowPart == b.LowPart && a.HighPart == b.HighPart; }
// Display-only switches (not saved: Lossless Scaling always starts on the enhanced view).
static std::atomic<int> g_compare{ 0 };             // 0 enhanced, 1 split (left original | right enhanced), 2 original only
static std::atomic<float> g_splitPos{ 0.5f };
static std::atomic<bool> g_hudShow{ false };        // outline the protected HUD areas on screen (display only)
static std::string g_gameExe;                       // under g_statusMu: the program that had focus last (never LS's own windows)
static std::string g_gameSeen;                      // present thread only: the exe the per-game switch last acted on
static std::atomic<uint32_t> g_marker{ 0 }; static std::atomic<uint64_t> g_markerUntil{ 0 };   // corner square after a hotkey
static std::atomic<bool> g_watchdogKill{ false }; static std::atomic<uint64_t> g_rearmAtMs{ 0 }; static std::atomic<int> g_rearms{ 0 };
static uint64_t g_otherDispatches = 0; static int g_hookCount = 0; static std::string g_tapDevInfo = "none yet";
// present side
static uint64_t g_presents = 0, g_lsPresents = 0, g_composed = 0; static IDXGISwapChain* g_lastSwap = nullptr; static bool g_lastSwapIsLs = false; static IDXGISwapChain* g_lastSwapOther = nullptr;  // g_lastSwap: LS's chain; g_lastSwapOther: the last one seen on another device
static uint64_t g_lastDelta = 0; static double g_lastOffset = 0; static double g_lastTarget = 0;
static uint64_t g_presentStage[8] = {};   // diagnostics: how far each present gets

// Wrapped tooltip for the widget just submitted, after a short hover delay.
static void Tip(const char* text) {
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled)) return;
    const char* hint = lsp::SliderHint();   // sliders add "default, double-click resets, Ctrl+scroll fine-tunes"
    ImGui::BeginTooltip(); ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f); ImGui::TextUnformatted(text);
    if (hint) { ImGui::Dummy(ImVec2(0, 2)); ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]); ImGui::TextUnformatted(hint); ImGui::PopStyleColor(); }
    ImGui::PopTextWrapPos(); ImGui::EndTooltip();
}
// Wrapped help text in the disabled colour (ImGui::TextDisabled does not wrap, so long lines ran off the panel).
static void Note(const char* fmt, ...) {
    char b[1024]; va_list a; va_start(a, fmt); vsnprintf(b, sizeof b, fmt, a); va_end(a);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
    ImGui::TextWrapped("%s", b);
    ImGui::PopStyleColor();
}
// Every collapsible section of the panel starts closed (lsp::SectionHeader with no second argument); the person opens what they need.
// A titled block of the panel: some room, a thin line, the title in the small capitals of the other apps, and a little room under it.
static void Block(const char* title, bool first = false) {
    const float u = ImGui::GetFontSize();
    if (!first) { ImGui::Dummy(ImVec2(0, u * 0.7f)); ImGui::Separator(); ImGui::Dummy(ImVec2(0, u * 0.5f)); }
    lsp::SectionLabel(title);
    ImGui::Dummy(ImVec2(0, u * 0.35f));
}
static void SetStatus(const char* s) { std::lock_guard<std::mutex> lk(g_statusMu); g_status = s; }
static std::string GetStatus() { std::lock_guard<std::mutex> lk(g_statusMu); return g_status; }

static void Log(const char* fmt, ...) {
    char b[1024]; va_list a; va_start(a, fmt); vsnprintf(b, sizeof b, fmt, a); va_end(a);
    std::lock_guard<std::mutex> lk(g_logMu);
    if (g_host) g_host->Log(LSPROXY_LOG_INFO, b);
    if (g_logFile) { SYSTEMTIME t; GetLocalTime(&t); fprintf(g_logFile, "[%02d:%02d:%02d.%03d] %s\n", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, b); fflush(g_logFile); }
}
static void Kill(const char* why) { g_killed = true; { std::lock_guard<std::mutex> lk(g_statusMu); g_killReason = why; } Log("DISABLED: %s", why); }

// ------------------------------------------------------------------ crash diagnostics
// This DLL links the CRT statically, so the manager's abort / terminate / invalid-parameter handlers (which live
// in the host's copy of the CRT) never see a failure that starts here: an exception escaping one of our threads
// ends Lossless Scaling with only a fast-fail event in Windows and nothing in any log. Write down what happened
// and where before the process goes. Offsets resolve against the DLSS5NR01.map built next to the DLL.
static void CrashLog(const char* fmt, ...) {
    char b[512]; va_list a; va_start(a, fmt); vsnprintf(b, sizeof b, fmt, a); va_end(a);
    const bool locked = g_logMu.try_lock();   // never wait on a lock the crashing thread may hold
    if (g_logFile) { fprintf(g_logFile, "%s\n", b); fflush(g_logFile); }
    if (locked) g_logMu.unlock();
}
static void CrashBacktrace(const char* why) {
    static std::atomic<bool> s_once{ false };
    if (s_once.exchange(true)) return;      // terminate() calls abort(); report once
    CrashLog("CRASH: %s (thread %lu)", why, GetCurrentThreadId());
    void* frames[40] = {};
    const USHORT n = CaptureStackBackTrace(0, 40, frames, nullptr);
    for (USHORT i = 0; i < n; i++) {
        HMODULE m = nullptr; wchar_t path[MAX_PATH] = L"?";
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)frames[i], &m) && m)
            GetModuleFileNameW(m, path, MAX_PATH);
        const wchar_t* base = wcsrchr(path, L'\\'); base = base ? base + 1 : path;
        CrashLog("  #%u %ls+0x%llx", (unsigned)i, base, (unsigned long long)((uintptr_t)frames[i] - (uintptr_t)m));
    }
}
static void __cdecl NrOnAbort(int) { CrashBacktrace("abort() called"); }
static void NrOnTerminate() {
    if (std::exception_ptr ep = std::current_exception()) {
        try { std::rethrow_exception(ep); }
        catch (const std::exception& e) { CrashLog("CRASH: uncaught C++ exception: %s", e.what()); }
        catch (...) { CrashLog("CRASH: uncaught non-standard exception"); }
    }
    CrashBacktrace("std::terminate");
    abort();
}
static void __cdecl NrOnInvalidParameter(const wchar_t*, const wchar_t*, const wchar_t*, unsigned, uintptr_t) {
    CrashBacktrace("invalid CRT parameter");
}
static void InstallCrashDiagnostics() {
    std::set_terminate(NrOnTerminate);
    std::signal(SIGABRT, NrOnAbort);
    _set_invalid_parameter_handler(NrOnInvalidParameter);
}

// ------------------------------------------------------------------ config persistence
static std::string CfgGet(const char* k, const char* def) { return g_host ? g_host->GetConfig(kAddonId, k, def) : def; }
static void CfgSet(const char* k, const std::string& v) { if (g_host) g_host->SetConfig(kAddonId, k, v.c_str()); }
static float CfgGetF(const char* k, float d) { std::string s = CfgGet(k, ""); return s.empty() ? d : (float)atof(s.c_str()); }
static int CfgGetI(const char* k, int d) { std::string s = CfgGet(k, ""); return s.empty() ? d : atoi(s.c_str()); }
static void CfgSetF(const char* k, float v) { char b[32]; snprintf(b, 32, "%g", v); CfgSet(k, b); }
static void CfgSetI(const char* k, int v) { CfgSet(k, std::to_string(v)); }

static void LoadConfig() {
    std::lock_guard<std::mutex> lk(g_cfgMu); Config& c = g_cfg;
    c.enabled = CfgGetI("enabled", 1) != 0;
    c.p.style = CfgGetI("style", 0); c.p.useAutoMask = CfgGetI("autoMask", 1);
    c.p.intensity = CfgGetF("intensity", 1.0f); c.p.localStructure = CfgGetF("localStructure", 1.0f); c.p.localTone = CfgGetF("localTone", 1.0f); c.p.skinStructure = CfgGetF("skinStructure", -1.0f);
    c.p.useFlow = CfgGetI("useFlow", 1) != 0; c.p.flowUnit = CfgGetF("flowUnit", 2.0f);
    c.p.workingScale = CfgGetF("workingScale", 0.35f); c.p.composeIntensity = CfgGetF("composeIntensity", 1.0f); c.p.maxDelta = CfgGetF("maxDelta", 0.5f); c.p.ghostGuard = std::clamp(CfgGetF("ghostGuard", 0.5f), 0.0f, 1.0f);
    c.p.hiProtect = CfgGetF("hiProtect", 0.85f); c.p.debugView = CfgGetI("debugView", 0);
    if (c.p.debugView > 5) c.p.debugView = 0;
    c.lsFirst = CfgGetI("lsFirst", 1) != 0;
    c.p.passes = (uint32_t)std::clamp(CfgGetI("passes", 1), 1, 4);
    c.p.sharpen = std::clamp(CfgGetF("sharpen", 0.0f), 0.0f, 1.0f);
    c.p.saturation = std::clamp(CfgGetF("saturation", 1.0f), 0.0f, 2.0f); c.p.vibrance = std::clamp(CfgGetF("vibrance", 0.0f), 0.0f, 1.0f);
    c.p.brightness = std::clamp(CfgGetF("brightness", 0.0f), -0.3f, 0.3f); c.p.contrast = std::clamp(CfgGetF("contrast", 1.0f), 0.5f, 1.5f); c.p.gamma = std::clamp(CfgGetF("gamma", 1.0f), 0.5f, 2.0f);
    c.p.shadows = std::clamp(CfgGetF("shadows", 0.0f), -1.0f, 1.0f); c.p.highlights = std::clamp(CfgGetF("highlights", 0.0f), -1.0f, 1.0f);
    c.p.grain = std::clamp(CfgGetF("grain", 0.0f), 0.0f, 1.0f); c.p.grainSize = std::clamp(CfgGetF("grainSize", 1.0f), 1.0f, 4.0f);
    c.p.deltaSmooth = std::clamp(CfgGetF("deltaSmooth", 0.0f), 0.0f, 0.95f); c.p.hudFeather = std::clamp(CfgGetF("hudFeather", 0.004f), 0.0f, 0.05f);
    HudParse(CfgGet("hud", ""), c.p);
    c.hotkeys = CfgGetI("hotkeys", 1) != 0;
    c.keyAB = CfgGetI("keyAB", VK_F6); c.keySplit = CfgGetI("keySplit", VK_F7); c.keySharpDn = CfgGetI("keySharpDn", VK_F8); c.keySharpUp = CfgGetI("keySharpUp", VK_F9);
    c.keyPreset = CfgGetI("keyPreset", VK_F10);
    c.gameAuto = CfgGetI("gameAuto", 1) != 0; c.games.clear();
    { const std::string names = CfgGet("gameList", ""); size_t pos = 0;
      while (pos < names.size()) { size_t end = names.find('|', pos); if (end == std::string::npos) end = names.size();
          const std::string n = names.substr(pos, end - pos); pos = end + 1;
          const std::string pn = n.empty() ? std::string() : CfgGet(("game." + n).c_str(), "");
          if (!pn.empty()) c.games.push_back({ n, pn }); } }
    g_presets.clear();
    { const std::string names = CfgGet("presetNames", ""); size_t pos = 0;
      while (pos < names.size()) { size_t end = names.find('|', pos); if (end == std::string::npos) end = names.size();
          const std::string n = names.substr(pos, end - pos); pos = end + 1;
          const std::string d = n.empty() ? std::string() : CfgGet(("preset." + n).c_str(), "");
          if (!d.empty()) g_presets.push_back({ n, d }); } }
    // Startup view, for the offline test host only (the panel and the hotkeys change it, nothing saves it).
    g_compare = std::clamp(CfgGetI("compareStart", 0), 0, 2); g_splitPos = std::clamp(CfgGetF("splitStart", 0.5f), 0.05f, 0.95f);
    c.freshFlow = CfgGetI("freshFlow", 1) != 0;
    c.tapMode = CfgGetI("tapMode", 0); c.frameSlot = CfgGetI("frameSlot", -1); c.tickSig = CfgGet("tickSig", ""); c.tapSig = CfgGet("tapSig", "");
    c.watchdogMs = CfgGetF("watchdogMs", 80.0f); c.snippetPath = CfgGet("snippetPath", "");
}
static void SaveConfig() {
    Config c; { std::lock_guard<std::mutex> lk(g_cfgMu); c = g_cfg; }
    CfgSetI("enabled", c.enabled); CfgSetI("style", c.p.style); CfgSetI("autoMask", c.p.useAutoMask);
    CfgSetF("intensity", c.p.intensity); CfgSetF("localStructure", c.p.localStructure); CfgSetF("localTone", c.p.localTone); CfgSetF("skinStructure", c.p.skinStructure);
    CfgSetI("useFlow", c.p.useFlow); CfgSetF("flowUnit", c.p.flowUnit);
    CfgSetF("workingScale", c.p.workingScale); CfgSetF("composeIntensity", c.p.composeIntensity); CfgSetF("maxDelta", c.p.maxDelta); CfgSetF("ghostGuard", c.p.ghostGuard);
    CfgSetF("hiProtect", c.p.hiProtect); CfgSetI("debugView", c.p.debugView);
    CfgSetI("lsFirst", c.lsFirst);
    CfgSetI("passes", (int)c.p.passes); CfgSetF("sharpen", c.p.sharpen); CfgSetF("saturation", c.p.saturation); CfgSetF("vibrance", c.p.vibrance); CfgSetF("brightness", c.p.brightness); CfgSetF("contrast", c.p.contrast); CfgSetF("gamma", c.p.gamma); CfgSetI("hotkeys", c.hotkeys);
    CfgSetF("shadows", c.p.shadows); CfgSetF("highlights", c.p.highlights); CfgSetF("grain", c.p.grain); CfgSetF("grainSize", c.p.grainSize);
    CfgSetF("deltaSmooth", c.p.deltaSmooth); CfgSetF("hudFeather", c.p.hudFeather); CfgSet("hud", HudSerialize(c.p));
    { CfgSetI("gameAuto", c.gameAuto); std::string gl; for (auto& g : c.games) { if (!gl.empty()) gl += '|'; gl += g.first; CfgSet(("game." + g.first).c_str(), g.second); } CfgSet("gameList", gl); }
    CfgSetI("keyAB", c.keyAB); CfgSetI("keySplit", c.keySplit); CfgSetI("keySharpDn", c.keySharpDn); CfgSetI("keySharpUp", c.keySharpUp); CfgSetI("keyPreset", c.keyPreset);
    { std::string names; std::lock_guard<std::mutex> lk(g_cfgMu);
      for (auto& pr : g_presets) { if (!names.empty()) names += '|'; names += pr.name; CfgSet(("preset." + pr.name).c_str(), pr.data); }
      CfgSet("presetNames", names); }
    CfgSetI("freshFlow", c.freshFlow ? 1 : 0);
    CfgSetI("tapMode", c.tapMode); CfgSetI("frameSlot", c.frameSlot); CfgSet("tickSig", c.tickSig); CfgSet("tapSig", c.tapSig);
    CfgSetF("watchdogMs", c.watchdogMs); CfgSet("snippetPath", c.snippetPath);
    if (g_host) g_host->SaveConfig();
}
static void ApplyTapRoles() {
    Config c; { std::lock_guard<std::mutex> lk(g_cfgMu); c = g_cfg; }
    DispatchSig tick, tap; tick.Parse(c.tickSig); tap.Parse(c.tapSig);
    g_tap.SetRoles(tick, tap, c.tapMode == 1 ? FrameTap::Manual : FrameTap::Auto, c.frameSlot);
    g_tap.SetFreshFlow(c.freshFlow);
}

// ------------------------------------------------------------------ engine lifecycle
static std::wstring Narrow2Wide(const std::string& s) { std::wstring w(s.size(), L' '); int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], (int)w.size()); w.resize(n > 0 ? n : 0); return w; }
static std::string Wide2Narrow(const std::wstring& w) { std::string s(w.size() * 3, ' '); int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], (int)s.size(), nullptr, nullptr); s.resize(n > 0 ? n : 0); return s; }

// ---- Requirements (requirements.h). What is on this machine is gathered off the UI thread (it reads file headers, including the model's, which is
// large); the engine's own state is added when the panel is drawn, so it is always current.
static std::mutex g_reqMu; static req::Inputs g_reqIn; static bool g_reqHave = false;
// These two helper threads are detached and tracked by a flag, never kept as a std::thread object: Lossless Scaling ends the process without
// calling AddonShutdown, and a std::thread that is still joinable when this DLL's statics are destroyed calls std::terminate (a crash at exit).
static std::atomic<bool> g_reqBusy{ false };
static std::wstring ModelPathNow() {
    std::string sp; { std::lock_guard<std::mutex> lk(g_cfgMu); sp = g_cfg.snippetPath; }
    return sp.empty() ? g_lsDir + L"\\nvngx_dlssnr.dll" : Narrow2Wide(sp);
}
static void ScanRequirementsBody(const std::wstring& model, const std::wstring& addonDir) {
    const req::Inputs in = req::Gather(model, addonDir);
    const req::Report rep = req::Evaluate(in);
    for (const auto& r : rep.rows) Log("requirements: %s: %s%s", r.label.c_str(), r.value.c_str(), r.level == req::Level::Ok ? "" : (r.level == req::Level::Note ? "  [note]" : "  [MISSING]"));
    std::lock_guard<std::mutex> lk(g_reqMu); g_reqIn = in; g_reqHave = true;
}
static void ScanRequirementsGuarded(const std::wstring& model, const std::wstring& addonDir) {   // no objects here: __try cannot unwind them
    __try { ScanRequirementsBody(model, addonDir); }
    __except (EXCEPTION_EXECUTE_HANDLER) { Log("requirements: the check crashed (0x%08lx)", GetExceptionCode()); }
}
static void ScanRequirements() {
    if (g_reqBusy.exchange(true)) return;
    const std::wstring model = ModelPathNow(), addonDir = g_addonDir;
    std::thread([model, addonDir] { ScanRequirementsGuarded(model, addonDir); g_reqBusy = false; }).detach();
}
// The compatibility self-test: nr_selftest.exe loads the model in its own process and tries it once on the graphics card; what it says appears as a row.
// A detached thread tracked by a flag (see above); the test program itself is in a job that ends it if Lossless Scaling ends first.
static std::atomic<bool> g_selfTestBusy{ false };
static req::SelfTestState g_selfTestState = req::SelfTestState::NotRun;   // these three are guarded by g_reqMu
static std::string g_selfTestKey, g_selfTestText;
static void SelfTestBody() {
    { std::lock_guard<std::mutex> lk(g_reqMu); g_selfTestState = req::SelfTestState::Running; g_selfTestKey.clear(); g_selfTestText.clear(); }
    const req::SelfTestResult r = req::RunSelfTest(g_addonDir, ModelPathNow(), g_lsDir);
    Log("compatibility test: %s (%s): %s", r.passed ? "passed" : "FAILED", r.key.c_str(), r.text.c_str());
    std::lock_guard<std::mutex> lk(g_reqMu);
    g_selfTestState = r.passed ? req::SelfTestState::Passed : req::SelfTestState::Failed;
    g_selfTestKey = r.key; g_selfTestText = r.text;
}
static void SelfTestGuarded() {   // no objects here: __try cannot unwind them
    __try { SelfTestBody(); }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("compatibility test crashed inside the addon (0x%08lx)", GetExceptionCode());
        g_selfTestState = req::SelfTestState::Failed;
    }
}
static void RunSelfTestAsync() {
    if (g_selfTestBusy.exchange(true)) return;
    std::thread([] { SelfTestGuarded(); g_selfTestBusy = false; }).detach();
}

// "Browse for the model file": the user picks their own copy of nvngx_dlssnr.dll and it is copied into the Lossless Scaling folder (req::PlaceModel).
// The file dialog is modal and blocks its thread, so it runs on a worker thread, never on the manager's UI thread.
static std::atomic<bool> g_placeBusy{ false };
static std::string g_placeMsg; static bool g_placeOk = false;   // guarded by g_reqMu
static std::wstring PickModelFile() {
    wchar_t file[MAX_PATH * 2] = {};
    OPENFILENAMEW o = {}; o.lStructSize = sizeof o;
    o.hwndOwner = FindWindowW(L"EchoAddonManagerClass", nullptr);
    o.lpstrFilter = L"DLL files (*.dll)\0*.dll\0All files\0*.*\0";
    o.lpstrFile = file; o.nMaxFile = MAX_PATH * 2;
    o.lpstrTitle = L"Pick your copy of nvngx_dlssnr.dll";
    o.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
    return GetOpenFileNameW(&o) ? std::wstring(file) : std::wstring();
}
static void PlaceModelBody() {
    const std::wstring picked = PickModelFile();
    if (picked.empty()) { std::lock_guard<std::mutex> lk(g_reqMu); g_placeMsg = "No file picked."; g_placeOk = false; return; }
    const req::PlaceResult r = req::PlaceModel(picked, g_lsDir, g_lsDir + L"\\backups");
    Log("place model: %s (%s)", r.message.c_str(), r.ok ? "ok" : "refused");
    { std::lock_guard<std::mutex> lk(g_reqMu); g_placeMsg = r.message; g_placeOk = r.ok; }
    if (r.ok) { ScanRequirements(); RunSelfTestAsync(); }   // a new file: see at once whether it works on this graphics card
}
static void PlaceModelGuarded() {   // no objects here: __try cannot unwind them
    __try { PlaceModelBody(); }
    __except (EXCEPTION_EXECUTE_HANDLER) { Log("place model crashed (0x%08lx)", GetExceptionCode()); }
}
static void BrowseAndPlace() {
    if (g_placeBusy.exchange(true)) return;
    std::thread([] { PlaceModelGuarded(); g_placeBusy = false; }).detach();
}
static req::Report RequirementsNow(bool engineFailed, bool engineReady, bool engineRunning, const char* engineError) {
    req::Inputs in; { std::lock_guard<std::mutex> lk(g_reqMu); in = g_reqIn; in.selfTest = g_selfTestState; in.selfTestKey = g_selfTestKey; in.selfTestText = g_selfTestText; }
    in.engine = engineFailed ? req::EngineState::Failed : (engineRunning ? req::EngineState::Running : (engineReady ? req::EngineState::Ready : req::EngineState::NotStarted));
    if (engineFailed && engineError) in.engineError = engineError;
    return req::Evaluate(in);
}

static void StartEngine(LUID luid) {
    if (g_engineStarting.exchange(true)) return;
    // Detached and tracked by g_engineStarting (cleared on every path out): a std::thread object kept in a static would still be joinable if the
    // process ends without AddonShutdown, and destroying it then calls std::terminate.
    std::thread([luid]() {
      try {
        { std::lock_guard<std::mutex> lk(g_tapMu); g_bridge.Shutdown(); if (g_engine.IsReady() || g_engine.IsFailed()) g_engine.Shutdown(); }
        std::string sp; { std::lock_guard<std::mutex> lk(g_cfgMu); sp = g_cfg.snippetPath; }
        std::wstring snippet = sp.empty() ? g_lsDir + L"\\nvngx_dlssnr.dll" : Narrow2Wide(sp);
        SetStatus("engine: loading model...");
        bool ok = g_engine.Init(luid, g_addonDir + L"\\" NR_FORWARDER_FILENAME, snippet, g_addonDir, g_lsDir, [](const char* m) { Log("%s", m); });
        g_engineLuid = luid; g_engineLuidValid = true;
        if (!ok) { g_failedLuid = luid; g_failedLuidValid = true; Log("engine failed on LUID %08x:%08x; it will start again when LS runs LSFG on another NVIDIA adapter", luid.HighPart, luid.LowPart); }
        SetStatus(ok ? "engine ready" : g_engine.Stats().lastError);
        g_engineStarting = false;
      } catch (const std::exception& e) {
        Log("engine start threw: %s", e.what());
        g_failedLuid = luid; g_failedLuidValid = true;
        SetStatus("engine failed (exception; see the log)"); g_engineStarting = false;
      } catch (...) {
        Log("engine start threw a non-standard exception");
        g_failedLuid = luid; g_failedLuidValid = true;
        SetStatus("engine failed (exception; see the log)"); g_engineStarting = false;
      }
    }).detach();
}

static void DropDevice() {   // under g_tapMu
    g_bridge.Shutdown(); g_compose.Shutdown(); g_tap.Reset(); g_seenDevs.clear(); g_tapDev = nullptr; g_lastSwap = nullptr; g_lastSwapOther = nullptr; g_lastSwapIsLs = false;
}
static void OnDeviceEvent(uint32_t id, const void*, uint32_t, void*) {
    if (id == LSPROXY_EVENT_D3D11_DEVICE_CHANGED) {
        std::lock_guard<std::mutex> lk(g_tapMu);
        DropDevice(); g_dev = nullptr; g_ctx = nullptr; return;
    }
    // DEVICE_READY
    g_dev = (ID3D11Device*)g_host->GetD3D11Device(); g_ctx = (ID3D11DeviceContext*)g_host->GetD3D11DeviceContext();
    { std::lock_guard<std::mutex> lk(g_tapMu); DropDevice(); }
    { std::lock_guard<std::mutex> lk(g_statusMu); g_hasDisplay = false; g_adapterName = "?"; }
    if (!g_dev) return;
    IDXGIDevice* dx = nullptr;
    if (SUCCEEDED(g_dev->QueryInterface(IID_PPV_ARGS(&dx))) && dx) {
        IDXGIAdapter* ad = nullptr;
        if (SUCCEEDED(dx->GetAdapter(&ad)) && ad) {
            DXGI_ADAPTER_DESC d; ad->GetDesc(&d); g_luid = d.AdapterLuid;
            IDXGIOutput* o = nullptr; const bool display = SUCCEEDED(ad->EnumOutputs(0, &o)) && o; if (o) o->Release();
            { std::lock_guard<std::mutex> lk(g_statusMu); g_adapterName = Wide2Narrow(d.Description); g_hasDisplay = display; }
            bool nvidia = (d.VendorId == 0x10DE);
            g_armed = nvidia;
            Log("device %p on '%s' LUID %08x:%08x display=%d -> %s", g_dev, Wide2Narrow(d.Description).c_str(), d.AdapterLuid.HighPart, d.AdapterLuid.LowPart, (int)display, nvidia ? "NVIDIA, ok" : "not NVIDIA, ignored");
            if (!nvidia) SetStatus("waiting: LS device is not an NVIDIA adapter");
            else if (!g_engine.IsReady()) SetStatus("waiting for LSFG dispatches");
            ad->Release();
        }
        dx->Release();
    }
    // The engine is started from the tap, on the adapter of the device that actually runs LSFG (single GPU,
    // hybrid laptop, or either card of a dual-GPU rig) — not from device events, which fire for every device LS makes.
}

// ------------------------------------------------------------------ the tap (LS render thread)
static const char* FmtName(uint32_t f) {
    switch (f) { case 87: return "BGRA8"; case 91: return "BGRA8s"; case 28: return "RGBA8"; case 29: return "RGBA8s"; case 24: return "RGB10A2"; case 10: return "RGBA16F"; case 34: return "RG16F"; case 16: return "RG32F"; case 49: return "RG8"; case 41: return "R32F"; case 54: return "R16F"; case 61: return "R8"; case 26: return "R11G11B10F"; case 2: return "RGBA32F"; default: return "?"; }
}

// Live numbers for the host's Performance tab and the addon's card (host 0.5.0 and newer; an older host has no such calls).
static bool HostHasLive() { return g_host && g_host->GetHostVersion() >= 0x010000; }   // API 1.0 has SetStatus / PublishMetric
static void PublishLive(const NrStats& st) {
    if (!HostHasLive()) return;
    static uint64_t s_slowAt = 0, s_statusAt = 0, s_prevRuns = 0, s_prevSkipped = 0; static double s_keep = 100.0;
    const double iv = g_bridge.LastIntervalMs();
    if (iv > 0) g_host->PublishMetric(kAddonId, "frame_ms", iv, "ms");
    const uint64_t now = GetTickCount64();
    if (now - s_slowAt >= 200) {   // five times a second is plenty for the slower numbers
        s_slowAt = now;
        const uint64_t runs = g_bridge.Runs(), skipped = g_bridge.Skipped();
        const uint64_t dr = runs - s_prevRuns, ds = skipped - s_prevSkipped; s_prevRuns = runs; s_prevSkipped = skipped;
        if (dr + ds > 0) s_keep = 100.0 * (double)dr / (double)(dr + ds);
        g_host->PublishMetric(kAddonId, "model_ms", st.nrMs, "ms");
        g_host->PublishMetric(kAddonId, "model_total_ms", st.totalMs, "ms");
        g_host->PublishMetric(kAddonId, "gpu_start_ms", st.startMs, "ms");
        g_host->PublishMetric(kAddonId, "keepup_pct", s_keep, "%");
        g_host->PublishMetric(kAddonId, "tap_cpu_ms", g_bridge.CpuMs(), "ms");
    }
    if (now - s_statusAt >= 1000) {
        s_statusAt = now;
        char b[96]; snprintf(b, sizeof b, s_keep >= 90.0 ? "Running, model %.1f ms, keeps up %.0f%%" : "Model is behind: %.1f ms, keeps up %.0f%%", st.nrMs, s_keep);
        g_host->SetStatus(kAddonId, b, s_keep >= 90.0 ? 1 : 2);
    }
}
static void OnPresent(IDXGISwapChain* sc, void* user);
static void ReleaseDecision(TapDecision& d) { if (d.frame) d.frame->Release(); if (d.flow) d.flow->Release(); d.frame = nullptr; d.flow = nullptr; }
static bool TapBody(ID3D11DeviceContext* ctx, uint32_t x, uint32_t y, uint32_t z) {
    TapDecision d; bool run = g_tap.Observe(ctx, x, y, z, d);
    if (!run) { ReleaseDecision(d); return false; }
    if (!g_tapDev) { ReleaseDecision(d); return false; }
    LUID want{}; for (auto& s : g_seenDevs) if (s.dev == g_tapDev) want = s.luid;
    g_tapLuid = want; g_tapLuidValid = true;
    bool engineMatches = g_engine.IsReady() && g_engineLuidValid && SameLuid(g_engineLuid, want);
    if (!engineMatches) {
        ReleaseDecision(d);
        if (g_engineStarting) return false;
        if (g_engine.IsFailed() && g_failedLuidValid && SameLuid(g_failedLuid, want)) return false;   // this adapter can't run NGX; wait for LS to move
        // Hysteresis: LS creates devices on every adapter and may run this pass on more than one for a moment.
        // Only follow an adapter that keeps issuing the tap while the engine's adapter stays silent.
        bool firstStart = !g_engineLuidValid;
        if (!firstStart) { if (!SameLuid(g_pendingLuid, want)) { g_pendingLuid = want; g_pendingTaps = 0; } if (++g_pendingTaps < kSwitchTaps) return false; }
        g_pendingTaps = 0;
        Log("engine follows the LSFG device: starting on LUID %08x:%08x", want.HighPart, want.LowPart); StartEngine(want);
        return false;   // table keeps filling while the model loads
    }
    g_pendingTaps = 0;
    if (!g_bridge.IsReady() && !g_bridge.Init(g_tapDev, ctx, &g_engine, [](const char* m) { Log("%s", m); })) { Kill("bridge init failed"); ReleaseDecision(d); return false; }
    if (!g_compose.IsReady() && !g_compose.Init(g_tapDev, [](const char* m) { Log("%s", m); })) { Kill("compose init failed"); ReleaseDecision(d); return false; }
    if (!PresentHook::Installed() && !PresentHook::Install(g_tapDev, OnPresent, nullptr, [](const char* m) { Log("%s", m); })) { Kill("could not hook dxgi Present"); ReleaseDecision(d); return false; }
    D3D11_TEXTURE2D_DESC td; d.frame->GetDesc(&td);
    if (td.Width < 64 || td.Height < 64) {   // a minimised or mid-resize capture: nothing to enhance, and no reason to rebuild the model for it
        static uint64_t s_lastTinyTap = 0; if (g_tap.Taps() - s_lastTinyTap > 600) { s_lastTinyTap = g_tap.Taps(); Log("skipping a %ux%u capture (too small)", td.Width, td.Height); }
        ReleaseDecision(d); return false;
    }
    { char b[96]; snprintf(b, sizeof b, "%ux%u %s slot %d", td.Width, td.Height, FmtName(td.Format), d.frameSlot); std::lock_guard<std::mutex> lk(g_statusMu); g_frameInfo = b; }
    if (!g_bridge.Ensure(td.Width, td.Height, td.Format)) { SetStatus("unsupported frame format"); ReleaseDecision(d); return false; }
    NrParams p; float wd; bool lsFirst; { std::lock_guard<std::mutex> lk(g_cfgMu); p = g_cfg.p; wd = g_cfg.watchdogMs; lsFirst = g_cfg.lsFirst; }
    g_bridge.SetLsGpuPriority(lsFirst ? 7 : 0);
    bool reset = g_requestReset.exchange(false);
    ID3D11ShaderResourceView* saved[8] = {}; ctx->CSGetShaderResources(0, 8, saved);
    ID3D11ShaderResourceView* nulls[8] = {}; ctx->CSSetShaderResources(0, 8, nulls);
    bool started = g_bridge.Submit(d.frame, d.flow, d.flowW, d.flowH, p, reset, g_tap.Taps());
    ctx->CSSetShaderResources(0, 8, saved); for (auto* s : saved) if (s) s->Release();
    ReleaseDecision(d);
    const NrStats& st = g_engine.Stats();
    if (started) { g_nrRuns++; g_lastNrMs = st.nrMs; g_lastTotalMs = st.totalMs; g_avgNrMs = g_avgNrMs == 0 ? st.nrMs : g_avgNrMs * 0.95 + st.nrMs * 0.05; }
    if (g_engine.IsFailed()) Kill(st.lastError);
    PublishLive(st);
    if (st.nrMs > wd) {
        if (++g_watchdogHits >= 30) {
            Kill("NR slower than watchdog threshold for 30 frames");
            g_watchdogKill = true; g_rearmAtMs = GetTickCount64() + 10000;   // a loading screen or a focus change is not a reason to stay off
        }
    } else g_watchdogHits = 0;
    if (g_nrRuns == 1) SetStatus("running");
    const uint64_t taps = g_tap.Taps();
    if (taps == 1 || taps == 60 || taps % 300 == 0)
        Log("tap #%llu: model %.1f ms (avg %.1f), run %.1f ms, GPU start +%.1f done +%.1f ms after submit, tap CPU %.2f ms, interval %.1f ms, runs %llu skipped %llu, fails %llu | presents %llu (%s), composed %llu, compose CPU %.2f ms, last delta frame %llu offset %.2f",
            (unsigned long long)taps, st.nrMs, g_avgNrMs, st.totalMs, st.startMs, st.doneMs, g_bridge.CpuMs(), g_bridge.IntervalMs(), (unsigned long long)g_bridge.Runs(), (unsigned long long)g_bridge.Skipped(), (unsigned long long)st.fails,
            (unsigned long long)g_lsPresents, g_tap.PresentPattern(), (unsigned long long)g_composed, g_compose.CpuMs(), (unsigned long long)g_lastDelta, g_lastOffset);
    if (taps == 60 || taps % 300 == 0)
        Log("motion vectors so far: this frame's flow %llu, the previous frame's %llu, frames dropped waiting for a flow pass %llu",
            (unsigned long long)g_tap.FreshRuns(), (unsigned long long)g_tap.StaleRuns(), (unsigned long long)g_tap.DroppedWaiting());
    if (taps % 300 == 0) {   // how the game's frame time was distributed over the last 300 frames (the line above is smoothed)
        float p50, p95, p99, worst; int n, o20, o33;
        if (g_bridge.TakeFrameTimeWindow(p50, p95, p99, worst, n, o20, o33)) {
            if (HostHasLive()) { g_host->PublishMetric(kAddonId, "frame_p50_ms", p50, "ms"); g_host->PublishMetric(kAddonId, "frame_p95_ms", p95, "ms"); g_host->PublishMetric(kAddonId, "frame_p99_ms", p99, "ms"); }
            Log("frame time over the last %d frames: p50 %.1f ms, p95 %.1f, p99 %.1f, worst %.1f | %d frames over 20 ms (%.0f%%), %d over 33 ms | model %.1f ms, GPU start +%.1f ms",
                n, p50, p95, p99, worst, o20, 100.0 * o20 / n, o33, st.nrMs, st.startMs);
        }
    }
    if (taps == 60) PresentHook::DumpState([](const char* m) { Log("%s", m); });
    if (taps == 60) Log("present stages: hook hits %u, body %llu, ready %llu, noted %llu, targeted %llu, with delta %llu", PresentHook::Hits(), (unsigned long long)g_presentStage[0], (unsigned long long)g_presentStage[1], (unsigned long long)g_presentStage[2], (unsigned long long)g_presentStage[3], (unsigned long long)g_presentStage[4]);
    return false;   // never skip LS's own dispatch
}
static int SehFilter(unsigned code, const char* where) { char b[64]; snprintf(b, sizeof b, "exception 0x%08x in %s", code, where); Kill(b); return EXCEPTION_EXECUTE_HANDLER; }
static bool TapGuarded(ID3D11DeviceContext* ctx, uint32_t x, uint32_t y, uint32_t z) {
    __try { return TapBody(ctx, x, y, z); } __except (SehFilter(GetExceptionCode(), "tap")) { return false; }
}
// The inline hook sees every device in the process (LS creates several, on both GPUs). Classify each
// device once: only the one on the armed adapter (NVIDIA, drives the display) is tapped. Runs under g_tapMu.
static bool DeviceUsable(ID3D11DeviceContext* ctx) {
    ID3D11Device* dev = nullptr; ctx->GetDevice(&dev); if (!dev) return false;
    for (auto& s : g_seenDevs) if (s.dev == dev) { dev->Release(); return s.ok; }
    bool ok = false; std::string name = "?"; LUID luid{};
    IDXGIDevice* dx = nullptr;
    if (SUCCEEDED(dev->QueryInterface(IID_PPV_ARGS(&dx))) && dx) {
        IDXGIAdapter* ad = nullptr;
        if (SUCCEEDED(dx->GetAdapter(&ad)) && ad) {
            DXGI_ADAPTER_DESC d; ad->GetDesc(&d); luid = d.AdapterLuid; name = Wide2Narrow(d.Description);
            ok = (d.VendorId == 0x10DE);
            ad->Release();
        }
        dx->Release();
    }
    if (g_seenDevs.size() < 32) g_seenDevs.push_back({ dev, ok, luid });
    if (ok && g_tapDev != dev) { g_bridge.Shutdown(); g_compose.Shutdown(); g_tap.Reset(); g_tapDev = dev; g_lastSwap = nullptr; g_lastSwapOther = nullptr; g_lastSwapIsLs = false; }
    char b[192]; snprintf(b, sizeof b, "%p on %s (LUID %08x) -> %s", (void*)dev, name.c_str(), (unsigned)luid.LowPart, ok ? "TAPPED" : "ignored");
    if (ok) { std::lock_guard<std::mutex> lk(g_statusMu); g_tapDevInfo = b; }
    Log("dispatching device %s", b);
    dev->Release(); return ok;
}
static void ShapeStr(const DispatchSig& s, char* out, size_t n);
// Every few seconds, if new dispatch shapes appeared, write the whole table to the log so the
// fingerprints are available even without the panel. Runs under g_tapMu.
static void MaybeDumpTable() {
    static uint64_t lastQpc = 0; static size_t lastSize = 0; static LARGE_INTEGER freq = {};
    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
    LARGE_INTEGER q; QueryPerformanceCounter(&q);
    if ((uint64_t)q.QuadPart - lastQpc < (uint64_t)freq.QuadPart * 5) return;
    lastQpc = q.QuadPart;
    auto rows = g_tap.Snapshot(); if (rows.size() == lastSize) return; lastSize = rows.size();
    std::sort(rows.begin(), rows.end(), [](const DispatchEntry& a, const DispatchEntry& b) { return a.count > b.count; });
    DispatchSig tick, tap; g_tap.GetRoles(tick, tap);
    Log("--- dispatch table: %zu shapes, %llu dispatches, ticks %llu, taps %llu, roles %s/%s ---", rows.size(), (unsigned long long)g_tap.Dispatches(), (unsigned long long)g_tap.Ticks(), (unsigned long long)g_tap.Taps(), tick.Empty() ? "no-tick" : "tick", tap.Empty() ? "no-tap" : "tap");
    char b[512]; int n = 0;
    for (auto& e : rows) { if (++n > 40) break; ShapeStr(e.sig, b, sizeof b); Log("  %6u x (%u,%u,%u) %s%s", e.count, e.sig.x, e.sig.y, e.sig.z, b, e.roleAuto == 1 ? " [auto TICK]" : e.roleAuto == 2 ? " [auto TAP]" : ""); }
}
// A watchdog stop re-arms itself after 10 s, at most three times a session; every other reason stays off until re-armed by hand.
static void MaybeRearm() {
    if (!g_killed || !g_watchdogKill || GetTickCount64() < g_rearmAtMs || g_rearms >= 3) return;
    g_rearms++; g_watchdogKill = false; g_watchdogHits = 0; g_killed = false;
    Log("watchdog: re-armed (%d of 3)", (int)g_rearms);
}
static bool OnDispatch(ID3D11DeviceContext* ctx, UINT x, UINT y, UINT z, void*) {
    MaybeRearm();
    if (t_ownWork || g_killed || g_engineStarting || !ctx) return false;
    { std::lock_guard<std::mutex> lk(g_cfgMu); if (!g_cfg.enabled) return false; }
    std::lock_guard<std::mutex> lk(g_tapMu);
    if (!DeviceUsable(ctx)) { g_otherDispatches++; return false; }
    bool r = TapGuarded(ctx, x, y, z);
    MaybeDumpTable();
    return r;
}

// ------------------------------------------------------------------ hotkeys (polled at every LS present)
static void ShowMarker(uint32_t id) { g_marker = id; g_markerUntil = GetTickCount64() + 1200; }
// Ctrl+Shift + F-key. GetAsyncKeyState is global, so these work while the game has focus; the modifier pair keeps
// them off the game's own bindings. Edge-triggered: one action per press.
static void PollHotkeys() {
    // Runs at every present: only the six fields it needs are read (a copy of the whole Config allocates its strings and game list each frame)
    bool hotkeys; int keys[5];
    { std::lock_guard<std::mutex> lk(g_cfgMu); hotkeys = g_cfg.hotkeys; keys[0] = g_cfg.keyAB; keys[1] = g_cfg.keySplit; keys[2] = g_cfg.keySharpDn; keys[3] = g_cfg.keySharpUp; keys[4] = g_cfg.keyPreset; }
    static bool prev[5] = {};
    const bool mods = hotkeys && (GetAsyncKeyState(VK_CONTROL) & 0x8000) && (GetAsyncKeyState(VK_SHIFT) & 0x8000);
    for (int i = 0; i < 5; ++i) {
        const bool down = mods && keys[i] > 0 && (GetAsyncKeyState(keys[i]) & 0x8000);
        if (down && !prev[i]) {
            if (i == 0) { g_compare = g_compare == 2 ? 0 : 2; ShowMarker(g_compare == 2 ? 2 : 1); Log("hotkey: %s", g_compare == 2 ? "original only" : "enhanced"); }
            else if (i == 1) { g_compare = g_compare == 1 ? 0 : 1; ShowMarker(g_compare == 1 ? 3 : 1); Log("hotkey: %s", g_compare == 1 ? "split view" : "enhanced"); }
            else if (i == 4) {   // next preset
                std::string name, data;
                { std::lock_guard<std::mutex> lk(g_cfgMu);
                  if (!g_presets.empty()) { g_presetCursor = (g_presetCursor + 1) % (int)g_presets.size(); name = g_presets[g_presetCursor].name; data = g_presets[g_presetCursor].data; } }
                if (data.empty()) { Log("hotkey: no presets saved"); }
                else {
                    bool rebuild = false;
                    { std::lock_guard<std::mutex> lk(g_cfgMu); const float was = g_cfg.p.workingScale; PresetApplyTo(data, g_cfg.p); rebuild = g_cfg.p.workingScale != was; }
                    if (rebuild) g_requestReset = true;
                    SaveConfig(); ShowMarker(5); Log("hotkey: preset '%s'%s", name.c_str(), rebuild ? " (working scale changed: the model rebuilds)" : "");
                }
            }
            else {
                float now;
                { std::lock_guard<std::mutex> lk(g_cfgMu); now = g_cfg.p.sharpen = std::clamp(g_cfg.p.sharpen + (i == 3 ? 0.05f : -0.05f), 0.0f, 1.0f); }
                SaveConfig(); ShowMarker(4); Log("hotkey: sharpen %.2f", now);
            }
        }
        prev[i] = down;
    }
}

// ------------------------------------------------------------------ per-game looks
// The program that has focus, lower-cased, or "" when it is Lossless Scaling itself (its overlay and the manager live in
// this process) or cannot be read. Polled every few dozen presents.
static std::string DetectForegroundExe() {
    HWND w = GetForegroundWindow(); if (!w) return {};
    DWORD pid = 0; GetWindowThreadProcessId(w, &pid);
    if (!pid || pid == GetCurrentProcessId()) return {};
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid); if (!h) return {};
    wchar_t buf[MAX_PATH]; DWORD n = MAX_PATH; std::string r;
    if (QueryFullProcessImageNameW(h, 0, buf, &n)) { const wchar_t* b = wcsrchr(buf, L'\\'); r = Wide2Narrow(b ? b + 1 : buf); }
    CloseHandle(h);
    for (char& ch : r) ch = (char)tolower((unsigned char)ch);
    return r;
}
static void GameTick() {
    const std::string exe = DetectForegroundExe(); if (exe.empty()) return;
    { std::lock_guard<std::mutex> lk(g_statusMu); g_gameExe = exe; }
    if (exe == g_gameSeen) return;
    g_gameSeen = exe;
    std::string pname, data; bool autoOn;
    { std::lock_guard<std::mutex> lk(g_cfgMu); autoOn = g_cfg.gameAuto; for (auto& g : g_cfg.games) if (g.first == exe) pname = g.second;
      if (!pname.empty()) for (auto& pr : g_presets) if (pr.name == pname) data = pr.data; }
    if (!autoOn || pname.empty()) return;
    if (data.empty()) { Log("game %s: its preset '%s' no longer exists", exe.c_str(), pname.c_str()); return; }
    bool rebuild = false;
    { std::lock_guard<std::mutex> lk(g_cfgMu); const float was = g_cfg.p.workingScale; PresetApplyTo(data, g_cfg.p); rebuild = g_cfg.p.workingScale != was; }
    if (rebuild) g_requestReset = true;
    SaveConfig(); ShowMarker(5);
    Log("game %s took focus: preset '%s'%s", exe.c_str(), pname.c_str(), rebuild ? " (working scale changed: the model rebuilds)" : "");
}

// ------------------------------------------------------------------ the present hook (LS render thread)
// Every Present of LS's output swap chain: add the newest finished delta to the frame about to be shown.
static void PresentBody(IDXGISwapChain* sc) {
    g_presents++; g_presentStage[0]++;
    if (!g_tapDev || !g_bridge.IsReady() || !g_compose.IsReady()) return;
    g_presentStage[1]++;
    // LS's output swap chain lives on the tapped device (the proxy's own windows, if any, do not)
    bool isLs;
    // Two remembered chains: the manager window's own swap chain is presented between LS's, and a one-entry cache
    // re-classified (and logged) on every single Present.
    if (sc == g_lastSwap) isLs = true;
    else if (sc == g_lastSwapOther) isLs = false;
    else { ID3D11Device* dev = nullptr; sc->GetDevice(IID_PPV_ARGS(&dev)); isLs = (dev == g_tapDev); if (dev) dev->Release(); if (isLs) g_lastSwap = sc; else g_lastSwapOther = sc; g_lastSwapIsLs = isLs; Log("present: swap chain %p on %s device", (void*)sc, isLs ? "the tapped" : "another"); }
    if (!isLs) return;
    g_lsPresents++;
    PollHotkeys();
    if ((g_lsPresents & 31u) == 0) GameTick();
    PresentInfo pi = g_tap.NotePresent();
    g_presentStage[2]++;
    if (pi.target < 0) return;
    g_presentStage[3]++;
    ID3D11ShaderResourceView* dsrv = nullptr; uint32_t ww = 0, wh = 0;
    const uint64_t d = g_bridge.NewestDelta(&dsrv, &ww, &wh);
    if (!d) return;
    g_presentStage[4]++;
    NrParams p; { std::lock_guard<std::mutex> lk(g_cfgMu); p = g_cfg.p; }
    const int cmp = g_compare; const uint32_t marker = GetTickCount64() < g_markerUntil ? (uint32_t)g_marker : 0u;
    if (cmp == 2 && !marker) return;   // "original only": nothing to add, so the compose pass is skipped altogether
    ID3D11Texture2D* bb = nullptr;
    if (FAILED(sc->GetBuffer(0, IID_PPV_ARGS(&bb))) || !bb) return;
    uint32_t fw = 0, fh = 0; ID3D11Resource* flow = p.useFlow ? g_tap.NewestFlow(fw, fh) : nullptr;
    Compose11::Args a; a.target = bb; a.delta = dsrv; a.flow = flow; a.flowW = fw; a.flowH = fh; a.flowUnit = p.flowUnit;
    a.offset = (float)(pi.target - (double)d); a.intensity = p.composeIntensity; a.maxDelta = p.maxDelta; a.ghostGuard = p.ghostGuard; a.hiProtect = p.hiProtect; a.debugView = p.debugView; a.isGen = pi.gen;
    a.sharpen = p.sharpen; a.compare = (uint32_t)cmp; a.splitPos = g_splitPos; a.marker = marker;
    a.saturation = p.saturation; a.vibrance = p.vibrance;
    a.brightness = p.brightness; a.contrast = p.contrast; a.gamma = p.gamma;
    a.shadows = p.shadows; a.highlights = p.highlights; a.grain = p.grain; a.grainSize = p.grainSize; a.grainSeed = (uint32_t)g_presents;
    a.hudCount = p.hudCount; memcpy(a.hud, p.hud, sizeof a.hud); a.hudFeather = p.hudFeather; a.hudShow = g_hudShow;
    g_bridge.BeginDeltaUse(d);
    t_ownWork = true; bool ok = g_compose.Run(g_bridge.Context(), a); t_ownWork = false;
    g_bridge.EndDeltaUse(d);
    if (ok) { g_composed++; g_lastDelta = d; g_lastOffset = a.offset; g_lastTarget = pi.target; }
    if (flow) flow->Release(); bb->Release();
}
static void PresentGuarded(IDXGISwapChain* sc) {
    __try { PresentBody(sc); } __except (SehFilter(GetExceptionCode(), "present")) { t_ownWork = false; }
}
static void OnPresent(IDXGISwapChain* sc, void*) {
    if (g_killed && HostHasLive()) {   // a switched-off addon keeps saying so (a status that is not refreshed goes stale)
        static uint64_t s_at = 0; const uint64_t now = GetTickCount64();
        if (now - s_at >= 1000) { s_at = now; std::string why; { std::lock_guard<std::mutex> lk(g_statusMu); why = g_killReason; } g_host->SetStatus(kAddonId, ("Switched off: " + why).c_str(), 3); }
    }
    if (g_killed || g_engineStarting || !sc) return;
    { std::lock_guard<std::mutex> lk(g_cfgMu); if (!g_cfg.enabled) return; }
    std::lock_guard<std::mutex> lk(g_tapMu);
    PresentGuarded(sc);
}

// ------------------------------------------------------------------ panel
static void ShapeStr(const DispatchSig& s, char* out, size_t n) {
    if (n) out[0] = 0;   // a signature with no views leaves an empty string (callers print it)
    int k = 0; for (int i = 0; i < 8; ++i) if (s.srv[i].valid && k < (int)n - 48) k += snprintf(out + k, n - k, "S%d:%ux%u %s ", i, s.srv[i].w, s.srv[i].h, FmtName(s.srv[i].fmt));
    for (int i = 0; i < 4; ++i) if (s.uav[i].valid && k < (int)n - 48) k += snprintf(out + k, n - k, "U%d:%ux%u %s ", i, s.uav[i].w, s.uav[i].h, FmtName(s.uav[i].fmt));
}

LSPROXY_EXPORT void AddonRenderSettings() {
    Config c; { std::lock_guard<std::mutex> lk(g_cfgMu); c = g_cfg; }
    bool changed = false, createChanged = false, tapChanged = false;
    // Every slider bound to a field of the params gets that field's default (a fresh NrParams): a tick on the groove, a ring on the
    // knob while it differs, double-click to reset. Sliders bound to anything else simply have no default.
    static const NrParams kDefaults;
    auto SL = [&](const char* label, float* v, float lo, float hi, const char* fmt = "%.2f") {
        const char* base = (const char*)&c.p; const char* pv = (const char*)v; const float* def = nullptr;
        if (pv >= base && pv < base + sizeof(NrParams)) def = (const float*)((const char*)&kDefaults + (pv - base));
        return lsp::SliderFloat(label, v, lo, hi, fmt, 0, def);
    };

    // status
    std::string status = GetStatus(), frameInfo, adapterName; bool hasDisplay;
    { std::lock_guard<std::mutex> lk(g_statusMu); frameInfo = g_frameInfo; adapterName = g_adapterName; hasDisplay = g_hasDisplay; }
    Block("Status", true);
    if (g_killed) { ImGui::PushStyleColor(ImGuiCol_Text, lsp::theme::V(lsp::theme::kDanger)); ImGui::Text("DISABLED: %s", g_killReason.c_str()); ImGui::PopStyleColor(); ImGui::SameLine(); if (ImGui::SmallButton("Re-arm")) { g_killed = false; g_watchdogHits = 0; g_watchdogKill = false; g_rearms = 0; } Tip("Turn Neural Render back on after it switched itself off. If it switches off again, the reason above is still true."); }
    else { ImGui::PushStyleColor(ImGuiCol_Text, g_nrRuns ? lsp::theme::V(lsp::theme::kAccent) : lsp::theme::V(lsp::theme::kWarn)); ImGui::Text("%s", status.c_str()); ImGui::PopStyleColor(); }
    if (g_engine.IsFailed()) { ImGui::TextColored(lsp::theme::V(lsp::theme::kDanger), "engine: %s", g_engine.Stats().lastError); ImGui::SameLine(); if (ImGui::SmallButton("Retry engine")) { g_engineLuidValid = false; if (g_tapLuidValid) StartEngine(g_tapLuid); } Tip("Try to start the DLSS model again on the current graphics card."); }
    {   // ---- Requirements: what this needs, what was found, and what to do about anything missing
        bool have; { std::lock_guard<std::mutex> lk(g_reqMu); have = g_reqHave; }
        const bool failed = g_engine.IsFailed();
        const req::Report rep = RequirementsNow(failed, g_engine.IsReady(), g_nrRuns > 0, g_engine.Stats().lastError);
        Block("Requirements");
        ImGui::TextWrapped("You provide the model file yourself: nvngx_dlssnr.dll is not included with this addon and is never downloaded.");
        ImGui::Dummy(ImVec2(0, ImGui::GetFontSize() * 0.25f));
        Note("1. Put your copy of nvngx_dlssnr.dll in the Lossless Scaling folder, next to LosslessScaling.exe. The Browse button below copies it there for you.");
        Note("2. Press Test compatibility to check that it works on your graphics card.");
        Note("3. Turn on Enable below, then start your game and scale it as usual.");
        ImGui::Dummy(ImVec2(0, ImGui::GetFontSize() * 0.4f));
        // the verdict, in one line
        if (!have) ImGui::TextDisabled("Checking...");
        else if (rep.overall == req::Level::Missing) { ImGui::PushStyleColor(ImGuiCol_Text, lsp::theme::V(lsp::theme::kDanger)); ImGui::TextWrapped("Not ready yet. %s", rep.headline.c_str()); ImGui::PopStyleColor(); }
        else if (rep.overall == req::Level::Note) { ImGui::PushStyleColor(ImGuiCol_Text, lsp::theme::V(lsp::theme::kWarn)); ImGui::TextWrapped("Ready, with a note. %s", rep.headline.c_str()); ImGui::PopStyleColor(); }
        else { ImGui::TextColored(lsp::theme::V(lsp::theme::kAccent), "Everything is in place."); }
        ImGui::Dummy(ImVec2(0, ImGui::GetFontSize() * 0.3f));
        {   // (always open)
            for (const auto& row : rep.rows) {
                const ImVec4 col = row.level == req::Level::Ok ? lsp::theme::V(lsp::theme::kAccent) : (row.level == req::Level::Note ? lsp::theme::V(lsp::theme::kWarn) : lsp::theme::V(lsp::theme::kDanger));
                ImGui::TextColored(col, row.level == req::Level::Ok ? "OK     " : (row.level == req::Level::Note ? "NOTE   " : "MISSING"));
                ImGui::SameLine(ImGui::GetFontSize() * 5.2f); ImGui::Text("%s", row.label.c_str()); ImGui::SameLine(ImGui::GetFontSize() * 13.0f); ImGui::TextWrapped("%s", row.value.c_str());
                if (!row.hint.empty()) { ImGui::Indent(ImGui::GetFontSize() * 1.7f); ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled)); ImGui::TextWrapped("%s", row.hint.c_str()); ImGui::PopStyleColor(); ImGui::Unindent(ImGui::GetFontSize() * 1.7f); }
            }
            ImGui::Dummy(ImVec2(0, ImGui::GetFontSize() * 0.4f));
            const bool placing = g_placeBusy.load();
            if (placing) ImGui::BeginDisabled();
            if (ImGui::SmallButton(placing ? "Waiting for the file dialog..." : "Browse for the model file...")) BrowseAndPlace();
            if (placing) ImGui::EndDisabled();
            Tip("Pick your own copy of nvngx_dlssnr.dll: it is copied into the Lossless Scaling folder. A file already there is moved to the backups folder, never deleted. Nothing is downloaded.");
            ImGui::SameLine();
            const bool testing = g_selfTestBusy.load();
            if (testing) ImGui::BeginDisabled();
            if (ImGui::SmallButton(testing ? "Testing..." : "Test compatibility")) RunSelfTestAsync();
            if (testing) ImGui::EndDisabled();
            Tip("Runs the model file once, in a separate program, on your graphics card, and says whether it works there. It uses the graphics card for a few seconds, so do it before you start a game. It cannot take Lossless Scaling down if the model misbehaves.");
            ImGui::Dummy(ImVec2(0, ImGui::GetFontSize() * 0.15f));
            if (ImGui::SmallButton("Open the Lossless Scaling folder")) ShellExecuteW(nullptr, L"open", g_lsDir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            Tip("Where the model file, nvngx_dlssnr.dll, goes: next to LosslessScaling.exe.");
            ImGui::SameLine();
            if (ImGui::SmallButton("Check again")) ScanRequirements();
            Tip("Look again at the graphics card, the NVIDIA driver, the model file and the helper DLL. Use it after putting a file in place.");
            const std::wstring reportFile = g_addonDir + L"\\compatibility_report.txt";
            if (GetFileAttributesW(reportFile.c_str()) != INVALID_FILE_ATTRIBUTES) {
                ImGui::SameLine();
                if (ImGui::SmallButton("Open the compatibility report")) ShellExecuteW(nullptr, L"open", reportFile.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                Tip("A short text file about the last compatibility test: your graphics card, driver, Windows, the model file's name, version and size, and the result. Paste it into an issue or the compatibility table. It holds no folders, user name or file hash.");
            }
            { std::string msg; bool ok; { std::lock_guard<std::mutex> lk(g_reqMu); msg = g_placeMsg; ok = g_placeOk; }
              if (!msg.empty()) { ImGui::PushStyleColor(ImGuiCol_Text, ok ? lsp::theme::V(lsp::theme::kAccent) : lsp::theme::V(lsp::theme::kWarn)); ImGui::TextWrapped("%s", msg.c_str()); ImGui::PopStyleColor(); } }
        }
    }
    Block("Saved looks (load and save your settings)");
    Note("A look is a saved set of the sliders below. Pick one from the list to load it. Save updates the look you picked; Save as new keeps the sliders as they are now under a name of your choice.");
    ImGui::Dummy(ImVec2(0, ImGui::GetFontSize() * 0.3f));
    // ---- Saved looks. Pick one to apply it; Save updates it (or asks for a name); Save as new keeps the current sliders under a
    // ---- new name; Delete asks first. A look is the sliders below (model knobs, picture, HUD areas), not the hotkeys or advanced settings.
    {
        static int s_active = -1; static char s_name[48] = ""; static bool s_askSave = false, s_askDelete = false;
        std::vector<std::string> names, datas;
        { std::lock_guard<std::mutex> lk(g_cfgMu); for (auto& pr : g_presets) { names.push_back(pr.name); datas.push_back(pr.data); } }
        const std::string now = PresetSerialize(c.p);
        if (s_active >= (int)names.size()) s_active = -1;
        // a look saved by an older version lacks newer keys; compare it as it would apply now, so it still reads as unchanged
        for (auto& d : datas) { NrParams t = c.p; PresetApplyTo(d, t); d = PresetSerialize(t); }
        if (s_active < 0) for (size_t i = 0; i < datas.size(); ++i) if (datas[i] == now) { s_active = (int)i; break; }   // recognise the look already in use
        const bool modified = s_active >= 0 && datas[s_active] != now;
        const std::string label = s_active >= 0 ? names[s_active] + (modified ? "  (changed)" : "") : std::string("Custom");
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 13.0f);
        if (ImGui::BeginCombo("Saved look", label.c_str())) {
            for (int i = 0; i < (int)names.size(); ++i)
                if (ImGui::Selectable(names[i].c_str(), i == s_active)) {
                    const float was = c.p.workingScale;
                    if (PresetApplyTo(datas[i], c.p)) { changed = true; if (c.p.workingScale != was) createChanged = true; }
                    s_active = i;
                }
            if (names.empty()) ImGui::TextDisabled("No saved looks yet: set the sliders, then Save as new.");
            ImGui::EndCombo();
        }
        Tip("Your saved looks: pick one to apply it. The Next preset hotkey cycles through them in the game. A look with a different Model resolution makes the model rebuild (a brief hitch).");
        ImGui::SameLine();
        if (lsp::Button("Save", lsp::icons::kSave, lsp::ButtonKind::Primary)) {
            if (s_active >= 0) {
                std::lock_guard<std::mutex> lk(g_cfgMu);
                if (s_active < (int)g_presets.size()) g_presets[s_active].data = now;
                changed = true;
            } else { s_name[0] = 0; s_askSave = true; }
        }
        Tip(s_active >= 0 ? "Update the selected look with the sliders as they are now." : "Keep the sliders as they are now as a new look: you are asked for a name.");
        ImGui::SameLine();
        if (lsp::Button("Save as new", lsp::icons::kPlus)) { s_name[0] = 0; s_askSave = true; }
        Tip("Keep the current sliders under a new name. Using a name that already exists replaces that look.");
        ImGui::SameLine();   // always shown, so it can be found: greyed out until a look is picked
        if (s_active < 0) ImGui::BeginDisabled();
        if (lsp::Button("Delete", lsp::icons::kTrash, lsp::ButtonKind::Danger)) s_askDelete = true;
        if (s_active < 0) ImGui::EndDisabled();
        Tip(s_active >= 0 ? "Delete the selected look. Your current sliders are not changed." : "Pick a look in the list first, then this deletes it.");
        if (s_askSave) { ImGui::OpenPopup("Save look"); s_askSave = false; }
        if (ImGui::BeginPopupModal("Save look", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Name for this look");
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            const bool enter = ImGui::InputText("##lookname", s_name, sizeof s_name, ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::Dummy(ImVec2(0, 4));
            const std::string n = PresetCleanName(s_name);
            const bool can = !n.empty();
            if (!can) ImGui::BeginDisabled();
            if (lsp::Button("Save", lsp::icons::kCheck, lsp::ButtonKind::Primary) || (enter && can)) {
                { std::lock_guard<std::mutex> lk(g_cfgMu); bool found = false; for (auto& pr : g_presets) if (pr.name == n) { pr.data = now; found = true; } if (!found) g_presets.push_back({ n, now }); }
                s_active = -1; changed = true; ImGui::CloseCurrentPopup();
            }
            if (!can) ImGui::EndDisabled();
            ImGui::SameLine();
            if (lsp::Button("Cancel", lsp::icons::kClose)) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        if (s_askDelete) { ImGui::OpenPopup("Delete look"); s_askDelete = false; }
        if (ImGui::BeginPopupModal("Delete look", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Delete the look '%s'?", s_active >= 0 && s_active < (int)names.size() ? names[s_active].c_str() : "");
            ImGui::TextDisabled("Your current sliders stay as they are.");
            ImGui::Dummy(ImVec2(0, 4));
            if (lsp::Button("Delete", lsp::icons::kTrash, lsp::ButtonKind::Danger)) {
                { std::lock_guard<std::mutex> lk(g_cfgMu); if (s_active >= 0 && s_active < (int)g_presets.size()) { CfgSet(("preset." + g_presets[s_active].name).c_str(), ""); g_presets.erase(g_presets.begin() + s_active); } }
                s_active = -1; changed = true; ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (lsp::Button("Cancel", lsp::icons::kClose)) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }

    Block("Neural Rendering");
    changed |= ImGui::Checkbox("Enable DLSS 5 Neural Rendering", &c.enabled);
    Tip("Master switch. Off = Lossless Scaling runs untouched and the model stops.\nTo compare before and after while playing, use the Before / after hotkey instead: it keeps the model running.");
    ImGui::SameLine(); if (ImGui::SmallButton("Reset history")) g_requestReset = true;
    Tip("The model blends each frame with the ones before it. Press this after a scene cut, or if a ghost or smear seems stuck on screen.");
    ImGui::SameLine();
    if (ImGui::SmallButton("Restore defaults")) {
        const float was = c.p.workingScale; const NrParams keep = c.p; c.p = NrParams(); c.p.hudCount = keep.hudCount; memcpy(c.p.hud, keep.hud, sizeof c.p.hud); c.p.hudFeather = keep.hudFeather; c.lsFirst = true; changed = true; if (c.p.workingScale != was) createChanged = true;
    }
    Tip("Put the look and quality sliders back to this addon's defaults. Your presets, hotkeys and the advanced settings are not touched.");

    Block("Settings");
    Note("Open a section to change it. Sliders: double-click to reset, Ctrl+click to type a value, Ctrl+scroll to fine-tune. The small tick marks the default.");
    ImGui::Dummy(ImVec2(0, ImGui::GetFontSize() * 0.3f));
    if (lsp::SectionHeader("Model (what it does to the picture)")) {
        // Read by the model at every evaluate: changes apply on the next frame. Ranges are what the model honours
        // (docs/dlssnr-knobs.md): intensity clamps at 1, the local strengths do not clamp at all.
        int style = (int)c.p.style; const char* styles[] = { "Standard", "Natural", "Cinematic" };
        if (ImGui::Combo("Style", &style, styles, 3)) { c.p.style = style; changed = true; }
        Tip("The model's three looks. Standard is the default. Natural compresses highlights by about 10% and can read as a dark vignette. Cinematic is the third variant.");
        changed |= SL("Model intensity", &c.p.intensity, 0.0f, 1.0f, "%.2f");
        Tip("How much of its edit the model produces. 0 = the model changes nothing; 1 = full. The model clamps at 1.\n(The Blend amount slider below is separate: it scales what gets added to the picture afterwards.)");
        changed |= SL("Fine detail strength", &c.p.localStructure, -2.0f, 5.0f, "%.2f");
        Tip("How strongly the model works on fine detail (edges, textures, text). 1 is the default. Above about 5 it turns to garbage; negative values give an 'anti-detail' look. Not clamped by the model.");
        changed |= SL("Local contrast", &c.p.localTone, -2.0f, 5.0f, "%.2f");
        Tip("How strongly the model works on local tone and contrast (how bright and dark areas separate). 1 is the default; same range behaviour as Fine detail strength.");
        changed |= SL("Skin and face detail", &c.p.skinStructure, -1.0f, 3.0f, c.p.skinStructure <= -0.99f ? "same as fine detail" : "%.2f");
        Tip("Detail strength where the model thinks it sees skin. At the far left it simply follows Fine detail strength. It only acts where skin is detected, so the change can be subtle.");
        bool am = c.p.useAutoMask != 0;
        if (ImGui::Checkbox("Detect skin automatically", &am)) { c.p.useAutoMask = am; changed = true; }
        Tip("Let the model find skin and faces on its own (the 'auto mask'). The effect is small; it is on by default.");
        changed |= ImGui::Checkbox("Use Lossless Scaling's motion data", &c.p.useFlow);
        Tip("Feeds the motion Lossless Scaling's frame generation measures (its optical flow) to the model as motion vectors, and uses it to slide the enhancement onto the generated in-between frames. Turn it off only to test without motion.");
        { const NrStats& fs = g_engine.Stats(); ImGui::SameLine(); if (fs.hasFlow) ImGui::TextDisabled("(flow %ux%u)", fs.flowW, fs.flowH); else ImGui::TextDisabled("(no flow texture seen yet)"); }
        if (!c.p.useFlow) ImGui::BeginDisabled();
        if (ImGui::Checkbox("Use this frame's motion (wait for Lossless Scaling's flow)", &c.freshFlow)) { changed = true; tapChanged = true; }
        Tip("On: the model runs a moment later in each frame, once Lossless Scaling has measured how this frame moved, so its motion vectors are current. "
            "Off: it runs as soon as the frame is captured and gets the previous frame's motion, one frame late, which smears and ghosts when the camera turns, starts or stops. "
            "On is the default; the switch is here to compare the two.");
        if (!c.p.useFlow) ImGui::EndDisabled();
    }
    if (lsp::SectionHeader("Quality and performance")) {
        // The model costs ~10 ms + ~7 ms per megapixel on Ampere. The working scale is the only cost lever: past the
        // frame interval the model simply skips frames and the present side carries the last delta forward.
        createChanged |= SL("Model resolution", &c.p.workingScale, 0.25f, 1.0f, "%.2f x the frame");
        Tip("The frame is shrunk by this before the model sees it, and the model's change is stretched back up to the picture. 1.00 = the model sees the whole frame: best detail, costs the most. This is the only setting that changes the cost.\nOn an RTX 4070 Ti SUPER the model takes roughly 2.7 ms plus 1.8 ms per megapixel.\nChanging it rebuilds the model, which costs a short hitch.");
        {
            const NrStats& st = g_engine.Stats();
            if (g_bridge.Width()) {
                float mp = (float)st.workW * (float)st.workH / 1e6f; double iv = g_bridge.IntervalMs();
                uint64_t runs = g_bridge.Runs(), skipped = g_bridge.Skipped(), seen = runs + skipped;
                ImGui::TextWrapped("frame %ux%u -> model input %ux%u (%.2f MP): model %.1f ms (avg %.1f), frame interval %.1f ms", g_bridge.Width(), g_bridge.Height(), st.workW, st.workH, mp, st.nrMs, g_avgNrMs, iv);
                ImGui::TextWrapped("model keeps up with %llu of %llu frames (%.0f%%); GPU start +%.1f / done +%.1f ms after submit; tap CPU %.2f ms", (unsigned long long)runs, (unsigned long long)seen, seen ? 100.0 * runs / seen : 0.0, st.startMs, st.doneMs, g_bridge.CpuMs());
                ImGui::TextWrapped("presents: %llu on LS's swap chain (%s), composed %llu, compose CPU %.2f ms, target %s; newest delta = frame %llu, applied at offset %.2f frames",
                    (unsigned long long)g_lsPresents, g_tap.PresentPattern(), (unsigned long long)g_composed, g_compose.CpuMs(), g_compose.TargetInfo(), (unsigned long long)g_lastDelta, g_lastOffset);
                if (seen > 30 && runs * 2 < seen) ImGui::TextColored(lsp::theme::V(lsp::theme::kWarn), "the model runs on fewer than half of the frames: the delta is carried across frames by the flow. Lower the working scale for a fresher result.");
            } else ImGui::TextDisabled("(no frame tapped yet)");
        }
        { int ps = (int)c.p.passes; const int dps = (int)kDefaults.passes; if (lsp::SliderInt("Model passes", &ps, 1, 4, "%d", 0, &dps)) { c.p.passes = (uint32_t)ps; changed = true; } }
        Tip("How many times the model reworks each frame; every pass takes the previous result as its input. 1 = normal. 2 to 4 make the effect stronger (and can start to look over-processed, so compare with the Before / after hotkey).\nEach extra pass costs roughly another model run: watch the model time and the 'keeps up with' line below. If the model cannot keep up it skips frames, and the last result is carried forward.");
        changed |= SL("Temporal smoothing", &c.p.deltaSmooth, 0.0f, 0.9f, c.p.deltaSmooth <= 0.001f ? "off" : "%.2f");
        Tip("Blends the model's change for this frame with its change for the previous one, moved along with the picture by Lossless Scaling's motion data. It calms shimmer and crawling in fine detail (distant roads, fences, foliage) at the price of a little softness or ghosting when the camera moves fast. 0 = off; try 0.3 first. Costs almost nothing.");
        changed |= ImGui::Checkbox("Give Lossless Scaling GPU priority", &c.lsFirst);
        Tip("Raises Lossless Scaling's own graphics work above the model's on the shared card, so frame generation and presenting are not delayed while the model runs. Recommended.");
        changed |= SL("Blend amount", &c.p.composeIntensity, 0.0f, 2.0f);
        Tip("How much of the model's change is added to each presented frame. 1 = exactly what the model made; 0 = none; above 1 exaggerates it.");
        changed |= SL("Ghost guard", &c.p.ghostGuard, 0.0f, 1.0f, c.p.ghostGuard <= 0.001f ? "off" : "%.2f");
        Tip("Stops the faint copy of the previous frame that can trail moving things. The model works on an older frame and its change is moved onto the current one with Lossless Scaling's motion data; where that motion data is unreliable (the edge of a moving object, something just uncovered) the change lands in the wrong place. This fades the change out there, and a little more the older it is, and leaves still and steadily moving areas alone. 0 = off, 0.5 = a good start, 1 = strongest. To see where it acts, set Diagnostic view to Ghost guard: dark areas are faded.");
        changed |= SL("Limit per-pixel change", &c.p.maxDelta, 0.05f, 1.0f);
        Tip("The most any pixel's colour may be changed (0..1 of full range). Lower is safer and subtler; it stops the model from making harsh jumps.");
        changed |= SL("Protect bright areas from", &c.p.hiProtect, 0.5f, 1.0f, c.p.hiProtect >= 0.999f ? "off" : "%.2f");
        Tip("The model's change fades out as a pixel's brightness rises from this level to white, so highlights are not crushed. At the far right (off) the change applies everywhere.");
    }
    if (lsp::SectionHeader("Picture (sharpness, tone, colour, grain)")) {
        // Compose side, applied to every presented frame, real and generated alike.
        changed |= SL("Sharpen", &c.p.sharpen, 0.0f, 1.0f, c.p.sharpen <= 0.001f ? "off" : "%.2f");
        Tip("Contrast-adaptive sharpening of every presented frame, after the model's change is added. The model and the upscale both soften the picture; a little sharpening (0.2 to 0.4) puts the bite back. Costs almost nothing.");
        changed |= SL("Saturation", &c.p.saturation, 0.0f, 2.0f, fabsf(c.p.saturation - 1.0f) < 0.005f ? "unchanged" : "%.2f");
        Tip("Colour intensity of the finished picture. 1.00 = unchanged; 0 = black and white; above 1 = more vivid. Applied last, to real and generated frames alike. Costs nothing measurable.");
        changed |= SL("Vibrance", &c.p.vibrance, 0.0f, 1.0f, c.p.vibrance <= 0.001f ? "off" : "%.2f");
        Tip("Like Saturation, but it lifts muted colours much more than vivid ones, so skin tones and already-strong colours are not pushed further. Use this for a gentle, natural colour boost; use Saturation for a blunt one.");
        changed |= SL("Brightness", &c.p.brightness, -0.3f, 0.3f, fabsf(c.p.brightness) < 0.0005f ? "unchanged" : "%+.2f");
        Tip("Lifts or lowers every pixel by the same amount (on the picture's own 0 to 1 scale). Simple, but it also lifts blacks, so it can wash the picture out: for a brighter look without that, try Gamma first.");
        changed |= SL("Contrast", &c.p.contrast, 0.5f, 1.5f, fabsf(c.p.contrast - 1.0f) < 0.002f ? "unchanged" : "%.2f");
        Tip("Pushes the picture away from mid-grey (above 1) or toward it (below 1). Blacks get darker and whites brighter as it rises; some detail in the extremes can clip.");
        changed |= SL("Gamma", &c.p.gamma, 0.5f, 2.0f, fabsf(c.p.gamma - 1.0f) < 0.002f ? "unchanged" : "%.2f");
        Tip("Bends the mid-tones without touching pure black or pure white. Above 1 brightens the mid-tones (opens up dark scenes); below 1 darkens them. Usually the best brightness control.");
        changed |= SL("Shadows", &c.p.shadows, -1.0f, 1.0f, fabsf(c.p.shadows) < 0.005f ? "unchanged" : "%+.2f");
        Tip("Works on the dark parts of the picture only. Above 0 lifts them (opens up dark corners and dungeons); below 0 deepens them. Bright areas stay as they are.");
        changed |= SL("Highlights", &c.p.highlights, -1.0f, 1.0f, fabsf(c.p.highlights) < 0.005f ? "unchanged" : "%+.2f");
        Tip("Works on the bright parts of the picture only. Below 0 pulls them down (recovers sky and glare); above 0 pushes them up. Dark areas stay as they are.");
        changed |= SL("Film grain", &c.p.grain, 0.0f, 1.0f, c.p.grain <= 0.002f ? "off" : "%.2f");
        Tip("Fine monochrome noise, strongest in the mid-tones and different on every frame. Hides banding and the plastic look of upscaling. Keep it low (0.1 to 0.3).");
        { int gs = (int)c.p.grainSize; const int dgs = (int)kDefaults.grainSize; if (lsp::SliderInt("Grain size", &gs, 1, 4, "%d px", 0, &dgs)) { c.p.grainSize = (float)gs; changed = true; } }
        Tip("How big each grain speck is, in screen pixels. 1 is the finest; on a 4K screen 2 looks closest to film.");
    }
    if (lsp::SectionHeader("Keep the HUD untouched")) {
        Note("Rectangles where the picture stays exactly as Lossless Scaling made it: no model change, sharpening, tone, colour or grain. Use them for action bars, the minimap, chat and text.");
        changed |= SL("Edge softness", &c.p.hudFeather, 0.0f, 0.05f, c.p.hudFeather <= 0.0005f ? "hard edge" : "%.3f");
        Tip("How gradually the enhancement fades in outside a protected area, as a fraction of the screen. 0 = a hard edge.");
        { bool show = g_hudShow; if (ImGui::Checkbox("Show the areas on screen (display only)", &show)) g_hudShow = show; }
        Tip("Tints and outlines the protected areas in green so you can line them up with your HUD. Not saved: turn it off when you are done.");
        int rm = -1;
        for (uint32_t i = 0; i < c.p.hudCount; ++i) {
            ImGui::PushID((int)i); float* r = c.p.hud[i];
            ImGui::Text("Area %u", i + 1); ImGui::SameLine(); if (ImGui::SmallButton("Remove")) rm = (int)i;
            changed |= lsp::SliderFloat("Left", &r[0], 0.0f, 0.99f); changed |= lsp::SliderFloat("Top", &r[1], 0.0f, 0.99f);
            changed |= lsp::SliderFloat("Right", &r[2], 0.01f, 1.0f); changed |= lsp::SliderFloat("Bottom", &r[3], 0.01f, 1.0f);
            if (r[2] < r[0] + 0.01f) r[2] = r[0] + 0.01f;
            if (r[3] < r[1] + 0.01f) r[3] = r[1] + 0.01f;
            ImGui::PopID();
        }
        if (rm >= 0) { for (uint32_t k = (uint32_t)rm; k + 1 < c.p.hudCount; ++k) memcpy(c.p.hud[k], c.p.hud[k + 1], sizeof c.p.hud[k]); c.p.hudCount--; changed = true; }
        if (c.p.hudCount < (uint32_t)NrParams::kMaxHud && ImGui::SmallButton("Add area")) { const float d[4] = { 0.35f, 0.88f, 0.65f, 1.0f }; memcpy(c.p.hud[c.p.hudCount++], d, sizeof d); changed = true; }
        Tip("Adds a rectangle along the bottom centre of the screen. Drag its four sliders to cover your HUD.");
        ImGui::SameLine();
        if (ImGui::SmallButton("WoW starter layout")) {
            const float d[4][4] = { { 0.0f, 0.0f, 0.30f, 0.17f }, { 0.86f, 0.0f, 1.0f, 0.24f }, { 0.0f, 0.70f, 0.26f, 1.0f }, { 0.27f, 0.88f, 0.73f, 1.0f } };
            memcpy(c.p.hud, d, sizeof d); c.p.hudCount = 4; changed = true;
        }
        Tip("Fills four areas where World of Warcraft's default interface sits: unit frames (top left), minimap (top right), chat (bottom left) and the action bars (bottom centre). A starting point: turn on 'Show the areas' and adjust to your own layout.");
        if (c.p.hudCount) { ImGui::SameLine(); if (ImGui::SmallButton("Clear all")) { c.p.hudCount = 0; changed = true; } }
        Note("Areas are saved with the preset, so each game can have its own layout.");
    }
    if (lsp::SectionHeader("Compare and hotkeys")) {
        int cm = g_compare; const char* cms[] = { "Enhanced", "Split: left original | right enhanced", "Original only (before)" };
        if (ImGui::Combo("Compare view", &cm, cms, 3)) g_compare = cm;
        Tip("Enhanced = normal. Split = left of the line is the original, right is enhanced. Original only = as if Neural Render were off (it saves the compose work but the model keeps running). Display only; not saved.");
        float sp = g_splitPos; { const float dsp = 0.5f; if (lsp::SliderFloat("Split position", &sp, 0.05f, 0.95f, "%.2f", 0, &dsp)) g_splitPos = sp; }
        Tip("Where the split line sits, from the left edge (0) to the right edge (1) of the screen.");
        Note("Display only: the model keeps running, so switching is instant. Not saved: Lossless Scaling always starts enhanced.");
        changed |= ImGui::Checkbox("Hotkeys: Ctrl+Shift + key (work while the game has focus)", &c.hotkeys);
        Tip("Switch the compare view, sharpen and presets from inside the game. They are read while Lossless Scaling is presenting frames. The Ctrl+Shift pair keeps them away from the game's own key bindings.");
        auto fkey = [&](const char* label, int* vk) {
            int idx = *vk - VK_F1; if (idx < 0 || idx > 11) idx = 5;
            const char* names[] = { "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12" };
            // wide enough for "F12" plus the arrow at any display scale (a fixed pixel width clipped the key name)
            const ImGuiStyle& st = ImGui::GetStyle();
            ImGui::SetNextItemWidth(ImGui::CalcTextSize("F12").x + st.FramePadding.x * 2.0f + ImGui::GetFrameHeight() + st.ItemInnerSpacing.x);
            if (ImGui::Combo(label, &idx, names, 12)) { *vk = VK_F1 + idx; changed = true; }
        };
        fkey("Before / after", &c.keyAB); fkey("Split view", &c.keySplit); fkey("Sharpen -", &c.keySharpDn); fkey("Sharpen +", &c.keySharpUp); fkey("Next preset", &c.keyPreset);
        auto fname = [](int vk) { static char b[8][8]; static int n = 0; char* o = b[n++ & 7]; snprintf(o, 8, "F%d", vk - VK_F1 + 1); return (const char*)o; };
        Note("Now: Ctrl+Shift+%s before/after  |  %s split  |  %s / %s sharpen - / +  |  %s next preset  (%s)",
            fname(c.keyAB), fname(c.keySplit), fname(c.keySharpDn), fname(c.keySharpUp), fname(c.keyPreset), c.hotkeys ? "hotkeys on" : "hotkeys OFF: tick the box above");
        Note("A small square appears in the screen's top-left corner for a moment: green enhanced, red original, amber split, blue sharpen changed, purple preset.");
    }
    if (lsp::SectionHeader("Games (a look per program)")) {
        std::string cur; { std::lock_guard<std::mutex> lk(g_statusMu); cur = g_gameExe; }
        ImGui::Text("Program in focus: %s", cur.empty() ? "(none seen yet)" : cur.c_str());
        Tip("The program that had focus most recently, ignoring Lossless Scaling's own windows. While a game is being scaled this is the game.");
        changed |= ImGui::Checkbox("Switch to a program's saved look when it takes focus", &c.gameAuto);
        Tip("When a program listed below takes focus, its preset is applied automatically (once per change of program, so your own tweaks after that stay). A preset with a different working scale makes the model rebuild for a moment.");
        std::vector<std::string> names; { std::lock_guard<std::mutex> lk(g_cfgMu); for (auto& pr : g_presets) names.push_back(pr.name); }
        int rmg = -1;
        for (size_t i = 0; i < c.games.size(); ++i) {
            ImGui::PushID((int)i);
            ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(c.games[i].first.c_str()); ImGui::SameLine();
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
            if (ImGui::BeginCombo("##gp", c.games[i].second.c_str())) {
                for (auto& n : names) if (ImGui::Selectable(n.c_str(), n == c.games[i].second)) { c.games[i].second = n; changed = true; }
                ImGui::EndCombo();
            }
            ImGui::SameLine(); if (ImGui::SmallButton("Forget")) rmg = (int)i;
            ImGui::PopID();
        }
        if (rmg >= 0) { CfgSet(("game." + c.games[rmg].first).c_str(), ""); c.games.erase(c.games.begin() + rmg); changed = true; }
        if (c.games.empty()) ImGui::TextDisabled("No programs yet.");
        static char exeBuf[64] = ""; static int addSel = 0;
        ImGui::InputText("Program (exe name)", exeBuf, sizeof exeBuf);
        Tip("For example WowB.exe. Matching ignores upper and lower case.");
        ImGui::SameLine(); if (ImGui::SmallButton("Use program in focus") && !cur.empty()) snprintf(exeBuf, sizeof exeBuf, "%s", cur.c_str());
        if (addSel >= (int)names.size()) addSel = 0;
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
        if (ImGui::BeginCombo("Look to use", names.empty() ? "(save a preset first)" : names[addSel].c_str())) {
            for (int i = 0; i < (int)names.size(); ++i) if (ImGui::Selectable(names[i].c_str(), i == addSel)) addSel = i;
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Add") && exeBuf[0] && !names.empty()) {
            std::string e = exeBuf; for (char& ch : e) ch = (char)tolower((unsigned char)ch);
            e = PresetCleanName(e); bool found = false;
            for (auto& g : c.games) if (g.first == e) { g.second = names[addSel]; found = true; }
            if (!found && !e.empty()) c.games.push_back({ e, names[addSel] });
            changed = true;
        }
        Tip("Use the chosen saved look whenever this program takes focus.");
        if (ImGui::Button("Save the current look for the program in focus") && !cur.empty()) {
            std::string base = cur; const size_t dot = base.rfind('.'); if (dot != std::string::npos && dot > 0) base.resize(dot);
            const std::string n = PresetCleanName(base); const std::string d = PresetSerialize(c.p);
            { std::lock_guard<std::mutex> lk(g_cfgMu); bool found = false; for (auto& pr : g_presets) if (pr.name == n) { pr.data = d; found = true; } if (!found) g_presets.push_back({ n, d }); }
            bool found = false; for (auto& g : c.games) if (g.first == cur) { g.second = n; found = true; }
            if (!found) c.games.push_back({ cur, n });
            changed = true;
        }
        Tip("Stores everything you have set now (including the HUD areas) as a preset named after the program, and links the program to it.");
    }
    if (lsp::SectionHeader("Frame detection (advanced)")) {
        Note("How the addon recognises Lossless Scaling's new real frame. Auto works; change this only if the status stays stuck on waiting.");
        int mode = c.tapMode; if (ImGui::RadioButton("Auto", mode == 0)) { mode = 0; } ImGui::SameLine(); if (ImGui::RadioButton("Manual", mode == 1)) { mode = 1; }
        if (mode != c.tapMode) { c.tapMode = mode; tapChanged = true; }
        int fs = c.frameSlot + 1; const char* slots[] = { "auto (highest)", "S0", "S1", "S2", "S3", "S4", "S5", "S6", "S7" };
        if (ImGui::Combo("Frame slot", &fs, slots, 9)) { c.frameSlot = fs - 1; tapChanged = true; }
        DispatchSig tick, tap; g_tap.GetRoles(tick, tap); char b[512];
        ShapeStr(tick, b, sizeof b); ImGui::TextWrapped("TICK: %s  (%u,%u,%u) %s", tick.Empty() ? "-" : "", tick.x, tick.y, tick.z, b);
        ShapeStr(tap, b, sizeof b);  ImGui::TextWrapped("TAP : %s  (%u,%u,%u) %s", tap.Empty() ? "-" : "", tap.x, tap.y, tap.z, b);
        if (ImGui::SmallButton("Clear table")) g_tap.ClearTable(); ImGui::SameLine();
        if (ImGui::SmallButton("Clear roles")) { c.tickSig.clear(); c.tapSig.clear(); tapChanged = true; }
        auto rows = g_tap.Snapshot(); std::sort(rows.begin(), rows.end(), [](const DispatchEntry& a, const DispatchEntry& b) { return a.count > b.count; });
        if (ImGui::BeginTable("disp", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("count"); ImGui::TableSetupColumn("groups"); ImGui::TableSetupColumn("views"); ImGui::TableSetupColumn("auto"); ImGui::TableSetupColumn("use"); ImGui::TableHeadersRow();
            int n = 0;
            for (auto& e : rows) {
                if (++n > 24) break;
                ImGui::TableNextRow(); ImGui::PushID((int)(e.key & 0x7fffffff));
                ImGui::TableNextColumn(); ImGui::Text("%u", e.count);
                ImGui::TableNextColumn(); ImGui::Text("%u,%u,%u", e.sig.x, e.sig.y, e.sig.z);
                ImGui::TableNextColumn(); ShapeStr(e.sig, b, sizeof b); ImGui::TextWrapped("%s", b);
                ImGui::TableNextColumn(); ImGui::Text("%s", e.roleAuto == 1 ? "TICK" : e.roleAuto == 2 ? "TAP" : "");
                ImGui::TableNextColumn();
                if (ImGui::SmallButton("TAP")) { c.tapSig = e.sig.Serialize(); c.tapMode = 1; tapChanged = true; } ImGui::SameLine();
                if (ImGui::SmallButton("TICK")) { c.tickSig = e.sig.Serialize(); c.tapMode = 1; tapChanged = true; }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }
    if (lsp::SectionHeader("Technical status")) {
    ImGui::Text("last LS device: %s %s   engine: %s", adapterName.c_str(), hasDisplay ? "(drives a display)" : "(no display output)", g_engineLuidValid ? "on the LSFG device" : "not started");
    ImGui::Text("frame: %s   NR %.1f ms (avg %.1f)  run %.1f ms   runs %llu   fails %llu", frameInfo.c_str(), g_lastNrMs, g_avgNrMs, g_lastTotalMs, (unsigned long long)g_nrRuns, (unsigned long long)g_engine.Stats().fails);
    ImGui::Text("dispatches %llu  ticks %llu  taps %llu  gate:%s  float-slot %d", (unsigned long long)g_tap.Dispatches(), (unsigned long long)g_tap.Ticks(), (unsigned long long)g_tap.Taps(), g_tap.GateName(), g_engine.Stats().floatSlot);
    { std::string tdi; { std::lock_guard<std::mutex> lk(g_statusMu); tdi = g_tapDevInfo; }
      ImGui::Text("d3d11 hook: %d entry points   other-adapter dispatches: %llu   tapped device: %s", g_hookCount, (unsigned long long)g_otherDispatches, tdi.c_str()); }
    }
    if (lsp::SectionHeader("Advanced")) {
        int dv = (int)c.p.debugView; const char* views[] = { "Result", "Original", "Delta x4", "Frame role (green real, red generated)", "LSFG flow", "Ghost guard (white = full effect)" };
        if (ImGui::Combo("Diagnostic view", &dv, views, 6)) { c.p.debugView = dv; changed = true; }
        Tip("Shows what the addon is doing instead of the finished picture: the original frame, the model's change amplified 4x, which frames are real or generated, or the motion data. Leave on Result for normal use.");
        changed |= SL("Slow-model watchdog (ms)", &c.watchdogMs, 20.0f, 200.0f, "%.0f");
        Tip("If the model takes longer than this for 30 frames in a row, Neural Render switches itself off so it can never hurt your frame rate. It re-arms itself after 10 seconds, up to three times per session.");
        char sp[512]; strncpy(sp, c.snippetPath.c_str(), sizeof sp); sp[sizeof sp - 1] = 0;
        if (ImGui::InputText("Model file path (blank = Lossless Scaling folder)", sp, sizeof sp)) { c.snippetPath = sp; changed = true; }
        if (ImGui::IsItemDeactivatedAfterEdit()) { { std::lock_guard<std::mutex> lk(g_cfgMu); g_cfg.snippetPath = c.snippetPath; } ScanRequirements(); }
        Tip("Full path to nvngx_dlssnr.dll. Leave blank to use the copy next to LosslessScaling.exe.");
        if (ImGui::SmallButton("Restart engine")) { g_engineLuidValid = false; g_killed = false; if (g_tapLuidValid) StartEngine(g_tapLuid); }
        Tip("Tear the model down and start it again on the graphics card Lossless Scaling is using.");
        ImGui::SameLine();
        if (ImGui::SmallButton("Dump flow probe (3 frames)")) g_tap.ArmProbe(g_lsDir);
        Tip("Diagnostic: writes the next three frames and Lossless Scaling's motion data to files in its folder.");
        { std::string ps = g_tap.ProbeStatus(); if (ps != "idle") ImGui::TextWrapped("%s", ps.c_str()); }
        Note("present hook: %s, %u presents seen in the process   log: <LS folder>\\logs\\DLSS5NR01.log", PresentHook::Installed() ? "installed" : "not yet", PresentHook::Hits());
    }

    if (changed || createChanged || tapChanged) {
        { std::lock_guard<std::mutex> lk(g_cfgMu); g_cfg = c; }
        SaveConfig();
        if (tapChanged) ApplyTapRoles();
        if (createChanged) g_requestReset = true;   // the engine re-creates the feature on the next tap (CreateKeysEqual)
    }
}

// ------------------------------------------------------------------ exports
static void AddonInitializeBody(IHost* host, ImGuiContext* ctx, void* allocFunc, void* freeFunc, void* userData) {
    ImGui::SetCurrentContext(ctx);
    ImGui::SetAllocatorFunctions((ImGuiMemAllocFunc)allocFunc, (ImGuiMemFreeFunc)freeFunc, userData);
    lsp::InitAddonImGui();
    g_host = host;
    wchar_t exe[MAX_PATH]; GetModuleFileNameW(nullptr, exe, MAX_PATH); *wcsrchr(exe, L'\\') = 0; g_lsDir = exe;
    HMODULE self = nullptr; GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&AddonInitializeBody, &self);
    wchar_t mod[MAX_PATH]; GetModuleFileNameW(self, mod, MAX_PATH); *wcsrchr(mod, L'\\') = 0; g_addonDir = mod;
    CreateDirectoryW((g_lsDir + L"\\logs").c_str(), nullptr);   // <Lossless Scaling>\\logs, shared with the proxy's log
    {   // the log is started afresh at every start: keep the previous session's as .old, so a problem seen while playing is still there after a restart
        std::wstring log = g_lsDir + L"\\logs\\DLSS5NR01.log";
        if (!MoveFileExW(log.c_str(), (log + L".old").c_str(), MOVEFILE_REPLACE_EXISTING) && GetLastError() != ERROR_FILE_NOT_FOUND)
            // still open in another Lossless Scaling process (a second copy starting up): write beside it instead of wiping the running session's log
            log = g_lsDir + L"\\logs\\DLSS5NR01-" + std::to_wstring(GetCurrentProcessId()) + L".log";
        g_logFile = _wfopen(log.c_str(), L"w");
    }
    InstallCrashDiagnostics();
    LoadConfig(); ApplyTapRoles();
    ScanRequirements();
    if (CfgGet("selfTestOnStart", "0") == "1") RunSelfTestAsync();   // a diagnostic switch (config.json): the offline test host uses it to run the self-test without a click
    host->SubscribeEvent(LSPROXY_EVENT_D3D11_DEVICE_READY, OnDeviceEvent, nullptr);
    host->SubscribeEvent(LSPROXY_EVENT_D3D11_DEVICE_CHANGED, OnDeviceEvent, nullptr);
    Log("DLSS5NR01 initialised (host version 0x%x), addon dir %ls", host->GetHostVersion(), g_addonDir.c_str());
    // Own inline hook on d3d11.dll instead of the host's vtable patch (see dispatch_hook.h for why). The Present hook
    // needs a device to find dxgi's entry points; it is installed at the first tap.
    g_hookCount = DispatchHook::Install(OnDispatch, nullptr, [](const char* m) { Log("%s", m); });
    if (g_hookCount <= 0) Kill("could not hook d3d11 Dispatch");
    // No manual DEVICE_READY replay here: the host's last device pointer may already be destroyed
    // (LS creates and drops devices constantly); we only touch devices inside the event or from a live context.
}
static int InitFilter(EXCEPTION_POINTERS* ep) {
    void* addr = ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionAddress : nullptr;
    unsigned code = ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionCode : 0;
    HMODULE m = nullptr; wchar_t modName[MAX_PATH] = L"?";
    if (addr && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)addr, &m)) GetModuleFileNameW(m, modName, MAX_PATH);
    const wchar_t* base = wcsrchr(modName, L'\\'); base = base ? base + 1 : modName;
    char b[256]; snprintf(b, sizeof b, "exception 0x%08x in AddonInitialize at %ls+0x%llx", code, base, (unsigned long long)((uintptr_t)addr - (uintptr_t)m));
    Kill(b);
    return EXCEPTION_EXECUTE_HANDLER;
}
LSPROXY_EXPORT void AddonInitialize(IHost* host, ImGuiContext* ctx, void* allocFunc, void* freeFunc, void* userData) {
    __try { AddonInitializeBody(host, ctx, allocFunc, freeFunc, userData); }
    __except (InitFilter(GetExceptionInformation())) {}
}

LSPROXY_EXPORT void AddonShutdown() {
    Log("shutting down");
    g_killed = true;
    PresentHook::Uninstall();
    DispatchHook::Uninstall();
    if (g_host) { g_host->UnsubscribeEvent(LSPROXY_EVENT_D3D11_DEVICE_READY, OnDeviceEvent); g_host->UnsubscribeEvent(LSPROXY_EVENT_D3D11_DEVICE_CHANGED, OnDeviceEvent); }
    for (int i = 0; i < 3000 && g_engineStarting; ++i) Sleep(10);   // let a model load that is in progress finish (as the join used to)
    for (int i = 0; i < 300 && g_reqBusy; ++i) Sleep(10);   // a requirements scan takes milliseconds; an open file dialog is left (the process is ending)
    { std::lock_guard<std::mutex> lk(g_tapMu); g_bridge.Shutdown(); g_compose.Shutdown(); g_engine.Shutdown(); }
    SaveConfig();
    if (g_logFile) { fclose(g_logFile); g_logFile = nullptr; }
    g_host = nullptr;
}

LSPROXY_EXPORT uint32_t GetAddonCapabilities() { return LSPROXY_CAP_HAS_SETTINGS | LSPROXY_CAP_D3D11_DEVICE_ACCESS; }
LSPROXY_EXPORT const char* GetAddonName() { return "DLSS 5 Neural Rendering"; }
LSPROXY_EXPORT const char* GetAddonVersion() { return "0.7.0"; }
LSPROXY_EXPORT const char* GetAddonAuthor() { return "andreiday"; }
LSPROXY_EXPORT const char* GetAddonDescription() { return "Runs NVIDIA DLSS 5 Neural Rendering on Lossless Scaling's real frames on the display GPU and applies the result to every presented frame, without ever making LS wait. Needs your own copy of nvngx_dlssnr.dll (not included, never downloaded)."; }
