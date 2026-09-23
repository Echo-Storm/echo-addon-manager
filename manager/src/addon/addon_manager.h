#pragma once
#include "addon_info.h"
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

struct ImGuiContext;

namespace eam {

class HostImpl;

// Owns the list of addons found under the addons folder: scanning, loading and unloading their DLLs, starting them, switching them on and
// off, installing and removing them, and routing the few calls the host makes into them. Everything public is safe to call from the
// window's thread and from Lossless Scaling's; the list itself is guarded by GetMutex() for code that walks it.
class AddonManager {
public:
    explicit AddonManager(HostImpl* host);                          // the addons folder is "addons" beside the running program
    AddonManager(HostImpl* host, const std::wstring& addonsPath);   // or one of your choosing (the offline tests use this)
    ~AddonManager();

    void ScanAddons();                      // rebuild the list from disk
    void LoadAddons();                      // load the DLL of every enabled addon
    void UnloadAddons();
    void InitializeAddons(ImGuiContext* ctx);   // call each loaded addon's AddonInitialize

    std::vector<AddonInfo>& GetAddons();
    std::mutex& GetMutex();

    struct InstallResult { bool ok = false; std::string message; std::string id; };   // message is written for the user

    // Copies an addon from a folder, a .zip or a single .dll into the addons folder, switched off. An existing addon is never overwritten.
    InstallResult InstallAddon(const std::wstring& source);

    // Takes an addon out of service: unloads it (unless it needs a restart to unload) and moves its folder to addons\.removed\. Its
    // settings stay in config.json, so installing it again brings them back.
    InstallResult RemoveAddon(int index);

    void ToggleAddon(int index, bool enable);
    void LoadAddonNow(int index);   // load and start an enabled addon that is not loaded (auto-load off, or an earlier failure)
    void RenderAddonSettings(int index);
    bool InterceptResource(const wchar_t* name, const wchar_t* type, const void** outData, uint32_t* outSize);
    void LoadAddonIcons();

    HostImpl* GetHost() const { return m_host; }
    const std::wstring& GetAddonsPath() const { return m_addonsPath; }

private:
    bool Inspect(const std::filesystem::path& folder, AddonInfo& info);   // DiscoverAddon plus the switch and the security verdict
    std::string WhyNotLoadable(const AddonInfo& addon);                   // empty when nothing stands in the way
    void LoadModule(AddonInfo& addon);                                    // put the DLL in memory and read what it exports
    void FillFromExports(AddonInfo& addon);                               // manifest fields the DLL can supply
    bool StartAddon(AddonInfo& addon, ImGuiContext* ctx);                 // AddonInitialize, guarded; needs the window thread's context
    void UnloadModule(AddonInfo& addon);
    void ReleaseIcon(AddonInfo& addon);

    HostImpl* m_host;
    std::vector<AddonInfo> m_addons;
    std::mutex m_mutex;
    std::wstring m_addonsPath;
    std::wstring m_configPath;
    bool m_settingsMoved = false;   // a rename carried settings over during this scan and they still need saving
};

} // namespace eam
