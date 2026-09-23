// NrEngine: runs the model on its own Direct3D 12 device and queue, on the same graphics card as Lossless Scaling's D3D11 device.
//
// Per frame, one command list on one queue: shrink the frame to the working size (the "proxy"), turn LSFG's optical flow into the model's
// motion vectors, run the model (one to four passes), and write the delta (model minus proxy) into the bridge's shared result texture. The
// engine never touches Lossless Scaling's frames: it reads a copy and writes a delta that the D3D11 side applies when a frame is presented.
// Run() makes the queue wait for the fences the bridge names and signals another; the CPU never waits for the GPU, except to reuse a command
// allocator that is still busy.
#pragma once
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include "forwarder/nr_api.h"

// The settings. What the model listens to was measured one knob at a time (docs/dlssnr-knobs.md): all of it is read at every run, and only the
// working scale needs the feature made again.
struct NrParams {
    // read by the model
    uint32_t style = 0;           // 0 standard, 1 natural, 2 cinematic (more is taken as 2)
    uint32_t useAutoMask = 1;
    float intensity = 1.0f;       // the model keeps it to 0..1
    float localStructure = 1.0f;  // not limited; negative works, above 10 is garbage
    float localTone = 1.0f;       // not limited
    float skinStructure = -1.0f;  // -1 = the same as local structure (the model's own default)
    bool useFlow = true;          // LSFG's optical flow as the model's motion vectors
    float flowUnit = 2.0f;        // one flow unit is 1/flowUnit of a flow-texture pixel (measured 2.0 on Lossless Scaling 3.x)

    // the model's side
    float workingScale = 0.35f;   // the frame is shrunk by this before the model sees it: the one setting that decides the cost
    uint32_t passes = 1;          // model runs per frame (1..4), each taking the one before's result as its colour
    float deltaSmooth = 0.0f;     // blend of the previous delta (moved along the motion) into the new one, 0 = off, below 1

    // the compose, on the D3D11 side when a frame is presented (compose11)
    float composeIntensity = 1.0f;// how much of the delta lands
    float maxDelta = 0.5f;        // limit on |delta|
    float ghostGuard = 0.5f;      // fades the delta where LSFG's two motion fields disagree and as the delta ages; 0 = off
    float hiProtect = 0.85f;      // fades the delta as the source brightens from here to white (1 = off)
    float sharpen = 0.0f;         // contrast-adaptive sharpening of the presented frame, 0 = off
    float saturation = 1.0f;      // colour intensity, 1 = unchanged, 0 = grey
    float vibrance = 0.0f;        // extra saturation for muted colours only, 0 = off
    float brightness = 0.0f;      // added to every channel (encoded 0..1 values), 0 = unchanged
    float contrast = 1.0f;        // scale around mid-grey, 1 = unchanged
    float gamma = 1.0f;           // mid-tone curve, above 1 brightens mid-tones, 1 = unchanged
    float shadows = 0.0f;         // lifts (+) or deepens (-) the dark tones only, -1..1
    float highlights = 0.0f;      // brightens (+) or recovers (-) the bright tones only, -1..1
    float grain = 0.0f;           // film grain amount, 0 = off
    float grainSize = 1.0f;       // grain cell size in screen pixels (1..4)
    static const int kMaxHud = 6; // rectangles (fractions of the screen) where the picture is left untouched
    uint32_t hudCount = 0;
    float hud[kMaxHud][4] = {};   // left, top, right, bottom, 0..1
    float hudFeather = 0.004f;    // soft edge of those rectangles, as a fraction of the screen
    uint32_t debugView = 0;       // 0 result, 1 original, 2 delta x4, 3 frame role (real/generated), 4 LSFG flow, 5 ghost guard weight

    bool CreateKeysEqual(const NrParams& o) const { return workingScale == o.workingScale; }
    NrTuning Tuning() const { return NrTuning{ style, useAutoMask, 1u, intensity, localStructure, localTone, skinStructure }; }
};

struct NrStats {
    double nrMs = 0, totalMs = 0;   // the last finished run: the model's time, and the whole run's
    double startMs = 0, doneMs = 0; // when that run started and ended on the GPU, from its submission on the CPU (queue wait and contention)
    uint64_t frames = 0, fails = 0; // runs queued, and runs whose model evaluation failed
    int floatSlot = -1;             // the parameter block's float setter slot, as found
    bool hasFlow = false; uint32_t flowW = 0, flowH = 0;   // the LSFG flow now bound
    uint32_t workW = 0, workH = 0;  // the model's input size
    char lastError[256] = {};
};

class NrEngine {
public:
    using LogFn = std::function<void(const char*)>;

    // The card is the one with this LUID. forwarderPath: nvngx.dll_dlss5nr01.dll; snippetPath: the model file; dataPath: where NGX may
    // write; lsDir: searched for the model file as well.
    bool Init(const LUID& adapterLuid, const std::wstring& forwarderPath, const std::wstring& snippetPath, const std::wstring& dataPath,
              const std::wstring& lsDir, LogFn log);
    void Shutdown();
    bool IsReady() const { return m_ready; }
    bool IsFailed() const { return m_failed; }
    const NrStats& Stats() const { return m_stats; }
    ID3D12Device* Device() const { return m_dev; }

    void Drain() { if (m_dev) WaitIdle(); }   // wait until the queue is idle (before shared textures or fences go away)
    ID3D12Resource* OpenSharedTexture(HANDLE h);
    ID3D12Fence* OpenSharedFence(HANDLE h);
    // LSFG's flow for the next runs (borrowed: the bridge drains the engine before it lets go of it); null for none.
    void SetFlowInput(ID3D12Resource* flow, uint32_t w, uint32_t h);

    // Makes the feature and the scratch textures for this frame size and format and working scale; nothing to do when they are unchanged.
    bool Prepare(uint32_t width, uint32_t height, DXGI_FORMAT frameFormat, const NrParams& params);

    // One run from sharedIn (the frame copy) into sharedDelta (both opened from the bridge's handles, both in COMMON). The queue first waits
    // for waitFence >= waitValue (the copy is done) and usedFence >= usedValue (no present still reads sharedDelta), and afterwards signals
    // signalFence = signalValue. True when the model evaluated; Stats().frames counts every run that was queued.
    bool Run(ID3D12Resource* sharedIn, ID3D12Resource* sharedDelta, ID3D12Fence* waitFence, uint64_t waitValue, ID3D12Fence* usedFence,
             uint64_t usedValue, ID3D12Fence* signalFence, uint64_t signalValue, bool reset);

    void Log(const char* fmt, ...);   // also used by NGX's log callback

private:
    static const int kSlots = 4;          // command allocators in rotation
    static const int kPassDescriptors = 6, kPasses = 3;   // per pass: t0..t3, u0, u1; the passes: shrink, motion, delta
    static const int kDescriptorsPerSlot = kPassDescriptors * kPasses;

    bool CreateQueue(const LUID& luid);
    bool StartNgx();
    bool StartForwarder();
    bool FindFloatSlot();
    bool CreatePipelines();
    void ReleaseScratch();
    void Fail(const char* fmt, ...);
    ID3D12Resource* MakeTexture(uint32_t w, uint32_t h, DXGI_FORMAT fmt, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES state);
    void Upload(ID3D12Resource* texture, uint32_t bytesPerPixel, const void* pixels);   // records on the open list; leaves it readable
    void Transition(ID3D12Resource* r, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to);
    bool WaitIdle();                      // false when the GPU did not finish in time
    int TakeSlot();                       // the next allocator, reset for recording; -1 when the GPU still has not finished with it
    void ReadTimes(int slot);
    D3D12_CPU_DESCRIPTOR_HANDLE CpuDescriptor(int slot, int pass, int i) const;
    D3D12_GPU_DESCRIPTOR_HANDLE GpuDescriptor(int slot, int pass, int i) const;

    LogFn m_log;
    bool m_ready = false, m_failed = false;
    NrStats m_stats{};
    std::wstring m_forwarderPath, m_snippetPath, m_dataPath, m_lsDir;

    // Direct3D 12
    ID3D12Device* m_dev = nullptr;
    ID3D12CommandQueue* m_queue = nullptr;
    ID3D12CommandAllocator* m_alloc[kSlots] = {};
    ID3D12GraphicsCommandList* m_list = nullptr;
    ID3D12Fence* m_fence = nullptr; HANDLE m_fenceEvent = nullptr; uint64_t m_fenceValue = 0;
    uint64_t m_slotDone[kSlots] = {};     // the fence value that marks each allocator's last list as finished
    int64_t m_slotSubmitQpc[kSlots] = {};
    int m_nextSlot = 0;
    ID3D12QueryHeap* m_timestamps = nullptr; ID3D12Resource* m_timestampReadback = nullptr; uint64_t m_timestampFreq = 1;
    std::vector<ID3D12Resource*> m_uploads;   // upload buffers to release once the GPU has read them

    // the model, through the forwarder
    void* m_caps = nullptr;
    HMODULE m_forwarder = nullptr;
    PFN_nrfwd_probe m_probe = nullptr; PFN_nrfwd_init m_init = nullptr; PFN_nrfwd_set_float_slot m_setFloatSlot = nullptr;
    PFN_nrfwd_probe_float m_probeFloat = nullptr; PFN_nrfwd_get_float m_getFloat = nullptr; PFN_nrfwd_create m_create = nullptr;
    PFN_nrfwd_evaluate m_evaluate = nullptr; PFN_nrfwd_release m_release = nullptr; PFN_nrfwd_last_result m_lastResult = nullptr;
    void* m_feature = nullptr;
    bool m_resetHistory = true;           // the next run tells the model to start its history afresh

    // the frame and the scratch textures, at the working size
    uint32_t m_w = 0, m_h = 0; DXGI_FORMAT m_fmt = DXGI_FORMAT_UNKNOWN; NrParams m_params{};
    uint32_t m_ww = 0, m_wh = 0;
    ID3D12Resource* m_proxy = nullptr;    // RGBA8: the frame as the model sees it; rests readable
    ID3D12Resource* m_out[2] = {};        // RGBA8: the model's output (more passes go back and forth between the two); rest writable
    ID3D12Resource* m_history[2] = {};    // RGBA16F: the last smoothed delta (smoothing goes back and forth between the two); rest readable
    int m_historyRead = 0; bool m_historyValid = false;
    ID3D12Resource* m_depth = nullptr;    // R32F, flat: the model ignores depth
    ID3D12Resource* m_mvec = nullptr;     // RG16F: zero, or LSFG's flow in working-size pixels; rests readable
    ID3D12Resource* m_flow = nullptr; uint32_t m_flowW = 0, m_flowH = 0;   // LSFG's flow (the bridge's shared copy, borrowed)

    // the three passes of our own
    ID3D12RootSignature* m_rootSig = nullptr;
    ID3D12PipelineState* m_psoShrink = nullptr; ID3D12PipelineState* m_psoMotion = nullptr;
    ID3D12PipelineState* m_psoDelta = nullptr; ID3D12PipelineState* m_psoDeltaSmooth = nullptr;
    ID3D12DescriptorHeap* m_heap = nullptr; uint32_t m_descriptorSize = 0;
};
