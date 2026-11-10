# Testing

## Product self-test

The product self-test needs no retail content:

```powershell
.\build\win-msvc-rel\killzone.exe --selftest
```

It covers configuration migration and round-tripping, field-of-view math, renderer contracts, input, launcher
behavior, and other pure product checks. Presentation-upscaler checks include defaults, strict INI/environment/CLI
validation and precedence, canonical saves, and one-run override non-persistence.

## ISO-free regression suite

Build and run the complete hermetic suite under both supported Windows compilers:

```powershell
.\tools\scripts\cmake_preset.ps1 -Preset win-msvc-tests -Action build
.\tools\scripts\cmake_preset.ps1 -Preset win-msvc-rel -Action test -Label iso-free

.\tools\scripts\cmake_preset.ps1 -Preset win-clang-tests -Action build
.\tools\scripts\cmake_preset.ps1 -Preset win-clang-rel -Action test -Label iso-free
```

CTest supplies an isolated missing configuration through `KZ_CONFIG` and sets `KZ_ISO_FREE=1`. Disc checks must fail
before opening any path. BYOD tests remain separately labeled and disabled unless configured with an explicit
authorized image.

Changes that affect generated code or hook reachability should also run:

```powershell
python .\tools\scripts\verify_deps.py --strict
python .\tools\scripts\verify_resume_table.py .\generated
python .\tools\scripts\inventory_generated.py --check
git diff --check
```

## Headless BYOD runs

`tools\scripts\run_headless.ps1` runs the game with explicit private content paths, scripted input, and optional
periodic screenshots. Never commit retail captures, decoded assets, traces, memory cards, or generated sources.

When `KZ_SHOT_DIR` is set, each run writes `capture.jsonl` with effective settings, render/output dimensions,
post-process order, guest-vsync frame IDs, capture points, and input transitions. Validate or compare metadata without
using filenames or wall time:

```powershell
python .\tools\scripts\verify_capture_metadata.py .\work\run1 --require-fire
python .\tools\scripts\verify_capture_metadata.py .\work\run1 --compare .\work\run2 `
  --require-fire --summary .\work\comparison-summary.json
```

The summary contains only configuration, dimensions, event/frame counts, and hashes.
The `effective_config.presentation_upscaler` object distinguishes requested/effective providers and sharpening and
the fallback reason. A `config` record follows changes observed after presentation, and its pipeline names EASU and,
when enabled, RCAS before the existing CAS pass. The summary uses the latest such configuration. D3D and software
golden tests check that unavailable requests preserve the existing presentation, including CAS.

Focused FSR1 checks (the Vulkan tests require a Vulkan GPU and `VK_LAYER_KHRONOS_validation`):

```powershell
.\tools\scripts\cmake_preset.ps1 -Preset win-msvc-tests -Action build -Target kzgs_fsr1_gpu_test
.\tools\scripts\cmake_preset.ps1 -Preset win-msvc-tests -Action build -Target kzgs_test
.\tools\scripts\cmake_preset.ps1 -Preset win-msvc-tests -Action build -Target kzgs_fsr1_test
.\tools\scripts\cmake_preset.ps1 -Preset win-msvc-tests -Action build -Target kz_gs_replay
ctest --test-dir .\build\win-msvc-rel -R 'kzgs_fsr1_reference|kzgs_fsr1_vulkan_validation|kzgs_vulkan_fsr1|kzgs_fsr1_replay' --output-on-failure
```

The SDK-free provider ABI and Vulkan lifecycle test is built and run with:

```powershell
.\tools\scripts\cmake_preset.ps1 -Preset win-msvc-tests -Action build -Target kzgs_provider_test
.\tools\scripts\cmake_preset.ps1 -Preset win-msvc-tests -Action build -Target kzgs_test
ctest --test-dir .\build\win-msvc-rel -R '^kzgs_provider_(gate|vulkan|present|idle_error|init_error|reset_error)_validation$' --output-on-failure
```

Its test-only C DLL checks sized API buffers, version and extension/feature negotiation, absent providers, missing
temporal inputs, create/reset/dispatch failures, real Vulkan blits, redraws, resize, failed presents, device loss,
actual fence retirement, and DLL unloading. The direct Vulkan-vector test enables synchronization validation; the
synthetic GS presentation test uses standard validation, and checks real renderer dispatch, idle VSync/redraw,
fallback, capture, resize, reset, and device recreation. Separate processes test retention and fail-closed behavior
when device idle, SDK initialization, or reset cannot prove completion. A separate gate test leaves the test-only
spatial-mock hook disabled and verifies both provider names, a present DLL with spatial or temporal capability,
requested/effective metadata, exact missing/invalid masks, duplicate/reset history, pixel-identical native fallback,
and explicit FSR1 selection. The hook is enabled only for generic spatial-mock lifecycle tests; the DLL does not
implement a vendor upscaler and is not staged with production binaries.

Temporal qualification and metadata-only GS observation are covered by:

```powershell
.\tools\scripts\vsenv.bat cmake --build .\build\win-msvc-rel --target killzone kzgs_temporal_contract_test kzgs_test
ctest --test-dir .\build\win-msvc-rel -R '^kz_product_selftest$|^kzgs_temporal_(contract|d3d11|vulkan_validation)$' --output-on-failure
```

The pure synthetic contract test rejects missing, mismatched and invalid inputs and checks that history commits only
after a successful matching dispatch/present. The product self-test covers guest build/kick camera candidates,
menu/movie forwarding without logo-timer changes, and pending reset delivery. D3D11 WARP and Vulkan render synthetic color/depth triangles, exercise HW draw/display/present
observations and duplicate/reset paths, and assert that no candidate becomes a valid temporal resource or history.
Vulkan uses standard validation, not synchronization validation, because of the existing synthetic GS index-upload
hazard noted below. To print protected-content-safe diagnostics for this synthetic test only:

```powershell
$env:KZGS_TEMPORAL_DIAGNOSTICS = '1'
.\build\win-msvc-rel\ext\kzgs\kzgs_test.exe --renderer vulkan --temporal-validation
Remove-Item Env:\KZGS_TEMPORAL_DIAGNOSTICS
```

These tests do not qualify real gameplay inputs, camera motion, or any vendor SDK/GPU. The current gate outcome and
the restrictions on private gameplay captures are recorded in [the qualification findings](findings.md#temporal-qualification-instrumentation-fail--no-go-2026-11-10).

SDK-free vendor-adapter checks use independently authored test doubles and do not load a vendor SDK or require a GPU:

```powershell
.\tools\scripts\vsenv.bat cmake --build .\build\win-msvc-rel --target kzgs_vendor_conventions_test kzgs_vendor_shim_test kzgs_dlss_adapter_test kzgs_xess_requirements_test kzgs_vendor_config_stub kzgs_vendor_config_partial_stub kzgs_vendor_config_negotiation_stub
.\tools\scripts\vsenv.bat ctest --test-dir .\build\win-msvc-rel -R kzgs_vendor_configuration --output-on-failure
ctest --test-dir .\build\win-msvc-rel -R 'kzgs_vendor_conventions|kzgs_vendor_shim|kzgs_dlss_adapter|kzgs_xess_requirements' --output-on-failure
```

They cover 1.0/1.1 sized buffers and initialization failures, SDK-free motion/jitter/depth conversion vectors,
missing/invalid temporal data, explicit conventions, reset/lifetime and failure handling,
XeSS requirements, initialization and evaluation, and configure tests for disabled/invalid/partial SDKs, unsupported
targets, missing lifecycle symbols, independent vendor targets, exclusion from default builds, and explicit isolated
DLL targets. The successful fake-SDK build does not constitute a real SDK build or runtime qualification.
The current host does not provide the initialization scope; see [vendor SDK adapters](vendor-upscalers.md).

The CPU vectors check AMD constants and dispatch planning. The GPU-vector test enables synchronization validation,
compares EASU with an independently compiled AMD native-gather reference and odd cropped rectangles with tightly
packed inputs, checks edge/partial-tile writes and RCAS, and exercises failure cleanup and retry. The presentation
test checks active/fallback status, initialization failure and retry, resize/recreation, separate RCAS and CAS
effects, and repeated fixed-aspect/stretch capture without redispatch. The replay test generates a synthetic GS trace and compares
presented readback with burst output; it also checks the optional debug replay tool when built. PCSX2's existing
synthetic-scene index upload produces a
`SYNC-HAZARD-READ-AFTER-WRITE` under synchronization validation even with FSR off; synchronization validation is
therefore isolated to the direct FSR-vector test, while the full presentation test uses standard validation.
PCSX2 also emits the informational deprecated-device-layer VUID on this validation-layer version.

For active Vulkan FSR1 or an external provider, presented-size readback and negative-size bursts recomposite the
completed post-upscaler/post-CAS texture into the requested output rectangle; they do not rerun the upscaler. This
is not a literal swapchain-image readback. Internal-resolution captures and native presentation retain their existing
behavior.

For deterministic scheduling, use `KZ_SHOT_GUEST_INTERVAL=<vblanks>` and optionally
`KZ_SHOT_GUEST_FROM=<first-vblank>`. Prefix a start with `g`, such as `g60`, to count from the first gameplay frame.
Scripted input supports the same gameplay-relative prefix:

```powershell
$env:KZ_INPUT_SCRIPT = 'g0.5:Start'
python .\tools\scripts\image_compare.py .\work\baseline .\work\variant `
  --glob 'gs_*.png' --align-metadata
```

Each headless run also writes `process.json` with sampled memory peaks.

## Graphics qualification

The campaign gate runs fresh-card baseline and render-distance arms:

```powershell
.\tools\scripts\gfx03_campaign_gate.ps1 -Chapters 1,5,10 -Card .\work\<test-card> -Seconds 90 `
  -ExeDir .\build\win-msvc-rel -Iso <image> -Elf <SCUS_974.02>
```

It requires the chapter to load, stable dimensions, no guest/host exception, the original baseline path, measured
far/fog transformations in the 2x arm, and sustained presented gameplay. Its committed summary is
protected-content-safe.

Live comparisons are not necessarily pixel-deterministic. Align on guest events, record same-arm variance, and do
not attribute changes to a setting without a positive hook or telemetry signal. Detailed renderer evidence is in
[findings](findings.md).

## Audio diagnostics

Start with the callback ledger so producer-side file I/O cannot perturb the first measurement:

```powershell
$env:KZ_AUDIO_DIAG = "$PWD\work\audio-callback.csv"
& .\build\win-msvc-rel\killzone.exe --launcher
python .\tools\scripts\summarize_audio_diag.py .\work\audio-callback.csv `
  --summary .\work\audio-summary.json
```

The ledger records SPU2 ring depth, deliberate skips, underrun silence, queue depth, callback gaps, and stream
submission failures. Narrow a known event with `--from-seconds` and `--to-seconds`. Add `KZ_AUDIO_WAV=<path>` only in
a targeted follow-up: the raw tap writes on the producer thread and can change timing.

The product reserve/high-water defaults are 120/320 ms. Diagnostics can override them with
`KZ_AUDIO_BUFFER_MS=10..400` and `KZ_AUDIO_HIGH_MS=<reserve+10>..480`. The known combat-audio issue remains open;
larger buffers are not accepted without a new latency decision.

## Evidence rules

- Prefer synthetic inputs for unit and component tests.
- Pass every private path explicitly; tools must not discover retail content.
- Preserve only hashes, counts, timings, dimensions, bounds, settings, and other non-content evidence.
- A compile is not runtime acceptance.
- Use repeated runs for intermittent failures and keep baseline and variant conditions identical.
- A disabled feature must preserve measured original behavior.
