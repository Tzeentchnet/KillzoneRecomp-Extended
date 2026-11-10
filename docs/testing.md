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
The `effective_config.presentation_upscaler` object distinguishes the requested provider and sharpening from the
effective provider, sharpening, and fallback reason. All providers currently remain inactive. The KzGS D3D11 golden
test checks that an unavailable request preserves the existing presentation, including CAS.

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
