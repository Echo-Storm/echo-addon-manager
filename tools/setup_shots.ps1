# The README's two Setup pictures (docs\images\setup-install.png and setup-installed.png): a throwaway fake Lossless Scaling folder, Setup's --shot
# mode (its window kept off the screen, it installs into that folder only and remembers nothing), then the BMPs as PNGs in -Out. The fake
# folder is moved to %TEMP% afterwards, not deleted. Build the package first (tools\package.ps1): the Setup exe comes from dist.
#   powershell -File tools\setup_shots.ps1 [-Folder 'C:\Games\Lossless Scaling'] [-Out folder]
# The folder's path shows in the first picture, so pick a plain one; it must not exist yet (nothing real is ever used).
param([string]$Folder = "$env:SystemDrive\Games\Lossless Scaling", [string]$Out = "$env:TEMP\setup_shots")
$root = Split-Path $PSScriptRoot -Parent
$setup = Get-ChildItem "$root\dist\LSAddonManager-*-x64\LSAddonManagerSetup.exe" | Sort-Object LastWriteTime | Select-Object -Last 1
if (-not $setup) { Write-Host 'No Setup exe in dist: run tools\package.ps1 first.'; exit 1 }
$fake = "$root\installer\build\Release\fake_original.dll"
if (-not (Test-Path $fake)) { Write-Host "Not built: $fake (tools\run_addon_tests.ps1 -Only installer builds it)."; exit 1 }
$top = Split-Path $Folder -Parent
if (Test-Path $top) { Write-Host "$top already exists: pick a folder that does not (nothing real is used)."; exit 1 }
New-Item -ItemType Directory -Force $Folder, $Out | Out-Null
Copy-Item "$env:SystemRoot\System32\cmd.exe" (Join-Path $Folder 'LosslessScaling.exe')   # any exe: Setup only looks for the name
Copy-Item $fake (Join-Path $Folder 'Lossless.dll')                                          # a Lossless.dll that says "Lossless Scaling 3.2.2.0"
$p = Start-Process -FilePath $setup.FullName -ArgumentList @('--shot', "`"$Out`"", '--folder', "`"$Folder`"", '--instance-name', 'shot') -PassThru
$finished = $p.WaitForExit(60000)
if (-not $finished) { $p.Kill() }
Move-Item $top (Join-Path $env:TEMP ('setup-shot-folder-' + (Get-Date -Format 'yyyyMMdd-HHmmss')))
if (-not $finished) { Write-Host 'Setup did not finish.'; exit 1 }
python "$PSScriptRoot\bmp2png.py" $Out
Write-Host "Copy setup-start.png to docs\images\setup-install.png and setup-done.png to docs\images\setup-installed.png (from $Out)."
