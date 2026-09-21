# Changelog

## Unreleased

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
