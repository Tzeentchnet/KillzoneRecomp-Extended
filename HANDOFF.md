# KillzoneRecomp session handoff

**Last updated:** 2026-10-22
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
Task: GFX-03 reinvestigation (prior continuation) + AUDIO-01 scheduling fix attempt, tried and reverted (this
  continuation) + preparing for a public test release
Owner/session: Claude Code, 2026-10-21/22, branch gfx03-reinvestigation (off master @ 7afddb1)
Machine: Windows 11 x64 (AMD Ryzen 7 9800X3D, 8C/16T); MSVC and clang-cl presets; NVIDIA RTX 5070; disc image at
  E:\Git\Killzone\Killzone (USA).iso
Status: GFX-03 part of this branch unchanged from the prior record below. This continuation investigated a
  human-reported audio cutout while running the branch's elevated per-class LOD sliders in Escape, pivoted (at the
  user's explicit request) to an AUDIO-01 scheduling fix using native-PC spare cores, validated it looked good
  through headless automated soaks, then a hands-on session caught it causing an interactive freeze (force-close
  required) - reproducing an already-documented rejection of IOP-thread priority elevation. The whole attempt
  (priority boost, RPC cycle-credit accounting, CPU-affinity reservation) is REVERTED, not just defaulted off:
  ext/PS2Recomp is back to the clean locked commit, src/kz_main.cpp's paired affinity helper is deleted. AUDIO-01
  is back to its exact pre-session state (the already-shipped 120 ms/320 ms buffer mitigation only). The full
  attempt is preserved as a diff, not applied anywhere, at patches/ps2recomp/experimental/audio-iop-cycle-credit-priority.diff.
  Also found and self-corrected a measurement methodology bug along the way (see Evidence) - a documented
  retraction, not a deleted mistake.
Files changed (this continuation, final state): docs/findings.md (+"GFX-03 x AUDIO-01 interaction", its retraction,
  and "AUDIO-01 scheduling fix" with its own hands-on-freeze finding and revert); TODO.md (AUDIO-01 entry extended
  and closed back out); patches/README.md (audio-iop-cycle-credit-priority.diff entry updated);
  patches/ps2recomp/experimental/audio-iop-cycle-credit-priority.diff (regenerated to match the final attempted
  code before revert); this file. ext/PS2Recomp and src/kz_main.cpp have NO net change versus before this
  continuation - every edit to them was reverted in the same continuation. work/audio_lod_check/ holds every run's
  config, script, logs and callback.csv (gitignored, not committed, kept for anyone continuing this locally).
Evidence: see docs/findings.md "GFX-03 x AUDIO-01 interaction" for the retracted noise finding (first A/B looked
  like elevated LOD caused 44 -> 249 audio-loss events; three repeats of the identical config later landed at 249,
  0 and 0 - pure run-to-run noise from an open-loop KZ_INPUT_SCRIPT drifting into different actual combat content
  each run), and "AUDIO-01 scheduling fix" for the fix attempt: a clean 6-run interleaved multi-trial using
  PS2X_IOP_THREAD_STATS=1's content-independent max-backlog-lag looked genuinely good (unfixed 160/542/742 ms max
  lag vs. fixed 150/18/18 ms, never worse and twice clearly better) - but headless automation does no real
  window/input-polling, so it never exercised the lock-contention pattern that live play does, and a hands-on
  session froze near the end of an Escape firefight with little combat/audio active, not an exception. That matches
  "highest priority locks interactive gameplay," already rejected earlier in AUDIO-01's history, closely enough
  that THREAD_PRIORITY_ABOVE_NORMAL is judged equally unsafe for this thread, not just the literal highest level.
Acceptance commands and exact results: full regression set from HANDOFF.md section 4 not yet run this continuation
  - run it before treating this branch as release-ready. Each source edit in this continuation was individually
  rebuilt and exit-0 verified (tools\scripts\build_ps2recomp.bat, tools\scripts\cmake_preset.ps1 -Preset
  win-msvc-rel -Action build) but the full iso-free test suites under both compilers were not re-run after the
  final revert.
Blocker and minimal reproducer: none outstanding from this continuation - the regression it found is reverted out,
  not parked behind a flag. The prior continuation's own open blocker (render distance 2x + per-class LOD >=4x
  corrupting guest memory) is untouched; see the record it replaced, preserved via git history.
Next command: run the full regression set (section 4) before pushing/releasing. If resuming AUDIO-01 later, do not
  retry thread-priority elevation for the IOP+SPU2 thread in any form - two independent attempts (highest, then
  above-normal) have both caused interactive lockups; look at restructuring the lock to be fair regardless of
  priority, or moving SPU2 mixing off the EE-shared-lock path instead. GFX-03's elevated per-class LOD sliders stay
  opt-in only pending a non-noisy answer to whether they worsen AUDIO-01 (currently unconfirmed either way, not
  cleared) - see TODO.md GFX-03 entry, unchanged by this continuation.
Safe parallel work: WPN-01 remains read-only and requires BYOD authorization.
```
