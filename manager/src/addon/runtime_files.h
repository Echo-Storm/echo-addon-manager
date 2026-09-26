#pragma once
// The runtime files the addons use (NVIDIA's DLSS, AMD's FidelityFX, a model file the person provides), as addon.json lists them under
// "runtimes": what each file is, its version, whether it is signed and by whom, and whether it is the one the addon ships with. Read from
// the file on disk without loading it (the version resource, WinVerifyTrust with no network access, a SHA-256), on a thread of its own;
// the window only ever reads the results.
#include "addon_info.h"
#include <cstdint>
#include <string>
#include <vector>

namespace eam {

struct RuntimeFile {
    // from addon.json
    std::string label;            // "FSR", "DLSS", "DLSS 5 model"
    std::string addonId, addonName;
    bool addonOn = false;
    std::wstring path;            // where the file is looked for
    std::string shippedLabel;     // the version to show for the file the addon ships with ("3.1.4"), when its own says little
    // from the file
    enum class Signature { Unknown, Signed, Unsigned, Broken } signature = Signature::Unknown;   // Broken: signed, then changed
    bool exists = false, read = false;   // read: the thread has looked at it (until then only `exists` is known)
    bool loaded = false;                 // loaded in this process now (the addon is running on it)
    bool shipped = false;                // it is the file the addon ships with (its SHA-256 is the one in addon.json)
    bool shippedKnown = false;           // addon.json says which file it ships
    std::string version;                 // "310.9.1", from the file's version resource
    std::string description, company;    // from the version resource
    std::string signer;                  // the certificate's name, when signed
    std::string sha256;
    uint64_t size = 0;

    std::string ShownVersion() const;    // the shipped label for the shipped file, else the file's version
};

// The rows for the addons' runtimes, in the addons' order. lsDir: Lossless Scaling's folder ("{ls}" in a path); config: an addon's setting by
// key (a "config_key" in addon.json names a setting that holds the file's path, such as a model file chosen in the addon).
using ConfigLookup = std::string (*)(const std::string& addonId, const std::string& key);
std::vector<RuntimeFile> RuntimeFiles(const std::vector<AddonInfo>& addons, const std::wstring& lsDir, ConfigLookup config);

// Reads one file now (for the tests and the tools): everything above from `path`.
void InspectRuntimeFile(const std::wstring& path, RuntimeFile& into);

} // namespace eam
