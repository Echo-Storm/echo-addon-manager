// Bridge: moves Lossless Scaling's frames to the model and the model's results back.
//
// Lossless Scaling draws with D3D11; the model runs on its own D3D12 queue (NrEngine) on the same graphics card. The two share textures and fences
// through NT handles, and every wait between them is a GPU wait, never a CPU one. Lossless Scaling's queue never waits for the model: a frame that
// arrives while the model is still busy is skipped (the model runs at whatever rate the working scale allows), and the presents keep warping the
// newest result forward.
//
// Each real frame (Submit, on Lossless Scaling's render thread): the frame and LSFG's flow are copied into the shared input, "copied" is signalled
// with the frame's index, and a run is queued. The run waits for "copied", and for "released" to show that no present still reads the result slot
// it is about to overwrite; it writes the delta (model minus input) into that slot and signals "finished" with the frame's index.
// Each present: NewestDelta picks the newest finished slot, and BeginDeltaUse / EndDeltaUse go around the compose that reads it (the end signals
// "released").
#pragma once
#include <d3d11_4.h>
#include <dxgi1_2.h>
#include <cstdint>
#include <functional>
#include "engine/nr_engine.h"

class Bridge {
public:
    using LogFn = std::function<void(const char*)>;
    bool Init(ID3D11Device* dev, ID3D11DeviceContext* ctx, NrEngine* engine, LogFn log);
    void Shutdown();
    bool IsReady() const { return m_copied.d3d11 != nullptr; }
    // Makes the shared input fit a frame of this size and format (recreating it when either changed). False for a format the model cannot take.
    bool Ensure(uint32_t w, uint32_t h, DXGI_FORMAT fmt);
    // Hands a frame (and LSFG's flow, if any) to the model. The frame is only read. frameIndex names the result (the tap count). True when a run
    // was queued.
    bool Submit(ID3D11Texture2D* frame, ID3D11Texture2D* flow, uint32_t flowW, uint32_t flowH, const NrParams& params, bool reset, uint64_t frameIndex);
    // The newest finished result: its frame index (0 = none yet), a borrowed view of it and its size (the working size).
    uint64_t NewestDelta(ID3D11ShaderResourceView** srv, uint32_t* ww, uint32_t* wh);
    void BeginDeltaUse(uint64_t d);   // before recording a compose that reads result d on Lossless Scaling's context
    void EndDeltaUse(uint64_t d);     // after it
    ID3D11DeviceContext* Context() const { return m_ctx; }
    uint32_t Width() const { return m_input.w; }  uint32_t Height() const { return m_input.h; }  DXGI_FORMAT Format() const { return m_fmt; }
    double IntervalMs() const { return m_intervalMs; }        // time between frames, smoothed
    double LastIntervalMs() const { return m_lastIntervalMs; } // time between the last two frames
    // The time between frames (the game's frame time as Lossless Scaling sees it) since the last call: percentiles, the worst, and how many took
    // longer than 20 ms and 33 ms. False while there are too few frames.
    bool TakeFrameTimeWindow(float& p50, float& p95, float& p99, float& worst, int& n, int& over20, int& over33);
    double CpuMs() const { return m_cpuMs; }                   // CPU time Submit takes on Lossless Scaling's render thread, smoothed
    uint64_t Runs() const { return m_runs; }  uint64_t Skipped() const { return m_skipped; }
    // GPU thread priority of Lossless Scaling's D3D11 device (-7..7). Above 0 its work pre-empts the model's normal-priority queue, so LSFG's
    // pacing is not disturbed by the model sharing the graphics card. Set back to 0 at Shutdown.
    void SetLsGpuPriority(int p);
    static bool FormatSupported(DXGI_FORMAT f);
    static DXGI_FORMAT ViewFormat(DXGI_FORMAT f);   // the UNORM / FLOAT format to view a frame of format f with (sRGB and typeless map to it)

private:
    // A texture made on Lossless Scaling's device and opened on the model's.
    struct SharedTexture { ID3D11Texture2D* d3d11 = nullptr; ID3D12Resource* d3d12 = nullptr; ID3D11ShaderResourceView* view = nullptr; uint32_t w = 0, h = 0; void Release(); };
    struct SharedFence { ID3D11Fence* d3d11 = nullptr; ID3D12Fence* d3d12 = nullptr; void Release(); };
    // One result slot: the frame whose delta it holds (0 = nothing usable) and the "released" value the last compose reading it signals.
    struct Slot { SharedTexture delta; uint64_t frame = 0; uint64_t releasedAt = 0; };
    static const int kSlots = 3;
    static const int kFrameTimeCap = 1200;

    bool MakeFence(SharedFence& f, const char* name);
    bool MakeTexture(SharedTexture& t, uint32_t w, uint32_t h, DXGI_FORMAT fmt, bool modelWrites, const char* name);
    void DropInput();
    void DropFlow();
    void DropSlots();
    bool FitFlow(uint32_t w, uint32_t h);
    bool FitSlots(uint32_t w, uint32_t h);
    int FindSlot(uint64_t frame) const;
    void NoteFrameTime(int64_t now, int64_t freq);
    void Log(const char* fmt, ...);
    void LogOnce(uint64_t key, const char* fmt, ...);   // a failure that repeats every frame is logged once

    LogFn m_log;
    NrEngine* m_engine = nullptr;
    ID3D11Device5* m_dev = nullptr;
    ID3D11DeviceContext* m_ctx = nullptr;
    ID3D11DeviceContext4* m_ctx4 = nullptr;
    SharedTexture m_input;                 // the frame copy, in the frame's view format
    SharedTexture m_flow;                  // the flow copy, RGBA16F
    Slot m_slots[kSlots];                  // the results, RGBA16F at the working size
    SharedFence m_copied, m_finished, m_released;
    DXGI_FORMAT m_fmt = DXGI_FORMAT_UNKNOWN;
    uint64_t m_inFlight = 0;               // the frame index of the last queued run (0 = none)
    int m_newestSlot = -1;                 // the slot that run writes
    uint64_t m_releaseCount = 0;
    uint64_t m_runs = 0, m_skipped = 0;
    uint64_t m_lastFailure = 0;
    int64_t m_prevFrameQpc = 0;
    double m_intervalMs = 0, m_lastIntervalMs = 0, m_cpuMs = 0;
    float m_frameTimes[kFrameTimeCap] = {}; int m_frameTimeCount = 0;
    int m_lsPriority = 0; bool m_lsPriorityApplied = false;
};
