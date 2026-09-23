#include "instance_guard.h"
#include <windows.h>
#include <cwctype>

namespace lsproxy {
namespace instance {

namespace {
HANDLE g_held = nullptr;
}

std::wstring MutexName(const std::wstring& folder) {
    std::wstring key = folder;
    for (wchar_t& c : key) c = (c == L'/') ? L'\\' : static_cast<wchar_t>(std::towlower(c));
    while (key.size() > 3 && key.back() == L'\\') key.pop_back();
    uint64_t h = 1469598103934665603ull;   // FNV-1a: a short, stable name (a mutex name cannot contain '\')
    for (wchar_t c : key) { h ^= static_cast<uint16_t>(c); h *= 1099511628211ull; }
    wchar_t name[64];
    swprintf(name, 64, L"Local\\EchoAddonManager-%016llx", static_cast<unsigned long long>(h));
    return name;
}

bool Claim(const std::wstring& folder) {
    if (g_held) return true;   // already ours
    HANDLE m = CreateMutexW(nullptr, FALSE, MutexName(folder).c_str());
    if (!m) return true;       // cannot tell: behave as before rather than switch everything off
    if (GetLastError() == ERROR_ALREADY_EXISTS) { CloseHandle(m); return false; }
    g_held = m;
    return true;
}

void Release() {
    if (g_held) { CloseHandle(g_held); g_held = nullptr; }
}

} // namespace instance
} // namespace lsproxy
