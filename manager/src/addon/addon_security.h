#pragma once
#include "addon_info.h"
#include <string>

namespace lsproxy {

class AddonSecurity {
public:
    // Compute SHA-256 hash of a file
    static std::string ComputeSHA256(const std::wstring& filePath);

    // Verify DLL against trusted hashes
    static SecurityVerdict VerifyDll(const std::wstring& dllPath, const std::string& addonId);

    // Load trusted hashes from trusted_addons.json
    static void LoadTrustedHashes(const std::wstring& basePath);

    // SEH-safe function call wrapper
    template<typename Ret, typename Func, typename... Args>
    static Ret SafeCall(const char* context, Ret defaultVal, Func func, Args... args);
};

// Template implementation
template<typename Ret, typename Func, typename... Args>
Ret AddonSecurity::SafeCall(const char* context, Ret defaultVal, Func func, Args... args) {
    __try {
        return func(args...);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        // Can't call LOG_ERROR here due to SEH/C++ mixing rules
        // The caller should log after catching
        return defaultVal;
    }
}

// Specialization for void return
template<typename Func, typename... Args>
void SafeCallVoid(const char* context, Func func, Args... args) {
    __try {
        func(args...);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        // Caller handles logging
    }
}

} // namespace lsproxy
