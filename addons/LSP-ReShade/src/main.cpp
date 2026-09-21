#include "input_sim.hpp"
#include "window_manager.hpp"
#include <lsproxy/addon_sdk.h>
#include "imgui.h"
#include <lsproxy/lsp_widgets.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <windows.h>

// ============================================================================
// Global state
// ============================================================================

static IHost* g_host = nullptr;
static const char* ADDON_ID = "LSP-ReShade";

std::atomic<bool> g_inputPassthrough{false};
static std::atomic<bool> g_threadRunning{true};
static std::atomic<int>  g_hotkeyVk{VK_HOME};
static std::atomic<bool> g_hotkeyCtrl{false};
static std::atomic<bool> g_hotkeyAlt{false};
static std::atomic<bool> g_hotkeyShift{false};
static std::atomic<bool> g_autoClickRepress{true};
static std::atomic<bool> g_isSimulating{false};
static std::atomic<int>  g_autoClickThreads{0};   // auto click & repress threads still running
static std::thread       g_worker;                // owned: joined in AddonShutdown

// Keep this DLL mapped for the rest of the process. Used when a window is still subclassed with our window procedure and we
// cannot take it out (another subclass sits on top of ours): unloading now would leave that window calling freed code.
static void PinModule() {
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN, (LPCWSTR)&PinModule, &self);
}

// ============================================================================
// Host logging helper
// ============================================================================

static void HostLog(LsProxyLogLevel level, const char* fmt, ...) {
    if (!g_host) return;
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    g_host->Log(level, buf);
}

// ============================================================================
// Window detection
// ============================================================================

static HWND FindLSWindow() {
    HWND found = nullptr;
    EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
        DWORD pid;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid == GetCurrentProcessId() && IsWindowVisible(hwnd)) {
            *(HWND*)lParam = hwnd;
            return FALSE;
        }
        return TRUE;
    }, (LPARAM)&found);
    return found;
}

static HWND FindTargetWindow(HWND hLS) {
    if (!hLS) return nullptr;
    HWND h = GetWindow(hLS, GW_HWNDNEXT);
    while (h) {
        if (IsWindowVisible(h)) {
            DWORD pid;
            GetWindowThreadProcessId(h, &pid);
            if (pid != GetCurrentProcessId()) {
                char cls[256];
                GetClassNameA(h, cls, sizeof(cls));
                if (strcmp(cls, "Progman") != 0 &&
                    strcmp(cls, "Shell_TrayWnd") != 0 &&
                    strcmp(cls, "WorkerW") != 0) {
                    return h;
                }
            }
        }
        h = GetWindow(h, GW_HWNDNEXT);
    }
    return nullptr;
}

static std::atomic<bool> g_testIgnoreFocus{false};   // set only by the offline lifecycle test

static bool HasValidFocus() {
    if (g_testIgnoreFocus) return true;
    HWND hFg = GetForegroundWindow();
    if (!hFg) return false;

    DWORD pid = 0;
    GetWindowThreadProcessId(hFg, &pid);
    if (pid == GetCurrentProcessId()) return true;

    HWND hLS = FindLSWindow();
    if (hLS) {
        HWND hTarget = FindTargetWindow(hLS);
        if (hFg == hTarget) return true;
    }
    return false;
}

// ============================================================================
// Hotkey check
// ============================================================================

static bool IsHotkeyPressed() {
    if (g_hotkeyVk == 0) return false;

    bool key  = (GetAsyncKeyState(g_hotkeyVk) & 0x8000) != 0;
    bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    bool alt  = (GetAsyncKeyState(VK_MENU)    & 0x8000) != 0;
    bool shft = (GetAsyncKeyState(VK_SHIFT)   & 0x8000) != 0;

    return key &&
           ctrl == g_hotkeyCtrl.load() &&
           alt  == g_hotkeyAlt.load() &&
           shft == g_hotkeyShift.load();
}

// ============================================================================
// Auto click & repress logic
// ============================================================================

static void RunAutoClickRepress(bool newState) {
    g_isSimulating = true;
    struct Done { ~Done() { g_isSimulating = false; g_autoClickThreads--; } } done;   // even if a sleep is cut short

    int vk   = g_hotkeyVk.load();
    bool c   = g_hotkeyCtrl.load();
    bool a   = g_hotkeyAlt.load();
    bool s   = g_hotkeyShift.load();

    if (newState) { // Turning ON
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        InputSim::Click();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        InputSim::PressCombo(vk, c, a, s);
    } else { // Turning OFF
        std::this_thread::sleep_for(std::chrono::milliseconds(450));
        InputSim::Click();
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
}

// ============================================================================
// Worker thread
// ============================================================================

static void WorkerThread() {
    HostLog(LSPROXY_LOG_INFO, "Worker thread started");
    bool lastHotkeyState = false;
    int cleanupCounter = 0;

    while (g_threadRunning) {
        // Auto-disable on focus loss
        if (g_inputPassthrough && !HasValidFocus()) {
            g_inputPassthrough = false;
            HostLog(LSPROXY_LOG_INFO, "Passthrough disabled: focus lost");
            WindowMgr::RestoreAll();
        }

        // Check hotkey
        bool pressed = IsHotkeyPressed();

        if (pressed && !lastHotkeyState && !g_isSimulating) {
            if (HasValidFocus()) {
                bool newState = !g_inputPassthrough.load();
                g_inputPassthrough = newState;
                HostLog(LSPROXY_LOG_INFO, "Passthrough %s", newState ? "ON" : "OFF");

                if (!newState) {
                    WindowMgr::RestoreAll();
                }

                if (g_autoClickRepress && g_threadRunning) {
                    g_autoClickThreads++;
                    std::thread([newState]() { RunAutoClickRepress(newState); }).detach();
                }
            }
        }
        lastHotkeyState = pressed;

        // Active loop: process windows
        if (g_inputPassthrough) {
            EnumWindows([](HWND hwnd, LPARAM) -> BOOL {
                DWORD pid;
                GetWindowThreadProcessId(hwnd, &pid);
                if (pid == GetCurrentProcessId() && IsWindowVisible(hwnd)) {
                    WindowMgr::ProcessWindow(hwnd);
                    EnumChildWindows(hwnd, [](HWND child, LPARAM) -> BOOL {
                        WindowMgr::ProcessWindow(child);
                        return TRUE;
                    }, 0);
                }
                return TRUE;
            }, 0);

            if (++cleanupCounter >= 100) {
                WindowMgr::CleanupDeadWindows();
                cleanupCounter = 0;
            }

            ClipCursor(NULL);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
    HostLog(LSPROXY_LOG_INFO, "Worker thread stopped");
}

// ============================================================================
// Settings persistence via IHost
// ============================================================================

static void LoadSettings() {
    if (!g_host) return;
    g_hotkeyVk        = std::atoi(g_host->GetConfig(ADDON_ID, "hotkey_vk", "36")); // VK_HOME
    g_hotkeyCtrl      = std::string(g_host->GetConfig(ADDON_ID, "hotkey_ctrl", "0")) == "1";
    g_hotkeyAlt       = std::string(g_host->GetConfig(ADDON_ID, "hotkey_alt", "0")) == "1";
    g_hotkeyShift     = std::string(g_host->GetConfig(ADDON_ID, "hotkey_shift", "0")) == "1";
    g_autoClickRepress = std::string(g_host->GetConfig(ADDON_ID, "auto_click_repress", "1")) == "1";
}

static void SaveSettings() {
    if (!g_host) return;
    g_host->SetConfig(ADDON_ID, "hotkey_vk", std::to_string(g_hotkeyVk.load()).c_str());
    g_host->SetConfig(ADDON_ID, "hotkey_ctrl", g_hotkeyCtrl ? "1" : "0");
    g_host->SetConfig(ADDON_ID, "hotkey_alt", g_hotkeyAlt ? "1" : "0");
    g_host->SetConfig(ADDON_ID, "hotkey_shift", g_hotkeyShift ? "1" : "0");
    g_host->SetConfig(ADDON_ID, "auto_click_repress", g_autoClickRepress ? "1" : "0");
    g_host->SaveConfig();
}

// ============================================================================
// UI helper
// ============================================================================

static const char* GetKeyName(int vk) {
    static char buf[32];
    if (vk == 0) return "None";
    if (vk >= VK_F1 && vk <= VK_F12) { sprintf_s(buf, "F%d", vk - VK_F1 + 1); return buf; }
    switch (vk) {
        case VK_INSERT: return "Insert";
        case VK_DELETE: return "Delete";
        case VK_HOME:   return "Home";
        case VK_END:    return "End";
        case VK_PRIOR:  return "Page Up";
        case VK_NEXT:   return "Page Down";
    }
    if (vk >= 'A' && vk <= 'Z') { buf[0] = (char)vk; buf[1] = 0; return buf; }
    sprintf_s(buf, "Key %d", vk);
    return buf;
}

// ============================================================================
// Addon exports
// ============================================================================

LSPROXY_EXPORT void AddonInitialize(IHost* host, ImGuiContext* ctx,
                                    void* allocFunc, void* freeFunc, void* userData) {
    ImGui::SetCurrentContext(ctx);
    ImGui::SetAllocatorFunctions((ImGuiMemAllocFunc)allocFunc, (ImGuiMemFreeFunc)freeFunc, userData);
    lsp::InitAddonImGui();

    g_host = host;
    LoadSettings();

    g_threadRunning = true;
    if (g_worker.joinable()) g_worker.join();   // a previous instance still winding down
    g_worker = std::thread(WorkerThread);

    HostLog(LSPROXY_LOG_INFO, "ReShade Input Passthrough initialized");
}

LSPROXY_EXPORT void AddonShutdown() {
    HostLog(LSPROXY_LOG_INFO, "Shutting down");
    g_threadRunning = false;
    if (g_worker.joinable()) g_worker.join();
    // an auto click & repress in flight finishes its sleeps (well under two seconds) before the code goes away
    for (int i = 0; i < 200 && g_autoClickThreads.load() > 0; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    g_inputPassthrough = false;
    WindowMgr::RestoreAll();
    if (g_autoClickThreads.load() > 0 || WindowMgr::StillHooked()) { PinModule(); HostLog(LSPROXY_LOG_WARN, "a window still uses this addon's procedure: the DLL stays loaded until Lossless Scaling exits"); }
    g_host = nullptr;
}

LSPROXY_EXPORT uint32_t GetAddonCapabilities() {
    return LSPROXY_CAP_HAS_SETTINGS;   // safe to switch on and off while running: the worker is joined and the windows are restored
}

LSPROXY_EXPORT void AddonRenderSettings() {
    ImGui::TextWrapped("Lets you use the mouse and keyboard on a ReShade overlay (its menu, its UI) while Lossless Scaling is scaling the game. "
                       "Press the hotkey below to turn passthrough on, and again to turn it off.");
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 5));

    // Status indicator
    if (g_inputPassthrough) {
        ImGui::PushStyleColor(ImGuiCol_Text, lsp::theme::V(lsp::theme::kAccent));
        ImGui::Text("Passthrough: ON (press the hotkey again to turn it off)");
        ImGui::PopStyleColor();
    } else {
        ImGui::TextDisabled("Passthrough: off");
    }

    ImGui::Dummy(ImVec2(0, 5));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 5));

    // Auto click & repress
    bool autoClick = g_autoClickRepress;
    if (ImGui::Checkbox("Auto Click & Repress", &autoClick)) {
        g_autoClickRepress = autoClick;
        SaveSettings();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("When you turn passthrough on or off, this addon clicks the mouse once (wherever the pointer is) and, when turning on,\n"
                          "presses the hotkey again. ReShade needs that click to take focus, and the second press to open its menu.\n"
                          "Turn it off if you would rather do that yourself: the click lands on whatever is under the pointer.");
    }

    ImGui::Dummy(ImVec2(0, 5));
    ImGui::Separator();
    lsp::SectionLabel("Hotkey");
    ImGui::Dummy(ImVec2(0, 3));

    // Modifier checkboxes
    bool c = g_hotkeyCtrl, a = g_hotkeyAlt, s = g_hotkeyShift;
    bool changed = false;
    if (ImGui::Checkbox("Ctrl", &c))  { g_hotkeyCtrl = c;  changed = true; }
    ImGui::SameLine();
    if (ImGui::Checkbox("Alt", &a))   { g_hotkeyAlt = a;   changed = true; }
    ImGui::SameLine();
    if (ImGui::Checkbox("Shift", &s)) { g_hotkeyShift = s;  changed = true; }

    // Key selector
    int currentKey = g_hotkeyVk;
    if (ImGui::BeginCombo("Key", GetKeyName(currentKey))) {
        static const int keys[] = {
            0, VK_F1, VK_F2, VK_F3, VK_F4, VK_F5, VK_F6, VK_F7, VK_F8,
            VK_F9, VK_F10, VK_F11, VK_F12, VK_INSERT, VK_DELETE,
            VK_HOME, VK_END, VK_PRIOR, VK_NEXT,
            'A','B','C','D','E','F','G','H','I','J','K','L','M',
            'N','O','P','Q','R','S','T','U','V','W','X','Y','Z'
        };
        for (int vk : keys) {
            if (ImGui::Selectable(GetKeyName(vk), currentKey == vk)) {
                g_hotkeyVk = vk;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }

    if (changed) SaveSettings();
}

// Test hook (used by tools/lifecycle_test.cpp only): force passthrough without the hotkey or a focused target window.
extern "C" __declspec(dllexport) void LspReShade_SetPassthroughForTest(bool on) {
    g_testIgnoreFocus = on;
    g_inputPassthrough = on;
}

LSPROXY_EXPORT const char* GetAddonName()        { return "ReShade Input Passthrough"; }
LSPROXY_EXPORT const char* GetAddonVersion()     { return "0.1.0"; }
LSPROXY_EXPORT const char* GetAddonAuthor()      { return "FrankBarretta / Echo-Storm"; }
LSPROXY_EXPORT const char* GetAddonDescription() {
    return "Lets you use the mouse and keyboard on a ReShade overlay (its menu) while "
           "Lossless Scaling is scaling the game. Press a hotkey to turn it on and off.";
}

// ============================================================================
// DLL Entry Point
// ============================================================================

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
    }
    return TRUE;
}
