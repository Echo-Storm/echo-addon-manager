// ShaderHook: lets addons replace the resources Lossless Scaling loads from its own DLL (its compiled shaders are resources in it).
//
// Lossless_original.dll reads a resource with FindResourceW, LoadResource, SizeofResource, LockResource and FreeResource from kernel32. Those five
// slots of its import table are pointed at ours. When an addon supplies a replacement, FindResourceW hands out a handle of our own and the other
// four answer for it from a copy of the addon's bytes; every other resource goes to kernel32 as before.
#pragma once
#include <windows.h>
#include <cstdint>
#include <functional>

namespace ShaderHook {

// Asked for every resource the module looks up: true, with the bytes, to replace it. The bytes are copied at once.
using Interceptor = std::function<bool(const wchar_t* name, const wchar_t* type, const void** data, uint32_t* size)>;

// Points `module`'s resource imports at the hook. False when it imports none of them (then nothing is changed).
bool InstallHooks(HMODULE module, Interceptor intercept);
// Puts the module's import slots back. Replacement bytes already handed out stay valid until the process ends.
void UninstallHooks();

} // namespace ShaderHook
