# Presentation-pacing comparison for external frame generation (docs/frame-generation.md).
# Each arm boots headless into a chapter on a fresh copy of -Card (a testing card that already holds a profile; the
# front end does not create one on an empty card in this flow) with V-Sync off and the intro skipped, records
# KZGS_PRESENT_LOG and 10 s captures, and summarizes the guest-time window -FromGuestS..-ToGuestS with
# present_pacing_analyze.py. -ReadbackFree replaces captures with guest-time/presentation-sequence metadata samples.
# -Timeline records the same guest window and writes machine-readable scheduler/vblank/renderer metrics. -GpuTiming
# enables asynchronous whole-frame GPU timestamp queries and requires -Timeline. Raw logs and captures stay under ignored work\.
#
# The window is guest time, not gameplay time: on SCES-52004 the local player is not detected, so gameplay-relative
# input ("g" script steps) and KZ_HEADLESS_GAMEPLAY_SECONDS never start. Check separate captures to confirm that the window
# is gameplay; on SCES-52004 chapter 1 without input, gameplay is visible from about 40 s until the player dies near
# 115 s. Arms are "name:fps:pacing[:precise-wait[:scale-policy]]", for example "fallback:60:0:0" and
# "precise:60:0:1". The optional quality|balanced policy requires -HeadroomPreset and changes only its auto scale.
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
    [switch]$ReadbackFree,
    [switch]$Timeline,
    [switch]$GpuTiming,
    [switch]$HeadroomPreset,
    [string]$WindowSize = "",
    [string]$Region = "",
    [string]$ExeDir = "",
    [string]$Root = $env:KZ_ROOT,
    [string]$Iso = $env:KZ_ISO,
    [string]$Elf = $env:KZ_GAME_ELF
)
. "$PSScriptRoot\kz_paths.ps1"
$wd = Resolve-KzRoot $Root
if (-not $ExeDir) { $ExeDir = "$wd\build\win-msvc-rel" }
$isoPath = Resolve-KzRequiredFile $Iso KZ_ISO Iso
$elfPath = Resolve-KzRequiredFile $Elf KZ_GAME_ELF Elf
$cardPath = (Resolve-Path -LiteralPath $Card).Path
if ($Chapter -lt 1 -or $Chapter -gt 11) { throw "Chapter $Chapter is outside 1..11." }
if ($ToGuestS -le $FromGuestS) { throw "-ToGuestS must be after -FromGuestS." }
if ($Seconds -le 0) { $Seconds = [int][math]::Ceiling($ToGuestS) + 15 }
if (-not $Region) {
    $leaf = [IO.Path]::GetFileName($elfPath).ToUpperInvariant()
    $Region = if ($leaf -eq 'SCES_520.04') { 'eu' } elseif ($leaf -eq 'SCES_528.93') { 'pal' } else { 'us' }
}
if ($Region -notin @('us', 'pal', 'eu')) { throw "-Region must be us, pal or eu." }
if ($GpuTiming -and -not $Timeline) { throw "-GpuTiming requires -Timeline." }
if ($HeadroomPreset -and $Renderer -ne 'vulkan') { throw "-HeadroomPreset requires -Renderer vulkan." }
if ($HeadroomPreset -and -not $WindowSize) { $WindowSize = '1920x1080' }
if ($WindowSize -and $WindowSize -notmatch '^[1-9][0-9]*x[1-9][0-9]*$') {
    throw "-WindowSize must be WIDTHxHEIGHT with positive integers."
}

$configDir = Get-KzWorkDirectory $wd "${Name}_config"
New-Item -ItemType Directory -Force $configDir | Out-Null

$rows = @()
foreach ($arm in $Arms) {
    $parts = $arm -split ':'
    if ($parts.Count -notin @(3, 4, 5) -or $parts[1] -notmatch '^[0-9]+$' -or
        $parts[2] -notin @('0', '1') -or ($parts.Count -ge 4 -and $parts[3] -notin @('0', '1')) -or
        ($parts.Count -eq 5 -and $parts[4] -notin @('quality', 'balanced'))) {
        throw "Arm '$arm' must be name:fps:pacing[:precise-wait[:scale-policy]], with switches 0 or 1 and policy quality or balanced."
    }
    $armName, $fps, $pacing = $parts[0..2]
    $preciseWait = if ($parts.Count -ge 4) { $parts[3] } else { $null }
    $effectivePreciseWait = if ($null -ne $preciseWait) { $preciseWait } else { '0' }
    if ($parts.Count -eq 5 -and -not $HeadroomPreset) {
        throw "Arm '$arm' selects a scale policy without -HeadroomPreset."
    }
    $scalePolicy = if ($parts.Count -eq 5) { $parts[4] } elseif ($HeadroomPreset) { 'balanced' } else { '' }
    $runName = "${Name}_c{0:D2}_{1}" -f $Chapter, $armName
    $config = Join-Path $configDir "${armName}.ini"
    $configText = "[Config]`nVersion=3`n`n[Launcher]`nShow=0`n`n[Display]`nWindowMode=borderless`nVSync=0"
    if ($HeadroomPreset) {
        $configText += "`nFpsLimit=60`n`n[Graphics]`nRenderer=vulkan`nRenderScalePolicy=$scalePolicy`nUpscale=0"
        $configText += "`nUpscaler=fsr1`nUpscalerSharpening=15`nFSR3InputMode=strict`nAnisotropy=16"
        $configText += "`nMipmapping=ps2`nFXAA=0`nSMAA=0`nTextureReplacement=0`nBilinear=1"
        $configText += "`nSharpScaling=1`nSharpen=0`nNoiseFilter=0`nMotionBlur=0`nGlow=0`nLensBlur=0"
        $configText += "`nRenderDistance=1`nLodScale=1"
    }
    $configText += "`n`n[Game]`nSkipIntro=1`nSkipLogos=1`n"
    Set-Content -LiteralPath $config -Encoding utf8NoBOM -Value $configText
    $out = Get-KzWorkDirectory $wd $runName
    $runCard = Get-KzWorkDirectory $wd "${runName}_card"
    Remove-Item -Recurse -Force -LiteralPath $runCard, "${runCard}_slot1" -ErrorAction SilentlyContinue
    Copy-Item -Recurse -LiteralPath $cardPath -Destination $runCard
    $presentLog = Join-Path $out "present.csv"
    $timelineLog = Join-Path $out "timeline.csv"
    $timelineMetrics = Join-Path $out "timeline_metrics.json"
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
    $envs += "PS2X_PRECISE_WAIT=$effectivePreciseWait"
    if ($WindowSize) { $envs += "KZ_WINDOW_SIZE=$WindowSize" }
    if ($ReadbackFree) {
        $envs += "KZ_CAPTURE_METADATA_ONLY=1"
        $envs += "KZ_SHOT_GUEST_INTERVAL=$fps"
    }
    if ($Timeline) {
        $timelineStart = [math]::Max(0, [math]::Floor($FromGuestS - 10))
        $timelineEnd = [math]::Min($Seconds - 1, [math]::Ceiling($ToGuestS + 10))
        $envs += "KZ_TIMELINE=$timelineLog"
        $envs += "KZ_TL_START=$timelineStart"
        $envs += "KZ_TL_END=$timelineEnd"
    }
    if ($GpuTiming) { $envs += "KZ_GPU_TIMING=1" }
    if ($InputScript) { $envs += "KZ_INPUT_SCRIPT=$InputScript" }
    $result = & "$PSScriptRoot\run_headless.ps1" -Name $runName -Seconds $Seconds -Interval 10 -ExeDir $ExeDir `
        -EnvSet ($envs -join '|') -Root $wd -Iso $isoPath -Elf $elfPath
    foreach ($kv in $envs) { Remove-Item "env:$(($kv -split '=', 2)[0])" -ErrorAction SilentlyContinue }

    $stdout = Get-Content -Raw "$out\stdout.log" -ErrorAction SilentlyContinue
    $problems = @()
    if (($result -join ' ') -notmatch 'exited code=0') { $problems += ($result -join ' ') }
    if ($stdout -notmatch "front end: level $Chapter\.") { $problems += "chapter $Chapter not reached" }
    if ($pacing -eq '1' -and $stdout -notmatch 'even frame pacing at') { $problems += 'pacing not enabled' }
    $effectiveConfig = $null
    if ($HeadroomPreset) {
        $captureLog = Join-Path $out "capture.jsonl"
        if (Test-Path -LiteralPath $captureLog) {
            $configRecord = Get-Content -LiteralPath $captureLog |
                ForEach-Object { $_ | ConvertFrom-Json -ErrorAction Stop } |
                Where-Object record -eq 'config' |
                Select-Object -First 1
            if ($null -ne $configRecord) { $effectiveConfig = $configRecord.effective_config }
        }
        if ($null -eq $effectiveConfig) {
            $problems += 'no effective renderer config record'
        } else {
            $expectedScaleMode = if ($scalePolicy -eq 'balanced') { 'auto_balanced' } else { 'auto_output_height' }
            if ($effectiveConfig.render_scale.mode -ne $expectedScaleMode) {
                $problems += "render scale mode is $($effectiveConfig.render_scale.mode), expected $expectedScaleMode"
            }
            if ($effectiveConfig.presentation_upscaler.requested -ne 'fsr1' -or
                $effectiveConfig.cas_sharpness -ne 0 -or $effectiveConfig.aa -ne 'none' -or
                $effectiveConfig.texture_replacement -ne $false) {
                $problems += 'headroom renderer contract mismatch'
            }
            if ($WindowSize -eq '1920x1080') {
                $expectedScale = if ($scalePolicy -eq 'balanced') { 2 } else { 3 }
                $expectedUpscaler = if ($scalePolicy -eq 'balanced') { 'fsr1' } else { 'off' }
                if ($effectiveConfig.render_scale.effective -ne $expectedScale -or
                    $effectiveConfig.presentation_upscaler.effective -ne $expectedUpscaler -or
                    $effectiveConfig.output_resolution.width -ne 1920 -or
                    $effectiveConfig.output_resolution.height -ne 1080) {
                    $problems += '1080p headroom comparison contract mismatch'
                }
            }
        }
    }
    $summary = $null
    if (Test-Path -LiteralPath $presentLog) {
        $json = & python -I "$PSScriptRoot\present_pacing_analyze.py" $presentLog --target-hz $fps `
            --capture-log "$out\capture.jsonl" --from-guest-s $FromGuestS --to-guest-s $ToGuestS `
            --output "$out\pacing.json"
        if ($json) { $summary = ($json -join "`n") | ConvertFrom-Json } else { $problems += 'analysis failed' }
    } else {
        $problems += 'no present log'
    }
    $metrics = $null
    if ($Timeline) {
        if (Test-Path -LiteralPath $timelineLog) {
            $timelineOutput = & python -I "$PSScriptRoot\tl_analyze.py" $timelineLog --region $Region `
                --vblank-hz $fps --guest-window-s $FromGuestS $ToGuestS --metrics-output $timelineMetrics 2>&1
            $timelineExit = $LASTEXITCODE
            $timelineOutput | Set-Content -LiteralPath (Join-Path $out "timeline.txt") -Encoding utf8NoBOM
            if ($timelineExit -eq 0 -and (Test-Path -LiteralPath $timelineMetrics)) {
                $metrics = Get-Content -Raw -LiteralPath $timelineMetrics | ConvertFrom-Json
            } else {
                $problems += 'timeline analysis failed'
            }
        } else {
            $problems += 'no timeline log'
        }
    }
    $waitBackend = if ($effectivePreciseWait -eq '1') { 'precise' } else { 'condition_variable' }
    $pacingWaits = $null
    if ($null -ne $metrics) {
        $pacingProperty = $metrics.waits.PSObject.Properties['pacing']
        if ($pacingProperty) { $pacingWaits = $pacingProperty.Value }
    }
    $waitMetrics = $null
    $actualBackends = @()
    if ($null -ne $pacingWaits) {
        foreach ($property in $pacingWaits.PSObject.Properties) {
            if ($property.Value.count -gt 0) { $actualBackends += $property.Name }
        }
        $expectedProperty = $pacingWaits.PSObject.Properties[$waitBackend]
        if ($expectedProperty) { $waitMetrics = $expectedProperty.Value }
    }
    if ($Timeline -and $null -eq $waitMetrics) {
        $problems += "no $waitBackend pacing waits"
    }
    $unexpectedBackends = @($actualBackends | Where-Object { $_ -ne $waitBackend })
    if ($Timeline -and $unexpectedBackends.Count -gt 0) {
        $problems += "unexpected pacing wait backend: $($unexpectedBackends -join '+')"
    }
    $waitCount = if ($null -ne $waitMetrics) { $waitMetrics.deadline_count } else { $null }
    $waitOvershootP95 = if ($null -ne $waitMetrics) { $waitMetrics.overshoot_ms.p95 } else { $null }
    $waitOvershootP99 = if ($null -ne $waitMetrics) { $waitMetrics.overshoot_ms.p99 } else { $null }
    $vblankP95 = if ($null -ne $metrics) { $metrics.vblank.interval_ms.p95 } else { $null }
    $vblankOver25Pct = if ($null -ne $metrics -and $null -ne $metrics.vblank.over_25_ms_ratio) {
        100 * $metrics.vblank.over_25_ms_ratio
    } else { $null }
    $gpuFrame = if ($null -ne $metrics) { $metrics.renderer.gpu_frame_ms } else { $null }
    if ($GpuTiming -and ($null -eq $gpuFrame -or $gpuFrame.count -le 0)) {
        $problems += 'no GPU frame timing samples'
    }
    $uploadDrawCpu = if ($null -ne $metrics) { $metrics.renderer.upload_draw_cpu_ms } else { $null }
    $mergeCpu = if ($null -ne $metrics) { $metrics.renderer.merge_cpu_ms } else { $null }
    $upscaleCpu = if ($null -ne $metrics) { $metrics.renderer.presentation_upscale_cpu_ms } else { $null }
    $sharpenCpu = if ($null -ne $metrics) { $metrics.renderer.sharpen_cpu_ms } else { $null }
    $presentCpu = if ($null -ne $metrics) { $metrics.renderer.final_presentation_cpu_ms } else { $null }
    $rows += [pscustomobject]@{
        arm = $armName; fps = [int]$fps; pacing = [int]$pacing; precise_wait = [int]$effectivePreciseWait
        headroom_preset = [bool]$HeadroomPreset; render_scale_policy = $scalePolicy; window_size = $WindowSize
        effective_scale = $effectiveConfig.render_scale.effective
        effective_upscaler = $effectiveConfig.presentation_upscaler.effective
        first_guest_s = $summary.capture_window.first_guest_s; last_guest_s = $summary.capture_window.last_guest_s
        mean_fps = $summary.mean_fps; p50_ms = $summary.interval_ms.p50; p95_ms = $summary.interval_ms.p95
        p99_ms = $summary.interval_ms.p99; max_ms = $summary.interval_ms.max; stdev_ms = $summary.interval_ms.stdev
        bunched = $summary.bunched; long = $summary.long; jitter_ms = $summary.mean_successive_delta_ms
        wait_backend = ($actualBackends -join '+'); wait_count = $waitCount
        wait_overshoot_p95_ms = $waitOvershootP95; wait_overshoot_p99_ms = $waitOvershootP99
        vblank_p95_ms = $vblankP95; vblank_over_25_pct = $vblankOver25Pct
        gpu_frame_count = $gpuFrame.count; gpu_frame_p50_ms = $gpuFrame.median
        gpu_frame_p95_ms = $gpuFrame.p95; gpu_frame_p99_ms = $gpuFrame.p99; gpu_frame_max_ms = $gpuFrame.max
        gpu_frame_under_12_ms = if ($null -ne $gpuFrame) { $gpuFrame.p95 -lt 12 } else { $null }
        upload_draw_cpu_p95_ms = $uploadDrawCpu.p95; merge_cpu_p95_ms = $mergeCpu.p95
        upscale_cpu_p95_ms = $upscaleCpu.p95; sharpen_cpu_p95_ms = $sharpenCpu.p95
        present_cpu_p95_ms = $presentCpu.p95
        verdict = $summary.verdict; problems = ($problems -join '; ')
    }
}
foreach ($key in @(
    "PS2X_HEADLESS", "PS2X_HEADLESS_INTERVAL", "PS2X_HEADLESS_SECONDS",
    "KZ_HEADLESS_GAMEPLAY_SECONDS", "KZ_SHOT_DIR", "KZ_SHOT_INTERVAL",
    "KZ_CAPTURE_METADATA_ONLY", "KZ_SHOT_GUEST_INTERVAL", "KZ_WINDOW_SIZE",
    "KZ_TIMELINE", "KZ_TL_START", "KZ_TL_END",
    "KZ_GPU_TIMING", "PS2X_PRECISE_WAIT"
)) {
    Remove-Item "env:$key" -ErrorAction SilentlyContinue
}
$rows | Format-Table -AutoSize | Out-String -Width 220
$rows | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $wd "work\${Name}_summary.json") -Encoding utf8NoBOM
