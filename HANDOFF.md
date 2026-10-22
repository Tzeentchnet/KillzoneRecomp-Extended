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
Task: GFX-03 reinvestigation, then launcher exposure + grass-cap experiment
Owner/session: Claude Code, 2026-10-21/22, branch gfx03-reinvestigation (off master @ 7afddb1)
Machine: Windows 11 x64; MSVC and clang-cl presets; NVIDIA RTX 5070; disc image at E:\Git\Killzone\Killzone (USA).iso
Status: both parts complete; no WORKDONE change (GFX-03 itself is not reopened - see below). Part 1 replaced the
  withdrawn shared-context LOD mechanism with a per-class one (static/instanced/skinned LOD, visible-distance cull,
  grass), each passing independently up to 8x in chapter 5; RenderDistance=2 combined with any elevated class still
  corrupts guest memory, root cause not isolated. Part 2 (this continuation) shipped four of those per-class scales
  as real [Graphics] config keys + launcher sliders (1..2, default 1), with RenderDistance>1 hard-forcing all four
  back to 1 in both the config resolver and the live UI; separately investigated raising the grass instance cap
  (1500), which reproducibly crashes and was not pursued further.
Files changed (cumulative, both parts): src/kz_detail.{h,cpp} (new); src/kz_config.{h,cpp}; src/kz_launcher.cpp;
  src/kz_hook_addresses.h; src/kz_main.cpp; src/kz_post.cpp (character LOD probe moved into kz_detail.cpp);
  config/generated-manifest.json (+8 debug hooks: LodStreamRequest, GrassRender, LodStaticInstanced,
  LodSelectSimple, LodSelectBlended, AllocFailHandler, PacketFrameBegin, GrassRecordCapClamp); docs/findings.md;
  IMPLEMENTATION_PLAN.md; README.md; TODO.md; LEARNINGS.md; patches/README.md; HANDOFF.md;
  patches/ps2recomp/experimental/gfx03-host-write-rpc-diagnostics.diff (new, not applied; null-by-default
  host-write observer + in-flight offloaded-RPC receive-buffer registry on the locked PS2Recomp checkout, gated by
  PS2X_GUEST_RANGE_WRITE_OBSERVER so kz_detail.cpp degrades cleanly without it). The locked PS2Recomp submodule
  itself is clean (patch not applied in the committed tree). Committed as one commit on gfx03-reinvestigation
  (part 1); part 2 (this continuation) is uncommitted at hand-off - commit before anything else.
Evidence: see docs/findings.md "GFX-03 reinvestigation" (part 1: consumer table, streaming/grass/stack analysis,
  the render-distance-2x failure matrix, every mechanism ruled out) and "GFX-03 follow-up" (part 2: the new config
  keys' validation - self-tests, a resolver unit test, and a live boot of both the safe and unsafe combinations
  through an actual killzone.ini via KZ_CONFIG, plus a 45 s scripted chapter-5 gameplay pass; and the grass-cap
  crash, bisected to its counter-bias half specifically, with the buffer-resize half confirmed safe-but-inert alone).
Acceptance commands and exact results: win-clang-tests and win-msvc-tests builds PASS; ISO-free suites PASS
  13/13 each (MSVC and clang-cl); strict dependencies PASS 31/0/0; resume table PASS (132,656 dispatch entries,
  100,661 resume cases, 31,995 generated targets); generated inventory PASS (64 hook signatures,
  101,715 recompiler-resumable entries); full diff check PASS; product --selftest PASS 0 failures under both
  compilers (covers the new config/resolver/launcher-preset self-tests too).
Blocker and minimal reproducer: (part 1, unresolved) render distance 2x + per-class LOD >=4x on any tested class
  reliably corrupts guest memory within seconds of level load in chapter 5 (direct start + scripted input); root
  cause not found (a heap-corrupting rpcCopyToRdram write was observed, writer mechanism not identified). (Part 2,
  resolved as a no-go, not a blocker) raising the grass instance cap via a shared-counter bias crashes the EE
  scheduler; kept in kz_detail.cpp behind KZ_DETAIL_GRASSCAP purely as documented negative evidence, explicitly not
  for use.
Next command: if resumed on part 1's open question, apply the experimental patch and KZ_DETAIL_HEAPCHECK against a
  chapter-5 render2+LOD4 repro; walk forward from the next captured heap break to the write's caller, or compare
  against WIN-06's own allocator-corruption findings for a shared first-fault signature. If instead extending part
  2 toward GFX-03's own original acceptance bar, run the full BYOD/campaign matrix (all chapters, longer soaks,
  human visual review) on the four new sliders at 2x before treating that range as campaign-accepted, not just
  chapter-5-safe. If pursuing the grass cap further, either trace the walker's MMI record-write address computation
  fully, or attempt a literal ELF-level constant patch via a full generated-code regeneration (not attempted this
  session; the private generated tree is 32k files and large/risky to redo blind).
Safe parallel work: WPN-01 remains read-only and requires BYOD authorization. AUDIO-01 is paused where the
  2026-10-21 session left it (see git history for that record); this session did not touch it.
```
