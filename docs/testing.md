# Testing

## Product self-test

The product self-test needs no retail content:

```powershell
.\build\win-msvc-rel\killzone.exe --selftest
```

It covers configuration migration and round-tripping, field-of-view math, renderer contracts, input, launcher
behavior, controller-family mapping and procedural controller glyphs, and other pure product checks.
Presentation-upscaler checks include defaults, strict INI/environment/CLI validation and precedence, canonical
saves, and one-run override non-persistence.

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

### Upscaler validation baseline (2026-11-10)

The completed implementation validation covered superproject `ccd5adc1a46b7fb85bc6bc1079902d04cb54b75c` with
companion PCSX2 `61570a47840d2f14e802456eb4524ae5b0a597c7`. It was ISO-free except where stated otherwise:

| Check | Recorded result and scope |
|---|---|
| MSVC and clang-cl test builds, strict configure preflights, ISO-free CTest | **33/33 passed per compiler**, including the product, FSR1, provider, temporal, synthetic replay, and D3D11/D3D12 WARP/software tests. Nine Vulkan-validation tests ran per compiler. |
| Vulkan FSR1 and renderer validation | Direct FSR1/provider vectors used synchronization validation; full synthetic GS presentation used standard validation because of the existing index-upload hazard described below. These are not gameplay image-quality tests. |
| Additional D3D12 hardware/WARP | **4/4 passed** for resize, 720p–4K, minimize/restore, and injected device loss. Synthetic D3D11 SMAA+CAS activation and unavailable-provider pixel equivalence passed **2/2**. |
| Packaging and provenance | Package self-tests passed **10 checks per compiler**, including excluded SDK/protected-content decoys. Dependency, capture-metadata, and image-comparison self-tests passed; strict dependency verification passed **31/31**. |
| Generated sources | Resume-table verification passed with **132,656 dispatch entries** and **100,661 resume cases**. `inventory_generated.py --check` failed with **35 legacy unused-hook findings**, reproduced identically with no extras on clean pre-upscaler `7f4f8263cc94d953d6a5ef8f3ff702e4a51fdb1a` and PCSX2 `d3e6982a3e36647d642bcd805d3ca5b442d0975c`. This is a known failing baseline, not a passed gate. |

Vulkan BYOD A/B was **skipped**, not passed: neither `KZ_ISO` nor `KZ_BYOD_ISO` named an explicitly authorized image.
The PAL generated tree was unavailable, so PAL hook execution was not tested. At that recorded baseline there was no
FSR 3 provider; no real DLSS/XeSS SDK, vendor runtime, or vendor-GPU integration was qualified. The strict measured
temporal gate remains **FAIL/NO-GO**; see
[GFX-04 and the temporal observations](findings.md#temporal-qualification-instrumentation-fail--no-go-2026-11-10).

### Source-built FSR 3 validation

Provision and build the locked FidelityFX path, then run its focused test:

```powershell
.\tools\scripts\provision_fidelityfx.ps1
.\tools\scripts\vsenv.bat cmake --build .\build\win-msvc-rel --target kzgs_test kzgs_upscaler_fsr3
.\tools\scripts\vsenv.bat ctest --test-dir .\build\win-msvc-rel -C RelWithDebInfo `
  -R kzgs_fsr3_vulkan_validation --output-on-failure
```

`kzgs_fsr3_vulkan_validation` runs the real source-built FidelityFX FSR 3.1.6 and Optical Flow code against an
ISO-free synthetic GS scene. It verifies native bootstrap fallback and exact jitter geometry, non-empty active
output distinct from fallback, a translated second frame and committed history, missing-input fallback, recovery,
resize/recreation, absent composition/UI masks, and forced partial FSR/Optical Flow context-creation cleanup.
The latter two failure probes must release every Vulkan object under validation.

The test passes **standard** Vulkan validation on the checked NVIDIA GeForce RTX 5070. It sets
`VK_LAYER_VALIDATE_SYNC=0` because the baseline synthetic GS index-buffer upload/read hazard occurs before FSR work;
do not report this full integration test as synchronization-validation clean. The scene is not evidence of gameplay
image quality, and experimental approximation support does not change the strict temporal FAIL/NO-GO result.

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
the fallback reason, provider result codes, temporal observation/validity, and valid/missing/invalid input and reason
masks. A `config` record follows changes observed after presentation, and its pipeline names EASU and, when enabled,
RCAS before the existing CAS pass. The summary uses the latest such configuration. D3D and software golden tests
check that unavailable requests preserve the existing presentation, including CAS.

### High-refresh campaign audit

`tools\scripts\high_refresh_campaign_audit.ps1` runs chapter scenarios sequentially at 30, 60, 120 and 144 Hz.
Every run receives a fresh copy of the supplied private memory card and writes its raw logs, captures, card, and
`KZ_PARITY_LOG` trace only under ignored `work\`. It never runs rates concurrently. The 30 Hz run is the reference;
`tools\scripts\high_refresh_analyze.py` compares game-time rate, configured guest-word transition sequences and
transition times, and optional final player position. Its JSON summary contains counts, timing deltas, pass/fail
reasons, and a scenario hash, not captures, memory values, positions, paths, or game data.

The scenario manifest is intentionally required. A run that merely enters every chapter without crashing is a smoke
test, not campaign timing parity; the analyzer reports `incomplete` unless every requested chapter supplies at least
one of `audit_words`, `max_final_position_delta`, or a scenario-specific `required_log_patterns` check. Guest-word
transitions are the strongest check for doors, platforms, dialogue states, waves, and completion conditions:

```json
{
  "schema_version": 1,
  "chapters": [
    {
      "chapter": 1,
      "input_script": "g1:ly=-1:5",
      "min_gameplay_seconds": 30,
      "parity_words": ["c:0x100"],
      "audit_words": [
        {
          "name": "c:0x100",
          "min_transitions": 1,
          "time_tolerance_s": 0.25
        }
      ],
      "max_final_position_delta": 1.0,
      "environment": {
        "KZ_GOD": "1"
      }
    }
  ]
}
```

The address above is illustrative, not a discovered campaign state. Each chapter needs a private, researched route
and observable specific to the sequence being audited. Run a selected set with:

```powershell
.\tools\scripts\high_refresh_campaign_audit.ps1 `
  -Chapters 1,5,10 -Card .\work\audit-card -Scenario .\work\high-refresh-scenarios.json `
  -Iso $env:KZ_ISO -Elf $env:KZ_GAME_ELF
```

Before a private run, validate the content-safe analyzer with:

```powershell
python .\tools\scripts\high_refresh_analyze.py --self-test
```

No complete 11-chapter scenario/result matrix has been recorded yet, so campaign-wide 30/60/120/144 Hz parity
remains unverified. Do not add a fixed-step script hook from another port without first identifying this game's
script update and message-delivery semantics and producing a failing local scenario.

Focused FSR1 checks (the Vulkan tests require a Vulkan GPU and `VK_LAYER_KHRONOS_validation`):

```powershell
.\tools\scripts\cmake_preset.ps1 -Preset win-msvc-tests -Action build -Target kzgs_fsr1_gpu_test
.\tools\scripts\cmake_preset.ps1 -Preset win-msvc-tests -Action build -Target kzgs_test
.\tools\scripts\cmake_preset.ps1 -Preset win-msvc-tests -Action build -Target kzgs_fsr1_test
.\tools\scripts\cmake_preset.ps1 -Preset win-msvc-tests -Action build -Target kz_gs_replay
ctest --test-dir .\build\win-msvc-rel -R 'kzgs_fsr1_reference|kzgs_fsr1_vulkan_validation|kzgs_vulkan_fsr1|kzgs_fsr1_replay' --output-on-failure
```

The direct GPU test also simulates successful waits, device loss, and non-device-loss idle failures, checking that
the latter never explicitly destroy potentially in-flight children.

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
after a successful matching dispatch/present. Blocking and successfully queued non-blocking temporal VSync submissions
copy guest metadata before returning. The product self-test covers guest build/kick camera candidates,
menu/movie forwarding without logo-timer changes, and pending reset delivery. D3D11 WARP and Vulkan render synthetic color/depth triangles, exercise HW draw/display/present
observations and duplicate/reset paths, verify raw-matrix structural traits and reset-safe consecutive-build pairing,
and assert that no candidate becomes a valid temporal resource or history.
Vulkan uses standard validation, not synchronization validation, because of the existing synthetic GS index-upload
hazard noted below. To print protected-content-safe diagnostics for this synthetic test only:

```powershell
$env:KZGS_TEMPORAL_DIAGNOSTICS = '1'
.\build\win-msvc-rel\ext\kzgs\kzgs_test.exe --renderer vulkan --temporal-validation
Remove-Item Env:\KZGS_TEMPORAL_DIAGNOSTICS
```

These tests do not qualify real gameplay inputs, camera motion, or any vendor SDK/GPU. The current gate outcome and
the restrictions on private gameplay captures are recorded in [the qualification findings](findings.md#temporal-qualification-instrumentation-fail--no-go-2026-11-10).

An authorized SCES-52004 chapter-1 follow-up kept the ISO, generated code, card, captures, and raw logs under ignored
`work\`. Its content-safe counts found no unambiguous depth frame in 5,213 presented world observations. A shorter
post-change run found 927 reset-free consecutive same-camera matrix pairs, but only finite/nonzero structural traits,
so no matrix semantics were promoted. With Vulkan, `KZ_UPSCALE=1`, FSR 3, and explicit `experimental` input policy,
209/772 world observations carried matched jitter and 207 passed the approximation-only dispatch contract; unmatched
VSyncs fell back. This is execution evidence for the experimental path, not strict temporal or image-quality
qualification.

`verify_capture_metadata.py` accepts FSR 3 status, validates either strict or experimental required-input masks, and
requires an effective FSR 3 provider to report temporal validity and the `provider_fsr3` pipeline position:

```powershell
python .\tools\scripts\verify_capture_metadata.py --selftest
python .\tools\scripts\verify_capture_metadata.py .\work\<private-run>
```

SDK-free vendor-adapter checks use independently authored test doubles and do not load a vendor SDK or require a GPU:

```powershell
.\tools\scripts\vsenv.bat cmake --build .\build\win-msvc-rel --target kzgs_vendor_conventions_test kzgs_vendor_shim_test kzgs_dlss_adapter_test kzgs_xess_requirements_test kzgs_vendor_config_stub kzgs_vendor_config_partial_stub kzgs_vendor_config_negotiation_stub
.\tools\scripts\vsenv.bat ctest --test-dir .\build\win-msvc-rel -R kzgs_vendor_configuration --output-on-failure
ctest --test-dir .\build\win-msvc-rel -R 'kzgs_vendor_conventions|kzgs_vendor_shim|kzgs_dlss_adapter|kzgs_xess_requirements' --output-on-failure
```

They cover 1.0/1.1/1.2 sized buffers, approximation provenance, scalar-camera validity, and initialization failures,
SDK-free motion/jitter/depth conversion vectors,
missing/invalid temporal data, explicit conventions, reset/lifetime and failure handling,
XeSS requirements, initialization and evaluation, and configure tests for disabled/invalid/partial SDKs, unsupported
targets, missing lifecycle symbols, independent vendor targets, exclusion from default builds, and explicit isolated
DLL targets. The successful fake-SDK build does not constitute a real SDK build or runtime qualification.
See [vendor SDK adapters](vendor-upscalers.md) for the separate source-built FSR 3 path and DLSS/XeSS restrictions.

Run the dependency, capture, and image self-tests and the packaging checks with:

```powershell
python .\tools\scripts\verify_deps.py --selftest
python .\tools\scripts\verify_deps.py --strict
python .\tools\scripts\verify_capture_metadata.py --selftest
python .\tools\scripts\image_compare.py --selftest
.\tools\scripts\package_selftest.ps1 -BuildDirectory .\build\win-msvc-rel
.\tools\scripts\package.ps1 -Config win-msvc-rel -Destination .\work\package-preview -DryRun
.\tools\scripts\make_release.ps1 -Version 0.1.0 -From .\build\win-msvc-rel `
  -OutputDirectory .\work\release-preview -DryRun
```

The packaging self-test uses real audited dependency hashes and a fresh fixture directory under `work`. It
contaminates fixture inputs with vendor/provider DLLs, SDK headers and import libraries, PDBs,
caches, and protected-content-shaped decoys. It verifies LF/CRLF source and staging combinations against the same
resource inventory and rejects altered binary, vendored FSR1, and shader bytes. It also checks that Git attributes
disable text conversion for the byte-exact vendored headers and license. It checks both dry-runs, the actual staging
directory and ZIP contents, the staged executable's ISO-free self-test, required license files, a substituted DLL/CRT
hash, a DLL disguised as the executable, and preservation of an occupied destination. The source and legal reviews in
[release and distribution](release.md) remain mandatory.

The CPU vectors check AMD constants and dispatch planning. The GPU-vector test enables synchronization validation,
compares EASU with an independently compiled AMD native-gather reference and odd cropped rectangles with tightly
packed inputs, checks edge/partial-tile writes and RCAS, and exercises failure cleanup and retry. It also simulates
successful idle, device loss, and a non-device-loss idle error after recorded FSR1 work; the last case retains every
child until completion is proven, rather than destroying it in flight. The presentation
test checks active/fallback status with the default SharpScaling enabled, pre-prescale source dimensions,
initialization failure and retry, resize/recreation, separate RCAS and CAS effects, and repeated fixed-aspect/stretch
capture without redispatch. The replay test generates a synthetic GS trace and compares labeled diagnostic
recompositions with presented-size readback for that trace; it also checks the optional debug replay tool when built. PCSX2's existing
synthetic-scene index upload produces a
`SYNC-HAZARD-READ-AFTER-WRITE` under synchronization validation even with FSR off; synchronization validation is
therefore kept on direct FSR1/provider vector tests, while the full presentation test uses standard validation.
PCSX2 also emits the informational deprecated-device-layer VUID on this validation-layer version.

For active Vulkan FSR1 or the test-only spatial mock, presented-size readback and negative-size bursts can
recompose a cached image without rerunning the upscaler. A skipped or failed present may instead return a pre-CAS
or current-frame snapshot. Negative-size Vulkan bursts are labeled `presentation_recomposition`, not
`swapchain_backbuffer`, and their replay files use the `recomposed_` prefix. Do not treat them as evidence of a
successful present or compare them with literal backbuffer captures. Internal-resolution captures and native
presentation retain their existing behavior. Captures built with the reviewed companion API use pipeline contract 2:
sharp prescale appears only on the fallback path. Builds using the previously locked companion emit contract 1
instead, and the validator warns that active-upscaler pipeline order was not verified. The API capability check is
supplemented by the SharpScaling integration regression.

For an ISO-free, synthetic replay using the targets built above:

```powershell
.\build\win-msvc-rel\ext\kzgs\kzgs_test.exe --write-synthetic-trace .\work\synthetic-fsr1.gs
.\build\win-msvc-rel\kz_gs_replay.exe .\work\synthetic-fsr1.gs .\work\fsr1-replay `
  --renderer vulkan --upscale 1 --upscaler fsr1 --upscaler-sharpening 60 `
  --cas 30 --bilinear 1 --present 1003x757 --every 3 --backbuffer
```

`--upscale` controls internal GS scaling, `--upscaler-sharpening` controls provider sharpening, and `--cas` controls
the separate later pass. FSR 3 additionally requires `--fsr3-input-mode experimental` to allow marked
approximations. Compare a separate `--upscaler off` run under otherwise identical settings. `kz_gs_replay`
creates a hidden window; the optional `kzgs_replay` debug tool needs `--window --present WxH` for a surface.
Both print initial and final requested/effective status and accept the same one-run environment/CLI controls without
reading the INI. Synthetic traces are not gameplay qualification; keep any authorized retail trace or capture private.

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
