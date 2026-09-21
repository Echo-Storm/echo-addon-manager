// NrEngine — the D3D12 sidecar: driver-core capability block, forwarder-driven DLSSNR feature 18, and the
// model-side passes. Lives on the same adapter as the host's D3D11 device.
//
// One run per submitted frame, on one queue:  downsample -> flow->mvec -> model -> delta (shared, work-res).
// The engine never touches LS's frames: it reads a copy and writes a delta the D3D11 side applies at present
// time. Run() waits for the fences the bridge names and signals another; it never blocks the CPU.
#pragma once
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <cstdint>
#include <string>
#include <functional>
#include <vector>
#include "forwarder/lspnr_api.h"

// What the model listens to was measured knob by knob (docs/dlssnr-knobs.md). Everything the model reads is a
// runtime control; only the working scale forces a feature rebuild.
struct NrParams {
    // model (read at every evaluate)
    uint32_t style = 0;           // 0 standard, 1 natural, 2 cinematic (the model clamps higher values to 2)
    uint32_t useAutoMask = 1;
    float intensity = 1.0f;       // model-side intensity, the model clamps it to 0..1
    float localStructure = 1.0f;  // unclamped; negative accepted, >10 is garbage
    float localTone = 1.0f;       // unclamped
    float skinStructure = -1.0f;  // -1 = follow local structure (the model's own default)
    bool useFlow = true;          // LSFG's optical flow as the model's motion vectors
    float flowUnit = 2.0f;        // one flow unit = 1/flowUnit flow-texture px (measured 2.0 on LS 3.x)

    // size (model side) and compose (D3D11 side, at present time)
    float workingScale = 0.35f;   // the frame is shrunk by this before the model sees it (the only cost lever)
    float composeIntensity = 1.0f;// how much of the delta lands
    float maxDelta = 0.5f;        // clamp on |delta|
    float ghostGuard = 0.5f;      // compose side: 0 = off; fades the delta where LSFG's two motion fields disagree and as the delta ages
    float hiProtect = 0.85f;      // fade the delta as the source luminance rises from here to white (1 = off)
    uint32_t passes = 1;          // model runs per frame (1..4): each pass takes the previous result as its colour input
    float sharpen = 0.0f;         // compose side: contrast-adaptive sharpening of the presented frame, 0 = off
    float saturation = 1.0f;      // compose side: colour intensity of the presented frame, 1 = unchanged, 0 = greyscale
    float vibrance = 0.0f;        // compose side: extra saturation for muted colours only, 0 = off
    float brightness = 0.0f;      // compose side: added to every channel (encoded values, 0..1 scale), 0 = unchanged
    float contrast = 1.0f;        // compose side: scale around mid-grey, 1 = unchanged
    float gamma = 1.0f;           // compose side: mid-tone curve, above 1 brightens mid-tones, 1 = unchanged
    float shadows = 0.0f;         // compose side: lifts (+) or deepens (-) the dark tones only, -1..1, 0 = unchanged
    float highlights = 0.0f;      // compose side: brightens (+) or recovers (-) the bright tones only, -1..1, 0 = unchanged
    float grain = 0.0f;           // compose side: film grain amount, 0 = off
    float grainSize = 1.0f;       // compose side: grain cell size in screen pixels (1..4)
    float deltaSmooth = 0.0f;     // model side: blend of the previous (motion-warped) delta into the new one, 0 = off, <1
    static const int kMaxHud = 6; // compose side: rectangles (fractions of the screen) where the picture is left untouched
    uint32_t hudCount = 0;
    float hud[kMaxHud][4] = {};   // left, top, right, bottom, 0..1
    float hudFeather = 0.004f;    // soft edge of those rectangles, as a fraction of the screen
    uint32_t debugView = 0;       // 0 result, 1 original, 2 delta x4, 3 frame role (real/generated), 4 LSFG flow, 5 ghost guard weight

    bool CreateKeysEqual(const NrParams& o) const { return workingScale == o.workingScale; }
    LspnrTuning Tuning() const { return LspnrTuning{ style, useAutoMask, 1u, intensity, localStructure, localTone, skinStructure }; }
};

struct NrStats {
    double nrMs = 0, totalMs = 0;   // last completed run: model time, whole run (downsample..delta)
    double startMs = 0, doneMs = 0; // last completed pass: GPU start / GPU end, measured from the CPU submit (queue wait + contention)
    uint64_t frames = 0, fails = 0;
    int floatSlot = -1;
    bool hasFlow = false; uint32_t flowW = 0, flowH = 0;   // LSFG flow input currently bound
    uint32_t workW = 0, workH = 0;                        // the model input size
    char lastError[256] = {};
};

class NrEngine {
public:
    using LogFn = std::function<void(const char*)>;

    // paths: forwarder DLL, snippet DLL, NGX data path (logs), extra snippet search dir (LS folder)
    bool Init(const LUID& adapterLuid, const std::wstring& forwarderPath, const std::wstring& snippetPath,
              const std::wstring& dataPath, const std::wstring& lsDir, LogFn log);
    void Shutdown();
    bool IsReady() const { return m_ready; }
    bool IsFailed() const { return m_failed; }
    const NrStats& Stats() const { return m_stats; }

    ID3D12Device* Device() const { return m_dev; }
    void Drain() { if (m_dev) WaitIdle(); }   // block until the queue is idle (before shared resources go away)
    ID3D12Resource* OpenSharedTexture(HANDLE h);
    ID3D12Fence* OpenSharedFence(HANDLE h);
    // LSFG flow for the next Run (borrowed; the bridge drains the engine before releasing it). nullptr = none.
    void SetFlowInput(ID3D12Resource* flow, uint32_t w, uint32_t h);

    // (Re)creates the feature and scratch for this frame size/format and working scale. Cheap when unchanged.
    bool Prepare(uint32_t width, uint32_t height, DXGI_FORMAT frameFormat, const NrParams& params);

    // One run: waits on the queue for waitFence >= waitValue (the frame copy) and usedFence >= usedValue (the
    // last present that read sharedDelta), runs downsample -> model -> delta from sharedIn into sharedDelta
    // (both opened from the host's shared handles, both in COMMON), then signals signalFence = signalValue.
    // Never blocks the CPU except to recycle a command allocator.
    bool Run(ID3D12Resource* sharedIn, ID3D12Resource* sharedDelta, ID3D12Fence* waitFence, uint64_t waitValue,
             ID3D12Fence* usedFence, uint64_t usedValue, ID3D12Fence* signalFence, uint64_t signalValue, bool reset);

private:
    bool InitD3D12(const LUID& luid);
    bool InitNgx();
    bool InitForwarder();
    bool InitComposer();
    bool ProbeFloatSlot();
    void ReleaseFeatureAndScratch();
    void UploadTexture(ID3D12Resource* tex, uint32_t bpp, const void* data);   // records on m_list (open), leaves tex in NON_PIXEL_SHADER_RESOURCE
    void Fail(const char* fmt, ...);
public:
    void Log(const char* fmt, ...);   // also used by the NGX log callback
private:
    ID3D12Resource* MakeTex(uint32_t w, uint32_t h, DXGI_FORMAT fmt, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES state);
    void Barrier(ID3D12GraphicsCommandList* list, ID3D12Resource* r, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after);
    void WaitIdle();
    int  AcquireSlot();                                // recycles an allocator (may block briefly)
    void ReadTimestamps(int slot, double& aMs, double& bMs);
    D3D12_CPU_DESCRIPTOR_HANDLE Cpu(int slot, int i);
    D3D12_GPU_DESCRIPTOR_HANDLE Gpu(int slot, int i);

    LogFn m_log;
    bool m_ready = false, m_failed = false;
    NrStats m_stats{};
    std::wstring m_forwarderPath, m_snippetPath, m_dataPath, m_lsDir;

    // D3D12: one direct queue, kFrames allocator slots
    ID3D12Device* m_dev = nullptr;
    ID3D12CommandQueue* m_queue = nullptr;
    static const int kFrames = 4;
    ID3D12CommandAllocator* m_alloc[kFrames] = {};
    ID3D12GraphicsCommandList* m_list = nullptr;
    ID3D12Fence* m_ownFence = nullptr; HANDLE m_ownEvent = nullptr; uint64_t m_ownValue = 0; uint64_t m_allocFence[kFrames] = {};
    int m_frameIndex = 0;
    ID3D12QueryHeap* m_qheap = nullptr; ID3D12Resource* m_qread = nullptr; uint64_t m_tsFreq = 1;
    int64_t m_submitQpc[kFrames] = {};

    // NGX
    void* m_caps = nullptr;
    HMODULE m_fwd = nullptr;
    PFN_lspnr_probe m_pProbe = nullptr; PFN_lspnr_init m_pInit = nullptr; PFN_lspnr_set_float_slot m_pSetFloatSlot = nullptr;
    PFN_lspnr_probe_float m_pProbeFloat = nullptr; PFN_lspnr_get_float m_pGetFloat = nullptr; PFN_lspnr_create m_pCreate = nullptr;
    PFN_lspnr_evaluate m_pEvaluate = nullptr; PFN_lspnr_release m_pRelease = nullptr; PFN_lspnr_last_result m_pLast = nullptr;
    void* m_feature = nullptr;

    // frame config
    uint32_t m_w = 0, m_h = 0; DXGI_FORMAT m_fmt = DXGI_FORMAT_UNKNOWN; NrParams m_params{};
    uint32_t m_ww = 0, m_wh = 0;         // model input size
    ID3D12Resource* m_proxy = nullptr;   // work-res RGBA8 (UAV|SRV): the frame as the model sees it
    ID3D12Resource* m_nrOut = nullptr;   // work-res RGBA8 (UAV): model output
    ID3D12Resource* m_nrOut2 = nullptr;  // second output buffer: multi-pass ping-pongs between the two
    ID3D12Resource* m_prev[2] = {};      // work-res RGBA16F: the last smoothed delta (temporal smoothing ping-pongs between them)
    int m_prevIdx = 0; bool m_prevValid = false;
    ID3D12Resource* m_depth = nullptr;   // work-res R32F flat (the model ignores depth)
    ID3D12Resource* m_mvec = nullptr;    // work-res RG16F (UAV|SRV): zero, or LSFG flow converted to work-res pixels
    ID3D12Resource* m_flowIn = nullptr; uint32_t m_flowW = 0, m_flowH = 0;   // borrowed LSFG flow (shared RGBA16F)
    std::vector<ID3D12Resource*> m_garbage;
    bool m_needReset = true;

    // model-side passes
    ID3D12RootSignature* m_rootSig = nullptr;
    ID3D12PipelineState* m_psoDown = nullptr; ID3D12PipelineState* m_psoFlow = nullptr; ID3D12PipelineState* m_psoDelta = nullptr; ID3D12PipelineState* m_psoDeltaSm = nullptr;
    ID3D12DescriptorHeap* m_heap = nullptr; uint32_t m_descSize = 0;
    static const int kDescPerSlot = 16;
};
