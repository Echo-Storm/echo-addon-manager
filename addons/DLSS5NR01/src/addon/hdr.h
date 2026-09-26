// What a frame holds (SDR, scRGB or HDR10) and the display's SDR white, for the passes that work on an SDR view of it (engine/hdr_hlsl.h).
#pragma once
#include <d3d11.h>
#include <dxgi.h>
#include <cstdint>

namespace nr {

enum class FrameEncoding : uint32_t { Sdr = 0, ScRgb = 1, Hdr10 = 2 };

// Windows' HDR state for the display a swap chain is on (or, without one, any display of the device's graphics card): whether it runs in
// HDR, and the SDR white it uses (Settings > Display > HDR > SDR content brightness), in nits (80, scRGB's 1.0, on a display in SDR). Read at
// most every two seconds.
struct DisplayHdr { bool hdr = false; float whiteNits = 200.0f; };
DisplayHdr QueryDisplayHdr(IDXGISwapChain* chain, ID3D11Device* device);

// A frame of this format: 8-bit is SDR; half-float is scRGB; 10-bit is HDR10 when the display runs in HDR, else 10-bit SDR. setting (the
// addon's "Frame encoding", for a setup the automatic choice gets wrong): 0 decides so; 1 takes the half-float and 10-bit frames as SDR; 2 as
// HDR (scRGB, HDR10). 8-bit frames are SDR whatever it says.
FrameEncoding EncodingOf(DXGI_FORMAT viewFormat, int setting, bool displayHdr);
const char* EncodingName(FrameEncoding e);

} // namespace nr
