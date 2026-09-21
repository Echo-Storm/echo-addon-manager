#pragma once
#include "addon_info.h"
#include <string>

namespace lsproxy {

// Optional integrity checking of addon DLLs against addons\trusted_addons.json, a file of the form
//   { "<addon id>": ["<SHA-256 of its DLL, in hex>", ...], ... }
// The verdict is only used when Settings > Security is above "allow everything".
class AddonSecurity {
public:
    // The SHA-256 of a file as 64 lowercase hex digits, or "" if the file cannot be read.
    static std::string ComputeSHA256(const std::wstring& filePath);

    // Trusted when the DLL's hash is one of the addon's listed hashes, Tampered when the addon is listed but this DLL is not one of them,
    // Unknown when there is no list, the addon is not on it, or the DLL cannot be read.
    static SecurityVerdict VerifyDll(const std::wstring& dllPath, const std::string& addonId);

    // Reads trusted_addons.json from `basePath` (the addons folder) and replaces whatever list was loaded before. No file means an empty
    // list; a file that cannot be parsed leaves the previous list in place.
    static void LoadTrustedHashes(const std::wstring& basePath);
};

} // namespace lsproxy
