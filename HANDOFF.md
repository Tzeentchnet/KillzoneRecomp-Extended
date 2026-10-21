# KillzoneRecomp session handoff

**Last updated:** 2026-10-20
**Workspace:** `D:\Github` (also holds the superseded `plan.md` and `KillzoneRecomp_Master_PRD.md`, history only)
**Implementation repository:** `D:\Github\KillzoneRecomp-Extended`
**Branch / last substantive commit:** `master` / see `git log` (2026-10-20 session commits)
**PS2Recomp revision:** `93d19aaa4f933787df05eed348a8283aa249a22b` (`Tzeentchnet/PS2Recomp`, branch `killzone`)
**GitHub origin:** `Tzeentchnet/KillzoneRecomp-Extended`, private during cleanup

This file is the session protocol for people and agents: how to start, which rules apply, how to check for
regressions, and how to hand off. It does not hold task status. That lives in [`TODO.md`](TODO.md) and
[`WORKDONE.md`](WORKDONE.md).

## 1. Read order

1. [`IMPLEMENTATION_PLAN.md`](IMPLEMENTATION_PLAN.md) - product requirements, work-package definitions,
   dependencies, acceptance commands, stop conditions, release gate and sign-offs. Authoritative for scope.
2. [`TODO.md`](TODO.md) - what is open, on which machine, and the next command.
3. [`WORKDONE.md`](WORKDONE.md) - what is proven, with evidence. Authoritative for completion.
4. This file - session rules.
5. [`README.md`](README.md) - build, test and BYOD workflows.
6. [`LEARNINGS.md`](LEARNINGS.md) - lessons from earlier sessions (lock updates, A/B tools, gates).

Code and passing acceptance commands override prose. Fix any stale document in the same session.

## 2. Session start

```powershell
Set-Location D:\Github\KillzoneRecomp-Extended
git status --short --untracked-files=all
git submodule status --recursive
python .\tools\scripts\verify_deps.py --strict
```

Preserve unrelated working-tree changes; integrate them instead of resetting or cleaning.

## 3. Parallel work

Labels used in `TODO.md`:

- **`[SUBAGENT-PARALLEL]`** - may run concurrently in an isolated worktree or branch, one writer per file scope.
- **`[READ-PARALLEL]`** - read-only investigation in the shared worktree; the parent session owns all edits.
- **`[SERIAL]`** - waits on dependencies, or must be done by the integration owner.
- **`[HUMAN]`** - needs a human decision or authorization; never delegate it as an approval.

Writer subagents never share a working tree. The macOS branches all touch `CMakeLists.txt`, `CMakePresets.json`,
`README.md` and the status documents. Give each writer its own worktree and merge serially in one integration session.

Cross-compilation or emulation on Windows is not acceptance evidence for any AppleClang compile-and-run gate.

## 4. Windows regression set

Run from the repository root before handing off any change that can affect the build or runtime:

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

Expected today: 31/0/0 dependency checks, both generated gates PASS (55 hook signatures and 101,715
recompiler-resumable entries), 13/13 ISO-free under each compiler. WIN-06 has its own gate,
`tools\scripts\win06_gate.ps1` (about 18 minutes for ten runs; see `LEARNINGS.md`). The focused PORT-01 gate is
`tools\scripts\cmake_preset.ps1 -Preset win-clang-rel -Action build -Target kz_host_core`.

The recompiler is built separately: `tools\scripts\build_ps2recomp.bat` (output in `build\ps2recomp`). Rebuild it
before `regen.py` whenever `ext/PS2Recomp` changes.

## 5. Privacy and repository rules

- Never commit the retail ISO, `SCUS_974.02`, the function-map CSV, regeneration logs, `work/`, or anything under the
  private generated tree.
- Do not copy private inputs into reports, prompts, issue bodies or external services.
- Committed provenance may contain hashes, counts, bounds and signature hashes only.
- BYOD tests stay separately labeled and disabled by default.
- Preserve the fixed hook addresses and validate them with `inventory_generated.py`; never retarget a hook to make a
  test pass.
- Keep the GitHub origin private until a human decides to publish it. Agents must not change repository visibility.
- Another machine may fetch the committed implementation from the private `origin/master`. Retail content, private
  function maps and generated sources must be provisioned separately on an authorized host.

## 6. End-of-session update

Before handing off:

1. Update the task's entry in `TODO.md` (status, evidence so far, next command, blocker).
2. If the task passed its plan acceptance, move it to `WORKDONE.md` with commit, machine, commands and exact results.
3. Add any new evidence to `docs/findings.md` and any runtime patch rationale to `patches/README.md`.
4. Add any lesson that would have saved this session time to `LEARNINGS.md`.
5. Replace the session record below.
6. When a milestone changes state or a role signs off, update `IMPLEMENTATION_PLAN.md` (section 12, approval status).

Session record template:

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

## 7. Latest session record

```text
Task: Continue GFX-03 with a bounded character-LOD probe and gameplay-relative measurement
Owner/session: Copilot SDK in VS Code, 2026-10-20
Machine: Windows 11 x64; MSVC and clang-cl presets; D3D11 diagnostic captures on NVIDIA GeForce RTX 5070
Status: M0 and M1 remain complete. GFX-03 safety checkpoint 9007d9a remains the product baseline: elevated shared-context LOD is withdrawn and clamped to 1x. The corrected skinned-only probe attributed and crossed the corpse's authored threshold but failed the reversible same-process visual gate, so character LOD is a no-go for this milestone. No replacement launcher control or safety campaign was approved. STAB-01 remains deferred and release-blocking.
Files changed: README.md; TODO.md; IMPLEMENTATION_PLAN.md; docs/findings.md; LEARNINGS.md; HANDOFF.md; config/generated-manifest.json; src/kz_aim.cpp; src/kz_gs.{h,cpp}; src/kz_hook_addresses.h; src/kz_input.{h,cpp}; src/kz_post.cpp; tools/scripts/image_compare.py; tools/scripts/verify_capture_metadata.py. Protected images, cards, logs, metrics and JSONL remain under ignored work/.
Acceptance commands and exact results: `killzone.exe --selftest` PASS; `verify_resume_table.py` PASS (132,656 dispatch entries, 100,661 resume cases); generated inventory PASS (56 hooks); image/capture-metadata self-tests PASS; MSVC and clang-cl ISO-free 13/13; both compilers build; `git diff --check` PASS. The earlier 90,000-call / 44.03% probe result is withdrawn because it included static-resource callers. Corrected attribution found the corpse's three skinned parts at center `(-461.80,113.65,34.96)`, threshold 3.0: unzoomed metric 4.424 selects level 1; zoom metric 2.072 selects level 0. A radius-1 targeted 90-second 4x run changed 495/10,000 skinned selections and exited normally. Corpse ROI repeat 3.3385 versus probe 3.7330/6.5207 did not survive the same-process gate: activation 4.4227 versus baseline temporal 8.8006.
New facts or decisions: helper return `0x3F4F84` is shared by static and skinned outer callers. Correct skinned filters are simple `0x3F4EA0` + saved return `0x3FF3FC`, and blended `0x3F4F84` + saved return `0x3FF4F0`. The diagnostic can log resources, target a center/radius and bound a gameplay window; 1x always executes the original helper. `KZ_INPUT_SCRIPT=g...` and `KZ_SHOT_GUEST_FROM=g...` share the first local-player controller update; frame metadata records `gameplay_offset_vsync`. The product `LodScale` clamp remains 1x.
Blocker and minimal reproducer: STAB-01 remains deferred but blocks M4/release; its retained gate is tools\scripts\win06_gate.ps1 -Runs 10 -Name <name> -Card work\win04_20261019_card -Iso <image> -Elf game\SCUS_974.02. AUDIO-01 reproduces in first-mission action-heavy firefights: effects/dialogue truncate about 100 ms several times per fight.
Next command: keep character LOD closed unless offline mesh comparison proves a materially different authored pair. Continue render-distance, mipmap and texture-detail campaign qualification; do not run character-probe hands-on/campaign gates.
Safe parallel work: read-only WPN-01 discovery and AUDIO-01 instrumentation design may proceed independently. Do not reactivate STAB-01 without reprioritization.
```
