#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_NO_STDIO   // the file is read here, by its wide path: stb's own fopen would take the path in the ANSI code page
#include "../../third_party/stb_image.h"
#include "icon_loader.h"
#include "../log/logger.h"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

namespace eam {

namespace {
ID3D11Device* g_device = nullptr;
constexpr int kMaxSide = 1024;
constexpr size_t kMaxFileBytes = 16u << 20;

std::string Utf8(const std::wstring& path) { return std::filesystem::path(path).u8string(); }   // for the log

std::vector<unsigned char> ReadFile(const std::wstring& path) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || size == 0 || size > kMaxFileBytes) return {};
    std::ifstream file(std::filesystem::path(path), std::ios::binary);
    return std::vector<unsigned char>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}
} // namespace

void SetIconDevice(ID3D11Device* device) { g_device = device; }

bool DecodeImageFile(const std::wstring& path, std::vector<unsigned char>& rgba, int& w, int& h) {
    const std::vector<unsigned char> file = ReadFile(path);
    int channels = 0;
    w = h = 0;
    if (file.empty() || !stbi_info_from_memory(file.data(), (int)file.size(), &w, &h, &channels)) {
        LOG_WARN("Icons", "Not an image that can be read (PNG, JPEG or BMP up to 16 MB): %s", Utf8(path).c_str());
        return false;
    }
    if (w <= 0 || h <= 0 || w > kMaxSide || h > kMaxSide) {
        LOG_WARN("Icons", "Too large for an icon (%dx%d; at most %d a side): %s", w, h, kMaxSide, Utf8(path).c_str());
        return false;
    }
    unsigned char* const pixels = stbi_load_from_memory(file.data(), (int)file.size(), &w, &h, &channels, 4);
    if (!pixels) { LOG_WARN("Icons", "Could not decode %s: %s", Utf8(path).c_str(), stbi_failure_reason()); return false; }
    rgba.assign(pixels, pixels + (size_t)w * h * 4);
    stbi_image_free(pixels);
    return true;
}

ID3D11ShaderResourceView* LoadIconTexture(const std::wstring& path) {
    if (!g_device) return nullptr;
    std::vector<unsigned char> rgba;
    int w = 0, h = 0;
    if (!DecodeImageFile(path, rgba, w, h)) return nullptr;

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = (UINT)w; desc.Height = (UINT)h; desc.MipLevels = 1; desc.ArraySize = 1; desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1; desc.Usage = D3D11_USAGE_IMMUTABLE; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    const D3D11_SUBRESOURCE_DATA pixels{ rgba.data(), (UINT)w * 4, 0 };
    ID3D11Texture2D* texture = nullptr;
    HRESULT hr = g_device->CreateTexture2D(&desc, &pixels, &texture);
    ID3D11ShaderResourceView* view = nullptr;
    if (SUCCEEDED(hr)) { hr = g_device->CreateShaderResourceView(texture, nullptr, &view); texture->Release(); }
    if (FAILED(hr)) { LOG_WARN("Icons", "Could not make a texture for %s: 0x%08x", Utf8(path).c_str(), (unsigned)hr); return nullptr; }
    LOG_DEBUG("Icons", "Loaded %dx%d: %s", w, h, Utf8(path).c_str());
    return view;
}

} // namespace eam
