# Upstream contribution candidates

This document records issues, pull requests, and design discussions that may be worth opening with
[ran-j/PS2Recomp](https://github.com/ran-j/PS2Recomp) or
[PCSX2/pcsx2](https://github.com/PCSX2/pcsx2). It tracks both filed submissions and candidates that still need
evidence, design agreement, or independent human authorship.

The duplicate check and source review were refreshed on 2026-11-10 against:

- PS2Recomp `main` at
  [`e5636981a1c9de368f6b71e819c3dc188dfedbf4`](https://github.com/ran-j/PS2Recomp/commit/e5636981a1c9de368f6b71e819c3dc188dfedbf4).
- PCSX2 `master` at
  [`c8081bf5d63b1b83b77112632e1113a25b81058c`](https://github.com/PCSX2/pcsx2/commit/c8081bf5d63b1b83b77112632e1113a25b81058c).
- The published
  [PS2Recomp `killzone` branch](https://github.com/Tzeentchnet/PS2Recomp/tree/killzone).
- The published
  [PCSX2 upscaler-foundation branch](https://github.com/Tzeentchnet/pcsx2/tree/feature/kzgs-upscaler-foundation).

Before filing, rebase or reproduce against the then-current upstream head. Avoid including retail data, generated
game code, screenshots, traces containing protected content, or paths to local disc images.

## Filed submissions

| Date | Submission | Status | Validation |
|---|---|---|---|
| 2026-11-10 | [ran-j/PS2Recomp#436](https://github.com/ran-j/PS2Recomp/pull/436) - avoid broad fallback for unresolved JALR calls | Open | Windows Release, MSVC and ClangCL: 446/446 tests passed |
| 2026-11-10 | [ran-j/PS2Recomp#437](https://github.com/ran-j/PS2Recomp/pull/437) - wrap VIF MPG uploads within VU micro memory | Open | Windows Release, MSVC and ClangCL: 448/448 tests passed |
| 2026-11-10 | [ran-j/PS2Recomp#438](https://github.com/ran-j/PS2Recomp/issues/438) - RFC for multiple recompiled images in one executable | Open | Design discussion backed by a tested downstream prototype |

## Recommended filing order

| Order | Repository | Form | Candidate | Status |
|---:|---|---|---|---|
| 1 | PCSX2 | Small PR | Store compute specialization-constant values | Blocked by PCSX2's LLM policy; requires independent human authorship |
| 2 | PS2Recomp | Small PR | Wrap VIF MPG uploads within VU micro memory | Filed as [ran-j/PS2Recomp#437](https://github.com/ran-j/PS2Recomp/pull/437) |
| 3 | PS2Recomp | PR for existing issue | Model all 32 VU integer/control-register slots | Existing issue has independent game evidence |
| 4 | PS2Recomp | Small PR | Limit unresolved JALR resume points to the return PC | Filed as [ran-j/PS2Recomp#436](https://github.com/ran-j/PS2Recomp/pull/436) |
| 5 | PCSX2 | Issue | Vulkan index-upload synchronization hazard | Reproduce on current PCSX2 `master` first |
| 6 | PS2Recomp | Small PR | Interpret VIF STCYCL `CL=0` as 256 | Extract from the mixed local test-restoration commit |
| 7 | PS2Recomp | RFC issue | Namespaced output for multiple recompiled images | Filed as [ran-j/PS2Recomp#438](https://github.com/ran-j/PS2Recomp/issues/438) |
| 8 | PCSX2 | RFC issue | Supported external presentation/integration hooks | Submit the design before splitting the companion branch |
| 9 | PS2Recomp | Issue | Fixed IOP RPC instruction budgets can truncate valid handlers | Needs a minimal non-Killzone reproduction |

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

#### Unresolved JALR should register only its return PC as resumable

**Filed:** [ran-j/PS2Recomp#436](https://github.com/ran-j/PS2Recomp/pull/436) on 2026-11-10.

An unresolved `JALR` is an indirect call. The caller needs a resume label at `pc + 8`, but treating it like an
unresolved local jump promotes broad portions of the caller to indirect fallback entries. In the Killzone generated
set this produced labels and switch cases throughout roughly 4,000 functions and inhibited optimization across
ordinary instructions.

- Local change:
  [`0014-jalr-resume-only-return.patch`](../patches/ps2recomp/0014-jalr-resume-only-return.patch).
- Submitted implementation:
  [`f07c61b`](https://github.com/Tzeentchnet/PS2Recomp/commit/f07c61be385400d7d8a40c0fa763bed34f9229c5).
- Duplicate search: no matching open issue or PR was found.
- Related but distinct:
  [ran-j/PS2Recomp#260](https://github.com/ran-j/PS2Recomp/issues/260) concerns resuming inside an
  always-taken branch delay slot, and
  [ran-j/PS2Recomp#415](https://github.com/ran-j/PS2Recomp/pull/415) concerns a real delay slot located at the next
  function.

The canonical test verifies that `pc + 8` remains a resume entry and emitted label, unrelated caller instructions are
not promoted, and no jump-table target is fabricated. The existing unresolved-`JR` fallback test remains unchanged.
The full Windows Release suite passed 446/446 tests under MSVC 19.44 and ClangCL 19.1.5.

#### VIF STCYCL `CL=0` should mean 256

**Suggested title:** `VIF: treat STCYCL CL=0 as 256 during UNPACK source accounting`

The local standalone test restoration found that `WL=0` was expanded to 256 while `CL=0` remained zero. Treating
both zero fields consistently as 256 fixes fill/source-vector accounting.

- Local source:
  [`fbde3f0`](https://github.com/Tzeentchnet/PS2Recomp/commit/fbde3f0cbbdec560201bd1001919eda4c59376d4).
- The commit is mixed; submit only the two STCYCL changes and their focused tests.
- Duplicate search: no matching open issue or PR was found.

This should not be submitted as “restore the standalone suite.” The same local commit contains unrelated audio
guards, JALR tests, test filtering, corrected GIF continuation fixtures, and MPEG/SIF fixture repairs.

#### Optional test filtering

**Suggested title:** `Tests: support a PS2X_TEST_FILTER substring`

The local `MiniTest` runner can filter by suite, test, or `suite.test` through `PS2X_TEST_FILTER`. This is useful for
hardware-vector and runtime debugging, but it is low priority and should be a separate tooling PR with tests for no
matches and exact suite/test selection.

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
| [ran-j/PS2Recomp#393](https://github.com/ran-j/PS2Recomp/issues/393) | Add portable-VU1 tests if Killzone can provide a synthetic paired-instruction reproduction; do not open another `vf0` issue. |
| [ran-j/PS2Recomp#397](https://github.com/ran-j/PS2Recomp/issues/397), [#434](https://github.com/ran-j/PS2Recomp/issues/434), and [#435](https://github.com/ran-j/PS2Recomp/issues/435) | Contribute instruction tests or review; these already cover the known MMI translation findings. |
| [ran-j/PS2Recomp#410](https://github.com/ran-j/PS2Recomp/pull/410) | Compare the fork's first-fit IOP heap reuse in [`3838fa9`](https://github.com/Tzeentchnet/PS2Recomp/commit/3838fa941a15015e4df556f801747f97e91ada71) and contribute long-session fragmentation tests rather than filing a second allocator report. |
| [ran-j/PS2Recomp#413](https://github.com/ran-j/PS2Recomp/pull/413) | Exercise invocation stacks near kernel-reserved memory and contribute load-address boundary tests; do not propose a second stack-reservation mechanism. |
| [ran-j/PS2Recomp#415](https://github.com/ran-j/PS2Recomp/pull/415) | Coordinate next-function delay-slot coverage with #260 so both direct execution and scheduler resume use the decoded instruction exactly once. |
| [ran-j/PS2Recomp#417](https://github.com/ran-j/PS2Recomp/pull/417) | Contribute Killzone's CALL/RET chain evidence or tests from [`bd94dfc`](https://github.com/Tzeentchnet/PS2Recomp/commit/bd94dfc9984d8ef3fd8e7ea2b57931abe9635f98); upstream's repeated-state detection is preferable to another fixed cap. |
| [ran-j/PS2Recomp#418](https://github.com/ran-j/PS2Recomp/pull/418) | Add VU1 JR/JALR tests that mutate the source VI register in the paired instruction. This is separate from the EE unresolved-JALR resume-entry candidate above. |
| [ran-j/PS2Recomp#419](https://github.com/ran-j/PS2Recomp/pull/419) | Compare post-transfer MADR values with synthetic chained and normal DMA tests; do not retain a fork-only MADR policy if this PR covers it. |
| [ran-j/PS2Recomp#420](https://github.com/ran-j/PS2Recomp/pull/420) | Add provenance/build-script testing for nonzero generation failure; do not open a second false-success report. |
| [ran-j/PS2Recomp#423](https://github.com/ran-j/PS2Recomp/issues/423) | Add Killzone deterministic-trace evidence only after reproducing the timer/VBlank ordering difference locally. |

Standalone-entry resume support, improved floating-point semantics, SIF syscall dispatch, and GIF IMAGE2 behavior are
already represented by [#271](https://github.com/ran-j/PS2Recomp/pull/271),
[#272](https://github.com/ran-j/PS2Recomp/pull/272),
[#274](https://github.com/ran-j/PS2Recomp/pull/274), and
[#384](https://github.com/ran-j/PS2Recomp/pull/384). Do not refile the older fork patches for those behaviors.

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
[`ComputePipelineBuilder::SetSpecializationValue`](https://github.com/PCSX2/pcsx2/blob/c8081bf5d63b1b83b77112632e1113a25b81058c/pcsx2/GS/Renderers/Vulkan/VKBuilders.cpp)
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
