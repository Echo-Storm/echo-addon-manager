#include "fileinfo.h"
#include <windows.h>
#include <bcrypt.h>
#include <cwctype>
#include <vector>

#pragma comment(lib, "version.lib")
#pragma comment(lib, "bcrypt.lib")

namespace setup {

std::string Narrow(const std::wstring& text) {
    if (text.empty()) return std::string();
    const int n = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), n, nullptr, nullptr);
    return out;
}

std::wstring Widen(const std::string& text) {
    if (text.empty()) return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), n);
    return out;
}

std::wstring JoinPath(const std::wstring& a, const std::wstring& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    const wchar_t last = a.back();
    return (last == L'\\' || last == L'/') ? a + b : a + L"\\" + b;
}

std::wstring LowerCase(std::wstring text) {
    for (wchar_t& c : text) c = static_cast<wchar_t>(towlower(c));
    return text;
}

bool Exists(const std::wstring& path) { return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES; }

bool FileSizeOf(const std::wstring& path, uint64_t& size) {
    WIN32_FILE_ATTRIBUTE_DATA d = {};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &d) || (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) return false;
    size = (static_cast<uint64_t>(d.nFileSizeHigh) << 32) | d.nFileSizeLow;
    return true;
}

namespace {
std::string StringValue(const std::vector<char>& block, const wchar_t* langCodepage, const wchar_t* name) {
    wchar_t key[128];
    swprintf(key, 128, L"\\StringFileInfo\\%s\\%s", langCodepage, name);
    void* value = nullptr;
    UINT len = 0;
    if (!VerQueryValueW(block.data(), key, &value, &len) || !value || len == 0) return std::string();
    return Narrow(static_cast<const wchar_t*>(value));
}
}

VersionResource ReadVersionResource(const std::wstring& path) {
    VersionResource r;
    DWORD ignored = 0;
    const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &ignored);
    if (!size) return r;
    std::vector<char> block(size);
    if (!GetFileVersionInfoW(path.c_str(), 0, size, block.data())) return r;

    // the language and code page the strings are stored under (usually the first translation), then a couple of common ones as a fallback
    std::vector<std::wstring> tables;
    struct Translation { WORD language, codepage; };
    void* t = nullptr;
    UINT tlen = 0;
    if (VerQueryValueW(block.data(), L"\\VarFileInfo\\Translation", &t, &tlen) && t) {
        const auto* list = static_cast<const Translation*>(t);
        for (UINT i = 0; i < tlen / sizeof(Translation); ++i) {
            wchar_t b[16];
            swprintf(b, 16, L"%04x%04x", list[i].language, list[i].codepage);
            tables.push_back(b);
        }
    }
    tables.push_back(L"040904b0");
    tables.push_back(L"040904e4");
    tables.push_back(L"000004b0");
    for (const auto& table : tables) {
        const std::string product = StringValue(block, table.c_str(), L"ProductName");
        if (product.empty() && StringValue(block, table.c_str(), L"FileDescription").empty()) continue;
        r.ok = true;
        r.product = product;
        r.company = StringValue(block, table.c_str(), L"CompanyName");
        r.description = StringValue(block, table.c_str(), L"FileDescription");
        r.fileVersion = StringValue(block, table.c_str(), L"FileVersion");
        break;
    }
    if (r.ok && r.fileVersion.empty()) {
        VS_FIXEDFILEINFO* fixedInfo = nullptr;
        UINT len = 0;
        if (VerQueryValueW(block.data(), L"\\", reinterpret_cast<void**>(&fixedInfo), &len) && fixedInfo) {
            char v[64];
            snprintf(v, sizeof v, "%u.%u.%u.%u", HIWORD(fixedInfo->dwFileVersionMS), LOWORD(fixedInfo->dwFileVersionMS), HIWORD(fixedInfo->dwFileVersionLS), LOWORD(fixedInfo->dwFileVersionLS));
            r.fileVersion = v;
        }
    }
    return r;
}

bool FileContainsText(const std::wstring& path, const std::string& ascii) {
    if (ascii.empty()) return false;
    std::string wide;   // the same text as UTF-16LE
    for (const char c : ascii) { wide += c; wide += '\0'; }
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    const size_t keep = wide.size();                      // the tail of one piece is searched again with the next, so a match cannot fall between two
    std::string buf;
    std::vector<char> chunk(1 << 20);
    bool found = false;
    for (;;) {
        DWORD got = 0;
        if (!ReadFile(f, chunk.data(), static_cast<DWORD>(chunk.size()), &got, nullptr) || got == 0) break;
        buf.append(chunk.data(), got);
        if (buf.find(ascii) != std::string::npos || buf.find(wide) != std::string::npos) { found = true; break; }
        if (buf.size() > keep) buf.erase(0, buf.size() - keep);
    }
    CloseHandle(f);
    return found;
}

std::string Sha256File(const std::wstring& path) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (f == INVALID_HANDLE_VALUE) return std::string();
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::string hex;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0 && BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) == 0) {
        std::vector<unsigned char> buf(1 << 20);
        bool ok = true;
        for (;;) {
            DWORD got = 0;
            if (!ReadFile(f, buf.data(), static_cast<DWORD>(buf.size()), &got, nullptr)) { ok = false; break; }
            if (got == 0) break;
            if (BCryptHashData(hash, buf.data(), got, 0) != 0) { ok = false; break; }
        }
        unsigned char digest[32] = {};
        if (ok && BCryptFinishHash(hash, digest, sizeof digest, 0) == 0) {
            static const char* digits = "0123456789abcdef";
            for (const unsigned char b : digest) { hex += digits[b >> 4]; hex += digits[b & 15]; }
        }
    }
    if (hash) BCryptDestroyHash(hash);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    CloseHandle(f);
    return hex;
}

} // namespace setup
