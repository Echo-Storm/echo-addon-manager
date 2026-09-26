#include "addon/lsrec.h"
#include <windows.h>
#include <cstring>

namespace nr::lsrec {

namespace {

// QOI's operations (the first byte of a chunk)
constexpr uint8_t kOpIndex = 0x00, kOpDiff = 0x40, kOpLuma = 0x80, kOpRun = 0xC0, kOpRgb = 0xFE, kOpRgba = 0xFF, kMask = 0xC0;

struct Px { uint8_t r, g, b, a; };
inline bool operator==(Px x, Px y) { return x.r == y.r && x.g == y.g && x.b == y.b && x.a == y.a; }
inline int Hash(Px p) { return (p.r * 3 + p.g * 5 + p.b * 7 + p.a * 11) % 64; }

} // namespace

void Compress(const uint8_t* data, uint32_t unitsPerRow, uint32_t rows, uint32_t pitch, std::vector<uint8_t>& out) {
    Px index[64] = {};
    Px prev{ 0, 0, 0, 255 };
    int run = 0;
    const size_t start = out.size();
    out.resize(start + static_cast<size_t>(unitsPerRow) * rows * 5 + 16);   // the worst case: every unit as RGBA
    uint8_t* o = out.data() + start;
    for (uint32_t y = 0; y < rows; ++y) {
        const uint8_t* row = data + static_cast<size_t>(y) * pitch;
        for (uint32_t x = 0; x < unitsPerRow; ++x) {
            Px px; memcpy(&px, row + x * 4, 4);
            if (px == prev) {
                if (++run == 62) { *o++ = static_cast<uint8_t>(kOpRun | (run - 1)); run = 0; }
                continue;
            }
            if (run) { *o++ = static_cast<uint8_t>(kOpRun | (run - 1)); run = 0; }
            const int h = Hash(px);
            if (index[h] == px) *o++ = static_cast<uint8_t>(kOpIndex | h);
            else {
                index[h] = px;
                if (px.a == prev.a) {
                    const int8_t vr = static_cast<int8_t>(px.r - prev.r), vg = static_cast<int8_t>(px.g - prev.g), vb = static_cast<int8_t>(px.b - prev.b);
                    const int8_t vgr = static_cast<int8_t>(vr - vg), vgb = static_cast<int8_t>(vb - vg);
                    if (vr > -3 && vr < 2 && vg > -3 && vg < 2 && vb > -3 && vb < 2)
                        *o++ = static_cast<uint8_t>(kOpDiff | (vr + 2) << 4 | (vg + 2) << 2 | (vb + 2));
                    else if (vgr > -9 && vgr < 8 && vg > -33 && vg < 32 && vgb > -9 && vgb < 8) {
                        *o++ = static_cast<uint8_t>(kOpLuma | (vg + 32));
                        *o++ = static_cast<uint8_t>((vgr + 8) << 4 | (vgb + 8));
                    } else { *o++ = kOpRgb; *o++ = px.r; *o++ = px.g; *o++ = px.b; }
                } else { *o++ = kOpRgba; *o++ = px.r; *o++ = px.g; *o++ = px.b; *o++ = px.a; }
            }
            prev = px;
        }
    }
    if (run) *o++ = static_cast<uint8_t>(kOpRun | (run - 1));
    out.resize(static_cast<size_t>(o - out.data()));
}

bool Decompress(const uint8_t* data, size_t bytes, uint8_t* out, size_t units) {
    Px index[64] = {};
    Px px{ 0, 0, 0, 255 };
    int run = 0;
    size_t p = 0;
    for (size_t i = 0; i < units; ++i) {
        if (run > 0) --run;
        else {
            if (p >= bytes) return false;
            const uint8_t b1 = data[p++];
            if (b1 == kOpRgb) { if (p + 3 > bytes) return false; px.r = data[p]; px.g = data[p + 1]; px.b = data[p + 2]; p += 3; }
            else if (b1 == kOpRgba) { if (p + 4 > bytes) return false; px.r = data[p]; px.g = data[p + 1]; px.b = data[p + 2]; px.a = data[p + 3]; p += 4; }
            else if ((b1 & kMask) == kOpIndex) px = index[b1];
            else if ((b1 & kMask) == kOpDiff) {
                px.r = static_cast<uint8_t>(px.r + ((b1 >> 4) & 3) - 2); px.g = static_cast<uint8_t>(px.g + ((b1 >> 2) & 3) - 2); px.b = static_cast<uint8_t>(px.b + (b1 & 3) - 2);
            } else if ((b1 & kMask) == kOpLuma) {
                if (p >= bytes) return false;
                const uint8_t b2 = data[p++];
                const int vg = (b1 & 0x3F) - 32;
                px.r = static_cast<uint8_t>(px.r + vg - 8 + ((b2 >> 4) & 0x0F)); px.g = static_cast<uint8_t>(px.g + vg); px.b = static_cast<uint8_t>(px.b + vg - 8 + (b2 & 0x0F));
            } else run = b1 & 0x3F;
            index[Hash(px)] = px;
        }
        memcpy(out + i * 4, &px, 4);
    }
    return true;
}

uint32_t BytesPerPixel(uint32_t f) {
    switch (f) {
    case 28: case 87: case 24: return 4;   // R8G8B8A8_UNORM, B8G8R8A8_UNORM, R10G10B10A2_UNORM
    case 10: return 8;                     // R16G16B16A16_FLOAT
    default: return 0;
    }
}

bool Write(const std::wstring& path, const FileHeader& header, const std::vector<const Frame*>& frames, std::string* error) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"wb") || !f) { if (error) *error = "the file could not be made"; return false; }
    FileHeader h = header; h.frameCount = static_cast<uint32_t>(frames.size());
    bool ok = fwrite(&h, sizeof h, 1, f) == 1;
    for (const Frame* fr : frames) {
        if (!ok) break;
        FrameHeader fh = fr->header; fh.bytes = static_cast<uint32_t>(fr->data.size());
        ok = fwrite(&fh, sizeof fh, 1, f) == 1 && (fr->data.empty() || fwrite(fr->data.data(), fr->data.size(), 1, f) == 1);
    }
    if (fclose(f) != 0) ok = false;
    if (!ok && error) *error = "writing failed (is the disk full?)";
    return ok;
}

bool Reader::Open(const std::wstring& path, std::string* error) {
    Close();
    auto fail = [&](const char* why) { if (error) *error = why; Close(); return false; };
    if (_wfopen_s(&m_file, path.c_str(), L"rb") || !m_file) { m_file = nullptr; return fail("the file could not be opened"); }
    if (fread(&m_header, sizeof m_header, 1, m_file) != 1 || memcmp(m_header.magic, "LSREC01", 8) != 0) return fail("not a recording (.lsrec)");
    if (m_header.version != 1 || m_header.headerBytes != sizeof(FileHeader)) return fail("a recording of a newer version");
    if (!m_header.width || !m_header.height || BytesPerPixel(m_header.format) != m_header.bytesPerPixel) return fail("the header is damaged");
    const uint32_t rawBytes = m_header.width * m_header.height * m_header.bytesPerPixel;
    int64_t offset = sizeof m_header;
    for (uint32_t i = 0; i < m_header.frameCount; ++i) {
        Entry e;
        if (_fseeki64(m_file, offset, SEEK_SET) != 0 || fread(&e.header, sizeof e.header, 1, m_file) != 1) break;   // a cut-off file: the frames before it
        if (e.header.rawBytes != rawBytes || (e.header.codec == kRaw && e.header.bytes != rawBytes)) return fail("a frame header is damaged");
        e.offset = offset + sizeof e.header;
        offset = e.offset + e.header.bytes;
        m_frames.push_back(e);
    }
    return true;
}

void Reader::Close() {
    if (m_file) { fclose(m_file); m_file = nullptr; }
    m_frames.clear();
}

bool Reader::Read(size_t i, std::vector<uint8_t>& pixels) {
    if (!m_file || i >= m_frames.size()) return false;
    const Entry& e = m_frames[i];
    pixels.resize(e.header.rawBytes);
    if (_fseeki64(m_file, e.offset, SEEK_SET) != 0) return false;
    if (e.header.codec == kRaw) return fread(pixels.data(), e.header.rawBytes, 1, m_file) == 1;
    m_buffer.resize(e.header.bytes);
    if (e.header.bytes && fread(m_buffer.data(), e.header.bytes, 1, m_file) != 1) return false;
    return e.header.codec == kQoi && Decompress(m_buffer.data(), m_buffer.size(), pixels.data(), e.header.rawBytes / 4);
}

} // namespace nr::lsrec
