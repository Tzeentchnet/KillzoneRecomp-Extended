# Documentation

The root [README](../README.md) is the project overview. Detailed guidance and technical records live here.

## Using and building the port

- [Configuration and controls](configuration.md) - launcher settings, default controls, `killzone.ini` compatibility,
  field of view, and testing options.
- [Building](building.md) - prerequisites, dependency provisioning, game-code regeneration, CMake presets, and
  developer build workflow.
- [Testing](testing.md) - ISO-free tests, BYOD scenarios, headless runs, capture comparison, and focused diagnostics.
- [Release and distribution](release.md) - licensing boundaries, protected-content rules, and release checks.

## Technical reference

- [Experimental spatial presentation](configuration.md#experimental-presentation-upscaler) - Vulkan FSR 1,
  configuration precedence, diagnostics, and native fallback; no temporal reconstruction is qualified.
- [Vulkan upscaler provider ABI](upscaler-provider-abi.md) - versioning, negotiation, resource ownership, synchronization,
  optional inputs, and fallback rules.
- [Local vendor SDK adapters](vendor-upscalers.md) - opt-in build, current blockers, and license/provenance fields.
- [Frame generation research](frame-generation.md) - in-engine FSR 3.1/DLSS frame-generation gaps, proposed
  temporal markers, and Lossless Scaling support (preset, even frame pacing, pacing analyzer).
- [Upstream contribution candidates](upstream.md) - potential PS2Recomp and PCSX2 issues, PRs, RFCs, duplicate
  reports, and filing prerequisites.
- [Findings](findings.md) - reverse-engineering evidence, runtime behavior, and measured experiments.
- [Asset formats](assetforms.md) - DAT, Core/CoreStream, texture, model, and animation research.
- [Runtime patches](../patches/README.md) - rationale and evidence for PS2Recomp changes.
- [Renderer](../ext/kzgs/README.md), [VU](../ext/kzvu/README.md), [IPU](../ext/kzipu/README.md), and
  [SPU2](../ext/kzspu2/README.md) component notes.

## Archived work

- [Killzone Online research](multiplayer/README.md) - preserved prototype findings; online play is not implemented.
- [Fork project-management archive](archive/README.md) - superseded plans, task ledgers, handoffs, and process notes.
- [Upstream import plan](archive/upstream_comparison.md) - completed October 2026 comparison and import record.
- [Original upstream notes](archive/upstream/README.md) - superseded design and performance documents.
