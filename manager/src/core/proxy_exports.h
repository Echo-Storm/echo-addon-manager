// Lossless.dll's exports. The linker forwards each one to Lossless_original.dll, so Lossless Scaling calls the real function with no code of ours
// in between. ApplySettings is the exception: main.cpp defines it, to publish EAM_EVENT_SETTINGS_APPLIED once the real one has run.
// Included by main.cpp only.
#pragma once

#define EAM_FORWARD(name) __pragma(comment(linker, "/export:" #name "=Lossless_original." #name))
EAM_FORWARD(Activate)
EAM_FORWARD(GetAdapterNames)
EAM_FORWARD(GetDisplayNames)
EAM_FORWARD(GetDwmRefreshRate)
EAM_FORWARD(GetForegroundWindowEx)
EAM_FORWARD(Init)
EAM_FORWARD(IsWindowsBuildAtLeast)
EAM_FORWARD(SetDriverSettings)
EAM_FORWARD(SetWindowsSettings)
EAM_FORWARD(UnInit)
#undef EAM_FORWARD
