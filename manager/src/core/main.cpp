#include "proxy_exports.h"
#include "shader_hook.h"
#include "d3d11_hook.h"
#include "../addon/addon_manager.h"
#include "../config/config_manager.h"
#include "../event/event_system.h"
#include "../features/features.h"
#include "../host/host_impl.h"
#include "../log/logger.h"
#include "../gui/gui_manager.h"
#include "../../sdk/include/lsproxy/version.h"
#include <algorithm>
#include <filesystem>
#include <memory>
#include <windows.h>

namespace fs = std::filesystem;

// ============================================================================
// Manual ApplySettings wrapper
// ============================================================================

typedef void(__fastcall* ApplySettings_t)(
    int, int, int, int,
    float, uint8_t, int, uint8_t,
    int, int, int, float, float,
    int, uint8_t, uint8_t, uint8_t, uint8_t,
    int, int, uint8_t, uint8_t,
    int, int, uint8_t, int, int,
    int, int, int, int, uint8_t);

static HMODULE g_hOriginal = nullptr;
static ApplySettings_t g_origApplySettings = nullptr;

// Core owns all subsystems with proper lifetime management
struct Core {
    std::unique_ptr<lsproxy::HostImpl> host;
    std::unique_ptr<lsproxy::AddonManager> addonManager;

    bool Init() {
        // Must run before GuiManager creates its window, or Windows bitmap-stretches it instead.
        if (HMODULE hUser32 = GetModuleHandleW(L"user32.dll")) {
            typedef BOOL(WINAPI* PFN_SetProcessDpiAwarenessContext)(HANDLE);
            auto pSetCtx = (PFN_SetProcessDpiAwarenessContext)
                GetProcAddress(hUser32, "SetProcessDpiAwarenessContext");
            if (pSetCtx) {
                const HANDLE kPerMonitorAwareV2 = (HANDLE)-4; // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
                pSetCtx(kPerMonitorAwareV2);
            } else {
                SetProcessDPIAware();
            }
        }

        // Determine paths
        wchar_t buffer[MAX_PATH];
        GetModuleFileNameW(NULL, buffer, MAX_PATH);
        fs::path exePath(buffer);
        // Logs live in <Lossless Scaling>\\logs so the program's own folder stays readable.
        std::error_code logDirError;
        fs::create_directories(exePath.parent_path() / "logs", logDirError);
        fs::path logPath = exePath.parent_path() / "logs" / LSPROXY_PRODUCT_FILE ".log";

        // Initialize logger first
        lsproxy::Logger::Instance().Init(logPath.wstring());
        LOG_INFO("Core", "%s v%s (built %s) starting...", LSPROXY_PRODUCT_NAME, LSPROXY_VERSION_STRING, __DATE__);
        lsproxy::InstallCrashLogging();

        // Load original DLL
        HMODULE hLosslessOriginal = LoadLibraryW(L"Lossless_original.dll");
        if (!hLosslessOriginal) {
            LOG_ERROR("Core", "Failed to load Lossless_original.dll!");
            return false;
        }
        g_hOriginal = hLosslessOriginal;
        LOG_INFO("Core", "Lossless_original.dll loaded");

        // Resolve ApplySettings for our manual wrapper
        g_origApplySettings = (ApplySettings_t)GetProcAddress(hLosslessOriginal, "ApplySettings");
        if (!g_origApplySettings) {
            LOG_ERROR("Core", "Failed to find ApplySettings in Lossless_original.dll!");
        } else {
            LOG_INFO("Core", "ApplySettings hooked for addon override support");
        }

        // Create host
        host = std::make_unique<lsproxy::HostImpl>();

        // Create addon manager (loads config, scans addons)
        addonManager = std::make_unique<lsproxy::AddonManager>(host.get());

        // Settings > Log Level (loaded with the config in the AddonManager constructor).
        lsproxy::Logger::Instance().SetMinLevel(static_cast<lsproxy::LogLevel>(std::clamp(
            lsproxy::ConfigManager::Instance().GlobalGetOr<int>(nullptr, "log_level", 2), 0, 4)));

        // An unreadable addons folder must not take Lossless Scaling down with it (this runs in DllMain).
        try {
            addonManager->ScanAddons();
        } catch (const std::exception& e) {
            LOG_ERROR("Core", "Scanning addons failed: %s", e.what());
        }

        // Install shader hooks
        ShaderHook::Initialize(addonManager.get());
        ShaderHook::InstallHooks();
        LOG_INFO("Core", "Shader hooks installed");

        // Install D3D11 device capture hook (for CUDA/DLSS addons)
        D3D11Hook::Initialize(host.get());
        LOG_INFO("Core", "D3D11 hooks installed");

        // Built-in features that are switched on (on their own threads where they hook things: this runs in DllMain)
        lsproxy::features::Start();

        // Start GUI thread (loads + initializes addons)
        lsproxy::GuiManager::StartGuiThread(addonManager.get());

        return true;
    }

    void Shutdown() {
        LOG_INFO("Core", "Shutting down...");

        // Publish shutdown event
        lsproxy::EventBus::Instance().Publish(LSPROXY_EVENT_HOST_SHUTDOWN);

        lsproxy::features::Stop();
        D3D11Hook::Shutdown();
        ShaderHook::UninstallHooks();
        ShaderHook::Shutdown();

        if (addonManager) {
            addonManager.reset();
        }
        host.reset();

        lsproxy::Logger::Instance().Shutdown();
    }
};

static std::unique_ptr<Core> g_core;

// ============================================================================
// Exported ApplySettings wrapper — forwards to original, then fires event
// ============================================================================

extern "C" __declspec(dllexport) void __fastcall ApplySettings(
    int scalingMode, int scalingFitMode, int scalingType, int scalingSubtype,
    float scaleFactor, uint8_t resizeBeforeScale, int sharpness, uint8_t vrs,
    int frameGenType, int frameGenSize, int frameGenMode, float frameGenMultiplier, float frameGenTarget,
    int frameGenFlowScale, uint8_t clipCursor, uint8_t adjustCursorSpeed, uint8_t hideCursor, uint8_t scaleCursor,
    int syncMode, int maxFrameLatency, uint8_t gsyncSupport, uint8_t hdrSupport,
    int captureApi, int queueTarget, uint8_t drawFps, int gpuId, int displayId,
    int cropLeft, int cropTop, int cropRight, int cropBottom, uint8_t multiDisplayMode)
{
    // Record what Lossless Scaling applies, so a comparison of scalers or frame-generation settings can be lined up
    // with the addon logs afterwards (the numbers are LS's own enum values).
    LOG_INFO("Core", "ApplySettings: scalingMode=%d fit=%d type=%d subtype=%d factor=%.2f sharpness=%d | frameGen type=%d size=%d mode=%d mult=%.2f target=%.1f flowScale=%d | capture=%d sync=%d maxLatency=%d queue=%d",
             scalingMode, scalingFitMode, scalingType, scalingSubtype, scaleFactor, sharpness,
             frameGenType, frameGenSize, frameGenMode, frameGenMultiplier, frameGenTarget, frameGenFlowScale,
             captureApi, syncMode, maxFrameLatency, queueTarget);

    // Forward to original
    if (g_origApplySettings) {
        g_origApplySettings(scalingMode, scalingFitMode, scalingType, scalingSubtype,
            scaleFactor, resizeBeforeScale, sharpness, vrs,
            frameGenType, frameGenSize, frameGenMode, frameGenMultiplier, frameGenTarget,
            frameGenFlowScale, clipCursor, adjustCursorSpeed, hideCursor, scaleCursor,
            syncMode, maxFrameLatency, gsyncSupport, hdrSupport,
            captureApi, queueTarget, drawFps, gpuId, displayId,
            cropLeft, cropTop, cropRight, cropBottom, multiDisplayMode);
    }

    // Notify addons that settings were just applied — they can now override globals
    lsproxy::EventBus::Instance().Publish(LSPROXY_EVENT_SETTINGS_APPLIED);
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH: {
        DisableThreadLibraryCalls(hModule);
        g_core = std::make_unique<Core>();
        if (!g_core->Init()) {
            g_core.reset();
            return FALSE;
        }
        break;
    }
    case DLL_PROCESS_DETACH:
        if (g_core) {
            g_core->Shutdown();
            g_core.reset();
        }
        break;
    }
    return TRUE;
}
