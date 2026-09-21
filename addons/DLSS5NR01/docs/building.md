# Building

## Prerequisites

- Visual Studio 2022 Build Tools with the C++ workload (MSVC v143, Windows SDK).
- CMake 3.20 or newer.
- The NVIDIA DLSS SDK headers and static library in `external/ngx` (see
  `external/ngx/README.md` for the exact files). The SDK's license does not allow redistributing
  them, so they are not in the repository. CMake stops with a clear message if they are missing.
- Internet access at configure time: CMake fetches Dear ImGui (the docking branch commit that
  matches the layout compiled into the manager) and MinHook.

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
| `DLSS5NR01.dll` | the Echo Addon Manager addon |
| `nvngx.dll_lspnr.dll` | the forwarder, the only module that calls the DLSSNR snippet |
| `lspnr_hosttest.exe` | offline test host for the addon |
| `lspnr_harness.exe` | standalone model harness |

Install by copying the two DLLs and `addon.json` into
`<Lossless Scaling>\addons\DLSS5NR01\` while Lossless Scaling is closed.

## Offline test host

```
build\Release\lspnr_hosttest.exe build\Release\DLSS5NR01.dll - <path to nvngx_dlssnr.dll> [key=value ...]
```

Loads the addon with a fake `IHost`, renders headless ImGui frames, issues a synthetic LSFG
dispatch pattern (pyramid, flow, two interpolation composes) and presents through a real flip
swap chain on the display GPU in LSFG's X3 order. It prints the addon's log and checks:

- `TAP READ-ONLY`: the frame handed to LSFG is unchanged after the tap.
- `COMPOSE APPLIED`: the back buffer read two presents later differs from the input by the delta.
- `TAP FOLLOWED`: the tap re-learns after a `ResizeBuffers`.

`key=value` pairs override addon settings (for example `workingScale=0.5`). The second argument
is ignored and only kept for old scripts. It writes `present_gen.bmp` beside the exe.

## Harness

```
build\Release\lspnr_harness.exe image.png [--only 1440p] [--iters N] [--style S] [--intensity I] ...
```

Runs the model on an image without Lossless Scaling, GPU-timed, and writes `in_<size>.png` /
`out_<size>.png` beside the exe. The full option list and what each knob measurably does are in
[dlssnr-knobs.md](dlssnr-knobs.md). `--trace` logs every parameter the snippet reads; `--perfscan`
queries the snippet's scaling-ratio callback for every performance mode.

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
src/engine/      the D3D12 sidecar: NGX core, feature 18, model-side passes and shaders
src/forwarder/   nvngx.dll_lspnr.dll and its C API
src/harness/     standalone measurement harness
tools/           offline test host, release packaging
external/        NVIDIA SDK drop point (ignored by git); the addon SDK headers are ../../manager/sdk
docs/            this folder
```
