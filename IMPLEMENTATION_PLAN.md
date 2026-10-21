# KillzoneRecomp-Extended implementation plan

**Focus:** Windows graphical improvements and configurable gameplay tuning
**Fork:** [Tzeentchnet/KillzoneRecomp-Extended](https://github.com/Tzeentchnet/KillzoneRecomp-Extended) (private)
**Upstream:** [drakolordx7/KillzoneRecomp](https://github.com/drakolordx7/KillzoneRecomp)
**Supported game build:** Killzone NTSC-U, SCUS-97402
**Primary platform:** Windows 10/11 x64
**Primary renderer:** Direct3D 12
**Last revised:** 2026-10-20

This plan supersedes the earlier portability-first roadmap. Work already completed under that roadmap remains valid
evidence in [`WORKDONE.md`](WORKDONE.md), but macOS, Metal, new mod loading, Blender integration, AI/navigation
expansion and spatial-audio work are no longer release blockers. The active product direction is:

1. Improve image quality and display behavior on the working Windows port.
2. Establish whether a supported DLSS 5 integration is technically viable.
3. Add measured, reversible player-weapon tuning through the existing `killzone.ini`.
4. Make the game's exact `Shooterman` developer profile the default.
5. Preserve the original game and the current PC-port configuration as explicit fallback baselines.

## 1. Document system and authority

| Document | Role |
|---|---|
| `IMPLEMENTATION_PLAN.md` (this file) | Scope, dependencies, work packages, acceptance criteria and release gates |
| [`TODO.md`](TODO.md) | Current queue, status, machine, blocker and next command |
| [`WORKDONE.md`](WORKDONE.md) | Immutable ledger of verified completions and their evidence |
| [`HANDOFF.md`](HANDOFF.md) | Session procedure, regression commands and current handoff |
| [`docs/findings.md`](docs/findings.md) | Reverse-engineering facts, captures and measurements |
| [`patches/README.md`](patches/README.md) | Rationale and evidence for PS2Recomp/runtime patches |

Rules:

1. This plan defines scope. `TODO.md` defines current status. `WORKDONE.md` defines what has actually passed.
2. A task is complete only after every acceptance item named here has passed on its required hardware.
3. Compilation is not runtime acceptance.
4. Retail-content tests are BYOD-only and require an explicit ISO or extracted executable path.
5. A new hook address or guest layout must be added to the generated manifest with signature and bounds checks.
6. A feature disabled in `killzone.ini` must leave the measured baseline unchanged.
7. Unsupported states and malformed settings produce visible diagnostics; they do not silently select a different
   feature or return a success-shaped fallback.

## 2. Product decisions

### 2.1 Windows and renderer priority

- Windows x64 is the only active release platform.
- Direct3D 12 is the reference renderer because it renders correctly on the current NVIDIA test machine.
- D3D11 and Vulkan remain compatibility backends. They must not regress, but feature parity on them does not block
  a D3D12-first graphics milestone unless the release matrix explicitly includes that feature.
- The known D3D11 black-output defect remains a compatibility issue, not a reason to hold D3D12 improvements.

### 2.2 DLSS 5 scope

DLSS 5 is a qualification target, not an approved implementation commitment. The active plan ends with a documented
go/no-go decision based on the current public SDK, D3D12 compatibility, required inputs, supported hardware
and measured prototype behavior.

### 2.3 `Shooterman` profile scope

The game recognizes the case-sensitive profile name `Shooterman`. Current evidence shows that the name changes only
three front-end profile queries: level unlock, maximum difficulty and animatic unlock. It is not evidence of changed
recoil, accuracy, AI, damage or other gameplay behavior.

The launcher and documentation may label this the **SHOOTERMAN developer test profile**, but the on-card name must be
exactly `Shooterman`.

- It is the default name only when an isolated testing workflow creates a profile on an empty test memory card.
- It never renames, replaces or silently selects a player's existing profile.
- It never writes to the normal memory card when `Testing.TestMemoryCard=1`.
- It is independent of all `[Gameplay]` tuning.

No prebuilt retail save or memory-card image is distributed. Profile creation continues through the game's own
front-end flow.

### 2.4 Gameplay-tuning scope

The first gameplay-tuning release is limited to player weapon handling:

- recoil impulse and recovery;
- shot spread/dispersion and its recovery;
- hip-fire, aim-down-sights, movement and stance modifiers only where the executable proves they are distinct;
- optional per-weapon overrides after stable weapon identities are known.
- aim assist tuning (or turn off)

It does not change enemy accuracy, AI, damage, health, ammunition, fire rate, animation timing, enemy aim assist or weapon
balance unless a later work package explicitly adds and qualifies that field.

## 3. Recorded `killzone.ini` baseline

The existing local `killzone.ini` is runtime state rather than a committed template. Its non-sensitive values are
recorded here as the comparison baseline. The ISO path is intentionally excluded.

| Section | Baseline |
|---|---|
| `Display` | `WindowMode=borderless`, `Width=0`, `Height=0`, `VSync=1`, `FpsLimit=0` |
| `Graphics` renderer/output | `Renderer=d3d12`, `Upscale=0`, `Aspect=16:9` |
| `Graphics` filtering/AA | `Anisotropy=16`, `FXAA=0`, `SMAA=1`, `SmaaThreshold=0.2`, `Bilinear=1` |
| `Graphics` presentation | `SharpScaling=1`, `Sharpen=30`, `TextureReplacement=1` |
| `Graphics` game effects | `NoiseFilter=0`, `MotionBlur=0`, `Glow=0`, `LensBlur=0` |
| `Graphics` scene range | `RenderDistance=1`, `LodScale=1` |
| `Input` | `MouseSensitivity=1`, `InvertY=0`, `RawMouse=1`, `AimMode=Hold`, `StickDeadzone=0.15` |
| `Audio` | `MasterVolume=100` |
| `Testing` | `UnlockAllLevels=0`, `SkipIntro=0`, `StartLevel=0`, `StartDifficulty=0`, `TestMemoryCard=0`, `DefaultProfile=Shooterman` |
| `Launcher` | `Show=1` |
| `Bindings` | Existing version 3 bindings and duplicate menu/game mappings are preserved |

Baseline rules:

1. Existing keys keep their current meaning, spelling, range and migration behavior unless a dedicated config task
   changes them.
2. Saving a new graphics or gameplay setting must not reset paths, bindings or unrelated values.
3. `Upscale=0` continues to mean the current automatic integer internal scale until GFX-02 defines a compatible
   render/output-scale model.
4. `RenderDistance=1`, `LodScale=1` and every new gameplay multiplier at `1.0` mean original behavior.
5. The D3D12 baseline must not claim SMAA is active while the backend ignores it. GFX-01 must either implement it or
   expose the backend limitation honestly.

## 4. Verified starting facts and open blockers

| Area | Starting point |
|---|---|
| Rendering | D3D11 and D3D12 render on the current NVIDIA machine. D3D11's historical all-zero output no longer reproduces on the pinned stack. |
| Image quality | Integer internal scaling up to 8x, anisotropy, FXAA, D3D11-only SMAA, sharp scaling, CAS sharpening and texture replacement already exist. |
| Scene range | Render-distance and root-LOD multipliers already exist in [`src/kz_post.cpp`](src/kz_post.cpp). Their safe limits and campaign effects are not fully qualified. |
| Temporal inputs | The renderer does not yet provide a proven color/depth/motion/exposure/reactive/UI/history contract suitable for DLSS. |
| Testing profile | PROFILE-01 passed: the isolated empty-card workflow creates exact `Shooterman` through the native UI, native chapter 11/Hard selection works without unlock hooks, and populated/normal card hashes remain unchanged. |
| Weapon tuning | No recoil or accuracy symbol, field or hook is currently approved. Existing references to weapon objects and spread UI are discovery leads only. |
| Stability | WIN-06, the intermittent chapter 11 crash/corruption, also reproduces after the full intro. The testing shortcut is not the common cause. |
| Audio | First-mission firefights can truncate effects and dialogue by roughly 100 ms several times per fight; AUDIO-01 tracks the baseline defect. |

WIN-06 remains a release blocker. Graphics capture and read-only weapon research may proceed in parallel, but no
campaign-wide quality or balance conclusion is valid while the repeated-load gate is unstable.

## 5. Configuration contract

### CFG-01 - Version and test the existing schema

- **Objective:** Extend `killzone.ini` without breaking or silently rewriting the baseline.
- **Work:**
  - Add a file-level schema version separate from `[Bindings] Version=3`.
  - Parse typed booleans, integers, floats and enums with explicit diagnostics.
  - Preserve all known unrelated values during launcher saves.
  - Define deterministic migration from files without the schema version.
  - Keep environment variables as automation overrides, not hidden persistent settings.
- **Acceptance:**
  - An exact baseline fixture loads and round-trips without changing effective values.
  - Missing, partial, old-version, unknown-key, duplicate-key and malformed-value fixtures have explicit tests.
  - A malformed gameplay or graphics value names its section, key and rejected value.
  - Paths and bindings survive every launcher edit made by the new feature tabs.
- **Stop:** Do not add gameplay or DLSS-facing keys to the launcher until this contract passes.

### Initial gameplay keys

WPN-01 must prove the executable semantics before finalizing the schema. The intended minimum interface is:

```ini
[Gameplay]
RecoilScale=1.0
SpreadScale=1.0
RecoveryScale=1.0
```

Semantics:

- `1.0` is original game behavior.
- `RecoilScale` scales only the evidenced player recoil contribution.
- `SpreadScale` scales angular dispersion; lower values are more accurate. The launcher may label this "Accuracy",
  but the file key must state the stored quantity and direction unambiguously.
- `RecoveryScale` scales the evidenced return/recovery rate; higher values recover faster.
- Values are finite, range-checked and boot-time only in the first implementation.
- AI and non-player weapons remain unchanged.
- Per-weapon sections are deferred until WPN-04 establishes stable, human-readable weapon IDs.

The testing profile is separate:

```ini
[Testing]
DefaultProfile=Shooterman
```

This key is consumed only by the isolated test-profile creation flow. It does not select gameplay tuning.

## 6. Shared validation and evidence

### 6.1 ISO-free gate

Every code-changing work package runs the smallest relevant tests plus the checked Windows regression set before
integration:

```powershell
python .\tools\scripts\verify_deps.py --strict
python .\tools\scripts\verify_resume_table.py .\generated
python .\tools\scripts\inventory_generated.py --check
tools\scripts\cmake_preset.ps1 -Preset win-msvc-tests -Action build
tools\scripts\cmake_preset.ps1 -Preset win-msvc-rel -Action test -Label iso-free
tools\scripts\cmake_preset.ps1 -Preset win-clang-tests -Action build
tools\scripts\cmake_preset.ps1 -Preset win-clang-rel -Action test -Label iso-free
git diff --check
```

New tests use redistributable synthetic inputs whenever possible. An ISO-free test must never discover retail
content through the repository parent, a user profile, the registry, a normal `killzone.ini` or an old build tree.

### 6.2 BYOD scenarios

Graphics and gameplay qualification use explicit local content paths and an isolated memory card. The minimum scene
set is:

1. logos and front end;
2. profile creation and campaign selection;
3. tutorial/first combat with HUD and weapon effects;
4. an outdoor fog/LOD scene;
5. rain or lens-overlay content;
6. a cutscene and FMV transition;
7. chapter 11 repeated loading;
8. save, load, return to front end and shutdown.

Captures retain only non-protected metrics in source control. Retail screenshots, traces and decoded content remain
local unless the repository owner separately approves their use.

### 6.3 Measurement rules

Record comparisons against the exact baseline INI and source revision:

- frame-time p50/p95/p99/max and GPU time;
- host and guest memory high-water;
- shader compilation and presentation stutter;
- output resolution, internal resolution and active post-process path;
- image deltas on deterministic captures;
- shot origin/direction, impact point, recoil impulse, recovery and stance/ADS/movement state;
- crash, corruption and hook-installation failures.

Do not select aesthetic defaults from one screenshot or one weapon. Keep raw measurements separate from product
judgment.

## 7. Work packages

Work inside each lane is serial unless dependencies say otherwise. The graphics and gameplay lanes may run in
parallel after BASE-01, CFG-01 and TEST-01.

### Foundation

#### BASE-01 - Lock the Windows enhancement baseline

- **Objective:** Make the recorded INI and D3D12 behavior reproducible before changing image quality or gameplay.
- **Work:** Complete the hands-on WIN-04 checks; capture the BYOD scene set; record the GPU, driver, display, compiler,
  source revision and effective INI; preserve the existing testing, input and texture-replacement behavior.
- **Acceptance:** Profile creation, first-mission gameplay, save/load, return to front end, high-refresh timing, mouse
  aim, controller input and clean shutdown pass with all testing options off.
- **Stop:** Any regression becomes a separately reproduced baseline defect before enhancement work relies on it.
- **Status (2026-10-20):** Complete on Windows. The hands-on D3D12 run passed profile/gameplay, Continue/load,
  front-end return, high-refresh mouse timing, controller input and exit-code-0 shutdown with all testing options off.
  VSync-on is the selected baseline after eliminating tearing seen with VSync off.

#### AUDIO-01 - Reproduce combat audio clipping

- **Depends on:** BASE-01
- **Objective:** Reproduce and localize short audio truncation during action-heavy gameplay.
- **Reproducer:** First mission, action-heavy firefights, speakers; gunfire/effects and dialogue end roughly 100 ms
  early several times per fight.
- **Acceptance:** A deterministic capture identifies whether the loss begins in guest/SPU2 production, the host
  callback queue or the output device, and the corrected path completes a combat soak without truncated events.
- **Stop:** Do not hide clipping with global buffering or latency increases without measuring event delivery and
  callback timing.

#### STAB-01 - Resolve WIN-06 repeated-load corruption

- **Objective:** Fix the underlying emulation/runtime fault that intermittently corrupts memory or crashes chapter 11.
- **Acceptance:** At least ten consecutive chapter 11 loads complete without host crash, texture corruption or HUD
  corruption using the standard gate and a known isolated card.
- **Stop:** Do not hide the failure by disabling content, swallowing an allocator error or excluding failed runs.
- **Status (2026-10-20):** Investigation deferred by project decision after multiple independent ten-run diagnostic
  arms. This does not waive acceptance; STAB-01 remains an M4 and release blocker.

#### TEST-01 - Graphics and weapon measurement harness

- **Depends on:** BASE-01
- **Objective:** Add deterministic capture and metric formats shared by both enhancement lanes.
- **Work:** Versioned metadata, frame IDs, effective config, renderer/backend identity, image capture points, guest-time
  shot events and protected-content-safe summaries.
- **Acceptance:** Repeated baseline runs can be aligned by frame/event and compared without relying on filenames or
  wall-clock timing alone.
- **Status (2026-10-20):** Complete. `capture.jsonl` schema 1 records effective renderer/testing configuration,
  guest-vsync frame IDs, capture points and fire-action transitions. The verifier produces path-free hash/count
  summaries and aligns repeated runs by event/frame IDs.

### Graphics lane

#### GFX-01 - D3D12 presentation and post-process parity

- **Depends on:** BASE-01, CFG-01, TEST-01
- **Objective:** Make the active D3D12 settings truthful and deterministic.
- **Work:**
  - Trace the final scene-to-swap-chain path.
  - Implement D3D12 SMAA parity or mark SMAA unavailable on D3D12 and choose an explicit supported AA path.
  - Verify FXAA, CAS, bilinear/sharp scaling, crop, resize, minimize, device loss and renderer restart.
  - Expose the effective, not merely requested, post-process chain in diagnostics.
- **Acceptance:** Synthetic golden tests and the BYOD scene set prove each toggle independently; disabled toggles are
  baseline-identical; no launcher control reports an effect that the selected backend ignores.
- **Stop:** Do not stack sharpening or AA passes whose order and color space are unknown.
- **Status (2026-10-20):** Complete. D3D12 labels SMAA unavailable and does not substitute FXAA. Runtime logs and
  capture metadata report the effective AA/scaling/CAS/crop chain. Deterministic final-back-buffer A/Bs and the
  D3D12 resize/minimize/device-loss/renderer-restart integration test pass.

#### GFX-02 - Separate internal render scale from output scale

- **Depends on:** GFX-01
- **Objective:** Replace the single ambiguous integer-upscale decision with an explicit render/output contract while
  retaining `Upscale=0` compatibility.
- **Work:** Define render resolution, output resolution, aspect/crop, scaling filter and post-process order; add
  capability reporting; preserve integer supersampling; permit controlled sub-output inputs only for experiments.
- **Acceptance:** 720p, 1080p, 1440p and 4K output matrices have exact effective-resolution diagnostics and stable
  resize/history behavior; legacy INIs produce their previous effective integer scale.
- **Stop:** No temporal upscaler work while render and output coordinates are ambiguous.
- **Status (2026-10-20):** Complete. `Upscale=0` retains the legacy whole-scale rule based on 448-line output
  coverage; fixed scales and output resize are independent. Runtime metadata reports the scale decision, declared
  pipeline and completed-frame render/output dimensions. Product runs at 720p, 1080p, 1440p and 4K exited 0 with
  exact outputs and effective 2x/3x/4x/5x Killzone render sizes; the hardware fixed-2x resize matrix also passes.

#### GFX-03 - Qualify render distance, LOD and texture clarity

- **Depends on:** BASE-01, TEST-01
- **Objective:** Turn existing graphics controls into safe, measured improvements.
- **Work:** Verify hook signatures; test `RenderDistance` and `LodScale` independently; measure RenderZone, fog,
  streaming, actor and DMA-chain behavior; qualify anisotropy, texture replacement and sharpening interactions.
- **Acceptance:** Each control at `1.0` is baseline-identical; approved elevated ranges pass the BYOD scene set and
  campaign checkpoints without cumulative bounds growth, missing actors, streaming failures or memory exhaustion.
- **Stop:** Reduce the supported range instead of adding sentinel values or unbounded allocations.
- **Status (2026-10-20):** In progress. Baseline distance/LOD installs no hooks; independent 2x/4x arms transform
  real far/fog/LOD values and exit cleanly. The original RenderZone post-call wrapper was invalid and has been
  replaced by manifest-validated consumer hooks. Same-arm live captures remain too variable for visual acceptance;
  deterministic scene/campaign and high-water qualification is still open.

#### GFX-04 - Temporal input feasibility contract

- **Depends on:** GFX-02
- **Objective:** Determine whether this renderer can supply trustworthy inputs for modern temporal reconstruction.
- **Work:**
  - Prove scene color format and color space.
  - Identify usable depth and its convention.
  - Identify camera transforms and jitter control.
  - Measure static and dynamic motion-vector feasibility.
  - Classify HUD, transparency and reactive content.
  - Define history reset events for cuts, teleports, pause, resize, resolution change, save/load and device loss.
  - Add debug views for every proposed input.
- **Acceptance:** A go/no-go report in [`docs/findings.md`](docs/findings.md) includes captures, coordinate conventions,
  missing inputs, dynamic-object limitations and a native fallback contract.
- **Stop:** Return "no-go" if required inputs cannot be made correct without broad game-specific draw guessing. Do not
  substitute optical flow and call the result a motion-vector implementation.

#### DLSS-01 - DLSS 5 qualification

- **Depends on:** GFX-04
- **Objective:** Decide whether a supported DLSS 5 path should receive a future implementation plan.
- **Work:**
  - Pin the public SDK/API and acquisition terms evaluated.
  - Obtain human disposition for build, redistribution, notices and runtime loading.
  - Map GFX-04 resources to the SDK's D3D12 requirements.
  - Build a protected-content-free synthetic spike if the SDK terms permit it.
  - Measure capability detection, unsupported GPU behavior, resource state transitions, reset and native fallback.
  - List the exact work still required for Super Resolution and DLAA separately.
- **Acceptance:** A signed go/no-go decision records SDK version, license status, supported hardware/API, required
  renderer changes, unresolved image-quality risks and estimated implementation/test cost.
- **Stop:** Any unclear redistribution right, unavailable public SDK, unsupported D3D12 path or unresolvable required
  input is a no-go for product integration. No NVIDIA binary is committed.

#### GFX-05 - Display follow-up

- **Depends on:** GFX-01
- **Objective:** Improve display behavior without blocking the core graphics lane.
- **Work:** Qualify 4:3, 16:9 and optional ultrawide geometry/HUD behavior; establish linear-color behavior and SDR
  parity before any HDR output proposal.
- **Acceptance:** Menus, FMVs, scopes, HUD and gameplay geometry pass the selected display matrix.
- **Stop:** Do not claim scene-referred HDR when only SDR remapping is available.

### Gameplay lane

#### PROFILE-01 - Default `Shooterman` isolated test profile

- **Depends on:** CFG-01
- **Objective:** Use the game's developer profile as the default empty-card testing profile without affecting players.
- **Work:** Add `Testing.DefaultProfile`; default it to exact `Shooterman`; use the existing front-end typing/creation
  flow; retain explicit overrides for automation; log the selected card root and profile name without protected data.
- **Acceptance:**
  - An empty `testcard` creates `Shooterman` through the game UI.
  - The native profile methods expose all campaign levels, difficulties and animatics.
  - A populated test card is not renamed or overwritten.
  - The normal memory card is byte-identical before and after an isolated run.
  - Changing `[Gameplay]` has no effect on profile unlocks, and changing the test profile has no effect on tuning.
- **Stop:** Never ship a prebuilt card or write a test profile to the normal card without an explicit user action.

#### WPN-01 - Discover and measure the player weapon model

- **Depends on:** BASE-01, TEST-01; explicit BYOD authorization
- **Objective:** Identify the actual functions and data that produce recoil, shot direction, spread and recovery.
- **Work:** Trace one semi-automatic and one automatic weapon across hip fire, ADS, moving and stationary states;
  separate camera kick, viewmodel animation, aim sway, shot dispersion, bloom and crosshair presentation; identify RNG
  ownership and stable weapon identity.
- **Acceptance:** [`docs/findings.md`](docs/findings.md) records approved symbols/offsets, units, call order, ownership,
  ranges and a repeatable baseline metric for each proven component. Every proposed hook passes manifest validation.
- **Stop:** Do not expose a field based only on correlation, a string name or a one-weapon memory edit.

#### WPN-02 - Global recoil control

- **Depends on:** CFG-01, WPN-01
- **Objective:** Add a reversible player-only recoil multiplier.
- **Work:** Scale the smallest evidenced recoil contribution; preserve viewmodel animation and shot direction unless
  WPN-01 proves they are the same value; keep AI and mounted/scripted weapons on original behavior.
- **Acceptance:**
  - `RecoilScale=1.0` is metric- and capture-identical to baseline.
  - At least two non-default values produce the predicted change in cumulative impulse and recovery.
  - Semi-automatic, automatic, ADS, hip-fire, movement, weapon switch, reload, cutscene and save/load cases pass.
  - Invalid values fail visibly and do not install a partial hook.
- **Stop:** If camera, projectile and animation effects cannot be isolated safely, expose only the proven aggregate and
  document its coupled behavior.

#### WPN-03 - Global accuracy and recovery controls

- **Depends on:** CFG-01, WPN-01
- **Objective:** Add player-only spread and recovery multipliers with unambiguous units.
- **Work:** Scale the evidenced angular dispersion and recovery path; preserve RNG sequence where practical; ensure the
  crosshair/spread indicator remains truthful; keep aim assist and AI accuracy unchanged.
- **Acceptance:**
  - `SpreadScale=1.0` and `RecoveryScale=1.0` match the baseline shot distribution and timing.
  - Non-default values change the measured 50th/95th percentile impact radius and recovery curve in the expected
    direction over a statistically useful sample.
  - Hip-fire, ADS, moving, crouched and sustained-fire states are tested where the game distinguishes them.
  - Zero and boundary values have defined, safe behavior.
- **Stop:** Do not label recoil reduction as accuracy or infer accuracy from crosshair art alone.

#### WPN-04 - Per-weapon overrides and launcher UX

- **Depends on:** WPN-02, WPN-03
- **Objective:** Add optional per-weapon tuning without making the INI fragile.
- **Work:** Define stable IDs, inheritance from global values, reset-to-original controls, effective-value diagnostics
  and launcher explanations. Keep the INI authoritative and human-editable.
- **Acceptance:** Unknown weapon IDs are diagnosed; omitted overrides inherit globals; reset restores `1.0`; every
  supported weapon category completes the combat regression matrix.
- **Stop:** If stable IDs cannot survive level loads and object recreation, keep global controls only.

#### GAME-01 - Integrated gameplay qualification

- **Depends on:** PROFILE-01, WPN-02, WPN-03, STAB-01
- **Objective:** Approve useful tuning ranges without silently changing unrelated game systems.
- **Work:** Compare original and candidate settings across representative weapons, campaign chapters and difficulty
  levels; collect hands-on feedback separately from metrics; document recommended ranges rather than pretending one
  value is objectively correct.
- **Acceptance:** Original values remain available in one action; campaign progression and saves are unchanged; no AI,
  damage, timing or animation regression is detected; recommended ranges have measurement and play-test evidence.
- **Stop:** Do not make a non-original gameplay value the built-in default without a separate product decision.

## 8. Milestones and dependency order

| Milestone | Exit criteria |
|---|---|
| **M0 - Baseline locked** | BASE-01, CFG-01 and TEST-01 pass; current INI is reproducible; WIN-06 has a root-cause path and remains explicitly blocking if not fixed. |
| **M1 - D3D12 image-quality foundation** | GFX-01 and GFX-02 pass; active AA/scaling settings are truthful; render/output coordinates and native fallback are stable. |
| **M2 - Safe graphics and weapon controls** | GFX-03, PROFILE-01, WPN-01, WPN-02 and WPN-03 pass; original behavior is preserved at `1.0`. |
| **M3 - Modern-rendering decision** | GFX-04 and DLSS-01 produce signed go/no-go decisions; no DLSS product claim is made. |
| **M4 - Enhancement release candidate** | STAB-01, GAME-01 and the selected display follow-up pass; release gate evidence is complete. |

M0 and M1 passed on 2026-10-20. STAB-01 remains explicitly deferred and blocking for M4/release.

Parallel order after M0:

```text
GFX-01 -> GFX-02 -> GFX-04 -> DLSS-01
   |         |
   +-> GFX-05

GFX-03 ------------------------------+
                                       +-> release qualification
PROFILE-01 ---------------------------+
WPN-01 -> WPN-02 + WPN-03 -> WPN-04 -+
STAB-01 ------------------------------+
```

GFX-03, PROFILE-01, WPN-01 and STAB-01 may proceed independently once their named prerequisites pass.

## 9. Deferred and out-of-scope work

The following work is not deleted, but it is outside the active release plan:

- native macOS/Apple Silicon, Metal and MetalFX;
- AArch64 JIT work beyond preserving existing evidence;
- DLSS product integration, DLSS frame generation and Ray Reconstruction;
- FSR/XeSS integration until the common temporal contract is qualified;
- ray tracing;
- new VFS/mod-loading behavior and loose-asset expansion;
- Blender import/export tooling;
- AI/navigation tuning;
- spatial audio and head tracking;
- multiplayer/online restoration;
- new game content or redistribution of protected assets.

Reactivating any item requires a new work package with dependencies, acceptance criteria and release impact. Existing
source and tests for deferred platform work must continue to compile where the checked Windows build depends on them.

## 10. Risk register

| Risk | Required mitigation |
|---|---|
| Existing INIs are silently changed | Versioned migration fixtures, effective-config diagnostics and round-trip tests |
| D3D12 control is enabled but ignored | Backend capability reporting and synthetic toggle tests |
| DLSS lacks correct depth/motion/UI inputs | GFX-04 debug views and a permitted no-go decision |
| Proprietary SDK cannot be distributed | Human legal disposition; never commit vendor binaries |
| `Shooterman` is mistaken for a tuning preset | Separate Testing and Gameplay sections; explicit UI copy and tests |
| Recoil is confused with animation or shot spread | WPN-01 component tracing before mutation |
| Accuracy tuning affects AI or RNG globally | Player ownership checks, deterministic captures and AI regression cases |
| Hook/layout drift corrupts guest memory | Executable hash, signature manifest, bounds checks and fail-closed installation |
| Elevated LOD/distance exhausts memory | High-water telemetry, bounded ranges and campaign regression |
| WIN-06 invalidates long-run results | Keep STAB-01 as a release blocker and report failed runs |
| Protected content enters source or artifacts | BYOD separation, package allowlists and protected-content scans |

## 11. Release gate

An enhancement release requires:

- clean MSVC and clang-cl builds and the complete ISO-free suite;
- BASE-01 and STAB-01 runtime acceptance;
- D3D12 golden and BYOD scene-set acceptance on the supported GPU/driver matrix;
- config migration and reset-to-original tests;
- weapon tuning at `1.0` matching baseline and non-default values matching documented semantics;
- proof that the default `Shooterman` test workflow never touches the normal memory card;
- campaign, save/load, cutscene, FMV, resize, device-loss, shutdown and soak tests;
- frame-time, memory and shader-stutter measurements;
- dependency notices, corresponding-source review, SBOM and secret/protected-content scans;
- no DLSS claim unless a later approved implementation plan has passed;
- human approval of the release artifact's legal, trademark and distribution evidence.

## 12. Approval status

This plan records the requested direction but does not substitute for named release sign-off.

| Role | Status |
|---|---|
| Product direction | Graphics/gameplay focus requested 2026-10-20 |
| Windows/runtime owner | M0 and M1 accepted 2026-10-20; STAB-01 remains deferred and release-blocking |
| Graphics owner | Pending GFX-04 and DLSS-01 |
| Gameplay owner | Pending WPN-01 evidence |
| Test/release owner | Pending hardware and campaign matrices |
| Legal/distribution owner | Existing LEGAL-01 disposition retained; any DLSS artifact requires separate review |
