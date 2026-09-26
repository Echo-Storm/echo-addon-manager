#include "runtime_files.h"
#include "addon_security.h"
#include <windows.h>
#include <softpub.h>
#include <wincrypt.h>
#include <wintrust.h>
#include <filesystem>
#include <map>
#include <mutex>
#include <thread>
#pragma comment(lib, "wintrust.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "version.lib")

namespace eam {

namespace {

std::string Utf8(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n : 0, '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}
std::wstring Wide(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n > 0 ? n : 0, L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
    return w;
}

// The version resource: the fixed numbers (as "a.b.c", or "a.b.c.d" when the last is not 0) and two of the strings.
void ReadVersion(const std::wstring& path, RuntimeFile& f) {
    DWORD handle = 0;
    const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &handle);
    if (!size) return;
    std::vector<unsigned char> data(size);
    if (!GetFileVersionInfoW(path.c_str(), 0, size, data.data())) return;
    VS_FIXEDFILEINFO* fixed = nullptr; UINT len = 0;
    if (VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&fixed), &len) && fixed && len >= sizeof(VS_FIXEDFILEINFO)) {
        const unsigned a = HIWORD(fixed->dwFileVersionMS), b = LOWORD(fixed->dwFileVersionMS), c = HIWORD(fixed->dwFileVersionLS), d = LOWORD(fixed->dwFileVersionLS);
        char text[48];
        if (d) snprintf(text, sizeof text, "%u.%u.%u.%u", a, b, c, d); else snprintf(text, sizeof text, "%u.%u.%u", a, b, c);
        f.version = text;
    }
    struct Lang { WORD language, codePage; }* langs = nullptr;
    if (!VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void**>(&langs), &len) || !langs || len < sizeof(Lang)) return;
    auto text = [&](const wchar_t* key) {
        wchar_t query[128]; swprintf(query, 128, L"\\StringFileInfo\\%04x%04x\\%s", langs[0].language, langs[0].codePage, key);
        wchar_t* value = nullptr; UINT n = 0;
        return VerQueryValueW(data.data(), query, reinterpret_cast<void**>(&value), &n) && value && n ? Utf8(value) : std::string();
    };
    f.description = text(L"FileDescription");
    f.company = text(L"CompanyName");
}

// Authenticode, as Windows judges it, from what is on this computer only (no revocation check, no downloads): signed and intact, signed
// but changed since, or not signed. The signer's name when signed.
void ReadSignature(const std::wstring& path, RuntimeFile& f) {
    WINTRUST_FILE_INFO file{}; file.cbStruct = sizeof file; file.pcwszFilePath = path.c_str();
    WINTRUST_DATA data{}; data.cbStruct = sizeof data; data.dwUIChoice = WTD_UI_NONE; data.fdwRevocationChecks = WTD_REVOKE_NONE;
    data.dwUnionChoice = WTD_CHOICE_FILE; data.pFile = &file; data.dwStateAction = WTD_STATEACTION_VERIFY;
    data.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL | WTD_REVOCATION_CHECK_NONE;
    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    const LONG result = WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &action, &data);
    if (result == ERROR_SUCCESS) f.signature = RuntimeFile::Signature::Signed;
    else if (result == TRUST_E_NOSIGNATURE || result == TRUST_E_SUBJECT_FORM_UNKNOWN || result == TRUST_E_PROVIDER_UNKNOWN) f.signature = RuntimeFile::Signature::Unsigned;
    else if (result == TRUST_E_BAD_DIGEST || result == CRYPT_E_HASH_VALUE) f.signature = RuntimeFile::Signature::Broken;
    else f.signature = RuntimeFile::Signature::Unsigned;   // expired, untrusted root, ...: not a signature to rely on
    if (f.signature != RuntimeFile::Signature::Unsigned && data.hWVTStateData) {
        if (CRYPT_PROVIDER_DATA* provider = WTHelperProvDataFromStateData(data.hWVTStateData))
            if (CRYPT_PROVIDER_SGNR* signer = WTHelperGetProvSignerFromChain(provider, 0, FALSE, 0))
                if (signer->csCertChain && signer->pasCertChain && signer->pasCertChain[0].pCert) {
                    wchar_t name[256] = {};
                    CertGetNameStringW(signer->pasCertChain[0].pCert, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, name, 256);
                    f.signer = Utf8(name);
                }
    }
    data.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &action, &data);
}

// What is known of each file, by path: read again when its size or time changes. The reading runs on a thread of its own (a 34 MB file's
// SHA-256 and signature take a moment); the list holds what was read last. Never freed: nothing may be joined as the process ends.
struct Cached { uint64_t size = 0; FILETIME written{}; bool exists = false; RuntimeFile file; bool reading = false; ULONGLONG checkedAt = 0; };
struct Cache { std::mutex mutex; std::map<std::wstring, Cached> byPath; };
Cache& TheCache() { static Cache* c = new Cache; return *c; }

} // namespace

std::string RuntimeFile::ShownVersion() const {
    if (shipped && !shippedLabel.empty()) return shippedLabel;
    std::string v = version;
    if (std::count(v.begin(), v.end(), '.') == 3) v.resize(v.rfind('.'));   // 4.1.1.2740 -> 4.1.1: the build number is in the tooltip
    return v;
}

void InspectRuntimeFile(const std::wstring& path, RuntimeFile& f) {
    WIN32_FILE_ATTRIBUTE_DATA a{};
    f.exists = GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &a) && !(a.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
    f.read = true;
    if (!f.exists) return;
    f.size = (uint64_t(a.nFileSizeHigh) << 32) | a.nFileSizeLow;
    ReadVersion(path, f);
    ReadSignature(path, f);
    f.sha256 = AddonSecurity::ComputeSHA256(path);
}

std::vector<RuntimeFile> RuntimeFiles(const std::vector<AddonInfo>& addons, const std::wstring& lsDir, ConfigLookup config) {
    std::vector<RuntimeFile> rows;
    Cache& cache = TheCache();
    const ULONGLONG now = GetTickCount64();
    for (const AddonInfo& addon : addons) {
        for (const AddonManifest::Runtime& slot : addon.manifest.runtimes) {
            RuntimeFile row;
            row.label = slot.name; row.addonId = addon.id; row.addonName = addon.GetDisplayName(); row.addonOn = addon.enabled;
            row.shippedLabel = slot.shippedLabel; row.shippedKnown = !slot.shippedSha256.empty();
            // where: the addon's setting when it names one and has a value, else the manifest's path ("{ls}/..." or relative to the addon)
            std::string file = slot.file;
            if (!slot.configKey.empty() && config) { const std::string chosen = config(addon.id, slot.configKey); if (!chosen.empty()) file = chosen; }
            std::wstring path;
            if (file.rfind("{ls}", 0) == 0) path = lsDir + Wide(file.substr(4));
            else if (std::filesystem::path(Wide(file)).is_absolute()) path = Wide(file);
            else path = (std::filesystem::path(addon.dllPath).parent_path() / Wide(file)).wstring();
            for (wchar_t& ch : path) if (ch == L'/') ch = L'\\';
            row.path = path;

            std::lock_guard<std::mutex> lock(cache.mutex);
            Cached& c = cache.byPath[path];
            if (now - c.checkedAt >= 2000) {   // a new file in its place (a swap by hand, an update) is noticed within two seconds
                c.checkedAt = now;
                WIN32_FILE_ATTRIBUTE_DATA a{};
                const bool exists = GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &a) && !(a.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
                const uint64_t size = exists ? (uint64_t(a.nFileSizeHigh) << 32) | a.nFileSizeLow : 0;
                const bool changed = exists != c.exists || size != c.size || CompareFileTime(&a.ftLastWriteTime, &c.written) != 0;
                if (changed && !c.reading) {
                    c.exists = exists; c.size = size; c.written = a.ftLastWriteTime; c.reading = true;
                    std::thread([path] {
                        RuntimeFile f; InspectRuntimeFile(path, f);
                        Cache& cc = TheCache();
                        std::lock_guard<std::mutex> l(cc.mutex);
                        Cached& done = cc.byPath[path]; done.file = f; done.reading = false;
                        done.checkedAt = 0;   // look again at once: it may have changed while it was read
                    }).detach();
                }
            }
            const RuntimeFile& known = c.file;
            row.exists = c.exists; row.read = known.read;
            row.signature = known.signature; row.version = known.version; row.description = known.description; row.company = known.company;
            row.signer = known.signer; row.sha256 = known.sha256; row.size = known.size;
            row.shipped = row.shippedKnown && !known.sha256.empty() && _stricmp(known.sha256.c_str(), slot.shippedSha256.c_str()) == 0;
            rows.push_back(row);
        }
    }
    return rows;
}

} // namespace eam
