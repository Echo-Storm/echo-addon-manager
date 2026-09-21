#pragma once
#include "../../sdk/include/lsproxy/addon_exports.h"
#include <cstdint>
#include <d3d11.h>
#include <string>
#include <vector>
#include <windows.h>

namespace lsproxy {

// What trusted_addons.json says about an addon's DLL.
enum class SecurityVerdict : uint8_t {
    Unknown,    // there is nothing to compare with
    Trusted,    // the DLL's SHA-256 is on the list
    Tampered,   // the addon is on the list but this DLL is not the listed one
    Unsigned    // not used yet
};

// addon.json, or what the DLL itself reports when a field is missing.
struct AddonManifest {
    std::string name;
    std::string version;
    std::string author;
    std::string description;
    std::string minHostVersion;              // the lowest addon API it can run on, "1.0.0"
    std::string dll;                         // a DLL name other than the folder's
    std::string icon;                        // an icon file name other than icon.png
    std::vector<std::string> dependencies;   // ids of addons that must load first
    std::vector<std::string> renamedFrom;    // folder names this addon used to have: its settings are carried over, and those folders are hidden
    std::vector<std::string> tags;
    bool parsed = false;                     // addon.json was found and was valid
};

// The functions a loaded addon's DLL offers. Every one of them may be missing.
struct AddonExports {
    AddonInit_t init = nullptr;
    AddonShutdown_t shutdown = nullptr;
    AddonRenderSettings_t renderSettings = nullptr;
    AddonInterceptResource_t intercept = nullptr;
    GetAddonName_t name = nullptr;
    GetAddonVersion_t version = nullptr;
    GetAddonAuthor_t author = nullptr;
    GetAddonDescription_t description = nullptr;
};

struct AddonInfo {
    // who it is
    std::wstring folderName;   // the folder under addons\, which is also the addon's id
    std::string id;            // the same, in UTF-8
    std::wstring dllPath;
    std::wstring configPath;   // an .ini file the addon ships with, if any
    AddonManifest manifest;

    // what state it is in
    bool enabled = true;                   // the user's switch
    HMODULE hModule = nullptr;             // non-null while the DLL is loaded
    uint32_t capabilities = 0;             // LSPROXY_CAP_* bits, read from the DLL
    SecurityVerdict security = SecurityVerdict::Unknown;
    std::string errorMessage;              // why it is not running, in words for the user
    bool faulted = false;                  // it crashed while starting
    bool settingsFaulted = false;          // its settings panel hit an invalid CRT parameter and is no longer drawn
    AddonExports exports;

    // its icon
    std::wstring iconPath;
    ID3D11ShaderResourceView* iconTexture = nullptr;   // released by the manager

    // the window's per-addon state
    bool showSettings = false;
    bool selected = false;

    const std::string& GetDisplayName() const { return manifest.name.empty() ? id : manifest.name; }
    const std::string& GetDisplayVersion() const {
        static const std::string kUnknown = "?.?.?";
        return manifest.version.empty() ? kUnknown : manifest.version;
    }
    const std::string& GetDisplayAuthor() const {
        static const std::string kUnknown = "Unknown";
        return manifest.author.empty() ? kUnknown : manifest.author;
    }
    bool IsLoaded() const { return hModule != nullptr; }
    bool RequiresRestart() const { return (capabilities & LSPROXY_CAP_REQUIRES_RESTART) != 0; }
};

} // namespace lsproxy
