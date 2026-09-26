// nr_rectest: the recorder's codec and file (addon/lsrec.h), with no GPU. Lossless on every kind of content (noise, gradients, long runs,
// changing alpha, rows with padding, half-float frames), within its worst-case size, and a file written and read back, a cut-off one
// included. Also prints how fast it compresses a game-like 1080p frame and how small it gets.
#include "addon/lsrec.h"
#include <windows.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

using namespace nr::lsrec;

static int g_failures = 0;
static void Check(bool ok, const char* what, const char* detail = "") {
    printf("  %s  %s%s%s\n", ok ? "PASS" : "FAIL", what, *detail ? "  " : "", detail);
    if (!ok) ++g_failures;
}

// Compresses units (with `pitch` bytes a row) and decompresses them: the same bytes, and no more than the worst case.
static bool RoundTrip(const std::vector<uint8_t>& data, uint32_t unitsPerRow, uint32_t rows, uint32_t pitch, size_t* compressed = nullptr) {
    std::vector<uint8_t> packed;
    Compress(data.data(), unitsPerRow, rows, pitch, packed);
    if (compressed) *compressed = packed.size();
    if (packed.size() > static_cast<size_t>(unitsPerRow) * rows * 5) return false;
    std::vector<uint8_t> out(static_cast<size_t>(unitsPerRow) * rows * 4);
    if (!Decompress(packed.data(), packed.size(), out.data(), static_cast<size_t>(unitsPerRow) * rows)) return false;
    for (uint32_t y = 0; y < rows; ++y)
        if (memcmp(out.data() + static_cast<size_t>(y) * unitsPerRow * 4, data.data() + static_cast<size_t>(y) * pitch, unitsPerRow * 4) != 0) return false;
    return true;
}

// A game-like frame: smooth shading, edges, a little noise (what a 3D game's picture compresses like, roughly)
static std::vector<uint8_t> GameLike(uint32_t w, uint32_t h, uint32_t seed) {
    std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4);
    std::mt19937 rng(seed);
    for (uint32_t y = 0; y < h; ++y) for (uint32_t x = 0; x < w; ++x) {
        uint8_t* p = &px[(static_cast<size_t>(y) * w + x) * 4];
        const double sky = y < h / 3 ? 1.0 : 0.0;
        const double shade = 0.5 + 0.5 * std::sin(x * 0.01 + y * 0.004 + seed);
        const int noise = static_cast<int>(rng() % 7) - 3;
        const bool edge = ((x / 97 + y / 61) & 1) != 0;
        p[0] = static_cast<uint8_t>(std::clamp(static_cast<int>(sky ? 200 : 60 + 120 * shade) + noise, 0, 255));
        p[1] = static_cast<uint8_t>(std::clamp(static_cast<int>(sky ? 170 : (edge ? 90 : 140) * shade + 30) + noise, 0, 255));
        p[2] = static_cast<uint8_t>(std::clamp(static_cast<int>(sky ? 120 : 40 + 60 * shade) + noise, 0, 255));
        p[3] = 255;
    }
    return px;
}

int main() {
    printf("== codec (QOI's operations on 4-byte units)\n");
    std::mt19937 rng(7);
    {
        std::vector<uint8_t> noise(257 * 131 * 4); for (auto& b : noise) b = static_cast<uint8_t>(rng());
        Check(RoundTrip(noise, 257, 131, 257 * 4), "random bytes (every unit a literal)");
    }
    {
        std::vector<uint8_t> solid(1000 * 50 * 4, 0); for (size_t i = 3; i < solid.size(); i += 4) solid[i] = 255;
        size_t bytes = 0; const bool ok = RoundTrip(solid, 1000, 50, 4000, &bytes);
        char d[64]; snprintf(d, sizeof d, "%zu bytes for 50000 units", bytes);
        Check(ok && bytes < 1000, "one colour (runs past 62 units)", d);
    }
    {
        std::vector<uint8_t> grad(640 * 360 * 4);
        for (uint32_t y = 0; y < 360; ++y) for (uint32_t x = 0; x < 640; ++x) { uint8_t* p = &grad[(y * 640 + x) * 4]; p[0] = (uint8_t)x; p[1] = (uint8_t)y; p[2] = (uint8_t)(x + y); p[3] = 255; }
        Check(RoundTrip(grad, 640, 360, 640 * 4), "gradients (small differences)");
    }
    {
        std::vector<uint8_t> alpha(300 * 7 * 4); for (size_t i = 0; i < alpha.size(); ++i) alpha[i] = static_cast<uint8_t>(i % 4 == 3 ? rng() % 3 * 100 : rng() % 4);
        Check(RoundTrip(alpha, 300, 7, 1200), "alpha that changes (RGBA literals)");
    }
    {
        const uint32_t w = 333, h = 41, pitch = 1408;   // rows with padding after them, as a mapped texture has
        std::vector<uint8_t> padded(static_cast<size_t>(pitch) * h, 0xCD);
        for (uint32_t y = 0; y < h; ++y) for (uint32_t x = 0; x < w * 4; ++x) padded[y * pitch + x] = static_cast<uint8_t>((x * 7 + y * 3) ^ (rng() & 1));
        Check(RoundTrip(padded, w, h, pitch), "rows with padding between them");
    }
    {
        std::vector<uint8_t> half(200 * 100 * 8);   // a half-float frame: two units a pixel
        for (size_t i = 0; i < half.size(); i += 2) { const uint16_t v = static_cast<uint16_t>(0x3C00 + (rng() % 512)); memcpy(&half[i], &v, 2); }
        Check(RoundTrip(half, 400, 100, 1600), "a half-float frame (two units a pixel)");
    }
    {
        std::vector<uint8_t> packed; std::vector<uint8_t> px(64 * 4, 9);
        Compress(px.data(), 64, 1, 256, packed);
        std::vector<uint8_t> out(64 * 4);
        Check(!Decompress(packed.data(), packed.size() / 2, out.data(), 64) || packed.size() < 4, "cut-off data is refused");
    }

    printf("== speed and size (1920x1080, game-like)\n");
    {
        const std::vector<uint8_t> frame = GameLike(1920, 1080, 3);
        std::vector<uint8_t> packed; packed.reserve(frame.size());
        const auto t0 = std::chrono::steady_clock::now();
        const int reps = 5;
        for (int i = 0; i < reps; ++i) { packed.clear(); Compress(frame.data(), 1920, 1080, 1920 * 4, packed); }
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / reps;
        std::vector<uint8_t> out(frame.size());
        const auto t1 = std::chrono::steady_clock::now();
        const bool ok = Decompress(packed.data(), packed.size(), out.data(), 1920 * 1080) && out == frame;
        const double dms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count();
        char d[160]; snprintf(d, sizeof d, "compress %.1f ms (%.0f MB/s), decompress %.1f ms, %.1f MB -> %.1f MB (%.0f%%)", ms, frame.size() / 1048576.0 / (ms / 1000.0), dms,
                              frame.size() / 1048576.0, packed.size() / 1048576.0, 100.0 * packed.size() / frame.size());
        Check(ok, "lossless on a 1080p frame", d);
    }

    printf("== file\n");
    wchar_t tmp[MAX_PATH]; GetTempPathW(MAX_PATH, tmp);
    const std::wstring path = std::wstring(tmp) + L"nr_rectest.lsrec";
    const uint32_t w = 160, h = 90;
    std::vector<std::vector<uint8_t>> pictures;
    std::vector<Frame> frames(5);
    for (int i = 0; i < 5; ++i) {
        pictures.push_back(GameLike(w, h, 10 + i));
        frames[i].header.index = 100 + i * 2; frames[i].header.qpc = 1000 * i; frames[i].header.rawBytes = w * h * 4;
        if (i == 2) { frames[i].header.codec = kRaw; frames[i].data = pictures[i]; }   // one stored as it is
        else Compress(pictures[i].data(), w, h, w * 4, frames[i].data);
    }
    FileHeader header; header.width = w; header.height = h; header.format = 87; header.bytesPerPixel = 4; header.source = kPresented;
    header.qpcFrequency = 10000000; snprintf(header.game, sizeof header.game, "falloutnv.exe");
    std::vector<const Frame*> list; for (const Frame& f : frames) list.push_back(&f);
    Check(Write(path, header, list), "written");
    {
        Reader r; std::string error;
        bool ok = r.Open(path, &error) && r.Count() == 5 && r.Header().width == w && strcmp(r.Header().game, "falloutnv.exe") == 0;
        std::vector<uint8_t> px;
        for (int i = 0; ok && i < 5; ++i) ok = r.Read(i, px) && px == pictures[i] && r.FrameInfo(i).index == 100u + i * 2;
        Check(ok, "read back: header, indexes and every frame", error.c_str());
    }
    {   // cut off in the middle of the last frame: the ones before it still read
        FILE* f = nullptr; _wfopen_s(&f, path.c_str(), L"rb"); std::vector<uint8_t> all;
        if (f) { fseek(f, 0, SEEK_END); all.resize(ftell(f)); fseek(f, 0, SEEK_SET); fread(all.data(), 1, all.size(), f); fclose(f); }
        const std::wstring cut = std::wstring(tmp) + L"nr_rectest_cut.lsrec";
        _wfopen_s(&f, cut.c_str(), L"wb"); if (f) { fwrite(all.data(), 1, all.size() - frames[4].data.size() / 2, f); fclose(f); }
        Reader r; std::vector<uint8_t> px;
        const bool ok = r.Open(cut) && r.Read(3, px) && px == pictures[3] && !r.Read(4, px);
        Check(ok, "a cut-off file: the frames before the cut read, the cut one does not");
        DeleteFileW(cut.c_str());
    }
    {
        const std::wstring bad = std::wstring(tmp) + L"nr_rectest_bad.lsrec";
        FILE* f = nullptr; _wfopen_s(&f, bad.c_str(), L"wb"); if (f) { fputs("not a recording at all, just some text that is long enough to fill a header .........................................................", f); fclose(f); }
        Reader r; std::string error;
        const bool opened = r.Open(bad, &error);
        Check(!opened, "another file is refused", error.c_str());
        DeleteFileW(bad.c_str());
    }
    DeleteFileW(path.c_str());

    printf(g_failures ? "\nRECORDER TEST FAILED (%d)\n" : "\nRECORDER TEST PASSED\n", g_failures);
    return g_failures ? 1 : 0;
}
