// Offline test for LSP_Windowed.dll: no windows, no game. Loads the addon into this process, lets it hook DXGI and user32, then
// enumerates outputs the way Lossless Scaling does and checks the virtual display appears only while the addon is enabled.
//
//   windowed_proxy_test.exe <path to LSP_Windowed.dll>
#include <windows.h>
#include <dxgi1_6.h>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <thread>
#include <lsproxy/addon_sdk.h>
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "user32.lib")

struct FakeHost : IHost {
    std::map<std::string, std::string> cfg;
    void Log(LsProxyLogLevel, const char* m) override { printf("  [addon] %s\n", m); }
    const char* GetConfig(const char*, const char* k, const char* d) override { auto it = cfg.find(k); return it == cfg.end() ? d : it->second.c_str(); }
    void SetConfig(const char*, const char* k, const char* v) override { cfg[k] = v; }
    void SaveConfig() override {}
    uint32_t GetHostVersion() override { return 0x10000; }
    void SubscribeEvent(uint32_t, LsProxyEventCallback, void*) override {}
    void UnsubscribeEvent(uint32_t, LsProxyEventCallback) override {}
    void PublishEvent(uint32_t, const void*, uint32_t) override {}
    void* GetD3D11Device() override { return nullptr; }
    void* GetD3D11DeviceContext() override { return nullptr; }
    void SetPreDispatchCallback(LsProxyPreDispatchCallback, void*) override {}
    void SetPostDispatchCallback(LsProxyPostDispatchCallback, void*) override {}
    void* GetCurrentComputeShader() override { return nullptr; }
    uint32_t GetDispatchCount() override { return 0; }
    void SetStatus(const char*, const char*, int) override {}
    void PublishMetric(const char*, const char*, double, const char*) override {}
};

static int g_fail = 0;
static void Check(const char* what, bool ok) { printf("%s  %s\n", ok ? "PASS" : "FAIL", what); if (!ok) g_fail++; }

struct Outputs { int count = 0; bool hasVirtual = false; };

static Outputs EnumDxgi() {
    Outputs o;
    IDXGIFactory1* f = nullptr;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&f)))) return o;
    IDXGIAdapter1* a = nullptr;
    if (SUCCEEDED(f->EnumAdapters1(0, &a))) {
        for (UINT i = 0;; ++i) {
            IDXGIOutput* out = nullptr;
            if (a->EnumOutputs(i, &out) == DXGI_ERROR_NOT_FOUND) break;
            DXGI_OUTPUT_DESC d{};
            out->GetDesc(&d);
            o.count++;
            if (wcsstr(d.DeviceName, L"DISPLAY_VIRTUAL")) o.hasVirtual = true;
            out->Release();
            if (i > 16) break;
        }
        a->Release();
    }
    f->Release();
    return o;
}

static BOOL CALLBACK CountMon(HMONITOR, HDC, LPRECT, LPARAM p) { (*(int*)p)++; return TRUE; }
static int EnumMonitors() { int n = 0; EnumDisplayMonitors(nullptr, nullptr, CountMon, (LPARAM)&n); return n; }

using Init_t = void (*)(IHost*, ImGuiContext*, void*, void*, void*);
using Void_t = void (*)();

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc < 2) { printf("usage: windowed_proxy_test <LSP_Windowed.dll>\n"); return 2; }

    const Outputs before = EnumDxgi();
    const int monBefore = EnumMonitors();
    printf("before loading: %d DXGI output(s) on adapter 0, %d monitor(s)\n", before.count, monBefore);
    Check("no virtual display before the addon is loaded", !before.hasVirtual);

    const bool offMode = argc > 2 && !strcmp(argv[2], "off");   // "off": start with the addon's Enable checkbox cleared
    static FakeHost host;
    if (offMode) host.cfg["enabled"] = "0";
    HMODULE mod = LoadLibraryA(argv[1]);
    if (!mod) { printf("FAIL  cannot load (%lu)\n", GetLastError()); return 1; }
    auto init = (Init_t)GetProcAddress(mod, "AddonInitialize");
    auto shut = (Void_t)GetProcAddress(mod, "AddonShutdown");
    if (!init || !shut) { printf("FAIL  exports missing\n"); return 1; }
    init(&host, nullptr, nullptr, nullptr, nullptr);
    std::this_thread::sleep_for(std::chrono::milliseconds(800));   // the hooks are installed on their own thread

    const Outputs cur = EnumDxgi();
    const int monCur = EnumMonitors();
    printf("addon %s: %d DXGI output(s), %d monitor(s)\n", offMode ? "disabled" : "enabled", cur.count, monCur);
    if (offMode) {
        Check("disabled: the virtual display is not offered", cur.count == before.count && !cur.hasVirtual);
        Check("disabled: no extra monitor in EnumDisplayMonitors", monCur == monBefore);
    } else {
        Check("enabled: one extra DXGI output, the virtual display", cur.count == before.count + 1 && cur.hasVirtual);
        Check("enabled: one extra monitor in EnumDisplayMonitors", monCur == monBefore + 1);
    }

    shut();
    // DXGI objects created while the addon was active carry vtables in the DLL, so it must not have been unloaded
    const char* base = std::strrchr(argv[1], '\\') ? std::strrchr(argv[1], '\\') + 1 : (std::strrchr(argv[1], '/') ? std::strrchr(argv[1], '/') + 1 : argv[1]);
    FreeLibrary(mod);
    Check("the DLL stays mapped after shutdown (DXGI objects may still point into it)", GetModuleHandleA(base) != nullptr);
    const Outputs after = EnumDxgi();   // must still work (the hooks are removed, the pinned code is inert)
    Check("DXGI still works after the addon shut down", after.count == before.count);

    printf("\n%s\n", g_fail ? "PROXY TEST FAILED" : "PROXY TEST PASSED");
    return g_fail ? 1 : 0;
}
