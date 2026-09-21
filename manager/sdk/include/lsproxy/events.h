#pragma once
#include <cstdint>

// Event IDs for the addon event system
enum LsProxyEvent : uint32_t {
    LSPROXY_EVENT_ADDON_LOADED       = 1,
    LSPROXY_EVENT_ADDON_UNLOADED     = 2,
    LSPROXY_EVENT_SETTINGS_CHANGED   = 3,
    LSPROXY_EVENT_SHADER_INTERCEPTED = 4,
    LSPROXY_EVENT_HOST_SHUTDOWN      = 5,
    LSPROXY_EVENT_SETTINGS_APPLIED   = 6, // Fired after ApplySettings completes
    LSPROXY_EVENT_D3D11_DEVICE_READY = 7, // Fired when D3D11 device pointer is captured
    LSPROXY_EVENT_D3D11_DEVICE_CHANGED = 8, // Fired when device is recreated (addons should clear stale state)

    // Custom events start here (for inter-addon communication)
    LSPROXY_EVENT_CUSTOM             = 0x10000
};

// Event callback signature
typedef void (*LsProxyEventCallback)(uint32_t eventId, const void* data, uint32_t dataSize, void* userData);

// Event data structures
struct LsProxyAddonEventData {
    const char* addonName;
    const char* addonVersion;
};

struct LsProxyShaderEventData {
    const wchar_t* resourceName;
    const wchar_t* resourceType;
    uint32_t dataSize;
};
