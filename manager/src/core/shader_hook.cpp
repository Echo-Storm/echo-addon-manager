#include "shader_hook.h"
#include "iat_patcher.h"
#include "../addon/addon_manager.h"
#include "../log/logger.h"
#include "../../sdk/include/lsproxy/addon_exports.h"
#include <cstring>
#include <map>
#include <sstream>

namespace ShaderHook {

static lsproxy::AddonManager* g_addonManager = nullptr;
static std::map<HRSRC, CachedShader> g_shaderCache;
static CRITICAL_SECTION g_cacheLock;
static bool g_hooksInstalled = false;

// Original API function pointers
static HRSRC(WINAPI *g_origFindResourceW)(HMODULE, LPCWSTR, LPCWSTR) = nullptr;
static HGLOBAL(WINAPI *g_origLoadResource)(HMODULE, HRSRC) = nullptr;
static DWORD(WINAPI *g_origSizeofResource)(HMODULE, HRSRC) = nullptr;
static LPVOID(WINAPI *g_origLockResource)(HGLOBAL) = nullptr;
static BOOL(WINAPI *g_origFreeResource)(HGLOBAL) = nullptr;

static const uint32_t CUSTOM_SHADER_MAGIC = 0xF00DB00Fu;

inline HRSRC MakeCustomHandle(DWORD id) {
    uintptr_t magicShift = (sizeof(uintptr_t) == 8) ? 32 : 16;
    uintptr_t v = (((uintptr_t)CUSTOM_SHADER_MAGIC) << magicShift) | (uintptr_t)(id & 0xFFFF);
    return (HRSRC)(v);
}

inline bool IsCustomHandle(HRSRC handle) {
    uintptr_t v = (uintptr_t)handle;
    uintptr_t magicShift = (sizeof(uintptr_t) == 8) ? 32 : 16;
    return (v >> magicShift) == (uintptr_t)CUSTOM_SHADER_MAGIC;
}

inline DWORD GetCustomHandleId(HRSRC handle) {
    return (DWORD)((uintptr_t)handle & 0xFFFF);
}

static const CachedShader* GetCachedShader(HRSRC handle) {
    EnterCriticalSection(&g_cacheLock);
    auto it = g_shaderCache.find(handle);
    LeaveCriticalSection(&g_cacheLock);
    if (it != g_shaderCache.end()) return &it->second;
    return nullptr;
}

HRSRC WINAPI HookedFindResourceW(HMODULE hModule, LPCWSTR lpName, LPCWSTR lpType) {
    if (g_addonManager) {
        const void* customData = nullptr;
        uint32_t customSize = 0;

        if (g_addonManager->InterceptResource(lpName, lpType, &customData, &customSize)) {
            if (customData && customSize > 0) {
                DWORD handleId = IS_INTRESOURCE(lpName) ? (DWORD)(uintptr_t)lpName : 0xFFFF;
                HRSRC customHandle = MakeCustomHandle(handleId);

                EnterCriticalSection(&g_cacheLock);
                CachedShader& cached = g_shaderCache[customHandle];
                cached.bytecode.assign((const uint8_t*)customData, (const uint8_t*)customData + customSize);
                cached.size = customSize;
                LeaveCriticalSection(&g_cacheLock);

                LOG_DEBUG("ShaderHook", "Intercepted resource, handle: 0x%llX", (unsigned long long)(uintptr_t)customHandle);
                return customHandle;
            }
        }
    }
    return g_origFindResourceW ? g_origFindResourceW(hModule, lpName, lpType) : nullptr;
}

HGLOBAL WINAPI HookedLoadResource(HMODULE hModule, HRSRC hResInfo) {
    if (IsCustomHandle(hResInfo)) return (HGLOBAL)hResInfo;
    return g_origLoadResource ? g_origLoadResource(hModule, hResInfo) : nullptr;
}

DWORD WINAPI HookedSizeofResource(HMODULE hModule, HRSRC hResInfo) {
    if (IsCustomHandle(hResInfo)) {
        const CachedShader* cached = GetCachedShader(hResInfo);
        return cached ? cached->size : 0;
    }
    return g_origSizeofResource ? g_origSizeofResource(hModule, hResInfo) : 0;
}

LPVOID WINAPI HookedLockResource(HGLOBAL hResData) {
    HRSRC asHandle = (HRSRC)hResData;
    if (IsCustomHandle(asHandle)) {
        const CachedShader* cached = GetCachedShader(asHandle);
        if (cached && !cached->bytecode.empty()) return (void*)cached->bytecode.data();
    }
    return g_origLockResource ? g_origLockResource(hResData) : nullptr;
}

BOOL WINAPI HookedFreeResource(HGLOBAL hResData) {
    if (IsCustomHandle((HRSRC)hResData)) return TRUE;
    return g_origFreeResource ? g_origFreeResource(hResData) : TRUE;
}

static void PatchMemory(HMODULE hModule, DWORD rva, const std::vector<uint8_t>& bytes) {
    if (!hModule) return;
    uint8_t* address = (uint8_t*)hModule + rva;
    DWORD oldProtect;
    if (VirtualProtect(address, bytes.size(), PAGE_EXECUTE_READWRITE, &oldProtect)) {
        std::memcpy(address, bytes.data(), bytes.size());
        VirtualProtect(address, bytes.size(), oldProtect, &oldProtect);
        LOG_DEBUG("ShaderHook", "Patched memory at RVA 0x%X", rva);
    }
}

void Initialize(lsproxy::AddonManager* addonManager) {
    g_addonManager = addonManager;
    InitializeCriticalSection(&g_cacheLock);
    LOG_INFO("ShaderHook", "Initialized");
}

void Shutdown() {
    EnterCriticalSection(&g_cacheLock);
    g_shaderCache.clear();
    LeaveCriticalSection(&g_cacheLock);
    DeleteCriticalSection(&g_cacheLock);
    g_addonManager = nullptr;
}

void InstallHooks() {
    if (g_hooksInstalled) return;

    HMODULE hKernel32 = GetModuleHandleW(L"kernel32.dll");
    if (!hKernel32) return;

    g_origFindResourceW = (HRSRC(WINAPI*)(HMODULE, LPCWSTR, LPCWSTR))GetProcAddress(hKernel32, "FindResourceW");
    g_origLoadResource = (HGLOBAL(WINAPI*)(HMODULE, HRSRC))GetProcAddress(hKernel32, "LoadResource");
    g_origSizeofResource = (DWORD(WINAPI*)(HMODULE, HRSRC))GetProcAddress(hKernel32, "SizeofResource");
    g_origLockResource = (LPVOID(WINAPI*)(HGLOBAL))GetProcAddress(hKernel32, "LockResource");
    g_origFreeResource = (BOOL(WINAPI*)(HGLOBAL))GetProcAddress(hKernel32, "FreeResource");

    if (!g_origFindResourceW || !g_origLoadResource || !g_origSizeofResource ||
        !g_origLockResource || !g_origFreeResource) {
        LOG_ERROR("ShaderHook", "Failed to get original function pointers");
        return;
    }

    HMODULE hLosslessOriginal = GetModuleHandleW(L"Lossless_original.dll");
    if (!hLosslessOriginal) {
        LOG_ERROR("ShaderHook", "Failed to get Lossless_original.dll handle");
        return;
    }

    IatPatcher::PatchIat(hLosslessOriginal, "kernel32.dll", "FindResourceW", &HookedFindResourceW);
    IatPatcher::PatchIat(hLosslessOriginal, "kernel32.dll", "LoadResource", &HookedLoadResource);
    IatPatcher::PatchIat(hLosslessOriginal, "kernel32.dll", "SizeofResource", &HookedSizeofResource);
    IatPatcher::PatchIat(hLosslessOriginal, "kernel32.dll", "LockResource", &HookedLockResource);
    IatPatcher::PatchIat(hLosslessOriginal, "kernel32.dll", "FreeResource", &HookedFreeResource);

    // Apply LS1 patches if any addon requests it
    bool shouldApplyPatches = false;
    if (g_addonManager) {
        std::lock_guard<std::mutex> lock(g_addonManager->GetMutex());
        auto& addons = g_addonManager->GetAddons();
        for (const auto& addon : addons) {
            if (addon.enabled && (addon.capabilities & LSPROXY_CAP_PATCH_LS1_LOGIC)) {
                shouldApplyPatches = true;
                break;
            }
        }
    }

    if (shouldApplyPatches) {
        std::vector<uint8_t> nops = {0x90, 0x90};
        std::vector<DWORD> patchOffsets = {
            0x51ac, 0x59c6, 0x5ab7, 0x5bc9, 0x5ce2,
            0x65ec, 0x6f04, 0x6fe7, 0x78a7, 0x7f86,
            0x8056, 0x8128, 0x8201, 0x8bdc, 0x92f0,
            0x941d, 0x9d6c, 0xa480, 0xa589
        };
        for (DWORD rva : patchOffsets) {
            PatchMemory(hLosslessOriginal, rva, nops);
        }
        LOG_INFO("ShaderHook", "Applied %zu memory patches", patchOffsets.size());
    }

    FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
    g_hooksInstalled = true;
    LOG_INFO("ShaderHook", "Hooks installed");
}

void UninstallHooks() {
    if (!g_hooksInstalled) return;
    g_hooksInstalled = false;
    LOG_INFO("ShaderHook", "Hooks uninstalled");
}

} // namespace ShaderHook
