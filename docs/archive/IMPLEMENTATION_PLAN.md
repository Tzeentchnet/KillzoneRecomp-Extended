# KillzoneRecomp-Extended implementation plan

**Supersedes:** the workspace-level `plan.md` and `KillzoneRecomp_Master_PRD.md` (PRD-KZREC-2026-V3), kept at
`D:\Github\` for history only. Their requirements are merged into this file.
**Fork:** [Tzeentchnet/KillzoneRecomp-Extended](https://github.com/Tzeentchnet/KillzoneRecomp-Extended) (private)
**Upstream:** [drakolordx7/KillzoneRecomp](https://github.com/drakolordx7/KillzoneRecomp)
**Audited fork baseline:** `5f0700079f9ac4e14799d66d01747dbcdcec55f6`
**Supported game build:** Killzone NTSC-U, SCUS-97402
**Last revised:** 2026-10-20

This plan defines *what* each work package is, what it depends on, how it is accepted, and when it must stop. It
does not carry status. Status lives in two files:

- [`TODO.md`](TODO.md) - the durable queue of open work packages, in priority order.
- [`WORKDONE.md`](WORKDONE.md) - the ledger of verified completions, each with its acceptance evidence.

## 1. Document system

| Document | Role | Changes when |
|---|---|---|
| `IMPLEMENTATION_PLAN.md` (this file) | Work-package definitions, gates, milestones | Scope, dependencies or acceptance criteria change |
| [`TODO.md`](TODO.md) | Open queue: status, machine, next command, blockers | Every session that touches a task |
| [`WORKDONE.md`](WORKDONE.md) | Verified completions and their evidence | A task passes its acceptance commands |
| [`HANDOFF.md`](HANDOFF.md) | Session protocol: startup, rules, regression commands, latest session | Every session |
| [`docs/findings.md`](docs/findings.md) | Reverse-engineering and measurement log (cited from source comments) | New evidence is produced |
| [`patches/README.md`](patches/README.md) | Rationale and measurements for every PS2Recomp patch | A runtime patch changes |
| [`docs/archive/`](docs/archive/) | Superseded upstream design documents | Never; read-only history |

Rules:

1. A task moves from `TODO.md` to `WORKDONE.md` only when every acceptance command in this plan has passed on the
   machine the plan names. Compilation alone never completes a task whose acceptance requires execution.
   Cross-compilation or emulation never substitutes for a native AppleClang compile-and-run gate.
2. If this plan, `TODO.md`, and `HANDOFF.md` disagree, this plan wins for scope and acceptance, `WORKDONE.md` wins for
   what has been proven, and code plus passing commands override all prose. Fix the stale file in the same session.
3. A new task gets an ID and an entry in this plan (objective, dependencies, acceptance, stop condition) before it
   enters `TODO.md`.

## 2. Verified baseline and corrected assumptions

The fork inherits a working Windows x64 port: D3D11/D3D12/Vulkan rendering, high-refresh timing fixes, mouse and
keyboard aim, SDL gamepad input, texture replacement, render-distance and root-LOD scaling, and headless scripted
runs. These are baseline capabilities to preserve, not deliverables.

| Area | Repository-grounded correction |
|---|---|
| Thread model | Not three threads. Active contexts: SDL/event thread, EE/runtime thread, IOP worker, VIF1/VU1/GIF worker, GS/render thread, host audio callback. Every cross-thread feature defines ownership, lifetime, synchronization, shutdown and back-pressure. |
| Generated code | `20,745` was a folded generated-file count, not a function count. The current lock separates Ghidra functions, labels, translation units, generated functions and dispatch entries (`config/generated-manifest.json`). |
| LOD hooks | Render-distance and root-LOD hooks are in [`src/kz_post.cpp`](src/kz_post.cpp). New RenderZone and actor hooks build on those paths. |
| Addresses | Earlier RenderZone constants mixed vtable values with live object pointers. No address or structure offset is approved until it is in the GEN-01 manifest with a signature, bounds and runtime evidence. RenderZone bounds must never be padded cumulatively. |
| DMA buffers | Chains use dynamically growing pooled snapshots, not a fixed 3 MB buffer; a fixed 16 MB replacement is not a solution. Captures show asynchronous source overwrites, so unconditional zero-copy (Metal/UMA) is prohibited. |
| macOS | ARM64, AppleClang, Metal, W^X executable memory, packaging, signing and notarization are feasibility programs, not capabilities. |
| Temporal rendering | Depth conventions, motion vectors, dynamic-object identity, HUD classification, jitter and reactive masks do not yet form a renderer contract. Vendor upscalers wait for it. |
| Mod and Blender scope | New mod/VFS support is deferred. Blender tooling is owned by a separate project and does not gate this repository. Existing format research remains evidence, not an active importer, exporter or custom-level commitment here. |
| AI | AI structures and pathfinding hooks are hypotheses until read-only tracing proves them. |
| Legal | Dependency licenses, SDK redistribution, generated-code provenance, trademarks and protected-content exclusion require an explicit human-approved gate. |

### Product outcomes

Subject to the gates in this plan, the program targets:

1. A reproducible and relocatable Windows baseline.
2. A portable host boundary and a qualified native macOS ARM64 path.
3. Baseline-safe LOD and culling controls.
4. A backend-neutral temporal-rendering contract with native fallback.
5. Modern display, input and accessibility behavior.
6. Evidence-based AI and audio extension points.
7. Signed, auditable, BYOD-only release artifacts.

The release model is a native desktop application that uses a user-supplied disc image. The current host is Windows
10/11 x64 (MSVC or clang-cl, AVX2). Apple Silicon macOS is a candidate host, subject to Phase 0.

New mod support, including VFS overlays and loose-asset replacement, is deferred. The inherited texture-replacement
path remains a baseline capability to preserve, but expanding mod behavior is not an active deliverable. Blender
add-ons and integration are owned by a separate project; their schedule and acceptance do not gate this repository.

### Claims that are not approved

Do not make these claims in code, documentation, the launcher or release notes until their gates pass:

- Native macOS support before Phase 0 passes.
- Unconditional zero-copy guest or DMA memory.
- DLSS, MetalFX, XeSS, FSR, HDR or frame generation before their contracts and licenses are qualified.
- New mod-loading, archive-overlay, loose-asset replacement, Blender or custom-level support; those capabilities
  are deferred or external to this project.
- Campaign Recast/Detour pathfinding.
- "True HDR" when only SDR remapping is implemented.
- Steam Deck support, unless Linux becomes an explicit target.

## 3. Operating principles

1. One bounded objective and one isolated worktree per writing agent.
2. State prerequisites, owned files, deliverables, and acceptance commands.
3. Define a stop or escalation condition instead of speculative fallback code.
4. Require explicit BYOD authorization for any task that reads the disc image, extracted executable, generated code,
   or game assets.
5. Preserve baseline behavior whenever a new feature is disabled.
6. Keep ISO-free and BYOD test suites separate; prefer synthetic, protected-content-free fixtures.
7. Do not begin a conditional implementation until its research gate passes.
8. Record exact dependency, compiler, SDK, executable and test revisions.
9. Surface unsupported states explicitly; never return success-shaped defaults.

No retail executable, extracted asset, generated game-derived source, disc image, private function map, or
proprietary SDK binary may be committed or distributed.

## 4. Phase 0 - feasibility and reproducibility

Phase 0 is the critical path. Post-gate lanes stay blocked until the Phase 0 integration review passes.

### Wave 1 - source and compliance

#### BASE-01 - Canonical source and dependency lock

- **Objective:** One machine-verifiable source of truth for every dependency.
- **Work:** Reconcile the PS2Recomp gitlink with the tracked patch series. Pin PS2Recomp, PCSX2, SDL3, ImGui, FFmpeg,
  the PCSX2 Windows dependency bundle and directly fetched build dependencies, with license, source URL, revision,
  checksum, acquisition method and redistribution status. Provide an offline strict verifier.
- **Acceptance:** `python .\tools\scripts\verify_deps.py --strict`; `git submodule status --recursive`
- **Stop:** Escalate if the gitlink and patch series cannot be reconciled without changing runtime behavior.

#### LEGAL-01 - Distribution and provenance inventory `[HUMAN]`

- **Objective:** Written disposition of everything that governs distribution.
- **Work:** Record project and dependency licenses; source-offer and notice obligations; treatment of generated
  game-derived output; trademark and naming rules; proprietary upscaler SDK acquisition and redistribution rules;
  protected-content and secret scans for source and release artifacts.
- **Acceptance:** Written human disposition. Tool output alone cannot pass this gate.

### Wave 2 - relocatable baseline and tests

#### BASE-02 - Relocatable tooling

- **Depends on:** BASE-01
- **Objective:** Remove machine-specific paths without changing runtime behavior. Parameterize repository,
  generated-code, dependency, output and package paths; require an explicit argument or environment variable for BYOD
  inputs.
- **Acceptance:** `git grep -n -E 'D:\\KillzoneRecomp|C:\\Users\\drakolord' -- config tools README.md CMakeLists.txt`
  returns no matches.
- **Stop:** Never silently infer an ISO path or search outside the workspace.

#### BASE-03 - Reproducible CMake presets and preflight

- **Depends on:** BASE-01, BASE-02
- **Objective:** Configure reproducibly from a clean checkout in any path, with checked MSVC and clang-cl presets, a
  VS Code task workflow, dependency and toolchain preflight, and specific guidance for each missing manual input.
- **Acceptance:** `cmake --preset win-msvc-rel`; `cmake --preset win-clang-rel`
- **Stop:** Do not download or guess unpinned dependencies during configure.

#### TEST-01 - Hermetic ISO-free test layer

- **Depends on:** BASE-03
- **Objective:** CTest registration with `iso-free` and `byod` labels and a hermetic config override for `--selftest`.
- **Acceptance:** `ctest --preset win-msvc-rel -N`; `ctest --preset win-msvc-rel --output-on-failure -L iso-free`
- **Stop:** An ISO-free test must fail rather than open an ISO from a colocated or user-global configuration.

#### TEST-02 - Renderer parity oracle

- **Depends on:** TEST-01
- **Objective:** Deterministic synthetic golden tests for D3D11, D3D12 and Vulkan.
- **Acceptance:** `ctest --preset win-msvc-rel --output-on-failure -R '^kzgs_(d3d11|d3d12|vulkan)_golden$'`
- **Stop:** Resolve nondeterminism before the corpus qualifies Metal or temporal changes.

#### GEN-01 - Generated-code and hook manifest `[BYOD]`

- **Depends on:** BASE-03; explicit BYOD authorization
- **Objective:** Exact executable and hook provenance: supported executable hashes; separate counts for Ghidra
  functions, labels, generated translation units and dispatch entries; signature and address assertions for every
  installed hook.
- **Acceptance:** `python .\tools\scripts\verify_resume_table.py .\generated`;
  `python .\tools\scripts\inventory_generated.py --check`
- **Stop:** Reject hard-coded hooks that cannot be uniquely resolved and bounds-validated.

### Wave 3 - platform viability

#### PORT-01 - Host platform boundary

- **Depends on:** TEST-01
- **Objective:** Renderer-free `kz_host_core` owning paths and memory mapping, timing and synchronization,
  executable-memory protection and cache invalidation, window/native handles, crash reporting, and compiler
  intrinsics/SIMD selection.
- **Acceptance:** `cmake --build --preset win-clang-rel --target kz_host_core`
- **Stop:** Surface unsupported semantics explicitly; do not imitate Win32 with silent defaults.

#### ARM-01 - ARM64 generated guest-code smoke gate `[macOS]`

- **Depends on:** GEN-01, PORT-01
- **Objective:** Prove representative guest code and runtime calling conventions under AppleClang ARM64: control flow,
  guest memory, floating point, dispatch, scheduler transitions and generated helpers. Create the
  `macos-arm64-guest-smoke` workflow preset (it does not exist yet). Keep x86 microVU and Windows renderers out.
- **Acceptance (native Apple Silicon):** `cmake --workflow --preset macos-arm64-guest-smoke`
- **Stop:** At the first unexplained ABI, pointer-width, alignment, atomics or floating-point mismatch, record a
  minimal reproducer and re-scope before product work.

#### VU-01 - Portable VU interpreter `[macOS]`

- **Depends on:** PORT-01, TEST-01
- **Objective:** An ARM64-compatible interpreter reference that never compiles or initializes the x86 microVU JIT,
  with deterministic conformance coverage of arithmetic, flags, branches, stalls, memory, XGKICK,
  invalidation-visible state and lifecycle. The existing Windows microVU path and tests are preserved.
- **Acceptance (native Apple Silicon):**
  `cmake --preset macos-arm64`; `cmake --build --preset macos-arm64`;
  `ctest --preset macos-arm64 --output-on-failure -R '^kzvu_interpreter_'`
- **Stop:** Stop if the portable path depends on x86 register layout, emitters or implicit host floating-point state.
  The interpreter must be correct before it is used as a JIT oracle.

#### VU-02 - Bounded AArch64 JIT spike `[macOS]`

- **Depends on:** VU-01
- **Objective:** Execute a bounded microprogram corpus with valid macOS W^X transitions: never writable and executable
  at once, instruction-cache invalidation after publication, lifecycle/invalidation/reuse/teardown tests, and
  differential comparison of every result against VU-01.
- **Acceptance (native Apple Silicon):** `ctest --preset macos-arm64 --output-on-failure -R '^kzvu_a64_jit_smoke$'`
- **Stop:** Keep the interpreter fallback and defer the JIT if correctness, source compatibility, hardened-runtime
  behavior or entitlements fail.

#### MAC-01 - macOS host smoke tests `[macOS]`

- **Depends on:** PORT-01
- **Objective:** Prove SDL window, events, timing, paths and audio without game code or a renderer, using explicit
  Darwin implementations of the PORT-01 services and Cocoa native-window extraction that keeps Objective-C types out
  of platform-neutral headers.
- **Acceptance (native Apple Silicon, real CoreAudio):**
  `ctest --preset macos-arm64 --output-on-failure -R '^kz_host_(window|audio)_smoke$'`
- **Stop:** On an unexplained main-thread, Cocoa ownership, event-loop or audio lifecycle violation.

#### GS-01 - Minimal Metal parity gate `[macOS]`

- **Depends on:** MAC-01, TEST-02
- **Objective:** Wire a minimal Metal backend through the real `kzgs` API and render the synthetic golden corpus;
  resize, minimize, device loss and shutdown must be correct.
- **Acceptance (native Apple Silicon):** `ctest --preset macos-arm64 --output-on-failure -R '^kzgs_metal_golden$'`
- **Stop:** No MetalFX, HDR or temporal work until parity is measurable.

#### P0-EXIT - Phase 0 integration and exit review `[SERIAL]`

- **Depends on:** ARM-01, VU-01, VU-02 (or an explicit interpreter-fallback decision), MAC-01, GS-01, WIN-04, LEGAL-01
- **Objective:** One integration owner merges the macOS branches, resolves shared CMake/preset edits, runs every macOS
  gate, reruns the Windows regression set, replaces estimates with measured results, and records the JIT decision.
- **Acceptance:** Every Phase 0 exit criterion below is met and recorded in `WORKDONE.md`.

### Phase 0 exit criteria

- Repository, dependencies, toolchains, supported game build and SDKs are pinned.
- A clean Windows checkout configures and runs ISO-free tests from a non-developer path.
- The current Windows baseline is reproduced (WIN-04).
- Hook addresses and structure assumptions are evidenced.
- A deterministic renderer parity corpus exists.
- Representative generated code compiles and runs on ARM64.
- The portable VU interpreter passes its conformance corpus natively.
- The AArch64 JIT has a viable W^X path or an explicit interpreter-fallback decision.
- A minimal Metal backend passes renderer parity.
- Legal and distribution evidence has human disposition.
- Estimates are revised from measured results.

## 5. Windows baseline maintenance lane

These packages keep the shipped Windows port healthy. They may run in parallel with Phase 0 because they do not
change Phase 0 deliverables, but each must keep the Windows regression set green.

#### WIN-01 - D3D11 black output on NVIDIA

- **Objective:** Find why D3D11 presents an all-black image on the NVIDIA GeForce RTX 5070 (driver `32.0.16.1742`)
  while D3D12 renders the same GS stream correctly.
- **Acceptance:** A D3D11 BYOD capture of the boot movie and main menu is nonblack and matches D3D12 within the
  golden-test tolerance; `kzgs_d3d11_golden` still passes.
- **Stop:** If the cause is a driver defect with no safe workaround, record a minimal reproducer and keep D3D12 as the
  documented default for affected hardware.

#### WIN-02 - Unlock All Campaign Levels launcher option

- **Objective:** Opt-in launcher setting, default off, that makes every campaign level selectable.
- **Work:** First identify the game's canonical campaign-unlock state and decide whether the option overrides
  progression in memory, updates the active profile, or both. Any new guest address goes through the GEN-01 manifest.
- **Acceptance:** Tests cover disabled, enabled and existing-save behavior; with the option off, saves and progression
  are byte-identical to baseline.
- **Stop:** If the unlock state cannot be identified without writing to the user's memory-card profile, stop and ask.
- **Research result (2026-10-17):** the canonical state is the PlayerProfile per-level record; the menus read it only
  through three script methods (`IsLevelUnlocked`, `GetMaxDifficulty`, `IsAnimaticUnlocked`), which also implement the
  developers' "Shooterman" profile cheat. The option replaces those three queries in memory and never writes the
  profile (see `docs/findings.md` "Testing options").

#### WIN-05 - Testing start-up options: skip intro, start in a chapter, test memory card

- **Objective:** Opt-in launcher settings, default off, that shorten test sessions: skip the logos and intro cinematic,
  drive the front end into a chosen campaign chapter and difficulty, and use a separate test memory card.
- **Work:** Use the game's own front-end flow (no direct level loading): an autopilot keyed on the script's screen
  switches, a profile named `Tester` when the card has none, and the level file the script sets as the check. Every
  guest address goes through the GEN-01 manifest.
- **Acceptance:** With the options off no hook is installed and behavior is the baseline; scripted headless runs reach
  gameplay in a late chapter from an empty and from a populated card; the test card leaves the real card untouched; a
  repeated late-level load shows no crash or corruption beyond the measured baseline rate.
- **Stop:** If skipping part of the intro changes later game behavior (crashes, corruption) beyond the baseline, keep
  that part off by default and record the evidence rather than adding workarounds.
- **Evidence update (2026-10-20):** the full Intro path also reproduced WIN-06. Native movie skips after 2 or 30
  seconds and the original shortcut do not establish a higher failure rate than the current full-intro baseline, so
  the shortcut is not the common root cause.

#### WIN-06 - Chapter 11 load crash after a skipped intro cinematic

- **Objective:** Find why repeated chapter 11 ("Hope", `11.OTS`) loads crash the host process or corrupt textures in
  about one fifth of runs, and fix the underlying emulation fault rather than the downstream allocator or renderer
  symptoms. The testing shortcut made the workload practical but is not the common cause.
- **Evidence so far:** `docs/findings.md` sections "Testing options" and "WIN-06".
- **Acceptance:** A repeated chapter 11 load with the intro skipped matches the played-intro baseline (no crash, no
  texture or HUD corruption) over at least ten runs.
- **Stop:** If the fault lies in accepted MPEG/IPU or audio behavior (see `WORKDONE.md`), stop and agree a scoped
  change first.

#### WIN-03 - PS2Recomp standalone test-suite failures

- **Objective:** The broad standalone PS2Recomp test executable fails at `sceMpegDemuxPssRing` and later exits with an
  access violation. Determine whether these failures are pre-existing upstream, and fix or explicitly quarantine them.
- **Acceptance:** The standalone suite runs to completion; any quarantined case is named with its reason in
  `patches/README.md`.
- **Stop:** Do not change accepted MPEG/audio behavior (see `WORKDONE.md`) to make a test pass.

#### WIN-04 - Re-verify the Windows gameplay baseline

- **Objective:** Since the fork regenerated the function map and restored boot dispatch, prove the inherited gameplay
  baseline again: profile creation, first-mission gameplay, save/load, quit to front end, high-refresh timing and
  mouse aim, on D3D12 with the current build.
- **Acceptance:** A scripted headless run (`tools\scripts\run_headless.ps1`) reaches first-mission gameplay with
  periodic nonblack screenshots, plus a recorded hands-on play-test note.
- **Stop:** A regression against upstream behavior becomes its own scoped task with a reproducer.

## 6. Post-gate implementation lanes

Do not begin these before P0-EXIT, except for read-only research marked `[READ-PARALLEL]`. Within a lane, numbered
tasks are serial. The first post-gate priority group is TEMP-01 and LOD-01/LOD-02.

### LOD and culling

1. **LOD-01:** Pure tests for existing render-distance and root-LOD transformations; verify their hook addresses.
2. **LOD-02:** Trace frustum culling, RenderZone behavior, actor updates, scope behavior, mip selection and VIF-chain
   high-water at baseline and elevated settings.
3. **LOD-03:** Checked chain-pool telemetry and deterministic exhaustion behavior.
4. **LOD-04:** Idempotent, bounds-checked RenderZone control using temporary bounds or restoration.
5. **LOD-05:** Validated mesh-LOD scaling with a named cinematic mode, not a `999.0f` sentinel.
6. **LOD-06:** Actor update-distance control, only if mission, streaming and lifecycle coupling can be isolated.
7. **LOD-07:** Qualify each control independently: default parity, memory limits, frame-time percentiles, campaign.

Validation: `ctest --preset win-msvc-rel --output-on-failure -R '^kz_(post|hook_manifest)_'`;
`ctest --preset win-msvc-rel --output-on-failure -R '^byod_lod_capture$' -L byod`

### Deferred: VFS and mod loading

New mod support is not part of the current milestones or release gate. Do not start VFS-01 through VFS-06. The
inherited texture-replacement path remains baseline behavior and must not regress, but it is not authorization to
expand mod loading.

If product scope later reactivates mod support, revise this plan and `TODO.md` before scheduling this deferred
sequence:

1. **VFS-01:** Trace the FilePS2 RPC state machine: service ID, function, path, counters, result, timing, callbacks,
   cancellation, thread.
2. **VFS-02:** Resolve archive offset units, hierarchy, alignment, compression and unknown fields.
3. **VFS-03:** Bounds-checked read-only parser with synthetic fixtures.
4. **VFS-04:** Deterministic overlay resolution, manifests, load order, case rules and path normalization as an
   ISO-free library.
5. **VFS-05:** Fulfill one harmless BYOD request while preserving asynchronous counters, errors, callbacks,
   cancellation and archive fallback. Instant completion that changes observable RPC behavior is not acceptable.
6. **VFS-06:** Traversal, symlink, case-collision, oversized-file, concurrent-read, fault-injection and soak tests.

Reactivation validation (not currently active): `-R '^byod_fileps2_trace$' -L byod`, `-R '^kz_archive_'`,
`-R '^kz_vfs_'`, `-R '^byod_vfs_overlay$' -L byod`

### Temporal rendering and upscaling

1. **TEMP-01:** Backend-neutral frame IDs, pass capture and a replayable native fallback.
2. **TEMP-02:** Prove color-space and depth conventions with synthetic geometry.
3. **TEMP-03:** BYOD capture of dynamic batches, HUD draws and current/previous transform candidates; go/no-go report.
4. **TEMP-04:** History lifecycle and reset rules: cuts, teleports, pause, resize, resolution change, save/load,
   device loss.
5. **TEMP-05:** Static motion vectors; zero motion for a static camera and analytical camera movement.
6. **TEMP-06:** Dynamic motion vectors for one evidenced actor/skinning path, with conservative reactive rejection.
7. **TEMP-07:** Evidence-based reactive/transparency classification with debug visualization.
8. **TEMP-08:** Isolate only proven HUD draws while preserving GS ordering.
9. **TEMP-09:** Common temporal interface: per-SDK coordinates, exposure, resources, reset, masks, fallback.
10. **TEMP-10:** One vendor-neutral temporal upscaler first.
11. **TEMP-11:** MetalFX, DLSS/Streamline or XeSS only as independently licensed and qualified adapters.

Validation: `-R '^kzgs_temporal_contract$'`, `-R '^byod_temporal_capture$' -L byod`, `-R '^kzgs_temporal_sequence$'`.
Frame generation is out of scope until a separate latency, scheduling, optical-flow, input-sampling and presentation
design is approved.

### HDR and display

1. Establish linear-color behavior independently of upscaling.
2. Preserve baseline SDR within an approved image tolerance.
3. Explicit SDR-to-HDR/EDR remapping: paper white, peak brightness, black level, metadata, safe fallback. Call it
   remapped HDR presentation unless scene-referred source semantics are proven.
4. Discover projection and HUD anchors at 21:9 and 32:9 before Hor+ or HUD repositioning.
5. Qualify 16:9, 21:9 and 32:9 geometry, menus, FMV, scopes and HUD.

### Deferred assets and external Blender tooling

Asset parser, serializer and export work whose purpose is mod support is deferred with the VFS lane. Blender add-ons,
Blender-version support and Blender import/export integration are owned by a separate project. They are not work
packages, milestones, acceptance criteria or release blockers here. Existing format research in
`docs/assetforms.md` remains reference evidence for this runtime.

Any future integration is limited to an explicitly versioned boundary approved after mod support is reactivated. The
external project must not become a build or release dependency without revising this plan.

#### ASSET-01 - Retail asset storage and build-pipeline research `[BYOD]` `[READ-PARALLEL]`

- **Objective:** Document how the supported retail build stores, groups and consumes assets, and distinguish evidenced
  source/authoring stages from shipped runtime formats so the external Blender project has a reliable interoperability
  reference.
- **Work:** Read-only disc inventory, DAT/Core/CoreStream relationship mapping, executable and runtime-consumer
  tracing, companion-file analysis, and build/tool metadata research. Record verified facts, bounded inferences and
  unknowns in `docs/assetforms.md`. This task does not implement parsers, exporters, VFS overlays or runtime changes.
- **Acceptance:** `docs/assetforms.md` contains a cited storage/build-pipeline synthesis, reproducible non-content
  metadata where practical, importer implications, and an explicit remaining-unknowns list; `git diff --check`.
- **Stop:** Do not copy protected content into source or reports, infer source formats from runtime layouts without
  evidence, or add runtime instrumentation under this task. Any required runtime change becomes a separately approved
  work package and must pass the Windows regression set plus a BYOD game smoke run.

#### ASSET-02 - Skinned geometry and animation decoder review `[BYOD]` `[READ-PARALLEL]`

- **Objective:** Re-derive the skinned-mesh, Skeleton/BoneMap, packed-animation and pose-sampling paths from the
  supported executable and correct the external Blender project's interoperability reference.
- **Work:** Verify CoreStream ownership bases, nested packet directories, VIF/VU layout, strip controls, matrix
  palette construction, name binding, packed-track boundaries, codec dispatch and interpolation. Distinguish strict
  serialized-format validation from runtime fallback behavior and retain corpus-only claims as such.
- **Acceptance:** `docs/assetforms.md` documents the verified decoder behavior, selector domains, bounds and remaining
  unknowns; known timing, scatter and control-bit formulas match executable control/data flow; `git diff --check`.
- **Stop:** Do not claim source topology or authoring semantics from runtime packets, accept unsupported codec selectors
  as valid serialization, or change runtime code under this task. Any runtime implementation must pass the Windows
  regression set and a BYOD animated-gameplay smoke run.

#### ASSET-03 - Retail animation playback contract `[BYOD]` `[READ-PARALLEL]`

- **Objective:** Define and validate the complete path required to decode retail animation clips and play them on the
  corresponding rigged meshes in the external Blender project.
- **Work:** Inventory animation-bearing Cores and streams from the authorized disc; resolve resource, Skeleton,
  BoneMap and clip linkage; measure codec-selector use; decode representative and edge-case tracks; verify rest/bind
  pose, local-to-world composition, timing, looping and interpolation; and specify Blender armature, basis, action and
  sampling behavior. Temporary analysis may read protected content but must retain only non-content metadata.
- **Acceptance:** `docs/assetforms.md` contains an evidence-labeled end-to-end playback contract, retail selector and
  linkage measurements, validated codec/bounds rules, Blender conversion rules, and explicit unresolved cases;
  `git diff --check`.
- **Stop:** Do not commit extracted assets or decoded retail animation data, modify the external Blender project under
  this research task, or invent missing source/DCC semantics. Any required runtime instrumentation becomes a separate
  work package and must pass the Windows regression set plus a BYOD animated-gameplay smoke run.

### Input and accessibility

1. Version and test the existing INI schema before adding sections.
2. Resolve the parsed but unused `RawMouse` setting.
3. Pure tests for hip, ADS and focal-length/monitor-distance sensitivity math, preserving current defaults.
4. SDL gyro and device/glyph-state tests.
5. Adaptive triggers, advanced haptics, lightbar and Bluetooth as separate platform-qualified features.
6. Head-bob, sway and shake controls one at a time with baseline defaults and gameplay-regression checks.
7. No Steam Deck claims unless Linux becomes an explicit target.

### AI and navigation

1. Resolve real perception, state, path, waypoint and cover symbols; no placeholder names such as
   `FUN_PathfindQuery`.
2. Trace one reversible AI scalar and prove its isolated effect.
3. Versioned tuning schema exposing only proven fields, units and ranges.
4. Waypoint/cover visualization before editing; preserve unknown and mission-critical records.

Recast/Detour and custom-level navigation are out of scope while mod support is deferred.

Validation: `python .\tools\scripts\verify_symbol_manifest.py .\config\killzone-symbols.toml --group ai`;
`-R '^byod_ai_scalar$' -L byod`

### Audio

1. Determine whether SPU2 voices can be stably associated with world emitters, categories, lifetimes and listener
   transforms; keep stereo for ambiguous voices.
2. One legally approved HRTF backend only after emitter association is demonstrated.
3. Latency, resampling, channel, device-change and double-spatialization criteria.
4. Apple head tracking only through stable public APIs with capability, privacy, disconnect and fallback tests.

Loose audio replacement is deferred with mod support.

The accepted FMV audio policy (four ADPCM banks, two proactive MPEG refills per picture, 40 ms host reserve) is a
baseline; changing it requires a reproducible timestamped regression.

### Release and compliance (continuous)

Clean-machine build and installation tests; dependency notices and source-offer obligations; SBOM; protected-content
and secret scanning; hardware and renderer matrices; frame-time percentiles, memory high-water, latency,
shader-cache and thermal measurements; campaign, checkpoint, save/load, cutscene, FMV, shutdown and soak tests; macOS
signing, hardened runtime, entitlements, notarization and DMG dry runs; BYOD, supported-disc, safe-mode,
diagnostics and uninstall documentation.

## 7. Milestones

| Milestone | Exit criteria |
|---|---|
| **M0 Feasibility approved** | Phase 0 passes; every speculative feature is retained, deferred or removed; legal and distribution decisions are recorded; measured estimates replace the original calendar. |
| **M1 Portable core** | Windows x64 and native macOS ARM64 build in CI; the supported disc reaches the same approved checkpoint on both; JIT differential and W^X tests pass or the interpreter fallback is explicit; Metal golden scenes pass; typed configuration and platform defaults work; bundle, signing and notarization dry runs pass. |
| **M2 LOD controls** | Each control independently toggleable with baseline-identical defaults; RenderZone changes idempotent and bounds-checked; no guest or host overflow; missions, saves, streaming and actor lifecycle correct; memory and frame-time budgets pass on the approved hardware matrix. |
| **M3 Modern rendering** | Color, depth, motion, mask, UI and history contracts have tests and debug views; one approved upscaler plus native fallback per platform; every temporal reset case correct; HUD stable; HDR claims match source semantics; quality, stutter, memory and percentile budgets pass; no frame generation unless separately approved. |
| **M4 Ergonomics** | 16:9, 21:9 and 32:9 matrices pass; mouse input frame-rate independent; controllers reconnect without leaving active effects; gyro and DualSense claims match the tested matrix; accessibility settings preserve gameplay. |
| **M5 AI, audio, release candidate** | AI tuning is schema-validated, evidence-based and baseline-safe; spatial audio has verified emitter mapping and fallback; head tracking uses public APIs; signed artifacts, SBOM, notices, guides, campaign regression, soak and legal checks pass; no protected or unlicensed content is distributed. |

## 8. Configuration requirements

The existing `killzone.ini` configuration is the baseline. New settings must:

- Use a versioned, typed schema, validated with explicit ranges and enum values.
- Preserve baseline behavior by default.
- Be ignored only with a visible diagnostic; malformed values are never silently accepted.
- Be exposed only when the corresponding feature gate passes.
- Record platform-specific defaults separately from shared behavior.

Candidate groups are `Portability`, `LOD`, `Temporal`, `Display`, `Input`, `Accessibility` and `Audio`.
Listing a group here does not mean its keys are implemented.

## 9. Performance and evidence requirements

No fixed frame-time or memory budget is accepted without measurement on an approved hardware matrix. For every
material change, record frame time p50/p95/p99/max, CPU time per execution context, GPU time per backend and pass,
host and guest memory high-water, DMA-chain pool high-water and exhaustion behavior, shader-cache and startup
stutter, input and audio latency where relevant, and long-run stability and thermals. Correctness gates take
precedence over throughput: no trade of determinism, guest semantics, memory safety, shutdown correctness or
baseline parity for average frame rate.

Test suites:

- **ISO-free:** synthetic or otherwise redistributable fixtures only. It must never discover retail content from the
  repository parent, user profile, registry, global configuration or a previous build directory.
- **BYOD:** explicit authorization and an explicit content path. Logs and artifacts avoid protected content unless
  kept locally for the authorized test and excluded from source control and release packages.

Each completed work package retains, in `WORKDONE.md`: source and dependency revisions, commands and environment,
pass/fail output, measured baselines and comparisons, and any stop decision or deferred risk.

## 10. Risk register

| Risk | Required mitigation |
|---|---|
| Dependency or patch drift | Immutable lock, patch hashes, tree reconciliation, strict preflight |
| Protected content enters source or release | BYOD separation, ignore rules, package allowlists, scans |
| Incorrect hook address or layout | Executable-hash manifest, signatures, bounds checks, fail-closed installation |
| DMA source lifetime violation | Snapshot ownership proof, overwrite telemetry, no unconditional zero-copy |
| LOD memory or scheduling regression | Pool telemetry, deterministic exhaustion, independent controls, campaign regression |
| Temporal ghosting or instability | Proven depth/motion/UI/mask contract, reset tests, debug views, native fallback |
| macOS JIT incompatibility | Interpreter oracle, W^X lifecycle tests, cache invalidation, explicit fallback |
| Campaign AI breakage | Proven symbols and schemas; baseline-safe tuning, visualization before mutation and no custom-navigation integration |
| License or SDK redistribution violation | Human legal gate, SBOM, notices, source obligations, artifact audit |
| Deferred or external scope leaks into a release | Keep mod/VFS work out of the active queue and release gate; keep Blender tooling non-blocking |

## 11. Release gate

A release candidate requires:

- Clean-machine build and installation tests.
- Supported hardware, renderer, controller and display matrices.
- Campaign, checkpoint, save/load, cutscene, FMV, shutdown and soak tests.
- Dependency notices, source-offer obligations and an SBOM.
- Secret and protected-content scans.
- macOS signing, hardened runtime, entitlements, notarization and DMG dry runs when macOS is included.
- BYOD, supported-disc, safe-mode, diagnostics and uninstall guides.
- Human approval of legal, trademark, provenance and distribution evidence (LEGAL-01).

## 12. Approval status

This plan is not a substitute for human sign-off. All earlier blanket "Approved" entries are withdrawn. Record a
sign-off here with the date and the person approving.

| Role | Status |
|---|---|
| Product owner | Pending |
| Windows/platform lead | Pending |
| macOS/Metal lead | Pending Phase 0 evidence |
| Graphics lead | Pending renderer-contract evidence |
| Audio/input lead | Pending platform matrices |
| Legal/distribution owner | Approved 2026-10-20 by repository owner (LEGAL-01) |
