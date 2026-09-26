// eam_runtimetest: the Runtimes list's facts about a file (runtime_files.h), with no window. A DLL loaded under another spelling of its path
// (the mixed slashes NVIDIA's loader uses) must count as loaded; the version, signature and SHA-256 must be read; a file not loaded must not
// count. The DLL is AMD's signed FidelityFX runtime the FSR Upscaler ships (argument 1).
#include "src/addon/runtime_files.h"
#include <windows.h>
#include <cstdio>
#include <string>
#include <thread>
#include <chrono>

using namespace eam;

static int g_failures = 0;
static void Check(bool ok, const char* what, const std::string& detail = "") {
    printf("  %s  %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : "  ", detail.c_str());
    if (!ok) ++g_failures;
}

int main(int argc, char** argv) {
    if (argc < 2) { printf("usage: eam_runtimetest <folder holding fsr\\amd_fidelityfx_dx12.dll>\n"); return 2; }
    std::wstring dir(argv[1], argv[1] + strlen(argv[1]));
    for (wchar_t& ch : dir) if (ch == L'/') ch = L'\\';
    AddonInfo addon; addon.id = "FSR3UPSC"; addon.manifest.name = "FSR Upscaler"; addon.enabled = true;
    addon.dllPath = dir + L"\\FSR3UPSC.dll";
    AddonManifest::Runtime slot; slot.name = "FSR"; slot.file = "fsr/amd_fidelityfx_dx12.dll";
    slot.shippedSha256 = "12a5081257ec95b0b53ad51b4a87fb3c03f97fe0bbb59f9496968f8d50ef93a6"; slot.shippedLabel = "3.1.4";
    addon.manifest.runtimes.push_back(slot);
    const std::vector<AddonInfo> addons = { addon };

    auto rows = [&] {   // the facts are read on a thread of their own: wait for them
        std::vector<RuntimeFile> r;
        for (int i = 0; i < 100; ++i) { r = RuntimeFiles(addons, dir); if (!r.empty() && r[0].read) break; std::this_thread::sleep_for(std::chrono::milliseconds(50)); }
        return r;
    };
    std::vector<RuntimeFile> before = rows();
    Check(before.size() == 1 && before[0].exists && before[0].read, "the file is found and read");
    if (before.empty()) return 1;
    Check(before[0].signature == RuntimeFile::Signature::Signed && before[0].signer.find("Advanced Micro Devices") != std::string::npos, "signed by AMD", before[0].signer);
    Check(before[0].shipped && before[0].ShownVersion() == "3.1.4", "the shipped file, shown as 3.1.4", before[0].ShownVersion());
    Check(!before[0].loaded, "not loaded yet: a cross");

    // loaded the way NVIDIA's loader spells paths: the folder with backslashes, then a forward slash
    const std::wstring spelled = dir + L"\\fsr/amd_fidelityfx_dx12.dll";
    HMODULE module = LoadLibraryExW(spelled.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    Check(module != nullptr, "loaded under a mixed-slash path");
    std::this_thread::sleep_for(std::chrono::milliseconds(2100));   // the module list is read at most every two seconds
    std::vector<RuntimeFile> after = rows();
    Check(!after.empty() && after[0].loaded, "now a tick: the same file, however its path was written");
    if (module) FreeLibrary(module);

    printf(g_failures ? "\nRUNTIME TEST FAILED (%d)\n" : "\nRUNTIME TEST PASSED\n", g_failures);
    return g_failures ? 1 : 0;
}
