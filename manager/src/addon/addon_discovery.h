#pragma once
#include "addon_info.h"
#include <filesystem>
#include <string>
#include <vector>

namespace eam {

// UTF-8 text for a wide string (folder names, paths in log lines).
std::string WideToUtf8(const std::wstring& text);

// Looks inside one addon folder and fills in what is on disk: the id, the manifest, the DLL, an .ini and an icon. The DLL is the one the
// manifest names, else <folder>.dll, else the first .dll in the folder. Returns false when there is no DLL, which means it is not an addon.
// The user's switch and the security verdict are not decided here.
bool DiscoverAddon(const std::filesystem::path& folder, AddonInfo& info);

// A vector icon (icon.svg): the d="..." of each of its paths, in order, and the width of its viewBox (24 without one). False when it has
// no paths (or is too big to be an icon).
bool ReadSvgIcon(const std::filesystem::path& file, std::vector<std::string>& paths, float& view);

} // namespace eam
