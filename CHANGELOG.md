# Changelog

## 0.7.9 (internal, 2026-09-23)

Not a public release (the manager still shows 0.7.0).

**Neural Rendering rewritten as our own code.** The part of it that still matches the original DLSS 5 plugin fell from 55% at the start of this work (41% before this batch) to 11%, and
what remains is mostly declarations whose names are the settings, and common idioms. The manager is at 11% too. The MIT notices of both projects stay where they are, beside ours. The
addon's author on its card is now Echo-Storm, and andreiday is thanked in NOTICE.md and the README.

- **The engine** (the model's own D3D12 device and queue) and **the forwarder** (the one module that may call the model file) are rewritten. The run is the same pass for pass: the same
  descriptors, barriers, flow scale and model passes, and every model scenario gives the same results. Also:
  - **Fixed: a GPU hang could crash Lossless Scaling.** If the GPU had not finished with a command allocator after two seconds, the engine reset it anyway, taking commands from under the
    GPU. That frame is now skipped instead; since 0.7.5 the bridge handles a run that was not queued.
  - **Fixed: upload buffers could be freed while the GPU still read them,** when waiting for the GPU timed out. They are now kept until it has finished.
  - Each of the engine's passes has its own descriptors. Before, two passes shared a slot, and two descriptors were made at every run but never used.
- **The present-time compose is rewritten**, and its shader is split into named steps: HUD areas, ghost guard, sharpening, tone, tonal ranges, colour, grain. The results are identical. The
  scratch texture a swap chain buffer needs when it cannot be written directly is now made only for such a buffer.
- **The model-side shaders are rewritten**, with identical maths.
- **`addon.cpp` is split into its parts:**
  - `runtime.cpp`: the frame path.
  - `panel.cpp`: the settings panel.
  - `tasks.cpp`: the requirements scan, the compatibility test and the file dialog.
  - `settings.cpp`: the settings and saved looks.
  - `log.cpp`: the log and the crash reports.
  - `state.h`: what they share, with which lock guards what.
- **The settings are table-driven.** Every setting of a look (its key, default and range) is written once. Before, the ranges were written three times (loading, applying a look, the panel)
  and could drift apart. Saved settings and looks load as before. A new offline test, `nr_settingstest`, checks looks as text and back, the ranges, the HUD areas, names, and the settings file
  through a stand-in host.
- **Fixed: a data race in the panel.** The reason Neural Rendering switched itself off was read on the window thread without the lock the render thread writes it under.
- Dead state removed: six variables that were written but never read.
- **`nr_harness` is retired.** It was the research tool that ran the model on a still image, and the self-test and the test host cover what it checked. It is in the history at tag v0.7.8, and
  `docs/dlssnr-knobs.md` keeps its findings.

The manager:

- **Fixed: an addon's settings panel that crashed was drawn again at every frame**, crashing each time and leaving the window's drawing half done. It is now switched off for the session after
  the first crash, as a panel that passes a bad CRT argument already was. The core test checks it with a test addon whose panel crashes.
- The About tab and the README's credits now describe what came from LosslessProxy (the idea, the addon interface, the ReShade and Windowed features) instead of the old code share.
- **README: LosslessProxy's addons load here.** The interface has only grown at the end, the exports and the older `AddonInit` name are accepted, and the events and capability bits are the
  same. The caution: an addon with a settings panel should be rebuilt against this SDK, because LosslessProxy did not pin its Dear ImGui.
- **Tests run in parallel.** `tools\run_addon_tests.ps1` builds everything once, then runs the suites that can overlap (core, sample, update, installer and the Neural Rendering model
  scenarios) all at once, and the window tests one after another beside them. It prints one line per test program. A full run takes about 7 minutes, down from 10; a change-based one about
  1.5. `-Only` with `-All` now means those suites at full depth.

## 0.7.8 (internal, 2026-09-23)

Not a public release (the manager still shows 0.7.0).

- **The manager's start-up (`main.cpp`) rewritten as our own code.**
  - One `Start`/`Stop` pair: the log, Lossless_original.dll, the one-manager-per-folder check, the settings and addons, the hooks, the built-in features, then the window thread.
  - `ApplySettings` keeps its exact signature, and the pointer to the real one now takes its type from ours, instead of a second hand-written copy of 32 parameters that could drift.
  - Its log lines say what failed and what that means ("settings will not reach Lossless Scaling").
- **The addon SDK headers rewritten** (`ihost.h`, `addon_sdk.h`; `events.h` in 0.7.7), with the same declarations in the same order (the frozen 1.0 test checks that). The documentation now says what the
  manager actually does:
  - `GetD3D11Device` is the newest device, and new ones come as scaling starts and stops;
  - capabilities are declarations, not a gate (it said the device was withheld from addons that did not ask; it never was);
  - each addon has one dispatch callback per userData, removed by itself when the addon unloads;
  - `GetConfig`'s text stays valid for hundreds of later calls.
- The one-line search box widget is gone; the Addons tab draws its search field directly.
- The original project's code is now 11% of the manager's lines (from 19.9% at the start of this work). What remains is mostly declarations whose names are the API or the manager's own interface, and
  common idioms; renaming them only to move the number is not worth the churn.

## 0.7.7 (internal, 2026-09-23)

Not a public release (the manager still shows 0.7.0).

- **Fixed (in 0.7.6, before it reached a game): the dispatch callbacks watched only the newest device.** Lossless Scaling makes two D3D11 devices within a tenth of a second when scaling
  starts, and its compute passes run on the first. The logs of 2026-09-23 show this twice. The manager now watches the contexts of every device Lossless Scaling makes, and the core test checks
  that the first device's passes still call back after a second device is made.
- **Addon API 1.1: `IHost::GetDispatchingContext()`.** Inside a dispatch callback it gives the context the pass runs on, which tells Lossless Scaling's devices apart. Like every addition, it is
  appended to the end of `IHost`, so addons built against 1.0 are unaffected.
- **Neural Rendering uses the manager's dispatch callback instead of a hook of its own.** Each of Lossless Scaling's passes now goes through one detour instead of two stacked ones (two code hooks
  on the same function can undo each other when one is removed). The addon no longer needs MinHook, and asks for API 1.1 (`min_host_version`). It declares `EAM_CAP_DISPATCH_HOOK`, and its panel
  shows the passes seen instead of a hook count. The offline test host now runs the callback the way the manager does, and every model scenario passes.
- **Fixed: two addons that both registered a dispatch callback without userData replaced each other's.** A callback was identified by its userData alone, and the SDK's default is null.
  It is now identified by the calling addon together with its userData.
- **The addon interface is now locked by a test.** The core test holds a frozen copy of the interface as released with API 1.0 and calls today's host through it, as an addon built against
  0.7.0 does. Every call must reach the right function, and the event ids and payload layouts must be unchanged.
- `events.h` rewritten, documenting each event and its payload. `SETTINGS_CHANGED` and `SHADER_INTERCEPTED` are marked as reserved: they were declared but never sent.
- **Tests: only what the change can affect.**
  - `tools\run_addon_tests.ps1` runs the suites that the files changed since the last commit can affect, and prints only failures and the time each took. `-Only core,gui` picks suites and
    `-All` runs everything; the suites are core, features, sample, update, gui, installer, setupexe and nr.
  - The Neural Rendering model scenarios have an everyday set (`run_hosttest_matrix.py --quick`: 3 scenarios, about 70 s) beside the full one (15, about 6 minutes).

## 0.7.6 (internal, 2026-09-23)

Not a public release (the manager still shows 0.7.0).

The manager's core hooks rewritten as our own code, with the bugs found on the way:

- **Fixed: addons' dispatch callbacks never ran under Lossless Scaling.** The manager patched `Dispatch` in the context's function table. Each context has its own copy of that table, and
  `SetMultithreadProtected` (which Lossless Scaling calls) swaps the copy's entries, so the patch stopped firing (checked with a test program). The same was true of `GetDispatchCount` and
  `GetCurrentComputeShader`. Neural Rendering was not affected because it hooks the code itself; the manager now does that too. It hooks each `Dispatch` implementation in d3d11.dll and runs the
  callbacks only for Lossless Scaling's context, with no cost to its dispatches while no addon has a callback.
  - The hooks are installed on the manager's window thread, before the addons load. Finding them needs D3D, which must not run inside DllMain.
  - `GetCurrentComputeShader` now asks the context during a dispatch callback, instead of hooking every `CSSetShader`; outside a callback it returns null.
  - An addon's own dispatches from inside a callback do not call it back.
- **Fixed: Windowed mode and any other MinHook user in the manager could switch each other off.** Windowed mode started MinHook for itself and, when it stopped, disabled every hook in the manager
  and shut MinHook down. MinHook is now shared with a count of users, and each part switches only its own hooks on and off.
- **Fixed, resource replacement** (the hook addons use to replace Lossless Scaling's shaders):
  - every replaced resource with a string name got the same handle, so a second one replaced the first;
  - shutting down deleted a lock that later calls still used;
  - uninstalling left the import table pointing at the hook;
  - it said "Hooks installed" even when patching had failed.

  Each replacement now gets its own handle, and the same resource with the same bytes gets the same handle again. Replacement bytes stay valid for as long as Lossless Scaling may hold them.
  Uninstalling puts the import slots back, and the log says how many of the five functions were hooked.
- **Fixed: icons in a folder with non-ASCII characters in its name did not load** (addon icons, and the window icon's PNG fallback). The image library opened files by an ANSI path. Files are now read by
  their full path, and images over 1024 pixels a side or 16 MB are refused.
- The D3D11 device hook checks what it patches. It now logs a failure instead of "installed", and never calls a missing `D3D11CreateDevice`.
- The export forwarding list is now one line per export; the exports were checked against Lossless Scaling's own DLL (all 11).
- `iat_patcher.h` is replaced by `core/hook_util` (import table lookup, delay imports, the shared MinHook).
- New core tests, on this program's own imports and on real D3D11 devices:
  - resource replacement: separate handles, reuse, pass-through and uninstall;
  - dispatch callbacks, including after multithread protection is switched on;
  - another device's context is left alone;
  - skipping a pass;
  - nothing runs after shutdown.

## 0.7.5 (internal, 2026-09-23)

Not a public release (the manager still shows 0.7.0).

- **Neural Rendering's bridge and hooks rewritten as our own code.** The bridge hands frames to the model and results back to Lossless Scaling; the hooks watch Lossless Scaling's compute passes and presents. The
  original DLSS 5 plugin is now an inspiration for these, not their source.
- **Fixed: Neural Rendering could stop for good after one failed run.** If a run was never queued (the model was not ready at that moment), the bridge still waited for it to finish. Since it never did, every
  later frame was skipped, silently. A run that is never queued is now simply forgotten.
- **Fixed: a race when the hooks are removed.** The dispatch hook read its callback twice, so removing it at the wrong moment could call a null function. Both hooks now read the callback once, atomically.
  The present hook's hit count is atomic too.
- **Old "LosslessProxy" names removed** (`lsproxy`, `LSPROXY_`, `LsProxy`, `lsp::`, `LP-icon`):
  - The addon SDK now lives in `manager/sdk/include/eam/` (`addon_sdk.h`, `widgets.h`, `icons.h` and the rest), with `EAM_*` macros, `Eam*` types and the `eam::ui` widget namespace. Only source code
    names changed: the functions an addon exports and the layout of everything passed between the manager and an addon are the same, so addons already built keep working. An addon's source needs its
    includes and names updated to match.
  - The icons are `manager-icon.ico` and `manager-icon.png`. The installer (and `tools/deploy.ps1`) moves `LP-icon.*` from an earlier install to the backups.
  - ReShade passthrough and Windowed mode keep their settings under `ReShadePassthrough` and `WindowedMode`. Settings saved under the old ids move across by themselves the first time the manager starts,
    never over settings already there. Old standalone addon folders with those names are still recognised and ignored.
  - Settings backups are marked `eam_settings_backup`; backups made by earlier versions still import.
  - The test programs are `eam_coretest`, `eam_guitest` and so on.
- New tests for all three: the settings move, old backups, and the old icon files moved aside.

## 0.7.4 (internal, 2026-09-23)

Not a public release (the manager still shows 0.7.0).

- **Neural Rendering's frame tap rewritten as our own code.** The part that watches Lossless Scaling's compute passes, finds the capture pass, and hands the model each frame with its motion was
  the largest piece still taken almost unchanged from the original DLSS 5 plugin. It now reads each pass's bound views once, and spells out the flow-pass and generated-frame rules. It also drops the
  per-pass fields nothing read any more. The saved pass text in the settings is unchanged, so existing configurations keep working.
- **Fixed: textures held after the tap went away.** The frame tap had no destructor, so the flow textures and a held frame it still owned were never released when it was destroyed (found by the new
  test: three references left where one was expected).
- **New offline test `nr_taptest`**, part of `tools/run_addon_tests.ps1`. It uses a real D3D11 device and textures shaped like LSFG 3's, with no model or NVIDIA SDK needed at run time. It checks:
  - which pass is the capture;
  - that every frame gets its own motion, and what happens when a flow pass is skipped or the flow scale changes;
  - where each present sits between real frames;
  - the old one-frame-late timing, with fresh flow off;
  - that no texture reference is left behind.
- `ROADMAP.md`: openNR noted under "watching", with what it is and is not.

## 0.7.3 (internal, 2026-09-23)

Not a public release (the manager still shows 0.7.0).

- **One manager per Lossless Scaling folder.** Lossless Scaling runs one copy of itself: starting it again loads `Lossless.dll`, hands over to the running copy and exits a moment later.
  In that moment the manager used to start completely a second time (a second tray icon, a hotkey that could not be registered, a window, the addons) and Neural Rendering reopened its log
  for writing, which wiped the running session's log and interleaved the two (seen live on 2026-09-23). The first manager in a folder now holds a named mutex; a later copy only forwards to
  Lossless Scaling and starts nothing. Neural Rendering also never wipes a log another process still has open: it writes `DLSS5NR01-<process id>.log` instead. The core test checks the guard
  with a real second process (the same folder in capitals or with a trailing separator counts as the same folder).
- First live results of the 0.7.2 motion timing, in World of Warcraft: Forever at 1440p 120 Hz: 59,904 of 60,000 frames got their own frame's motion; "ghosting seemed better".
- `ROADMAP.md`: a screenshot button and key with a chosen folder, for the 0.8 interface pass.

## 0.7.2 (internal, 2026-09-22)

Not a public release (the manager still shows 0.7.0).

- **Neural Rendering gets this frame's own motion.** The model has no game motion vectors, so it is given Lossless Scaling's optical flow instead. It used to run the moment a frame was
  captured, before LSFG had measured that frame's motion, so it got the previous frame's flow: right while motion stays steady, wrong whenever it changes (turning, starting, stopping,
  strafing), where the model's own history is then pulled the wrong way, which shows as smearing, ghosting and invented detail. The frame is now held from its capture until LSFG has issued
  that frame's finest flow pass, and handed to the model on the next dispatch, a moment later in the same frame. A frame for which LSFG runs no flow pass is dropped rather than run late.
  A new switch, **Use this frame's motion**, under *Model* (on by default), brings back the old timing for comparison. The log reports how many frames got which motion; the scenario
  matrix checks that the default gives this frame's motion and that the switch gives the old behaviour (new `flow_previous` scenario).
- Neural Rendering keeps the previous session's log as `DLSS5NR01.log.old`: a problem seen while playing is still there after Lossless Scaling restarts.
- `ROADMAP.md`: a much easier HUD protection (draw the areas over a snapshot, detect them automatically, one layout per game) joins the ideas for after 1.0.

## 0.7.1 (internal, 2026-09-22)

Not a public release: the manager still shows 0.7.0 (the shown version moves at 0.8). A bug sweep of the manager itself (the code that runs inside Lossless Scaling), with a failing test written for each bug before it was fixed, and dead code removed.

- **Loading settings from a file no longer gets undone by the restart it asks for.** The running addons still hold their old settings, and Neural Rendering writes all of its settings back when it
  shuts down, so the restart put the old saved looks, per-game looks and sliders back over the imported ones. After an import, every later write (by an addon or by the manager) now waits for the restart,
  and the Settings tab says so until then.
- **An addon's dispatch callback that faults no longer ends Lossless Scaling.** These callbacks run on Lossless Scaling's render thread and were called unguarded; a faulting one is now removed and logged.
- **Switching an addon off takes back the callbacks it left registered.** An addon that forgets to unsubscribe from events or clear its dispatch callback left pointers into its unloaded DLL; the next
  dispatch would have run freed code. Everything whose code lies in the addon's DLL is removed when it is unloaded, and the log names the addon.
- **Settings that are not valid UTF-8 no longer crash the save.** An addon storing text in the ANSI code page (a path with an accented letter, for example) made writing `config.json`, and a settings backup,
  throw; such bytes are now replaced.
- **A hand-edited `config.json` whose `addons` or `global` is not an object** no longer throws at the first write: those parts start afresh and the original file is kept as `config.json.corrupt`. An addon
  entry that is not an object is replaced when that addon writes to it.
- **One bad frame no longer takes the manager window, and Lossless Scaling, down.** An error while drawing or while handling a click (a file that could not be written, a folder that vanished) is caught,
  the half-drawn frame is unwound with Dear ImGui's error recovery, and the window carries on with a note in the log and a toast.
- Two settings imports in the same second no longer overwrite each other's copy of the previous settings.
- **ReShade passthrough could freeze the manager window when switched off.** Its watcher restyled every window of the process, the manager's own included; switching passthrough off makes
  the manager's thread wait for the watcher, and a restyle in flight at that moment waits for the manager's thread. The manager's window is now left alone (it is no ReShade overlay).
- **Windowed mode no longer wraps an older DXGI factory as a newer one.** Where `IDXGIFactory6` is missing it used to pass an `IDXGIFactory2` off as one, which would call methods that object does not
  have; it now leaves the factory unwrapped and says so in the log (every supported Windows has `IDXGIFactory6`).
- Neural Rendering no longer copies its whole settings (strings and the per-game list included) at every presented frame just to read the five hotkeys.
- `ROADMAP.md` has an "Ideas for after 1.0" list: a multi-condition GPU limiter, an auto mode for Neural Rendering, per-game profiles, a before/after capture, a session summary, a stuck-state watchdog
  and a DLSS 4.5 addon.
- **Dead code removed:** the "LS1 logic" memory patches inherited from the original project (19 hard-coded addresses for an old Lossless Scaling build, which could never be applied because the hooks are
  installed before any addon's capabilities are known; the capability bit stays in the SDK as reserved, with no effect), two DirectX 11 vtable hooks that only passed calls through
  (`CSSetShaderResources`, `CSSetUnorderedAccessViews`: one less patch in Lossless Scaling and one less hop per call), an unused `ReloadAddons`, and a per-dispatch counter in Neural Rendering that was
  never read. A duplicated test header is shared instead of copied.
- Neural Rendering and the sample addon use `FetchContent_MakeAvailable` instead of `FetchContent_Populate`, which newer CMake releases deprecate and will remove.
- New checks: the core test covers settings of the wrong shape, invalid UTF-8, the import freeze, faulting dispatch callbacks and an addon that leaves its callbacks behind (a new "leaky" test-addon mode);
  the window test makes frames throw and checks the window survives.

## 0.7.0 (2026-09-21)

Upgrading from 0.6.0: run `EchoAddonManagerSetup.exe` from the new zip and choose **Update**, or copy the new files over the old ones. Nothing to migrate. The addon API is unchanged (1.0.0).
This release is bug fixes (found by reading the code and the logs of real sessions) and the last items on the way to 1.0 that needed no one but the maintainer: a sample addon, a written promise for the
addon API, a questions-and-answers page, shareable model compatibility reports, and automated builds.

- **Setup: Lossless Scaling running was missed when the folder was written another way.** The check compared path text exactly, so a folder given with a trailing or forward slash, a `..`, or its short (8.3) name
  was not recognised as the folder of a running Lossless Scaling (the install then failed at the first file in use, and rolled back, but without saying why). Both sides are now brought to one spelling first (`CanonicalPath`).
- **Setup: an update failed on read-only files.** An installed file marked read-only could not be replaced (the whole update rolled back). The flag is lifted for the swap and put back if the swap fails or is rolled back.
- **Setup: two runs in the same second shared a backups folder** and the second overwrote the first's copies (the folder name is a time stamp to the second). A folder that exists gets a number added.
- **Setup: only one window at a time**, so a second Setup cannot start a second install in the middle of the first. A drive root as the folder (`D:\`) no longer breaks the "restart as administrator" command line.
  Choosing the folder *above* Lossless Scaling's (for example `D:\Utilities`) now uses the Lossless Scaling folder inside it when there is exactly one. The uninstall option that takes the addons out says that the addons' settings go with them.
- **The tray icon comes back after a failed re-add.** A live log showed one failed re-add after Explorer restarted lose the icon for the whole session; the window now retries every two seconds for a minute, and treats an add that
  Explorer carried out but reported as failed as done. The window test forces two failures and checks that the icon returns.
- **A sample addon** (`examples/SampleAddon`): settings, a settings panel in the manager's look, a status line and a metric, about a hundred commented lines, with its own CMake file. It is loaded, started and drawn by the real manager
  in a new offline test (`lsproxy_sampletest`, 12 checks) and built standalone by the CI script, so the addon the guide points to cannot silently stop working.
- **A written compatibility promise for the addon API** (`docs/api-compatibility.md`): for 1.x, existing exports, `IHost` calls (new ones only at the end), capability bits, events, `addon.json` keys, the settings layout and the pinned Dear ImGui commit do
  not change; what is outside the promise; what guards it.
- **A questions-and-answers page** (`docs/faq.md`) and a **model compatibility page** (`docs/model-compatibility.md`). `nr_selftest.exe --report <file>` writes a short text file to share (graphics card, driver, Windows, the model file's name,
  version and size, the result and a ready-made table row; no folders, user name or hash), the addon passes it on every test and shows an **Open the compatibility report** button, and the scenario matrix checks the report's contents.
- **Automated builds:** `tools/ci.ps1` and `.github/workflows/build.yml` build the manager, the installer and the sample addon from a clean checkout and run the tests that need no GPU (addon handling, install, update check, sample addon,
  the installer core, its file bundle, and the Setup exe's silent mode). Neural Rendering and the window tests need NVIDIA's SDK and a desktop, so they stay with the regular runner.

## 0.6.0 (2026-09-21)

Upgrading from 0.5.0: run `EchoAddonManagerSetup.exe` from the new zip (it offers **Update**), or copy the new files over the old ones as before. Nothing to migrate. The addon API is unchanged (1.0.0).
The main news is the installer; the README was rewritten around it.

- **`EchoAddonManagerSetup.exe`: an installer with a window, in one file** (`installer/`, in the zip). It finds the Lossless Scaling folder (a running copy, the last folder used, Steam
  libraries, the usual places on every drive such as `Utilities` and `Games`, or "Browse" for any other copy; a folder you pick is remembered), says what state it is in and offers the one thing that fits:
  **Install**, **Update**, **Repair** (after a Lossless Scaling update put its own `Lossless.dll` back) or **Reinstall**, plus **Uninstall** (keeping or taking out the addons).
  It refuses while Lossless Scaling runs, backs up everything it replaces, verifies by hash, undoes itself if anything fails, and never touches the person's settings, other addons or
  removed addons. When the folder needs administrator rights it offers to restart itself as administrator. Afterwards it can copy the person's own `nvngx_dlssnr.dll` into the folder
  (only when there is none; nothing is downloaded), and every page says that file is not included. The files it installs are a resource of the exe, unpacked to a temporary
  folder; a silent mode (`--silent install|uninstall|status --folder <dir>`) exists for scripts. Windows' own TaskDialog draws it. New offline tests in the runner: the file bundle
  (28 checks, including damaged and hostile bundles that must write nothing) and the exe end to end on fake folders (25 checks: silent install, reinstall, repair after an update,
  uninstall, refusals, Lossless Scaling running, and the window opening and closing by itself). The package script builds the exe with the files inside, and checks it installs them byte for byte.
- **The README was rewritten** around the installer: what it is, a three-step start with a picture of Setup, what ships, how to keep it up to date (the update check and Setup's *Update* and
  *Repair*), a troubleshooting section, and the install by hand kept as the alternative. The addon README and user guide say Setup installs the addon. `INSTALL.txt` in the zip starts with Setup.
- The build instructions no longer list build targets that were removed (`-Only host,reshade,windowed`); they say `-Only host,nr`.

## 0.5.0 (2026-09-21)

Upgrading from 0.4.1: copy the new files over the old ones. Nothing to migrate. The addon API is unchanged (1.0.0). The manager now checks GitHub once a day for a newer release: it is **on by default**
and can be turned off in *Settings > Updates* (see below; it is the only thing the manager sends over the internet, and it never downloads or installs anything).

- **An update check.** Once a day the manager asks github.com for the latest release of this project and compares its version number with yours. If there is a newer one, the status bar says
  "Update available: 0.5.0", the About tab and the Settings tab show it with an **Open the download page** button, and a notice appears once (a toast, or a balloon from the notification area
  when the window is hidden). It **never downloads or installs anything**. It is **on by default** and can be turned off in *Settings > Updates*; **Check now** (Settings and About) always works.
  It is the manager's only network access: an HTTPS request to api.github.com that GitHub sees as your IP address plus the program's name and version. The page it opens is built from the
  release's tag on this project's own address, never taken from the answer, and an oversized, slow or unreadable answer is refused. New offline test `lsproxy_updatetest` (46 checks, against a small
  server of its own on the loopback address, so it needs no internet; add `live` to also ask the real GitHub).
- **`Lossless.dll` now carries a version resource** (product "Echo Addon Manager", the release version), so Windows Explorer shows its version and the coming installer can tell it from Lossless
  Scaling's own `Lossless.dll` without loading it.
- **Toward 1.0: the installer's core, not shipped yet** (`installer/`, with `ROADMAP.md` saying what 1.0 needs). It finds the Lossless Scaling folder (a running copy, Steam libraries, the
  usual places, the last folder used), decides from the two DLLs' version resources what state the folder is in, and can install, update, repair after a Lossless Scaling update (which puts its
  own `Lossless.dll` back over ours) and uninstall, with backups of everything it replaces, verification by hash, a rollback if anything fails part-way, and no changes to the person's `config.json`,
  other addons or removed addons. It recognises earlier installs (0.4.1 and before have no version resource) by the log file name inside their `Lossless.dll`, and refuses while Lossless Scaling
  runs. A command line (`setup_cli`) drives it; the window comes next. 84 offline checks (`setup_test`, in the test runner) on fake folders, including a failure midway that must roll back.
- **The README, the addon README, the user guide and `INSTALL.txt` say what this was tested with:** World of Warcraft: Forever (the beta; it runs as `WowB.exe`), Lossless Scaling 3.2.2.0, Windows 11, RTX 4070 Ti SUPER; other
  games and setups are untested.
- Test hygiene: the features test starts each run with a fresh temporary folder (a leftover from the abrupt-exit run could make a later run with the same process id fail), and the window test
  switches the update check off so it never touches the internet.

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
