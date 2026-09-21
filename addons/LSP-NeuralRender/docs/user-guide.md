# User guide

This is the long version of the README's install section: what to expect on first run, what
every setting does, how to pick the working scale, and what to do when something is off.

## What this addon is, and is not

It runs NVIDIA's DLSS 5 Neural Rendering model on the frames Lossless Scaling has already
captured, inside the Lossless Scaling process, on the GPU that runs LSFG. It never touches the
game: no files in the game folder, no hooks in the game process, nothing for an anti-cheat to see.
The game does not need DLSS, motion vectors or any particular API.

It is not DLSS upscaling. The 310.8 DLSSNR build does no upsampling and ignores depth. It is a
style and detail model that edits a finished image, and the addon feeds it the image Lossless
Scaling is about to show.

## Requirements

- Lossless Scaling 3.x and a working Echo Addon Manager install (it opens, other addons
  load).
- An NVIDIA RTX GPU for LSFG. On a dual-GPU rig that is the display card; the game GPU can be
  anything. RTX 30 was the development hardware; RTX 40/50 should be faster but were not tested.
- `nvngx_dlssnr.dll`, the DLSSNR snippet. Not included, not linked, not distributed by this
  project. Two builds of version 310.8.0 circulate; only one of them creates its feature on
  Ampere (see *Engine errors* below).

## Install

1. Extract the release zip. Copy `addons\LSP-NeuralRender` into the Lossless Scaling folder, so the
   folder holds `LSP_NeuralRender.dll`, `nvngx.dll_lspnr.dll` and `addon.json`.
2. Put `nvngx_dlssnr.dll` next to `LosslessScaling.exe`. If you keep it elsewhere, set its full
   path under *Advanced > Snippet path* later.
3. Start Lossless Scaling, open Echo Addon Manager and enable *Neural Render
   (DLSS 5)*.

To update, replace the two DLLs while Lossless Scaling is closed. To uninstall, delete the folder.
Settings live in Echo Addon Manager's `addons\config.json` under the key `LSP-NeuralRender`; delete
that block to reset them.

## First run

Start scaling a game with LSFG on and open the addon's settings. The status line at the top
tells you where the addon is:

| Status | Meaning |
|---|---|
| waiting for device | Lossless Scaling has not created its D3D11 device yet. Start scaling. |
| waiting for LSFG dispatches | Device seen, no LSFG passes yet. Scaling must be active. |
| engine: loading model... | The model is being created on the GPU that issued the LSFG pass. A few seconds. |
| running | The model runs and the compose lands on presented frames. |
| unsupported frame format | The captured frame is not 8-bit RGBA/BGRA (for example HDR). |
| DISABLED: ... | The addon switched itself off. See *Troubleshooting*. *Re-arm* turns it back on. |

Below the status line, *frame* shows the captured size and format, *NR* the model time of the
last run and its average, *taps* how many real frames were seen, and the d3d11 hook line how many
entry points are hooked (5 on current Windows builds).

The log at `<Lossless Scaling>\logs\LSP_NeuralRender.log` is rewritten on every start. Every 300
frames it writes one line with the model time, how far behind the submit the GPU started and
finished, the frame interval, how many runs were made and skipped, the present pattern, and the
newest delta's frame and offset. NGX's own logs land in the addon folder.

## Settings

Every control has a tooltip: hover it for a moment. Some older text in this guide and in the changelog calls *Model
resolution* the *working scale*, *Fine detail strength* "local structure", *Local contrast* "local tone" and *Blend
amount* "apply strength".

Below the sliders, **Restore defaults** (next to *Reset history*) puts the look and quality sliders back to the addon's
defaults without touching presets, hotkeys or the advanced settings.

Everything applies on the next frame. Only *Working scale* re-creates the model's feature, which
takes a moment and resets its temporal history.

### Look (what the model does to the picture)

| Control | Default | What it does |
|---|---|---|
| Style | Standard | The model's three looks: Standard, Natural, Cinematic. |
| Model intensity | 1.00 | How much of the model's edit is produced. 0 is a bit-exact passthrough. The model clamps at 1. |
| Fine detail strength | 1.00 | Fine detail strength. Unclamped: values above ~5 degrade, negatives invert the edit. |
| Local contrast | 1.00 | Local contrast strength. Same range behaviour as local structure. |
| Skin and face detail | same as fine detail | Detail strength where the model thinks there is skin. -1 follows *Local structure*. |
| Detect skin automatically | on | The model's own skin detection. |
| Use Lossless Scaling's motion data | on | Feeds LSFG's flow to the model as motion vectors, and uses it at present time to move the delta onto generated frames. Turn it off only to diagnose the flow; the delta then stays put on generated frames. |

What each of these measurably does, knob by knob, is in [dlssnr-knobs.md](dlssnr-knobs.md).

### Quality and performance

| Control | Default | What it does |
|---|---|---|
| Model resolution | 0.35 | The frame is shrunk by this before the model sees it. The only cost lever. |
| Model passes | 1 | How many times the model reworks each frame (1 to 4); every pass takes the previous result as its input, so the effect compounds. Each extra pass costs roughly another model run: watch the model time and the "keeps up with" line. If the model cannot keep up it skips frames and the last result is carried forward. Compare with the Before / after hotkey: more passes can look over-processed. |
| Give Lossless Scaling GPU priority | on | Raises Lossless Scaling's D3D11 device to GPU thread priority +7 so LSFG's own passes and presents pre-empt the model on the shared GPU. Leave it on unless you are measuring. |
| Blend amount | 1.00 | How much of the delta is added at present time. Above 1 exaggerates the model's edit. |
| Ghost guard | 0.50 | Stops the faint copy of the previous frame that can trail moving things. The delta is made from an older frame and moved onto the current one with Lossless Scaling's motion data; where that data is unreliable (the edge of a moving object, something just uncovered) it lands in the wrong place. The guard fades the delta there, and a little more the older the delta is, and leaves still and steadily moving areas alone. 0 = off, 1 = strongest. *Diagnostic view > Ghost guard* shows where it acts (dark = faded). |
| Limit per-pixel change | 0.50 | Clamp on the per-channel delta (0..1 scale). Limits how far one pixel may move. |
| Protect bright areas from | 0.85 | Fades the delta out as the source luminance rises from here to white. Keeps the model from crushing bright areas. 1.00 turns it off. |
| Temporal smoothing | off | Blends the model's change for this frame with its change for the previous one, moved along with the picture by Lossless Scaling's motion data. Calms shimmer and crawling in fine detail (distant roads, fences, foliage) at the price of a little softness or ghosting when the camera moves fast. 0 is off; try 0.3 first. Costs almost nothing (one work-resolution pass). |
| Sharpen | off | Contrast-adaptive sharpening (the FidelityFX CAS formula) of every presented frame, applied after the delta is added. The model and the upscale both soften; 0.2-0.4 puts the bite back. It costs nothing measurable: it is a few more taps in the compose pass that already runs. |
| Saturation | unchanged | Colour intensity of the finished picture, applied last to real and generated frames alike. 1.00 leaves it alone, 0 is black and white, above 1 is more vivid. Costs nothing measurable. |
| Vibrance | off | Like Saturation, but it lifts muted colours much more than vivid ones, so skin tones and already-strong colours are not pushed further. The gentle way to add colour. |
| Brightness | unchanged | Adds or subtracts the same amount from every pixel. Simple, but it lifts blacks too and can wash the picture out; try Gamma first. Applied on the picture's own values, like a monitor's brightness control. |
| Contrast | unchanged | Pushes the picture away from mid-grey (above 1) or toward it (below 1). Extremes can clip as it rises. |
| Gamma | unchanged | Bends the mid-tones without moving pure black or pure white. Above 1 brightens the mid-tones, below 1 darkens them. Usually the best brightness control. |
| Shadows | unchanged | Works on the dark parts of the picture only: above 0 lifts them, below 0 deepens them. Bright areas stay as they are. |
| Highlights | unchanged | Works on the bright parts only: below 0 pulls them down (recovers sky and glare), above 0 pushes them up. |
| Film grain | off | Fine monochrome noise, strongest in the mid-tones, different on every presented frame. Keep it low (0.1 to 0.3). |
| Grain size | 1 px | The size of a grain speck in screen pixels (1 to 4). |
| Diagnostic view | Result | *Original* shows the frame untouched, *Delta x4* the delta amplified, *Frame role* tints real frames green and generated frames red, *LSFG flow* shows the flow field. |

The lines above the controls are live: the model input size in megapixels, the measured model
time next to the estimate (10 ms + 7 ms per megapixel on Ampere), the frame interval, how many
frames the model keeps up with, how long after the submit the GPU started and finished, and how
many presents were composed with which pattern. A warning appears when the model runs on fewer
than half of the frames.

### Keep the HUD untouched

Up to six rectangles (given as fractions of the screen: left, top, right, bottom) inside which the picture stays exactly as
Lossless Scaling made it: no model change, sharpening, tone, colour or grain. Use them for the action bars, minimap, chat and
any text the model might soften. *Edge softness* fades the enhancement in over a few pixels outside a rectangle instead of a
hard edge. *Show the areas on screen* tints and outlines the rectangles in green so they can be lined up with the HUD (display
only, not saved). *WoW starter layout* fills four rectangles where World of Warcraft's default interface sits; it is a starting
point, not a measurement of anyone's own layout. The rectangles are saved with presets, so each game can have its own.

### Compare and hotkeys

Judging the model by eye is hard without a reference, so the addon can show the original beside (or instead of) the
enhanced frame. It only changes what is displayed; the model keeps running, so switching is instant. The choice is
not saved: Lossless Scaling always starts on the enhanced view.

| Control | What it does |
|---|---|
| Compare view | *Enhanced*, *Split* (left of the line original, right enhanced) or *Original only*. *Original only* skips the compose pass altogether. |
| Split position | Where the split line sits, as a fraction of the width. |
| Hotkeys | **Ctrl+Shift + F-key**, polled at every present, so they work while the game has focus (the modifier pair keeps them off the game's own bindings). Defaults: **F6** before/after, **F7** split view, **F8** / **F9** sharpen down / up by 0.05, **F10** next preset. The keys are configurable (F1-F12); the panel lists the current bindings and says when hotkeys are switched off. |

There is no overlay in the game, so a hotkey shows a small coloured square in the screen's top-left corner for about a
second: **green** enhanced, **red** original only, **amber** split, **blue** sharpen changed, **purple** preset applied. Actions are also written
to `LSP_NeuralRender.log` (`hotkey: ...`). Hotkeys are read at present time, so they do nothing while Lossless Scaling
is not presenting a frame.

### Saved looks (presets), at the top of the panel

The row at the top is the quickest way to change the whole picture: **Look** lists your saved looks (pick one to apply it), **Save** updates the
selected look with the sliders as they are now (or asks for a name if you are on *Custom*), **Save as new** keeps the current sliders under a new
name, and **Delete** removes the selected look after asking. The label shows *(changed)* when the sliders no longer match the selected look. The
older description below still applies to what a look contains.

### Presets

A preset is a named look: style, intensity, the local strengths, the skin and mask settings, optical-flow use, working
scale, apply strength, max delta, highlight protection and sharpen. **Save current as preset** stores the sliders as they
are now (saving under an existing name overwrites it); **Apply** loads one; **Delete** removes it. The **next preset**
hotkey (default Ctrl+Shift+F10) cycles through them in order, which is the quickest way to judge two looks in the game.
Compare, hotkey and tap settings are not part of a preset. A preset with a different working scale makes the model
rebuild, which costs a short hitch on the frame it happens.

Presets are stored in `addons\config.json` under this addon (`presetNames` plus one `preset.<name>` string each). Since 0.4.0
a preset also carries shadows, highlights, grain, temporal smoothing and the HUD rectangles. Presets saved by an older version
simply leave those settings as they are when applied.

### Games (a look per program)

The panel shows the program that has focus (Lossless Scaling's own windows are ignored, so while a game is scaled this is the
game). Link a program's exe name to a preset and, with *Switch to a program's saved look when it takes focus* on, that preset is
applied when the program takes focus (once per change of program, so tweaks you make afterwards stay). *Save the current look for
the program in focus* stores the sliders as a preset named after the program and links it in one step; programs can also be added
by typing the exe name. Matching ignores case. Applying a preset with a different working scale makes the model rebuild for a
moment. The focus is polled every few dozen presents, so a switch takes a fraction of a second. Stored as `gameList` and
`game.<exe>` keys next to the presets.

### Choosing the working scale

1. Note the frame interval (the game's frame time as Lossless Scaling sees it).
2. Raise *Working scale* until the model time approaches the interval. The keep-up line should stay
   near 100%.
3. If the game or LSFG lose smoothness, back off one step. The model shares the GPU with LSFG;
   even with the priority raised, a run that fills the whole interval leaves LSFG less room.

A model that is late does not slow anything down. The present side keeps using the newest
finished delta, moved by the flow, so the enhancement lags the image by a frame or two. That is
the trade past the interval: freshness, not frame rate.

### Frame detection (advanced)

Which of Lossless Scaling's compute passes is treated as "a new real frame arrived". *Auto* picks
the pass that reads a full-size colour texture and writes only smaller outputs (LSFG's pyramid
pass), and it re-learns after a resolution change. The table lists every dispatch shape seen, with
its count and views. *Manual* with the *TAP* button pins a row; *Clear roles* goes back to auto.
*Frame slot* overrides which of the pass's input textures is taken as the frame.

You should not need this unless a future Lossless Scaling version changes its pipeline.

### Advanced

| Control | Default | What it does |
|---|---|---|
| Slow-model watchdog (ms) | 80 | If the model takes longer than this for 30 frames in a row the addon disables itself. Raise it if you deliberately run a large working scale. |
| Model file path | blank | Full path to `nvngx_dlssnr.dll`. Blank means the Lossless Scaling folder. |
| Restart engine | | Tears the model down and starts it again on the current LSFG adapter. |
| Dump flow probe | | Diagnostic: writes the next three tapped frames and LSFG's flow textures to the Lossless Scaling folder. |

## Troubleshooting

**The status stays at "waiting for LSFG dispatches".** Frame generation must be on in Lossless
Scaling and the game must be scaling. The addon only sees compute dispatches; with LSFG off there
is nothing to tap.

**"DISABLED: NR slower than watchdog threshold for 30 frames".** The working scale is too high
for the GPU. Lower it, press *Re-arm*, or raise the watchdog.

**"DISABLED: could not hook d3d11 Dispatch" or "could not hook dxgi Present".** Another tool in
the process replaced the hook points in a way the addon cannot chain to. Check the log for which
step failed. Overlays that hook Present (the NVIDIA overlay, RTSS) are expected and work; the
addon uses the swap chain vtable precisely so they do.

**Engine errors** (red line in the panel and `NrEngine FAILED:` in the log):

| Message | Cause |
|---|---|
| snippet probe 0x... | `nvngx_dlssnr.dll` was not found or is not a DLSSNR snippet. Check the path. |
| snippet Init_Ext: ... | NGX refused to initialise on this adapter. Virtual display adapters and non-NVIDIA cards do this. |
| CreateFeature(18): FeatureNotSupported | This snippet build does not create its feature on your GPU. On Ampere, only one of the two circulating 310.8.0 builds works. |
| adapter LUID not found | The GPU that ran LSFG disappeared (device change). The engine restarts on the next tap. |

**Presents are counted but nothing is composed.** Look at the log line at tap 60: *present
stages* shows how far each present gets. If *targeted* is zero the tap has not seen a real frame
yet; if *with delta* is zero the model has not finished a run.

**A faint copy of the previous frame trails moving things (only with Neural Rendering on).** The delta comes from an older frame and is moved by
Lossless Scaling's motion data, which is coarse and wrong at object edges. Raise *Ghost guard* (0.5 is the default; try 0.7 to 1), then lower *Limit per-pixel change*
and *Fine detail strength*, and keep *Temporal smoothing* at 0 to 0.3. A smaller *Model resolution* makes the delta fresher. *Diagnostic view > Ghost guard* shows where
the guard fades the delta; the *applied at offset* number under *Technical status* is the delta's age in frames.

**The image morphs on generated frames.** Turn on *Debug view > LSFG flow* to confirm
the flow is being seen (the panel shows its size next to the flow checkbox). If it reads "no flow
texture seen yet", the tap did not identify LSFG's flow pass; try *Clear table* and let it
re-learn.

**The picture looks over-sharpened or noisy.** Lower *Local structure* first, then *Max delta*.
*Intensity* scales the whole edit.

**Lossless Scaling itself got choppy.** Check *LS's GPU work first* is on. Then lower the working
scale: even a prioritised model run competes for the same GPU, and on a single-GPU rig it competes
with the game as well.

## Dual-GPU notes

The addon follows the adapter that runs LSFG. Set Lossless Scaling's *Preferred GPU* to the display
card; the model then runs there and the game GPU is untouched. If Lossless Scaling briefly runs
passes on both cards during a device change, the engine waits until one adapter keeps issuing the
tap before it moves.
