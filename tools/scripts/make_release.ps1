# Stages and archives only audited runtime inputs. Legal and protected-content review is still required.
param(
    [Parameter(Mandatory)]
    [ValidatePattern('^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$')]
    [string]$Version,
    [string]$From = '',
    [string]$OutputDirectory = '',
    [string]$CrtDirectory = '',
    [switch]$DryRun
)

. "$PSScriptRoot\package_manifest.ps1"
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if (-not $From) { $From = Join-Path $root 'dist\Killzone' }
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $root 'work\release' }
if (-not $CrtDirectory) { $CrtDirectory = Find-KzCrtDirectory }
$From = [IO.Path]::GetFullPath($From)
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
$CrtDirectory = [IO.Path]::GetFullPath($CrtDirectory)

$name = "Killzone-PC-v$Version-alpha"
$stage = Join-Path $OutputDirectory $name
$package = Join-Path $stage 'Killzone'
$zip = Join-Path $OutputDirectory "$name.zip"
Assert-KzPlainPath $OutputDirectory
Assert-KzPlainPath $stage
Assert-KzPlainPath $zip
if ((Test-Path -LiteralPath $stage) -or (Test-Path -LiteralPath $zip)) {
    throw "Release stage or archive already exists; choose a fresh version and output directory: $stage"
}

$plan = @(Get-KzPackagePlan -Root $root -InputDirectory $From)
$crtPlan = @(Get-KzCrtPlan -Root $root -CrtDirectory $CrtDirectory)
$all = @($plan) + @($crtPlan)
$readme = Join-Path $root 'tools\release\README.txt'
[void](Get-KzPackageFileHash $readme)
if ($DryRun) {
    foreach ($entry in $all) { "INCLUDE Killzone\$($entry.Relative)" }
    'INCLUDE Killzone\README.txt'
    "DRY RUN: $($all.Count + 1) files -> $zip; vendor SDKs, caches, symbols and unlisted files excluded"
    'REVIEW REQUIRED: protected content, notices, corresponding source and redistribution rights.'
    return
}

New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
Copy-KzPackagePlan -Plan $all -Destination $package
$releaseReadme = Join-Path $package 'README.txt'
(Get-Content -LiteralPath $readme -Raw).Replace('{VERSION}', $Version) |
    Set-Content -LiteralPath $releaseReadme -Encoding UTF8

Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$stream = $null
$archive = $null
$created = $false
try {
    Assert-KzPlainPath $zip
    $stream = [IO.File]::Open($zip, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
    $created = $true
    $archive = [IO.Compression.ZipArchive]::new($stream, [IO.Compression.ZipArchiveMode]::Create, $false)
    foreach ($entry in $all) {
        $file = Join-Path $package $entry.Relative
        if ((Get-KzPackageFileHash $file) -cne $entry.Hash) {
            throw "Release staging file changed before archiving: $($entry.Relative)"
        }
        $archiveName = 'Killzone/' + $entry.Relative.Replace('\', '/')
        [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
            $archive, $file, $archiveName, [IO.Compression.CompressionLevel]::Optimal)
    }
    [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
        $archive, $releaseReadme, 'Killzone/README.txt', [IO.Compression.CompressionLevel]::Optimal)
} catch {
    if ($archive) { $archive.Dispose(); $archive = $null }
    if ($stream) { $stream.Dispose(); $stream = $null }
    if ($created -and (Test-Path -LiteralPath $zip)) { Remove-Item -LiteralPath $zip -Force }
    throw
} finally {
    if ($archive) { $archive.Dispose() }
    if ($stream) { $stream.Dispose() }
}
"{0}  {1:N0} MB  (runtime from {2})" -f $zip, ((Get-Item -LiteralPath $zip).Length / 1MB), $CrtDirectory
'REVIEW REQUIRED: protected content, notices, corresponding source and redistribution rights.'
