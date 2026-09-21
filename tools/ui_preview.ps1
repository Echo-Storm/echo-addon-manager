# Renders the manager theme, the addon cards and the addon panels offscreen (no window, no screen capture) and converts the
# BMPs to PNGs so they can be viewed. The NR panel comes from the test host's shot= mode (see run_hosttest_matrix.py).
#   powershell -File ui_preview.ps1 [-Out C:\path\to\folder] [-Scale 1.25]
param([string]$Out = "$env:TEMP\ui_preview", [double]$Scale = 1.25)
New-Item -ItemType Directory -Force $Out | Out-Null
$root = Split-Path $PSScriptRoot -Parent   # the repository folder
$exe = "$root\manager\build\Release\lsproxy_uipreview.exe"
$dlls = @(
    "$root\addons\LSP-ReShade\build\Release\LSP_ReShade.dll",
    "$root\addons\LSP-Windowed\build\LSP_Windowed\Release\LSP_Windowed.dll"
)
& $exe ($Out -replace '\\', '/') $Scale @($dlls | Where-Object { Test-Path $_ }) | Select-Object -Last 8
python "$PSScriptRoot\bmp2png.py" $Out
