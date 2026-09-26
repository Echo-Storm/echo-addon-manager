#pragma once
#include <cstdint>

namespace eam {

// Memory and processor use of the whole machine, for the readout in the window's header. Read on demand from the thread that asks
// (two cheap system calls), at most once a second; nothing runs while nobody asks.
class SystemStats {
public:
    struct Snapshot {
        bool ok = false;
        double cpuPercent = 0;               // all cores, since the sample before (0 on the first)
        uint64_t ramUsedMB = 0, ramTotalMB = 0;
    };
    static SystemStats& Instance();
    Snapshot Get();                          // a fresh sample when the last is a second old
    void InjectForPreview(const Snapshot& s) { m_snap = s; m_injected = true; }

private:
    Snapshot m_snap;
    uint64_t m_idle = 0, m_total = 0, m_takenAtMs = 0;
    bool m_injected = false;
};

} // namespace eam
