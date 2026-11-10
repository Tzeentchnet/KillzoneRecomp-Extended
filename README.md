# KillzoneRecomp-Extended

A native Windows PC port of **Killzone** (PlayStation 2, 2004), built by statically recompiling the game's PS2 code
with [PS2Recomp](https://github.com/ran-j/PS2Recomp) and using PCSX2-based GS, microVU, IPU, and SPU2 components.

> **Bring your own disc.** This repository contains no game code or data. You need a supported Killzone NTSC-U
> (`SCUS-97402`) or PAL (`SCES-52893`, English/Russian/Greek) disc image.

This project continues [drakolordx7/KillzoneRecomp](https://github.com/drakolordx7/KillzoneRecomp) with additional
graphics, display, input, configuration, testing, and runtime work. The active target is Windows x64.

## Highlights

- D3D11, D3D12, and Vulkan renderers with internal resolution scaling up to 8x.
- Native 4:3 and 16:9 plus automatic, 21:9, and 32:9 ultrawide modes with centered HUD and menu presentation.
- Independent world and weapon field-of-view controls with zoom-preserving tangent-space scaling.
- High-refresh gameplay with real-time game-speed correction.
- Raw mouse aim, rebindable keyboard controls, SDL3 controllers, adjustable dead zone/range, and dynamic button
  prompts.
- Anisotropic filtering, FXAA, D3D11 SMAA, sharp scaling, CAS sharpening, and bounded 2x view distance.
- Default-off Vulkan FSR 1 spatial presentation, available through
  [experimental configuration](docs/configuration.md#experimental-presentation-upscaler).
- Launcher-managed display, graphics, controls, audio, disc, and isolated testing settings.
- US and PAL runtime support from separately generated code sets in one executable.

## Getting started

### Requirements

- Windows 10/11 x64
- Visual Studio Build Tools 2022 or newer with **Desktop development with C++**
- CMake 3.24+, Python 3.11+, and an x64 CPU with SSE4.1
- A supported retail disc image

Clone with submodules:

```powershell
git clone --recursive https://github.com/Tzeentchnet/KillzoneRecomp-Extended
Set-Location .\KillzoneRecomp-Extended
```

Follow the [building guide](docs/building.md) to provision locked dependencies, generate private game-derived C++,
and build `killzone.exe`. Start `launch_launcher.bat`, select your disc image, and configure the port.

Player settings and bindings are documented in [Configuration and controls](docs/configuration.md).

## Project status

This is a test build, not an official release.

- Boot, movies, menus, profile creation, campaign gameplay, save/load, mouse/keyboard, controllers, high-refresh
  timing, and clean shutdown have been validated on the current Windows build.
- D3D11, D3D12, and Vulkan are covered by ISO-free renderer tests. D3D12 is the reference backend.
- Intermittent effects or dialogue truncation can still occur during action-heavy combat. The current buffering
  mitigation reduces the problem but does not eliminate it.
- VSync is recommended; disabling it can produce substantial tearing.
- macOS/Apple Silicon work is experimental and deferred from the active release path.

Not implemented:

- Killzone Online, DNAS, network login, lobby, or multiplayer gameplay.
- DLSS, XeSS, FSR 2+, or other temporal reconstruction. The GS boundary does not expose a reliable gameplay depth,
  motion-vector, or separate HUD/reactive contract.
- HDR output; the current renderer presents composed SDR color.
- Distribution of retail assets, generated game code, saves, or memory-card images.

Technical status and evidence live in [Findings](docs/findings.md). Archived online research is preserved under
[Multiplayer](docs/multiplayer/README.md).

## Development

The checked Windows workflow is:

```powershell
.\tools\scripts\cmake_preset.ps1 -Preset win-msvc-rel
.\tools\scripts\cmake_preset.ps1 -Preset win-msvc-tests -Action build
.\tools\scripts\cmake_preset.ps1 -Preset win-msvc-rel -Action test -Label iso-free
```

Equivalent clang-cl presets are available. See:

- [Building](docs/building.md)
- [Testing and diagnostics](docs/testing.md)
- [Documentation index](docs/README.md)
- [Runtime patch rationale](patches/README.md)
- [Release and distribution rules](docs/release.md)

Generated sources and retail inputs remain private. The committed dependency lock and generated manifest retain only
reproducible, protected-content-safe provenance.

## Repository layout

| Path | Purpose |
|---|---|
| `src/` | Port integration, launcher, input, timing, patches, and runtime hooks |
| `ext/kzgs`, `ext/kzvu`, `ext/kzipu`, `ext/kzspu2` | PCSX2-derived components |
| `ext/PS2Recomp` | Locked PS2Recomp runtime fork |
| `host/` | Renderer-independent host platform boundary |
| `config/` | Recompiler configurations and dependency/generated provenance |
| `patches/` | Reconstructible PS2Recomp changes and rationale |
| `tools/` | Build, regeneration, testing, profiling, and analysis scripts |
| `docs/` | User guides, technical evidence, and archived research |

## Credits

- [drakolordx7](https://github.com/drakolordx7) and contributors created the original Killzone recompilation and PC
  port foundation.
- [Ash01Live](https://github.com/Ash01Live) contributed the original field-of-view settings through
  [drakolordx7/KillzoneRecomp#21](https://github.com/drakolordx7/KillzoneRecomp/pull/21).
- [ran-j/PS2Recomp](https://github.com/ran-j/PS2Recomp) and its contributors provide the static recompiler and runtime
  foundation.
- [PCSX2](https://github.com/PCSX2/pcsx2) contributors provide the renderer, microVU, IPU, and SPU2 foundations.
- [AMD FidelityFX FSR 1](ext/kzgs/third_party/fsr1/README.md) provides the MIT-licensed spatial upscaling source.

Developed with AI assistance.

## License

Repository-owned code and documentation are MIT under [LICENSE](LICENSE), except for the components identified there
as GPL-3.0. PCSX2-derived libraries, PS2Recomp, the field-of-view contribution, and every compiled build remain
GPL-3.0; see [LICENSE-GPL-3.0](LICENSE-GPL-3.0).

Killzone is a trademark of Sony Interactive Entertainment. This project is not affiliated with Sony or Guerrilla
Games.
