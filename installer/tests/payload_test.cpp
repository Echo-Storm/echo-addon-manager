// Offline test of the installer's file bundle (payload.h): a folder goes in, the same files come out, and a damaged or hostile bundle writes nothing.
//   setup_payload_test.exe
#include "fileinfo.h"
#include "payload.h"
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace setup;

static int g_failed = 0;
static void Check(const char* what, bool ok, const std::string& detail = "") {
    printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : "  (", detail.empty() ? "" : (detail + ")").c_str());
    if (!ok) ++g_failed;
}
static void Write(const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary);
    f << text;
}
static std::string Hash(const fs::path& p) { return Sha256File(p.wstring()); }
static size_t CountFiles(const fs::path& dir) {
    size_t n = 0;
    std::error_code ec;
    if (!fs::exists(dir, ec)) return 0;
    for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) if (it->is_regular_file(ec)) ++n;
    return n;
}

// Build a blob by hand, so hostile ones can be made
static void Put(std::vector<uint8_t>& b, const void* p, size_t n) { const uint8_t* x = static_cast<const uint8_t*>(p); b.insert(b.end(), x, x + n); }
static std::vector<uint8_t> Blob(const std::vector<std::pair<std::string, std::string>>& files, uint32_t count = 0xFFFFFFFF, uint32_t format = 1, const char* magic = "EAMPAYLD") {
    std::vector<uint8_t> b;
    Put(b, magic, 8);
    Put(b, &format, 4);
    const uint32_t n = count == 0xFFFFFFFF ? static_cast<uint32_t>(files.size()) : count;
    Put(b, &n, 4);
    for (const auto& f : files) {
        const uint16_t len = static_cast<uint16_t>(f.first.size());
        const uint64_t size = f.second.size();
        Put(b, &len, 2);
        Put(b, f.first.data(), f.first.size());
        Put(b, &size, 8);
        Put(b, f.second.data(), f.second.size());
    }
    return b;
}

int wmain() {
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    const fs::path root = fs::path(tmp) / (L"setup_payload_test_" + std::to_wstring(GetCurrentProcessId()));
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root);

    // 1. A folder like the release: pack, validate, unpack, compare
    const fs::path src = root / L"src";
    Write(src / L"Lossless.dll", std::string("MZ\0real dll bytes", 17) + std::string(70000, 'x'));
    Write(src / L"manager-icon.ico", "icon");
    Write(src / L"addons" / L"DLSS5NR01" / L"DLSS5NR01.dll", std::string(3000, 'a'));
    Write(src / L"addons" / L"DLSS5NR01" / L"addon.json", "{ \"id\": \"DLSS5NR01\" }");
    Write(src / L"addons" / L"empty.bin", "");
    Write(src / L"addons" / L"\u00e9t\u00e9.txt", "accents in a name");
    std::vector<uint8_t> blob, again;
    std::string err;
    Check("packing a folder works", PackFolder(src.wstring(), blob, err), err);
    Check("packing the same folder again gives the same blob (sorted, deterministic)", PackFolder(src.wstring(), again, err) && blob == again);
    size_t count = 0;
    uint64_t bytes = 0;
    Check("the blob validates", ValidatePayload(blob.data(), blob.size(), count, bytes, err), err);
    Check("it counts the six files", count == 6, std::to_string(count));
    const fs::path out = root / L"out";
    Check("unpacking works", UnpackTo(blob.data(), blob.size(), out.wstring(), err), err);
    bool same = true;
    for (fs::recursive_directory_iterator it(src), end; it != end; ++it) {
        if (!it->is_regular_file()) continue;
        const fs::path rel = fs::relative(it->path(), src);
        if (Hash(it->path()) != Hash(out / rel)) { same = false; printf("      differs: %s\n", Narrow(rel.wstring()).c_str()); }
    }
    Check("every file comes out byte for byte the same (including an empty file and an accented name)", same && CountFiles(out) == 6);

    // 2. Names
    Check("a plain relative name is fine", SafeRelativePath("addons/DLSS5NR01/addon.json"));
    Check("a name in the top folder is fine", SafeRelativePath("Lossless.dll"));
    const char* bad[] = {"", "/etc/passwd", "a/../b", "../x", "..", ".", "a//b", "a/", "/a", "C:/x", "C:x", "a\\b", "a/./b", "trailing.", "trailing ", "a/b:stream", "a*b", "a?b", "a|b", "a<b", "con\x01"};
    bool allBad = true;
    for (const char* b : bad) {
        if (SafeRelativePath(b)) { allBad = false; printf("      accepted: %s\n", b); }
    }
    Check("empty, absolute, parent, drive, backslash, stream, wildcard and control-character names are refused", allBad);
    Check("a name of 300 characters is refused", !SafeRelativePath(std::string(300, 'a')));

    // 3. Hostile and damaged blobs write nothing
    struct Case { const char* what; std::vector<uint8_t> blob; };
    std::vector<Case> cases = {
        {"a name that climbs out of the folder", Blob({{"../evil.dll", "x"}})},
        {"an absolute name", Blob({{"/evil.dll", "x"}})},
        {"a drive letter", Blob({{"C:/evil.dll", "x"}})},
        {"a backslash", Blob({{"a\\evil.dll", "x"}})},
        {"the same file twice", Blob({{"a.txt", "1"}, {"a.txt", "2"}})},
        {"the same file twice in a different case", Blob({{"a.txt", "1"}, {"A.TXT", "2"}})},
        {"no files at all", Blob({}, 0)},
        {"more files promised than present", Blob({{"a.txt", "1"}}, 5)},
        {"a wrong header", Blob({{"a.txt", "1"}}, 0xFFFFFFFF, 1, "NOTOURS!")},
        {"an unknown format number", Blob({{"a.txt", "1"}}, 0xFFFFFFFF, 9)},
        {"an absurd number of files", Blob({{"a.txt", "1"}}, 4000000000u)},
    };
    {   // a file whose size field is enormous
        std::vector<uint8_t> b = Blob({{"a.txt", "1"}});
        const uint64_t huge = 0xFFFFFFFFFFFFFFF0ull;
        std::memcpy(&b[8 + 4 + 4 + 2 + 5], &huge, 8);
        cases.push_back({"a size field that would overflow", b});
    }
    {   // trailing bytes
        std::vector<uint8_t> b = Blob({{"a.txt", "1"}});
        b.push_back(0);
        cases.push_back({"bytes left over at the end", b});
    }
    for (const auto& c : cases) {
        const fs::path o = root / (std::wstring(L"bad_") + std::to_wstring(&c - &cases[0]));
        std::string e;
        size_t n = 0;
        uint64_t by = 0;
        const bool v = ValidatePayload(c.blob.data(), c.blob.size(), n, by, e);
        const bool u = UnpackTo(c.blob.data(), c.blob.size(), o.wstring(), e);
        Check((std::string("refused, and nothing written: ") + c.what).c_str(), !v && !u && CountFiles(o) == 0 && !fs::exists(root / L"evil.dll"), e);
    }
    Check("a null blob and an empty blob are refused", !ValidatePayload(nullptr, 0, count, bytes, err) && !ValidatePayload(blob.data(), 0, count, bytes, err));

    // 4. Cut short at every length, and every byte of the header and names flipped: never a crash, and a cut blob is never accepted
    bool cutOk = true;
    for (size_t n = 0; n < blob.size(); n += (n < 400 ? 1 : 997)) {
        std::string e;
        if (ValidatePayload(blob.data(), n, count, bytes, e)) { cutOk = false; printf("      accepted a blob cut to %zu bytes\n", n); }
    }
    Check("a blob cut short at any length is refused", cutOk);
    bool flipOk = true;
    const fs::path fz = root / L"fuzz";
    std::vector<uint8_t> tiny = Blob({{"a/b.txt", "hello"}, {"c.txt", "world"}});
    for (size_t i = 0; i < tiny.size(); ++i) {
        std::vector<uint8_t> m = tiny;
        m[i] ^= 0xFF;
        std::string e;
        if (UnpackTo(m.data(), m.size(), fz.wstring(), e)) {   // accepted: then its files must have stayed inside the folder
            for (fs::recursive_directory_iterator it(fz), end; it != end; ++it) {
                const std::wstring rel = fs::relative(it->path(), fz).wstring();
                if (rel.find(L"..") != std::wstring::npos) flipOk = false;
            }
        }
        fs::remove_all(fz, ec);
    }
    Check("flipping any byte of a tiny blob never crashes and never writes outside the folder", flipOk && !fs::exists(root / L"evil.dll"));

    // 5. This test exe carries no bundle
    const uint8_t* d = nullptr;
    size_t s = 0;
    Check("an exe without a bundle says so", !EmbeddedPayload(d, s));

    fs::remove_all(root, ec);
    printf("\n%s (%d failed)\n", g_failed ? "PAYLOAD TEST FAILED" : "PAYLOAD TEST PASSED", g_failed);
    return g_failed ? 1 : 0;
}
