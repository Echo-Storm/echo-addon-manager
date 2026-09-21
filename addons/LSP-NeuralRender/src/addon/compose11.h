// Compose11 — applies the model's work-res delta to a frame Lossless Scaling is about to present, on LS's own
// D3D11 device. One compute pass per presented frame (real or generated):
//
//   out = frame + upsample(delta of frame d, sampled where that content sits in this frame)
//
// The delta belongs to real frame d; the presented frame sits `offset` frames after it (0 = the same frame,
// 1/3 and 2/3 = generated frames of the next interval, 1 = the next real frame when its own delta is not done,
// >1 = the model is behind). The delta is moved with LSFG's flow: for offset > 0 along the previous->current
// field (zw), for offset < 0 (the delta is from the later frame) back along current->previous (xy).
#pragma once
#include <d3d11.h>
#include <cstdint>
#include <functional>

class Compose11 {
public:
    using LogFn = std::function<void(const char*)>;
    struct Args {
        ID3D11Texture2D* target = nullptr;             // the swap chain buffer about to be presented (read and written)
        ID3D11ShaderResourceView* delta = nullptr;     // work-res delta, RGBA16F
        ID3D11Resource* flow = nullptr; uint32_t flowW = 0, flowH = 0; float flowUnit = 2.0f;   // LSFG flow (RGBA16F) or nullptr
        float offset = 0;                              // presented frame - d, in real frames
        float intensity = 1, maxDelta = 0.5f, hiProtect = 0.85f; uint32_t debugView = 0; bool isGen = false;
        float ghostGuard = 0;                          // 0 = off; see the shader: fades the delta where the motion fields disagree and with its age
        float sharpen = 0;                             // 0 = off; CAS-style, on the composed frame
        float saturation = 1, vibrance = 0;            // colour: 1 / 0 = unchanged
        float brightness = 0, contrast = 1, gamma = 1; // tone: 0 / 1 / 1 = unchanged
        float shadows = 0, highlights = 0;             // tonal ranges: 0 = unchanged
        float grain = 0, grainSize = 1; uint32_t grainSeed = 0;   // film grain (0 = off), cell size in px, per-present seed
        uint32_t hudCount = 0; float hud[6][4] = {}; float hudFeather = 0.004f; bool hudShow = false;   // areas left untouched
        uint32_t compare = 0;                          // 0 enhanced, 1 split (left = original), 2 original only
        float splitPos = 0.5f;                         // where the split line sits, 0..1 of the width
        uint32_t marker = 0;                           // corner square for feedback: 1 green, 2 red, 3 amber, 4 blue, 5 purple
    };
    bool Init(ID3D11Device* dev, LogFn log);
    void Shutdown();
    bool IsReady() const { return m_cs != nullptr; }
    bool Run(ID3D11DeviceContext* ctx, const Args& a);
    double CpuMs() const { return m_cpuMs; }
    const char* TargetInfo() const { return m_targetInfo; }
    uint64_t Runs() const { return m_runs; }

private:
    bool EnsureScratch(const D3D11_TEXTURE2D_DESC& td, DXGI_FORMAT vf);
    ID3D11ShaderResourceView* FlowSrv(ID3D11Resource* flow);
    void Log(const char* fmt, ...);
    LogFn m_log;
    ID3D11Device* m_dev = nullptr;
    ID3D11ComputeShader* m_cs = nullptr; ID3D11Buffer* m_cb = nullptr; ID3D11SamplerState* m_samp = nullptr;
    ID3D11Texture2D* m_src = nullptr; ID3D11ShaderResourceView* m_srcSrv = nullptr;   // copy of the target (the pass reads it)
    ID3D11Texture2D* m_dst = nullptr; ID3D11UnorderedAccessView* m_dstUav = nullptr;  // the result when the target has no UAV access
    uint32_t m_sw = 0, m_sh = 0; DXGI_FORMAT m_sfmt = DXGI_FORMAT_UNKNOWN;
    ID3D11Resource* m_flowRes = nullptr; ID3D11ShaderResourceView* m_flowSrv = nullptr;   // cached per flow resource
    double m_cpuMs = 0; uint64_t m_runs = 0; char m_targetInfo[96] = "none yet"; uint64_t m_lastFailKey = 0;
};
