# Fetches AMD's FidelityFX runtime the FSR Upscaler loads, amd_fidelityfx_dx12.dll (FSR 3.1.4), from AMD's own public repository,
# https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK (release v1.1.4, its PrebuiltSignedDLL folder), into
# addons\DLSS5NR01\external\ffx\bin. The build copies it into the FSR addon's fsr folder, and the release ships it there.
#
# It is AMD's, under the MIT licence (addons\DLSS5NR01\third_party\ffx\LICENSE.txt, which goes into the release next to it); the API
# headers the addon is built against are in that third_party folder already. Nothing is committed from here: external\ is ignored by git.
#
# The file is pinned to one commit of AMD's repository and checked against a SHA-256 value, and its Authenticode signature must be valid and
# AMD's, so what the addon loads is exactly what AMD published. A file already in place that matches is left alone.
#   powershell -File tools\fetch_ffx_sdk.ps1 [-Dest <folder>]
param([string]$Dest = '')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (-not $Dest) { $Dest = "$root\addons\DLSS5NR01\external\ffx" }

$commit = 'c6efa6bf7f2027b3ec94f28578bb5965eabb9e55'   # AMD FidelityFX SDK 1.1.4
$files = @(
    @{ Remote = 'PrebuiltSignedDLL/amd_fidelityfx_dx12.dll'; Local = 'bin\amd_fidelityfx_dx12.dll'; Sha = '12a5081257ec95b0b53ad51b4a87fb3c03f97fe0bbb59f9496968f8d50ef93a6'; Signed = $true }   # 6.7 MB
)

function Sha($path) {
    $sha = [Security.Cryptography.SHA256]::Create()
    $stream = [IO.File]::OpenRead($path)
    try { return (($sha.ComputeHash($stream) | ForEach-Object { $_.ToString('x2') }) -join '') } finally { $stream.Dispose() }
}
function SignedByAmd($path) {
    $s = Get-AuthenticodeSignature -LiteralPath $path
    return $s.Status -eq 'Valid' -and $s.SignerCertificate.Subject -like 'CN=Advanced Micro Devices*'
}

$failed = 0
foreach ($f in $files) {
    $target = "$Dest\$($f.Local)"
    if ((Test-Path $target) -and (Sha $target) -eq $f.Sha) { Write-Host "  in place: $($f.Local)"; continue }
    New-Item -ItemType Directory -Force (Split-Path $target) | Out-Null
    $tmp = "$target.download"
    Write-Host "  $($f.Remote)"
    Invoke-WebRequest -Uri "https://raw.githubusercontent.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/$commit/$($f.Remote)" -OutFile $tmp -UseBasicParsing
    $got = Sha $tmp
    if ($got -ne $f.Sha) { Write-Host "  CHECKSUM MISMATCH for $($f.Remote): got $got"; Remove-Item $tmp; $failed++; continue }
    if ($f.Signed -and -not (SignedByAmd $tmp)) { Write-Host "  NOT SIGNED BY AMD: $($f.Remote)"; Remove-Item $tmp; $failed++; continue }
    Move-Item -Force $tmp $target
}
if ($failed) { Write-Host "$failed file(s) failed; nothing wrong was kept."; exit 1 }
Write-Host "AMD's FidelityFX runtime is in $Dest\bin."
