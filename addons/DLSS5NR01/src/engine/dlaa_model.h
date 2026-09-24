// DLAA: NVIDIA's DLSS Super Resolution at 1x, through the NGX SDK and NVIDIA's own runtime (nvngx_dlss.dll, which ships with the addon under
// NVIDIA's licence). The functions have the shape of the Neural Rendering forwarder's (nr_api.h), so the engine runs either model the same way:
// the frame at the working size in, the model's picture out, and the difference added to each presented frame.
//
// What Lossless Scaling gives is not what DLSS is made for, and it is fed as follows:
//   * no camera jitter: the offsets are 0, so DLSS smooths and steadies edges but cannot add detail beyond the frame's own;
//   * no depth: a flat depth (the engine's);
//   * motion: LSFG's optical flow in working-size pixels (the engine's motion vectors, as Neural Rendering gets them);
//   * the frame is tone-mapped (LDR), with the HUD in it: no HDR flag, exposure 1.
#pragma once
#include "forwarder/nr_api.h"

namespace dlaa {

// The DLSS preset for DLAA: 0 = NVIDIA's default for DLAA (K, the DLSS 4 transformer), or a preset letter's number (K = 11, M = 13: DLSS 4.5's
// second-generation transformer). Read when a feature is made.
void SetPreset(unsigned preset);

void* __cdecl Create(ID3D12GraphicsCommandList* cmd, void* caps, const NrCreateParams* p);
int __cdecl Evaluate(ID3D12GraphicsCommandList* cmd, void* feature, void* caps, const NrEvalParams* p);
void __cdecl Release(void* feature);
int __cdecl LastResult(int which);
// The parameter block goes before NGX shuts down.
void Shutdown();

} // namespace dlaa
