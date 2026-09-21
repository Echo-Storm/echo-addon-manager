# LSP-Windowed-Mode — Virtual Monitor / Windowed Mode

Enables windowed and borderless windowed mode for Lossless Scaling by injecting a virtual monitor. Lossless Scaling normally requires exclusive fullscreen — this addon tricks it into running in a window.

## What It Does

The addon hooks DXGI monitor enumeration APIs to inject a fake virtual monitor (`0xBADF00D`). Lossless Scaling sees this as a valid display target, allowing it to run in a window instead of requiring fullscreen.

Supported modes:

- **Split-Screen** — Position Lossless Scaling on half the screen (left/right/top/bottom), with the game on the other half
- **Window Positioning** — Place the Lossless Scaling window next to the target game window at a custom position

## Installation

It ships with Echo Addon Manager: unzip the release into your Lossless Scaling folder and the addon is in `addons\LSP-Windowed\`, switched off. Turn it
on in the manager's Addons tab. To install it on its own, use the manager's **Install addon** button and choose the folder (it must contain `LSP_Windowed.dll`
and `addon.json`).

To build it from source: `powershell -File tools\build_all.ps1 -Only windowed` in the repository root.

## Configuration

Open Echo Addon Manager and select Windowed Mode in the Addons tab.

| Setting | Description |
|---------|-------------|
| Mode | Split-Screen or Window Positioning |
| Split Direction | Left, Right, Top, or Bottom (split-screen mode) |
| Target Window | Which window to position next to (positioning mode) |

## How It Works

- Uses **MinHook** to intercept DXGI's `IMonitorEnumSink` and related interfaces
- Injects a fake monitor with handle `0xBADF00D` into the enumeration results
- Hooks `IDXGIOutput::GetDesc` to return fabricated monitor geometry
- Calculates window rectangles for split-screen or side-by-side layouts
- Integrates with D3D11 for Direct3D-aware window management

## Requirements

- Echo Addon Manager 0.1.0 or newer
- Dependencies: MinHook (fetched automatically by CMake)





## ⚠️ Disclaimer

This add-on is an unofficial extension for Lossless Scaling.

It is NOT affiliated with, endorsed by, or supported by
Lossless Scaling Developers in any way.

This add-on was developed through independent analysis and
reverse engineering of the software behavior. No proprietary
source code or assets are included.

Use at your own risk.

The author assumes no responsibility for any damage, data loss,
account bans, or other consequences resulting from the use of
this add-on.

This add-on may interact with the software at runtime in
non-documented ways.


## Legal Notice / Trademarks

All trademarks, product names, and company names are the property
of their respective owners and are used for identification purposes only.

