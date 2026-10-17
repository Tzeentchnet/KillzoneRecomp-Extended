[CmdletBinding()]
param(
    [string]$Elf = $env:KZ_GAME_ELF,
    [string]$Root = $env:KZ_ROOT,
    [string]$Output,
    [string]$Metadata,
    [string]$GhidraHome,
    [string]$JavaHome
)

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\kz_paths.ps1"

$rootPath = Resolve-KzRoot $Root
$elfPath = Resolve-KzRequiredFile $Elf 'KZ_GAME_ELF' 'Elf'
if (-not $Output) { $Output = Join-Path $rootPath 'work\functions.csv' }
if (-not $Metadata) { $Metadata = "$Output.meta.json" }
if (-not $GhidraHome) { $GhidraHome = Join-Path $rootPath 'tools\ghidra_12.1.3_PUBLIC' }
if (-not $JavaHome) { $JavaHome = Join-Path $rootPath 'tools\jdk-21.0.12.1+1' }

$headless = Join-Path $GhidraHome 'support\analyzeHeadless.bat'
$java = Join-Path $JavaHome 'bin\java.exe'
$exporter = Join-Path $rootPath 'ext\PS2Recomp\ps2xRecomp\tools\ghidra\ExportPS2Functions.java'
$normalizer = Join-Path $rootPath 'tools\scripts\normalize_function_map.py'
foreach ($required in @($headless, $java, $exporter, $normalizer)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required analysis tool is missing: $required. Run tools\scripts\provision_analysis_tools.ps1."
    }
}

$outputPath = [IO.Path]::GetFullPath($Output)
$metadataPath = [IO.Path]::GetFullPath($Metadata)
$rawOutputPath = "$outputPath.raw-$PID"
$workDir = Join-Path $rootPath 'work'
$exportToml = Join-Path $workDir 'ghidra-export.toml'
$projectName = "KillzoneExport_$PID"
New-Item -ItemType Directory -Force $workDir, (Split-Path -Parent $outputPath), (Split-Path -Parent $metadataPath) |
    Out-Null

$env:JAVA_HOME = $JavaHome
$arguments = @(
    $workDir,
    $projectName,
    '-import',
    $elfPath,
    '-analysisTimeoutPerFile',
    '1800',
    '-scriptPath',
    (Split-Path -Parent $exporter),
    '-postScript',
    (Split-Path -Leaf $exporter),
    $exportToml,
    $rawOutputPath,
    '-deleteProject'
)
try {
    & $headless @arguments
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
    if (-not (Test-Path -LiteralPath $rawOutputPath -PathType Leaf)) {
        throw "Ghidra completed without creating $rawOutputPath."
    }
    & python $normalizer --elf $elfPath --input $rawOutputPath --output $outputPath --metadata $metadataPath --exporter $exporter
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}
finally {
    Remove-Item -LiteralPath $rawOutputPath -Force -ErrorAction SilentlyContinue
}
