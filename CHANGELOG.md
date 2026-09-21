# Changelog

## Unreleased

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
  (on the `rewrite` branch until it has run in Lossless Scaling). New offline test `lsproxy_coretest` covers scanning, manifests, loading, starting, switching,
  removing, installing, the security levels, a faulting addon, the settings file and the host interface (94 checks); the window pictures are pixel-identical
  before and after (`tools/compare_ui_renders.py`). Small fixes on the way: a wrong-typed field in `addon.json` is skipped instead of dropping the rest of the
  manifest, and loading a second settings file no longer inherits what the first one last saved. The addon safety checks (SHA-256 against `trusted_addons.json`), the dependency ordering and the event bus were rewritten too, with fixes: the addon list
  no longer reverses its order every time it is sorted (installing or removing an addon used to flip it), a trusted-hash list that is loaded again replaces the old
  one instead of adding to it, a hash written in capitals matches, and a subscriber that faults is now logged. The share of the manager's code that is still the
  original project's went from 42.8% to 31.3% (`tools/measure_original_share.py`); the offline test now has 122 checks. Counting the ReShade and Windowed features, which moved into the manager
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
