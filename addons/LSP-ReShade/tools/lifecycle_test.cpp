// Offline lifecycle test for LSP_ReShade.dll. Nothing here is visible or takes focus: the only window is a plain popup parked
// far off-screen and shown without activation, in this test's own process.
//
//   1. load + initialise, force passthrough on: the addon must subclass the window (its WNDPROC changes)
//   2. shut down: the window's original WNDPROC must be back, the worker thread must be gone, FreeLibrary must succeed
//   3. again, but with another subclass layered on top of ours before shutdown: the DLL must pin itself (stay mapped) and the
//      window must keep working when messages are sent to it afterwards
//
//   reshade_lifecycle_test.exe <path to LSP_ReShade.dll>
#include <windows.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <lsproxy/addon_sdk.h>

struct FakeHost : IHost {
    void Log(LsProxyLogLevel, const char* m) override { printf("  [addon] %s\n", m); }
    const char* GetConfig(const char*, const char*, const char* d) override { return d; }
    void SetConfig(const char*, const char*, const char*) override {}
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

static WNDPROC g_below = nullptr;
static std::atomic<int> g_topHits{ 0 };
static LRESULT CALLBACK TopProc(HWND h, UINT m, WPARAM w, LPARAM l) { ++g_topHits; return CallWindowProc(g_below, h, m, w, l); }

using Init_t = void (*)(IHost*, ImGuiContext*, void*, void*, void*);
using Void_t = void (*)();
using Test_t = void (*)(bool);

// The window lives on its own thread with a message loop (as in a real process): the addon's worker thread changes window styles,
// which sends messages to the owning thread, so that thread must be pumping.
struct WindowThread {
    std::thread t;
    std::atomic<HWND> hwnd{ nullptr };
    std::atomic<DWORD> tid{ 0 };
    void Start() {
        t = std::thread([this] {
            tid = GetCurrentThreadId();
            HWND h = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"STATIC", L"lifecycle", WS_POPUP, -32000, -32000, 200, 100, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
            ShowWindow(h, SW_SHOWNOACTIVATE);
            hwnd = h;
            MSG m;
            while (GetMessageW(&m, nullptr, 0, 0) > 0) { TranslateMessage(&m); DispatchMessageW(&m); }
            DestroyWindow(h);
        });
        while (!hwnd.load()) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    void Stop() {
        PostThreadMessageW(tid.load(), WM_QUIT, 0, 0);
        if (t.joinable()) t.join();
    }
};

static void RunOnce(const char* dll, bool layerOnTop) {
    printf("== %s\n", layerOnTop ? "another subclass on top of the addon's" : "plain window");
    WindowThread wt;
    wt.Start();
    HWND hwnd = wt.hwnd;
    Check("test window is visible (off-screen, not activated)", IsWindowVisible(hwnd) != 0);
    WNDPROC original = (WNDPROC)GetWindowLongPtrW(hwnd, GWLP_WNDPROC);

    HMODULE mod = LoadLibraryA(dll);
    if (!mod) { printf("FAIL  cannot load %s (%lu)\n", dll, GetLastError()); g_fail++; wt.Stop(); return; }
    auto init = (Init_t)GetProcAddress(mod, "AddonInitialize");
    auto shut = (Void_t)GetProcAddress(mod, "AddonShutdown");
    auto force = (Test_t)GetProcAddress(mod, "LspReShade_SetPassthroughForTest");
    if (!init || !shut || !force) { printf("FAIL  exports missing\n"); g_fail++; wt.Stop(); return; }
    static FakeHost host;
    init(&host, nullptr, nullptr, nullptr, nullptr);   // the addon only touches ImGui inside its panel, which is not called here
    force(true);
    for (int i = 0; i < 100; ++i) {
        if ((WNDPROC)GetWindowLongPtrW(hwnd, GWLP_WNDPROC) != original) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    WNDPROC hooked = (WNDPROC)GetWindowLongPtrW(hwnd, GWLP_WNDPROC);
    Check("passthrough on: the addon subclassed the window", hooked != original);

    if (layerOnTop) { g_below = hooked; g_topHits = 0; SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)TopProc); }

    const auto t0 = std::chrono::steady_clock::now();
    shut();
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    printf("  shutdown took %.0f ms\n", ms);
    Check("shutdown returned promptly (worker joined)", ms < 3000);

    if (!layerOnTop) Check("shutdown restored the window's original procedure", (WNDPROC)GetWindowLongPtrW(hwnd, GWLP_WNDPROC) == original);
    FreeLibrary(mod);
    const char* base = std::strrchr(dll, '\\') ? std::strrchr(dll, '\\') + 1 : (std::strrchr(dll, '/') ? std::strrchr(dll, '/') + 1 : dll);
    const bool stillMapped = GetModuleHandleA(base) != nullptr;
    if (layerOnTop) {
        Check("the DLL stayed loaded because our procedure is still in the chain", stillMapped);
        const LRESULT r = SendMessageW(hwnd, WM_NULL, 0, 0);   // goes through TopProc -> the addon's HookProc -> the original
        Check("messages still work through the chain", g_topHits.load() > 0 && r == 0);
    } else {
        Check("the DLL unloaded cleanly", !stillMapped);
        SendMessageW(hwnd, WM_NULL, 0, 0);
        Check("the window still works after the DLL is gone", true);
    }
    wt.Stop();
}

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc < 2) { printf("usage: reshade_lifecycle_test <LSP_ReShade.dll>\n"); return 2; }
    RunOnce(argv[1], false);
    RunOnce(argv[1], true);
    printf("\n%s\n", g_fail ? "LIFECYCLE TEST FAILED" : "LIFECYCLE TEST PASSED");
    return g_fail ? 1 : 0;
}
