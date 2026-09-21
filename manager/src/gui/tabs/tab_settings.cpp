#include "tab_settings.h"
#include "../gui_scale.h"
#include "../../addon/addon_manager.h"
#include "../../config/config_manager.h"
#include "../../config/settings_backup.h"
#include "../../diag/diagnostics.h"
#include "../../host/gpu_stats.h"
#include "../../log/logger.h"
#include "../../../sdk/include/lsproxy/version.h"
#include "../widgets/file_dialog.h"
#include "../widgets/toast.h"
#include "../widgets/tooltip.h"
#include "../gui_manager.h"
#include "imgui.h"
#include "lsproxy/lsp_widgets.h"
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace lsproxy {

namespace fs = std::filesystem;

static void OpenPath(const std::wstring& path) { ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL); }

static std::wstring ExeDir() {
    wchar_t buffer[MAX_PATH];
    GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    return fs::path(buffer).parent_path().wstring();
}

static std::string Utf8(const std::wstring& w) {
    if (w.empty()) return {};
    std::string s((size_t)WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr), 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], (int)s.size(), nullptr, nullptr);
    return s;
}

static std::string WindowsVersion() {
    using RtlGetVersion_t = LONG (WINAPI*)(OSVERSIONINFOW*);
    OSVERSIONINFOW v{ sizeof v };
    if (HMODULE nt = GetModuleHandleW(L"ntdll.dll"))
        if (auto f = (RtlGetVersion_t)GetProcAddress(nt, "RtlGetVersion")) f(&v);
    char b[64]; snprintf(b, sizeof b, "Windows %lu.%lu build %lu", v.dwMajorVersion, v.dwMinorVersion, v.dwBuildNumber);
    return b;
}

// What goes into info.txt of the diagnostics zip: versions, the machine, and every addon's state. No personal data beyond the folder
// path of the install.
static std::string BuildDiagnosticsSummary(AddonManager* manager) {
    std::ostringstream o;
    o << LSPROXY_PRODUCT_NAME << " diagnostics\n";
    o << "manager: " << LSPROXY_VERSION_STRING << " (built " << __DATE__ << " " << __TIME__ << ")\n";
    o << "system: " << WindowsVersion() << "\n";
    o << "install folder: " << Utf8(ExeDir()) << "\n";
    GpuStats::Instance().SampleOnce();
    const GpuStats::Snapshot g = GpuStats::Instance().Get();
    if (g.ok) o << "gpu: " << g.name << ", driver " << g.driver << ", " << g.vramTotalMB << " MB, power limit " << (int)g.powerLimitW << " W\n";
    else o << "gpu: not available (" << g.why << ")\n";
    o << "\naddons:\n";
    if (manager) for (const auto& a : manager->GetAddons()) {
        o << "  " << a.GetDisplayName() << " [" << a.id << "] v" << a.GetDisplayVersion()
          << "  enabled=" << (a.enabled ? "yes" : "no") << "  loaded=" << (a.IsLoaded() ? "yes" : "no") << "  faulted=" << (a.faulted ? "yes" : "no")
          << "  restart-needed=" << (a.RequiresRestart() ? "yes" : "no");
        if (!a.errorMessage.empty()) o << "  error: " << a.errorMessage;
        o << "\n";
    }
    auto& cfg = ConfigManager::Instance();
    o << "\nmanager settings: security_level=" << cfg.GlobalGetOr<int>(nullptr, "security_level", 0) << " log_level=" << cfg.GlobalGetOr<int>(nullptr, "log_level", 2)
      << " auto_load=" << cfg.GlobalGetOr<bool>(nullptr, "auto_load", true) << " interface_size=" << cfg.GlobalGetOr<int>("ui", "scale_percent", 100) << "%\n";
    return o.str();
}

static std::wstring DesktopDir() {
    PWSTR p = nullptr; std::wstring out;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &p)) && p) { out = p; CoTaskMemFree(p); }
    if (out.empty()) out = ExeDir();
    return out;
}

static std::wstring s_importPath;
static std::string s_importText;   // a settings file waiting for confirmation
static int s_importAddons = 0;
static bool s_askImport = false;

// Every option here takes effect as soon as it is changed and is saved at once.
void RenderTabSettings(AddonManager* manager) {
    auto& config = ConfigManager::Instance();
    ImGui::Dummy(ImVec2(0, S(4)));

    // ---- Backup and restore
    lsp::SectionLabel("Backup and restore");
    ImGui::Dummy(ImVec2(0, S(2)));
    if (lsp::Button("Save settings to a file", lsp::icons::kSave, lsp::ButtonKind::Primary)) {
        std::wstring path;
        SYSTEMTIME t; GetLocalTime(&t);
        wchar_t name[64]; swprintf(name, 64, L"EchoAddonManager-settings-%04d%02d%02d.json", t.wYear, t.wMonth, t.wDay);
        if (widgets::PickSaveFile(L"Save settings", name, L"Settings backup", L"*.json", path)) {
            std::ofstream out(fs::path(path), std::ios::binary);
            out << MakeSettingsBackupText(config.Snapshot(), LSPROXY_VERSION_STRING);
            out.close();
            widgets::ToastShow(out ? "Settings saved." : "Could not write that file.", out ? widgets::ToastType::Success : widgets::ToastType::Error);
        }
    }
    widgets::Tip("Saves the settings of every addon and of this manager into one file, to keep or to move to another PC.");
    ImGui::SameLine();
    if (lsp::Button("Load settings from a file", lsp::icons::kDownload)) {
        std::wstring path;
        if (widgets::PickOpenFile(L"Load settings", L"Settings backup", L"*.json", path)) {
            std::ifstream in(fs::path(path), std::ios::binary);
            std::stringstream ss; ss << in.rdbuf();
            const std::string text = ss.str();
            const BackupParse p = ParseSettingsBackup(text);
            if (!p.ok) widgets::ToastShow(p.message, widgets::ToastType::Error, 6.0f);
            else { s_importPath = path; s_importText = text; s_importAddons = p.addonCount; s_askImport = true; }
        }
    }
    widgets::Tip("Replaces the current settings with the ones in a file saved earlier. You are asked first, and your current settings are kept aside. Takes effect after Lossless Scaling restarts.");
    if (s_askImport) { ImGui::OpenPopup("Load settings"); s_askImport = false; }
    if (ImGui::BeginPopupModal("Load settings", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 32.0f);
        ImGui::TextWrapped("Replace your settings with the ones in this file?");
        ImGui::TextDisabled("%s", Utf8(fs::path(s_importPath).filename().wstring()).c_str());
        ImGui::Dummy(ImVec2(0, S(4)));
        ImGui::TextWrapped("It holds the settings of %d addon%s. Your current settings are saved next to the config first, and the new ones apply after Lossless Scaling restarts.",
                           s_importAddons, s_importAddons == 1 ? "" : "s");
        ImGui::PopTextWrapPos();
        ImGui::Dummy(ImVec2(0, S(6)));
        if (lsp::Button("Load", lsp::icons::kCheck, lsp::ButtonKind::Primary)) {
            const ImportResult r = ImportSettings(s_importText, config.Snapshot(), fs::path(ExeDir()) / L"backups", [&](const nlohmann::json& j) { config.Replace(j); });
            widgets::ToastShow(r.message, r.ok ? widgets::ToastType::Success : widgets::ToastType::Error, 9.0f);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (lsp::Button("Cancel", lsp::icons::kClose)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // ---- Interface
    ImGui::Dummy(ImVec2(0, S(16)));
    lsp::SectionLabel("Interface");
    ImGui::Dummy(ImVec2(0, S(2)));
    {
        int pct = config.GlobalGetOr<int>("ui", "scale_percent", 100);
        const int def = 100;
        ImGui::SetNextItemWidth(S(320));
        if (lsp::SliderInt("Interface size", &pct, 75, 200, "%d%%", 0, &def)) {
            config.GlobalSet("ui", "scale_percent", pct);
            config.Save();
            GuiManager::RequestUserScale();   // applied at the start of the next frame
        }
        widgets::Tip("Makes everything in this window smaller or larger, on top of Windows' own display scaling. 100% is the size Windows asks for.");
    }
    bool openOnStart = config.GlobalGetOr<bool>("ui", "open_on_start", true);
    if (ImGui::Checkbox("Open this window when Lossless Scaling starts", &openOnStart)) { config.GlobalSet("ui", "open_on_start", openOnStart); config.Save(); }
    widgets::Tip("Off = the manager starts hidden in the notification area (tray). Your addons load and run either way. Closing this window with the X only hides it.");
    bool hotkeyOn = config.GlobalGetOr<bool>("ui", "hotkey_enabled", true);
    if (ImGui::Checkbox("Open / close hotkey: Ctrl+Shift +", &hotkeyOn)) { config.GlobalSet("ui", "hotkey_enabled", hotkeyOn); config.Save(); GuiManager::ApplyHotkey(); }
    widgets::Tip("Works anywhere in Windows, including while a game has focus. If another program already uses the combination, the note below says so; pick another key.");
    ImGui::SameLine();
    {
        int idx = config.GlobalGetOr<int>("ui", "hotkey_vk", VK_F12) - VK_F1;
        if (idx < 0 || idx > 11) idx = 11;
        const char* names[] = { "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12" };
        const ImGuiStyle& st = ImGui::GetStyle();
        ImGui::SetNextItemWidth(ImGui::CalcTextSize("F12").x + st.FramePadding.x * 2.0f + ImGui::GetFrameHeight() + st.ItemInnerSpacing.x);
        if (ImGui::Combo("##mgrhotkey", &idx, names, 12)) { config.GlobalSet("ui", "hotkey_vk", VK_F1 + idx); config.Save(); GuiManager::ApplyHotkey(); }
    }
    ImGui::TextDisabled("Hotkey: %s", GuiManager::HotkeyStatus());

    // ---- Addons
    ImGui::Dummy(ImVec2(0, S(16)));
    lsp::SectionLabel("Addons");
    ImGui::Dummy(ImVec2(0, S(2)));
    {
        int securityLevel = config.GlobalGetOr<int>(nullptr, "security_level", 0);
        const char* securityItems[] = { "Allow all addons", "Warn about untrusted addons", "Block untrusted addons" };
        ImGui::SetNextItemWidth(S(320));
        if (ImGui::Combo("Security", &securityLevel, securityItems, 3)) {
            config.GlobalSet(nullptr, "security_level", securityLevel); config.Save();
            widgets::ToastShow("Applies the next time an addon is loaded", widgets::ToastType::Info);
        }
        widgets::Tip("What to do with an addon whose DLL is not on the trusted list (addons\\trusted_addons.json): allow it, log a warning, or refuse to load it. With no such file, blocking refuses every addon.");
    }
    bool autoLoad = config.GlobalGetOr<bool>(nullptr, "auto_load", true);
    if (ImGui::Checkbox("Load enabled addons when Lossless Scaling starts", &autoLoad)) { config.GlobalSet(nullptr, "auto_load", autoLoad); config.Save(); }
    widgets::Tip("Off = start with nothing loaded, then press Load now on the addon you want.");

    // ---- Logging and support
    ImGui::Dummy(ImVec2(0, S(16)));
    lsp::SectionLabel("Logging and support");
    ImGui::Dummy(ImVec2(0, S(2)));
    {
        int logLevel = config.GlobalGetOr<int>(nullptr, "log_level", 2);
        const char* logItems[] = { "Trace", "Debug", "Info", "Warn", "Error" };
        ImGui::SetNextItemWidth(S(320));
        if (ImGui::Combo("Log detail", &logLevel, logItems, 5)) { config.GlobalSet(nullptr, "log_level", logLevel); config.Save(); Logger::Instance().SetMinLevel(static_cast<LogLevel>(logLevel)); }
        widgets::Tip("Messages below this level are dropped before they are recorded, in the Logs tab and in EchoAddonManager.log. Info is a good default; Debug and Trace are for tracking down a problem.");
    }
    if (lsp::Button("Create a diagnostics file", lsp::icons::kPackage, lsp::ButtonKind::Primary)) {
        const DiagResult d = CreateDiagnosticsZip(fs::path(ExeDir()), BuildDiagnosticsSummary(manager), fs::path(DesktopDir()));
        widgets::ToastShow(d.message, d.ok ? widgets::ToastType::Success : widgets::ToastType::Error, 9.0f);
        if (d.ok) ShellExecuteW(nullptr, L"open", L"explorer.exe", (L"/select,\"" + d.zip.wstring() + L"\"").c_str(), nullptr, SW_SHOWNORMAL);
    }
    widgets::Tip("Puts the logs, the settings and a short summary (versions, GPU, which addons are on) into one zip on your Desktop, for a bug report. Nothing is uploaded: you decide who gets the file.");
    ImGui::SameLine();
    if (lsp::Button("Logs folder", lsp::icons::kFolder)) OpenPath(ExeDir() + L"\\logs");
    ImGui::SameLine();
    if (lsp::Button("Addons folder", lsp::icons::kFolder)) OpenPath(ExeDir() + L"\\addons");
    ImGui::SameLine();
    if (lsp::Button("config.json", lsp::icons::kFile)) OpenPath(ExeDir() + L"\\addons\\config.json");

    ImGui::Dummy(ImVec2(0, S(14)));
    ImGui::TextDisabled("Changes are saved as you make them.");
}

} // namespace lsproxy
