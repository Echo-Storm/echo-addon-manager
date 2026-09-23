// The addon interface exactly as released with API 1.0 (manager 0.7.0), frozen. The core test calls today's host through these declarations, as
// an addon built against that release does: every call must still reach the right function, and every event payload must keep its layout.
// Never edit this file; a new API version gets a new frozen copy beside it.
#pragma once
#include <cstdint>

namespace abi_v1_0 {

enum LogLevel : uint8_t { TRACE = 0, DEBUG = 1, INFO = 2, WARN = 3, ERR = 4 };

enum Event : uint32_t {
    ADDON_LOADED = 1, ADDON_UNLOADED = 2, SETTINGS_CHANGED = 3, SHADER_INTERCEPTED = 4, HOST_SHUTDOWN = 5, SETTINGS_APPLIED = 6,
    D3D11_DEVICE_READY = 7, D3D11_DEVICE_CHANGED = 8, CUSTOM = 0x10000
};

typedef void (*EventCallback)(uint32_t eventId, const void* data, uint32_t dataSize, void* userData);
typedef bool (*PreDispatchCallback)(uint32_t x, uint32_t y, uint32_t z, void* userData);
typedef void (*PostDispatchCallback)(uint32_t x, uint32_t y, uint32_t z, void* userData);

struct AddonEventData { const char* addonName; const char* addonVersion; };
struct ShaderEventData { const wchar_t* resourceName; const wchar_t* resourceType; uint32_t dataSize; };

struct IHost {
    virtual ~IHost() = default;
    virtual void Log(LogLevel level, const char* message) = 0;
    virtual const char* GetConfig(const char* addonId, const char* key, const char* defaultVal) = 0;
    virtual void SetConfig(const char* addonId, const char* key, const char* value) = 0;
    virtual void SaveConfig() = 0;
    virtual uint32_t GetHostVersion() = 0;
    virtual void SubscribeEvent(uint32_t eventId, EventCallback callback, void* userData) = 0;
    virtual void UnsubscribeEvent(uint32_t eventId, EventCallback callback) = 0;
    virtual void PublishEvent(uint32_t eventId, const void* data, uint32_t dataSize) = 0;
    virtual void* GetD3D11Device() = 0;
    virtual void* GetD3D11DeviceContext() = 0;
    virtual void SetPreDispatchCallback(PreDispatchCallback callback, void* userData) = 0;
    virtual void SetPostDispatchCallback(PostDispatchCallback callback, void* userData) = 0;
    virtual void* GetCurrentComputeShader() = 0;
    virtual uint32_t GetDispatchCount() = 0;
    virtual void SetStatus(const char* addonId, const char* text, int level) = 0;
    virtual void PublishMetric(const char* addonId, const char* key, double value, const char* unit) = 0;
};

} // namespace abi_v1_0
