# KillzoneRecomp session handoff

**Last updated:** 2026-10-20
**Workspace:** `D:\Github` (also holds the superseded `plan.md` and `KillzoneRecomp_Master_PRD.md`, history only)
**Implementation repository:** `D:\Github\KillzoneRecomp-Extended`
**Branch / last substantive commit:** `master` / see `git log` (2026-10-20 session commits)
**PS2Recomp revision:** `b44b2dcba0fe5a247b6f42176dd41f36168b6565` (`Tzeentchnet/PS2Recomp`, branch `killzone`)
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

Expected today: 31/0/0 dependency checks, both generated gates PASS (51 hook signatures and 101,715
recompiler-resumable entries), 12/12 ISO-free under each compiler. WIN-06 has its own gate,
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
Task: PROFILE-01 default Shooterman isolated test profile complete; CFG-01 complete; STAB-01 remains active
Owner/session: Copilot SDK in VS Code, 2026-10-20
Machine: Windows 11 x64; MSVC and clang-cl presets; D3D12 on NVIDIA GeForce RTX 5070
Status: PROFILE-01 and CFG-01 are complete in WORKDONE. BASE-01 still needs hands-on acceptance. STAB-01 remains in progress and its diagnostic changes were preserved.
Files changed for PROFILE-01: src/kz_config.cpp and .h; src/kz_testing.cpp and .h; src/kz_main.cpp; src/kz_launcher.cpp; IMPLEMENTATION_PLAN.md; README.md; TODO.md; docs/findings.md; HANDOFF.md
Acceptance commands and exact results: verify_deps.py --strict PASS 31/0/0; verify_resume_table.py PASS (132,656/100,661/31,995); inventory_generated.py --check PASS (52 hook signatures, 101,715 resumable entries); MSVC ISO-free 13/13; clang-cl ISO-free 13/13; git diff --check PASS.
New facts or decisions: [Testing] DefaultProfile defaults to exact Shooterman and is consumed only when Start in reaches Create Profile with an explicit isolated card root. KZ_DEFAULT_PROFILE is the validated automation override. Explicit KZ_UNLOCK_ALL=0 suppresses convenience hooks for native-query testing. The empty-card run created exact Shooterman and reached chapter 1; the populated-card native run reached chapter 11 Hard with zero unlock-hook logs. Populated-test-card and normal-card hashes were unchanged. The generated card is retained only as an ignored local fixture under work/.
Blocker and minimal reproducer: PROFILE-01 has no blocker. STAB-01 still reproduces with tools\scripts\win06_gate.ps1 -Runs 10 -Name win06 -Card work\win04_20261019_card -Iso <image> -Elf game\SCUS_974.02.
Next command: complete the BASE-01 hands-on D3D12 checklist to unblock TEST-01, or continue STAB-01 with tools\scripts\win06_gate.ps1 -Runs 10 -Name win06_watch5 -Card work\win04_20261019_card -Iso <image> -Elf game\SCUS_974.02 -EnvSet "KZ_WIN06_WATCH=1|KZ_CRASH_TRACE=1".
Safe parallel work: WIN-01; WIN-03; BASE-01 hands-on
```
