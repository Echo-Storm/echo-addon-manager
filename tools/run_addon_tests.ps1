# Builds and runs the offline tests that need no game (the window test needs a GPU and shows the window for a moment):
#   * lsproxy_installtest        installing an addon from a folder, a zip or a lone DLL (temporary folder only)
#   * lsproxy_coretest           the manager's addon handling: scan, manifests, load, init, switch, remove, install, security, a faulting addon
#   * lsproxy_featurestest       the built-in features: ReShade passthrough on a hidden window (subclass, restore, a layered subclass), and Windowed
#                                mode's virtual display through DXGI and user32 (run twice: switched on and off at start-up)
#   * lsproxy_updatetest        the update check: version numbers, GitHub's answer, when a check is due, and the check itself against a small local server
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
Build "$root\manager\build" @('lsproxy_installtest', 'lsproxy_coretest', 'lsproxy_featurestest', 'lsproxy_guitest', 'lsproxy_updatetest')

Run 'install' "$root\manager\build\Release\lsproxy_installtest.exe" @()
Run 'core (addon handling)' "$root\manager\build\Release\lsproxy_coretest.exe" @()
Run 'features (Windowed on at start-up)' "$root\manager\build\Release\lsproxy_featurestest.exe" @()
Run 'features (Windowed off at start-up)' "$root\manager\build\Release\lsproxy_featurestest.exe" @('off')
# The process ends while a background thread is still running and nothing shut it down, as can happen when Lossless Scaling exits: the exit code
# must be 0 (a std::thread still joinable at that point crashes the process)
Run 'exit with the ReShade watcher running' "$root\manager\build\Release\lsproxy_featurestest.exe" @('abrupt')
Run 'exit with the GPU sampler running' "$root\manager\build\Release\lsproxy_coretest.exe" @('abrupt-gpu')
Run 'update check (against a local server, no internet needed)' "$root\manager\build\Release\lsproxy_updatetest.exe" @()
Run 'window (defaults)' "$root\manager\build\Release\lsproxy_guitest.exe" @()
Run 'window (saved placement)' "$root\manager\build\Release\lsproxy_guitest.exe" @('place')
Run 'window (saved placement, interface size 150 %)' "$root\manager\build\Release\lsproxy_guitest.exe" @('scaled')
# The installer's core (find the folder, tell whose Lossless.dll is whose, install / update / repair / uninstall with rollback), on fake folders in %TEMP%.
# It needs the manager's Lossless.dll, built above by build_all.ps1.
if (-not (Test-Path "$root\installer\build\CMakeCache.txt")) { & cmake -S "$root\installer" -B "$root\installer\build" -G 'Visual Studio 17 2022' -A x64 2>&1 | Select-String -Pattern 'error' | ForEach-Object { Write-Host $_.Line } }
Build "$root\installer\build" @('setup_core', 'setup_cli', 'setup_test', 'setup_payload_test', 'pack_payload', 'EchoAddonManagerSetup')
Run 'installer core (fake Lossless Scaling folders)' "$root\installer\build\Release\setup_test.exe" @()
Run 'installer file bundle (pack, unpack, hostile and damaged bundles)' "$root\installer\build\Release\setup_payload_test.exe" @()
# The Setup exe itself, end to end on fake folders: silent install / reinstall / repair / uninstall / refusals, and the wizard window opening and closing by itself
Write-Host '== Setup exe (fake Lossless Scaling folders, silent mode and the window)'
$out = & powershell -NoProfile -File "$root\installer\tests\setup_exe_test.ps1" -Root $root 2>&1 | Out-String
$out.TrimEnd() -split "`r?`n" | Where-Object { $_ -match '^\s*(PASS|FAIL)|PASSED|FAILED' } | ForEach-Object { Write-Host "  $_" }
if ($LASTEXITCODE -ne 0) { $script:fail++ }
# Neural Rendering's requirements check: only when that addon has been configured (it needs the NVIDIA SDK to configure, though not to run this test)
if (Test-Path "$root\addons\DLSS5NR01\build\CMakeCache.txt") {
    Build "$root\addons\DLSS5NR01\build" @('nr_reqtest')
    Run 'Neural Rendering requirements check' "$root\addons\DLSS5NR01\build\Release\nr_reqtest.exe" @()
}
if ($fail) { Write-Host "$fail test program(s) failed"; exit 1 } else { Write-Host 'all addon tests passed' }
