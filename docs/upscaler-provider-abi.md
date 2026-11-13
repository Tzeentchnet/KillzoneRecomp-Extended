# Vulkan upscaler provider ABI

`ext/kzgs/include/kzgs_upscaler_provider.h` is the C ABI for an **optional, trusted native DLL**. It uses C fixed-width
values and Vulkan C types only; no STL, PCSX2 types, vendor SDK, or C++ runtime objects cross the boundary. The current
host is Windows x64 and looks only for the selected provider in `<exe>/upscalers/kzgs_upscaler_fsr3.dll`,
`<exe>/upscalers/kzgs_upscaler_dlss.dll`, or `<exe>/upscalers/kzgs_upscaler_xess.dll`. A provider executes native
code in the game process; install only trusted DLLs and their dependencies. Optional in-tree DLSS/XeSS adapter source
requires explicit local SDK paths. The source-built FSR 3.1.6 provider is staged with builds that enable
`KZGS_BUILD_FSR3_PROVIDER`; it uses locked FidelityFX/miniz sources and carries their notices. See
[local vendor adapters](vendor-upscalers.md).

The host loads a selected DLL before Vulkan instance/device creation, obtains `kzgs_upscaler_get_api`, and negotiates
ABI major 1, minor 2. The provider returns a version with the same major and a minor no greater than the requested
minor. Major mismatch, a missing entry point, missing mandatory API functions, or an incompatible provider kind
causes native presentation fallback. The `off`, FSR1, D3D11, D3D12, software, and surfaceless paths remain independent.
Changing to a provider not negotiated at KzGS open requires closing/reopening KzGS (normally a game or replay
restart); switching off and back to the provider already negotiated for that device is allowed.

**Current production gate:** strict FSR 3 Super Resolution, DLSS, and XeSS requests remain fail-closed because the
[measured input gate](findings.md#temporal-qualification-instrumentation-fail--no-go-2026-11-10) is FAIL/NO-GO.
Only FSR 3 may opt into ABI 1.2 approximations through the explicit `experimental` policy. DLSS/XeSS never accept
those approximations. ABI negotiation and the test-only spatial mock are not temporal qualification.

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
  supported color encodings. Kind values 1, 2, and 3 identify DLSS, XeSS, and FSR 3 respectively; recognizing a kind
  does not qualify an implementation. Required inputs must also be supported. ABI 1.2 providers that intentionally
  accept provenance-marked substitutes advertise `KZGS_PROVIDER_CAP_APPROXIMATE_INPUTS`; this capability alone never
  enables them. The host checks these fields before context creation.
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
  functions, image formats, extents, crop, encoding, and sharpening. ABI 1.0 permits allocation but forbids GPU
  submission and recording in `create`. ABI 1.1 optionally provides the strictly scoped initialization contract
  below; ABI 1.2 adds input provenance and partial camera validity without changing that rule. If `create` returns
  an error with a non-null context, the host
  destroys that context except when completion is unresolved, in which case it abandons the context.
  `reset` is called on a fresh context before its first evaluation with the reason for the discontinuity.
  `evaluate` records commands; `destroy` releases provider-owned resources.
- The result code is authoritative. `KzgsProviderStatus` supplies a structured reason, native error code, and
  optional message; the host also reports a stable fallback reason through `kzgsGetUpscalerStatus`. Never throw C++
  exceptions across this boundary.
- The host serializes callbacks for a provider. The optional XeSS adapter additionally requires SDK calls on its
  initialization thread; it rejects wrong-thread evaluation before recording and abandons a context rather than
  calling SDK destruction on an unsafe thread.

### ABI 1.1 initialization scope

The appended `KzgsProviderCreateInfo::initialization` pointer and `depth_convention` field are absent on 1.0 hosts.
Providers must test their sizes before reading them, return a version no higher than the requested minor, and reject
missing required 1.1 fields rather than reading a full newer structure from a 1.0 buffer. A 1.1 host can still use
1.0 providers, which ignore these trailing fields. A known forward/reversed depth convention describes the entire
context; every frame's camera flags must agree. `UNKNOWN` is not a valid convention for providers that need to choose
a depth mode at initialization.

The optional `run_exclusive` host callback is used **only synchronously during create**. The host must:

1. Exclude all other host activity on **every queue of this device**, including submits, presents and queue/device
   waits, for the entire scope; serialize device and provider operations as Vulkan requires.
2. Prove previously submitted device work complete before calling the supplied work function **once, on the calling
   thread**, and prohibit concurrent host queue use while it runs. The vendor may perform its own initialization
   submissions only inside that function. The callback and its data must never be retained.
3. Prove all device work complete again after the function returns, even if vendor initialization failed, before
   releasing the exclusive scope. Return `KZGS_PROVIDER_OK` only when these host guarantees hold and the work
   function ran once; the provider reports the work's separate result. Report failures with structured status.

The local SDK shim rejects a work callback on another thread before invoking any vendor code.

If the host fails after invoking work, the provider treats completion as unproven and returns
`KZGS_PROVIDER_ERROR_SYNCHRONIZATION_UNRESOLVED`. The host disables providers for the process, abandons rather than
reuses or destroys potentially in-flight SDK resources, and retains the provider module even if a later device-idle
wait succeeds. This result is distinct from diagnosed device loss. A host must not advertise the scope merely
because it has called an unchecked idle-wait function. Any missing, undersized, or failed scope fails closed; 1.0
hosts cannot initiate queue-submitting SDK initialization.

The flags also require explicit qualification of the exact SDK/runtime, device and driver for its execution and
synchronization behavior. NGX additionally requires qualification of the adapter's `GENERAL` input/output layout,
access masks and cross-dispatch dependencies. These flags are assertions backed by independent validation, not a way
to request an unsafe override. The host supplies the exclusive scope and SDK-execution qualification used by the
pinned source-built FidelityFX path. It does not assert NGX's separate `GENERAL`-layout qualification, and those
flags do not make the DLSS/XeSS test doubles into vendor qualification.

### ABI 1.2 approximation provenance and camera validity

ABI 1.2 appends `KzgsProviderFrame::approximate_inputs` and
`KzgsProviderCamera::valid_fields`. `approximate_inputs` must be a subset of `valid_inputs`; a provider may accept
nonzero bits only when it advertises `KZGS_PROVIDER_CAP_APPROXIMATE_INPUTS`. Strict policy requires the mask to be
zero. The in-tree FSR 3 provider is the only implementation that advertises this capability; DLSS and XeSS reject
all approximation bits.

Camera validity is field-specific. `KZGS_PROVIDER_CAMERA_VALID_SCALARS` covers finite near plane, far plane, and
vertical FOV; `KZGS_PROVIDER_CAMERA_VALID_MATRICES` covers matched current/previous view-to-clip and world-to-view
matrices. Strict DLSS/XeSS validation requires the full matrix set. Experimental FSR 3 requires only scalar validity
and marks those scalars approximate. Older providers negotiate minor 0/1 and never read these trailing fields.

## Frame and resource contract

`KzgsProviderFrame` contains a source-frame identity, a candidate presentation ID, and a size-tagged synchronization
record. A new source frame gets a new identity within the device lifetime; a failed present may reuse its candidate
presentation ID. The host does **not** call `evaluate` on a redraw/duplicate. A VSync with no intervening GS draws
or transfers is conservatively treated as a potential repeat even if display registers or the interlace field
changed; the host cannot prove those are new temporal inputs. FSR1 is unaffected. The host falls back to native
presentation and resets history before the next provider dispatch. Skipped/blank VSyncs, failed presents, game
resets, resize, and configuration changes also break history. The caller never claims a successful presentation
until PCSX2 confirms it.

Presented-size readback and Vulkan bursts may reuse a cached provider result without another evaluation. They are
diagnostic recompositions, not literal swapchain images: skipped or failed presents can instead produce current-frame
or pre-CAS snapshots. Negative-size bursts identify this as `presentation_recomposition`, and cached provider state
is invalidated with the context.

The source and output are distinct host-owned, single-mip/layer color images. Their handles, views, format, whole-image
extents, half-open pixel rectangles, subresource ranges, layouts, guaranteed usage flags, and queue-family ownership
are explicit. The output rectangle starts at `(0,0)` and is distinct from the window-coordinate presentation
rectangle. Currently both images are `VK_FORMAT_R8G8B8A8_UNORM`, enter in `VK_IMAGE_LAYOUT_GENERAL`, and are on the
graphics queue. The color encoding is **unknown**; an otherwise eligible provider must explicitly support unknown
encoding. Experimental FSR 3 first normalizes that source into a host-owned
`VK_FORMAT_R8G8B8A8_UNORM` linear-SDR image and passes the normalized image instead; the original composed image is
not relabeled. `usage` lists guaranteed usage bits, not necessarily every bit used at image creation.

### Temporal qualification (host-internal)

`kzgs_temporal.h` defines a stricter GS-thread observation/qualification contract.
It requires nonzero frame, presentation, scene, device, provider, and resource identities; matching frame/scene IDs,
pixel grids, and source rectangles for merged/world color, depth, motion, and reactive/composition/UI masks; and
known image formats. The current synthetic validator accepts Vulkan linear SDR `R8G8B8A8_UNORM` or linear HDR
`R16G16B16A16_SFLOAT` world color, `D16_UNORM` or `D32_SFLOAT` depth with explicit forward/reversed direction,
`R16G16_SFLOAT` motion, and `R8_UNORM` masks. Unknown or packed formats fail closed.

Motion is previous-minus-current in **source pixels**, with the previous frame explicitly identified and a
jitter-inclusion flag. Motion passed to a provider excludes jitter; jitter-inclusive vectors must not be submitted
without a validated conversion. Jitter is current-frame source pixels, positive right/down. Strict camera data must
contain finite, nonsingular current/previous view/projection matrices, valid near/far and FOV, matching
previous/current frame and scene IDs, and a depth convention consistent with the depth image. ABI 1.2 experimental
FSR 3 instead accepts provenance-marked scalar camera fields. Frame delta must be finite, positive, and matched to
those frames. Extra world-color and UI bits are host-internal and are not ABI input bits.

History is speculative until both dispatch and the matching presentation succeed. Missing or unmatched resources,
unqualified formats, duplicate/out-of-order presentations, discontinuities, and failed dispatch/presentation reset
history; a duplicate successful presentation ID cannot be reused after a reset. Strict policy marks only observed,
qualified inputs and rejects FSR 3 SR, DLSS, and XeSS before `create`/`evaluate`. Experimental FSR 3 is a separate
policy: it requires matched color, world jitter, scalar camera data, and game-frame time, then the Vulkan
approximation stage supplies forward far depth, FidelityFX Optical Flow motion, and a residual-derived reactive
mask. Every substitute is marked in both the host diagnostics and ABI 1.2 `approximate_inputs`; composition/UI masks
remain absent. No approximation is exposed to DLSS or XeSS. A missing DLL, unsupported device, or failed negotiation
can still be the primary fallback reason. See [the measured strict-mode no-go and instrumentation limits](findings.md#temporal-qualification-instrumentation-fail--no-go-2026-11-10).

`kzgsGetUpscalerStatus` and capture metadata report whether a frame was observed, its ID, temporal validity,
`valid_inputs`, `missing_inputs`, `invalid_inputs`, and `invalid_reasons`. Before the first observation, a vendor
request reports all required inputs missing rather than treating defaults as evidence. These fields describe the
last observed frame, not permission to dispatch a later frame. Input masks use bits 0..9 in this order: composed
color, depth, motion, jitter, reactive mask, composition mask, camera, matched frame time, world color, UI mask.
`invalid_inputs` identifies each declared input that failed validation; `invalid_reasons` uses the
`KZGS_TEMPORAL_INVALID_*` bits in `kzgs_temporal.h` and may also contain frame-wide identity or geometry failures.
The status message names missing/invalid inputs when the temporal gate is the primary fallback.

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
submits outstanding work and requires a successful `vkDeviceWaitIdle` before destroying contexts, then releases its
DLL reference only after device teardown. The local SDK adapters pin their modules for process lifetime before SDK
initialization to make later abandonment safe. `VK_ERROR_DEVICE_LOST` is reported separately as terminal loss, not as successful
completion; providers must abandon lost-device work without waiting or submitting. For another idle-wait error the
host retains the DLL, abandons affected objects, and disables providers for the rest of the process instead of
pretending the GPU completed. A provider-reported device loss latches fallback until a new Vulkan device has been
negotiated; it is not retried on the lost device.

## Optional matched inputs

Validity bits in `valid_inputs` are authoritative; storage without its bit is absent. Under strict policy, composed
`COLOR` is currently the only qualified bit, so a temporal provider is rejected. Under experimental FSR 3 policy,
the host promotes a matched candidate only with explicit approximation provenance and constructs the remaining
images before ABI dispatch.

The guest observation path carries bounded camera near/far/FOV/aspect, raw current render-camera matrices copied
from game-object offsets `+0x310` and `+0x400`, simulation delta, and the matched jitter state. They are
double-buffered with the game's build, latched at its kick, and copied with the ordered VSync command. Matrix
layout/coordinate semantics and provider-compatible previous transforms are still not proven, so the matrices remain
diagnostic and multiple incompatible candidates fail closed. Metadata may report finite/nonzero and simple last-row
or last-column structural traits, plus a pair/change mask for consecutive successfully presented world builds with
the same camera and no reset. It never exposes matrix elements or promotes that pair into `valid_inputs`.
Experimental FSR 3 uses only the bounded scalar camera values and frame time, marking them approximate.

When supplied, depth is Vulkan's zero-to-one normalized device depth with the camera's reverse-depth/infinite-far flags; motion RG values
times `motion_scale_x/y` are current-to-previous displacement in source pixels, excluding jitter; jitter is the
current projection offset in source pixels, positive right/down. Reactive and composition masks are normalized
`[0,1]`, with larger values identifying less reliable history and regions requiring composition protection,
respectively. Camera matrices are column-major, multiply column vectors, and contain matched current/previous
world-to-view and view-to-clip transforms. `frame_time_ms` is positive elapsed time since the prior source frame.
Providers must still check image formats, extents, subresources, and conventions before accepting a frame.

For the in-tree FSR 3 provider, generated motion is full-resolution `R16G16_SFLOAT`, generated depth is forward
`R32_SFLOAT`, and the optional reactive mask is `R8_UNORM`. The raw Optical Flow field is current-to-previous
`R16G16_SINT` at 8x8 block resolution; the host expands it and applies
`rawFlow + currentJitter - previousJitter` so the ABI motion excludes jitter. The composition and UI pointers remain
null because no trustworthy masks exist.

The in-tree `kzgs_mock_provider` is a test-only C DLL which uses a Vulkan blit; it is not an upscaler adapter and is
not staged with the game. Separate test-only C++ hooks explicitly permit this named spatial mock to exercise ABI
command recording and fences, failures, redraw/reset/resize/device-loss handling, and unload. Those hooks are never
enabled by configuration, reject temporal-capability providers, and are not vendor qualification; the gate tests run
without them.

Both optional DLSS/XeSS adapters have only been exercised with fake SDKs, not real vendor headers, runtimes or GPUs.
Their conditional initialization paths require the ABI 1.1 exclusive scope and explicit qualification, and neither
is usable with the current strict color-only input set. The source-built FSR 3 provider is exercised with its real
FidelityFX source and Vulkan implementation, but only its explicit ABI 1.2 experimental policy can dispatch.
