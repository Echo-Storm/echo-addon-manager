#include "config_manager.h"
#include "../log/logger.h"
#include <filesystem>
#include <fstream>
#include <windows.h>

namespace fs = std::filesystem;

namespace lsproxy {

static constexpr const char* kEnabledKey = "_enabled";  // host-owned
static constexpr const char* kLegacyEnabledKey = "enabled";

ConfigManager& ConfigManager::Instance() {
    static ConfigManager instance;
    return instance;
}

void ConfigManager::Load(const std::wstring& path) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_path = path;
    m_basePath = fs::path(path).parent_path().wstring();

    std::error_code ec;
    if (fs::exists(path, ec)) {
        try {
            std::ifstream file(path);
            m_data = nlohmann::json::parse(file);
            if (!m_data.is_object()) throw std::runtime_error("top level is not an object");
            m_loaded = true;
            LOG_INFO("Config", "Loaded config from config.json");
            return;
        } catch (const std::exception& e) {
            // Do not let the next Save() silently replace a file the user may want to repair.
            const std::wstring backup = path + L".corrupt";
            CopyFileW(path.c_str(), backup.c_str(), FALSE);
            LOG_ERROR("Config", "Failed to parse config.json (%s); kept a copy as config.json.corrupt and started fresh",
                      e.what());
        }
    }

    // Try migrating from old INI
    MigrateFromIni();
    m_loaded = true;
}

void ConfigManager::MigrateFromIni() {
    fs::path iniPath = fs::path(m_basePath) / "addons_config.ini";
    m_data = nlohmann::json{{"global", nlohmann::json::object()}, {"addons", nlohmann::json::object()}};
    std::error_code ec;
    if (!fs::exists(iniPath, ec)) return;

    LOG_INFO("Config", "Migrating from addons_config.ini...");

    // Scan addons directory for folder names to read INI keys
    if (fs::exists(m_basePath, ec)) {
        for (const auto& entry : fs::directory_iterator(m_basePath)) {
            if (!entry.is_directory()) continue;
            std::wstring name = entry.path().filename().wstring();
            int status = GetPrivateProfileIntW(L"Addons", name.c_str(), 1, iniPath.wstring().c_str());

            int size = WideCharToMultiByte(CP_UTF8, 0, name.c_str(), (int)name.size(), nullptr, 0, nullptr, nullptr);
            std::string utf8Name(size, 0);
            WideCharToMultiByte(CP_UTF8, 0, name.c_str(), (int)name.size(), &utf8Name[0], size, nullptr, nullptr);

            m_data["addons"][utf8Name][kEnabledKey] = (status != 0);
        }
    }

    try {
        std::ofstream file(m_path);
        file << m_data.dump(2);
        LOG_INFO("Config", "Migration complete, saved config.json");
    } catch (...) {
        LOG_ERROR("Config", "Failed to save migrated config");
    }
}

void ConfigManager::Save() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_loaded || m_path.empty()) {
        LOG_WARN("Config", "Save called before config was loaded, ignoring");
        return;
    }
    try {
        std::string json = m_data.dump(2);
        // Only write if we have actual content (protect against wiping the file)
        if (json.size() <= 4) {
            LOG_WARN("Config", "Save aborted: config data is empty");
            return;
        }
        if (json == m_lastWritten) return;

        // Write beside the target and swap it in, so a crash or a full disk mid-write cannot
        // leave a truncated config.json behind.
        const std::wstring tmp = m_path + L".tmp";
        {
            std::ofstream file(tmp, std::ios::binary | std::ios::trunc);
            file << json;
            file.flush();
            if (!file) {
                LOG_ERROR("Config", "Failed to write config temp file");
                return;
            }
        }
        if (!MoveFileExW(tmp.c_str(), m_path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
            LOG_ERROR("Config", "Failed to replace config.json (error %lu)", GetLastError());
            DeleteFileW(tmp.c_str());
            return;
        }
        m_lastWritten = std::move(json);
    } catch (const std::exception& e) {
        LOG_ERROR("Config", "Failed to save config: %s", e.what());
    }
}

std::string ConfigManager::Get(const std::string& addonId, const std::string& key, const std::string& defaultVal) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto addons = m_data.find("addons");
    if (addons == m_data.end() || !addons->is_object()) return defaultVal;
    auto addon = addons->find(addonId);
    if (addon == addons->end() || !addon->is_object()) return defaultVal;
    auto value = addon->find(key);
    if (value == addon->end()) return defaultVal;

    if (value->is_string()) return value->get<std::string>();
    if (value->is_boolean()) return value->get<bool>() ? "1" : "0";
    if (value->is_number()) return value->dump();
    return defaultVal;
}

void ConfigManager::Set(const std::string& addonId, const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_data["addons"][addonId][key] = value;
}

bool ConfigManager::IsAddonEnabled(const std::string& addonId, bool defaultVal) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto addons = m_data.find("addons");
    if (addons == m_data.end() || !addons->is_object()) return defaultVal;
    auto addon = addons->find(addonId);
    if (addon == addons->end() || !addon->is_object()) return defaultVal;

    auto own = addon->find(kEnabledKey);
    if (own != addon->end() && own->is_boolean()) return own->get<bool>();

    // Older hosts stored the flag as a boolean under "enabled". A string there belongs to the addon.
    auto legacy = addon->find(kLegacyEnabledKey);
    if (legacy != addon->end() && legacy->is_boolean()) return legacy->get<bool>();
    return defaultVal;
}

void ConfigManager::SetAddonEnabled(const std::string& addonId, bool enabled) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto& addon = m_data["addons"][addonId];
    addon[kEnabledKey] = enabled;
    auto legacy = addon.find(kLegacyEnabledKey);
    if (legacy != addon.end() && legacy->is_boolean()) addon.erase(legacy);
}

nlohmann::json ConfigManager::Snapshot() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_data;
}

void ConfigManager::Replace(const nlohmann::json& all) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_data = all;
        if (!m_data.is_object()) m_data = nlohmann::json::object();
    }
    Save();
}

nlohmann::json ConfigManager::GlobalGet(const char* section, const char* key) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto global = m_data.find("global");
    if (global == m_data.end() || !global->is_object()) return nullptr;
    const nlohmann::json* node = &*global;
    if (section) {
        auto s = node->find(section);
        if (s == node->end() || !s->is_object()) return nullptr;
        node = &*s;
    }
    auto v = node->find(key);
    return v == node->end() ? nlohmann::json(nullptr) : *v;
}

void ConfigManager::GlobalSet(const char* section, const char* key, nlohmann::json value) {
    std::lock_guard<std::mutex> lock(m_mutex);
    nlohmann::json& global = m_data["global"];
    if (!global.is_object()) global = nlohmann::json::object();
    nlohmann::json* node = &global;
    if (section) {
        nlohmann::json& s = global[section];
        if (!s.is_object()) s = nlohmann::json::object();
        node = &s;
    }
    (*node)[key] = std::move(value);
}

} // namespace lsproxy
