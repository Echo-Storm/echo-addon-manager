#pragma once
#include <mutex>
#include <string>
#include "../../third_party/nlohmann/json.hpp"

namespace eam {

// The settings file, addons\config.json:
//   { "addons": { "<addon id>": { "_enabled": true, "<key>": "<value>", ... } },
//     "global":  { "<host setting>": ..., "<section>": { "<host setting>": ... } } }
// One instance for the whole process. Every call is safe from any thread.
class ConfigManager {
public:
    static ConfigManager& Instance();

    // Reads the file. One that cannot be read as a JSON object is copied to <name>.corrupt and settings start empty; the copy is what
    // a person would repair, so it is never overwritten by a later Save. An old addons_config.ini beside it is carried over.
    void Load(const std::wstring& path);

    // Writes the file if anything changed, by writing a temporary file and swapping it in, so a crash mid-write cannot leave half a file.
    void Save();

    // An addon's own settings. Every value is text; a hand-edited number or boolean reads back as its text ("1" / "0" for booleans).
    std::string Get(const std::string& addonId, const std::string& key, const std::string& defaultVal = "");
    void Set(const std::string& addonId, const std::string& key, const std::string& value);

    // Whether an addon is switched on. Kept under "_enabled" so it cannot collide with an addon's own "enabled" setting (Neural Rendering
    // has one). A boolean "enabled" written by older versions is still honoured; text there belongs to the addon.
    bool IsAddonEnabled(const std::string& addonId, bool defaultVal = true);
    void SetAddonEnabled(const std::string& addonId, bool enabled);

    // An addon that changed its folder name keeps its settings: moves everything stored under `from` to `to`, unless `to` already has settings
    // of its own. Returns true if something moved (the caller saves).
    bool RenameAddonSection(const std::string& from, const std::string& to);

    // A copy of everything, and a wholesale replacement that is saved at once (settings backup and restore).
    // With `untilRestart` (loading settings from a file), every later write is dropped until the file is loaded again at the next start: the
    // running addons still hold their old settings, and one that writes them all back when it shuts down would undo the import.
    nlohmann::json Snapshot() const;
    void Replace(const nlohmann::json& all, bool untilRestart = false);
    bool RestartPending() const;

    // The host's own settings under "global"; `section` may be null for a top-level key. GlobalGet gives null when the setting is absent.
    nlohmann::json GlobalGet(const char* section, const char* key) const;
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
    std::wstring m_basePath;     // the folder holding the file, which is the addons folder
    std::string m_lastWritten;   // what is on disk, so an unchanged Save writes nothing
    mutable std::mutex m_mutex;
    bool m_loaded = false;
    bool m_frozen = false;       // settings were imported: writes wait for the restart
    bool m_frozenNoted = false;  // the first dropped write is logged, not every one
    bool Frozen();               // true (and logged once) when a write must be dropped; call with m_mutex held
};

} // namespace eam
