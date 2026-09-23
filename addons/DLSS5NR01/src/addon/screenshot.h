// Screenshots of the picture as it is shown: taken from Lossless Scaling's swap chain at a present, after Neural Rendering's result, the scaling
// and frame generation are all in it (ReShade's own screenshots show the game's frame before any of that).
//
// A request (the panel's button or the Ctrl+Shift key) is served at the next present of Lossless Scaling's swap chain: the buffer is copied on
// the GPU, read back a few presents later without waiting for the GPU, and written as a PNG on a thread of its own, so the game never stalls.
#pragma once
#include <d3d11.h>
#include <dxgi.h>
#include <string>

namespace nr::screenshot {

void Request();
// At every present of Lossless Scaling's swap chain, after the compose (on its render thread, under g_frameMutex).
void OnPresent(ID3D11DeviceContext* ctx, IDXGISwapChain* chain, const std::string& game);
void Forget();   // the device is going away: drop a copy in progress

std::wstring Folder();         // the chosen folder, or Pictures\Lossless Scaling
void ChooseFolder();           // a folder dialog, on a thread of its own
bool Choosing();
bool Busy();                   // a picture is being taken or written
std::string LastResult(bool& ok);

// For the test: the conversion of one row of a presented buffer to 8-bit BGRA with full alpha. False for a format it cannot convert.
bool ToBgra8(DXGI_FORMAT format, const void* row, unsigned width, unsigned char* out);

} // namespace nr::screenshot
