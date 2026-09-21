#pragma once
// What Neural Rendering needs to run, checked and explained in plain words: an NVIDIA card, the NVIDIA driver's NGX core, the model file
// (nvngx_dlssnr.dll), this addon's helper DLL, and whether the engine started. Evaluate() is pure (it takes what was found and says what it
// means), Gather() is what looks at the machine. Nothing here loads the model or downloads anything.
#include <cstdint>
#include <string>
#include <vector>

namespace req {

enum class Level { Ok, Note, Missing };

struct Row {
    std::string label;   // "Graphics card", "NVIDIA driver", "Model file", "Helper DLL", "Engine"
    std::string value;   // what was found, in one line
    std::string hint;    // what to do about it; empty when the row is Ok
    Level level = Level::Ok;
};

// The model this addon was built and tested against. Another build may work; it has just not been seen here.
constexpr uint64_t kTestedModelSize = 165840496ull;
constexpr const char* kTestedModelVersion = "310.8";
constexpr uint64_t kSmallestPlausibleModel = 20ull * 1024 * 1024;   // the real one is about 158 MB; anything far smaller is not it

enum class EngineState { NotStarted, Ready, Running, Failed };

struct Inputs {
    bool nvidiaFound = false;                // an NVIDIA hardware adapter exists
    std::string gpuName;
    uint64_t gpuMemoryBytes = 0;

    bool ngxRegistered = false;              // the driver's registry entry (HKLM\SOFTWARE\NVIDIA Corporation\Global\NGXCore) names a folder
    bool ngxCoreFound = false;               // and _nvngx.dll is in it
    std::string ngxCoreVersion;              // its file version, "32.0.16.1692"

    std::string modelPath;                   // where the model is expected (UTF-8)
    bool modelFound = false;
    uint64_t modelSize = 0;
    std::string modelVersion;                // its file version, "310.8.0.0" (or "310,8,0,0")

    bool helperFound = false;                // nvngx.dll_dlss5nr01.dll beside the addon

    EngineState engine = EngineState::NotStarted;
    std::string engineError;                 // the engine's own message when it failed
};

struct Report {
    std::vector<Row> rows;
    Level overall = Level::Ok;               // Missing if any row is; Note if any row is; else Ok
    std::string headline;                    // the first problem in one line, or that everything is in place
};

Report Evaluate(const Inputs& in);

std::string DriverFromNgxVersion(const std::string& fileVersion);   // "32.0.16.1692" -> "616.92"; "" when it is not in that shape
std::string VersionShort(const std::string& fileVersion);           // "310.8.0.0" or "310,8,0,0" -> "310.8"
std::string SizeText(uint64_t bytes);                               // "158.2 MB"
std::string PlainEngineError(const std::string& raw);               // the engine's message in words a person can act on

// Looks at this machine. `modelPath` is where the model is expected, `addonDir` the folder holding the helper DLL. The engine fields are
// left at their defaults for the caller to fill in. Reads file headers only: it does not load the model, and takes well under a second.
Inputs Gather(const std::wstring& modelPath, const std::wstring& addonDir);

} // namespace req
