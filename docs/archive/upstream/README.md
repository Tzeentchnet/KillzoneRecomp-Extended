# Archived upstream design documents

These documents were written in the original
[drakolordx7/KillzoneRecomp](https://github.com/drakolordx7/KillzoneRecomp) repository between 2026-09-28 and
2026-09-30, before this fork's audited baseline (`5f0700079f9ac4e14799d66d01747dbcdcec55f6`). They are kept for their
reasoning and measurements. They are not plans for this fork: none of their items is a queued task unless it also
appears in [`../../../TODO.md`](../../../TODO.md).

| Document | What it was | Why it is archived |
|---|---|---|
| [`gs_integration.md`](gs_integration.md) | "Phase 3" plan for putting PCSX2's GS behind PS2Recomp's pipeline | Implemented upstream as `ext/kzgs`; the description of the current GS integration lives in [`ext/kzgs/README.md`](../../../ext/kzgs/README.md) |
| [`perf_research.md`](perf_research.md) | Ranked performance research for the 22 -> 120 fps goal | The goal was reached upstream (patches 0016-0025, see [`patches/README.md`](../../../patches/README.md)); measurements are reproduced in [`../../findings.md`](../../findings.md) |
| [`perf_fresh_ideas.md`](perf_fresh_ideas.md) | Unbuilt performance idea list, ranked by expected payoff | Speculative; the fork's queue is governed by [`../../../IMPLEMENTATION_PLAN.md`](../../../IMPLEMENTATION_PLAN.md) |

Ideas from these files can be promoted only by adding a scoped work package to the implementation plan and the TODO
queue, with acceptance commands and a stop condition. Line references inside them point at the upstream code of that
time and may be stale.
