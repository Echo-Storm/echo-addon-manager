# Builds Release and assembles dist\EchoAddonManager-<version>-x64.zip: the manager, the addons that build, EchoAddonManagerSetup.exe (one file that carries them all),
# an install note, and the licences.
# The DLSSNR model is never packaged. NVIDIA's NGX library is linked into Neural Rendering and ships under NVIDIA's licence (NOTICE.md), with
# NVIDIA-LICENSE.txt beside it. Neural Rendering is left out (with a note) when it did not build; work-in-progress addons unless -IncludeWip.
#   powershell -File tools\package.ps1 [-Version 0.7.0] [-SkipBuild]
param(
    [string]$Version = '',
    [switch]$SkipBuild,
    [switch]$IncludeWip   # also package addons marked work in progress (the DLSS 4 and FSR 3 Upscalers)
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (-not $Version) {   # default: the release number in version.h
    $Version = (Select-String -Path "$root\manager\sdk\include\eam\version.h" -Pattern 'EAM_VERSION_STRING "([^"]+)"').Matches[0].Groups[1].Value
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
Copy-Item "$root\manager\manager-icon.ico", "$root\manager\manager-icon.png" $stage

$addons = @(
    @{ Id = 'DLSS5NR01'; Dir = "$root\addons\DLSS5NR01"; Bin = "$root\addons\DLSS5NR01\build\Release"; Files = @('DLSS5NR01.dll', 'nvngx.dll_dlss5nr01.dll', 'nr_selftest.exe');
       # NVIDIA's files, under NVIDIA's licence (NOTICE.md): the licence must travel with the binaries that contain NVIDIA's code
       Extra = @{ 'NVIDIA-LICENSE.txt' = "$root\addons\DLSS5NR01\external\ngx\LICENSE.txt" } },
    # DLSS 4 Upscaler: built from the same sources; its addon.json lives in products\DLSS4DLAA, and NVIDIA's DLSS runtime ships in its dlss folder
    @{ Id = 'DLSS4DLAA'; Wip = $true; Dir = "$root\addons\DLSS5NR01\products\DLSS4DLAA"; Bin = "$root\addons\DLSS5NR01\build\Release"; Files = @('DLSS4DLAA.dll');
       Extra = @{ 'NVIDIA-LICENSE.txt' = "$root\addons\DLSS5NR01\external\ngx\LICENSE.txt"; 'LICENSE.txt' = "$root\addons\DLSS5NR01\LICENSE";
                  'dlss\nvngx_dlss.dll' = "$root\addons\DLSS5NR01\external\ngx\bin\nvngx_dlss.dll" } },
    # FSR 3 Upscaler: the same sources again; AMD's FidelityFX runtime (MIT, signed by AMD) ships in its fsr folder (tools\fetch_ffx_sdk.ps1)
    @{ Id = 'FSR3UPSC'; Wip = $true; Dir = "$root\addons\DLSS5NR01\products\FSR3UPSC"; Bin = "$root\addons\DLSS5NR01\build\Release"; Files = @('FSR3UPSC.dll');
       Extra = @{ 'AMD-FidelityFX-LICENSE.txt' = "$root\addons\DLSS5NR01\third_party\ffx\LICENSE.txt"; 'LICENSE.txt' = "$root\addons\DLSS5NR01\LICENSE";
                  'fsr\amd_fidelityfx_dx12.dll' = "$root\addons\DLSS5NR01\external\ffx\bin\amd_fidelityfx_dx12.dll" } }
)
$included = @(); $skipped = @()
foreach ($a in $addons) {
    if ($a.Wip -and -not $IncludeWip) { Write-Host "$($a.Id) is work in progress: not packaged (-IncludeWip packages it)."; continue }
    $missing = @($a.Files | Where-Object { -not (Test-Path "$($a.Bin)\$_") })
    if ($missing.Count) { $skipped += $a.Id; continue }
    $dst = "$stage\addons\$($a.Id)"
    New-Item -ItemType Directory -Force $dst | Out-Null
    foreach ($f in $a.Files) { Copy-Item "$($a.Bin)\$f" $dst }
    Copy-Item "$($a.Dir)\addon.json" $dst
    if (Test-Path "$($a.Dir)\icon.png") { Copy-Item "$($a.Dir)\icon.png" $dst }
    if (Test-Path "$($a.Dir)\LICENSE") { Copy-Item "$($a.Dir)\LICENSE" "$dst\LICENSE.txt" }
    if ($a.Extra) { foreach ($extra in $a.Extra.Keys) { Need $a.Extra[$extra] "$extra for $($a.Id) (run tools\$(if ($a.Id -eq 'FSR3UPSC') { 'fetch_ffx_sdk.ps1' } else { 'fetch_ngx_sdk.ps1' }))"; New-Item -ItemType Directory -Force (Split-Path "$dst\$extra") | Out-Null; Copy-Item $a.Extra[$extra] "$dst\$extra" } }
    $included += $a.Id
}
if (-not ($included -contains 'DLSS5NR01')) { Write-Host 'Neural Rendering did not build: it is not in this package.' }

# The single-file installer: the files above (only the ones that get installed) are packed into a bundle that becomes a resource of Setup.exe.
# It is built in its own folder (installer\build_setup), so the test build of the installer, which carries no files, stays as the tests expect.
$setupBuild = "$root\installer\build_setup"
$payloadDir = "$dist\payload-$Version"
if (Test-Path $payloadDir) { Remove-Item -Recurse -Force $payloadDir }
New-Item -ItemType Directory -Force $payloadDir | Out-Null
Copy-Item "$stage\Lossless.dll", "$stage\manager-icon.ico", "$stage\manager-icon.png" $payloadDir
Copy-Item "$stage\addons" "$payloadDir\addons" -Recurse
if (-not (Test-Path "$setupBuild\CMakeCache.txt")) { & cmake -S "$root\installer" -B $setupBuild -G 'Visual Studio 17 2022' -A x64 | Out-Null }
& cmake --build $setupBuild --config Release --target pack_payload | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'the installer packer did not build' }
$bundle = "$dist\payload-$Version.bin"
& "$setupBuild\Release\pack_payload.exe" $payloadDir $bundle
if ($LASTEXITCODE -ne 0) { throw 'packing the installer files failed' }
& cmake -S "$root\installer" -B $setupBuild "-DSETUP_PAYLOAD=$bundle" | Out-Null
& cmake --build $setupBuild --config Release --target EchoAddonManagerSetup 2>&1 | Select-String -Pattern ' error |warning C' | ForEach-Object { Write-Host $_.Line }
$setupExe = "$setupBuild\Release\EchoAddonManagerSetup.exe"
if (-not (Test-Path $setupExe)) { throw 'the Setup exe did not build' }

# Check the finished exe as a person would use it: it must report the version it carries, and install into a fake Lossless Scaling folder byte for byte
$check = "$env:TEMP\setup_check_$PID"
if (Test-Path $check) { Remove-Item -Recurse -Force $check }
New-Item -ItemType Directory $check | Out-Null
$vlog = "$check\version.log"
$p = Start-Process $setupExe -ArgumentList '--version', '--no-remember', '--log', "`"$vlog`"" -PassThru -Wait -WindowStyle Hidden
$said = if (Test-Path $vlog) { (Get-Content $vlog -Raw).Trim() } else { '' }
if ($p.ExitCode -ne 0 -or $said -ne "payload $Version") { throw "the Setup exe reports '$said' (exit $($p.ExitCode)), expected 'payload $Version'" }
$fakeOriginal = "$root\installer\build\Release\fake_original.dll"
if (Test-Path $fakeOriginal) {
    $fake = "$check\ls"
    New-Item -ItemType Directory $fake | Out-Null
    Copy-Item "$env:SystemRoot\System32\cmd.exe" "$fake\LosslessScaling.exe"
    Copy-Item $fakeOriginal "$fake\Lossless.dll"
    $ilog = "$check\install.log"
    $p = Start-Process $setupExe -ArgumentList '--silent', 'install', '--folder', "`"$fake`"", '--no-remember', '--log', "`"$ilog`"" -PassThru -Wait -WindowStyle Hidden
    if ($p.ExitCode -ne 0) { throw "the Setup exe could not install into a test folder: $(Get-Content $ilog -Raw)" }
    $bad = @(Get-ChildItem $payloadDir -Recurse -File | Where-Object {
        $rel = $_.FullName.Substring($payloadDir.Length + 1)
        (Get-FileHash $_.FullName).Hash -ne (Get-FileHash "$fake\$rel" -ErrorAction SilentlyContinue).Hash })
    if ($bad.Count) { throw "the Setup exe installed files that differ from the package: $($bad.Name -join ', ')" }
    if ((Get-FileHash "$fake\Lossless_original.dll").Hash -ne (Get-FileHash $fakeOriginal).Hash) { throw 'the Setup exe did not keep the original Lossless.dll' }
    Write-Host "  Setup exe checked: reports $Version and installs $((Get-ChildItem $payloadDir -Recurse -File).Count) files byte for byte into a test folder"
} else {
    Write-Host '  (Setup exe installed-files check skipped: build the installer tests first to get the stand-in Lossless.dll)'
}
Remove-Item -Recurse -Force $check -ErrorAction SilentlyContinue
Copy-Item $setupExe "$stage\EchoAddonManagerSetup.exe"
Remove-Item -Recurse -Force $payloadDir

Copy-Item "$root\LICENSE" "$stage\LICENSE.txt"
Copy-Item "$root\NOTICE.md", "$root\DISCLAIMER.md", "$root\CHANGELOG.md" $stage
@"
Echo Addon Manager $Version
=============================

The addon manager for Lossless Scaling. Read DISCLAIMER.md first.

IMPORTANT: DLSS 5 Neural Rendering needs a file you provide yourself. It needs your own copy of nvngx_dlssnr.dll. It is NOT included in this
download, this project does not download it, and it does not say where to get it. Put your copy in the Lossless Scaling folder, next to
LosslessScaling.exe (the addon's "Browse for the model file..." button copies it there for you), then press "Test compatibility" in the addon's
panel. Everything else here works without it.

Tested with World of Warcraft: Forever (the beta; it runs as WowB.exe), Lossless Scaling 3.2.2.0, Windows 11,
RTX 4070 Ti SUPER. Other games and setups are untested.

The manager checks github.com once a day for a newer release of this project (on by default; turn it off in Settings > Updates). It only compares version
numbers: nothing is downloaded or installed, and it is the only thing the manager sends over the internet.

Easiest: run EchoAddonManagerSetup.exe
-------
Close Lossless Scaling, run EchoAddonManagerSetup.exe and follow it: it finds the Lossless Scaling folder (or lets you pick it, for copies that are not
from Steam), installs, updates, repairs the install after a Lossless Scaling update, and uninstalls. Everything it replaces is backed up first, your settings
and other addons are never touched, and it undoes itself if anything goes wrong. Like every file in this project it is unsigned, so Windows SmartScreen may
warn you ("More info", then "Run anyway"); it runs without administrator rights unless the Lossless Scaling folder needs them. Or do it by hand:

Install by hand (Lossless Scaling 3.2.2.0 was the tested version)
-------
1. Close Lossless Scaling and open its folder, for example
   C:\Program Files (x86)\Steam\steamapps\common\Lossless Scaling
2. First time only: rename the original Lossless.dll to Lossless_original.dll. Keep it: the manager forwards to it.
3. Copy Lossless.dll, manager-icon.ico, manager-icon.png and the addons folder from this zip into that folder.
4. Start Lossless Scaling. The manager window opens by itself.
5. DLSS 5 Neural Rendering also needs nvngx_dlssnr.dll next to LosslessScaling.exe. It is not included and this project does not say where to
   find it. ReShade input passthrough and Windowed mode are built into the manager (its Features tab); they arrive switched off.
   If you used the old separate ReShade or Windowed addon folders, the manager ignores them; you can remove them.

Updating: close Lossless Scaling and copy the new files over the old ones. Your settings (addons\config.json) carry over.
From 0.1.0: Neural Rendering is now addons\DLSS5NR01 (it was addons\LSP-NeuralRender) and its saved settings and looks move to the new name by themselves
the first time it starts. ReShade passthrough and Windowed mode are built in (Features tab). The old LSP-NeuralRender, LSP-ReShade and LSP-Windowed
folders are ignored by the manager; you can remove them.
After a Lossless Scaling update: it may put its own Lossless.dll back. Run EchoAddonManagerSetup.exe again (it offers "Repair"), or by hand: delete the stale Lossless_original.dll, rename the new
Lossless.dll to Lossless_original.dll, and copy ours in again.

Uninstall: run EchoAddonManagerSetup.exe and choose Uninstall, or by hand: delete our Lossless.dll, rename Lossless_original.dll back to Lossless.dll, and delete the addons folder if you like.

Licence: MIT (LICENSE.txt). Credits and third-party licences: NOTICE.md.
"@ | Set-Content -Encoding UTF8 "$stage\INSTALL.txt"

$zip = "$dist\$name.zip"
if (Test-Path $zip) { Remove-Item $zip }
# Entry by entry, with "/" in the names: Compress-Archive and ZipFile.CreateFromDirectory in Windows PowerShell write backslashes as path
# separators, which the zip format does not allow and unzip tools on other systems complain about.
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$fileStream = [IO.File]::Create($zip)
$archive = New-Object IO.Compression.ZipArchive($fileStream, [IO.Compression.ZipArchiveMode]::Create)
Get-ChildItem $stage -Recurse -File | ForEach-Object {
    $entryName = $_.FullName.Substring($stage.Length + 1).Replace('\', '/')
    [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $_.FullName, $entryName, [IO.Compression.CompressionLevel]::Optimal)
}
$archive.Dispose()
$fileStream.Dispose()
Write-Host "package: $zip"
Write-Host "  addons included: $($included -join ', ')"
if ($skipped.Count) { Write-Host "  addons NOT included (not built): $($skipped -join ', ')" }
Get-ChildItem $stage -Recurse -File | ForEach-Object { '  ' + $_.FullName.Substring($stage.Length + 1) }
