#include "addon_manager.h"
#include "addon_dependency.h"
#include "addon_install.h"
#include "addon_security.h"
#include "../gui/icon_loader.h"
#include "../config/config_manager.h"
#include "../event/event_system.h"
#include "../host/host_impl.h"
#include "../log/logger.h"
#include "../../third_party/nlohmann/json.hpp"
#include "../../sdk/include/lsproxy/version.h"
#include "imgui.h"
#include <filesystem>
#include <cwctype>
#include <fstream>
#include <vector>
#include <windows.h>

namespace fs = std::filesystem;

namespace lsproxy {

// SEH wrapper helpers - these must not have C++ objects with destructors
static uint32_t SehGetCaps(GetAddonCaps_t func) {
    __try { return func(); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

static bool SehInitAddon(AddonInit_t func, IHost* host, ImGuiContext* ctx,
                         void* alloc, void* free, void* ud) {
    __try { func(host, ctx, alloc, free, ud); return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static void SehShutdownAddon(AddonShutdown_t func) {
    __try { func(); }
    __except(EXCEPTION_EXECUTE_HANDLER) {}
}

static void SehRenderSettings(AddonRenderSettings_t func) {
    __try { func(); }
    __except(EXCEPTION_EXECUTE_HANDLER) {}
}

static bool SehInterceptResource(AddonInterceptResource_t func,
                                 const wchar_t* name, const wchar_t* type,
                                 const void** outData, uint32_t* outSize) {
    __try { return func(name, type, outData, outSize); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static const char* SehGetString(const char*(*func)()) {
    __try { return func(); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

// Helper: wstring to UTF-8
static std::string WToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), nullptr, 0, nullptr, nullptr);
    std::string result(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), &result[0], size, nullptr, nullptr);
    return result;
}

// "1.2.3" (a suffix such as "-beta.1" is ignored) -> 0x00010203, the LSPROXY_API_VERSION_INT layout.
// Returns 0 for anything unparsable so a malformed manifest never blocks an addon.
static uint32_t ParseVersionInt(const std::string& text) {
    unsigned major = 0, minor = 0, patch = 0;
    if (sscanf_s(text.c_str(), "%u.%u.%u", &major, &minor, &patch) < 2) return 0;
    return (major << 16) | ((minor & 0xFF) << 8) | (patch & 0xFF);
}

AddonManager::AddonManager(HostImpl* host) : m_host(host) {
    wchar_t buffer[MAX_PATH];
    GetModuleFileNameW(NULL, buffer, MAX_PATH);
    fs::path exePath(buffer);
    m_addonsPath = (exePath.parent_path() / "addons").wstring();
    m_configPath = (exePath.parent_path() / "addons" / "config.json").wstring();

    if (!fs::exists(m_addonsPath)) {
        fs::create_directory(m_addonsPath);
    }

    AddonSecurity::LoadTrustedHashes(m_addonsPath);
    ConfigManager::Instance().Load(m_configPath);
}

AddonManager::~AddonManager() {
    UnloadAddons();
    // Release icon textures
    for (auto& addon : m_addons) {
        if (addon.iconTexture) {
            addon.iconTexture->Release();
            addon.iconTexture = nullptr;
        }
    }
}

bool AddonManager::ScanFolder(const fs::path& folder, AddonInfo& info) {
    info.folderName = folder.filename().wstring();
    info.id = WToUtf8(info.folderName);

    fs::path manifestPath = folder / "addon.json";
    if (fs::exists(manifestPath)) {
        ParseManifest(info, manifestPath.wstring());
    }

    bool foundDll = false;

    if (!info.manifest.dll.empty()) {
        fs::path dllPath = folder / info.manifest.dll;
        if (fs::exists(dllPath)) {
            info.dllPath = dllPath.wstring();
            foundDll = true;
        }
    }

    if (!foundDll) {
        fs::path expectedDll = folder / (folder.filename().string() + ".dll");
        if (fs::exists(expectedDll)) {
            info.dllPath = expectedDll.wstring();
            foundDll = true;
        }
    }

    if (!foundDll) {
        for (const auto& subEntry : fs::directory_iterator(folder)) {
            if (subEntry.path().extension() == ".dll") {
                info.dllPath = subEntry.path().wstring();
                foundDll = true;
                break;
            }
        }
    }

    for (const auto& subEntry : fs::directory_iterator(folder)) {
        if (subEntry.path().extension() == ".ini") {
            info.configPath = subEntry.path().wstring();
            break;
        }
    }

    if (!foundDll) return false;

    // Discover icon: manifest "icon" field, or auto-detect icon.png/jpg
    if (!info.manifest.icon.empty()) {
        fs::path iconPath = folder / info.manifest.icon;
        if (fs::exists(iconPath)) info.iconPath = iconPath.wstring();
    }
    if (info.iconPath.empty()) {
        for (const auto& ext : {".png", ".jpg", ".jpeg", ".bmp"}) {
            fs::path iconPath = folder / ("icon" + std::string(ext));
            if (fs::exists(iconPath)) { info.iconPath = iconPath.wstring(); break; }
        }
    }

    info.enabled = ConfigManager::Instance().IsAddonEnabled(info.id, true);
    info.security = AddonSecurity::VerifyDll(info.dllPath, info.id);
    return true;
}

void AddonManager::ScanAddons() {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& addon : m_addons) {
        if (addon.iconTexture) { addon.iconTexture->Release(); addon.iconTexture = nullptr; }
    }
    m_addons.clear();

    if (!fs::exists(m_addonsPath)) return;

    for (const auto& entry : fs::directory_iterator(m_addonsPath)) {
        if (!fs::is_directory(entry.path())) continue;
        if (entry.path().filename().wstring().rfind(L".", 0) == 0) continue;   // .install-* staging folders
        AddonInfo info;
        if (ScanFolder(entry.path(), info)) m_addons.push_back(std::move(info));
    }

    AddonDependency::Resolve(m_addons);
    LOG_INFO("AddonManager", "Scanned %zu addons", m_addons.size());
}

// ---------------------------------------------------------------------------------------
// Installing an addon from a folder, a .zip or a single .dll (the file work is in addon_install.cpp)
// ---------------------------------------------------------------------------------------

AddonManager::InstallResult AddonManager::InstallAddon(const std::wstring& source) {
    InstallResult r;
    const PlaceResult placed = PlaceAddon(fs::path(source), fs::path(m_addonsPath));
    if (!placed.ok) { r.message = placed.message; return r; }

    AddonInfo info;
    if (!ScanFolder(placed.dest, info)) {
        std::error_code ec; fs::remove_all(placed.dest, ec);   // the folder we just created, holding no usable addon
        r.message = "That addon has no DLL, so there is nothing to load.";
        return r;
    }
    info.enabled = false;   // installed switched off: an addon is code that runs inside Lossless Scaling, so the user turns it on on purpose
    ConfigManager::Instance().SetAddonEnabled(info.id, false);
    ConfigManager::Instance().Save();
    r.id = info.id;
    r.ok = true;
    r.message = "Installed '" + info.GetDisplayName() + "' (switched off). Turn it on with its switch.";
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_addons.push_back(std::move(info));
        AddonDependency::Resolve(m_addons);
    }
    LoadAddonIcons();
    LOG_INFO("AddonManager", "Installed addon '%s' from %s", r.id.c_str(), WToUtf8(source).c_str());
    return r;
}

AddonManager::InstallResult AddonManager::RemoveAddon(int index) {
    InstallResult r;
    std::lock_guard<std::mutex> lock(m_mutex);
    if (index < 0 || index >= (int)m_addons.size()) { r.message = "That addon is no longer in the list."; return r; }
    AddonInfo& a = m_addons[index];
    const std::string name = a.GetDisplayName(), id = a.id;
    if (a.hModule) {
        if (a.RequiresRestart()) {
            r.message = "'" + name + "' is loaded and needs Lossless Scaling restarted before it can be removed. Switch it off, restart Lossless Scaling, then remove it.";
            return r;
        }
        UnloadAddon(a);
        if (a.hModule) { r.message = "Could not unload '" + name + "'; restart Lossless Scaling and try again."; return r; }
    }
    const RemoveResult mv = MoveAddonToRemoved(fs::path(m_addonsPath) / a.folderName, fs::path(m_addonsPath));
    if (!mv.ok) { r.message = mv.message; return r; }
    if (a.iconTexture) { a.iconTexture->Release(); a.iconTexture = nullptr; }
    m_addons.erase(m_addons.begin() + index);
    AddonDependency::Resolve(m_addons);
    ConfigManager::Instance().SetAddonEnabled(id, false);   // installing it again starts it switched off; its other settings are kept
    ConfigManager::Instance().Save();
    r.ok = true; r.id = id;
    r.message = "Removed '" + name + "'. Its folder is in addons/.removed (move it back to restore it); its settings are kept.";
    LOG_INFO("AddonManager", "Removed addon '%s' -> %s", id.c_str(), WToUtf8(mv.movedTo.wstring()).c_str());
    return r;
}

void AddonManager::ParseManifest(AddonInfo& addon, const std::wstring& jsonPath) {
    try {
        std::ifstream file(jsonPath);
        auto data = nlohmann::json::parse(file);

        if (data.contains("name")) addon.manifest.name = data["name"].get<std::string>();
        if (data.contains("version")) addon.manifest.version = data["version"].get<std::string>();
        if (data.contains("author")) addon.manifest.author = data["author"].get<std::string>();
        if (data.contains("description")) addon.manifest.description = data["description"].get<std::string>();
        if (data.contains("min_host_version")) addon.manifest.minHostVersion = data["min_host_version"].get<std::string>();
        if (data.contains("dll")) addon.manifest.dll = data["dll"].get<std::string>();
        if (data.contains("icon")) addon.manifest.icon = data["icon"].get<std::string>();

        if (data.contains("dependencies") && data["dependencies"].is_array()) {
            for (const auto& dep : data["dependencies"])
                addon.manifest.dependencies.push_back(dep.get<std::string>());
        }
        if (data.contains("tags") && data["tags"].is_array()) {
            for (const auto& tag : data["tags"])
                addon.manifest.tags.push_back(tag.get<std::string>());
        }

        addon.manifest.parsed = true;
    } catch (const std::exception& e) {
        LOG_WARN("AddonManager", "Failed to parse addon.json for '%s': %s", addon.id.c_str(), e.what());
    }
}

void AddonManager::PopulateFromExports(AddonInfo& addon) {
    if (!addon.hModule) return;

    if (addon.manifest.name.empty() && addon.GetNameFunc) {
        const char* val = SehGetString(addon.GetNameFunc);
        if (val) addon.manifest.name = val;
    }
    if (addon.manifest.version.empty() && addon.GetVersionFunc) {
        const char* val = SehGetString(addon.GetVersionFunc);
        if (val) addon.manifest.version = val;
    }
    if (addon.manifest.author.empty() && addon.GetAuthorFunc) {
        const char* val = SehGetString(addon.GetAuthorFunc);
        if (val) addon.manifest.author = val;
    }
    if (addon.manifest.description.empty() && addon.GetDescFunc) {
        const char* val = SehGetString(addon.GetDescFunc);
        if (val) addon.manifest.description = val;
    }
}

void AddonManager::LoadAddons() {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& addon : m_addons) {
        if (addon.enabled && !addon.hModule) {
            LoadAddon(addon);
        }
    }
}

void AddonManager::LoadAddon(AddonInfo& addon) {
    addon.errorMessage.clear();

    if (!addon.manifest.minHostVersion.empty()) {
        const uint32_t need = ParseVersionInt(addon.manifest.minHostVersion);
        if (need > (uint32_t)LSPROXY_API_VERSION_INT) {
            addon.errorMessage = "Needs a newer " LSPROXY_PRODUCT_NAME " (addon API " + addon.manifest.minHostVersion +
                                 " or newer; this one provides " LSPROXY_API_VERSION_STRING ")";
            LOG_ERROR("AddonManager", "Not loading '%s': %s", addon.id.c_str(), addon.errorMessage.c_str());
            return;
        }
    }

    // Settings > Security Level: 0 allow all, 1 warn, 2 block anything not on trusted_addons.json.
    const int securityLevel = ConfigManager::Instance().GlobalGetOr<int>(nullptr, "security_level", 0);
    if (securityLevel >= 1 && addon.security != SecurityVerdict::Trusted) {
        const char* what = addon.security == SecurityVerdict::Tampered ? "does not match its trusted hash"
                                                                       : "is not on the trusted list";
        if (securityLevel >= 2) {
            addon.errorMessage = std::string("Blocked: the DLL ") + what + " (Settings > Security Level)";
            LOG_WARN("AddonManager", "Not loading '%s': %s", addon.id.c_str(), addon.errorMessage.c_str());
            return;
        }
        LOG_WARN("AddonManager", "Loading '%s' although its DLL %s", addon.id.c_str(), what);
    }

    HMODULE hAddon = LoadLibraryExW(addon.dllPath.c_str(), NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!hAddon) {
        hAddon = LoadLibraryW(addon.dllPath.c_str());
    }

    if (!hAddon) {
        DWORD err = GetLastError();
        addon.errorMessage = "LoadLibrary failed (error " + std::to_string(err) + ")";
        LOG_ERROR("AddonManager", "Failed to load '%s': %s", addon.id.c_str(), addon.errorMessage.c_str());
        return;
    }

    addon.hModule = hAddon;
    addon.faulted = false;

    addon.InitFunc = (AddonInit_t)GetProcAddress(hAddon, "AddonInitialize");
    if (!addon.InitFunc)
        addon.InitFunc = (AddonInit_t)GetProcAddress(hAddon, "AddonInit");

    addon.ShutdownFunc = (AddonShutdown_t)GetProcAddress(hAddon, "AddonShutdown");
    addon.RenderSettingsFunc = (AddonRenderSettings_t)GetProcAddress(hAddon, "AddonRenderSettings");
    addon.InterceptResourceFunc = (AddonInterceptResource_t)GetProcAddress(hAddon, "AddonInterceptResource");
    addon.GetNameFunc = (GetAddonName_t)GetProcAddress(hAddon, "GetAddonName");
    addon.GetVersionFunc = (GetAddonVersion_t)GetProcAddress(hAddon, "GetAddonVersion");
    addon.GetAuthorFunc = (GetAddonAuthor_t)GetProcAddress(hAddon, "GetAddonAuthor");
    addon.GetDescFunc = (GetAddonDescription_t)GetProcAddress(hAddon, "GetAddonDescription");

    GetAddonCaps_t getCaps = (GetAddonCaps_t)GetProcAddress(hAddon, "GetAddonCapabilities");
    if (getCaps) {
        addon.capabilities = SehGetCaps(getCaps);
    }

    PopulateFromExports(addon);

    LOG_INFO("AddonManager", "Loaded addon '%s' v%s",
             addon.GetDisplayName().c_str(), addon.GetDisplayVersion().c_str());
}

void AddonManager::InitializeAddons(ImGuiContext* ctx) {
    std::lock_guard<std::mutex> lock(m_mutex);

    ImGuiMemAllocFunc alloc_func;
    ImGuiMemFreeFunc free_func;
    void* user_data;
    ImGui::GetAllocatorFunctions(&alloc_func, &free_func, &user_data);

    for (auto& addon : m_addons) {
        if (!addon.enabled || !addon.hModule || !addon.InitFunc) continue;

        bool success = SehInitAddon(addon.InitFunc, m_host, ctx,
                                    (void*)alloc_func, (void*)free_func, user_data);
        if (!success) {
            addon.faulted = true;
            addon.errorMessage = "Crashed during initialization";
            LOG_ERROR("AddonManager", "Addon '%s' crashed during init!", addon.id.c_str());
        } else {
            LsProxyAddonEventData eventData;
            eventData.addonName = addon.GetDisplayName().c_str();
            eventData.addonVersion = addon.GetDisplayVersion().c_str();
            EventBus::Instance().Publish(LSPROXY_EVENT_ADDON_LOADED, &eventData, sizeof(eventData));
        }
    }
}

void AddonManager::UnloadAddons() {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (int i = (int)m_addons.size() - 1; i >= 0; i--) {
        if (m_addons[i].hModule) {
            UnloadAddon(m_addons[i]);
        }
    }
}

void AddonManager::UnloadAddon(AddonInfo& addon) {
    if (!addon.hModule) return;

    if (addon.ShutdownFunc) {
        SehShutdownAddon(addon.ShutdownFunc);
    }

    LsProxyAddonEventData eventData;
    eventData.addonName = addon.GetDisplayName().c_str();
    eventData.addonVersion = addon.GetDisplayVersion().c_str();
    EventBus::Instance().Publish(LSPROXY_EVENT_ADDON_UNLOADED, &eventData, sizeof(eventData));

    FreeLibrary(addon.hModule);
    addon.hModule = nullptr;
    addon.InitFunc = nullptr;
    addon.ShutdownFunc = nullptr;
    addon.RenderSettingsFunc = nullptr;
    addon.InterceptResourceFunc = nullptr;
    addon.GetNameFunc = nullptr;
    addon.GetVersionFunc = nullptr;
    addon.GetAuthorFunc = nullptr;
    addon.GetDescFunc = nullptr;
    addon.capabilities = 0;
    addon.faulted = false;
}

void AddonManager::ReloadAddons() {
    UnloadAddons();
    ScanAddons();
    LoadAddons();
}

void AddonManager::LoadAddonIcons() {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& addon : m_addons) {
        if (addon.iconTexture) continue; // Already loaded
        if (addon.iconPath.empty()) continue;

        addon.iconTexture = IconLoader_LoadFromFile(addon.iconPath);
    }
}

void AddonManager::ToggleAddon(int index, bool enable) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (index < 0 || index >= (int)m_addons.size()) return;

    auto& addon = m_addons[index];
    addon.enabled = enable;

    // If addon requires restart, just save the config - don't hot load/unload
    if (addon.RequiresRestart()) {
        LOG_INFO("AddonManager", "Addon '%s' requires restart to %s",
                 addon.GetDisplayName().c_str(), enable ? "enable" : "disable");
    } else {
        if (enable && !addon.hModule) {
            LoadAndInitAddon(addon);
        } else if (!enable && addon.hModule) {
            UnloadAddon(addon);
        }
    }

    ConfigManager::Instance().SetAddonEnabled(addon.id, enable);
    ConfigManager::Instance().Save();
}

// Load an addon after startup: needs the ImGui context of the calling (GUI) thread.
void AddonManager::LoadAndInitAddon(AddonInfo& addon) {
    LoadAddon(addon);
    if (!addon.hModule || !addon.InitFunc) return;

    ImGuiContext* ctx = ImGui::GetCurrentContext();
    if (!ctx) return;

    ImGuiMemAllocFunc alloc_func;
    ImGuiMemFreeFunc free_func;
    void* user_data;
    ImGui::GetAllocatorFunctions(&alloc_func, &free_func, &user_data);

    if (!SehInitAddon(addon.InitFunc, m_host, ctx, (void*)alloc_func, (void*)free_func, user_data)) {
        addon.faulted = true;
        addon.errorMessage = "Crashed during initialization";
        LOG_ERROR("AddonManager", "Addon '%s' crashed during init!", addon.id.c_str());
        return;
    }
    LsProxyAddonEventData eventData;
    eventData.addonName = addon.GetDisplayName().c_str();
    eventData.addonVersion = addon.GetDisplayVersion().c_str();
    EventBus::Instance().Publish(LSPROXY_EVENT_ADDON_LOADED, &eventData, sizeof(eventData));
}

void AddonManager::LoadAddonNow(int index) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (index < 0 || index >= (int)m_addons.size()) return;
    auto& addon = m_addons[index];
    if (addon.enabled && !addon.hModule) LoadAndInitAddon(addon);
}

void AddonManager::RenderAddonSettings(int index) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (index < 0 || index >= (int)m_addons.size()) return;

    auto& addon = m_addons[index];
    if (addon.hModule && addon.RenderSettingsFunc && !addon.settingsFaulted) {
        const uint32_t before = InvalidParameterCount();
        SehRenderSettings(addon.RenderSettingsFunc);
        // A bad argument to a CRT function makes the call fail and leaves whatever the addon does
        // next undefined, and the panel runs every frame. Stop drawing it after the first event.
        if (InvalidParameterCount() != before) {
            addon.settingsFaulted = true;
            addon.errorMessage = "Its settings panel hit an invalid parameter and was turned off (see Logs)";
            LOG_ERROR("AddonManager", "Settings panel of '%s' hit an invalid CRT parameter; not drawing it again this session",
                      addon.id.c_str());
        }
    }
}

bool AddonManager::InterceptResource(const wchar_t* name, const wchar_t* type,
                                     const void** outData, uint32_t* outSize) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& addon : m_addons) {
        if (addon.enabled && addon.InterceptResourceFunc && !addon.faulted) {
            if (SehInterceptResource(addon.InterceptResourceFunc, name, type, outData, outSize)) {
                return true;
            }
        }
    }
    return false;
}

std::vector<AddonInfo>& AddonManager::GetAddons() { return m_addons; }
std::mutex& AddonManager::GetMutex() { return m_mutex; }

} // namespace lsproxy
