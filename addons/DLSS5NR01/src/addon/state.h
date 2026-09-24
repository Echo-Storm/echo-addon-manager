// What the parts of the addon share: the frame path (runtime.cpp, on Lossless Scaling's render thread), the panel (panel.cpp, on the
// manager's window thread), the background jobs (tasks.cpp) and the exports (addon.cpp).
//
// Locks: g_settingsMutex guards g_config and g_looks. g_frameMutex guards the machinery (engine, bridge, tap, compose) and the device
// bookkeeping, and is held for the whole of each tapped pass and each present. g_textMutex guards the strings shown in the panel. The counters
// the panel only displays are written on the render thread and read without a lock (a torn read shows a wrong number for one frame).
#pragma once
#include <eam/addon_sdk.h>
#include "addon/bridge.h"
#include "addon/auto_quality.h"
#include "addon/compose11.h"
#include "addon/frame_tap.h"
#include "addon/requirements.h"
#include "addon/product.h"
#include "addon/settings.h"
#include "engine/nr_engine.h"
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

namespace nr {


extern IHost* g_host;
extern std::wstring g_lsDir, g_addonDir;

extern std::mutex g_settingsMutex;
extern Config g_config;
extern std::vector<Look> g_looks;

extern std::mutex g_frameMutex;
extern NrEngine g_engine;
extern Bridge g_bridge;
extern FrameTap g_tap;
extern Compose11 g_compose;

// Switched off: by hand, by a failure, or by the watchdog (which switches it back on after a pause, three times a session at most).
extern std::atomic<bool> g_off, g_offByWatchdog;
extern std::atomic<uint64_t> g_backOnAtMs;
extern std::atomic<int> g_backOnCount;
extern std::atomic<bool> g_engineStarting, g_resetRequested;

// The engine's card, and the card the frames come from (the engine follows it).
extern std::atomic<bool> g_engineCardKnown;
extern LUID g_engineCard;
extern LUID g_frameCard; extern std::atomic<bool> g_frameCardKnown;

// Display only, never saved (Lossless Scaling always starts on the enhanced picture).
extern std::atomic<int> g_compare;          // 0 enhanced, 1 split, 2 original only
extern std::atomic<float> g_splitPos;
extern std::atomic<bool> g_showHud;         // outline the protected areas on screen

// For the panel.
// Auto quality: changed by the frame path, read by the panel (both briefly, under g_autoMutex).
extern std::mutex g_autoMutex;
extern AutoQuality g_auto;

extern std::mutex g_textMutex;
extern std::string g_status, g_offReason, g_frameText, g_cardName, g_tappedDeviceText, g_focusExe;
extern bool g_cardDrivesDisplay;
extern uint64_t g_runs, g_otherPasses, g_lsPresents, g_composed, g_lastDelta;
extern double g_lastModelMs, g_avgModelMs, g_lastRunMs, g_lastOffset;

void SetStatus(const std::string& text);
std::string Status();
void SwitchOff(const std::string& why);   // stays off until switched on again (or the watchdog's pause is over)
void SwitchOn();                           // by hand, from the panel

// The settings, as the panel changed them: kept, saved, and whatever depends on them updated.
void Commit(const Config& config, bool tapRolesChanged, bool modelSizeChanged);
void ApplyTapRoles();

// runtime.cpp
void StartEngine(LUID card);
void RestartEngine();
// Only one addon of the pair works on the frames (runtime.cpp): who has them, taking and giving them up, the check on the frame path, and the
// start (an addon that is on but finds the other one in charge switches itself off).
std::string FrameOwner();
void ClaimFrames();
void ReleaseFrames();
bool OwnsFrames();
void SettleFramesAtStart();
void ForgetFramePath();
// DLSS as Lossless Scaling's scaler (the DLSS 4 addon): what the panel shows, and letting go at shutdown.
struct ScalerView { bool starting = false, ready = false, failed = false; std::string error; uint32_t inW = 0, inH = 0, outW = 0, outH = 0;
                    double gpuMs = 0; uint64_t runs = 0, nisSeen = 0; uint32_t perFrame = 0; };
ScalerView GetScalerView();
void StopScaler();   // the frame path's device state, as at a device change                      // on the card the frames come from
void OnDeviceEvent(uint32_t id, const void* data, uint32_t size, void* user);
bool OnPass(uint32_t x, uint32_t y, uint32_t z, void* user);   // the manager's pre-dispatch callback
std::string PassText(const DispatchSig& sig);                    // the views of a pass, for the log and the panel
void ResetWatchdog();

// tasks.cpp: the jobs that run beside the window (they read files, open a dialog, or start the compatibility test)
void ScanRequirements();
void RunSelfTest();
void BrowseForModel();
bool SelfTesting();
bool Browsing();
bool Scanning();
std::wstring ModelPath();
bool RequirementsScanned();
struct EngineView { bool failed, ready, running; const char* error; };
req::Report Requirements(const EngineView& engine);   // what was found, with the engine's state as it is now
std::string BrowseResult(bool& ok);                    // what the last "Browse for the model file" did

// panel.cpp
void DrawPanel();

} // namespace nr
