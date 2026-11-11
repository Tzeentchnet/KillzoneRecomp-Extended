[CmdletBinding()]
param(
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$lockPath = Join-Path $repoRoot 'config\dependencies.lock.json'
$depsRoot = Join-Path $repoRoot 'ext\kzgs\deps'

function Get-LockedDependency {
    param(
        [Parameter(Mandatory)]
        [string]$Id
    )

    $matches = @($script:lock.dependencies | Where-Object { $_.id -eq $Id })
    if ($matches.Count -ne 1) {
        throw "Expected exactly one '$Id' entry in $lockPath."
    }

    return $matches[0]
}

function Get-LocalPath {
    param(
        [Parameter(Mandatory)]
        [string]$RelativePath
    )

    return Join-Path $repoRoot ($RelativePath.Replace('/', '\'))
}

function Test-DependencyMarkers {
    param(
        [Parameter(Mandatory)]
        [object]$Dependency,

        [Parameter(Mandatory)]
        [string]$Directory
    )

    foreach ($marker in $Dependency.local.markers) {
        if (-not (Test-Path -LiteralPath (Join-Path $Directory $marker))) {
            return $false
        }
    }

    return $true
}

function Get-VerifiedArchive {
    param(
        [Parameter(Mandatory)]
        [object]$Dependency
    )

    $archivePath = Get-LocalPath $Dependency.local.archive
    $expectedSize = [int64]$Dependency.size
    $expectedSha256 = ([string]$Dependency.sha256).ToLowerInvariant()

    if (Test-Path -LiteralPath $archivePath) {
        $archive = Get-Item -LiteralPath $archivePath
        $archiveHash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($archive.Length -eq $expectedSize -and $archiveHash -eq $expectedSha256) {
            return $archivePath
        }

        if (-not $Force) {
            throw "Existing archive failed verification: $archivePath. Re-run with -Force to replace it."
        }

        Remove-Item -LiteralPath $archivePath -Force
    }

    $downloadPath = "$archivePath.download-$PID"
    if (Test-Path -LiteralPath $downloadPath) {
        Remove-Item -LiteralPath $downloadPath -Force
    }

    Write-Host "Downloading $($Dependency.id) from $($Dependency.source_url)"
    try {
        Invoke-WebRequest -Uri $Dependency.source_url -OutFile $downloadPath -UseBasicParsing

        $download = Get-Item -LiteralPath $downloadPath
        if ($download.Length -ne $expectedSize) {
            throw "Downloaded size mismatch for $($Dependency.id): expected $expectedSize, got $($download.Length)."
        }

        $downloadHash = (Get-FileHash -LiteralPath $downloadPath -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($downloadHash -ne $expectedSha256) {
            throw "Downloaded SHA-256 mismatch for $($Dependency.id): expected $expectedSha256, got $downloadHash."
        }

        Move-Item -LiteralPath $downloadPath -Destination $archivePath
    }
    finally {
        if (Test-Path -LiteralPath $downloadPath) {
            Remove-Item -LiteralPath $downloadPath -Force
        }
    }

    return $archivePath
}

function Expand-LockedDependency {
    param(
        [Parameter(Mandatory)]
        [object]$Dependency
    )

    $destination = Get-LocalPath $Dependency.local.directory
    if (-not $Force -and (Test-DependencyMarkers -Dependency $Dependency -Directory $destination)) {
        Write-Host "$($Dependency.id) is already provisioned at $destination"
        return $destination
    }

    $archivePath = Get-VerifiedArchive -Dependency $Dependency
    $extractRoot = Join-Path $depsRoot ".provision-$($Dependency.id)-$PID"

    if (Test-Path -LiteralPath $extractRoot) {
        Remove-Item -LiteralPath $extractRoot -Recurse -Force
    }

    if (Test-Path -LiteralPath $destination) {
        Remove-Item -LiteralPath $destination -Recurse -Force
    }

    try {
        Write-Host "Extracting $($Dependency.id) to $destination"
        Expand-Archive -LiteralPath $archivePath -DestinationPath $extractRoot

        $roots = @(Get-ChildItem -LiteralPath $extractRoot -Directory)
        if ($roots.Count -ne 1) {
            throw "Expected one top-level directory in $archivePath, found $($roots.Count)."
        }

        Move-Item -LiteralPath $roots[0].FullName -Destination $destination
    }
    finally {
        if (Test-Path -LiteralPath $extractRoot) {
            Remove-Item -LiteralPath $extractRoot -Recurse -Force
        }
    }

    if (-not (Test-DependencyMarkers -Dependency $Dependency -Directory $destination)) {
        throw "Provisioned $($Dependency.id) is missing one or more locked marker files."
    }

    return $destination
}

function Test-GeneratedShaders {
    param(
        [Parameter(Mandatory)]
        [string]$OutputDirectory,

        [Parameter(Mandatory)]
        [string[]]$ExpectedHeaders,

        [Parameter(Mandatory)]
        [object]$SdkDependency,

        [Parameter(Mandatory)]
        [object]$ToolDependency
    )

    $stampPath = Join-Path $OutputDirectory '.kzgs-fidelityfx-shaders.json'
    if (-not (Test-Path -LiteralPath $stampPath)) {
        return $false
    }

    try {
        $stamp = Get-Content -LiteralPath $stampPath -Raw | ConvertFrom-Json
    }
    catch {
        return $false
    }

    if ($stamp.sdk_revision -ne $SdkDependency.revision -or
        $stamp.tool_revision -ne $ToolDependency.revision -or
        $stamp.generator_schema -ne 8) {
        return $false
    }

    foreach ($header in $ExpectedHeaders) {
        $headerPath = Join-Path $OutputDirectory $header
        if (-not (Test-Path -LiteralPath $headerPath)) {
            return $false
        }

        foreach ($include in (Select-String -LiteralPath $headerPath -Pattern '^#include "([^"]+)"')) {
            $includedPath = Join-Path $OutputDirectory $include.Matches[0].Groups[1].Value
            if (-not (Test-Path -LiteralPath $includedPath)) {
                return $false
            }
        }
    }

    return $true
}

function Update-FidelityFXPermutationHeader {
    param(
        [Parameter(Mandatory)]
        [string]$Header
    )

    $content = [IO.File]::ReadAllText($Header)
    $content = [Text.RegularExpressions.Regex]::Replace(
        $content,
        '(const unsigned char\*\s+blobData;\r?\n)',
        '$1    const char*          entryName;' + [Environment]::NewLine,
        1)
    $content = [Text.RegularExpressions.Regex]::Replace(
        $content,
        '(\{\s*g_[A-Za-z0-9_]+_size,\s*g_[A-Za-z0-9_]+_data,)',
        '$1 "main",')
    [IO.File]::WriteAllText($Header, $content, [Text.UTF8Encoding]::new($false))
}

function Update-FidelityFXReflectionNames {
    param(
        [Parameter(Mandatory)]
        [string]$PermutationHeader
    )

    $passName = [IO.Path]::GetFileNameWithoutExtension($PermutationHeader) -replace '_permutations$', ''
    $directory = Split-Path -Parent $PermutationHeader
    $permutationContent = [IO.File]::ReadAllText($PermutationHeader)
    foreach ($include in [Text.RegularExpressions.Regex]::Matches(
        $permutationContent, '#include "([^"]+)"')) {
        $header = Join-Path $directory $include.Groups[1].Value
        $content = [IO.File]::ReadAllText($header)
        $namesMatch = [Text.RegularExpressions.Regex]::Match(
            $content, 'static const char\*\s+\w+_CBVResourceNames\[\]\s*=\s*\{[^}]*\};')
        $bindingsMatch = [Text.RegularExpressions.Regex]::Match(
            $content, 'static const uint32_t\s+\w+_CBVResourceBindings\[\]\s*=\s*\{([^}]*)\};')
        if (-not $namesMatch.Success -and -not $bindingsMatch.Success) {
            continue
        }
        if (-not $namesMatch.Success -or -not $bindingsMatch.Success) {
            throw "Incomplete CBV reflection metadata in $header."
        }
        if ($namesMatch.Value -notmatch '""') {
            continue
        }

        $names = foreach ($binding in [Text.RegularExpressions.Regex]::Matches(
            $bindingsMatch.Groups[1].Value, '\d+')) {
            $bindingValue = [int]$binding.Value
            if ($passName -like 'ffx_opticalflow_*') {
                switch ($bindingValue) {
                    128 { 'cbOF' }
                    129 { 'cbOF_SPD' }
                    default { throw "Unknown Optical Flow CBV binding $bindingValue in $header." }
                }
            }
            elseif ($passName -like 'ffx_fsr3upscaler_*') {
                switch ($bindingValue) {
                    128 { 'cbFSR3Upscaler' }
                    129 {
                        if ($passName -like 'ffx_fsr3upscaler_luma_pyramid_pass*' -or
                            $passName -like 'ffx_fsr3upscaler_shading_change_pyramid_pass*') {
                            'cbSPD'
                        }
                        elseif ($passName -like 'ffx_fsr3upscaler_rcas_pass*') {
                            'cbRCAS'
                        }
                        elseif ($passName -like 'ffx_fsr3upscaler_autogen_reactive_pass*') {
                            'cbGenerateReactive'
                        }
                        else {
                            throw "Unknown FSR 3 secondary CBV binding in $header."
                        }
                    }
                    default { throw "Unknown FSR 3 CBV binding $bindingValue in $header." }
                }
            }
            else {
                throw "Unknown FidelityFX pass '$passName' with CBV reflection in $header."
            }
        }

        $nameList = ($names | ForEach-Object { "`"$_`"," }) -join ' '
        $replacement = $namesMatch.Value.Substring(0, $namesMatch.Value.IndexOf('{')) +
            "{  $nameList };"
        $content = $content.Remove($namesMatch.Index, $namesMatch.Length).
            Insert($namesMatch.Index, $replacement)
        [IO.File]::WriteAllText($header, $content, [Text.UTF8Encoding]::new($false))
    }
}

function Invoke-FidelityFXShaderCompiler {
    param(
        [Parameter(Mandatory)]
        [string]$Compiler,

        [Parameter(Mandatory)]
        [string]$Source,

        [Parameter(Mandatory)]
        [string]$Name,

        [Parameter(Mandatory)]
        [string[]]$Includes,

        [Parameter(Mandatory)]
        [string[]]$EffectArguments,

        [Parameter(Mandatory)]
        [int]$HalfPrecision,

        [Parameter(Mandatory)]
        [string]$OutputDirectory
    )

    $arguments = @(
        '-compiler=glslang',
        '-e', 'CS',
        '--target-env', 'vulkan1.1',
        '-S', 'comp',
        '-Os',
        '-reflection',
        '-deps=gcc',
        '-DFFX_GPU=1',
        '-DFFX_GLSL=1'
    )
    $arguments += $EffectArguments
    $arguments += "-name=$Name"
    $arguments += "-DFFX_HALF=$HalfPrecision"
    foreach ($include in $Includes) {
        $arguments += "-I$include"
    }
    $arguments += "-output=$OutputDirectory"
    $arguments += $Source

    Write-Host "Generating $Name"
    for ($attempt = 1; $attempt -le 3; $attempt++) {
        & $Compiler @arguments
        if ($LASTEXITCODE -eq 0) {
            break
        }

        if ($attempt -eq 3) {
            throw "FidelityFX shader generation failed for $Source with exit code $LASTEXITCODE."
        }

        Write-Warning "FidelityFX shader compiler exited with $LASTEXITCODE for $Name; retrying ($attempt/3)."
        Start-Sleep -Seconds 1
    }

    $permutationHeader = Join-Path $OutputDirectory "${Name}_permutations.h"
    Update-FidelityFXReflectionNames -PermutationHeader $permutationHeader
    Update-FidelityFXPermutationHeader -Header $permutationHeader
}

function New-FidelityFXShaders {
    param(
        [Parameter(Mandatory)]
        [string]$SdkDirectory,

        [Parameter(Mandatory)]
        [string]$ToolDirectory,

        [Parameter(Mandatory)]
        [object]$SdkDependency,

        [Parameter(Mandatory)]
        [object]$ToolDependency
    )

    $compiler = Join-Path $ToolDirectory 'sdk\tools\binary_store\FidelityFX_SC.exe'
    $outputDirectory = Join-Path $SdkDirectory 'generated\vk'
    $fsrGpuDirectory = Join-Path $SdkDirectory 'Kits\FidelityFX\upscalers\fsr3\include\gpu'
    $fsrShaderDirectory = Join-Path $ToolDirectory 'sdk\src\backends\vk\shaders\fsr3upscaler'
    $opticalFlowGpuDirectory = Join-Path $SdkDirectory 'Kits\FidelityFX\framegeneration\fsr3\include\gpu'
    $opticalFlowShaderDirectory = Join-Path $ToolDirectory 'sdk\src\backends\vk\shaders\opticalflow'
    $compatibilityGpuDirectory = Join-Path $ToolDirectory 'sdk\include\FidelityFX\gpu'
    $coreGpuDirectory = Join-Path $SdkDirectory 'Kits\FidelityFX\api\internal\gpu'

    $fsrPasses = @(
        'ffx_fsr3upscaler_autogen_reactive_pass',
        'ffx_fsr3upscaler_accumulate_pass',
        'ffx_fsr3upscaler_luma_pyramid_pass',
        'ffx_fsr3upscaler_prepare_reactivity_pass',
        'ffx_fsr3upscaler_prepare_inputs_pass',
        'ffx_fsr3upscaler_shading_change_pass',
        'ffx_fsr3upscaler_rcas_pass',
        'ffx_fsr3upscaler_shading_change_pyramid_pass',
        'ffx_fsr3upscaler_luma_instability_pass',
        'ffx_fsr3upscaler_debug_view_pass'
    )
    $opticalFlowPasses = @(
        'ffx_opticalflow_prepare_luma_pass',
        'ffx_opticalflow_compute_luminance_pyramid_pass',
        'ffx_opticalflow_generate_scd_histogram_pass',
        'ffx_opticalflow_compute_scd_divergence_pass',
        'ffx_opticalflow_compute_optical_flow_advanced_pass_v5',
        'ffx_opticalflow_filter_optical_flow_pass_v5',
        'ffx_opticalflow_scale_optical_flow_advanced_pass_v5'
    )
    $variants = @(
        [pscustomobject]@{ Suffix = ''; HalfPrecision = 0 },
        [pscustomobject]@{ Suffix = '_wave64'; HalfPrecision = 0 },
        # glslang's HLSL front end cannot parse the SDK's min16 aliases, so the required ABI variants remain full precision.
        [pscustomobject]@{ Suffix = '_16bit'; HalfPrecision = 0 },
        [pscustomobject]@{ Suffix = '_wave64_16bit'; HalfPrecision = 0 }
    )

    $expectedHeaders = @()
    foreach ($pass in ($fsrPasses + $opticalFlowPasses)) {
        foreach ($variant in $variants) {
            $expectedHeaders += "$pass$($variant.Suffix)_permutations.h"
        }
    }

    if (-not $Force -and
        (Test-GeneratedShaders -OutputDirectory $outputDirectory -ExpectedHeaders $expectedHeaders `
            -SdkDependency $SdkDependency -ToolDependency $ToolDependency)) {
        Write-Host "FidelityFX Vulkan shader permutations are already generated at $outputDirectory"
        return $outputDirectory
    }

    if (Test-Path -LiteralPath $outputDirectory) {
        Remove-Item -LiteralPath $outputDirectory -Recurse -Force
    }
    New-Item -ItemType Directory -Path $outputDirectory | Out-Null
    $compatibilityDirectory = Join-Path $outputDirectory 'source'
    New-Item -ItemType Directory -Path (Join-Path $compatibilityDirectory 'fsr3upscaler') -Force | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $compatibilityDirectory 'opticalflow') -Force | Out-Null
    $fsrCompatibilityCallbacks = Join-Path $compatibilityDirectory `
        'fsr3upscaler\ffx_fsr3upscaler_callbacks_glsl.h'
    Copy-Item -LiteralPath (Join-Path $compatibilityGpuDirectory `
        'fsr3upscaler\ffx_fsr3upscaler_callbacks_glsl.h') `
        -Destination $fsrCompatibilityCallbacks
    $fsrCompatibilityContent = [IO.File]::ReadAllText($fsrCompatibilityCallbacks).
        Replace('binding = 1000', 'binding = 320').
        Replace('binding = 1001', 'binding = 321')
    [IO.File]::WriteAllText(
        $fsrCompatibilityCallbacks,
        $fsrCompatibilityContent,
        [Text.UTF8Encoding]::new($false))
    Copy-Item -LiteralPath (Join-Path $compatibilityGpuDirectory `
        'opticalflow\ffx_opticalflow_callbacks_glsl.h') `
        -Destination (Join-Path $compatibilityDirectory `
        'opticalflow\ffx_opticalflow_callbacks_glsl.h')

    $fsrArguments = @(
        '-DFFX_FSR3UPSCALER_OPTION_UPSAMPLE_SAMPLERS_USE_DATA_HALF=0',
        '-DFFX_FSR3UPSCALER_OPTION_ACCUMULATE_SAMPLERS_USE_DATA_HALF=0',
        '-DFFX_FSR3UPSCALER_OPTION_REPROJECT_SAMPLERS_USE_DATA_HALF=1',
        '-DFFX_FSR3UPSCALER_OPTION_POSTPROCESSLOCKSTATUS_SAMPLERS_USE_DATA_HALF=0',
        '-DFFX_FSR3UPSCALER_OPTION_UPSAMPLE_USE_LANCZOS_TYPE=2',
        '-DFFX_FSR3UPSCALER_OPTION_REPROJECT_USE_LANCZOS_TYPE={0,1}',
        '-DFFX_FSR3UPSCALER_OPTION_HDR_COLOR_INPUT={0,1}',
        '-DFFX_FSR3UPSCALER_OPTION_LOW_RESOLUTION_MOTION_VECTORS={0,1}',
        '-DFFX_FSR3UPSCALER_OPTION_JITTERED_MOTION_VECTORS={0,1}',
        '-DFFX_FSR3UPSCALER_OPTION_INVERTED_DEPTH={0,1}',
        '-DFFX_FSR3UPSCALER_OPTION_APPLY_SHARPENING={0,1}'
    )
    $opticalFlowArguments = @(
        '-DFFX_OPTICALFLOW_OPTION_HDR_COLOR_INPUT={0,1}'
    )

    foreach ($pass in $fsrPasses) {
        foreach ($variant in $variants) {
            Invoke-FidelityFXShaderCompiler -Compiler $compiler `
                -Source (Join-Path $fsrShaderDirectory "$pass.glsl") `
                -Name "$pass$($variant.Suffix)" `
                -Includes @($compatibilityDirectory, $fsrGpuDirectory,
                    (Join-Path $fsrGpuDirectory 'fsr3upscaler'), $coreGpuDirectory,
                    $compatibilityGpuDirectory) `
                -EffectArguments $fsrArguments `
                -HalfPrecision $variant.HalfPrecision `
                -OutputDirectory $outputDirectory
        }
    }

    foreach ($pass in $opticalFlowPasses) {
        foreach ($variant in $variants) {
            Invoke-FidelityFXShaderCompiler -Compiler $compiler `
                -Source (Join-Path $opticalFlowShaderDirectory "$pass.glsl") `
                -Name "$pass$($variant.Suffix)" `
                -Includes @($compatibilityDirectory, $opticalFlowGpuDirectory,
                    (Join-Path $opticalFlowGpuDirectory 'opticalflow'), $coreGpuDirectory,
                    $compatibilityGpuDirectory) `
                -EffectArguments $opticalFlowArguments `
                -HalfPrecision $variant.HalfPrecision `
                -OutputDirectory $outputDirectory
        }
    }

    $stamp = [ordered]@{
        generator_schema = 8
        sdk_revision = [string]$SdkDependency.revision
        tool_revision = [string]$ToolDependency.revision
    } | ConvertTo-Json
    [IO.File]::WriteAllText(
        (Join-Path $outputDirectory '.kzgs-fidelityfx-shaders.json'),
        $stamp,
        [Text.UTF8Encoding]::new($false))

    if (-not (Test-GeneratedShaders -OutputDirectory $outputDirectory -ExpectedHeaders $expectedHeaders `
        -SdkDependency $SdkDependency -ToolDependency $ToolDependency)) {
        throw 'FidelityFX shader generation completed but the generated output set is incomplete.'
    }

    return $outputDirectory
}

if (-not (Test-Path -LiteralPath $lockPath)) {
    throw "Dependency lock file not found: $lockPath"
}

$lock = Get-Content -LiteralPath $lockPath -Raw | ConvertFrom-Json
$sdkDependency = Get-LockedDependency -Id 'fidelityfx-sdk'
$toolDependency = Get-LockedDependency -Id 'fidelityfx-shader-tools'
$minizDependency = Get-LockedDependency -Id 'miniz'

New-Item -ItemType Directory -Force -Path $depsRoot | Out-Null

$sdkDirectory = Expand-LockedDependency -Dependency $sdkDependency
$toolDirectory = Expand-LockedDependency -Dependency $toolDependency
$minizDirectory = Expand-LockedDependency -Dependency $minizDependency
$shaderDirectory = New-FidelityFXShaders -SdkDirectory $sdkDirectory -ToolDirectory $toolDirectory `
    -SdkDependency $sdkDependency -ToolDependency $toolDependency

Write-Host "Provisioned FidelityFX SDK: $sdkDirectory"
Write-Host "Provisioned FidelityFX shader tools: $toolDirectory"
Write-Host "Provisioned miniz: $minizDirectory"
Write-Host "Generated FidelityFX Vulkan shaders: $shaderDirectory"
