# Runs each configured campaign scenario sequentially at 30/60/120/144 Hz with a fresh private memory card.
# Raw logs, captures, cards, and parity traces stay under ignored work/. The analyzer emits a content-safe summary.
param(
    [int[]]$Chapters = @(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11),
    [int[]]$Rates = @(30, 60, 120, 144),
    [string]$Name = "high_refresh_campaign",
    [Parameter(Mandatory)][string]$Card,
    [Parameter(Mandatory)][string]$Scenario,
    [int]$Seconds = 120,
    [int]$GameplaySeconds = 45,
    [int]$Interval = 10,
    [int]$Difficulty = 1,
    [string]$Renderer = "d3d12",
    [string]$ExeDir = "",
    [string]$EnvSet = "",
    [string]$Root = $env:KZ_ROOT,
    [string]$Iso = $env:KZ_ISO,
    [string]$Elf = $env:KZ_GAME_ELF
)

. "$PSScriptRoot\kz_paths.ps1"
$wd = Resolve-KzRoot $Root
$null = Get-KzWorkDirectory $wd $Name
$isoPath = Resolve-KzRequiredFile $Iso KZ_ISO Iso
$elfPath = Resolve-KzRequiredFile $Elf KZ_GAME_ELF Elf
if (-not (Test-Path -LiteralPath $Card -PathType Container)) {
    throw "Card directory does not exist: $Card"
}
$cardPath = (Resolve-Path -LiteralPath $Card).Path
$scenarioPath = Resolve-KzRequiredFile $Scenario KZ_HIGH_REFRESH_SCENARIO Scenario
if (-not $ExeDir) { $ExeDir = Join-Path $wd "build\win-msvc-rel" }
if (-not (Test-Path -LiteralPath (Join-Path $ExeDir "killzone.exe") -PathType Leaf)) {
    throw "killzone.exe does not exist under ExeDir: $ExeDir"
}
if ($Difficulty -lt 0 -or $Difficulty -gt 2) { throw "Difficulty must be 0, 1 or 2." }
if ($Seconds -le 0 -or $GameplaySeconds -le 0 -or $Interval -le 0) {
    throw "Seconds, GameplaySeconds and Interval must be positive."
}
if ($Rates -notcontains 30) { throw "Rates must include the 30 Hz reference." }
if (@($Rates | Select-Object -Unique).Count -ne $Rates.Count) { throw "Rates must be unique." }
foreach ($rate in $Rates) {
    if ($rate -lt 30 -or $rate -gt 480) { throw "Rate $rate is outside 30..480." }
}
if (@($Chapters | Select-Object -Unique).Count -ne $Chapters.Count) { throw "Chapters must be unique." }
foreach ($chapter in $Chapters) {
    if ($chapter -lt 1 -or $chapter -gt 11) { throw "Chapter $chapter is outside 1..11." }
}

$manifest = Get-Content -Raw -LiteralPath $scenarioPath | ConvertFrom-Json
if ($manifest.schema_version -ne 1 -or $null -eq $manifest.chapters) {
    throw "Scenario must use schema_version 1 and contain a chapters array."
}
$scenarios = @{}
foreach ($entry in @($manifest.chapters)) {
    if ($null -eq $entry.chapter -or $scenarios.ContainsKey([int]$entry.chapter)) {
        throw "Every scenario chapter must be present once."
    }
    $scenarios[[int]$entry.chapter] = $entry
}
foreach ($chapter in $Chapters) {
    if (-not $scenarios.ContainsKey($chapter)) {
        throw "Scenario has no entry for chapter $chapter."
    }
}

function Add-AuditEnvironment {
    param([System.Collections.Generic.List[string]]$Values, [string]$Key, [string]$Value)
    if ($Key -notmatch '^[A-Z][A-Z0-9_]*$') { throw "Invalid environment name in scenario: $Key" }
    if ($Value.Contains('|')) { throw "Environment value for $Key cannot contain '|'." }
    $Values.Add("$Key=$Value")
}

$runs = 0
foreach ($chapter in $Chapters) {
    $entry = $scenarios[$chapter]
    foreach ($rate in $Rates) {
        $runName = "{0}_c{1:D2}_{2}hz" -f $Name, $chapter, $rate
        $out = Get-KzWorkDirectory $wd $runName
        $runCard = Get-KzWorkDirectory $wd "${runName}_card"
        Remove-Item -Recurse -Force -LiteralPath $runCard -ErrorAction SilentlyContinue
        Remove-Item -Recurse -Force -LiteralPath "${runCard}_slot1" -ErrorAction SilentlyContinue
        Copy-Item -Recurse -LiteralPath $cardPath -Destination $runCard

        $envs = [System.Collections.Generic.List[string]]::new()
        Add-AuditEnvironment $envs "KZ_START_LEVEL" "$chapter"
        Add-AuditEnvironment $envs "KZ_START_DIFFICULTY" "$Difficulty"
        Add-AuditEnvironment $envs "KZ_MC_ROOT" "$runCard"
        Add-AuditEnvironment $envs "KZ_RENDERER" "$Renderer"
        Add-AuditEnvironment $envs "KZ_FPS" "$rate"
        Add-AuditEnvironment $envs "KZ_TIMING_LOG" "1"
        Add-AuditEnvironment $envs "KZ_FRONTEND_LOG" "1"
        Add-AuditEnvironment $envs "KZ_AUDIO_DEVICE" "0"
        Add-AuditEnvironment $envs "KZ_PARITY_LOG" (Join-Path $out "parity.csv")
        if (-not [string]::IsNullOrWhiteSpace([string]$entry.input_script)) {
            Add-AuditEnvironment $envs "KZ_INPUT_SCRIPT" ([string]$entry.input_script)
        }
        $parityWords = @($entry.parity_words | ForEach-Object { [string]$_ })
        if ($parityWords.Count) {
            Add-AuditEnvironment $envs "KZ_PARITY_WORDS" ($parityWords -join ',')
        }
        if ($null -ne $entry.environment) {
            foreach ($property in $entry.environment.PSObject.Properties) {
                Add-AuditEnvironment $envs $property.Name ([string]$property.Value)
            }
        }
        foreach ($item in ($EnvSet -split '[|]' | Where-Object { $_ })) {
            $key, $value = $item -split '=', 2
            Add-AuditEnvironment $envs $key $value
        }

        "Running chapter $chapter at $rate Hz ($($runs + 1)/$($Chapters.Count * $Rates.Count))..."
        & "$PSScriptRoot\run_headless.ps1" -Name $runName -Seconds $Seconds -GameplaySeconds $GameplaySeconds `
            -Interval $Interval -ExeDir $ExeDir -EnvSet ($envs -join '|') -Root $wd -Iso $isoPath -Elf $elfPath
        foreach ($item in $envs) {
            Remove-Item "env:$(($item -split '=', 2)[0])" -ErrorAction SilentlyContinue
        }
        ++$runs
    }
}

foreach ($key in @(
    "PS2X_HEADLESS", "PS2X_HEADLESS_INTERVAL", "PS2X_HEADLESS_SECONDS",
    "KZ_HEADLESS_GAMEPLAY_SECONDS", "KZ_SHOT_DIR", "KZ_SHOT_INTERVAL"
)) {
    Remove-Item "env:$key" -ErrorAction SilentlyContinue
}

$summary = Join-Path $wd "work\$Name-summary.json"
$arguments = @(
    "$PSScriptRoot\high_refresh_analyze.py",
    "--work-root", (Join-Path $wd "work"),
    "--name", $Name,
    "--scenario", $scenarioPath,
    "--gameplay-seconds", "$GameplaySeconds",
    "--output", $summary,
    "--rates"
) + @($Rates | ForEach-Object { "$_" }) + @("--chapters") + @($Chapters | ForEach-Object { "$_" })
& python @arguments
exit $LASTEXITCODE
