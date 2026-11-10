param([string]$BuildDirectory = '')

. "$PSScriptRoot\kz_paths.ps1"
. "$PSScriptRoot\package_manifest.ps1"
$ErrorActionPreference = 'Stop'
$root = Resolve-KzRoot ''
if (-not $BuildDirectory) { $BuildDirectory = Join-Path $root 'build\win-msvc-rel' }
$BuildDirectory = [IO.Path]::GetFullPath($BuildDirectory)
$work = Join-Path (Join-Path $root 'work') ("package-selftest-" + [guid]::NewGuid().ToString('N'))
if (Test-Path -LiteralPath $work) { throw "Self-test workspace already exists: $work" }
New-Item -ItemType Directory -Path $work -Force | Out-Null
$oldTemp = $env:TEMP
$oldTmp = $env:TMP
try {
    $scratch = Join-Path $work 'scratch'
    New-Item -ItemType Directory -Path $scratch | Out-Null
    $env:TEMP = $scratch
    $env:TMP = $scratch
    $input = Join-Path $work 'build'
    $package = Join-Path $work 'package'
    $release = Join-Path $work 'release'
    $plan = @(Get-KzPackagePlan -Root $root -InputDirectory $BuildDirectory)
    foreach ($entry in $plan) {
        if ($entry.Relative -in @('LICENSE', 'LICENSE-GPL-3.0')) { continue }
        $target = Join-Path $input $entry.Relative
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
        Copy-Item -LiteralPath $entry.Source -Destination $target
    }
    Set-Content -LiteralPath (Join-Path $input 'killzone.pdb') -Value 'synthetic product symbol fixture'

    $decoys = @(
        'nvngx_dlss.dll', 'nvngx_dlssd.dll', 'libxess.dll',
        'kzgs_upscaler_dlss.dll', 'kzgs_upscaler_xess.dll', 'nvngx_dlss.pdb', 'libxess.pdb',
        'nvsdk_ngx_d.lib', 'libxess.lib', 'game.iso', 'eeMemory.bin',
        'upscalers\kzgs_upscaler_dlss.dll', 'upscalers\kzgs_upscaler_xess.dll',
        'SDK\DLSS\include\nvsdk_ngx_vk.h', 'SDK\DLSS\lib\nvsdk_ngx_d.lib',
        'SDK\XeSS\lib\libxess.lib', 'SDK\XeSS\bin\libxess.dll',
        'cache\shader_cache.bin', 'disc\SLUS_000.00', 'generated\private.cpp',
        'resources\shaders\vulkan\nvngx_dlss.dll', 'resources\shaders\kzgs\fsr1\libxess.dll',
        'resources\shaders\kzgs\fsr1\cache\shader_cache.bin', 'resources\prompts\private.png'
    )
    foreach ($relative in $decoys) {
        $path = Join-Path $input $relative
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $path) | Out-Null
        Set-Content -LiteralPath $path -Value 'synthetic excluded fixture'
    }
    $dry = @(& "$PSScriptRoot\package.ps1" -Root $root -BuildDirectory $input -Destination $package -DryRun)
    if ((Test-Path -LiteralPath $package) -or
        @($dry | Where-Object { $_ -match '(?i)^INCLUDE .*?(dlss|xess|\.pdb|\.lib|cache|private)' }).Count) {
        throw 'Package dry-run created files or selected an excluded input.'
    }
    'PASS package dry-run excludes vendor DLLs, SDK roots, import libs, symbols, protected fixtures and caches'

    $pdbDry = @(& "$PSScriptRoot\package.ps1" -Root $root -BuildDirectory $input `
        -Destination (Join-Path $work 'symbols-package') -DryRun -WithPdb)
    if (@($pdbDry | Where-Object { $_ -eq 'INCLUDE killzone.pdb' }).Count -ne 1 -or
        @($pdbDry | Where-Object { $_ -match '(?i)^INCLUDE .*?(nvngx|xess).*\.pdb' }).Count) {
        throw 'Explicit product-symbol selection included the wrong PDB.'
    }
    'PASS product symbols are opt-in and vendor symbols remain excluded'

    & "$PSScriptRoot\package.ps1" -Root $root -BuildDirectory $input -Destination $package | Out-Null
    $expectedPlan = @(Get-KzPackagePlan -Root $root -InputDirectory $input)
    $expected = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($entry in $expectedPlan) { [void]$expected.Add($entry.Relative) }
    [void]$expected.Add('README.txt')
    $actual = @(Get-ChildItem -LiteralPath $package -Recurse -File -Force |
        ForEach-Object { $_.FullName.Substring($package.Length + 1) })
    if ($actual.Count -ne $expected.Count -or @($actual | Where-Object { -not $expected.Contains($_) }).Count) {
        throw 'Default package inventory differs from the explicit plan.'
    }
    foreach ($relative in (@($decoys) + @('killzone.pdb'))) {
        if (Test-Path -LiteralPath (Join-Path $package $relative)) {
            throw "Default package included excluded fixture: $relative"
        }
    }
    if (-not (Test-Path -LiteralPath (Join-Path $package 'LICENSE-GPL-3.0')) -or
        -not (Test-Path -LiteralPath (Join-Path $package 'resources\shaders\kzgs\fsr1\LICENSE.txt'))) {
        throw 'Required license notices were not staged.'
    }
    "PASS staged package contains exactly $($actual.Count) approved files and required GPL/FSR notices"

    $occupied = Join-Path $work 'occupied'
    New-Item -ItemType Directory -Force -Path (Join-Path $occupied 'cache') | Out-Null
    $sentinel = Join-Path $occupied 'cache\player-data.bin'
    Set-Content -LiteralPath $sentinel -Value 'preserve user data'
    $refused = $false
    try {
        & "$PSScriptRoot\package.ps1" -Root $root -BuildDirectory $input -Destination $occupied -DryRun | Out-Null
    } catch { $refused = $true }
    if (-not $refused -or -not (Test-Path -LiteralPath $sentinel)) {
        throw 'Nonempty destination was not safely rejected.'
    }
    'PASS nonempty destinations are rejected without deleting player data'

    foreach ($relative in $decoys) {
        $path = Join-Path $package $relative
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $path) | Out-Null
        Set-Content -LiteralPath $path -Value 'synthetic release decoy'
    }
    Set-Content -LiteralPath (Join-Path $package 'killzone.pdb') -Value 'synthetic product symbols'
    $crt = Join-Path $work 'crt'
    New-Item -ItemType Directory -Path $crt | Out-Null
    $crtPlan = @(Get-KzCrtPlan -Root $root -CrtDirectory (Find-KzCrtDirectory))
    foreach ($entry in $crtPlan) {
        Copy-Item -LiteralPath $entry.Source -Destination (Join-Path $crt $entry.Relative)
    }
    Set-Content -LiteralPath (Join-Path $crt 'nvngx_dlss.dll') -Value 'synthetic CRT directory decoy'
    $runtimeFile = Join-Path $crt 'vcruntime140.dll'
    Set-Content -LiteralPath $runtimeFile -Value 'renamed vendor CRT decoy'
    $rejectedCrt = $false
    try {
        & "$PSScriptRoot\make_release.ps1" -Version tampered -From $package `
            -OutputDirectory $release -CrtDirectory $crt -DryRun | Out-Null
    } catch { $rejectedCrt = $_.Exception.Message -match 'audited hash' }
    if (-not $rejectedCrt -or (Test-Path -LiteralPath $release)) {
        throw 'A substituted Visual C++ runtime was not rejected.'
    }
    $originalRuntime = $crtPlan | Where-Object { $_.Relative -eq 'vcruntime140.dll' }
    Copy-Item -LiteralPath $originalRuntime.Source -Destination $runtimeFile -Force
    'PASS a renamed vendor DLL substituted for a CRT binary is rejected'
    $releaseDry = @(& "$PSScriptRoot\make_release.ps1" -Version selftest -From $package `
        -OutputDirectory $release -CrtDirectory $crt -DryRun)
    if ((Test-Path -LiteralPath $release) -or
        @($releaseDry | Where-Object { $_ -match '(?i)^INCLUDE .*?(dlss|xess|\.pdb|\.lib|cache|private)' }).Count) {
        throw 'Release dry-run created files or selected an excluded input.'
    }
    'PASS release dry-run excludes contaminated source and CRT directory decoys'
    & "$PSScriptRoot\make_release.ps1" -Version selftest -From $package `
        -OutputDirectory $release -CrtDirectory $crt | Out-Null
    $zip = Join-Path $release 'Killzone-PC-vselftest-alpha.zip'
    $expectedZip = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $expectedHashes = [Collections.Generic.Dictionary[string,string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($entry in (@($expectedPlan) + @($crtPlan))) {
        $archiveName = 'Killzone/' + $entry.Relative.Replace('\', '/')
        [void]$expectedZip.Add($archiveName)
        $expectedHashes.Add($archiveName, $entry.Hash)
    }
    [void]$expectedZip.Add('Killzone/README.txt')
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $archive = [IO.Compression.ZipFile]::OpenRead($zip)
    try {
        $names = @($archive.Entries | ForEach-Object { $_.FullName })
        if ($names.Count -ne $expectedZip.Count -or
            @($names | Where-Object { -not $expectedZip.Contains($_) }).Count) {
            throw 'Release archive inventory differs from the explicit plan.'
        }
        foreach ($relative in (@($decoys) + @('killzone.pdb'))) {
            $archiveName = 'Killzone/' + $relative.Replace('\', '/')
            if ($names -contains $archiveName) {
                throw "Release archive included excluded fixture: $relative"
            }
        }
        $sha = [Security.Cryptography.SHA256]::Create()
        try {
            foreach ($entry in $archive.Entries) {
                if (-not $expectedHashes.ContainsKey($entry.FullName)) { continue }
                $stream = $entry.Open()
                try {
                    $hash = [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-', '').ToLowerInvariant()
                    if ($hash -cne $expectedHashes[$entry.FullName]) {
                        throw "Release archive entry differs from its approved source: $($entry.FullName)"
                    }
                } finally { $stream.Dispose() }
            }
        } finally { $sha.Dispose() }
    } finally { $archive.Dispose() }
    "PASS release ZIP contains exactly $($names.Count) approved entries and verified contents; no vendor SDK, PDB, cache or protected fixtures"

    $oldConfig = $env:KZ_CONFIG
    $oldIsoFree = $env:KZ_ISO_FREE
    $oldAudio = $env:KZ_AUDIO_DEVICE
    $oldHeadless = $env:PS2X_HEADLESS
    try {
        $env:KZ_CONFIG = Join-Path $scratch 'config.ini'
        $env:KZ_ISO_FREE = '1'
        $env:KZ_AUDIO_DEVICE = '0'
        $env:PS2X_HEADLESS = '1'
        Push-Location $scratch
        try {
            $releaseExe = Join-Path $release 'Killzone-PC-vselftest-alpha\Killzone\killzone.exe'
            & $releaseExe --selftest | Out-Null
            if ($LASTEXITCODE -ne 0) { throw 'Packaged executable failed its ISO-free self-test.' }
        } finally { Pop-Location }
    } finally {
        $env:KZ_CONFIG = $oldConfig
        $env:KZ_ISO_FREE = $oldIsoFree
        $env:KZ_AUDIO_DEVICE = $oldAudio
        $env:PS2X_HEADLESS = $oldHeadless
    }
    'PASS packaged executable runs its ISO-free self-test with the approved runtime files'

    Copy-Item -LiteralPath (Join-Path $input 'SDL3.dll') -Destination (Join-Path $input 'killzone.exe') -Force
    $rejectedExe = $false
    try {
        & "$PSScriptRoot\package.ps1" -Root $root -BuildDirectory $input `
            -Destination (Join-Path $work 'dll-as-exe') -DryRun | Out-Null
    } catch { $rejectedExe = $_.Exception.Message -match 'not a Windows x64 executable' }
    if (-not $rejectedExe -or (Test-Path -LiteralPath (Join-Path $work 'dll-as-exe'))) {
        throw 'A DLL renamed as the game executable was not rejected.'
    }
    'PASS a DLL renamed as the game executable is rejected'
    Copy-Item -LiteralPath (Join-Path $BuildDirectory 'killzone.exe') -Destination (Join-Path $input 'killzone.exe') -Force
    Set-Content -LiteralPath (Join-Path $input 'SDL3.dll') -Value 'renamed vendor DLL decoy'
    $rejected = $false
    try {
        & "$PSScriptRoot\package.ps1" -Root $root -BuildDirectory $input `
            -Destination (Join-Path $work 'tampered-package') -DryRun | Out-Null
    } catch { $rejected = $_.Exception.Message -match 'audited hash' }
    if (-not $rejected -or (Test-Path -LiteralPath (Join-Path $work 'tampered-package'))) {
        throw 'An allowlisted DLL name with untrusted contents was not rejected.'
    }
    'PASS an allowlisted DLL with an incorrect content hash is rejected'
    'Package self-test: PASS'
} finally {
    $env:TEMP = $oldTemp
    $env:TMP = $oldTmp
    Remove-Item -LiteralPath $work -Recurse -Force
}
