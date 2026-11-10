# Local Vulkan vendor SDK adapters

These are **experimental, default-OFF developer targets**, not release features. The renderer currently supplies
neither matched temporal inputs nor a known color encoding, so both providers remain unavailable and fall back.
No actual NVIDIA or Intel SDK was available to validate these adapters; test doubles are not evidence of SDK,
driver, GPU, image-quality, or redistribution qualification.
The host rejects **every DLSS/XeSS provider before context creation**, even a DLL claiming spatial or color-only
capability. The measured gate remains a no-go; see [temporal qualification](findings.md#temporal-qualification-instrumentation-fail--no-go-2026-11-10).
In particular, NGX's real image layouts, synchronization, command-buffer-only evaluation, and motion/jitter
behavior have not been independently established. DLSS refuses initialization unless an ABI 1.1 host provides
exclusive device-idle initialization and explicitly attests to those SDK/runtime/device qualifications.
The current host provides neither the initialization scope nor any qualification flags.

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

These are **unqualified conditional paths**: the real vendor headers, import libraries, runtime behavior, and GPUs
were not available to compile or validate them here. The successful fake-SDK targets exercise only the independent
test doubles, not the actual SDK binary interfaces. The current host supplies no scope or qualification flags, and
the temporal no-go independently prevents either provider from being created.

## Local build

Use a Windows x64 MSVC or clang-cl developer shell and an SDK obtained by you under its own terms. Keep SDKs outside
the repository. There is no SDK download, vendoring, auto-discovery, installation, or release staging. The SDK
release is a **declared** provenance field; compile/link checks and hashes do not independently authenticate it.
The vendor runtime or driver may have separate update/telemetry behavior; that behavior has not been validated
or controlled by these build scripts.

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

Each target is `EXCLUDE_FROM_ALL`, independent of `kzgs`, and writes to
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

This work does **not** approve redistribution of an adapter, vendor SDK component, or combined binary. Obtain
independent legal, GPL-compatibility, trademark, notice, and vendor-license review before considering distribution.
The fake test headers and functions are independently authored test doubles, not copies of either SDK.
