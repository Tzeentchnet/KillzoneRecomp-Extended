# Usage: ghidra_callers.ps1 -Addrs "0x2b5928" -Depth 2 -Out .\work\callers.txt
param([string]$Addrs, [int]$Depth = 2, [string]$Out, [string]$Root = $env:KZ_ROOT, [string]$JavaHome, [string]$GhidraHome, [string]$ProjectDir)
. "$PSScriptRoot\kz_paths.ps1"
$Root = Resolve-KzRoot $Root
if (-not $Out) { throw 'Specify -Out.' }
if (-not $JavaHome) { $JavaHome = "$Root\tools\jdk-21.0.12.1+1" }
if (-not $GhidraHome) { $GhidraHome = "$Root\tools\ghidra_12.1.3_PUBLIC" }
if (-not $ProjectDir) { $ProjectDir = "$Root\ghidra" }
$env:JAVA_HOME = $JavaHome; $env:PATH = "$env:JAVA_HOME\bin;$env:PATH"
& "$GhidraHome\support\analyzeHeadless.bat" $ProjectDir Killzone -process SCUS_974.02 -noanalysis -readOnly -scriptPath "$Root\tools\ghidra_scripts" -postScript KzCallers.java $Addrs $Depth $Out *> "$Out.log"
if (Test-Path $Out) { Get-Content $Out } else { "failed; see $Out.log" }
