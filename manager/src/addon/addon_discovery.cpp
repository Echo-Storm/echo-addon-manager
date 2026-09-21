#include "addon_discovery.h"
#include "addon_manifest.h"
#include "../log/logger.h"
#include <algorithm>
#include <cwctype>
#include <system_error>
#include <windows.h>

namespace fs = std::filesystem;

namespace lsproxy {

std::string WideToUtf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0, nullptr, nullptr);
    std::string out((size_t)bytes, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), out.data(), bytes, nullptr, nullptr);
    return out;
}

namespace {

bool HasExtension(const fs::path& p, const wchar_t* wanted) {
    std::wstring ext = p.extension().wstring();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](wchar_t c) { return (wchar_t)std::towlower(c); });
    return ext == wanted;
}

bool IsFile(const fs::path& p) {
    std::error_code ec;
    return fs::is_regular_file(p, ec);
}

// The first file in `folder` with the given extension, in the order the file system lists them. Empty when there is none.
fs::path FirstWithExtension(const fs::path& folder, const wchar_t* ext) {
    std::error_code ec;
    for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec))
        if (it->is_regular_file(ec) && HasExtension(it->path(), ext)) return it->path();
    return {};
}

fs::path PickDll(const fs::path& folder, const AddonManifest& manifest) {
    if (!manifest.dll.empty()) {
        const fs::path named = folder / manifest.dll;
        if (IsFile(named)) return named;
    }
    const fs::path byFolderName = folder / (folder.filename().wstring() + L".dll");
    if (IsFile(byFolderName)) return byFolderName;
    return FirstWithExtension(folder, L".dll");
}

fs::path PickIcon(const fs::path& folder, const AddonManifest& manifest) {
    if (!manifest.icon.empty()) {
        const fs::path named = folder / manifest.icon;
        if (IsFile(named)) return named;
    }
    for (const wchar_t* name : { L"icon.png", L"icon.jpg", L"icon.jpeg", L"icon.bmp" }) {
        const fs::path guess = folder / name;
        if (IsFile(guess)) return guess;
    }
    return {};
}

} // namespace

bool DiscoverAddon(const fs::path& folder, AddonInfo& info) {
    info.folderName = folder.filename().wstring();
    info.id = WideToUtf8(info.folderName);

    const fs::path manifestFile = folder / "addon.json";
    if (IsFile(manifestFile)) {
        std::string problem;
        if (!ReadManifest(manifestFile, info.manifest, &problem))
            LOG_WARN("AddonManager", "Ignoring addon.json of '%s': %s", info.id.c_str(), problem.c_str());
    }

    const fs::path dll = PickDll(folder, info.manifest);
    if (dll.empty()) return false;
    info.dllPath = dll.wstring();

    const fs::path ini = FirstWithExtension(folder, L".ini");
    if (!ini.empty()) info.configPath = ini.wstring();

    const fs::path icon = PickIcon(folder, info.manifest);
    if (!icon.empty()) info.iconPath = icon.wstring();
    return true;
}

} // namespace lsproxy
