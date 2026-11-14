# Upstream contribution candidates

This document records issues, pull requests, and design discussions that may be worth opening with
[ran-j/PS2Recomp](https://github.com/ran-j/PS2Recomp) or
[PCSX2/pcsx2](https://github.com/PCSX2/pcsx2). It tracks both filed submissions and candidates that still need
evidence, design agreement, or independent human authorship.

The broad PS2Recomp, related-project, and PCSX2 source review was refreshed on 2026-11-13. A focused
scheduler-pacing and GPU-profiling review was also completed that day. The baselines are:

- PS2Recomp `main` at
  [`e5636981a1c9de368f6b71e819c3dc188dfedbf4`](https://github.com/ran-j/PS2Recomp/commit/e5636981a1c9de368f6b71e819c3dc188dfedbf4).
- PCSX2 `master` at
  [`0ab00c49b157be667a8a2f0bf4cda83f75a9198e`](https://github.com/PCSX2/pcsx2/commit/0ab00c49b157be667a8a2f0bf4cda83f75a9198e).
- The locked local PS2Recomp revision `bf7d2d5ca364130a166b35d6a6dd697dff1bc046`; the latest published
  [`killzone` branch is still `fbde3f0`](https://github.com/Tzeentchnet/PS2Recomp/commit/fbde3f0cbbdec560201bd1001919eda4c59376d4).
- The locked local PCSX2 revision `accbf0cd94a49d73130be4a17b1b3bee586de4af`; the published
  [PCSX2 upscaler-foundation branch](https://github.com/Tzeentchnet/pcsx2/tree/feature/kzgs-upscaler-foundation)
  still ends at `1fb920189c12766af55bdc9178619c0c66eb1a5e`.

Before filing, rebase or reproduce against the then-current upstream head. Avoid including retail data, generated
game code, screenshots, traces containing protected content, or paths to local disc images.

## Local dependency and ecosystem refresh (2026-11-13)

The first release-hygiene task is publication, not another merge. Both locked companion revisions are newer than
their public branches. A clean clone cannot reproduce the current tree until those exact revisions are published;
the dependency verifier therefore correctly reports both as unpublished.

| Area | Decision | Evidence and scope |
|---|---|---|
| PS2Recomp canonical base | Keep the current patch base for now | Canonical `main` is still `e5636981`. The local resume-point and floating-point work already represents two post-`75d729ce` behavior changes. Canonical SIF dispatch [`2c5fbb9`](https://github.com/ran-j/PS2Recomp/commit/2c5fbb9389e11dd95693385969490c9e8e6f57b4) and GIF IMAGE2 [`e5636981`](https://github.com/ran-j/PS2Recomp/commit/e5636981a1c9de368f6b71e819c3dc188dfedbf4) are now exact backports, with an additional local dispatcher regression; the fifth upstream commit only documents `.recomp.json`. Rebasing only to duplicate those changes would churn the 45-topic reconstruction series without adding behavior. |
| PCSX2 VU slice | Integrated selectively | Upstream commits [`62a79afff`](https://github.com/PCSX2/pcsx2/commit/62a79afff984dcf919fbb5ba8bdb5679d181ca57), [`854604401`](https://github.com/PCSX2/pcsx2/commit/8546044011687f1d4d38d307cbd1c3d8355a4383), and [`222285122`](https://github.com/PCSX2/pcsx2/commit/2222851226644208117edf28754c821765098374) correct 64-bit XGKICK timing, interpreter cycle tracking, and the `nextBlockCycles` width. They remain in local companion `accbf0cd9`; KzVU still passes all 1,776 JIT/interpreter/reference checks after the subsequent renderer backports. |
| Isolated PCSX2 GS fixes | Integrated selectively | Upstream commits [`3c8df07e8`](https://github.com/PCSX2/pcsx2/commit/3c8df07e8e367caef8386fd949d6ca44bf2f7ad0), [`d1662db34`](https://github.com/PCSX2/pcsx2/commit/d1662db34afa310d2665b85af98405f61e440911), and [`2b5e9df73`](https://github.com/PCSX2/pcsx2/commit/2b5e9df7364c5e4e5de98281469a584fefd63927) fix DX12/Vulkan merge feedback-copy extents, bound odd-width PSMT4 uploads, and provide the D3D12 Tier-0 render-pass fallback. D3D11, D3D12, Vulkan, and software golden tests pass in local companion `accbf0cd9`. |
| D3D12 per-stage GPU timing | Integrated as a local diagnostics extension | Companion `accbf0cd9` mirrors the Vulkan logical-range lifecycle across command-list rotation. Timestamp `EndQuery` is legal inside a D3D12 render pass; only `ResolveQueryData` is prohibited there, so all resolves remain at the existing command-list boundary. An ISO-free WARP test requires positive whole-frame, upload/draw, merge, and final-presentation samples. |
| Full PCSX2 GS refresh | Rebase on an isolated branch; do not cherry-pick broad renderer changes piecemeal | The companion starts from `646df006`, while current `master` is 180 commits ahead of that base: 73 touch `pcsx2/GS`, three touch the KzVU slice, and none touch the KzIPU or KzSPU2 source slices. A merge probe found nine content-conflict hunks in `GSRenderer.cpp`, `GSDeviceVK.cpp`, and `GSTextureVK.cpp`. The broader Vulkan render-pass/layout/feedback work is tightly coupled, so a coherent base refresh is safer than selecting additional renderer commits. |
| PCSX2 work still in review | Track, do not build on it yet | [PCSX2/pcsx2#15043](https://github.com/PCSX2/pcsx2/pull/15043) is a draft backend refactor and [PCSX2/pcsx2#14850](https://github.com/PCSX2/pcsx2/pull/14850) still proposes a broad TFX-shader unification. Re-evaluate them only after merge and after the current GS refresh is stable. |
| SDL3 controller rumble | Integrated as a guarded adaptation | ReSands' `resands` branch supplied the DS2-to-SDL motor mapping: the large analog motor drives SDL's low-frequency channel and the small digital motor drives the high-frequency channel. Local companion `bf7d2d5` additionally retains the six-byte libpad actuator alignment, forwards commands through a separate host callback, and stops output on focus loss, removal, shutdown, or a 500 ms stale-command lease. SDL calls stay on the window thread, failures are reported once per device, and the standalone suite passes 456/456. |
| Sampled EE guest-call profiling | Use as the opt-in EE attribution path | Hardnyx's [`834f4965`](https://github.com/Hardnyx/PS2Recomp/commit/834f4965d6e5f8cc49f0fddedbc73e8ba9ed02f9) is retained as local `77b15da`; follow-up `6b11b9b` isolates Windows clock APIs and `bf7d2d5` batches each report into one stderr insertion. Normal builds compile out the hooks; instrumented builds require `PS2X_EE_PROFILE=1`, and the current instrumented suite passes 456/456. The original multi-insertion reports reduced matched gameplay to 52.613-53.288 FPS with 32.368-35.508 ms p99 and 36 bunched intervals per arm. Batched reports recovered 59.885/59.914 FPS, 17.993-18.051 ms p99, and 0-2 bunched intervals, against three normal controls at 59.855-59.927 FPS, 17.182-17.276 ms p99, and 0-3 bunched intervals. Instrumented process CPU time remained inside the normal control range. Both 125-second arms emitted 63 bounded report batches without dropped keys, but every batch had `nested_calls=0`; the run qualifies outer scheduler dispatch attribution, not nested-call coverage. Prefer this non-unwinding path over `KZ_PROFILE`, but keep it opt-in. |
| New PS2Recomp correctness reports | Track upstream; no current Killzone regeneration | [ran-j/PS2Recomp#439](https://github.com/ran-j/PS2Recomp/issues/439) fixes VU0 macro random-register semantics and [ran-j/PS2Recomp#440](https://github.com/ran-j/PS2Recomp/issues/440) fixes newlib-compatible `rand()`. Searches of the available US and SCES-52004 generated trees found no affected VU random operations or generated `rand`/`srand` calls. Revisit after an upstream implementation or evidence from the unavailable SCES-52893 executable. |

### Related-project decisions

| Source | Decision | Reason |
|---|---|---|
| [Vnnc1/gow-native-pc](https://github.com/Vnnc1/gow-native-pc) | Adapt the fixed-step test concept only | Its test-only fixed 1/60-second gameplay step made repeated combat trajectories deterministic and is relevant to matched campaigns. The implementation uses God of War-specific delta-time hooks, so Killzone needs an independently identified and guarded boundary. |
| [Vnnc1/gow-native-pc frame-latency experiment](https://github.com/Vnnc1/gow-native-pc/commit/dd4562522d8c79679a90bc8077ce457253a7423b) | Do not adapt | The project's own RX 580 control made the two-frame default off after substantially worse GPU-bound p95/p99 pacing. Killzone's matched campaigns also found no recurring GS throttle or command-ring waits. |
| [Destinity13/TimeSplitters-2-Native-Recomp-Windows-Linux-SteamDeck `505c4f9`](https://github.com/Destinity13/TimeSplitters-2-Native-Recomp-Windows-Linux-SteamDeck/commit/505c4f9d82d0a44aff58ad369b57d8396ca20e40) | Do not copy the dynamic automatic-scale guard | It lowers resolution from aggregate FPS after fixed time thresholds without separating CPU, guest-scheduler, or GPU pressure. The existing balanced policy is deterministic and backed by direct GPU timestamps. Dynamic scaling would need GPU-time thresholds, hysteresis, recovery, and visible transition testing. |
| [w1284603830/PS2Recomp-Fixer](https://github.com/w1284603830/PS2Recomp-Fixer) | Reference individual mechanisms; never bulk-merge | Its series is based on `eb8688b`, combines about 17,000 added lines across runtime, renderer, game adapters, and generic tooling, and overlaps many Killzone patches. Runtime overlays and IOP message boxes may help other games, but Killzone has no current overlay or mailbox blocker. |
| [Hardnyx/PS2Recomp](https://github.com/Hardnyx/PS2Recomp) and [ReSands/PS2Recomp](https://github.com/ReSands/PS2Recomp) GS work | Do not integrate into KzGS | Their current scanout, scanline-mask, and measured CPU-rasterizer precision work targets PS2Recomp's native GS. Keep it as hardware-reference evidence; KzGS follows PCSX2's renderer instead. |
| [Korvonordico/shadow-of-rome-recompiled](https://github.com/Korvonordico/shadow-of-rome-recompiled) and other KzGS/KzVU adopters | Watch for independently validated shared fixes | These projects confirm that the PCSX2 GS plus microVU architecture generalizes, but their recent commits are launcher, game-patch, ReShade, and packaging changes rather than newer reusable renderer code. |

## PS2Recomp refresh (2026-11-11)

Canonical `main` is unchanged from the 2026-11-10 review. There is no pull request newer than #437. The two submitted
PRs have no review or check results yet, and the multi-image RFC has no maintainer response. The useful new activity
is therefore in fork branches, not canonical commits:

| Priority | Source | Decision | Reason |
|---:|---|---|---|
| 1 | [PR #436 follow-up `7035404`](https://github.com/Tzeentchnet/PS2Recomp/commit/70354045ef118b11452d16f835575efa359c816f) | Adapt into the Killzone fork | The pinned implementation treats `JALR $zero, $rs` as a call. The submitted follow-up correctly keeps that no-link form on the indirect-jump fallback path and adds a focused test. |
| 2 | [PR #437 implementation `9023b80`](https://github.com/Tzeentchnet/PS2Recomp/commit/9023b80d5cfb12881c76a1ef4277c8139dd7356d) | Adapt missing pieces | The pinned `4b3c2ab` handles VIF1 only. The submitted version also covers VIF0 and rejects incomplete payloads before changing micro memory. Preserve the fork's VIF1 identical-upload optimization and statistics while porting it. |
| 3 | [Hardnyx VIF1 streaming work](https://github.com/Hardnyx/PS2Recomp/commits/main/) | Prototype as a reconciled series | It retains split MPG, UNPACK, fixed-register, DIRECT, and I-bit state across DMA blocks with extensive tests. It is relevant, but its MPG commit clips instead of wrapping and writes partial payloads immediately, conflicting with #437's validated behavior. Do not cherry-pick it alone. |
| 4 | [AlenDavid handler-table discovery `813c01ed`](https://github.com/AlenDavid/PS2Recomp/commit/813c01edd8f0e37059e5fd0ad0870a23a869c255) | Evaluate against all three Killzone ELFs | The three-entry, fixed-stride heuristic could replace some explicit analyzer starts. Compare discovered starts, rejection counts, and generated coverage before adopting it; any new false function split is correctness-critical. |
| 5 | [IOP import decode cache `f42ad188`](https://github.com/batatinhafcbatatinha-pixel/PS2Recomp/commit/f42ad1887c4a5ff359cc0aaa214008f73e69ad94) | Benchmark, then consider | The cache revalidates both stub words and import-table magic and clears on reset/unload. It is small and general, but there is no Killzone profile showing import-stub scanning is material. |
| - | [IOMAN PR #326](https://github.com/ran-j/PS2Recomp/pull/326), mislabeled [PR #433](https://github.com/ran-j/PS2Recomp/pull/433), and broad [PR #426](https://github.com/ran-j/PS2Recomp/pull/426) | Defer | #326 and #433 compete for the same read-only host-file API. #426 combines that work with ISO9660, Android, CDVD, GS, VU, and diagnostics changes. Killzone has no current IOMAN blocker and already has a separate ISO/VFS path. |
| - | [BioRhythmic scheduler hold `c3dcb5ab`](https://github.com/BioRhythmicStudios/PS2Recomp/commit/c3dcb5ab4cb4971b94a18677bbde75034015b6c3) | Do not adapt as written | It adds an untested process-global counter with a manually balanced C API and game-thread-only assumptions. If re-entrant host hooks need this, use a scoped scheduler-owned hold with underflow and resume tests. |

The other newly active fork work is not a direct fit. Hardnyx's GS scanout series targets PS2Recomp's renderer rather
than KzGS. The broad GPU-GS, SPU2, and VU performance branches in
[batatinhafcbatatinha-pixel/PS2Recomp](https://github.com/batatinhafcbatatinha-pixel/PS2Recomp) and
[AlenDavid/PS2Recomp](https://github.com/AlenDavid/PS2Recomp) need isolated correctness tests and Killzone benchmarks,
not bulk merges. Android and OpenGL ES work in
[LTDCrissOK/PS2Recomp](https://github.com/LTDCrissOK/PS2Recomp) is outside the active Windows/KzGS path.

### Adaptation trial results (2026-11-11)

All five recommendations were adapted on the isolated
`test/recommended-adaptations-2026-11-11` PS2Recomp branch and exercised from the
`test/ps2recomp-adaptations-2026-11-11` port branch:

| Candidate | Trial decision | Measured result |
|---|---|---|
| JALR no-link handling | Accept | `JALR $zero, $rs` now retains the unresolved indirect-jump fallback while link-producing JALRs keep call semantics. The focused regression and the complete PS2Recomp suite pass. |
| Complete VIF MPG semantics | Accept | VIF0 now masks and wraps destinations and rejects incomplete payloads before modifying micro memory. Existing VIF1 wrapping, identical-upload suppression, and statistics are preserved. All four focused MPG tests pass. |
| Fragmented VIF1 streams | Accept the reconciled implementation | Partial opcode bytes and fixed-register, MPG, UNPACK, DIRECT/DIRECTHL, and I-bit payloads are retained atomically across calls; `FBRST.RST` cancels retained state. Existing PATH2 continuation remains integrated. All 19 focused VIF1 tests pass. |
| Fixed-stride handler tables | Accept for the two available executables; keep the third comparison pending | Positive and negative synthetic fixtures pass. Regenerating the US and SCES-52004 builds produced zero changed, added, or removed generated files, and their register tables and Ghidra function maps remained byte-identical. SCES-52893 remains untested because its ELF/media is unavailable locally. |
| IOP import cache validation | Accept the correctness hardening; do not claim a Killzone speedup | This fork already had an import cache. Cache hits now revalidate both stub words and import-table magic, while existing reset and module-range invalidation remain. All four IOP suites pass. A synthetic 1,024-stub benchmark measured 4,594 ns cold versus 7.98 ns cached (575.7x) and 4,847 ns cold versus 7.87 ns cached (615.9x); this isolates cache mechanics, not a game workload. |

Integration validation after the final retained-buffer optimization:

- The complete standalone PS2Recomp suite passes 450/450.
- The Windows port builds in an initialized MSVC environment and all 37 enabled ISO-free tests pass.
- Strict dependency reconstruction passes all 39 checks, including all 35 topic-patch checksums and the canonical
  base-to-target patch.
- US regeneration produced 31,998 generated files with no differences; SCES-52004 produced 32,015 with no
  differences.
- An accelerated SCES-52004 headless run using the supported `KZ_IPU=off` automation path reached `MainMenu` at
  8.4 seconds and remained stable through the 60-second run.

### Submission-readiness review (2026-11-11)

The three unfiled contribution candidates were replayed against canonical `main` at `e5636981`; the VIF1 candidate
was tested on top of PR #437 at `9023b80`. Focused duplicate searches found no matching open submission.

| Candidate | Readiness | Canonical-tree result | Submission plan |
|---|---|---|---|
| Fragmented VIF1 command streams | Not ready to file as-is | Cherry-picking local commits `2fb312f` and `debadfe` onto PR #437 conflicts in `ps2_memory.h`, `ps2_memory.cpp`, and `ps2_vif1_interpreter.cpp`; the expanded tests apply cleanly. The local implementation is coupled to the fork's existing PATH2 continuation state. | Wait for or explicitly stack on #437, then manually port only bounded VIFcode/payload retention. Coordinate with Hardnyx, whose active branch contains the original split-command work plus newer DMA/interrupt/PATH2 work and has already reverted an unsafe PACKED/REGLIST continuation scanner. Preserve original authorship and exclude that reverted scanner. |
| Fixed-stride handler-table discovery | Technically ready; coordinate authorship before filing | Local `b21ead1` applies cleanly to canonical `main`; the canonical suite passes 446/446. The fork suite passes 450/450, and US/SCES-52004 regeneration remains byte-identical. The local adaptation adds four rejection fixtures for internal labels and repeated struct fields to AlenDavid's original `813c01ed`. | Prefer that AlenDavid file the PR, or preserve `813c01ed` authorship and add the extra guards/tests as a separate follow-up commit. Disclose that SCES-52893 comparison is unavailable rather than treating it as a blocker. |
| VIF STCYCL zero fields | Not ready; prior proposal was incorrect | PCSX2 preserves `CL=0` and expands only `WL=0` to 256. Canonical PS2Recomp currently maps both zero fields to 1 in VIF0 and VIF1. The historical `fbde3f0` change instead expanded `CL=0` in two VIF1 helper paths, included unrelated DIRECT fixture repairs, and added no focused STCYCL regression. A reference-aligned two-channel experiment passed 441/447 tests: the new zero-field test and five existing UNPACK tests exposed missing VIF0 fill behavior, VIF1's zero-payload guard, and fixtures that rely on the current zero-to-one default. | Do not submit the historical patch. First define reset/default behavior, implement VIF0 and VIF1 fill cycles consistently, add explicit `CL=0` and `WL=0` regressions, and update legacy fixtures to issue the STCYCL state they require. |

Recommended order for these three:

1. Coordinate and submit the fixed-stride handler-table PR.
2. Rebase the bounded fragmented-VIF1 core after #437, in coordination with Hardnyx.
3. Keep STCYCL zero-field behavior in research until the reference-aligned model passes the complete suite.

## Filed submissions

| Date | Submission | Status | Validation |
|---|---|---|---|
| 2026-11-10 | [ran-j/PS2Recomp#436](https://github.com/ran-j/PS2Recomp/pull/436) - avoid broad fallback for unresolved JALR calls | Open; no review/checks as of 2026-11-11 | Windows Release, MSVC and ClangCL: 447/447 tests passed |
| 2026-11-10 | [ran-j/PS2Recomp#437](https://github.com/ran-j/PS2Recomp/pull/437) - wrap VIF MPG uploads within VU micro memory | Open; no review/checks as of 2026-11-11 | Windows Release, MSVC and ClangCL: 448/448 tests passed |
| 2026-11-10 | [ran-j/PS2Recomp#438](https://github.com/ran-j/PS2Recomp/issues/438) - RFC for multiple recompiled images in one executable | Open; no response as of 2026-11-11 | Design discussion backed by a tested downstream prototype |

## Recommended filing order

| Order | Repository | Form | Candidate | Status |
|---:|---|---|---|---|
| 1 | PCSX2 | Small PR | Store compute specialization-constant values | Blocked by PCSX2's LLM policy; requires independent human authorship |
| 2 | PS2Recomp | Small PR | Wrap VIF MPG uploads within VU micro memory | Filed as [ran-j/PS2Recomp#437](https://github.com/ran-j/PS2Recomp/pull/437) |
| 3 | PS2Recomp | Small analyzer PR | Discover fixed-stride handler tables | Technically ready; coordinate with AlenDavid and preserve original authorship |
| 4 | PS2Recomp | Focused runtime PR | Add opt-in interruptible high-resolution Windows waits | Extract the reusable waiter and tests from the fork; omit fork-only timeline events |
| 5 | PS2Recomp | PR for existing issue | Model all 32 VU integer/control-register slots | Existing issue has independent game evidence |
| 6 | PS2Recomp | Small PR | Limit unresolved JALR resume points to the return PC | Filed as [ran-j/PS2Recomp#436](https://github.com/ran-j/PS2Recomp/pull/436) |
| 7 | PS2Recomp | Stacked runtime PR | Preserve fragmented VIF1 command streams | Manually port after #437 and coordinate with Hardnyx |
| 8 | PCSX2 | Issue | Vulkan index-upload synchronization hazard | Reproduce on current PCSX2 `master` first |
| 9 | PS2Recomp | RFC issue | Namespaced output for multiple recompiled images | Filed as [ran-j/PS2Recomp#438](https://github.com/ran-j/PS2Recomp/issues/438) |
| 10 | PCSX2 | Feature issue | Add per-stage GPU timing to GS performance captures | Issue first; agree on stages, backend scope, and `PerformanceMetrics` integration |
| 11 | PCSX2 | RFC issue | Supported external presentation/integration hooks | Submit the design before splitting the companion branch |
| 12 | PS2Recomp | Issue | Fixed IOP RPC instruction budgets can truncate valid handlers | Needs a minimal non-Killzone reproduction |
| - | PS2Recomp | Research before PR | Model VIF STCYCL zero fields | Current proposal contradicts PCSX2 and breaks existing UNPACK coverage |

## PS2Recomp

### New pull-request candidates

#### VIF MPG uploads should wrap within VU micro memory

**Filed:** [ran-j/PS2Recomp#437](https://github.com/ran-j/PS2Recomp/pull/437) on 2026-11-10.

The canonical submission covers both 4 KiB VU0 and 16 KiB VU1 micro memory. It masks MPG destinations to the target
region, splits crossing payloads between the end and address zero, and validates the complete payload before changing
micro memory. This matches the address masking and split-write behavior in PCSX2's VIF implementation.

- Submitted implementation:
  [`9023b80`](https://github.com/Tzeentchnet/PS2Recomp/commit/9023b80d5cfb12881c76a1ef4277c8139dd7356d).
- Earlier downstream implementation:
  [`4b3c2ab`](https://github.com/Tzeentchnet/PS2Recomp/commit/4b3c2abb7ecbe0de3a2c55c2ee853ae6257b2606).
- Reconstructable patch:
  [`0031-vif1-mpg-address-wrap.patch`](../patches/ps2recomp/0031-vif1-mpg-address-wrap.patch).
- Duplicate search: no matching open PS2Recomp issue or PR was found for MPG address wrapping.
- Tests cover VIF0 and VIF1 crossing uploads, masked immediates `0x0200` and `0x0800`, incomplete payloads, and the
  pre-existing `NUM=0` behavior. The full Windows Release suite passed 448/448 tests under MSVC 19.44 and
  ClangCL 19.1.5.

**Downstream adaptation gap:** the pinned fork and
[`0031-vif1-mpg-address-wrap.patch`](../patches/ps2recomp/0031-vif1-mpg-address-wrap.patch) still contain the earlier
VIF1-only implementation. Port the submitted VIF0 behavior and complete-payload validation while retaining the
fork's VIF1 identical-upload suppression and counters.

Hardnyx's later
[`933a4b54`](https://github.com/Hardnyx/PS2Recomp/commit/933a4b54cb6912e5939f71c007bb9a5c02b3729b)
shows the orthogonal case where a valid MPG payload spans DMA packets. Its surrounding series also retains split
UNPACK, fixed-register, DIRECT, and I-bit state. A combined implementation should buffer or otherwise validate a
complete MPG command across packets, then apply #437's masked, wrapping upload atomically. Cherry-picking
`933a4b54` alone would regress wrapping and the incomplete-payload guarantee.

#### Unresolved JALR should register only its return PC as resumable

**Filed:** [ran-j/PS2Recomp#436](https://github.com/ran-j/PS2Recomp/pull/436) on 2026-11-10.

A `JALR` with a nonzero link destination is an indirect call. The caller needs a resume label at `pc + 8`, but
treating it like an unresolved local jump promotes broad portions of the caller to indirect fallback entries. In the
Killzone generated set this produced labels and switch cases throughout roughly 4,000 functions and inhibited
optimization across ordinary instructions. `JALR $zero, $rs` produces no return link and must remain on the unresolved
indirect-jump fallback path.

- Local change:
  [`0014-jalr-resume-only-return.patch`](../patches/ps2recomp/0014-jalr-resume-only-return.patch).
- Submitted implementation:
  [`7035404`](https://github.com/Tzeentchnet/PS2Recomp/commit/70354045ef118b11452d16f835575efa359c816f).
- Duplicate search: no matching open issue or PR was found.
- Related but distinct:
  [ran-j/PS2Recomp#260](https://github.com/ran-j/PS2Recomp/issues/260) concerns resuming inside an
  always-taken branch delay slot, and
  [ran-j/PS2Recomp#415](https://github.com/ran-j/PS2Recomp/pull/415) concerns a real delay slot located at the next
  function.

The canonical tests verify that `pc + 8` remains a resume entry and emitted label for link-producing JALRs, unrelated
caller instructions are not promoted, `JALR $zero` retains broad fallback without a return entry, and no jump-table
target is fabricated. The existing unresolved-`JR` fallback test remains unchanged. The full Windows Release suite
passed 447/447 tests under MSVC 19.44 and ClangCL 19.1.5.

**Downstream adaptation gap:** the pinned fork and
[`0014-jalr-resume-only-return.patch`](../patches/ps2recomp/0014-jalr-resume-only-return.patch) contain the first
submission commit but not follow-up [`7035404`](https://github.com/Tzeentchnet/PS2Recomp/commit/70354045ef118b11452d16f835575efa359c816f).
They currently classify every `SPECIAL_JALR`, including `rd == 0`, as a call. Port the `rd != 0` guard and its
no-link regression test, then refresh the reconstructable patch.

#### VIF STCYCL zero fields need a reference-aligned model

**Status:** not ready to file. The earlier `CL=0 means 256` proposal was incorrect.

PCSX2 retains the eight-bit `CL` value, including zero, while interpreting `WL=0` as 256 for UNPACK size and fill
decisions. Canonical PS2Recomp instead maps both zero fields to 1 in its VIF0 and VIF1 interpreters. The historical
[`fbde3f0`](https://github.com/Tzeentchnet/PS2Recomp/commit/fbde3f0cbbdec560201bd1001919eda4c59376d4)
change expanded `CL=0` to 256 in two later VIF1 helper/source-size paths, did not fix the primary VIF0/VIF1 loops,
and bundled unrelated DIRECT fixture corrections.

An isolated canonical-tree experiment preserved `CL=0`, expanded `WL=0` to 256, and added VIF0/VIF1 regressions.
It passed 441/447 tests. The six failures showed that a complete patch must also:

- Implement VIF0 fill cycles instead of dropping writes without source data.
- Permit VIF1 zero-source-byte UNPACK commands to perform row-fill writes.
- Make reset/default CYCLE behavior explicit.
- Update five existing UNPACK fixtures that rely implicitly on zero CYCLE behaving as `CL=1, WL=1`.

Do not submit `fbde3f0` or a two-line extraction. Revisit this as a dedicated semantic series with PCSX2-aligned
fixtures and full-suite coverage.

#### Optional test filtering

**Suggested title:** `Tests: support a PS2X_TEST_FILTER substring`

The local `MiniTest` runner can filter by suite, test, or `suite.test` through `PS2X_TEST_FILTER`. This is useful for
hardware-vector and runtime debugging, but it is low priority and should be a separate tooling PR with tests for no
matches and exact suite/test selection.

#### Opt-in interruptible high-resolution Windows waits

**Suggested title:** `Runtime: add opt-in interruptible high-resolution Windows waits`

The fork's [`0037` scheduler patch](../patches/ps2recomp/0037-Add-precise-interruptible-scheduler-waits.patch) adds
an `EeHostWait` abstraction backed on Windows by a high-resolution waitable timer and an auto-reset wake event.
Posted events, newly scheduled deadlines, stop requests, and wait failures retain the condition-variable fallback.
A notification generation counter closes the event-before-wait race. `PS2X_PRECISE_WAIT=1` keeps the behavior
opt-in, and timer creation or wait failure reports the error before permanently falling back for that scheduler.

This is general runtime work rather than a Killzone policy. Twelve focused interrupt/wait tests pass in both precise
and fallback modes. In two interleaved 60 Hz pairs over the same guest-time window, precise waits reduced deadline
wake-overshoot p95 from 1.350-1.357 ms to 0.486-0.490 ms and p99 from 1.470-1.473 ms to 0.511-0.513 ms. The evidence
and limitations are recorded in [the frame-pacing analysis](frame-generation.md#matched-scheduler-wait-ab-scus-97402-chapter-1).

Do not submit local commit `9e9e887` unchanged. It also encodes exact wake telemetry through the fork-only
`ps2tl` event stream, whose surrounding timeline implementation is absent from canonical PS2Recomp. Prepare a clean
branch from current canonical `main` containing only:

1. The reusable `EeHostWait` implementation and platform-neutral fallback.
2. The notification-generation/lost-wakeup protection.
3. Scheduler integration for pacing and idle/deadline waits.
4. The hermetic timeout, notification, stop, rearm, and fallback tests.
5. Minimal failure logging, without project-specific timeline IDs.

The relevant canonical scheduler files have not changed since the fork's locked base, so a focused port should be
tractable, but it still needs an upstream-tree build and complete test run. Duplicate searches on 2026-11-13 found
no issue or PR for a waitable timer, condition-variable wake precision, or wake overshoot.

This does not resolve [ran-j/PS2Recomp#423](https://github.com/ran-j/PS2Recomp/issues/423). That issue's root cause
is choosing timer-versus-VBlank order using host time instead of guest cycles. A more precise host wake may change
how often the race appears, but it cannot make that ordering deterministic. Reference #423 only as adjacent
scheduler evidence, and contribute exact wake telemetry there only after reproducing its test. The evidence-focused
structure of [ran-j/PS2Recomp#328](https://github.com/ran-j/PS2Recomp/pull/328) is a useful model for the PR.

### New issue or RFC candidates

#### Namespaced output for more than one recompiled image

**Filed:** [ran-j/PS2Recomp#438](https://github.com/ran-j/PS2Recomp/issues/438) on 2026-11-10.

Some games load a frontend, overlay, or secondary executable that must coexist with the main generated set. The fork
implements configurable symbol prefixes, registration-function names, and table-end bounds, with generator and
recompiler tests:

- Initial implementation:
  [`4301048`](https://github.com/Tzeentchnet/PS2Recomp/commit/4301048699d3565b1a906f0ac2ad6593d88817d1).
- Consolidated implementation:
  [`d042263`](https://github.com/Tzeentchnet/PS2Recomp/commit/d042263bad8f1c5a55feb368b078062aeb81eea7).

The RFC treats those commits as prototype evidence rather than an upstream-ready patch because the public
configuration names and registration contract affect every generated project. It asks maintainers to decide:

- Whether namespacing belongs in `.recomp.json` or a higher-level image manifest.
- Collision-free generated symbols, function tables, and registration entry points.
- Runtime dispatch between resident images and overlays.
- Compatibility with a single-image configuration.
- Whether image unload/replacement is in scope.

#### Fixed IOP RPC budgets can stop valid server work

**Suggested title:** `IOP RPC: fixed instruction budget can truncate valid server calls`

Killzone's sound driver legitimately frees a large batch of samples during level unload. The former two-million
instruction cap stopped the handler midway, leaked SPU allocations, and left its semaphore owned. The fork raises
the budget, makes it configurable through `PS2X_IOP_RPC_BUDGET`, and logs exhaustion:

- Local evidence and implementation:
  [`ed449b3`](https://github.com/Tzeentchnet/PS2Recomp/commit/ed449b360ae3ec79ce46564972ed75b7cd74d9af).

The upstream issue should ask whether the correct solution is cooperative scheduling, a cycle/time deadline, or an
explicitly configurable safety guard. A 400-million-instruction default is useful evidence but should not be
presented as the general solution. Produce a minimal synthetic RPC server or evidence from a second title before
filing.

#### Missing-target diagnostics

**Suggested title:** `RFC: opt-in guest-state dump for calls to missing generated functions`

[`87e0f3b`](https://github.com/Tzeentchnet/PS2Recomp/commit/87e0f3b2abd859232cea9d0fa805239975192d9a)
adds an opt-in missing-target dump with guest PC, registers, and a bounded stack walk. This is useful when analyzer
coverage is incomplete, but it is diagnostic policy rather than an emulation fix. Discuss the expected log format,
privacy, release-build cost, and integration with existing runtime error reporting before preparing a PR.

### Existing reports: contribute instead of opening duplicates

| Existing report | Recommended contribution |
|---|---|
| [ran-j/PS2Recomp#260](https://github.com/ran-j/PS2Recomp/issues/260) | Prepare a focused PR for the always-taken delay-slot resume path and coordinate it with #415. |
| [ran-j/PS2Recomp#262](https://github.com/ran-j/PS2Recomp/issues/262) | Implement the corrected root cause: model 32 VI/control-register slots. Do not upstream the fork's CMSAR0-only workaround as the general fix. |
| [ran-j/PS2Recomp#326](https://github.com/ran-j/PS2Recomp/pull/326), [#433](https://github.com/ran-j/PS2Recomp/pull/433), and [#426](https://github.com/ran-j/PS2Recomp/pull/426) | Reconcile the competing read-only IOMAN host-file APIs before contributing tests. #433's OpenGL title does not match its IOMAN diff, while #426 mixes the same work with a much larger Android/ISO/diagnostics branch. |
| [ran-j/PS2Recomp#393](https://github.com/ran-j/PS2Recomp/issues/393) | Add portable-VU1 tests if Killzone can provide a synthetic paired-instruction reproduction; do not open another `vf0` issue. |
| [ran-j/PS2Recomp#397](https://github.com/ran-j/PS2Recomp/issues/397), [#434](https://github.com/ran-j/PS2Recomp/issues/434), and [#435](https://github.com/ran-j/PS2Recomp/issues/435) | Contribute instruction tests or review; these already cover the known MMI translation findings. |
| [ran-j/PS2Recomp#410](https://github.com/ran-j/PS2Recomp/pull/410) | Compare the fork's first-fit IOP heap reuse in [`3838fa9`](https://github.com/Tzeentchnet/PS2Recomp/commit/3838fa941a15015e4df556f801747f97e91ada71) and contribute long-session fragmentation tests rather than filing a second allocator report. |
| [ran-j/PS2Recomp#413](https://github.com/ran-j/PS2Recomp/pull/413) | Exercise invocation stacks near kernel-reserved memory and contribute load-address boundary tests; do not propose a second stack-reservation mechanism. |
| [ran-j/PS2Recomp#415](https://github.com/ran-j/PS2Recomp/pull/415) | Coordinate next-function delay-slot coverage with #260 so both direct execution and scheduler resume use the decoded instruction exactly once. |
| [ran-j/PS2Recomp#417](https://github.com/ran-j/PS2Recomp/pull/417) | Contribute Killzone's CALL/RET chain evidence or tests from [`bd94dfc`](https://github.com/Tzeentchnet/PS2Recomp/commit/bd94dfc9984d8ef3fd8e7ea2b57931abe9635f98); upstream's repeated-state detection is preferable to another fixed cap. |
| [ran-j/PS2Recomp#418](https://github.com/ran-j/PS2Recomp/pull/418) | Add VU1 JR/JALR tests that mutate the source VI register in the paired instruction. This is separate from the EE unresolved-JALR resume-entry candidate above. |
| [ran-j/PS2Recomp#419](https://github.com/ran-j/PS2Recomp/pull/419) | Compare post-transfer MADR values with synthetic chained and normal DMA tests; do not retain a fork-only MADR policy if this PR covers it. |
| [ran-j/PS2Recomp#420](https://github.com/ran-j/PS2Recomp/pull/420) | Add provenance/build-script testing for nonzero generation failure; do not open a second false-success report. |
| [ran-j/PS2Recomp#423](https://github.com/ran-j/PS2Recomp/issues/423) | Add Killzone deterministic-trace or exact wake evidence only after reproducing the timer/VBlank ordering difference locally. Do not claim that the precise host-wait backend fixes its guest-cycle ordering root cause. |

Standalone-entry resume support, improved floating-point semantics, SIF syscall dispatch, and GIF IMAGE2 behavior are
already represented upstream by [#271](https://github.com/ran-j/PS2Recomp/pull/271),
[#272](https://github.com/ran-j/PS2Recomp/pull/272),
[#274](https://github.com/ran-j/PS2Recomp/pull/274), and
[#384](https://github.com/ran-j/PS2Recomp/pull/384). The local stack now carries exact backports of #274 and #384;
do not refile the older fork patches for those behaviors.

### Poor upstream candidates without more evidence

The MPEG, SDRDRV, input, and Killzone-specific HLE patches should remain in the fork unless another game or a
hardware/reference test demonstrates a general PS2Recomp defect. Performance patches for worker threads, frame
pipelining, generated locals, and game-specific budgets also need isolated benchmarks and a much smaller change
surface before upstream discussion.

## PCSX2

### Blocked pull-request candidate

#### Compute specialization constants record metadata but not values

**Suggested title:** `Vulkan: store compute specialization constant values`

**Submission status:** Not filed. PCSX2's
[pull-request template](https://github.com/PCSX2/pcsx2/blob/master/.github/PULL_REQUEST_TEMPLATE.md) requires AI-use
disclosure, and its
[LLM usage policy](https://pcsx2.net/docs/contributing/#large-language-model-llm-usage-policy) prohibits new
contributors from submitting AI-generated contribution content. `Tzeentchnet` has no prior PCSX2 pull requests, so
this fix must be independently authored, tested, explained, and submitted by a human contributor under that policy.

Current PCSX2 `master` still accepts `value` in
[`ComputePipelineBuilder::SetSpecializationValue`](https://github.com/PCSX2/pcsx2/blob/0ab00c49b157be667a8a2f0bf4cda83f75a9198e/pcsx2/GS/Renderers/Vulkan/VKBuilders.cpp)
but only appends the `VkSpecializationMapEntry` and increases `dataSize`; it does not store `value` in
`m_smap_constants`. A caller selecting two branches through a specialization constant can therefore receive the
default/old data. Street Recomp found the same defect while selecting EASU versus RCAS.

The defect is small and independent of KzGS. An independently prepared fix should add a builder-level test that sets
at least two nonzero values and inspects `VkSpecializationInfo::pData`, then stores each value at the map entry's
declared offset. No matching open PCSX2 issue or PR was found in the 2026-11-10 duplicate search.

### New issue candidates

#### Vulkan index upload triggers a synchronization-validation hazard

**Suggested title:** `Vulkan: index upload can be read by vkCmdDrawIndexed without a visible transfer-to-index barrier`

The KzGS synthetic scene reliably reports `SYNC-HAZARD-READ-AFTER-WRITE`: `vkCmdDrawIndexed` reads an index-buffer
range following `vkCmdCopyBuffer`. The warning reproduces with FSR disabled and on the clean pinned PCSX2 baseline,
so it is not caused by the upscaler hooks. See the recorded
[validation scope](testing.md#upscaler-validation-baseline-2026-11-10).

Do not file this yet. First:

1. Reproduce it on current PCSX2 `master`, not only the pinned 2026 dependency.
2. Reduce the KzGS synthetic scene to the smallest legal draw/upload sequence.
3. Capture the complete validation message, command-buffer sequence, buffer ranges, and queue-family ownership without
   including game data.
4. Confirm whether the upload allocator already emits a barrier whose range or stage/access masks are insufficient.

No matching open PCSX2 issue or PR was found for the exact validation message or copy/draw sequence.

#### Per-stage GPU timing in GS performance captures

**Suggested title:** `GS: expose per-stage GPU timings in performance captures`

PCSX2 already has asynchronous whole-frame GPU timing. The merged
[PCSX2/pcsx2#14814](https://github.com/PCSX2/pcsx2/pull/14814) added built-in GS performance captures and cleaned up
Vulkan/D3D12 timing, making `PerformanceMetrics` and GS dump replay the natural upstream integration points.
[PCSX2/pcsx2#14792](https://github.com/PCSX2/pcsx2/pull/14792) and
[PCSX2/pcsx2#14809](https://github.com/PCSX2/pcsx2/pull/14809) also show that query enablement and backend cost need
to remain explicit.

The companion proof of concept measures upload/draw, merge, presentation upscaling, sharpening, and final
presentation on Vulkan and D3D12. Results are collected asynchronously when command-buffer/list resources are reused;
logical stages survive command-buffer/list rollover, and nested stages are supported. Vulkan range starts use
top-of-pipe timestamps and range ends use bottom-of-pipe timestamps. D3D12 uses timestamp `EndQuery` markers for both
boundaries and defers `ResolveQueryData` until the command list's existing end. In the final Vulkan same-build control:

- Both arms produced 900 whole-frame samples with no screenshot readbacks or campaign problems.
- The enabled arm produced 900 samples for every expected stage; the separate sharpening stage was correctly absent
  because FSR1 RCAS ran inside presentation upscaling.
- Stage means summed to 99.88% of whole-frame mean GPU time.
- Enabling stage markers produced no measurable regression in that pair.
- A separate `VK_LAYER_KHRONOS_validation` run produced the expected samples with no VUID, validation warning, or
  validation error.

See [the Vulkan stage-timing validation](frame-generation.md#vulkan-stage-timing-validation) for the measurements
and caveats. They establish that the concept works; they do not define the correct upstream stage contract.

Open an issue before preparing code. The current implementation is not a direct PR candidate because it reports
through the companion-only `GSRenderTimingHooks`, begins upload/draw ranges in KzGS's command loop, and includes an
external FSR1 presentation boundary. The companion branch also contains substantial unrelated renderer integration,
while current PCSX2 `master` has newer Vulkan render-pass and synchronization changes. Do not cherry-pick local
commits `c789f3960`, `81511c2ee`, or `aec325fcb` into upstream.
Any issue text or later implementation must be independently written and verified under PCSX2's LLM policy described
above; this planning section is not submission text.

Ask maintainers to decide:

1. Which upstream-native stages are useful and stable enough to expose.
2. Whether a Vulkan-only first implementation is acceptable.
3. Whether results belong in `PerformanceMetrics`, GS dump output, the OSD, or more than one of those surfaces.
4. How unsupported stages and backends should be represented.
5. Whether timestamps should exist only during an explicit performance capture.

D3D12 render passes prohibit `ResolveQueryData`, but not timestamp `EndQuery`. The local implementation therefore
writes stage boundaries without ending the active pass, then resolves all whole-frame and stage queries together at
the command list's existing end. Upstream work should reimplement this directly on current `master`, verify it under
the D3D12 debug layer and built-in performance captures, and use built-in reporting rather than external callbacks.

#### Backend-independent final-present observation for embedders

**Suggested title:** `RFC: expose successful-present status and final-image observation to embedding hosts`

KzGS needs to distinguish a queued VSync from a frame that actually reached presentation, reset history on resize or
device loss, and capture the final presentation result without rerunning an upscaler. PCSX2 already has screenshot and
capture paths, but the static-library host needs a small backend-independent contract.

An RFC should define the use cases before code:

- Successful-present identity versus emulated VSync identity.
- Whether observation occurs before or after OSD/video-capture composition.
- Read-only GPU image access versus asynchronous readback.
- Resize, skipped-frame, device-loss, and duplicate-present semantics.
- D3D11, D3D12, Vulkan, Metal, and software-backend expectations.

The current companion branch is evidence that the contract is useful, not a ready monolithic PR.

### RFC and possible PR series: external presentation integration

The
[PCSX2 companion branch](https://github.com/Tzeentchnet/pcsx2/tree/feature/kzgs-upscaler-foundation)
adds no-op-by-default hooks used by KzGS. Submitting the branch wholesale would mix policy, Vulkan integration, temporal
observations, and review fixes. Start with an RFC, then split only the accepted pieces:

1. A common renderer hook before sharp-bilinear prescaling, with the original path retained on no-op/failure.
2. Pre-device Vulkan instance/device extension and feature negotiation.
3. An external-command bridge with explicit image layouts, cache invalidation, and completion synchronization.
4. Successful-present identity and resize-aware redraw behavior.
5. Optional draw/display-target observations, only if PCSX2 maintainers want a generic diagnostics interface.

The RFC should explicitly state that Killzone's [temporal qualification](findings.md#temporal-qualification-instrumentation-fail--no-go-2026-11-10)
failed: gameplay had no matched depth in 20/20 sampled frames and no trustworthy object motion, world-only jitter, or
UI/reactive masks. The hooks enable controlled experiments and spatial FSR1; they do not prove that temporal
reconstruction can work for arbitrary PS2 titles.

### Changes that should remain downstream

- Killzone camera, scene, reset, and temporal-validity metadata.
- The KzGS provider ABI and optional DLSS/XeSS packaging policy.
- FSR1 configuration names and launcher/runtime diagnostics.
- Balanced automatic scaling, the 60 FPS headroom preset, KzGS trace IDs, and the private-game campaign policy.
- Protected-content-safe capture metadata and project release allowlists.
- Companion-only guest versus optimized depth-test observations unless PCSX2 accepts a general diagnostics API.

Review fixes such as restoring a borrowed Vulkan `pNext` chain are necessary for the companion hook implementation,
but they do not describe bugs in unmodified PCSX2 and should not be filed independently.

## Submission checklist

For each upstream report:

1. Reproduce on the current upstream default branch and record the exact commit.
2. Search open and closed issues and PRs again.
3. Reduce the behavior to one issue or one independently reviewable PR.
4. Include an ISO-free synthetic test whenever possible.
5. State what was measured, what was inferred, and what was not tested.
6. Exclude retail binaries, generated game source, screenshots, raw GS/VU dumps, and local media paths.
7. Preserve upstream formatting, platform coverage, and license notices.
8. Run the upstream test suite on at least Windows/MSVC and Linux or a second available toolchain.
9. Link the downstream commit as supporting evidence, not as a request to merge the entire fork.
10. Update this document with the filed issue/PR URL and final disposition.
