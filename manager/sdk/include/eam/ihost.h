#pragma once
#include "events.h"
#include <cstdint>

// Log levels matching the host logger
enum EamLogLevel : uint8_t {
    EAM_LOG_TRACE = 0,
    EAM_LOG_DEBUG = 1,
    EAM_LOG_INFO  = 2,
    EAM_LOG_WARN  = 3,
    EAM_LOG_ERROR = 4
};

// Called around each of Lossless Scaling's compute passes (ID3D11DeviceContext::Dispatch on its device's immediate context), on its render
// thread, with the pass's thread group counts. A pre-dispatch callback returns true to have the pass skipped (then no post-dispatch callback runs).
// Dispatches an addon makes itself from inside a callback do not call back.
typedef bool (*EamPreDispatchCallback)(uint32_t threadGroupCountX,
                                           uint32_t threadGroupCountY,
                                           uint32_t threadGroupCountZ,
                                           void* userData);
typedef void (*EamPostDispatchCallback)(uint32_t threadGroupCountX,
                                            uint32_t threadGroupCountY,
                                            uint32_t threadGroupCountZ,
                                            void* userData);

// Host interface exposed to addons
// Addons receive a pointer to this in AddonInitialize.
// All methods are safe to call from any thread.
struct IHost {
    virtual ~IHost() = default;

    // Logging
    virtual void Log(EamLogLevel level, const char* message) = 0;

    // Configuration (per-addon, persisted in config.json)
    virtual const char* GetConfig(const char* addonId, const char* key, const char* defaultVal = "") = 0;
    virtual void SetConfig(const char* addonId, const char* key, const char* value) = 0;
    virtual void SaveConfig() = 0;

    // Host version
    virtual uint32_t GetHostVersion() = 0;

    // Event system
    virtual void SubscribeEvent(uint32_t eventId, EamEventCallback callback, void* userData = nullptr) = 0;
    virtual void UnsubscribeEvent(uint32_t eventId, EamEventCallback callback) = 0;
    virtual void PublishEvent(uint32_t eventId, const void* data = nullptr, uint32_t dataSize = 0) = 0;

    // D3D11 device access (requires EAM_CAP_D3D11_DEVICE_ACCESS).
    // Returns nullptr if device not yet created or capability not requested.
    // Pointers are borrowed — do NOT Release() them.
    virtual void* GetD3D11Device() = 0;
    virtual void* GetD3D11DeviceContext() = 0;

    // Dispatch hooks (requires EAM_CAP_DISPATCH_HOOK).
    // Pre-dispatch: return true to skip the original Dispatch call.
    virtual void SetPreDispatchCallback(EamPreDispatchCallback callback, void* userData = nullptr) = 0;
    virtual void SetPostDispatchCallback(EamPostDispatchCallback callback, void* userData = nullptr) = 0;

    // Inside a dispatch callback: the ID3D11ComputeShader bound for the pass (borrowed, do not Release). Null outside one.
    virtual void* GetCurrentComputeShader() = 0;

    // Lossless Scaling's compute passes since its device was created (for diagnostics).
    virtual uint32_t GetDispatchCount() = 0;

    // ---- Added in API 1.0 (GetHostVersion() >= 0x010000). Appended at the end of the interface, so addons built against an
    // ---- older header are unaffected; an addon built against this one must check the host version before calling them.

    // A one-line live status for the addon (shown on its card and in the manager's status bar): "Running, model 6.6 ms".
    // level: 0 = info (grey), 1 = ok (green), 2 = warning (amber), 3 = error (red). Call at most a few times a second; a status that
    // is not refreshed for a few seconds is treated as stale and hidden. Pass an empty text to clear it.
    virtual void SetStatus(const char* addonId, const char* text, int level) = 0;

    // A numeric sample for the Performance tab: one series per (addonId, key), keys are short snake_case names ("frame_ms",
    // "model_ms"), `unit` is free text ("ms", "%") used for labels. The host keeps the last few thousand samples per series.
    // Cheap enough for every frame; do not publish faster than that.
    virtual void PublishMetric(const char* addonId, const char* key, double value, const char* unit) = 0;
};
