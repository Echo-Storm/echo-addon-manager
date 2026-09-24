# Copies built files into the Lossless Scaling folder, with the safety rules learned the hard way:
#   * refuses while the game (WowB.exe, or whatever -Game names) is running: replacing files under a running session is how
#     sessions get lost;
#   * refuses while Lossless Scaling itself is running (it holds the DLLs open), unless -StopLS is given AND the game is not running;
#   * backs the old file up into <LS folder>\backups\ (never deletes) with a timestamp before replacing it.
#   powershell -File deploy.ps1 -What host|nr|all [-StopLS] [-LsDir '<Lossless Scaling folder>']   (or set the LS_DIR environment variable) [-Game WowB]
param(
    [Parameter(Mandatory = $true)][ValidateSet('host', 'nr', 'all')][string]$What,
    [switch]$StopLS,
    [string]$LsDir = $(if ($env:LS_DIR) { $env:LS_DIR } else { 'C:\Program Files (x86)\Steam\steamapps\common\Lossless Scaling' }),
    [string]$Game = 'WowB'
)
$root = Split-Path $PSScriptRoot -Parent   # the repository folder
$items = @{
    host     = @{ Src = "$root\manager\build\Release\Lossless.dll"; Dst = "$LsDir\Lossless.dll"; Extra = @("$root\manager\manager-icon.ico", "$root\manager\manager-icon.png") }
    nr       = @{ Src = "$root\addons\DLSS5NR01\build\Release\DLSS5NR01.dll"; Dst = "$LsDir\addons\DLSS5NR01\DLSS5NR01.dll"; Extra = @("$root\addons\DLSS5NR01\build\Release\nvngx.dll_dlss5nr01.dll", "$root\addons\DLSS5NR01\build\Release\nr_selftest.exe", "$root\addons\DLSS5NR01\addon.json",
                  @{ Src = "$root\addons\DLSS5NR01\build\Release\dlss\nvngx_dlss.dll"; Rel = 'dlss\nvngx_dlss.dll' }, @{ Src = "$root\addons\DLSS5NR01\external\ngx\LICENSE.txt"; Rel = 'NVIDIA-LICENSE.txt' }) }
}
if (-not (Test-Path "$LsDir\Lossless.dll")) { Write-Host "No Lossless Scaling folder at $LsDir (pass -LsDir or set LS_DIR)."; exit 4 }
if (Get-Process $Game -ErrorAction SilentlyContinue) { Write-Host "$Game is running: not touching the Lossless Scaling folder. Close the game first."; exit 2 }
$ls = Get-Process LosslessScaling -ErrorAction SilentlyContinue
if ($ls) {
    if (-not $StopLS) { Write-Host 'Lossless Scaling is running (it holds the DLLs). Close it, or pass -StopLS.'; exit 3 }
    Stop-Process -Id $ls.Id; Start-Sleep 2
}
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$names = if ($What -eq 'all') { @('host', 'nr') } else { @($What) }
foreach ($n in $names) {
    $it = $items[$n]
    if (-not (Test-Path $it.Src)) { Write-Host "[$n] not built: $($it.Src)"; continue }
    $files = @(@{ Src = $it.Src; Dst = $it.Dst })
    # an extra file goes next to the main one, or (given as @{ Src; Rel }) into a subfolder of it
    foreach ($e in @($it.Extra)) {
        if ($e -is [hashtable]) { if (Test-Path $e.Src) { $files += @{ Src = $e.Src; Dst = Join-Path (Split-Path $it.Dst) $e.Rel } } }
        elseif ($e) { $files += @{ Src = $e; Dst = Join-Path (Split-Path $it.Dst) (Split-Path $e -Leaf) } }
    }
    foreach ($f in $files) {
        New-Item -ItemType Directory -Force (Split-Path $f.Dst) | Out-Null
        if (Test-Path $f.Dst) {
            $bk = "$LsDir\backups"; New-Item -ItemType Directory -Force $bk | Out-Null
            Copy-Item $f.Dst "$bk\$([IO.Path]::GetFileNameWithoutExtension($f.Dst))-$stamp$([IO.Path]::GetExtension($f.Dst))"
        }
        Copy-Item $f.Src $f.Dst -Force
        Write-Host "[$n] $($f.Dst)"
    }
}
# ReShade passthrough and Windowed mode are built into the manager now. If the old standalone addon folders are still in addons\, move them
# aside (never delete): the manager ignores them, but there is no reason to leave them.
if ($names -contains 'host') {
    foreach ($old in 'LSP-ReShade', 'LSP-Windowed') {
        $from = "$LsDir\addons\$old"
        if (Test-Path $from) {
            $aside = "$LsDir\backups\retired-addons-$stamp"
            New-Item -ItemType Directory -Force $aside | Out-Null
            Move-Item $from "$aside\$old"
            Write-Host "[host] moved the retired addon folder $old to $aside"
        }
    }
    # the icons were called LP-icon.ico / .png up to 0.7.4 (now manager-icon): move the old ones aside
    foreach ($old in 'LP-icon.ico', 'LP-icon.png') {
        if (Test-Path "$LsDir\$old") {
            New-Item -ItemType Directory -Force "$LsDir\backups" | Out-Null
            Move-Item "$LsDir\$old" "$LsDir\backups\$([IO.Path]::GetFileNameWithoutExtension($old))-$stamp$([IO.Path]::GetExtension($old))"
            Write-Host "[host] moved the old $old to the backups"
        }
    }
}
# Neural Rendering's helper DLL was called nvngx.dll_lspnr.dll before 0.2.1; the addon no longer loads it. Move a stale copy aside.
if ($names -contains 'nr') {
    $stale = "$LsDir\addons\DLSS5NR01\nvngx.dll_lspnr.dll"
    if (Test-Path $stale) {
        $aside = "$LsDir\backups\retired-addons-$stamp"
        New-Item -ItemType Directory -Force $aside | Out-Null
        Move-Item $stale "$aside\nvngx.dll_lspnr.dll"
        Write-Host "[nr] moved the old helper DLL nvngx.dll_lspnr.dll to $aside"
    }
}
if ($ls) { Write-Host 'Lossless Scaling was stopped; start it again yourself.' }
