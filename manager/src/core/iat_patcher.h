#pragma once

#include <windows.h>

// IAT patching utilities
namespace IatPatcher {

    template<typename FuncPtr>
    FuncPtr PatchIat(HMODULE moduleToPatc, const char* importDllName, const char* importName, FuncPtr newFunction) {
        PIMAGE_DOS_HEADER pDosHeader = (PIMAGE_DOS_HEADER)moduleToPatc;
        if (pDosHeader->e_magic != IMAGE_DOS_SIGNATURE) {
            return nullptr;
        }

        PIMAGE_NT_HEADERS pNtHeaders = (PIMAGE_NT_HEADERS)((BYTE*)moduleToPatc + pDosHeader->e_lfanew);
        if (pNtHeaders->Signature != IMAGE_NT_SIGNATURE) {
            return nullptr;
        }

        PIMAGE_IMPORT_DESCRIPTOR pImportDesc = (PIMAGE_IMPORT_DESCRIPTOR)
            ((BYTE*)moduleToPatc + pNtHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);

        if (!pImportDesc) {
            return nullptr;
        }

        for (; pImportDesc->Name; pImportDesc++) {
            const char* dllName = (const char*)((BYTE*)moduleToPatc + pImportDesc->Name);
            if (_stricmp(dllName, importDllName) != 0) {
                continue;
            }

            PIMAGE_THUNK_DATA pThunk = (PIMAGE_THUNK_DATA)((BYTE*)moduleToPatc + pImportDesc->FirstThunk);
            PIMAGE_THUNK_DATA pOrigThunk = (PIMAGE_THUNK_DATA)((BYTE*)moduleToPatc + pImportDesc->OriginalFirstThunk);

            for (; pThunk->u1.Function; pThunk++, pOrigThunk++) {
                if (IMAGE_SNAP_BY_ORDINAL(pOrigThunk->u1.Ordinal)) {
                    continue;
                }

                PIMAGE_IMPORT_BY_NAME pImportByName = (PIMAGE_IMPORT_BY_NAME)
                    ((BYTE*)moduleToPatc + pOrigThunk->u1.AddressOfData);

                if (strcmp((const char*)pImportByName->Name, importName) != 0) {
                    continue;
                }

                FuncPtr originalFunc = (FuncPtr)pThunk->u1.Function;

                DWORD oldProtect = 0;
                VirtualProtect(pThunk, sizeof(*pThunk), PAGE_READWRITE, &oldProtect);
                pThunk->u1.Function = (ULONG_PTR)newFunction;
                VirtualProtect(pThunk, sizeof(*pThunk), oldProtect, &oldProtect);

                return originalFunc;
            }
        }

        return nullptr;
    }

    // Patch a delay-loaded import. Delay imports use IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT
    // and the thunk table is resolved lazily on first call.
    template<typename FuncPtr>
    FuncPtr PatchDelayIat(HMODULE moduleToPatch, const char* importDllName, const char* importName, FuncPtr newFunction) {
        PIMAGE_DOS_HEADER pDosHeader = (PIMAGE_DOS_HEADER)moduleToPatch;
        if (pDosHeader->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;

        PIMAGE_NT_HEADERS pNtHeaders = (PIMAGE_NT_HEADERS)((BYTE*)moduleToPatch + pDosHeader->e_lfanew);
        if (pNtHeaders->Signature != IMAGE_NT_SIGNATURE) return nullptr;

        auto& delayDir = pNtHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT];
        if (delayDir.VirtualAddress == 0) return nullptr;

        // ImgDelayDescr structure (delayimp.h)
        struct ImgDelayDescr {
            DWORD grAttrs;      // must be 1 (RVAs used)
            DWORD rvaDLLName;
            DWORD rvaHmod;
            DWORD rvaIAT;       // delay IAT
            DWORD rvaINT;       // delay INT (name table)
            DWORD rvaBoundIAT;
            DWORD rvaUnloadIAT;
            DWORD dwTimeStamp;
        };

        auto* pDesc = (ImgDelayDescr*)((BYTE*)moduleToPatch + delayDir.VirtualAddress);

        for (; pDesc->rvaDLLName; pDesc++) {
            const char* dllName = (const char*)((BYTE*)moduleToPatch + pDesc->rvaDLLName);
            if (_stricmp(dllName, importDllName) != 0) continue;

            auto* pIAT = (PIMAGE_THUNK_DATA)((BYTE*)moduleToPatch + pDesc->rvaIAT);
            auto* pINT = (PIMAGE_THUNK_DATA)((BYTE*)moduleToPatch + pDesc->rvaINT);

            for (; pINT->u1.AddressOfData; pIAT++, pINT++) {
                if (IMAGE_SNAP_BY_ORDINAL(pINT->u1.Ordinal)) continue;

                auto* pImportByName = (PIMAGE_IMPORT_BY_NAME)
                    ((BYTE*)moduleToPatch + pINT->u1.AddressOfData);

                if (strcmp((const char*)pImportByName->Name, importName) != 0) continue;

                FuncPtr originalFunc = (FuncPtr)pIAT->u1.Function;

                DWORD oldProtect = 0;
                VirtualProtect(pIAT, sizeof(*pIAT), PAGE_READWRITE, &oldProtect);
                pIAT->u1.Function = (ULONG_PTR)newFunction;
                VirtualProtect(pIAT, sizeof(*pIAT), oldProtect, &oldProtect);

                return originalFunc;
            }
        }

        return nullptr;
    }
}
