#pragma once
#include <filesystem>
#include <string>

namespace eam {

struct PlaceResult {
    bool ok = false;
    std::string message;          // for the user
    std::filesystem::path dest;   // the new addon folder (inside addonsDir) when ok
};

// Puts an addon from `source` (a folder, a .zip or a single .dll) into a new folder inside `addonsDir`.
// Never overwrites an existing folder, never touches `source`. A zip is unpacked with Windows' own tar.exe into a temporary
// ".install-*" folder inside addonsDir, which is removed again. The folder must contain an addon.json or a .dll.
PlaceResult PlaceAddon(const std::filesystem::path& source, const std::filesystem::path& addonsDir);

struct RemoveResult {
    bool ok = false;
    std::string message;              // for the user
    std::filesystem::path movedTo;    // where the folder went when ok
};

// "Removes" an addon by moving its folder into <addonsDir>\.removed\<name>-<timestamp>. Nothing is erased, so a mistake is one
// move back. Refuses a folder that is not directly inside addonsDir. Fails (with a message) if the folder is in use, for example
// while its DLL is still loaded.
RemoveResult MoveAddonToRemoved(const std::filesystem::path& addonFolder, const std::filesystem::path& addonsDir);

} // namespace eam
