# Changelog

## Unreleased

- New **compatibility self-test**: `nr_selftest.exe` (in the addon folder) loads the model in its own process, creates feature 18 on the NVIDIA card at 1280x720, evaluates a
  synthetic picture and checks it changed; it prints `SELFTEST <code> <KEY> <words>` and exits with the same code (0 pass, 10 to 20 for what went wrong). The addon's
  **Test compatibility** button runs it (and it runs by itself after **Browse for the model file...**) and shows a *Compatibility test* row. `req::RunProcess`,
  `req::ParseSelfTest` and `req::RunSelfTest` do the launching and reading, with tests (`nr_reqtest`, the `selftest` scenario and direct `nr_selftest.exe` checks in
  the scenario runner). `selfTestOnStart=1` in the addon's settings runs it at start-up (a diagnostic switch used by the offline test host).

## 0.2.2 (2026-09-21)

- **Fixed: a crash when Lossless Scaling exits without shutting the addon down.** The model start-up thread (and the requirements scan and file-dialog threads added below)
  are detached and tracked by flags instead of `std::thread` objects kept in statics, which called `std::terminate` if still joinable at process exit. `AddonShutdown` waits for a
  model load in progress, as the join used to. New test scenario `exit_abrupt` (`exitmode=abrupt` in the test host), which failed before the fix.
- A requirements line at the top of the panel that is always visible: green "all in place", amber for a note, red for what stops it. A model that cannot run on the
  graphics card (`FeatureNotSupported` at `CreateFeature`) is explained in words. **Browse for the model file...** copies a model file you pick into the Lossless Scaling folder
  (`req::PlaceModel`; the file that was there goes to `backups`, never deleted).
- New **Requirements** section at the top of the panel (and a red line above it when something stops the addon from running): graphics card, NVIDIA
  driver (NGX core), model file, helper DLL and engine state, each OK, NOTE or MISSING with a hint, and the engine's error messages in words. A model file
  that is not the tested build (310.8, 158.2 MB) is a note, a file under 20 MB counts as missing. The result is also written to the log at start-up. Nothing
  is loaded or downloaded to do this. New offline test `nr_reqtest`.

## 0.2.1 (2026-09-21)

- The last `lspnr` names are gone: the helper DLL is now `nvngx.dll_dlss5nr01.dll` (it must still contain `nvngx.dll`, which the DLSSNR snippet checks in its caller's path), the test tools are `nr_hosttest` and `nr_harness`, and the helper's exported functions start with `nrfwd_`. Nothing changes on screen. `tools\deploy.ps1` moves a stale `nvngx.dll_lspnr.dll` aside.

## 0.2.0 (2026-09-21)

- **Renamed to DLSS5NR01** (it was `LSP-NeuralRender`; the prefix meant LosslessProxy, and the addon has been changed a great deal from the original). The folder, the settings
  id, the DLL (`DLSS5NR01.dll`) and the log (`logs\DLSS5NR01.log`) all carry the new name; the name on screen is still DLSS 5 Neural Rendering. `addon.json` says
  `renamed_from`, so the manager carries the saved settings, looks and on/off state over to the new name on first start and hides the old folder.
- **Delete** on the saved-looks row is always shown (greyed out until a look is picked); it used to appear only after a look was selected, which made it easy to miss.
- **Ghost guard** (new slider, default 0.5, 0 = the old behaviour): fades the model's delta where LSFG's two motion fields disagree (object edges, newly
  uncovered areas) and a little more the older the delta is, which removes the faint copy of the previous frame that trailed moving things. Diagnostic view
  "Ghost guard" shows the weight. Saved looks made earlier still read as unchanged. Offline test: with a fake flow whose right half disagrees with itself, that half
  is faded from 6.33 to 0.19 mean change and the agreeing half is untouched.

## 0.5.0-echo.1 (local fork, 2026-09-21)

- Reports **live status and metrics** to the host (frame time, model cost, how often the model keeps up, GPU start time; a one-line status on the
  addon's card and in the manager's status bar). Only with a host of 0.5.0 or newer; on an older host it does nothing extra.
- **Saved looks at the top of the panel**, one row: Look (pick to apply), Save, Save as new, Delete (asks first). The old Presets section is gone.
- Technical status lines moved into a collapsed section; user-facing name **DLSS 5 Neural Rendering**.

## 0.4.0-echo.1 (local fork, 2026-09-21)

- **HUD protection**: up to six rectangles where the picture stays exactly as Lossless Scaling made it; edge softness;
  on-screen outline while adjusting; saved with presets.
- **Shadows** and **Highlights** sliders, **Film grain** (with size).
- **Temporal smoothing** of the model's delta (motion-warped by LSFG's flow), for shimmer on distant fine detail.
- **Per-program looks**: link a program's exe to a preset; applied automatically when the program takes focus.
- **Restyled panel** and sliders in the manager's new colour scheme.
- Fix: mid-word text wrapping in the addon's panel.
- Test host: never shows a window; `shot=` renders the panel offscreen; scenario matrix in `dev-tools/run_hosttest_matrix.py`.
- Roadmap item added to the README: automatic model resolution.

## 0.3.0-echo.1 (local fork, 2026-09-19)

- **Sharpen**: contrast-adaptive sharpening on every presented frame, after the delta (`sharpen`, 0 = off).
- **Compare views**: enhanced / split (left original, right enhanced) / original only, display-only.
- **Hotkeys**: Ctrl+Shift + F6 before/after, F7 split, F8/F9 sharpen -/+ (configurable, polled at present), with a
  corner marker square as feedback and `hotkey:` lines in the log.
- **Brightness**, **Contrast** and **Gamma** sliders in the compose pass, applied before saturation (part of a preset).
- **Model passes** (1-4, default 1): rerun the model on its own result to strengthen the effect (part of a preset).
- **Saturation** and **Vibrance** sliders in the compose pass (part of a preset).
- **Presets**: save / apply / delete named looks, and a Ctrl+Shift+F10 hotkey that cycles them (purple corner marker).
- **Clearer names and tooltips** for every control (for example Working scale is now *Model resolution*, Apply strength
  *Blend amount*, Local structure *Fine detail strength*); **Restore defaults** button; the log moves to
  `<Lossless Scaling>\logs\LSP_NeuralRender.log`.
- Crash logging inside the addon, engine-start thread guard, present-hook unload safety, panel race fix, tiny-capture
  guard, self-re-arming watchdog, measured cost in the panel.

## 0.2.0 (2026-09-04)

Lossless Scaling never waits for the model any more.

- Read-only tap: the addon copies the real frame and LSFG's optical flow out at LSFG's
  per-real-frame pass and never writes Lossless Scaling's textures.
- Free-running model on its own D3D12 queue. A frame is skipped when the previous run is still on
  the GPU. The model writes a work-resolution delta (model minus input) into one of three
  NT-shared slots.
- Present-time compose: a swap-chain vtable patch (Present / Present1) sees every frame Lossless
  Scaling presents; a compute pass on Lossless Scaling's own D3D11 device adds the newest finished
  delta, moved by LSFG's flow to where the content sits in that frame. Generated frames get the
  right fraction of the flow; the tap learns how many presents LS makes per real frame and
  whether the real frame comes first or last.
- Three shared fences (copyIn, done, used) keep the two queues in step with GPU-side waits only.
- Removed the inline and delayed scheduling modes, the resolve pass, the window, the masks,
  luma-only, delta smoothing and the guided upsample. All of them either put the model's time on
  LS's queue or produced visible artifacts through LSFG's interpolation.
- Panel: keep-up ratio, GPU start/done times relative to submit, tap CPU time, present pattern,
  compose count and CPU time, newest delta and its offset; debug views for original, delta x4,
  frame role and LSFG flow.
- Forwarder: calls `NVSDK_NGX_D3D12_PopulateParameters_Impl` and exposes the snippet's
  scaling-ratio callback. Harness: `--perf N` and `--perfscan`.
- Offline test host drives a real flip swap chain with an X3 present pattern and checks that the
  tap is read-only, the compose lands, and a resolution switch is followed.

## 0.1.0 (2026-09-03)

- First working addon: ImGui settings panel, inline d3d11 Dispatch hook,
  frame tap by dispatch shape, D3D11/D3D12 bridge, DLSSNR feature 18 through the forwarder.
- Inline and async modes; working-scale cost lever; LSFG optical flow as the model's motion
  vectors.
- Standalone harness measuring the model and every knob it reads (`docs/dlssnr-knobs.md`).
