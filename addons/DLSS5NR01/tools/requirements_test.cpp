// Offline test of the requirements checker: the decision logic with made-up findings, the small text helpers, and a real look at this machine
// (which only has to work and be well formed; what it finds depends on the machine).
//   nr_reqtest.exe [path to nvngx_dlssnr.dll]
#include "addon/requirements.h"
#include <windows.h>
#include <cstdio>
#include <string>

using namespace req;

static int g_failed = 0;
static void Check(const char* what, bool ok, const std::string& detail = "") {
    printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : "  (", detail.empty() ? "" : (detail + ")").c_str());
    if (!ok) ++g_failed;
}
static const Row* Find(const Report& r, const char* label) {
    for (const auto& row : r.rows) if (row.label == label) return &row;
    return nullptr;
}
static bool Has(const std::string& s, const char* part) { return s.find(part) != std::string::npos; }

// Everything present and the tested model.
static Inputs Good() {
    Inputs in;
    in.nvidiaFound = true; in.gpuName = "NVIDIA GeForce RTX 4070 Ti SUPER"; in.gpuMemoryBytes = 12ull << 30;
    in.ngxRegistered = true; in.ngxCoreFound = true; in.ngxCoreVersion = "32.0.16.1692";
    in.modelPath = "D:\\LS\\nvngx_dlssnr.dll"; in.modelFound = true; in.modelSize = kTestedModelSize; in.modelVersion = "310,8,0,0";
    in.helperFound = true;
    return in;
}

int main(int argc, char** argv) {
    // ---- text helpers
    Check("driver number from the NGX core version", DriverFromNgxVersion("32.0.16.1692") == "616.92" && DriverFromNgxVersion("31.0.15.5222") == "552.22");
    Check("...and nothing when the version is not in that shape", DriverFromNgxVersion("").empty() && DriverFromNgxVersion("1.2").empty() && DriverFromNgxVersion("a.b.c.d").empty());
    Check("short version accepts dots and commas", VersionShort("310.8.0.0") == "310.8" && VersionShort("310,8,0,0") == "310.8" && VersionShort("").empty() && VersionShort("7") == "7");
    Check("size text", SizeText(165840496ull) == "158.2 MB" && SizeText(0) == "0 MB" && SizeText(12ull << 30) == "12288.0 MB");

    // ---- all in place
    Report r = Evaluate(Good());
    Check("all in place: overall Ok, five rows in order", r.overall == Level::Ok && r.rows.size() == 5 && r.rows[0].label == "Graphics card" && r.rows[4].label == "Engine");
    Check("all in place: the tested model is named as such", Has(Find(r, "Model file")->value, "310.8") && Has(Find(r, "Model file")->value, "tested") && Find(r, "Model file")->level == Level::Ok);
    Check("all in place: the driver is shown as a driver number", Has(Find(r, "NVIDIA driver")->value, "616.92"));
    Check("all in place: the card shows its memory", Has(Find(r, "Graphics card")->value, "12 GB") && Has(Find(r, "Graphics card")->value, "RTX 4070"));
    Check("all in place: no hints on rows that are fine", Find(r, "Model file")->hint.empty() && Find(r, "Graphics card")->hint.empty());
    Check("all in place: the engine not having started is not a problem", Find(r, "Engine")->level == Level::Ok && Has(Find(r, "Engine")->value, "not started"));
    Check("all in place: the headline says so", Has(r.headline, "in place"));

    // ---- each thing missing
    Inputs in = Good(); in.nvidiaFound = false; r = Evaluate(in);
    Check("no NVIDIA card: Missing, and it says what is needed", r.overall == Level::Missing && Find(r, "Graphics card")->level == Level::Missing && Has(Find(r, "Graphics card")->hint, "RTX"));
    Check("no NVIDIA card: the headline names the card", Has(r.headline, "Graphics card"));

    in = Good(); in.ngxRegistered = false; in.ngxCoreFound = false; r = Evaluate(in);
    Check("no NGX core registered: Missing, hint says to install or repair the driver", Find(r, "NVIDIA driver")->level == Level::Missing && Has(Find(r, "NVIDIA driver")->hint, "driver"));
    in = Good(); in.ngxCoreFound = false; r = Evaluate(in);
    Check("NGX core registered but the file is gone: Missing, and says the file", Find(r, "NVIDIA driver")->level == Level::Missing && Has(Find(r, "NVIDIA driver")->value, "_nvngx.dll"));

    in = Good(); in.modelFound = false; in.modelSize = 0; in.modelVersion.clear(); r = Evaluate(in);
    Check("no model file: Missing, with the path it looked at", Find(r, "Model file")->level == Level::Missing && Has(Find(r, "Model file")->value, "D:\\LS\\nvngx_dlssnr.dll"));
    Check("no model file: the hint says where to put it and that this project does not say where to get it",
          Has(Find(r, "Model file")->hint, "next to LosslessScaling.exe") && Has(Find(r, "Model file")->hint, "does not say where"));
    Check("no model file: the headline is about the model", Has(r.headline, "Model file"));

    in = Good(); in.modelSize = 4096; r = Evaluate(in);
    Check("a tiny model file is not accepted as the model", Find(r, "Model file")->level == Level::Missing && Has(Find(r, "Model file")->value, "too small"));

    in = Good(); in.modelVersion = "311,2,0,0"; r = Evaluate(in);
    Check("another model version: a note, not an error, naming the tested one", Find(r, "Model file")->level == Level::Note && Has(Find(r, "Model file")->value, "311.2") && Has(Find(r, "Model file")->value, "310.8"));
    Check("another model version: overall is Note and the headline says it", r.overall == Level::Note && Has(r.headline, "Model file"));
    in = Good(); in.modelSize = kTestedModelSize + 1; r = Evaluate(in);
    Check("the tested version but another size is also only a note", Find(r, "Model file")->level == Level::Note);
    in = Good(); in.modelVersion.clear(); r = Evaluate(in);
    Check("a model with no readable version is a note", Find(r, "Model file")->level == Level::Note && Has(Find(r, "Model file")->value, "unknown"));

    in = Good(); in.helperFound = false; r = Evaluate(in);
    Check("helper DLL missing: Missing, hint says the addon folder is incomplete", Find(r, "Helper DLL")->level == Level::Missing && Has(Find(r, "Helper DLL")->hint, "incomplete"));

    // ---- engine
    in = Good(); in.engine = EngineState::Running; r = Evaluate(in);
    Check("engine running: Ok", Find(r, "Engine")->level == Level::Ok && Has(Find(r, "Engine")->value, "running"));
    in = Good(); in.engine = EngineState::Ready; r = Evaluate(in);
    Check("engine ready: Ok", Find(r, "Engine")->level == Level::Ok && Has(Find(r, "Engine")->value, "ready"));
    in = Good(); in.engine = EngineState::Failed; in.engineError = "snippet probe 0x0 (D:\\LS\\nvngx_dlssnr.dll)"; r = Evaluate(in);
    Check("engine failed with everything present: Missing, in words, pointing at Restart engine and the log",
          r.overall == Level::Missing && Find(r, "Engine")->level == Level::Missing && Has(Find(r, "Engine")->value, "could not be loaded") &&
          Has(Find(r, "Engine")->hint, "Restart engine") && Has(Find(r, "Engine")->hint, "log"));
    in = Good(); in.modelFound = false; in.engine = EngineState::Failed; in.engineError = "snippet probe 0x0"; r = Evaluate(in);
    Check("engine failed because the model is missing: the first problem in the headline is the model, not the engine", Has(r.headline, "Model file"));
    Check("...and the engine row points back up instead of repeating", Has(Find(r, "Engine")->hint, "above"));

    // ---- plain-language engine messages
    Check("plain: model probe", Has(PlainEngineError("snippet probe 0x0 (x)"), "model file could not be loaded"));
    Check("plain: helper load", Has(PlainEngineError("forwarder LoadLibrary 126 (x)"), "helper DLL could not be loaded"));
    Check("plain: helper export", Has(PlainEngineError("forwarder export nrfwd_probe missing"), "different version"));
    Check("plain: NGX core", Has(PlainEngineError("NGX core Init: NVSDK_NGX_Result_FAIL_NotSupported"), "NGX core did not start") && Has(PlainEngineError("NGX core Init: NVSDK_NGX_Result_FAIL_NotSupported"), "NotSupported"));
    Check("plain: model init", Has(PlainEngineError("snippet Init_Ext: X"), "refused to start"));
    Check("plain: device", Has(PlainEngineError("D3D12CreateDevice 0x887a0004"), "Direct3D 12"));
    Check("plain: unknown messages are shown as they are", PlainEngineError("something new") == "something new");

    // ---- this machine
    std::wstring model = L"nvngx_dlssnr.dll";
    if (argc > 1) { int n = MultiByteToWideChar(CP_ACP, 0, argv[1], -1, nullptr, 0); model.assign(n, L'\0'); MultiByteToWideChar(CP_ACP, 0, argv[1], -1, model.data(), n); model.resize(n - 1); }
    wchar_t exe[MAX_PATH]; GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dir = exe; dir.resize(dir.find_last_of(L'\\'));
    const Inputs real = Gather(model, dir);
    const Report rr = Evaluate(real);
    printf("---- this machine\n");
    for (const auto& row : rr.rows) printf("  [%s] %s: %s%s%s\n", row.level == Level::Ok ? "ok" : (row.level == Level::Note ? "note" : "MISSING"), row.label.c_str(), row.value.c_str(), row.hint.empty() ? "" : "\n        -> ", row.hint.c_str());
    printf("  headline: %s\n", rr.headline.c_str());
    Check("this machine: five rows, each with a label and a value", rr.rows.size() == 5 && !rr.rows[0].value.empty() && !rr.rows[4].value.empty());
    Check("this machine: a row that is not Ok always has a hint (the engine row may just say it has not started)", [&] { for (const auto& row : rr.rows) if (row.level != Level::Ok && row.hint.empty()) return false; return true; }());
    {   // the helper is looked for by name in the addon folder: found where it is, not found in an empty folder
        wchar_t tmp[MAX_PATH]; GetTempPathW(MAX_PATH, tmp);
        const std::wstring empty = std::wstring(tmp) + L"nr_reqtest_empty";
        CreateDirectoryW(empty.c_str(), nullptr);
        const bool inEmpty = Gather(model, empty).helperFound;
        RemoveDirectoryW(empty.c_str());
        const bool onDisk = GetFileAttributesW((dir + L"\\nvngx.dll_dlss5nr01.dll").c_str()) != INVALID_FILE_ATTRIBUTES;
        Check("this machine: the helper DLL is found where it is and not in an empty folder", !inEmpty && real.helperFound == onDisk);
    }

    printf("\n%s (%d failed)\n", g_failed ? "REQUIREMENTS TEST FAILED" : "REQUIREMENTS TEST PASSED", g_failed);
    return g_failed ? 1 : 0;
}
