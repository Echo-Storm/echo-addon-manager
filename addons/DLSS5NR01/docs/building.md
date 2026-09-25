# Building

## Prerequisites

- Visual Studio 2022 Build Tools with the C++ workload (MSVC v143, Windows SDK).
- CMake 3.20 or newer.
- The NVIDIA DLSS SDK headers and static library in `external/ngx` (see
  `external/ngx/README.md` for the exact files). The SDK's license does not allow redistributing
  them, so they are not in the repository. CMake stops with a clear message if they are missing.
  `powershell -File tools\fetch_ngx_sdk.ps1 -AcceptNvidiaLicense` fetches exactly these files from
  NVIDIA's own public repository (pinned to one commit and checked by SHA-256); it runs only when
  you say you accept NVIDIA's licence.
- Internet access at configure time: CMake fetches Dear ImGui (the docking branch commit that
  matches the layout compiled into the manager).
- For the FSR 3 Upscaler: AMD's FidelityFX runtime in `external/ffx/bin`.
  `powershell -File tools\fetch_ffx_sdk.ps1` fetches `amd_fidelityfx_dx12.dll` from AMD's repository (FidelityFX SDK v1.1.4, pinned,
  checked by SHA-256 and AMD's signature); the build copies it into `build\Release\fsr`. Without it the FSR addon builds but cannot run.
  The FidelityFX API headers (MIT) are in `third_party/ffx`.

## Build

```
build.bat
```

or by hand:

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Outputs in `build\Release`:

| File | What |
|---|---|
| `DLSS5NR01.dll` | the LS Addon Manager addon |
| `nvngx.dll_dlss5nr01.dll` | the forwarder, the only module that calls the DLSSNR snippet |
| `nr_hosttest.exe` | offline test host for the addon |

Install by copying the two DLLs and `addon.json` into
`<Lossless Scaling>\addons\DLSS5NR01\` while Lossless Scaling is closed.

## Offline test host

```
build\Release\nr_hosttest.exe build\Release\DLSS5NR01.dll - <path to nvngx_dlssnr.dll> [key=value ...]
```

Loads the addon with a fake `IHost`, renders headless ImGui frames, issues a synthetic LSFG
dispatch pattern (pyramid, flow, two interpolation composes) and presents through a real flip
swap chain on the display GPU in LSFG's X3 order. It prints the addon's log and checks:

- `TAP READ-ONLY`: the frame handed to LSFG is unchanged after the tap.
- `COMPOSE APPLIED`: the back buffer read two presents later differs from the input by the delta.
- `TAP FOLLOWED`: the tap re-learns after a `ResizeBuffers`.

`key=value` pairs override addon settings (for example `workingScale=0.5`). The second argument
is ignored and only kept for old scripts. It writes `present_gen.bmp` beside the exe.

For the upscalers (`build\Release\DLSS4DLAA.dll` or `FSR3UPSC.dll`), `nis=1` adds a fake NIS pass that paints its output magenta, which
the upscaler must replace (`[check-nis] ... DLSS REPLACED NIS`); `nisbgra=1` and `nisnoflow=1` give frame generation off (a BGRA8 frame,
no flow passes); `nisW=`, `nisH=`, `nisScale=` set the frame's size and the scale (1 for DLAA); `nismove=1` slides an aliased picture
5.37 x 2.21 px a frame and measures the output against the ideal picture (`[check-move]`). `tools\run_hosttest_matrix.py` runs the
scenarios (`scaler*`, `dlaa_4k_move`, `fsr_*`).

## Harness (retired)

`nr_harness`, the research tool that ran the model on a still image, was retired in 0.7.9: the self-test (`nr_selftest.exe`) and the test host cover what it checked.
It is in the history at tag v0.7.8 for new knob measurements ([dlssnr-knobs.md](dlssnr-knobs.md) lists what it found).

## Release package

```
powershell -ExecutionPolicy Bypass -File ..\..\tools\package.ps1
```

Builds Release, then assembles `dist\DLSS5NR01-v<version>.zip` with the addon folder, the
harness, the install guide and the licenses. The DLSSNR snippet and the NVIDIA SDK are never
included.

## Layout

```
src/addon/       the addon: host glue and panel (addon.cpp), dispatch hook,
                 frame tap, bridge, present hook, compose
src/engine/      the D3D12 sidecar: NGX core, feature 18, model-side passes and shaders;
                 the upscalers' engine (sr_engine) and motion estimate (flow_estimator)
products/        addon.json of the addons built from these sources besides Neural Rendering (DLSS4DLAA, FSR3UPSC)
third_party/ffx/ AMD's FidelityFX API headers (MIT)
src/forwarder/   nvngx.dll_dlss5nr01.dll and its C API
src/harness/     standalone measurement harness
tools/           offline test host, release packaging
external/        NVIDIA SDK and AMD runtime drop point (ignored by git); the addon SDK headers are ../../manager/sdk
docs/            this folder
```
