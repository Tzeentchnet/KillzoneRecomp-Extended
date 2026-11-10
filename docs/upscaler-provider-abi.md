# Vulkan upscaler provider ABI

`ext/kzgs/include/kzgs_upscaler_provider.h` is the C ABI for an **optional, trusted native DLL**. It uses C fixed-width
values and Vulkan C types only; no STL, PCSX2 types, vendor SDK, or C++ runtime objects cross the boundary. The current
host is Windows x64 and looks only for the selected provider in `<exe>/upscalers/kzgs_upscaler_dlss.dll` or
`<exe>/upscalers/kzgs_upscaler_xess.dll`. A provider executes native code in the game process; install only trusted
DLLs and their dependencies. Optional in-tree adapter source requires explicit local SDK paths; no proprietary SDK
or provider binary is bundled or automatically staged. See [local vendor adapters](vendor-upscalers.md).

The host loads a selected DLL before Vulkan instance/device creation, obtains `kzgs_upscaler_get_api`, and negotiates
ABI major 1, minor 0. The provider returns a version with the same major and a minor no greater than the requested
minor. Major mismatch, a missing entry point, missing mandatory API functions, or an incompatible provider kind
causes native presentation fallback. The `off`, FSR1, D3D11, D3D12, software, and surfaceless paths remain independent.
Changing to a provider not negotiated at KzGS open requires closing/reopening KzGS (normally a game or replay
restart); switching off and back to the provider already negotiated for that device is allowed.

## Sizes, versioning, and calls

- Every extensible structure begins with `struct_size`. A caller initializes it to the actual buffer capacity; a
  callee writes at most that many bytes and returns its own known size in the field. Read a field only if
  `KZGS_PROVIDER_HAS_FIELD` is true and it fits the caller's allocated buffer. Larger trailing fields are ignored;
  missing mandatory fields cause an explicit failure. Frozen value types such as `KzgsProviderRect` are not extended.
  Frame image, synchronization, and camera descriptors are pointers to separately sized structures, so each can grow
  without changing offsets in the containing frame.
- `KzgsProviderRequirements::feature_block_stride` is the byte stride of the provider's feature-block array; each
  element has its own size. The array, stride, and `VkBool32` bit arrays must preserve their types' natural
  alignment. Providers retain storage for returned arrays until the next provider call, and the host copies it
  immediately. Strings are UTF-8. Status messages are borrowed only until the next call and copied before then.
  A status may be null; when provided, its size must be honored.
- `query_info` identifies the provider kind, spatial/temporal capabilities, supported and required input flags, and
  supported color encodings. Required inputs must also be supported. The host checks these before context creation.
- `instance_requirements` returns all required instance extension names. `device_requirements` runs after physical
  device selection, before logical device creation, and returns all required device extension names, core
  `VkPhysicalDeviceFeatures`, and optional feature blocks. The host verifies availability, enables the requirements
  together, and retries PCSX2's native device configuration if optional negotiation/device creation fails.
  Device extensions/features are all-or-nothing for provider use; already-enabled instance extensions may remain
  enabled if later device negotiation fails. The provider must include its extension dependencies.
- A feature block specifies a `VkStructureType` and an array of `VkBool32` members in the corresponding Vulkan
  feature struct's declaration order, excluding `sType` and `pNext`. Missing trailing members are false. The host
  checks every requested bit with `vkGetPhysicalDeviceFeatures2` before enabling it. V1 accepts 16-bit storage and
  variable pointers (Vulkan 1.1); and, with their named device extensions, 8-bit storage, shader float16/int8, descriptor indexing,
  buffer device address, timeline semaphore, scalar block layout, shader atomic int64, Vulkan memory model,
  synchronization2, mutable descriptor type, and shader integer dot product. Unrecognized or duplicate blocks,
  malformed arrays, missing extension dependencies, and
  structures overlapping PCSX2's own feature chain fail closed. The registry can grow without changing the ABI.
- `create` receives the actual instance, physical device, logical device, graphics queue/family, Vulkan proc-address
  functions, image formats, extents, crop, encoding, and sharpening. It may allocate provider-owned resources but
  must not submit or record GPU commands. If it returns an error with a non-null context, the host destroys that
  context. `reset` is called on a fresh context before its first evaluation with the reason for the discontinuity.
  `evaluate` records commands; `destroy` releases provider-owned resources.
- The result code is authoritative. `KzgsProviderStatus` supplies a structured reason, native error code, and
  optional message; the host also reports a stable fallback reason through `kzgsGetUpscalerStatus`. Never throw C++
  exceptions across this boundary.

## Frame and resource contract

`KzgsProviderFrame` contains a source-frame identity, a candidate presentation ID, and a size-tagged synchronization
record. A new source frame gets a new identity within the device lifetime; a failed present may reuse its candidate
presentation ID. The host does **not** call `evaluate` on a redraw/duplicate. A VSync with no intervening GS draws
or transfers is conservatively treated as a potential repeat even if display registers or the interlace field
changed; the host cannot prove those are new temporal inputs. FSR1 is unaffected. The host falls back to native
presentation and resets history before the next provider dispatch. Skipped/blank VSyncs, failed presents, game
resets, resize, and configuration changes also break history. The caller never claims a successful presentation
until PCSX2 confirms it.

Presented-size readback and Vulkan bursts reuse the completed provider/CAS result without another evaluation.
They recomposite that result rather than literally reading the swapchain image, and are invalidated with the context.

The source and output are distinct host-owned, single-mip/layer color images. Their handles, views, format, whole-image
extents, half-open pixel rectangles, subresource ranges, layouts, guaranteed usage flags, and queue-family ownership
are explicit. The output rectangle starts at `(0,0)` and is distinct from the window-coordinate presentation
rectangle. Currently both images are `VK_FORMAT_R8G8B8A8_UNORM`, enter in `VK_IMAGE_LAYOUT_GENERAL`, and are on the
graphics queue. The color encoding is **unknown**; a provider must explicitly support unknown encoding to run.
`usage` lists guaranteed usage bits, not necessarily every bit used at image creation.

### Temporal qualification (host-internal; ABI unchanged)

`kzgs_temporal.h` defines a stricter GS-thread observation/qualification contract, not additional ABI 1.0 fields.
It requires nonzero frame, presentation, scene, device, provider, and resource identities; matching frame/scene IDs,
pixel grids, and source rectangles for merged/world color, depth, motion, and reactive/composition/UI masks; and
known image formats. The current synthetic validator accepts Vulkan linear SDR `R8G8B8A8_UNORM` or linear HDR
`R16G16B16A16_SFLOAT` world color, `D16_UNORM` or `D32_SFLOAT` depth with explicit forward/reversed direction,
`R16G16_SFLOAT` motion, and `R8_UNORM` masks. Unknown or packed formats fail closed.

Motion is previous-minus-current in **source pixels**, with the previous frame explicitly identified and a
jitter-inclusion flag. ABI 1.0 requires motion excluding jitter; jitter-inclusive vectors must not be submitted
without an independently validated conversion. Jitter is current-frame source pixels, positive right/down, and must
affect world rendering only. Camera data must contain finite, nonsingular current/previous view/projection matrices, valid
near/far and FOV, matching previous/current frame and scene IDs, and a depth convention consistent with the depth
image. Frame delta must be finite, positive, and matched to those frames. Extra world-color and UI bits are
host-internal and are not ABI input bits.

History is speculative until both dispatch and the matching presentation succeed. Missing or unmatched resources,
unqualified formats, duplicate/out-of-order presentations, discontinuities, and failed dispatch/presentation reset
history; a duplicate successful presentation ID cannot be reused after a reset. Production currently marks only
observed composed color, never promotes guest/GS candidates, and reports temporal validity false. All providers
advertising `KZGS_PROVIDER_CAP_TEMPORAL` are rejected before `create`/`evaluate`, independently of their stated
required inputs. See [the measured no-go and instrumentation limits](findings.md#temporal-qualification-instrumentation-fail--no-go-2026-11-10).

The host ends its render pass and supplies a recording graphics command buffer with source and output made available
by Vulkan barriers. The provider may bind its own Vulkan state and issue work only on that command buffer. It must
not submit, end, reset, retain, or use the command buffer concurrently; submit to other queues; modify the source; or
access other PCSX2-tracked resources. On **every** return path, including errors after partial recording, it must end
any render pass, synchronize its internal operations, and return both images in the supplied layouts and queue-family
ownership. The host barriers them for subsequent PCSX2 reads and invalidates/restores its Vulkan binding cache even
when the callback fails. All command-buffer/image/view and frame-structure pointers are borrowed for the call only.
Providers must own/copy any history; retaining a source image for a later frame is not allowed.

`submission_token` is PCSX2's monotonically increasing fence counter for the recording command buffer, **not** a
`VkFence` and not necessarily submitted yet. `completed_token` is the highest counter known complete. A provider
must keep its own per-dispatch resources until their tokens complete. The host retains retired contexts and output
images until `GetCompletedFenceCounter() >= last use`; it never uses a fixed frame delay. On normal teardown it
submits outstanding work and requires a successful `vkDeviceWaitIdle` before destroying contexts, then unloads the
DLL only after device teardown. `VK_ERROR_DEVICE_LOST` is reported separately as terminal loss, not as successful
completion; providers must abandon lost-device work without waiting or submitting. For another idle-wait error the
host retains the DLL, abandons affected objects, and disables providers for the rest of the process instead of
pretending the GPU completed. A provider-reported device loss latches fallback until a new Vulkan device has been
negotiated; it is not retried on the lost device.

## Optional matched inputs

Validity bits in `valid_inputs` are authoritative. `COLOR` is currently the **only** bit supplied: depth, motion,
jitter, reactive/composition masks, camera, and frame time are absent, with null pointers or zeroed values, and must
not be inferred from their storage. A provider requiring any of these inputs is rejected. Future producers may set a bit only when
that input is matched to the same source frame and its size-tagged image/camera descriptor is complete. Optional
images share the color image's pixel grid and `source_rect`; their descriptors still provide explicit extents and
formats.

When supplied, depth is Vulkan's zero-to-one normalized device depth with the camera's reverse-depth/infinite-far flags; motion RG values
times `motion_scale_x/y` are current-to-previous displacement in source pixels, excluding jitter; jitter is the
current projection offset in source pixels, positive right/down. Reactive and composition masks are normalized
`[0,1]`, with larger values identifying less reliable history and regions requiring composition protection,
respectively. Camera matrices are column-major, multiply column vectors, and contain matched current/previous
world-to-view and view-to-clip transforms. `frame_time_ms` is positive elapsed time since the prior source frame.
Providers must still check image formats, extents, subresources, and conventions before accepting a frame.

The in-tree `kzgs_mock_provider` is a test-only C DLL which uses a Vulkan blit; it is not an upscaler adapter and is
not staged with the game. It exercises negotiation, sized buffers, real command recording and fences, failures,
redraw/reset/resize/device-loss handling, safe fallback, and unload under Vulkan validation.

The optional DLSS adapter has only been exercised with a fake SDK, not a real vendor runtime or GPU. The optional
XeSS source negotiates requirements but deliberately rejects context creation because its SDK initialization may
submit work forbidden by ABI 1.0. Neither is currently usable with the renderer's color-only inputs.
