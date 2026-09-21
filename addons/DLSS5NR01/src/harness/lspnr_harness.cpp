// lspnr_harness — proves and measures the NR engine without Lossless Scaling.
//
//   lspnr_harness.exe [image.png] [--adapter N] [--snippet path] [--debug] [--iters N]
//
// 1. D3D12 device on the display NVIDIA adapter.
// 2. Driver core init (static lib) + capability block.
// 3. Forwarder (nvngx.dll_lspnr.dll): probe, float-slot round-trip, init, create feature 18.
// 4. Evaluate on the image at 1080p / 1440p / 4K with flat depth + zero motion vectors,
//    GPU-timed with timestamp queries; writes in_<size>.png / out_<size>.png beside the exe.

#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wincodec.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdarg>
#include <string>
#include <vector>
#include <cmath>
#include "nvsdk_ngx.h"
#include "forwarder/lspnr_api.h"

// ------------------------------------------------------------------ logging
static FILE* g_log = nullptr;
static void P(const char* fmt, ...) {
    char buf[4096]; va_list a; va_start(a, fmt); vsnprintf(buf, sizeof buf, fmt, a); va_end(a);
    fputs(buf, stdout); fputc('\n', stdout); fflush(stdout);
    if (g_log) { fputs(buf, g_log); fputc('\n', g_log); fflush(g_log); }
}
static const char* R(int r) {
    switch ((NVSDK_NGX_Result)r) {
    case NVSDK_NGX_Result_Success: return "Success";
    case NVSDK_NGX_Result_Fail: return "Fail";
    case NVSDK_NGX_Result_FAIL_FeatureNotSupported: return "FAIL_FeatureNotSupported";
    case NVSDK_NGX_Result_FAIL_PlatformError: return "FAIL_PlatformError";
    case NVSDK_NGX_Result_FAIL_FeatureAlreadyExists: return "FAIL_FeatureAlreadyExists";
    case NVSDK_NGX_Result_FAIL_FeatureNotFound: return "FAIL_FeatureNotFound";
    case NVSDK_NGX_Result_FAIL_InvalidParameter: return "FAIL_InvalidParameter";
    case NVSDK_NGX_Result_FAIL_ScratchBufferTooSmall: return "FAIL_ScratchBufferTooSmall";
    case NVSDK_NGX_Result_FAIL_NotInitialized: return "FAIL_NotInitialized";
    case NVSDK_NGX_Result_FAIL_UnsupportedInputFormat: return "FAIL_UnsupportedInputFormat";
    case NVSDK_NGX_Result_FAIL_RWFlagMissing: return "FAIL_RWFlagMissing";
    case NVSDK_NGX_Result_FAIL_MissingInput: return "FAIL_MissingInput";
    case NVSDK_NGX_Result_FAIL_UnableToInitializeFeature: return "FAIL_UnableToInitializeFeature";
    case NVSDK_NGX_Result_FAIL_OutOfDate: return "FAIL_OutOfDate";
    case NVSDK_NGX_Result_FAIL_OutOfGPUMemory: return "FAIL_OutOfGPUMemory";
    case NVSDK_NGX_Result_FAIL_UnsupportedFormat: return "FAIL_UnsupportedFormat";
    case NVSDK_NGX_Result_FAIL_UnableToWriteToAppDataPath: return "FAIL_UnableToWriteToAppDataPath";
    case NVSDK_NGX_Result_FAIL_UnsupportedParameter: return "FAIL_UnsupportedParameter";
    case NVSDK_NGX_Result_FAIL_Denied: return "FAIL_Denied";
    case NVSDK_NGX_Result_FAIL_NotImplemented: return "FAIL_NotImplemented";
    default: return "?";
    }
}
static void NVSDK_CONV LogCb(const char* msg, NVSDK_NGX_Logging_Level lvl, NVSDK_NGX_Feature f) {
    std::string s(msg ? msg : ""); while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    if (s.find("NGXLoadConfig") != std::string::npos) return;   // config dump spam
    P("      [ngx L%d f%d] %s", (int)lvl, (int)f, s.c_str());
}

// ------------------------------------------------------------------ WIC
static IWICImagingFactory* g_wic = nullptr;
static bool LoadImage(const wchar_t* path, std::vector<uint8_t>& rgba, UINT& w, UINT& h) {
    IWICBitmapDecoder* dec = nullptr;
    if (FAILED(g_wic->CreateDecoderFromFilename(path, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec))) return false;
    IWICBitmapFrameDecode* fr = nullptr; dec->GetFrame(0, &fr);
    IWICFormatConverter* cv = nullptr; g_wic->CreateFormatConverter(&cv);
    bool ok = SUCCEEDED(cv->Initialize(fr, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom));
    if (ok) { cv->GetSize(&w, &h); rgba.resize((size_t)w * h * 4); ok = SUCCEEDED(cv->CopyPixels(nullptr, w * 4, (UINT)rgba.size(), rgba.data())); }
    cv->Release(); fr->Release(); dec->Release();
    return ok;
}
static bool SavePng(const wchar_t* path, UINT w, UINT h, const uint8_t* rgba) {
    IWICStream* st = nullptr; g_wic->CreateStream(&st);
    if (FAILED(st->InitializeFromFilename(path, GENERIC_WRITE))) { st->Release(); return false; }
    IWICBitmapEncoder* enc = nullptr; g_wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc);
    enc->Initialize(st, WICBitmapEncoderNoCache);
    IWICBitmapFrameEncode* fe = nullptr; IPropertyBag2* pb = nullptr; enc->CreateNewFrame(&fe, &pb);
    fe->Initialize(pb); fe->SetSize(w, h);
    // The PNG encoder may answer SetPixelFormat with BGRA instead of the RGBA we asked for; honour whatever it chose.
    WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppRGBA; fe->SetPixelFormat(&fmt);
    const uint8_t* px = rgba; std::vector<uint8_t> swapped;
    if (IsEqualGUID(fmt, GUID_WICPixelFormat32bppBGRA) || IsEqualGUID(fmt, GUID_WICPixelFormat32bppBGR)) {
        swapped.assign(rgba, rgba + (size_t)w * h * 4);
        for (size_t i = 0; i < swapped.size(); i += 4) { uint8_t t = swapped[i]; swapped[i] = swapped[i + 2]; swapped[i + 2] = t; }
        px = swapped.data();
    } else if (!IsEqualGUID(fmt, GUID_WICPixelFormat32bppRGBA)) {
        P("  SavePng: encoder chose an unexpected pixel format; colours may be off");
    }
    bool ok = SUCCEEDED(fe->WritePixels(h, w * 4, w * h * 4, const_cast<BYTE*>(px))) && SUCCEEDED(fe->Commit()) && SUCCEEDED(enc->Commit());
    if (pb) pb->Release(); fe->Release(); enc->Release(); st->Release();
    return ok;
}
static void Resample(const std::vector<uint8_t>& src, UINT sw, UINT sh, std::vector<uint8_t>& dst, UINT dw, UINT dh) {
    dst.resize((size_t)dw * dh * 4);
    for (UINT y = 0; y < dh; ++y) { UINT sy = (UINT)((uint64_t)y * sh / dh);
        for (UINT x = 0; x < dw; ++x) { UINT sx = (UINT)((uint64_t)x * sw / dw);
            memcpy(&dst[((size_t)y * dw + x) * 4], &src[((size_t)sy * sw + sx) * 4], 4); } }
}

// ------------------------------------------------------------------ D3D12
struct Ctx {
    ID3D12Device* dev = nullptr; ID3D12CommandQueue* queue = nullptr; ID3D12CommandAllocator* alloc = nullptr;
    ID3D12GraphicsCommandList* list = nullptr; ID3D12Fence* fence = nullptr; HANDLE ev = nullptr; UINT64 fv = 0;
    std::vector<ID3D12Resource*> garbage;   // upload/readback buffers released after the next wait
    bool Init(IDXGIAdapter1* adapter, bool debug) {
        if (debug) { ID3D12Debug* dbg = nullptr; if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dbg)))) { dbg->EnableDebugLayer(); dbg->Release(); P("  D3D12 debug layer ON"); } }
        if (FAILED(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&dev)))) return false;
        D3D12_COMMAND_QUEUE_DESC q{}; q.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (FAILED(dev->CreateCommandQueue(&q, IID_PPV_ARGS(&queue)))) return false;
        if (FAILED(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc)))) return false;
        if (FAILED(dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc, nullptr, IID_PPV_ARGS(&list)))) return false;
        if (FAILED(dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))) return false;
        ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        return true;
    }
    void ExecAndWait() {
        list->Close(); ID3D12CommandList* l[] = { list }; queue->ExecuteCommandLists(1, l);
        queue->Signal(fence, ++fv);
        if (fence->GetCompletedValue() < fv) { fence->SetEventOnCompletion(fv, ev); WaitForSingleObject(ev, INFINITE); }
        for (auto* g : garbage) g->Release(); garbage.clear();
        alloc->Reset(); list->Reset(alloc, nullptr);
    }
};
static void Barrier(ID3D12GraphicsCommandList* l, ID3D12Resource* r, D3D12_RESOURCE_STATES b, D3D12_RESOURCE_STATES a) {
    D3D12_RESOURCE_BARRIER x{}; x.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; x.Transition.pResource = r;
    x.Transition.StateBefore = b; x.Transition.StateAfter = a; x.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    l->ResourceBarrier(1, &x);
}
static ID3D12Resource* MakeTex(ID3D12Device* dev, UINT w, UINT h, DXGI_FORMAT fmt, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES state) {
    D3D12_HEAP_PROPERTIES hp{}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC d{}; d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; d.Width = w; d.Height = h; d.DepthOrArraySize = 1;
    d.MipLevels = 1; d.Format = fmt; d.SampleDesc.Count = 1; d.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN; d.Flags = flags;
    ID3D12Resource* r = nullptr; HRESULT hr = dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, state, nullptr, IID_PPV_ARGS(&r));
    if (FAILED(hr)) P("  CreateCommittedResource %ux%u fmt=%d failed 0x%08x", w, h, (int)fmt, hr);
    return r;
}
static ID3D12Resource* MakeBuf(ID3D12Device* dev, UINT64 size, D3D12_HEAP_TYPE heap, D3D12_RESOURCE_STATES state) {
    D3D12_HEAP_PROPERTIES hp{}; hp.Type = heap;
    D3D12_RESOURCE_DESC d{}; d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; d.Width = size; d.Height = 1; d.DepthOrArraySize = 1;
    d.MipLevels = 1; d.Format = DXGI_FORMAT_UNKNOWN; d.SampleDesc.Count = 1; d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ID3D12Resource* r = nullptr; dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, state, nullptr, IID_PPV_ARGS(&r));
    return r;
}
// Records an upload into the current list; the texture must currently be in COPY_DEST.
static void Upload(Ctx& c, ID3D12Resource* tex, UINT w, UINT h, UINT bpp, const void* data, D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_DESC d = tex->GetDesc(); D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{}; UINT rows = 0; UINT64 rowSize = 0, total = 0;
    c.dev->GetCopyableFootprints(&d, 0, 1, 0, &fp, &rows, &rowSize, &total);
    ID3D12Resource* up = MakeBuf(c.dev, total, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    uint8_t* dst = nullptr; up->Map(0, nullptr, (void**)&dst);
    for (UINT y = 0; y < h; ++y) memcpy(dst + fp.Offset + (size_t)y * fp.Footprint.RowPitch, (const uint8_t*)data + (size_t)y * w * bpp, (size_t)w * bpp);
    up->Unmap(0, nullptr);
    D3D12_TEXTURE_COPY_LOCATION dl{}; dl.pResource = tex; dl.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; dl.SubresourceIndex = 0;
    D3D12_TEXTURE_COPY_LOCATION sl{}; sl.pResource = up; sl.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; sl.PlacedFootprint = fp;
    c.list->CopyTextureRegion(&dl, 0, 0, 0, &sl, nullptr);
    Barrier(c.list, tex, D3D12_RESOURCE_STATE_COPY_DEST, after);
    c.garbage.push_back(up);
}
static std::vector<uint8_t> Readback(Ctx& c, ID3D12Resource* tex, UINT w, UINT h, UINT bpp, D3D12_RESOURCE_STATES cur) {
    D3D12_RESOURCE_DESC d = tex->GetDesc(); D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{}; UINT rows = 0; UINT64 rowSize = 0, total = 0;
    c.dev->GetCopyableFootprints(&d, 0, 1, 0, &fp, &rows, &rowSize, &total);
    ID3D12Resource* rb = MakeBuf(c.dev, total, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    Barrier(c.list, tex, cur, D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION sl{}; sl.pResource = tex; sl.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; sl.SubresourceIndex = 0;
    D3D12_TEXTURE_COPY_LOCATION dl{}; dl.pResource = rb; dl.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; dl.PlacedFootprint = fp;
    c.list->CopyTextureRegion(&dl, 0, 0, 0, &sl, nullptr);
    Barrier(c.list, tex, D3D12_RESOURCE_STATE_COPY_SOURCE, cur);
    c.ExecAndWait();
    std::vector<uint8_t> out((size_t)w * h * bpp); uint8_t* src = nullptr; rb->Map(0, nullptr, (void**)&src);
    for (UINT y = 0; y < h; ++y) memcpy(&out[(size_t)y * w * bpp], src + fp.Offset + (size_t)y * fp.Footprint.RowPitch, (size_t)w * bpp);
    rb->Unmap(0, nullptr); rb->Release();
    return out;
}

// ------------------------------------------------------------------ forwarder
struct Fwd {
    HMODULE h = nullptr;
    PFN_lspnr_probe probe = nullptr; PFN_lspnr_init init = nullptr; PFN_lspnr_set_float_slot setFloatSlot = nullptr;
    PFN_lspnr_probe_float probeFloat = nullptr; PFN_lspnr_get_float getFloat = nullptr; PFN_lspnr_create create = nullptr;
    PFN_lspnr_evaluate evaluate = nullptr; PFN_lspnr_release release = nullptr; PFN_lspnr_last_result last = nullptr;
    PFN_lspnr_scaling_ratio scalingRatio = nullptr;
    bool Load(const std::wstring& path) {
        h = LoadLibraryW(path.c_str()); if (!h) { P("  forwarder LoadLibrary failed: %lu (%ls)", GetLastError(), path.c_str()); return false; }
#define GP(n, v) v = (decltype(v))GetProcAddress(h, n); if (!v) { P("  forwarder export missing: %s", n); return false; }
        GP("lspnr_probe", probe) GP("lspnr_init", init) GP("lspnr_set_float_slot", setFloatSlot) GP("lspnr_probe_float", probeFloat)
        GP("lspnr_get_float", getFloat) GP("lspnr_create", create) GP("lspnr_evaluate", evaluate) GP("lspnr_release", release) GP("lspnr_last_result", last) GP("lspnr_scaling_ratio", scalingRatio)
#undef GP
        return true;
    }
};

// ------------------------------------------------------------------ capability block helpers
// Direct vtable calls into the driver core's block (no caller-name check applies to the block itself).
static void BSetUI (void* b, const char* k, unsigned v)            { ((void(*)(void*, const char*, unsigned))(*(void***)b)[3])(b, k, v); }
static void BSetULL(void* b, const char* k, unsigned long long v)  { ((void(*)(void*, const char*, unsigned long long))(*(void***)b)[0])(b, k, v); }
static int  g_blockFloatSlot = 5;
static void BSetF  (void* b, const char* k, float v)               { ((void(*)(void*, const char*, float))(*(void***)b)[g_blockFloatSlot])(b, k, v); }

// Get-trace: replace the block's vtable pointer with a copy whose Get slots (8..15, uniform
// (this, key, out*) signature on x64) record every key the snippet reads and the result it got.
#include <map>
static void** g_realVt = nullptr; static void* g_traceVt[64]; static bool g_traceOn = false;
struct ReadRec { int slot; int result; int count; };
static std::map<std::string, ReadRec> g_reads;
typedef int (*PFN_BlockGet)(void*, const char*, void*);
template<int S> static int TraceGet(void* self, const char* key, void* out) {
    int r = ((PFN_BlockGet)g_realVt[S])(self, key, out);
    if (g_traceOn && key) { auto& e = g_reads[key]; e.slot = S; e.result = r; e.count++; }
    return r;
}
static void InstallTrace(void* block) {
    g_realVt = *(void***)block;
    memcpy(g_traceVt, g_realVt, sizeof(g_traceVt));
    g_traceVt[8] = (void*)&TraceGet<8>;  g_traceVt[9] = (void*)&TraceGet<9>;  g_traceVt[10] = (void*)&TraceGet<10>; g_traceVt[11] = (void*)&TraceGet<11>;
    g_traceVt[12] = (void*)&TraceGet<12>; g_traceVt[13] = (void*)&TraceGet<13>; g_traceVt[14] = (void*)&TraceGet<14>; g_traceVt[15] = (void*)&TraceGet<15>;
    *(void***)block = g_traceVt; g_traceOn = true;
}
static void RemoveTrace(void* block) { if (g_realVt) *(void***)block = g_realVt; g_traceOn = false; }
static void DumpTrace(const char* phase) {
    P("  [trace:%s] %zu distinct keys read by the snippet:", phase, g_reads.size());
    for (auto& kv : g_reads) P("     get[%2d] %-52s -> %-8s x%d", kv.second.slot, kv.first.c_str(), kv.second.result == 1 ? "hit" : "MISS", kv.second.count);
    g_reads.clear();
}

static uint16_t f2h(float f) { uint32_t x; memcpy(&x, &f, 4); uint32_t s = (x >> 16) & 0x8000; int e = ((x >> 23) & 0xff) - 112; uint32_t m = x & 0x7fffff; if (e <= 0) return (uint16_t)s; if (e >= 31) return (uint16_t)(s | 0x7c00); return (uint16_t)(s | (e << 10) | (m >> 13)); }
static float h2f(uint16_t h) { uint32_t s = (h & 0x8000) << 16, e = (h >> 10) & 0x1f, m = h & 0x3ff, x; if (e == 0) x = s; else if (e == 31) x = s | 0x7f800000; else x = s | ((e + 112) << 23) | (m << 13); float f; memcpy(&f, &x, 4); return f; }

// ------------------------------------------------------------------ main
int wmain(int argc, wchar_t** argv) {
    wchar_t exeDir[MAX_PATH]; GetModuleFileNameW(nullptr, exeDir, MAX_PATH); *wcsrchr(exeDir, L'\\') = 0;
    const wchar_t* lsDir = L"C:\\Program Files (x86)\\Steam\\steamapps\\common\\Lossless Scaling";
    std::wstring image = std::wstring(lsDir) + L"\\LosslessScaling 2026-09-02 20-48-24_1.png";
    std::wstring snippet = std::wstring(lsDir) + L"\\nvngx_dlssnr.dll";
    int adapterIdx = -1; bool debug = false; int iters = 30;
    unsigned autoMask = 1, uiCorr = 1, style = 1, preset = 0; float intensity = 1.0f, strength = 1.0f;
    std::wstring depthMode = L"flat", mvMode = L"zero", only, fmtName = L"rgba8";
    bool trace = false, stdkeys = false, primesr = false; float scaling = 1.0f;
    float ls = -99.0f, lt = -99.0f, skin = -99.0f, evalInt = -99.0f; int evalStyle = -1;
    int perf = -1; bool perfScan = false;   // --perf N: PerfQualityValue -> scaling-ratio callback -> DLSSNR.ScalingRatio; --perfscan: try 0..6
    int passes = 1; bool resetAll = false, cmaskOff = false; std::wstring cmask, cmaskCh = L"1111"; UINT customW = 0, customH = 0;   // --cmaskch RGBA e.g. 0100 = only G set on the left half   // --passes N (feed output back as colour N-1 times), --resetall, --cmask r8|r32f|rgba8   // -99 = follow --strength; eval* = override at evaluate time (after create)
    for (int i = 1; i < argc; ++i) {
        std::wstring a = argv[i];
        auto next = [&](void) -> const wchar_t* { return (i + 1 < argc) ? argv[++i] : L""; };
        if      (a == L"--adapter")   adapterIdx = _wtoi(next());
        else if (a == L"--snippet")   snippet = next();
        else if (a == L"--iters")     iters = _wtoi(next());
        else if (a == L"--automask")  autoMask = (unsigned)_wtoi(next());
        else if (a == L"--uicorr")    uiCorr = (unsigned)_wtoi(next());
        else if (a == L"--style")     style = (unsigned)_wtoi(next());
        else if (a == L"--preset")    preset = (unsigned)_wtoi(next());
        else if (a == L"--intensity") intensity = (float)_wtof(next());
        else if (a == L"--strength")  strength = (float)_wtof(next());
        else if (a == L"--depth")     depthMode = next();      // flat | ramp | zero | one
        else if (a == L"--mv")        mvMode = next();         // zero | small
        else if (a == L"--only")      only = next();           // 1080p | 1440p | 4K
        else if (a == L"--fmt")       fmtName = next();        // rgba8 | rgba16f
        else if (a == L"--trace")     trace = true;            // log every key the snippet reads from the block
        else if (a == L"--stdkeys")   stdkeys = true;          // also populate the standard DLSS keys a game would have set
        else if (a == L"--primesr")   { primesr = true; stdkeys = true; }   // create a DLSS-SR feature through the core first (plants the core's callbacks in the block)
        else if (a == L"--scaling")   scaling = (float)_wtof(next());       // DLSSNR.ScalingRatio
        else if (a == L"--ls")        ls = (float)_wtof(next());            // DLSSNR.LocalStructureStrength
        else if (a == L"--lt")        lt = (float)_wtof(next());            // DLSSNR.LocalToneStrength
        else if (a == L"--skin")      skin = (float)_wtof(next());          // DLSSNR.SkinStructureStrength
        else if (a == L"--evalint")   evalInt = (float)_wtof(next());       // set DLSSNR.Intensity in the block AFTER create (tests evaluate-time tuning)
        else if (a == L"--evalstyle") evalStyle = _wtoi(next());            // same for DLSSNR.Style
        else if (a == L"--passes")    passes = _wtoi(next());
        else if (a == L"--resetall")  resetAll = true;                        // DLSSNR.Reset=1 on every evaluate (no temporal history)
        else if (a == L"--cmask")     cmask = next();                         // feed DLSSNR.ControlMask: left half 1, right half 0
        else if (a == L"--cmaskch")   cmaskCh = next();
        else if (a == L"--cmaskoff")  cmaskOff = true;                        // after the warm-up evaluates, hand the model a null mask again
        else if (a == L"--size")      { const wchar_t* v = next(); swscanf(v, L"%ux%u", &customW, &customH); }   // any WxH instead of the presets
        else if (a == L"--perf")      perf = _wtoi(next());
        else if (a == L"--perfscan")  perfScan = true;
        else if (a == L"--debug")     debug = true;
        else image = a;
    }
    g_log = _wfopen((std::wstring(exeDir) + L"\\lspnr_harness_log.txt").c_str(), L"w");
    P("=== lspnr_harness ===");
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&g_wic));

    // ---- image
    std::vector<uint8_t> src; UINT sw = 0, sh = 0;
    if (LoadImage(image.c_str(), src, sw, sh)) P("[img] %ls  %ux%u", image.c_str(), sw, sh);
    else { sw = 2560; sh = 1440; src.resize((size_t)sw * sh * 4); P("[img] could not load %ls — using synthetic pattern", image.c_str());
        for (UINT y = 0; y < sh; ++y) for (UINT x = 0; x < sw; ++x) { uint8_t* p = &src[((size_t)y * sw + x) * 4];
            p[0] = (uint8_t)(x * 255 / sw); p[1] = (uint8_t)(y * 255 / sh); p[2] = ((x / 8 + y / 8) & 1) ? 217 : 38; p[3] = 255; } }

    // ---- adapter
    IDXGIFactory1* fac = nullptr; CreateDXGIFactory1(IID_PPV_ARGS(&fac));
    std::vector<IDXGIAdapter1*> adapters; int pick = -1;
    for (UINT i = 0;; ++i) { IDXGIAdapter1* a = nullptr; if (fac->EnumAdapters1(i, &a) == DXGI_ERROR_NOT_FOUND) break;
        DXGI_ADAPTER_DESC1 d; a->GetDesc1(&d); IDXGIOutput* o = nullptr; bool disp = SUCCEEDED(a->EnumOutputs(0, &o)) && o; if (o) o->Release();
        P("[gpu] [%u] '%ls' LUID=%08x:%08x display=%d", i, d.Description, d.AdapterLuid.HighPart, d.AdapterLuid.LowPart, disp);
        { IDXGIAdapter2* a2 = nullptr; if (SUCCEEDED(a->QueryInterface(IID_PPV_ARGS(&a2))) && a2) { DXGI_ADAPTER_DESC2 d2; a2->GetDesc2(&d2);
            P("[gpu]     preemption granularity: graphics %d (0 dma-buffer,1 primitive,2 triangle,3 pixel,4 instruction)  compute %d (0 dma-buffer,1 dispatch,2 thread-group,3 thread,4 instruction)", (int)d2.GraphicsPreemptionGranularity, (int)d2.ComputePreemptionGranularity); a2->Release(); } }
        adapters.push_back(a); if (pick < 0 && d.VendorId == 0x10DE && disp) pick = (int)i; }
    if (adapterIdx >= 0) pick = adapterIdx; if (pick < 0) pick = 0;
    P("[gpu] using adapter [%d]", pick);

    Ctx c; if (!c.Init(adapters[pick], debug)) { P("D3D12 init failed"); return 1; }
    P("[d3d12] device %p", c.dev);

    // ---- driver core + capability block
    const unsigned long long APPID = 0x24480451ull;
    const wchar_t* paths[] = { lsDir, exeDir };
    NVSDK_NGX_FeatureCommonInfo ci{}; ci.PathListInfo.Path = paths; ci.PathListInfo.Length = 2;
    ci.LoggingInfo.LoggingCallback = LogCb; ci.LoggingInfo.MinimumLoggingLevel = NVSDK_NGX_LOGGING_LEVEL_ON; ci.LoggingInfo.DisableOtherLoggingSinks = false;
    NVSDK_NGX_Result r = NVSDK_NGX_D3D12_Init(APPID, exeDir, c.dev, &ci, NVSDK_NGX_Version_API);
    P("[core] NVSDK_NGX_D3D12_Init -> %s", R(r));
    if (NVSDK_NGX_FAILED(r)) return 2;
    NVSDK_NGX_Parameter* caps = nullptr; r = NVSDK_NGX_D3D12_GetCapabilityParameters(&caps);
    P("[core] GetCapabilityParameters -> %s block=%p", R(r), caps);
    if (!caps) return 2;

    // ---- forwarder
    Fwd f; if (!f.Load(std::wstring(exeDir) + L"\\" + LSPNR_FORWARDER_FILENAME)) return 3;
    int bits = f.probe(snippet.c_str());
    P("[fwd] probe(%ls) -> 0x%x (need 0xF)", snippet.c_str(), bits);
    if ((bits & 0xF) != 0xF) return 3;

    // Float-slot probe. The snippet reads its floats through getter slot 14 (observed with --trace), so the
    // only correct criterion is "getter 14 returns exactly the float we wrote". Validating a setter against
    // its own +8 getter is a trap: the double pair round-trips raw XMM bits and looks like a float pair.
    const int kSnippetFloatGetter = 14;
    int floatSlot = -1;
    for (int s : { 6, 5, 1, 2, 4, 7 }) {
        f.probeFloat(caps, "LSPNR.Probe", 1.5f, s);
        alignas(8) uint8_t b14[8] = {}, bOwn[8] = {};
        int g14 = f.getFloat(caps, "LSPNR.Probe", b14, kSnippetFloatGetter);
        int gOwn = f.getFloat(caps, "LSPNR.Probe", bOwn, s + 8);
        float v14, vOwn; double d14; memcpy(&v14, b14, 4); memcpy(&vOwn, bOwn, 4); memcpy(&d14, b14, 8);
        P("[fwd] setter %d: getter14 -> %d as float %g (as double %g)   getter%d -> %d as float %g", s, g14, v14, d14, s + 8, gOwn, vOwn);
        f.probeFloat(caps, "LSPNR.Probe", 0.0f, s);   // scrub so the next candidate cannot inherit the value
        if (g14 == 1 && fabsf(v14 - 1.5f) < 1e-6f) { floatSlot = s; break; }
    }
    if (floatSlot < 0) { P("!!! no setter makes getter 14 return the float — cannot set tuning floats"); return 4; }
    f.setFloatSlot(floatSlot); g_blockFloatSlot = floatSlot; P("[fwd] float setter slot = %d", floatSlot);

    if (trace) InstallTrace(caps);
    int ir = f.init(snippet.c_str(), exeDir, c.dev, caps);
    P("[fwd] init -> %d (%s), PopulateParameters_Impl -> %s", ir, R(ir), R(f.last(3)));
    if (trace) DumpTrace("init");
    if (ir != 1) return 5;
    if (perfScan) for (unsigned q = 0; q < 7; ++q) { float ratio = -1.0f; int rr = f.scalingRatio(caps, q, &ratio); P("[fwd] scaling-ratio callback(PerfQualityValue=%u) -> %d (%s)  DLSSNR.ScalingRatio = %g", q, rr, rr == -2 ? "no callback in block" : R(rr), ratio); }
    if (perf >= 0) { float ratio = -1.0f; int rr = f.scalingRatio(caps, (unsigned)perf, &ratio); P("[fwd] scaling-ratio callback(PerfQualityValue=%d) -> %d (%s)  DLSSNR.ScalingRatio = %g", perf, rr, rr == -2 ? "no callback in block" : R(rr), ratio); if (rr == 1 && ratio > 0) scaling = ratio; }
    const bool half = (fmtName == L"rgba16f");
    const DXGI_FORMAT colorFmt = half ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_R8G8B8A8_UNORM;
    const UINT colorBpp = half ? 8 : 4;
    P("[cfg] color format = %ls  stdkeys=%d trace=%d", fmtName.c_str(), stdkeys, trace);

    // ---- sizes
    struct Sz { UINT w, h; const wchar_t* name; } sizes[] = { {1920, 1080, L"1080p"}, {2560, 1440, L"1440p"}, {3840, 2160, L"4K"}, {0, 0, L"custom"} };
    if (!customW) sizes[3].w = 0; else { sizes[3].w = customW; sizes[3].h = customH; only = L"custom"; }
    for (auto& z : sizes) {
        if (z.w == 0) continue;
        if (!only.empty() && only != z.name) continue;
        P("");
        P("[%ls] %ux%u  automask=%u uicorr=%u style=%u preset=%u intensity=%g strength=%g depth=%ls mv=%ls",
          z.name, z.w, z.h, autoMask, uiCorr, style, preset, intensity, strength, depthMode.c_str(), mvMode.c_str());
        std::vector<uint8_t> rgba; Resample(src, sw, sh, rgba, z.w, z.h);
        const UINT iw = z.w, ih = z.h; const std::vector<uint8_t>& rgbaIn = rgba;
        std::vector<float> depth((size_t)iw * ih, 0.5f);
        if (depthMode == L"zero") std::fill(depth.begin(), depth.end(), 0.0f);
        else if (depthMode == L"one") std::fill(depth.begin(), depth.end(), 1.0f);
        else if (depthMode == L"ramp") for (UINT y = 0; y < ih; ++y) for (UINT x = 0; x < iw; ++x) depth[(size_t)y * iw + x] = 0.05f + 0.9f * (float)y / (float)ih;
        std::vector<uint16_t> mv((size_t)iw * ih * 2, 0);
        if (mvMode == L"small") std::fill(mv.begin(), mv.end(), (uint16_t)0x3800);   // 0.5 px in every channel
        // colour payload in the chosen format (rgba16f = sRGB byte values / 255 as half floats, no linearisation)
        std::vector<uint8_t> colorBytes;
        if (half) { colorBytes.resize((size_t)iw * ih * 8); uint16_t* q = (uint16_t*)colorBytes.data(); for (size_t i = 0; i < (size_t)iw * ih * 4; ++i) q[i] = f2h(rgbaIn[i] / 255.0f); }
        else colorBytes = rgbaIn;

        auto* color  = MakeTex(c.dev, iw, ih, colorFmt,                     D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
        auto* depthT = MakeTex(c.dev, iw, ih, DXGI_FORMAT_R32_FLOAT,        D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
        auto* mvT    = MakeTex(c.dev, iw, ih, DXGI_FORMAT_R16G16_FLOAT,     D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
        auto* out    = MakeTex(c.dev, z.w, z.h, colorFmt,                   D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        if (!color || !depthT || !mvT || !out) continue;
        const auto SRV = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        Upload(c, color, iw, ih, colorBpp, colorBytes.data(), SRV);
        Upload(c, depthT, iw, ih, 4, depth.data(), SRV);
        Upload(c, mvT, iw, ih, 4, mv.data(), SRV);
        ID3D12Resource* cmT = nullptr;
        if (!cmask.empty()) {
            DXGI_FORMAT cf = cmask == L"r32f" ? DXGI_FORMAT_R32_FLOAT : cmask == L"rgba8" ? DXGI_FORMAT_R8G8B8A8_UNORM : DXGI_FORMAT_R8_UNORM;
            UINT bpp = cf == DXGI_FORMAT_R8_UNORM ? 1 : 4;
            std::vector<uint8_t> cm((size_t)iw * ih * bpp, 0);
            for (UINT y = 0; y < ih; ++y) for (UINT x = 0; x < iw / 2; ++x) {
                uint8_t* q = &cm[((size_t)y * iw + x) * bpp];
                if (cf == DXGI_FORMAT_R32_FLOAT) { float one = 1.0f; memcpy(q, &one, 4); } else for (UINT ch = 0; ch < bpp; ++ch) q[ch] = (ch < cmaskCh.size() && cmaskCh[ch] == L'1') ? 255 : 0; }
            cmT = MakeTex(c.dev, iw, ih, cf, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
            Upload(c, cmT, iw, ih, bpp, cm.data(), SRV);
            P("  control mask: %ls left half = 1, right half = 0", cmask.c_str());
        }
        c.ExecAndWait();

        // Standard DLSS keys a game's own DLSS-SR would have left in the shared block.
        auto SetStdKeys = [&](unsigned reset) {
            if (!stdkeys) return;
            BSetUI(caps, "Width", z.w); BSetUI(caps, "Height", z.h); BSetUI(caps, "OutWidth", z.w); BSetUI(caps, "OutHeight", z.h);
            BSetUI(caps, "DLSS.Render.Subrect.Dimensions.Width", z.w); BSetUI(caps, "DLSS.Render.Subrect.Dimensions.Height", z.h);
            BSetUI(caps, "DLSS.Feature.Create.Flags", 0u); BSetUI(caps, "PerfQualityValue", 5u /*DLAA*/); BSetUI(caps, "DLSS.Enable.Output.Subrects", 0u);
            BSetUI(caps, "Reset", reset); BSetUI(caps, "CreationNodeMask", 1u); BSetUI(caps, "VisibilityNodeMask", 1u);
            BSetF(caps, "Jitter.Offset.X", 0.0f); BSetF(caps, "Jitter.Offset.Y", 0.0f); BSetF(caps, "MV.Scale.X", 1.0f); BSetF(caps, "MV.Scale.Y", 1.0f);
            BSetF(caps, "DLSS.Pre.Exposure", 1.0f); BSetF(caps, "DLSS.Exposure.Scale", 1.0f); BSetF(caps, "Sharpness", 0.0f);
            BSetULL(caps, "Color", (unsigned long long)(uintptr_t)color); BSetULL(caps, "Depth", (unsigned long long)(uintptr_t)depthT);
            BSetULL(caps, "MotionVectors", (unsigned long long)(uintptr_t)mvT); BSetULL(caps, "Output", (unsigned long long)(uintptr_t)out);
        };

        LspnrCreateParams cp{}; cp.width = iw; cp.height = ih; cp.preset = preset; cp.scalingRatio = scaling;
        cp.tuning = LspnrTuning{ style, autoMask, uiCorr, intensity, ls > -98.0f ? ls : strength, lt > -98.0f ? lt : strength, skin > -98.0f ? skin : strength };
        SetStdKeys(1);
        NVSDK_NGX_Handle* hSR = nullptr;
        if (primesr) {
            NVSDK_NGX_Result sr = NVSDK_NGX_D3D12_CreateFeature(c.list, NVSDK_NGX_Feature_SuperSampling, caps, &hSR);
            c.ExecAndWait();
            P("  prime: core CreateFeature(SuperSampling/DLAA) -> %s handle=%p", R(sr), hSR);
            if (trace) DumpTrace("core-SR-create");
        }
        void* feature = f.create(c.list, caps, &cp);
        P("  create(18) -> %s handle=%p", R(f.last(1)), feature);
        if (trace) DumpTrace("create");
        if (!feature) { c.ExecAndWait(); continue; }
        c.ExecAndWait();   // creation records GPU work

        LspnrEvalParams ep{}; ep.color = color; ep.depth = depthT; ep.mvec = mvT; ep.output = out;
        ep.width = iw; ep.height = ih; ep.guideWidth = iw; ep.guideHeight = ih; ep.depthInverted = 0; ep.mvScaleX = 1.0f; ep.mvScaleY = 1.0f;
        ep.scalingRatio = scaling; ep.tuning = cp.tuning; ep.controlMask = cmT;
        // first (reset) + warmup
        ep.reset = 1; SetStdKeys(1); int er = f.evaluate(c.list, feature, caps, &ep); c.ExecAndWait();
        P("  first evaluate -> %s", R(er));
        if (trace) { DumpTrace("evaluate#1"); }
        if (er != 1) { f.release(feature); continue; }
        ep.reset = resetAll ? 1u : 0u;
        for (int i = 0; i < 3; ++i) { SetStdKeys(0); f.evaluate(c.list, feature, caps, &ep); c.ExecAndWait(); }
        // multi-pass: feed the model its own answer as the next colour input (passes-1 times); the timed loop then runs the final pass
        for (int p = 1; p < passes; ++p) {
            Barrier(c.list, out, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
            Barrier(c.list, color, SRV, D3D12_RESOURCE_STATE_COPY_DEST);
            c.list->CopyResource(color, out);
            Barrier(c.list, color, D3D12_RESOURCE_STATE_COPY_DEST, SRV);
            Barrier(c.list, out, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            c.ExecAndWait();
            ep.reset = 1; SetStdKeys(1); int pr = f.evaluate(c.list, feature, caps, &ep); c.ExecAndWait();
            ep.reset = resetAll ? 1u : 0u;
            for (int i = 0; i < 2; ++i) { SetStdKeys(0); f.evaluate(c.list, feature, caps, &ep); c.ExecAndWait(); }
            P("  pass %d input <- previous output (evaluate -> %s)", p + 1, R(pr));
        }
        if (trace) { DumpTrace("evaluate#2-4"); RemoveTrace(caps); }
        if (evalInt > -98.0f) { ep.tuning.intensity = evalInt; P("  evaluate-time override: DLSSNR.Intensity = %g", evalInt); }
        if (evalStyle >= 0)   { ep.tuning.style = (unsigned)evalStyle; P("  evaluate-time override: DLSSNR.Style = %d", evalStyle); }
        if (cmaskOff)         { ep.controlMask = nullptr; P("  evaluate-time override: DLSSNR.ControlMask = null"); }

        // timed
        D3D12_QUERY_HEAP_DESC qd{}; qd.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP; qd.Count = 2;
        ID3D12QueryHeap* qh = nullptr; c.dev->CreateQueryHeap(&qd, IID_PPV_ARGS(&qh));
        ID3D12Resource* qrb = MakeBuf(c.dev, 16, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
        UINT64 freq = 1; c.queue->GetTimestampFrequency(&freq);
        double sum = 0, mn = 1e9, mx = 0; int fails = 0;
        LARGE_INTEGER pf, t0, t1; QueryPerformanceFrequency(&pf); QueryPerformanceCounter(&t0);
        for (int i = 0; i < iters; ++i) {
            SetStdKeys(0);
            c.list->EndQuery(qh, D3D12_QUERY_TYPE_TIMESTAMP, 0);
            if (f.evaluate(c.list, feature, caps, &ep) != 1) ++fails;
            c.list->EndQuery(qh, D3D12_QUERY_TYPE_TIMESTAMP, 1);
            c.list->ResolveQueryData(qh, D3D12_QUERY_TYPE_TIMESTAMP, 0, 2, qrb, 0);
            c.ExecAndWait();
            UINT64* ts = nullptr; qrb->Map(0, nullptr, (void**)&ts); double ms = (double)(ts[1] - ts[0]) / freq * 1000.0; qrb->Unmap(0, nullptr);
            sum += ms; if (ms < mn) mn = ms; if (ms > mx) mx = ms;
        }
        QueryPerformanceCounter(&t1);
        double wall = (double)(t1.QuadPart - t0.QuadPart) / pf.QuadPart * 1000.0 / iters;
        P("  >>> %ls  NR GPU: avg %.2f ms  min %.2f  max %.2f   (wall %.2f ms/iter, %d/%d failed)", z.name, sum / iters, mn, mx, wall, fails, iters);
        qh->Release(); qrb->Release();

        // output
        std::vector<uint8_t> res = Readback(c, out, z.w, z.h, colorBpp, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        if (half) {   // back to bytes for compare/PNG
            std::vector<uint8_t> b((size_t)z.w * z.h * 4); const uint16_t* q = (const uint16_t*)res.data();
            for (size_t i = 0; i < b.size(); ++i) { float v = h2f(q[i]); v = v < 0 ? 0 : v > 1 ? 1 : v; b[i] = (uint8_t)(v * 255.0f + 0.5f); }
            res.swap(b);
        }
        uint64_t diff = 0, nz = 0; for (size_t i = 0; i < res.size(); i += 4) { for (int k = 0; k < 3; ++k) { diff += (uint64_t)abs((int)res[i + k] - (int)rgba[i + k]); nz += res[i + k] != 0; } }
        double meanDiff = (double)diff / (res.size() / 4 * 3);
        P("  output: nonzero=%llu/%zu  mean |out-in| = %.2f / 255  %s", (unsigned long long)nz, res.size() / 4 * 3, meanDiff, meanDiff > 0.5 ? "(model changed the image)" : "(UNCHANGED?)");
        std::wstring pin = std::wstring(exeDir) + L"\\in_" + z.name + L".png", pout = std::wstring(exeDir) + L"\\out_" + z.name + L".png";
        SavePng(pin.c_str(), iw, ih, rgbaIn.data()); SavePng(pout.c_str(), z.w, z.h, res.data());
        P("  wrote %ls / %ls", pin.c_str(), pout.c_str());

        f.release(feature);
        if (hSR) NVSDK_NGX_D3D12_ReleaseFeature(hSR);
        color->Release(); depthT->Release(); mvT->Release(); out->Release();
    }

    NVSDK_NGX_D3D12_Shutdown1(c.dev);
    P(""); P("=== done ===");
    if (g_log) fclose(g_log);
    return 0;
}
