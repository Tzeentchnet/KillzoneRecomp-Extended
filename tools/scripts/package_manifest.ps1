# Shared, fail-closed package inventory. No directory is copied recursively.

function Assert-KzPlainPath {
    param([Parameter(Mandatory)][string]$Path)

    $probe = [IO.Path]::GetFullPath($Path)
    while ($probe) {
        if (Test-Path -LiteralPath $probe) {
            $item = Get-Item -LiteralPath $probe -Force
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw "Package path traverses a reparse point: $probe"
            }
        }
        $parent = [IO.Path]::GetDirectoryName($probe)
        if (-not $parent -or $parent -eq $probe) { break }
        $probe = $parent
    }
}

function Get-KzPackageFileHash {
    param([Parameter(Mandatory)][string]$Path)

    Assert-KzPlainPath $Path
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Required package input is missing: $Path"
    }
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Assert-KzGameExecutable {
    param([Parameter(Mandatory)][string]$Path)

    $reader = [IO.BinaryReader]::new([IO.File]::OpenRead($Path))
    $valid = $false
    try {
        if ($reader.BaseStream.Length -ge 64 -and $reader.ReadUInt16() -eq 0x5a4d) {
            $reader.BaseStream.Position = 0x3c
            $offset = $reader.ReadUInt32()
            if ($offset -le ($reader.BaseStream.Length - 26)) {
                $reader.BaseStream.Position = $offset
                $signature = $reader.ReadUInt32()
                $machine = $reader.ReadUInt16()
                $sections = $reader.ReadUInt16()
                $reader.BaseStream.Position = $offset + 22
                $flags = $reader.ReadUInt16()
                $magic = $reader.ReadUInt16()
                $valid = $signature -eq 0x4550 -and $machine -eq 0x8664 -and $sections -gt 0 -and
                    ($flags -band 0x0002) -ne 0 -and ($flags -band 0x2000) -eq 0 -and $magic -eq 0x20b
            }
        }
    } finally { $reader.Dispose() }
    if (-not $valid) { throw "Package input is not a Windows x64 executable (DLLs are prohibited): $Path" }
}

function Get-KzPackageManifest {
    param([Parameter(Mandatory)][string]$Root)

    $path = Join-Path $Root 'config\package-allowlist.json'
    [void](Get-KzPackageFileHash $path)
    $manifest = Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
    if ($manifest.schema_version -ne 1 -or $manifest.resource_count -le 0 -or
        $manifest.resource_inventory_sha256 -cnotmatch '^[0-9a-f]{64}$' -or
        @($manifest.runtime_dlls).Count -eq 0 -or @($manifest.msvc_runtime_dlls).Count -eq 0) {
        throw 'Invalid package allowlist.'
    }
    $names = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($dll in $manifest.runtime_dlls) {
        if ($dll.name -cnotmatch '^[A-Za-z0-9][A-Za-z0-9_.-]*\.dll$' -or
            $dll.name -match '(?i)dlss|xess|nvngx|nvsdk_ngx|kzgs_upscaler' -or
            $dll.sha256 -cnotmatch '^[0-9a-f]{64}$' -or
            $dll.dependency -notin @('sdl3-vc', 'ffmpeg-msvc', 'pcsx2-windows-dependencies') -or
            -not $names.Add($dll.name)) {
            throw "Invalid or prohibited runtime DLL allowlist entry: $($dll.name)"
        }
    }
    foreach ($dll in $manifest.msvc_runtime_dlls) {
        if ($dll.name -cnotmatch '^(msvcp140(?:_1|_2|_atomic_wait)?|vcruntime140(?:_1)?)\.dll$' -or
            $dll.sha256 -cnotmatch '^[0-9a-f]{64}$' -or -not $names.Add($dll.name)) {
            throw "Invalid Visual C++ runtime allowlist entry: $($dll.name)"
        }
    }
    return $manifest
}

function Get-KzPackageResourcePlan {
    param(
        [Parameter(Mandatory)][string]$Root,
        [Parameter(Mandatory)][string]$InputDirectory,
        [Parameter(Mandatory)]$Manifest
    )

    $mappings = @(
        @{ Source = 'resources\prompts'; Destination = 'resources\prompts'; Extensions = @('.png', '.txt') },
        @{ Source = 'ext\pcsx2\bin\resources\shaders\common'; Destination = 'resources\shaders\common'; Extensions = @('.h', '.fx') },
        @{ Source = 'ext\pcsx2\bin\resources\shaders\dx11'; Destination = 'resources\shaders\dx11'; Extensions = @('.hlsl', '.fx') },
        @{ Source = 'ext\pcsx2\bin\resources\shaders\opengl'; Destination = 'resources\shaders\opengl'; Extensions = @('.glsl') },
        @{ Source = 'ext\pcsx2\bin\resources\shaders\vulkan'; Destination = 'resources\shaders\vulkan'; Extensions = @('.glsl') },
        @{ Source = 'ext\kzgs\shaders'; Destination = 'resources\shaders\kzgs'; Extensions = @('.glsl', '.hlsl', '.txt') },
        @{ Source = 'ext\kzgs\third_party\fsr1'; Destination = 'resources\shaders\kzgs\fsr1'; Extensions = @('.h', '.txt', '.md') }
    )
    $inventory = [Collections.Generic.SortedDictionary[string,string]]::new([StringComparer]::Ordinal)
    $entries = [Collections.Generic.List[object]]::new()
    foreach ($mapping in $mappings) {
        $directory = Join-Path $Root $mapping.Source
        Assert-KzPlainPath $directory
        if (-not (Test-Path -LiteralPath $directory -PathType Container)) {
            throw "Audited resource source is missing: $directory"
        }
        foreach ($file in (Get-ChildItem -LiteralPath $directory -File)) {
            if ($file.Extension.ToLowerInvariant() -notin $mapping.Extensions) { continue }
            $relative = Join-Path $mapping.Destination $file.Name
            $sourceHash = Get-KzPackageFileHash $file.FullName
            $input = Join-Path $InputDirectory $relative
            $inputHash = Get-KzPackageFileHash $input
            if ($sourceHash -cne $inputHash) {
                throw "Staged resource does not match the audited source: $relative"
            }
            $inventory.Add($relative.ToLowerInvariant(), $sourceHash)
            $entries.Add([pscustomobject]@{ Source = $input; Relative = $relative; Hash = $inputHash })
        }
    }
    $canonical = [Text.StringBuilder]::new()
    foreach ($entry in $inventory.GetEnumerator()) {
        [void]$canonical.Append($entry.Key).Append('|').Append($entry.Value).Append("`n")
    }
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        $digest = [BitConverter]::ToString($sha.ComputeHash(
            [Text.Encoding]::UTF8.GetBytes($canonical.ToString()))).Replace('-', '').ToLowerInvariant()
    } finally {
        $sha.Dispose()
    }
    if ($inventory.Count -ne $Manifest.resource_count -or $digest -cne $Manifest.resource_inventory_sha256) {
        throw "Audited resource inventory changed: count=$($inventory.Count) sha256=$digest"
    }
    return $entries.ToArray()
}

function Get-KzPackagePlan {
    param(
        [Parameter(Mandatory)][string]$Root,
        [Parameter(Mandatory)][string]$InputDirectory,
        [switch]$WithPdb
    )

    $manifest = Get-KzPackageManifest $Root
    Assert-KzPlainPath $InputDirectory
    if (-not (Test-Path -LiteralPath $InputDirectory -PathType Container)) {
        throw "Package input directory does not exist: $InputDirectory"
    }
    $entries = [Collections.Generic.List[object]]::new()
    $exe = Join-Path $InputDirectory 'killzone.exe'
    $exeHash = Get-KzPackageFileHash $exe
    Assert-KzGameExecutable $exe
    $entries.Add([pscustomobject]@{ Source = $exe; Relative = 'killzone.exe'; Hash = $exeHash })
    foreach ($dll in $manifest.runtime_dlls) {
        $file = Join-Path $InputDirectory $dll.name
        $hash = Get-KzPackageFileHash $file
        if ($hash -cne $dll.sha256) {
            throw "Runtime DLL does not match its audited hash: $($dll.name)"
        }
        $entries.Add([pscustomobject]@{ Source = $file; Relative = $dll.name; Hash = $hash })
    }
    foreach ($entry in (Get-KzPackageResourcePlan $Root $InputDirectory $manifest)) {
        $entries.Add($entry)
    }
    foreach ($name in @('LICENSE', 'LICENSE-GPL-3.0')) {
        $file = Join-Path $Root $name
        $entries.Add([pscustomobject]@{ Source = $file; Relative = $name; Hash = (Get-KzPackageFileHash $file) })
    }
    if ($WithPdb) {
        $pdb = Join-Path $InputDirectory 'killzone.pdb'
        $entries.Add([pscustomobject]@{ Source = $pdb; Relative = 'killzone.pdb'; Hash = (Get-KzPackageFileHash $pdb) })
    }
    return $entries.ToArray()
}

function Assert-KzFreshDirectory {
    param([Parameter(Mandatory)][string]$Path)

    Assert-KzPlainPath $Path
    if (Test-Path -LiteralPath $Path) {
        if (-not (Test-Path -LiteralPath $Path -PathType Container) -or
            @(Get-ChildItem -LiteralPath $Path -Force).Count -ne 0) {
            throw "Destination must be new or empty; existing player data is never removed: $Path"
        }
    }
}

function Copy-KzPackagePlan {
    param([Parameter(Mandatory)]$Plan, [Parameter(Mandatory)][string]$Destination)

    Assert-KzFreshDirectory $Destination
    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    foreach ($entry in $Plan) {
        $destinationFile = Join-Path $Destination $entry.Relative
        $parent = Split-Path -Parent $destinationFile
        Assert-KzPlainPath $parent
        New-Item -ItemType Directory -Force -Path $parent | Out-Null
        Assert-KzPlainPath $destinationFile
        Copy-Item -LiteralPath $entry.Source -Destination $destinationFile
        if ((Get-KzPackageFileHash $destinationFile) -cne $entry.Hash) {
            throw "Package input changed during copying: $($entry.Relative)"
        }
    }
}

function Find-KzCrtDirectory {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
        throw 'Visual Studio discovery is unavailable; specify -CrtDirectory.'
    }
    $install = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($LASTEXITCODE -ne 0 -or -not $install) {
        throw 'Visual C++ installation not found; specify -CrtDirectory.'
    }
    $redist = Join-Path $install 'VC\Redist\MSVC'
    if (Test-Path -LiteralPath $redist -PathType Container) {
        foreach ($version in (Get-ChildItem -LiteralPath $redist -Directory |
            Where-Object { $_.Name -match '^\d+(?:\.\d+)+$' } |
            Sort-Object { [version]$_.Name } -Descending)) {
            $x64 = Join-Path $version.FullName 'x64'
            if (Test-Path -LiteralPath $x64 -PathType Container) {
                $crt = Get-ChildItem -LiteralPath $x64 -Directory -Filter 'Microsoft.VC*.CRT' |
                    Sort-Object Name -Descending | Select-Object -First 1
                if ($crt) { return $crt.FullName }
            }
        }
    }
    throw 'Visual C++ x64 redistributable not found; specify -CrtDirectory.'
}

function Get-KzCrtPlan {
    param([Parameter(Mandatory)][string]$Root, [Parameter(Mandatory)][string]$CrtDirectory)

    $manifest = Get-KzPackageManifest $Root
    $entries = [Collections.Generic.List[object]]::new()
    foreach ($dll in $manifest.msvc_runtime_dlls) {
        $file = Join-Path $CrtDirectory $dll.name
        $hash = Get-KzPackageFileHash $file
        if ($hash -cne $dll.sha256) {
            throw "Visual C++ runtime DLL does not match its audited hash: $($dll.name)"
        }
        $entries.Add([pscustomobject]@{ Source = $file; Relative = $dll.name; Hash = $hash })
    }
    return $entries.ToArray()
}
