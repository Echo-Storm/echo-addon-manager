<p align="center"><img src="docs/images/banner.svg" alt="Echo Addon Manager: the addon manager for Lossless Scaling" width="100%"></p>

<p align="center"><b>The addon manager for Lossless Scaling.</b><br>Install, switch on and tune addons from one window, and watch your frame rate and GPU while you play.</p>

Echo Addon Manager loads alongside [Lossless Scaling](https://store.steampowered.com/app/993090/Lossless_Scaling/) and gives it a proper addon system: one window
to install, switch on and tune addons, a live view of frame time and GPU load while a game runs, and one-click backup of every setting. It ships with the
**DLSS 5 Neural Rendering** addon and two built-in features, **ReShade input passthrough** and **Windowed mode**, and it is free and MIT-licensed. It is an unofficial project, not
affiliated with the Lossless Scaling developers; read the [disclaimer](DISCLAIMER.md) before you install it.

<table>
<tr>
<td valign="top" width="50%"><b>Addons</b><br><img src="docs/images/addons.png" alt="The Addons tab"></td>
<td valign="top" width="50%"><b>DLSS 5 Neural Rendering</b><br><img src="docs/images/neural-rendering-panel.png" alt="The Neural Rendering panel"></td>
</tr>
<tr>
<td valign="top"><b>Performance</b><br><img src="docs/images/performance.png" alt="The Performance tab"></td>
<td valign="top"><b>Settings</b><br><img src="docs/images/settings.png" alt="The Settings tab"></td>
</tr>
<tr>
<td valign="top"><b>Features</b><br><img src="docs/images/features.png" alt="The Features tab"></td>
<td valign="top"><b>About</b><br><img src="docs/images/about.png" alt="The About tab"></td>
</tr>
</table>

<sub>These pictures are rendered offscreen by the project's own preview tool (`tools/ui_preview.ps1`), so the Performance numbers and log lines are sample data, not a
measurement. The Neural Rendering panel comes from the offline test host.</sub>

Status: **0.2.0.** Built and tested against Lossless Scaling **3.2.2.0** (the official build) on Windows 11 with an RTX 4070 Ti SUPER; other
setups are untested.

## What you get

- **An addon manager that stays out of the way.** It opens with Lossless Scaling, hides to the notification area when you close it, and comes
  back with a click on its icon or **Ctrl+Shift+F12** (you can change the key). Every control has a tooltip.
- **Install and remove addons without touching folders.** *Install addon* takes a folder, a zip or a lone DLL, or drop one on the window. New
  addons arrive switched off. *Remove* moves an addon into `addons\.removed` after asking; nothing is ever erased.
- **Each addon's own settings, inline.** Sliders that reset on double-click, show a small tick where the default is, and fine-tune with Ctrl+scroll.
- **A live Performance tab.** Game frame rate and frame time (average, slowest 5%, worst), what each addon costs, and your GPU's load, power
  against its limit, clocks, temperature and memory, with a plain-words summary ("the GPU is at its power limit"). GPU numbers come from NVIDIA's
  own driver library and are only read while the tab is open.
- **Live status on the addon card and in the status bar**, published by the addon itself.
- **Back up and restore.** Save every addon's settings and the manager's to one file; load it back after a confirmation (the current settings are
  kept aside first). One button makes a **diagnostics zip** with your logs and settings for a bug report; nothing is uploaded anywhere.
- **Interface size** from 75% to 200%, sharp on any display, per-monitor DPI aware.
- **Safe by design.** A faulting addon cannot take Lossless Scaling down with it, settings are written atomically, and a corrupt settings file is
  kept rather than overwritten. Optional SHA-256 checks of addon DLLs against a trust list.

## What ships with it

Two **built-in features**, on the Features tab, each a switch with a few options that appear once it is on. They started as addons of the original project and are part of the manager now.

| Feature | What it does | Default |
|---------|--------------|---------|
| **ReShade input passthrough** | Lets the mouse and keyboard reach a ReShade overlay while Lossless Scaling is scaling the game. A hotkey (Home by default) turns it on and off; it can be switched on at any time. | off |
| **Windowed mode and second monitor** | Adds a virtual display the size of your game window so Lossless Scaling can work with a windowed game or a second monitor, with split-screen and side-by-side options. It must be in place before Lossless Scaling starts, so switching it on needs a restart; switching it off is immediate. | off |

And one **addon**, which is separate because it needs an NVIDIA GPU and a file you supply:

| Addon | What it does | Default |
|-------|--------------|---------|
| **DLSS 5 Neural Rendering** | Runs NVIDIA's DLSS 5 neural model on the frames Lossless Scaling captures and applies the result to every frame it presents, real and generated, without ever making Lossless Scaling wait. Saved looks, per-game looks, HUD protection, shadows and highlights, colour, film grain and temporal smoothing. Needs an NVIDIA RTX GPU and a copy of `nvngx_dlssnr.dll` that you supply. [More](addons/DLSS5NR01/README.md) | on |

## Install

You need Lossless Scaling 3.2.2.0 installed and Windows 10 or 11, x64. There is no installer yet; installing is a copy.

1. Download `EchoAddonManager-<version>-x64.zip` from the [releases page](../../releases) and unzip it.
2. Close Lossless Scaling. Open its folder (for a Steam install, for example `C:\Program Files (x86)\Steam\steamapps\common\Lossless Scaling`).
3. **First time only:** rename the original `Lossless.dll` to `Lossless_original.dll`. Keep it: the manager forwards to it.
4. Copy everything from the zip into that folder: `Lossless.dll`, the two `LP-icon` files and the `addons` folder.
5. Start Lossless Scaling. The manager window opens by itself.
6. For Neural Rendering, also put your `nvngx_dlssnr.dll` next to `LosslessScaling.exe`. It is not included, and this project does not say where to
   find it.

**Updating:** close Lossless Scaling and copy the new `Lossless.dll` and `addons` over the old ones. Your settings live in `addons\config.json` and carry over.

**After a Lossless Scaling update:** an update may put its own `Lossless.dll` back. If the manager stops appearing, delete the stale
`Lossless_original.dll`, rename the new `Lossless.dll` to `Lossless_original.dll`, and copy ours in again.

**Uninstall:** delete our `Lossless.dll`, rename `Lossless_original.dll` back to `Lossless.dll`, and delete the `addons` folder if you like.

## Using it

| Tab | What it does |
|-----|--------------|
| **Addons** | Every addon with its state, version and author, a live status line, and a switch. Click one to open its own settings, an overview (description, tags, dependencies, errors) and its config file. |
| **Features** | The built-in features: ReShade input passthrough and Windowed mode. |
| **Performance** | Frame rate and frame time, addon cost, GPU load, power, clocks, temperature, memory, and a short reading of what is limiting you. |
| **Settings** | Backup and restore, interface size, open-at-start and the hotkey, security level, log detail, the diagnostics file, and shortcuts to the logs and addons folders. |
| **Logs** | The last 10,000 entries with a level filter. |
| **About** | Version, credits, and the Ko-fi link. |

**Closing the window** does not stop anything: the manager hides to the notification area (Windows may keep its icon under the ^ arrow next to the clock).

Files it reads and writes, all inside the Lossless Scaling folder:

| File | What |
|------|------|
| `addons\config.json` | Every addon's on/off state and settings, and the manager's (`global`). Written to a temporary file and swapped in; an unreadable one is kept as `config.json.corrupt`. |
| `addons\trusted_addons.json` | Optional: `{ "<addon id>": ["<sha256 of its DLL>"] }`, used by the Security setting. |
| `addons\.removed\` | Addons you removed, each in a folder with a timestamp. |
| `backups\` | Copies of settings made before a restore replaced them. |
| `logs\EchoAddonManager.log` | The manager's log. It rolls over to `.old` at 8 MB. Addons may write their own log files here too (Neural Rendering does). |

## How it works

```
LosslessScaling.exe
  └─ loads Lossless.dll          <- Echo Addon Manager (takes the original's place)
       ├─ loads Lossless_original.dll (the real engine) and forwards its exports untouched
       ├─ watches DirectX 11 compute work and shader loading, for addons that ask
       ├─ loads addons from addons\
       └─ draws the manager window with Dear ImGui on its own thread
```

Because it loads as a proxy DLL, nothing in Lossless Scaling is patched on disk and removing it puts everything back. Addons share the manager's
Dear ImGui context, so they draw their settings inline and look the same.

## Build from source

Visual Studio 2022 (Desktop C++ workload) and CMake 3.20+ on Windows. Dear ImGui and MinHook are fetched at pinned versions when CMake first configures.

```powershell
powershell -File tools\build_all.ps1                     # manager and all addons
powershell -File tools\build_all.ps1 -Only host,reshade,windowed   # everything except Neural Rendering
powershell -File tools\run_addon_tests.ps1               # offline tests, no game and no visible window
```

Building Neural Rendering from source (not needed to use the release zip) also needs NVIDIA's DLSS SDK headers and static library in
`addons/DLSS5NR01/external/ngx`. Download them yourself and follow the README in that folder; they are not redistributable, so they are not in this repository. `tools\deploy.ps1 -What all -LsDir <Lossless Scaling folder>` copies a build into a Lossless Scaling folder with backups and refuses to
run while Lossless Scaling or your game is open. `tools\package.ps1` builds the release zip.

## Writing an addon

See [docs/addon-authors.md](docs/addon-authors.md): the exports, the host interface, live status and metrics, the shared look, and the rules that are easy to trip over.

## Credits

Echo Addon Manager began as [LosslessProxy](https://github.com/FrankBarretta/LosslessProxy) by **FrankBarretta**, and we are grateful for it. About a third
of the manager's code (mainly the proxy DLL and the DirectX 11 and shader hooks) is still theirs, and the ReShade and Windowed features started there as
addons; the rest, including how addons are found, checked and loaded, the settings file, the event system, the window and the live status and metrics,
was written or rewritten since (`tools/measure_original_share.py` measures it). Neural Rendering is by **andreiday**, extended here. The full list, with licences, is in [NOTICE.md](NOTICE.md). Lossless Scaling belongs to its author; this project is unofficial.

If it is useful to you, you can [support it on Ko-fi](https://ko-fi.com/xechostormx).

MIT licence: [LICENSE](LICENSE). [Changelog](CHANGELOG.md).
