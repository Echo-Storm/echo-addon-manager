# A DLSS 4.5 addon: what could work from Lossless Scaling's frames

Research for the roadmap item "a DLSS 4.5 addon next to Neural Rendering" (2026-09-23). Nothing is built yet.

## What DLSS 4.5 is

NVIDIA's DLSS 4.5 (January 2026) brought a second-generation transformer model for Super Resolution and Ray Reconstruction (about five times
the compute of the first transformer model), and Dynamic Multi Frame Generation with a 6x mode. In the SDK the new Super Resolution model is
presets **M** (the default for Performance) and **L** (Ultra Performance); DLAA, Quality and Balanced default to preset **K** (the DLSS 4
transformer). The public SDK this project already builds against (NVIDIA/DLSS 310.9.1, `tools/fetch_ngx_sdk.ps1`) has all of them.

## What Lossless Scaling gives an addon

| Input | What there is |
|---|---|
| Colour | The captured frame at the game's size, tone-mapped (LDR), with the HUD in it |
| Motion | LSFG's optical flow, a quarter of the frame's size, between consecutive real frames (Neural Rendering already turns it into motion vectors) |
| Depth | None (Neural Rendering feeds a flat depth) |
| Camera jitter | None: the game does not jitter its camera for us |
| Where the result goes | Added to each presented frame at present time, including LSFG's generated frames (moved along the flow), as Neural Rendering does |

## Feature by feature

| DLSS feature | Needs | Verdict |
|---|---|---|
| **DLAA** (Super Resolution at 1x) | colour, motion vectors (low-res allowed: `MVLowRes`), depth, jitter optional, exposure optional (`AutoExposure`) | **Feasible.** Everything but depth and jitter is there. It fits Neural Rendering's pipeline as it is: the same capture, flow and delta compose, with feature 1 in place of feature 18. |
| **Super Resolution** (upscaling to the output size) | the same, and the result must *replace* Lossless Scaling's scaled image | **Possible later, much harder.** LSFG makes its generated frames at the capture size and Lossless Scaling scales them afterwards, so DLSS would have to run on every presented frame (3 to 6 a real frame) or the real and generated frames would not match. |
| **Ray Reconstruction** | G-buffers (albedo, normals, roughness, depth) | **Not possible:** a captured frame has none of them. |
| **Frame Generation / Multi Frame Generation** | game depth and motion vectors, and control of the swap chain (Streamline) | **Not possible, and not wanted:** it is LSFG's job, and both cannot own the presentation. |

## The open questions only a test can answer

- **Quality without jitter and depth.** DLSS reaches past the native resolution by accumulating jittered samples. Without jitter, DLAA becomes
  a temporally stable anti-aliaser and reconstructor, still the main value for games with poor or no anti-aliasing. Flat depth takes away its
  motion vector dilation at edges. How it compares with the original frame has to be seen in a game. DLSS5ForAll's WoW run on 2026-09-19
  (Super Resolution on a captured window, flat depth) shows that it runs this way, not how good it looks.
- **Cost.** DLAA at 1080p with preset K should cost about the same as Neural Rendering at a similar working size. Preset M is heavier. Measure both.
- **The HUD.** The HUD is in the frame, so temporal accumulation can smear text that changes. The HUD editor's protected areas already handle
  this for Neural Rendering and would do the same here.

## Licence (NVIDIA RTX SDK licence, from github.com/NVIDIA/DLSS)

- Distribution is allowed "as incorporated in object code format into a software application" that has "material additional functionality"
  beyond the SDK, under terms "at least as protective" as NVIDIA's. Unlike `nvngx_dlssnr.dll`, NVIDIA's DLSS runtime (`nvngx_dlss.dll`) is
  meant to ship with applications, so an addon could legally include it. Asking the person for their own copy (as Neural Rendering does) also works.
- The SDK must not be made "subject to an open source software license" (for example one that makes it "redistributable at no charge").
  Our MIT code is fine, but the notices must say which files contain NVIDIA's code and that NVIDIA's licence covers them, not MIT.
- The application must "include the NVIDIA Marks ... in the about box of the application (if present)".
- Only for "systems with NVIDIA GPUs"; no reverse engineering.

**This also applies to what ships today.** `DLSS5NR01.dll` and `nr_selftest.exe` link NVIDIA's `nvsdk_ngx_s.lib`, so NVIDIA's object code is in
the release zip. NOTICE.md currently says the SDK is not "included, linked or distributed", which is wrong for the binaries. It should say that
those two files contain NVIDIA NGX SDK object code under NVIDIA's licence, and the About box should name NVIDIA DLSS.

## Suggested shape

A second **model** inside the existing addon ("DLSS 5 Neural Rendering" or "DLSS DLAA") rather than a separate addon: the capture, flow,
compose, HUD editor, screenshots, compare views, auto quality and the new background model building are all shared, and the engine change is
the feature, its create flags and its evaluate parameters. First step: a spike that runs DLAA (presets K and M) in the host test and then live in
WoW, judged with the existing split-screen compare.
