#pragma once
// Small facts about files, learned without loading them (loading a DLL runs its code, which the installer must never do to Lossless Scaling's or ours).
#include <cstdint>
#include <string>

namespace setup {

struct VersionResource {
    bool ok = false;               // the file has a version resource
    std::string product;           // "Lossless Scaling" or "Echo Addon Manager"
    std::string company;
    std::string description;
    std::string fileVersion;       // "3.2.2.0", "0.4.1"
};
VersionResource ReadVersionResource(const std::wstring& path);

std::string Sha256File(const std::wstring& path);     // lower-case hex; empty when the file cannot be read
// Does the file contain this text, as plain characters or as UTF-16 (how a wide string literal is stored)? Reads the file in pieces; nothing is loaded or run.
bool FileContainsText(const std::wstring& path, const std::string& ascii);
bool FileSizeOf(const std::wstring& path, uint64_t& size);
bool Exists(const std::wstring& path);                // a file or a folder

std::string Narrow(const std::wstring& text);         // UTF-16 to UTF-8
std::wstring Widen(const std::string& text);          // UTF-8 to UTF-16
std::wstring JoinPath(const std::wstring& a, const std::wstring& b);
std::wstring LowerCase(std::wstring text);
// One spelling for a folder or file path, so two paths can be compared: made absolute, with '.' and '..' and forward slashes resolved, short (8.3) names and
// links followed when the path exists, no trailing separator (except a drive root) and lower case. Empty in, empty out.
std::wstring CanonicalPath(const std::wstring& path);

} // namespace setup
