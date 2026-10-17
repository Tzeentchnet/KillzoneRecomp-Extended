[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidateSet('win-msvc-rel', 'win-clang-rel', 'win-msvc-tests', 'win-clang-tests')]
    [string]$Preset,
    [ValidateSet('configure', 'build', 'test')]
    [string]$Action = 'configure',
    [string]$Label,
    [string]$Target,
    [string]$Root = $env:KZ_ROOT,
    [string]$Cmake = $env:KZ_CMAKE
)

. "$PSScriptRoot\kz_paths.ps1"

$rootPath = Resolve-KzRoot $Root
if ([string]::IsNullOrWhiteSpace($Cmake)) {
    $cmakeCommand = Get-Command cmake.exe -ErrorAction SilentlyContinue
    if ($cmakeCommand) {
        $Cmake = $cmakeCommand.Source
    } elseif (Test-Path -LiteralPath 'C:\Program Files\CMake\bin\cmake.exe') {
        $Cmake = 'C:\Program Files\CMake\bin\cmake.exe'
    } else {
        throw 'CMake was not found. Install CMake 3.24 or newer, add it to PATH, or set KZ_CMAKE.'
    }
}

$Cmake = (Resolve-Path -LiteralPath $Cmake).Path
if ($Preset -like 'win-clang-*' -and [string]::IsNullOrWhiteSpace($env:KZ_LLVM_BIN)) {
    $clangCommand = Get-Command clang-cl.exe -ErrorAction SilentlyContinue
    if ($clangCommand) {
        $env:KZ_LLVM_BIN = Split-Path -Parent $clangCommand.Source
    } elseif (Test-Path -LiteralPath 'C:\Program Files\LLVM\bin\clang-cl.exe' -PathType Leaf) {
        $env:KZ_LLVM_BIN = 'C:\Program Files\LLVM\bin'
    }
}
$localNinja = Join-Path $rootPath 'tools\dl\ninja\ninja.exe'
if ([string]::IsNullOrWhiteSpace($env:KZ_NINJA) -and (Test-Path -LiteralPath $localNinja -PathType Leaf)) {
    $env:KZ_NINJA = $localNinja
}
$tool = $Cmake
switch ($Action) {
    'build' {
        $toolArgs = @('--build', '--preset', $Preset)
        if (-not [string]::IsNullOrWhiteSpace($Target)) {
            $toolArgs += @('--target', $Target)
        }
    }
    'test' {
        $tool = Join-Path (Split-Path -Parent $Cmake) 'ctest.exe'
        if (-not (Test-Path -LiteralPath $tool -PathType Leaf)) {
            throw "CTest was not found next to CMake at $tool."
        }
        $toolArgs = @('--preset', $Preset, '--output-on-failure')
        if (-not [string]::IsNullOrWhiteSpace($Label)) {
            $toolArgs += @('-L', $Label)
        }
    }
    default {
        $toolArgs = @('--preset', $Preset)
    }
}

$env:KZ_ROOT = $rootPath
Push-Location $rootPath
try {
    & "$PSScriptRoot\vsenv.bat" $tool @toolArgs
    $result = $LASTEXITCODE
} finally {
    Pop-Location
}
exit $result
