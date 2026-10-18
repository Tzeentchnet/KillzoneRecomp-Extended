# KillzoneRecomp execution handoff

**Last updated:** 2026-10-17 17:37 PDT
**Workspace:** `E:\Git\Killzone`
**Implementation repository:** `E:\Git\Killzone\KillzoneRecomp-Extended`
**Branch / implementation commit:** `master` / `24e9cfa341a70e92a8aa1c2d0be3685e5f4fd236`
**PS2Recomp revision:** `917b379aa76949dc4ce42cdc1eaa0d3313fc76eb`
**GitHub origin:** `Tzeentchnet/KillzoneRecomp-Extended` is private during
cleanup. The PS2Recomp dependency now uses `Tzeentchnet/PS2Recomp`; the
original public upstream remains unchanged.

This is the living execution handoff for future sessions and agents. Update it
at the end of every implementation session that changes status, dependencies,
validation evidence, blockers, or the recommended next action.

## 1. Authority and startup order

Read these in order before changing code:

1. [`../plan.md`](../plan.md) - authoritative milestone order, dependencies,
   acceptance commands, and stop conditions.
2. [`../KillzoneRecomp_Master_PRD.md`](../KillzoneRecomp_Master_PRD.md) -
   product requirements and current milestone status.
3. This document - current operational state and delegation boundaries.
4. [`README.md`](README.md) - supported build, test, and BYOD workflows.

Code and passing acceptance commands override stale prose. If this handoff
disagrees with the plan, follow the plan and correct this file in the same
session.

At the start of every session:

```powershell
Set-Location E:\Git\Killzone\KillzoneRecomp-Extended
git status --short --untracked-files=all
git submodule status --recursive
```

The integrated work from BASE-01 through PORT-01, the Windows-validated
implementations of VU-01 and MAC-01, the boot/dispatch restoration, and the
memory-card and MPEG ownership corrections are committed through
`24e9cfa341a70e92a8aa1c2d0be3685e5f4fd236`. Preserve any unrelated
working-tree changes rather than resetting or cleaning them.

## 2. Parallelization labels

- **`[SUBAGENT-PARALLEL]`** - may run concurrently in an isolated worktree or
  branch with a single writer for its file scope.
- **`[READ-PARALLEL]`** - read-only investigation may run concurrently in the
  shared worktree; the parent session owns all edits.
- **`[SERIAL]`** - must wait for dependencies or must be performed by the
  integration owner.
- **`[HUMAN]`** - requires a human decision or authorization and must not be
  delegated as an approval.

Writer subagents must not share one working tree. The macOS branches below
touch common integration files such as `CMakeLists.txt`, `CMakePresets.json`,
`README.md`, and this handoff. Give each writer an isolated worktree and let
one integration session merge and validate them serially. In a shared
worktree, use subagents only for bounded read-only inventories.

## 3. Completed baseline

| Milestone | Status | Acceptance evidence |
|---|---|---|
| BASE-01 | Complete | Strict dependency verification: 28 passed, 0 warnings, 0 failures |
| BASE-02 | Complete | Relocatable build, run, regeneration, and BYOD path gates pass |
| BASE-03 | Complete | Checked MSVC and clang-cl presets configure successfully |
| TEST-01 | Complete | Hermetic ISO-free CTest layer and disabled-by-default BYOD test |
| TEST-02 | Complete | D3D11, D3D12, and Vulkan deterministic golden tests |
| GEN-01 | Complete | 132,656 dispatch entries, 100,661 globally dispatchable resume cases, 42 locked hook signatures |
| PORT-01 | Complete | Renderer-free `kz_host_core`; MSVC and clang-cl builds and host component tests pass |

Latest Windows validation:

```text
MSVC ISO-free:    12/12 passed
clang-cl ISO-free: 12/12 passed
Portable VU:      28/28 checks passed with MSVC and clang-cl
Host smoke:       3/3 passed with MSVC and clang-cl
Strict dependencies: 31 passed, 0 warnings, 0 failures
Generated inventory: PASS with 42 locked hook signatures
Resume table: PASS with 132,656 dispatch entries, 100,661 resume cases, and 31,995 targets
Deterministic regeneration: 31,998 files, 0 changed/new, 0 removed
Complete D3D12 movie probe: visible decoded frames at 35, 40, 45, and 50 s
Minimized D3D12 boot probe: physical MCSERV, successful host memory-card operations, and main menu at 80 s
Cleaned D3D12 intro cadence: 49 image changes in 1.6169 s (30.3/s)
Cleaned D3D12 menu cadence: 43 image changes in 1.4664 s (29.32/s)
Cleaned D3D12 attract cadence: 44 image changes in 1.4724 s (29.88/s; 42 ideal two-vblank gaps, one four-vblank gap)
Long D3D12 idle timeline: attract reel active by 160 s, advancing through 240 s, menu restored by 260 s, exit 0
PS2Recomp low-water prefetch regression: PASS
Visible corrected MSVC D3D12 session: responsive Killzone window with nonblack logo frame; left running
git diff --check: PASS
```

The current private generated tree has 31,996 generated translation units and
31,995 generated functions. Retail content and generated game-derived source
remain ignored and must not be committed.

The Windows guest now boots through FilePS2 completion, completes host
memory-card operations while physical `MCSERV.IRX` remains loaded, renders the
opening movies, and reaches the main menu. Authoritative bindings prevent
overlapping generated owners from shadowing three memory-card and two MPEG
runtime stubs. The MPEG wrappers establish `$ra` as the return PC before a
handler can suspend, so asynchronous decoder waits resume in the guest caller.
Self-fed movies now invoke `sceMpegCbNodata` when four or fewer decoded
pictures remain instead of waiting for the queue to become empty. This keeps
the opening, menu, and attract movies near their 29.97 fps source cadence
without changing `sceCdSt`-produced streams or spinning on asynchronous reads.
On the current NVIDIA GeForce RTX 5070 driver (`32.0.16.1742`), D3D11 still
presents an all-black image even though the guest and GS counters advance.
D3D12 renders correctly. The local MSVC `killzone.ini` therefore persists
`Renderer=d3d12`; the launcher also directs users with a D3D11 black frame to
D3D12. D3D11 remains available and is not claimed fixed.

## 4. Current Phase 0 dependency graph

```text
GEN-01 + PORT-01 ──> ARM-01

PORT-01 + TEST-01 ──> VU-01 ──> VU-02

PORT-01 ──> MAC-01 ──> GS-01
TEST-02 ─────────────────^

ARM-01 + VU-01/VU-02 + MAC-01/GS-01
                └──> Phase 0 integration and exit review
```

ARM-01, VU-01, and MAC-01 have no dependency edges between them. They are the
first parallel batch on the Apple Silicon/macOS machine.

| Work package | Machine | Parallelization | Status |
|---|---|---|---|
| ARM-01 ARM64 guest smoke | Apple Silicon/macOS | **`[SUBAGENT-PARALLEL]`** with VU-01 and MAC-01 | Deferred to macOS machine |
| VU-01 portable interpreter | Apple Silicon/macOS | **`[SUBAGENT-PARALLEL]`** with ARM-01 and MAC-01 | Implementation and Windows conformance complete; AppleClang ARM64 acceptance pending |
| MAC-01 host smoke | Apple Silicon/macOS | **`[SUBAGENT-PARALLEL]`** with ARM-01 and VU-01 | Implementation and Windows smoke complete; AppleClang ARM64 acceptance pending |
| VU-02 bounded AArch64 JIT | Apple Silicon/macOS | **`[SERIAL]`** after VU-01; then parallel-safe with the MAC/GS branch | Waiting on VU-01 |
| GS-01 minimal Metal parity | Apple Silicon/macOS | **`[SERIAL]`** after MAC-01; then parallel-safe with the VU branch | Waiting on MAC-01 |
| Phase 0 integration | Apple Silicon/macOS plus Windows regression | **`[SERIAL]`** integration owner only | Waiting on all selected gates |
| Legal/distribution disposition | Human | **`[HUMAN]`**, may proceed alongside engineering | Pending |

Cross-compilation or emulation on Windows is not acceptance evidence for any
AppleClang compile-and-run gate.

## 5. Parallel subagent work packets

### Agent A - ARM-01 guest-code smoke

**Label:** `[SUBAGENT-PARALLEL]`
**Scope:** ARM64 guest corpus, macOS workflow preset, and only the minimum
renderer-free runtime support needed by that corpus.

Bounded objective:

1. Add the `macos-arm64-guest-smoke` workflow and its configure/build/test
   presets. The workflow does not exist yet.
2. Compile and execute representative control flow, guest memory,
   floating-point behavior, dispatch, scheduler transitions, and generated
   helpers with AppleClang ARM64.
3. Keep x86 microVU and Windows renderer libraries out of the target.
4. Stop at the first unexplained ABI, pointer-width, alignment, atomic, or
   floating-point mismatch. Record a minimal reproducer before changing
   semantics.

Acceptance:

```sh
cmake --workflow --preset macos-arm64-guest-smoke
```

Expected primary scope:

```text
CMakePresets.json
cmake/
tests/arm64-smoke/
host/
```

Do not commit private generated sources. Use the authorized private generated
tree only as input and verify it against `config/generated-manifest.json`.

### Agent B - VU-01 portable interpreter

**Label:** `[SUBAGENT-PARALLEL]`
**Scope:** Portable VU interpreter and its conformance corpus. Do not begin the
AArch64 JIT in this packet.

Bounded objective:

1. Isolate an ARM64-compatible interpreter path that does not compile or
   initialize the x86 microVU JIT.
2. Add deterministic interpreter tests for arithmetic, flags, branches,
   stalls, memory behavior, XGKICK, invalidation-visible state, and lifecycle.
3. Preserve the existing Windows microVU path and tests.
4. Use the interpreter as the future VU-02 oracle only after every conformance
   test passes.

Acceptance:

```sh
cmake --preset macos-arm64
cmake --build --preset macos-arm64
ctest --preset macos-arm64 --output-on-failure -R '^kzvu_interpreter_'
```

Current implementation evidence:

- `KZ_PORTABLE_VU_ONLY=ON` configures only the PS2Recomp VU interpreter
  sources and their conformance target. It does not configure game-derived
  sources, renderers, PCSX2 microVU, or the x86 emitter.
- The `macos-arm64` configure, build, and test presets select Apple Silicon
  and the exact locked sse2neon revision.
- `kzvu_interpreter_conformance` has 28 deterministic checks covering
  arithmetic, FMAC flags, branches and delay slots, stalls, memory masks and
  latency, wrapping XGKICK, decoded-code invalidation, cycle budgets, resume,
  reset, reuse, and T-bit lifecycle.
- The complete Windows ISO-free suites pass 12/12 under MSVC and clang-cl;
  the pre-existing microVU differential test still passes.
- The locked `ext/PS2Recomp` checkout remains unmodified and strict dependency
  verification remains 31 passed, 0 warnings, 0 failures.

Do not mark VU-01 complete or begin VU-02 until the three acceptance commands
above pass natively with AppleClang on Apple Silicon.

Expected primary scope:

```text
ext/kzvu/
ext/PS2Recomp/ps2xRuntime/src/lib/vu/
tests/vu/
```

Stop if the portable path depends on x86 register layout, code emitters, or
implicit host floating-point state that has not been made explicit.

### Agent C - MAC-01 renderer-free host smoke

**Label:** `[SUBAGENT-PARALLEL]`
**Scope:** macOS implementations of the typed host boundary plus SDL window,
event, timing, path, and audio smoke tests. No Metal renderer work.

Bounded objective:

1. Replace non-Windows unsupported stubs with explicit macOS implementations
   for the PORT-01 services needed by the smoke tests.
2. Add Cocoa native-window extraction through SDL without leaking Objective-C
   types into platform-neutral headers.
3. Prove paths, monotonic timing, synchronization, window lifecycle, events,
   and audio startup/shutdown without game code or a renderer.
4. Keep unsupported services explicit; do not return success-shaped defaults.

Acceptance:

```sh
ctest --preset macos-arm64 --output-on-failure -R '^kz_host_(window|audio)_smoke$'
```

Expected primary scope:

```text
host/
src/kz_sdl_host.*
tests/host/
CMakeLists.txt
```

Stop on an unexplained main-thread, Cocoa ownership, event-loop, or audio
lifecycle violation.

Current implementation evidence:

- `KZ_MACOS_PHASE0_ONLY=ON` builds the Darwin host core, renderer-free SDL
  smokes, and portable VU gate without game-derived code or a renderer.
- The exact SDL 3.4.16 source archive is locked and verified offline. The
  macOS configuration builds that source rather than accepting a mutable
  system package.
- The Darwin backend implements path, environment, ARM64 CPU/FP state,
  pthread naming, W^X mapping/protection, instruction-cache invalidation, and
  main-thread Cocoa backing metrics. Unsupported diagnostics remain errors.
- The window smoke covers creation, native extraction, metrics, event
  delivery, and teardown. The audio smoke covers device format, callback
  delivery, resume, pause, and teardown.
- Both complete Windows ISO-free suites pass 12/12; the focused host subset
  passes 3/3 with MSVC and clang-cl.

Do not mark MAC-01 complete or begin GS-01 until the acceptance command above
passes natively with AppleClang on Apple Silicon and the real CoreAudio path.

## 6. Dependent serial packets

### VU-02 - bounded AArch64 JIT

**Label:** `[SERIAL]` after VU-01.

- Never map a page writable and executable simultaneously.
- Invalidate the instruction cache after code publication.
- Test allocation, write/protect, execution, invalidation, reuse, reset, and
  teardown.
- Differentially compare every microprogram result against VU-01.
- Keep the interpreter fallback if correctness, hardened-runtime behavior,
  source compatibility, or entitlements fail.

Acceptance:

```sh
ctest --preset macos-arm64 --output-on-failure -R '^kzvu_a64_jit_smoke$'
```

### GS-01 - minimal Metal parity

**Label:** `[SERIAL]` after MAC-01, but it may run concurrently with VU-02 in
separate worktrees.

- Wire only the minimal Metal backend through the real `kzgs` API.
- Render the existing deterministic synthetic golden corpus.
- Validate resize, minimize, device loss, and shutdown.
- Do not begin MetalFX, HDR, or temporal work.

Acceptance:

```sh
ctest --preset macos-arm64 --output-on-failure -R '^kzgs_metal_golden$'
```

### Phase 0 integration and exit review

**Label:** `[SERIAL]`.

The integration owner merges the macOS branches, resolves shared CMake and
preset edits, runs all macOS gates, then reruns the Windows baseline. Revise
the PRD estimates from measured results and record the interpreter/JIT
fallback decision. Human legal and distribution evidence must also receive a
disposition before Phase 0 is declared complete.

## 7. Post-gate lanes

Do not begin these implementation lanes before the Phase 0 integration review.
After their named prerequisites pass, use separate worktrees and subagents:

| Lane | First queued package | Parallelization |
|---|---|---|
| LOD/culling | LOD-01 pure tests, then LOD-02 traces | **`[SUBAGENT-PARALLEL]`** with VFS and temporal |
| VFS/mod loading | VFS-01 FilePS2 RPC trace | **`[SUBAGENT-PARALLEL]`** with LOD and temporal |
| Temporal rendering | TEMP-01 frame IDs/pass capture/native fallback | **`[SUBAGENT-PARALLEL]`** with LOD and VFS |
| HDR/display | Linear-color and SDR-parity evidence | **`[READ-PARALLEL]`** initially; implementation waits on renderer contracts |

Within each lane, numbered tasks remain serial unless the plan explicitly
states otherwise. Vendor upscalers, MetalFX, HDR output, dynamic motion
vectors, and frame generation are not early parallel tasks.

### Deferred launcher backlog

- **Unlock All Campaign Levels** - requested as a future opt-in launcher
  setting. It is intentionally not part of the MPEG playback fix. Before
  implementation, identify the game's canonical campaign-unlock state and
  define whether the option overrides progression in memory, updates the
  active profile, or both. Keep the default off and cover disabled,
  enabled, and existing-save behavior.

## 8. Windows regression commands

Run from the implementation repository:

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

The focused PORT-01 gate is:

```powershell
tools\scripts\cmake_preset.ps1 -Preset win-clang-rel -Action build -Target kz_host_core
```

## 9. Privacy and repository rules

- Never commit the retail ISO, `SCUS_974.02`, function-map CSV, regeneration
  logs, or anything under the private generated tree.
- Do not copy private inputs into reports, prompts, issue bodies, or external
  services.
- Committed provenance may contain hashes, counts, bounds, and signature
  hashes only.
- BYOD tests stay separately labeled and disabled by default.
- Preserve the exact fixed hook addresses and validate them through
  `inventory_generated.py`; do not retarget hooks to make a test pass.
- Preserve the dirty worktree. Integrate existing changes instead of
  reverting files wholesale.
- Keep the GitHub origin private until repository cleanup and an explicit
  human decision to publish it. Agents must not change repository visibility.
- A different machine may fetch the committed implementation from the private
  `origin/master`. Retail content, private function maps, and generated sources
  remain excluded and must be provisioned separately on an authorized host.

## 10. Required end-of-session update

Before handing off, update the timestamp and append or revise the relevant
task row. Record:

```text
Task:
Owner/session:
Machine:
Status: not started | in progress | blocked | complete
Files changed:
Acceptance commands and exact results:
New facts or decisions:
Blocker and minimal reproducer:
Next command:
Safe parallel work:
```

Also update the master PRD when a milestone changes state. Do not mark a task
complete based only on compilation when its plan acceptance requires runtime
execution.

## 11. Latest session record

```text
Task: VU-01 portable interpreter
Owner/session: Copilot SDK implementation session, 2026-10-17
Machine: Windows x64, MSVC 19.44.35229.0 and clang-cl 23.1.3
Status: in progress - implementation and Windows conformance complete; native AppleClang ARM64 acceptance pending
Files changed: CMakeLists.txt; CMakePresets.json; ext/kzvu/portable/; ext/kzvu/test/ps2recomp_stubs.*; ext/kzvu/test/kzvu_test.cpp; README.md; ext/kzvu/README.md; docs/findings.md; ../plan.md; ../KillzoneRecomp_Master_PRD.md; HANDOFF.md
Acceptance commands and exact results: isolated KZ_PORTABLE_VU_ONLY configure/build/test passed; kzvu_interpreter_conformance 28/28 checks passed under MSVC and clang-cl; MSVC ISO-free 10/10; clang-cl ISO-free 10/10; dependency verification 28 passed, 0 warnings, 0 failures; generated inventory PASS; resume table PASS; git diff --check PASS
New facts or decisions: the existing PS2Recomp interpreter is the portable oracle; the gate compiles its three VU sources without PCSX2 microVU or game-derived code, and KZ_VU=builtin plus KZ_VU0=interp bypasses kzvu initialization in the runtime
Blocker and minimal reproducer: AppleClang ARM64 execution is unavailable on this Windows host; no implementation failure is known. Run the next commands natively rather than substituting cross-compilation or emulation.
Next command: cmake --preset macos-arm64 && cmake --build --preset macos-arm64 && ctest --preset macos-arm64 --output-on-failure -R '^kzvu_interpreter_'
Safe parallel work: ARM-01, MAC-01, and the human legal/distribution disposition; VU-02 remains blocked on native VU-01 acceptance
```

```text
Task: MAC-01 renderer-free host smoke
Owner/session: Copilot SDK implementation session, 2026-10-17
Machine: Windows x64, MSVC 19.44.35229.0 and clang-cl 23.1.3
Status: in progress - implementation and Windows smoke complete; native AppleClang ARM64 acceptance pending
Files changed: CMakeLists.txt; CMakePresets.json; config/dependencies.lock.json; host/CMakeLists.txt; host/src/core_macos.mm; host/test/core_test.cpp; host/test/window_smoke.cpp; host/test/audio_smoke.cpp; README.md; docs/findings.md; ../plan.md; ../KillzoneRecomp_Master_PRD.md; HANDOFF.md
Acceptance commands and exact results: focused clang-cl host suite 3/3; MSVC ISO-free 12/12; clang-cl ISO-free 12/12; dependency verification 31 passed, 0 warnings, 0 failures; generated inventory PASS; resume table PASS; git diff --check PASS
New facts or decisions: the macOS Phase 0 preset builds the checksum-locked SDL 3.4.16 source; Darwin mappings enforce W^X and explicitly invalidate the instruction cache; Cocoa metrics require the main thread; Windows audio automation uses SDL dummy while native macOS acceptance uses CoreAudio
Blocker and minimal reproducer: AppleClang ARM64 execution is unavailable on this Windows host. The committed source can now move through the private origin; no implementation failure is known.
Next command: cmake --preset macos-arm64 && cmake --build --preset macos-arm64 && ctest --preset macos-arm64 --output-on-failure -R '^kz_host_(window|audio)_smoke$'
Safe parallel work: ARM-01, native VU-01 acceptance, and the human legal/distribution disposition; GS-01 remains blocked on native MAC-01 acceptance and VU-02 remains blocked on native VU-01 acceptance
```

```text
Task: Windows boot, movie, and menu restoration
Owner/session: Copilot SDK implementation session, 2026-10-17
Machine: Windows x64, NVIDIA GeForce RTX 5070 driver 32.0.16.1742, MSVC 19.44.35229.0, and clang-cl 23.1.3
Status: complete for guest boot and D3D12 visual acceptance; D3D11 remains a reproducible backend/driver-specific black-output issue
Files changed: config/killzone.toml; config/generated-manifest.json; tools/scripts/normalize_function_map.py; tools/scripts/find_code_pointers.py; tools/scripts/regen.py; tools/scripts/kz_generated_manifest.py; tools/scripts/inventory_generated.py; src/kz_hook_addresses.h; src/kz_overrides.cpp; src/kz_main.cpp; src/kz_launcher.cpp; README.md; docs/findings.md; HANDOFF.md; private work/functions.csv metadata and generated outputs
Acceptance commands and exact results: deterministic regeneration produced 31,998 files with 0 changed/new and 0 removed; resume table PASS with 132,656 dispatch entries, 100,661 resume cases, and 31,995 targets; generated inventory PASS with 42 unique hook signatures; strict dependency and patch reconstruction 31 passed, 0 warnings, 0 failures; MSVC build and ISO-free 12/12 PASS; clang-cl build and ISO-free 12/12 PASS; D3D12 probes rendered decoded FMV frames at 35, 40, 45, and 50 seconds and reached the main menu at 75-80 seconds; the minimized probe kept physical MCSERV, completed host memory-card operations, and reported no MCSERV semaphore timeout; the rebuilt visible client opened a responsive Killzone game window and a 45-second capture contained the rendered Killzone logo rather than a black frame; git diff --check PASS; implementation committed as cc7fc9cb51b84afc1fbf2208f26497bf6681b460
New facts or decisions: upstream PS2Recomp was not the failed assumption. The private local function corpus omitted executable continuations, delay slots, generated resume PCs, and materialized callbacks. Schema-3 range normalization, fixed-point tail/resume closure, and narrow callback-boundary discovery restore global dispatchability. Explicit ELF plus ISO boot now remounts extracted disc files at cdrom0:. First-registration-wins also allowed overlapping generated owners to shadow three configured memory-card stubs and two MPEG stubs. Authoritative local bindings restore coherent host memory-card state. Killzone-specific MPEG wrappers set `pc=$ra` before invoking handlers because `sceMpegGetPicture` may suspend; without that contract, the saved context resumed at the handler entry and looped instead of returning to the guest caller. D3D11 and D3D12 receive the same valid GS stream, but only D3D12 produces nonblack output on this driver.
Blocker and minimal reproducer: D3D11 remains exactly black through a 90-second capture while D3D12 renders with the same executable, ELF, ISO, settings, and guest state. This does not block D3D12 play-testing. Native Apple Silicon acceptance remains unavailable on this Windows host.
Next command: continue the manual visual test in the running D3D12 window; after Windows play-testing, continue the native macOS acceptance commands in the VU-01 and MAC-01 records
Safe parallel work: bounded D3D11 backend investigation may proceed independently of native ARM-01, VU-01, and MAC-01 acceptance
```

```text
Task: MPEG playback cadence restoration
Owner/session: Copilot SDK implementation session, 2026-10-17
Machine: Windows x64, NVIDIA GeForce RTX 5070 driver 32.0.16.1742, MSVC 19.44.35229.0, and clang-cl 23.1.3
Status: complete for D3D12 intro, menu, and attract playback
Files changed: .gitmodules; config/dependencies.lock.json; ext/PS2Recomp/ps2xRuntime/src/lib/Kernel/Stubs/MPEG.cpp; ext/PS2Recomp/ps2xTest/src/ps2_runtime_expansion_tests.cpp; patches/README.md; patches/ps2recomp/0028-mpeg-nodata-prefetch.patch; patches/ps2recomp/combined.lock.diff; docs/findings.md; HANDOFF.md
Acceptance commands and exact results: cleaned MSVC and clang-cl runtime builds PASS; MSVC ISO-free 12/12 PASS; clang-cl ISO-free 12/12 PASS; strict dependency and 60-path canonical patch reconstruction 31 passed, 0 warnings, 0 failures; generated inventory PASS with 42 hook signatures; resume table PASS with 132,656 entries, 100,661 resume cases, and 31,995 targets; focused PS2Recomp low-water callback regression PASS; opening cinematic 30.3 image changes/s, animated menu 29.32/s, and idle attract reel 29.88/s; 380-second attract timeline exited 0; git diff --check PASS
New facts or decisions: the 29.97 fps source was repeatedly starved because nodata callbacks began only after the decoded queue emptied and Killzone normally supplied one or two pictures per callback. Self-fed movies now prefetch at a four-picture low-water mark into the existing eight-picture queue. Prefetch excludes sceCdSt streams and stops at EOF, sequence end, stream end, or decoder failure; the no-progress retry state prevents callback spinning. PS2Recomp commit 917b379aa76949dc4ce42cdc1eaa0d3313fc76eb is published on Tzeentchnet/PS2Recomp killzone, and parent implementation commit 24e9cfa341a70e92a8aa1c2d0be3685e5f4fd236 is published on origin/master.
Blocker and minimal reproducer: no D3D12 playback blocker remains. The broader standalone PS2Recomp test executable has unrelated existing failures and exits with an access violation after the MPEG tests; the new focused regression passes before that failure. D3D11 remains the separate known black-output issue on this driver.
Next command: visually inspect intro, menu, attract playback, and audio in the responsive D3D12 build left running; implement the deferred Unlock All Campaign Levels launcher option only as a separate scoped task
Safe parallel work: bounded D3D11 backend investigation and design research for the deferred campaign-unlock option may proceed independently of native ARM-01, VU-01, and MAC-01 acceptance
```
