# Repeat issue-4 runs (reporter card, Continue, walk forward, KZ_KILLALL clears the waves) until the guest stalls (dma/vif counters stop for 3 heartbeats).
# Usage: i4hunt.ps1 -Tries 6 -Seconds 200 [-ExeDir dir] [-Script "105:ly=-1:12"] [-EnvSet "..."] ; logs in work\i4h_<n>; prints one line per try.
param([int]$Tries = 6, [int]$Seconds = 200, [string]$ExeDir = "D:\KillzoneRecomp\build\agenta", [string]$Script = "105:ly=-1:12", [string]$EnvSet = "KZ_GOD=1|KZ_ACTORS=10|KZ_KILLALL=135,2|PS2X_HEARTBEAT_THREADS=1", [string]$Extra = "", [int]$Start = 1)
for ($n = $Start; $n -lt $Start + $Tries; $n++) {
    & D:\KillzoneRecomp\.weaver\i4run.ps1 -Name "i4h_$n" -Seconds $Seconds -Interval 10 -ExeDir $ExeDir -Script $Script -EnvSet $EnvSet -Extra $Extra | Out-Null
    $hb = Select-String -Path "D:\KillzoneRecomp\work\i4h_$n\stderr.log" -Pattern '^\[headless\] t=' | ForEach-Object { $_.Line }
    $dma = $hb | ForEach-Object { if ($_ -match 'vif=(\d+)') { [int]$Matches[1] } }
    $stall = $false
    for ($i = 3; $i -lt $dma.Count; $i++) { if ($dma[$i] -eq $dma[$i-1] -and $dma[$i-1] -eq $dma[$i-2] -and $dma[$i-2] -eq $dma[$i-3]) { $stall = $true; break } }
    "try $n : " + $(if ($stall) { "STALL" } else { "ok" }) + " (heartbeats $($dma.Count))"
    if ($stall) { break }
}
