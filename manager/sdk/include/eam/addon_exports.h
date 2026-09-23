#pragma once
#include "ihost.h"

// Forward declaration
struct ImGuiContext;

// Convenience macro for addon exports
#define EAM_EXPORT extern "C" __declspec(dllexport)

// Addon Capabilities Bitmask
enum EamAddonCaps : uint32_t {
    EAM_CAP_NONE             = 0,
    EAM_CAP_HAS_SETTINGS     = 1 << 0,  // Addon provides a settings UI via AddonRenderSettings
    EAM_CAP_REQUIRES_RESTART = 1 << 1,  // Enable/disable requires app restart (no hot reload)
    EAM_CAP_PATCH_LS1_LOGIC      = 1 << 3,  // Reserved, has no effect (it never did in this manager); kept so older addons still compile
    EAM_CAP_D3D11_DEVICE_ACCESS  = 1 << 4,  // Request D3D11 device/context pointers via IHost
    EAM_CAP_DISPATCH_HOOK        = 1 << 5   // Request pre/post Dispatch callbacks
};

// Backward compatibility aliases
#define ADDON_CAP_NONE           EAM_CAP_NONE
#define ADDON_CAP_HAS_SETTINGS   EAM_CAP_HAS_SETTINGS
#define ADDON_CAP_REQUIRES_RESTART EAM_CAP_REQUIRES_RESTART
#define ADDON_CAP_PATCH_LS1_LOGIC      EAM_CAP_PATCH_LS1_LOGIC
#define ADDON_CAP_D3D11_DEVICE_ACCESS  EAM_CAP_D3D11_DEVICE_ACCESS
#define ADDON_CAP_DISPATCH_HOOK        EAM_CAP_DISPATCH_HOOK

/*
 * Required addon exports:
 *
 * EAM_EXPORT void AddonInitialize(IHost* host, ImGuiContext* ctx,
 *                                     void* allocFunc, void* freeFunc, void* userData);
 * EAM_EXPORT void AddonShutdown();
 * EAM_EXPORT uint32_t GetAddonCapabilities();
 *
 * Optional exports:
 *
 * EAM_EXPORT void AddonRenderSettings();
 * EAM_EXPORT bool AddonInterceptResource(const wchar_t* name, const wchar_t* type,
 *                                            const void** outData, uint32_t* outSize);
 * EAM_EXPORT const char* GetAddonName();
 * EAM_EXPORT const char* GetAddonVersion();
 * EAM_EXPORT const char* GetAddonAuthor();
 * EAM_EXPORT const char* GetAddonDescription();
 */

// Function pointer types for host-side use
typedef void (*AddonInit_t)(IHost* host, ImGuiContext* ctx, void* alloc_func, void* free_func, void* user_data);
typedef void (*AddonShutdown_t)();
typedef uint32_t (*GetAddonCaps_t)();
typedef void (*AddonRenderSettings_t)();
typedef bool (*AddonInterceptResource_t)(const wchar_t* name, const wchar_t* type, const void** outData, uint32_t* outSize);
typedef const char* (*GetAddonName_t)();
typedef const char* (*GetAddonVersion_t)();
typedef const char* (*GetAddonAuthor_t)();
typedef const char* (*GetAddonDescription_t)();
