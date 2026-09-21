// Bridge — hands copies of Lossless Scaling's frames to the D3D12 NrEngine on the same adapter and hands the
// model's deltas back to LS's device. NT-shared textures created on the D3D11 side, shared fences, GPU-side
// waits only. LS's queue never waits for the model:
//
//   tap (LS render thread)    copy frame (+ LSFG flow) -> shared input, signal copyIn, start a run. If the previous
//                             run is still on the GPU the frame is skipped: the model free-runs at whatever rate the
//                             working scale allows, and the present side warps the newest delta forward.
//   run (NrEngine queue)      wait copyIn, wait "used" (no present still reads the slot), write delta[slot], signal done
//   present (LS render thread)  the newest finished delta (done >= its frame) is applied to the presented frame
//                             (BeginDeltaUse: GPU wait on done, instant; EndDeltaUse: signal "used").
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
    bool IsReady() const { return m_copyIn11 != nullptr; }
    // (Re)creates the shared input for this size/format. Returns false for unsupported formats.
    bool Ensure(uint32_t w, uint32_t h, DXGI_FORMAT fmt);
    // Read-only tap. frameIndex = the tap count (the frame's index; deltas are named by it). Returns true if a run started.
    bool Submit(ID3D11Texture2D* frame, ID3D11Texture2D* flow, uint32_t flowW, uint32_t flowH, const NrParams& params, bool reset, uint64_t frameIndex);
    // The newest finished delta: its frame index (0 = none yet), a borrowed SRV and the work size.
    uint64_t NewestDelta(ID3D11ShaderResourceView** srv, uint32_t* ww, uint32_t* wh);
    void BeginDeltaUse(uint64_t d);   // before recording a compose that reads delta d on LS's context
    void EndDeltaUse(uint64_t d);     // after it
    ID3D11DeviceContext* Context() const { return m_ctx; }
    uint32_t Width() const { return m_w; }  uint32_t Height() const { return m_h; }  DXGI_FORMAT Format() const { return m_fmt; }
    double IntervalMs() const { return m_intervalMs; }   // smoothed time between taps
    double LastIntervalMs() const { return m_lastRawMs; } // the most recent time between two taps (unsmoothed)
    // Time between taps (= the game's frame time as Lossless Scaling sees it) over the frames since the last call:
    // percentiles, the worst frame, and how many were slower than 20 ms and 33 ms. False if too few frames.
    bool TakeFrameTimeWindow(float& p50, float& p95, float& p99, float& worst, int& n, int& over20, int& over33);
    double CpuMs() const { return m_cpuMs; }             // smoothed CPU time LS's render thread spends in Submit
    uint64_t Runs() const { return m_runs; }  uint64_t Skipped() const { return m_skipped; }
    // GPU thread priority of LS's own D3D11 device (-7..7). Above 0 its work pre-empts our normal-priority D3D12 queue,
    // so LSFG's compose/present pacing is not disturbed by the model running next to it on the same GPU.
    void SetLsGpuPriority(int p);
    static bool FormatSupported(DXGI_FORMAT f);
    static DXGI_FORMAT ViewFormat(DXGI_FORMAT f);   // *_SRGB / typeless -> UNORM view format

private:
    static const int kSlots = 3;
    void ReleaseTextures();
    void ReleaseDeltas();
    bool EnsureFlow(uint32_t w, uint32_t h);
    bool EnsureDeltas(uint32_t ww, uint32_t wh);
    bool Share(ID3D11Texture2D* t, ID3D12Resource** out);
    bool MakeSharedFence(ID3D11Fence** f11, ID3D12Fence** f12, const char* what);
    int  SlotOf(uint64_t frame) const { for (int i = 0; i < kSlots; ++i) if (m_slotFrame[i] == frame) return i; return -1; }
    void Log(const char* fmt, ...);
    LogFn m_log;
    NrEngine* m_engine = nullptr;
    ID3D11Device5* m_dev5 = nullptr;
    ID3D11DeviceContext* m_ctx = nullptr;
    ID3D11DeviceContext4* m_ctx4 = nullptr;
    ID3D11Texture2D* m_in11 = nullptr; ID3D12Resource* m_in12 = nullptr;                                     // the frame copy
    ID3D11Texture2D* m_flow11 = nullptr; ID3D12Resource* m_flow12 = nullptr; uint32_t m_fw = 0, m_fh = 0;   // LSFG flow copy, RGBA16F
    ID3D11Texture2D* m_delta11[kSlots] = {}; ID3D12Resource* m_delta12[kSlots] = {}; ID3D11ShaderResourceView* m_deltaSrv[kSlots] = {};
    uint64_t m_slotFrame[kSlots] = {};    // frame index whose delta the slot holds (0 = nothing valid)
    uint64_t m_slotLastUse[kSlots] = {};  // "used" fence value of the last compose recorded against the slot
    uint32_t m_dw = 0, m_dh = 0;          // delta (work) size
    ID3D11Fence* m_copyIn11 = nullptr; ID3D12Fence* m_copyIn12 = nullptr;   // LS signals frameIndex after the copy; the run waits
    ID3D11Fence* m_done11 = nullptr;   ID3D12Fence* m_done12 = nullptr;     // the run signals frameIndex when its delta is finished
    ID3D11Fence* m_used11 = nullptr;   ID3D12Fence* m_used12 = nullptr;     // LS signals a counter after each compose; runs wait
    uint64_t m_lastRun = 0; int m_lastRunSlot = -1; uint64_t m_useCounter = 0;
    uint64_t m_runs = 0, m_skipped = 0;
    uint32_t m_w = 0, m_h = 0; DXGI_FORMAT m_fmt = DXGI_FORMAT_UNKNOWN;
    uint64_t m_lastFailKey = 0;   // log each failing (w,h,fmt) once
    int64_t m_lastQpc = 0; double m_intervalMs = 0, m_cpuMs = 0, m_lastRawMs = 0;
    static const int kIvCap = 1200; float m_ivRing[kIvCap] = {}; int m_ivCount = 0;
    int m_lsPrio = 0; bool m_lsPrioSet = false;
};
