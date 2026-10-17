[CmdletBinding()]
param(
    [string]$Root = $env:KZ_ROOT,
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\kz_paths.ps1"

$rootPath = Resolve-KzRoot $Root
$lockPath = Join-Path $rootPath 'config\dependencies.lock.json'
$lock = Get-Content -LiteralPath $lockPath -Raw | ConvertFrom-Json
$records = @($lock.tool_requirements | Where-Object { $_.id -eq 'ninja' })
if ($records.Count -ne 1) {
    throw "Expected exactly one Ninja record in $lockPath; found $($records.Count)."
}

$record = $records[0]
$version = $record.requirement -replace '^Exactly\s+', ''
if ($version -notmatch '^\d+\.\d+\.\d+$' -or -not $record.source_url -or -not $record.sha256 -or -not $record.size) {
    throw "The Ninja lock record is incomplete in $lockPath."
}

$toolDir = Join-Path $rootPath 'tools\dl\ninja'
$ninjaExe = Join-Path $toolDir 'ninja.exe'
$archive = Join-Path $rootPath "tools\dl\ninja-$version-win.zip"
$partial = "$archive.partial"

if (Test-Path -LiteralPath $ninjaExe -PathType Leaf) {
    $installedVersion = (& $ninjaExe --version).Trim()
    if ($LASTEXITCODE -eq 0 -and $installedVersion -match "^$([regex]::Escape($version))([.-]|$)") {
        Write-Output "Ninja $installedVersion is already provisioned at $ninjaExe."
        exit 0
    }
    if (-not $Force) {
        throw "Unexpected Ninja version '$installedVersion' at $ninjaExe. Rerun with -Force to replace it."
    }
    Remove-Item -LiteralPath $toolDir -Recurse -Force
}

New-Item -ItemType Directory -Force (Split-Path -Parent $archive) | Out-Null
if (Test-Path -LiteralPath $archive -PathType Leaf) {
    $archiveItem = Get-Item -LiteralPath $archive
    $archiveHash = Get-KzSha256 $archive
    if ($archiveItem.Length -ne [long]$record.size -or $archiveHash -ne $record.sha256) {
        if (-not $Force) {
            throw "The cached Ninja archive does not match the dependency lock. Rerun with -Force to replace it."
        }
        Remove-Item -LiteralPath $archive -Force
    }
}

try {
    if (-not (Test-Path -LiteralPath $archive -PathType Leaf)) {
        Invoke-WebRequest -UseBasicParsing -Uri $record.source_url -OutFile $partial
        $download = Get-Item -LiteralPath $partial
        $downloadHash = Get-KzSha256 $partial
        if ($download.Length -ne [long]$record.size) {
            throw "Ninja archive size mismatch: expected $($record.size), got $($download.Length)."
        }
        if ($downloadHash -ne $record.sha256) {
            throw "Ninja archive SHA-256 mismatch: expected $($record.sha256), got $downloadHash."
        }
        Move-Item -LiteralPath $partial -Destination $archive
    }

    Expand-Archive -LiteralPath $archive -DestinationPath $toolDir -Force
} finally {
    if (Test-Path -LiteralPath $partial) {
        Remove-Item -LiteralPath $partial -Force
    }
}

if (-not (Test-Path -LiteralPath $ninjaExe -PathType Leaf)) {
    throw "The locked Ninja archive did not contain ninja.exe."
}
$installedVersion = (& $ninjaExe --version).Trim()
if ($LASTEXITCODE -ne 0 -or $installedVersion -notmatch "^$([regex]::Escape($version))([.-]|$)") {
    throw "Provisioned Ninja version '$installedVersion' does not match $version."
}
Write-Output "Provisioned Ninja $installedVersion at $ninjaExe."
