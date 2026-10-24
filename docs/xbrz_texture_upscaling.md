# xBRZ texture upscaling

**Status:** Private visual-probe tooling ready; authorized A/B capture pending

**Started:** 2026-10-24

**Feature branch:** `feature/xbrz-texture-upscaling`

## Decision summary

Add an opt-in 2x xBRZ mode for decoded game textures. The prototype must use
PCSX2's hardware-renderer texture hash cache, leave render targets and
framebuffer feedback unchanged, and give manually supplied replacement textures
priority. The feature remains off by default and is not part of any existing
graphics preset.

The first implementation factor is deliberately limited to 2x. A PS2 texture
can be much larger than a PSP texture, and memory use grows with the square of
the scale factor. Higher factors are deferred until 2x has passed visual,
frame-time, and memory qualification.

## Purpose

Internal render scale improves geometry edges and the sampling of the final
scene, but it does not add source texels to the game's original textures. xBRZ
is an edge-aware CPU scaler that can improve hard-edged low-resolution art
before it is uploaded to the GPU.

The expected strongest candidates are:

- HUD elements and fonts.
- Weapon icons, signs, decals, and logos.
- Other textures with deliberate hard edges and limited color transitions.

The expected weak candidates are noisy or photographic surfaces. Killzone's
gritty art direction may make some materials look rounded or painted after
xBRZ processing. The feature therefore needs direct A/B evidence and must
remain optional.

This is texture upscaling, not whole-frame post-processing. Applying xBRZ to
the composed frame would also process high-resolution geometry, particles,
movies, and HUD output and would conflict with the existing internal-resolution
pipeline.

## User-facing contract

The prototype contract is:

| Surface | Value |
|---|---|
| Launcher label | `Texture upscaling` |
| Choices | `Off (original textures)`, `2x xBRZ` |
| Configuration | `[Graphics] TextureUpscaling=Off|Xbrz2x` |
| Automation override | `KZ_TEXTURE_UPSCALE=off|xbrz2` |
| Diagnostic logging | `KZ_TEXTURE_UPSCALE_LOG=1` |
| Default | `Off` |

Behavior:

- D3D11, D3D12, and Vulkan use the same setting through the shared PCSX2
  hardware texture cache.
- The software renderer does not upscale textures.
- Existing presets explicitly select `Off`.
- Manual replacement textures take priority over generated xBRZ textures.
- Disabling the option preserves the existing texture-cache path and output.
- A global initialization or resource failure disables xBRZ for the run,
  reports the reason, and continues with native textures.
- A per-texture eligibility or budget rejection is counted and optionally
  logged; it is not a fatal renderer error.

The configuration schema will need a version bump with a deterministic
`TextureUpscaling=Off` migration for older files.

## Prior art and source provenance

The local PSP recompiler runtime already has a bounded xBRZ implementation:

- Source: PPSSPP v1.20.4 commit
  `fa50bb1976065c4f8b1b47af227d367fe9771555`.
- Wrapper: a small C ABI around decoded 32-bit RGBA input.
- Current PSP range: 1x through 5x.
- Validated backends: D3D12 and Vulkan.
- Exclusions: framebuffer and render-target textures.
- Caching: scale once when decoded, then retain the GPU resource.

The xBRZ files carry their existing GNU GPL notice and MAME linking exception.
The exact notice and provenance README must be retained and included in the
project's release notices and SBOM.

Killzone must vendor the reviewed source under
`ext/kzgs/third_party/xbrz/`. It must not depend on a developer-specific
`D:\Github\psp-recomp` checkout.

The foundation imports the reviewed source byte-for-byte from
`Tzeentchnet/psp-recomp` commit
`138c2b8bc96ac5ec7e4f7b4224b37b28b9375ee5` (`Add hardware texture scaling
controls`). The dependency lock verifies these normalized SHA-256 values:

| File | Normalized SHA-256 |
|---|---|
| `README.md` | `178f69b186531fbea6cad6a87cfe8fa60eb33e541783dd09f6a3ffc0024eb165` |
| `config.h` | `bbb7d3ab1c25851673a71c126188cc1f02dc483819f04af8a8241dac71e9fe1a` |
| `xbrz.cpp` | `35283301d48ff518c8394c674195d086325697a51bf4d72213e636f1df213c7e` |
| `xbrz.h` | `4d458002afe225303cb013191466196104ea12cf5d3526f031f2df3a8add52d7` |

## Killzone renderer path

The current configuration flow is:

1. `KzConfig` loads strict launcher and INI state.
2. `toKzgs()` converts it to `KzgsConfig`.
3. `kzgs` converts that to `Pcsx2Config::GSOptions`.
4. All hardware backends use PCSX2's `GSTextureCache`.

For ordinary sampled textures, `GSTextureCache::LookupHashCache()`:

1. Computes the content and palette-aware hash key.
2. Checks for a cached resource.
3. Checks for a manual replacement.
4. Decodes GS local memory and applies the palette.
5. Uploads the base level and authored mip levels.

Sources that directly alias render targets take a separate path. This lets the
prototype upscale decoded source textures without changing render-target
resolution, framebuffer feedback, depth surfaces, presentation, or internal
render scale.

## Required ordering

Texture selection must use this order:

1. Direct render-target or framebuffer-feedback source: use the existing
   target resource unchanged.
2. Manual replacement texture: use the replacement unchanged.
3. Eligible decoded source with `Xbrz2x`: decode, scale, upload, and cache.
4. Ineligible or rejected source: use the existing native decoded texture.

Paletted textures must be scaled only after palette application. The cache key
must retain the palette hash so two palettes using the same indices cannot
share the wrong generated texture.

Each authored mip level must be scaled independently. Scaling only the base
level and generating unrelated lower levels would change the game's mip
transitions.

## Prototype safety boundaries

The first runtime prototype uses these limits:

- Scale factor: exactly 2x.
- Hardware texture sources only.
- Maximum native dimensions: the existing hash-cache eligibility limit.
- Maximum generated dimension: checked against the active backend's texture
  limit before allocation.
- Generated xBRZ hash-cache budget: 512 MiB.
- Existing global PCSX2 hash-cache safety limit remains authoritative.
- Movies detected through the existing FMV state are not scaled.
- Render targets, depth surfaces, framebuffer feedback, and software-renderer
  textures are never scaled.
- Width, height, pitch, pixel count, and byte-size multiplication must be
  overflow checked.

Generated resources remain content-hash cached. When guest texture contents
change, normal cache invalidation creates a new native or xBRZ resource rather
than partially updating a scaled image. xBRZ needs neighboring rows to make
edge decisions, so applying ordinary dirty rectangles directly to the scaled
texture would be incorrect.

The first implementation may scale synchronously on a cache miss so its cost
can be measured accurately. If first-use stalls fail the acceptance gate, the
next design step is:

1. Decode and copy immutable RGBA input on the GS thread.
2. Scale it on the existing texture worker or a bounded dedicated worker.
3. Inject the finished resource through the hash-cache replacement mechanism.
4. Keep the native resource active until injection completes.

Async work must be cancelable during renderer reset and shutdown. It must not
read mutable GS memory from a worker thread.

## Memory model

RGBA memory grows by the square of the factor:

| Native dimensions | Native | 2x | 3x | 5x |
|---|---:|---:|---:|---:|
| 256x256 | 256 KiB | 1 MiB | 2.25 MiB | 6.25 MiB |
| 512x512 | 1 MiB | 4 MiB | 9 MiB | 25 MiB |
| 1024x1024 | 4 MiB | 16 MiB | 36 MiB | 100 MiB |

The prototype must track at least:

- Number of native, manually replaced, generated, and rejected textures.
- Generated GPU bytes currently resident and peak resident bytes.
- Temporary CPU bytes and peak temporary bytes.
- Cache hits, misses, evictions, and content invalidations.
- Total, maximum, and percentile xBRZ CPU time.
- Rejection reason: unsupported path, FMV, dimensions, budget, allocation, or
  scaler failure.

Normal runs should log one startup summary and one shutdown summary. Detailed
per-texture records are enabled only by `KZ_TEXTURE_UPSCALE_LOG=1`.

## Dependency and build strategy

The shared xBRZ source and a narrow wrapper belong in `ext/kzgs`. Unit tests
must compile the same wrapper and source as the product.

The cache integration belongs in the maintained `Tzeentchnet/pcsx2` fork
because `kzgs` currently compiles PCSX2's texture-cache sources directly.
Copying `GSTextureCache.cpp` into this repository would create a large,
unreviewable source fork and is not acceptable.

Dependency update order:

1. Implement and test the bounded PCSX2 fork change.
2. Commit and push the PCSX2 commit before changing the superproject lock.
3. Update `config/dependencies.lock.json` to the remotely reachable commit.
4. Refresh the local dependency checkout.
5. Run `python tools\scripts\verify_deps.py --strict`.
6. Confirm a fresh checkout can fetch the locked revision.

The missing-PS2Recomp-gitlink incident must not be repeated.

## Staged work

### Stage 0: feature contract

- Establish this document and the feature branch.
- Keep product behavior unchanged.

### Stage 0A: scaler foundation

- Vendor the reviewed xBRZ source with provenance, notices, and immutable
  normalized hashes.
- Add a checked, allocation-free 2x wrapper and an ISO-free unit test.
- Register the source with strict dependency verification.
- Keep the scaler disconnected from the runtime texture path.

### Stage 1: private visual probe

- Capture representative textures with the existing dump path.
- Run the reviewed xBRZ implementation over the private dumps.
- Load the results through existing replacement-texture support.
- Compare menu, HUD, weapons, characters, environment surfaces, and effects
  at native and high internal resolution; verify movies remain unmodified.
- Do not commit or distribute dumped or generated game-derived images.

This stage decides whether xBRZ is visually suitable for enough Killzone
textures to justify runtime integration.

### Stage 2: runtime 2x prototype

- Add strict configuration, launcher, and automation surfaces.
- Integrate 2x generation after manual replacement lookup.
- Add exclusions, budget enforcement, and instrumentation.
- Update and verify the pinned PCSX2 fork.

### Stage 3: qualification

- Run ISO-free unit, component, and renderer golden tests.
- Run the authorized gameplay matrix.
- Capture performance, memory, cache, and visual evidence.
- Decide whether synchronous scaling is acceptable.
- Keep the feature experimental or remove it if its quality/cost tradeoff
  fails.

Three-times and higher scaling, GPU compute scaling, automatic per-texture
classification, and prebuilt texture packs are outside the initial prototype.

## Private visual-probe workflow

The offline `kzgs_xbrz_probe` tool exercises the production scaler wrapper
without adding a runtime texture-cache call site. It uses Windows Imaging
Component to decode and encode PNGs, preserves PCSX2's relative filenames, and
does not require an ISO.

Build and verify it from the repository root:

```powershell
tools\scripts\vsenv.bat cmake --build --preset win-msvc-tests --target kzgs_xbrz_probe
build\win-msvc-rel\ext\kzgs\kzgs_xbrz_probe.exe --self-test build\win-msvc-rel\test-state\xbrz-probe
```

For an authorized private capture:

1. Set `KZ_TEX_DUMP=1`, start the game normally, and capture only the short
   scene needed for the comparison.
2. Clear `KZ_TEX_DUMP` immediately after the capture. Dumps are written to
   `<exe>\textures\SCUS-97402\dumps`.
3. Copy a curated set of non-FMV PNGs into an ignored private directory such
   as `work\xbrz-probe\selected`. Keep every PCSX2 filename unchanged.
4. Preview eligibility without writing output:

   ```powershell
   build\win-msvc-rel\ext\kzgs\kzgs_xbrz_probe.exe `
     --input work\xbrz-probe\selected `
     --output build\win-msvc-rel\textures\SCUS-97402\replacements `
     --dry-run
   ```

5. Remove `--dry-run` to generate the private replacement PNGs. Existing
   replacements are preserved unless `--overwrite` is supplied explicitly.
6. Restart the game with `KZ_TEX_REPLACE=0` for the baseline and
   `KZ_TEX_REPLACE=1` for the xBRZ comparison. Record the renderer, internal
   resolution, scene, source dimensions, and visual result.

The current dump switch also permits dumps while an FMV is active. The probe
cannot identify movies from a hash filename, so FMV selection must be excluded
manually. Inputs over 1024 pixels in either dimension are reported and skipped.
The output directory is refused when it is the input directory or one of its
descendants.

Keep all dumped and generated images in ignored build/work locations or
outside the repository. Never stage, commit, publish, or distribute them.

## Validation matrix

### ISO-free

- xBRZ wrapper:
  - Reject null input, zero dimensions, unsupported factors, and overflow.
  - Preserve alpha.
  - Produce deterministic 2x output.
  - Preserve a single-color image exactly.
- Offline probe:
  - Round-trip a synthetic BGRA8 PNG through WIC and xBRZ without changing
    color or alpha.
  - Accept a 1024-pixel input dimension and reject 1025 pixels.
  - Make `--dry-run` write no output.
  - Preserve existing replacements unless overwrite is explicit.
  - Reject an output directory nested under the input directory.
- Configuration:
  - Missing setting defaults to `Off`.
  - Valid values round-trip.
  - Invalid values produce a configuration diagnostic.
  - Older schema versions migrate to `Off`.
- Texture cache:
  - Manual replacement beats generated xBRZ.
  - Palette variants do not alias.
  - Every authored mip has the expected scaled dimensions.
  - Render-target and framebuffer-feedback sources remain unchanged.
  - Budget rejection returns the native texture and records the reason.
  - Disabling the feature preserves current golden output.
- Renderer:
  - D3D11, D3D12, and Vulkan component and golden tests.
  - Renderer reset and shutdown with queued or resident generated textures.

### Authorized game-data testing

Use the supported normal-profile route and record exact settings:

- Opening movies and front end.
- Profile selection and first-mission gameplay.
- Weapon, scope, HUD, particle, and transparent textures.
- Quit to menu and Continue/load.
- Renderer switch runs for D3D11, D3D12, and Vulkan.
- Manual replacement precedence using a private synthetic test replacement.
- At least one 90-second capture for frame time, scaling time, memory, and
  cache churn with the feature off and on.

## Acceptance gates

| Gate | Requirement |
|---|---|
| Off-path parity | Existing ISO-free and renderer golden results remain unchanged |
| Correct scope | No render target, feedback, depth, movie, or software texture is xBRZ-scaled |
| Replacement precedence | Manual replacements always win |
| Cache correctness | No stale texture, palette alias, partial-update corruption, or shutdown race |
| Memory safety | Generated residency never exceeds the configured 512 MiB prototype budget |
| Backend coverage | D3D11, D3D12, and Vulkan complete the component and gameplay routes |
| Visual quality | Selected hard-edged textures improve without widespread painted or halo artifacts |
| Stability | No renderer fault, device loss, host crash, or new gameplay failure |
| Dependency integrity | Strict dependency verification passes and every pinned commit is remotely reachable |

Performance thresholds will be fixed after the Stage 1 baseline capture. At a
minimum, warmed-cache gameplay must remain within 2% of the disabled median
frame time, and one-time cache population must not create repeated long-frame
bursts during normal traversal.

## Stop conditions

Stop runtime integration and retain only the private replacement-pack workflow
if any of these remain true after one bounded correction:

- Most representative environment or character textures look worse.
- Generated textures repeatedly exhaust the 512 MiB budget.
- Dynamic texture churn causes recurring frame-time spikes.
- Correct async operation would require unsafe GS-memory access or a broad
  renderer rewrite.
- The disabled path cannot remain output-equivalent to the current renderer.

## Evidence log

| Date | Stage | Evidence | Result |
|---|---|---|---|
| 2026-10-24 | Architecture review | PSP implementation and Killzone/PCSX2 cache paths inspected | 2x hash-cache prototype is feasible; higher factors deferred |
| 2026-10-24 | Stage 0 | Feature contract and branch created | Product behavior unchanged |
| 2026-10-24 | Stage 0A | Reviewed xBRZ source vendored from `psp-recomp` commit `138c2b8bc96a`; checked 2x wrapper and ISO-free test added | Product behavior remains unchanged; there is no runtime call site |
| 2026-10-24 | Foundation verification | `kzgs_test` and `kzgs_xbrz_test` built; focused CTest passed 2/2; strict dependency verification passed 33/33; a deliberately corrupted temporary xBRZ hash was rejected | Foundation gates passed |
| 2026-10-24 | Stage 1 tooling | Added WIC-based private PNG probe with dry-run, exact 1024/1025 eligibility boundary, existing-file protection, atomic output, and a synthetic round-trip self-test | Probe/scaler/GS CTest passed 3/3; CLI guard paths passed; authorized visual A/B evidence is still required |
