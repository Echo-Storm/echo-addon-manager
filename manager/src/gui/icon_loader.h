#pragma once
#include <d3d11.h>
#include <string>

namespace lsproxy {

// Set the D3D11 device used for texture creation (call once during GUI init)
void IconLoader_SetDevice(ID3D11Device* device);

// Load an image file (PNG, JPG, BMP, etc.) as a D3D11 shader resource view.
// Returns nullptr on failure. Caller must Release() the returned texture.
ID3D11ShaderResourceView* IconLoader_LoadFromFile(const std::wstring& path);

} // namespace lsproxy
