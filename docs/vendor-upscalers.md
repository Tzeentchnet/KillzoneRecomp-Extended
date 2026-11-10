# Local Vulkan vendor SDK adapters

These are **experimental, default-OFF developer targets**, not release features. The renderer currently supplies
neither matched temporal inputs nor a known color encoding, so both providers remain unavailable and fall back.
No actual NVIDIA or Intel SDK was available to validate these adapters; test doubles are not evidence of SDK,
driver, GPU, image-quality, or redistribution qualification.
In particular, NGX initialization's compliance with the ABI's no-submission rule, real image layouts, and
motion/jitter behavior have not been independently established; qualify these before any future host supplies
temporal inputs.

The DLSS source implements NGX extension/capability negotiation and a deferred, command-buffer-recorded feature
lifecycle. XeSS currently implements version, extension, and feature negotiation only. Its context creation
**fails closed**: the XeSS Vulkan header says `xessVKInit` may create staging buffers and submit copy-queue work,
whereas [provider ABI 1.0](upscaler-provider-abi.md) prohibits submissions during `create` and permits only the
borrowed command buffer during `evaluate`. Full XeSS initialization/evaluation requires a separately designed
synchronization contract and real-SDK validation; it is not implemented here.

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
the ABI. Its current SDK
does not support the adapter's sharpening control: use zero. Device negotiation conservatively requires
`shaderStorageImageWriteWithoutFormat`, as in NVIDIA's Vulkan example. Concurrent NGX contexts on different Vulkan
devices are rejected. Actual SDK behavior remains unqualified.

## Exact reference provenance and license fields

These are the public references consulted for implementation, **not claims that either SDK was installed or built**:

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
