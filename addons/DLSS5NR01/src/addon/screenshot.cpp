#include "addon/screenshot.h"
#include "addon/state.h"
#include "addon/log.h"
#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wincodec.h>
#include <atomic>
#include <cstring>
#include <thread>
#include <vector>

namespace nr::screenshot {

namespace {

std::atomic<bool> g_requested{ false }, g_writing{ false }, g_choosing{ false };
std::mutex g_resultMutex;
std::string g_result; bool g_resultOk = false;

// The copy in progress (render thread only, under g_frameMutex).
struct Pending { ID3D11Texture2D* staging = nullptr; DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN; unsigned w = 0, h = 0; int presentsWaited = 0; std::wstring path; };
Pending g_pending;
constexpr int kWaitPresents = 30;   // after this many presents the read-back waits for the GPU (it has long finished by then)

void SetResult(const std::string& text, bool ok) {
    std::lock_guard<std::mutex> lock(g_resultMutex);
    g_result = text; g_resultOk = ok;
}

std::wstring Wide(const std::string& s) {
    std::wstring w(s.size(), L'\0');
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), w.data(), static_cast<int>(w.size()));
    w.resize(n > 0 ? n : 0);
    return w;
}
std::string Utf8(const std::wstring& w) {
    std::string s(w.size() * 3, '\0');
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), s.data(), static_cast<int>(s.size()), nullptr, nullptr);
    s.resize(n > 0 ? n : 0);
    return s;
}

// "WowB_2026-09-23_14-05-33.png" in the folder, made if it does not exist.
std::wstring NewPath(const std::string& game) {
    const std::wstring folder = Folder();
    SHCreateDirectoryExW(nullptr, folder.c_str(), nullptr);
    std::wstring name = Wide(game.empty() ? std::string("Lossless Scaling") : game);
    if (const size_t dot = name.rfind(L'.'); dot != std::wstring::npos && dot > 0) name.resize(dot);
    SYSTEMTIME t; GetLocalTime(&t);
    wchar_t stamp[40];
    swprintf(stamp, 40, L"_%04u-%02u-%02u_%02u-%02u-%02u", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    std::wstring path = folder + L"\\" + name + stamp;
    std::wstring candidate = path + L".png";
    for (int n = 2; GetFileAttributesW(candidate.c_str()) != INVALID_FILE_ATTRIBUTES && n < 100; ++n) candidate = path + L"-" + std::to_wstring(n) + L".png";
    return candidate;
}

bool WritePng(const std::wstring& path, const std::vector<unsigned char>& bgra, unsigned w, unsigned h, std::string& error) {
    const HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IWICImagingFactory* factory = nullptr; IWICStream* stream = nullptr; IWICBitmapEncoder* encoder = nullptr; IWICBitmapFrameEncode* frame = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (SUCCEEDED(hr)) hr = factory->CreateStream(&stream);
    if (SUCCEEDED(hr)) hr = stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE);
    if (SUCCEEDED(hr)) hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
    if (SUCCEEDED(hr)) hr = encoder->Initialize(stream, WICBitmapEncoderNoCache);
    if (SUCCEEDED(hr)) hr = encoder->CreateNewFrame(&frame, nullptr);
    if (SUCCEEDED(hr)) hr = frame->Initialize(nullptr);
    if (SUCCEEDED(hr)) hr = frame->SetSize(w, h);
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    if (SUCCEEDED(hr)) hr = frame->SetPixelFormat(&format);
    if (SUCCEEDED(hr) && format != GUID_WICPixelFormat32bppBGRA) hr = E_UNEXPECTED;
    if (SUCCEEDED(hr)) hr = frame->WritePixels(h, w * 4, static_cast<UINT>(bgra.size()), const_cast<BYTE*>(bgra.data()));
    if (SUCCEEDED(hr)) hr = frame->Commit();
    if (SUCCEEDED(hr)) hr = encoder->Commit();
    for (IUnknown* p : { static_cast<IUnknown*>(frame), static_cast<IUnknown*>(encoder), static_cast<IUnknown*>(stream), static_cast<IUnknown*>(factory) }) if (p) p->Release();
    if (SUCCEEDED(init)) CoUninitialize();
    if (FAILED(hr)) { char text[64]; snprintf(text, sizeof text, "the PNG could not be written (0x%08lx)", static_cast<unsigned long>(hr)); error = text; DeleteFileW(path.c_str()); }
    return SUCCEEDED(hr);
}

// The copy has reached the CPU: convert it and write it on a thread of its own.
void Finish(ID3D11DeviceContext* ctx, bool wait) {
    D3D11_MAPPED_SUBRESOURCE mapped{};
    const HRESULT hr = ctx->Map(g_pending.staging, 0, D3D11_MAP_READ, wait ? 0 : D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
    if (hr == DXGI_ERROR_WAS_STILL_DRAWING) return;
    Pending done = g_pending;
    g_pending = {};
    if (FAILED(hr)) { done.staging->Release(); SetResult("The picture could not be read back from the graphics card.", false); g_writing = false; return; }
    std::vector<unsigned char> bgra(static_cast<size_t>(done.w) * done.h * 4);
    bool converted = true;
    for (unsigned y = 0; y < done.h && converted; ++y)
        converted = ToBgra8(done.format, static_cast<const unsigned char*>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch, done.w, bgra.data() + static_cast<size_t>(y) * done.w * 4);
    ctx->Unmap(done.staging, 0);
    done.staging->Release();
    if (!converted) { SetResult("Frames in this format cannot be saved yet.", false); g_writing = false; return; }
    std::thread([pixels = std::move(bgra), done] {
        std::string error;
        if (WritePng(done.path, pixels, done.w, done.h, error)) { SetResult("Saved " + Utf8(done.path), true); Log("screenshot: %s", Utf8(done.path).c_str()); }
        else { SetResult("Not saved: " + error, false); Log("screenshot failed: %s", error.c_str()); }
        g_writing = false;
    }).detach();
}

} // namespace

void Request() { g_requested = true; }

void OnPresent(ID3D11DeviceContext* ctx, IDXGISwapChain* chain, const std::string& game) {
    if (g_pending.staging) Finish(ctx, ++g_pending.presentsWaited >= kWaitPresents);
    if (!g_requested || g_pending.staging || g_writing) return;
    g_requested = false;
    ID3D11Texture2D* buffer = nullptr;
    if (FAILED(chain->GetBuffer(0, IID_PPV_ARGS(&buffer)))) { SetResult("Lossless Scaling's picture could not be reached.", false); return; }
    D3D11_TEXTURE2D_DESC desc; buffer->GetDesc(&desc);
    D3D11_TEXTURE2D_DESC staging = desc;
    staging.MipLevels = 1; staging.ArraySize = 1; staging.SampleDesc = { 1, 0 }; staging.Usage = D3D11_USAGE_STAGING;
    staging.BindFlags = 0; staging.CPUAccessFlags = D3D11_CPU_ACCESS_READ; staging.MiscFlags = 0;
    ID3D11Device* dev = nullptr; buffer->GetDevice(&dev);
    ID3D11Texture2D* copy = nullptr;
    const bool made = desc.SampleDesc.Count == 1 && dev && SUCCEEDED(dev->CreateTexture2D(&staging, nullptr, &copy));
    if (dev) dev->Release();
    if (!made) { buffer->Release(); SetResult("This kind of swap chain buffer cannot be copied.", false); return; }
    ctx->CopyResource(copy, buffer);
    buffer->Release();
    g_writing = true;
    g_pending = { copy, desc.Format, desc.Width, desc.Height, 0, NewPath(game) };
}

void Forget() {
    if (g_pending.staging) { g_pending.staging->Release(); g_pending = {}; g_writing = false; }
}

std::wstring Folder() {
    std::string chosen;
    { std::lock_guard<std::mutex> lock(g_settingsMutex); chosen = g_config.screenshotFolder; }
    if (!chosen.empty()) return Wide(chosen);
    PWSTR pictures = nullptr;
    std::wstring folder = L"C:\\Screenshots";
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Pictures, 0, nullptr, &pictures))) folder = std::wstring(pictures) + L"\\Lossless Scaling";
    CoTaskMemFree(pictures);
    return folder;
}

void ChooseFolder() {
    if (g_choosing.exchange(true)) return;
    std::thread([] {
        if (SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE))) {
            IFileOpenDialog* dialog = nullptr;
            if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
                DWORD options = 0; dialog->GetOptions(&options);
                dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
                dialog->SetTitle(L"Where screenshots go");
                IShellItem* item = nullptr;
                if (SUCCEEDED(dialog->Show(FindWindowW(L"EchoAddonManagerClass", nullptr))) && SUCCEEDED(dialog->GetResult(&item))) {
                    PWSTR path = nullptr;
                    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                        Config c; std::vector<Look> looks;
                        { std::lock_guard<std::mutex> lock(g_settingsMutex); g_config.screenshotFolder = Utf8(path); c = g_config; looks = g_looks; }
                        SaveSettings(g_host, kAddonId, c, looks);
                        Log("screenshots go to %s", Utf8(path).c_str());
                        CoTaskMemFree(path);
                    }
                    item->Release();
                }
                dialog->Release();
            }
            CoUninitialize();
        }
        g_choosing = false;
    }).detach();
}

bool Choosing() { return g_choosing; }
bool Busy() { return g_requested || g_writing; }

std::string LastResult(bool& ok) {
    std::lock_guard<std::mutex> lock(g_resultMutex);
    ok = g_resultOk;
    return g_result;
}

} // namespace nr::screenshot
