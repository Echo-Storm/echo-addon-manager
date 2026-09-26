# Fetches the FSR 4 runtime the FSR Upscaler offers as a second choice in the manager's Runtimes list: FSR 4.1.1b INT8 with the RDNA 2
# fix, the OptiScaler team's build of AMD's FSR 4 upscaler (their Discord; the GitHub release below is a repost, byte for byte the same
# file: its SHA-256 matches the one they published). It runs FSR 4 on graphics cards AMD's own FSR 4 does not support.
#
# It is AMD's upscaler library (amd_fidelityfx_upscaler_dx12.dll, version 4.1.1.2740), changed by OptiScaler, so it is not signed. It offers
# the same five FidelityFX functions as the FSR 3 runtime the addon ships (amd_fidelityfx_dx12.dll), so the addon runs on it unchanged. It
# goes into external\fsr4 as amd_fidelityfx_dx12.dll (the name the addon loads); packaging and deploying put it in the FSR addon's
# runtimes\FSR\0dd77d9c folder, where the Runtimes list finds it. The shipped FSR 3.1.4 stays the default. Nothing is committed from here.
#
# The archive is 7-Zip: 7-Zip must be installed. The file is checked against its SHA-256; a file already in place that matches is left alone.
#   powershell -File tools\fetch_fsr4.ps1 [-Dest <folder>]
param([string]$Dest = '')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (-not $Dest) { $Dest = "$root\addons\DLSS5NR01\external\fsr4" }

$url = 'https://github.com/the3rdparty1917/fsr4xyz/releases/download/4.1.1b/FSR_4.1.1b_INT8_with_RDNA2_fix.7z'
$inside = '4.1.1b\amd_fidelityfx_upscaler_dx12.dll'
$sha = '0dd77d9c78d1ef9bc330cf4697ab3ffe24bc1aa7850e4130263dc922107fbd75'
$target = "$Dest\amd_fidelityfx_dx12.dll"

function Sha($path) {
    $h = [Security.Cryptography.SHA256]::Create()
    $stream = [IO.File]::OpenRead($path)
    try { return (($h.ComputeHash($stream) | ForEach-Object { $_.ToString('x2') }) -join '') } finally { $stream.Dispose() }
}

if ((Test-Path $target) -and (Sha $target) -eq $sha) { Write-Host "  in place: $target"; exit 0 }
$sevenZip = @("$env:ProgramFiles\7-Zip\7z.exe", "${env:ProgramFiles(x86)}\7-Zip\7z.exe") | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $sevenZip) { Write-Host 'The archive is 7-Zip: install 7-Zip (7-zip.org) and run this again.'; exit 1 }
$work = Join-Path ([IO.Path]::GetTempPath()) "fetch_fsr4_$PID"
New-Item -ItemType Directory -Force $work | Out-Null
try {
    Write-Host "  $url"
    Invoke-WebRequest -Uri $url -OutFile "$work\fsr4.7z" -UseBasicParsing
    & $sevenZip e -y "-o$work\out" "$work\fsr4.7z" $inside | Out-Null
    $dll = "$work\out\amd_fidelityfx_upscaler_dx12.dll"
    if (-not (Test-Path $dll)) { Write-Host "  the archive does not hold $inside"; exit 1 }
    $got = Sha $dll
    if ($got -ne $sha) { Write-Host "  CHECKSUM MISMATCH: got $got, want $sha; nothing was kept"; exit 1 }
    New-Item -ItemType Directory -Force $Dest | Out-Null
    Move-Item -Force $dll $target
} finally {
    Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue   # the download and the unpacked copy (temporary files of this script)
}
Write-Host "FSR 4.1.1b (OptiScaler) is in $Dest."
