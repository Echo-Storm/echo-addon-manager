#pragma once
#include <cstdint>
#include <vector>
#include <windows.h>

namespace lsproxy {
class AddonManager;
}

namespace ShaderHook {

struct CachedShader {
    std::vector<uint8_t> bytecode;
    uint32_t size = 0;
};

void Initialize(lsproxy::AddonManager* addonManager);
void Shutdown();
void InstallHooks();
void UninstallHooks();

} // namespace ShaderHook
