# TODO - current milestone queue

**Milestone:** M0 - Windows enhancement baseline
**Last updated:** 2026-10-20

The durable queue of open work. Definitions, dependencies, acceptance commands and stop conditions are in
[`IMPLEMENTATION_PLAN.md`](IMPLEMENTATION_PLAN.md); verified completions are in [`WORKDONE.md`](WORKDONE.md). A task
leaves this file only when its plan acceptance passes on the named machine. Then move it to `WORKDONE.md` with the
evidence.

Status values: `not started` | `in progress` | `blocked` | `complete (move it)`.
Labels: `[SUBAGENT-PARALLEL]` isolated worktree, single writer per file scope; `[READ-PARALLEL]` read-only, parent
owns edits; `[SERIAL]` waits on dependencies or the integration owner; `[HUMAN]` needs a human decision.

## Summary

| # | ID | Task | Machine | Label | Status | Blocked by |
|---|---|---|---|---|---|---|
| 1 | BASE-01 | Lock the Windows enhancement baseline (legacy WIN-04) | Windows | `[SERIAL]` | in progress | hands-on play-test |
| 2 | STAB-01 | Resolve chapter 11 repeated-load corruption (legacy WIN-06) | Windows | `[SERIAL]` | in progress | - |
| 3 | TEST-01 | Graphics and weapon measurement harness | Windows | `[SERIAL]` | blocked | BASE-01 |
| 4 | GFX-01 | D3D12 presentation and post-process parity | Windows | `[SERIAL]` | blocked | TEST-01 |
| 5 | GFX-03 | Qualify render distance, LOD and texture clarity | Windows | `[SERIAL]` | blocked | BASE-01, TEST-01 |
| 6 | WPN-01 | Discover and measure the player weapon model | Windows | `[READ-PARALLEL]` | blocked | BASE-01, TEST-01, BYOD authorization |
| 7 | GFX-02 | Separate internal render scale from output scale | Windows | `[SERIAL]` | blocked | GFX-01 |
| 8 | WPN-02 / WPN-03 | Global recoil, spread and recovery controls | Windows | `[SERIAL]` | blocked | WPN-01 |
| 9 | GFX-04 | Temporal input feasibility contract | Windows | `[SERIAL]` | blocked | GFX-02 |
| 10 | DLSS-01 | DLSS 5 qualification and go/no-go decision | Windows | `[HUMAN]` | blocked | GFX-04, SDK/legal access |

BASE-01 and STAB-01 are the current executable work. TEST-01 follows BASE-01. Graphics and gameplay then split into
independent lanes as defined in the plan. CFG-01 and PROFILE-01 are complete in [`WORKDONE.md`](WORKDONE.md). DLSS-01
is qualification only; product integration is not in this queue.

## Paused work from the superseded portability roadmap

The native macOS/Apple Silicon packages below are no longer on the active release path. Their status and evidence are
retained so completed work is not lost, but no task in this section blocks M0-M4 and no new work should begin without
a plan revision.

### 1. VU-01 - portable VU interpreter (native acceptance)

- **Status:** in progress. Implementation and Windows conformance complete; native AppleClang ARM64 run pending.
- **Windows evidence so far:** `KZ_PORTABLE_VU_ONLY=ON` compiles only PS2Recomp's three VU interpreter sources and
  the conformance target (no game code, renderers, PCSX2 microVU or x86 emitter). `kzvu_interpreter_conformance`:
  28/28 checks under MSVC and clang-cl, covering arithmetic, FMAC flags, branches and delay slots, stalls, memory
  masks and latency, wrapping XGKICK, decoded-code invalidation, cycle budgets, resume, reset, reuse and T-bit
  lifecycle. The `macos-arm64` presets pin the locked sse2neon revision. Re-run passing from `D:\Github` on
  2026-10-17 (see `WORKDONE.md`).
- **Scope:** `ext/kzvu/portable/`, `ext/kzvu/test/`, `ext/PS2Recomp/ps2xRuntime/src/lib/vu/` (the submodule stays at
  its locked revision)
- **Next command (on the Mac):**

  ```sh
  cmake --preset macos-arm64
  cmake --build --preset macos-arm64
  ctest --preset macos-arm64 --output-on-failure -R '^kzvu_interpreter_'
  ```

- **Do not:** mark complete or begin VU-02 until all three pass natively.

### 2. MAC-01 - renderer-free host smoke (native acceptance)

- **Status:** in progress. Implementation and Windows smoke complete; native AppleClang ARM64 run with real CoreAudio
  pending.
- **Windows evidence so far:** `KZ_MACOS_PHASE0_ONLY=ON` builds the Darwin host core, renderer-free SDL smokes and the
  portable VU gate. The SDL 3.4.16 source archive is checksum-locked and built from source. The Darwin backend
  implements paths, environment, ARM64 CPU/FP state, pthread naming, W^X mapping/protection, instruction-cache
  invalidation and main-thread Cocoa backing metrics. Window smoke covers creation, native extraction, metrics, event
  delivery and teardown; audio smoke covers device format, callback delivery, resume, pause and teardown. Host subset
  3/3 under MSVC and clang-cl (Windows uses SDL's dummy audio driver).
- **Scope:** `host/` (including `host/src/core_macos.mm` and `host/test/`), `src/kz_sdl_host.*`, `CMakeLists.txt`
- **Next command (on the Mac):**

  ```sh
  ctest --preset macos-arm64 --output-on-failure -R '^kz_host_(window|audio)_smoke$'
  ```

- **Do not:** mark complete or begin GS-01 until it passes natively with CoreAudio.

### 3. ARM-01 - ARM64 generated guest-code smoke gate

- **Status:** not started. Deferred by project decision to the Apple Silicon machine; Windows cannot satisfy an
  AppleClang compile-and-run gate.
- **First step:** create the `macos-arm64-guest-smoke` workflow and its configure/build/test presets (missing today).
- **Scope:** `CMakePresets.json`, `cmake/`, a new `tests/arm64-smoke/` directory, `host/`
- **Inputs:** the authorized private generated tree, verified against `config/generated-manifest.json`. Never commit it.
- **Acceptance:** `cmake --workflow --preset macos-arm64-guest-smoke`

### 4. VU-02 - bounded AArch64 JIT spike

- **Status:** blocked on native VU-01 acceptance. After VU-01, it may run alongside the MAC-01/GS-01 branch in a
  separate worktree.
- **Acceptance:** `ctest --preset macos-arm64 --output-on-failure -R '^kzvu_a64_jit_smoke$'`

### 5. GS-01 - minimal Metal parity gate

- **Status:** blocked on native MAC-01 acceptance. After MAC-01, it may run alongside VU-02.
- **Acceptance:** `ctest --preset macos-arm64 --output-on-failure -R '^kzgs_metal_golden$'`

## Existing Windows baseline work

### BASE-01 (legacy WIN-04) - re-verify the Windows gameplay baseline

- **Status:** in progress. The scripted portion passed on 2026-10-19; the hands-on acceptance remains.
- **Scripted evidence (win-clang-rel, D3D12):** a clean isolated card entered the game's create-profile flow, saved
  `Tester`, selected `1.Trenches` on Normal and reached first-mission gameplay. The 120 Hz run exited 0 after 120 s,
  produced 11 distinct nonblack captures through 110 s, and logged no crash signature. The start-in-chapter
  autopilot was the scripted driver; this does not replace the hands-on baseline with testing options disabled.
- **Next command:** hands-on D3D12 play-test with testing options disabled: create/select a profile, play the first
  mission, save/load, quit to the front end, and check high-refresh timing and mouse aim.
- **Why now:** it protects every graphics and gameplay comparison in the revised plan.

### 7. WIN-01 - D3D11 black output on NVIDIA

- **Status:** not started. D3D11 stays exactly black through a 90-second capture on an RTX 5070 (driver
  `32.0.16.1742`), while D3D12 renders the same GS stream with the same executable, ELF, ISO, settings and guest state.
  The synthetic `kzgs_d3d11_golden` test passes on WARP, so the defect is in the hardware path.
- **Workaround in place:** the local `killzone.ini` uses `Renderer=d3d12`, and the launcher points users with a black
  D3D11 frame to D3D12.

### 8. WIN-03 - PS2Recomp standalone test-suite failures

- **Status:** not started. The broad standalone PS2Recomp test executable reaches a `sceMpegDemuxPssRing` failure and
  later an access violation. The focused MPEG low-water and two-callback regressions pass before that point.

### 9. WIN-02 - Unlock All Campaign Levels launcher option

- **Status:** in progress; implemented 2026-10-17 (launcher Testing tab, `[Testing] UnlockAllLevels`, `KZ_UNLOCK_ALL`).
  The unlock state is the profile's per-level record, read only through three PlayerProfile script methods; the option
  replaces those queries in memory and never writes the profile (`docs/findings.md` "Testing options").
- **Evidence so far (win-clang-rel, headless):** an ordinary fresh profile opened level select on chapter 11 and
  reached chapter 11 gameplay; memory-card files hashed identical before and after four unlock-on runs; with the option
  off no hook is installed; `--selftest` covers the INI round trip and default. The 2026-10-19 Windows regression set
  passed under both compilers; generated inventory PASS reports 47 hook signatures.
- **Next command:** hands-on enabled/disabled check, including an existing save.

### 10. WIN-05 - Testing start-up options

- **Status:** in progress; implemented 2026-10-17. Skip intro (logos screen and cinematic), Start in chapter 1-11 with
  difficulty (autopilot through the game's own menus, creates a `Tester` profile on an empty card), Test memory card
  (`testcard` folder next to `killzone.ini`).
- **Evidence so far:** autopilot reached chapter 1 gameplay (Normal) from a populated card and chapter 3 (Hard) from an
  empty card after creating and saving `Tester`; `SetLevelFile` confirmed `1.Trenches`, `3.Station`, `11.OTS`. The
  2026-10-19 Windows regression set passed 12/12 under both compilers; a fresh isolated card again saved `Tester` and
  reached chapter 1 gameplay at 120 Hz on D3D12.
- **Known issue:** WIN-06 also reproduces after the full intro; the shortcut is no longer considered its common cause.
- **Next command:** a hands-on run of the Test memory card option; then resolve WIN-06.

### STAB-01 (legacy WIN-06) - Chapter 11 repeated-load corruption

- **Status:** in progress. A full-intro run reached `11.OTS` at 225.7 s and hit the same missing-target failure, ruling
  out the testing shortcut as the common cause. Ownership tracing now proves a cross-owner use-after-free before the
  allocator/vector collapse; the lifecycle transition that leaves the alias live is not yet identified.
- **Gate:** `tools\scripts\win06_gate.ps1 -Runs 10 -Name <name> -Card work\win04_20261019_card -Iso <image>
  -Elf game\SCUS_974.02` (D3D12, skipped intro, fresh card copy per run, pass/fail summary). Acceptance is 10/10.
- **Ruled out (ten-run batches, win-clang-rel, D3D12):**
  - VIF1 MPG address wrap (`0030`, kept as a correctness fix): 8/10.
  - Movie/MPEG/IPU path: `KZ_IPU=off`, so no movie, decoder or stream callback exists at all: 8/10.
  - Interrupts delivered inside guest `di`/`ei` (the scheduler ignores Status.EIE): only 0-2 benign deliveries per
    run, and enforcing the mask gave 8/10. Kept as `patches/ps2recomp/experimental/ee-di-interrupt-gate.diff`.
  - Threaded IOP: `PS2X_IOP_THREAD=0` (synchronous IOP on the EE thread) gave 4/10 (`win06_iopsync_*`).
  - Earlier sessions: scratchpad-to-RAM DMA into low RAM.
- **Current localization:** cleanup `0x2E7B38 -> 0x2E7440 -> 0x2E8170` freed node `0x01F0FF00` for owner
  `0x007E1EC0`, while live parent `0x01281020` retained that allocation in its embedded vector field
  `parent+0x5C = 0x0128107C`. Seven guest-vsyncs later vector resize `0x4299F4` called realloc on the freed pointer,
  repeating at least three times. A separate successful watched run showed the allocator returning a different block
  ten vsyncs after free even though its header had been overwritten with live float data. Later consequences remain
  the multi-megabyte wrapped realloc copy, corrupt vtables, bad DMA records, timer/pool overwrites and renderer crashes.
- **Latest gate evidence:** `win06_watch5` and `win06_watch6` were 4/5; watched batches can also complete despite
  allocator invariant breaks. Two clean watched batches and one uninstrumented batch reached 10/10, but the next
  uninstrumented batch was 7/10 (one hang, one host exception, one missing target), so STAB-01 remains open.
- **Renderer hardening:** x64 crash registers proved that the GS palette crash was an inverted alpha-index range:
  its unsigned width wrapped to `0xFFFFFFC7`. The pinned PCSX2 guard now falls back to the full palette range and
  skips a separate null depth-target optimization; `kzgs_palette_guard` passes. This removed host exceptions, but
  `win06_gsguard` remained 8/10: one severe texture-corruption run and one EE `pc=ra=0x50` run. A guarded watched
  batch was 10/10, so the watch perturbs the timing and does not close the task.
- **Current decision:** do not guard the allocator, skip bad frees, clear stale fields generically or hide corrupt
  pointers. The watch now records heap bounds, cleanup owner/list provenance, delete-wrapper callers and bounded list
  chains. Next, add a targeted return hook for the 36-byte-element vector resize at `0x4299FC` so allocation results
  are tied to owner fields before cleanup, then identify the missing lifecycle clear. Acceptance still requires an
  uninstrumented 10/10 gate with visually clean captures.

## Retired integration gate

### P0-EXIT - superseded

- **Status:** retired by the 2026-10-20 graphics/gameplay plan revision.
- **Disposition:** the macOS branches and evidence remain available, but their integration is not a prerequisite for
  the Windows enhancement milestones.

## Enhancement queue after M0

Definitions, dependencies, acceptance criteria and stop conditions are in plan section 7.

| Priority | ID | Task | Label |
|---|---|---|---|
| 1 | GFX-01 / GFX-02 | Truthful D3D12 post-processing, then explicit render/output scaling | `[SERIAL]` |
| 1 | WPN-01 | Recoil/spread/recovery discovery and baseline metrics | `[READ-PARALLEL]` |
| 2 | GFX-03 | LOD, distance and texture-clarity qualification | `[SERIAL]` |
| 2 | WPN-02 / WPN-03 | Player-only recoil, spread and recovery controls | `[SERIAL]` |
| 3 | GFX-04 | Temporal color/depth/motion/UI/history feasibility | `[SERIAL]` |
| 4 | DLSS-01 | Public SDK, licensing and D3D12 go/no-go qualification | `[HUMAN]` |

DLSS product integration, frame generation, macOS/Metal, mod/VFS implementation, Blender tooling, AI/navigation and
spatial audio remain deferred. The completed ASSET-01 through ASSET-03 research in `WORKDONE.md` and
`docs/assetforms.md` remains historical interoperability evidence.
