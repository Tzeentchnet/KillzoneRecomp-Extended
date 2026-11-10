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
- `Upscale=0..8` selects automatic or fixed internal supersampling.
- `Aspect=4:3|16:9|auto|21:9|32:9|stretch` controls presentation. Ultrawide modes widen
  the world while keeping HUD and menus in the central 16:9 region. Stretch fills by distortion.
- `Mipmapping=ps2` preserves authored mip transitions. `Mipmapping=highest` always samples the base mip for draws
  that enable mipmapping.
- `RenderDistance=1..2` scales far distance and fog together. Values above 2 expose incomplete distant geometry and
  are clamped.
- `FieldOfView` and `WeaponFieldOfView` are horizontal degrees measured at 16:9. `0` keeps the game defaults
  (approximately 87 degrees world and 73 degrees weapon). Zooming retains its tangent-space magnification.
  `Fov` and `WeaponFov` remain accepted migration aliases; canonical names win and are written on save.

### Experimental presentation upscaler

These experimental settings have no launcher controls:

```ini
[Graphics]
Upscaler=off
UpscalerSharpening=0
```

`Upscaler` accepts `off`, `fsr1`, `dlss`, or `xess` (case-insensitive); saves use these lowercase names.
`fsr1` runs AMD FidelityFX FSR 1 EASU on the Vulkan hardware renderer, after crop/aspect selection and before the
existing CAS/final presentation filters. `UpscalerSharpening` is provider-owned: 0 disables RCAS; 1 through 100 enable
it, mapping linearly from 2 to 0 AMD attenuation stops. It is independent of the existing `Sharpen`/CAS setting.
The existing `Upscale` selects automatic or integer GS supersampling and is unchanged. Increasing that internal
scale can make the cropped source larger than the output, in which case presentation FSR1 does not run.

FSR1 runs only when neither output axis is smaller than the cropped source and at least one axis is larger. Otherwise
its status is `no_upscale_needed` and the existing presentation path is used. D3D11, D3D12, software, surfaceless
Vulkan, unavailable DLSS/XeSS, and initialization/dispatch failures also fall back to the existing native presentation
and filters; provider sharpening is then zero. A vendor request does **not** automatically select FSR1. Off preserves
the existing path. No proprietary SDK is included or required. The vendored AMD FSR 1 source is MIT licensed; see
its [notice and provenance](../ext/kzgs/third_party/fsr1/README.md).

Optional Vulkan providers may be placed in `<exe>/upscalers/kzgs_upscaler_dlss.dll` or
`<exe>/upscalers/kzgs_upscaler_xess.dll`. No vendor binary is bundled; the optional local SDK adapters are
unqualified and the current host provides no vendor initialization scope, as described in
[local vendor adapters](vendor-upscalers.md).
A provider must be selected
before the Vulkan device is created so its required extensions and features can be negotiated; selecting a different
provider on an already-open device reports `device_recreation_required` and falls back until KzGS is reopened
(normally by restarting the game or replay).
The current renderer exposes composed color only, with an unknown color encoding. **Both DLSS and XeSS are
unavailable**, even if a DLL claims spatial or color-only capability: the measured matched temporal-input gate is
FAIL/NO-GO. Explicitly select `fsr1` to use the independent spatial path. No estimated or zero motion is
substituted. The host supplies no vendor SDK initialization scope, and no real SDK or vendor GPU was qualified.

For a single run, use `KZ_UPSCALER` and `KZ_UPSCALER_SHARPENING`, or start the executable directly, for example:

```powershell
.\build\win-msvc-rel\killzone.exe --upscaler=fsr1 --upscaler-sharpening=35
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
| `required_input_unavailable`, `unsupported_color_encoding` | Required matched inputs or an accepted color encoding are unavailable; the current vendor gate rejects all DLSS/XeSS requests. |
| `device_recreation_required` | A different provider was selected after device creation; restart/reopen KzGS to negotiate it. |
| `initialization_failed`, `dispatch_failed`, `present_failed`, `reset_failed`, `duplicate_present`, `device_lost` | Initialization, evaluation, presentation, history/reset, duplicate-frame, or device failure; native presentation is used when possible. |

The launcher's **Validated enhancements** preset selects the measured stock-game setup: D3D12, VSync, automatic
internal scale, 16x anisotropy, highest mip detail, clean PS2 effects, replacement textures off, 2x view distance,
and 30% sharpening.

## Display and game options

- **Show FPS** displays a counter in the top-left corner.
- **Skip intro movies** skips the long boot intro. Story cutscenes, menu backgrounds, and attract movies are unchanged.
- **Skip logos** separately skips the Sony and Guerrilla logo screens.
- Keyboard/mouse button prompts switch back to PlayStation prompts when a controller is used.

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
