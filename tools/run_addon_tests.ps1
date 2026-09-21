# Builds and runs the offline tests that need no GPU load, no game and no visible window:
#   * lsproxy_installtest        installing an addon from a folder, a zip or a lone DLL (temporary folder only)
#   * lsproxy_coretest           the manager's addon handling: scan, manifests, load, init, switch, remove, install, security, a faulting addon
#   * lsproxy_featurestest       the built-in features: ReShade passthrough on a hidden window (subclass, restore, a layered subclass), and Windowed
#                                mode's virtual display through DXGI and user32 (run twice: switched on and off at start-up)
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
Build "$root\manager\build" @('lsproxy_installtest', 'lsproxy_coretest', 'lsproxy_featurestest')

Run 'install' "$root\manager\build\Release\lsproxy_installtest.exe" @()
Run 'core (addon handling)' "$root\manager\build\Release\lsproxy_coretest.exe" @()
Run 'features (Windowed on at start-up)' "$root\manager\build\Release\lsproxy_featurestest.exe" @()
Run 'features (Windowed off at start-up)' "$root\manager\build\Release\lsproxy_featurestest.exe" @('off')
if ($fail) { Write-Host "$fail test program(s) failed"; exit 1 } else { Write-Host 'all addon tests passed' }
