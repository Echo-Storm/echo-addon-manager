// FrameTap — recognises Lossless Scaling's compute dispatches by shape and decides when NR runs.
// Two roles: TICK (LSFG flow pass, once per real frame) and TAP (LSFG's first pass on a new real frame, where
// the frame is bound). Signatures are (x,y,z) + SRV/UAV shapes; shader pointers are matched live but never
// persisted (they change with every device). It also keeps the present-side bookkeeping: which of LS's presents
// are generated frames and where each one sits between two real frames.
#pragma once
#include <d3d11.h>
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>

struct ViewShape { uint32_t w = 0, h = 0; uint32_t fmt = 0; bool valid = false; bool tex2d = false; };
struct DispatchSig {
    uint32_t x = 0, y = 0, z = 0;
    ViewShape srv[8]; ViewShape uav[4];
    std::string Serialize() const;
    bool Parse(const std::string& s);
    bool operator==(const DispatchSig& o) const;
    uint64_t Hash() const;
    bool Empty() const { return x == 0 && y == 0 && z == 0; }
};

struct DispatchEntry {
    DispatchSig sig; uint64_t key = 0; void* cs = nullptr;
    uint32_t count = 0, countThisTick = 0, perFrame = 0; uint64_t lastSeenFrame = 0; uint64_t lastSeen = 0; int roleAuto = 0;   // 1 tick, 2 tap; lastSeen = dispatch counter
};

struct TapDecision {
    bool isTick = false, isTap = false;
    ID3D11Texture2D* frame = nullptr;   // borrowed (AddRef'd by us; caller releases)
    int frameSlot = -1;
    // LSFG's finest optical flow, as written after the PREVIOUS real frame (AddRef'd; caller releases). RGBA16F:
    // xy = displacement current -> previous frame, zw = the reverse; units = pixels of a texture twice this size.
    ID3D11Texture2D* flow = nullptr; uint32_t flowW = 0, flowH = 0;
};

// One of LS's presents, placed between real frames. With N presents per real frame and the real frame k
// arriving at the tap, LS shows k-1 (or the generated frames between k-1 and k) and finally k.
struct PresentInfo {
    uint64_t tap = 0;        // taps so far = index k of the newest real frame
    int index = 0;           // ordinal of this present since that tap
    int perFrame = 0;        // presents per real frame, learned from the last complete interval (0 = not yet)
    bool gen = false;        // a generated frame: an interpolation compose ran since the previous present
    double target = -1;      // the frame this present shows, in real-frame units (k-1 .. k); < 0 = before any tap
};

class FrameTap {
public:
    enum Mode { Auto = 0, Manual = 1 };
    void Reset();                                  // device changed: forget pointers/cache
    void SetRoles(const DispatchSig& tick, const DispatchSig& tap, Mode mode, int frameSlotPref /* -1 auto */);
    void GetRoles(DispatchSig& tick, DispatchSig& tap) const;
    // Called for every dispatch on LS's render thread. Fills `d`; returns true if NR should run now.
    bool Observe(ID3D11DeviceContext* ctx, uint32_t x, uint32_t y, uint32_t z, TapDecision& d);
    // Called for every Present of LS's swap chain, on the presenting thread.
    PresentInfo NotePresent();
    // The flow LSFG wrote most recently (AddRef'd; caller releases): the pair (k-1,k) once its flow pass ran.
    ID3D11Resource* NewestFlow(uint32_t& w, uint32_t& h);
    const char* PresentPattern() const { return m_pattern; }
    std::vector<DispatchEntry> Snapshot() const;   // for the panel
    uint64_t Ticks() const { return m_ticks; }
    uint64_t Taps() const { return m_taps; }
    uint64_t Dispatches() const { return m_dispatches; }
    const char* GateName() const { return m_gateName; }
    void ClearTable();

    // Flow probe (diagnostic): over the next three TAPs, write the tapped frame and every RGBA16F texture the
    // finest LSFG flow pass wrote since the previous TAP to <dir>\flowprobe_*.bin, and log their pointers.
    // Read on the next TAP, the flow textures relate the two previously tapped frames.
    void ArmProbe(const std::wstring& dir);
    std::string ProbeStatus() const;

private:
    static void Describe(ID3D11Resource* r, ViewShape& s);
    void AutoAssign();
    mutable std::mutex m_mu;
    std::unordered_map<uint64_t, DispatchEntry> m_table;
    struct CacheKey { void* cs; uint32_t x, y, z; bool operator==(const CacheKey& o) const { return cs == o.cs && x == o.x && y == o.y && z == o.z; } };
    struct CacheHash { size_t operator()(const CacheKey& k) const { return std::hash<void*>()(k.cs) ^ (k.x * 73856093u) ^ (k.y * 19349663u) ^ (k.z * 83492791u); } };
    std::unordered_map<CacheKey, uint64_t, CacheHash> m_cache;
    DispatchSig m_tick, m_tap; uint64_t m_tickKey = 0, m_tapKey = 0; Mode m_mode = Auto; int m_frameSlotPref = -1;
    uint64_t m_ticks = 0, m_taps = 0, m_dispatches = 0, m_lastNrTick = ~0ull, m_frameCounter = 0;
    void* m_lastFramePtr = nullptr; uint64_t m_lastNrQpc = 0; void* m_recent[4] = {}; int m_recentIdx = 0;
    const char* m_gateName = "none";
    uint32_t m_autoLargestW = 0, m_autoLargestH = 0; uint64_t m_lastAutoAssign = 0;

    // LSFG flow tracking: the first finest-level RGBA16F UAV0 pass with a coarser level at S4 after each TAP
    ID3D11Resource* m_flowRes = nullptr; uint32_t m_flowW = 0, m_flowH = 0;           // handed out at the TAP
    ID3D11Resource* m_flowCand = nullptr; uint32_t m_flowCandW = 0, m_flowCandH = 0; uint64_t m_frameFlowArea = 0;

    // present bookkeeping (under m_mu)
    int m_presentsSinceTap = 0; bool m_genSinceLastPresent = false; int m_perFrame = 0; bool m_realFirst = false; char m_pattern[64] = "learning";

    // probe state (all under m_mu)
    bool m_probeArmed = false; std::wstring m_probeDir; int m_probeTaps = 0; std::string m_probeStatus = "idle";
    uint64_t m_probeFlowArea = 0;                                            // area of the finest RGBA16F UAV0 seen
    struct ProbeTex { ID3D11Resource* res; DispatchSig sig; uint64_t dispatch; };
    std::vector<ProbeTex> m_probeFlows;                                      // flow textures written since the last TAP
    struct ProbeCompose { void* srv0; uint64_t dispatch; };
    std::vector<ProbeCompose> m_probeCompose;
    void ProbeLog(const char* fmt, ...);
    void ProbeDump(ID3D11DeviceContext* ctx, ID3D11Resource* res, const wchar_t* tag, int tapIdx, int sub);
};
