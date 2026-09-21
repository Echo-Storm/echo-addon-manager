# Writing an addon

An addon is a native DLL (C++17, x64) in its own folder under `addons\`, with a small `addon.json`. The SDK headers are in
[`manager/sdk/include/lsproxy/`](../manager/sdk/include/lsproxy); include `lsproxy/addon_sdk.h`.

```
addons\
  MyAddon\
    addon.json       manifest
    MyAddon.dll      the addon
    icon.png         optional, shown on the addon's card
```

## The smallest addon

```cpp
#include <lsproxy/addon_sdk.h>
#include "imgui.h"

static IHost* g_host = nullptr;

LSPROXY_EXPORT void AddonInitialize(IHost* host, ImGuiContext* ctx, void* allocFunc, void* freeFunc, void* userData) {
    ImGui::SetCurrentContext(ctx);
    ImGui::SetAllocatorFunctions((ImGuiMemAllocFunc)allocFunc, (ImGuiMemFreeFunc)freeFunc, userData);
    g_host = host;
    g_host->Log(LSPROXY_LOG_INFO, "Hello from my addon");
}

LSPROXY_EXPORT void     AddonShutdown()          { g_host = nullptr; }
LSPROXY_EXPORT uint32_t GetAddonCapabilities()   { return LSPROXY_CAP_NONE; }
LSPROXY_EXPORT const char* GetAddonName()        { return "My Addon"; }
LSPROXY_EXPORT const char* GetAddonVersion()     { return "1.0.0"; }
LSPROXY_EXPORT const char* GetAddonAuthor()      { return "Me"; }
LSPROXY_EXPORT const char* GetAddonDescription() { return "Does something useful."; }
```

`addon.json`:

```json
{
    "name": "My Addon",
    "version": "1.0.0",
    "author": "Me",
    "description": "What it does, in a sentence.",
    "min_host_version": "1.0.0",
    "dependencies": [],
    "tags": ["category"]
}
```

`min_host_version` is compared with the **addon API** version (below), not with the manager's release number. An addon that needs a newer
API than the running manager provides is not loaded, and its card says why. Optional keys: `"dll"` (a DLL name other than the folder's) and `"icon"`.

## Exports

| Export | Signature | |
|--------|-----------|---|
| `AddonInitialize` | `void(IHost*, ImGuiContext*, void*, void*, void*)` | required: the host, the shared ImGui context and its allocator |
| `AddonShutdown` | `void()` | required |
| `GetAddonCapabilities` | `uint32_t()` | required: a bit mask, below |
| `AddonRenderSettings` | `void()` | draws the addon's panel (with `LSPROXY_CAP_HAS_SETTINGS`) |
| `AddonInterceptResource` | `bool(const wchar_t*, const wchar_t*, const void**, uint32_t*)` | replace one of Lossless Scaling's built-in shaders |
| `GetAddonName` / `Version` / `Author` / `Description` | `const char*()` | shown in the manager |

Capabilities: `LSPROXY_CAP_HAS_SETTINGS`, `LSPROXY_CAP_REQUIRES_RESTART` (enabling or disabling needs a restart), `LSPROXY_CAP_D3D11_DEVICE_ACCESS`
(ask for Lossless Scaling's device and context), `LSPROXY_CAP_DISPATCH_HOOK` (pre and post `Dispatch` callbacks).

## The host interface (`IHost`)

| Call | |
|------|---|
| `Log(level, message)` | thread-safe; shows in the Logs tab and `logs\EchoAddonManager.log` |
| `GetConfig(addonId, key, default)`, `SetConfig(...)`, `SaveConfig()` | your settings, stored in `addons\config.json` under your id |
| `GetHostVersion()` | the addon API version, `(major << 16) \| (minor << 8) \| patch` |
| `SubscribeEvent`, `UnsubscribeEvent`, `PublishEvent` | the event bus (events in `events.h`) |
| `GetD3D11Device()`, `GetD3D11DeviceContext()` | with `LSPROXY_CAP_D3D11_DEVICE_ACCESS` |
| `SetPreDispatchCallback`, `SetPostDispatchCallback`, `GetCurrentComputeShader`, `GetDispatchCount` | with `LSPROXY_CAP_DISPATCH_HOOK` |
| `SetStatus(addonId, text, level)` | one line on your card and in the status bar ("Running, model 6.6 ms"); level 0 grey, 1 green, 2 amber, 3 red; refresh it, a status not refreshed for a few seconds is hidden |
| `PublishMetric(addonId, key, value, unit)` | a number for the Performance tab; call it as often as you have a new value (the manager keeps the newest few thousand per series) |

`SetStatus` and `PublishMetric` are the newest calls, appended at the end of the interface, so an addon built against an older header is unaffected.
The addon API version is in `sdk/include/lsproxy/version.h`; it moves on its own, not with the release number. Bump its minor number when calls are added.

## Rules that are easy to trip over

- **Match the host's Dear ImGui.** The host and every addon share one `ImGuiContext`, so the struct layout must be identical. Build against the commit
  pinned in `manager/CMakeLists.txt` (each addon's CMake fetches the same one). An addon that carries its own ImGui build must also call
  `lsp::InitAddonImGui()` (from `lsp_widgets.h`) once after `SetCurrentContext`: some ImGui statics live per DLL and are otherwise uninitialised
  (symptom: text wrapping in the middle of words).
- **`enabled` is yours, `_enabled` is the host's.** The host keeps an addon's on/off state as `_enabled` in that addon's config object. Use any other
  key for your own settings.
- **A static CRT is invisible to the host's crash handlers.** If your DLL links the CRT statically (`/MT`), an `abort()`, `std::terminate` or invalid
  parameter inside it does not reach the host and ends Lossless Scaling with only a Windows fast-fail event. Install your own `std::set_terminate`,
  `signal(SIGABRT, ...)` and `_set_invalid_parameter_handler` and log a backtrace. Catch exceptions on any thread you create.
- **`GetConfig` strings** stay valid for the next 512 `GetConfig` calls; copy one if you need it longer.
- **Do not block the render thread.** Callbacks the manager makes from Lossless Scaling's threads (dispatch hooks, present) should return quickly.
- **A settings panel that trips an invalid CRT parameter** is switched off for the rest of the session after the first event.

## Look and feel: `lsp_widgets.h` and `lsp_icons.h`

Header-only helpers that give a panel the same look as the manager: the palette (`lsp::theme`), `SectionHeader`/`SectionLabel`, `Button` and
`IconButton` with icons, `SliderFloat`/`SliderInt` with a default (double-click resets, Ctrl+click types a value, Ctrl+scroll fine-tunes), a
`LineGraph`, and SVG-path icons (`lsp::icons::k...`) drawn straight into ImGui's draw list, so they are crisp at any scale.

## Testing without Lossless Scaling

The manager ships an offscreen renderer (`lsproxy_uipreview`) that draws its own tabs and any addon's panel into a picture without opening a window
(`tools\ui_preview.ps1`). The manager's own tests (`manager\tools`, including a small test addon you can copy) are run by `tools\run_addon_tests.ps1`, and Neural Rendering has its own in
`addons\LSP-NeuralRender\tools`, run by `tools\run_hosttest_matrix.py`. Copy that approach: load your DLL in a small host that implements `IHost`, render the panel, and check the result.
