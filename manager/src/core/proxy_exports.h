#pragma once

// Forward all Lossless.dll exports to Lossless_original.dll
// This must be included in exactly one translation unit (main.cpp)
//
// ApplySettings is NOT forwarded here — it's manually intercepted in main.cpp
// so the proxy can fire LSPROXY_EVENT_SETTINGS_APPLIED after the call.

#pragma comment(linker, "/export:Activate=Lossless_original.Activate")
#pragma comment(linker, "/export:GetAdapterNames=Lossless_original.GetAdapterNames")
#pragma comment(linker, "/export:GetDisplayNames=Lossless_original.GetDisplayNames")
#pragma comment(linker, "/export:GetDwmRefreshRate=Lossless_original.GetDwmRefreshRate")
#pragma comment(linker, "/export:GetForegroundWindowEx=Lossless_original.GetForegroundWindowEx")
#pragma comment(linker, "/export:Init=Lossless_original.Init")
#pragma comment(linker, "/export:IsWindowsBuildAtLeast=Lossless_original.IsWindowsBuildAtLeast")
#pragma comment(linker, "/export:SetDriverSettings=Lossless_original.SetDriverSettings")
#pragma comment(linker, "/export:SetWindowsSettings=Lossless_original.SetWindowsSettings")
#pragma comment(linker, "/export:UnInit=Lossless_original.UnInit")
