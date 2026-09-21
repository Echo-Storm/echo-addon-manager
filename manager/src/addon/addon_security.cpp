#include "addon_security.h"
#include "../log/logger.h"
#include "../../third_party/nlohmann/json.hpp"
#include <bcrypt.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <unordered_map>

#pragma comment(lib, "bcrypt.lib")

namespace fs = std::filesystem;

namespace lsproxy {

static std::unordered_map<std::string, std::vector<std::string>> g_trustedHashes;

std::string AddonSecurity::ComputeSHA256(const std::wstring& filePath) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) return "";

    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_HASH_HANDLE hHash = nullptr;

    if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) {
        return "";
    }

    DWORD hashObjSize = 0, dataSize = 0;
    BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&hashObjSize, sizeof(DWORD), &dataSize, 0);

    std::vector<uint8_t> hashObj(hashObjSize);
    if (BCryptCreateHash(hAlg, &hHash, hashObj.data(), hashObjSize, nullptr, 0, 0) != 0) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return "";
    }

    char buffer[8192];
    while (file.read(buffer, sizeof(buffer)) || file.gcount() > 0) {
        BCryptHashData(hHash, (PUCHAR)buffer, (ULONG)file.gcount(), 0);
    }

    DWORD hashSize = 32; // SHA-256 = 32 bytes
    std::vector<uint8_t> hash(hashSize);
    BCryptFinishHash(hHash, hash.data(), hashSize, 0);
    BCryptDestroyHash(hHash);
    BCryptCloseAlgorithmProvider(hAlg, 0);

    std::ostringstream oss;
    for (uint8_t b : hash) {
        oss << std::hex << std::setfill('0') << std::setw(2) << (int)b;
    }
    return oss.str();
}

SecurityVerdict AddonSecurity::VerifyDll(const std::wstring& dllPath, const std::string& addonId) {
    if (g_trustedHashes.empty()) {
        return SecurityVerdict::Unknown;
    }

    auto it = g_trustedHashes.find(addonId);
    if (it == g_trustedHashes.end()) {
        return SecurityVerdict::Unknown;
    }

    std::string hash = ComputeSHA256(dllPath);
    if (hash.empty()) {
        return SecurityVerdict::Unknown;
    }

    for (const auto& trusted : it->second) {
        if (trusted == hash) {
            return SecurityVerdict::Trusted;
        }
    }

    return SecurityVerdict::Tampered;
}

void AddonSecurity::LoadTrustedHashes(const std::wstring& basePath) {
    fs::path hashFile = fs::path(basePath) / "trusted_addons.json";
    if (!fs::exists(hashFile)) {
        LOG_DEBUG("Security", "No trusted_addons.json found, skipping hash verification");
        return;
    }

    try {
        std::ifstream file(hashFile);
        auto data = nlohmann::json::parse(file);

        for (auto& [addonId, hashes] : data.items()) {
            if (hashes.is_array()) {
                for (const auto& h : hashes) {
                    g_trustedHashes[addonId].push_back(h.get<std::string>());
                }
            }
        }

        LOG_INFO("Security", "Loaded trusted hashes for %zu addons", g_trustedHashes.size());
    } catch (const std::exception& e) {
        LOG_ERROR("Security", "Failed to parse trusted_addons.json: %s", e.what());
    }
}

} // namespace lsproxy
