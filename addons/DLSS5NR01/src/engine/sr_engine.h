// SrEngine: DLSS Super Resolution on a Direct3D 12 device and queue of our own, on the graphics card Lossless Scaling scales on (the DLSS 4
// Upscaler; scaler11.h is the Lossless Scaling side).
//
// NVIDIA's DLSS code never runs on Lossless Scaling's D3D11 device. Run D3D11 DLSS there crashed Lossless Scaling within seconds, three times,
// each in a different place (NVIDIA's driver, NVIDIA's API, Lossless Scaling's own checks); DLSS on a device of our own, as Neural Rendering and
// the DLAA test ran, never did. Lossless Scaling's side only copies textures and signals and waits on fences; everything else happens here.
//
// A run: the queue waits (on the GPU) for "copied", measures the motion from the frames themselves (FlowEstimator) or turns frame
// generation's flow into motion vectors, runs DLSS from the shared input (the frame at the game's size) into the shared output (the picture
// at the screen's size), and signals "done". The CPU never waits for the GPU,
// except to reuse a command allocator that is still busy.
#pragma once
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <cstdint>
#include <functional>
#include <string>
#include "engine/flow_estimator.h"

class SrEngine {
public:
    using LogFn = std::function<void(const char*)>;
    // On the card with this LUID; NVIDIA's runtime (nvngx_dlss.dll) is looked for in runtimeDir. Touches no device but its own: may run on any
    // thread.
    bool Init(const LUID& card, const std::wstring& dataPath, const std::wstring& runtimeDir, LogFn log);
    void Shutdown();
    bool IsReady() const { return m_ready; }
    bool IsFailed() const { return m_failed; }
    const std::string& LastError() const { return m_error; }

    ID3D12Resource* OpenSharedTexture(HANDLE h);
    ID3D12Fence* OpenSharedFence(HANDLE h);
    void Drain();   // waits until the queue is idle (before shared textures or fences go away)

    // One upscaled frame. in: the frame (COMMON, the game's size); out: the picture (COMMON, the screen's size, writable); flow: frame
    // generation's flow (COMMON, RGBA16F) or null. The queue waits for copied >= copiedValue first and signals done = doneValue after.
    // motionFraction: the part of a real frame between two presented frames. sharpen: contrast-adaptive sharpening of DLSS's picture (0 = off;
    // DLSS 4 has none of its own, and Lossless Scaling's NIS does sharpen). estimate: measure the motion from the frames (flow is then
    // ignored). False when nothing was queued (done is then never signalled).
    bool Run(ID3D12Resource* in, uint32_t inW, uint32_t inH, DXGI_FORMAT inFormat, ID3D12Resource* out, uint32_t outW, uint32_t outH, DXGI_FORMAT outFormat,
             ID3D12Resource* flow, uint32_t flowW, uint32_t flowH, float flowUnit, float motionFraction, bool estimate, unsigned preset, float sharpen, bool reset,
             ID3D12Fence* copied, uint64_t copiedValue, ID3D12Fence* done, uint64_t doneValue);

    double GpuMs() const { return m_gpuMs; }   // everything a run does, on the GPU, smoothed
    double MotionMs() const { return m_motionMs; }   // of that, the motion (the estimate, or the flow pass)
    uint64_t Runs() const { return m_runs; }
    double LastBuildMs() const { return m_buildMs; }

private:
    static const int kSlots = 4;
    bool EnsureFeature(uint32_t inW, uint32_t inH, uint32_t outW, uint32_t outH, unsigned preset);
    bool EnsureInputs(uint32_t w, uint32_t h);
    bool EnsureSharpenTarget(uint32_t w, uint32_t h, DXGI_FORMAT fmt);
    int TakeSlot();
    void ReadTime(int slot);
    bool WaitIdle();
    void Transition(ID3D12Resource* r, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to);
    void Fail(const char* fmt, ...);
    void Log(const char* fmt, ...);

    LogFn m_log;
    bool m_ready = false, m_failed = false;
    std::string m_error;
    ID3D12Device* m_dev = nullptr;
    ID3D12CommandQueue* m_queue = nullptr;
    ID3D12CommandAllocator* m_alloc[kSlots] = {};
    ID3D12GraphicsCommandList* m_list = nullptr;
    ID3D12Fence* m_fence = nullptr; HANDLE m_event = nullptr; uint64_t m_fenceValue = 0;
    uint64_t m_slotDone[kSlots] = {}; int m_nextSlot = 0;
    ID3D12QueryHeap* m_timestamps = nullptr; ID3D12Resource* m_timestampReadback = nullptr; uint64_t m_timestampFreq = 1;
    double m_gpuMs = 0, m_motionMs = 0;
    FlowEstimator m_estimator; bool m_estimatedLast = false; uint64_t m_estimates = 0;
    // the motion pass
    ID3D12RootSignature* m_rootSig = nullptr; ID3D12PipelineState* m_motionPso = nullptr; ID3D12PipelineState* m_sharpenPso = nullptr;
    ID3D12Resource* m_unsharpened = nullptr; uint32_t m_unsharpenedW = 0, m_unsharpenedH = 0; DXGI_FORMAT m_unsharpenedFmt = DXGI_FORMAT_UNKNOWN;   // DLSS's picture before sharpening
    ID3D12DescriptorHeap* m_heap = nullptr; uint32_t m_descriptorSize = 0;
    // DLSS
    void* m_params = nullptr;   // NVSDK_NGX_Parameter*
    void* m_feature = nullptr;  // NVSDK_NGX_Handle*
    uint32_t m_inW = 0, m_inH = 0, m_outW = 0, m_outH = 0; unsigned m_preset = ~0u;
    ID3D12Resource* m_motion = nullptr;   // RG16F at the game's size, rests readable
    ID3D12Resource* m_distrust = nullptr; // R8 at the game's size, rests readable: where the measured motion cannot be trusted (DLSS's bias mask)
    ID3D12Resource* m_depth = nullptr;    // R32F, flat
    ID3D12Resource* m_depthUpload = nullptr;
    uint64_t m_runs = 0; double m_buildMs = 0;
};
