#pragma once
#include "addon_info.h"
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

struct ImGuiContext;

namespace lsproxy {

class HostImpl;

class AddonManager {
public:
    explicit AddonManager(HostImpl* host);                                   // addons live in "addons" beside the running program
    AddonManager(HostImpl* host, const std::wstring& addonsPath);            // or in a folder of your choosing (the offline tests use this)
    ~AddonManager();

    void ScanAddons();
    void LoadAddons();
    void UnloadAddons();
    void ReloadAddons();
    void InitializeAddons(ImGuiContext* ctx);

    // Thread-safe accessors
    std::vector<AddonInfo>& GetAddons();
    std::mutex& GetMutex();

    // Installs an addon from a folder, a .zip or a single .dll into the addons folder, switched off. Never overwrites an
    // existing addon folder. The message is meant for the user.
    struct InstallResult { bool ok = false; std::string message; std::string id; };
    InstallResult InstallAddon(const std::wstring& source);

    // Takes an addon out of service: unloads it (unless it needs a restart to unload) and moves its folder to addons\.removed\.
    // Its settings stay in config.json, so installing it again brings them back.
    InstallResult RemoveAddon(int index);

    void ToggleAddon(int index, bool enable);
    // Loads and initialises an enabled addon that is not loaded yet (auto-load off, or a failed load).
    void LoadAddonNow(int index);
    void RenderAddonSettings(int index);
    bool InterceptResource(const wchar_t* name, const wchar_t* type,
                           const void** outData, uint32_t* outSize);

    // Load icon textures for all addons that have icon files
    void LoadAddonIcons();

    HostImpl* GetHost() const { return m_host; }

    // Paths
    const std::wstring& GetAddonsPath() const { return m_addonsPath; }

private:
    bool ScanFolder(const std::filesystem::path& folder, AddonInfo& out);   // reads one addon folder; false if it has no DLL
    void LoadAddon(AddonInfo& addon);
    void LoadAndInitAddon(AddonInfo& addon);   // hot path: needs the GUI thread's ImGui context
    void UnloadAddon(AddonInfo& addon);
    void ParseManifest(AddonInfo& addon, const std::wstring& jsonPath);
    void PopulateFromExports(AddonInfo& addon);

    HostImpl* m_host;
    std::vector<AddonInfo> m_addons;
    std::mutex m_mutex;
    std::wstring m_addonsPath;
    std::wstring m_configPath;
};

} // namespace lsproxy
