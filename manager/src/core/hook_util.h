// What the manager's hooks share: MinHook, which more than one part of the manager uses, and the import tables of Lossless_original.dll.
#pragma once
#include <windows.h>

namespace eam::hooks {

// MinHook is one per module: every part of the manager that creates hooks with it calls Begin first and End when it is done, and MinHook is
// started with the first Begin and stopped with the last End. Each part enables and disables its own hooks by target, never MH_ALL_HOOKS.
bool Begin();
void End();

// The slot in `module`'s import address table through which it calls `function` of `dll` (names compared without regard to case for the DLL),
// or null when it does not import it by name. The delay variant looks in the delay-load table, whose slot holds a loader stub until the first call.
void** ImportSlot(HMODULE module, const char* dll, const char* function);
void** DelayImportSlot(HMODULE module, const char* dll, const char* function);

// Writes `fn` into the slot and returns what was there; null when the page could not be made writable (the slot is then unchanged).
void* SwapSlot(void** slot, void* fn);

} // namespace eam::hooks
