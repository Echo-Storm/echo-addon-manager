// The frame path, on Lossless Scaling's render thread and its graphics card.
//
//   capture k ─┬─ LSFG's capture pass on frame k       <- the TAP (with fresh motion: just after the frame's flow pass): frame k and LSFG's flow
//              │                                          are copied to the model's input and a run starts, unless the model is still busy. The
//              │                                          frame itself is only read.
//              ├─ LSFG's flow and generated frames ...
//              └─ Present (generated, generated, real)  <- each present: the newest finished delta is added to the frame about to be shown,
//                                                          moved along LSFG's flow to where that content is in this frame.
//
// Lossless Scaling's queue never waits for the model: the model can be late for a frame, never slow Lossless Scaling down.
#include "addon/state.h"
#include "addon/log.h"
#include "addon/present_hook.h"
#include "addon/screenshot.h"
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <thread>

namespace nr {

namespace {

bool SameCard(const LUID& a, const LUID& b) { return a.LowPart == b.LowPart && a.HighPart == b.HighPart; }

std::string Utf8(const std::wstring& w) {
    std::string s(w.size() * 3, '\0');
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), s.data(), static_cast<int>(s.size()), nullptr, nullptr);
    s.resize(n > 0 ? n : 0);
    return s;
}

const char* FormatName(uint32_t f) {
    switch (f) {
    case 87: return "BGRA8"; case 91: return "BGRA8s"; case 28: return "RGBA8"; case 29: return "RGBA8s"; case 24: return "RGB10A2";
    case 10: return "RGBA16F"; case 34: return "RG16F"; case 16: return "RG32F"; case 49: return "RG8"; case 41: return "R32F";
    case 54: return "R16F"; case 61: return "R8"; case 26: return "R11G11B10F"; case 2: return "RGBA32F"; default: return "?";
    }
}

// The card a device is on, and whether it is NVIDIA's.
struct Card { LUID luid{}; std::string name = "?"; bool nvidia = false; bool drivesDisplay = false; };
Card CardOf(ID3D11Device* dev) {
    Card card;
    IDXGIDevice* dxgi = nullptr;
    if (FAILED(dev->QueryInterface(IID_PPV_ARGS(&dxgi)))) return card;
    IDXGIAdapter* adapter = nullptr;
    if (SUCCEEDED(dxgi->GetAdapter(&adapter))) {
        DXGI_ADAPTER_DESC desc; adapter->GetDesc(&desc);
        card.luid = desc.AdapterLuid; card.name = Utf8(desc.Description); card.nvidia = desc.VendorId == 0x10DE;
        IDXGIOutput* output = nullptr;
        card.drivesDisplay = SUCCEEDED(adapter->EnumOutputs(0, &output));
        if (output) output->Release();
        adapter->Release();
    }
    dxgi->Release();
    return card;
}

// ---- the render thread's own state (under g_frameMutex)

struct SeenDevice { ID3D11Device* dev; bool nvidia; LUID card; };
std::vector<SeenDevice> g_seen;               // each device that has dispatched, judged once (not AddRef'd; forgotten at device events)
ID3D11Device* g_tapDevice = nullptr;          // the device the bridge works with
LUID g_candidateCard{}; uint32_t g_candidateTaps = 0;   // frames arriving from a card the engine is not on
constexpr uint32_t kFollowAfterTaps = 20;
LUID g_failedCard{}; bool g_failedCardKnown = false;     // the card the engine last failed on: it is not tried again by itself
IDXGISwapChain* g_lsChain = nullptr;          // Lossless Scaling's swap chain
IDXGISwapChain* g_otherChain = nullptr;       // the last one seen on another device (the manager's window presents between Lossless Scaling's)
uint64_t g_presents = 0;
uint64_t g_presentStages[5] = {};             // diagnostics: how far presents get
uint32_t g_watchdogHits = 0;
uint64_t g_lastRunAtMs = 0;                   // when the model last started a run: an older result is not composed (see Compose)
constexpr uint64_t kMaxResultAgeMs = 500;
thread_local bool t_ownWork = false;          // our own compose pass runs on Lossless Scaling's context: its dispatch is not a pass of theirs

std::atomic<uint32_t> g_marker{ 0 };          // the corner square after a hotkey, and until when
std::atomic<uint64_t> g_markerUntil{ 0 };
void ShowMarker(uint32_t colour) { g_marker = colour; g_markerUntil = GetTickCount64() + 1200; }

void ReleaseDecision(TapDecision& d) {
    if (d.frame) d.frame->Release();
    if (d.flow) d.flow->Release();
    d.frame = nullptr; d.flow = nullptr;
}

void ForgetDevice() {   // under g_frameMutex
    screenshot::Forget();
    g_bridge.Shutdown(); g_compose.Shutdown(); g_tap.Reset();
    g_seen.clear(); g_tapDevice = nullptr; g_lsChain = nullptr; g_otherChain = nullptr;
}

// ---- live numbers for the manager (its Performance tab and the addon's card)

void PublishLive(const NrStats& st) {
    static uint64_t slowAt = 0, statusAt = 0, runsBefore = 0, skippedBefore = 0;
    static double keepUp = 100.0;
    const double interval = g_bridge.LastIntervalMs();
    if (interval > 0) g_host->PublishMetric(kAddonId, "frame_ms", interval, "ms");
    const uint64_t now = GetTickCount64();
    if (now - slowAt >= 200) {   // five times a second is plenty for the rest
        slowAt = now;
        const uint64_t runs = g_bridge.Runs(), skipped = g_bridge.Skipped();
        const uint64_t newRuns = runs - runsBefore, newSkipped = skipped - skippedBefore;
        runsBefore = runs; skippedBefore = skipped;
        if (newRuns + newSkipped) keepUp = 100.0 * newRuns / static_cast<double>(newRuns + newSkipped);
        g_host->PublishMetric(kAddonId, "model_ms", st.nrMs, "ms");
        g_host->PublishMetric(kAddonId, "model_total_ms", st.totalMs, "ms");
        { std::lock_guard<std::mutex> lock(g_autoMutex); if (g_auto.Scale() > 0) g_host->PublishMetric(kAddonId, "model_scale", g_auto.Scale(), "x"); }
        g_host->PublishMetric(kAddonId, "gpu_start_ms", st.startMs, "ms");
        g_host->PublishMetric(kAddonId, "keepup_pct", keepUp, "%");
        g_host->PublishMetric(kAddonId, "tap_cpu_ms", g_bridge.CpuMs(), "ms");
    }
    if (now - statusAt >= 1000) {
        statusAt = now;
        char text[96];
        snprintf(text, sizeof text, keepUp >= 90.0 ? "Running, model %.1f ms, keeps up %.0f%%" : "Model is behind: %.1f ms, keeps up %.0f%%", st.nrMs, keepUp);
        g_host->SetStatus(kAddonId, text, keepUp >= 90.0 ? 1 : 2);
    }
}

void LogProgress(const NrStats& st) {
    const uint64_t taps = g_tap.Taps();
    if (taps == 1 || taps == 60 || taps % 300 == 0)
        Log("tap #%llu: model %.1f ms (avg %.1f), run %.1f ms, GPU start +%.1f done +%.1f ms after submit, tap CPU %.2f ms, interval %.1f ms, runs %llu skipped %llu, fails %llu | presents %llu (%s), composed %llu, compose CPU %.2f ms, last delta frame %llu offset %.2f",
            (unsigned long long)taps, st.nrMs, g_avgModelMs, st.totalMs, st.startMs, st.doneMs, g_bridge.CpuMs(), g_bridge.IntervalMs(), (unsigned long long)g_bridge.Runs(),
            (unsigned long long)g_bridge.Skipped(), (unsigned long long)st.fails, (unsigned long long)g_lsPresents, g_tap.PresentPattern(), (unsigned long long)g_composed,
            g_compose.CpuMs(), (unsigned long long)g_lastDelta, g_lastOffset);
    if (taps == 60 || taps % 300 == 0)
        Log("motion vectors so far: this frame's flow %llu, the previous frame's %llu, frames dropped waiting for a flow pass %llu",
            (unsigned long long)g_tap.FreshRuns(), (unsigned long long)g_tap.StaleRuns(), (unsigned long long)g_tap.DroppedWaiting());
    if (taps % 300 == 0) {   // the spread of the game's frame times over the last 300 frames (the line above is smoothed)
        float p50, p95, p99, worst; int n, over20, over33;
        if (g_bridge.TakeFrameTimeWindow(p50, p95, p99, worst, n, over20, over33)) {
            g_host->PublishMetric(kAddonId, "frame_p50_ms", p50, "ms");
            g_host->PublishMetric(kAddonId, "frame_p95_ms", p95, "ms");
            g_host->PublishMetric(kAddonId, "frame_p99_ms", p99, "ms");
            Log("frame time over the last %d frames: p50 %.1f ms, p95 %.1f, p99 %.1f, worst %.1f | %d frames over 20 ms (%.0f%%), %d over 33 ms | model %.1f ms, GPU start +%.1f ms",
                n, p50, p95, p99, worst, over20, 100.0 * over20 / n, over33, st.nrMs, st.startMs);
        }
    }
    if (taps == 60) {
        PresentHook::DumpState([](const char* m) { Log("%s", m); });
        Log("present stages: hook hits %u, body %llu, ready %llu, noted %llu, targeted %llu, with delta %llu", PresentHook::Hits(), (unsigned long long)g_presentStages[0],
            (unsigned long long)g_presentStages[1], (unsigned long long)g_presentStages[2], (unsigned long long)g_presentStages[3], (unsigned long long)g_presentStages[4]);
    }
}

void OnPresent(IDXGISwapChain* sc);
void Compose(IDXGISwapChain* sc);

// The engine runs on the card Lossless Scaling's frames come from. Lossless Scaling makes devices on every card and may run a pass on more than
// one for a moment, so it moves only after frames have kept coming from another card for a while.
bool EngineOnFrameCard(const LUID& card) {
    g_frameCard = card; g_frameCardKnown = true;
    if (g_engine.IsReady() && g_engineCardKnown && SameCard(g_engineCard, card)) { g_candidateTaps = 0; return true; }
    if (g_engineStarting) return false;
    if (g_engine.IsFailed() && g_failedCardKnown && SameCard(g_failedCard, card)) return false;   // it cannot run there: wait for Lossless Scaling to move
    if (g_engineCardKnown) {
        if (!SameCard(g_candidateCard, card)) { g_candidateCard = card; g_candidateTaps = 0; }
        if (++g_candidateTaps < kFollowAfterTaps) return false;
    }
    g_candidateTaps = 0;
    Log("engine follows the LSFG device: starting on LUID %08x:%08x", card.HighPart, card.LowPart);
    StartEngine(card);
    return false;   // the table keeps filling while the model loads
}

// One tapped pass (under g_frameMutex). Never skips Lossless Scaling's own dispatch.
void Tap(ID3D11DeviceContext* ctx, uint32_t x, uint32_t y, uint32_t z) {
    TapDecision d;
    if (!g_tap.Observe(ctx, x, y, z, d) || !g_tapDevice) { ReleaseDecision(d); return; }
    LUID card{};
    for (const SeenDevice& s : g_seen) if (s.dev == g_tapDevice) card = s.card;
    if (!EngineOnFrameCard(card)) { ReleaseDecision(d); return; }

    auto log = [](const char* m) { Log("%s", m); };
    if (!g_bridge.IsReady() && !g_bridge.Init(g_tapDevice, ctx, &g_engine, log)) { SwitchOff("bridge init failed"); ReleaseDecision(d); return; }
    if (!g_compose.IsReady() && !g_compose.Init(g_tapDevice, log)) { SwitchOff("compose init failed"); ReleaseDecision(d); return; }
    if (!PresentHook::Installed() && !PresentHook::Install(g_tapDevice, OnPresent, log)) { SwitchOff("could not hook dxgi Present"); ReleaseDecision(d); return; }

    D3D11_TEXTURE2D_DESC frame; d.frame->GetDesc(&frame);
    if (frame.Width < 64 || frame.Height < 64) {   // a minimised window or one in the middle of a resize: nothing to do, and no reason to rebuild the model
        static uint64_t loggedAt = 0;
        if (g_tap.Taps() - loggedAt > 600) { loggedAt = g_tap.Taps(); Log("skipping a %ux%u capture (too small)", frame.Width, frame.Height); }
        ReleaseDecision(d); return;
    }
    { char text[96]; snprintf(text, sizeof text, "%ux%u %s slot %d", frame.Width, frame.Height, FormatName(frame.Format), d.frameSlot);
      std::lock_guard<std::mutex> lock(g_textMutex); g_frameText = text; }
    if (!g_bridge.Ensure(frame.Width, frame.Height, frame.Format)) { SetStatus("unsupported frame format"); ReleaseDecision(d); return; }

    NrParams p; float watchdogMs; bool lsFirst; AutoQuality::Settings autoSettings;
    { std::lock_guard<std::mutex> lock(g_settingsMutex); p = g_config.p; watchdogMs = g_config.watchdogMs; lsFirst = g_config.lsFirst;
      autoSettings = { g_config.autoQuality, g_config.autoBudgetMs, g_config.autoFloor }; }
    const float ceiling = p.workingScale;
    if (autoSettings.on) { std::lock_guard<std::mutex> lock(g_autoMutex); if (g_auto.Scale() > 0) p.workingScale = std::min(ceiling, g_auto.Scale()); }
    g_bridge.SetLsGpuPriority(lsFirst ? 7 : 0);
    // Lossless Scaling's pass may have the frame bound as an input: the copy needs it unbound, and it is put back after
    ID3D11ShaderResourceView* bound[8] = {}; ctx->CSGetShaderResources(0, 8, bound);
    ID3D11ShaderResourceView* none[8] = {}; ctx->CSSetShaderResources(0, 8, none);
    const bool started = g_bridge.Submit(d.frame, d.flow, d.flowW, d.flowH, p, g_resetRequested.exchange(false), g_tap.Taps());
    ctx->CSSetShaderResources(0, 8, bound);
    for (ID3D11ShaderResourceView* v : bound) if (v) v->Release();
    ReleaseDecision(d);

    const NrStats& st = g_engine.Stats();
    if (started) {
        ++g_runs; g_lastModelMs = st.nrMs; g_lastRunMs = st.totalMs; g_lastRunAtMs = GetTickCount64();
        g_avgModelMs = g_avgModelMs == 0 ? st.nrMs : g_avgModelMs * 0.95 + st.nrMs * 0.05;
        if (g_runs == 1) SetStatus("running");
        // auto quality: the scale it picks here is used from the next frame (changing it rebuilds the model)
        std::lock_guard<std::mutex> lock(g_autoMutex);
        const uint64_t now = GetTickCount64();
        if (g_auto.Update(now, st.nrMs, static_cast<float>(g_bridge.LastIntervalMs()), ceiling, autoSettings) && !g_auto.History().empty() && g_auto.History().back().atMs == now) {
            const AutoQuality::Step& s = g_auto.History().back();
            Log("auto: model resolution %.2f -> %.2f (model %.1f ms, budget %.1f ms)", s.from, s.to, s.modelMs, autoSettings.budgetMs);
        }
    }
    if (g_engine.IsFailed()) SwitchOff(st.lastError);
    if (g_host->GetHostVersion() >= 0x010000) PublishLive(st);
    // the watchdog: a model that stays slower than the limit for 30 frames is switched off, and back on 10 s later (a loading screen or a
    // change of focus is no reason to stay off)
    if (st.nrMs > watchdogMs) {
        if (++g_watchdogHits >= 30) { SwitchOff("NR slower than watchdog threshold for 30 frames"); g_offByWatchdog = true; g_backOnAtMs = GetTickCount64() + 10000; }
    } else g_watchdogHits = 0;
    LogProgress(st);
}

int OnFault(unsigned code, const char* where) {
    char text[64]; snprintf(text, sizeof text, "exception 0x%08x in %s", code, where);
    SwitchOff(text);
    return EXCEPTION_EXECUTE_HANDLER;
}
void TapGuarded(ID3D11DeviceContext* ctx, uint32_t x, uint32_t y, uint32_t z) {   // no objects here: __try cannot unwind them
    __try { Tap(ctx, x, y, z); } __except (OnFault(GetExceptionCode(), "tap")) {}
}

// Each device that dispatches is judged once: only one on an NVIDIA card is tapped (under g_frameMutex).
bool Tappable(ID3D11DeviceContext* ctx) {
    ID3D11Device* dev = nullptr;
    ctx->GetDevice(&dev);
    if (!dev) return false;
    for (const SeenDevice& s : g_seen) if (s.dev == dev) { dev->Release(); return s.nvidia; }
    const Card card = CardOf(dev);
    if (g_seen.size() < 32) g_seen.push_back({ dev, card.nvidia, card.luid });
    if (card.nvidia && g_tapDevice != dev) { g_bridge.Shutdown(); g_compose.Shutdown(); g_tap.Reset(); g_tapDevice = dev; g_lsChain = nullptr; g_otherChain = nullptr; }
    char text[192];
    snprintf(text, sizeof text, "%p on %s (LUID %08x) -> %s", static_cast<void*>(dev), card.name.c_str(), static_cast<unsigned>(card.luid.LowPart), card.nvidia ? "TAPPED" : "ignored");
    if (card.nvidia) { std::lock_guard<std::mutex> lock(g_textMutex); g_tappedDeviceText = text; }
    Log("dispatching device %s", text);
    dev->Release();
    return card.nvidia;
}

// Every few seconds, when new kinds of pass have appeared, the whole table goes to the log (under g_frameMutex).
void LogPassTable() {
    static uint64_t loggedAt = 0; static size_t loggedRows = 0;
    const uint64_t now = GetTickCount64();
    if (now - loggedAt < 5000) return;
    loggedAt = now;
    std::vector<DispatchEntry> rows = g_tap.Snapshot();
    if (rows.size() == loggedRows) return;
    loggedRows = rows.size();
    std::sort(rows.begin(), rows.end(), [](const DispatchEntry& a, const DispatchEntry& b) { return a.count > b.count; });
    DispatchSig tick, tap; g_tap.GetRoles(tick, tap);
    Log("--- dispatch table: %zu shapes, %llu dispatches, ticks %llu, taps %llu, roles %s/%s ---", rows.size(), (unsigned long long)g_tap.Dispatches(),
        (unsigned long long)g_tap.Ticks(), (unsigned long long)g_tap.Taps(), tick.Empty() ? "no-tick" : "tick", tap.Empty() ? "no-tap" : "tap");
    for (size_t i = 0; i < rows.size() && i < 40; ++i) {
        const DispatchEntry& e = rows[i];
        Log("  %6u x (%u,%u,%u) %s%s", e.count, e.sig.x, e.sig.y, e.sig.z, PassText(e.sig).c_str(), e.roleAuto == 1 ? " [auto TICK]" : e.roleAuto == 2 ? " [auto TAP]" : "");
    }
}

// Ctrl+Shift + an F key, read at every present with GetAsyncKeyState (which works while the game has focus; the modifier pair keeps them
// off the game's own keys). One action per press.
void ApplyLookNow(const std::string& name, const std::string& data, const char* why) {
    bool rebuild;
    { std::lock_guard<std::mutex> lock(g_settingsMutex); const float before = g_config.p.workingScale; ApplyLook(data, g_config.p); rebuild = g_config.p.workingScale != before; }
    if (rebuild) g_resetRequested = true;
    Config c; std::vector<Look> looks;
    { std::lock_guard<std::mutex> lock(g_settingsMutex); c = g_config; looks = g_looks; }
    SaveSettings(g_host, kAddonId, c, looks);
    ShowMarker(5);
    Log("%s: preset '%s'%s", why, name.c_str(), rebuild ? " (working scale changed: the model rebuilds)" : "");
}

void ReadHotkeys() {
    bool on; int keys[6];
    { std::lock_guard<std::mutex> lock(g_settingsMutex);   // only what is needed: a copy of the whole Config would allocate at every present
      on = g_config.hotkeys; keys[0] = g_config.keyAB; keys[1] = g_config.keySplit; keys[2] = g_config.keySharpDn; keys[3] = g_config.keySharpUp; keys[4] = g_config.keyPreset;
      keys[5] = g_config.keyShot; }
    static bool wasDown[6] = {};
    static int lookCursor = -1;
    const bool modifiers = on && (GetAsyncKeyState(VK_CONTROL) & 0x8000) && (GetAsyncKeyState(VK_SHIFT) & 0x8000);
    for (int i = 0; i < 6; ++i) {
        const bool down = modifiers && keys[i] > 0 && (GetAsyncKeyState(keys[i]) & 0x8000);
        if (down && !wasDown[i]) {
            if (i == 0) { g_compare = g_compare == 2 ? 0 : 2; ShowMarker(g_compare == 2 ? 2 : 1); Log("hotkey: %s", g_compare == 2 ? "original only" : "enhanced"); }
            else if (i == 1) { g_compare = g_compare == 1 ? 0 : 1; ShowMarker(g_compare == 1 ? 3 : 1); Log("hotkey: %s", g_compare == 1 ? "split view" : "enhanced"); }
            else if (i == 5) { screenshot::Request(); Log("hotkey: screenshot"); }   // no corner square: it would be in the picture
            else if (i == 4) {
                Look next;
                { std::lock_guard<std::mutex> lock(g_settingsMutex); if (!g_looks.empty()) { lookCursor = (lookCursor + 1) % static_cast<int>(g_looks.size()); next = g_looks[lookCursor]; } }
                if (next.data.empty()) Log("hotkey: no presets saved"); else ApplyLookNow(next.name, next.data, "hotkey");
            } else {
                float sharpen;
                { std::lock_guard<std::mutex> lock(g_settingsMutex); sharpen = g_config.p.sharpen = std::clamp(g_config.p.sharpen + (i == 3 ? 0.05f : -0.05f), 0.0f, 1.0f); }
                Config c; std::vector<Look> looks;
                { std::lock_guard<std::mutex> lock(g_settingsMutex); c = g_config; looks = g_looks; }
                SaveSettings(g_host, kAddonId, c, looks);
                ShowMarker(4);
                Log("hotkey: sharpen %.2f", sharpen);
            }
        }
        wasDown[i] = down;
    }
}

// The program in focus, lower case; empty when it is Lossless Scaling itself (its overlay and the manager are in this process) or unknown.
std::string FocusExe() {
    const HWND window = GetForegroundWindow();
    if (!window) return {};
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    if (!pid || pid == GetCurrentProcessId()) return {};
    const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return {};
    wchar_t path[MAX_PATH]; DWORD size = MAX_PATH; std::string exe;
    if (QueryFullProcessImageNameW(process, 0, path, &size)) { const wchar_t* name = wcsrchr(path, L'\\'); exe = Utf8(name ? name + 1 : path); }
    CloseHandle(process);
    for (char& c : exe) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    return exe;
}

// A look per program: when a listed program takes focus, its look is applied once (so the person's own changes after that stay).
void FollowFocus() {
    static std::string actedOn;
    const std::string exe = FocusExe();
    if (exe.empty()) return;
    { std::lock_guard<std::mutex> lock(g_textMutex); g_focusExe = exe; }
    if (exe == actedOn) return;
    actedOn = exe;
    std::string lookName, data; bool follow;
    { std::lock_guard<std::mutex> lock(g_settingsMutex);
      follow = g_config.gameAuto;
      for (const auto& [program, look] : g_config.games) if (program == exe) lookName = look;
      for (const Look& l : g_looks) if (!lookName.empty() && l.name == lookName) data = l.data; }
    if (!follow || lookName.empty()) return;
    if (data.empty()) { Log("game %s: its preset '%s' no longer exists", exe.c_str(), lookName.c_str()); return; }
    ApplyLookNow(lookName, data, ("game " + exe + " took focus").c_str());
}

// One present of any swap chain in the process (under g_frameMutex): on Lossless Scaling's, the newest finished delta goes onto the frame.
void Present(IDXGISwapChain* sc) {
    ++g_presents; ++g_presentStages[0];
    if (!g_tapDevice || !g_bridge.IsReady() || !g_compose.IsReady()) return;
    ++g_presentStages[1];
    bool ours;   // Lossless Scaling's chain is on the tapped device; two remembered chains, so the manager's window does not make it look again each time
    if (sc == g_lsChain) ours = true;
    else if (sc == g_otherChain) ours = false;
    else {
        ID3D11Device* dev = nullptr;
        sc->GetDevice(IID_PPV_ARGS(&dev));
        ours = dev == g_tapDevice;
        if (dev) dev->Release();
        (ours ? g_lsChain : g_otherChain) = sc;
        Log("present: swap chain %p on %s device", static_cast<void*>(sc), ours ? "the tapped" : "another");
    }
    if (!ours) return;
    ++g_lsPresents;
    ReadHotkeys();
    if ((g_lsPresents & 31u) == 0) FollowFocus();
    Compose(sc);
    std::string game;
    { std::lock_guard<std::mutex> lock(g_textMutex); game = g_focusExe; }
    screenshot::OnPresent(g_bridge.Context(), sc, game);   // after the compose: the picture as it is shown
}

// The newest finished delta onto the frame about to be shown (Lossless Scaling's swap chain, under g_frameMutex).
void Compose(IDXGISwapChain* sc) {
    const PresentInfo shown = g_tap.NotePresent();
    ++g_presentStages[2];
    if (shown.target < 0) return;
    ++g_presentStages[3];
    ID3D11ShaderResourceView* delta = nullptr; uint32_t dw = 0, dh = 0;
    const uint64_t deltaFrame = g_bridge.NewestDelta(&delta, &dw, &dh);
    if (!deltaFrame) return;
    // A result the model has not replaced for a while belongs to another picture: frame generation was switched off (Lossless Scaling still
    // presents, but the model has nothing to run on), or the game paused. Added to every frame it would stand still on the screen.
    if (GetTickCount64() - g_lastRunAtMs > kMaxResultAgeMs) return;
    ++g_presentStages[4];
    NrParams p;
    { std::lock_guard<std::mutex> lock(g_settingsMutex); p = g_config.p; }
    const int compare = g_compare;
    const uint32_t marker = GetTickCount64() < g_markerUntil ? g_marker.load() : 0u;
    if (compare == 2 && !marker) return;   // "original only": nothing to add, so no compose pass at all
    ID3D11Texture2D* buffer = nullptr;
    if (FAILED(sc->GetBuffer(0, IID_PPV_ARGS(&buffer)))) return;
    uint32_t fw = 0, fh = 0;
    ID3D11Resource* flow = p.useFlow ? g_tap.NewestFlow(fw, fh) : nullptr;
    Compose11::Args a;
    a.target = buffer; a.delta = delta; a.flow = flow; a.flowW = fw; a.flowH = fh; a.flowUnit = p.flowUnit;
    a.offset = static_cast<float>(shown.target - static_cast<double>(deltaFrame)); a.isGen = shown.gen;
    a.intensity = p.composeIntensity; a.maxDelta = p.maxDelta; a.ghostGuard = p.ghostGuard; a.hiProtect = p.hiProtect; a.debugView = p.debugView;
    a.sharpen = p.sharpen; a.saturation = p.saturation; a.vibrance = p.vibrance; a.brightness = p.brightness; a.contrast = p.contrast; a.gamma = p.gamma;
    a.shadows = p.shadows; a.highlights = p.highlights; a.grain = p.grain; a.grainSize = p.grainSize; a.grainSeed = static_cast<uint32_t>(g_presents);
    a.hudCount = p.hudCount; memcpy(a.hud, p.hud, sizeof a.hud); a.hudFeather = p.hudFeather; a.hudShow = g_showHud;
    a.compare = static_cast<uint32_t>(compare); a.splitPos = g_splitPos; a.marker = marker;
    g_bridge.BeginDeltaUse(deltaFrame);
    t_ownWork = true;
    const bool composed = g_compose.Run(g_bridge.Context(), a);
    t_ownWork = false;
    g_bridge.EndDeltaUse(deltaFrame);
    if (composed) { ++g_composed; g_lastDelta = deltaFrame; g_lastOffset = a.offset; }
    if (flow) flow->Release();
    buffer->Release();
}
void PresentGuarded(IDXGISwapChain* sc) {   // no objects here: __try cannot unwind them
    __try { Present(sc); } __except (OnFault(GetExceptionCode(), "present")) { t_ownWork = false; }
}

void OnPresent(IDXGISwapChain* sc) {
    if (g_off && g_host->GetHostVersion() >= 0x010000) {   // switched off: keep saying so (a status that is not refreshed goes stale)
        static uint64_t saidAt = 0;
        const uint64_t now = GetTickCount64();
        if (now - saidAt >= 1000) {
            saidAt = now;
            std::string why; { std::lock_guard<std::mutex> lock(g_textMutex); why = g_offReason; }
            g_host->SetStatus(kAddonId, ("Switched off: " + why).c_str(), 3);
        }
    }
    if (g_off || g_engineStarting || !sc) return;
    { std::lock_guard<std::mutex> lock(g_settingsMutex); if (!g_config.enabled) return; }
    std::lock_guard<std::mutex> lock(g_frameMutex);
    PresentGuarded(sc);
}

} // namespace

std::string PassText(const DispatchSig& sig) {
    std::string text;
    char view[64];
    for (int i = 0; i < 8; ++i) if (sig.srv[i].valid) { snprintf(view, sizeof view, "S%d:%ux%u %s ", i, sig.srv[i].w, sig.srv[i].h, FormatName(sig.srv[i].fmt)); text += view; }
    for (int i = 0; i < 4; ++i) if (sig.uav[i].valid) { snprintf(view, sizeof view, "U%d:%ux%u %s ", i, sig.uav[i].w, sig.uav[i].h, FormatName(sig.uav[i].fmt)); text += view; }
    return text;
}

// The model loads on a thread of its own (it takes seconds). Detached and tracked by g_engineStarting, cleared on every way out: a std::thread
// kept in a static would still be joinable if the process ends without AddonShutdown, and destroying it then ends the process.
void StartEngine(LUID card) {
    if (g_engineStarting.exchange(true)) return;
    std::thread([card] {
        auto failed = [card](const char* why) {
            Log("%s", why);
            g_failedCard = card; g_failedCardKnown = true;
            SetStatus("engine failed (exception; see the log)");
            g_engineStarting = false;
        };
        try {
            { std::lock_guard<std::mutex> lock(g_frameMutex); g_bridge.Shutdown(); if (g_engine.IsReady() || g_engine.IsFailed()) g_engine.Shutdown(); }
            SetStatus("engine: loading model...");
            const bool ok = g_engine.Init(card, g_addonDir + L"\\" NR_FORWARDER_FILENAME, ModelPath(), g_addonDir, g_lsDir, [](const char* m) { Log("%s", m); });
            g_engineCard = card; g_engineCardKnown = true;
            if (!ok) {
                g_failedCard = card; g_failedCardKnown = true;
                Log("engine failed on LUID %08x:%08x; it will start again when LS runs LSFG on another NVIDIA adapter", card.HighPart, card.LowPart);
            }
            SetStatus(ok ? "engine ready" : g_engine.Stats().lastError);
            g_engineStarting = false;
        } catch (const std::exception& e) {
            failed((std::string("engine start threw: ") + e.what()).c_str());
        } catch (...) {
            failed("engine start threw a non-standard exception");
        }
    }).detach();
}

void RestartEngine() {
    g_engineCardKnown = false;
    if (g_frameCardKnown) StartEngine(g_frameCard);
}

void OnDeviceEvent(uint32_t id, const void*, uint32_t, void*) {
    { std::lock_guard<std::mutex> lock(g_frameMutex); ForgetDevice(); }
    if (id == EAM_EVENT_D3D11_DEVICE_CHANGED) return;
    auto* const dev = static_cast<ID3D11Device*>(g_host->GetD3D11Device());
    { std::lock_guard<std::mutex> lock(g_textMutex); g_cardDrivesDisplay = false; g_cardName = "?"; }
    if (!dev) return;
    const Card card = CardOf(dev);
    { std::lock_guard<std::mutex> lock(g_textMutex); g_cardName = card.name; g_cardDrivesDisplay = card.drivesDisplay; }
    Log("device %p on '%s' LUID %08x:%08x display=%d -> %s", static_cast<void*>(dev), card.name.c_str(), card.luid.HighPart, card.luid.LowPart, card.drivesDisplay ? 1 : 0,
        card.nvidia ? "NVIDIA, ok" : "not NVIDIA, ignored");
    if (!card.nvidia) SetStatus("waiting: LS device is not an NVIDIA adapter");
    else if (!g_engine.IsReady()) SetStatus("waiting for LSFG dispatches");
    // The engine starts from the tap, on the card whose device actually runs LSFG (one card, a hybrid laptop, or either card of a two-card
    // machine), not from these events, which come for every device Lossless Scaling makes.
}

bool OnPass(uint32_t x, uint32_t y, uint32_t z, void*) {
    // after a pause, the watchdog switches it back on (three times a session at most); every other reason waits for the person
    if (g_off && g_offByWatchdog && GetTickCount64() >= g_backOnAtMs && g_backOnCount < 3) {
        ++g_backOnCount; g_offByWatchdog = false; g_watchdogHits = 0; g_off = false;
        Log("watchdog: re-armed (%d of 3)", g_backOnCount.load());
    }
    auto* const ctx = static_cast<ID3D11DeviceContext*>(g_host ? g_host->GetDispatchingContext() : nullptr);
    if (t_ownWork || g_off || g_engineStarting || !ctx) return false;
    { std::lock_guard<std::mutex> lock(g_settingsMutex); if (!g_config.enabled) return false; }
    std::lock_guard<std::mutex> lock(g_frameMutex);
    if (!Tappable(ctx)) { ++g_otherPasses; return false; }
    TapGuarded(ctx, x, y, z);
    LogPassTable();
    return false;   // Lossless Scaling's pass always runs
}

void ResetWatchdog() { g_watchdogHits = 0; }

} // namespace nr
