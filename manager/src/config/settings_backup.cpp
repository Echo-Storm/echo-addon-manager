#include "settings_backup.h"
#include <windows.h>
#include <fstream>

namespace lsproxy {

using json = nlohmann::json;
namespace fs = std::filesystem;

static std::string Timestamp() {
    SYSTEMTIME t; GetLocalTime(&t);
    char b[32]; snprintf(b, sizeof b, "%04d%02d%02d-%02d%02d%02d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    return b;
}

std::string MakeSettingsBackupText(const json& config, const char* proxyVersion) {
    json j;
    j["lsproxy_settings_backup"] = 1;
    j["proxy_version"] = proxyVersion ? proxyVersion : "";
    j["created"] = Timestamp();
    j["config"] = config;
    return j.dump(2);
}

BackupParse ParseSettingsBackup(const std::string& text) {
    BackupParse r;
    if (text.empty()) { r.message = "That file is empty."; return r; }
    if (text.size() > 8u * 1024 * 1024) { r.message = "That file is too large to be a settings backup."; return r; }
    json j;
    try { j = json::parse(text); }
    catch (const std::exception&) { r.message = "That file is not valid settings data (it could not be read as JSON)."; return r; }
    if (!j.is_object()) { r.message = "That file is not a settings backup."; return r; }
    json cfg;
    if (j.contains("lsproxy_settings_backup") && j.contains("config")) cfg = j["config"];
    else if (j.contains("addons") || j.contains("global")) cfg = j;   // a plain config.json copied by hand
    else { r.message = "That file is not a settings backup (no addon settings found)."; return r; }
    if (!cfg.is_object()) { r.message = "That backup is damaged: its settings are not in the expected form."; return r; }
    if (cfg.contains("addons")) {
        if (!cfg["addons"].is_object()) { r.message = "That backup is damaged: the addon settings are not in the expected form."; return r; }
        for (auto it = cfg["addons"].begin(); it != cfg["addons"].end(); ++it)
            if (!it.value().is_object()) { r.message = "That backup is damaged: the settings of '" + it.key() + "' are not in the expected form."; return r; }
        r.addonCount = (int)cfg["addons"].size();
    }
    if (cfg.contains("global") && !cfg["global"].is_object()) { r.message = "That backup is damaged: the manager settings are not in the expected form."; return r; }
    if (!cfg.contains("addons")) cfg["addons"] = json::object();
    if (!cfg.contains("global")) cfg["global"] = json::object();
    r.ok = true; r.config = cfg;
    r.message = "Settings for " + std::to_string(r.addonCount) + " addon" + (r.addonCount == 1 ? "" : "s") + " found.";
    return r;
}

ImportResult ImportSettings(const std::string& text, const json& current, const fs::path& backupDir, const std::function<void(const json&)>& apply) {
    ImportResult r;
    const BackupParse p = ParseSettingsBackup(text);
    if (!p.ok) { r.message = p.message; return r; }
    std::error_code ec;
    fs::create_directories(backupDir, ec);
    const fs::path prev = backupDir / ("config.before-import-" + Timestamp() + ".json");
    {
        std::ofstream out(prev, std::ios::binary);
        out << current.dump(2);
        if (!out) { r.message = "Could not save your current settings first, so nothing was changed."; return r; }
    }
    apply(p.config);
    r.ok = true; r.previousSavedAs = prev;
    r.message = p.message + " Restart Lossless Scaling to apply them. Your previous settings were kept as " + prev.filename().string() + ".";
    return r;
}

} // namespace lsproxy
