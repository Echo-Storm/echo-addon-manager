# The DLSS 4 and FSR 3 Upscalers

Two addons, built from Neural Rendering's sources, that take the place of Lossless Scaling's **NIS** scaler with a temporal upscaler:

| Addon | Upscaler | Needs | Comes with |
|-------|----------|-------|------------|
| **DLSS 4 Upscaler** (`DLSS4DLAA`) | NVIDIA DLSS Super Resolution (models K and M) | an NVIDIA RTX card | NVIDIA's DLSS runtime 310.9.1 (`dlss\nvngx_dlss.dll`, NVIDIA's licence) |
| **FSR 3 Upscaler** (`FSR3UPSC`) | AMD FidelityFX Super Resolution 3.1 | any DirectX 12 card (AMD, NVIDIA, Intel) | AMD's FidelityFX runtime (`fsr\amd_fidelityfx_dx12.dll`, FidelityFX SDK v1.1.4, MIT, signed by AMD) |

Both are a **preview** (0.9.1, work in progress): they come in the download switched off, and so far they were developed and tested with
World of Warcraft: Forever only, on an RTX 4070 Ti SUPER.

A temporal upscaler builds each picture from several frames, so it has to know where every pixel was in the frame before. A game with DLSS or
FSR built in tells it; Lossless Scaling does not. These addons **measure that motion from the frames themselves**, so they work in any game
Lossless Scaling can scale, with frame generation on or off, and need nothing from the game.

## Setting up

1. In the manager's addon list, switch on **one** of the two. They take the same pass, so turning one on turns the other off. Either works
   beside DLSS 5 Neural Rendering.
2. In Lossless Scaling's profile for the game, choose **NIS** as the Scaling Type. The addon replaces exactly that pass; with another scaler
   it waits and does nothing.
3. Run the game **in a window smaller than the screen**, so there is something to upscale. 2560x1440 on a 4K screen is 1.5x, the upscalers'
   quality mode and the best starting point; 1920x1080 on 4K (2x) works too, softer. A borderless window the size of the screen (for example
   World of Warcraft's *Windowed (Fullscreen)* at 3840x2160 on a 4K screen) gives 1:1: nothing is upscaled, and the addon runs as
   anti-aliasing (DLSS's DLAA, FSR's native AA). Avoid exclusive fullscreen: Lossless Scaling cannot scale over it.
4. Frame generation can be on or off.
5. Scale the game as usual. The addon's panel (Upscaling) shows `DLSS upscales 2560x1440 -> 3840x2160 (x1.50) ...` (or `FSR 3 upscales ...`) once it runs.

In-game settings that help: a cheap post-process anti-aliasing (CMAA2 or FXAA) smooths edges before the upscaler sees them; heavy
multisampling (MSAA 4x/8x) costs a lot and adds little here; keep the game's own render scale at 100% so it does not upscale first.

## Settings (the addon's panel)

- **DLSS model** (DLSS 4 Upscaler only): *NVIDIA's default (K)* or *M (DLSS 4.5)*. M is heavier (about twice K's cost) and differs in how it
  treats fine detail; compare them in your game.
- **Sharpening** (0.3 to start with): DLSS 4 has no sharpening of its own, so the DLSS addon sharpens its picture with the contrast-adaptive
  (CAS) formula; the FSR addon uses AMD's own RCAS. NIS sharpens too (Lossless Scaling's Sharpness), so without it the upscaler can look
  softer next to NIS. 0.2 to 0.6 is the useful range. Ctrl+Shift+F8 / F9 lower and raise it in the game.
- **Motion**: *Measured from the frames* (the default), *Lossless Scaling's frame generation* (its flow: coarser, a quarter of the game's size,
  and only with frame generation on), or *None* (the upscaler assumes nothing moves: sharp when still, smeared when the camera turns; there to
  compare).
- **Stability** (off to start with): less shimmer on thin lines, wires and leaves, for a little more trailing behind what moves. The motion
  measurement then judges trust by a pixel's surroundings rather than the pixel itself, so flicker is no longer reported as "do not trust the
  history here" and the upscaler averages it out; FSR 3 also keeps more history and reacts less to small shading changes (AMD's tuning keys).
  Real motion is followed as before. 0.5 is a good start.
- **Before / after** (Ctrl+Shift+F6): switches between the upscaler and Lossless Scaling's own NIS while you play. The hotkeys work only while
  the upscaler is actually upscaling.

## What it costs

Measured on an RTX 4070 Ti SUPER in World of Warcraft: Forever (GPU time a presented frame, everything the addon does):

| | DLSS 4 (model K) | FSR 3 |
|---|---|---|
| 2560x1440 -> 3840x2160 | about 2.4 ms (before the motion estimate got cheaper) | about 1.85 ms |
| 3840x2160, 1:1 (anti-aliasing) | about 3.2 ms (the same) | about 2.2 ms |

Of that, the motion estimate is about 0.35 ms at 2560x1440 and 0.7 ms at 3840x2160. The panel and the log show the addon's own numbers.
The addon adds one frame of latency: the picture shown is the one the upscaler finished for the frame before, so Lossless Scaling never waits.

## What to expect

- **In motion** the measured motion makes the difference: without it, anything moving smears. With it, the picture holds together while the
  camera turns, and where the motion cannot be trusted (background just uncovered, particles, transparent effects) the upscaler is told to
  lean on the current frame instead of its history.
- **Fine detail**: a game with DLSS built in shifts its camera by a fraction of a pixel every frame, which lets DLSS rebuild detail finer
  than the render size. A captured frame has no such shifts (and no depth), so the upscalers work from what each frame shows; camera motion
  gives them some of the same variety. The same goes for anti-aliasing: a line thinner than a pixel is simply missing in parts of the frame,
  and no upscaler can draw it back. Turn on the game's own anti-aliasing (MSAA) if it has one: in Fallout: New Vegas 8x (`iMultiSample=8`)
  made a large difference, and the upscaler on top of it looks better again. Stability then calms what shimmer is left.
- **Text and HUD** are part of the captured frame, so the upscaler treats them like the rest of the picture; thin text can come out a little
  softer or dimmer than with NIS (FSR 3 keeps it crisper than DLSS). A game with DLSS built in draws its HUD after upscaling. Keeping NIS for
  HUD areas is on the roadmap.
- On a clean, low-detail game (World of Warcraft) the difference to NIS is small. Games with foliage, fine detail and busy motion show it more.
- **A window of another shape than the screen** (a 4:3 game on a 16:9 screen): Lossless Scaling scales it into part of the screen, and the
  upscalers leave that to NIS for now.

## When something is wrong

The addon writes `logs\DLSS4DLAA.log` or `logs\FSR3UPSC.log` in the Lossless Scaling folder.

| What you see | Why, and what to do |
|--------------|---------------------|
| The panel says it is waiting for the NIS pass | Lossless Scaling's Scaling Type is not NIS, or scaling has not started. |
| No difference at all | At 1:1 (the game fills the screen) there is nothing to upscale; run the game in a smaller window. Check the panel says it upscales, and try the Before / after hotkey. |
| The panel keeps waiting although NIS is chosen | The game window's shape differs from the screen's (a 4:3 game on a 16:9 screen, or a window dragged to an odd size): Lossless Scaling then scales into part of the screen, which the upscalers do not take yet, so NIS runs. Give the game a window of the screen's shape (2560x1440 on a 3840x2160 screen). |
| A black picture | Should not happen since 0.9.1. The log's `probe:` lines say how bright the frame the upscaler got and the picture it made are: 0 of 255 means black. Please report it with the log. |
| Smear when moving | Check Motion is *Measured from the frames*. The log's `motion estimator:` lines give the average motion found and how much of the picture was marked untrusted. |
| "... could not run: ..." in the panel | The runtime is missing from the addon's `dlss` or `fsr` folder (reinstall the addon), or, for DLSS, the card is not an NVIDIA RTX card. NIS runs as usual meanwhile. |

## How it works

On Lossless Scaling's side (`src/addon/scaler11.cpp`), at the NIS pass (recognised by what is bound: the frame, NIS's two 2x64 coefficient
tables, the picture at the screen's size, one thread group per 32x24 pixels), the addon reads the frame into a shared texture with a small
compute pass through the NIS pass's own view of it, signals a shared fence, copies the newest finished picture into the pass's output and
skips NIS. Everything else runs on a Direct3D 12 device of the addon's own (`src/engine/sr_engine.cpp`), which waits for that fence on the GPU:

1. **The motion estimate** (`src/engine/flow_estimator.cpp`): the frame's brightness at its own size and halved down to about 64 pixels
   wide; from the smallest size up, every 4x4 block finds where its 8x8 surroundings were in the frame before (seeded from the size below,
   with a small cost for straying from that guess); a fraction of a pixel at half size; a 3x3 vector median; and, at the game's size, every
   pixel picks the best of "not moving", its own block's vector and the three nearest blocks' by how well its 3x3 surroundings match. The
   same step gives the **distrust mask**: where even the best vector leaves a pixel unlike the frame before.
2. **The upscaler**: DLSS (NVIDIA's NGX API) or FSR 3.1 (AMD's FidelityFX API), given the frame, the motion vectors, a flat depth and the
   distrust mask (DLSS's bias-toward-current-colour mask, FSR's reactive mask), with no camera jitter.
3. The DLSS addon's sharpening pass (FSR sharpens inside its own pass).

Why it is built this way, briefly: NVIDIA's D3D11 DLSS run on Lossless Scaling's own device crashed it, so the upscaler has a device of its
own; making Lossless Scaling's queue wait for the upscaler is avoided (one frame of latency instead); and with frame generation off the NIS
pass reads Lossless Scaling's capture directly, a keyed-mutex texture shared from its capture device, which a plain copy read as black, so the
frame is read the way NIS reads it. `scalerHandoff` in the addon's config (0 the default; 1 GPU wait; 2 upscaler runs but NIS stays; 3 NIS
runs and the picture is pasted at Present) keeps the variants that found this. [architecture.md](architecture.md) has the details.
