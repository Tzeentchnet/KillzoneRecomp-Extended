# KillzoneRecomp

A native Windows PC port of **Killzone** (PlayStation 2, 2004), made by statically recompiling the game's PS2 code to
C++ with [PS2Recomp](https://github.com/ran-j/PS2Recomp). PCSX2's hardware renderer, microVU, IPU and SPU2 are linked in
as libraries.

**No game code or data is in this repository.** You need your own disc image of Killzone NTSC-U (SCUS-97402).

Developed with AI assistance (Claude Code). Findings, measurements and the reasoning behind each change are in
[`docs/findings.md`](docs/findings.md) and [`patches/README.md`](patches/README.md).

Execution state, next-session instructions, and subagent-safe parallel work are
tracked in [`HANDOFF.md`](HANDOFF.md).

## Status

Test build. Boot, menus, profile creation and the first mission's gameplay are verified in automated runs; longer
play-throughs and hands-on testing are ongoing.

- **Frame rate:** runs at the monitor's refresh rate. Measured in gameplay: about 117-120 fps at 120 Hz and about
  130-140 fps at 144 Hz. The game's clock runs at real time at 120 Hz (the frame timer is rescaled, not sped up).
- **Mouse and keyboard:** mouse aim is an engine patch (raw counts go into the game's own look rate: no stick
  acceleration or dead zone), with fully rebindable keys. Controllers work through SDL3 with the original PS2 layout.
- **Graphics:** internal resolution up to 8x (supersampling), native 16:9, sharp scaling, anisotropic filtering,
  optional FXAA; D3D11, D3D12 or Vulkan.
- **Launcher:** display, graphics, controls and audio settings; checks the disc image is the supported version.
- **Not included:** Killzone Online (LOADER.ELF and the network modules); its servers are long gone.

## Default controls

| | |
|---|---|
| Mouse | aim |
| Left / right / middle mouse | fire / hold to aim / secondary fire |
| W A S D | move (menus: navigate) |
| Left Shift / C or Left Ctrl | sprint / crouch |
| R / G / Q or wheel | reload / grenade / switch weapon |
| E or F / X or Mouse4 | use / special item |
| Escape / Tab | pause / objectives |
| Arrow keys, Enter | menus |

The profile name can be typed directly on the game's on-screen keyboard. Bindings live in `killzone.ini` under
`[Bindings]`.

## Building

Windows x64, Visual Studio Code, Visual Studio Build Tools 2022 or later with the **Desktop development with C++**
workload, CMake 3.24+, Ninja 1.13.2, Python 3.11+, and a CPU with AVX2. The full Visual Studio IDE is not required.

1. Clone with submodules: `git clone --recursive https://github.com/drakolordx7/KillzoneRecomp`.
   `ext/PS2Recomp` is the `killzone` branch of [drakolordx7/PS2Recomp](https://github.com/drakolordx7/PS2Recomp).
2. Provision the exact dependencies recorded in `config/dependencies.lock.json`:
   - PCSX2 source in `ext/pcsx2` at the locked commit, unmodified.
   - The locked `pcsx2-windows-dependencies.7z` retained in `ext/kzgs/deps/` and extracted there.
   - The locked SDL3 VC development archive retained in `ext/sdl3/` and extracted there.
   - For macOS, the locked SDL3 source archive retained in `ext/sdl3/` and extracted as
     `ext/sdl3/SDL3-3.4.16-source`.
   - Run `python tools\scripts\verify_deps.py --strict` before configuring. The verifier is offline and checks the
     submodule gitlink, PS2Recomp patch reconstruction, CMake declarations, revisions, archive sizes and SHA-256 hashes.
3. Extract `SCUS_974.02` from your own disc image to a private path and set `KZ_GAME_ELF` to that file.
4. Build the recompiler: `tools\scripts\build_ps2recomp.bat`.
5. Provision the locked Ghidra and JDK tools: `tools\scripts\provision_analysis_tools.ps1`.
6. Recompile the game: `python tools\scripts\regen.py --elf "$env:KZ_GAME_ELF"` (writes `generated/`, about 20k C++ files).
6. Build: `tools\scripts\build.bat RelWithDebInfo` -> `build\RelWithDebInfo\killzone.exe`.
7. Optional: `tools\scripts\package.ps1` makes a player folder in `dist\Killzone`.

`killzone.exe --selftest` runs the unit checks. `tools\scripts\run_headless.ps1` runs the game without a window, with
scripted input (`KZ_INPUT_SCRIPT`) and periodic screenshots, which is how everything here was verified.

Build scripts discover the repository from their own location. `KZ_ROOT` and `KZ_BUILD_ROOT` override the source and
build roots; `KZ_VCVARS64` can point to `vcvars64.bat`, `KZ_CMAKE` can point to `cmake.exe`, `KZ_NINJA` can point to
Ninja 1.13.2, and `KZ_LLVM_BIN` selects an LLVM `bin` directory. Scripts that read user-supplied content never search
for it: pass `-Iso` / `-Elf` to PowerShell scripts or set `KZ_ISO` / `KZ_GAME_ELF`; Python tools expose matching
`--iso` / `--elf` arguments.

### Visual Studio Code

From this repository root, open the folder directly in VS Code:

```powershell
code .
```

Install the recommended CMake Tools, C/C++, and PowerShell extensions when prompted. Use **Terminal > Run Build Task**
for the default MSVC build, or run one of the `Killzone: Configure ...` / `Killzone: Build ...` tasks. These tasks call
`tools\scripts\cmake_preset.ps1`, which locates CMake, loads the headless MSVC Build Tools environment, and invokes the
checked preset. They first run `tools\scripts\provision_ninja.ps1`, which downloads only the version and checksum in
the dependency lock. The clang-cl task prompts for the LLVM `bin` directory. `Killzone: Build host core (clang-cl)`
runs the renderer-free portability gate without building the game or renderer backends.

### Checked CMake presets

Install CMake 3.24 or newer, provision the locked inputs above, and generate private game-derived sources with
`regen.py`. Run `tools\scripts\provision_ninja.ps1` to install the locked Ninja 1.13.2 locally. The VS Code tasks are
the primary workflow. From an x64 Build Tools shell, the equivalent commands are:

```powershell
cmake --preset win-msvc-rel
cmake --build --preset win-msvc-rel

$env:KZ_LLVM_BIN = 'C:\Program Files\LLVM\bin'
cmake --preset win-clang-rel
cmake --build --preset win-clang-rel
```

The configure preflight runs the strict offline dependency verifier, validates the selected compiler and Ninja
version, and checks `KZ_GENERATED_DIR` (default `generated\`) before dependency population begins. Outside a Build
Tools shell, `tools\scripts\cmake_preset.ps1 -Preset win-msvc-rel` loads the latest available x64 environment; set
`KZ_VCVARS64` when the Build Tools installation cannot be discovered automatically.

### Host platform boundary

`kz_host_core` is independent of SDL, game code, and renderer libraries. Its typed API owns executable-relative paths,
page mapping/protection and instruction-cache invalidation, monotonic timing and synchronization, native-window
metadata, diagnostics, environment mutation, CPU feature detection, and guest floating-point setup. Unsupported host
operations raise `kz::host::Error`; they never return Win32-shaped placeholder values.

From a Build Tools shell, the PORT-01 build gate is:

```powershell
cmake --build --preset win-clang-rel --target kz_host_core
```

From a normal VS Code terminal, use the task above or its checked wrapper:

```powershell
tools\scripts\cmake_preset.ps1 -Preset win-clang-rel -Action build -Target kz_host_core
```

The `kz_host_core_component` ISO-free test exercises executable-path resolution, UTF-8 path conversion, environment
updates, monotonic sleep, a manual-reset event, CPU/SIMD detection, thread naming, and a writable-to-executable page
transition followed by instruction-cache invalidation and execution. `kz_host_window_smoke` adds SDL window creation,
native-handle extraction, drawable metrics, event delivery, and teardown; `kz_host_audio_smoke` opens, starts, pauses,
and closes an SDL playback stream after observing its callback. Windows automation uses SDL's dummy audio driver;
the macOS acceptance run uses the default CoreAudio device.

### ARM64 cross-machine handoff

ARM-01 is intentionally deferred to an Apple Silicon/macOS machine. This Windows checkout does not use cross-compilation
or emulation as a substitute for the required AppleClang compile-and-run workflow. The ARM-01 deliverable includes
creating the currently missing workflow preset and making this command pass:

```sh
cmake --workflow --preset macos-arm64-guest-smoke
```

VU-01 and MAC-01 do not depend on ARM-01 and may run in parallel on the Apple Silicon machine. The checked
`macos-arm64` preset is an isolated Phase 0 configuration: it builds the locked SDL 3.4.16 source, renderer-free host
smokes, and the portable VU interpreter while excluding game-derived code, renderers, PCSX2 microVU, and the x86
emitter:

```sh
cmake --preset macos-arm64
cmake --build --preset macos-arm64
ctest --preset macos-arm64 --output-on-failure -R '^kzvu_interpreter_'
ctest --preset macos-arm64 --output-on-failure -R '^kz_host_(window|audio)_smoke$'
```

The portable VU corpus and all three host tests pass under MSVC and clang-cl on Windows, but that is not a substitute
for the native AppleClang ARM64 acceptance run. VU-02 remains blocked on native VU-01 acceptance; GS-01 remains
blocked on native MAC-01 acceptance.

### Tests

Build and run the hermetic suite without granting disc access:

```powershell
tools\scripts\cmake_preset.ps1 -Preset win-msvc-rel
tools\scripts\cmake_preset.ps1 -Preset win-msvc-tests -Action build
tools\scripts\cmake_preset.ps1 -Preset win-msvc-rel -Action test -Label iso-free
```

CTest supplies an isolated missing configuration through `KZ_CONFIG` and sets `KZ_ISO_FREE=1`; disc checks fail
before opening any path. The `kz_ipu_byod` test is labeled `byod` and remains disabled unless CMake is configured
with `-DKZ_BYOD_ISO=<authorized-disc-path>` (or `KZ_ISO` is set before the first configure).

### Generated-code provenance

Generated game code and the source executable remain private and ignored. The committed
`config\generated-manifest.json` contains only provenance hashes, named counts, table bounds, instruction-patch
assertions, and unique hashes for the fixed guest addresses in `src\kz_hook_addresses.h`.

```powershell
python .\tools\scripts\verify_resume_table.py .\generated
python .\tools\scripts\inventory_generated.py --check
```

The current lock distinguishes 16,045 Ghidra functions, 19,307 Ghidra code labels, 31,996 generated translation
units, 31,995 generated functions, 132,656 dispatch entries, 101,715 resumable entries reported by PS2Recomp, and
100,661 globally dispatchable resume cases. Regeneration merges every locked hook address into the configured
entry-point set so a hard-coded hook cannot remain silently unreachable. The inventory gate also proves that all 37
fixed guest addresses are executable, dispatchable, signature-matched, and uniquely resolvable in the supported
executable.

## Layout

- `src/` - the port: game overrides and patches, input, mouse aim, timing, launcher, config, GS/VU/IPU/SPU2 glue.
- `ext/kzgs`, `ext/kzvu`, `ext/kzipu`, `ext/kzspu2` - PCSX2's GS, microVU, IPU and SPU2 built as static libraries.
- `ext/PS2Recomp` - recompiler and runtime (fork, `killzone` branch).
- `patches/` - the runtime changes as patches, with a description and measurements for each.
- `config/killzone.toml` - PS2Recomp configuration for SCUS-97402.
- `tools/` - build, test, profiling and Ghidra helper scripts.
- `docs/` - findings and performance research.

## License

GPL-3.0 (see `LICENSE`). The port links PCSX2 (GPL-3.0-or-later) and PS2Recomp (GPL-3.0).
Killzone is a trademark of Sony Interactive Entertainment; this project is not affiliated with Sony or Guerrilla Games.
