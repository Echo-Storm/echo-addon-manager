// A small addon for the offline manager tests (eam_coretest). It does nothing useful: it records what the manager asks of it in
// "calls.txt" beside its DLL, and its behaviour is chosen by a one-word "mode.txt" beside it:
//   (none)          normal
//   restart         asks for a restart to be enabled or disabled
//   crash_init      faults in AddonInitialize
//   crash_shutdown  faults in AddonShutdown
//   leaky           subscribes to event 0x7E57 and sets a pre-dispatch callback, and leaves both registered when it is shut down
#include <eam/addon_sdk.h>
#include <windows.h>
#include <fstream>
#include <string>

static std::wstring Folder() {
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&Folder, &self);
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(self, path, MAX_PATH);
    std::wstring p = path;
    return p.substr(0, p.find_last_of(L"\\/"));
}

static std::string Mode() {
    std::ifstream f(Folder() + L"\\mode.txt");
    std::string m;
    std::getline(f, m);
    return m;
}

static void Note(const char* what) {
    std::ofstream f(Folder() + L"\\calls.txt", std::ios::app);
    f << what << "\n";
}

static void Fault() {
    volatile int* p = nullptr;
    *p = 1;
}

static void OnTestEvent(uint32_t, const void*, uint32_t, void*) { Note("event"); }
static bool OnTestPreDispatch(uint32_t, uint32_t, uint32_t, void*) { Note("dispatch"); return false; }

EAM_EXPORT void AddonInitialize(IHost* host, ImGuiContext*, void*, void*, void*) {
    Note("init");
    if (Mode() == "crash_init") Fault();
    if (Mode() == "leaky" && host) {   // registers callbacks and never clears them (AddonShutdown below does not): the manager has to
        host->SubscribeEvent(0x7E57, OnTestEvent, nullptr);
        host->SetPreDispatchCallback(OnTestPreDispatch, reinterpret_cast<void*>(0x7E57));
    }
}

EAM_EXPORT void AddonShutdown() {
    Note("shutdown");
    if (Mode() == "crash_shutdown") Fault();
}

EAM_EXPORT uint32_t GetAddonCapabilities() {
    return EAM_CAP_HAS_SETTINGS | (Mode() == "restart" ? EAM_CAP_REQUIRES_RESTART : 0);
}

EAM_EXPORT void AddonRenderSettings() { Note("settings"); }

EAM_EXPORT bool AddonInterceptResource(const wchar_t* name, const wchar_t*, const void** outData, uint32_t* outSize) {
    if (name && std::wstring(name) == L"test.shader") {
        static const char kData[] = "HELLO";
        *outData = kData;
        *outSize = sizeof kData - 1;
        return true;
    }
    return false;
}

EAM_EXPORT const char* GetAddonName()        { return "Test Addon"; }
EAM_EXPORT const char* GetAddonVersion()     { return "9.9.9"; }
EAM_EXPORT const char* GetAddonAuthor()      { return "Tester"; }
EAM_EXPORT const char* GetAddonDescription() { return "For the offline tests"; }
