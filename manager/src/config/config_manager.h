#pragma once
#include <mutex>
#include <string>
#include "../../third_party/nlohmann/json.hpp"

namespace lsproxy {

class ConfigManager {
public:
    static ConfigManager& Instance();

    void Load(const std::wstring& path);
    void Save();

    // Per-addon config. Values are strings; a hand-edited number or bool reads back as text.
    std::string Get(const std::string& addonId, const std::string& key, const std::string& defaultVal = "");
    void Set(const std::string& addonId, const std::string& key, const std::string& value);

    // Addon enabled/disabled. Kept under its own key ("_enabled") so it cannot collide with an
    // addon's own "enabled" setting (LSP-NeuralRender has one); a legacy boolean "enabled" written
    // by older versions of the host is still honoured.
    bool IsAddonEnabled(const std::string& addonId, bool defaultVal = true);
    void SetAddonEnabled(const std::string& addonId, bool enabled);

    // Backup and restore: a copy of everything, and a wholesale replacement (saved at once). Thread-safe.
    nlohmann::json Snapshot() const;
    void Replace(const nlohmann::json& all);

    // Host settings ("global"). Thread-safe; `section` may be null for a top-level key.
    nlohmann::json GlobalGet(const char* section, const char* key) const; // null when absent
    void GlobalSet(const char* section, const char* key, nlohmann::json value);

    template <class T>
    T GlobalGetOr(const char* section, const char* key, T defaultVal) const {
        nlohmann::json v = GlobalGet(section, key);
        if (v.is_null()) return defaultVal;
        try { return v.get<T>(); } catch (...) { return defaultVal; }
    }

private:
    ConfigManager() = default;
    void MigrateFromIni();

    nlohmann::json m_data;
    std::wstring m_path;
    std::wstring m_basePath; // addons directory
    std::string m_lastWritten; // skip rewriting identical content
    mutable std::mutex m_mutex;
    bool m_loaded = false;
};

} // namespace lsproxy
