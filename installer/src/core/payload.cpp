#include "payload.h"
#include "fileinfo.h"
#include <windows.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>

namespace fs = std::filesystem;

namespace setup {

namespace {

constexpr char kMagic[8] = {'E', 'A', 'M', 'P', 'A', 'Y', 'L', 'D'};
constexpr uint32_t kFormat = 1;
constexpr uint32_t kMaxFiles = 2000;
constexpr uint64_t kMaxFileBytes = 256ull * 1024 * 1024;
constexpr uint64_t kMaxTotalBytes = 512ull * 1024 * 1024;
constexpr size_t kMaxPathBytes = 240;

void Put(std::vector<uint8_t>& out, const void* p, size_t n) {
    const uint8_t* b = static_cast<const uint8_t*>(p);
    out.insert(out.end(), b, b + n);
}
template <class T> void PutNum(std::vector<uint8_t>& out, T v) {   // little-endian on every machine this runs on
    Put(out, &v, sizeof(v));
}

struct Reader {
    const uint8_t* p;
    size_t left;
    bool Take(void* dst, size_t n) {
        if (n > left) return false;
        if (dst) std::memcpy(dst, p, n);
        p += n;
        left -= n;
        return true;
    }
};

struct Entry {
    std::string path;
    const uint8_t* data;
    uint64_t size;
};

bool Parse(const uint8_t* data, size_t size, std::vector<Entry>& entries, uint64_t& bytes, std::string& error) {
    Reader r{data, size};
    char magic[8];
    uint32_t format = 0, count = 0;
    if (!data || !r.Take(magic, 8) || std::memcmp(magic, kMagic, 8) != 0) { error = "the installer's file bundle is not valid (wrong header)"; return false; }
    if (!r.Take(&format, 4) || format != kFormat) { error = "the installer's file bundle has an unknown format"; return false; }
    if (!r.Take(&count, 4) || count == 0 || count > kMaxFiles) { error = "the installer's file bundle lists an impossible number of files"; return false; }
    std::set<std::string> seen;
    bytes = 0;
    for (uint32_t i = 0; i < count; ++i) {
        uint16_t len = 0;
        if (!r.Take(&len, 2) || len == 0 || len > kMaxPathBytes || len > r.left) { error = "the installer's file bundle is damaged (a file name)"; return false; }
        std::string path(reinterpret_cast<const char*>(r.p), len);
        r.Take(nullptr, len);
        if (!SafeRelativePath(path)) { error = "the installer's file bundle names a file outside its folder: " + path; return false; }
        std::string key = path;
        std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (!seen.insert(key).second) { error = "the installer's file bundle lists a file twice: " + path; return false; }
        uint64_t n = 0;
        if (!r.Take(&n, 8) || n > kMaxFileBytes || n > r.left) { error = "the installer's file bundle is damaged (a file is cut short): " + path; return false; }
        bytes += n;
        if (bytes > kMaxTotalBytes) { error = "the installer's file bundle is far too large"; return false; }
        entries.push_back({path, r.p, n});
        r.Take(nullptr, static_cast<size_t>(n));
    }
    if (r.left != 0) { error = "the installer's file bundle has unexpected bytes at the end"; return false; }
    return true;
}

} // namespace

bool SafeRelativePath(const std::string& path) {
    if (path.empty() || path.size() > kMaxPathBytes) return false;
    if (path.front() == '/' || path.back() == '/') return false;
    size_t start = 0;
    while (start <= path.size()) {
        size_t end = path.find('/', start);
        if (end == std::string::npos) end = path.size();
        const std::string part = path.substr(start, end - start);
        if (part.empty() || part == "." || part == "..") return false;
        if (part.back() == '.' || part.back() == ' ') return false;   // Windows would trim these, so the name would not be what it says
        start = end + 1;
    }
    for (unsigned char c : path) {
        if (c < 0x20 || c == ':' || c == '\\' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') return false;
    }
    return true;
}

bool PackFolder(const std::wstring& dir, std::vector<uint8_t>& blob, std::string& error) {
    blob.clear();
    std::error_code ec;
    const fs::path root = fs::path(dir);
    if (!fs::is_directory(root, ec)) { error = "not a folder: " + Narrow(dir); return false; }
    std::vector<std::pair<std::string, fs::path>> files;
    for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        std::string rel = Narrow(fs::relative(it->path(), root, ec).generic_wstring());
        if (!SafeRelativePath(rel)) { error = "a file name cannot go into the bundle: " + rel; return false; }
        files.emplace_back(rel, it->path());
    }
    if (ec) { error = "cannot read the folder: " + ec.message(); return false; }
    if (files.empty() || files.size() > kMaxFiles) { error = "the folder has no files (or far too many)"; return false; }
    std::sort(files.begin(), files.end());
    Put(blob, kMagic, 8);
    PutNum<uint32_t>(blob, kFormat);
    PutNum<uint32_t>(blob, static_cast<uint32_t>(files.size()));
    for (const auto& f : files) {
        std::ifstream in(f.second, std::ios::binary);
        std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (!in && !in.eof()) { error = "cannot read " + f.first; return false; }
        PutNum<uint16_t>(blob, static_cast<uint16_t>(f.first.size()));
        Put(blob, f.first.data(), f.first.size());
        PutNum<uint64_t>(blob, static_cast<uint64_t>(bytes.size()));
        Put(blob, bytes.data(), bytes.size());
    }
    return true;
}

bool ValidatePayload(const uint8_t* data, size_t size, size_t& count, uint64_t& bytes, std::string& error) {
    std::vector<Entry> entries;
    if (!Parse(data, size, entries, bytes, error)) { count = 0; return false; }
    count = entries.size();
    return true;
}

bool UnpackTo(const uint8_t* data, size_t size, const std::wstring& dir, std::string& error) {
    std::vector<Entry> entries;
    uint64_t bytes = 0;
    if (!Parse(data, size, entries, bytes, error)) return false;
    std::error_code ec;
    fs::create_directories(dir, ec);
    for (const auto& e : entries) {
        const fs::path target = fs::path(dir) / fs::path(Widen(e.path));
        fs::create_directories(target.parent_path(), ec);
        std::ofstream out(target, std::ios::binary | std::ios::trunc);
        if (out) out.write(reinterpret_cast<const char*>(e.data), static_cast<std::streamsize>(e.size));
        out.close();
        if (!out) { error = "cannot write " + e.path + " (is the disk full or the temporary folder locked?)"; return false; }
    }
    return true;
}

bool EmbeddedPayload(const uint8_t*& data, size_t& size) {
    HRSRC res = FindResourceW(nullptr, MAKEINTRESOURCEW(kPayloadResourceId), RT_RCDATA);
    if (!res) return false;
    HGLOBAL h = LoadResource(nullptr, res);
    if (!h) return false;
    data = static_cast<const uint8_t*>(LockResource(h));
    size = SizeofResource(nullptr, res);
    return data && size > 0;
}

} // namespace setup
