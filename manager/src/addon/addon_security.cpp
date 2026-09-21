#include "addon_security.h"
#include "../log/logger.h"
#include "../../third_party/nlohmann/json.hpp"
#include <algorithm>
#include <bcrypt.h>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <unordered_map>
#include <vector>

#pragma comment(lib, "bcrypt.lib")

namespace fs = std::filesystem;

namespace lsproxy {

namespace {

std::mutex g_listMutex;
std::unordered_map<std::string, std::vector<std::string>> g_trusted;   // addon id -> its allowed hashes, lowercase hex

std::string Lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return text;
}

// Closes a CNG algorithm provider and hash object however the function that opened them ends.
struct Sha256 {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::vector<UCHAR> state;

    bool Open() {
        if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) return false;
        DWORD stateSize = 0, got = 0;
        if (BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&stateSize, sizeof stateSize, &got, 0) != 0) return false;
        state.resize(stateSize);
        return BCryptCreateHash(alg, &hash, state.data(), stateSize, nullptr, 0, 0) == 0;
    }
    ~Sha256() {
        if (hash) BCryptDestroyHash(hash);
        if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    }
};

} // namespace

std::string AddonSecurity::ComputeSHA256(const std::wstring& filePath) {
    std::ifstream in(filePath, std::ios::binary);
    if (!in) return {};

    Sha256 sha;
    if (!sha.Open()) return {};

    std::vector<char> chunk(64 * 1024);
    while (in) {
        in.read(chunk.data(), (std::streamsize)chunk.size());
        const std::streamsize got = in.gcount();
        if (got > 0 && BCryptHashData(sha.hash, (PUCHAR)chunk.data(), (ULONG)got, 0) != 0) return {};
    }

    UCHAR digest[32];
    if (BCryptFinishHash(sha.hash, digest, sizeof digest, 0) != 0) return {};

    static const char kDigits[] = "0123456789abcdef";
    std::string hex;
    hex.reserve(64);
    for (UCHAR byte : digest) {
        hex.push_back(kDigits[byte >> 4]);
        hex.push_back(kDigits[byte & 0x0F]);
    }
    return hex;
}

SecurityVerdict AddonSecurity::VerifyDll(const std::wstring& dllPath, const std::string& addonId) {
    std::vector<std::string> allowed;
    {
        std::lock_guard<std::mutex> lock(g_listMutex);
        const auto entry = g_trusted.find(addonId);
        if (entry == g_trusted.end()) return SecurityVerdict::Unknown;
        allowed = entry->second;
    }

    const std::string actual = ComputeSHA256(dllPath);
    if (actual.empty()) return SecurityVerdict::Unknown;
    return std::find(allowed.begin(), allowed.end(), actual) != allowed.end() ? SecurityVerdict::Trusted : SecurityVerdict::Tampered;
}

void AddonSecurity::LoadTrustedHashes(const std::wstring& basePath) {
    const fs::path file = fs::path(basePath) / "trusted_addons.json";
    std::unordered_map<std::string, std::vector<std::string>> loaded;

    std::error_code ec;
    if (fs::exists(file, ec)) {
        std::ifstream in(file);
        const nlohmann::json doc = nlohmann::json::parse(in, nullptr, /*allow_exceptions=*/false);
        if (!doc.is_object()) {
            LOG_ERROR("Security", "Failed to parse trusted_addons.json: it is not a JSON object; keeping the list loaded before");
            return;
        }
        for (const auto& [addonId, hashes] : doc.items()) {
            if (!hashes.is_array()) continue;
            for (const auto& h : hashes)
                if (h.is_string()) loaded[addonId].push_back(Lower(h.get<std::string>()));
        }
        LOG_INFO("Security", "Loaded trusted hashes for %zu addons", loaded.size());
    } else {
        LOG_DEBUG("Security", "No trusted_addons.json found, skipping hash verification");
    }

    std::lock_guard<std::mutex> lock(g_listMutex);
    g_trusted = std::move(loaded);
}

} // namespace lsproxy
