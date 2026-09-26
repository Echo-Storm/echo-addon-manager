#pragma once
// The runtime files the addons use (NVIDIA's DLSS, AMD's FidelityFX, a model file the person provides), as addon.json lists them under
// "runtimes": what each file is, its version, whether it is signed and by whom, whether it is loaded now, and whether it is the one the
// addon ships with. Read from the file on disk without loading it (the version resource, WinVerifyTrust with no network access, a SHA-256),
// on a thread of its own; the window only ever reads the results.
//
// Another file can be chosen for each: it is copied into the addon's folder (runtimes\<name>\<first 8 of its SHA-256>\<the file name the
// addon loads>), and the addon's setting named by "config_key" is set to it (empty: the shipped or default file). The addon follows that
// setting while it runs (a new engine on the new file); what is loaded shows in the list.
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
    std::string configKey;        // the addon setting that holds a chosen file's path
    std::vector<std::string> exportsNeeded;   // functions a file must offer to be taken for this runtime
    std::wstring addonDir;
    std::wstring defaultPath;     // the shipped (or default) file
    std::wstring path;            // the file in use: the chosen one, else the default
    bool usingDefault = true;
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
    std::string FileName() const;        // the name the addon loads it by ("amd_fidelityfx_dx12.dll")
};

// The rows for the addons' runtimes, in the addons' order. lsDir: Lossless Scaling's folder ("{ls}" in a path).
std::vector<RuntimeFile> RuntimeFiles(const std::vector<AddonInfo>& addons, const std::wstring& lsDir);

// What is known of any file (the same cache and thread): for the files in a runtime's library.
RuntimeFile DescribeRuntimeFile(const std::wstring& path, const RuntimeFile& like);

// The files added for this runtime, newest first.
std::vector<std::wstring> RuntimeLibrary(const RuntimeFile& slot);
// A library file's own name: the first line of the ABOUT.txt beside it ("FSR 4.1.1b INT8 with the RDNA 2 fix"), or "" when there is none.
std::string RuntimeFileTitle(const std::wstring& path);
// Copies `source` into the runtime's library, after checking it is a DLL with the functions the runtime needs. `added`: where it went.
bool AddRuntimeFile(const RuntimeFile& slot, const std::wstring& source, std::wstring& added, std::string& error);
// Makes the addon use `path` (empty: the default file), saved in its settings.
void UseRuntimeFile(const RuntimeFile& slot, const std::wstring& path);
// Moves a library file out (to runtimes\.removed: nothing is erased); the default takes over when it was the one in use.
bool RemoveRuntimeFile(const RuntimeFile& slot, const std::wstring& path, std::string& error);

// Reads one file now (for the tests and the tools): everything above from `path`.
void InspectRuntimeFile(const std::wstring& path, RuntimeFile& into);
// The names a DLL exports, read from the file (it is not loaded). False when it is not a 64-bit DLL.
bool ReadDllExports(const std::wstring& path, std::vector<std::string>& names);

} // namespace eam
