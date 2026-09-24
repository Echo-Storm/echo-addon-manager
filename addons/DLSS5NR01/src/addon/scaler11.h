// DLSS Super Resolution as Lossless Scaling's scaler (the DLSS 4 Upscaler): the Lossless Scaling side.
//
// Lossless Scaling scales every frame it presents, real and generated, with one compute pass. With NIS chosen that pass is easy to know: the
// frame at the game's size in (t0), NIS's two 2x64 RGBA32F coefficient tables (t1, t2), the picture at the screen's size out (u0), and one
// thread group per 32x24 pixels of it. The addon skips that pass and puts DLSS's picture in its output instead.
//
// DLSS itself runs on a D3D12 device of our own (SrEngine, engine/sr_engine.h): running NVIDIA's D3D11 DLSS on Lossless Scaling's device
// crashed it. On Lossless Scaling's context this side only copies the frame (and frame generation's flow) into shared textures, signals a
// shared fence, makes its queue wait on the GPU for the upscaled picture, and copies that into the pass's output. The CPU never waits.
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
    bool IsReady() const { return m_copied.d3d11 != nullptr; }
    ID3D11Device* Device() const { return m_dev; }

    // At the NIS pass, on its context: the frame goes to the engine, DLSS's picture comes back into the pass's output. False when DLSS did not
    // run for this frame (the NIS pass should then run as usual). flow: frame generation's newest flow (RGBA16F) or null.
    bool Upscale(const NisPass& pass, ID3D11Resource* flow, uint32_t flowW, uint32_t flowH, float flowUnit, float motionFraction, unsigned preset, bool reset);

private:
    struct Shared { ID3D11Texture2D* d3d11 = nullptr; ID3D12Resource* d3d12 = nullptr; uint32_t w = 0, h = 0; DXGI_FORMAT fmt = DXGI_FORMAT_UNKNOWN; void Release(); };
    struct Fence { ID3D11Fence* d3d11 = nullptr; ID3D12Fence* d3d12 = nullptr; void Release(); };
    bool Fit(Shared& t, uint32_t w, uint32_t h, DXGI_FORMAT fmt, bool engineWrites, const char* name);
    bool MakeFence(Fence& f, const char* name);
    void Log(const char* fmt, ...);

    LogFn m_log;
    SrEngine* m_engine = nullptr;
    ID3D11Device5* m_dev = nullptr;
    ID3D11DeviceContext* m_ctx = nullptr;
    ID3D11DeviceContext4* m_ctx4 = nullptr;
    Shared m_in, m_out, m_flow;
    Fence m_copied, m_done;
    uint64_t m_frame = 0;
    bool m_loggedFormat = false;
};

} // namespace nr
