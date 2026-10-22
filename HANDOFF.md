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
Task: AUDIO-01 localization and bounded host mitigation
Owner/session: Copilot SDK in VS Code, 2026-10-21
Machine: Windows 11 x64; MSVC and clang-cl presets; SDL/WASAPI default output device
Status: in progress. Paired hands-on captures localize the cuts to SPU2 ring starvation/catch-up on the host path. The committed 120/320 ms reserve/high-water mitigation reduces severity with no noticeable action latency but does not eliminate large combat dropouts.
Files changed for AUDIO-01: src/kz_audio.{h,cpp}; ext/kzspu2/{include/kzspu2.h,src/kzspu2.cpp,README.md}; tools/scripts/summarize_audio_diag.py; patches/ps2recomp/experimental/audio-iop-cycle-credit-priority.diff; README.md; TODO.md; docs/findings.md; LEARNINGS.md; patches/README.md; HANDOFF.md. Captures remain ignored under work/. The locked PS2Recomp submodule is clean.
Evidence: Escape baseline 184.04 s: 317,863 underrun frames, 254,851 skipped frames, 1,104 loss rows, many audible cuts. Hope 220.45 s: 161,858 / 166,077, 466 rows, a few cuts. SDL callbacks stayed near 10 ms with zero producer overflow, submit failure or diagnostic loss. A focused audible enemy-wave cut aligned with IOP backlog reaching 954.27 ms.
Mitigation / decisions: reserve 40 -> 120 ms and high water 150 -> 320 ms; firing/reload latency was not noticeable and cuts fell from many to a few. Synchronous IOP was worse. Highest worker priority locked gameplay. RPC cycle credit fixed 9.8-second IOP lead but not the dropout. The unaccepted above-normal-priority / cycle-credit candidate is preserved as an experimental patch only.
Acceptance commands and exact results: final MSVC and clang-cl builds PASS; ISO-free suites PASS 13/13 each; strict dependencies PASS 31/0/0; resume table PASS (132,656 dispatch entries, 100,661 resume cases, 31,995 generated targets); generated inventory PASS (56 hooks, 101,715 recompiler-resumable entries); full diff check PASS. Clang-cl reports only the existing unused-flag and MSVC CRT deprecation warnings.
Blocker and minimal reproducer: Escape enemy-wave combat still produces an audible large cut with the 120 ms mitigation. AUDIO-01 cannot move to WORKDONE without a stable zero-loss combat soak.
Next command: if resumed, apply `patches/ps2recomp/experimental/audio-iop-cycle-credit-priority.diff` in an isolated branch, add repeated interactive stability coverage, and reject any priority change that locks once.
Safe parallel work: WPN-01 remains read-only and requires BYOD authorization. AUDIO-01 is paused after this documented push.
```
