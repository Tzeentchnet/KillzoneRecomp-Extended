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
- **Prefer A/B switches over theories.** `KZ_IPU=off` removes every movie (all opens fail, the same path the intro
  skip takes) and settled in one batch that WIN-06 is not caused by the movie/MPEG HLE path.

- **Page-protection watch for "who wrote this guest word":** `KZ_WIN06_WATCH=1` (`src/kz_win06_watch.cpp`) shows
  the pattern: read-only host pages, a vectored handler that snapshots the page and single-steps the store, a diff on
  the trap, and PDB symbols. Generated function names carry the guest address and their source comments the guest
  pc, so the log names the guest instruction. Retarget `kRanges` for another word; no regeneration needed.
- **A write attributed to `ps2CgWr*Slow` is a guest store above 32 MB that wrapped.** The slow path masks the address
  with `PS2_RAM_MASK`, so an out-of-range guest pointer silently overwrites low RAM. Check the host registers for
  the unmasked address (the watch prints those that alias the written offset).
- **`replaceFunction` hooks can check arguments, not results.** Generated functions often return through tail-jump
  fragments (`ctx->pc = target | 1`, followed by the call site), so `v0` is not final when the original returns to
  the hook.

## Gates and automation

- **WIN-06 gate:** `tools\scripts\win06_gate.ps1 -Runs 10 -Name <name> -Card work\win04_20261019_card -Iso <image>
  -Elf game\SCUS_974.02 [-EnvSet "A=1|B=2"]`. Each run gets a fresh copy of the card; a run fails on a non-zero exit,
  a guest `missing-target`, a host exception, a missing chapter 11 level file, or mixed capture sizes. A run takes
  about 1.8 minutes, so ten runs take about 18 minutes; run batches sequentially (parallel runs change timing).
- **Run gate scripts with absolute paths.** The PowerShell tool's working directory can drift into a submodule after
  a `cd` in another command; `tools\scripts\win06_gate.ps1` then fails with "module 'tools' could not be loaded".
- **A one-in-five failure needs ten runs per arm.** Three-run batches (as in the original WIN-06 table) cannot
  separate a 20% failure rate from zero.
