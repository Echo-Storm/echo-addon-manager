#include "requirements.h"
#include "forwarder/nr_api.h"
#include <windows.h>
#include <dxgi.h>
#include <cstdio>
#include <cstdlib>

#pragma comment(lib, "version.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "advapi32.lib")

namespace req {

namespace {

bool Has(const std::string& s, const char* part) { return s.find(part) != std::string::npos; }
bool StartsWith(const std::string& s, const char* prefix) { return s.rfind(prefix, 0) == 0; }
std::string After(const std::string& s, const char* marker) {
    const size_t at = s.find(marker);
    return at == std::string::npos ? std::string() : s.substr(at + strlen(marker));
}

Row MakeRow(const char* label, Level level, std::string value, std::string hint = "") {
    Row r;
    r.label = label;
    r.level = level;
    r.value = std::move(value);
    r.hint = std::move(hint);
    return r;
}

std::string Utf8(const std::wstring& w) {
    if (w.empty()) return std::string();
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

// "a.b.c.d" of a file's fixed version resource; empty when it has none.
std::string FileVersion(const std::wstring& path) {
    DWORD ignored = 0;
    const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &ignored);
    if (!size) return std::string();
    std::string block(size, '\0');
    if (!GetFileVersionInfoW(path.c_str(), 0, size, block.data())) return std::string();
    VS_FIXEDFILEINFO* fixedInfo = nullptr;
    UINT len = 0;
    if (!VerQueryValueW(block.data(), L"\\", reinterpret_cast<void**>(&fixedInfo), &len) || !fixedInfo || len < sizeof(VS_FIXEDFILEINFO)) return std::string();
    char v[64];
    snprintf(v, sizeof v, "%u.%u.%u.%u", HIWORD(fixedInfo->dwFileVersionMS), LOWORD(fixedInfo->dwFileVersionMS), HIWORD(fixedInfo->dwFileVersionLS), LOWORD(fixedInfo->dwFileVersionLS));
    return v;
}

bool FileSize(const std::wstring& path, uint64_t& size) {
    WIN32_FILE_ATTRIBUTE_DATA d = {};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &d) || (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) return false;
    size = (static_cast<uint64_t>(d.nFileSizeHigh) << 32) | d.nFileSizeLow;
    return true;
}

std::wstring RegistryString(HKEY root, const wchar_t* subkey, const wchar_t* value) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, subkey, 0, KEY_READ | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS) return std::wstring();
    wchar_t buf[1024] = {};
    DWORD bytes = sizeof buf - sizeof(wchar_t), type = 0;
    const bool ok = RegQueryValueExW(key, value, nullptr, &type, reinterpret_cast<BYTE*>(buf), &bytes) == ERROR_SUCCESS && type == REG_SZ;
    RegCloseKey(key);
    return ok ? std::wstring(buf) : std::wstring();
}

} // namespace

std::string DriverFromNgxVersion(const std::string& fileVersion) {
    // NVIDIA's file versions end with the driver number: 32.0.16.1692 is driver 616.92, 31.0.15.5222 is 552.22.
    unsigned a, b, c, d;
    char extra;
    if (sscanf(fileVersion.c_str(), "%u.%u.%u.%u%c", &a, &b, &c, &d, &extra) != 4) return std::string();
    char digits[32];
    snprintf(digits, sizeof digits, "%u%04u", c % 10, d);   // the last digit of the third part, then the fourth part padded to four
    const std::string s = digits;
    return s.substr(0, s.size() - 2) + "." + s.substr(s.size() - 2);
}

std::string VersionShort(const std::string& fileVersion) {
    std::string out;
    int parts = 0;
    for (const char c : fileVersion) {
        if (c == '.' || c == ',') { if (++parts == 2) break; out += '.'; }
        else if (c != ' ') out += c;
    }
    return out;
}

std::string SizeText(uint64_t bytes) {
    char b[48];
    if (bytes == 0) return "0 MB";
    snprintf(b, sizeof b, "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    return b;
}

std::string PlainEngineError(const std::string& raw) {
    if (StartsWith(raw, "snippet probe")) return "the model file could not be loaded (it may be the wrong file or damaged)";
    if (StartsWith(raw, "forwarder LoadLibrary")) return "the helper DLL could not be loaded";
    if (StartsWith(raw, "forwarder export")) return "the helper DLL is from a different version of this addon";
    if (StartsWith(raw, "NGX core Init")) return "the NVIDIA NGX core did not start: " + After(raw, ": ");
    if (StartsWith(raw, "snippet Init_Ext")) return "the model file refused to start: " + After(raw, ": ");
    if (StartsWith(raw, "D3D12CreateDevice")) return "could not create a Direct3D 12 device on this graphics card";
    if (StartsWith(raw, "CreateFeature")) return "the model could not create its Neural Rendering feature: " + After(raw, ": ");
    return raw;
}

Report Evaluate(const Inputs& in) {
    Report rep;

    // Graphics card
    if (in.nvidiaFound) {
        std::string v = in.gpuName;
        if (in.gpuMemoryBytes) v += " (" + std::to_string(static_cast<unsigned long long>((in.gpuMemoryBytes + (1ull << 29)) >> 30)) + " GB)";
        rep.rows.push_back(MakeRow("Graphics card", Level::Ok, v));
    } else {
        rep.rows.push_back(MakeRow("Graphics card", Level::Missing, "no NVIDIA graphics card found",
                                   "DLSS 5 Neural Rendering runs only on an NVIDIA RTX graphics card."));
    }

    // NVIDIA driver (its NGX core)
    if (in.ngxRegistered && in.ngxCoreFound) {
        const std::string drv = DriverFromNgxVersion(in.ngxCoreVersion);
        std::string v = drv.empty() ? "NGX core " + (in.ngxCoreVersion.empty() ? std::string("found") : in.ngxCoreVersion)
                                    : "driver " + drv + " (_nvngx.dll " + in.ngxCoreVersion + ")";
        rep.rows.push_back(MakeRow("NVIDIA driver", Level::Ok, v));
    } else {
        rep.rows.push_back(MakeRow("NVIDIA driver", Level::Missing,
                                   in.ngxRegistered ? "the driver names its NGX core, but _nvngx.dll is missing" : "the NVIDIA driver's NGX core is not registered",
                                   "Install or repair the NVIDIA graphics driver (a clean install). The NGX core, _nvngx.dll, is part of it."));
    }

    // Model file
    if (!in.modelFound) {
        rep.rows.push_back(MakeRow("Model file", Level::Missing, "not found: " + in.modelPath,
                                   "Put your copy of nvngx_dlssnr.dll in the Lossless Scaling folder, next to LosslessScaling.exe, or set its path under Advanced. "
                                   "It is not included with this addon, and this project does not say where to get it."));
    } else if (in.modelSize < kSmallestPlausibleModel) {
        rep.rows.push_back(MakeRow("Model file", Level::Missing, "found, but only " + SizeText(in.modelSize) + ": too small to be the model",
                                   "The file looks damaged or is not the model (the model is about 150 MB). Replace it with a complete copy."));
    } else {
        const std::string ver = VersionShort(in.modelVersion);
        const bool tested = ver == kTestedModelVersion && in.modelSize == kTestedModelSize;
        const std::string what = "version " + (ver.empty() ? std::string("unknown") : ver) + ", " + SizeText(in.modelSize);
        if (tested) rep.rows.push_back(MakeRow("Model file", Level::Ok, what + ": the build this addon was tested with"));
        else rep.rows.push_back(MakeRow("Model file", Level::Note, what + ": not the build this addon was tested with (" + std::string(kTestedModelVersion) + ")",
                                        "It may still work. If the engine fails to start, this file is the first thing to check."));
    }

    // Helper DLL
    if (in.helperFound) rep.rows.push_back(MakeRow("Helper DLL", Level::Ok, "present"));
    else rep.rows.push_back(MakeRow("Helper DLL", Level::Missing, "nvngx.dll_dlss5nr01.dll is missing from the addon folder",
                                    "The addon folder is incomplete: copy nvngx.dll_dlss5nr01.dll from the release zip next to DLSS5NR01.dll."));

    // Engine
    bool problemAbove = false;
    for (const auto& r : rep.rows) if (r.level == Level::Missing) problemAbove = true;
    switch (in.engine) {
    case EngineState::NotStarted: rep.rows.push_back(MakeRow("Engine", Level::Ok, "not started yet: it starts when Lossless Scaling scales a game")); break;
    case EngineState::Ready:      rep.rows.push_back(MakeRow("Engine", Level::Ok, "ready")); break;
    case EngineState::Running:    rep.rows.push_back(MakeRow("Engine", Level::Ok, "running")); break;
    case EngineState::Failed:
        rep.rows.push_back(MakeRow("Engine", Level::Missing, in.engineError.empty() ? std::string("failed to start") : PlainEngineError(in.engineError),
                                   problemAbove ? "See the problem above; press Restart engine under Advanced once it is fixed."
                                                : "Press Restart engine under Advanced after fixing it. The log (logs\\DLSS5NR01.log in the Lossless Scaling folder) has the details."));
        break;
    }

    // Overall and the one-line headline: the first problem, else the first note
    for (const auto& r : rep.rows) {
        if (r.level == Level::Missing) { rep.overall = Level::Missing; rep.headline = r.label + ": " + r.value; break; }
    }
    if (rep.overall == Level::Ok) {
        for (const auto& r : rep.rows) {
            if (r.level == Level::Note) { rep.overall = Level::Note; rep.headline = r.label + ": " + r.value; break; }
        }
    }
    if (rep.overall == Level::Ok) rep.headline = "Everything Neural Rendering needs is in place.";
    return rep;
}

namespace {

std::wstring FullPathOf(const std::wstring& p) {
    wchar_t b[2 * MAX_PATH];
    const DWORD n = GetFullPathNameW(p.c_str(), 2 * MAX_PATH, b, nullptr);
    return (n > 0 && n < 2 * MAX_PATH) ? std::wstring(b) : p;
}

bool IsFolder(const std::wstring& p) {
    const DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

bool EndsWithDll(const std::wstring& p) { return p.size() >= 4 && _wcsicmp(p.c_str() + p.size() - 4, L".dll") == 0; }

std::wstring TimeStamp() {
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t b[32];
    swprintf(b, 32, L"%04u%02u%02u-%02u%02u%02u", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    return b;
}

PlaceResult Refuse(const std::string& why) {
    PlaceResult r;
    r.message = why;
    return r;
}

} // namespace

PlaceResult PlaceModel(const std::wstring& source, const std::wstring& lsDir, const std::wstring& backupDir) {
    const std::wstring dest = lsDir + L"\\nvngx_dlssnr.dll";

    const DWORD attr = GetFileAttributesW(source.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) return Refuse("That file was not found: " + Utf8(source));
    if (attr & FILE_ATTRIBUTE_DIRECTORY) return Refuse("That is a folder, not a file. Pick nvngx_dlssnr.dll itself.");
    if (!EndsWithDll(source)) return Refuse("That is not a .dll file. Pick your copy of nvngx_dlssnr.dll.");
    uint64_t size = 0;
    if (!FileSize(source, size)) return Refuse("That file could not be read: " + Utf8(source));
    if (size < kSmallestPlausibleModel) return Refuse("That file is only " + SizeText(size) + ", too small to be the model (it is about 150 MB). Pick the complete file.");
    if (!IsFolder(lsDir)) return Refuse("The Lossless Scaling folder was not found: " + Utf8(lsDir));

    PlaceResult r;
    r.placedPath = dest;
    if (_wcsicmp(FullPathOf(source).c_str(), FullPathOf(dest).c_str()) == 0) {
        r.ok = true;
        r.message = "That file is already in place in the Lossless Scaling folder. Nothing changed.";
        return r;
    }

    // Move a model that is already there aside (a rename works even while a program has it loaded, a delete or an overwrite would not)
    std::wstring backup;
    uint64_t existing = 0;
    if (FileSize(dest, existing)) {
        CreateDirectoryW(backupDir.c_str(), nullptr);
        const std::wstring stem = backupDir + L"\\nvngx_dlssnr-" + TimeStamp();
        backup = stem + L".dll";
        for (int n = 2; GetFileAttributesW(backup.c_str()) != INVALID_FILE_ATTRIBUTES && n < 1000; ++n) backup = stem + L"-" + std::to_wstring(n) + L".dll";
        if (!MoveFileExW(dest.c_str(), backup.c_str(), MOVEFILE_COPY_ALLOWED)) {
            return Refuse("The model file that is there could not be moved aside (error " + std::to_string(GetLastError()) + "). Close Lossless Scaling and try again.");
        }
    }

    // Write the new copy under a temporary name, then rename it into place; put the old one back if anything fails
    const std::wstring part = dest + L".part";
    auto restore = [&] { if (!backup.empty()) MoveFileExW(backup.c_str(), dest.c_str(), MOVEFILE_COPY_ALLOWED); };
    if (!CopyFileW(source.c_str(), part.c_str(), FALSE)) {
        const DWORD e = GetLastError();
        DeleteFileW(part.c_str());
        restore();
        return Refuse("The file could not be copied (error " + std::to_string(e) + "). Is there room in the Lossless Scaling folder's drive?");
    }
    if (!MoveFileExW(part.c_str(), dest.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        const DWORD e = GetLastError();
        DeleteFileW(part.c_str());
        restore();
        return Refuse("The copy could not be put in place (error " + std::to_string(e) + ").");
    }

    r.ok = true;
    r.backupPath = backup;
    r.message = "Placed nvngx_dlssnr.dll (" + SizeText(size) + ") in the Lossless Scaling folder. Restart Lossless Scaling, or press Restart engine under Advanced, to use it.";
    if (!backup.empty()) r.message += " The file that was there is in the backups folder.";
    return r;
}

Inputs Gather(const std::wstring& modelPath, const std::wstring& addonDir) {
    Inputs in;

    // First NVIDIA hardware adapter
    IDXGIFactory1* factory = nullptr;
    if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory)))) {
        IDXGIAdapter1* adapter = nullptr;
        for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
            DXGI_ADAPTER_DESC1 d = {};
            adapter->GetDesc1(&d);
            adapter->Release();
            if (d.VendorId == 0x10DE && !(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
                in.nvidiaFound = true;
                in.gpuName = Utf8(d.Description);
                in.gpuMemoryBytes = d.DedicatedVideoMemory;
                break;
            }
        }
        factory->Release();
    }

    // The driver's NGX core: the registry names the driver folder, _nvngx.dll is in it
    const std::wstring ngxFolder = RegistryString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\NVIDIA Corporation\\Global\\NGXCore", L"FullPath");
    in.ngxRegistered = !ngxFolder.empty();
    if (in.ngxRegistered) {
        const std::wstring core = ngxFolder + L"\\_nvngx.dll";
        uint64_t size = 0;
        in.ngxCoreFound = FileSize(core, size);
        if (in.ngxCoreFound) in.ngxCoreVersion = FileVersion(core);
    }

    // The model and this addon's helper
    in.modelPath = Utf8(modelPath);
    in.modelFound = FileSize(modelPath, in.modelSize);
    if (in.modelFound) in.modelVersion = FileVersion(modelPath);
    uint64_t helperSize = 0;
    in.helperFound = FileSize(addonDir + L"\\" NR_FORWARDER_FILENAME, helperSize);
    return in;
}

} // namespace req
