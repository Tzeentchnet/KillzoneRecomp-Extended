# TODO - current milestone queue

**Milestone:** M0 - Feasibility approved (Phase 0)
**Last updated:** 2026-10-19

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
| 1 | VU-01 | Portable VU interpreter: native acceptance | Apple Silicon | `[SUBAGENT-PARALLEL]` | in progress | Mac access |
| 2 | MAC-01 | macOS host smoke: native acceptance | Apple Silicon | `[SUBAGENT-PARALLEL]` | in progress | Mac access |
| 3 | ARM-01 | ARM64 guest-code smoke gate | Apple Silicon | `[SUBAGENT-PARALLEL]` | not started | Mac access |
| 4 | VU-02 | Bounded AArch64 JIT spike | Apple Silicon | `[SERIAL]` | blocked | VU-01 |
| 5 | GS-01 | Minimal Metal parity gate | Apple Silicon | `[SERIAL]` | blocked | MAC-01 |
| 6 | WIN-04 | Re-verify Windows gameplay baseline | Windows | `[SERIAL]` | in progress | hands-on play-test |
| 7 | WIN-01 | D3D11 black output on NVIDIA | Windows | `[SUBAGENT-PARALLEL]` | not started | - |
| 8 | WIN-03 | PS2Recomp standalone test-suite failures | Windows | `[SUBAGENT-PARALLEL]` | not started | - |
| 9 | WIN-02 | Unlock All Campaign Levels launcher option | Windows | `[SERIAL]` | in progress | hands-on check |
| 10 | WIN-05 | Testing start-up options (skip intro, start in chapter, test card) | Windows | `[SERIAL]` | in progress | WIN-06 |
| 11 | WIN-06 | Chapter 11 load crash after a skipped intro cinematic | Windows | `[SUBAGENT-PARALLEL]` | not started | - |
| 12 | LEGAL-01 | Distribution and provenance disposition | Human | `[HUMAN]` | not started | - |
| 13 | P0-EXIT | Phase 0 integration and exit review | Mac + Windows | `[SERIAL]` | blocked | 1-6, 12 |

Rows 1-3 have no dependency edges between them and form the first parallel batch on the Mac. Rows 6-11 are the work
available on the current Windows machine.

## Critical path - Apple Silicon/macOS

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

## Windows machine - available now

### 6. WIN-04 - re-verify the Windows gameplay baseline

- **Status:** in progress. The scripted portion passed on 2026-10-19; the hands-on acceptance remains.
- **Scripted evidence (win-clang-rel, D3D12):** a clean isolated card entered the game's create-profile flow, saved
  `Tester`, selected `1.Trenches` on Normal and reached first-mission gameplay. The 120 Hz run exited 0 after 120 s,
  produced 11 distinct nonblack captures through 110 s, and logged no crash signature. The start-in-chapter
  autopilot was the scripted driver; this does not replace the hands-on baseline with testing options disabled.
- **Next command:** hands-on D3D12 play-test with testing options disabled: create/select a profile, play the first
  mission, save/load, quit to the front end, and check high-refresh timing and mouse aim.
- **Why now:** a Phase 0 exit criterion, and it protects every later Windows change.

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

- **Status:** in progress; implemented 2026-10-17. Skip intro (logos screen and cinematic only; boot to main menu
  6-7 s instead of about 87 s), Start in chapter 1-11 with difficulty (autopilot through the game's own menus,
  creates a `Tester` profile on an empty card), Test memory card (`testcard` folder next to `killzone.ini`).
- **Evidence so far:** autopilot reached chapter 1 gameplay (Normal) from a populated card and chapter 3 (Hard) from an
  empty card after creating and saving `Tester`; `SetLevelFile` confirmed `1.Trenches`, `3.Station`, `11.OTS`. The
  2026-10-19 Windows regression set passed 12/12 under both compilers; a fresh isolated card again saved `Tester` and
  reached chapter 1 gameplay at 120 Hz on D3D12.
- **Known issue:** WIN-06. Do not mark complete while the skip makes chapter 11 loads fail above the baseline.
- **Next command:** a hands-on run of the Test memory card option; then resolve WIN-06.

### 11. WIN-06 - Chapter 11 load crash after a skipped intro cinematic

- **Status:** not started (parked by decision on 2026-10-17, to be analyzed later). Evidence and run names are in
  `docs/findings.md` "Testing options", section "Known issue". Diagnostic switch: `KZ_SKIP_INTRO_MASK` (1 logos only,
  2 cinematic only, 3 both).
- **Next command:** repeat a chapter 11 load with `KZ_START_LEVEL=11` (intro skipped) under `KZ_CRASH_TRACE=1` to get
  the faulting host stack.

## Human

### 12. LEGAL-01 - distribution and provenance disposition

- **Status:** not started. Needs written decisions on licenses, notices and source offers, generated-code treatment,
  trademarks and naming, SDK redistribution, and protected-content scanning. Also needs a decision on when, if ever,
  the private `origin` becomes public. Agents must not change repository visibility.

## Integration

### 13. P0-EXIT - Phase 0 integration and exit review

- **Status:** blocked on rows 1-6 and 12.
- **Work:** merge the macOS branches serially, rerun every macOS gate, rerun the Windows regression set from
  [`HANDOFF.md`](HANDOFF.md), record the JIT/interpreter decision, and replace estimates with measured results.
  Writer subagents never share a worktree: the macOS branches all touch `CMakeLists.txt`, `CMakePresets.json`,
  `README.md` and the status documents.

## Queued after Phase 0

Not startable until P0-EXIT passes, except read-only research. Definitions are in plan section 6.

| Priority | ID | Task | Label |
|---|---|---|---|
| 1 | TEMP-01 | Frame IDs, pass capture, replayable native fallback | `[SUBAGENT-PARALLEL]` |
| 1 | LOD-01 / LOD-02 | Pure tests for existing LOD transforms, then culling/RenderZone traces | `[SUBAGENT-PARALLEL]` |
| 2 | HDR/display | Linear-color and SDR-parity evidence | `[READ-PARALLEL]` |
| 3 | Input lane | INI schema versioning, `RawMouse` resolution, sensitivity-math tests | `[SUBAGENT-PARALLEL]` |

Vendor upscalers, MetalFX, HDR output, dynamic motion vectors and frame generation are not early parallel tasks.
Mod/VFS implementation is deferred and does not enter this queue. Blender implementation belongs to a separate
project. The completed ASSET-01 through ASSET-03 research in `WORKDONE.md` and `docs/assetforms.md` is the
interoperability evidence that project can consume.
