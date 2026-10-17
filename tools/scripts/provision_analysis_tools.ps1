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
$toolIds = @('temurin-jdk', 'ghidra')

foreach ($toolId in $toolIds) {
    $records = @($lock.tool_requirements | Where-Object { $_.id -eq $toolId })
    if ($records.Count -ne 1) {
        throw "Expected exactly one $toolId record in $lockPath; found $($records.Count)."
    }
    $record = $records[0]
    if (-not $record.source_url -or -not $record.sha256 -or -not $record.size -or
        -not $record.local.archive -or -not $record.local.directory -or -not $record.local.markers) {
        throw "The $toolId lock record is incomplete in $lockPath."
    }

    $archive = Join-Path $rootPath $record.local.archive
    $destination = Join-Path $rootPath $record.local.directory
    $partial = "$archive.partial"
    $markersPresent = @($record.local.markers | Where-Object {
        -not (Test-Path -LiteralPath (Join-Path $destination $_))
    }).Count -eq 0

    if ($markersPresent) {
        Write-Output "$toolId is already provisioned at $destination."
        continue
    }
    if ((Test-Path -LiteralPath $destination) -and -not $Force) {
        throw "$destination is incomplete. Rerun with -Force to replace it."
    }
    if (Test-Path -LiteralPath $destination) {
        Remove-Item -LiteralPath $destination -Recurse -Force
    }

    New-Item -ItemType Directory -Force (Split-Path -Parent $archive) | Out-Null
    if (Test-Path -LiteralPath $archive -PathType Leaf) {
        $archiveItem = Get-Item -LiteralPath $archive
        $archiveHash = Get-KzSha256 $archive
        if ($archiveItem.Length -ne [long]$record.size -or $archiveHash -ne $record.sha256) {
            if (-not $Force) {
                throw "The cached $toolId archive does not match the dependency lock. Rerun with -Force to replace it."
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
                throw "$toolId archive size mismatch: expected $($record.size), got $($download.Length)."
            }
            if ($downloadHash -ne $record.sha256) {
                throw "$toolId archive SHA-256 mismatch: expected $($record.sha256), got $downloadHash."
            }
            Move-Item -LiteralPath $partial -Destination $archive
        }

        Expand-Archive -LiteralPath $archive -DestinationPath (Split-Path -Parent $destination) -Force
    } finally {
        if (Test-Path -LiteralPath $partial) {
            Remove-Item -LiteralPath $partial -Force
        }
    }

    $missingMarkers = @($record.local.markers | Where-Object {
        -not (Test-Path -LiteralPath (Join-Path $destination $_))
    })
    if ($missingMarkers) {
        throw "$toolId extraction is incomplete at $destination; missing $($missingMarkers -join ', ')."
    }
    Write-Output "Provisioned $toolId at $destination."
}
