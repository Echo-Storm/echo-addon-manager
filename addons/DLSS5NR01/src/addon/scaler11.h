// DLSS Super Resolution as Lossless Scaling's scaler (the DLSS 4 addon).
//
// Lossless Scaling scales every frame it presents, real and generated, with one compute pass. With NIS chosen that pass is easy to know: the
// frame at the game's size in (t0), NIS's two 2x64 RGBA32F coefficient tables (t1, t2), the picture at the screen's size out (u0), and one
// thread group per 32x24 pixels of it. The addon lets that pass be skipped and runs DLSS Super Resolution into the same output instead, on
// Lossless Scaling's own D3D11 device and immediate context, in the same place in its command stream: everything after it (frame generation's
// presents, Neural Rendering) sees an ordinary scaled picture.
//
// DLSS is fed what there is: no camera jitter, a flat depth, and frame generation's optical flow as motion vectors (a presented frame is a
// fraction of a real frame after the one before it: half, at 2x). Without flow (frame generation off) the motion is zero.
#pragma once
#include <d3d11.h>
#include <cstdint>
#include <functional>
#include <string>

namespace nr {

// The NIS pass, recognised from what is bound when it is dispatched; false for any other pass.
struct NisPass { ID3D11Resource* in = nullptr; ID3D11Resource* out = nullptr; uint32_t inW = 0, inH = 0, outW = 0, outH = 0; };
bool FindNisPass(ID3D11DeviceContext* ctx, uint32_t x, uint32_t y, uint32_t z, NisPass& pass);   // AddRef's in/out: ReleaseNisPass
void ReleaseNisPass(NisPass& pass);

class Scaler11 {
public:
    using LogFn = std::function<void(const char*)>;
    // NGX on Lossless Scaling's device; NVIDIA's runtime is looked for in runtimeDir (the addon's dlss folder).
    bool Init(ID3D11Device* dev, const std::wstring& dataPath, const std::wstring& runtimeDir, LogFn log);
    void Shutdown();
    bool IsReady() const { return m_ready; }
    bool IsFailed() const { return m_failed; }
    const std::string& LastError() const { return m_error; }
    ID3D11Device* Device() const { return m_dev; }

    // Upscales pass.in into pass.out on ctx, where the NIS pass would have run. flow: frame generation's newest flow (null for none), in
    // units of 1/flowUnit of a flow-texture pixel; motionFraction: the part of a real frame between two presented frames. preset: DLSS
    // preset for this quality (0 = NVIDIA's default). False when DLSS could not run (the NIS pass should then run as usual).
    bool Run(ID3D11DeviceContext* ctx, const NisPass& pass, ID3D11Resource* flow, uint32_t flowW, uint32_t flowH, float flowUnit,
             float motionFraction, unsigned preset, bool reset);

    double GpuMs() const { return m_gpuMs; }       // DLSS's own GPU time, read back a few frames later
    uint64_t Runs() const { return m_runs; }
    uint32_t Builds() const { return m_builds; }

private:
    bool EnsureFeature(ID3D11DeviceContext* ctx, uint32_t inW, uint32_t inH, uint32_t outW, uint32_t outH, unsigned preset);
    bool EnsureInputs(uint32_t inW, uint32_t inH);
    void MakeMotion(ID3D11DeviceContext* ctx, ID3D11Resource* flow, uint32_t flowW, uint32_t flowH, float flowUnit, float fraction);
    void ReadTimes(ID3D11DeviceContext* ctx);
    void Fail(const char* what);
    void Log(const char* fmt, ...);

    LogFn m_log;
    bool m_ready = false, m_failed = false;
    std::string m_error;
    ID3D11Device* m_dev = nullptr;        // Lossless Scaling's (AddRef'd)
    void* m_params = nullptr;             // NVSDK_NGX_Parameter*
    void* m_feature = nullptr;            // NVSDK_NGX_Handle*
    uint32_t m_inW = 0, m_inH = 0, m_outW = 0, m_outH = 0; unsigned m_preset = ~0u;
    // motion vectors (RG16F) and a flat depth (R32F) at the game's size
    ID3D11Texture2D* m_motion = nullptr; ID3D11UnorderedAccessView* m_motionUav = nullptr;
    ID3D11Texture2D* m_depth = nullptr;
    ID3D11ComputeShader* m_motionShader = nullptr; ID3D11Buffer* m_constants = nullptr; ID3D11SamplerState* m_sampler = nullptr;
    // GPU timing: a few queries in rotation, read without waiting
    static const int kQueries = 4;
    ID3D11Query* m_disjoint[kQueries] = {}; ID3D11Query* m_begin[kQueries] = {}; ID3D11Query* m_end[kQueries] = {};
    bool m_queryUsed[kQueries] = {}; int m_nextQuery = 0;
    double m_gpuMs = 0;
    uint64_t m_runs = 0; uint32_t m_builds = 0;
};

} // namespace nr
