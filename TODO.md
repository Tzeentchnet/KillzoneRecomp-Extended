# TODO - current milestone queue

**Milestone:** M2 - Safe graphics and weapon controls
**Last updated:** 2026-10-22

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
| 1 | WPN-01 | Discover and measure the player weapon model | Windows | `[READ-PARALLEL]` | not started | BYOD authorization |
| 2 | AUDIO-01 | Reproduce combat audio clipping | Windows | `[READ-PARALLEL]` | in progress | - |
| 3 | WPN-02 / WPN-03 | Global recoil, spread and recovery controls | Windows | `[SERIAL]` | blocked | WPN-01 |

The active graphics lane is complete through GFX-05. WPN-01 and AUDIO-01 are the remaining Windows enhancement lanes.
GFX-03, GFX-04 and GFX-05 are complete in [`WORKDONE.md`](WORKDONE.md); GFX-04 returned a technical no-go for temporal
reconstruction, so DLSS-01 is retired unless the renderer/input contract is fundamentally revised. STAB-01/WIN-06 is
retired from the release plan because it
qualified the direct-start/skip-level diagnostic path, which exact `Shooterman` plus native campaign selection has
superseded. BASE-01, CFG-01, TEST-01, GFX-01, GFX-02, GFX-03, GFX-04, GFX-05, PROFILE-01, WIN-01, WIN-03 and WIN-05 are complete in
[`WORKDONE.md`](WORKDONE.md). DLSS-01 is qualification only; product integration is not in this queue.

**GFX-03 reinvestigation and follow-up (2026-10-21/22, no WORKDONE change — GFX-03 itself is not reopened):** a
session replaced the withdrawn shared-context LOD scaling with per-class scaling (`src/kz_detail.cpp`) to see
whether the mechanism, not just the symptom, could be isolated. Each class (static/instanced/skinned LOD, grass,
visible-distance cull) passes independently up to 8x in a chapter-5-only gate; `RenderDistance=2` combined with
*any* elevated class still corrupts the guest heap, reproducing the original no-go with cleaner evidence but no
identified root cause. A follow-up session then (a) exposed the four per-class scales as real `[Graphics]` config
keys and launcher sliders, range `1..2`, with `RenderDistance > 1` hard-forcing all four back to `1.0` in both the
config resolver and the live UI (shipped, validated via config self-tests, a resolver unit test, and a live boot +
45 s scripted-gameplay check through an actual `killzone.ini`, not just diagnostic env vars), and (b) investigated
raising the grass-instance cap above 1500, which reproducibly crashes (bisected to a specific sub-mechanism; the
cap is NOT raised, no config exposure was added for it). See `docs/findings.md` "GFX-03 reinvestigation" and "GFX-03
follow-up" for the full writeup, every ruled-out mechanism, and the experimental PS2Recomp diagnostic patch.
Remaining gap before this could be considered against GFX-03's own original acceptance bar: no full BYOD/campaign
matrix (chapter-5-only evidence so far), no human visual review of the four new sliders' output.

### AUDIO-01 - Combat audio clipping

- **Status:** in progress. Paired hands-on captures localize the audible loss to the host SPU2 output path. The
  callback ledger and summarizer are complete. The committed mitigation reduces severity but does not satisfy the
  no-cut combat acceptance.
- **Reproducer:** Escape, action-heavy firefights, speakers, VSync on. Gunfire/effects and dialogue truncate by about
  100 ms several times per fight. Hope is a lower-severity comparison, not a clean negative arm: the paired run heard
  a few cuts across background audio, scripted dialogue and combat chatter.
- **Evidence:** the Escape baseline produced 317,863 underrun-silence frames and skipped 254,851 catch-up frames over
  184 seconds; Hope produced 161,858 / 166,077 over 220 seconds. SDL requests stayed near 10 ms, with zero SPU2 ring
  overflow, stream-submit failure or diagnostic-record loss. During a focused audible combat cut the IOP worker's
  published-cycle backlog reached 954.27 ms while the audio callback remained timely.
- **Mitigation retained:** the host reserve is 120 ms (was 40) and high water is 320 ms (was 150), with diagnostic
  overrides `KZ_AUDIO_BUFFER_MS` / `KZ_AUDIO_HIGH_MS`. Hands-on firing/reload latency was not noticeable and cuts fell
  from many to a few, but a large combat dropout still reproduced.
- **Rejected / parked:** highest IOP thread priority locked interactive gameplay; synchronous IOP was worse in the
  long gate. RPC cycle-credit accounting held IOP lead near 6 ms instead of 9.8 seconds but did not eliminate a
  hands-on dropout. The combined cycle-credit / above-normal-priority candidate is preserved, not applied, at
  `patches/ps2recomp/experimental/audio-iop-cycle-credit-priority.diff`.
- **Next step:** if AUDIO-01 resumes, apply the experimental patch in an isolated branch, add a repeated interactive
  stability gate before any priority change, and require zero callback-path loss in a combat soak. Do not raise the
  reserve further without a fresh latency decision.

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

## Retired integration gate

### WIN-02 - unlock-query convenience hooks

- **Status:** superseded by the exact `Shooterman` profile on 2026-10-21; not a release blocker.
- **Reason:** `Shooterman` exposes every campaign map and difficulty through native profile queries and menus. The
  non-persistent unlock hooks remain available as a diagnostic convenience but are not the supported all-map path.

### STAB-01 / WIN-06 - direct-start repeated-load diagnostic

- **Status:** retired by product decision on 2026-10-21; not a release, milestone or GFX-03 blocker.
- **Reason:** the investigation qualified failures reached through the direct-start/skip-level diagnostic. Exact
  `Shooterman` already unlocks every campaign map through the game's native profile queries and menu flow, so the
  project no longer needs that diagnostic path for release acceptance.
- **Evidence retention:** allocator, cleanup-list, wrapped-write and renderer-hardening evidence remains in
  [`docs/findings.md`](docs/findings.md). `win06_gate.ps1` and the optional hooks remain diagnostic tools, not gates.
- **Scope:** this is a retirement, not a claim that the diagnostic path's corruption was fixed.

### DLSS-01 - stopped by GFX-04

- **Status:** retired by the 2026-10-21 GFX-04 technical no-go.
- **Reason:** gameplay exposes no stable depth target, camera matrices, motion vectors or separate HUD/reactive mask.
  Optical-flow substitution is outside the accepted contract. No SDK or NVIDIA binary is required or committed.
- **Reopen only if:** a future renderer or guest integration supplies validated color, depth, motion, UI/reactive and
  history-reset inputs without broad draw guessing.

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
