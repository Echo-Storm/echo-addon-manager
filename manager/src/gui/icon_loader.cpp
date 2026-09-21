#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#include "../../third_party/stb_image.h"
#include "icon_loader.h"
#include "../log/logger.h"
#include <string>

namespace lsproxy {

static ID3D11Device* g_iconDevice = nullptr;

void IconLoader_SetDevice(ID3D11Device* device) {
    g_iconDevice = device;
}

// Convert wstring path to UTF-8 for stb_image
static std::string WToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), nullptr, 0, nullptr, nullptr);
    std::string result(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), &result[0], size, nullptr, nullptr);
    return result;
}

ID3D11ShaderResourceView* IconLoader_LoadFromFile(const std::wstring& path) {
    if (!g_iconDevice) return nullptr;

    // stb_image doesn't support wchar paths directly, use UTF-8
    std::string utf8Path = WToUtf8(path);

    int width, height, channels;
    unsigned char* data = stbi_load(utf8Path.c_str(), &width, &height, &channels, 4); // Force RGBA
    if (!data) {
        LOG_WARN("IconLoader", "Failed to load icon: %s", utf8Path.c_str());
        return nullptr;
    }

    // Create D3D11 texture
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA subResource = {};
    subResource.pSysMem = data;
    subResource.SysMemPitch = width * 4;

    ID3D11Texture2D* pTexture = nullptr;
    HRESULT hr = g_iconDevice->CreateTexture2D(&desc, &subResource, &pTexture);
    stbi_image_free(data);

    if (FAILED(hr) || !pTexture) {
        LOG_WARN("IconLoader", "Failed to create texture for icon: %s", utf8Path.c_str());
        return nullptr;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    ID3D11ShaderResourceView* pSRV = nullptr;
    hr = g_iconDevice->CreateShaderResourceView(pTexture, &srvDesc, &pSRV);
    pTexture->Release();

    if (FAILED(hr)) {
        LOG_WARN("IconLoader", "Failed to create SRV for icon: %s", utf8Path.c_str());
        return nullptr;
    }

    LOG_DEBUG("IconLoader", "Loaded icon %dx%d: %s", width, height, utf8Path.c_str());
    return pSRV;
}

} // namespace lsproxy
