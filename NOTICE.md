# Notice and credits

Echo Addon Manager began as [LosslessProxy](https://github.com/FrankBarretta/LosslessProxy) by **FrankBarretta** (MIT, 2025).
That project's proxy DLL and its DirectX 11 and shader hooks, and the ReShade and Windowed features (they began as addons), are the foundation of this one, and
we are grateful for them. At the start of this repository about half of the manager's source lines were still that code (48% by
`git blame`, before the history was started fresh); after the addon handling, safety checks, settings file, event system, host interface and
part of the window code were rewritten, about a third is (32.7% by `tools/measure_original_share.py`, which compares against the original's lines; that counts the ReShade and Windowed
features, now in `manager/src/features`, as the original's, since they began as its addons). The window, tabs, Performance, backup, install and remove, tray, shared look and the live status and metrics interface are
new. The original copyright and licence are in [LICENSE](LICENSE), which must stay with every copy, however much of it remains.

## What is in this repository, and under what terms

| Part | Author | Licence |
|------|--------|---------|
| `manager/` | Echo-Storm, on FrankBarretta's LosslessProxy | MIT, [LICENSE](LICENSE) |
| `manager/src/features` (ReShade passthrough, Windowed mode) | FrankBarretta's addons, reworked and built in by Echo-Storm | MIT, [LICENSE](LICENSE) |
| `addons/LSP-NeuralRender` | andreiday, extended by Echo-Storm | MIT, its `LICENSE` |
| `tools/` | Echo-Storm | MIT |

## Third-party code that is built in or fetched at build time

| Component | Licence | Where |
|-----------|---------|-------|
| [Dear ImGui](https://github.com/ocornut/imgui) (a pinned commit) | MIT | fetched by CMake, compiled into the manager and each addon that draws a panel |
| [nlohmann/json](https://github.com/nlohmann/json) | MIT | `manager/third_party/nlohmann` |
| [stb_image](https://github.com/nothings/stb) | public domain / MIT | `manager/third_party/stb_image.h` |
| [MinHook](https://github.com/TsudaKageyu/minhook) | BSD-2-Clause | fetched by CMake for the manager's Windowed feature and for the Neural Rendering addon |
| Icon shapes | drawn in the manner of the [Lucide](https://lucide.dev) set (ISC) | `manager/sdk/include/lsproxy/lsp_icons.h` |

## Not part of this repository

- **Lossless Scaling** is its author's product. This project is unofficial and is not affiliated with or endorsed by them.
- The **NVIDIA DLSS SDK** (needed only to build the Neural Rendering addon) and the **DLSSNR snippet** (`nvngx_dlssnr.dll`, needed
  to run it) belong to NVIDIA. Neither is included, linked or distributed here, and this project does not say where to find them.
