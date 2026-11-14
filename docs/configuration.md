# Configuration and controls

The launcher manages `killzone.ini` next to `killzone.exe`. Delete that file to restore defaults. If launcher startup
is disabled, run `killzone.exe --launcher` or set `[Launcher] Show=1`.

## Default controls

| Input | Action |
|---|---|
| Mouse | Aim |
| Left / right / middle mouse | Fire / hold to aim / secondary fire |
| W A S D | Move; navigate menus |
| Left Shift | Sprint |
| C or Left Ctrl | Crouch |
| R / G | Reload / grenade |
| Q or mouse wheel | Switch weapon |
| E or F | Use |
| X or Mouse4 | Special item |
| Escape / Tab | Pause / objectives |
| Arrow keys / Enter | Navigate and select in menus |

Bindings are stored under `[Bindings]` and can be changed in the launcher's Controls tab. Mouse aim supports hold and
toggle modes. Sprint defaults to hold; toggle sprint releases on a second press or after 300 ms without movement.
Controller input uses the original PS2 layout with adjustable radial dead zone and PS2-style stick range.
Keyboard/mouse prompt replacement remains automatic by default. Controller-family glyph replacement is separately
disabled by default; enable **Controller glyphs** or set `[Input] ControllerGlyphs=1` to use Xbox labels for SDL
standard/Xbox devices and position-correct Nintendo labels for Switch devices. PlayStation, GameCube, unknown, and
unsupported devices keep the game's original PlayStation artwork.

## File compatibility

Launcher-managed files use a schema version independent of the bindings version:

```ini
[Config]
Version=3
```

Files without a version are treated as legacy version 0. Supported older versions migrate when the launcher saves.
The parser strictly validates booleans, integers, finite numbers, and enums. Invalid values, clamps, duplicate keys,
and unknown settings are reported in `killzone.log`. The last duplicate value is effective, unknown settings are
preserved, and saves retain the disc path and all bindings. A build refuses to overwrite a file from a newer schema.

## Graphics

- `Renderer=d3d11|d3d12|vulkan` selects the backend. SMAA is available only under D3D11; FXAA is portable.
- `Upscale=0..8` selects automatic (`0`) or fixed (`1..8`) internal supersampling.
- `RenderScalePolicy=quality|balanced` selects the automatic policy when `Upscale=0`.
- `Aspect=4:3|16:9|auto|21:9|32:9|stretch` controls presentation. Ultrawide modes widen
  the world while keeping HUD and menus in the central 16:9 region. Stretch fills by distortion.
- `Mipmapping=ps2` preserves authored mip transitions. `Mipmapping=highest` always samples the base mip for draws
  that enable mipmapping.
- `RenderDistance=1..2` scales far distance and fog together. Values above 2 expose incomplete distant geometry and
  are clamped.
- `FieldOfView` and `WeaponFieldOfView` are horizontal degrees measured at 16:9. `0` keeps the game defaults
  (approximately 87 degrees world and 73 degrees weapon). Zooming retains its tangent-space magnification.
  `Fov` and `WeaponFov` remain accepted migration aliases; canonical names win and are written on save.

### Automatic render scale and 60 FPS headroom

`RenderScalePolicy=quality` preserves the original automatic behavior: select the smallest integer scale whose
448-line target covers the output height. It remains the default, and existing configurations without the new key
load as `quality`, so their effective scale does not change. `balanced` leaves raster headroom and relies on
presentation upscaling where necessary:

| Output | Quality auto | Balanced auto | Internal raster-pixel reduction |
|---|---:|---:|---:|
| 1280x720 | 2x (896p target) | 2x (896p target) | 0% |
| 1920x1080 | 3x (1344p target) | 2x (896p target) | 55.6% |
| 2560x1440 | 4x (1792p target) | 3x (1344p target) | 43.8% |
| 3840x2160 | 5x (2240p target) | 4x (1792p target) | 36.0% |

The percentages in this table are raster-pixel reductions, not predictions of GPU-time savings. A controlled
1080p measurement on one RTX 5070 found 29.2% and 40.8% lower whole-frame GPU p95 in two balanced-versus-quality
pairs; see [frame generation](frame-generation.md#matched-gpu-headroom-ab-scus-97402-chapter-1). Hardware, drivers,
scene, output mode, and fixed renderer costs can change that result. Explicit `Upscale=1..8` and the one-run
`KZ_UPSCALE` override ignore the automatic policy. Ultrawide automatic modes also account for output width.
Startup logs and capture metadata report `auto_output_height` for quality and `auto_balanced` for balanced.

The launcher's **60 FPS headroom** preset selects a 60 fps limit, Vulkan, balanced automatic scale, FSR 1 with 15%
provider RCAS, no second CAS or AA pass, replacement textures off, 1x view distance, and clean effects (film grain,
motion blur, glow, and lens blur off). It leaves V-Sync, frame pacing, window mode, aspect ratio, input, and audio
unchanged. At an output already smaller than the internal source, FSR 1 safely reports `no_upscale_needed`.

### Experimental presentation upscaler

The launcher's **Graphics** tab exposes Off, FSR 1, and experimental FSR 3 Super Resolution selection plus provider
sharpening. Its **FSR 1 quality** preset selects
Vulkan, 3x internal rendering, FSR 1 with 15% RCAS, PS2 mip transitions, 16x anisotropy, and disables the separate
CAS and AA passes. This is the recommended starting point for 1440p output. DLSS and XeSS remain unavailable.

The same settings can be edited directly:

```ini
[Graphics]
RenderScalePolicy=quality
Upscale=0
Upscaler=off
UpscalerSharpening=0
FSR3InputMode=strict
```

`Upscaler` accepts `off`, `fsr1`, `fsr3`, `dlss`, or `xess` (case-insensitive); saves use these lowercase names.
`fsr1` runs AMD FidelityFX FSR 1 EASU on the Vulkan hardware renderer, after crop/aspect selection and before the
existing CAS/final presentation filters. `UpscalerSharpening` is provider-owned: 0 disables RCAS; 1 through 100 enable
it. FSR 1 maps that range linearly from 2 to 0 AMD attenuation stops; FSR 3 maps it to the SDK's 0..1 sharpness.
It is independent of the existing `Sharpen`/CAS setting.
`Upscale` and `RenderScalePolicy` select the independent GS render resolution. Increasing that internal scale can
make the cropped source larger than the output, in which case presentation FSR1 does not run.

`FSR3InputMode` accepts `strict` (default) or `experimental`:

- `strict` requires qualified matched depth, motion, world-only jitter, camera, and timing. Those inputs are not
  currently available, so strict FSR 3 intentionally falls back.
- `experimental` enables the source-built FidelityFX FSR 3.1.6 provider on Vulkan with explicitly marked
  approximations: world-camera Halton jitter, FidelityFX Optical Flow-derived motion, constant forward far depth, a
  color-residual reactive mask, and guest scalar camera/timing data. It does not fabricate composition/UI masks.

Experimental mode is Super Resolution only; it does not generate frames or replace the swapchain. The HUD and
viewmodel remain unjittered while the composed image is reconstructed, so HUD edges, particles, disocclusions, and
newly exposed geometry may ghost or otherwise reconstruct incorrectly.

FSR1 and FSR 3 run only when neither output axis is smaller than the cropped source and at least one axis is larger. Otherwise
its status is `no_upscale_needed` and the existing presentation path is used. D3D11, D3D12, software, surfaceless
Vulkan, unavailable DLSS/XeSS, strict or incomplete FSR 3 input, and initialization/dispatch failures also fall back to the existing native
presentation and filters; provider sharpening is then zero. A temporal-provider request does **not** automatically
select FSR1. Off preserves the existing path. No proprietary SDK is included or required. The vendored AMD FSR 1
source is MIT licensed; see
its [notice and provenance](../ext/kzgs/third_party/fsr1/README.md).

Optional Vulkan providers may be placed in `<exe>/upscalers/kzgs_upscaler_fsr3.dll`,
`<exe>/upscalers/kzgs_upscaler_dlss.dll`, or `<exe>/upscalers/kzgs_upscaler_xess.dll`. Builds with
`KZGS_BUILD_FSR3_PROVIDER=ON` stage the source-built FSR 3 DLL and its FidelityFX/miniz notices automatically.
The optional local DLSS/XeSS SDK adapters remain unqualified and are not staged by ordinary builds; see
[local vendor adapters](vendor-upscalers.md).
A provider must be selected
before the Vulkan device is created so its required extensions and features can be negotiated; selecting a different
provider on an already-open device reports `device_recreation_required` and falls back until KzGS is reopened
(normally by restarting the game or replay).
The measured matched temporal-input gate remains FAIL/NO-GO for strict FSR 3, DLSS, and XeSS. Experimental FSR 3 is
the only path permitted to convert the ordered candidates into ABI 1.2 approximations; DLSS/XeSS remain strict.
The first FSR 3 presentation after startup or resize intentionally falls back while publishing exact jitter
geometry. The next matched frame can activate with reset history.

For a single run, use `KZ_UPSCALER`, `KZ_UPSCALER_SHARPENING`, and `KZ_FSR3_INPUT_MODE`, or start the executable
directly, for example:

```powershell
.\build\win-msvc-rel\killzone.exe --upscaler=fsr1 --upscaler-sharpening=35
.\build\win-msvc-rel\killzone.exe --upscaler=fsr3 --fsr3-input-mode=experimental
```

Set `Renderer=vulkan` in the INI as well. Each CLI setting takes precedence over its corresponding environment
variable, which takes precedence over that INI setting; setting only one override does not replace the other.
`--option value` and `--option=value` both work. Saves never persist the one-run overrides. Invalid INI/environment
values are reported and ignored; invalid or incomplete upscaler CLI options exit with an error. CLI errors are
emitted before log redirection. `launch_launcher.bat` clears inherited `KZ_` variables and does not forward these
arguments, so launch the executable directly for environment or CLI experiments. Both GS replay tools accept the
same environment variables and CLI options, but do not read `killzone.ini`.

#### Status and fallback

The game's `killzone.log` (or its existing console/redirected output) reports requested and effective providers,
sharpening, and a reason; the replay tools print initial and final status. `kzgsGetUpscalerStatus` and capture
metadata also expose provider error codes and the last observed temporal-input validity, missing/invalid masks, and
reasons; the status API can additionally provide a message. Before the first vendor observation, all required temporal
inputs are reported missing. A primary loading or negotiation failure can appear before the temporal gate; the masks
are still diagnostic, not permission to dispatch. See [testing](testing.md#headless-byod-runs) and the
[provider ABI](upscaler-provider-abi.md).

| Reason | Meaning |
|---|---|
| `disabled`, `active`, `waiting_for_frame` | Off, successfully presented, or awaiting a successful frame. |
| `not_implemented`, `no_surface` | The requested path is unavailable on the backend, or Vulkan has no presentation surface. |
| `no_upscale_needed`, `invalid_dimensions` | The cropped source/output dimensions do not need or cannot support the pass. |
| `provider_missing`, `provider_version_mismatch`, `provider_invalid` | DLL/dependency load, ABI version, entry point, kind, or required-function failure. |
| `instance_extension_unavailable`, `device_extension_unavailable`, `device_feature_unavailable` | Vulkan requirements could not be negotiated. |
| `required_input_unavailable`, `unsupported_color_encoding` | Required matched inputs or an accepted color encoding are unavailable. This is expected for strict FSR 3 and all DLSS/XeSS requests; experimental FSR 3 also uses it during bootstrap or after unmatched input. |
| `device_recreation_required` | A different provider was selected after device creation; restart/reopen KzGS to negotiate it. |
| `initialization_failed`, `dispatch_failed`, `present_failed`, `reset_failed`, `duplicate_present`, `device_lost` | Initialization, evaluation, presentation, history/reset, duplicate-frame, or device failure; native presentation is used when possible. |
| `idle_wait_failed` | A non-device-loss Vulkan idle wait failed; potentially in-flight Vulkan upscaler resources are abandoned instead of explicitly destroyed. |

The launcher's **Validated enhancements** preset selects the measured stock-game setup: D3D12, VSync, quality
automatic internal scale, 16x anisotropy, highest mip detail, clean PS2 effects, replacement textures off, 2x view
distance, and 30% sharpening.

## Display and game options

- **Frame rate** (`[Display] FpsLimit`) sets the game's own rate; `0` follows the monitor refresh. The list includes
  72 and 82 fps (half of 144 and 165 Hz) for 2x external frame generation, and the INI accepts any integer.
- **Even frame pacing** (`[Display] FramePacing=0|1`, experimental, default off) holds a frame that arrives less than
  three quarters of a game frame after the previous presentation, unless another frame is already waiting, so frames
  are not presented in quick pairs after a hitch. Late frames are never delayed. It is intended for external frame
  generation with V-Sync off. Measured, it helped when the game ran below its frame-rate limit and was unreliable when
  the game ran right at it ([measurements](frame-generation.md#measured-pacing-sces-52004-chapter-1)).
  `KZ_FRAME_PACING=0|1` overrides it for one run.
- The Display tab's **External frame generation** preset selects borderless window, V-Sync off, and a frame rate of
  half the monitor refresh (at least 60 fps) for Lossless Scaling, AMD AFMF, or NVIDIA Smooth Motion. It does not
  change even frame pacing. See [frame generation](frame-generation.md#external-frame-generation-lossless-scaling).
- **Show FPS** displays a counter in the top-left corner.
- **Skip intro movies** skips the long boot intro. Story cutscenes, menu backgrounds, and attract movies are unchanged.
- **Skip logos** separately skips the Sony and Guerrilla logo screens.
- Keyboard/mouse button prompts switch back to controller prompts when a controller is used. Controller prompts use
  the original PlayStation artwork unless the opt-in controller-glyph setting recognizes an Xbox or Switch layout.

The related one-run overrides include `KZ_FOV`, `KZ_WEAPON_FOV`, `KZ_RENDER_DISTANCE`,
`KZ_SKIP_LONG_INTRO`, and `KZ_MOVIE_LOG`.

## Testing options

The Testing tab can use an isolated memory card, unlock campaign levels without modifying saves, skip startup content,
or start in a selected chapter and difficulty. These options are disabled by default.

On an empty isolated card, **Start in** creates the configured profile through the game's own front end. The default
name is exact `Shooterman`, which exposes all maps through native profile queries. It never renames an existing
profile or writes to the normal memory card. Prebuilt saves and memory-card images are not distributed.

Automation equivalents are:

```text
KZ_UNLOCK_ALL=0|1
KZ_SKIP_INTRO=0|1
KZ_START_LEVEL=1..11
KZ_START_DIFFICULTY=0..2
KZ_DEFAULT_PROFILE=<name>
KZ_FRONTEND_LOG=1
```

Use the native campaign menu with exact `Shooterman` for supported all-map testing. Direct chapter selection remains
a diagnostic convenience rather than a release-qualification path.
