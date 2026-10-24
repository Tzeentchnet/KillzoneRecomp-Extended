# Upstream comparison and import plan

**Research date:** 2026-10-24

**Local baseline:** `KillzoneRecomp-Extended` at `ef6339a`

**Direct upstream reviewed:** [`drakolordx7/KillzoneRecomp`](https://github.com/drakolordx7/KillzoneRecomp) at
[`832c8ad`](https://github.com/drakolordx7/KillzoneRecomp/commit/832c8adb7b730c0b98c03516dc582d139606c1d8)
(`v0.1.3-alpha`)

**PS3 experiment reviewed:** [`maiconjsv/killzone_pc`](https://github.com/maiconjsv/killzone_pc) at
[`c851471`](https://github.com/maiconjsv/killzone_pc/commit/c851471796ea753fd8eb3d7427454de6bcc641ec)

## Decision

1. `drakolordx7/KillzoneRecomp` remains the only code-relevant upstream. It has
   several correctness and UX fixes worth adapting.
2. Do not merge or cherry-pick upstream wholesale. The local fork and upstream
   share `5f07000`, but the local branch is 9 commits ahead and upstream is 17
   commits ahead. The branches overlap in portability, GS, runtime override,
   launcher, input, packaging, and documentation code.
3. Port the SIF return-PC and log-durability fixes first. Both address failures
   that are still possible in the local implementation.
4. Port the input improvements as a second bounded change. Add skip-intro as a
   separate opt-in feature.
5. Treat ultrawide as an experimental post-Phase-0 feature, not a drop-in fix.
   It directly advances M4, but upstream has unresolved visual reports and did
   not qualify D3D12 or Vulkan.
6. Do not import code from `maiconjsv/killzone_pc`. It targets the PS3 version,
   is not reproducibly buildable from its public pins, and has not rendered a
   first game frame.

## Repository relationship

The direct upstream comparison is:

- Common base:
  [`5f070007`](https://github.com/drakolordx7/KillzoneRecomp/commit/5f0700079f9ac4e14799d66d01747dbcdcec55f6)
- Upstream range:
  [`v0.1.0-alpha...master`](https://github.com/drakolordx7/KillzoneRecomp/compare/v0.1.0-alpha...master)
- Local-only commits: 9
- Upstream-only commits: 17
- Upstream release reviewed:
  [`v0.1.3-alpha`](https://github.com/drakolordx7/KillzoneRecomp/releases/tag/v0.1.3-alpha)

A merge-tree probe reports textual conflicts in:

- `README.md`
- `docs/findings.md`
- `src/kz_aim.cpp`
- `src/kz_gs.cpp`
- `src/kz_gs.h`
- `src/kz_main.cpp`
- `src/kz_overrides.cpp`

Five more files were modified on both sides and would require semantic review
even though Git can auto-merge them: `ext/kzgs/include/kzgs.h`,
`ext/kzgs/src/kzgs.cpp`, `src/kz_config.cpp`, `src/kz_launcher.cpp`, and
`tools/scripts/package.ps1`.

The local fork's dependency lock, host platform boundary, hook manifest,
deterministic regeneration, D3D12 boot restoration, and FMV audio changes must
remain authoritative. Upstream did not change its PS2Recomp gitlink in the
reviewed 17-commit range.

## Import priorities

### P0: yield-safe SIF module bindings

**Source:** [`a94d24a`](https://github.com/drakolordx7/KillzoneRecomp/commit/a94d24a4160ad952584a6d5eb7e6a5aecb1c320b)

Upstream found that an address-bound `sceSifLoadModule` or
`sceSifStopModule` can be entered directly by the scheduler after a guest
yield. The generic runtime handler leaves `ctx->pc` unchanged, so the scheduler
can re-enter the same handler repeatedly until the IOP module arena is
exhausted. The upstream wrappers call the runtime handler and then set
`ctx->pc = $ra`.

The local fork is still exposed:

- [`src/kz_overrides.cpp`](../src/kz_overrides.cpp) binds both addresses through
  `bindAddressHandler`.
- The helper in
  [`game_overrides.cpp`](../ext/PS2Recomp/ps2xRuntime/src/lib/game_overrides.cpp)
  is only `replaceFunction(address, handler)`.
- The SIF handlers in
  [`RPC.cpp`](../ext/PS2Recomp/ps2xRuntime/src/lib/Kernel/Syscalls/RPC.cpp)
  set the return value but not the return PC.
- The older intermittent `[IOP] module arena exhausted` signature is already
  recorded in [`findings.md`](findings.md).

**Adaptation:**

1. Add local wrappers in `kz_sif_modules.cpp` using the locked
   `kz_hook::SifLoadModule` and `kz_hook::SifStopModule` symbols.
2. Move those two entries from the named binding table to the custom binding
   table. Their addresses are already in the hook manifest.
3. Do not cherry-pick the whole commit; it also adds 400+ lines of unrelated,
   memory-poking developer cheats.
4. Run repeated BYOD headless boots with
   `PS2X_STRESS_YIELD=2`, then rerun the normal Windows acceptance matrix.

This is the first recommended implementation.

### P0: crash/hang-safe log redirection

**Source:** logging hunk from
[`e97b034`](https://github.com/drakolordx7/KillzoneRecomp/commit/e97b034baae04869f583d0ef11e1d82ce7cd68d9)

Upstream fixed two concrete logging faults:

- `stderr` was opened through a second append handle, while `stdout` retained
  an independent file position and could overwrite appended error lines.
- MSVC treats `_IOLBF` as fully buffered, so up to 4 KiB of the most important
  pre-hang output could be lost.

The local host boundary currently has the same two-handle pattern in
[`core_win32.cpp`](../host/src/core_win32.cpp) and
[`core_macos.mm`](../host/src/core_macos.mm). It improves on upstream by
centralizing rotation and errors, but it does not yet duplicate one stream
descriptor or make both streams unbuffered.

**Adaptation:**

1. Keep the typed host API and error handling.
2. On Windows, duplicate the redirected `stdout` descriptor into `stderr` with
   `_dup2`; use `dup2` on POSIX.
3. Make both C streams unbuffered and keep `std::ios::sync_with_stdio(true)`.
4. Add an isolated child-process test that interleaves stdout/stderr, exits
   normally and abruptly, and verifies that no records are overwritten.

### P1: mouse scope and gamepad aim behavior

**Sources:**

- [`ceec827`](https://github.com/drakolordx7/KillzoneRecomp/commit/ceec827eb709fe458016c2a5363dd9bdcc0acd3f)
- [`29af12c`](https://github.com/drakolordx7/KillzoneRecomp/commit/29af12c9fde0d307569cd3b5bd3cb322b60b444f)
- [`8ce511b`](https://github.com/drakolordx7/KillzoneRecomp/commit/8ce511b58421e53a107b6f0fe50fe41d2ef8b059)

The series temporarily exposes aim mode 2 as mode 1 around the scope reticle
block for keyboard/mouse input, preventing snap-back and stick-style clamping.
The final ordering performs the swap before `ApplyLook` so a guest yield cannot
skip it. It restores the real mode at the next known call and includes a safety
restore at the next controller update.

The small `29af12c` fix is independently important: local `updateHoldAim`
currently applies hold-to-aim reconciliation even when the active device is a
gamepad. That can pulse R3 off immediately after the player toggles zoom. The
existing prose in [`findings.md`](findings.md) says gamepad R3 remains a
toggle, but the current code does not enforce that condition.

**Adaptation:**

- Apply the keyboard/mouse device guard first.
- Port the scope sequence manually into the locally modified
  [`kz_aim.cpp`](../src/kz_aim.cpp).
- Add the `0x00218710` restore hook through
  [`kz_hook_addresses.h`](../src/kz_hook_addresses.h) and
  [`generated-manifest.json`](../config/generated-manifest.json), not as a
  source literal.
- Preserve the local time-fix wrapper ordering around `ApplyLook`.
- Test mouse hold/release, gamepad toggle, forced yields, and a scope weapon.

### P1: PS2-like controller stick range

**Source:** [`03c81ae`](https://github.com/drakolordx7/KillzoneRecomp/commit/03c81aeab41133d717f480bca90a3fbfc03ab857)

Upstream replaces per-axis dead zones with a radial dead zone, applies a
configurable range (default 1.33), and clamps each axis. This lets a modern
circular stick reach the square-ish DualShock 2 diagonal range; Killzone's
cubic stick response otherwise makes diagonal movement and turning much too
slow.

This is a small, high-value manual port into
[`kz_input.cpp`](../src/kz_input.cpp). Add pure input self-tests for center,
dead-zone boundary, cardinal full scale, diagonal full scale, and output
clamping before changing the default.

### P1: skip only the boot intro

**Sources:**

- [`195d05e`](https://github.com/drakolordx7/KillzoneRecomp/commit/195d05e7c1a026407bac543b54651e9b1b91694d)
- [`3418065`](https://github.com/drakolordx7/KillzoneRecomp/commit/34180652e08c7e91d6cf9e66479e3927c7150802)

The feature intercepts only `Movies/Intro/Intro`; logos still appear, while
story movies, menu backgrounds, and attract playback remain unchanged. It is
default-off and useful for both players and the test loop.

Port it as an isolated feature. Add `0x0026ECC8` to the locked hook header and
manifest, then verify disabled parity, enabled boot time, story FMV playback,
menu movies, and the attract loop.

### P2: optional toggle sprint

**Source:** [`b710b1d`](https://github.com/drakolordx7/KillzoneRecomp/commit/b710b1d4f93d1eba83642a50ecf48500c61fa4cd)

The option latches L3 and releases it on the next press or after 300 ms without
movement. Original hold-to-sprint remains the default. This is a reasonable
accessibility feature after the correctness-oriented input work, with unit
tests for latch, second-press release, idle release, menus, device switching,
and disabled parity.

### P2: ultrawide, after Phase 0

**Sources:**

- [`29e46a1`](https://github.com/drakolordx7/KillzoneRecomp/commit/29e46a1078673cceca9f3c9ddc87352ba36abcec)
- [`8ce511b`](https://github.com/drakolordx7/KillzoneRecomp/commit/8ce511b58421e53a107b6f0fe50fe41d2ef8b059)
- [`e97b034`](https://github.com/drakolordx7/KillzoneRecomp/commit/e97b034baae04869f583d0ef11e1d82ce7cd68d9)

The implementation provides Hor+ world rendering, a central 16:9 HUD,
pillarboxed menus and movies, match-window/21:9/32:9 modes, and deferred
restoration when a hooked guest function yields.

This is useful evidence for the ergonomics lane, but it is not ready to merge:

- Upstream reports D3D12 and Vulkan as untested.
- Local D3D12 is the only proven renderer on the current machine.
- Open upstream
  [issue #13](https://github.com/drakolordx7/KillzoneRecomp/issues/13)
  reports residual skybox/distant-texture flicker.
- The local GS API now uses `kz::host::NativeWindow`, producing direct merge
  conflicts in `kz_gs.*` and semantic overlap in `kzgs`.
- The upstream implementation contains several direct guest addresses. The
  local inventory gate rejects hook literals that bypass
  [`kz_hook_addresses.h`](../src/kz_hook_addresses.h).
- The local plan explicitly requires projection/HUD discovery and
  16:9/21:9/32:9 qualification before claiming M4.

Use a separate branch after Phase 0. Lock every hook signature, adapt the GS
frame-aspect flag to the current native-window API, and add synthetic plus BYOD
coverage for geometry, HUD, text, menus, FMV, scopes, skyboxes, distant
impostors, resize, and all three Windows renderers.

### P2: release packaging concept

**Source:** [`7741e04`](https://github.com/drakolordx7/KillzoneRecomp/commit/7741e04e8804c0d466ce0078da15beca2cbfcdd8)

App-local VC++ runtime DLLs and a versioned player README are useful. The
upstream script itself must not be imported: it hard-codes
`D:\KillzoneRecomp`, Visual Studio 18 Community, and 7-Zip paths and does not
participate in the local dependency lock or release-compliance gates.

Extend the existing relocatable [`package.ps1`](../tools/scripts/package.ps1)
instead. Discover the selected toolchain's redistributable directory, fail
explicitly when required DLLs are absent, lock provenance, produce notices and
an SBOM, audit the staged tree for protected content, and test installation on
a clean machine.

## Upstream material not to import

- `kz_devtools` cheats from `a94d24a`: useful for upstream's private test
  workflow, but mixed with direct memory writes and not needed for the SIF fix.
- `.weaver` scripts verbatim: several hard-code paths, saves, build folders,
  and local assumptions. Reuse test ideas through the existing relocatable
  scripts.
- README/status/changelog commits: they describe upstream's release, not this
  fork.
- Old performance patches or the upstream PS2Recomp patch stack: the local fork
  already contains the performance work and then adds dependency locking,
  portability, dispatch closure, boot restoration, and FMV fixes. Upstream
  also states that its patch sequence has not been replay-tested cleanly.
- The unmerged present-drop experiment for external frame caps: upstream
  reports only limited validation. Track
  [issue #9](https://github.com/drakolordx7/KillzoneRecomp/issues/9) instead.

Other upstream issues that should remain in the regression watch list are the
[level-two lockup](https://github.com/drakolordx7/KillzoneRecomp/issues/4),
[enemy-audio loss](https://github.com/drakolordx7/KillzoneRecomp/issues/17),
and [non-AVX2 support](https://github.com/drakolordx7/KillzoneRecomp/issues/8).

## `killzone_pc` assessment

`maiconjsv/killzone_pc` is not another implementation of this PS2 port. It
targets Killzone HD for PS3:

| Area | This fork | `killzone_pc` |
|---|---|---|
| Guest CPU | MIPS EE | PPC64 PPU |
| Coprocessors | VU0/VU1 and IOP | SPUs/SPURS/wws_job |
| Graphics | PS2 GS through PCSX2-derived `kzgs` | PS3 RSX/GCM to Vulkan |
| Audio | SPU2 | PS3 `cell*` HLE |
| Current state | Boots, menus, first-mission testing | Vulkan clear only; no first game frame |

The reviewed PS3 repository has additional reproducibility blockers:

- It spins in `cellSpursGetWorkloadInfo(wid=2)` because idle SPU requests are
  not decremented; job barriers and interpreter completeness are subsequent
  blockers.
- Its public `ps3recomp` submodule points at an unavailable commit.
- `build.sh --run` requires an existing `out/EBOOT.functions.json` but does not
  invoke the documented lifting step that creates it.
- Its game-specific HLE stubs are mostly empty or TODO examples.
- Committed SPU lift logs still contain hundreds of unsupported instructions.
- Documentation refers to a nonexistent `main` branch, and the PSARC tool
  advertises an option its parser does not implement.

The repository-owned code is MIT-licensed, but architecture, guest ABI, HLE,
renderer commands, and generated output are incompatible. Its generic ideas
(separate generated-code flags, incremental lifting, flip-boundary
presentation, and memory-aware compile parallelism) already have more mature
PS2-specific equivalents here. Reassess only after it publishes a valid
submodule pin, a reproducible clean build, automated tests, and a first
rendered game frame.

## Build-review correction

The earlier [`compilation_review.md`](../../compilation_review.md) explicitly
states that it could not inspect the local build files. Its largest proposed
wins are already implemented:

- [`regen.py`](../tools/scripts/regen.py) generates into a temporary directory
  and copies only changed files.
- [`CMakeLists.txt`](../CMakeLists.txt) separates frequently edited port code
  into `kz_port`, enables generated-code unity batches, and configures a PCH.
- The checked MSVC/clang-cl presets, ISO-free CTest layer, renderer goldens,
  hook inventory, and deterministic regeneration provide a stronger local
  validation loop than either reviewed repository.

Keep measuring compile and link changes, but do not reimplement these items
from the speculative report.

## Recommended work packages

1. **UPSYNC-01 - SIF return contract**
   - Port only the two wrappers.
   - Validate strict dependencies, hook inventory, resume table, both ISO-free
     suites, and repeated `PS2X_STRESS_YIELD=2` BYOD boots.
2. **UPSYNC-02 - durable host log**
   - Fix descriptor ownership in both host backends.
   - Add an ISO-free child-process log integrity test.
3. **UPSYNC-03 - input correctness**
   - Gamepad aim guard, scope-yield sequence, and radial stick range.
   - Add pure tests before enabling the 1.33 default.
4. **UPSYNC-04 - skip intro**
   - Add the locked hook and default-off launcher/config integration.
   - Recheck the complete FMV timeline.
5. **UPSYNC-05 - accessibility**
   - Add opt-in toggle sprint with state-machine tests.
6. **UPSYNC-06 - ultrawide research branch**
   - Start only after the Phase 0 gates permit M4 work.
7. **UPSYNC-07 - release staging**
   - Adapt app-local runtime packaging only through the release/compliance
     lane.

### Implementation status (2026-10-24)

UPSYNC-01 through UPSYNC-05 are implemented on `test/upsync-01-05` as bounded
manual ports integrated with this fork's hook manifest, config, launcher, and
ISO-free test layer. The two new guest hooks use signatures computed from the
authorized local `SCUS_974.02`. Forced-yield validation also extended
UPSYNC-01's return contract to this fork's additional `sceSifInitRpc` and
memory-card address bindings. Runtime and full Windows acceptance results are
recorded in `HANDOFF.md`. UPSYNC-06 and UPSYNC-07 remain deferred exactly as
specified above.

No code from either external repository should be copied before the
corresponding package has tests, locked hook provenance where applicable, and
disabled/default behavior that preserves the current baseline.
