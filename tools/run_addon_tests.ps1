# Builds and runs the offline tests that need no GPU load, no game and no visible window:
#   * lsproxy_installtest        installing an addon from a folder, a zip or a lone DLL (temporary folder only)
#   * reshade_lifecycle_test     LSP-ReShade subclasses a window off-screen, restores it, and pins itself when it cannot
#   * windowed_proxy_test on/off LSP-Windowed adds the virtual display only while enabled (DXGI + EnumDisplayMonitors in-process)
#   powershell -File run_addon_tests.ps1
$root = Split-Path $PSScriptRoot -Parent   # the repository folder
$fail = 0
function Run($name, $exe, $testArgs) {
    Write-Host "== $name"
    $out = & $exe @testArgs 2>&1 | Out-String
    $out.TrimEnd() -split "`r?`n" | Where-Object { $_ -match '^(PASS|FAIL)|PASSED|FAILED' } | ForEach-Object { Write-Host "  $_" }
    if ($LASTEXITCODE -ne 0) { $script:fail++ }
}
function Build($dir, $targets) {
    Push-Location $dir
    & cmake --build . --config Release --target @targets 2>&1 | Select-String -Pattern ' error ' | ForEach-Object { Write-Host $_.Line }
    Pop-Location
}
Build "$root\manager\build" @('lsproxy_installtest')
Build "$root\addons\LSP-ReShade\build" @('LSP_ReShade', 'reshade_lifecycle_test')
Build "$root\addons\LSP-Windowed\build" @('LSP_Windowed', 'windowed_proxy_test')

Run 'install' "$root\manager\build\Release\lsproxy_installtest.exe" @()
Run 'reshade lifecycle' "$root\addons\LSP-ReShade\build\Release\reshade_lifecycle_test.exe" @("$root\addons\LSP-ReShade\build\Release\LSP_ReShade.dll")
$wdll = "$root\addons\LSP-Windowed\build\LSP_Windowed\Release\LSP_Windowed.dll"
Run 'windowed, enabled' "$root\addons\LSP-Windowed\build\Release\windowed_proxy_test.exe" @($wdll, 'on')
Run 'windowed, disabled' "$root\addons\LSP-Windowed\build\Release\windowed_proxy_test.exe" @($wdll, 'off')
if ($fail) { Write-Host "$fail test program(s) failed"; exit 1 } else { Write-Host 'all addon tests passed' }
