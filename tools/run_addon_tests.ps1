# Builds and runs the offline tests that need no game (the window test needs a GPU and shows the window for a moment):
#   * lsproxy_installtest        installing an addon from a folder, a zip or a lone DLL (temporary folder only)
#   * lsproxy_coretest           the manager's addon handling: scan, manifests, load, init, switch, remove, install, security, a faulting addon
#   * lsproxy_featurestest       the built-in features: ReShade passthrough on a hidden window (subclass, restore, a layered subclass), and Windowed
#                                mode's virtual display through DXGI and user32 (run twice: switched on and off at start-up)
#   * lsproxy_guitest            the manager window: hidden start, show and hide, the hotkey message, close to the tray, saved placement (run three
#                                times: plain, with a saved placement, with the interface size at 150 %), teardown; and the pure window logic
#   powershell -File run_addon_tests.ps1
$root = Split-Path $PSScriptRoot -Parent   # the repository folder
$fail = 0
function Run($name, $exe, $testArgs) {
    Write-Host "== $name"
    if (-not (Test-Path $exe)) { Write-Host "  FAIL  not built: $exe"; $script:fail++; return }
    $out = & $exe @testArgs 2>&1 | Out-String
    $out.TrimEnd() -split "`r?`n" | Where-Object { $_ -match '^(PASS|FAIL)|PASSED|FAILED' } | ForEach-Object { Write-Host "  $_" }
    if ($LASTEXITCODE -ne 0) { $script:fail++ }
}
function Build($dir, $targets) {
    Push-Location $dir
    & cmake --build . --config Release --target @targets 2>&1 | Select-String -Pattern ' error ' | ForEach-Object { Write-Host $_.Line }
    Pop-Location
}
Build "$root\manager\build" @('lsproxy_installtest', 'lsproxy_coretest', 'lsproxy_featurestest', 'lsproxy_guitest')

Run 'install' "$root\manager\build\Release\lsproxy_installtest.exe" @()
Run 'core (addon handling)' "$root\manager\build\Release\lsproxy_coretest.exe" @()
Run 'features (Windowed on at start-up)' "$root\manager\build\Release\lsproxy_featurestest.exe" @()
Run 'features (Windowed off at start-up)' "$root\manager\build\Release\lsproxy_featurestest.exe" @('off')
# The process ends while a background thread is still running and nothing shut it down, as can happen when Lossless Scaling exits: the exit code
# must be 0 (a std::thread still joinable at that point crashes the process)
Run 'exit with the ReShade watcher running' "$root\manager\build\Release\lsproxy_featurestest.exe" @('abrupt')
Run 'exit with the GPU sampler running' "$root\manager\build\Release\lsproxy_coretest.exe" @('abrupt-gpu')
Run 'window (defaults)' "$root\manager\build\Release\lsproxy_guitest.exe" @()
Run 'window (saved placement)' "$root\manager\build\Release\lsproxy_guitest.exe" @('place')
Run 'window (saved placement, interface size 150 %)' "$root\manager\build\Release\lsproxy_guitest.exe" @('scaled')
# Neural Rendering's requirements check: only when that addon has been configured (it needs the NVIDIA SDK to configure, though not to run this test)
if (Test-Path "$root\addons\DLSS5NR01\build\CMakeCache.txt") {
    Build "$root\addons\DLSS5NR01\build" @('nr_reqtest')
    Run 'Neural Rendering requirements check' "$root\addons\DLSS5NR01\build\Release\nr_reqtest.exe" @()
}
if ($fail) { Write-Host "$fail test program(s) failed"; exit 1 } else { Write-Host 'all addon tests passed' }
