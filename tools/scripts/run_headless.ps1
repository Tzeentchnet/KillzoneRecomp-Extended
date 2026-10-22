# Usage: run_headless.ps1 -Name h1 -Seconds 90 -Interval 5 [-Extra "--sample 15"]
# Boots killzone.exe without a window (works while the PC is locked). Frames -> work/<Name>/gs_*.png (kzgs readback),
# heartbeat lines ([headless] ...) -> work/<Name>/stderr.log.
# -ExeDir runs another copy of the exe (build/<Config> when empty), -EnvSet "A=1|B=2" sets environment variables for the run
# (a separator other than ; because KZ_INPUT_SCRIPT values contain it). Example: -EnvSet "KZ_WINDOW_SIZE=2560x1440|KZ_SHOT_PRESENT=1"
# also writes gp_*.png, the frame as presented in a window of that size, next to the gs_*.png internal-resolution frames.
param([string]$Name = "h", [int]$Seconds = 90, [int]$GameplaySeconds = 0, [int]$Interval = 5, [string]$Config = "RelWithDebInfo", [string]$Extra = "", [switch]$NoIso, [string]$ExeDir = "", [string]$EnvSet = "", [string]$Root = $env:KZ_ROOT, [string]$Iso = $env:KZ_ISO, [string]$Elf = $env:KZ_GAME_ELF)
. "$PSScriptRoot\kz_paths.ps1"
$wd = Resolve-KzRoot $Root
$elf = Resolve-KzRequiredFile $Elf KZ_GAME_ELF Elf
if (-not $NoIso) { $iso = Resolve-KzRequiredFile $Iso KZ_ISO Iso }
$out = Get-KzWorkDirectory $wd $Name; Remove-Item -Recurse -Force $out -ErrorAction SilentlyContinue; New-Item -ItemType Directory -Force $out | Out-Null
$env:PS2X_HEADLESS = '1'; $env:PS2X_HEADLESS_INTERVAL = "$Interval"; $env:PS2X_HEADLESS_SECONDS = "$Seconds"
$env:KZ_SHOT_DIR = $out; $env:KZ_SHOT_INTERVAL = "$Interval"   # frames from the hardware GS (kzgs)
if ($GameplaySeconds -gt 0) { $env:KZ_HEADLESS_GAMEPLAY_SECONDS = "$GameplaySeconds" }
else { Remove-Item env:KZ_HEADLESS_GAMEPLAY_SECONDS -ErrorAction SilentlyContinue }
foreach ($kv in ($EnvSet -split '[|]' | Where-Object { $_ })) { $k, $v = $kv -split '=', 2; Set-Item "env:$k" $v }
if (-not $ExeDir) { $ExeDir = "$wd\build\$Config" }
$p = Start-Process "$ExeDir\killzone.exe" -ArgumentList (@('--elf', "`"$elf`"") + $(if ($NoIso) { @() } else { @('--iso', "`"$iso`"") }) + ($Extra -split " " | Where-Object { $_ })) `
    -WorkingDirectory $wd -RedirectStandardOutput "$out\stdout.log" -RedirectStandardError "$out\stderr.log" -PassThru -NoNewWindow
$peakWorkingSet = [int64]0
$peakPagedMemory = [int64]0
$peakVirtualMemory = [int64]0
$samples = 0
$deadline = [DateTime]::UtcNow.AddSeconds($Seconds + 30)
while (-not $p.HasExited -and [DateTime]::UtcNow -lt $deadline) {
    $p.Refresh()
    $peakWorkingSet = [math]::Max($peakWorkingSet, $p.WorkingSet64)
    $peakPagedMemory = [math]::Max($peakPagedMemory, $p.PagedMemorySize64)
    $peakVirtualMemory = [math]::Max($peakVirtualMemory, $p.VirtualMemorySize64)
    ++$samples
    Start-Sleep -Milliseconds 250
}
$timedOut = -not $p.HasExited
if ($timedOut) {
    Stop-Process -Id $p.Id -Force
}
$p.WaitForExit()
$processMetrics = [ordered]@{
    schema_version = 1
    exit_code = $p.ExitCode
    timed_out = $timedOut
    sample_count = $samples
    peak_working_set_bytes = $peakWorkingSet
    peak_paged_memory_bytes = $peakPagedMemory
    peak_virtual_memory_bytes = $peakVirtualMemory
    total_processor_ms = [math]::Round($p.TotalProcessorTime.TotalMilliseconds, 3)
}
$processMetrics | ConvertTo-Json -Compress | Set-Content -LiteralPath "$out\process.json" -Encoding utf8NoBOM
if ($timedOut) { "hung past limit (killed)" } else { "exited code=$($p.ExitCode)" }
"peak working set: {0:N1} MiB" -f ($peakWorkingSet / 1MB)
"frames: " + (Get-ChildItem "$out\gs_*.png" -ErrorAction SilentlyContinue).Count
