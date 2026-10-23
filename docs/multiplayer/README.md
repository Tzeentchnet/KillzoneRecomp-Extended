# Killzone multiplayer restoration

- **Status:** experimental online-client bring-up; not yet playable
- **Branch:** `MPwork`
- **Last verified:** 2026-10-23
- **Task queue:** [`TODO.md`](TODO.md)

## Goal

The minimum viable result is two legally provisioned Killzone clients on the same local network entering and playing
the same match through a locally hosted service.

Retail Killzone does not contain a native LAN mode. Its offline split-screen game and its online game are different
execution paths. The practical route to LAN play is therefore:

1. reconstruct the retail Killzone Online client;
2. replace obsolete PS2 network and DNAS dependencies with local-compatible host services;
3. redirect the client to a local Medius-compatible server;
4. prove login, lobby, join, gameplay, and disconnect behavior with two clients.

The candidate server is [hashsploit/clank](https://github.com/hashsploit/clank), whose Killzone configuration records:

| Setting | Retail value |
|---|---:|
| Application ID | `10724` |
| MUIS host | `killzone-prod.muis.pdonline.scea.com` |
| MUIS port | `10071` |
| Universe/game-service port | `10075` |

No Clank integration has been completed yet. These values define the intended local-service target, not a claim that
the current executable can log in.

## Current state

| Area | State | Evidence |
|---|---|---|
| Separate online executable | Working | `KZ_ONLINE_BUILD` produces `killzone-online.exe` |
| Retail online asset extraction | Working | `LOADER.ELF`, four REL modules, and `DNAS300.IMG` are extracted from the authorized disc |
| SNR2 parsing and relocation | Working for staged modules | Synthetic tests pass; loader, BLGFX, DNASMC, and ONLINE have been inspected and relocated |
| Loader execution | Working | The online loader boots in the native runtime |
| BLGFX and DNASMC startup | Working | Their recovered constructors execute |
| ONLINE startup | Partially working | All 503 static constructors and the indexed client export execute |
| Content/IOP startup | Partially working | `PFILE_R.IRX` loads and reports `FileStreamer activated!` |
| NETWORK module | Not running | Its deterministic runtime base is still unknown |
| DNAS | Not implemented | No bypass or local compatibility layer exists |
| Native TCP/UDP | Not implemented | No `sceInet` host bridge exists |
| Local Medius service | Not integrated | No client login against Clank has been attempted |
| Multiplayer UI/gameplay | Not working | No menu, account, lobby, match join, or network gameplay is available |

The latest smoke test advances past callback `0x007C18E8` without a missing-target or runtime error and remains at
guest PC `0x007651A8`. That point is the next runtime investigation target; it is not evidence of a working menu or
network connection.

## Retail online architecture

`SCUS_974.02` owns the offline campaign and split-screen modes. Selecting online play launches a separate
`LOADER.ELF`, which loads four SN Systems ProDG SNR2 modules:

| Module | Role | Recovered placement |
|---|---|---:|
| `BLGFX.REL` | Online loading graphics | `0x0021C700` |
| `DNASMC.REL` | DNAS client support | `0x002BF600` |
| `NETWORK.REL` | PS2 network stack integration | Unknown |
| `ONLINE.REL` | Online UI, content, and game client | `0x0035EA00` |

Campaign and online guests cannot safely share one executable because their guest address spaces overlap and the
campaign host hooks target fixed campaign addresses. The port therefore builds them separately:

```text
SCUS_974.02 + campaign generated tree -> killzone.exe
LOADER.ELF + relocated REL modules   -> killzone-online.exe
```

The online build remains at the retail 60 Hz compatibility rate and does not install campaign-only timing, input,
post-processing, detail, test automation, or stability hooks.

## Verified reverse-engineering facts

- The REL files use the SNR2 format rather than ELF.
- The loader's linked SNR2 table is at guest address `0x0017B800`.
- Across the loader and all four modules, all 175 unique imports have providers.
- The loader table contains 748 symbols, 732 nonzero exports, and 23 linked named relocations.
- The SNR2 resident image size is the named-relocation-table offset (`header[1]`), not the complete REL container.
- ONLINE contains one indexed export at module offset `0x5C0`, mapped to guest address `0x0035EFC0`.
- ONLINE's constructor table contains 503 entries.
- A staged bundle without NETWORK intentionally leaves 31 ONLINE imports unresolved.
- Loader imports `fcntl`, `link`, and `times` are also expected to close when NETWORK is placed.
- The filtered private function map now contains 17,761 records.
- PS2Recomp reports zero decode failures and the same 877 unhandled instructions inherited from the loader-only
  analysis. No new ONLINE analyzer failures have been introduced.

Runtime-proven initialization has reached core objects, resource management, world nodes, materials, meshes,
static meshes, drawables, textures, animated textures, and fonts. NETWORK code or a network API boundary has not yet
been observed.

## Tracked implementation

### Build separation

[`CMakeLists.txt`](../../CMakeLists.txt) provides:

- `KZ_ONLINE_BUILD` for the loader variant;
- `KZ_GENERATED_DIR` for a private generated source tree;
- `killzone-online.exe` as the online output name;
- stable address-based unity groups for incremental generated-code builds.

[`src/kz_main.cpp`](../../src/kz_main.cpp) selects the correct guest and rejects campaign/online mismatches.
[`src/kz_timing.cpp`](../../src/kz_timing.cpp) disables campaign address patches and keeps the online guest at 60 Hz.

### Authorized-disc extraction

[`src/kz_iso.cpp`](../../src/kz_iso.cpp) extracts the online loader, module files, DNAS image, and required IOP
modules from the user's authorized disc. Retail inputs and generated derivatives remain ignored and must never be
committed.

### SNR2 reconstruction

[`tools/scripts/kz_snr2.py`](../../tools/scripts/kz_snr2.py):

- validates SNR2 headers, extents, alignments, symbol tables, and indexed exports;
- decodes packed and named relocations;
- supports relocation types 2, 4, 5, and 6;
- resolves loader and cross-module imports;
- patches loader-linked relocation sites before static recompilation;
- trims modules to their resident images;
- appends relocated modules as executable ELF segments and sections;
- restricts generated bundle output to ignored `work/`.

[`tools/scripts/test_kz_snr2.py`](../../tools/scripts/test_kz_snr2.py) covers relocation behavior, malformed input,
wide symbol indices, loader patching, image trimming, and indexed exports.

### Runtime bridges

[`src/kz_online_overrides.cpp`](../../src/kz_online_overrides.cpp) installs loader-specific host overrides for:

- SIF module stop, unload, and lookup operations needed in the BIOS-less runtime;
- verified ONLINE CDVD SDK entry points backed by the existing mounted-ISO handlers.

These bridges allow the loader and ONLINE content bootstrap to proceed without pretending that an unavailable RPC
service succeeded.

### Incremental generated-code workflow

[`tools/scripts/sync_recompiled_output.py`](../../tools/scripts/sync_recompiled_output.py) runs PS2Recomp into a
staging tree, localizes direct-call declarations, and copies only content changes into the selected generated tree.
[`tools/scripts/regen.py`](../../tools/scripts/regen.py) uses the same synchronization path for the campaign.

The online measurement after adding callback `0x007C18E8` was:

| Operation | Result |
|---|---:|
| Generated files changed | 4 |
| Guest unity groups compiled | 1 (`guest_7c1`) |
| Registration groups compiled | 1 |
| Executable links | 1 |
| Subsequent unchanged regeneration | 0 changed files |
| Subsequent unchanged build | 0 compiles, 0 links |

Before this change, adding one generated function rebuilt approximately 140 generated unity objects.

## Validation snapshot

The `MPwork` branch was validated on Windows with clang-cl on 2026-10-23:

| Gate | Result |
|---|---|
| Python tool tests | 14 passed |
| Online target | `killzone-online.exe` current; zero compile or link work after unchanged regeneration |
| Campaign target | `killzone.exe` rebuilt successfully after the stable-unity migration |
| ISO-free CTest suite | 13/13 passed |
| Online headless smoke | 30 seconds; `PFILE_R.IRX` active, no missing target or guest/runtime error |

The first build from an older clang-cl build directory may need a one-time PCH refresh because the generated
declaration headers were removed from the PCH. A newly configured build directory does not carry that stale
artifact.

## Private developer workflow

All commands operate on user-supplied retail files. Do not copy retail binaries, generated C++, function maps,
manifests containing private paths, or runtime dumps into source control.

Inspect import closure:

```powershell
python .\tools\scripts\kz_snr2.py inspect `
  --loader <private-loader-path> `
  <private-module-paths...>
```

Create a staged synthetic ELF under ignored `work/`:

```powershell
python .\tools\scripts\kz_snr2.py bundle `
  --loader <private-loader-path> `
  --module <private-module-path>@<guest-base> `
  --out .\work\multiplayer\online.elf
```

Until NETWORK is placed, partial ONLINE bundles require `--allow-unresolved`. The complete four-module bundle must
remove that option and reach zero unresolved imports.

Run PS2Recomp without invalidating unchanged outputs:

```powershell
python .\tools\scripts\sync_recompiled_output.py `
  --config <private-recompiler-config> `
  --log .\work\multiplayer\recompiler.log
```

Configure and build the separate online executable:

```powershell
cmake -S . -B <online-build-dir> -G Ninja `
  -DKZ_ONLINE_BUILD=ON `
  -DKZ_GENERATED_DIR=<private-generated-tree>
cmake --build <online-build-dir> --target killzone
```

For a noninteractive smoke test, set `PS2X_HEADLESS=1` and pass `--no-launcher` plus an explicitly quoted `--iso`
argument. `killzone-online.exe` does not implement a `--headless` command-line option.

## Current limitations

- The tracked repository contains the private-safe reconstruction tools, not retail binaries or generated game code.
- The current ONLINE function-map selection and runtime evidence live under ignored `work/`; a clean checkout needs
  authorized private inputs and a regenerated map.
- NETWORK placement and complete import closure remain prerequisites for meaningful socket work.
- DNAS behavior must be understood before implementing a narrow local compatibility result.
- A successful local server process alone is not acceptance. The client must complete protocol login and lobby
  operations.
- A successful lobby alone is not LAN gameplay acceptance. Two clients must exchange game traffic and complete a
  controlled match join, gameplay interval, and disconnect.

See [`TODO.md`](TODO.md) for ordered milestones, dependencies, acceptance criteria, and stop conditions.
