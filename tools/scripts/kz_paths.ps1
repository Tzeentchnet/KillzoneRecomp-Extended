$script:KzDefaultRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))

function Resolve-KzRoot {
    param([string]$Root)

    if ([string]::IsNullOrWhiteSpace($Root)) { $Root = $env:KZ_ROOT }
    if ([string]::IsNullOrWhiteSpace($Root)) { $Root = $script:KzDefaultRoot }
    $Root = [IO.Path]::GetFullPath($Root)
    if (-not (Test-Path -LiteralPath (Join-Path $Root 'CMakeLists.txt') -PathType Leaf)) {
        throw "KillzoneRecomp root is invalid: $Root"
    }
    return $Root
}

function Resolve-KzRequiredFile {
    param(
        [string]$Value,
        [string]$EnvironmentName,
        [string]$ParameterName
    )

    if ([string]::IsNullOrWhiteSpace($Value)) {
        $Value = [Environment]::GetEnvironmentVariable($EnvironmentName)
    }
    if ([string]::IsNullOrWhiteSpace($Value)) {
        throw "Specify -$ParameterName or set $EnvironmentName."
    }
    if (-not (Test-Path -LiteralPath $Value -PathType Leaf)) {
        throw "$ParameterName does not exist: $Value"
    }
    return (Resolve-Path -LiteralPath $Value).Path
}

function Get-KzWorkDirectory {
    param(
        [string]$Root,
        [string]$Name
    )

    if ([string]::IsNullOrWhiteSpace($Name) -or $Name -ne [IO.Path]::GetFileName($Name)) {
        throw "Work name must be a single path component: $Name"
    }
    return (Join-Path (Join-Path $Root 'work') $Name)
}

function Get-KzSha256 {
    param([string]$Path)

    $resolved = Resolve-KzRequiredFile $Path 'KZ_UNUSED' 'Path'
    $stream = [IO.File]::OpenRead($resolved)
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try {
        $hash = $algorithm.ComputeHash($stream)
        return ([BitConverter]::ToString($hash)).Replace('-', '').ToLowerInvariant()
    } finally {
        $algorithm.Dispose()
        $stream.Dispose()
    }
}
