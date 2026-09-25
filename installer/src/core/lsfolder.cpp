#include "lsfolder.h"
#include "fileinfo.h"
#include <windows.h>
#include <tlhelp32.h>
#include <fstream>
#include <filesystem>
#include <sstream>

namespace setup {

bool LooksLikeLosslessScaling(const std::wstring& dir) {
    if (dir.empty()) return false;
    return Exists(JoinPath(dir, L"LosslessScaling.exe")) && (Exists(JoinPath(dir, L"Lossless.dll")) || Exists(JoinPath(dir, L"Lossless_original.dll")));
}

std::vector<std::wstring> ParseSteamLibraries(const std::string& vdfText) {
    std::vector<std::wstring> libraries;
    std::istringstream in(vdfText);
    std::string line;
    while (std::getline(in, line)) {
        // a line such as:   "path"		"D:\\SteamLibrary"
        const size_t key = line.find("\"path\"");
        if (key == std::string::npos) continue;
        const size_t open = line.find('"', key + 6);
        if (open == std::string::npos) continue;
        std::string value;
        size_t i = open + 1;
        for (; i < line.size() && line[i] != '"'; ++i) {
            if (line[i] == '\\' && i + 1 < line.size() && (line[i + 1] == '\\' || line[i + 1] == '"')) { value += line[i + 1]; ++i; }
            else value += line[i];
        }
        if (i >= line.size() || value.empty()) continue;   // no closing quote: not a usable line
        libraries.push_back(Widen(value));
    }
    return libraries;
}

namespace {

std::wstring g_registryKey = L"Software\\LSAddonManager";

std::wstring RegistryString(HKEY root, const wchar_t* subkey, const wchar_t* value) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, subkey, 0, KEY_READ, &key) != ERROR_SUCCESS) return std::wstring();
    wchar_t buf[1024] = {};
    DWORD bytes = sizeof buf - sizeof(wchar_t), type = 0;
    const bool ok = RegQueryValueExW(key, value, nullptr, &type, reinterpret_cast<BYTE*>(buf), &bytes) == ERROR_SUCCESS && type == REG_SZ;
    RegCloseKey(key);
    return ok ? std::wstring(buf) : std::wstring();
}

std::wstring ParentOf(const std::wstring& path) {
    const size_t at = path.find_last_of(L"\\/");
    return at == std::wstring::npos ? std::wstring() : path.substr(0, at);
}

void Add(std::vector<Candidate>& out, const std::wstring& dir, const char* how) {
    if (!LooksLikeLosslessScaling(dir)) return;
    const std::wstring key = LowerCase(dir);
    for (const auto& c : out) if (LowerCase(c.dir) == key) return;
    out.push_back({ dir, how });
}

} // namespace

std::vector<std::wstring> ScanDrive(const std::wstring& driveRoot) {
    std::vector<std::wstring> found;
    if (driveRoot.empty()) return found;
    static const wchar_t* const kParents[] = {
        L"", L"Utilities", L"Games", L"Apps", L"Programs", L"Tools", L"Software", L"Program Files", L"Program Files (x86)",
        L"Steam\\steamapps\\common", L"SteamLibrary\\steamapps\\common", L"Games\\Steam\\steamapps\\common", L"Games\\SteamLibrary\\steamapps\\common"};
    namespace fs = std::filesystem;
    for (const wchar_t* parent : kParents) {
        std::error_code ec;
        const fs::path dir = *parent ? fs::path(driveRoot) / parent : fs::path(driveRoot);
        if (!fs::is_directory(dir, ec)) continue;
        for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
            std::error_code e2;
            if (!it->is_directory(e2)) continue;
            const std::wstring name = LowerCase(it->path().filename().wstring());
            if (name.rfind(L"lossless scaling", 0) != 0 && name.rfind(L"losslessscaling", 0) != 0) continue;
            const std::wstring candidate = it->path().wstring();
            if (!LooksLikeLosslessScaling(candidate)) continue;
            bool seen = false;
            for (const auto& f : found) if (LowerCase(f) == LowerCase(candidate)) seen = true;
            if (!seen) found.push_back(candidate);
        }
    }
    return found;
}

std::vector<Candidate> FindCandidates() {
    std::vector<Candidate> found;

    // 1. a Lossless Scaling that is running right now: its own folder is certain
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W p = {};
        p.dwSize = sizeof p;
        for (BOOL ok = Process32FirstW(snap, &p); ok; ok = Process32NextW(snap, &p)) {
            if (_wcsicmp(p.szExeFile, L"LosslessScaling.exe") != 0) continue;
            HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, p.th32ProcessID);
            if (!h) continue;
            wchar_t path[MAX_PATH * 2] = {};
            DWORD n = static_cast<DWORD>(sizeof path / sizeof path[0]);
            if (QueryFullProcessImageNameW(h, 0, path, &n)) Add(found, ParentOf(path), "running");
            CloseHandle(h);
        }
        CloseHandle(snap);
    }

    // 2. the folder used last time
    Add(found, RememberedFolder(), "used last time");

    // 3. Steam: its own folder and every library it lists
    std::wstring steam = RegistryString(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath");
    if (steam.empty()) steam = RegistryString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Valve\\Steam", L"InstallPath");
    for (wchar_t& c : steam) if (c == L'/') c = L'\\';
    if (!steam.empty()) {
        Add(found, JoinPath(steam, L"steamapps\\common\\Lossless Scaling"), "Steam library");
        std::ifstream vdf(std::filesystem::path(JoinPath(steam, L"steamapps\\libraryfolders.vdf")), std::ios::binary);
        if (vdf) {
            std::stringstream ss;
            ss << vdf.rdbuf();
            for (const auto& lib : ParseSteamLibraries(ss.str())) Add(found, JoinPath(lib, L"steamapps\\common\\Lossless Scaling"), "Steam library");
        }
    }

    // 4. the usual places
    Add(found, L"C:\\Program Files (x86)\\Steam\\steamapps\\common\\Lossless Scaling", "default location");
    Add(found, L"C:\\Program Files\\Lossless Scaling", "default location");
    Add(found, L"C:\\Program Files (x86)\\Lossless Scaling", "default location");

    // 5. copies that are not from Steam: the usual places on every fixed drive
    const DWORD drives = GetLogicalDrives();
    for (int i = 0; i < 26; ++i) {
        if (!(drives & (1u << i))) continue;
        const std::wstring root = std::wstring(1, static_cast<wchar_t>(L'A' + i)) + L":\\";
        if (GetDriveTypeW(root.c_str()) != DRIVE_FIXED) continue;
        for (const auto& dir : ScanDrive(root)) Add(found, dir, "found on a drive");
    }
    return found;
}

void RememberFolder(const std::wstring& dir) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, g_registryKey.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) != ERROR_SUCCESS) return;
    RegSetValueExW(key, L"LastFolder", 0, REG_SZ, reinterpret_cast<const BYTE*>(dir.c_str()), static_cast<DWORD>((dir.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
}

std::wstring RememberedFolder() {
    std::wstring folder = RegistryString(HKEY_CURRENT_USER, g_registryKey.c_str(), L"LastFolder");
    if (folder.empty() && g_registryKey == L"Software\\LSAddonManager")   // remembered before 0.9.1, under the old name
        folder = RegistryString(HKEY_CURRENT_USER, L"Software\\EchoAddonManager", L"LastFolder");
    return folder;
}

void UseRegistryKeyForTest(const wchar_t* subkey) { g_registryKey = subkey ? subkey : L"Software\\LSAddonManager"; }

} // namespace setup
