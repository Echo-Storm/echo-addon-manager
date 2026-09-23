#include "shader_hook.h"
#include "iat_patcher.h"
#include "../addon/addon_manager.h"
#include "../log/logger.h"
#include <map>

namespace ShaderHook {

static eam::AddonManager* g_addonManager = nullptr;
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
        return cached ? static_cast<DWORD>(cached->bytecode.size()) : 0;
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

void Initialize(eam::AddonManager* addonManager) {
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

    g_hooksInstalled = true;
    LOG_INFO("ShaderHook", "Hooks installed");
}

void UninstallHooks() {
    if (!g_hooksInstalled) return;
    g_hooksInstalled = false;
    LOG_INFO("ShaderHook", "Hooks uninstalled");
}

} // namespace ShaderHook
