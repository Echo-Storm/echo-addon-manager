#pragma once
#include "../../third_party/nlohmann/json.hpp"
#include <filesystem>
#include <functional>
#include <string>

namespace lsproxy {

// The text of a settings backup file: the whole configuration (every addon's settings and the manager's own), wrapped with the
// version and date so a file can be recognised later.
std::string MakeSettingsBackupText(const nlohmann::json& config, const char* proxyVersion);

struct BackupParse {
    bool ok = false;
    std::string message;        // for the user
    nlohmann::json config;      // the configuration inside, when ok
    int addonCount = 0;         // how many addons' settings it holds
};

// Reads a backup (our wrapper, or a bare config.json). Validates the shape and refuses anything that is not a settings file.
BackupParse ParseSettingsBackup(const std::string& text);

struct ImportResult { bool ok = false; std::string message; std::filesystem::path previousSavedAs; };

// Replaces the settings with the ones in `text`: validates first, writes the current settings to backupDir\config.before-import-<time>.json,
// and only then calls `apply` with the new configuration. Nothing changes if any step fails.
ImportResult ImportSettings(const std::string& text, const nlohmann::json& current, const std::filesystem::path& backupDir,
                            const std::function<void(const nlohmann::json&)>& apply);

} // namespace lsproxy
