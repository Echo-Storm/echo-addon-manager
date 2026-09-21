# Notice and credits

Echo Addon Manager began as [LosslessProxy](https://github.com/FrankBarretta/LosslessProxy) by **FrankBarretta** (MIT, 2025).
That project's proxy DLL, its DirectX 11 and shader hooks, its addon loader and event system, and the ReShade and Windowed
addons are the foundation of this one, and we are grateful for them. As of 0.1.0 about half of the manager's source lines are
still that code (measured with `git blame` before this repository's history was started fresh); the window, tabs, Performance,
backup, install and remove, tray, shared look and the live status and metrics interface are new. The original copyright and
licence are in [LICENSE](LICENSE), which must stay with every copy.

## What is in this repository, and under what terms

| Part | Author | Licence |
|------|--------|---------|
| `manager/` | Echo-Storm, on FrankBarretta's LosslessProxy | MIT, [LICENSE](LICENSE) |
| `addons/LSP-ReShade`, `addons/LSP-Windowed` | FrankBarretta, reworked by Echo-Storm | MIT, each folder's `LICENSE` |
| `addons/LSP-NeuralRender` | andreiday, extended by Echo-Storm | MIT, its `LICENSE` |
| `tools/` | Echo-Storm | MIT |

## Third-party code that is built in or fetched at build time

| Component | Licence | Where |
|-----------|---------|-------|
| [Dear ImGui](https://github.com/ocornut/imgui) (a pinned commit) | MIT | fetched by CMake, compiled into the manager and each addon that draws a panel |
| [nlohmann/json](https://github.com/nlohmann/json) | MIT | `manager/third_party/nlohmann` |
| [stb_image](https://github.com/nothings/stb) | public domain / MIT | `manager/third_party/stb_image.h` |
| [MinHook](https://github.com/TsudaKageyu/minhook) | BSD-2-Clause | fetched by CMake for the Windowed and Neural Rendering addons |
| Icon shapes | drawn in the manner of the [Lucide](https://lucide.dev) set (ISC) | `manager/sdk/include/lsproxy/lsp_icons.h` |

## Not part of this repository

- **Lossless Scaling** is its author's product. This project is unofficial and is not affiliated with or endorsed by them.
- The **NVIDIA DLSS SDK** (needed only to build the Neural Rendering addon) and the **DLSSNR snippet** (`nvngx_dlssnr.dll`, needed
  to run it) belong to NVIDIA. Neither is included, linked or distributed here, and this project does not say where to find them.
