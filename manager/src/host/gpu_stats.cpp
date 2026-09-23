#include "gpu_stats.h"
#include "metrics.h"
#include <windows.h>
#include <chrono>

namespace eam {

namespace {
using nvmlReturn = int;   // 0 = success
struct nvmlUtil { unsigned gpu, memory; };
struct nvmlMem { unsigned long long total, free, used; };

struct Api {
    nvmlReturn (*Init)() = nullptr;
    nvmlReturn (*Shutdown)() = nullptr;
    nvmlReturn (*GetCount)(unsigned*) = nullptr;
    nvmlReturn (*GetHandle)(unsigned, void**) = nullptr;
    nvmlReturn (*GetName)(void*, char*, unsigned) = nullptr;
    nvmlReturn (*GetUtil)(void*, nvmlUtil*) = nullptr;
    nvmlReturn (*GetPower)(void*, unsigned*) = nullptr;
    nvmlReturn (*GetPowerLimit)(void*, unsigned*) = nullptr;
    nvmlReturn (*GetClock)(void*, int, unsigned*) = nullptr;
    nvmlReturn (*GetTemp)(void*, int, unsigned*) = nullptr;
    nvmlReturn (*GetMem)(void*, nvmlMem*) = nullptr;
    nvmlReturn (*GetThrottle)(void*, unsigned long long*) = nullptr;
    nvmlReturn (*GetDriver)(char*, unsigned) = nullptr;
};
Api g_api;

int64_t NowMs() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
}

GpuStats& GpuStats::Instance() { static GpuStats g; return g; }

bool GpuStats::Init() {
    if (m_inited) return m_dev != nullptr;
    m_inited = true;
    HMODULE lib = LoadLibraryW(L"nvml.dll");
    if (!lib) lib = LoadLibraryW(L"C:\\Program Files\\NVIDIA Corporation\\NVSMI\\nvml.dll");
    if (!lib) { std::lock_guard<std::mutex> lk(m_mutex); m_snap.why = "nvml.dll was not found (it comes with the NVIDIA driver)"; return false; }
    m_lib = lib;
#define BIND(field, name) g_api.field = (decltype(g_api.field))GetProcAddress(lib, name)
    BIND(Init, "nvmlInit_v2"); BIND(Shutdown, "nvmlShutdown"); BIND(GetCount, "nvmlDeviceGetCount_v2"); BIND(GetHandle, "nvmlDeviceGetHandleByIndex_v2");
    BIND(GetName, "nvmlDeviceGetName"); BIND(GetUtil, "nvmlDeviceGetUtilizationRates"); BIND(GetPower, "nvmlDeviceGetPowerUsage");
    BIND(GetPowerLimit, "nvmlDeviceGetEnforcedPowerLimit"); BIND(GetClock, "nvmlDeviceGetClockInfo"); BIND(GetTemp, "nvmlDeviceGetTemperature");
    BIND(GetMem, "nvmlDeviceGetMemoryInfo"); BIND(GetThrottle, "nvmlDeviceGetCurrentClocksThrottleReasons"); BIND(GetDriver, "nvmlSystemGetDriverVersion");
#undef BIND
    if (!g_api.Init || !g_api.GetCount || !g_api.GetHandle) { std::lock_guard<std::mutex> lk(m_mutex); m_snap.why = "nvml.dll is missing functions"; return false; }
    if (g_api.Init() != 0) { std::lock_guard<std::mutex> lk(m_mutex); m_snap.why = "NVML could not start"; return false; }
    unsigned n = 0;
    if (g_api.GetCount(&n) != 0 || n == 0) { std::lock_guard<std::mutex> lk(m_mutex); m_snap.why = "no NVIDIA GPU reported"; return false; }
    void* dev = nullptr;
    if (g_api.GetHandle(0, &dev) != 0) { std::lock_guard<std::mutex> lk(m_mutex); m_snap.why = "could not open the GPU"; return false; }
    m_dev = dev;
    char name[128] = {}, drv[64] = {};
    if (g_api.GetName) g_api.GetName(dev, name, sizeof name);
    if (g_api.GetDriver) g_api.GetDriver(drv, sizeof drv);
    std::lock_guard<std::mutex> lk(m_mutex);
    m_snap.name = name; m_snap.driver = drv; m_snap.deviceCount = (int)n;
    return true;
}

bool GpuStats::SampleOnce() {
    if (!Init()) return false;
    Snapshot s;
    { std::lock_guard<std::mutex> lk(m_mutex); s = m_snap; }
    s.ok = true; s.why.clear();
    nvmlUtil u{}; if (g_api.GetUtil && g_api.GetUtil(m_dev, &u) == 0) { s.utilGpu = u.gpu; s.utilMem = u.memory; }
    unsigned v = 0;
    if (g_api.GetPower && g_api.GetPower(m_dev, &v) == 0) s.powerW = v / 1000.0;
    if (g_api.GetPowerLimit && g_api.GetPowerLimit(m_dev, &v) == 0) s.powerLimitW = v / 1000.0;
    if (g_api.GetClock && g_api.GetClock(m_dev, 0, &v) == 0) s.clockGraphics = v;   // NVML_CLOCK_GRAPHICS
    if (g_api.GetClock && g_api.GetClock(m_dev, 2, &v) == 0) s.clockMem = v;        // NVML_CLOCK_MEM
    if (g_api.GetTemp && g_api.GetTemp(m_dev, 0, &v) == 0) s.tempC = v;             // NVML_TEMPERATURE_GPU
    nvmlMem m{}; if (g_api.GetMem && g_api.GetMem(m_dev, &m) == 0) { s.vramUsedMB = m.used >> 20; s.vramTotalMB = m.total >> 20; }
    unsigned long long th = 0; if (g_api.GetThrottle && g_api.GetThrottle(m_dev, &th) == 0) s.throttle = th;
    s.ageSeconds = 0;
    { std::lock_guard<std::mutex> lk(m_mutex); m_snap = s; m_takenAtMs = NowMs(); }
    Metrics& M = Metrics::Instance();
    M.Publish("system", "gpu_util", s.utilGpu, "%");
    M.Publish("system", "gpu_power_w", s.powerW, "W");
    M.Publish("system", "gpu_clock_mhz", s.clockGraphics, "MHz");
    M.Publish("system", "gpu_temp_c", s.tempC, "C");
    M.Publish("system", "vram_used_mb", (double)s.vramUsedMB, "MB");
    return true;
}

void GpuStats::InjectForPreview(const Snapshot& s) {
    std::lock_guard<std::mutex> lk(m_mutex);
    m_snap = s; m_snap.ok = true; m_takenAtMs = NowMs();
}

void GpuStats::Run() {
    while (!m_stop) {
        const int64_t now = NowMs();
        if (now - m_wantedAtMs.load() < 4000) SampleOnce();   // only while the tab is being looked at
        for (int i = 0; i < 5 && !m_stop; ++i) Sleep(100);
    }
}

void GpuStats::Wanted() {
    m_wantedAtMs = NowMs();
    if (!m_started.exchange(true)) m_thread = std::thread([this] { Run(); });
}

GpuStats::Snapshot GpuStats::Get() const {
    std::lock_guard<std::mutex> lk(m_mutex);
    Snapshot s = m_snap;
    if (s.ok) s.ageSeconds = (NowMs() - m_takenAtMs) / 1000.0;
    return s;
}

void GpuStats::Shutdown() {
    m_stop = true;
    if (m_thread.joinable()) m_thread.join();
    if (m_inited && m_dev && g_api.Shutdown) g_api.Shutdown();   // the library stays loaded (the process is ending)
    m_dev = nullptr;
}

std::string GpuStats::ThrottleText(uint64_t m) {
    std::string out;
    auto add = [&](uint64_t bit, const char* text) { if (m & bit) { if (!out.empty()) out += ", "; out += text; } };
    add(0x4, "power limit");            // SW power cap
    add(0x80, "power brake");           // HW power brake
    add(0x20, "temperature");           // SW thermal slowdown
    add(0x40, "temperature (hardware)");
    add(0x8, "hardware slowdown");
    add(0x10, "sync boost");
    add(0x2, "application clocks");
    return out;                         // 0x1 (idle) and 0x100 (display clocks) are not worth showing
}

} // namespace eam
