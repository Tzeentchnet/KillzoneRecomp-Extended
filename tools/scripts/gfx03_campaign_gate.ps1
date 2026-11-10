# GFX-03 render-distance campaign gate. Each chapter/arm gets a fresh copy of the supplied testing card.
# Example:
#   gfx03_campaign_gate.ps1 -Chapters 1,5,10 -Card work\gfx03_card -Seconds 75 `
#       -Iso <image> -Elf game\SCUS_974.02
param(
    [int[]]$Chapters = @(1, 5, 10),
    [string[]]$Arms = @("baseline", "render2"),
    [string]$Name = "gfx03_campaign",
    [Parameter(Mandatory)][string]$Card,
    [int]$Seconds = 75,
    [int]$GameplaySeconds = 30,
    [string]$ExeDir = "",
    [string]$EnvSet = "",
    [string]$Root = $env:KZ_ROOT,
    [string]$Iso = $env:KZ_ISO,
    [string]$Elf = $env:KZ_GAME_ELF
)
. "$PSScriptRoot\kz_paths.ps1"
$wd = Resolve-KzRoot $Root
if (-not $ExeDir) { $ExeDir = "$wd\build\win-msvc-rel" }
$cardPath = (Resolve-Path -LiteralPath $Card).Path

foreach ($chapter in $Chapters) {
    if ($chapter -lt 1 -or $chapter -gt 11) { throw "Chapter $chapter is outside 1..11." }
}
foreach ($arm in $Arms) {
    if ($arm -notin @("baseline", "render2")) { throw "Unknown arm '$arm'; use baseline or render2." }
}

function Get-PngSize([string]$Path) {
    $bytes = [IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 24) { throw "PNG is truncated: $Path" }
    $w = ([int]$bytes[16] -shl 24) -bor ([int]$bytes[17] -shl 16) -bor ([int]$bytes[18] -shl 8) -bor [int]$bytes[19]
    $h = ([int]$bytes[20] -shl 24) -bor ([int]$bytes[21] -shl 16) -bor ([int]$bytes[22] -shl 8) -bor [int]$bytes[23]
    "${w}x${h}"
}

$failed = 0
$rows = @()
foreach ($chapter in $Chapters) {
    foreach ($arm in $Arms) {
        $runName = "{0}_c{1:D2}_{2}" -f $Name, $chapter, $arm
        $runCard = Get-KzWorkDirectory $wd "${runName}_card"
        Remove-Item -Recurse -Force -LiteralPath $runCard -ErrorAction SilentlyContinue
        Remove-Item -Recurse -Force -LiteralPath "${runCard}_slot1" -ErrorAction SilentlyContinue
        Copy-Item -Recurse -LiteralPath $cardPath -Destination $runCard

        $distance = if ($arm -eq "render2") { "2" } else { "1" }
        $envs = @(
            "KZ_START_LEVEL=$chapter",
            "KZ_MC_ROOT=$runCard",
            "KZ_RENDERER=d3d12",
            "KZ_RENDER_DISTANCE=$distance",
            "KZ_POST_LOG=1",
            "KZ_AUDIO_DEVICE=0",
            "KZ_FPS=60"
        ) + ($EnvSet -split '[|]' | Where-Object { $_ })
        $result = & "$PSScriptRoot\run_headless.ps1" -Name $runName -Seconds $Seconds -Interval 5 -ExeDir $ExeDir `
            -GameplaySeconds $GameplaySeconds -EnvSet ($envs -join '|') -Root $wd -Iso $Iso -Elf $Elf
        foreach ($kv in $envs) {
            Remove-Item "env:$(($kv -split '=', 2)[0])" -ErrorAction SilentlyContinue
        }

        $out = Get-KzWorkDirectory $wd $runName
        $stdout = Get-Content -Raw "$out\stdout.log" -ErrorAction Stop
        $stderr = Get-Content -Raw "$out\stderr.log" -ErrorAction Stop
        $process = Get-Content -Raw "$out\process.json" -ErrorAction Stop | ConvertFrom-Json
        $sizes = @(
            Get-ChildItem "$out\gs_*.png" -ErrorAction SilentlyContinue |
                Sort-Object Name |
                ForEach-Object { Get-PngSize $_.FullName }
        )
        $problems = @()
        if (($result -join ' ') -notmatch 'exited code=0') { $problems += ($result -join ' ') }
        if ($stdout -notmatch "front end: level $chapter\.") { $problems += "chapter $chapter not reached" }
        if ($GameplaySeconds -gt 0 -and $stdout -notmatch 'headless gameplay limit reached at [0-9]+ rendered frames') {
            $problems += 'gameplay duration not reached'
        }
        if ("$stdout$stderr" -match 'missing-target|fatal exception|0xC0000|unhandled exception') {
            $problems += 'guest/host exception'
        }
        if ($sizes.Count -eq 0) { $problems += 'no frames' }
        elseif (@($sizes | Select-Object -Unique).Count -ne 1) {
            $problems += "frame sizes $(($sizes | Select-Object -Unique) -join ',')"
        }
        if ($arm -eq "render2") {
            if ($stdout -notmatch 'view distance: render x2\.00, LOD x1\.00 \(selected hooks installed\)') {
                $problems += 'render-distance hooks not installed'
            }
            if ($stdout -notmatch 'far plane call [0-9]+:') { $problems += 'no transformed far-plane call' }
            if ($stdout -notmatch 'fog call [0-9]+:') { $problems += 'no transformed fog call' }
        }
        elseif ($stdout -notmatch 'view distance: render x1\.00, LOD x1\.00 \(original; no hooks\)') {
            $problems += 'baseline detail path not original'
        }

        $row = [ordered]@{
            chapter = $chapter
            arm = $arm
            passed = $problems.Count -eq 0
            frames = $sizes.Count
            frame_size = if ($sizes.Count) { $sizes[0] } else { $null }
            peak_working_set_bytes = [int64]$process.peak_working_set_bytes
            peak_paged_memory_bytes = [int64]$process.peak_paged_memory_bytes
            peak_virtual_memory_bytes = [int64]$process.peak_virtual_memory_bytes
            problems = @($problems)
        }
        $rows += [pscustomobject]$row
        if ($problems.Count) {
            ++$failed
            "{0}: FAIL ({1})" -f $runName, ($problems -join '; ')
        }
        else {
            "{0}: pass ({1} frames {2}, peak {3:N1} MiB)" -f `
                $runName, $sizes.Count, $sizes[0], ($process.peak_working_set_bytes / 1MB)
        }
    }
}

$summary = [ordered]@{
    schema_version = 1
    seconds = $Seconds
    gameplay_seconds = $GameplaySeconds
    rows = $rows
}
$summaryPath = Join-Path $wd "work\$Name-summary.json"
$summary | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $summaryPath -Encoding utf8NoBOM
"GFX-03 campaign gate: $($rows.Count - $failed)/$($rows.Count) passed; summary $summaryPath"
if ($failed) { exit 1 }
