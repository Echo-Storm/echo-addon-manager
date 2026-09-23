#include "hook_util.h"
#include <MinHook.h>
#include <cstring>
#include <mutex>

namespace eam::hooks {

namespace {
std::mutex g_minHookMutex;
int g_minHookUsers = 0;

const IMAGE_NT_HEADERS* Headers(HMODULE module) {
    const auto* base = reinterpret_cast<const BYTE*>(module);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (!module || dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    return nt->Signature == IMAGE_NT_SIGNATURE ? nt : nullptr;
}

// Walks a name table (import lookup table or delay name table) beside its address table and returns the address slot of `function`.
void** FindInTables(BYTE* base, DWORD namesRva, DWORD slotsRva, const char* function) {
    if (!namesRva || !slotsRva) return nullptr;
    auto* names = reinterpret_cast<IMAGE_THUNK_DATA*>(base + namesRva);
    auto* slots = reinterpret_cast<IMAGE_THUNK_DATA*>(base + slotsRva);
    for (; names->u1.AddressOfData; ++names, ++slots) {
        if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
        const auto* byName = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
        if (std::strcmp(reinterpret_cast<const char*>(byName->Name), function) == 0) return reinterpret_cast<void**>(&slots->u1.Function);
    }
    return nullptr;
}
} // namespace

bool Begin() {
    std::lock_guard<std::mutex> lock(g_minHookMutex);
    if (g_minHookUsers == 0) {
        const MH_STATUS st = MH_Initialize();
        if (st != MH_OK && st != MH_ERROR_ALREADY_INITIALIZED) return false;
    }
    ++g_minHookUsers;
    return true;
}

void End() {
    std::lock_guard<std::mutex> lock(g_minHookMutex);
    if (g_minHookUsers == 0) return;
    if (--g_minHookUsers == 0) MH_Uninitialize();
}

void** ImportSlot(HMODULE module, const char* dll, const char* function) {
    const IMAGE_NT_HEADERS* nt = Headers(module);
    if (!nt) return nullptr;
    const IMAGE_DATA_DIRECTORY& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return nullptr;
    auto* base = reinterpret_cast<BYTE*>(module);
    for (auto* d = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); d->Name; ++d) {
        if (_stricmp(reinterpret_cast<const char*>(base + d->Name), dll) != 0) continue;
        // without a separate lookup table (an old linker) the address table itself still holds the names until the loader binds it
        if (void** slot = FindInTables(base, d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk, d->FirstThunk, function)) return slot;
    }
    return nullptr;
}

void** DelayImportSlot(HMODULE module, const char* dll, const char* function) {
    const IMAGE_NT_HEADERS* nt = Headers(module);
    if (!nt) return nullptr;
    const IMAGE_DATA_DIRECTORY& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT];
    if (!dir.VirtualAddress) return nullptr;
    auto* base = reinterpret_cast<BYTE*>(module);
    for (auto* d = reinterpret_cast<IMAGE_DELAYLOAD_DESCRIPTOR*>(base + dir.VirtualAddress); d->DllNameRVA; ++d) {
        if (!(d->Attributes.RvaBased)) continue;   // the pre-VC7 layout with pointers instead of RVAs: not produced by any current linker
        if (_stricmp(reinterpret_cast<const char*>(base + d->DllNameRVA), dll) != 0) continue;
        if (void** slot = FindInTables(base, d->ImportNameTableRVA, d->ImportAddressTableRVA, function)) return slot;
    }
    return nullptr;
}

void* SwapSlot(void** slot, void* fn) {
    DWORD protect = 0;
    if (!slot || !VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &protect)) return nullptr;
    void* const was = *slot;
    *slot = fn;
    VirtualProtect(slot, sizeof(void*), protect, &protect);
    return was;
}

} // namespace eam::hooks
