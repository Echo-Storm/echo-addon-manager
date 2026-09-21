# Renders the manager theme, the addon cards and the addon panels offscreen (no window, no screen capture) and converts the
# BMPs to PNGs so they can be viewed. The NR panel comes from the test host's shot= mode (see run_hosttest_matrix.py).
#   powershell -File ui_preview.ps1 [-Out C:\path\to\folder] [-Scale 1.25]
param([string]$Out = "$env:TEMP\ui_preview", [double]$Scale = 1.25)
New-Item -ItemType Directory -Force $Out | Out-Null
$root = Split-Path $PSScriptRoot -Parent   # the repository folder
$exe = "$root\manager\build\Release\lsproxy_uipreview.exe"
$dlls = @()   # addon DLLs whose settings panels should be rendered too (Neural Rendering's comes from the test host instead)
& $exe ($Out -replace '\\', '/') $Scale @($dlls | Where-Object { Test-Path $_ }) | Select-Object -Last 8
python "$PSScriptRoot\bmp2png.py" $Out
