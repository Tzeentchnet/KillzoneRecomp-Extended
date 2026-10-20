# WIN-06 acceptance gate: repeated chapter 11 starts with the intro skipped (TODO.md, IMPLEMENTATION_PLAN.md WIN-06).
# Usage: win06_gate.ps1 -Runs 10 -Name win06_gate -Card work\win04_20261019_card [-ExeDir build\win-clang-rel]
#        [-EnvSet "PS2X_DI_GATE=0"] [-Seconds 35] [-Iso <image>] [-Elf <SCUS_974.02>]
# Each run boots headless with KZ_START_LEVEL=11 (which also skips the intro) on a fresh copy of -Card, so runs never
# share memory-card state. Frames and logs go to work\<Name>_NN. A run passes when it exits 0, reaches the chapter 11
# level file, logs no guest missing-target or host exception, and every capture has the size of the first one (the
# known failure shows 1536x2688 frames among the normal 2048x1792 ones).
param(
    [int]$Runs = 10,
    [string]$Name = "win06_gate",
    [Parameter(Mandatory)][string]$Card,
    [int]$Seconds = 35,
    [string]$ExeDir = "",
    [string]$EnvSet = "",
    [string]$Root = $env:KZ_ROOT,
    [string]$Iso = $env:KZ_ISO,
    [string]$Elf = $env:KZ_GAME_ELF
)
. "$PSScriptRoot\kz_paths.ps1"
$wd = Resolve-KzRoot $Root
if (-not $ExeDir) { $ExeDir = "$wd\build\win-clang-rel" }
$cardPath = (Resolve-Path -LiteralPath $Card).Path

function Get-PngSize([string]$Path) {
    $bytes = [IO.File]::ReadAllBytes($Path)
    $w = ([int]$bytes[16] -shl 24) -bor ([int]$bytes[17] -shl 16) -bor ([int]$bytes[18] -shl 8) -bor [int]$bytes[19]
    $h = ([int]$bytes[20] -shl 24) -bor ([int]$bytes[21] -shl 16) -bor ([int]$bytes[22] -shl 8) -bor [int]$bytes[23]
    "${w}x${h}"
}

$failed = 0
for ($i = 1; $i -le $Runs; ++$i) {
    $runName = "{0}_{1:D2}" -f $Name, $i
    $runCard = Get-KzWorkDirectory $wd "${runName}_card"
    Remove-Item -Recurse -Force $runCard, "${runCard}_slot1" -ErrorAction SilentlyContinue
    Copy-Item -Recurse -LiteralPath $cardPath -Destination $runCard
    $envs = @("KZ_START_LEVEL=11", "KZ_MC_ROOT=$runCard", "KZ_RENDERER=d3d12") +
        ($EnvSet -split '[|]' | Where-Object { $_ })
    $result = & "$PSScriptRoot\run_headless.ps1" -Name $runName -Seconds $Seconds -Interval 5 -ExeDir $ExeDir `
        -EnvSet ($envs -join '|') -Root $wd -Iso $Iso -Elf $Elf
    foreach ($kv in $envs) { Remove-Item "env:$(($kv -split '=', 2)[0])" -ErrorAction SilentlyContinue }

    $out = Get-KzWorkDirectory $wd $runName
    $stdout = Get-Content -Raw "$out\stdout.log" -ErrorAction SilentlyContinue
    $stderr = Get-Content -Raw "$out\stderr.log" -ErrorAction SilentlyContinue
    $sizes = @(Get-ChildItem "$out\gs_*.png" -ErrorAction SilentlyContinue | Sort-Object Name | ForEach-Object { Get-PngSize $_.FullName })
    $problems = @()
    if (($result -join ' ') -notmatch 'exited code=0') { $problems += ($result -join ' ') }
    if ($stdout -notmatch 'front end: level 11\.OTS') { $problems += 'chapter 11 not reached' }
    $missing = ([regex]::Matches("$stderr", 'missing-target')).Count
    if ($missing) { $problems += "$missing missing-target" }
    if ("$stdout$stderr" -match 'fatal exception|0xC0000|unhandled exception') { $problems += 'host exception' }
    if ($sizes.Count -eq 0) { $problems += 'no frames' }
    elseif (@($sizes | Select-Object -Unique).Count -ne 1) { $problems += "frame sizes $(($sizes | Select-Object -Unique) -join ',')" }
    $deferred = ([regex]::Matches("$stderr", 'di-gate')).Count
    if ($problems.Count) { ++$failed; "{0}: FAIL ({1})" -f $runName, ($problems -join '; ') }
    else { "{0}: pass ({1} frames {2}{3})" -f $runName, $sizes.Count, $sizes[0], $(if ($deferred) { ", $deferred di-gate lines" } else { "" }) }
}
"WIN-06 gate: $($Runs - $failed)/$Runs passed"
if ($failed) { exit 1 }
