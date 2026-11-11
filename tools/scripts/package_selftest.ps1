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

    $fixtureRoot = Join-Path $work 'eol-source'
    $fixtureInput = Join-Path $work 'eol-build'
    $resourceMappings = @(
        @{ Source = 'resources\prompts'; Destination = 'resources\prompts' },
        @{ Source = 'ext\pcsx2\bin\resources\shaders\common'; Destination = 'resources\shaders\common' },
        @{ Source = 'ext\pcsx2\bin\resources\shaders\dx11'; Destination = 'resources\shaders\dx11' },
        @{ Source = 'ext\pcsx2\bin\resources\shaders\opengl'; Destination = 'resources\shaders\opengl' },
        @{ Source = 'ext\pcsx2\bin\resources\shaders\vulkan'; Destination = 'resources\shaders\vulkan' },
        @{ Source = 'ext\kzgs\shaders'; Destination = 'resources\shaders\kzgs' },
        @{ Source = 'ext\kzgs\third_party\fsr1'; Destination = 'resources\shaders\kzgs\fsr1' }
    )
    $encoding = [Text.Encoding]::GetEncoding(28591)
    foreach ($mapping in $resourceMappings) {
        $sourceDir = Join-Path $fixtureRoot $mapping.Source
        $inputDir = Join-Path $fixtureInput $mapping.Destination
        New-Item -ItemType Directory -Force -Path $sourceDir, $inputDir | Out-Null
        foreach ($file in (Get-ChildItem -LiteralPath (Join-Path $root $mapping.Source) -File)) {
            $relative = Join-Path $mapping.Destination $file.Name
            $bytes = [IO.File]::ReadAllBytes($file.FullName)
            if (Test-KzNormalizeResourceText $relative) {
                $lf = $encoding.GetString($bytes).Replace("`r`n", "`n")
                [IO.File]::WriteAllBytes((Join-Path $sourceDir $file.Name), $encoding.GetBytes($lf))
                [IO.File]::WriteAllBytes((Join-Path $inputDir $file.Name), $encoding.GetBytes($lf.Replace("`n", "`r`n")))
            } else {
                [IO.File]::WriteAllBytes((Join-Path $sourceDir $file.Name), $bytes)
                [IO.File]::WriteAllBytes((Join-Path $inputDir $file.Name), $bytes)
            }
        }
    }
    $manifest = Get-KzPackageManifest $root
    $eolPlan = @(Get-KzPackageResourcePlan -Root $fixtureRoot -InputDirectory $fixtureInput -Manifest $manifest)
    if ($eolPlan.Count -ne $manifest.resource_count) { throw 'LF/CRLF resource inventory count changed.' }
    foreach ($mapping in $resourceMappings) {
        foreach ($file in (Get-ChildItem -LiteralPath (Join-Path $fixtureRoot $mapping.Source) -File)) {
            $relative = Join-Path $mapping.Destination $file.Name
            if (-not (Test-KzNormalizeResourceText $relative)) { continue }
            $inputFile = Join-Path $fixtureInput $relative
            $lf = $encoding.GetString([IO.File]::ReadAllBytes($file.FullName))
            [IO.File]::WriteAllBytes($file.FullName, $encoding.GetBytes($lf.Replace("`n", "`r`n")))
            [IO.File]::WriteAllBytes($inputFile, $encoding.GetBytes($lf))
        }
    }
    [void]@(Get-KzPackageResourcePlan -Root $fixtureRoot -InputDirectory $fixtureInput -Manifest $manifest)
    'PASS audited resource inventory is identical for LF and CRLF checkouts and staged text'

    $vendorAttributes = @(& git -C $root check-attr text -- `
        'ext\kzgs\third_party\fsr1\ffx_a.h' 'ext\kzgs\third_party\fsr1\ffx_fsr1.h' `
        'ext\kzgs\third_party\fsr1\LICENSE.txt')
    if ($LASTEXITCODE -ne 0 -or $vendorAttributes.Count -ne 3 -or
        @($vendorAttributes | Where-Object { $_ -notmatch ': text: unset$' }).Count -ne 0) {
        throw 'Vendored FSR1 headers and license must disable Git text conversion.'
    }
    'PASS vendored FSR1 Git attributes preserve exact upstream bytes across checkouts'

    $binary = (Get-ChildItem -LiteralPath (Join-Path $fixtureInput 'resources\prompts') -Filter '*.png' -File | Select-Object -First 1).FullName
    $originalBinary = [IO.File]::ReadAllBytes($binary)
    $changedBinary = [byte[]]::new($originalBinary.Length + 1)
    [Array]::Copy($originalBinary, $changedBinary, $originalBinary.Length)
    $changedBinary[$originalBinary.Length] = 13
    [IO.File]::WriteAllBytes($binary, $changedBinary)
    $rejectedBinary = $false
    try { [void]@(Get-KzPackageResourcePlan -Root $fixtureRoot -InputDirectory $fixtureInput -Manifest $manifest) }
    catch { $rejectedBinary = $_.Exception.Message -match 'does not match the audited source' }
    if (-not $rejectedBinary) { throw 'Changed binary resource was not rejected byte-for-byte.' }
    [IO.File]::WriteAllBytes($binary, $originalBinary)

    $vendor = Join-Path $fixtureInput 'resources\shaders\kzgs\fsr1\ffx_a.h'
    $originalVendor = [IO.File]::ReadAllBytes($vendor)
    $vendorText = $encoding.GetString($originalVendor).Replace("`r`n", "`n")
    [IO.File]::WriteAllBytes($vendor, $encoding.GetBytes($vendorText.Replace("`n", "`r`n")))
    $rejectedVendor = $false
    try { [void]@(Get-KzPackageResourcePlan -Root $fixtureRoot -InputDirectory $fixtureInput -Manifest $manifest) }
    catch { $rejectedVendor = $_.Exception.Message -match 'does not match the audited source' }
    if (-not $rejectedVendor) { throw 'Modified upstream FSR1 bytes were not rejected.' }
    [IO.File]::WriteAllBytes($vendor, $originalVendor)

    $shader = Join-Path $fixtureInput 'resources\shaders\kzgs\fsr1_easu.glsl'
    [IO.File]::AppendAllText($shader, "`n// altered`n")
    $rejectedText = $false
    try { [void]@(Get-KzPackageResourcePlan -Root $fixtureRoot -InputDirectory $fixtureInput -Manifest $manifest) }
    catch { $rejectedText = $_.Exception.Message -match 'does not match the audited source' }
    if (-not $rejectedText) { throw 'Changed shader text was not rejected.' }
    'PASS binary and vendored FSR1 bytes remain exact and text edits are rejected'

    foreach ($entry in $plan) {
        if ($entry.Relative -in @('LICENSE', 'LICENSE-GPL-3.0')) { continue }
        $target = Join-Path $input $entry.Relative
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
        Copy-Item -LiteralPath $entry.Source -Destination $target
    }
    Set-Content -LiteralPath (Join-Path $input 'killzone.pdb') -Value 'synthetic product symbol fixture'

    $decoys = @(
        'nvngx_dlss.dll', 'nvngx_dlssd.dll', 'libxess.dll',
        'kzgs_upscaler_fsr3.dll', 'kzgs_upscaler_dlss.dll', 'kzgs_upscaler_xess.dll',
        'nvngx_dlss.pdb', 'libxess.pdb',
        'nvsdk_ngx_d.lib', 'libxess.lib', 'game.iso', 'eeMemory.bin',
        'upscalers\kzgs_upscaler_fsr3.dll', 'upscalers\kzgs_upscaler_dlss.dll',
        'upscalers\kzgs_upscaler_xess.dll',
        'upscalers\licenses\FidelityFX-SDK-2.4.0\license.md',
        'upscalers\licenses\FidelityFX-SDK-2.4.0\3rdpartynotice.md',
        'upscalers\licenses\miniz-LICENSE',
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
        @($dry | Where-Object { $_ -match '(?i)^INCLUDE .*?(fsr3|fidelityfx|miniz-license|dlss|xess|\.pdb|\.lib|cache|private)' }).Count) {
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
        @($releaseDry | Where-Object { $_ -match '(?i)^INCLUDE .*?(fsr3|fidelityfx|miniz-license|dlss|xess|\.pdb|\.lib|cache|private)' }).Count) {
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
