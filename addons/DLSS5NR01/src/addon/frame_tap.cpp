#include "addon/frame_tap.h"
#include <cstdarg>
#include <cstdio>
#include <cstdio>
#include <cstring>
#include <windows.h>

// ------------------------------------------------------------------ signature
std::string DispatchSig::Serialize() const {
    char b[1024]; int n = snprintf(b, sizeof b, "%u,%u,%u", x, y, z);
    for (int i = 0; i < 8; ++i) n += snprintf(b + n, sizeof b - n, "|%u:%u:%u", srv[i].valid ? srv[i].w : 0, srv[i].valid ? srv[i].h : 0, srv[i].valid ? srv[i].fmt : 0);
    for (int i = 0; i < 4; ++i) n += snprintf(b + n, sizeof b - n, "|%u:%u:%u", uav[i].valid ? uav[i].w : 0, uav[i].valid ? uav[i].h : 0, uav[i].valid ? uav[i].fmt : 0);
    return b;
}
bool DispatchSig::Parse(const std::string& s) {
    DispatchSig t; const char* p = s.c_str(); int c = 0;
    if (sscanf(p, "%u,%u,%u%n", &t.x, &t.y, &t.z, &c) != 3) return false; p += c;
    for (int i = 0; i < 12; ++i) {
        ViewShape& v = i < 8 ? t.srv[i] : t.uav[i - 8]; unsigned w, h, f;
        if (sscanf(p, "|%u:%u:%u%n", &w, &h, &f, &c) != 3) return false; p += c;
        v.w = w; v.h = h; v.fmt = f; v.valid = (w != 0); v.tex2d = v.valid;
    }
    *this = t; return true;
}
bool DispatchSig::operator==(const DispatchSig& o) const {
    if (x != o.x || y != o.y || z != o.z) return false;
    for (int i = 0; i < 8; ++i) if (srv[i].valid != o.srv[i].valid || (srv[i].valid && (srv[i].w != o.srv[i].w || srv[i].h != o.srv[i].h || srv[i].fmt != o.srv[i].fmt))) return false;
    for (int i = 0; i < 4; ++i) if (uav[i].valid != o.uav[i].valid || (uav[i].valid && (uav[i].w != o.uav[i].w || uav[i].h != o.uav[i].h || uav[i].fmt != o.uav[i].fmt))) return false;
    return true;
}
uint64_t DispatchSig::Hash() const {
    uint64_t h = 1469598103934665603ull; auto mix = [&](uint64_t v) { h ^= v; h *= 1099511628211ull; };
    mix(x); mix(y); mix(z);
    for (int i = 0; i < 8; ++i) { mix(srv[i].valid); if (srv[i].valid) { mix(srv[i].w); mix(srv[i].h); mix(srv[i].fmt); } }
    for (int i = 0; i < 4; ++i) { mix(uav[i].valid); if (uav[i].valid) { mix(uav[i].w); mix(uav[i].h); mix(uav[i].fmt); } }
    return h ? h : 1;
}

// ------------------------------------------------------------------ tap
// Formats a captured frame can have (what the bridge accepts, plus HDR variants for the future).
static bool IsColorFmt(uint32_t f) {
    switch (f) {
    case DXGI_FORMAT_R8G8B8A8_UNORM: case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: case DXGI_FORMAT_R8G8B8A8_TYPELESS:
    case DXGI_FORMAT_B8G8R8A8_UNORM: case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: case DXGI_FORMAT_B8G8R8A8_TYPELESS:
    case DXGI_FORMAT_R10G10B10A2_UNORM: case DXGI_FORMAT_R10G10B10A2_TYPELESS:
    case DXGI_FORMAT_R16G16B16A16_FLOAT: case DXGI_FORMAT_R16G16B16A16_TYPELESS: return true;
    default: return false;
    }
}
void FrameTap::Describe(ID3D11Resource* r, ViewShape& s) {
    s = {}; if (!r) return; s.valid = true;
    D3D11_RESOURCE_DIMENSION dim; r->GetType(&dim);
    if (dim == D3D11_RESOURCE_DIMENSION_TEXTURE2D) {
        ID3D11Texture2D* t = nullptr; if (SUCCEEDED(r->QueryInterface(IID_PPV_ARGS(&t))) && t) { D3D11_TEXTURE2D_DESC d; t->GetDesc(&d); s.w = d.Width; s.h = d.Height; s.fmt = (uint32_t)d.Format; s.tex2d = true; t->Release(); }
    } else { s.w = 1; s.h = 1; s.fmt = 0xFFFF; }
}

void FrameTap::Reset() {
    std::lock_guard<std::mutex> lk(m_mu); m_cache.clear(); m_lastFramePtr = nullptr; memset(m_recent, 0, sizeof m_recent); m_lastNrTick = ~0ull;
    if (m_flowRes) m_flowRes->Release(); if (m_flowCand) m_flowCand->Release(); m_flowRes = m_flowCand = nullptr; m_frameFlowArea = 0; m_flowW = m_flowH = 0;
    m_presentsSinceTap = 0; m_genSinceLastPresent = false; m_perFrame = 0; m_realFirst = false; strcpy(m_pattern, "learning");
}
void FrameTap::ClearTable() { std::lock_guard<std::mutex> lk(m_mu); m_table.clear(); m_cache.clear(); }

PresentInfo FrameTap::NotePresent() {
    std::lock_guard<std::mutex> lk(m_mu);
    PresentInfo p; p.tap = m_taps; p.index = m_presentsSinceTap++; p.gen = m_genSinceLastPresent; m_genSinceLastPresent = false;
    const int n = m_perFrame > 0 ? m_perFrame : 3;   // X3 until an interval has been counted
    p.perFrame = m_perFrame;
    if (m_taps == 0) return p;
    if (!p.gen) {
        // a real frame: the one that just arrived when it comes after the generated frames, the previous one when it comes first
        m_realFirst = (p.index == 0);
        p.target = m_realFirst ? (double)m_taps - 1.0 : (double)m_taps;
    } else {
        double t = m_realFirst ? (double)p.index / n : (double)(p.index + 1) / n;
        if (t > 1.0) t = 1.0;
        p.target = (double)m_taps - 1.0 + t;
    }
    snprintf(m_pattern, sizeof m_pattern, "%d per real frame, real frame %s", n, m_realFirst ? "first" : "last");
    return p;
}

ID3D11Resource* FrameTap::NewestFlow(uint32_t& w, uint32_t& h) {
    std::lock_guard<std::mutex> lk(m_mu);
    ID3D11Resource* r = m_flowCand ? m_flowCand : m_flowRes;
    if (!r) { w = h = 0; return nullptr; }
    if (m_flowCand) { w = m_flowCandW; h = m_flowCandH; } else { w = m_flowW; h = m_flowH; }
    r->AddRef(); return r;
}

void FrameTap::SetRoles(const DispatchSig& tick, const DispatchSig& tap, Mode mode, int frameSlotPref) {
    std::lock_guard<std::mutex> lk(m_mu);
    m_tick = tick; m_tap = tap; m_tickKey = tick.Empty() ? 0 : tick.Hash(); m_tapKey = tap.Empty() ? 0 : tap.Hash(); m_mode = mode; m_frameSlotPref = frameSlotPref;
}
void FrameTap::GetRoles(DispatchSig& tick, DispatchSig& tap) const { std::lock_guard<std::mutex> lk(m_mu); tick = m_tick; tap = m_tap; }
std::vector<DispatchEntry> FrameTap::Snapshot() const { std::lock_guard<std::mutex> lk(m_mu); std::vector<DispatchEntry> v; v.reserve(m_table.size()); for (auto& kv : m_table) v.push_back(kv.second); return v; }

// Auto role, as observed on Lossless Scaling 3.x (LSFG 3): the captured real frame is the largest texture
// in the process, and the FIRST thing LSFG does with a new one is a pyramid/downsample pass that reads the
// full frame and writes only smaller levels (R8). That pass runs exactly once per real frame, before the
// flow and interpolation passes. Running NR on its full-size SRV, in place, right before that dispatch puts
// the enhanced frame in front of everything downstream: the pyramid, the flow, the generated frames and the
// presented real frame. So TAP = that pass, no TICK needed (every occurrence is a new real frame).
// Excluded on purpose: passes writing a full-size COLOR texture (the per-output-frame compose/blit, which
// runs 2-4x more often). A full-size single-channel output (a luma level) is still a downsample pass.
// The frame itself must be a colour texture: LS also keeps full-size R8 luma planes around.
void FrameTap::AutoAssign() {
    // Only shapes dispatched recently count: when the game changes resolution or window mode, LS's old passes
    // stay in the table with their old counts and must not win the "rarest" test below.
    const uint64_t kStaleAfter = 3000;   // dispatches (~20-30 real frames)
    auto fresh = [&](const DispatchEntry& e) { return m_dispatches - e.lastSeen < kStaleAfter; };
    uint32_t lw = 0, lh = 0;
    for (auto& kv : m_table) { if (!fresh(kv.second)) continue; for (auto& s : kv.second.sig.srv) if (s.valid && s.tex2d && IsColorFmt(s.fmt) && (uint64_t)s.w * s.h > (uint64_t)lw * lh) { lw = s.w; lh = s.h; } }
    if (!lw) return;
    m_autoLargestW = lw; m_autoLargestH = lh;
    for (auto& kv : m_table) kv.second.roleAuto = 0;
    DispatchEntry* best = nullptr; DispatchEntry* fallback = nullptr;
    for (auto& kv : m_table) {
        auto& e = kv.second; bool full = false, anyUav = false, allSmaller = true;
        if (!fresh(e)) continue;
        for (auto& s : e.sig.srv) if (s.valid && s.w == lw && s.h == lh && IsColorFmt(s.fmt)) full = true;
        for (auto& u : e.sig.uav) if (u.valid) { anyUav = true; if (u.tex2d && u.w >= lw && u.h >= lh && IsColorFmt(u.fmt)) allSmaller = false; }
        if (!full || !anyUav || !allSmaller) continue;
        if (!fallback || e.count > fallback->count) fallback = &e;                       // early on: most frequent candidate
        if (e.count >= 10 && (!best || e.count < best->count)) best = &e;                 // settled: the per-real-frame one is the rarest
    }
    DispatchEntry* tap = best ? best : fallback;
    if (!tap) return;
    tap->roleAuto = 2;
    if (m_mode == Auto) { m_tap = tap->sig; m_tapKey = tap->key; m_tick = DispatchSig{}; m_tickKey = 0; }
}

bool FrameTap::Observe(ID3D11DeviceContext* ctx, uint32_t x, uint32_t y, uint32_t z, TapDecision& d) {
    d = {}; m_dispatches++;
    ID3D11ComputeShader* cs = nullptr; ctx->CSGetShader(&cs, nullptr, nullptr); if (cs) cs->Release();
    ID3D11ShaderResourceView* srvs[8] = {}; ctx->CSGetShaderResources(0, 8, srvs);
    ID3D11UnorderedAccessView* uavs[4] = {}; ctx->CSGetUnorderedAccessViews(0, 4, uavs);
    ID3D11Resource* srvRes[8] = {};
    DispatchSig sig; sig.x = x; sig.y = y; sig.z = z;
    for (int i = 0; i < 8; ++i) if (srvs[i]) { srvs[i]->GetResource(&srvRes[i]); Describe(srvRes[i], sig.srv[i]); }
    ID3D11Resource* uavRes[4] = {};
    for (int i = 0; i < 4; ++i) if (uavs[i]) { uavs[i]->GetResource(&uavRes[i]); Describe(uavRes[i], sig.uav[i]); }
    uint64_t key = sig.Hash();

    bool run = false;
    {
        std::lock_guard<std::mutex> lk(m_mu);
        auto& e = m_table[key]; if (e.count == 0) { e.sig = sig; e.key = key; } e.cs = cs; e.count++; e.lastSeenFrame = m_frameCounter; e.lastSeen = m_dispatches;
        if (m_table.size() > 256) m_table.clear();
        if (m_mode == Auto && (m_tapKey == 0 || (m_dispatches - m_lastAutoAssign) > 600)) { AutoAssign(); m_lastAutoAssign = m_dispatches; }
        if (m_mode == Auto && m_tapKey) { auto it = m_table.find(m_tapKey); if (it == m_table.end() || m_dispatches - it->second.lastSeen > 3000) { AutoAssign(); m_lastAutoAssign = m_dispatches; } }
        if (m_tickKey && key == m_tickKey) { d.isTick = true; m_ticks++; m_frameCounter++; }
        if (m_tapKey && key == m_tapKey) {
            d.isTap = true; m_taps++;
            if (m_presentsSinceTap > 0) m_perFrame = m_presentsSinceTap;
            m_presentsSinceTap = 0; m_genSinceLastPresent = false;
            // frame N = the highest (or preferred) SRV slot holding a texture of TAP's largest size
            uint32_t lw = 0, lh = 0; for (auto& s : sig.srv) if (s.valid && IsColorFmt(s.fmt) && (uint64_t)s.w * s.h > (uint64_t)lw * lh) { lw = s.w; lh = s.h; }
            int slot = -1;
            if (m_frameSlotPref >= 0 && m_frameSlotPref < 8 && srvRes[m_frameSlotPref]) slot = m_frameSlotPref;
            else for (int i = 7; i >= 0; --i) if (srvRes[i] && sig.srv[i].valid && sig.srv[i].w == lw && sig.srv[i].h == lh && IsColorFmt(sig.srv[i].fmt)) { slot = i; break; }
            if (slot >= 0) {
                // With a TICK: once per tick. Without one (the normal LSFG case): every occurrence of the TAP
                // pass is a new real frame, so run every time.
                bool gate;
                if (m_tickKey) { gate = (m_lastNrTick != m_ticks); m_gateName = "tick"; }
                else { gate = true; m_gateName = "every"; }
                if (gate) {
                    ID3D11Texture2D* t = nullptr; if (SUCCEEDED(srvRes[slot]->QueryInterface(IID_PPV_ARGS(&t))) && t) { d.frame = t; d.frameSlot = slot; run = true; }
                    m_lastNrTick = m_ticks; m_lastFramePtr = srvRes[slot];
                }
            }
        }
        // LSFG flow: between two TAPs the flow passes run coarse to fine; the first pass at each larger level is the
        // main chain (the second is a weaker auxiliary field). At the TAP the finest candidate becomes this frame's flow.
        if (d.isTap) {
            // No flow pass since the last TAP (LS skipped it, e.g. a duplicated capture): keep the previous flow rather
            // than flip to zero vectors for one frame, which the in-game log showed as an on/off toggle every frame.
            if (m_flowCand) { if (m_flowRes) m_flowRes->Release(); m_flowRes = m_flowCand; m_flowW = m_flowCandW; m_flowH = m_flowCandH; }
            m_flowCand = nullptr; m_frameFlowArea = 0;
            if (m_flowRes && d.frame) { ID3D11Texture2D* ft = nullptr; if (SUCCEEDED(m_flowRes->QueryInterface(IID_PPV_ARGS(&ft))) && ft) { d.flow = ft; d.flowW = m_flowW; d.flowH = m_flowH; } }
        } else if (uavRes[0] && sig.uav[0].valid && sig.uav[0].fmt == 10 /*RGBA16F*/ && srvRes[4]) {
            uint64_t area = (uint64_t)sig.uav[0].w * sig.uav[0].h;
            if (area > m_frameFlowArea) { if (m_flowCand) m_flowCand->Release(); uavRes[0]->AddRef(); m_flowCand = uavRes[0]; m_flowCandW = sig.uav[0].w; m_flowCandH = sig.uav[0].h; m_frameFlowArea = area; }
        } else if (uavRes[0] && sig.uav[0].valid && sig.uav[0].tex2d && IsColorFmt(sig.uav[0].fmt) && m_autoLargestW && sig.uav[0].w == m_autoLargestW && sig.uav[0].h == m_autoLargestH) {
            // A generated frame being composed: a full-size colour output fed by two full-size colour frames, or by
            // a colour frame plus a flow field. (The real frame reaches the screen through a copy or a single-input pass.)
            int fullColour = 0; bool anyFlow = false;
            for (auto& v : sig.srv) if (v.valid && v.tex2d) { if (IsColorFmt(v.fmt) && v.w == m_autoLargestW && v.h == m_autoLargestH) fullColour++; if (v.fmt == 10) anyFlow = true; }
            if (fullColour >= 2 || anyFlow) m_genSinceLastPresent = true;
        }
    }
    // ---- flow probe (diagnostic; only while armed from the panel)
    if (m_probeArmed) {
        std::lock_guard<std::mutex> lk(m_mu);
        if (m_probeArmed) {
            // the finest flow pass: UAV0 is RGBA16F; keep the largest such area as "finest"
            if (uavRes[0] && sig.uav[0].valid && sig.uav[0].fmt == 10 /*RGBA16F*/) {
                uint64_t area = (uint64_t)sig.uav[0].w * sig.uav[0].h;
                if (area > m_probeFlowArea) { m_probeFlowArea = area; for (auto& f : m_probeFlows) f.res->Release(); m_probeFlows.clear(); }
                if (area == m_probeFlowArea && m_probeFlows.size() < 8) { uavRes[0]->AddRef(); m_probeFlows.push_back({ uavRes[0], sig, m_dispatches });
                    ProbeLog("flow pass #%llu U0=%p (%ux%u) S4=%p S5=%p (%u,%u,%u)", (unsigned long long)m_dispatches, (void*)uavRes[0], sig.uav[0].w, sig.uav[0].h, (void*)srvRes[4], (void*)srvRes[5], x, y, z); }
            }
            // full-size compose pass (S0 full-size colour in, U0 full-size colour out): who is S0?
            if (!d.isTap && srvRes[0] && uavRes[0] && sig.srv[0].valid && sig.uav[0].valid && IsColorFmt(sig.srv[0].fmt) && IsColorFmt(sig.uav[0].fmt)
                && sig.srv[0].w == m_autoLargestW && sig.srv[0].h == m_autoLargestH && sig.uav[0].w == m_autoLargestW && m_probeCompose.size() < 12) {
                m_probeCompose.push_back({ (void*)srvRes[0], m_dispatches });
                ProbeLog("compose pass #%llu S0=%p U0=%p", (unsigned long long)m_dispatches, (void*)srvRes[0], (void*)uavRes[0]);
            }
            if (d.isTap && d.frame) {
                ProbeLog("TAP #%llu frame=%p (%ux%u): dumping frame + %zu flow texture(s)", (unsigned long long)m_dispatches, (void*)d.frame, m_autoLargestW, m_autoLargestH, m_probeFlows.size());
                ProbeDump(ctx, d.frame, L"frame", m_probeTaps, 0);
                int k = 0; for (auto& f : m_probeFlows) { ProbeDump(ctx, f.res, L"flow", m_probeTaps, k++); f.res->Release(); }
                m_probeFlows.clear();
                if (++m_probeTaps >= 3) { m_probeArmed = false; m_probeStatus += "\nprobe done: files in the LS folder (flowprobe_*.bin)"; }
            }
        }
    }
    for (int i = 0; i < 8; ++i) { if (srvRes[i]) srvRes[i]->Release(); if (srvs[i]) srvs[i]->Release(); }
    for (int i = 0; i < 4; ++i) { if (uavRes[i]) uavRes[i]->Release(); if (uavs[i]) uavs[i]->Release(); }
    return run;
}

void FrameTap::ArmProbe(const std::wstring& dir) {
    std::lock_guard<std::mutex> lk(m_mu);
    for (auto& f : m_probeFlows) f.res->Release(); m_probeFlows.clear(); m_probeCompose.clear();
    m_probeDir = dir; m_probeTaps = 0; m_probeFlowArea = 0; m_probeStatus = "armed: waiting for 3 taps"; m_probeArmed = true;
}
std::string FrameTap::ProbeStatus() const { std::lock_guard<std::mutex> lk(m_mu); return m_probeStatus; }
void FrameTap::ProbeLog(const char* fmt, ...) {
    char b[512]; va_list a; va_start(a, fmt); vsnprintf(b, sizeof b, fmt, a); va_end(a);
    if (m_probeStatus.size() < 6000) { m_probeStatus += "\n"; m_probeStatus += b; }
    FILE* f = _wfopen((m_probeDir + L"\\flowprobe_log.txt").c_str(), L"a"); if (f) { fprintf(f, "%s\n", b); fclose(f); }
}
// Copies the texture to a staging copy and writes it tightly packed: flowprobe_<tag><idx>_<sub>_<w>x<h>_fmt<f>.bin
void FrameTap::ProbeDump(ID3D11DeviceContext* ctx, ID3D11Resource* r, const wchar_t* tag, int idx, int sub) {
    ID3D11Texture2D* t = nullptr; if (FAILED(r->QueryInterface(IID_PPV_ARGS(&t))) || !t) return;
    D3D11_TEXTURE2D_DESC d; t->GetDesc(&d);
    uint32_t bpp = d.Format == DXGI_FORMAT_R16G16B16A16_FLOAT ? 8 : (d.Format == DXGI_FORMAT_R8_UNORM ? 1 : d.Format == DXGI_FORMAT_R16G16_FLOAT ? 4 : 4);
    D3D11_TEXTURE2D_DESC sd = d; sd.Usage = D3D11_USAGE_STAGING; sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ; sd.BindFlags = 0; sd.MiscFlags = 0; sd.MipLevels = 1; sd.ArraySize = 1;
    ID3D11Device* dev = nullptr; ctx->GetDevice(&dev);
    ID3D11Texture2D* st = nullptr; HRESULT hr = dev ? dev->CreateTexture2D(&sd, nullptr, &st) : E_FAIL;
    if (SUCCEEDED(hr) && st) {
        ctx->CopyResource(st, t);
        D3D11_MAPPED_SUBRESOURCE m{}; hr = ctx->Map(st, 0, D3D11_MAP_READ, 0, &m);
        if (SUCCEEDED(hr)) {
            wchar_t name[MAX_PATH]; swprintf(name, MAX_PATH, L"%s\\flowprobe_%s%d_%d_%ux%u_fmt%u.bin", m_probeDir.c_str(), tag, idx, sub, d.Width, d.Height, (unsigned)d.Format);
            FILE* f = _wfopen(name, L"wb");
            if (f) { for (UINT y = 0; y < d.Height; ++y) fwrite((const uint8_t*)m.pData + (size_t)y * m.RowPitch, 1, (size_t)d.Width * bpp, f); fclose(f); }
            ctx->Unmap(st, 0);
            ProbeLog("  wrote %ls (%s)", name, f ? "ok" : "OPEN FAILED");
        } else ProbeLog("  Map failed 0x%08x for %ls (deferred context?)", (unsigned)hr, tag);
        st->Release();
    } else ProbeLog("  staging CreateTexture2D failed 0x%08x", (unsigned)hr);
    if (dev) dev->Release(); t->Release();
}
