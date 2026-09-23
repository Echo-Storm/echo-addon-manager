#pragma once
#include "addon_info.h"
#include <filesystem>
#include <string>

namespace eam {

// Reads an addon.json into `out`. A field of the wrong type is skipped rather than failing the whole file. Returns false, and says why in
// `problem`, when the file cannot be read or is not a JSON object; `out` is then left as it was and `parsed` stays false.
bool ReadManifest(const std::filesystem::path& file, AddonManifest& out, std::string* problem = nullptr);

// "1.2.3" or "1.2" as the addon API number the host reports (0x00MMmmpp). Anything after the digits, like "-beta.1", is ignored.
// Text that does not start with major.minor gives 0, which never blocks an addon.
uint32_t ParseApiVersion(const std::string& text);

} // namespace eam
