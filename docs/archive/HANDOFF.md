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
Task: WIN-06 write watch (side session of the 2026-10-20 session: TODO review and src audit; dependency lock repair; WIN-06 investigation; LEGAL-01 disposition)
Owner/session: Claude Code desktop sessions, 2026-10-20
Machine: Windows 11 x64; MSVC and clang-cl presets; D3D12 on NVIDIA GeForce RTX 5070
Status: lock repaired and regression set green; LEGAL-01 complete; WIN-06 in progress: the writer of the timer block is named (realloc of a garbage pointer wraps past 32 MB), the source of the garbage pointer is not
Files changed: src/kz_win06_watch.cpp and .h (new, KZ_WIN06_WATCH diagnostic); src/kz_hook_addresses.h and config/generated-manifest.json (4 debug hooks: HeapFree, HeapRealloc, HeapReallocC, UploadRecords); src/kz_main.cpp; config/dependencies.lock.json; patches/ps2recomp/combined.lock.diff; patches/ps2recomp/0030-vif1-mpg-address-wrap.patch; patches/ps2recomp/experimental/ee-di-interrupt-gate.diff; patches/README.md; tools/scripts/win06_gate.ps1; docs/findings.md; IMPLEMENTATION_PLAN.md; TODO.md; WORKDONE.md; HANDOFF.md; README.md; LEARNINGS.md (new)
Acceptance commands and exact results: verify_deps.py --strict PASS 31/0/0; verify_resume_table.py PASS; inventory_generated.py --check PASS (51 hook signatures, 101,715 resumable entries); MSVC ISO-free 12/12; clang-cl ISO-free 12/12; git diff --check PASS; WIN-06 gate 6/10, 5/10, 8/10, 6/10 with KZ_WIN06_WATCH=1 (win06_watch, _watch2, _watch3, _watch4); 4/10 with PS2X_IOP_THREAD=0 (win06_iopsync); earlier 8/10 with KZ_IPU=off and 8/10 with the experimental di/ei gate
New facts or decisions: The game calls realloc (wrapper 0x173660, ra 0x173694) with garbage pointers (0x8, 0x0D3A0C15); realloc's copy (0x1731A0-0x1731C4) takes a multi-MB size from the fake header, runs past 32 MB, and ps2CgWr128Slow masks it onto low RAM (destination 0x02517740 -> 0x517740), wiping the kernel area, main stack and libkernel's timer block. Other failures are other consumers of corrupt heap data (vtable call 0x3B4978, DMA packet builder 0x159D30, host GS palette crash). No host DMA/SIF/VIF/IPU write touched the timer block. Threaded IOP, movies and di/ei delivery are ruled out. src audit against upstream 5f07000 found one regression (no-disc path; fixed). The repository owner supplied the written LEGAL-01 disposition; origin remains private.
Blocker and minimal reproducer: tools\scripts\win06_gate.ps1 -Runs 10 -Name win06 -Card work\win04_20261019_card -Iso <image> -Elf game\SCUS_974.02 fails 2-5/10.
Next command: tools\scripts\win06_gate.ps1 -Runs 10 -Name win06_watch5 -Card work\win04_20261019_card -Iso <image> -Elf game\SCUS_974.02 -EnvSet "KZ_WIN06_WATCH=1|KZ_CRASH_TRACE=1" until a "BAD realloc wrapper" line names the caller of 0x173660; then watch that container's pointer field
Safe parallel work: GS palette-read bounds check (host crash in GSGetRGBA8AlphaMinMax); Apple Silicon VU-01/MAC-01/ARM-01; WIN-01; WIN-03; WIN-04 hands-on
```
