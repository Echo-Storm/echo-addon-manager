// DLSS Super Resolution as Lossless Scaling's scaler (the DLSS 4 Upscaler): the Lossless Scaling side.
//
// Lossless Scaling scales every frame it presents, real and generated, with one compute pass. With NIS chosen that pass is easy to know: the
// frame at the game's size in (t0), NIS's two 2x64 RGBA32F coefficient tables (t1, t2), the picture at the screen's size out (u0), and one
// thread group per 32x24 pixels of it. The addon skips that pass and puts DLSS's picture in its output instead.
//
// DLSS itself runs on a D3D12 device of our own (SrEngine, engine/sr_engine.h): running NVIDIA's D3D11 DLSS on Lossless Scaling's device
// crashed it. On Lossless Scaling's context this side only copies the frame (and frame generation's flow) into shared textures, signals a
// shared fence, and copies DLSS's newest finished picture into the pass's output. By default that is the frame before's: Lossless
// Scaling's queue never waits on the engine's, and the CPU never waits either (Handoff::Late).
#pragma once
#include <d3d11_4.h>
#include <d3d12.h>
#include <cstdint>
#include <functional>

class SrEngine;

namespace nr {

// The NIS pass, recognised from what is bound when it is dispatched; false for any other pass.
struct NisPass { ID3D11Resource* in = nullptr; ID3D11Resource* out = nullptr; uint32_t inW = 0, inH = 0, outW = 0, outH = 0; DXGI_FORMAT inFmt = DXGI_FORMAT_UNKNOWN, outFmt = DXGI_FORMAT_UNKNOWN; };
bool FindNisPass(ID3D11DeviceContext* ctx, uint32_t x, uint32_t y, uint32_t z, NisPass& pass);   // AddRef's in/out: ReleaseNisPass
void ReleaseNisPass(NisPass& pass);

class ScalerLink {
public:
    using LogFn = std::function<void(const char*)>;
    // On Lossless Scaling's render thread: the shared fences between its device and the engine's.
    bool Init(ID3D11Device* dev, ID3D11DeviceContext* ctx, SrEngine* engine, LogFn log);
    void Shutdown();
    // Before letting go of a device Lossless Scaling has replaced: why it went (hung, reset, removed, or not at all), and how far the two
    // fences had got, so the log says whether a wait was left unanswered.
    void ReportDeviceChange();
    bool IsReady() const { return m_copied.d3d11 != nullptr; }
    // How the picture comes back. Late: the newest finished one (the frame before's), nothing waits. Wait: this frame's, Lossless Scaling's
    // queue waits on the GPU for it (the first design; with frame generation off it left Lossless Scaling restarting, 2026-09-24).
    // Observe: DLSS runs but NIS's picture stays, to tell whether running DLSS at all is what upsets Lossless Scaling.
    // AtPresent: NIS runs as usual, and DLSS's newest picture is copied over its output just before Present (PresentCopy).
    enum class Handoff { Late = 0, Wait = 1, Observe = 2, AtPresent = 3 };
    // AtPresent, from the Present hook on Lossless Scaling's presenting thread: the picture chosen at the NIS pass goes into the swap chain's
    // back buffer. Nothing happens for another swap chain (another device or size) or when no picture is waiting.
    void PresentCopy(IDXGISwapChain* sc);
    ID3D11Device* Device() const { return m_dev; }

    // At the NIS pass, on its context: the frame goes to the engine, DLSS's picture comes back into the pass's output. False when DLSS did not
    // run for this frame (the NIS pass should then run as usual). flow: frame generation's newest flow (RGBA16F) or null.
    // estimate: the engine measures the motion from the frames (flow unused).
    bool Upscale(const NisPass& pass, ID3D11Resource* flow, uint32_t flowW, uint32_t flowH, float flowUnit, float motionFraction, bool estimate, unsigned preset,
                 float sharpen, bool reset, Handoff handoff = Handoff::Late);

private:
    struct Shared { ID3D11Texture2D* d3d11 = nullptr; ID3D12Resource* d3d12 = nullptr; uint32_t w = 0, h = 0; DXGI_FORMAT fmt = DXGI_FORMAT_UNKNOWN; void Release(); };
    struct Fence { ID3D11Fence* d3d11 = nullptr; ID3D12Fence* d3d12 = nullptr; void Release(); };
    bool Fit(Shared& t, uint32_t w, uint32_t h, DXGI_FORMAT fmt, bool engineWrites, const char* name);
    bool MakeFence(Fence& f, const char* name);
    void Log(const char* fmt, ...);
    void DescribeTargets(const NisPass& pass);
    bool MakeGrabShader();
    void Probe(uint64_t shown, bool inFresh);

    LogFn m_log;
    SrEngine* m_engine = nullptr;
    ID3D11Device5* m_dev = nullptr;
    ID3D11DeviceContext* m_ctx = nullptr;
    ID3D11DeviceContext4* m_ctx4 = nullptr;
    // The frame is read into m_in by a small compute pass through the NIS pass's own t0 view, not copied: with frame generation off that
    // frame is a keyed-mutex texture shared from Lossless Scaling's capture device, and CopyResource from it gave all black (2026-09-24).
    ID3D11ComputeShader* m_grab = nullptr;
    ID3D11UnorderedAccessView* m_inUav = nullptr;
    Shared m_in, m_out[2], m_flow;   // frame n's picture goes to m_out[n % 2], so the one before stays readable while DLSS writes
    Fence m_copied, m_done;
    uint64_t m_frame = 0;             // the newest frame handed to the engine
    uint64_t m_holds[2] = {};         // the frame whose finished picture each m_out holds (0: none)
    Handoff m_handoff = Handoff::Late;
    uint64_t m_atPresent = 0;         // AtPresent: the frame whose picture PresentCopy puts in the back buffer (0: none)
    uint64_t m_copiedAtPresent = 0;
    // once per link: how bright the frame DLSS gets and the picture it makes are (a black picture shows here)
    ID3D11Texture2D* m_probe[2] = {};
    int m_probeState = 0;
    bool m_loggedFormat = false, m_described = false;
};

} // namespace nr
