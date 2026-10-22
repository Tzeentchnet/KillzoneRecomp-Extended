# Issue #4 runs: headless run from a fresh COPY of the reporter's card (work\saves\issue4_reporter\mc0).
# Usage: i4run.ps1 -Name x -Seconds 300 -Interval 5 [-Script "110:r1:1000;..."] [-Menu "<menu part of the input script>"] [-ExeDir dir] [-EnvSet "A=1|B=2"] [-Card dir]
# Default menu: start, Game, Campaign, profile, then 3 more crosses (level, difficulty, class) 12 s apart. Silent (headless, no audio).
param([string]$Name = "i4", [int]$Seconds = 300, [int]$Interval = 5, [string]$Script = "", [string]$Menu = "30:start:0.3;38:cross:0.3;46:cross:0.3;54:cross:0.3;66:cross:0.3;78:cross:0.3;90:cross:0.3", [string]$ExeDir = "D:\KillzoneRecomp\dist\Killzone", [string]$EnvSet = "", [string]$Extra = "", [string]$Card = "D:\KillzoneRecomp\work\saves\issue4_reporter\mc0")
$mc = "D:\KillzoneRecomp\work\saves\${Name}_mc"
if (Test-Path $mc) { Remove-Item -Recurse -Force $mc }
Copy-Item -Recurse $Card $mc
$s = $Menu
if ($Script) { $s += ";" + $Script }
$env = "KZ_IPU=off|KZ_MC_ROOT=$mc|KZ_INPUT_SCRIPT=$s"
if ($EnvSet) { $env += "|" + $EnvSet }
& D:\KillzoneRecomp\tools\scripts\run_headless.ps1 -Name $Name -Seconds $Seconds -Interval $Interval -Extra ("--no-launcher " + $Extra) -ExeDir $ExeDir -EnvSet $env
