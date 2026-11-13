# Presentation-pacing comparison for external frame generation (docs/frame-generation.md).
# Each arm boots headless into a chapter on a fresh copy of -Card (a testing card that already holds a profile; the
# front end does not create one on an empty card in this flow) with V-Sync off and the intro skipped, records
# KZGS_PRESENT_LOG and 10 s captures, and summarizes the guest-time window -FromGuestS..-ToGuestS with
# present_pacing_analyze.py. Raw logs and captures stay under ignored work\.
#
# The window is guest time, not gameplay time: on SCES-52004 the local player is not detected, so gameplay-relative
# input ("g" script steps) and KZ_HEADLESS_GAMEPLAY_SECONDS never start. Check the captures to confirm that the window
# is gameplay; on SCES-52004 chapter 1 without input, gameplay is visible from about 40 s until the player dies near
# 115 s. Arms are "name:fps:pacing", for example "fps60-off:60:0".
# Example:
#   present_pacing_campaign.ps1 -Card work\<card> -Iso <image> -Elf <SCES_520.04 or SCUS_974.02> -Chapter 1
param(
    [Parameter(Mandatory)][string]$Card,
    [string[]]$Arms = @("fps60-off:60:0", "fps60-on:60:1", "fps72-off:72:0", "fps72-on:72:1"),
    [string]$Name = "pacing",
    [int]$Chapter = 1,
    [double]$FromGuestS = 40,
    [double]$ToGuestS = 110,
    [int]$Seconds = 0,
    [string]$Renderer = "vulkan",
    [string]$InputScript = "",
    [string]$ExeDir = "",
    [string]$Root = $env:KZ_ROOT,
    [string]$Iso = $env:KZ_ISO,
    [string]$Elf = $env:KZ_GAME_ELF
)
. "$PSScriptRoot\kz_paths.ps1"
$wd = Resolve-KzRoot $Root
if (-not $ExeDir) { $ExeDir = "$wd\build\win-msvc-rel" }
$cardPath = (Resolve-Path -LiteralPath $Card).Path
if ($Chapter -lt 1 -or $Chapter -gt 11) { throw "Chapter $Chapter is outside 1..11." }
if ($ToGuestS -le $FromGuestS) { throw "-ToGuestS must be after -FromGuestS." }
if ($Seconds -le 0) { $Seconds = [int][math]::Ceiling($ToGuestS) + 15 }

$configDir = Get-KzWorkDirectory $wd "${Name}_config"
New-Item -ItemType Directory -Force $configDir | Out-Null
$config = Join-Path $configDir "killzone.ini"
Set-Content -LiteralPath $config -Encoding utf8NoBOM -Value "[Launcher]`nShow=0`n`n[Display]`nWindowMode=borderless`nVSync=0`n`n[Game]`nSkipIntro=1`nSkipLogos=1`n"

$rows = @()
foreach ($arm in $Arms) {
    $parts = $arm -split ':'
    if ($parts.Count -ne 3 -or $parts[1] -notmatch '^[0-9]+$' -or $parts[2] -notin @('0', '1')) {
        throw "Arm '$arm' must be name:fps:pacing with pacing 0 or 1."
    }
    $armName, $fps, $pacing = $parts
    $runName = "${Name}_c{0:D2}_{1}" -f $Chapter, $armName
    $out = Get-KzWorkDirectory $wd $runName
    $runCard = Get-KzWorkDirectory $wd "${runName}_card"
    Remove-Item -Recurse -Force -LiteralPath $runCard, "${runCard}_slot1" -ErrorAction SilentlyContinue
    Copy-Item -Recurse -LiteralPath $cardPath -Destination $runCard
    $presentLog = Join-Path $out "present.csv"
    $envs = @(
        "KZ_CONFIG=$config",
        "KZ_START_LEVEL=$Chapter",
        "KZ_MC_ROOT=$runCard",
        "KZ_RENDERER=$Renderer",
        "KZ_AUDIO_DEVICE=0",
        "KZ_FPS=$fps",
        "KZ_FRAME_PACING=$pacing",
        "KZGS_PRESENT_LOG=$presentLog"
    )
    if ($InputScript) { $envs += "KZ_INPUT_SCRIPT=$InputScript" }
    $result = & "$PSScriptRoot\run_headless.ps1" -Name $runName -Seconds $Seconds -Interval 10 -ExeDir $ExeDir `
        -EnvSet ($envs -join '|') -Root $wd -Iso $Iso -Elf $Elf
    foreach ($kv in $envs) { Remove-Item "env:$(($kv -split '=', 2)[0])" -ErrorAction SilentlyContinue }

    $stdout = Get-Content -Raw "$out\stdout.log" -ErrorAction SilentlyContinue
    $problems = @()
    if (($result -join ' ') -notmatch 'exited code=0') { $problems += ($result -join ' ') }
    if ($stdout -notmatch "front end: level $Chapter\.") { $problems += "chapter $Chapter not reached" }
    if ($pacing -eq '1' -and $stdout -notmatch 'even frame pacing at') { $problems += 'pacing not enabled' }
    $summary = $null
    if (Test-Path -LiteralPath $presentLog) {
        $json = & python -I "$PSScriptRoot\present_pacing_analyze.py" $presentLog --target-hz $fps `
            --capture-log "$out\capture.jsonl" --from-guest-s $FromGuestS --to-guest-s $ToGuestS `
            --output "$out\pacing.json"
        if ($json) { $summary = ($json -join "`n") | ConvertFrom-Json } else { $problems += 'analysis failed' }
    } else {
        $problems += 'no present log'
    }
    $rows += [pscustomobject]@{
        arm = $armName; fps = [int]$fps; pacing = [int]$pacing
        first_guest_s = $summary.capture_window.first_guest_s; last_guest_s = $summary.capture_window.last_guest_s
        mean_fps = $summary.mean_fps; p50_ms = $summary.interval_ms.p50; p95_ms = $summary.interval_ms.p95
        p99_ms = $summary.interval_ms.p99; max_ms = $summary.interval_ms.max; stdev_ms = $summary.interval_ms.stdev
        bunched = $summary.bunched; long = $summary.long; jitter_ms = $summary.mean_successive_delta_ms
        verdict = $summary.verdict; problems = ($problems -join '; ')
    }
}
$rows | Format-Table -AutoSize | Out-String -Width 220
$rows | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $wd "work\${Name}_summary.json") -Encoding utf8NoBOM
