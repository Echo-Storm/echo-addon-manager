#include "addon/hdr.h"
#include <dxgi1_6.h>
#include <windows.h>
#include <cwchar>
#include <vector>

namespace nr {

namespace {

template <class T> void SafeRelease(T*& p) { if (p) { p->Release(); p = nullptr; } }

// The SDR white Windows uses for the display with this GDI name ("\\.\DISPLAY1"), in nits; 0 when it cannot be read.
float SdrWhiteNits(const wchar_t* gdiName) {
    UINT32 paths = 0, modes = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &paths, &modes) != ERROR_SUCCESS) return 0.0f;
    std::vector<DISPLAYCONFIG_PATH_INFO> path(paths);
    std::vector<DISPLAYCONFIG_MODE_INFO> mode(modes);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &paths, path.data(), &modes, mode.data(), nullptr) != ERROR_SUCCESS) return 0.0f;
    for (UINT32 i = 0; i < paths; ++i) {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
        source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME; source.header.size = sizeof source;
        source.header.adapterId = path[i].sourceInfo.adapterId; source.header.id = path[i].sourceInfo.id;
        if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS || wcscmp(source.viewGdiDeviceName, gdiName) != 0) continue;
        DISPLAYCONFIG_SDR_WHITE_LEVEL white{};
        white.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL; white.header.size = sizeof white;
        white.header.adapterId = path[i].targetInfo.adapterId; white.header.id = path[i].targetInfo.id;
        if (DisplayConfigGetDeviceInfo(&white.header) == ERROR_SUCCESS && white.SDRWhiteLevel > 0) return white.SDRWhiteLevel / 1000.0f * 80.0f;   // 1000 = 80 nits
    }
    return 0.0f;
}

DisplayHdr FromOutput(IDXGIOutput* output) {
    DisplayHdr d;
    IDXGIOutput6* o6 = nullptr;
    if (!output || FAILED(output->QueryInterface(IID_PPV_ARGS(&o6)))) return d;
    DXGI_OUTPUT_DESC1 desc{};
    if (SUCCEEDED(o6->GetDesc1(&desc))) {
        d.hdr = desc.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
        // in HDR, SDR white is where Windows' slider puts it; a display in SDR shows scRGB 1.0 as its white (80 nits in scRGB's own terms)
        if (!d.hdr) d.whiteNits = 80.0f;
        else if (const float w = SdrWhiteNits(desc.DeviceName); w > 1.0f) d.whiteNits = w;
    }
    o6->Release();
    return d;
}

} // namespace

DisplayHdr QueryDisplayHdr(IDXGISwapChain* chain, ID3D11Device* device) {
    static DisplayHdr cached; static ULONGLONG readAt = 0; static const void* readFor = nullptr;
    const ULONGLONG now = GetTickCount64();
    const void* key = chain ? static_cast<const void*>(chain) : static_cast<const void*>(device);
    if (readAt && now - readAt < 2000 && key == readFor) return cached;
    readAt = now; readFor = key;
    DisplayHdr d;
    IDXGIOutput* output = nullptr;
    if (chain && SUCCEEDED(chain->GetContainingOutput(&output)) && output) {
        d = FromOutput(output);
        output->Release();
    } else if (device) {   // no swap chain yet: the first display of the card that runs in HDR, if any
        IDXGIDevice* dxgi = nullptr; IDXGIAdapter* adapter = nullptr;
        if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&dxgi))) && SUCCEEDED(dxgi->GetAdapter(&adapter))) {
            for (UINT i = 0; adapter->EnumOutputs(i, &output) != DXGI_ERROR_NOT_FOUND; ++i) {
                const DisplayHdr o = FromOutput(output);
                SafeRelease(output);
                if (o.hdr) { d = o; break; }
                if (i == 0) d = o;
            }
        }
        SafeRelease(adapter); SafeRelease(dxgi);
    }
    cached = d;
    return d;
}

FrameEncoding EncodingOf(DXGI_FORMAT viewFormat, int setting, bool displayHdr) {
    switch (viewFormat) {
    case DXGI_FORMAT_R16G16B16A16_FLOAT: return setting == 1 ? FrameEncoding::Sdr : FrameEncoding::ScRgb;
    case DXGI_FORMAT_R10G10B10A2_UNORM: return setting == 2 || (setting == 0 && displayHdr) ? FrameEncoding::Hdr10 : FrameEncoding::Sdr;
    default: return FrameEncoding::Sdr;   // 8-bit holds SDR only
    }
}

const char* EncodingName(FrameEncoding e) {
    switch (e) {
    case FrameEncoding::ScRgb: return "scRGB (HDR)";
    case FrameEncoding::Hdr10: return "HDR10 (PQ)";
    default: return "SDR";
    }
}

} // namespace nr
