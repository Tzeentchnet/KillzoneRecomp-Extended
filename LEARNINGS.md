# LEARNINGS - lessons for the next session

**Last updated:** 2026-10-20

Short, reusable lessons about working on this repository: what tripped a session up, which checks catch it, and
which tools answer a question fastest. Evidence and measurements live in [`docs/findings.md`](docs/findings.md); task
status lives in [`TODO.md`](TODO.md). Add a lesson when it would have saved a later session time; keep each one to a
few lines with a pointer to the evidence.

## Repository hygiene

- **A PS2Recomp gitlink bump is not done until the lock moves with it.** Commit `b00e576` moved `ext/PS2Recomp` to
  `b44b2dc` without updating `config/dependencies.lock.json`, the canonical `patches/ps2recomp/combined.lock.diff`
  or a topic patch, so `verify_deps.py --strict` failed on `master`. After any submodule commit: export the commit
  with `git format-patch -1` into `patches/ps2recomp/NNNN-*.patch`, regenerate the canonical diff with
  `git -C ext/PS2Recomp diff <base_commit> HEAD`, update `target_commit`, `target_tree`, the canonical sha256 and
  path count, the topic-patch list and the `ps2recomp` dependency revision, add a `patches/README.md` entry, then run
  the verifier. The canonical diff is plain `git diff` output and reproduces byte for byte.
- **`verify_deps.py --strict` also fails on a dirty submodule** ("tracked files have local modifications"). Keep
  runtime experiments in a branch or an `experimental/*.diff` under `patches/ps2recomp/`, not in the pinned checkout.

## Investigating runtime faults

- **The upstream oracle is `5f07000`** (`git diff 5f07000 HEAD -- src`). Every `src/` file not in that diff is
  byte-identical to the known-working upstream port; audit the diff first.
- **`work/` holds the run history** of earlier sessions (`win06_*`, `t_*`). Read a few `stdout.log`/`stderr.log`
  pairs before designing a new experiment; the failing signature and the exact environment are usually there.
- **Guest disassembly without Ghidra:** load `game/SCUS_974.02`'s PT_LOAD segments and disassemble with Python
  `capstone` (MIPS64 little endian); EE-only opcodes (`di`, `ei`, MMI) show as `.word`. `jal` callers of a function
  are found by searching for the word `0x0C000000 | (addr >> 2)`. Generated sources also carry the disassembly as
  comments, but only for addresses inside the function map; libkernel helpers between mapped functions are missing.
- **`ps2TraceGuestWrite` is a no-op** in this runtime revision (`ps2_runtime.h`), so write tracing needs a host
  hook (`replaceFunction`) or a runtime change; changing a header included by generated code rebuilds ~32k files.
  `ee_scheduler.h` and `Kernel/Stubs/*.cpp` are not included by generated code: editing them rebuilds in seconds.
- **The scheduler ignores `di`/`ei`.** `EeScheduler::run()` injects pending interrupt/SIF/stream invocations and
  performs time-slice and event-driven thread switches without looking at the running context's Status.EIE.
  Measured: such deliveries are rare in the WIN-06 workload (0-2 per run) and the observed ones were benign. Treat it
  as a known deviation; the experimental gate is `patches/ps2recomp/experimental/ee-di-interrupt-gate.diff`.
- **`sceMpegDelete` is guest code, not an HLE stub** (the bound libmpeg entries are listed in the ignored
  `work/killzone.merged.toml`), so runtime state keyed on "movie deleted" never fires for Killzone.
- **Prefer A/B switches over theories.** `KZ_IPU=off` removes every movie and showed that WIN-06 is not caused by
  MPEG/IPU decoding. Native skips after 2 or 30 seconds also failed, but a full-intro run later reproduced the same
  fault, so startup lifecycle was correlation rather than the common cause.
- **Baseline claims need the same ten-run gate as a fix.** Six clean full-intro runs were not enough to establish a
  zero-failure baseline; the next measured full-intro run failed after reaching chapter 11.

- **Page-protection watch for "who wrote this guest word":** `KZ_WIN06_WATCH=1` (`src/kz_win06_watch.cpp`) shows
  the pattern: read-only host pages, a vectored handler that snapshots the page and single-steps the store, a diff on
  the trap, and PDB symbols. Generated function names carry the guest address and their source comments the guest
  pc, so the log names the guest instruction. Add `KZ_WIN06_WATCH_RANGE=<start>-<end>` for one half-open guest range;
  no source edit or regeneration is needed.
- **A write attributed to `ps2CgWr*Slow` is a guest store above 32 MB that wrapped.** The slow path masks the address
  with `PS2_RAM_MASK`, so an out-of-range guest pointer silently overwrites low RAM. Check the host registers for
  the unmasked address (the watch prints those that alias the written offset).
- **Print native crash registers before guessing from a renderer stack.** In the GS palette crash, `r8d` at
  `GSGetRGBA8AlphaMinMax` was `0xFFFFFFC7` in both captured failures: an inverted byte-index range had wrapped to a
  roughly 4 GiB width. The pointer was not the bad argument. Keep the conservative range guard covered by
  `kzgs_palette_guard`.
- **`replaceFunction` hooks can check arguments, not results.** Generated functions often return through tail-jump
  fragments (`ctx->pc = target | 1`, followed by the call site), so `v0` is not final when the original returns to
  the hook.
- **A passing repeated-load run can still contain allocator corruption.** `win06_listhead2_01` completed while the
  watch recorded an invalid vector allocation and later free. Treat internal invariant reports as failures when
  evaluating a fix, and use a targeted return hook rather than a whole-RAM reference scan to preserve timing.
- **Re-run old renderer defects before changing code.** WIN-01's historical D3D11 all-zero output no longer
  reproduced on the same RTX 5070/driver: the hardware golden, actual swap-chain capture and original 90-second
  scenario all rendered. Close stale findings with current evidence instead of adding speculative backend workarounds.

## Gates and automation

- **WIN-06 gate:** `tools\scripts\win06_gate.ps1 -Runs 10 -Name <name> -Card work\win04_20261019_card -Iso <image>
  -Elf game\SCUS_974.02 [-EnvSet "A=1|B=2"]`. It pins D3D12 and gives each skipped-intro run a fresh card; a run fails
  on a non-zero exit, a guest `missing-target`, a host exception, a missing chapter 11 level file, or mixed capture
  sizes. The default is 35 seconds; run batches sequentially because parallel runs change timing.
- **Alternative code generators need their own dispatch closure.** For `PS2X_CODEGEN_DSP=0`, collect literal legacy
  tail targets, direct-dispatch targets, and `jr $ra` resume PCs; scanning only DSP tail helpers and switch labels
  leaves deterministic boot-time missing targets.
- **Run gate scripts with absolute paths.** The PowerShell tool's working directory can drift into a submodule after
  a `cd` in another command; `tools\scripts\win06_gate.ps1` then fails with "module 'tools' could not be loaded".
- **A one-in-five failure needs ten runs per arm.** Three-run batches (as in the original WIN-06 table) cannot
  separate a 20% failure rate from zero.
- **Run `ps2x_tests.exe` from `ext/PS2Recomp`.** Some code-generator tests read source fixtures relative to that
  directory. Use `PS2X_TEST_FILTER=<substring>` for focused diagnosis. The accepted broad result is 439/439, not a
  root-directory run with unrelated fixture-path failures.
- **Align captures on guest events, not wall time.** TEST-01 records guest-vsync frame IDs and fire-action edges in
  `capture.jsonl`. A scripted pulse can be absent while the game is not polling input; the comparer aligns nearest
  matching events and reports unmatched events instead of forcing ordinal matches or hiding them.
- **CAS must be measured at the swap chain.** PCSX2's presented-size snapshot includes aspect/crop/scaling but occurs
  before final Present-time CAS. GFX-01's D3D12 `EndPresent` hook captures the current back buffer before Present;
  internal/presented snapshots are not valid evidence that CAS is active or inactive.
- **Do not infer exact GS width from the scale selector.** `Upscale=0` chooses a whole multiplier from the 448-line
  output rule, but exact width comes from the active GS display: Killzone measured 512x448 while the synthetic kzgs
  scene is 640x448. Use completed-frame `kzgsGetResolutionInfo` telemetry, not `640 * scale`.
- **Large hidden overlapped windows are work-area-clamped.** The first 1080p/1440p/4K kzgs matrix silently became
  1920x941/2297x941. Switch the test HWND to a borderless popup for unconstrained dimensions, assert the physical
  swap-chain size, then restore the ordinary style before minimize/restore coverage.
- **A replacement wrapper cannot inspect a tail-dispatched guest return.** The original RenderZone wrapper called
  `FUN_00339C38` and immediately read zero output because the generated owner returned after dispatching continuation
  `0x339D30`. Hook finalized consumers instead: GFX-03 uses `0x1C1A2C` for far plane and call-site-gated `0x151180`
  for fog. Require logs with real source/transformed values before accepting any output hook.
- **Equal guest-vsync IDs do not guarantee equal gameplay scenes.** Cloned-card GFX-03 baseline repeats at identical
  frame IDs still changed 80.377% of pixels because front-end/loading/tutorial progress is host-timing-sensitive.
  Measure same-arm variance and use a deterministic state/trace before attributing live pixel deltas to a setting.
