// Addon icons: a PNG, JPEG or BMP file made into a texture the window can draw.
#pragma once
#include <d3d11.h>
#include <string>
#include <vector>

namespace eam {

// The window's device, which the textures are made on (set when the window's device is created).
void SetIconDevice(ID3D11Device* device);

// Reads and decodes the image at `path` into RGBA, 8 bits a channel, rows top to bottom. False (and a line in the log) when the file cannot be
// read or decoded, or is larger than 16 MB or 1024 pixels a side. The path is used as it is (any characters).
bool DecodeImageFile(const std::wstring& path, std::vector<unsigned char>& rgba, int& w, int& h);

// The image at `path` as a texture view (the caller releases it), or null when there is no device, the file cannot be read or decoded, or the
// image is larger than an icon has any reason to be (1024 pixels a side).
ID3D11ShaderResourceView* LoadIconTexture(const std::wstring& path);

} // namespace eam
