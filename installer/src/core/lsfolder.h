#pragma once
// Where is Lossless Scaling? Several places are tried; none of them is authoritative, so the person can always pick a folder by hand.
#include <string>
#include <vector>

namespace setup {

struct Candidate {
    std::wstring dir;
    std::string how;               // "running", "Steam library", "default location", "used last time"
};

// A folder that holds LosslessScaling.exe and a Lossless.dll (ours or the original) or a Lossless_original.dll.
bool LooksLikeLosslessScaling(const std::wstring& dir);

// The library folders named in Steam's libraryfolders.vdf ("path" entries, with the doubled backslashes undone).
std::vector<std::wstring> ParseSteamLibraries(const std::string& vdfText);

// Folders on one drive that look like Lossless Scaling: a folder whose name starts with "Lossless Scaling" or "LosslessScaling", directly under the drive's root or under
// a short list of usual parents (Utilities, Games, Apps, Program Files, Steam library folders...). One level only, so it is quick and never crawls a disk.
std::vector<std::wstring> ScanDrive(const std::wstring& driveRoot);

// Folders on one drive that look like Lossless Scaling: a folder whose name starts with "Lossless Scaling" or "LosslessScaling", directly under the drive's root or under
// a short list of usual parents (Utilities, Games, Apps, Program Files, Steam library folders...). One level only, so it is quick and never crawls a disk.
std::vector<std::wstring> ScanDrive(const std::wstring& driveRoot);

// Every place worth offering, best first, each checked with LooksLikeLosslessScaling and listed once.
std::vector<Candidate> FindCandidates();

// The folder used last time (kept in HKCU\Software\LSAddonManager).
void RememberFolder(const std::wstring& dir);
std::wstring RememberedFolder();
void UseRegistryKeyForTest(const wchar_t* subkey);   // tests keep their own key, so they never touch the person's remembered folder; null restores the normal one

} // namespace setup
