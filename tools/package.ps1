# Builds Release and assembles dist\EchoAddonManager-<version>-x64.zip: the manager, the addons that build, an install note, and the licences.
# NVIDIA's DLSS SDK and the DLSSNR snippet are never packaged. Neural Rendering is left out (with a note) when it did not build.
#   powershell -File tools\package.ps1 [-Version 0.1.0] [-SkipBuild]
param(
    [string]$Version = '',
    [switch]$SkipBuild
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (-not $Version) {   # default: the release number in version.h
    $Version = (Select-String -Path "$root\manager\sdk\include\lsproxy\version.h" -Pattern 'LSPROXY_VERSION_STRING "([^"]+)"').Matches[0].Groups[1].Value
}
if (-not $SkipBuild) {
    & powershell -NoProfile -File "$PSScriptRoot\build_all.ps1" -Only host,nr
    if ($LASTEXITCODE -ne 0) { Write-Host 'Some target did not build (Neural Rendering needs the NVIDIA SDK in external\ngx); packaging what is there.' }
}

$name = "EchoAddonManager-$Version-x64"
$dist = "$root\dist"
$stage = "$dist\$name"
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force "$stage\addons" | Out-Null

function Need($path, $what) { if (-not (Test-Path $path)) { throw "missing build output: $what ($path)" } }
Need "$root\manager\build\Release\Lossless.dll" 'the manager'
Copy-Item "$root\manager\build\Release\Lossless.dll" $stage
Copy-Item "$root\manager\LP-icon.ico", "$root\manager\LP-icon.png" $stage

$addons = @(
    @{ Id = 'DLSS5NR01'; Dir = "$root\addons\DLSS5NR01"; Bin = "$root\addons\DLSS5NR01\build\Release"; Files = @('DLSS5NR01.dll', 'nvngx.dll_lspnr.dll') }
)
$included = @(); $skipped = @()
foreach ($a in $addons) {
    $missing = @($a.Files | Where-Object { -not (Test-Path "$($a.Bin)\$_") })
    if ($missing.Count) { $skipped += $a.Id; continue }
    $dst = "$stage\addons\$($a.Id)"
    New-Item -ItemType Directory -Force $dst | Out-Null
    foreach ($f in $a.Files) { Copy-Item "$($a.Bin)\$f" $dst }
    Copy-Item "$($a.Dir)\addon.json" $dst
    if (Test-Path "$($a.Dir)\icon.png") { Copy-Item "$($a.Dir)\icon.png" $dst }
    if (Test-Path "$($a.Dir)\LICENSE") { Copy-Item "$($a.Dir)\LICENSE" "$dst\LICENSE.txt" }
    $included += $a.Id
}
if (-not ($included -contains 'DLSS5NR01')) { Write-Host 'Neural Rendering did not build: it is not in this package.' }

Copy-Item "$root\LICENSE" "$stage\LICENSE.txt"
Copy-Item "$root\NOTICE.md", "$root\DISCLAIMER.md", "$root\CHANGELOG.md" $stage
@"
Echo Addon Manager $Version
=============================

The addon manager for Lossless Scaling. Read DISCLAIMER.md first.

Install (Lossless Scaling 3.2.2.0 was the tested version)
-------
1. Close Lossless Scaling and open its folder, for example
   C:\Program Files (x86)\Steam\steamapps\common\Lossless Scaling
2. First time only: rename the original Lossless.dll to Lossless_original.dll. Keep it: the manager forwards to it.
3. Copy Lossless.dll, LP-icon.ico, LP-icon.png and the addons folder from this zip into that folder.
4. Start Lossless Scaling. The manager window opens by itself.
5. DLSS 5 Neural Rendering also needs nvngx_dlssnr.dll next to LosslessScaling.exe. It is not included and this project does not say where to
   find it. ReShade input passthrough and Windowed mode are built into the manager (its Features tab); they arrive switched off.
   If you used the old separate ReShade or Windowed addon folders, the manager ignores them; you can remove them.

Updating: close Lossless Scaling and copy the new files over the old ones. Your settings (addons\config.json) carry over.
After a Lossless Scaling update: it may put its own Lossless.dll back. Delete the stale Lossless_original.dll, rename the new
Lossless.dll to Lossless_original.dll, and copy ours in again.

Uninstall: delete our Lossless.dll, rename Lossless_original.dll back to Lossless.dll, and delete the addons folder if you like.

Licence: MIT (LICENSE.txt). Credits and third-party licences: NOTICE.md.
"@ | Set-Content -Encoding UTF8 "$stage\INSTALL.txt"

$zip = "$dist\$name.zip"
if (Test-Path $zip) { Remove-Item $zip }
Compress-Archive -Path "$stage\*" -DestinationPath $zip
Write-Host "package: $zip"
Write-Host "  addons included: $($included -join ', ')"
if ($skipped.Count) { Write-Host "  addons NOT included (not built): $($skipped -join ', ')" }
Get-ChildItem $stage -Recurse -File | ForEach-Object { '  ' + $_.FullName.Substring($stage.Length + 1) }
