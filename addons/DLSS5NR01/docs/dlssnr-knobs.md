# What the DLSSNR model actually listens to

Measured 2026-09-03 with `nr_harness.exe` (a research tool, retired in 0.7.9; in the history at tag v0.7.8) against the 310.8 snippet on an RTX 3090,
2560x1440 RGBA8, `--style 0 --intensity 1 --strength 1` as the baseline. "vs in" is the mean absolute
pixel difference from the input frame (0..255), "vs base" the difference from the baseline output.
Bit-identical means max difference 0 over the whole frame.

## Keys the snippet reads (from `--trace`)

Create time (only these force a feature rebuild): `DLSSNR.Width`, `DLSSNR.Height`,
`DLSSNR.Hint.Render.Preset`, `DLSSNR.ScalingRatio`, `CreationNodeMask`, `VisibilityNodeMask`.

Every evaluate: `Color`, `Depth`, `MVec`, `Output` (+ their subrects), `ControlMask`, `UI`, `UIAlpha`
(+ subrects, optional), `MVecScaleX/Y`, `Intensity`, `LocalToneStrength`, `LocalStructureStrength`,
`SkinStructureStrength`, `UseAutoMask`, `UICorrection`, `Style`, `Reset`, `DepthInverted`, `Enabled`,
`ScalingRatio`.

So intensity, style, the three strengths and the masks are runtime controls. Changing them in the shared
block before an evaluate takes effect on that evaluate (verified: intensity 0.5 set after create matches
intensity 0.5 set at create to 0.1/255; the residual is the snippet resetting its temporal history on a
control change). No rebuild, no hitch.

Present in the DLL but never read in this path: `Backbuffer`, `BidirectionalDistortionField` (+ subrects).
`GlobalToneStrength` is not a key of this model at all.

## Results

| Knob | Range that matters | Effect |
|---|---|---|
| `Hint.Render.Preset` 0..3 | none | Bit-identical. This build ships one weight set (`CC_Control_History_Blend_Quantize_With_Teacher_..._2026_07_04`); other presets fall back to it. |
| `Style` | 0, 1, 2 | Three distinct results (vs in 6.63 / 5.48 / 5.40). Values above 2 clamp to 2. |
| `Intensity` | 0..1 | 0 = bit-exact passthrough, 0.5 = about half the edit, 1 = full. Values above 1 clamp to 1, negative clamps to 0. |
| `LocalStructureStrength` | unclamped | 0 → 4.23, 0.5 → 5.35, 1 → 6.63, 2 → 6.52, 3 → 4.40, 5 → 4.37, 10 → 15.9, 20 → 58 (garbage). Negative values are accepted and produce a different, "anti-detail" result (-1 → 5.00, -3 → 3.64). |
| `LocalToneStrength` | unclamped | 0 → 3.33, 2 → 7.89, 3 → 8.14, 5 → 7.34, 10 → 12.8, 20 → 41 (garbage). Negative accepted (-1 → 2.49, -3 → 7.33). |
| `SkinStructureStrength` | -1 or 0.. | -1 = follow local structure (bit-identical to base). 0 and 3 change only a little on this frame (0.5 / 1.2 vs base): it only acts where the model believes there is skin. |
| `UseAutoMask` | 0/1 | Small change (0.56 vs base) with no ControlMask supplied. |
| `UICorrection` | 0/1 | Bit-identical without a `UI`/`UIAlpha` texture. |
| `Depth` | ignored | flat 0.5, 0, 1 and a vertical ramp are bit-identical, with and without motion vectors. This build does not use depth. |
| `MVec` | used | 0.5 px constant motion changes the result by 1.05 vs base: motion vectors warp the recurrent history. |
| `Reset` every frame | small | 0.73 vs base on a static image. The recurrence exists but is weak. |
| `ScalingRatio` 0.5..2 | none | Bit-identical and same GPU time. Read, but inert in this build. |
| Colour RGBA16F vs RGBA8 | tiny | 0.38 vs base (precision only). |
| Multi-pass (feed output back as Color) | 2..5 | Compounds: 2 passes 11.4, 3 passes 14.9, 5 passes 20.1 vs in. Each pass costs a full model run. |
| `ControlMask` (RGBA8, R8 or R32F, frame size) | per pixel | Where the mask is black the output is bit-identical to the input; where it is white the model applies. A single non-zero channel (R, G, B or A alone) counts as black; R8/R32F therefore act as "off". Supplying a mask replaces the auto mask (closest full-frame run: `UseAutoMask 0`). |

GPU time was 41-49 ms in every case: no knob changes the cost except multi-pass.

## What the addon does with this (2026-09-04; see architecture.md for the full design)

- Tuning is written at every evaluate; only the working scale rebuilds the feature.
- **Cost curve**: ~10 ms floor + ~7 ms/MP in-game (harness: 11-12 ms floor, 9.5 ms/MP at idle clocks, GPU at
  1950 MHz during the runs so the floor is the model's own work). Below ~1 MP the size barely matters; tiling pays
  the floor per tile.
- **Whole frame, always.** One model run per submitted frame on one D3D12 queue: downsample -> flow->mvec ->
  model -> delta (model - proxy, work-res RGBA16F, shared). The delta is applied to every frame LS presents, on
  LS's device, at present time (`src/addon/compose11.cpp`): frame + bilinear upsample of the delta sampled where
  the flow says that content sits in the presented frame, clamped, faded near white.
- **Free-running model, LS never waits.** The tap only copies the frame out; if the previous run is still on
  the GPU the frame is skipped and the present side carries the last delta forward with LSFG's flow. Past the
  frame interval the working scale costs freshness, not frame rate. (Replaced the inline/delayed scheduling of
  2026-09-03, which put the model's time or a full interval on LS's queue.)
- **Performance mode / scaling ratio (2026-09-04):** the Streamline 2.13 NR plugin sets `PerfQualityValue` and
  calls the snippet's `DLSSNRComputeScalingRatioCallback` (planted by `NVSDK_NGX_D3D12_PopulateParameters_Impl`,
  which the forwarder now calls after Init). On 310.8 every supported mode (0, 1, 2, 4, 5) answers ratio 1.0 and an
  evaluate whose Output is larger than Color fails with InvalidParameter: no model-side upsampling in this build.
  The 310.8.0 snippet from the DLSS 310.8.0 + Streamline 2.13 drop is a different build (other hash, same size) that
  refuses to create on a 3090; the one in the LS folder is the Ampere-capable one.
- Removed after in-game testing: the stale-delta async (morphed visibly through LSFG's interpolation), flat-area
  protection (per-texel steps became squares, then dots), the window (spent the budget on part of the frame),
  passes, masks, luma-only, delta smoothing, edge-guided upsample. All are in the git history if wanted back.
- `MVec` is LSFG's own flow (one frame stale at the tap; the model's recurrence is weak).

## Harness options used

```
nr_harness.exe in_1440p.png --only 1440p --iters 2 --style S --intensity I --strength X
    [--ls X] [--lt X] [--skin X] [--preset N] [--automask 0|1] [--uicorr 0|1]
    [--depth flat|ramp|zero|one] [--mv zero|small] [--scaling R] [--fmt rgba8|rgba16f]
    [--evalint X] [--evalstyle N]        # set after create, before the timed evaluates
    [--passes N] [--resetall]
    [--cmask r8|r32f|rgba8] [--cmaskch RGBA]   # left half = 1, right half = 0
    [--trace]                             # log every key the snippet reads
    [--perf N] [--perfscan]               # PerfQualityValue -> scaling-ratio callback -> DLSSNR.ScalingRatio
```
