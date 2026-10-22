# LEARNINGS - lessons for the next session

**Last updated:** 2026-10-22

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
- **The retired direct-start diagnostic needed the same ten-run gate as a fix.** Six clean full-intro runs were not
  enough to establish a zero-failure baseline; the next measured full-intro run failed after reaching chapter 11.

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
- **Never write diagnostic files from the SDL audio callback.** AUDIO-01 uses a bounded SPSC record queue and a
  background CSV writer; the callback only samples counters and publishes one fixed-size record. Record diagnostic
  queue loss explicitly, and reject incomplete captures before drawing timing conclusions.
- **A cumulative audio counter needs an event timeline before it suggests a fix.** The normal high-water path can
  discard roughly 100 ms in one callback, matching the reported symptom, but the same one-time skip appears in
  non-combat startup runs. Correlate a heard cutout to a narrow callback window and the raw pre-ring WAV before
  changing pacing or buffering.
- **Capture the callback boundary before enabling the raw WAV tap.** `KZ_AUDIO_DIAG` publishes fixed-size records to
  a background writer; `KZ_AUDIO_WAV` necessarily writes from the SPU2 producer thread. Use callback-only captures
  for continuity counts, then add the WAV in a targeted rerun if producer-side waveform evidence is still needed.
- **IOP lead and IOP backlog are separate audio failure modes.** Offloaded RPCs can double-count emulated time and
  grow lead by seconds, while a combat wave can still leave the worker nearly one second behind published EE time.
  Fixing lead alone did not eliminate callback starvation.
- **Do not promote thread priority from an automated audio win alone.** Highest IOP/SPU2 priority reduced backlog in
  headless gates but locked interactive Escape. Preserve such candidates under `patches/ps2recomp/experimental/`
  until a repeated interactive stability gate passes.

## Gates and automation

- **Retired WIN-06 diagnostic gate:** `tools\scripts\win06_gate.ps1 -Runs 10 -Name <name> -Card work\win04_20261019_card -Iso <image>
  -Elf game\SCUS_974.02 [-EnvSet "A=1|B=2"]`. It pins D3D12 and gives each skipped-intro run a fresh card; a run fails
  on a non-zero exit, a guest `missing-target`, a host exception, a missing chapter 11 level file, or mixed capture
  sizes. The default is 35 seconds; run batches sequentially because parallel runs change timing. This tool now
  qualifies only the direct-start diagnostic path. Supported all-map testing uses exact `Shooterman` and native
  campaign selection, so this gate is not part of release acceptance.
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
- **LOD's final context is in PS2 scratchpad.** Entry `0x1C2168` and continuation `0x1C21B0` both precede the final
  mutation; setter continuation `0x151E64` receives the computed value in f12 and writes context `0x70003A00+0x1AC`.
  Do not apply RDRAM pointer bounds to scratchpad addresses. At 4x, require a logged `1.436 -> 5.742` setter value.
- **Qualify elevated graphics controls together as well as alone.** Escape passed render distance 2x and LOD 4x
  independently in headless runs, but 2x/4x froze 2/3 runs and another mixed arm produced a guest missing-target.
  More importantly, the hands-on LOD-only retest still froze with buzzing audio. Interactive failure overrides the
  headless pass: elevated LOD is now unavailable and forced to 1x until a different bounded mechanism is proven.
- **Check TEX1 before blaming mipmaps.** The first gameplay trace had 130 TEX1 writes with MXL=0, so every mip mode
  was necessarily identical. A docking trace with 148 MXL=3 states changed only one of 25 dense frames in base-only
  mode. Record MXL/MMIN/K counts and exact affected frames before adding or evaluating a mip control.
- **Qualify texture controls with a four-way deterministic replay.** On the docking trace, anisotropy 0x/16x and PS2
  versus base-only mipmapping each have a positive visual case, and both remain active in the combined arm. Replaying
  the 0x baseline byte-identically separates real sampler changes from capture drift; interaction is expected where
  base-only changes anisotropic-sampler eligibility.
- **A cached PS2 depth target is not a temporal depth contract.** Loading held a stable `PSMZ24` target for 20 frames,
  but 20 consecutive gameplay frames had no depth target and reused the same GS address as color. Always correlate
  target type with the displayed framebuffer and actual scene before proposing a depth input.
- **The GS boundary is too late for motion reconstruction.** kzgs receives post-transform `XYZ` plus texture/color/fog
  attributes, not camera matrices, object identity or previous transforms. Global jitter would also move HUD and
  sprites. Do not label optical flow or draw matching as engine motion vectors.
- **Separate engine aspect from presentation aspect.** Killzone's 16:9 setting changes engine FOV/HUD through the
  native widescreen flag, while kzgs controls pillarbox/fill at presentation. A replay can prove presentation geometry
  but cannot turn a widescreen trace into true 4:3 engine evidence; use matched live arms for that.
- **Stretch is not ultrawide support.** At 3440x1440, 16:9 preserves geometry with pillarboxes; stretch fills the
  output and changes 96.938% of pixels by widening the whole frame. Label the distortion explicitly.
- **Keep VSync on in the accepted interactive baseline.** Ultrawide 4:3/16:9/scope testing remained correct with
  VSync on; turning it off still causes egregious tearing. The validated preset and fresh defaults now enable it.
- **Equal guest-vsync IDs do not guarantee equal gameplay scenes.** Cloned-card GFX-03 baseline repeats at identical
  frame IDs still changed 80.377% of pixels because front-end/loading/tutorial progress is host-timing-sensitive.
  Measure same-arm variance and use a deterministic state/trace before attributing live pixel deltas to a setting.
- **Anchor live comparisons to gameplay, not boot.** Prefix scripted input and guest capture starts with `g` and
  record `gameplay_offset_vsync`; the final GFX-03 pair aligned at relative delta zero despite an 18-vblank boot-clock
  difference. The anchor reduced mean repeat error from 13.2611 to 0.9629, but did not make the scene pixel exact.
- **A helper return address does not identify the resource class.** Point-to-AABB helper return `0x3F4F84` is shared
  by `+0x2C` static and `+0x30` skinned outer callers. The first character-probe count was mostly static resources.
  Inspect the selector's saved outer return: skinned paths are `0x3FF3FC` (simple) and `0x3FF4F0` (blended).
- **A close-up can hide an LOD effect by selecting level zero at baseline.** Rifle zoom made the corpse large and
  stationary but moved its metric from 4.424 to 2.072 across the authored threshold 3.0, selecting level zero before
  the probe. Attribute runtime bounds and thresholds first; then test unzoomed and require a reversible same-process
  delta above temporal noise.
- **Campaign gates need fresh cards and memory peaks, not screenshots alone.** The GFX-03 matrix clones one card per
  chapter/arm, checks chapter reachability, hook effects, frame shape and exceptions, and samples process memory every
  250 ms. Chapters 1/5/10 showed no consistent working-set growth at render distance 2x.
- **Fog is also an authored visibility boundary.** Render distance 2x is acceptable, but pushing farther reveals
  enemy spawns and low-poly backgrounds. Clamp the user/config/environment range to the visually safe maximum instead
  of treating low runtime cost as permission for an unbounded slider.
- **Fixed wall-time memory peaks require aligned game state.** Chapter 6 produced a false-looking 426.7/808.0 MiB
  comparison. Counting presented gameplay frames catches the real issue: repeated baseline missing-target stalls and
  one 2x host crash prevented either arm from completing an aligned duration. Do not attribute memory deltas until
  both arms pass the same state-relative gate.
- **A failing graphics checkpoint can belong to the baseline stability lane.** With render-distance hooks absent, the
  retained allocator diagnostics caught Chapter 6 cleanup freeing resource-record fields as heap chunks through the
  same cleanup chain already studied by STAB-01. Localize the baseline arm before blaming an elevated graphics
  setting, and do not claim a shared first writer merely because the downstream corruption class matches.
- **One shared context field with twenty readers is not one setting.** `ctx+0x1AC` fans out into static/instanced/
  skinned LOD selection, the mesh visible-distance cull, grass view distance, scope zoom and ~15 unclassified
  per-class gates. Scaling it to probe "LOD" conflates all of them; a call-site-gated hook on the shared AABB-distance
  helper (`0x2C8178`) plus per-selector threshold-table patching isolates each consumer without touching the others.
  `src/kz_detail.cpp` is the reusable per-class harness; by 2026-10-22 it also drives four real `[Graphics]` config
  keys directly (no env var needed for the shipped path), with `KZ_DETAIL_*` remaining as a diagnostic override.
- **A streamed LOD system degrades gracefully even when the selector is wrong.** Elevated per-class LOD distance
  (static/instanced/skinned/grass, up to 8x) passed independently because the draw path only ever shows the nearest
  *resident* level (`0x3FF318`/`0x3EF648`, residency check `0x189C08`); a selection the streamer hasn't loaded yet
  just keeps showing the old level while `0x189CB0` stamps it for background loading. Don't assume a wrong LOD
  selection is unsafe without checking whether the consumer actually requires the data to be present.
- **Rule out stack, heap, packet-buffer and known race mechanisms with sampled telemetry before calling a crash
  "allocator corruption" by resemblance alone.** Render distance 2x + elevated LOD reproduces WIN-06-like symptoms
  (stale-pointer indirect calls, the same `0x2E7B38` cleanup chain, the same streamer object address), but minimum
  stack pointer, heap used/free/top, DMA packet high-water and an in-flight offloaded-RPC registry all stayed normal
  in the failing runs this session measured. Matching downstream symptoms is not the same as a shared first cause;
  see WIN-06's own inability to pin one across 7-10 ten-run batches.
- **`ps2TraceGuestRangeWrite`/`ps2TraceGuestWrite` are real, if currently no-op, hook points.** Turning the range-write
  one into a null-by-default observer callback (`patches/ps2recomp/experimental/gfx03-host-write-rpc-diagnostics.diff`)
  cost one relaxed atomic load per host-side guest-RAM write and found a corrupting `rpcCopyToRdram` write within one
  session, instead of the page-protection watch's larger timing perturbation.
- **Capstone mis-decodes the R5900's own instruction extensions; trust the generated C++ over raw disassembly for
  EE-specific opcodes.** `mult $ac3, $v1, $a1` in capstone's output looked like a register-naming artifact; the
  generated code showed it is really EE's 3-operand `MULT` writing the low 32 bits of the product straight into
  `$v1` (rd), not just HI/LO. Re-deriving a mechanism from `generated/*.cpp` instead of hand-decoding raw words
  caught this before it caused a wrong implementation.
- **A clean caller-return-address check correctly isolates "the one true top-level call" among several recursive
  self-calls to the same function.** The grass walker (`0x3EE6F8`) recurses into itself at four internal call sites;
  gating a one-time side effect (seeding a shared counter) on `ra == 0x3EF258` (the *only* return address its actual
  outer caller uses; every recursive self-call returns elsewhere) fires exactly once per object per frame, not once
  per recursion level. Same pattern as the LOD selector hooks' caller-return gating, reused successfully here.
- **Bisect a crashing hook into independently-testable halves before concluding "unsafe" covers the whole idea.**
  A grass-cap raise needed both a buffer-size increase (one register at a clean dispatch-table entry, confirmed
  safe alone) and a shared-counter bias (to make the walker's unmodifiable hardcoded comparisons use the new
  ceiling). Splitting them behind a temporary env var in one extra build-and-run cycle cleanly isolated the crash to
  the bias half, instead of discarding the whole mechanism or, worse, shipping the untested combination.
- **A main-thread-stack VLA sized by `min(count, N)*record_size`, with the comparison baked as a raw immediate in
  at least two places, cannot be safely re-targeted by pre-seeding the counted field unless every place that field
  is read is confirmed to be *only* an increment-and-compare.** Pre-seeding grass's shared per-object record counter
  to retarget hardcoded `slti ..., 0x5DC` checks crashed reproducibly (a guest `DirectJump` to a garbage PC inside
  the EE scheduler, stack pointer offset consistent with the larger buffer being in use) — the field likely also
  feeds a write-address/index computation in the walker's vectorised (MMI) record-writing code that was not fully
  traced. Prefer a literal ELF-level constant patch (requires full generated-code regeneration) over a runtime bias
  trick for this class of problem; the regeneration was not attempted this session given the generated tree's size
  and fragility, so that alternative remains unverified too.
