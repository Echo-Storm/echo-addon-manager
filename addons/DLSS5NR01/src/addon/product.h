// Which addon this build is. The same sources make two addons (CMake builds both):
//   DLSS5NR01  DLSS 5 Neural Rendering, which runs the person's own nvngx_dlssnr.dll;
//   DLSS4DLAA  DLSS 4 DLAA, which runs NVIDIA's DLSS runtime that ships with it (NR_PRODUCT_DLAA defined).
// Each has its own folder, settings and log, and only one works on Lossless Scaling's frames at a time (see OwnsFrames in runtime.cpp).
#pragma once
#include <cstring>

namespace nr {

#ifdef NR_PRODUCT_DLAA
inline constexpr bool kDlaaAddon = true;
inline constexpr const char* kAddonId = "DLSS4DLAA";
inline constexpr const wchar_t* kAddonIdW = L"DLSS4DLAA";
inline constexpr const char* kProductName = "DLSS 4 DLAA (WIP)";
// Work in progress: on a captured frame DLSS gets no camera jitter and no depth, and in World of Warcraft at 4K (2026-09-24) it changed
// nothing visible while costing 3 to 4 ms of GPU time a frame. It stays switched off (its Enable box greyed out) until a way around that is
// found; the test host alone can still run it (config key wipRun=1).
inline constexpr bool kWip = true;
#else
inline constexpr bool kDlaaAddon = false;
inline constexpr bool kWip = false;
inline constexpr const char* kAddonId = "DLSS5NR01";
inline constexpr const wchar_t* kAddonIdW = L"DLSS5NR01";
inline constexpr const char* kProductName = "DLSS 5 Neural Rendering";
#endif

// The other addon of the pair, by id: its name, for "... is on" messages.
inline const char* ProductNameOf(const char* id) {
    if (!id || !*id) return "";
    if (!strcmp(id, "DLSS5NR01")) return "DLSS 5 Neural Rendering";
    if (!strcmp(id, "DLSS4DLAA")) return "DLSS 4 DLAA (WIP)";
    return id;
}

} // namespace nr
