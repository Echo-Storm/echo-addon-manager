#include "system_stats.h"
#include <windows.h>

namespace eam {

namespace {
uint64_t Ticks(const FILETIME& f) { return (static_cast<uint64_t>(f.dwHighDateTime) << 32) | f.dwLowDateTime; }
}

SystemStats& SystemStats::Instance() {
    static SystemStats instance;
    return instance;
}

SystemStats::Snapshot SystemStats::Get() {
    if (m_injected) return m_snap;
    const uint64_t now = GetTickCount64();
    if (m_takenAtMs && now - m_takenAtMs < 1000) return m_snap;
    m_takenAtMs = now;

    MEMORYSTATUSEX mem{}; mem.dwLength = sizeof mem;
    if (GlobalMemoryStatusEx(&mem)) {
        m_snap.ramTotalMB = mem.ullTotalPhys / (1024 * 1024);
        m_snap.ramUsedMB = (mem.ullTotalPhys - mem.ullAvailPhys) / (1024 * 1024);
        m_snap.ok = true;
    }
    // processor: the share of time not idle since the last sample (kernel time includes idle time)
    FILETIME idle{}, kernel{}, user{};
    if (GetSystemTimes(&idle, &kernel, &user)) {
        const uint64_t i = Ticks(idle), total = Ticks(kernel) + Ticks(user);
        if (m_total && total > m_total) {
            const double busy = 1.0 - static_cast<double>(i - m_idle) / static_cast<double>(total - m_total);
            m_snap.cpuPercent = busy < 0 ? 0 : busy > 1 ? 100.0 : busy * 100.0;
        }
        m_idle = i; m_total = total;
    }
    return m_snap;
}

} // namespace eam
