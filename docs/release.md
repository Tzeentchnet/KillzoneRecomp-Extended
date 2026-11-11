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
`a21ffb8f6c13233ba336352bdff293894c706575`, copyright 2021 Advanced Micro Devices, Inc., under MIT. Keep the
[complete license and copyright notice](../ext/kzgs/third_party/fsr1/LICENSE.txt) and
[source revision and file hashes](../ext/kzgs/third_party/fsr1/README.md) with source distributions; runtime
packaging stages the headers, license and provenance notice in `resources/shaders/kzgs/fsr1`.
[`ffx_a.h`](../ext/kzgs/third_party/fsr1/ffx_a.h) also contains an embedded MIT notice for copyright 2014 Michal
Drobot (float-approximation concepts); retain that notice as well. Review the combined GPL distribution obligations
separately. No proprietary upscaler SDK is bundled.

The experimental FSR 3 provider is built from locked FidelityFX SDK `v2.4.0` and miniz `3.1.2` sources. Ordinary
build/test staging places the provider beside the executable and reproduces
`Kits\FidelityFX\docs\license.md`, `3rdpartynotice.md`, and the miniz license under `upscalers\licenses`.
Those staged files are development artifacts, not automatic release approval. The audited player-package plan still
excludes every provider DLL, including FSR 3, until a deliberate GPL/SDK notice, corresponding-source, binary-import,
and redistribution review updates that plan and its tests.

The renderer also builds the [Tzeentchnet/pcsx2](https://github.com/Tzeentchnet/pcsx2) companion at
`1fb920189c12766af55bdc9178619c0c66eb1a5e`, pinned in
[`config/dependencies.lock.json`](../config/dependencies.lock.json). It is GPL-3.0-or-later; provide the
corresponding source and notices for the exact distributed build, not just a link to unmodified upstream PCSX2.
The companion revision is published on `feature/kzgs-upscaler-foundation`; retain that exact revision and its
corresponding source when preparing a release.
Vendor providers have separate [license and provenance records](vendor-upscalers.md). DLSS/XeSS redistribution and
distribution of the source-built FSR 3 combined binary are not approved by this work.

## Protected content

Only non-content provenance may be committed:

- cryptographic hashes;
- function and table counts;
- address bounds;
- hook signature hashes;
- test settings, timings, dimensions, and aggregate results.

Scripts that consume retail inputs require explicit paths. BYOD tests remain disabled by default and must not search
parent directories, user profiles, the registry, or old build trees.

## Package inventory and dry-runs

[`config/package-allowlist.json`](../config/package-allowlist.json) pins exact SHA-256 hashes for the audited x64
SDL, FFmpeg and PCSX2-bundle runtime DLLs and the six Visual C++ release-runtime DLLs. The allowlist includes the
audited import closure; it is not permission to redistribute any component. The executable must be an x64 PE
executable, not a renamed DLL. Resources are selected only from the known prompt and shader source directories:
their complete inventory digest is pinned. Shader and other ordinary text resources are compared with CRLF normalized
to LF, so checkout line endings do not alter the inventory; binary files and the unmodified upstream FSR1 headers
and license remain byte-exact. Git attributes disable text conversion for those vendored files. The staged bytes are
still hashed and checked exactly when copied into the package
and archive. Unlisted files and directories are never copied. The package includes the repository MIT and GPL
license texts and the FSR1 notice.

Dry-run both stages without creating a destination:

```powershell
.\tools\scripts\package.ps1 -Config win-msvc-rel -Destination .\work\package-preview -DryRun
.\tools\scripts\make_release.ps1 -Version 0.1.0 -From .\build\win-msvc-rel `
  -OutputDirectory .\work\release-preview -DryRun
.\tools\scripts\package_selftest.ps1 -BuildDirectory .\build\win-msvc-rel
```

An actual package requires a **new or empty** destination. The script refuses to update a nonempty player folder
and never deletes saves, settings, or caches; use a fresh staging directory, then back up player data before any
manual update. `-WithPdb` explicitly adds only `killzone.pdb` to a private staging folder; it is absent by default
and never included by the release ZIP script. Local SDK roots, import libraries, provider/vendor DLLs (including the staged FSR 3 provider), symbols,
shader caches, and unlisted files are excluded even when present in the input tree. Release archiving uses the same
file plan rather than recursively zipping the stage.

The release script locates an x64 Visual C++ redistributable or accepts `-CrtDirectory`; its files must match the
pinned version. If a DLL, source resource, or CRT version changes, inspect its provenance, transitive imports,
licenses, notices, and corresponding-source obligations before deliberately updating the allowlist. Do not copy a
local vendor SDK into the package or update its hashes to make the check pass.

These are **inventory and integrity checks, not release approval**. They do not prove the executable or a separate
source distribution is free of protected content, provide corresponding source, establish redistribution rights,
or replace the scans and human reviews below. Never include protected generated game source to satisfy a source
request; resolve the legal and distribution requirements separately before publication.

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
