# Sample addon

The smallest addon that does what addons usually do. Copy this folder and build on it. It is tested against the real manager (`lsproxy_sampletest`), so it stays working.

What it shows, all in [`SampleAddon.cpp`](SampleAddon.cpp) (about 100 lines, commented):

- the **exports** the manager looks for, and the optional ones for a name, a version and a settings panel;
- reading and writing **settings** (`GetConfig`, `SetConfig`, `SaveConfig`), which the manager keeps in `addons\config.json`;
- a **settings panel** in the manager's own look, using `lsp_widgets.h`: section labels, a slider with a default (double-click resets it), buttons with icons;
- a live **status** line (`SetStatus`) and a number for the Performance tab (`PublishMetric`).

It needs no GPU access, starts no threads and asks for no restart.

## Build it

You need Visual Studio 2022 (Desktop C++ workload) and CMake 3.20 or newer. Only the SDK headers in `manager/sdk/include` are used.

```powershell
cmake -S examples/SampleAddon -B examples/SampleAddon/build -G "Visual Studio 17 2022" -A x64
cmake --build examples/SampleAddon/build --config Release
```

Then put two files in a folder called `SampleAddon` under the Lossless Scaling folder's `addons`:

```
<Lossless Scaling>\addons\SampleAddon\addon.json
<Lossless Scaling>\addons\SampleAddon\SampleAddon.dll     (from examples\SampleAddon\build\Release)
```

(or use **Install addon** on the manager's Addons tab and pick that folder). Start Lossless Scaling, open the manager, switch **Sample Addon** on and open its settings.
New addons arrive switched off.

## Make your own

1. Copy the folder and rename it, the DLL (`OUTPUT_NAME` in `CMakeLists.txt`), `kId` in the code and `addon.json`. The folder name under `addons\` is the addon's id: use the same text everywhere.
2. Change what the panel draws in `AddonRenderSettings`, and what you keep in `Settings`, `Load` and `Save`.
3. Read [docs/addon-authors.md](../../docs/addon-authors.md) for the rules that are easy to trip over (one shared Dear ImGui context, a static CRT, not blocking the render thread) and
   [docs/api-compatibility.md](../../docs/api-compatibility.md) for what will stay stable.
