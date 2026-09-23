#pragma once
#include <cstdint>
#include <vector>
#include <windows.h>

namespace eam {
class AddonManager;
}

namespace ShaderHook {

struct CachedShader {
    std::vector<uint8_t> bytecode;
};

void Initialize(eam::AddonManager* addonManager);
void Shutdown();
void InstallHooks();
void UninstallHooks();

} // namespace ShaderHook
