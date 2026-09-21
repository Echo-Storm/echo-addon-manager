#pragma once
#include "addon_info.h"
#include <filesystem>
#include <string>

namespace lsproxy {

// UTF-8 text for a wide string (folder names, paths in log lines).
std::string WideToUtf8(const std::wstring& text);

// Looks inside one addon folder and fills in what is on disk: the id, the manifest, the DLL, an .ini and an icon. The DLL is the one the
// manifest names, else <folder>.dll, else the first .dll in the folder. Returns false when there is no DLL, which means it is not an addon.
// The user's switch and the security verdict are not decided here.
bool DiscoverAddon(const std::filesystem::path& folder, AddonInfo& info);

} // namespace lsproxy
