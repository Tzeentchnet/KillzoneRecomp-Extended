# Release and distribution

This project is bring-your-own-disc software. Source control and release artifacts must never contain the retail disc
image, extracted executable, generated game-derived source, function maps, decoded assets, memory cards, traces, or
regeneration logs.

## License boundary

Repository-owned code and documentation are MIT under [`LICENSE`](../LICENSE), except for the components identified
there as GPL-3.0. PCSX2-derived `ext\kz*` libraries, PS2Recomp, the field-of-view contribution, and every compiled
build remain GPL-3.0; see [`LICENSE-GPL-3.0`](../LICENSE-GPL-3.0).

Third-party MIT, zlib, LGPL, GPL, and other components retain their own notice and corresponding-source obligations.
The dependency lock is an inventory and integrity gate, not a substitute for license review.

AMD FidelityFX FSR 1's `ffx_a.h` and `ffx_fsr1.h` are vendored unmodified from
[`GPUOpen-Effects/FidelityFX-FSR`](https://github.com/GPUOpen-Effects/FidelityFX-FSR) commit
`a21ffb8f6c13233ba336352bdff293894c706575`, under the MIT license. Keep
[`ext/kzgs/third_party/fsr1/LICENSE.txt`](../ext/kzgs/third_party/fsr1/LICENSE.txt) and its provenance notice with
source distributions; runtime packaging stages them in `resources/shaders/kzgs/fsr1`. Review the combined GPL
distribution obligations separately. No proprietary upscaler SDK is bundled.
Optional local vendor builds have separate [license and provenance records](vendor-upscalers.md); their
redistribution, including any combined binary, is not approved by this work.

## Protected content

Only non-content provenance may be committed:

- cryptographic hashes;
- function and table counts;
- address bounds;
- hook signature hashes;
- test settings, timings, dimensions, and aggregate results.

Scripts that consume retail inputs require explicit paths. BYOD tests remain disabled by default and must not search
parent directories, user profiles, the registry, or old build trees.

## Release checklist

Every release candidate requires:

1. Clean MSVC and clang-cl builds and the full ISO-free suite.
2. Explicit BYOD boot, gameplay, save/load, cutscene, FMV, resize, shutdown, and soak checks.
3. Dependency hash verification and a package allowlist.
4. Complete dependency notices and corresponding-source review.
5. An SBOM.
6. Secret and protected-content scans.
7. Review of FFmpeg configuration and any dependency marked `NOASSERTION` or review-required.
8. Confirmation that proprietary SDK binaries and excluded third-party bundles are absent.
9. Human approval of the artifact's legal, trademark, and distribution evidence.

Killzone is a trademark of Sony Interactive Entertainment. The project is not affiliated with Sony or Guerrilla
Games and must not package their logos, branding, or protected assets.
