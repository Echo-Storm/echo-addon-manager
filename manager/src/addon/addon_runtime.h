#pragma once
#include "addon_info.h"

struct IHost;
struct ImGuiContext;

namespace eam {

// Looks up every export the addon API defines in a loaded addon DLL. Missing ones stay null; AddonInitialize is also accepted as AddonInit.
void BindExports(HMODULE module, AddonExports& out);

// Calls into an addon behind a structured-exception guard, so a fault inside its code cannot take Lossless Scaling down with it. The
// calls are kept this small on purpose: a function with a __try block cannot also hold objects that need destructors.
namespace guarded {

uint32_t Capabilities(HMODULE module);   // 0 when the DLL has no GetAddonCapabilities, or it faults
bool Initialize(const AddonExports& fn, IHost* host, ImGuiContext* ctx, void* alloc, void* release, void* userData);   // false = it faulted
void Shutdown(const AddonExports& fn);
bool RenderSettings(const AddonExports& fn);   // false when the panel faulted
bool Intercept(const AddonExports& fn, const wchar_t* name, const wchar_t* type, const void** data, uint32_t* size);
const char* Text(const char* (*getter)());   // nullptr when the getter is missing or faults

} // namespace guarded

} // namespace eam
