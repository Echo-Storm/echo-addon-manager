#pragma once
#include "../../sdk/include/lsproxy/addon_exports.h"
#include <cstdint>
#include <string>
#include <vector>
#include <windows.h>
#include <d3d11.h>

namespace lsproxy {

enum class SecurityVerdict : uint8_t {
    Unknown,       // No hash data available
    Trusted,       // Hash matches trusted list
    Tampered,      // Hash mismatch
    Unsigned       // No signature
};

struct AddonManifest {
    std::string name;
    std::string version;
    std::string author;
    std::string description;
    std::string minHostVersion;
    std::string dll;              // DLL filename override
    std::string icon;             // Icon filename (e.g. "icon.png"), optional
    std::vector<std::string> dependencies;
    std::vector<std::string> tags;
    bool parsed = false;          // true if addon.json was found and parsed
};

struct AddonInfo {
    // Identity
    std::wstring folderName;      // Directory name (used as ID)
    std::string id;               // UTF-8 version of folderName
    std::wstring dllPath;
    std::wstring configPath;

    // Manifest (from addon.json or DLL exports)
    AddonManifest manifest;

    // Runtime state
    bool enabled = true;
    HMODULE hModule = nullptr;
    uint32_t capabilities = 0;
    SecurityVerdict security = SecurityVerdict::Unknown;
    std::string errorMessage;     // Last error if any
    bool faulted = false;         // true if addon crashed
    bool settingsFaulted = false; // its settings panel hit an invalid CRT parameter; panel no longer drawn

    // Icon
    std::wstring iconPath;                          // Full path to icon file
    ID3D11ShaderResourceView* iconTexture = nullptr; // Loaded GPU texture (nullable)

    // UI state
    bool showSettings = false;
    bool selected = false;

    // Cached function pointers
    AddonInit_t InitFunc = nullptr;
    AddonShutdown_t ShutdownFunc = nullptr;
    AddonRenderSettings_t RenderSettingsFunc = nullptr;
    AddonInterceptResource_t InterceptResourceFunc = nullptr;
    GetAddonName_t GetNameFunc = nullptr;
    GetAddonVersion_t GetVersionFunc = nullptr;
    GetAddonAuthor_t GetAuthorFunc = nullptr;
    GetAddonDescription_t GetDescFunc = nullptr;

    // Helpers
    const std::string& GetDisplayName() const {
        return manifest.name.empty() ? id : manifest.name;
    }
    const std::string& GetDisplayVersion() const {
        static const std::string unknown = "?.?.?";
        return manifest.version.empty() ? unknown : manifest.version;
    }
    const std::string& GetDisplayAuthor() const {
        static const std::string unknown = "Unknown";
        return manifest.author.empty() ? unknown : manifest.author;
    }
    bool IsLoaded() const { return hModule != nullptr; }
    bool RequiresRestart() const { return (capabilities & LSPROXY_CAP_REQUIRES_RESTART) != 0; }
};

} // namespace lsproxy
