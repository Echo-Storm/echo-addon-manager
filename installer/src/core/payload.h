#pragma once
// The files the installer carries, packed into one blob so that the installer can be a single exe (the blob is a resource of the exe).
//
// Format (all numbers little-endian): "EAMPAYLD", u32 format (1), u32 count, then per file: u16 length of the path, the path (UTF-8, '/' between
// folders, relative), u64 size, the bytes. No compression: the files are a few megabytes, and a format this plain can be checked completely before
// anything is written. Unpacking validates every path (nothing absolute, no "..", no drive letters, no duplicates) and every length before it creates a file.
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace setup {

constexpr int kPayloadResourceId = 101;   // RCDATA resource holding the blob

// Pack every file under `dir` (sorted, so the same folder always gives the same blob).
bool PackFolder(const std::wstring& dir, std::vector<uint8_t>& blob, std::string& error);

// Check a blob completely without writing anything; on success `count` and `bytes` say what is inside.
bool ValidatePayload(const uint8_t* data, size_t size, size_t& count, uint64_t& bytes, std::string& error);

// Validate, then write the files below `dir` (created if needed). Nothing is written when the blob does not validate.
bool UnpackTo(const uint8_t* data, size_t size, const std::wstring& dir, std::string& error);

// Is this a path that may appear in a blob: relative, '/'-separated, no empty part, no "." or "..", no ':' and no backslash?
bool SafeRelativePath(const std::string& path);

// The blob embedded in this exe; false when the exe carries none.
bool EmbeddedPayload(const uint8_t*& data, size_t& size);

} // namespace setup
