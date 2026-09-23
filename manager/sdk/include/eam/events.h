#pragma once
#include <cstdint>

// Event IDs for the addon event system
enum EamEvent : uint32_t {
    EAM_EVENT_ADDON_LOADED       = 1,
    EAM_EVENT_ADDON_UNLOADED     = 2,
    EAM_EVENT_SETTINGS_CHANGED   = 3,
    EAM_EVENT_SHADER_INTERCEPTED = 4,
    EAM_EVENT_HOST_SHUTDOWN      = 5,
    EAM_EVENT_SETTINGS_APPLIED   = 6, // Fired after ApplySettings completes
    EAM_EVENT_D3D11_DEVICE_READY = 7, // Fired when D3D11 device pointer is captured
    EAM_EVENT_D3D11_DEVICE_CHANGED = 8, // Fired when device is recreated (addons should clear stale state)

    // Custom events start here (for inter-addon communication)
    EAM_EVENT_CUSTOM             = 0x10000
};

// Event callback signature
typedef void (*EamEventCallback)(uint32_t eventId, const void* data, uint32_t dataSize, void* userData);

// Event data structures
struct EamAddonEventData {
    const char* addonName;
    const char* addonVersion;
};

struct EamShaderEventData {
    const wchar_t* resourceName;
    const wchar_t* resourceType;
    uint32_t dataSize;
};
