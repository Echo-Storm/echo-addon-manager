#include "addon_runtime.h"

namespace lsproxy {

template <class Fn>
static Fn Export(HMODULE module, const char* name) {
    return reinterpret_cast<Fn>(GetProcAddress(module, name));
}

void BindExports(HMODULE module, AddonExports& out) {
    out.init = Export<AddonInit_t>(module, "AddonInitialize");
    if (!out.init) out.init = Export<AddonInit_t>(module, "AddonInit");
    out.shutdown = Export<AddonShutdown_t>(module, "AddonShutdown");
    out.renderSettings = Export<AddonRenderSettings_t>(module, "AddonRenderSettings");
    out.intercept = Export<AddonInterceptResource_t>(module, "AddonInterceptResource");
    out.name = Export<GetAddonName_t>(module, "GetAddonName");
    out.version = Export<GetAddonVersion_t>(module, "GetAddonVersion");
    out.author = Export<GetAddonAuthor_t>(module, "GetAddonAuthor");
    out.description = Export<GetAddonDescription_t>(module, "GetAddonDescription");
}

namespace guarded {

uint32_t Capabilities(HMODULE module) {
    const auto fn = Export<GetAddonCaps_t>(module, "GetAddonCapabilities");
    if (!fn) return 0;
    __try { return fn(); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

bool Initialize(const AddonExports& fn, IHost* host, ImGuiContext* ctx, void* alloc, void* release, void* userData) {
    if (!fn.init) return false;
    __try { fn.init(host, ctx, alloc, release, userData); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

void Shutdown(const AddonExports& fn) {
    if (!fn.shutdown) return;
    __try { fn.shutdown(); }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

void RenderSettings(const AddonExports& fn) {
    if (!fn.renderSettings) return;
    __try { fn.renderSettings(); }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

bool Intercept(const AddonExports& fn, const wchar_t* name, const wchar_t* type, const void** data, uint32_t* size) {
    if (!fn.intercept) return false;
    __try { return fn.intercept(name, type, data, size); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

const char* Text(const char* (*getter)()) {
    if (!getter) return nullptr;
    __try { return getter(); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

} // namespace guarded
} // namespace lsproxy
