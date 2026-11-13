# Frame generation research (2026-11-13)

This page covers two different routes to frame generation:

1. **In-engine**, where the renderer gives an SDK (FSR 3.1 frame generation or DLSS frame generation) its own inputs.
2. **External**, where a separate program (Lossless Scaling, AMD AFMF, NVIDIA Smooth Motion) captures the finished
   window and interpolates it with optical flow alone.

The in-engine route is not implemented; it is blocked by the same input gaps as strict temporal upscaling
([findings](findings.md#temporal-qualification-instrumentation-fail--no-go-2026-11-10)). The external route
needs no integration, but its quality depends on the cadence of new game images, not just presentation calls. The port provides a
launcher preset, an opt-in even-pacing mode, and a pacing analyzer for it ([port support](#port-support)); none
has been play-tested with Lossless Scaling yet.

## In-engine frame generation

### Current temporal markers

The game-side hooks in [`kz_widescreen.cpp`](../src/kz_widescreen.cpp) record a per-build observation:

- the `WORLD`, `VIEWMODEL`, `2D`, and `JITTER_APPLIED` flags;
- near/far/FOV/aspect from the world camera;
- the game-timer frame time (elapsed ticks x seconds per tick x time scale);
- the raw render-camera matrices at object offsets `+0x310` and `+0x400`.

Observations are stored in the game's double buffer, latched at the DMA kick, and ordered with the VSync command
([`kz_gs.cpp`](../src/kz_gs.cpp)). In the GS thread, [`kzgs_temporal_runtime.cpp`](../ext/kzgs/src/kzgs_temporal_runtime.cpp)
reports structural traits for each matrix and pairs consecutive presented world builds that use the same camera
with no reset. It reports only which matrix changed, never the element values, and nothing is promoted to provider
inputs.

In 1,406 SCES-52004 world observations, 927 consecutive pairs were found (349 changed, 578 unchanged). Neither
matrix ever had an affine `0001` or perspective `00Z0` last row or column. Their layout and roles are still unknown.

### What frame generation needs

| Input | FSR 3.1 frame generation | DLSS frame generation | Status |
|---|---|---|---|
| HUD-less color and a separate UI layer | Strongly recommended; otherwise the HUD is interpolated | Strongly recommended | **Missing.** The presented image already has the HUD, weapon, effects, and post-processing drawn in. |
| Depth | Required | Required | **Missing.** Of 5,213 world frames, 5,190 had no display-correlated depth candidate and 23 had several. None had exactly one. |
| Motion vectors | Required; its optical flow covers gaps | Required | Only the experimental FSR 3 Optical Flow approximation. |
| Camera | Near/far/FOV | View-to-clip, clip-to-previous-clip, camera position and axes | Scalars are candidates; matrices have unknown meaning. |
| Pacing markers | Handled by its frame-interpolation swapchain | Reflex markers: simulation, render submit, present | Build, kick, and VSync IDs exist, but no timeline API. |
| Presentation ownership | Replaces the swapchain | Replaces the swapchain (Streamline) | **Not permitted.** Provider ABI 1.2 records only into the host's command buffer. |

FSR 3.1 frame generation sources are already present in the locked FidelityFX SDK
(`Kits/FidelityFX/framegeneration/fsr3`), including `FrameInterpolationSwapchainVK.cpp`. No DLSS SDK is available
locally ([vendor adapters](vendor-upscalers.md)).

### Marker work, in priority order

1. **World/HUD boundary marker inside the GS command stream.** The world, viewmodel, and 2D hooks run while the
   game *builds* its frame packet list, not while the GS *executes* it, so a host callback from those hooks fires at
   the wrong time ([findings](findings.md#ordered-observations-and-limits)).

   The proposal is to have the `viewmodel` and `ortho` hooks append a recognizable no-op GIF packet to the list
   being built. kzgs would detect that packet during GIF transfer and take a snapshot of the current color and the
   bound depth target. A world-only snapshot, or world plus weapon, gives a HUD-less color. Comparing it with the
   presented frame gives a UI layer, and the bound depth at that point is tied to the displayed world frame.

   This requires locating the packet builder's write pointer and proving the packet is ignored by the GS.

2. **Matrix semantics without exposing element values.** The missing affine/perspective signatures suggest the
   matrices include PS2 screen-space scale and offset, or are stored transposed. More sanitized traits could settle
   this:
   - whether the upper 3x3 is orthonormal by rows or by columns;
   - the sign of the determinant;
   - whether the scale terms match a projection built from the observed FOV and aspect.

   With known matrices, camera motion vectors can be reconstructed from depth plus the current and previous
   view-projection. That covers static world geometry. Moving and skinned objects still depend on the SDK's optical
   flow.

3. **Pacing markers on real game frames.** The experimental FSR 3 probe matched only 209 of 772 presented world
   VSyncs to a new jittered build. Interpolation must be driven by real builds and the game-timer delta, not by
   VSyncs, or it interpolates between identical frames. For DLSS, Reflex markers map onto the existing timeline:
   build start/end = simulation, DMA kick = render submit, GS present = present.

4. **Presentation ownership in the provider ABI.** Both SDKs need to own the swapchain or present path, which needs
   a new ABI version and a design for latency, UI composition, and fallback.

**Base-rate caveat:** AMD recommends about 60 fps before frame generation. Below that, interpolation artifacts and
latency dominate whatever the inputs are. Use the [high-refresh campaign audit](testing.md#high-refresh-campaign-audit)
to measure the game's real build rate first.

## External frame generation: Lossless Scaling

Lossless Scaling captures the game window and generates frames from its own optical flow. It needs no integration
and works with every renderer backend (`d3d11`, `d3d12`, `vulkan`). It also sees only the composed image, so the
HUD, crosshair, weapon, and FPS counter are interpolated like the world. The port can still affect the result through
its window mode, frame rate, and presentation cadence.

### How the port's behavior affects it

- **Window mode.** Lossless Scaling captures a window. `Borderless` (the default) and `Windowed` work.
  `Fullscreen` is SDL exclusive fullscreen ([`kz_main.cpp`](../src/kz_main.cpp)) and should not be used with it.
- **Submission is not image uniqueness.** By default the VSync worker submits only when GIF packets arrived or
  display registers changed ([`kz_gs.cpp`](../src/kz_gs.cpp), around the `presentAll` check). Either can happen
  without a new world image. `KZ_GS_PRESENT_ALL=1` also re-presents unchanged VSyncs; avoid it when assessing the
  base rate for Lossless Scaling.
- **Uneven cadence.** Presentation is non-blocking: when the GS thread is busy, that VSync's presentation is
  dropped and a later VSync can show the newest frame. Other GS commands still use a bounded ring and can block
  their producer when it fills. Irregular guest VSyncs and game-frame production can also produce uneven cadence.
- **Base frame rate.** The guest VSync rate, and with it the simulation step, is `FpsLimit` or the display refresh
  rate when `FpsLimit=0` ([`kz_timing.h`](../src/kz_timing.h)). With Lossless Scaling, the game should run at a fixed
  rate below the display refresh:
  - 120 Hz display: `FpsLimit=60` and a fixed 2x multiplier.
  - 144/165 Hz display: `FpsLimit=72`/`82` (in the launcher list) with fixed 2x, or `FpsLimit=60` with adaptive mode
    targeting the refresh rate. Rates other than 60 have not been validated for gameplay.
  - 60 fps is also the best-validated simulation rate above 30; higher rates still need gameplay validation
    ([findings](findings.md#frame-timing-key-for-high-refresh)).
- **VSync.** The port's `VSync` setting selects FIFO presentation. Community guides recommend disabling in-game
  VSync and letting Lossless Scaling handle synchronization. Because the port paces the game on its own guest VSync
  clock, turning presentation VSync off should not change simulation speed. Compare both settings for judder.
- **Scaling.** Use the port's internal `Upscale` and, on Vulkan, FSR 1 for resolution. Do not also enable
  Lossless Scaling's own upscaler, which would scale the image a second time.
- **Latency.** Interpolation holds back at least one real frame. At a 60 fps base this adds roughly 16 ms or more to
  mouse aim.

### Port support

The Display tab's **External frame generation** preset applies the configuration below, choosing `FpsLimit` as half
the monitor refresh, but never below 60 (120 Hz -> 60, 144 Hz -> 72, 165 Hz -> 82, 240 Hz -> 120). It leaves
**Even frame pacing** unchanged. The settings are documented in
[configuration](configuration.md#display-and-game-options).

```ini
[Display]
WindowMode=borderless
VSync=0
FpsLimit=60
```

In Lossless Scaling: frame generation 2x (fixed) when the limit is exactly half the refresh, otherwise adaptive
targeting the refresh rate; scaling off; DXGI capture. `ShowFps=1` (or **Show FPS** in the launcher) counts OSD
renders per presentation; it does not establish the rate of unique game images. This configuration has not been
play-tested with Lossless Scaling.

**Even frame pacing** (`FramePacing=1`, experimental, off by default;
[`kzgs_pacing.h`](../ext/kzgs/src/kzgs_pacing.h)) runs on the GS thread before each presentation, at the guest VSync
rate. A frame arriving less than three quarters of a period after the previous presentation is held until then,
unless another frame is already queued behind it; a late frame is never held. It separates the catch-up frame after
a hitch without adding a steady frame of latency. It cannot make a late frame on time.

### Measured pacing (SCES-52004, chapter 1)

An authorized headless diagnostic used SCES-52004, Vulkan, 2x internal scale (1280x896 output), V-Sync off,
60 Hz guest timing, experimental pacing off, and synchronous VIF1/VU1/GIF. Chapter 1 was started with intro skip and
no input. The following observations cover 29.5 seconds of wall time; screenshot readbacks were outside
the analyzed interval. The raw traces and captures remain private under ignored `work\` directories.

| Observation | Result |
|---|---|
| GS-vsync completions | 59.6/s; median interval 15.10 ms, p95 30.81 ms, p99 31.59 ms |
| Long completion intervals | 143 above 25 ms |
| Guest-vblank interval | p95 30.73 ms, close to the presentation-side p95 |
| Guest frame-limiter entries | 1,763 in 29.5 s, about 59.8/s |
| GS queue and thread | Queue normally contained one frame after submission; GS thread was idle about 60% of the timeline |
| GS-vsync call duration | Median about 1.8 ms; this excludes GIF-transfer and other GS work |

Of 139 guest-vblank intervals longer than 1.5 periods, 122 ended within 0.1 ms of a scheduler pacing-wait return
that reported no posted event. Reconstructing the fixed 16,667 us host cadence from the observed tick IDs gives a
median **lower bound** of 14.8 ms lateness for those 122 endpoints. This points to delayed wait returns or thread
descheduling as a useful investigation target, not proof of a particular timer defect: the requested wait deadline
is not recorded, and the phase is inferred from the earliest observed tick.

There was also a distinct long stall: a roughly 220 ms GS-vsync call overlapped a 141 ms wait for space in the GS
command ring and a 147 ms guest-vblank interval. The trace does not identify why the GS call took so long. A mostly
empty frame queue and CPU-side GS idle time therefore do not rule out occasional renderer or GPU-related stalls.
The former claim that the GS required about 15.5 ms per frame, and the conclusion that GS saturation was the primary
cause of the general cadence, are not established by these measurements.

A separate exploratory run requested a 1 ms Windows timer period via the profiler and recorded about 57.2 GS-vsync
completions/s but only 35.2 guest frame-limiter entries/s. This demonstrates that presentation counts can exceed the
game-loop proxy; neither count proves unique image rate. The game progressed differently between runs, actual timer
resolution was not verified, and the runs are **not a controlled A/B test** of the timer or pacing.

A standalone MSVC wait probe on the same machine provides a narrower clue, without running the game. For a requested
2 ms wait (50 samples after warmup), `std::condition_variable::wait_until` had a 15.53 ms median and 16.04 ms p95.
After a successful `timeBeginPeriod(1)` request it remained at 15.54 ms median and 16.03 ms p95; actual timer
resolution was not independently verified. A Windows high-resolution waitable timer measured 2.51 ms median and
2.54 ms p95 in the same probe ([reproduction](testing.md#presentation-pacing)). These results are consistent with
coarse wait timing on this machine, but the probe does not reproduce game workload or scheduler event notifications.
The waitable timer is not a drop-in replacement for the scheduler's interruptible condition-variable wait.

The previous campaign's 39 fps pacing-on result is withdrawn as a 40-110 guest-second comparison: the analyzer
silently selected 50.68-101.58 seconds, and captures show an extended near-static, HUD-less view. Other historical
campaign windows have not been independently aligned and should not be used to claim a pacing win or loss. In-window
screenshot readbacks can themselves stall the renderer. The updated analyzer requires boundary captures within
0.5 seconds of the requested guest times and reports the actual anchors
([testing](testing.md#presentation-pacing)).

These are headless observations on one machine (RTX 5070), in one scene, without player input. They do not measure
desktop compositing, a real swapchain presentation, or perceived smoothness; no default pacing or timer change is
justified. The EU/PAL asynchronous VIF1 worker is disabled because of a known memory-corruption race, not as a
performance recommendation.

### Remaining work

- Repeat matched gameplay segments with a window, real output sizes and render scales, V-Sync on and off, several
  runs per condition, and no in-window screenshot readbacks. Measure actual new game builds separately from presents.
- Trace guest deadline wakeups and workloads to distinguish scheduler/timer jitter from late game work before
  attempting a runtime fix.
- If prototyping a high-resolution wait, preserve prompt event and stop wakeups, test races and fallback on supported
  Windows versions, and measure power use and matched gameplay before considering a default change.
- Check whether 72/82 fps simulation behaves like 60 in the [high-refresh campaign audit](testing.md#high-refresh-campaign-audit).
- Play-test with Lossless Scaling at fixed 2x and adaptive modes; record judder and HUD artifacts.
- In every SCES-52004 headless run here, and in the earlier SCES-52004 temporal runs, the mouse-aim hook never logged
  the local player (`mouse aim: local player`). Gameplay-relative input and `KZ_HEADLESS_GAMEPLAY_SECONDS` therefore
  never started, so the pacing runs use guest-time windows. Mouse aim depends on the same detection; investigate
  separately.

### Sources

- [Notebookcheck Lossless Scaling optimization guide (May 2026)](https://www.notebookcheck.net/You-Should-Be-Modding-Lossless-Scaling-Optimization-Guide.1295503.0.html)
- [Corsair: Lossless Scaling overview](https://www.corsair.com/br/en/explorer/gamer/gaming-pcs/lossless-scaling-everything-you-need-to-know/)
- [r/OptimizedGaming Lossless Scaling resource (March 2025)](https://lr.eu.psf.lt/r/OptimizedGaming/comments/1jjl1t0/ultimate_lossless_scaling_upscaling_resource)

These are community guides, not official documentation from the developer.
