# Builds everything in the project: the manager (Lossless.dll), the three addons, the offscreen UI preview and the offline
# test host. Release x64. Nothing is deployed; see deploy.ps1.
#   powershell -File build_all.ps1 [-Only host,nr,reshade,windowed]
param([string[]]$Only = @())
$Only = @($Only | ForEach-Object { $_ -split "," } | Where-Object { $_ })   # accepts -Only a,b when started with -File
$root = Split-Path $PSScriptRoot -Parent   # the repository folder
$targets = @(
    @{ Name = 'host';     Dir = "$root\manager\build";                 Args = @('--target', 'Lossless', '--target', 'lsproxy_uipreview') },
    @{ Name = 'reshade';  Dir = "$root\addons\LSP-ReShade\build";            Args = @() },
    @{ Name = 'windowed'; Dir = "$root\addons\LSP-Windowed\build";           Args = @() },
    @{ Name = 'nr';       Dir = "$root\addons\LSP-NeuralRender\build";                            Args = @() }
)
$failed = 0
foreach ($t in $targets) {
    if ($Only.Count -and ($Only -notcontains $t.Name)) { continue }
    if (-not (Test-Path "$($t.Dir)\CMakeCache.txt")) {   # first time: configure (fetches Dear ImGui and MinHook at pinned versions)
        Write-Host "[$($t.Name)] configuring"
        & cmake -S (Split-Path $t.Dir -Parent) -B $t.Dir -G 'Visual Studio 17 2022' -A x64 2>&1 | Select-String -Pattern 'error|Error' | ForEach-Object { $_.Line }
        if ($LASTEXITCODE -ne 0) { $failed++; continue }
    }
    Write-Host "== $($t.Name)"
    Push-Location $t.Dir
    & cmake --build . --config Release @($t.Args) 2>&1 | Select-String -Pattern 'error|warning C4|vcxproj ->' | ForEach-Object { $_.Line }
    if ($LASTEXITCODE -ne 0) { $failed++ }
    Pop-Location
}
if ($failed) { Write-Host "$failed target(s) failed"; exit 1 } else { Write-Host 'all built' }
