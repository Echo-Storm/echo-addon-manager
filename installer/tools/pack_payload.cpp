// Packs a folder (laid out like the release zip) into the file bundle the installer carries: pack_payload <folder> <out.bin>
#include "payload.h"
#include "fileinfo.h"
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

int wmain(int argc, wchar_t** argv) {
    if (argc != 3) { puts("usage: pack_payload <folder> <out.bin>"); return 2; }
    std::vector<uint8_t> blob;
    std::string err;
    if (!setup::PackFolder(argv[1], blob, err)) { printf("pack failed: %s\n", err.c_str()); return 1; }
    size_t count = 0;
    uint64_t bytes = 0;
    if (!setup::ValidatePayload(blob.data(), blob.size(), count, bytes, err)) { printf("the bundle does not validate: %s\n", err.c_str()); return 1; }
    std::ofstream out(argv[2], std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(blob.data()), static_cast<std::streamsize>(blob.size()));
    out.close();
    if (!out) { puts("cannot write the output file"); return 1; }
    printf("packed %zu files, %llu bytes\n", count, static_cast<unsigned long long>(bytes));
    return 0;
}
