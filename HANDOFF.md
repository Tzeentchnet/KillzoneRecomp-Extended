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

Expected today: 31/0/0 dependency checks, both generated gates PASS (54 hook signatures and 101,715
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
Task: Complete BASE-01, TEST-01, GFX-01 and GFX-02; begin GFX-03
Owner/session: Copilot SDK in VS Code, 2026-10-20
Machine: Windows 11 x64; MSVC and clang-cl presets; D3D12 on NVIDIA GeForce RTX 5070
Status: M0 and M1 are complete. BASE-01 passed hands-on with VSync selected as the baseline; TEST-01 emits deterministic path-free measurement metadata; GFX-01 has truthful D3D12 post-processing and exact pre-Present capture; GFX-02 has an explicit, legacy-compatible render/output contract. GFX-03 inventory is active. STAB-01 remains deferred and release-blocking.
Files changed: IMPLEMENTATION_PLAN.md; README.md; TODO.md; WORKDONE.md; docs/findings.md; LEARNINGS.md; HANDOFF.md; src/kz_config.{h,cpp}; src/kz_gs.{h,cpp}; src/kz_input.cpp; src/kz_launcher.cpp; src/kz_main.cpp; ext/kzgs public/shim/CMake/README sources and tests, including new src/kz_present12.cpp; tools/gs_replay/gs_replay.cpp; new tools/scripts/verify_capture_metadata.py and image_compare.py. Protected traces, images, cards and JSONL remain under ignored work/.
Acceptance commands and exact results: BASE hands-on passed profile/gameplay, Continue/load, front-end return, mouse timing, controller and clean exit; VSync removed visible tearing. TEST-01 repeated runs aligned four fire events within two guest-vsyncs and reported two unmatched events. GFX-01 four-frame A/Bs: unavailable D3D12 SMAA 0.000% changed; FXAA 8.611%; CAS 19.243%; sharp/smooth 32.816%; crop 65.555%. Full D3D12 integration PASS includes exact fixed-2x 720p/1080p/1440p/4K outputs, resize, minimize/restore, injected device loss, 2,600-upload ring stress, packet continuation and D3D11 restart. GFX-02 product auto-scale matrix exited 0 at all outputs: 1024x896->1280x720 (2x, 3 captures), 1536x1344->1920x1080 (3x, 3), 2048x1792->2560x1440 (4x, 4), 2560x2240->3840x2160 (5x, 3). Metadata/image self-tests PASS. verify_deps.py --strict PASS 31/0/0; resume table PASS 132,656/100,661/31,995; generated inventory PASS (54 hooks, 101,715 resumable entries); MSVC ISO-free 13/13; clang-cl ISO-free 13/13; git diff --check PASS.
New facts or decisions: SMAA is D3D11-only and D3D12 never substitutes FXAA. CAS evidence must come from the pre-Present swap-chain buffer. `Upscale=0` remains ceil(output height / 448), 1x-8x, resolved at renderer start/settings apply; resize changes output only. Exact render width is dynamic: Killzone uses a 512x448 active display while the synthetic kzgs scene uses 640x448, so completed-frame telemetry is authoritative. The executed presentation order is GS merge, AA, conditional sharp prescale, crop/aspect, CAS, final present filter.
Blocker and minimal reproducer: STAB-01 remains deferred but blocks M4/release; its retained gate is tools\scripts\win06_gate.ps1 -Runs 10 -Name <name> -Card work\win04_20261019_card -Iso <image> -Elf game\SCUS_974.02. AUDIO-01 reproduces in first-mission action-heavy firefights: effects/dialogue truncate about 100 ms several times per fight.
Next command: inventory GFX-03's RenderDistance/LodScale hook definitions, manifest signatures, launcher/config ranges, anisotropy, texture replacement and sharpening surfaces; then define independent bounded A/B arms before changing behavior.
Safe parallel work: read-only WPN-01 discovery and AUDIO-01 instrumentation design may proceed independently. Do not reactivate STAB-01 without reprioritization.
```
