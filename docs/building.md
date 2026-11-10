# Building

## Requirements

- Windows 10/11 x64
- Visual Studio Build Tools 2022 or later with **Desktop development with C++**
- CMake 3.24 or newer
- Ninja 1.13.2, provisioned by the repository
- Python 3.11 or newer
- An x64 CPU with SSE4.1; AVX and AVX2 paths are selected at runtime
- Your own supported Killzone disc image

The supported executable builds are NTSC-U `SCUS-97402` and PAL `SCES-52893` (English, Russian, and Greek). The
repository does not contain game code or data.

## Clone and provision

1. Clone with submodules:

   ```powershell
   git clone --recursive https://github.com/Tzeentchnet/KillzoneRecomp-Extended
   Set-Location .\KillzoneRecomp-Extended
   ```

2. Provision the exact inputs recorded in
   [`config/dependencies.lock.json`](../config/dependencies.lock.json):

   - PCSX2 source under `ext\pcsx2` at the locked revision.
   - The locked PCSX2 Windows dependency archive retained and extracted under `ext\kzgs\deps`.
   - The locked SDL3 VC development archive retained and extracted under `ext\sdl3`.
   - For macOS experiments, the locked SDL3 source archive extracted as `ext\sdl3\SDL3-3.4.16-source`.

3. Verify the dependency set:

   ```powershell
   python .\tools\scripts\verify_deps.py --strict
   ```

4. Extract `SCUS_974.02` from your own disc to a private location and set `KZ_GAME_ELF`:

   ```powershell
   $env:KZ_GAME_ELF = 'D:\private\SCUS_974.02'
   ```

5. Build PS2Recomp and provision the locked analysis/build tools:

   ```powershell
   .\tools\scripts\build_ps2recomp.bat
   .\tools\scripts\provision_analysis_tools.ps1
   .\tools\scripts\provision_ninja.ps1
   ```

6. Generate the private C++ tree:

   ```powershell
   python .\tools\scripts\regen.py --elf $env:KZ_GAME_ELF
   ```

   For a combined US/PAL executable, generate the US tree first, then point `KZ_GAME_ELF` at `SCES_528.93` and run:

   ```powershell
   python .\tools\scripts\regen.py --pal --elf $env:KZ_GAME_ELF
   ```

   A missing `generated_pal\` directory produces a US-only build that reports PAL discs as unsupported.

7. Configure and build:

   ```powershell
   .\tools\scripts\cmake_preset.ps1 -Preset win-msvc-rel
   .\tools\scripts\cmake_preset.ps1 -Preset win-msvc-rel -Action build
   ```

   The executable is written to `build\win-msvc-rel\killzone.exe`.

8. Run `launch_launcher.bat`, choose the supported disc image, and configure the port. The launcher clears inherited
   automation variables before starting.

9. Optional: create a player folder under `dist\Killzone`:

   ```powershell
   .\tools\scripts\package.ps1
   ```

## Visual Studio Code

Open the repository root with `code .` and install the recommended CMake Tools, C/C++, and PowerShell extensions.
**Terminal > Run Build Task** invokes the default MSVC workflow. The `Killzone: Configure ...` and
`Killzone: Build ...` tasks use `tools\scripts\cmake_preset.ps1`, which locates CMake, loads the x64 Build Tools
environment, provisions the locked Ninja version, and invokes the checked preset.

The clang-cl task discovers LLVM from `KZ_LLVM_BIN` or prompts for its `bin` directory.

## Checked presets

From an x64 Build Tools shell, the wrapper commands are equivalent to:

```powershell
cmake --preset win-msvc-rel
cmake --build --preset win-msvc-rel

$env:KZ_LLVM_BIN = 'C:\Program Files\LLVM\bin'
cmake --preset win-clang-rel
cmake --build --preset win-clang-rel
```

Experimental default-OFF local DLSS/XeSS SDK targets, their qualification limits, and license/provenance requirements
are described in [Local Vulkan vendor SDK adapters](vendor-upscalers.md). They are not part of ordinary builds.

Configuration verifies locked dependencies, the compiler, Ninja, and the private generated source tree before
dependency population. Useful environment overrides are:

| Variable | Purpose |
|---|---|
| `KZ_ROOT` | Repository root |
| `KZ_BUILD_ROOT` | Build output root |
| `KZ_VCVARS64` | Explicit `vcvars64.bat` |
| `KZ_CMAKE` | Explicit `cmake.exe` |
| `KZ_NINJA` | Explicit Ninja 1.13.2 executable |
| `KZ_LLVM_BIN` | LLVM `bin` directory |
| `KZ_GENERATED_DIR` | Private generated source directory |

Scripts that consume private inputs never search for them. Pass `-Iso` / `-Elf` or set `KZ_ISO` / `KZ_GAME_ELF`.

## Edit-build loop

Configure once with `-DKZ_DEV_LINK=ON` to disable identical-COMDAT folding and shorten development relinks:

```powershell
.\tools\scripts\vsenv.bat cmake --preset win-msvc-rel -DKZ_DEV_LINK=ON
```

Editing `src\` recompiles `kz_port` and relinks. Editing headers included by generated code, or changing the generated
function set, rebuilds the generated unity batches. Disable `KZ_DEV_LINK` for release builds.

The older `tools\scripts\build.bat RelWithDebInfo` workflow remains available but the checked presets are preferred.

## Generated-code provenance

Generated game code, the source executable, function maps, and regeneration logs remain private and ignored. The
committed `config\generated-manifest.json` contains only protected-content-safe hashes, counts, bounds, and hook
signatures.

After regeneration, run:

```powershell
python .\tools\scripts\verify_resume_table.py .\generated
python .\tools\scripts\inventory_generated.py --check
```

The inventory gate verifies that fixed guest hook addresses remain executable, dispatchable, signature-matched, and
uniquely resolvable.

## Deferred platform work

`macos-arm64` is an experimental Phase 0 preset for the renderer-free host boundary and portable VU interpreter. It
does not build the game or renderer and is not an active release target:

```sh
cmake --preset macos-arm64
cmake --build --preset macos-arm64
ctest --preset macos-arm64 --output-on-failure -R '^kzvu_interpreter_'
ctest --preset macos-arm64 --output-on-failure -R '^kz_host_(window|audio)_smoke$'
```

Windows cross-checks do not substitute for native AppleClang and CoreAudio acceptance.
