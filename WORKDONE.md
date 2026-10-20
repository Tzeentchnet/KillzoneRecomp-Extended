# WORKDONE - verified completions

**Last updated:** 2026-10-20

The ledger of finished work. An entry belongs here only after the acceptance commands in
[`IMPLEMENTATION_PLAN.md`](IMPLEMENTATION_PLAN.md) pass on the machine the plan names. Open and partially evidenced
work, including Windows-only evidence for macOS gates, stays in [`TODO.md`](TODO.md). The reasoning and measurements
behind each item are in [`docs/findings.md`](docs/findings.md), [`docs/assetforms.md`](docs/assetforms.md) and
[`patches/README.md`](patches/README.md), as applicable.

Each entry records: commit, date, machine and toolchain, acceptance commands and results, decisions, and residual
risk. Do not edit an entry's evidence after the fact. If a later run disagrees, add a new dated re-verification.

## LEGAL-01 - Distribution and provenance disposition

- **Completed:** 2026-10-20 by direct repository-owner instruction.
- **Source baseline:** `6363163` plus the current documentation changes. No toolchain or runtime command applies;
  the plan requires written human disposition rather than tool output.
- **Project and copyleft obligations:** the project remains GPL-3.0. Any distributed binary must be accompanied by
  the applicable license and notices and an offer or equivalent access to complete corresponding source. The locked
  GPL dependencies retain their own corresponding-source and notice obligations.
- **Dependency disposition:** `config/dependencies.lock.json` is the distribution inventory. MIT and zlib components
  require their notices; FFmpeg requires an artifact-specific LGPL configuration, notice, relinking and source
  review. The PCSX2 Windows dependency bundle, libdwarf, and any other `NOASSERTION` or review-required entry are
  excluded from release artifacts until every bundled component is identified and cleared. A passing dependency
  hash check does not replace that review.
- **Generated and protected content:** the retail disc image, executable, extracted assets, private function map,
  generated game-derived source, regeneration logs and private generated tree are BYOD-only and must not be
  committed or distributed. Only non-content provenance such as hashes, counts, bounds and hook signatures may be
  published.
- **Trademark and naming:** Killzone remains identified as a Sony Interactive Entertainment trademark. Project
  naming is nominative only, must retain the non-affiliation disclaimer, and must not package Sony or Guerrilla
  logos, branding or protected assets.
- **Proprietary SDKs:** proprietary upscaler or platform SDK binaries are not vendored or redistributed. A feature
  that needs one remains disabled until builders acquire it under the vendor's terms and a separate redistribution
  review approves the resulting artifact.
- **Artifact controls:** every release candidate needs an SBOM, dependency notices, corresponding-source review,
  secret scan, protected-content scan and package-allowlist audit. Failure or an unresolved result blocks release.
- **Repository visibility:** `origin` remains private. LEGAL-01 completion does not authorize a visibility change;
  publication requires a separate explicit repository-owner decision.
- **Residual gate:** this closes the Phase 0 human-disposition dependency. It is not approval of a release artifact;
  the per-artifact checks in the plan's release gate remain mandatory.

## Re-verification: Windows regression set, 2026-10-20

- **Source baseline:** `b00e576` plus this session's working-tree changes (dependency lock re-pinned to PS2Recomp
  `b44b2dcba0fe5a247b6f42176dd41f36168b6565` with topic patch `0030`, `src/kz_main.cpp` no-disc message,
  `tools/scripts/win06_gate.ps1`, documentation)
- **Machine:** Windows 11 x64; checked MSVC and clang-cl presets

| Command | Result |
|---|---|
| `python .\tools\scripts\verify_deps.py --strict` | PASS: 31 passed, 0 warnings, 0 failures; 62-path canonical patch reconstructs the locked tree (it failed on `b00e576`: the gitlink had moved without the lock) |
| `python .\tools\scripts\verify_resume_table.py .\generated` | PASS: 132,656 dispatch entries, 100,661 resume cases, 31,995 targets |
| `python .\tools\scripts\inventory_generated.py --check` | PASS: 47 hook signatures |
| `tools\scripts\cmake_preset.ps1 -Preset win-msvc-tests -Action build`, then `-Preset win-msvc-rel -Action test -Label iso-free` | PASS: 12/12 |
| `tools\scripts\cmake_preset.ps1 -Preset win-clang-tests -Action build`, then `-Preset win-clang-rel -Action test -Label iso-free` | PASS: 12/12 |
| `git diff --check` | PASS |
| `killzone.exe --no-launcher` with `PS2X_HEADLESS=1` and a `KZ_CONFIG` without an ISO | exit 1 with "No game disc image is selected." (previously "ELF not found: " with an empty path) |

WIN-06 remains open (8/10 on its gate in all three arms measured this session); see `TODO.md`.

## Re-verification: Windows regression set, 2026-10-19

- **Source baseline:** `9f8ca86`; PS2Recomp `6e4bc1827a65d45be92d10f9eca97c04f062f4b8`
- **Machine:** Windows 11 x64; existing checked MSVC and clang-cl presets

| Command | Result |
|---|---|
| `python .\tools\scripts\verify_deps.py --strict` | PASS: 31 passed, 0 warnings, 0 failures |
| `python .\tools\scripts\verify_resume_table.py .\generated` | PASS: 132,656 dispatch entries, 100,661 resume cases, 31,995 targets |
| `python .\tools\scripts\inventory_generated.py --check` | PASS: 47 hook signatures, 101,715 recompiler-resumable entries |
| `tools\scripts\cmake_preset.ps1 -Preset win-msvc-tests -Action build`, then `-Preset win-msvc-rel -Action test -Label iso-free` | PASS: 12/12 |
| `tools\scripts\cmake_preset.ps1 -Preset win-clang-tests -Action build`, then `-Preset win-clang-rel -Action test -Label iso-free` | PASS: 12/12 |

WIN-04 remains open. Its scripted portion used an isolated empty card and the front-end autopilot on D3D12 at
120 Hz: it created and saved `Tester`, selected `1.Trenches` on Normal, reached first-mission gameplay, exited 0 after
120 seconds, and produced 11 distinct nonblack captures without a crash signature. The hands-on save/load,
quit-to-front-end, high-refresh and mouse checks remain in `TODO.md`.

## Re-verification: relocated checkout, 2026-10-17

The repository moved from `E:\Git\Killzone\KillzoneRecomp-Extended` to `D:\Github\KillzoneRecomp-Extended`. `build\`
was emptied and every artifact was rebuilt from D:. No source, config, cache or environment variable still points at
`E:` (ignored `work\*.toml` intermediates were rewritten; their paths are regenerated by `regen.py` anyway).

- **Commit:** `ef6339a` plus uncommitted working-tree changes (`KZ_DEV_LINK` option, README and doc updates)
- **Machine:** Windows 11 x64; MSVC 19.44.35229.0; clang-cl 23.1.3; CMake 4.4.4; Ninja 1.13.2 (locked, local)

| Command | Result |
|---|---|
| `tools\scripts\build_ps2recomp.bat` | PASS: 253/253 steps; `ps2_recomp.exe`, `ps2_analyzer.exe` built |
| `tools\scripts\cmake_preset.ps1 -Preset win-msvc-rel` then `-Action build` | PASS: configure + `killzone.exe` |
| `tools\scripts\cmake_preset.ps1 -Preset win-clang-rel` then `-Action build` | PASS: configure + `killzone.exe` |
| `tools\scripts\cmake_preset.ps1 -Preset win-msvc-tests -Action build`, then `-Preset win-msvc-rel -Action test -Label iso-free` | PASS: 12/12 |
| `tools\scripts\cmake_preset.ps1 -Preset win-clang-tests -Action build`, then `-Preset win-clang-rel -Action test -Label iso-free` | PASS: 12/12 |
| `python .\tools\scripts\verify_deps.py --strict` | PASS: 31 passed, 0 warnings, 0 failures; 61-path canonical patch reconstructs the locked tree |
| `python .\tools\scripts\verify_resume_table.py .\generated` | PASS: 132,656 dispatch entries, 100,661 resume cases, 31,995 targets |
| `python .\tools\scripts\inventory_generated.py --check` | PASS: 42 hook signatures, 101,715 recompiler-resumable entries |
| BASE-02 `git grep` path scan | PASS: no matches |

The ISO-free suite is `kz_product_selftest`, `kz_spu2_component`, `kz_vu1_component`, `kz_vu0_component`,
`kz_gs_component`, `kzgs_d3d11_golden`, `kzgs_d3d12_golden`, `kzgs_vulkan_golden`, `kz_host_core_component`,
`kz_host_window_smoke`, `kz_host_audio_smoke` and `kzvu_interpreter_conformance`. `kz_ipu_byod` is registered and
disabled by default.

Known noise, not failures: `iop_cdvd.cpp(287)` warning C4834 (discarded `[[nodiscard]]`) inside the PS2Recomp
submodule, and the failed toml11 GCC-flag probes during the PS2Recomp configure.

## Phase 0 work packages

### BASE-01 - Canonical source and dependency lock

- **Commit:** `3d4ebbb` (2026-10-17)
- **Evidence:** strict verification passes (28 checks at completion, 31 after later SDL/MPEG lock additions); the
  locked PS2Recomp patch reconstructs the exact target tree. `config/dependencies.lock.json` records source,
  revision or checksum, acquisition method, license and redistribution status.
- **Re-verified:** 2026-10-17 from D: (above).

### BASE-02 - Relocatable tooling

- **Commit:** `3d4ebbb` (2026-10-17)
- **Evidence:** the acceptance scan returns no matches. Scripts resolve the repository from their own location
  (`tools/scripts/kz_paths.ps1`, `build_ps2recomp.bat`, `vsenv.bat`) and require explicit `-Iso`/`-Elf` or
  `KZ_ISO`/`KZ_GAME_ELF` for BYOD inputs.
- **Re-verified:** 2026-10-17. The E: -> D: move needed no source changes, which is itself evidence of relocatability.

### BASE-03 - Reproducible presets and preflight

- **Commit:** `3d4ebbb` (2026-10-17)
- **Evidence:** `win-msvc-rel` and `win-clang-rel` configure with MSVC 19.44.35229.0 and clang-cl 23.1.3; configure
  preflight runs the strict verifier, validates the compiler and Ninja version, and checks `KZ_GENERATED_DIR`. VS Code
  tasks wrap `tools\scripts\cmake_preset.ps1`.
- **Re-verified:** 2026-10-17 from an empty `build\` on D:.

### TEST-01 - Hermetic ISO-free test layer

- **Commit:** `3d4ebbb` (2026-10-17)
- **Evidence:** CTest labels `iso-free` and `byod`; CTest injects an isolated missing `KZ_CONFIG` and `KZ_ISO_FREE=1`
  so disc checks fail before opening a path. At completion: five runnable ISO-free tests plus one disabled BYOD test;
  the suite has since grown to 12.
- **Re-verified:** 2026-10-17, 12/12 under both compilers.

### TEST-02 - Renderer parity oracle

- **Commit:** `3d4ebbb` (2026-10-17)
- **Evidence:** deterministic synthetic 1x/2x pixel oracles pass on D3D11 WARP, D3D12 WARP and Vulkan, five
  consecutive runs per backend.
- **Re-verified:** 2026-10-17 (`kzgs_d3d11_golden`, `kzgs_d3d12_golden`, `kzgs_vulkan_golden`).

### GEN-01 - Generated-code and hook manifest (BYOD)

- **Commit:** `3d4ebbb`, extended by `cc7fc9c` and `4ed9b95` (2026-10-17)
- **Evidence:** `config/generated-manifest.json` locks the supported executable, function map, generated tree,
  dispatch table, instruction patches and 42 fixed guest hook/lookup addresses, each uniquely signature-resolved and
  dispatchable. Separate counts: 16,045 Ghidra functions, 19,307 Ghidra code labels, 31,996 translation units, 31,995
  generated functions, 132,656 dispatch entries, 101,715 recompiler-resumable entries, 100,661 globally dispatchable
  resume cases. Regeneration is deterministic: 31,998 files, 0 changed/new, 0 removed on a repeat run.
- **Re-verified:** 2026-10-17 (resume table and inventory gates above).

### PORT-01 - Host platform boundary

- **Commit:** `3d4ebbb` (2026-10-17)
- **Evidence:** the renderer-free `kz_host_core` target owns typed paths, memory mapping, W^X/cache invalidation,
  timing/synchronization, native-window metadata, diagnostics, environment, CPU/SIMD detection and guest
  floating-point setup. Unsupported operations raise `kz::host::Error`. The clang-cl target build gate and both
  ISO-free suites pass.
- **Re-verified:** 2026-10-17 (`kz_host_core_component` in both suites).

## Asset format research

### ASSET-01 - Retail asset storage and build-pipeline research (BYOD, read-only)

- **Commit:** `b4d01ee` (2026-10-19)
- **Machine:** Windows 11 x64; supported NTSC-U `SCUS_974.02`; read-only executable, repository and previously
  validated archive evidence.
- **Evidence:** [`docs/assetforms.md`](docs/assetforms.md) now separates shipped containment, runtime I/O and
  authoring provenance. Executable code at VA `0x002C2EF8` proves byte `0x08` of the `FILES.DAT` header is the DAT
  volume count (5 in the validated build), while bytes `0x09..0x0B` remain unknown. Code at VA `0x001BCD20` proves
  the `appdir:`, `cache:`, `source:`, `binaries:` and `music:` mount relationships. Repository tracing proves the
  host serves raw ISO sectors to physical `PFILE_R.IRX`, generic EE File/async wrappers exist, and the host GS
  receives GIF bytes rather than semantic assets.
- **Build conclusion:** the minimum defensible model is unknown authoring source -> inferred platform
  cook/serialization -> verified Core/CoreStream resource -> verified DAT packaging -> disc image. Versioned
  Core/CoreTex strings, reconversion diagnostics and platform cache vocabulary support the cooking inference but do
  not identify an original DCC, cooker, source format or exact stage order.
- **Acceptance:** `git diff --check` PASS; Markdown code fences balanced (438). No runtime source or configuration
  changed, so a game smoke run was not applicable.
- **Residual unknowns:** the original DCC/editor and source formats, cooker/packer identity and commands, source-path
  mapping, pre-strip topology, pre-swizzle textures, exact build ordering, and the instruction-complete
  `PFILE_R.IRX` -> DAT -> Core/CoreStream consumer call chain.

### ASSET-02 - Skinned geometry and animation decoder review (BYOD, read-only)

- **Commit:** `b4d01ee` (2026-10-19)
- **Machine:** Windows 11 x64; supported NTSC-U `SCUS_974.02`; executable SHA-256
  `7d3c8ee59c9800ea5238e7ce62bfd09825eb50867123f71a42b81142b6842169`.
- **Corrected decoding:** category-major pose records are consumed sequentially and scattered to original-order bone
  slots; T6/T7/T8 time codecs use 12/24/6 Hz; ADC is a masked Boolean from the raw 16-bit strip control; and the
  packet-alignment count is distinct from per-vertex GS perspective Q.
- **Verified runtime behavior:** strict selector domains are T0-8, R0-9, P0-6 and S0-3. Higher rotation selectors and
  P7 are runtime fallbacks rather than proven serialized codecs; T9-15 leave bracket outputs uninitialized. R8
  requires descriptor bit 0 clear to keep boundary and sampler strides consistent. P6 is big-endian and may address
  the fourth translation slot. BoneMap names bind to the first case-insensitive Skeleton-name match.
- **Safety corrections:** object-relative CoreStream formulas, zero-packet handling, nested-directory ownership,
  DMA/chunk bounds, packed-block read guards, target bounds and adaptive-codec window limits are now explicit in
  [`docs/assetforms.md`](docs/assetforms.md).
- **Validation:** documentation-only; `git diff --check` PASS and Markdown code fences balanced (438). No runtime code
  or configuration changed, so game execution is not an acceptance requirement.
- **Residual unknowns:** actual retail selector frequencies, the R8 encoder invariant, P5 encoder width limits, R9
  valid-code invariants, external Skeleton selection for shared banks, decoded-pose comparison against runtime, and
  the dynamically uploaded skinned VU1 instruction sequence.

### ASSET-03 - Retail animation playback contract (BYOD, read-only)

- **Commit:** `b4d01ee` (2026-10-19)
- **Machine and source:** Windows 11 x64; authorized NTSC-U ISO SHA-256
  `01f071a7381df2aa4ddb49cff3b594ef4066a82dfaff26978cec8ec69cfe91d6`; matching executable SHA-256
  `7d3c8ee59c9800ea5238e7ce62bfd09825eb50867123f71a42b81142b6842169`.
- **Corpus evidence:** 398 variant-5 animation-bearing Cores contain 17,586 clips, 1,589,420 tracks and 14,014,155
  packed bytes. Every packed boundary, padding rule and finite-value check passed. All clips resolve a same-Core
  BoneMap; explicit level-instance graphs prove 656 resource/animation/Skeleton relationships, while shared banks
  retain caller-selected Skeleton requirements.
- **Playback evidence:** mode 1 repeats and mode 2 finishes and holds; serialized duration, not maximum key time, is
  authoritative. Track sampling supplies direct local Q/QT/QTS poses, category-zero static translation and
  category-two scale; hierarchy composition is `parent_global * local`. Root reconstruction uses
  `TotalDistance * (time / duration)` when compensation control is disabled, and simple events use segmented
  duration-relative intervals.
- **Deformation evidence:** packet palette bytes select transform records whose selectors are source-order Skeleton
  bones. Hardware uploads `G_animated` because vertices are bone-local; software skinning proves
  `G_animated * InverseBindGlobal`. The serialized temporary transform is `InverseBindGlobal`, so
  `RestGlobal = inverse(InverseBindGlobal)`.
- **Blender contract:** [`docs/assetforms.md`](docs/assetforms.md) now defines source identities, strict retail codec
  rules, one explicit basis conversion, rest/bind reconstruction, armature-space target matrices, `matrix_basis`,
  Blender 5.2 slotted Actions, authoritative timing, native-evaluator versus baked playback, looping, root motion,
  event metadata and decomposition residual rejection.
- **External-project review:** `D:\Github\Killzone_Blender` was inspected read-only. Its existing synthetic decoder and
  diagnostic rig importer are useful foundations, but T6-T8 rates, P6 byte order, strict P7/R8/P5 handling, clip
  extraction, BoneMap/Skeleton binding, pose construction and Action generation must be brought into conformance.
- **Acceptance:** documentation-only; `git diff --check`, Markdown structure and contract assertions pass. No runtime
  or Blender-project implementation changed, so game execution was not an acceptance requirement.
- **Residual unknowns:** external shared-bank Skeleton ownership, the complete serialized Compensate Root bridge,
  complex events, graph-level blending/masks/additive ordering, exceptional sheared rest corrections, explicit
  confirmation of inferred `+Y` forward/metre units, and animation encoding/export.

## Windows baseline restoration

### Boot, memory cards, movie and menu restoration

- **Commit:** `3d4ebbb`, `cc7fc9c` (2026-10-17)
- **Machine:** Windows x64, NVIDIA GeForce RTX 5070 driver `32.0.16.1742`, MSVC 19.44.35229.0, clang-cl 23.1.3
- **Root causes fixed:** the private function corpus omitted executable continuations, delay slots, generated resume
  PCs and materialized callbacks. Schema-3 range normalization, fixed-point tail/resume closure and callback-boundary
  discovery restore global dispatchability. Explicit ELF plus ISO boot remounts extracted disc files at `cdrom0:`.
  Authoritative local bindings stop overlapping generated owners from shadowing three memory-card and two MPEG stubs.
  The MPEG wrappers set `pc=$ra` before invoking handlers, so suspended `sceMpegGetPicture` calls resume in the guest
  caller.
- **Evidence:** D3D12 probes rendered decoded FMV frames at 35, 40, 45 and 50 s and reached the main menu at 75-80 s.
  The minimized probe kept physical `MCSERV.IRX`, completed host memory-card operations, and logged no MCSERV
  semaphore timeout. A 45-second capture of the visible client showed the Killzone logo. MSVC and clang-cl ISO-free
  12/12.
- **Residual risk:** D3D11 stays black on this driver (TODO WIN-01). Gameplay past the menu was not re-run (TODO
  WIN-04).

### MPEG playback cadence

- **Commit:** `24e9cfa` (2026-10-17); PS2Recomp `917b379` on `Tzeentchnet/PS2Recomp` branch `killzone`; patch
  `patches/ps2recomp/0028-mpeg-nodata-prefetch.patch`
- **Root cause:** the 29.97 fps source starved because `sceMpegCbNodata` callbacks began only after the decoded queue
  emptied, and Killzone usually supplied one or two pictures per callback.
- **Fix:** self-fed movies prefetch at a four-picture low-water mark into the existing eight-picture queue. Prefetch
  excludes `sceCdSt` streams, stops at EOF, sequence end, stream end or decoder failure, and a no-progress retry state
  prevents callback spinning.
- **Evidence:** opening cinematic 49 image changes in 1.6169 s (30.3/s); animated menu 43 in 1.4664 s (29.32/s);
  attract reel 44 in 1.4724 s (29.88/s; 42 ideal two-vblank gaps, one four-vblank gap). A 380-second idle timeline
  showed the attract reel active by 160 s and the menu restored by 260 s, exit 0. Focused PS2Recomp low-water
  regression PASS; strict dependency verification PASS.

### FMV audio continuity under scheduler stalls

- **Commit:** `4ed9b95`, documented in `573a8e5` and `ef6339a` (2026-10-17); PS2Recomp `6e4bc18`; patch
  `patches/ps2recomp/0029-mpeg-refill-budget.patch`
- **Root cause:** exact raw-PCM equality at a 3,584-frame lag showed stale replay of the two-bank movie ADPCM ring, not
  only host output starvation.
- **Accepted policy:** twelve coordinated guest instruction patches expand each channel to four banks (eight and
  sixteen were rejected because misses replayed longer spans). MPEG low-water refill is capped at two successful
  proactive callbacks per presented picture. The SDL callback primes with the device request plus a 40 ms reserve and
  keeps it during high-water recovery, without re-arming after later starvation.
- **Evidence:** three raw captures contained zero old 3,584-frame repeats (final: 12 new 7,168-frame intervals,
  0.878833 s total, 106.083 ms maximum). Two SDL/WASAPI captures measured 1,288 and 3,283 movie-window underrun frames
  (26.8 and 68.4 ms) against a 5,536-frame / 115.3 ms baseline. The video burst kept 44 changes in 90 fields. The user
  accepted the D3D12 audio subjectively ("Audio seems fine for now").
- **Residual risk:** a rare full four-bank miss (7,168 frames / 149.3 ms) is still possible when the movie update stalls
  for several hundred milliseconds. Change this policy only for a reproducible, timestamped regression.

## Inherited upstream baseline (before the fork)

Verified upstream by drakolordx7 at or before the audited baseline `5f0700079f9ac4e14799d66d01747dbcdcec55f6`
(2026-09-28 to 2026-10-01). These are not fork deliverables; WIN-04 re-establishes them on the current build.

| Capability | Evidence |
|---|---|
| Static recompilation with PS2Recomp; PCSX2 GS, microVU, IPU and SPU2 as static libraries | `ext/kz*/README.md` |
| High-refresh timing (frame timer rescaled; ~117-120 fps at 120 Hz) | findings "Frame timing", "High refresh implementation" |
| Raw-count mouse aim, rebindable keys, SDL3 controllers | findings "Mouse aim", "Controls", "Mouse yaw, second pass" |
| VIF1 worker, EE frame budget, IOP RPC offload, DMA copies, scheduler-free control flow, frame pipeline (patches 0016-0025) | `patches/README.md`; findings sections of the same names |
| D3D11/D3D12/Vulkan, up to 8x internal resolution, 16:9, SMAA/FXAA, CAS sharpening | findings "Post-process passes", "Menu jitter and blur" |
| Render-distance and root-LOD scaling (`src/kz_post.cpp`) | findings "Draw distance and LOD" |
| Boot movie audio (patch 0027), speed parity with the original | findings "FMV audio", "Speed parity with the original" |
| Archived design and performance notes | [`docs/archive/upstream/`](docs/archive/upstream/) |
