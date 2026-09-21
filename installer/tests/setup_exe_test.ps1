# End-to-end test of EchoAddonManagerSetup.exe on fake Lossless Scaling folders in %TEMP% (the real install is never touched):
#   * the silent mode: status, install, install again, uninstall, and the refusals (not a Lossless Scaling folder, no folder, Lossless Scaling running)
#   * that settings and other addons survive, and that the files come out byte for byte as they went in
#   * the wizard window: it opens (on a folder, and with none given), and closes itself, without changing anything
# Needs the exes built (run_addon_tests.ps1 does that) and the manager's Lossless.dll.
#   powershell -File setup_exe_test.ps1 -Root <repository folder>
param([Parameter(Mandatory = $true)][string]$Root)
$ErrorActionPreference = 'Stop'
$setup = "$Root\installer\build\Release\EchoAddonManagerSetup.exe"
$fakes = "$Root\installer\build\Release"
$ours = "$Root\manager\build\Release\Lossless.dll"
$fail = 0
$rememberedBefore = (Get-ItemProperty 'HKCU:\Software\EchoAddonManager' -ErrorAction SilentlyContinue).LastFolder
function Check($what, $ok, $detail = '') {
    if ($ok) { Write-Host "  PASS  $what" } else { Write-Host "  FAIL  $what  ($detail)"; $script:fail++ }
}
foreach ($f in @($setup, "$fakes\fake_original.dll", "$fakes\fake_original_new.dll", $ours)) { if (-not (Test-Path $f)) { Write-Host "  FAIL  not built: $f"; exit 1 } }
function Hash($p) { if (Test-Path $p) { (Get-FileHash $p -Algorithm SHA256).Hash } else { '' } }

$tmp = Join-Path $env:TEMP "setup_exe_test_$PID"
if (Test-Path $tmp) { Remove-Item -Recurse -Force $tmp }
New-Item -ItemType Directory $tmp | Out-Null

function Run($exeArgs, $name) {   # runs the (windowless) exe, returns exit code and its log text
    $log = "$tmp\$name.log"
    $p = Start-Process -FilePath $setup -ArgumentList ($exeArgs + @('--no-remember', '--log', "`"$log`"")) -PassThru -Wait -WindowStyle Hidden
    [pscustomobject]@{ Code = $p.ExitCode; Log = $(if (Test-Path $log) { Get-Content $log -Raw } else { '' }) }
}
function MakeLs($name, $dll = 'fake_original.dll') {
    $d = "$tmp\$name"
    New-Item -ItemType Directory "$d\addons\OtherAddon" -Force | Out-Null
    Copy-Item "$env:SystemRoot\System32\cmd.exe" "$d\LosslessScaling.exe"
    Copy-Item "$fakes\$dll" "$d\Lossless.dll"
    Set-Content "$d\addons\config.json" '{ "addons": { "mine": { "x": "1" } } }'
    Set-Content "$d\addons\OtherAddon\other.dll" 'someone else''s addon'
    $d
}

# The files to install, laid out like the release zip
$payload = "$tmp\payload"
New-Item -ItemType Directory "$payload\addons\DLSS5NR01" -Force | Out-Null
Copy-Item $ours "$payload\Lossless.dll"
Set-Content "$payload\LP-icon.ico" 'icon'; Set-Content "$payload\LP-icon.png" 'png'
Set-Content "$payload\addons\DLSS5NR01\DLSS5NR01.dll" 'addon dll'
Set-Content "$payload\addons\DLSS5NR01\addon.json" '{ "id": "DLSS5NR01" }'
$ls = MakeLs 'ls1'
$configBefore = Get-Content "$ls\addons\config.json" -Raw

Write-Host '== silent mode'
$r = Run @('--silent', 'status', '--folder', "`"$ls`"", '--payload', "`"$payload`"") 'status1'
Check 'status of a plain Lossless Scaling folder: not installed, and it knows what it carries' ($r.Code -eq 0 -and $r.Log -match 'situation: not installed' -and $r.Log -match 'payload: \d+\.\d+\.\d+') $r.Log
$r = Run @('--version', '--payload', "`"$payload`"") 'version'
Check '--version says what the setup carries' ($r.Code -eq 0 -and $r.Log -match '^payload \d+\.\d+\.\d+') $r.Log

$r = Run @('--silent', 'install', '--folder', "`"$ls`"", '--payload', "`"$payload`"") 'install1'
Check 'install succeeds' ($r.Code -eq 0) "$($r.Code) $($r.Log)"
Check 'the original Lossless.dll is kept as Lossless_original.dll, byte for byte' ((Hash "$ls\Lossless_original.dll") -eq (Hash "$fakes\fake_original.dll"))
Check 'our Lossless.dll is in place, byte for byte' ((Hash "$ls\Lossless.dll") -eq (Hash $ours))
Check 'the addon files came across, byte for byte' ((Hash "$ls\addons\DLSS5NR01\DLSS5NR01.dll") -eq (Hash "$payload\addons\DLSS5NR01\DLSS5NR01.dll") -and (Hash "$ls\addons\DLSS5NR01\addon.json") -eq (Hash "$payload\addons\DLSS5NR01\addon.json"))
Check 'the icons came across' ((Test-Path "$ls\LP-icon.ico") -and (Test-Path "$ls\LP-icon.png"))
Check 'the person''s settings and other addons are untouched' ((Get-Content "$ls\addons\config.json" -Raw) -eq $configBefore -and (Test-Path "$ls\addons\OtherAddon\other.dll"))
Check 'the replaced original went to a backups folder' ((Get-ChildItem "$ls\backups" -Recurse -Filter 'Lossless.dll' -ErrorAction SilentlyContinue | Measure-Object).Count -ge 1)
$r = Run @('--silent', 'status', '--folder', "`"$ls`"", '--payload', "`"$payload`"") 'status2'
Check 'status now says installed, with the version' ($r.Log -match 'situation: installed' -and $r.Log -match 'installed: \d+\.\d+\.\d+') $r.Log

$r = Run @('--silent', 'install', '--folder', "`"$ls`"", '--payload', "`"$payload`"") 'install2'
Check 'installing again (reinstall) succeeds and changes nothing that matters' ($r.Code -eq 0 -and (Hash "$ls\Lossless.dll") -eq (Hash $ours) -and (Hash "$ls\Lossless_original.dll") -eq (Hash "$fakes\fake_original.dll")) "$($r.Code) $($r.Log)"

# Lossless Scaling updates itself over ours: repair keeps the new original
Copy-Item "$fakes\fake_original_new.dll" "$ls\Lossless.dll" -Force
$r = Run @('--silent', 'status', '--folder', "`"$ls`"", '--payload', "`"$payload`"") 'status3'
Check 'after a Lossless Scaling update the folder is recognised' ($r.Log -match 'situation: after a Lossless Scaling update') $r.Log
$r = Run @('--silent', 'install', '--folder', "`"$ls`"", '--payload', "`"$payload`"") 'repair'
Check 'repair keeps the new original and puts ours back' ($r.Code -eq 0 -and (Hash "$ls\Lossless_original.dll") -eq (Hash "$fakes\fake_original_new.dll") -and (Hash "$ls\Lossless.dll") -eq (Hash $ours)) "$($r.Code) $($r.Log)"

$r = Run @('--silent', 'uninstall', '--folder', "`"$ls`"") 'uninstall'
Check 'uninstall puts Lossless Scaling''s own Lossless.dll back' ($r.Code -eq 0 -and (Hash "$ls\Lossless.dll") -eq (Hash "$fakes\fake_original_new.dll")) "$($r.Code) $($r.Log)"
Check 'uninstall leaves the addons and the settings' ((Test-Path "$ls\addons\DLSS5NR01\DLSS5NR01.dll") -and (Get-Content "$ls\addons\config.json" -Raw) -eq $configBefore)

Write-Host '== refusals'
$other = "$tmp\not_ls"; New-Item -ItemType Directory $other | Out-Null; Set-Content "$other\file.txt" 'x'
$r = Run @('--silent', 'install', '--folder', "`"$other`"", '--payload', "`"$payload`"") 'notls'
Check 'a folder that is not Lossless Scaling is refused and left alone' ($r.Code -ne 0 -and (Get-ChildItem $other | Measure-Object).Count -eq 1) "$($r.Code) $($r.Log)"
$r = Run @('--silent', 'install', '--payload', "`"$payload`"") 'nofolder'
Check 'silent mode without a folder is refused (exit 2)' ($r.Code -eq 2) "$($r.Code) $($r.Log)"
$r = Run @('--silent', 'install', '--folder', "`"$ls`"") 'nopayload'
Check 'an exe without files says so and does nothing (exit 2)' ($r.Code -eq 2 -and $r.Log -match 'carries none|no files') "$($r.Code) $($r.Log)"
$bad = "$tmp\badpayload"; New-Item -ItemType Directory $bad | Out-Null; Set-Content "$bad\Lossless.dll" 'not a real dll'
$before = Hash "$ls\Lossless.dll"
$r = Run @('--silent', 'install', '--folder', "`"$ls`"", '--payload', "`"$bad`"") 'badpayload'
Check 'files that are not ours are refused before anything changes' ($r.Code -ne 0 -and (Hash "$ls\Lossless.dll") -eq $before) "$($r.Code) $($r.Log)"

$ls2 = MakeLs 'ls2'
$run = Start-Process -FilePath "$ls2\LosslessScaling.exe" -ArgumentList '/k' -WindowStyle Hidden -PassThru
try {
    Start-Sleep -Milliseconds 500
    $r = Run @('--silent', 'install', '--folder', "`"$ls2`"", '--payload', "`"$payload`"") 'running'
    Check 'while Lossless Scaling runs from that folder, install is refused and nothing changes' ($r.Code -ne 0 -and (Hash "$ls2\Lossless.dll") -eq (Hash "$fakes\fake_original.dll") -and -not (Test-Path "$ls2\Lossless_original.dll")) "$($r.Code) $($r.Log)"
} finally { Stop-Process -Id $run.Id -Force -ErrorAction SilentlyContinue }

Write-Host '== the wizard window (opens by itself, closes by itself)'
$ls3 = MakeLs 'ls3'
$before = Hash "$ls3\Lossless.dll"
foreach ($case in @(
        @{ Name = 'on a given folder'; Args = @('--folder', "`"$ls3`"", '--payload', "`"$payload`"", '--test-close-ms', '1500') },
        @{ Name = 'with no folder given (the folder chooser or the only folder found)'; Args = @('--payload', "`"$payload`"", '--test-close-ms', '1500') },
        @{ Name = 'without files to install (the damaged-setup page)'; Args = @('--folder', "`"$ls3`"", '--test-close-ms', '1500') })) {
    $p = Start-Process -FilePath $setup -ArgumentList $case.Args -PassThru
    $closed = $p.WaitForExit(20000)
    if (-not $closed) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue }
    Check "the window $($case.Name) opens and closes itself with exit code 0" ($closed -and $p.ExitCode -eq 0) "closed=$closed code=$($p.ExitCode)"
}
Check 'opening the window changed nothing in the folder' ((Hash "$ls3\Lossless.dll") -eq $before -and -not (Test-Path "$ls3\Lossless_original.dll") -and -not (Test-Path "$ls3\backups"))
Check 'the window left no write-test file behind' (-not (Get-ChildItem $ls3 -Force -Filter '.echo_setup_write_test_*' -ErrorAction SilentlyContinue))

$mine = (Get-ItemProperty 'HKCU:\Software\EchoAddonManager' -ErrorAction SilentlyContinue).LastFolder
Check 'the person''s remembered folder was not touched by any of it' (-not $mine -or $mine -eq $rememberedBefore) "now: $mine"
Remove-Item 'HKCU:\Software\EchoAddonManager\SetupTest' -Recurse -ErrorAction SilentlyContinue
Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
if ($fail) { Write-Host "SETUP EXE TEST FAILED ($fail failed)"; exit 1 } else { Write-Host 'SETUP EXE TEST PASSED (0 failed)' }
