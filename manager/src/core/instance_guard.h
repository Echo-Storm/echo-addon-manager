#pragma once
// One manager per Lossless Scaling folder. Lossless Scaling allows one copy of itself: a second start loads Lossless.dll (so this manager),
// hands over to the copy that is already running and exits a moment later. That second copy must not start a second window, tray icon,
// hotkey, set of addons or settings file writer while it lives. The first manager to start in a folder holds a named mutex for it; any later
// one sees it taken and only forwards to Lossless_original.dll.
#include <string>

namespace eam {
namespace instance {

// True when this process now owns the folder (it is the first). The same folder written another way (case, a trailing separator) is the same folder.
bool Claim(const std::wstring& folder);
// Lets go of the folder (at shutdown, and in tests).
void Release();
// The name of the mutex for a folder (for tests and the log).
std::wstring MutexName(const std::wstring& folder);

} // namespace instance
} // namespace eam
