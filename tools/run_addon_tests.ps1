# Builds and runs the offline tests (no game needed; the window test needs a GPU and shows a window for a moment). By default only the suites
# that the files changed since the last commit can affect; -All runs every suite, -Only names them.
#
#   powershell -File tools\run_addon_tests.ps1                  the suites for what changed (git diff against HEAD, plus new files)
#   powershell -File tools\run_addon_tests.ps1 -Only core,gui   these suites
#   powershell -File tools\run_addon_tests.ps1 -All             every suite, as before a release (and the full Neural Rendering matrix)
#   powershell -File tools\run_addon_tests.ps1 -List            the suites and what starts each
#
# Suites:
#   core       eam_coretest (addon handling, settings, host interface, the frozen 1.0 interface, hooks) and eam_installtest (installing addons,
#              backups, diagnostics); also the exit with the GPU sampler running
#   features   eam_featurestest: ReShade passthrough and Windowed mode (switched on and off at start-up), and the exit with its watcher running
#   sample     eam_sampletest: examples\SampleAddon loaded, started and drawn by the real manager
#   update     eam_updatetest: the update check against a small local server
#   gui        eam_guitest: the manager window, three times (defaults, a saved placement, interface size 150 %)
#   installer  the Setup program's core and its file bundle, on fake Lossless Scaling folders
#   setupexe   the Setup exe end to end (silent install, repair, uninstall, the wizard window)
#   nr         Neural Rendering's requirements check and frame tap, and the quick set of its model scenarios (needs the NVIDIA SDK configured and
#              LS_DIR pointing at a Lossless Scaling folder with nvngx_dlssnr.dll); -All runs every scenario
param([string[]]$Only = @(), [switch]$All, [switch]$List)
$root = Split-Path $PSScriptRoot -Parent   # the repository folder
$mgr = "$root\manager\build\Release"
$nrBuild = "$root\addons\DLSS5NR01\build"

# name, the changed paths that start it (regular expressions on forward-slash paths), and what it builds and runs
$suites = [ordered]@{
    core      = @{ When = '^manager/(src|sdk)/|^manager/CMakeLists|^manager/tools/(core_test|test_addon|install_test|abi_)';
                   Build = @('manager', 'eam_coretest', 'eam_installtest');
                   Runs = @(@('core (addon handling, host, hooks)', "$mgr\eam_coretest.exe", @()), @('exit with the GPU sampler running', "$mgr\eam_coretest.exe", @('abrupt-gpu')),
                            @('install (addons, backups, diagnostics)', "$mgr\eam_installtest.exe", @())) }
    features  = @{ When = '^manager/src/(features|core|config)/|^manager/CMakeLists|^manager/tools/features_test';
                   Build = @('manager', 'eam_featurestest');
                   Runs = @(@('features (Windowed on at start-up)', "$mgr\eam_featurestest.exe", @()), @('features (Windowed off at start-up)', "$mgr\eam_featurestest.exe", @('off')),
                            @('exit with the ReShade watcher running', "$mgr\eam_featurestest.exe", @('abrupt'))) }
    sample    = @{ When = '^manager/sdk/|^manager/src/(addon|host|config)/|^examples/|^manager/tools/sample_test';
                   Build = @('manager', 'eam_sampletest');
                   Runs = @(, @('sample addon (examples\SampleAddon)', "$mgr\eam_sampletest.exe", @())) }
    update    = @{ When = '^manager/src/update/|^manager/sdk/include/eam/version\.h|^manager/tools/update_test';
                   Build = @('manager', 'eam_updatetest');
                   Runs = @(, @('update check (local server)', "$mgr\eam_updatetest.exe", @())) }
    gui       = @{ When = '^manager/src/gui/|^manager/sdk/include/eam/(widgets|icons)\.h|^manager/tools/gui_test';
                   Build = @('manager', 'eam_guitest');
                   Runs = @(@('window (defaults)', "$mgr\eam_guitest.exe", @()), @('window (saved placement)', "$mgr\eam_guitest.exe", @('place')),
                            @('window (saved placement, interface size 150 %)', "$mgr\eam_guitest.exe", @('scaled'))) }
    installer = @{ When = '^installer/src/core/|^installer/tests/(installer_test|payload_test)|^installer/CMakeLists|^manager/sdk/include/eam/version\.h';
                   Build = @('installer', 'setup_core', 'setup_test', 'setup_payload_test');
                   Runs = @(@('installer core (fake Lossless Scaling folders)', "$root\installer\build\Release\setup_test.exe", @()),
                            @('installer file bundle', "$root\installer\build\Release\setup_payload_test.exe", @())) }
    setupexe  = @{ When = '^installer/src/|^installer/tests/setup_exe_test|^installer/CMakeLists|^tools/package';
                   Build = @('installer', 'setup_core', 'setup_cli', 'pack_payload', 'EchoAddonManagerSetup');
                   Script = "$root\installer\tests\setup_exe_test.ps1" }
    nr        = @{ When = '^addons/DLSS5NR01/(src|tools|CMakeLists)|^manager/sdk/|^tools/run_hosttest_matrix';
                   Build = @('nr', 'nr_reqtest', 'nr_taptest', 'DLSS5NR01', 'nr_hosttest', 'nr_selftest');
                   Runs = @(@('Neural Rendering requirements check', "$nrBuild\Release\nr_reqtest.exe", @()),
                            @('Neural Rendering frame tap', "$nrBuild\Release\nr_taptest.exe", @()));
                   Matrix = $true }
}
$buildDirs = @{ manager = "$root\manager\build"; installer = "$root\installer\build"; nr = $nrBuild }

if ($List) { foreach ($n in $suites.Keys) { Write-Host ("{0,-10} when {1}" -f $n, $suites[$n].When) }; exit 0 }

# which suites
$chosen = @()
if ($All) { $chosen = @($suites.Keys) }
elseif ($Only.Count) { $chosen = @($Only | ForEach-Object { $_ -split ',' } | Where-Object { $_ }) }
else {
    $changed = @(git -C $root diff --name-only HEAD) + @(git -C $root ls-files --others --exclude-standard) | Where-Object { $_ -and $_ -notmatch '\.md$' }
    foreach ($n in $suites.Keys) { if ($changed | Where-Object { $_ -match $suites[$n].When }) { $chosen += $n } }
    if (-not $chosen.Count) { Write-Host 'Nothing changed that a test covers (documentation only, or nothing at all). -All runs everything.'; exit 0 }
}
foreach ($n in $chosen) { if (-not $suites.Contains($n)) { Write-Host "No suite called '$n' (-List shows them)."; exit 2 } }
Write-Host ("suites: {0}" -f ($chosen -join ', '))

$fail = 0
function Run($name, $exe, $testArgs) {
    Write-Host "== $name"
    if (-not (Test-Path $exe)) { Write-Host "  FAIL  not built: $exe"; $script:fail++; return }
    $out = & $exe @testArgs 2>&1 | Out-String
    $out.TrimEnd() -split "`r?`n" | Where-Object { $_ -cmatch '^\s*FAIL\b|FAILED' } | ForEach-Object { Write-Host "  $_" }
    if ($LASTEXITCODE -ne 0) { $script:fail++; Write-Host "  FAILED (exit $LASTEXITCODE)" }
}
function Build([string]$which, [string[]]$targets) {
    $dir = $buildDirs[$which]
    if ($which -eq 'nr' -and -not (Test-Path "$dir\CMakeCache.txt")) { Write-Host '  (Neural Rendering is not configured here: it needs the NVIDIA SDK; skipped)'; return $false }
    if ($which -eq 'installer' -and -not (Test-Path "$dir\CMakeCache.txt")) { & cmake -S "$root\installer" -B $dir -G 'Visual Studio 17 2022' -A x64 2>&1 | Select-String -Pattern 'error' | ForEach-Object { Write-Host $_.Line } }
    & cmake --build $dir --config Release --target @targets 2>&1 | Select-String -Pattern ' error ' | ForEach-Object { Write-Host $_.Line }
    return $true
}

$total = [Diagnostics.Stopwatch]::StartNew()
foreach ($n in $chosen) {
    $s = $suites[$n]; $clock = [Diagnostics.Stopwatch]::StartNew()
    Write-Host "#### $n"
    if (-not (Build $s.Build[0] @($s.Build | Select-Object -Skip 1))) { continue }
    foreach ($r in @($s.Runs)) { if ($r) { Run $r[0] $r[1] $r[2] } }
    if ($s.Script) {
        Write-Host '== Setup exe end to end'
        $out = & powershell -NoProfile -File $s.Script -Root $root 2>&1 | Out-String
        $out.TrimEnd() -split "`r?`n" | Where-Object { $_ -cmatch '^\s*FAIL\b|FAILED' } | ForEach-Object { Write-Host "  $_" }
        if ($LASTEXITCODE -ne 0) { $fail++; Write-Host "  FAILED (exit $LASTEXITCODE)" }
    }
    if ($s.Matrix) {
        if (-not $env:LS_DIR) { Write-Host '  (model scenarios skipped: set LS_DIR to a Lossless Scaling folder with nvngx_dlssnr.dll)' }
        else {
            [string[]]$matrixArgs = @("$root\tools\run_hosttest_matrix.py") + $(if ($All) { @() } else { @('--quick') })
            Write-Host ('== Neural Rendering model scenarios ({0})' -f $(if ($All) { 'all' } else { 'quick set' }))
            $out = & python $matrixArgs 2>&1 | Out-String
            $out.TrimEnd() -split "`r?`n" | Where-Object { $_ -cmatch '^\s*FAIL\b|SCENARIO|rror' } | ForEach-Object { Write-Host "  $_" }
            if ($LASTEXITCODE -ne 0) { $fail++; Write-Host "  FAILED (exit $LASTEXITCODE)" }
        }
    }
    Write-Host ("   {0}: {1:0} s" -f $n, $clock.Elapsed.TotalSeconds)
}
Write-Host ("{0:0} s in all" -f $total.Elapsed.TotalSeconds)
if ($fail) { Write-Host "$fail test program(s) failed"; exit 1 } else { Write-Host 'all addon tests passed' }
