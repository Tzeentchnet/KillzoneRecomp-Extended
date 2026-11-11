# Local Vulkan vendor SDK adapters

The source-built **AMD FidelityFX FSR 3.1.6 Super Resolution** provider is an experimental Vulkan feature.
Production-qualified matched depth, motion, masks, and full camera matrices are still unavailable, so its default
`strict` input mode fails closed. An explicit `experimental` mode instead supplies a provenance-marked approximation
bundle described below. It is not production image-quality qualification.

The DLSS and XeSS adapters remain default-OFF developer targets. No actual NVIDIA or Intel SDK was available to
validate them; test doubles are not evidence of SDK, driver, GPU, image-quality, or redistribution qualification.
Their strict temporal inputs remain unavailable. In particular, NGX's real image layouts, synchronization,
command-buffer-only evaluation, and motion/jitter behavior have not been independently established.

The conditional DLSS and XeSS sources implement SDK negotiation, initialization and command-buffer-recorded
evaluation against their respective headers. The XeSS Vulkan header says `xessVKInit` may create staging buffers
and submit copy-queue work. Both adapters therefore require the explicit, synchronous, exclusive-device-idle
initialization scope in [provider ABI 1.1](upscaler-provider-abi.md); ABI 1.0 or a host lacking that scope fails
closed before calling the SDK initializer. XeSS also requires an explicit SDK execution qualification;
NGX requires that plus a separate `GENERAL`-layout qualification. On a failure after initialization starts,
unproven completion is treated as terminal and potentially in-flight SDK objects are abandoned.
Before invoking an initializer, each adapter pins its own module for the rest of the process; if pinning fails,
initialization fails. This conservative lifetime also prevents unloading SDK code after device loss, wrong-thread
destruction, or an SDK destruction failure.

These DLSS/XeSS paths are **unqualified conditional paths**: the real vendor headers, import libraries, runtime
behavior, and GPUs were not available to compile or validate them here. The successful fake-SDK targets exercise
only independent test doubles, not the actual SDK binary interfaces. Their temporal no-go independently prevents
either provider from being created.

## FSR 3 Super Resolution implementation

This scope is **FSR 3 Super Resolution only**. Frame generation is intentionally excluded: it requires a separate
design for optical flow, latency, input sampling, frame pacing, UI composition, and presentation ownership. The host
recognizes `KZGS_PROVIDER_KIND_FSR3`, maps `Upscaler=fsr3` to
`<exe>\upscalers\kzgs_upscaler_fsr3.dll`, and builds that DLL from locked FidelityFX source. The provider records
onto the host's current Vulkan graphics command buffer; it does not replace the swapchain, submit another queue, or
retain the command buffer.

AMD's official
[FSR 3.1.6 Super Resolution documentation](https://gpuopen.com/manuals/fsr_sdk/techniques/super-resolution-upscaler/)
requires current render-resolution color and depth, current-to-previous 2D screen-space motion, applied projection
jitter, positive frame delta, and reset signaling. Motion normally excludes jitter unless the integration explicitly
uses the SDK's jitter-cancellation path. Exposure may be omitted when auto exposure is selected. Reactive and
transparency/composition masks are optional API resources but important for this game's particles, alpha blending,
weapon, HUD, and post-processing. The strict-mode blockers remain:

- gameplay depth was observed in 0/20 measured frames and has no qualified direction or displayed-frame match;
- the GS has transformed vertices but no object identity or previous transforms, so dynamic/skinned motion is absent;
- no qualified world-only color, UI/composition masks, or complete scene-cut source exists;
- the presented texture already contains world, weapon, HUD, transparency, effects, and post-processing.

`FSR3InputMode=experimental` is the only mode that permits substitutes. ABI 1.2 marks every substituted input in
`approximate_inputs`; the only qualified input is the observed composed color. The approximation bundle is:

- AMD phase-count/Halton projection jitter applied to the world camera only; viewmodel and 2D rendering remain
  unjittered;
- FidelityFX Optical Flow `R16G16_SINT` current-to-previous displacement, expanded from 8x8 blocks to full-resolution
  `R16G16_SFLOAT` motion with the current/previous jitter delta removed;
- constant forward far depth (`R32_SFLOAT`, value `1.0`);
- an `R8_UNORM` reactive mask derived from the residual between current color and flow-warped committed color;
- guest near/far/FOV and game-frame time, explicitly marked approximate;
- no fabricated composition or UI mask.

The first presentation after startup or geometry change is an intentional native fallback: it publishes the exact
source/output geometry needed by the game-side jitter producer. The next matched frame dispatches with a reset.
Optical Flow, approximation, and FSR history commit only after command recording **and** successful use by
presentation. Rejected input, failed dispatch/presentation, redraw, or resize invalidates speculative history.

The resulting image can ghost around HUD/viewmodel edges, particles, disocclusions, camera cuts, or newly exposed
geometry. Experimental mode is an opt-in research path, not evidence that the strict input gate passed.

### Locked source and shader generation

[`config/dependencies.lock.json`](../config/dependencies.lock.json) pins:

- FidelityFX SDK `v2.4.0` (`7539ae8b6568ad29ed78a753404351daf3b5cca7`), which contains FSR 3.1.6;
- AMD shader tooling commit `c6efa6bf7f2027b3ec94f28578bb5965eabb9e55`, used only at build time;
- miniz `3.1.2` commit `77d0dce8627735138c51770d1799a1ef48f2117d`.

Run `tools\scripts\provision_fidelityfx.ps1` to verify/extract those archives and generate the locked shader headers.
The SDK `v2.4.0` archive does not ship the required Vulkan generated headers or GLSL entry wrappers, so generation
uses AMD's older Vulkan GLSL wrappers/core while resolving algorithm headers from `v2.4.0` first. The generator
targets Vulkan 1.1 / SPIR-V 1.3, adapts the older sampler bindings to backend bindings 320/321, emits the `main`
entry metadata expected by the current backend, and retains full-precision variants for nominal 16-bit/wave64
permutations. The runtime requests `shaderStorageImageWriteWithoutFormat` for FidelityFX's formatless output.
Provisioning is hash-checked and idempotent.

## Local build

FSR 3 is source-built by default when its locked dependencies have been provisioned:

```powershell
.\tools\scripts\provision_fidelityfx.ps1
.\tools\scripts\vsenv.bat cmake --build .\build\win-msvc-rel --target kzgs_upscaler_fsr3
```

Set `KZGS_BUILD_FSR3_PROVIDER=OFF` only for a build that intentionally excludes it. Game/test staging copies
`kzgs_upscaler_fsr3.dll`, the FidelityFX notices, and the miniz license under the output's `upscalers` tree.
The source archives and shader compiler are build inputs, not runtime assets.

DLSS and XeSS instead require a Windows x64 MSVC or clang-cl developer shell and SDKs obtained by you under their
own terms. Keep those SDKs outside the repository. There is no DLSS/XeSS SDK download, vendoring, auto-discovery,
installation, or release staging. A declared SDK release, compile/link checks, and hashes do not independently
authenticate it. Vendor runtime or driver update/telemetry behavior has not been validated or controlled here.

For DLSS, the reference layout is:

- `include\nvsdk_ngx_vk.h` and related NGX headers, with NGX API `0x15` or newer.
- `lib\Windows_x86_64\x64\nvsdk_ngx_d.lib` (the provider uses the dynamic MSVC CRT, `/MD`).

For XeSS, the reference layout is `inc\xess\xess.h`, `inc\xess\xess_vk.h`, and `lib\libxess.lib`. Runtime
negotiation requires XeSS major version 3. Both options validate the selected files, target/toolchain and x64
compile/link compatibility at configure time. Paths and version strings are ignored when their option is OFF.

For example, from a configured x64 developer shell:

```powershell
cmake --preset win-msvc-rel -DKZGS_BUILD_DLSS_PROVIDER=ON `
  "-DKZGS_DLSS_SDK_DIR=D:\private\DLSS" -DKZGS_DLSS_SDK_VERSION=310.9.16
cmake --build --preset win-msvc-rel --target kzgs_upscaler_dlss

cmake --preset win-msvc-rel -DKZGS_BUILD_XESS_PROVIDER=ON `
  "-DKZGS_XESS_SDK_DIR=D:\private\XeSS" -DKZGS_XESS_SDK_VERSION=3.0.2
cmake --build --preset win-msvc-rel --target kzgs_upscaler_xess
```

Each DLSS/XeSS target is `EXCLUDE_FROM_ALL`, independent of `kzgs`, and writes to
`build\win-msvc-rel\local-sdk-providers\RelWithDebInfo`; an ordinary build or package does not stage it. For
private experiments only, a developer would manually place the selected provider in `<exe>\upscalers` and place its
licensed runtime dependency there as well: NVIDIA's `nvngx_dlss.dll` from the SDK's `lib\Windows_x86_64\rel`,
or Intel's `bin\libxess.dll`. Driver/runtime support is also required. Do not copy these files into ordinary
release build roots or distribution folders. The DLL loader accepts native code; use only trusted binaries.

DLSS accepts only explicitly identified linear SDR/HDR color, low-resolution `R16G16_SFLOAT` motion, sampled
depth-only `D16_UNORM` or `D32_SFLOAT`, finite camera data and jitter. Current-to-previous source-pixel motion scales
and projection jitter are passed explicitly, reverse depth is selected from camera flags, and history resets follow
the ABI. ABI 1.1 also requires a known, context-stable depth convention before initialization. Its current SDK
does not support the adapter's sharpening control: use zero. Device negotiation conservatively requires
`shaderStorageImageWriteWithoutFormat`, as in NVIDIA's Vulkan example. Concurrent NGX contexts on different Vulkan
devices are rejected. The adapter records conservative global and per-image barriers around evaluation, using
`VK_IMAGE_LAYOUT_GENERAL` for its inputs and output; the NGX-specific attestation is mandatory because those layout,
access, cross-dispatch, and no-hidden-submission assumptions are **not established by the synthetic tests**.
Actual SDK behavior remains unqualified.

XeSS uses low-resolution, non-jittered current-to-previous motion and a context-stable forward or reversed depth
mode. It maps ABI motion scales through `xessSetVelocityScale`, uses source-pixel projection jitter with unit jitter
scale, and passes the same source crop for color, depth and motion. It selects a supported quality range reported by
the SDK and rejects incompatible aspect ratios. Linear SDR uses the LDR flag and unit exposure; linear HDR uses
auto-exposure. Sharpening must be zero. The candidate path transitions sampled inputs to
`VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL`, leaves the storage output in `GENERAL`, records global memory
dependencies, and restores supplied layouts on all return paths. XeSS SDK context operations are serialized on the
initialization thread; wrong-thread evaluation fails before recording and unsafe destruction abandons the context.
These mappings and synchronization assumptions still need real SDK, driver, GPU, and temporal-image qualification.

## FSR 3 validation status

The ISO-free `kzgs_fsr3_vulkan_validation` test uses a synthetic GS scene and the real source-built provider. It
checks bootstrap fallback, active output readback, translated-frame Optical Flow/history advancement, missing-input
fallback, recovery, resize/recreation, and forced partial FSR/Optical Flow context-creation cleanup. It passes
standard Vulkan validation on an NVIDIA GeForce RTX 5070.

Synchronization validation is deliberately disabled for that full GS presentation test because the baseline
synthetic GS path reports a pre-existing index-buffer upload/read hazard before FSR work. Direct provider tests keep
their existing synchronization coverage. Do not describe the FSR 3 integration as synchronization-validation clean,
and do not treat a synthetic pattern as gameplay image-quality qualification.

## Exact reference provenance and license fields

These are the public references consulted for implementation, **not claims that either SDK was installed or built**:
The optional records in [`config/dependencies.lock.json`](../config/dependencies.lock.json) repeat these references
and license links with unknown local artifact hashes and license acceptance. They are not strict/default dependencies
and never cause local SDK discovery or downloads.

| Field | NVIDIA DLSS / NGX | Intel XeSS-SR |
|---|---|---|
| Publisher / SDK | NVIDIA Corporation / DLSS SDK | Intel Corporation / XeSS SDK |
| Reference source | [NVIDIA/DLSS](https://github.com/NVIDIA/DLSS) | [intel/xess](https://github.com/intel/xess) |
| Reference release | [`v310.9.16`](https://github.com/NVIDIA/DLSS/tree/v310.9.16), commit `9981c6350dc58c0ce21f024fd65e31e851c31689` | [`v3.0.2`](https://github.com/intel/xess/tree/v3.0.2), commit `8fe81bdbbaf00b3c1b733fd0d830c333dc84e6f0` |
| Header / API reference | `include/nvsdk_ngx_vk.h`, `nvsdk_ngx_defs.h`, `nvsdk_ngx_helpers_vk.h`; NGX API `0x15` | `inc/xess/xess_vk.h`, `inc/xess/xess.h`; runtime major 3 |
| Header license notice | `LicenseRef-NvidiaProprietary` in the reference headers | Intel copyright and license-authorization notice in the reference headers; no SPDX identifier asserted for this tag |
| SDK package license | [NVIDIA RTX SDKs LICENSE](https://github.com/NVIDIA/DLSS/blob/v310.9.16/LICENSE.txt), proprietary, `LicenseRef-NVIDIA-RTX-SDK` | [Intel Simplified Software License, October 2022](https://github.com/intel/xess/blob/v3.0.2/LICENSE.txt), `LicenseRef-Intel-Simplified-Software-License-October-2022`; header terms require separate verification |
| Local link input | `lib/Windows_x86_64/x64/nvsdk_ngx_d.lib` | `lib/libxess.lib` |
| Local runtime input | `lib/Windows_x86_64/rel/nvngx_dlss.dll` | `bin/libxess.dll` |
| Files included here | No vendor headers, libraries, or binaries | No vendor headers, libraries, or binaries |
| Redistribution approval | **Not approved by this work** | **Not approved by this work** |

Configure prints the user-declared release, selected absolute SDK root, NGX API version where available, and
SHA-256 of relevant headers and the link library. Preserve those values locally with the acquisition source,
license acceptance, runtime DLL filename/hash and, for XeSS, runtime `xessGetVersion` result. For example:

```powershell
Get-FileHash 'D:\private\DLSS\lib\Windows_x86_64\rel\nvngx_dlss.dll' -Algorithm SHA256
Get-FileHash 'D:\private\XeSS\bin\libxess.dll' -Algorithm SHA256
```

### Redistribution caveats

- The referenced NVIDIA RTX SDK license permits certain SDK materials, excluding internal developer tools, to be
  distributed in object code incorporated into an application, subject to its conditions. These include material
  additional functionality and distribution terms at least as protective as the SDK license; it forbids standalone
  SDK distribution and use that would make the SDK subject to an open-source license as described in section 4(e).
  Its supplement has NVIDIA-GPU, commercial-release notification, attribution/trademark, and other requirements.
  The reference headers separately require an express NVIDIA license agreement.
- The referenced Intel license permits redistribution of its binary-only software **without modification**, subject
  to reproduction of the copyright and terms in the software and distribution materials, no implied Intel/supplier
  endorsement, and its prohibitions on reverse engineering and alteration. Separately licensed third-party files
  retain their own terms. The XeSS headers have their own express-license/permission notice; do not assume the binary
  grant automatically resolves the header terms.

These are pointers to the specific reference licenses, not a complete legal analysis or a grant for another SDK
release or runtime. This work does **not** approve redistribution of an adapter, vendor SDK component, or combined
binary, and does not establish compatibility with the GPL. Obtain independent vendor-license, GPL-compatibility,
corresponding-source, trademark, and notice review before considering distribution. The fake test headers and
functions are independently authored test doubles, not copies of either SDK.
