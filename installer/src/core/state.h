#pragma once
// What state is a Lossless Scaling folder in? Decided from the two DLLs' version resources, never by loading them.
#include <string>

namespace setup {

enum class DllKind {
    Missing,
    Original,      // Lossless Scaling's own (product "Lossless Scaling")
    Ours,          // ours (product "Addon Manager for Lossless Scaling", or "Echo Addon Manager" before 0.9.1)
    Unknown,       // something else
};

struct DllInfo {
    bool exists = false;
    DllKind kind = DllKind::Missing;
    std::string version;           // its file version, "3.2.2.0" or "0.4.1"
    unsigned long long size = 0;
    bool legacy = false;           // ours, from a release before the version resource existed (up to 0.4.1): its version is unknown
};

DllInfo InspectDll(const std::wstring& path);

enum class Situation {
    NotLosslessScaling,   // no LosslessScaling.exe here
    NotInstalled,         // Lossless.dll is the original and there is no Lossless_original.dll: a plain Lossless Scaling
    Installed,            // ours as Lossless.dll, the original beside it as Lossless_original.dll
    AfterLsUpdate,        // Lossless.dll is an original AND a Lossless_original.dll is there: Lossless Scaling updated itself over ours
    NoOriginal,           // ours as Lossless.dll but no original to forward to: it cannot work, and only Lossless Scaling's own files can fix that
    BothOurs,             // both are ours: an earlier mix-up
    Unrecognised,         // Lossless.dll is neither: some other build
};

struct State {
    std::wstring dir;
    Situation situation = Situation::NotLosslessScaling;
    DllInfo lossless;             // Lossless.dll
    DllInfo original;             // Lossless_original.dll
    bool running = false;         // a LosslessScaling.exe from this folder is running (it holds the DLLs open)
    std::string lsVersion;        // Lossless Scaling's version, from whichever DLL is the original
    std::string installedVersion; // ours, when it is installed
};

State Inspect(const std::wstring& dir);
bool LosslessScalingRunning(const std::wstring& dir);   // a process named LosslessScaling.exe whose file is in `dir`

// What to offer, in words: the headline, the one action that fits, and whether it can be done now.
enum class Action { None, Install, Update, Repair, Reinstall };
struct Advice {
    std::string headline;         // "LS Addon Manager is not installed."
    std::string detail;           // a sentence more, or empty
    Action action = Action::None;
    std::string actionLabel;      // "Install", "Update to 0.5.0", ...
    bool canUninstall = false;
    bool blocked = false;         // the action cannot be done now (Lossless Scaling is running, or the folder is broken)
    std::string blockedReason;
};
// `payloadVersion` is the version this installer carries ("0.4.1"); empty when unknown.
Advice Advise(const State& state, const std::string& payloadVersion);

// -1, 0, 1 for "a.b.c[.d]" style versions compared as numbers; missing parts count as 0.
int CompareVersions(const std::string& a, const std::string& b);

} // namespace setup
