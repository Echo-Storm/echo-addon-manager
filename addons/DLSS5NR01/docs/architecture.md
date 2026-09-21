# Architecture

How the addon gets a neural-rendered frame onto the screen without Lossless Scaling ever waiting
for the model, and why each piece is built the way it is.

## The pipeline

Everything below happens inside the Lossless Scaling process, on the GPU that runs LSFG.

```
LS render thread (D3D11)                         model queue (D3D12, same adapter)

capture real frame k
  |
  v
LSFG pyramid pass on frame k  ---- TAP ---->     wait copyIn >= k
  copy frame k + LSFG flow into                  wait used   >= last present that read slot s
  the shared input, signal copyIn = k            downsample -> flow to mvec -> DLSSNR -> delta
  (skipped if the previous run                   into shared delta slot s, signal done = k
   is still on the GPU)
  |
LSFG flow + interpolation passes
  |
Present generated a  ---- PRESENT HOOK ---->     (no wait: newest delta with done >= its frame)
Present generated b       compose on LS's device: frame + delta warped by the flow,
Present real k            signal used after each compose
```

Three things make it work:

1. **The tap is read-only.** Lossless Scaling's textures are copied out, never written. LSFG does
   what it always does; the enhancement is added later.
2. **The model free-runs.** It has its own queue and skips frames it cannot keep up with. Its cost
   never appears on Lossless Scaling's queue.
3. **The compose is at present time.** Every frame LS presents, real or generated, gets the newest
   finished delta added on LS's own device, moved by LSFG's optical flow to where that content
   sits in the frame. A compose is one compute pass, about half a millisecond.

The delta for real frame k is normally finished before frame k is shown, so nothing is added to
the latency LS already has. If it is not, the delta of frame k-1 is warped forward by the flow.
A working scale past the frame interval therefore costs freshness of the enhancement, not frame
rate.

## Components

### DispatchHook (`src/addon/dispatch_hook.cpp`)

An inline code hook (MinHook) on every implementation of `ID3D11DeviceContext::Dispatch` inside
`d3d11.dll` (five entry points on current Windows builds). This is how the addon sees every compute
pass Lossless Scaling issues on every device in the process.

Why not the context vtable, which is what the manager's own dispatch callback patches: each
context carries a private copy of its vtable, and `ID3D11Multithread::SetMultithreadProtected`
rewrites that copy with a different set of entry points, dropping any patched slot. Lossless
Scaling enables multithread protection through Windows.Graphics.Capture, so vtable patches never
fire.

The addon's own compose dispatches are marked with a thread-local flag so the hook ignores them.

### FrameTap (`src/addon/frame_tap.cpp`)

Classifies dispatches by shape: thread-group counts plus the size and format of every bound SRV
and UAV. Shader pointers are matched live but never stored, because they change with every
device. Each shape is a row in a table the panel shows.

The **TAP** is LSFG's per-real-frame pyramid pass: it reads a full-size colour texture and writes
only smaller outputs. Among such passes the one that runs once per real frame is the rarest, so
after enough samples the rarest qualifying shape wins; early on, the most frequent one is used.
Only recently seen shapes count, so a resolution change re-learns cleanly. Manual mode pins a
shape from the panel.

The tap also tracks LSFG's finest optical-flow texture (the first finest-level RGBA16F UAV pass
with a coarser level bound after each tap) and LSFG's interpolation composes (a full-size colour
UAV written from two full-size colour SRVs, or from an RGBA16F SRV). The latter is what makes a
present "generated".

**Present bookkeeping.** Per real frame the tap counts presents and learns two things: how many
presents LS makes per real frame (X2, X3, X4) and whether the real frame is presented first or
last. From that, each present gets a target position in real-frame units: real frames land on
k or k-1, generated frames on k-1 plus i/N. The panel shows the learned pattern, for example
`3 per real frame, real frame last`.

### Bridge (`src/addon/bridge.cpp`)

The seam between Lossless Scaling's D3D11 device and the model's D3D12 queue on the same adapter.
NT-shared textures are created on the D3D11 side and opened by D3D12: one shared input frame, one
shared flow copy, and three delta slots (RGBA16F, work resolution). Three shared fences:

| Fence | Signalled by | Waited on by | Value |
|---|---|---|---|
| copyIn | LS's context, after the frame copy | the model queue before a run | frame index |
| done | the model queue, after the delta is written | LS's context before a compose (instant when the delta is finished) | frame index |
| used | LS's context, after each compose | the model queue before overwriting a slot | a running counter |

All waits are GPU-side. The CPU on LS's render thread only records a copy and a signal (one to
two milliseconds), then returns.

`Submit` skips the frame if the previous run's `done` value has not been reached. `NewestDelta`
hands the present side the highest finished frame index and its SRV. Three slots, round robin,
so a run never overwrites the delta a present still reads and the present side never reads a
half-written one.

`SetLsGpuPriority` raises LS's D3D11 device to GPU thread priority +7 (the *LS's GPU work first*
setting), so LSFG's passes and presents pre-empt the model's normal-priority queue.

### NrEngine (`src/engine/nr_engine.cpp`)

The D3D12 sidecar. It initialises the NGX driver core on the LSFG adapter, obtains the capability
parameter block, loads the forwarder, probes which parameter-block setter slot actually stores
floats (the public header order is not what the live block uses), and creates DLSSNR feature 18
at the working resolution.

One run is one command list: 4-tap box downsample of the frame to the working size, LSFG flow to
motion vectors, the model, then `delta = model - proxy` into the shared slot. The engine also
records timestamps and maps them to QPC with `GetClockCalibration`, so the panel can show how
long after the CPU submit the GPU actually started and finished the run.

### Forwarder (`src/forwarder/nr_forwarder.cpp`)

`nvngx.dll_dlss5nr01.dll` is the only module that calls the snippet. The snippet checks the module
name of its caller and rejects anything whose path does not contain `nvngx.dll`; this DLL's name
satisfies that check. Every snippet result is stored through a volatile before returning, so the
compiler cannot turn the call into a tail jump that would make the snippet see a different caller.

It exposes a small C API: probe, init, create, evaluate, release, the float-slot probe, and the
snippet's scaling-ratio callback. The contract is in `src/forwarder/nr_api.h`.

### PresentHook (`src/addon/present_hook.cpp`)

Patches slots 8 (`Present`) and 22 (`Present1`) of the swap chain vtable. The table is found by
creating a throwaway swap chain of the same class on LS's device; every swap chain of that class
shares the same static table inside `dxgi.dll`, and it is not refreshed per object.

Why not an inline code hook like the dispatch hook: something else in the process (the NVIDIA
overlay installs its own Present hook lazily at the first present) patches `dxgi!Present` after
the addon and chains to a pristine copy of the code, silently removing an inline hook installed
before it. A patched vtable slot keeps working and calls whatever code hook sits on the function,
so overlays are unaffected.

### Compose11 (`src/addon/compose11.cpp`)

One compute pass on Lossless Scaling's own D3D11 device per presented frame:

```
suv = offset > 0 ? uv - offset * flow.zw * unit : uv + offset * flow.xy * unit
d   = delta.Sample(suv) * strength, clamped to +-maxDelta, faded above hiProtect
out = saturate(frame + d)
```

`offset` is the presented frame's target position minus the delta's frame, in real-frame units.
Positive offsets move the delta forward along LSFG's previous-to-current field (`zw`); negative
ones move it back along current-to-previous (`xy`). The pass reads a copy of the back buffer and
writes the back buffer directly when it has UAV access, otherwise through a scratch texture and a
copy. Shader state is saved and restored around it.

## Threading and locks

The dispatch hook and the present hook both run on Lossless Scaling's render thread; the engine
starts on a worker thread. Everything below the hooks (tap, compose, bridge and engine teardown,
device bookkeeping) runs under one mutex. Settings are copied out under a second mutex at the
start of each tap and present, so the panel never blocks the render thread.

Both hook bodies are wrapped in SEH. Any exception disables the addon with a message in the panel
and the log; *Re-arm* turns it back on.

## Device selection

Lossless Scaling creates D3D11 devices on every adapter. The addon classifies each device once
when it first dispatches: only NVIDIA adapters are candidates. The engine starts on the adapter
of the device that issues the tap, so it follows LSFG on a single GPU, a hybrid laptop, or either
card of a dual-GPU rig. Switching adapters requires the new one to keep issuing the tap for 20
frames while the old one stays silent, so a device change does not bounce the engine. An adapter
on which the engine failed is not retried automatically.

## Performance model

Measured on an RTX 3090 (FP16 path): about 10 ms per run plus 7 ms per megapixel of model input,
independent of image content. Below one megapixel the size barely matters, which is why the
working scale sits between 0.3 and 0.5 on Ampere for 1440p-class frames. Tiling does not help:
the floor is paid per tile.

The model shares the GPU with LSFG and, on a single-GPU rig, with the game. Hardware-accelerated
GPU scheduling time-slices between the queues at dispatch boundaries, so a long model kernel can
delay an LSFG pass by up to its own length. Raising LS's GPU priority makes LS's work win those
slices; lowering the working scale shortens them.

## Decisions that did not survive

Kept in git history, removed because they either put model time on LS's queue or looked wrong
through LSFG's interpolation:

- Inline mode (model in place at the tap, LS waits for it) and delayed mode (previous frame's
  delta resolved in place): both stalled LS whenever the model overran the interval.
- Applying a stale delta in place, without present-time warping: LSFG interpolated the model's
  edit as content and it morphed visibly.
- Flat-area protection, a processing window, multi-pass, control masks, luma-only, delta
  smoothing, edge-guided upsample: each cost quality or budget for no measurable gain.
- Model-side upsampling: the 310.8 snippet answers scaling ratio 1.0 for every performance mode
  and rejects an output larger than its input.

## Testing

`nr_hosttest` (`tools/addon_host_test.cpp`) loads the built addon with a fake host, headless
ImGui and a synthetic LSFG dispatch pattern on a real flip swap chain, presenting generated,
generated, real like LSFG X3. It checks that the tap leaves LS's frame untouched, that the compose
lands on presented frames, and that a resolution switch is followed.

`nr_harness` (`src/harness/nr_harness.cpp`) runs the model without Lossless Scaling on an
image at 1080p, 1440p and 4K, GPU-timed, and drives every parameter the snippet reads. Its
findings are in [dlssnr-knobs.md](dlssnr-knobs.md).
