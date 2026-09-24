# Notice and credits

Echo Addon Manager began as [LosslessProxy](https://github.com/FrankBarretta/LosslessProxy) by **FrankBarretta** (MIT, 2025).
That project's proxy DLL and its DirectX 11 and shader hooks, and the ReShade and Windowed features (they began as addons), are the foundation of this one, and
we are grateful for them. At the start of this repository about half of the manager's source lines were still that code (48% by
`git blame`, before the history was started fresh); after the addon handling, safety checks, settings file, event system, host interface and
the window code were rewritten, just under a third is (29.4% by `tools/measure_original_share.py`, which compares against the original's lines; that counts the ReShade and Windowed
features, now in `manager/src/features`, as the original's, since they began as its addons). The window, tabs, Performance, backup, install and remove, tray, shared look and the live status and metrics interface are
new. The original copyright and licence are in [LICENSE](LICENSE), which must stay with every copy, however much of it remains.

## What is in this repository, and under what terms

| Part | Author | Licence |
|------|--------|---------|
| `manager/` | Echo-Storm, on FrankBarretta's LosslessProxy | MIT, [LICENSE](LICENSE) |
| `manager/src/features` (ReShade passthrough, Windowed mode) | FrankBarretta's addons, reworked and built in by Echo-Storm | MIT, [LICENSE](LICENSE) |
| `addons/DLSS5NR01` | Echo-Storm; it began as andreiday's DLSS 5 plugin for LosslessProxy, with thanks | MIT, its `LICENSE` |
| `tools/` | Echo-Storm | MIT |

## Third-party code that is built in or fetched at build time

| Component | Licence | Where |
|-----------|---------|-------|
| [Dear ImGui](https://github.com/ocornut/imgui) (a pinned commit) | MIT | fetched by CMake, compiled into the manager and each addon that draws a panel |
| [nlohmann/json](https://github.com/nlohmann/json) | MIT | `manager/third_party/nlohmann` |
| [stb_image](https://github.com/nothings/stb) | public domain / MIT | `manager/third_party/stb_image.h` |
| [MinHook](https://github.com/TsudaKageyu/minhook) | BSD-2-Clause | fetched by CMake for the manager (its code hooks and the Windowed feature) |
| Icon shapes | drawn in the manner of the [Lucide](https://lucide.dev) set (ISC) | `manager/sdk/include/eam/icons.h` |

## NVIDIA software in the release (not MIT)

The Neural Rendering addon uses **NVIDIA DLSS** technology. NVIDIA, the NVIDIA logo and DLSS are trademarks of NVIDIA Corporation; this
project is not affiliated with or endorsed by NVIDIA. Parts of NVIDIA's DLSS SDK, taken from NVIDIA's public repository
([NVIDIA/DLSS](https://github.com/NVIDIA/DLSS), release 310.9.1) by `tools/fetch_ngx_sdk.ps1`, are in the released files:

| What | Where in the release | Terms |
|------|----------------------|-------|
| NVIDIA's NGX SDK library (`nvsdk_ngx_s.lib`), as object code | linked into `addons/DLSS5NR01/DLSS5NR01.dll` and `nr_selftest.exe` | NVIDIA RTX SDKs licence |
| NVIDIA's DLSS runtime (`nvngx_dlss.dll`), unmodified | `addons/DLSS5NR01/nvngx_dlss.dll`, once the DLAA model ships | NVIDIA RTX SDKs licence |
| NVIDIA's licence text | `addons/DLSS5NR01/NVIDIA-LICENSE.txt` | |

These are NVIDIA's, under NVIDIA's own licence, which comes with them. This project's MIT licence does not cover them and does not make them
redistributable on its terms: they may be passed on only as part of this application and under NVIDIA's terms. They are not in this
repository (the SDK folder is ignored by git); a build fetches them from NVIDIA with the script above, which checks each one against a pinned
SHA-256.

## Not part of this repository or the release

- **Lossless Scaling** is its author's product. This project is unofficial and is not affiliated with or endorsed by them.
- The **DLSS NR model** (`nvngx_dlssnr.dll`, needed to run DLSS 5 Neural Rendering) belongs to NVIDIA and is not a file NVIDIA offers for
  redistribution. It is never included, downloaded or linked here, and this project does not say where to find it: the person supplies their own.
