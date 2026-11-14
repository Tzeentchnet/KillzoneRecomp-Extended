# Killzone Recomp Extended: Native Killzone PS2 Port for Windows PC

**Killzone Recomp Extended** is an unofficial native Windows PC port of **Killzone** (2004) for PlayStation 2 (PS2),
built through static recompilation. It combines the
[PS2Recomp static recompiler](https://github.com/ran-j/PS2Recomp) with PCSX2-based GS, microVU, IPU, and SPU2
components.

The port supports Direct3D 11, Direct3D 12, and Vulkan rendering, high-refresh gameplay, ultrawide displays,
mouse-and-keyboard controls, gamepads, internal resolution scaling, and optional AMD FidelityFX FSR 1.

> [!IMPORTANT]
> **NEW FEATURE: AMD FidelityFX FSR 3.1.6 Super Resolution**
>
> Vulkan now includes source-built FSR 3 Super Resolution through the explicit experimental-input mode. This is
> Super Resolution only, not frame generation, and its approximate temporal inputs can produce HUD or disocclusion
> ghosting. See the [experimental upscaler configuration](docs/configuration.md#experimental-presentation-upscaler).

> **Bring your own disc.** This repository contains no game code or data. You need a supported Killzone disc image:
> NTSC-U `SCUS-97402`, PAL `SCES-52893` (English/Russian/Greek), or PAL `SCES-52004`
> (English/French/German/Spanish/Italian/Dutch).

> [!NOTE]
> **Companion Blender add-on:** [Killzone Blender Importer](https://github.com/Tzeentchnet/Killzone-Blender-Importer)
> provides Blender 5.2 tools for inspecting and importing Killzone assets.

This community project continues [drakolordx7/KillzoneRecomp](https://github.com/drakolordx7/KillzoneRecomp) with
additional graphics, display, input, configuration, testing, and runtime work. The active development target is
Windows x64.

## Killzone PC port features

- D3D11, D3D12, and Vulkan renderers with internal resolution scaling up to 8x.
- A **60 FPS headroom** preset with balanced automatic scaling and Vulkan FSR 1 presentation upscaling.
- Native 4:3 and 16:9 plus automatic, 21:9, and 32:9 ultrawide modes with centered HUD and menu presentation.
- Independent world and weapon field-of-view controls with zoom-preserving tangent-space scaling.
- High-refresh gameplay with real-time game-speed correction.
- Raw mouse aim, rebindable keyboard controls, SDL3 controllers with DualShock 2 rumble, adjustable dead zone/range,
  and dynamic button prompts.
- Anisotropic filtering, FXAA, D3D11 SMAA, sharp scaling, CAS sharpening, and bounded 2x view distance.
- Default-off Vulkan FSR 1 spatial presentation, available through
  [experimental configuration](docs/configuration.md#experimental-presentation-upscaler).
- Default-off Vulkan FSR 3.1.6 Super Resolution in explicit experimental-input mode. This is SR only, not frame
  generation, and its synthetic depth/Optical Flow inputs can produce HUD or disocclusion ghosting.
- Launcher-managed display, graphics, controls, audio, disc, and isolated testing settings.
- US and two PAL executable versions supported from separately generated code sets in one executable.
- Readback-free pacing diagnostics with asynchronous whole-frame and Vulkan/D3D12 per-stage GPU timing.

## Build and play on Windows

### System and build requirements

- Windows 10/11 x64
- Visual Studio Build Tools 2022 or newer with **Desktop development with C++**
- CMake 3.24+, Python 3.11+, and an x64 CPU with SSE4.1
- A supported retail disc image

Clone with submodules:

```powershell
git clone --recursive https://github.com/Tzeentchnet/KillzoneRecomp-Extended
Set-Location .\KillzoneRecomp-Extended
```

The build can contain these separately generated executable versions:

| Disc | Boot executable | Languages | `regen.py` mode |
|---|---|---|---|
| NTSC-U `SCUS-97402` | `SCUS_974.02` | English | default |
| PAL `SCES-52893` | `SCES_528.93` | English, Russian, Greek | `--pal` |
| PAL `SCES-52004` | `SCES_520.04` | English, French, German, Spanish, Italian, Dutch | `--eu` |

The US generated tree is required. When `generated_pal\` or `generated_eu\` is present during configuration, CMake
adds that prefixed code set to the same `killzone.exe`; omitting either directory disables only that matching disc.

Follow the [Killzone Recomp Extended building guide](docs/building.md) to provision locked dependencies, generate
private game-derived C++, and build `killzone.exe`. Start `launch_launcher.bat`, select your disc image, and configure
the port.

Player settings and bindings are documented in the
[configuration and controls guide](docs/configuration.md).

## Killzone PC port compatibility and status

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
- DLSS, XeSS, FSR 2, or production-qualified temporal reconstruction. FSR 3 SR is available only as an explicit
  experiment using marked approximations; strict mode still fails closed because reliable gameplay depth, motion,
  and separate HUD/composition inputs are unavailable.
- HDR output; the current renderer presents composed SDR color.
- Distribution of retail assets, generated game code, saves, or memory-card images.

Technical status and evidence live in [Findings](docs/findings.md). Archived online research is preserved under
[Multiplayer](docs/multiplayer/README.md).

## Build, test, and contribute

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
| [`docs/xbrz_texture_upscaling.md`](docs/xbrz_texture_upscaling.md) | Feature contract, architecture, safety boundaries and acceptance gates for the xBRZ prototype |

## Credits

- [drakolordx7](https://github.com/drakolordx7) and contributors created the original Killzone recompilation and PC
  port foundation.
- [Ash01Live](https://github.com/Ash01Live) contributed the original field-of-view settings through
  [drakolordx7/KillzoneRecomp#21](https://github.com/drakolordx7/KillzoneRecomp/pull/21).
- [ran-j/PS2Recomp](https://github.com/ran-j/PS2Recomp) and its contributors provide the static recompiler and runtime
  foundation.
- [PCSX2](https://github.com/PCSX2/pcsx2) contributors provide the renderer, microVU, IPU, and SPU2 foundations.
- [AMD FidelityFX FSR 1](ext/kzgs/third_party/fsr1/README.md) provides the MIT-licensed spatial upscaling source.
- [AMD FidelityFX SDK](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK) provides the source-built FSR 3
  Super Resolution and Optical Flow implementation under its included notices.

Developed with AI assistance.

## License

Repository-owned code and documentation are MIT under [LICENSE](LICENSE), except for the components identified there
as GPL-3.0. PCSX2-derived libraries, PS2Recomp, the field-of-view contribution, and every compiled build remain
GPL-3.0; see [LICENSE-GPL-3.0](LICENSE-GPL-3.0).
The vendored [xBRZ sources](ext/kzgs/third_party/xbrz/README.md) retain their GPL notice and MAME linking exception.

Killzone is a trademark of Sony Interactive Entertainment. This project is not affiliated with Sony or Guerrilla
Games.
