# Changelog

## 0.4.1 (2026-09-21)

Upgrading from 0.4.0: copy the new files over the old ones. Nothing to migrate. This release is about how the panels look; nothing else changed.

- **Every collapsible section of the Neural Rendering panel now starts closed.** *Model*, *Quality and performance*, *Picture* and *Compare and hotkeys* used to open by themselves; now all nine
  sections (with *Keep the HUD untouched*, *Games*, *Frame detection*, *Technical status* and *Advanced*) start folded, so the panel opens as a short page: Status, Requirements, Saved looks, the
  on/off switch, and a list of sections to open. A check in the scenario runner fails if a section is ever set to open by default.
- **The open / close control of a section is now a boxed plus or minus**, not a small arrow: a white plus when the section is closed, a green minus when it is open, and it lights up when you point at it,
  so it is obvious that the section can be opened. It is the shared section widget, so the Performance tab's *All live values* section has it too.
- **Headings that cannot be closed are now the same bright green as an open section** (they were the dim green of a closed one), in the Neural Rendering panel and in the manager's own tabs
  (Settings, Features, About). Only a closed section is dim now, so at a glance bright means "this is open or always shown".

## 0.4.0 (2026-09-21)

Upgrading from 0.2.3: copy the new files over the old ones. Nothing to migrate. The version number goes from 0.2.3 to 0.4.0 on purpose: 0.3 is skipped, because
the step from 0.2.x (window rewrite, built-in features, the requirements check, the compatibility test, the reorganised panel) is bigger than a patch. The addon API is
unchanged (1.0.0), so addons built for 0.2.x still load.

- **The Neural Rendering panel is reorganised** into titled blocks with a line between them: **Status**, **Requirements**, **Saved looks (load and save your settings)**, **Neural Rendering**
  (the on/off switch) and **Settings** (the sections with the sliders). Requirements is always open now. It starts with what you have to do yourself, providing your own copy of
  `nvngx_dlssnr.dll`, in three numbered steps, then a one-line verdict, the rows, and the actions with the two main ones first (Browse, Test compatibility). Saved looks says what a look
  is and what Save, Save as new and Delete do; its list is now labelled *Saved look*. The first slider section is *Model (what it does to the picture)*, so it no longer sounds like a saved look.
- **You provide the model file yourself, said up front:** an important notice at the top of the README and of the addon's README, in the release's `INSTALL.txt`, and in the addon's description on its card.
- **Bugsweep.** A strict `/W4 /analyze` build of the manager and the addon found nothing serious; what it did find is fixed: the addon search box lower-cased text with `::tolower` on plain
  `char`s, which is undefined for non-ASCII names (an addon with an accented letter in its name); the crash logger's filter could pass a null pointer on; a `ReadFile` and a `swscanf` result were
  ignored; a process attribute list was used without a null check; and a test used the 49-day-wrapping `GetTickCount`. The analyzer's other notes are the deliberate `__except` guards
  around calls into addons (a faulting addon must not take Lossless Scaling down). The window, feature and core tests were run repeatedly (24 runs) to look for timing flakiness: none.
- **Cleanup.** The unused Dear ImGui demo source is no longer compiled into the manager; a README link that only worked through a GitHub quirk is now a plain address.
- **Settings tab:** the backup section is now *Backup and restore (save and load your settings)* with a line saying what it keeps, so it is clear that all addon settings and saved looks travel in one file.

## 0.2.3 (2026-09-21)

Upgrading from 0.2.2: copy the new files over the old ones. Nothing to migrate. `addons\DLSS5NR01` gains one file, `nr_selftest.exe`. If your security software
removes it (it is unsigned, like everything here), the *Test compatibility* button says the test program is missing; nothing else depends on it.

- **DLSS 5 Neural Rendering: a compatibility test.** A **Test compatibility** button runs the model file once, in a separate small program (`nr_selftest.exe`, new in the
  addon folder), on your graphics card, and adds a *Compatibility test* row that says whether it works there: the model must load, create its Neural Rendering feature and
  change a test picture. It runs by itself after **Browse for the model file...** has placed a file. Because it is a separate program, a model that crashes cannot take
  Lossless Scaling down. It tells you before playing that a model file cannot run on your card. Tested with the real model, and with a missing file, random bytes, no
  matching graphics card and a missing helper DLL, each of which ends with its own code and never a crash. The release zip gains `nr_selftest.exe`. The test program is started
  normally inside a job object that ends it if Lossless Scaling ends first (Windows 10 and later take the job as a start-up attribute), not started suspended and resumed: that
  is how malware starts processes it wants to tamper with, and security software watches for it.

## 0.2.2 (2026-09-21)

Upgrading from 0.2.1: copy the new files over the old ones. Nothing to migrate.

- **Fixed: a crash when Lossless Scaling exits.** Lossless Scaling can end its process without the manager shutting the addons down first, and a background thread
  that was still tracked by a `std::thread` object at that moment made Windows abort the program (exit code `0xC0000409`, an error on closing). Fixed in Neural Rendering
  (the model start-up thread, and the new requirements and file-dialog threads) and in the manager (the ReShade passthrough watcher and the Performance tab's GPU sampler).
  0.2.0 and 0.2.1 have this weakness in Neural Rendering's model start-up thread and in the manager's two threads, so it is worth updating. Found by tests that end the
  process the way Lossless Scaling can (`exitmode=abrupt`, `abrupt` and `abrupt-gpu`), which failed before the fix and are now in the test runners.
- **DLSS 5 Neural Rendering: a Requirements check** at the top of its panel: the graphics card, the NVIDIA driver's NGX core, the model file
  (`nvngx_dlssnr.dll`, with its version and size compared with the build it was tested with), the helper DLL and the engine's state, each marked OK,
  NOTE or MISSING with what to do about it, and engine failures explained in words. Nothing is loaded or downloaded to check. It does not fetch the model
  file, and this project still does not say where to get it. A **Browse for the model file...** button copies a model file you pick into the Lossless Scaling
  folder (the file that was there is moved to `backups`, never deleted).
- The release zip is now written with standard `/` path separators (earlier zips used `\`, which Windows Explorer and 7-Zip accept but other unzip tools warn about).
- **`tools\fetch_ngx_sdk.ps1`** fetches the NVIDIA DLSS SDK files needed to build Neural Rendering from NVIDIA's own public repository (pinned to a
  commit, checked by SHA-256) when you pass `-AcceptNvidiaLicense`; `-Latest` takes NVIDIA's newest commit instead, checked against NVIDIA's own git ids, and
  prints the lines to pin it. The pin is "DLSS 310.9.1 SDK", NVIDIA's newest today. The files are still not part of this repository, and never in the release zip.

## 0.2.1 (2026-09-21)

Upgrading from 0.2.0: copy the new files over the old ones. Nothing to migrate. Neural Rendering's helper DLL is now `nvngx.dll_dlss5nr01.dll`; the old
`nvngx.dll_lspnr.dll` in `addons\DLSS5NR01` is no longer used and can be deleted.

- **The manager window's code was rewritten** (window, tray icon, hotkey, dpi, the D3D11 device, the icons and the frame with its tabs and status bar), split into
  small modules under `manager/src/gui/window`, with a new offline test that drives the real window (`lsproxy_guitest`, run three ways) and checks the
  placement, hotkey text, scale limits and status text on their own. The pictures are pixel-identical to before. Fixed on the way: with the interface size
  set above or below 100 % the window was saved at a size divided by that factor and came back smaller after each close; the saved size now uses the display
  scale only. The hotkey label in the tray tip and the balloon uses the same F1 to F12 limit the hotkey itself does. The share of the manager's code that is
  still the original project's is now 29.4% (counting the ReShade and Windowed features as the original's).
- Neural Rendering's helper DLL, test tools and internal names no longer use the old `lspnr` prefix (details in the addon's changelog). Nothing changes on screen.
- **Repository clean-up.** Six of NVIDIA's DLSS SDK header files had been committed by mistake in the 0.2.0 source, although the SDK is not redistributable and
  is meant to be downloaded by whoever builds Neural Rendering (see `addons/DLSS5NR01/external/ngx/README.md`). They are removed from the repository and from its
  history, so the commit hashes after 0.1.0 changed and the `v0.2.0` tag points at the rewritten commit. The release zips never contained them. If you cloned this
  repository between the two releases, clone it again. The README now says that the SDK is needed only to build Neural Rendering, not to use the release.

## 0.2.0 (2026-09-21)

Upgrading from 0.1.0: copy the new files over the old ones, as in the install steps. Your settings carry over: Neural Rendering was renamed (its settings, looks and on/off are moved to the new
name automatically) and ReShade passthrough and Windowed mode are built in (their switches keep their place in `config.json`). The old `LSP-NeuralRender`, `LSP-ReShade` and `LSP-Windowed`
folders are ignored; you can remove them.

- **Neural Rendering is now `DLSS5NR01`** (folder, settings id, `DLSS5NR01.dll`, `DLSS5NR01.log`), no longer `LSP-NeuralRender`. New manager support for this: an addon's `addon.json` can list
  `renamed_from` folder names; the manager moves their saved settings (including on/off) to the new name, once, and hides the old folders so the same addon never runs twice. The
  saved-looks row's **Delete** button is always visible now. A compiler pass (`/W4` plus code analysis) over the manager and the addon found only trivial issues, all fixed.
- **ReShade input passthrough and Windowed mode are built into the manager** (a new **Features** tab) instead of being separate addons. Same behaviour, in
  the manager's own code and look, with the settings kept where they were (`LSP-ReShade` and `LSP-Windowed` in `config.json`). Gains: Windowed mode can be switched
  off and on again while Lossless Scaling runs (only switching it on for the first time needs a restart, because its hooks must be in before Lossless Scaling asks
  for displays), it starts earlier, its options are two combo boxes and two checkboxes instead of a panel, and the addon list no longer has two entries that are
  really settings. An old `LSP-ReShade` or `LSP-Windowed` addon folder is ignored, so the two versions can never both run; the deploy script moves such folders
  aside. MinHook (pinned, v1.3.4) is now a dependency of the manager. Offline test `lsproxy_featurestest` (run twice: Windowed on and off at start-up) replaces the
  two old addon tests. The release now ships one addon (Neural Rendering) instead of three.
- **Manager: addon handling, settings file, host interface, toggle switch, toast, addon card and logs tab rewritten** as smaller modules with the same behaviour
  (tried in Lossless Scaling before release). New offline test `lsproxy_coretest` covers scanning, manifests, loading, starting, switching,
  removing, installing, the security levels, a faulting addon, the settings file, the host interface, the safety checks, the dependency order and the event bus (126 checks); the window pictures are pixel-identical
  before and after (`tools/compare_ui_renders.py`). Small fixes on the way: a wrong-typed field in `addon.json` is skipped instead of dropping the rest of the
  manifest, and loading a second settings file no longer inherits what the first one last saved. The addon safety checks (SHA-256 against `trusted_addons.json`), the dependency ordering and the event bus were rewritten too, with fixes: the addon list
  no longer reverses its order every time it is sorted (installing or removing an addon used to flip it), a trusted-hash list that is loaded again replaces the old
  one instead of adding to it, a hash written in capitals matches, and a subscriber that faults is now logged. The share of the manager's code that is still the
  original project's went from 42.8% to 31.3% (`tools/measure_original_share.py`). Counting the ReShade and Windowed features, which moved into the manager
  and began as the original project's addons, it is 32.7%.
- **DLSS 5 Neural Rendering: Ghost guard.** New slider that fades the model's change where Lossless Scaling's motion data is unreliable and as the change ages, to remove the faint
  copy of the previous frame that could trail moving things. Default 0.5; 0 restores the old behaviour. See the addon's changelog.

## 0.1.0 (first release)

The first release of Echo Addon Manager as its own project, with a fresh history. It started from FrankBarretta's
[LosslessProxy](https://github.com/FrankBarretta/LosslessProxy) (see [NOTICE.md](NOTICE.md)).

### Manager
- Loads in place of Lossless Scaling's `Lossless.dll`, forwards to the original, and loads addons from `addons\`.
- Window with tabs Addons, Performance, Settings, Logs and About, in a dark neutral and green look with vector icons, tooltips on every control,
  and an interface size setting (75% to 200%). Hides to the notification area; opens again from its icon or a hotkey (Ctrl+Shift+F12 by default).
- **Install addon** from a folder, zip or DLL (button or drag and drop; new addons arrive switched off) and **Remove** with confirmation (moved to
  `addons\.removed`, never erased). Addon search (Ctrl+F). Each addon's settings, overview and config file in a detail pane.
- **Performance tab**: game frame rate and frame time, addon cost, GPU load, power, clocks, temperature and memory (NVML, read only while the tab
  is open), 20-second graphs and a plain-words summary.
- **Live status and metrics**: `SetStatus` and `PublishMetric` on the addon interface; a status line on each card and in the status bar.
- **Backup and restore** of all settings; **diagnostics zip** with logs, settings and a summary (nothing uploaded).
- Settings that work and save at once: security level, log detail, auto-load, open at start, hotkey.
- Atomic settings writes, a corrupt settings file is kept rather than replaced, a bounded log with timestamps, crash backtraces for the manager
  and any addon, a faulting addon is isolated, and Dear ImGui is pinned so addons and the manager share one layout.
- Addon API **1.0.0**, separate from the release number (`sdk/include/lsproxy/version.h`).

### Addons
- **DLSS 5 Neural Rendering** (andreiday, extended): read-only tap of Lossless Scaling's frames, a free-running model, present-time compose.
  Saved looks in a bar at the top of the panel, per-program looks, rectangles that keep the HUD untouched, shadows and highlights, sharpen,
  saturation and vibrance, film grain, temporal smoothing, compare and hotkeys, a live status line. Its own history is in
  [its changelog](addons/DLSS5NR01/CHANGELOG.md).
- **ReShade Input Passthrough**: reworked as a plain switch with a tooltip; window handling restored cleanly and pinned when it cannot be undone. Offline lifecycle test.
- **Windowed Mode**: reworked as a plain switch, panel fixed; adds the virtual display only while enabled. Offline test.

### Tools and tests
- `tools\build_all.ps1`, `deploy.ps1` (backs up, refuses while Lossless Scaling or your game runs), `run_addon_tests.ps1`, `run_hosttest_matrix.py`
  (ten Neural Rendering scenarios), `ui_preview.ps1` (offscreen renders of every tab), `package.ps1` (the release zip).
