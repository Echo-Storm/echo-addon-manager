// Which addon this build is. The same sources make three addons (CMake builds them all):
//   DLSS5NR01  DLSS 5 Neural Rendering, which runs the person's own nvngx_dlssnr.dll;
//   DLSS4DLAA  DLSS 4 Upscaler, NVIDIA DLSS Super Resolution in place of Lossless Scaling's NIS pass, with NVIDIA's runtime (NR_PRODUCT_DLAA);
//   FSR3UPSC   FSR 3 Upscaler, AMD FidelityFX Super Resolution 3.1 in the same place, with AMD's runtime, on any graphics card (NR_PRODUCT_FSR).
// Each has its own folder, settings and log. The two upscalers take the same pass, so they cannot run together (the FSR addon's addon.json
// names the DLSS one under "conflicts"); either works beside Neural Rendering.
#pragma once
#include <cstring>

namespace nr {

#if defined(NR_PRODUCT_FSR)
inline constexpr bool kScalerAddon = true;      // an upscaler in place of NIS (scaler11.h), not Neural Rendering
inline constexpr bool kFsrScaler = true;        // ... with AMD FSR 3 rather than NVIDIA DLSS
inline constexpr const char* kAddonId = "FSR3UPSC";
inline constexpr const wchar_t* kAddonIdW = L"FSR3UPSC";
inline constexpr const char* kProductName = "FSR 3 Upscaler";
inline constexpr const char* kUpscalerName = "FSR 3";
#elif defined(NR_PRODUCT_DLAA)
inline constexpr bool kScalerAddon = true;
inline constexpr bool kFsrScaler = false;
inline constexpr const char* kAddonId = "DLSS4DLAA";
inline constexpr const wchar_t* kAddonIdW = L"DLSS4DLAA";
inline constexpr const char* kProductName = "DLSS 4 Upscaler";
inline constexpr const char* kUpscalerName = "DLSS";
// DLSS Super Resolution in place of Lossless Scaling's NIS pass (scaler11.h). It began as DLAA on the captured frame, which in World of
// Warcraft at 4K changed nothing visible (no camera jitter, no depth), so it now does what DLSS is made for: the upscaling (and DLAA at 1:1).
// Its id is still DLSS4DLAA, so its settings stay where they are.
#else
inline constexpr bool kScalerAddon = false;
inline constexpr bool kFsrScaler = false;
inline constexpr const char* kAddonId = "DLSS5NR01";
inline constexpr const wchar_t* kAddonIdW = L"DLSS5NR01";
inline constexpr const char* kProductName = "DLSS 5 Neural Rendering";
inline constexpr const char* kUpscalerName = "DLSS";
#endif

// Another addon of the family, by id: its name, for "... is on" messages.
inline const char* ProductNameOf(const char* id) {
    if (!id || !*id) return "";
    if (!strcmp(id, "DLSS5NR01")) return "DLSS 5 Neural Rendering";
    if (!strcmp(id, "DLSS4DLAA")) return "DLSS 4 Upscaler";
    if (!strcmp(id, "FSR3UPSC")) return "FSR 3 Upscaler";
    return id;
}

} // namespace nr
