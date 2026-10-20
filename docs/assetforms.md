# Killzone PS2 asset formats

This document records the evidence needed by a Blender importer for the NTSC-U
Killzone executable `SCUS_974.02` (XOR CRC `0xCAAEC49C`). It separates verified
layout from working hypotheses so that the importer does not silently turn one
sample into a purported general format.

No game data belongs in this repository. Measurements below came from an
authorized NTSC-U disc image and the matching executable; only aggregate
counts, offsets, signatures, and format descriptions are recorded here.

## Evidence labels

- **Verified**: established either by direct control/data flow in the matching
  executable or by bounds-checked, internally consistent observations across
  the stated sample set. Text distinguishes executable behavior from corpus
  observations when that distinction affects validation.
- **Strong inference**: fits every current observation and PS2 hardware
  behavior, but still needs a runtime trace or more samples.
- **Unknown**: preserve the original bytes and reject editing until resolved.

A runtime selector fallback proves how the executable behaves for that value;
it does not by itself prove that the serializer emits the value. Strict format
validation therefore rejects undocumented fallback aliases unless explicitly
operating in runtime-parity mode.

## Immediate corrections

1. Retail assets are not loose files on the ISO. They are catalogued ranges in
   `FILES.DAT` and `FILES01.DAT` through `FILES04.DAT`.
2. `.mss` is Guerrilla's streamed sound format (`MCSS` / `Guerrilla MSS`), not
   a mesh or scene format.
3. No `.zone` or `.col` catalog entries exist in the validated build. Zones,
   portals, collision, meshes, entities, and resources are serialized objects
   inside paired `.core` and `.corestream` level lumps.
4. The first string in a compound Core file is the first serialized type, not
   a reliable file-wide "root type." Several level lumps begin with
   `WeaponPickupResource` only because that type was serialized first.
5. The ELF is valuable for schemas, loader behavior, reflection names, and
   vector-unit programs, but it does not contain the retail texture, model, or
   level payloads. ELF-only importing cannot replace the DAT archives.

## Source support

| Input | Catalog and asset bytes | Executable schemas | Recommended behavior |
|---|---|---|---|
| Disc ISO | Yes | Yes, by reading `SCUS_974.02` from the ISO | Preferred single-file source |
| Directory containing all five DAT files | Yes | No | Supported archive source; optionally pair with an ELF |
| ELF plus all five sibling DAT files | Yes | Yes | Supported combined source |
| ELF only | No | Yes | Schema/diagnostic mode only; report that asset archives are missing |

The importer should model these as two independent inputs:

- `ArchiveSource`: ISO or extracted DAT directory; supplies catalog entries and
  byte ranges.
- `ExecutableMetadata`: ELF; supplies build identity, reflection/schema
  evidence, loader code, and DVP/VU programs.

An ISO source can provide both. An ELF source must not search arbitrary drives
for copyrighted data; check only explicitly selected paths or the five exact
sibling filenames.

## 1. ISO and DAT containers

### 1.1 Disc files

The executable directly references `cdrom0:FILES.DAT`. The validated catalog
uses package indices 0 through 4:

| Package | Container bytes | Catalog files | Observed role |
|---:|---|---:|---|
| 0 | `FILES.DAT` (1,012,867,076) | 1,648 | Catalog, configuration/localization data, and many movies |
| 1 | `FILES01.DAT` (990,547,972) | 19 | Large movie continuation |
| 2 | `FILES02.DAT` (1,047,250,948) | 18 | Large movie continuation |
| 3 | `FILES03.DAT` (1,072,283,584) | 9,807 | Core metadata, most Core streams, MSS audio, and menu/movie data |
| 4 | `FILES04.DAT` (486,570,112) | 77 | Large Core streams, including every level stream |

The ISO filesystem is ordinary ISO 9660 with 2,048-byte logical sectors.
Catalog offsets, however, are byte offsets within a DAT container, not LBAs.
Small files in `FILES.DAT` can be packed without sector alignment, so a reader
must not round arbitrary entry offsets or sizes.

### 1.2 Validated inventory

| Measurement | Value |
|---|---:|
| Files | 11,569 |
| Directories | 1,163 |
| Root entries | 9 |
| `.mss` | 8,704 |
| `.dat` | 1,595 |
| `.core` | 653 |
| `.corestream` | 514 |
| `.pss` | 66 |
| Other files | 37 |

The nine root names are `effects`, `gameplay`, `killzoneps2.ini`,
`kinlumps.txt`, `localcache.ps2`, `localized`, `memorycard`, `menu`, and
`movies`.

### 1.3 `FILES.DAT` catalog header

All integers are little-endian. The first 24 bytes of package 0 are:

| Offset | Type | Meaning |
|---:|---|---|
| `0x00` | `u32` | Metadata byte count following this 24-byte header |
| `0x04` | `u32` | Declared file count |
| `0x08` | `u8` | DAT volume count; 5 in the validated build |
| `0x09` | `u8[3]` | Unknown; bytes `63 41 00` in the validated build |
| `0x0C` | `u32` | Unknown; `0x0012F624` in the validated build |
| `0x10` | `u32` | Reserved, zero |
| `0x14` | `u32` | Reserved, zero |

For the validated build, metadata size is `0x55FCC` and the first ordinary
catalogued payload begins at `0x55FE4` (`24 + metadata_size`).

The metadata block is:

```text
u32 name_table_size
u8  names[name_table_size]
u32 entry_table_size
u8  entry_table[entry_table_size]
```

Measured sizes are `0x30AF0` bytes for the names and `0x254D4` bytes for the
entry table. Names are null-terminated ASCII strings addressed by byte offset
from the start of `names`.

### 1.4 Entry table

The entry table starts with:

```text
u32 root_entry_count
```

Every following record is 12 bytes:

```text
u32 name_flags
u32 value_a
u32 value_b
```

For a file record:

```text
bits  0..22  name-table byte offset
bits 23..27  DAT package index
bits 28..30  zero in all validated file records
bit      31  zero

value_a       byte offset in the selected DAT
value_b       byte size
```

For a directory record:

```text
bits  0..22  name-table byte offset
bit      31  one

value_a       child-record byte offset from the entry-table start
value_b       child-record count
```

Directory high bits other than bit 31 vary and remain **unknown**. Do not apply
the file package-index interpretation to them. The directory tree covers every
record exactly once in the validated catalog; a parser should reject cycles,
overlapping child ranges, orphan records, invalid names, duplicate
case-insensitive paths, and out-of-container file ranges.

There is no observed archive-level encryption or compression layer. Reading
the selected DAT range returns the resource's actual signature (`Core...`,
`<CoreStreamData>`, `MCSS`, or the relevant media/text bytes). Individual
resource payloads can still use packed or hardware-native representations.

### 1.5 Safe extraction algorithm

```text
read 24-byte FILES.DAT header
validate metadata_size against package 0
read exactly 24 + metadata_size bytes
parse and validate the name table
walk the 12-byte record tree from entry-table offset 4
for a selected file:
    package = bits(name_flags, 23, 5)
    validate offset + size without integer overflow
    read exactly [offset, offset + size) from that DAT
```

Direct range reads are preferable to extracting all 4+ GiB first.

Aluigi's public
[`killzone.bms`](https://www.aluigi.altervista.org/bms/killzone.bms) independently
documents the same 23-bit name offset, 5-bit package index, directory flag,
recursive tree, and multi-DAT selection. Its current header and decompression
path also cover Killzone Liberation/PSP, so it is corroborating evidence for
the archive family, not a drop-in canonical SCUS-974.02 parser. The separate
[`killzone_beta.bms`](https://github.com/RetingencyPlan/le_quickbms_script_compendium/blob/0aab2740f0d5322f36f277a7498f66497b77fce8/killzone_beta.bms)
describes a different beta layout and must not be applied to retail data.

## 2. Core resource containers

### 2.1 Core header and string table

Every validated `.core` begins with:

```text
offset  size  field
0x00    16    ASCII "Core<1.79><PSX2>"
0x10     4    variant (1 or 5)
0x14     4    string_table_size
0x18     4    string_count, first word of the string-table payload
```

Each string is encoded as:

```text
u32 byte_length
u8  raw_bytes[byte_length]
u8  zero_padding[(-byte_length) & 3]
```

Strings are length-delimited and do not include a terminator. Preserve raw
bytes: not every entry is valid UTF-8, and empty entries exist. After the last
string, the validated files contain 0, 4, 8, or 12 additional zero bytes so
that the declared string-table size is consumed exactly. The serialized body
begins at `0x18 + string_table_size`.

Observed body schema descriptors use a 16-byte property pattern containing a
string-table index, a four-character type code, flags/size information, and an
offset or sentinel. Seen type codes include `BOOL`, `STR `, `INT `, `FLOA`,
and `BBOX`. The exact object-table, reference, inheritance, and relocation
semantics are not yet resolved, so a generic writer must not be inferred from
these descriptors alone.

### 2.2 Variants

| Variant | Count | Paired stream | Current interpretation |
|---:|---:|---|---|
| 1 | 139 | Never | Self-contained metadata and payload |
| 5 | 514 | Always | Metadata/object graph in `.core`, bulk data in matching `.corestream` |

This pairing is exact for all 653 validated Core files. It is safer to treat
the variant as a storage policy than as a resource type.

### 2.3 CoreStream framing

Every one of the 514 streams has the same 64-byte prefix:

```text
0x00  char[16]  "<CoreStreamData>"
0x10  u8[48]    zero
0x40  ...       stream payload
```

All stream catalog offsets are 2,048-byte aligned and all stream sizes are
multiples of 64 in this build. The payload has no proven global chunk table;
offsets and lengths appear to be owned by serialized Core objects. Preserve
the prefix and unreferenced bytes until those references are decoded.

### 2.4 Storage, build, and runtime boundaries

Do not collapse shipped containment, runtime I/O, and authoring provenance into one purported pipeline.

**Verified shipped containment.** The relation established by sections 1 and 2 is:

```text
ISO-resident DAT packages
  -> catalogued resource byte range
  -> self-contained Core, or paired Core metadata and CoreStream bulk data
  -> typed object and object-owned stream ranges
```

This is a containment relation, not by itself a trace of the runtime call graph.

**Verified catalog volume count.** Code at VA `0x002C2EF8` reads byte `0x08` of the 24-byte `FILES.DAT` header as
the total DAT volume count. With the validated value 5, package zero is `FILES.DAT` and the remaining package names
are constructed as `FILES01.DAT` through `FILES04.DAT`. Bytes `0x09..0x0B` remain **unknown** and must not be
interpreted as part of the count.

**Verified runtime namespace.** Code at VA `0x001BCD20` establishes these mounts or aliases:

```text
appdir:    -> cdrom0:FILES.DAT
cache:     -> appdir:<LocalCache>.PS2/
source:    -> appdir:
binaries:  -> appdir:Binaries/
music:     -> appdir:Sound/Music/
```

The spelling and case are executable conventions; catalog lookup remains subject to the archive rules in section 1.

**Verified runtime boundaries.** The host keeps the original ISO mounted, resolves ISO9660 records, and serves raw
2,048-byte sectors (`src/kz_iso.cpp:30-119,175-219`;
`ext/PS2Recomp/ps2xIOP/src/emulator/imports/iop_cdvd.cpp:606-809`). Physical `PFILE_R.IRX` executes in the emulated
IOP rather than being replaced by a semantic host asset layer
(`ext/PS2Recomp/ps2xIOP/src/iop_subsystem.cpp:568-607`). Generic EE File and asynchronous-stream wrappers exist
(`generated/entry_0014d1f4_0x14d1f4.cpp`; `generated/FUN_004963b8_0x4963b8.cpp`). At the opposite end of the
pipeline, the host GS receives GIF command and image bytes rather than Core objects, resource tokens, or authoring
semantics (`src/kz_gs.cpp:404-427`).

**Unknown.** No current trace proves the consumer joins from `PFILE_R.IRX` through `FILES.DAT` range resolution to a
particular Core/CoreStream loader. The physical I/O module, EE wrappers, DAT parser, Core loaders, and GS traffic are
independently evidenced, but must not yet be presented as one instruction-complete call chain.

**Strong inference.** Versioned `Core<1.79><PSX2>` and `CoreTex<1.05ctx>` strings, the incompatible-Core
reconversion diagnostic, and the platform-specific XBOX/`.PC`/`.PS2` cache conventions are consistent with an
offline, multi-target conversion/cache stage. WAV, VAG, and MSS labels establish additional format vocabulary.
They do not identify the producer, its command line, or its source formats.

The minimum defensible build model is therefore:

```text
unknown authoring source
  -> inferred platform cook/serialization
  -> verified Core[/CoreStream] resource
  -> verified DAT packaging
  -> disc image
```

Only the resource and packaging containment is verified; the chronological converter and packer stages remain
untraced.

**Unknown.** The original DCC or editor, cooker identity, source-project layout, pre-strip mesh topology, pre-swizzle
texture representation, and exact build-stage ordering remain unproven. Mission scripts provide the only currently
documented source-path-plus-compiled-payload example; it must not be generalized to other asset classes.

**Importer rule.** Reconstruct the shipped object graph without presenting it as the recovered original authoring
project. Preserve source bytes, physical definitions, Core-local identities, wrapper/occurrence distinctions, and
unknown records. Keep Blender conversion as an explicit layer and native export disabled until the unresolved data
round-trips safely.

## 3. Textures and PS2 swizzling

### 3.1 Proven standalone texture layout

Nineteen variant-1 loading-screen Core files share one byte-exact layout and
decode successfully. Each contains one 512x512, 8-bit indexed texture with a
256-entry, 32-bit RGBA palette.

For this schema only:

| Offset relative to Core body | Type | Value/meaning |
|---:|---|---|
| `0x00` | `u32` | `0xAC`; role unresolved, but required for this exact shape |
| `0x80` | `u32` | `0x18` value-block size |
| `0xD0` | `u32` | width = 512 |
| `0xD4` | `u32` | height = 512 |
| `0xD8` | `u32` | 1, semantics not generalized |
| `0xDC` | `u32` | 0, unknown |
| `0xE0` | `u32` | 1, semantics not generalized |
| `0xE4` | `u32` | chunk count = 9 |
| `0xE8` | `u32[9]` | eight `0x8000` chunks, then one `0x500` chunk |
| `0x10C` | `u8[0x40000]` | swizzled PSMT8 indices |
| `0x4010C` | `u8[0x400]` | 256 RGBA8888 palette entries |
| `0x4050C` | `u8[0x100]` | zero auxiliary area in all 19 samples |
| `0x4060C` | `u8[0x78]` | opaque footer/offset data |

The Core header/string table is `0xE0` bytes for these files, making the
absolute index-data offset `0x1EC` and total file size `0x40764`.

These offsets are not a generic Texture structure. Compound variant-5 files
have different schema sizes; a general parser must follow the Core object
description and its stream references.

### 3.2 Verified PSMT8 unswizzle

For each logical `(x, y)`, read the stored byte at:

```python
def psmt8_offset(x, y, width):
    block = (y & ~0x0F) * width + (x & ~0x0F) * 2
    swap = (((y + 2) >> 2) & 1) * 4
    py = (((y & ~3) >> 1) + (y & 1)) & 7
    column = py * width * 2 + ((x + swap) & 7) * 4
    byte = ((y >> 1) & 1) + ((x >> 2) & 2)
    return block + column + byte
```

Then:

```text
linear[y * width + x] = stored[psmt8_offset(x, y, width)]
```

The semantically decoded evidence covers positive dimensions divisible by 16
and a storage stride equal to `width`. The level corpus additionally proves
the address function is a complete permutation for 128x8, so its dimensional
domain extends to heights divisible by 8; that bijection is not by itself a
pixel-appearance proof. A general GS decoder must instead honor the texture
buffer width (`TBW`) and page boundaries.

### 3.3 CSM1 palette order

The 256-entry PSMT8 palette uses the standard CSM1 CT32 column permutation.
Map a logical pixel index to the stored palette entry with:

```python
stored_index = (
    (logical_index & 0xE7)
    | ((logical_index & 0x08) << 1)
    | ((logical_index & 0x10) >> 1)
)
```

This exchanges entries 8-15 with 16-23 in each 32-entry group. It is not a
pixel unswizzle and should not be applied twice. The validated file palettes
contain full-range 0-255 RGBA bytes; do not apply the GS framebuffer-alpha
doubling used when reading a CT32 CLUT back from GS VRAM.

### 3.4 Exact indexed plane order

The engine pixel-format table and upload path prove PSMT8 for `INDEX_8`,
`ALPHA_8`, and `GLOW_8`, and PSMT4 for `INDEX_4` and `ALPHA_4`. For widths
of at least 16, the runtime uses these alias uploads:

```text
sample PSMT8 -> upload PSMCT32 at width/2 x height/2
sample PSMT4 -> upload PSMCT16 at width/2 x height/2
```

CoreStream spans reach the upload DMA without CPU conversion. Composing the
host upload and sampled GS address functions gives exact serialized mappings.

For mip width `W >= 16`, PSMT8 formats use:

```text
block = (y & ~15)*W + (x & ~15)*2
swap  = (((y + 2) >> 2) & 1)*4
py    = (((y & ~3) >> 1) + (y & 1)) & 7

byte_offset =
    block
  + py*(2*W)
  + ((x + swap) & 7)*4
  + ((y >> 1) & 1)
  + ((x >> 2) & 2)
```

For `INDEX_4` and `ALPHA_4`, `W >= 16`:

```text
block = (y & ~15)*W + (x & ~31)*2
swap  = (((y + 2) >> 2) & 1)*4
py    = (((y & ~3) >> 1) + (y & 1)) & 7
u     = ((x + swap) & 7) + ((x >> 1) & 8)
k     = ((y >> 1) & 1) + ((x >> 2) & 2)

nibble_index = block + py*(2*W) + 4*u + k
byte_offset  = nibble_index >> 1
```

Even `nibble_index` selects the low nibble and odd selects the high nibble.
This is instruction- and address-proven, not the earlier statistical
candidate.

When mip width is below 16:

```text
PSMT8 byte index   = y*W + x
PSMT4 nibble index = y*W + x
```

Native PSMT4 remains low-nibble-first. The current corpus contains 13
`INDEX_4` and 66 `INDEX_8` native 8x8 mips. No retained original-PS2 capture
contains one of those small-mip uploads, but the runtime implementation and
independent GS test agree.

Validation covers 98 format/dimension cases and 4,129,024 texels over:

```text
W = 16, 32, 64, 128, 256, 512, 1024
H = 8, 16, 32, 64, 128, 256, 512
```

Every mapping has zero GS-address mismatches and is a complete permutation
with no missing or duplicate source position.

The four Street2 targets are now exactly decodable as `INDEX_4`:

```text
0x01F70928
0x01F49D6C
0x01F49D86
0x00570004
```

Each is 256x256 with a `0x8000` packed plane and the standard two-bank
`0x100` palette object. The existing Street2 capture still lacks a continuous
object-ID-to-upload attribution for those four assets, so their use and
runtime-selected alpha bank in that interval must not be inferred from
capture absence.

Relevant GS facts:

| PSM | Value | Bits/texel | Palette entries | Block | Page |
|---|---:|---:|---:|---:|---:|
| PSMT8 | `0x13` | 8 | 256 | 16x16 | 128x64 |
| PSMT4 | `0x14` | 4 | 16 | 32x16 | 128x128 |
| PSMT8H | `0x1B` | 8 | 256 | CT32 layout | CT32 layout |
| PSMT4HL | `0x24` | 4 | 16 | CT32 layout | CT32 layout |
| PSMT4HH | `0x2C` | 4 | 16 | CT32 layout | CT32 layout |

Do not describe PS2 swizzling as one Morton/Z-order transform. It is
PSM-specific page, block, column, and pixel addressing. For formats not proven
above, use PCSX2's `GSOffset`/local-memory tables or an equivalently tested
port, not a guessed permutation. Sony's
[*GS User's Manual*, version 6.0](https://github.com/ninjadynamics/PS2Docs/blob/3a6e97416b0e23c4e94288515adef521cf077682/GS_Users_Manual.pdf)
documents PSM and CLUT behavior on printed pages 22, 25, and 29-31; `TEX0` on
pages 125-126; and page/block/column layouts on pages 162 and 164-168.

### 3.5 Embedded variant-5 Texture containers

All 47 retail level Cores were parsed through their object directories and
owned CoreStream ranges. A second pass then inspected every one of the 653
catalogued Core files and fully decoded every variant-5 Core that declares an
exact `Texture` type:

| Scope | Texture Cores | Texture sections | Physical objects | Logical Core-local tokens | Payload bytes |
|---|---:|---:|---:|---:|---:|
| 47 level Cores | 47 | 6,503 | 12,613 | 12,566 | 273,141,504 |
| All variant-5 Texture Cores | 111 | 6,907 | 13,891 | 13,718 | 305,231,104 |

The complete variant-5 pass has 66 duplicate-token groups containing 173
extra physical definitions. Every duplicate is byte-equivalent in both its
serialized object and payload. The 19 self-contained variant-1 loading-screen
textures use the separate layout in section 3.1 and are not included in this
table.

Every Texture section has the same reflected property schema. The stable
serialized prefix is:

| Texture offset | Type | Meaning |
|---:|---|---|
| `+0x00` | `u32` | no-streaming flag |
| `+0x04` | `u32` | linear-storage flag |
| `+0x20` | `u32` | width |
| `+0x24` | `u32` | height |
| `+0x28` | `u32` | engine pixel-format enum |
| `+0x2C` | `u32` | zero in all 13,891 variant-5 objects |
| `+0x30` | `u32` | mip count |
| `+0x34` | `u32` | chunk count |
| `+0x38` | `u32[chunk_count]` | chunk sizes |

In particular, `+0x34` is a count, not a storage-format code. All 13,869
non-inline objects have `no_streaming == 0`, all 22 inline objects have
`no_streaming == 1`, and all 13,891 objects have `linear_storage == 0`. For
each of the 13,865 valid streamed objects:

```text
sum(chunk_size) == object-directory CoreStream size
```

After the chunk-size array, an ordinary streamed object has:

```text
u32 palette_chunk_index       # last chunk for INDEX_4/INDEX_8; zero otherwise
u32 zero
u32 serialized_record_count

record[serialized_record_count]:
    +0 u8  chunk_index
    +1 u8  zero
    +2 u16 byte_offset
    +4 u16 QWC
```

The record list has two identical halves. The loader consumes the first half
until the requested mip QWC is satisfied. Across all 13,887 valid textures:

```text
serialized span records          36,752
runtime-consumed records         18,376
multi-span textures               2,209
records with nonzero offsets      3,084
span reconstruction anomalies         0
```

Concatenating:

```python
chunk[byte_offset : byte_offset + QWC * 16]
```

reconstructs every raw mip prefix byte-for-byte. The spans exclude final
pixel-storage padding and indexed palette chunks. The 22 inline objects place
their payload between the chunk sizes and this same footer: ten are
`INDEX_8`, six are `ALPHA_8`, and six are `RGBA_8888`. Street1 texture
`0x01F67930` is the only level-inline example; the other 21 occur in shared,
first-person entity, and menu Cores.

The matching retail ELF has a 23-entry, `0x34`-byte pixel-format table at VA
`0x00519CD8`, file offset `0x003DACD8`. File offset `0x003DAD00` is record
0's `+0x28` name pointer, not the table base. The descriptor masks establish
the declared channel packing below. Packing is written least-significant to
most-significant; it is an engine declaration, not proof that an unobserved
format was serialized.

| Code | Name | Bits/texel | Descriptor packing | Loader sampling mapping | Physical | Logical | Serialized evidence |
|---:|---|---:|---|---|---:|---:|---|
| 0 | `INDEX_4` | 4 | indexed 4-bit | PSMT4 | 3,175 | 3,120 | proven |
| 1 | `INDEX_8` | 8 | indexed 8-bit | PSMT8 | 7,383 | 7,275 | proven |
| 2 | `ALPHA_4` | 4 | A4 | PSMT4 | 496 | 496 | proven |
| 3 | `ALPHA_8` | 8 | A8 | PSMT8 | 2,579 | 2,579 | proven |
| 4 | `GLOW_8` | 8 | indexed 8-bit | PSMT8 | 32 | 32 | plane proven; palette source unresolved |
| 5 | `RGBA_8888` | 32 | R8 G8 B8 A8 | PSMCT32 | 186 | 176 | proven |
| 6 | `RGBA_8888_REV` | 32 | B8 G8 R8 A8 | remap 6 -> 5, PSMCT32 | 0 | 0 | declared only |
| 7 | `RGBA_5551` | 16 | R5 G5 B5 A1 | PSMCT16 | 0 | 0 | declared only |
| 8 | `RGBA_5551_REV` | 16 | B5 G5 R5 A1 | none (`0xFF`) | 0 | 0 | declared only |
| 9 | `RGBA_4444` | 16 | R4 G4 B4 A4 | none (`0xFF`) | 0 | 0 | declared only |
| 10 | `RGBA_4444_REV` | 16 | B4 G4 R4 A4 | none (`0xFF`) | 0 | 0 | declared only |
| 11 | `RGB_888_32` | 32 | R8 G8 B8 X8 | none (`0xFF`) | 0 | 0 | declared only |
| 12 | `RGB_888_32_REV` | 32 | B8 G8 R8 X8 | none (`0xFF`) | 0 | 0 | declared only |
| 13 | `RGB_888` | 24 | R8 G8 B8 | PSMCT24 | 36 | 36 | proven |
| 14 | `RGB_888_REV` | 24 | B8 G8 R8 | remap 14 -> 13, PSMCT24 | 0 | 0 | declared only |
| 15 | `RGB_565` | 16 | R5 G6 B5 | none (`0xFF`) | 0 | 0 | declared only |
| 16 | `RGB_565_REV` | 16 | B5 G6 R5 | none (`0xFF`) | 0 | 0 | declared only |
| 17 | `RGB_555` | 16 | R5 G5 B5 X1 | PSMCT16 | 0 | 0 | declared only |
| 18 | `RGB_555_REV` | 16 | B5 G5 R5 X1 | none (`0xFF`) | 0 | 0 | declared only |
| 19 | `RGBE` | 32 | RGB masks; high byte unresolved | none (`0xFF`) | 0 | 0 | declared only |
| 20 | `INDEX_2X2` | 4 | 2x2-bit interleaving unresolved | none (`0xFF`) | 0 | 0 | declared only |
| 21 | `INDEX_2` | 2 | indexed 2-bit | PSMT4 table entry | 0 | 0 | declared only |
| 22 | `INVALID_PIXELFORMAT` | 0 | none | invalid sentinel | 4 | 4 | empty placeholders only |

Thus the complete NTSC-U catalog uses exactly the same eight enum codes as the
47-level subset. The 15 zero-count descriptors must remain engine-declared
formats, not supported serialized layouts. In particular, a valid sampling
table byte for codes 7, 17, or 21 does not establish their file packing,
palette framing, or upload dimensions. The four code-22 objects have
`0xFFFFFFFF` dimensions and mip count, zero chunks, and no payload. The 19
variant-1 textures carry no pixel-format enum and remain a separate proven
PSMT8 schema.

The loaders at `0x15B1C0` and `0x15B730` canonicalize exactly code 6 to code 5
and code 14 to code 13 before indexing the byte table at
`0x00513658/0x003D4658`; all other codes index it directly. A `0xFF` table
entry means there is no direct GS sampling mapping. Code 22 has an otherwise
ordinary raw table byte, but its empty sentinel objects establish no observed
upload and it must not be described as PSMT4 data.

Within the 47 level Cores, the default-LOD static scene reaches:

| Code | Name | Level objects | Selected direct | Selected instance |
|---:|---|---:|---:|---:|
| 0 | `INDEX_4` | 3,034 | 897 | 697 |
| 1 | `INDEX_8` | 6,320 | 2,898 | 1,040 |
| 2 | `ALPHA_4` | 494 | 281 | 338 |
| 3 | `ALPHA_8` | 2,564 | 902 | 1,579 |
| 4 | `GLOW_8` | 31 | 9 | 22 |
| 5 | `RGBA_8888` | 134 | 56 | 31 |
| 13 | `RGB_888` | 32 | 21 | 11 |
| 22 | `INVALID_PIXELFORMAT` | 4 | 3 | 0 |

The selected counts are unique Core-local tokens. Direct and instance columns
are independent.

`FUN_002DDC98` at VA/file `0x002DDC98/0x0019EC98` returns the descriptor's
bits-per-texel field.

The runtime upload path is also exact. `FUN_00159BB8` at VA/file
`0x00159BB8/0x0001ABB8` selects:

```text
runtime width >= 16:
  sample PSMT8  -> upload PSMCT32 at width/2, height/2
  sample PSMT4  -> upload PSMCT16 at width/2, height/2
  all other sample PSMs -> retain PSM and dimensions

runtime width < 16:
  retain native PSMT8/PSMT4 upload
```

`FUN_00159D30` at VA/file `0x00159D30/0x0001AD30` emits `BITBLTBUF`,
`TRXREG`, `TRXDIR=0`, and raw REF/image DMA spans. It performs no pixel-byte
transform between a runtime span and the GIF IMAGE payload.

The CoreStream loader at `0x340A80..0x340AE8` resolves each serialized span
directly to `[source_pointer,QWC]`. The upload loop at
`0x159F40..0x15A024` emits DMA REF tags from those pointers. There is no CPU
conversion buffer, gather copy, byte reorder, or nibble swap after chunk
residency. Together with the alias upload, this proves the exact mappings in
section 3.4 for all five indexed/plane formats.

The descriptor masks and direct upload path do prove:

```text
RGBA_8888  tightly packed little-endian R, G, B, A
RGB_888    tightly packed little-endian R, G, B
```

Parse those direct formats without swizzling or channel conversion. Preserve
RGBA alpha exactly. Do not synthesize a file alpha for RGB888; PSMCT24
sampling alpha is controlled by persistent GS `TEXA`.

All five indexed/plane paths use:

```text
CPSM = PSMCT32
CSM  = CSM1
CSA  = 0
CLD  = 1
```

TEXA does not replace or rescale alpha stored in a PSMCT32 CLUT.

The runtime palette objects are:

```text
*(u32*)0x0055A6C0 = ALPHA_4 normal/complement object
*(u32*)0x0055A6C4 = ALPHA_8 normal object
*(u32*)0x0055A6CC = ALPHA_8 complement object
*(u32*)0x0055A710 = GLOW_8 object
```

When the derived complementary-alpha Boolean is true:

```text
INDEX_4 -> attached object descriptor +0x50
INDEX_8 -> unchanged
ALPHA_4 -> shared object descriptor +0x50
ALPHA_8 -> separate complement object at 0x0055A6CC
GLOW_8  -> unchanged
```

This changes the selected descriptor and CBP, not CSA. The complete source
of that derived Boolean in the main render-state path is:

```text
runtime_gate =
    renderer+0x27C == 0
 && renderer+0x280 != 0
 && renderer+0x285 == 0

complement =
    operation in {6 ALPHASUB, 17 LAYERBUMPEMBOSSINV}
 && (runtime_gate || alpha_scalar != 255 || fade_mode != 0)
```

All other operations select the normal descriptor. The selected corpus has
96 operation-6 layers and no operation-17 layers. All 96 use scalar 255 and
fade mode 0, so their choice depends only on the runtime gate.

For every valid object, pixel storage obeys:

```python
raw_bytes = sum(
    ceil(
        max(1, width >> level)
        * max(1, height >> level)
        * bits_per_pixel
        / 8
    )
    for level in range(mip_count)
)

pixel_storage_bytes = align_up(raw_bytes, 0x100)
```

Bytes from `raw_bytes` to the aligned end are zero in every case. Mip counts
are 13,272 one-level, 49 two-level, 24 three-level, and 542 four-level
objects, plus the four invalid sentinels. The chunk array partitions the
combined payload; it is not one entry per mip. Concatenate pixel chunks, walk
mip lengths from dimensions and format, consume the final alignment, and only
then read an indexed palette.

The runtime loader independently confirms the mip walk. Instructions at
`0x15B324..0x15B3B8` halve dimensions and accumulate exact
`ceil(width * height * bpp / 8)` lengths into four reserved mip descriptors.
There is no per-mip alignment. Runtime span vectors and archive chunks are
transport partitions rather than semantic mip records.

All 3,175 `INDEX_4` palette chunks are exactly:

```text
+0x00  0x10-byte bank header
+0x10  16 RGBA entries
+0x50  identical 0x10-byte bank header
+0x60  16 RGBA entries
+0xA0  0x60 zero bytes
```

For every entry pair, the RGB bytes match and the two alpha bytes sum to 255.
This is a complementary-alpha two-bank container. The normal descriptor is
at object `+0x00`; a true complement policy selects descriptor `+0x50`.

All 7,383 `INDEX_8` palette chunks are exactly:

```text
+0x000  0x10-byte header
+0x010  256 RGBA entries
+0x410  0xF0 zero bytes
```

The PSMT8 address function in sections 3.2 and 3.4 is exact for every
observed `INDEX_8` dimension. CSM1 maps logical index `i` to serialized
palette entry:

```text
P(i) =
    (i & ~0x18)
  | ((i & 0x08) << 1)
  | ((i & 0x10) >> 1)
```

Generated ALPHA/GLOW palettes instead store logical entry `i` in upload slot
`P(i)`. A 16-entry PSMCT32 CSM1 CLUT remains logically linear from 0 to 15.

The serialized enum proves the four Street2 targets in section 3.4 are
`INDEX_4`; the exact PSMT4 mapping now settles their plane and nibble order.
All four are selected by instanced geometry. Their seven selected Material
references all use operation 5 `ALPHAADD`, scalar 255, and fade mode 2.
Operation 5 bypasses complement derivation, so all four select the normal
descriptor at palette object `+0x00`.

#### Street2 static graffiti material `0x01F7E8D4`

Street2 Core-local token `0x01F7E8D4` identifies one Material definition
reused by 11 `StaticMeshInstance` occurrences. Repeating its Blender display
name does not identify a second definition, and the token must not be resolved
outside the Street2 Core.

The Material has one fixed-size layer and no optional transform payload:

| Layer field | Value | Proven effect |
|---|---:|---|
| provider | Texture `0x01F70942` | one physical definition |
| texture format | `INDEX_8`, PSMT8 | 256x256, one mip, CSM1 palette |
| operation | 5, `ALPHAADD` | `(Cs-Cd)*As+Cd`, ordinary source-over |
| lighting mode | 1 | preserved; final runtime lighting remains unresolved |
| Z-write | 0 | disabled |
| framebuffer selector | 1 | ordinary selected framebuffer state |
| depth comparison | 3 | `GEQUAL` |
| alpha test / reference | 7 / 0 | `ALWAYS`; no alpha-test discard |
| wrap S / T | 0 / 0 | repeat / repeat |
| filter policy | 4 | alternate mip policy `0x120`; only one mip exists |
| texture enabled / TFX | 1 / 1 | textured `MODULATE` path |
| alpha source / scalar | 0 / 255 | unscaled source-alpha path |
| fade mode / values | 2 / `(15, 0.5, 0.5, 20)` | preserved serialized state |
| coordinate selector | 0 | direct UV0 |
| VU feature booleans | false / false | neither optional feature selected |

Operation 5 selects the normal palette descriptor. The decoded atlas visibly
contains graffiti and poster artwork, but that visual description is not a
serialized semantic name. Its decoded alpha coverage is:

| Alpha class | Texels |
|---|---:|
| zero | 24,157 |
| partial | 22,268 |
| 255 | 19,111 |
| distinct byte values | 110 |

The 11 selected regular-static leaves contain 404 triangle occurrences and
808 target-face vertices. Every target packet carries Native V4-8 color.
Across only the vertices used by this Material, RGB spans `0..128`, while
alpha is either 121 or 128. Direct UV0 spans:

```text
U = -0.9111328125 .. 0.99755859375
V = -1.0          .. 1.0
```

Repeat addressing is therefore material behavior, not a host-side clamp.
The texture and V4-8 values combine with the PS2 `128 == 1.0` convention:

```text
source_rgb   = texture_rgb   * vertex_rgb   / 128
source_alpha = texture_alpha * vertex_alpha / 128
framebuffer  = source_rgb * source_alpha
             + destination_rgb * (1 - source_alpha)
```

These are serialized static overlay/card triangles. They are not
`DecalResource` marks, do not use the runtime receiver projection described
in section 6, and must remain present when runtime decals are disabled.

The bounded Blender preview can reproduce the decoded texture, UV0, V4-8
modulation, and source-over alpha. It remains diagnostic because lighting
mode 1 and the complete retail lighting environment are not reconstructed;
the preview emission boost is a host visualization choice.

`ALPHA_4` and `ALPHA_8` are generated white-RGB scalar masks:

```text
ALPHA_4 normal[n]     = 0x00FFFFFF | (17*n << 24)
ALPHA_4 complement[n] = 0x00FFFFFF | ((255 - 17*n) << 24)

ALPHA_8 normal[n]     = 0x00FFFFFF | (n << 24)
ALPHA_8 complement[n] = 0x00FFFFFF | ((255 - n) << 24)
```

For a dynamic callback result `T[0x300]`, the exact `GLOW_8` logical palette
is:

```text
GLOW[0..127] = [0,0,0,0]

GLOW[128+i] =
    [T[381-3i], T[382-3i], T[383-3i], 128+i]
    for 0 <= i < 128

raw_upload[P(i)] = GLOW[i]
```

The byte transform is proven. The callback producer and artistic meaning of
`T` remain unknown.

Four `INVALID_PIXELFORMAT` placeholders have dimensions and mip count
`0xFFFFFFFF`, no chunks, and no payload. Three are referenced by selected
direct-world materials. Preserve an explicit missing-texture node rather than
trying to decode them or silently deleting their slots.

Street1 Texture `0x01F67930` is the sole inline object in the 47-level subset:

```text
128x128 INDEX_8, one mip, chunks [0x4000, 0x500]

+0x0000..+0x003F  header and chunk sizes
+0x0040..+0x453F  inline 0x4500-byte payload
+0x4540..+0x455F  ordinary post-chunk footer
```

Its CoreStream size is zero. The palette index is in the relocated footer at
`+0x4540`, not at the normal `+0x40`. Its correctly sliced palette has the
same standard `INDEX_8` layout.

Texture tokens are scoped to their owning Core. The same numeric value can
name different formats in different levels: token `0x00570004` is `INDEX_4`
in Street2 and `INDEX_8` in Park2. Across the complete variant-5 corpus, 66
Core-local tokens have duplicate directory definitions, for 173 extra
physical copies; the level subset contributes 13 groups and 47 copies. Each
group has identical metadata and payload hashes. Preserve the physical
entries for round trip, but resolve a verified equivalent group once as a
logical token.

#### Mountain Pass diagnostic material families

Mountain Pass Material `0x00FB0B4A` is the repeated foliage family. Both
serialized layers reference the same 256x256, one-mip `INDEX_8` Texture
`0x00561068` through direct UV0 and the normal CSM1 palette:

| Layer | Operation | Z-write | Alpha test | TFX policy | Fade | VU features |
|---:|---|---:|---|---:|---|---|
| 0 | 11 `LIGHTMAPCOLOR` | 1 | `GREATER`, reference 204 | 0 | 2, `(59, 0.5, 0.5, 60)` | true, true |
| 1 | 5 `ALPHAADD` | 0 | `ALWAYS`, reference 0 | 2 | 2, `(40, 0.5, 0.5, 60)` | false, true |

The runtime alpha-test reference is `204 >> 1 == 102`. The decoded foliage
atlas has 46,349 zero-alpha and 19,187 partial-alpha texels, no alpha-255
texels, and 144 distinct alpha values spanning 0..252. Neither of its two
regular-static definitions carries Native V4-8. Those definitions contribute
20 stored triangles and are selected by 411 instanced occurrences, producing
6,576 triangle occurrences.

Mountain Pass Material `0x005436FE` is the direct-world sandbag/rockwork
family. Its one direct-UV0 layer references opaque 256x256 `INDEX_8` Texture
`0x005417C8`:

```text
operation             5 ALPHAADD
lighting mode         1
Z-write               1
depth comparison      GEQUAL
alpha test            ALWAYS
TFX policy            2
alpha scalar          255
fade mode             0
VU features           false, false
```

All 65,536 decoded texels have alpha 255. The stored catalog contains 27
regular-static definitions using this Material; the default-LOD scene selects
14 direct occurrences with 10,725 triangle occurrences. Their packets carry
no Native V4-8, so this family is intentionally outside the bounded V4-8
`ALPHAADD` preview used for the Street2 overlays.

Direct-world object `Direct 0001`, regular-static token `0x00566D60`, has four
records, 2,266 triangles, and no Native V4-8:

| Record | Material | Triangles | Diagnostic color source |
|---:|---|---:|---|
| 0 | `0x00564AF0` | 1,199 | layer-0 `WRITE`, Texture `0x00558886`, UV0 |
| 1 | `0x0054701E` | 695 | layer-0 `WRITE`, Texture `0x00558804`, UV0 |
| 2 | `0x00547702` | 78 | layer-0 `ALPHAADD`, Texture `0x005588BA`, UV2 |
| 3 | `0x00546F22` | 294 | layer-0 `ALPHAADD`, Texture `0x005587EA`, UV0 |

Record 0's `WRITE` layer carries the optional six-float transform:

```text
(0.9878843, -0.1551932, 0.1551936,
 0.9878841, -0.0836546, -0.0715389)
```

Its exact runtime packing is known, but the multiplication convention remains
unresolved. A Blender diagnostic may therefore connect untransformed UV0 only
when it records that the transform was ignored. It must not label that graph
as native material reconstruction.

The purpose-built Blender material diagnostic now falls back to an eligible
decoded color layer only after exact layer-0 `WRITE` and bounded V4-8
`ALPHAADD` admission fail. Admission is structural rather than token-specific
and is limited to four observed families:

1. one direct-coordinate `ALPHAADD` color layer;
2. a transformed direct-coordinate layer-0 `WRITE`;
3. same-texture `LIGHTMAPCOLOR`, then `ALPHAADD`;
4. same-texture `ALPHAADD`, then `LIGHTMAPCOLOR`.

The selected layer must use a normal-bank `INDEX_4`, `INDEX_8`, `RGBA_8888`,
or `RGB_888` image, scalar 255, an enabled texture, and direct selector 0..7.
It connects source alpha for a single-layer `ALPHAADD`, or for the
same-texture foliage pair. Complex multi-layer fallbacks remain opaque so one
isolated pass does not make whole surfaces spuriously transparent. The graph
is explicitly marked `diagnostic-color-layerN`; native lighting, VU color,
multi-layer composition, and ignored six-float transforms remain unresolved.
Normal imports do not enable this fallback.

### 3.6 Geometry-aware texture dependencies

Texture selection must follow the direct-world and instance-definition roots
from section 4.9, then the selected default-LOD mesh records and their
Materials. Across all levels this resolves:

```text
Selected Material definitions:        11,870
Serialized layers:                    30,994
Texture providers:                    30,814
AnimatedTexture providers:               134
Null providers:                           46
Other/procedural providers:                 0
Unique direct Texture definitions:      5,067
Unique instance Texture definitions:    3,718
```

The old fixed-stride interpretation was wrong after any layer with flag
`0x20`: the following `0x18`-byte optional transform shifted every later
record. The stable sequential grammar is:

```python
cursor = material_blob + 0x18
for layer_index in range(u32(material_blob + 0x00)):
    provider_token = u32(cursor + 0x00)
    layer_flags = u8(cursor + 0x04)
    assert cursor[0x05:0x08] == b"\0\0\0"
    assert u32(cursor + 0x08) == 0x74
    coordinate_selector = u8(cursor + 0x31)
    decode_fixed_hwlayer(cursor + 0x0C)       # 0x34 bytes
    cursor += 0x40

    if coordinate_selector == 11:
        matrix_4x4 = read_f32x16(cursor)
        cursor += 0x40

    if layer_flags & 0x20:
        transform_6f = read_f32x6(cursor)
        cursor += 0x18

    if layer_flags & 0x40:
        create_default_transform_if_null()   # consumes no bytes
```

The selected corpus contains 29,435 flag-`0x00` and 1,559 flag-`0x20`
layers, with no flag-`0x40` or selector-11 payloads. The latter paths are
instruction-proven but not corpus-exercised. The post-layer tail is 8 or 16
bytes; its first word remains unnamed, and all remaining bytes are zero.

Every nonzero provider token resolves uniquely in the same Core:

```text
record+0x00 provider token
  -> runtime Material::Layer+0x08
  -> provider vtable+0x80
  -> HwLayer+0x18 resolved HwTexture*
```

The former 3,271 "opaque/procedural slots" were shifted portions of later
Texture layers. Do not use the obsolete fixed-stride classifier.

The 85 `AnimatedTexture` objects contain 1,302 frames:

```text
AnimatedTexture+0x00  u32 frame_count
AnimatedTexture+0x20  frame[frame_count]

frame+0x00             u32 Texture token
frame+0x04             f32 duration
```

Every frame token resolves to a Texture in the same Core, and every duration
is finite and positive. Observed frame counts are 2, 3, 4, 5, 6, 7, 9, 20,
32, and 41. Preserve the still-opaque object tail after the frame array.

An initial level texture import should:

1. validate all Texture and AnimatedTexture definitions;
2. preserve physical definitions but build a Core-local logical token map;
3. walk selected direct roots and placed instance definitions;
4. use only each selected static resource's default LOD;
5. decode each Material's variable-length layers sequentially;
6. resolve every layer provider and expand AnimatedTexture frames;
7. decode indexed planes with section 3.4 and direct formats as packed bytes;
8. retain explicit placeholders for invalid formats and unresolved dynamic
   GLOW palette sources.

Do not select textures from all stored LODs, all stored Materials, globally
equal token values, or bytes at a guessed fixed layer stride.

### 3.7 Material representations and runtime GS state

Serialized Core material blobs and reflected runtime material objects are
different layouts. Identical-looking offsets must not be transferred between
them:

```text
Serialized Core Material
  +0x00  u32 layer_count
  +0x18  first variable-size layer record

Runtime Material (size 0x30)
  +0x1C  s32 layer_count
  +0x24  Material::Layer*

Runtime Material::Layer (size 0x90)
  +0x08  optional resource provider
  +0x0C  embedded HwLayer (size 0x74)

Runtime HwLayer
  +0x18  resolved HwTexture*
  +0x58  optional ref-counted 4x4 matrix object
  +0x5C  optional ref-counted six-float transform object
```

Runtime `HwLayer+0x58` is not a second texture, and serialized layer 1 is not
always at Material `+0x58`. Optional payloads make subsequent record offsets
variable.

The fixed `0x34`-byte serialized HwLayer payload maps field-for-field:

| Record | `HwLayer` | Renderer | Proven role |
|---:|---:|---:|---|
| `+0x0C` | `+0x00` | `+0x20C` | operation enum |
| `+0x0D` | `+0x04` | `+0x210` | lighting gate/mode |
| `+0x0E` | `+0x08` | `+0x214` | Z-write |
| `+0x0F` | `+0x09` | `+0x215` | framebuffer-state selector |
| `+0x10` | `+0x0C` | `+0x218` | depth comparison |
| `+0x11` | `+0x10` | `+0x21C` | alpha-test enum |
| `+0x12` | `+0x14` | `+0x220` | alpha-test reference |
| runtime | `+0x18` | `+0x224` | resolved `HwTexture*` |
| `+0x13/+0x14` | `+0x1C/+0x20` | `+0x228/+0x22C` | wrap S/T |
| `+0x15` | `+0x24` | `+0x230` | filter policy |
| `+0x16` | `+0x28` | `+0x234` | texture enabled / TCC input |
| `+0x18` | `+0x2C` | `+0x238` | TFX policy |
| `+0x19` | `+0x30` | `+0x23C` | alpha-source mode |
| `+0x1A` | `+0x34` | `+0x240` | alpha scalar |
| `+0x1B` | `+0x38` | `+0x244` | alpha-fade-range mode |
| `+0x1C..+0x28` | `+0x3C..+0x48` | `+0x248..+0x254` | four fade floats |
| `+0x31` | `+0x54` | `+0x260` | coordinate selector |
| optional | `+0x58` | `+0x264` | selector-11 4x4 matrix object |
| optional | `+0x5C` | `+0x268` | flags `0x20/0x40` transform |
| `+0x32/+0x36/+0x3A` | `+0x60/+0x64/+0x68` | `+0x26C/+0x270/+0x274` | directional vector |
| `+0x3E/+0x3F` | `+0x70/+0x71` | `+0x27C/+0x27D` | VU-feature booleans |

The coordinate selector edge is instruction-complete:

```text
record+0x31 -> HwLayer+0x54 -> renderer+0x260
            -> dispatch table 0x0053D290
```

| Selector | Handler |
|---:|---|
| 0 | STQ0 / UV0 |
| 1 | STQ1 / UV1 |
| 2 | STQ2 |
| 3 | STQ3 |
| 4-7 | same implementation as selector 0 |
| 8 | generated environment coordinates |
| 9-10 | generated reflection coordinates |
| 11 | custom serialized 4x4 matrix |
| 12 | angular 1D |
| 13 | directional 1D |
| 14 | projection |
| 15 | projection with alpha back-test |
| 16-21 | distinct, still unnamed coordinate/VU variants |

The flag-`0x20` six-float object reaches `renderer+0x268` as:

```text
[p0, p1, 0, 0]
[p2, p3, p4, p5]
```

This packing is exact. The multiplication convention and translation-axis
names remain unresolved. Layers render in ascending serialized order; a
runtime mask may suppress a layer but does not reorder survivors.

Depth, alpha test, wrap, and filter remaps are:

```text
depth 0 -> NEVER, 1 -> GREATER, 3 -> GEQUAL, otherwise ALWAYS

alpha test:
0 NEVER, 1 LESS, 2 EQUAL, 3 LEQUAL,
4 GREATER, 5 NOTEQUAL, 6 GEQUAL, 7 ALWAYS
AREF = serialized reference >> 1

wrap 0 -> REPEAT, nonzero -> CLAMP

filter 0 -> nearest policy 0x000
filter 1 -> linear policy 0x080
filter 2 -> mip policy 0x060
filter 3..6 -> alternate mip policy 0x120
```

Serialized operation indexes separate VU/color and GS-state tables; it is not
a raw GS `ALPHA` value:

| Value | Name | GS-alpha policy / side effect |
|---:|---|---|
| 0 | `WRITE` | `B ? 0x44 : 0xA8` |
| 1 | `MUL2X` | `0x89` |
| 2 | `ALPHABLEND` | `0x89` |
| 3 | `ALPHAMUL` | `B ? 0x48 : 0x68` |
| 4 | `ALPHAMUL2X` | `B ? 0x42 : 0x62` |
| 5 | `ALPHAADD` | `0x44` |
| 6 | `ALPHASUB` | `B ? 0x46 : 0x89` |
| 7 | `WRITEMASK` | `0x89` |
| 8 | `MASKEDWRITE` | `0x48` |
| 9 | `MASKEDADD` | `0x42` |
| 10 | `MASKEDSUB` | `0x46` |
| 11 | `LIGHTMAPCOLOR` | `0x6A` |
| 12 | `LIGHTMAPCOLOR2X` | `0x54` |
| 13 | `LIGHTMAPINTENSITY` | `0x58` |
| 14 | `LIGHTMAPINTENSITY2X` | `0x52` |
| 15 | `GLOW` | `0x89` |
| 16 | `LAYERBUMPEMBOSS` | `0x89` |
| 17 | `LAYERBUMPEMBOSSINV` | operation-6 policy; requests HIGHLIGHT |
| 18 | `LAYERBUMPDOT3` | `0x89`; requests HIGHLIGHT |
| 19 | `PROJECTIVESHADOW` | `0xA8` |
| 20 | `STENCILSHADOW` | operation-0 policy |

The low byte uses GS `(A-B)*C+D`:

```text
0xA8 (Cs-0 )*FIX + 0
0x44 (Cs-Cd)*As  + Cd
0x89 (Cd-0 )*As  + 0
0x68 (Cs-0 )*FIX + Cd
0x48 (Cs-0 )*As  + Cd
0x62 (0-Cs )*FIX + Cd
0x42 (0-Cs )*As  + Cd
0x46 (0-Cd )*As  + Cd
0x6A (0-0  )*FIX + Cd
0x54 (Cs-Cd)*Ad  + Cd
0x58 (Cs-0 )*Ad  + Cd
0x52 (0-Cs )*Ad  + Cd
```

`B` is derived from alpha scalar/fade and runtime policy; it is not another
serialized field. Operations 17/18 request HIGHLIGHT, but serialized TFX
policy 3 must preserve that request. Otherwise TFX is MODULATE. TCC is also
derived from texture-enabled and operation state rather than stored as a
separate byte.

The `0x4B RENDERSTATE` fragment emits `FOGCOL`, `ALPHA_1`, `TEX1_1`,
`ZBUF_1`, `TEXCLUT`, `TEST_1`, `FRAME_1`, `CLAMP_1`, `TEX0_1`, and
`MIPTBP1_1`. This material packet does not write `TEXA` or `MIPTBP2`;
`TEXA` is persistent GS state. `HwLayer+0x24` selects the `TEX1` filtering
policy, while `HwLayer+0x1C/+0x20` reduce to repeat or clamp for S/T. In this
path:

```python
indexed_tbw = max(2, width >> 6)  # PSMT8 and PSMT4
direct_tbw = max(1, width >> 6)
```

`MIPTBP1` can describe three emitted mip levels. No material-state
`MIPTBP2` write was found.

The static SDDocking target and confounder Materials have identical
non-provider state:

```text
layer 0: 0x06135DBC, WRITE,           STQ0,       TFX policy 1
layer 1: 0x06135D20, LIGHTMAPCOLOR2X, reflection, TFX policy 1
layer 2: target 0x06136A6E / confounder 0x06136A06,
         LAYERBUMPDOT3, STQ1, TFX policy 3
```

A retained candidate capture has three matching texture-state shapes:

| Pass | Captured texture state | Captured ALPHA/TFX |
|---:|---|---|
| 0 | PSMT8 256x128, TBW 4 | `0xA8`, MODULATE |
| 1 | PSMT8 32x32, TBW 2 | `0x58`, MODULATE |
| 2 | PSMT4 256x128, TBW 4 | `0x89`, MODULATE |

All use PSMCT32 CSM1 CLUTs with CSA 0 and CLD 1, repeat/repeat, linear
filtering, no alpha test, and depth GEQUAL. Pass 0 agrees with unchanged
static state. Pass 1 contradicts operation 12's static `0x54`, and pass 2
contradicts operation 18 plus TFX policy 3's HIGHLIGHT request. The captured
PSMT4 payload also matches neither serialized third texture.

This is a runtime-state mutation or attribution collision, not a valid reason
to assign the all-MODULATE family to either unchanged Material. Layer 1's
captured coordinates vary with frame/view, which agrees with the static
reflection selector and rules out a conventional static lightmap for that
candidate.

### 3.8 Blender texture policy

- Keep span records, stored indices, raw palette objects, selected logical
  palette, decoded RGBA/mask data, and post-layer tails as separate fields.
- Record PSM/CPSM/CSM/CSA/TBW and mip metadata when known.
- Decode `INDEX_4`/`ALPHA_4` low-nibble-first with section 3.4; do not apply a
  generic Morton transform.
- Treat `ALPHA_*` as white-RGB scalar alpha masks. Preserve `GLOW_8` indices
  and its dynamic-palette dependency until the callback source is reproduced.
- Retain both 4-bit alpha banks and record the runtime complement policy;
  descriptor selection changes CBP, not CSA.
- Keep alpha unchanged for file-decoded RGBA. Apply GS alpha expansion only to
  direct formats for which the corresponding `TEXA` behavior requires it;
  TEXA does not alter PSMCT32 CLUT alpha.
- Preserve `RGB_888` as three file channels. Any opaque alpha added for a host
  image API is an importer policy, not recovered game data.
- Direct STQ0/STQ1 is V-down when decoded row zero is represented as the image
  top. Apply serialized coordinate transforms first, then flip decoded rows or
  host V exactly once, never both.
- A standard normalized host sampler needs no added half-texel bias. A
  software GS sampler must model `floor(U-0.5)`/`floor(V-0.5)`.
- Export remains blocked until span/chunk framing, swizzle, palette choice,
  mips, padding, optional Material payloads, and unknown fields round-trip
  byte-identically.

## 4. Models

### 4.1 Storage model

There is no standalone native mesh extension in the validated catalog.
Renderable models are serialized as object graphs in `.core` and bulk data in
the paired `.corestream`. A single lump can package textures, materials,
meshes, skeletons, collision, effects, sounds, and the owning game resource.

Observed schema/type names include:

- `MeshResource`, `RegularStaticMeshResource`,
  `RegularStaticMeshResourcePS2`, `MultiStaticMeshResource`,
  `LodStaticMeshResource`, and `StaticMeshInstance`
- `RegularSkinnedMeshResource`, `MultiSkinnedMeshResource`,
  `LodSkinnedMeshResource`, and `SimpleAnimatingSkinnedMeshInstance`
- `Skeleton`, `SkeletonAnimationResource`, bone maps, bone sets, and hulls
- `Material`, layers, material type, and Texture references
- `CollisionResourcePolySoup`, `CollisionResourceBox`,
  `CollisionMultiResource`, and `CollisionMeshInstance`
- `Mesh Description.Bounding Box`, maximum visible distance, lighting,
  prelighting, sort mode/order, and static/dynamic shadow flags

Counts of files whose first string names a mesh class are not mesh counts;
compound level and entity lumps contain many later mesh objects. The generic
typed-section and object-directory framing below now locates those embedded
objects without scanning the CoreStream for signatures. The seven files whose
first serialized type is `RegularStaticMeshResource` remain useful as a small,
repeatable geometry-decoding corpus.

ELF class registration supplies runtime instance sizes of `0x38` for
`RegularStaticMeshResource`, `0xB0` for `RegularStaticMeshResourcePS2`, and
`0x24` for `RegularSkinnedMeshResource`. These are allocation/reflection
sizes, not on-disc structure sizes.

In particular, the serialized `RegularSkinnedMeshResource` Skeleton token at
object-relative `+0x24` must not be interpreted as an in-memory field inside
the registered `0x24`-byte runtime instance. The serialized property block and
the registered runtime allocation describe different layouts.

### 4.2 Generic typed-section framing

A Core body is a sequence of typed schema sections plus occasional opaque or
not-yet-classified data. Three directory encodings are proven: a 32-byte
stream-owning resource entry, a 24-byte non-streaming BoneMap entry, and an
8-byte compact entry used by spatial objects and instances. This section
describes the streamed-resource form; the BoneMap and compact forms are
documented in sections 4.21 and 5.2.

Let `S` be the start of a validated streamed-resource section, `P` its
property count, and `N` its object count:

```text
S+0x00  u32 data_offset
S+0x04  u32 type_string_index
S+0x08  u32 object_count
S+0x0C  u32 inheritance_level_candidate
S+0x10  u32 property_count
S+0x14  property_descriptor property[property_count]  # 16 bytes each
         u32 prefix_a                                  # observed 1
         u32 prefix_b                                  # observed 1
         object_directory_entry object[object_count]   # 32 bytes each
S+data_offset
         u32 zero_object_prefix
S+data_offset+4
         serialized object blobs, concatenated in directory order
```

For this directory form, the exact framing invariant is:

```text
data_offset = 0x1C + 0x10 * P + 0x20 * N
```

In other words, `data_offset` includes the five header words, property
descriptors, two prefix words, and the complete object directory. It points
to a universal zero word; the first object begins four bytes later. Strict
scanning also validates the type/property string indices, both prefix words,
the zero object prefix, all declared object sizes, and Core/CoreStream bounds.
Merely finding a plausible type index or accepting arbitrary zero-property
candidates produces false positives.

Each property descriptor is four `u32` words. The first is a Core string-table
index and the second is a four-byte property type tag. The last two carry
flags, size/default information, or a runtime property offset depending on the
type. `General.Skeleton` happens to be `+0x24` in both the runtime descriptor
and the corrected serialized skinned object, but that equality is
class-specific; runtime reflection offsets must not be used as general file
offsets.

The fourth section-header word is consistently:

- `0` for `RegularStaticMeshResource` and
  `RegularSkinnedMeshResource`;
- `1` for `Skeleton`, `Material`, and both `Multi*MeshResource` types;
- `2` for `LodStaticMeshResource` and `LodSkinnedMeshResource`.

This is strongly consistent with inheritance depth, but the engine's exact
field name is not yet proven.

### 4.3 Object directory, stream ownership, and references

Each 32-byte streamed-resource object-directory entry is:

| Entry offset | Type | Meaning |
|---:|---|---|
| `0x00` | `u32` | Serialized object-blob size |
| `0x04` | `u32` | Object ID/reference token |
| `0x08` | `u32[4]` | Stable identity/GUID-like words; exact algorithm unknown |
| `0x18` | `u32` | Offset from the start of the complete paired CoreStream entry, including its 64-byte header |
| `0x1C` | `u32` | Owned CoreStream byte size |

Require the zero word at `S + data_offset`. Object blobs begin at
`S + data_offset + 4` and are concatenated using their declared sizes. The
stream offset is not relative to the Core section, serialized object blob, or
first payload byte. Resolve the absolute ownership span with checked
arithmetic:

```text
object_stream_base =
    checked_add(complete_CoreStream_entry_start,
                directory_entry.stream_offset)
object_stream_end =
    checked_add(object_stream_base,
                directory_entry.stream_size)
```

Require `object_stream_end` not to exceed the complete CoreStream entry.
Objects without stream data normally carry size zero and an offset at the end
of that entry. Treat `[object_stream_base, object_stream_end)` as the sole
ownership bound; do not infer ownership from the next DMA-looking signature.


A strict inventory of 102 retail Core files whose string tables advertise a
regular mesh schema found:

| Type | Objects | Mesh records | Packets | Stored triangles |
|---|---:|---:|---:|---:|
| `RegularStaticMeshResource` | 9,461 | 20,848 | 165,781 | 4,604,047 |
| `RegularSkinnedMeshResource` | 1,244 | 2,386 | 29,680 | 1,029,286 |
| `Skeleton` | 416 | | | |
| `Material` | 16,854 | | | |

For every one of the 23,234 regular-mesh records, the record's reference token
resolves to a `Material` directory entry in the same Core. The serialized
skinned-object value at `+0x24` resolves to a `Skeleton` entry for 1,233 of
1,244 objects; the remaining 11 store zero. These are verified typed
references, not hashes guessed from filenames.

Object IDs are not safe as globally unique primary keys. Some large Cores
repeat an ID for multiple copies of the same non-model type. Resolve a token
within its Core and require the expected target type; preserve all directory
identity words when duplicates remain.

### 4.4 Validated leading-static-mesh corpus

The following variant-5 resources use the same root schema and were parsed
directly from the retail ISO. Paths are relative to
`localcache.ps2/lumps/`.

| Resource | Records | Packets | Stream chunk | Strip vertices | Emitted triangles |
|---|---:|---:|---:|---:|---:|
| `custom.default` | 1 | 6 | `0x1300` | 340 | 186 |
| `custom.mortarshell` | 1 | 1 | `0x400` | 52 | 36 |
| `level.11.sdplatform_luger` | 1 | 1 | `0x200` | 25 | 12 |
| `level.11.sdplatform_templar` | 3 | 5 | `0xA00` | 178 | 118 |
| `level.5.docks2` | 1 | 1 | `0x100` | 4 | 2 |
| `level.7.1` | 1 | 2 | `0x700` | 85 | 49 |
| `level.7.3` | 1 | 1 | `0x200` | 25 | 12 |
| **Total** | **9** | **17** | | **709** | **415** |

Every packet offset, packet bound, record bound, DMA transfer length, vertex
count, optional stream flag, and primitive count described below agrees
across this corpus. This is a verified format for a *leading*
`RegularStaticMeshResource`. Generic ownership and embedded-object discovery
are covered separately below.

### 4.5 Leading static-mesh Core body

The body starts at `0x18 + string_table_size`. The first 15 strings must be
`RegularStaticMeshResource` followed by the 14 property names listed in its
root schema. The validated body begins:

| Body offset | Type/size | Meaning |
|---:|---|---|
| `0x000` | `u32[5]` | Root record: `{0x11C, 0, 1, 0, 14}` |
| `0x014` | 14 x 16 bytes | Property descriptors |
| `0x0F4` | `u32[2]` + 32-byte entry | Section prefix `{1, 1}` and object directory |
| `0x11C` | `u32` | Zero object prefix |
| `0x120` | `u8[0x40]` | Root property-value block |
| `0x138` | `f32` | `Lighting.Lightmap Density Factor` |
| `0x13C` | `f32` | `Visibility.Maximum visible distance` |
| `0x148` | `f32[6]` | Resource bounding box: min XYZ, max XYZ |
| `0x160` | variable | PS2 static-mesh custom block |

The descriptors at `0x14` exactly name sort order/mode, shadow and prelight
flags, linear/no-streaming/name settings, parent/name references, lightmap
density, maximum distance, and the bounding box. Reject this specialized
layout if the strings, root tuple, descriptor count, type codes, sizes, or
offsets differ.

Let `C = body + 0x160`. The custom block is:

```text
C+0x00  u32       stream_chunk_count
C+0x04  u32[]     stream_chunk_size[stream_chunk_count]
         align 4
         u32       mesh_record_count
         record[]  mesh_record[mesh_record_count], 24 bytes each
         u8[2]     root flags, semantics unknown
         align 4
         f32[16]   position decode matrix
         f32[4]    position scale vector
```

All seven retail roots have one stream chunk. Six have root flags `{1, 0}`;
`level.7.1` has `{0, 0}`. These flags are not yet named.

Each 24-byte mesh record is:

| Record offset | Type | Meaning |
|---:|---|---|
| `0x00` | `u32` | `Material` object ID/reference token |
| `0x04` | `u32` | Emitted triangle count |
| `0x08` | `u16` | Packet count |
| `0x0A` | `u8` | Unknown flag; 1 in all nine records |
| `0x0B` | `u8` | Attribute flags; bit `0x10` tracks the optional V4-8 stream |
| `0x0C` | `s16[3]` | Quantized record minimum XYZ |
| `0x12` | `s16[3]` | Quantized record maximum XYZ |

The six bounds are exactly the component-wise extrema of all packet positions
owned by that record. The first word resolves to a `Material` object-directory
entry and is the Blender material binding for the record.

### 4.6 Static CoreStream chunk and packet directory

The declared chunks are consecutive ranges beginning at the owning directory
entry's absolute CoreStream offset; there is no extra alignment between them.
That offset is `0x40` for the leading roots in the small corpus. Each object
owns exactly the sum of its declared chunk sizes. Bytes after that range
belong to later objects in the compound stream and must not be consumed as
static-mesh data.

The first chunk begins with a parallel-array packet directory. For each mesh
record, in record order:

```text
align 4
u32 packet_relative_offset[record.packet_count]
s16 packet_bounds[record.packet_count][6]  # min XYZ, max XYZ
u8  packet_chunk_index[record.packet_count]
```

Offsets are relative to the start of the selected chunk. All 17 retail
packets select chunk zero and all offsets are 16-byte aligned. There is no
stored packet byte length. Bound a packet by the next packet offset in the
same chunk, or by the chunk end for the final packet, then use its DMA `QWC`
for the actual transfer length. The directory's six signed bounds exactly
equal the extrema of the packet's decoded positions.

### 4.7 Static DMA/VIF packet

Each packet is a callable VIF1 DMA subchain, not arbitrary packed data. It
starts with this 128-bit tag/header:

| Packet offset | Value | Meaning |
|---:|---:|---|
| `0x00` | `0x60000000 OR QWC` | DMA tag ID 6 (`RET`) and following qword count |
| `0x04` | `0x80000000` | Fixed tag address/SPR word in all samples |
| `0x08` | `0x01000404` | VIF `STCYCL 4,4` |
| `0x0C` | `0x6C02C1D9` | VIF `UNPACK V4-32`, count 2, for the header vectors |

The transferred range ends at:

```text
packet_offset + 0x10 + QWC * 0x10
```

That endpoint is within the directory-derived packet bound in every sample.
Any remaining bytes in the bound are zero allocation padding.

The two unpacked V4-32 header vectors at `0x10` and `0x20` are:

```text
header_a = {
    vertex_count,
    3 * vertex_count,
    3 * align_up(vertex_count, 4),
    end_of_16_bit_coordinate_arrays_in_VU_memory,
}

header_b = {
    position_VU_address,       # always 0xB7
    normal_ADC_VU_address,
    color_or_coordinate_VU_address,
    first_coordinate_VU_address,
}
```

For vertex count `N`, the fixed prefix obeys:

```text
header_b[0] = 0xB7
header_b[1] = header_b[0] + N
header_b[2] = header_b[1] + N
```

Position targets `header_b[0]` and normal/ADC targets `header_b[1]`. If the
optional color stream exists, it targets `header_b[2]` and
`header_b[3] = header_b[2] + N`; otherwise
`header_b[3] = header_b[2]`. Every 16-bit coordinate command then consumes
one VU quadword per vertex and advances the destination by `N`.
`header_a[3]` is the destination after the last such coordinate command.

Starting at packet offset `0x30`, all 164,630 packets in the 47 level Cores
follow this grammar:

```text
V3-16 position
V4-5  normal/ADC
[ V4-8 unsigned color ]
{ V2-16 | V4-16 }*          # zero or more 4.12 coordinate commands
[ V1-8 | V2-8 | V3-8 ]     # optional terminal auxiliary stream
```

Every command has `NUM == N`; payloads are rounded up to the next four-byte
command boundary. The V4-8 command is present exactly when the mesh-record
flags contain `0x10`. V2-16 contributes one signed coordinate pair and
V4-16 contributes two. Do not require a final V2-16 command: 98,746 of the
164,630 physical packets do not have one.

The optional byte-packed command targets `header_a[3]`, after the coordinate
arrays. It does not contribute to the stored value of `header_a[3]`. Its
exact renderer semantics are not yet proven, so retain its raw bytes and
component width rather than treating it as another UV set.

The complete physical packet-shape inventory is:

| Commands after the fixed headers | Packets |
|---|---:|
| `69 6F 65` | 37,092 |
| `69 6F 6D` | 6,983 |
| `69 6F 6D 65` | 296 |
| `69 6F 6D 66` | 7 |
| `69 6F 6D 6D` | 27 |
| `69 6F 6E` | 287 |
| `69 6F 6E 65` | 25,578 |
| `69 6F 6E 6D` | 67,038 |
| `69 6F 6E 6D 62` | 609 |
| `69 6F 6E 6D 65` | 2,918 |
| `69 6F 6E 6D 65 62` | 8 |
| `69 6F 6E 6D 65 66` | 2,545 |
| `69 6F 6E 6D 65 6A` | 14,118 |
| `69 6F 6E 6D 66` | 278 |
| `69 6F 6E 6D 6A` | 1,639 |
| `69 6F 6E 6D 6D` | 473 |
| `69 6F 6E 6D 6D 66` | 1,443 |
| `69 6F 6E 6D 6D 6A` | 3,291 |

Here `62`, `66`, and `6A` are V1-8, V2-8, and V3-8 respectively. The original
seven-resource corpus happens to use only a final V2-16 coordinate command;
that is not a generic static-mesh invariant. Packets with no 16-bit
coordinate command are valid generated-coordinate material paths.

SDDocking adds a validated dual-coordinate variant. All 35 packets belonging
to material `0x061763EA`'s static mesh, and all 35 packets in the adjacent
`0x060E9986` confounder mesh, contain V3-16, V4-5, unsigned V4-8, then signed
V4-16. The four V4-16 lanes are two independent coordinate pairs, not padding
or an unrelated vector.

The last word of every DMA transfer is `0x6501C1EE`, an `UNPACK V2-16` of one
element to VU address `0x1EE`. Its four data bytes are not inside the `RET`
packet. The calling DMA/VIF chain must supply them. This dependency is why a
packet should not be submitted or interpreted as a complete standalone VIF
program.

### 4.8 Static vertex decoding

#### Positions and bounds

The 64-byte block after the record table is consistently:

```text
sx  0   0   0
0   sy  0   0
0   0   sz  0
tx  ty  tz  1
```

The following vector is `{sx, sy, sz, 0}`. The matrix values correspond to
the root floating-point bounding box: the scale is approximately the axis
range divided by `65528.45`, and the translation is the center plus half a
scale unit. Use the stored values rather than recomputing them.

The strongly supported source-space decode is:

```python
x = raw_x * sx + tx
y = raw_y * sy + ty
z = raw_z * sz + tz
```

where each raw component is signed 16-bit. Retain both the raw values and
matrix until a runtime vertex comparison confirms the last rounding details.

Packet and mesh-record integer bounds equal the raw-position extrema exactly.
The serialized floating resource AABB is slightly less exact: across 9,327
level resources, decoded extrema can exceed it by up to `0.196252` world
units, but always remain within four steps of the largest stored decode
scale. Use that quantization-aware tolerance rather than requiring strict
floating-point containment.

#### Packed normals and strip control

For a little-endian V4-5 word:

```python
nx = ((packed >> 0) & 0x1F) - 15
ny = ((packed >> 5) & 0x1F) - 15
nz = ((packed >> 10) & 0x1F) - 15
adc = (packed >> 15) & 1
normal = normalize((nx, ny, nz))
```

The biased-normal interpretation is a strong inference, but it is consistent
across all 709 vertices: axis-aligned samples decode exactly, and other vector
lengths cluster near 15 before normalization. The hardware V4-5 unpack expands
the three 5-bit channels and the one-bit W channel; the VU program still needs
to be traced to prove its exact normalization.

The ADC bit's topology role is verified. The first two vertices of every
packet have `adc == 1`; later set bits suppress the current triangle while
retaining triangle-strip state. Across every record, the stored triangle count
equals exactly the number of vertices with `adc == 0`. Reconstruct a standard
alternating-winding triangle strip without removing ADC vertices before
evaluating parity. There is no separate index array in this validated subset.

#### Coordinates, optional color, and auxiliary tails

Each V2-16 or V4-16 coordinate lane pair is signed 4.12 fixed point:

```python
q_gs = per_vertex_perspective_q
u = signed16_x / 4096.0
v = signed16_y / 4096.0
GS_ST = (u * q_gs, v * q_gs, q_gs, q_gs)
```

For V4-16, XY supplies UV0 and ZW supplies UV1. The target SDDocking mesh has
UV0 ranges `U=-1.326904..1.073975`, `V=-3.712646..3.794434`; UV1 stays near
the unit square at `U=0.000488..0.918701`,
`V=0.001953..0.996094`. The large UV0 range and captured repeat/repeat
addressing establish intentional tiling. Here `q_gs` names the per-vertex GS
perspective-Q value, not an aligned vertex count.

STQ0 applies `ITOF12` to XY and multiplies by `q_gs`. STQ1 routes ZW to XY,
applies `ITOF12`, and multiplies by `q_gs`. Neither direct path negates V. GS
normalized coordinates are:

```text
U = (S/q_gs) * texture_width
V = (T/q_gs) * texture_height
```

Linear filtering selects neighbors from `floor(U-0.5)` and
`floor(V-0.5)`. Thus texel `(x,y)` is centered at
`((x+0.5)/width,(y+0.5)/height)`. If decoded row zero is represented as the
image top, direct STQ is V-down. A standard normalized host sampler already
has the correct center convention; do not add a second half-texel bias. Flip
decoded rows or host V exactly once, after applying any serialized coordinate
transform.

Additional V2-16 or V4-16 commands continue at successive VU addresses and
can raise the stored total to four coordinate pairs. Preserve all pairs even
when the active material uses only UV0. The terminal V1-8, V2-8, and V3-8
streams are a separate command class whose semantics remain open; do not
divide those bytes by 4096 or expose them as proven UVs.

Serialized layer `+0x31` is copied directly to `HwLayer+0x54`, then to
renderer `+0x260`, where it chooses:

```text
0        -> UV0 / STQ0
1        -> UV1 / STQ1
2, 3     -> STQ2 / STQ3
4..7     -> same implementation as STQ0
8        -> generated environment coordinates
9, 10    -> generated reflection coordinates
11       -> serialized 4x4 matrix
12       -> angular 1D
13       -> directional 1D
14, 15   -> projection variants
16..21   -> distinct, still unnamed coordinate/VU variants
```

Generated environment/reflection modes use normals and matrices rather than
the stored V4-16 lanes. Target serialized selector bytes `{0, 10, 1}` fit
`{UV0, reflection, UV1}`, and match the base, view-dependent, and mask roles
seen in the ambiguous runtime family. The full serialized-to-renderer edge is
instruction-proven.

V4-8 is unsigned vertex diffuse RGBA, not unconditional final prelight.
`custom.mortarshell` is uniformly `{0x80, 0x80, 0x80, 0x80}`, while
`level.7.1` contains equal RGB grayscale and a varying fourth channel. All 35
target SDDocking packets use R 98-113, G 120-126, B 99-114, and A exactly
127.

The VU `LIGHT_CUBIC_DIFFUSE` path builds a cubic-light term from the squared
normal components and six signed-axis coefficients, scales it by `1/255`,
then computes:

```text
RGBout = trunc(cubic_light(normal) * V4_8.rgb)
```

Thus RGB is a per-vertex diffuse/albedo multiplier for dynamic lighting. The
target serializes `Lighting.Prelit=false`, while the separate `LIGHT_PRELIT`
path only copies vertex color. Alpha is material-path dependent: the
constant-alpha combine emits `128 / 2 = 64` without reading V4-8 A, whereas
the vertex-alpha alternative would truncate `127 / 2` to 63. The ambiguous
three-pass runtime family emits A=64, so its V4-8 A is not passed through.
Preserve all four source bytes even when a particular continuation ignores A.

SDDocking's two decoded RenderZones serialize black fog. Its LightZones
resolve to real directional lights, but no bounded trace establishes which
LightZone, if any, populated the six cubic-light coefficients for this mesh.
RenderZone fog is therefore not a demonstrated source of the observed vertex
color, and exact LightZone-to-mesh lighting provenance remains open.

### 4.9 Embedded static resources

The static custom block begins at corrected serialized object offset `+0x40`,
not at an absolute Core-body location. The leading-root address
`body + 0x160` is `body + 0x120 + 0x40`. To parse an embedded object:

1. take its blob from the typed section's object directory;
2. parse the custom block at object `+0x40`;
3. start its chunks at the directory entry's absolute CoreStream offset;
4. reject the object unless the chunk sum equals its directory stream size;
5. resolve every mesh-record token to a `Material` in the same Core.

The decode matrix is not at a fixed object offset. For `C` chunks and `R`
mesh records, locate it relative to the complete serialized object blob:

```python
cursor = 0x44 + 4 * C
cursor += 4 + 0x18 * R
cursor += 2                       # root flags
matrix_offset = align_up(cursor, 4)
matrix = read_f32_array(matrix_offset, 16)
scale = read_f32_array(matrix_offset + 0x40, 4)
```

`level.11.sdplatform_templar` contains seven separate
`RegularStaticMeshResource` sections, including one section with 34 objects.
The directory-owned parse validates 40 static objects, 357 mesh records,
2,251 DMA packets, 114,912 strip vertices, and 62,014 emitted triangles in
that one level. Every record resolves to a material, and the same bounds,
position, normal/ADC, optional color, UV, and triangle-count invariants used
for the leading roots hold. This closes the embedded-static discovery and
stream-ownership gap; `MultiStaticMeshResource`, `LodStaticMeshResource`, and
instance transforms are still separate wrapper formats.

A later strict pass over all 47 level Cores validates 9,327 regular static
objects, 164,630 physical packets, and 4,567,004 physically stored triangles.
It accepts the 18 packet-tail shapes in section 4.7 while retaining exact
packet allocation, chunk ownership, record bounds, ADC triangle totals, VU
destination equations, and material-reference checks.

### 4.10 Regular skinned Core object

The serialized `RegularSkinnedMeshResource` property block stores its nullable
`Skeleton` object ID at `+0x24`. Its PS2 custom block starts at `+0x40`:

```text
object+0x40  u32 chunk_count
             u32 chunk_size[chunk_count]
             u32 mesh_record_count
             skinned_record record[mesh_record_count]  # 16 bytes each
             u32 packed_resource_flags
             u32 auxiliary_offset                      # present in 1,244/1,244
             zero padding to the declared object size
```

Each 16-byte skinned record is:

| Record offset | Type | Meaning |
|---:|---|---|
| `0x00` | `u32` | Upper 16 bits: record directory offset in this object's stream; lower 16 bits: flags |
| `0x04` | `u32` | Packet count |
| `0x08` | `u32` | Emitted triangle count |
| `0x0C` | `u32` | `Material` object ID |

The record flags are zero in 2,010 records and one in 376 records. The high
byte of `packed_resource_flags` is the count of 32-byte transform/quantization
records in the auxiliary stream area; the retail range is 1 through 81. Its
low 24 bits are observed as `0`, `1`, or `0x101`. Exact feature names for
these bits are unknown.

The auxiliary offset is a valid nonzero serialized word in all 1,244 objects.
The former 29 "omissions" were terminal offset words hidden by the old
four-byte object-origin error; no recovery scan is required. Validate the
offset against the owned CoreStream range rather than inferring it by rounding
the minimum directory size.

Across all 1,244 objects:

- chunk sizes sum exactly to the object-directory CoreStream size;
- every record's material token resolves;
- 1,233 skeleton tokens resolve and 11 are null;
- the records declare 29,680 packets and 1,029,286 triangles.

### 4.11 Skinned CoreStream directory and palette

Resolve every nested address from its stated base, using checked additions,
multiplications, cumulative sums, and alignments:

```text
record_directory =
    checked_add(object_stream_base,
                record.directory_offset_and_flags >> 16)
auxiliary =
    checked_add(object_stream_base, auxiliary_offset)
chunk_start[i] =
    checked_add(object_stream_base,
                checked_sum(chunk_size[0:i]))
packet[i] =
    checked_add(chunk_start[packet_chunk_index[i]],
                packet_chunk_offset[i])
```

The record-directory and auxiliary offsets are object-stream-relative.
`packet_chunk_offset` is instead relative to the selected chunk.

EE `0x400C6C..0x400C74` skips records whose packet count `P` is zero, whereas
the renderer from `0x400FF8` assumes packet zero exists. Treat `P == 0` as an
empty, non-renderable record: do not dereference its nested directory, do not
evaluate `P - 1` in unsigned arithmetic, and do not submit it to that renderer.
This runtime behavior does not prove that a serialized zero-count record is
itself malformed.

For a nonempty record, the directory is:

```text
u32 packet_key[P]             # semantics unknown
u32 packet_chunk_offset[P]
u8  packet_chunk_index[P]
align 4
u8  packet_metadata_a[P]      # semantics unknown
align 4
u8  packet_metadata_b[max(P - 1, 0)]  # semantics unknown
align 4
zero padding to the next record directory or auxiliary area
```

Its checked extent is:

```text
d   = record_directory
e1  = checked_add(d,  checked_mul(4, P))
e2  = checked_add(e1, checked_mul(4, P))
e3  = checked_add(e2, P)
a1  = checked_align_up(e3, 4)
e4  = checked_add(a1, P)
a2  = checked_align_up(e4, 4)
e5  = checked_add(a2, max(P - 1, 0))
end = checked_align_up(e5, 4)
```

Require `d >= object_stream_base`. Require `end` not to exceed
`object_stream_end`, the auxiliary address, or the next greater validated
nonempty record-directory address, whichever comes first. Apply the same
ownership bound to the auxiliary transform table and palette rather than
locating their end from signatures.

`packet_chunk_index` is proven: adding the selected chunk's cumulative start
to `packet_chunk_offset` lands on the corresponding DMA `RET` packet for all
29,680 entries. Packet offsets are 16-byte aligned, may reset to zero in a
later chunk, and must be bounded by that chunk rather than by directory order
alone. Require:

```text
checked_sum(chunk_size[0:chunk_count]) == directory_entry.stream_size
packet_chunk_index[i] < chunk_count
packet_chunk_offset[i] % 16 == 0
packet_chunk_offset[i] + 16 <= chunk_size[packet_chunk_index[i]]

qwc = u32le(packet[i]) & 0xFFFF
dma_extent = checked_mul(checked_add(qwc, 1), 16)
packet_chunk_offset[i] + dma_extent <=
    chunk_size[packet_chunk_index[i]]
```

The initial 16-byte check must precede reading `QWC`; the final check bounds
the DMA tag plus its declared qwords within the selected chunk.

The auxiliary area begins with the count from the high byte of
`packed_resource_flags`:

```text
transform_record transform[transform_count]  # 32 bytes each

transform_record:
    f32 center_a[3]
    u32 selector
    f32 full_extent_b[3]
    u32 zero
```

All `A` and `B` components are finite, and all 33,966 observed `B` components
are strictly positive. The VU equations in section 4.13 prove that `A` is a
quantization-box center and `B` its full extent. The selector spans `0..119`.
It is the zero-based source-order bone index in the referenced Skeleton. The
correct bound is the packed Skeleton header's `u8 bone_count` at block
`+0x07`, not the outer object's name-string index at `+0x04`.

All 11,296 records whose mesh has a nonnull Skeleton token satisfy
`selector < bone_count`. The 11 meshes with null direct tokens occur in
wrapper contexts that each provide one Skeleton candidate; their remaining
26 records also satisfy the bound. Runtime `draw+0x74` remains authoritative
for those null-token cases. Of 1,070 nonnull meshes with wrapper context,
1,069 direct and wrapper tokens agree. The sole mismatch uses Skeletons with
the same 94 ordered bone names and categories but different temporary
transforms, so it does not introduce a selector permutation.

After the transform table is a compact palette-index list. A packet whose
header declares `G` groups consumes exactly `G` bytes from this list; each
byte selects one of the object's transform records. The list length over an
object is therefore:

```text
sum(packet.header_a[3] for packet in object)
```

The end of this list is zero-padded to 16 bytes and is exactly the first DMA
packet offset. There are 52,249 palette indices across the retail skinned
corpus. This relationship, the transform-table bounds, and every palette
index were validated against all 1,244 serialized auxiliary offsets. Every
one of the 11,322 local transform records is referenced by at least one packet
palette byte.

The EE lookup is direct. Both regular-skinned renderer paths load the
selector from record `+0x0C`, the matrix-array base from `draw+0x74`, shift
the selector left by six, and load four qwords:

```text
matrix_base = *(draw + 0x74)
selected    = matrix_base + 0x40 * record.selector

group[g] = {
    selected.C0,
    selected.C1,
    selected.C2,
    selected.C3,
    record.A + selector bits,
    record.B + zero,
}
```

The primary sequence is at EE `0x400EE0..0x400F30`; the alternate renderer
repeats it at `0x401B80..0x401BD4`. There is no mesh-local remap or
indirection between the serialized selector and the matrix array. The array
is the completed Skeleton palette `G[0..B-1]` in original source-bone order,
as detailed in section 4.20. The VU never reads the staged selector lane.

### 4.12 Skinned DMA/VIF memory layout

Skinned packets use the same callable DMA prefix as static packets:

```text
0x60000000 OR QWC, 0x80000000,
STCYCL 4,4, UNPACK V4-32u count=2 address=0x1D9
```

Define:

```text
N = output strip-target count
G = transform-group count
P = sum(group_size[0:G])       # logical position/normal pairs
N_aligned = align_up(N, 4)
C = packet has a V4-8u color/prelight tail
```

All 29,680 retail packet headers satisfy:

```text
header_a = {
    N,
    3 * N,
    3 * N_aligned,
    G,
}

header_b = {
    v3_start       = 0x0B5 + 6 * G,
    normal_scratch = 0x0B5 + N_aligned,
    tail_base      = max(0x0B5 + 2 * N_aligned, v3_start + 2 * P),
    uv_base        = tail_base + (N if C else 0),
}
```

Before the callable packet, the EE stages six VU qwords per group at
`TOP + 0x0B5`:

```text
group[g].q0 = C0                       # selected affine column 0
group[g].q1 = C1                       # selected affine column 1
group[g].q2 = C2                       # selected affine column 2
group[g].q3 = C3                       # translation column
group[g].q4 = (A.x, A.y, A.z, selector bits)
group[g].q5 = (B.x, B.y, B.z, 0)
```

`C0.w`, `C1.w`, and `C2.w` are zero; `C3.w` is one. The packet then:

1. uses masked unsigned `S8`, `STCYCL 1,6`, to write each group size into
   `q5.w`;
2. unpacks `2 * P` signed `V3-8` values at `v3_start`;
3. unpacks `2 * P` masked `S16` controls to the same address, supplying each
   V3 qword's W lane; and
4. unpacks target-indexed tail attributes.

The `max` in `tail_base` prevents target attributes from overwriting a large
still-live V3/control source. After geometry scatter, the common VU code
compacts `N` positions to `TOP + 0x0B5` and `N` normals to
`TOP + normal_scratch`.

Corpus and runtime validation covered:

| Measurement | Retail corpus | Captured runtime calls |
|---|---:|---:|
| Skinned objects | 1,244 | |
| Packets/calls | 29,680 | 193,550 |
| Transform groups | 52,249 | 462,075 |
| Logical position/normal pairs | 1,374,527 | 7,824,650 |
| Emitted strip targets | 1,661,564 | 9,907,000 |

All 462,075 staged runtime decode records matched serialized retail records
byte-for-byte.

### 4.13 Skinned geometry, transform, and topology

Each logical input vertex is exactly two signed `V3-8` records:

```text
record[2*i + 0] = (signed position byte x/y/z, control0)
record[2*i + 1] = (signed normal byte x/y/z, control1)
```

The second vector's rounded length is 126 through 129 for all 1,374,527
pairs; the first has no such invariant. For either control:

```python
raw_control = control & 0xFFFF
qword_offset = raw_control & 0x7FFF
assert qword_offset % 3 == 0
target = qword_offset // 3
assert 0 <= target < N
adc = (raw_control & 0x8000) != 0
```

Treat the control as a raw 16-bit word even when the VIF lane was decoded to a
signed host integer. An arithmetic right shift of a negative signed value
yields `-1`, not Boolean `1`; masking also avoids host-language shift
differences. The target bound must be checked before indexing strip output.

Every target `0..N-1` belongs to exactly one logical pair and transform group.
There are 1,087,490 same-target pairs and 287,037 different-target pairs:

```text
1,661,564 targets
    = 1,087,490 same-target pairs
    + 2 * 287,037 different-target pairs
```

A different-target pair writes the complete transformed position and normal
to both destinations. The duplicated targets may retain different UV/color
attributes. This is geometry duplication for strips and seams, not weighted
skin accumulation. Each logical pair is transformed once with implicit
weight `1.0`.

Interpret the staged matrix qwords as affine columns `C0..C3`, the selected
record as center `A` and full extent `B`, and the signed position bytes as
`p`. The exact position decode is:

```python
local.x = A.x + B.x * ((float(p.x) + 0.5) / 255.0)
local.y = A.y + B.y * ((float(p.y) + 0.5) / 255.0)
local.z = A.z + B.z * ((float(p.z) + 0.5) / 255.0)

position = (
    C0 * local.x
  + C1 * local.y
  + C2 * local.z
  + C3
)
```

Signed bytes span `-128..127`, so `(p + 0.5) / 255` spans exactly
`-0.5..+0.5`. There is no hidden factor of two or axis negation.

For signed normal bytes `n`, the exact decode is:

```python
q = 1.0 / sqrt(dot(C0.xyz, C0.xyz))
normal = q * (
    C0.xyz * (float(n.x) / 127.0)
  + C1.xyz * (float(n.y) / 127.0)
  + C2.xyz * (float(n.z) / 127.0)
)
```

The VU removes one uniform scale using `C0` and performs no inverse-transpose
calculation. Interpreter replay of one- and four-group packets matched these
equations within `2.19e-4` for position and `1.68e-7` for normal; differing
destinations received bit-identical position and normal qwords.

ADC is per destination and all four ADC combinations occur on
different-target pairs. For all 2,386 records, the stored triangle count
equals the number of unique source targets with `adc == 0`, totaling
1,029,286. Use source controls for strip topology: clipping can rewrite the
final GS `XYZF2` ADC bits.

The common decoder is an identical target-relative VU body duplicated at 52
material-specific MSCAL entry addresses across 684 captured active
microcode states. Material continuations diverge only after the shared
geometry kernel. The VU geometry decode and the EE selector-to-matrix lookup
are both resolved; material-specific tail semantics remain separate.

### 4.14 Skinned tail attributes

The 29,680 packet tails fall into ten shapes, nine nonempty:

| Packet count | Tail after V3/S16 | Structural interpretation |
|---:|---|---|
| 6 | none | zero candidate UV pairs, no color |
| 88 | `V4-8u` | zero pairs plus color/prelight |
| 20,558 | `V2-16` | one pair |
| 1,318 | `V2-16`, `V4-8u` | one pair plus color/prelight |
| 4,877 | `V4-16` | two pairs |
| 931 | `V4-16`, `V4-8u` | two pairs plus color/prelight |
| 852 | `V4-16`, `V2-16` | three pairs |
| 564 | `V4-16`, `V2-16`, `V4-8u` | three pairs plus color/prelight |
| 46 | `V4-16`, `V4-16` | four pairs |
| 440 | `V4-16`, `V4-16`, `V4-8u` | four pairs plus color/prelight |

Every tail unpack count is `N`. One signed `V2-16` contributes one pair and
one signed `V4-16` contributes two. Although the VIF command order places
signed pairs before optional color, VU-memory routing is:

```python
color_address = header_b.tail_base             # only when color exists
first_uv_address = header_b.uv_base
second_uv_pack_address = first_uv_address + N  # when present
```

The first `V2-16` pair, or `V4-16.xy`, is verified 4.12 fixed-point texture
data:

```python
u = signed16_x / 4096.0
v = signed16_y / 4096.0
GS_ST = (u * q_gs, v * q_gs, q_gs, q_gs)
```

Here `q_gs` is the per-vertex perspective-Q value; it is unrelated to
`N_aligned` in the packet-memory equations.

Treating `V4-16.zw` and later packs as additional texture-coordinate pairs is
a strong inference from the complete zero-through-four-pair schema;
material-layer tracing is still required to name them.

`V4-8u.xyz` is verified to route directly to vertex RGB in two material
continuations, while another continuation modulates or replaces it. Source
alpha is also material-dependent. Preserve all four raw bytes; RGB
color/prelight is strongly supported, but unconditional RGBA passthrough is
false.

### 4.15 Mesh instances and placement transforms

`StaticMeshInstance` and `SimpleAnimatingSkinnedMeshInstance` use compact
typed-section directories and own no CoreStream bytes. They use the same
section-level zero word: object zero starts at `S + data_offset + 4`. All 47
level Cores contain one `StaticMeshInstance` section; 37 contain the
simple-skinned type.

The 21,762 static instances have fixed `0x60`-byte blobs:

| Offset | Type | Meaning |
|---:|---|---|
| `0x00` | `u32` | Name-is-identifier flag, zero or one |
| `0x04` | `u32` | Core string index for name |
| `0x08` | `u32` | Parent token, always null |
| `0x0C` | `f32[16]` | Orientation |
| `0x4C` | `u32` | Static resource token |
| `0x50` | `u32` | Opaque footer |
| `0x54` | `u8[0x0C]` | Zero trailer |

Resource targets are 14,642 `LodStaticMeshResource`, 7,013
`RegularStaticMeshResource`, 106 `MultiStaticMeshResource`, and one legal
null. Preserve the footer; its observed values are `0x00000001`,
`0x00010001`, `0x00020001`, and `0x00030001`, with unknown meaning.

The 656 simple-skinned instances have fixed `0x70`-byte blobs:

| Offset | Type | Meaning |
|---:|---|---|
| `0x00` | `u32` | Core string index for name |
| `0x04` | `u32` | Name-is-identifier flag, zero or one |
| `0x08` | `u32` | Nullable `ProximitySensorInstance` parent |
| `0x0C` | `f32[16]` | Orientation |
| `0x4C` | `u32` | Skinned resource token |
| `0x50` | `u32` | `SkeletonAnimationResource` token |
| `0x54` | `u32` | Blend-shape animation token, always null |
| `0x58` | `u32[2]` | Zero |
| `0x60` | `u32` | Opaque footer |
| `0x64` | `u8[0x0C]` | Zero trailer |

Skinned resource targets are 303 regular, 233 LOD, and 120 Multi resources.
All default-animation tokens resolve, and 132 parent tokens resolve to
`ProximitySensorInstance`; the other 524 are null. No serialized
`SkinnedMeshInstance` or `MultiStaticMeshInstance` objects occur in the
612-Core non-mission-script corpus.

All 22,418 instance matrices begin at `+0x0C`, are finite, and have the
source row-major form:

```text
m00 m01 m02 0
m10 m11 m12 0
m20 m21 m22 0
tx  ty  tz  1
```

Apply a source point as `[x y z 1] * M`:

```python
world[j] = matrix[12 + j] + sum(
    local[i] * matrix[4*i + j] for i in range(3)
)
```

This convention transforms all 2,529 zone-linked static resource AABBs to
overlap their owning Zone. Preserve the complete matrix: 200 retail
instances have nonuniform scale, and no shear was observed.

Zone membership is authoritative. Only 1,705 of the 2,529 linked static
translations lie inside their Zone, but every transformed AABB overlaps it.
All 252 zone-linked simple-skinned instances are unparented; all 132
proximity-parented instances are not Zone members.

#### Authored occurrences and Blender instancing

`Prop` is an importer/UI category, not a serialized Killzone class. The two
proven authored render-placement classes are `StaticMeshInstance` and
`SimpleAnimatingSkinnedMeshInstance`. Gameplay objects such as
`PhysicsObjectInstance`, destructibles, pickups, and particle systems may also
be prop-like in an editor, but they retain their own schemas and must not be
coerced into either mesh-instance layout.

Keep definitions, wrappers, and occurrences distinct:

| Graph role | Serialized object | Transform source | Blender representation |
|---|---|---|---|
| Geometry definition | `Regular*MeshResource` | mesh-local or already-world vertex data | one reusable mesh datablock per Core directory object |
| Multi wrapper | `Multi*MeshResource` | none | resource collection/graph node; expand every selected child |
| LOD wrapper | `Lod*MeshResource` | none | resource collection/graph node; select child zero by default |
| Static occurrence | `StaticMeshInstance` | its `+0x0C` matrix | one authored occurrence object or owner Empty |
| Animated occurrence | `SimpleAnimatingSkinnedMeshInstance` | its `+0x0C` matrix plus preserved parent/animation references | one authored armature occurrence or owner Empty |
| Baked world occurrence | unreferenced static resource root | implicit identity | separate direct-world occurrence, not a synthesized instance record |

The ELF independently registers runtime allocation sizes of `0x20` for
`StaticMeshResource`, `0x50` for `StaticMeshResource::Instance`, `0xD0` for
`StaticMeshInstance`, `0x160` for `SkinnedMeshInstance`, `0x1A0` for
`SimpleAnimatingSkinnedMeshInstance`, `0x90` for
`MultiStaticMeshInstance`, and `0x140` for `CollisionMeshInstance`
(`FUN_00313268`, `FUN_00313790`, `FUN_003bc790`, `FUN_003af580`,
`FUN_003f0720`, and `FUN_003de0a8`). These are runtime/reflection sizes, not
serialized blob sizes. In particular, the reflected
`MultiStaticMeshInstance` class does not justify synthesizing one on import:
no object of that type occurs in the validated 612-Core corpus.

Identity rules for a Blender importer are:

- key a definition by its Core plus exact directory identity, not by display
  name or a bare object token;
- key an authored occurrence by its compact-directory identity and retain its
  referenced resource token separately;
- treat Core string indices and object tokens as Core-local;
- allow duplicate display names and repeated resource references;
- retain a marker distinguishing an explicit identity-matrix instance from an
  implicit direct-world occurrence;
- retain the original 16 serialized `f32` values even when a Blender matrix is
  also stored, so an unchanged export is not dependent on Blender transform
  decomposition.

Do not implement a shared byte-for-byte "instance header": the static layout
serializes name-is-identifier then name, while the simple-skinned layout
serializes name then name-is-identifier. Both place the parent token at
`+0x08` and the matrix at `+0x0C`, but that common tail does not make their
complete prefixes interchangeable.

Blender applies matrices to column vectors. If `M_src` is constructed from the
16 serialized values as the four source rows, the equivalent column-vector
matrix is:

```python
M_column = M_src.transposed()
```

If decoded vertices are converted into Blender coordinates by a basis matrix
`C`, apply the same basis to an occurrence exactly once:

```python
M_blender = C @ M_src.transposed() @ C.inverted()
```

This moves source translation from the last row into Blender's last column.
Passing the serialized rows directly to `object.matrix_world` is wrong. A
single scene-root axis conversion is also valid, but it must replace rather
than accompany per-mesh/per-instance basis conversion. Direct-world leaves
remain at identity after their already-world vertices receive the chosen axis
conversion.

For a Multi resource, create one authored owner for the placement and put the
selected child leaves below it or instance a resource collection; never apply
the placement once per wrapper level. Reuse leaf mesh datablocks, but create
distinct occurrence objects when the same definition is placed more than
once. A shared descendant reached through multiple legal wrapper paths is
multiple render occurrences sharing one definition, not a reason to collapse
the paths.

Zone membership is not transform parenting. Store it as a typed reference or
collection membership without changing `matrix_world`; an instance origin can
lie outside the Zone while its transformed bounds overlap it. The 132
simple-skinned parent references are different: they resolve to
`ProximitySensorInstance`, but static evidence has not yet established whether
the serialized child matrix is parent-local or already world-space. Preserve
both the raw matrix and parent token. A read-only preview may show the raw
matrix, but must not bake a Blender parent transform or claim a resolved world
placement until one runtime parent/child matrix trace settles composition
order.

### 4.16 Multi-resource wrappers

All Multi and LOD resources use expanded 32-byte directories but have zero
owned stream bytes. Referenced leaf resources retain bulk-data ownership.

`MultiStaticMeshResource` has:

```text
+0x00  u32  part_count                         # N
+0x04..+0x1B  opaque inherited words
+0x1C  u32  "MultiStaticMeshResource" string index
+0x20  f32  lightmap density = 1.0
+0x24  f32  maximum visible distance
+0x28  u32  part_token[N]
```

`MultiSkinnedMeshResource` adds a required Skeleton reference:

```text
+0x00  u32  part_count                         # N
+0x04..+0x1B  opaque inherited words
+0x1C  u32  "MultiSkinnedMeshResource" string index
+0x20  f32  lightmap density = 1.0
+0x24  f32  maximum visible distance
+0x28  u32  Skeleton token
+0x2C  u32  part_token[N]
```

There are 138 Multi-static objects with 584 declared slots and 240
Multi-skinned objects with 832 slots. Every part token is non-null. Static
children resolve to 447 regular and 137 LOD static resources. Skinned
children resolve to:

| Target type | Count |
|---|---:|
| `LodSkinnedMeshResource` | 467 |
| `RegularSkinnedMeshResource` | 329 |
| `LinkedStaticMeshResource` | 15 |
| `SkinnedBlendShapeResource` | 14 |
| `MultiSkinnedMeshResource` | 7 |

All 240 Skeleton tokens resolve. Both part arrays are entirely linear:

```python
tail = 0x28 if static else 0x2C
required_end = tail + 4 * N
require(N > 0 and required_end <= blob_size)
parts = read_u32_array(tail, N)
require(all_zero(blob[required_end:blob_size]))
```

Wrapper references form a graph, not a tree. Children can have up to 16
wrapper parents, and six objects reach wrapper depth three. The corrected
retail graph is acyclic, but use recursion-stack cycle detection and
memoization for untrusted or future data rather than assuming unique
ownership.

### 4.17 LOD-resource wrappers

`LodStaticMeshResource` has:

```text
+0x00  u32  mesh_count                         # N
+0x04  u32  lod_info_count                     # M == N
+0x08..+0x17  opaque inherited words
+0x18  u32  "LodStaticMeshResource" string index
+0x1C  u32  opaque flags
+0x20  u32  zero
+0x24  f32  lightmap density = 1.0
+0x28  f32  maximum visible distance
+0x2C  u32  mesh_token[N]
        f32  threshold[M]
```

All 2,817 static LOD objects store arrays linearly. Counts are two levels for
2,600 objects, three for 183, and four for 34. Children resolve to 5,865
regular and 20 Multi-static resources.

`LodSkinnedMeshResource` has:

```text
+0x00  u32  mesh_count                         # N
+0x04  u32  lod_info_count                     # M == N
+0x08..+0x17  opaque inherited words
+0x18  u32  "LodSkinnedMeshResource" string index
+0x1C  u32  opaque flags
+0x20  u32  zero
+0x24  f32  lightmap density = 1.0
+0x28  f32  maximum visible distance = FLT_MAX
+0x2C  u32  nullable Skeleton token
+0x30  u32  mesh_token[N]
        f32  threshold[M]
```

Of 371 skinned LOD objects, 310 have two levels, 60 have three, and one has
four. All static and skinned arrays are linear:

```python
mesh_end = mesh_start + 4 * N
required_end = mesh_end + 4 * M
require(required_end <= blob_size)
meshes = read_u32_array(mesh_start, N)
thresholds = read_f32_array(mesh_end, M)
require(all_zero(blob[required_end:blob_size]))
```

All LOD objects have `N == M`; threshold zero is exactly `0.0f`, and every
later threshold is finite, positive, and monotonic. The sole four-level
skinned sequence is `[0, 2, 4, 6]`. Static later boundaries range from
approximately `0.6` to `374.9235`; skinned boundaries range from 1 to 40.

The shared ELF selector starts at level one because threshold zero is the
baseline. With selection disabled it returns level zero. Otherwise it returns
`i - 1` for the first `metric <= threshold[i]`, or the final level. The
metric's physical meaning and units still require a runtime trace.

### 4.18 Skeleton outer object and packed header

All 416 `Skeleton` objects use expanded 32-byte directories, own no
CoreStream bytes, and parse with one exact layout. Together they contain
18,037 bones, 3,480 collision-hull slots, and 503 auxiliary attachment
records.

The ordinary expanded-directory object-origin rule is essential here:
`S + data_offset` is a section-level zero word, and object zero begins at
`S + data_offset + 4`. Offsets below use that corrected object start.

Let `H` be the hull count:

| Offset | Type | Meaning |
|---:|---|---|
| `+0x00` | `u32` | Hull count `H` |
| `+0x04` | `u32` | Core string index for Skeleton name |
| `+0x08` | `u8[12]` | Zero/default generic fields |
| `+0x14` | `u32[H]` | Collision-resource IDs in hull-slot order |
| `+0x14 + 4H` | `u32` | Core string index for bone-set text |
| next | `u32[4]` | Unknown custom words |
| next | bytes | Preserved alignment to object-relative `0x10` |
| aligned cursor | bytes | Packed Skeleton block |
| packed end | bytes | Zero tail to object-relative `0x10` |

```text
custom = 0x14 + 4 * H + 4
block  = align_up(custom + 16, 16)
```

The four custom words have several value sets but unknown meanings. Eight
objects have nonzero bytes in the alignment span after them, so preserve that
span rather than normalizing it as padding.

The packed block begins with:

| Block offset | Type | Meaning |
|---:|---|---|
| `+0x00` | `u16` | Serialized/stored block size |
| `+0x02` | `u16` | Packed runtime allocation size |
| `+0x04` | `u8` | Archive marker, serialized as one |
| `+0x05` | `u8` | Archive/transient field, serialized as zero |
| `+0x06` | `u8` | Auxiliary-record count `A` |
| `+0x07` | `u8` | Bone count `B` |
| `+0x08` | `u8[6]` | Category summary |
| `+0x0E` | `u8[2]` | Zero |
| `+0x10` | `u16` | Padded name-pool size `NP` |
| `+0x12` | `u16` | Used name-pool size `NU` |
| `+0x14` | `u32[11]` | Serialized-zero runtime pointer slots |

All 4,576 pointer words are zero, and `NP == align_up(NU, 4)`. If `C0`,
`C1`, and `C2` are the counts of bones in categories zero, one, and two, the
six summary bytes are exactly:

```text
[0, C0, C0, C1, C0 + C1, C2]
B = C0 + C1 + C2
```

### 4.19 Packed Skeleton arrays

Alignment in the serialized block is relative to `block`:

```text
cursor = 0x40

temporary_transform = cursor                 # B * 0x30
cursor += B * 0x30

cursor = align_up(cursor, 0x40)
six_float = cursor                            # B * 0x18
cursor += B * 0x18

cursor = align_up(cursor, 0x10)
auxiliary = cursor                            # A * 0x50
cursor += A * 0x50

cursor = align_up(cursor, 0x10)
bone_record = cursor                          # B * 0x20
cursor += B * 0x20

cursor = align_up(cursor, 4)
words_a = cursor                              # B * 4
cursor += B * 4

cursor = align_up(cursor, 4)
words_b = cursor                              # B * 4
cursor += B * 4

bytes_a = cursor                              # B
cursor += B
bytes_b = cursor                              # B
cursor += B
parent_index = cursor                         # B
cursor += B
bytes_d = cursor                              # B
cursor += B

name_pool = cursor                            # NP
cursor += NP

require cursor == stored_size
```

The ELF runtime allocation uses the same sequence but omits the temporary
`B * 0x30` array:

```text
cursor = 0x40
cursor = align_up(cursor, 0x40) + B * 0x18
cursor = align_up(cursor, 0x10) + A * 0x50
cursor = align_up(cursor, 0x10) + B * 0x20
cursor = align_up(cursor, 4)    + B * 4
cursor = align_up(cursor, 4)    + B * 4
cursor += B + B + B + B + NP

require cursor == runtime_size
```

Both size formulae, every alignment, and every source span match all 416
objects.

Each temporary transform is 12 finite floats:

```text
f32 linear_3x3[9]
f32 translation[3]
```

The loader interns complete 48-byte records in the global `SkeletonManager`,
receives a 20-bit global index, and patches each bone record:

```text
patched_0x18 =
    (serialized_0x18 & 0xC00003FF) |
    ((global_transform_index & 0xFFFFF) << 10)
```

It then frees the temporary array. The 18,037 references contain 2,499
byte-distinct matrices. Form a row-vector affine matrix from one serialized
record and transpose it to obtain the column-vector matrix used below:

```text
InverseBindGlobal[i] =
    transpose(row_affine(temporary_transform[i]))

RestGlobal[i] = inverse(InverseBindGlobal[i])
```

The software skinning path independently proves the interpretation by using:

```text
skin_matrix[i] = G_animated[i] * InverseBindGlobal[i]
```

for vertices stored in bind/model space. The hardware path instead uploads
`G_animated` directly because its decoded skinned vertices are already in
bone-local space:

```text
bind_vertex  = RestGlobal[i] * bone_local_vertex
posed_vertex = G_animated[i] * bone_local_vertex
```

The active hardware selector path therefore does not perform a missing
inverse-bind multiply: packet palette bytes resolve through transform records
to source-order Skeleton bones, and the selected matrix is already the
corresponding `G_animated[i]`. The transform-pool index remains absent from
that direct selector lookup, but the software path establishes the serialized
temporary transform's inverse-bind semantics.

Each retained six-float record has ordered finite triples:

```text
f32 candidate_min[3]
f32 candidate_max[3]
```

This is strongly consistent with a per-bone AABB. Preserve it as raw bounds
until its runtime consumer is traced.

### 4.20 Bone records, hierarchy, hulls, and attachments

Each `0x20`-byte bone record is:

| Offset | Type | Meaning |
|---:|---|---|
| `+0x00` | `f32[3]` | Unknown vector |
| `+0x0C` | `u32` | Name-pool offset for this bone |
| `+0x10` | `u32` | Name-pool offset for parent name; empty for root |
| `+0x14` | `u32` | Name-pool offset for collision-hull slot; empty if none |
| `+0x18`, bits 0-9 | bits | Hull slot, or `0x3FF` if no hull name |
| `+0x18`, bits 10-29 | bits | Transform-pool index patched by loader |
| `+0x18`, bits 30-31 | bits | Unknown preserved flags |
| `+0x1C`, low nibble | bits | Category zero, one, or two |
| `+0x1C`, remaining | bits | Unknown flags |

The separate `parent_index[B]` table proves the hierarchy:

- each Skeleton has exactly one root, always index zero;
- root parent is `0xFF`;
- every other parent is in range and less than the child;
- every chain reaches root without a cycle; and
- each parent-name string exactly equals the indexed parent's bone name.

Bone names are not guaranteed unique: six Skeletons have 21 duplicate-name
excess entries. Use indices, not a name-only dictionary, as the primary
hierarchy key. All name references must terminate within the first `NU`
bytes. The `NU..NP` padding is nonzero in 170 Skeletons and must be
preserved, not used to terminate a reference.

Hull slots are exact. Numbering nonempty bone hull names in source order
matches the low ten bits of `+0x18`; empty names use `0x3FF`. The 3,480 outer
IDs resolve in the same Core to:

| Target type | Count |
|---|---:|
| `CollisionResourceBox` | 2,974 |
| `CollisionResourcePolySoup` | 502 |
| `CollisionMultiResource` | 4 |

Repeated hull IDs are legal. The bone record supplies the slot label while
the outer array supplies its typed collision resource.

Each auxiliary `0x50`-byte record is:

```text
+0x00  f32 affine_matrix[16]
+0x40  u32 name_pool_offset
+0x44  u32 owning_bone_index_candidate
+0x48  u32 0xDEADBEEF
+0x4C  u32 0xDEADC0DE
```

All 503 records have finite row-vector affine matrices, valid names, both
sentinels, and an in-range `+0x44`. Names such as `muzzle_flash`,
`case_eject`, `Mount_Point`, and pulley/dismount points strongly identify
these as attachment/locator transforms, with `+0x44` as the owning-bone
candidate. They are distinct from collision hull slots.

The category tables also have exact structural relationships. Stable-sort
bone indices by `(category, source index)`, then:

```text
bytes_a[i] = ordinal of bone i within its category
bytes_b    = category-major bone-index order
bytes_d    = category-major bone-index order
words_b[k] = words_a[bytes_b[k]]
```

Preserve both identical byte permutations because the runtime retains
separate pointers. Category meanings and `words_a` semantics remain unknown.

The live matrix palette is constructed in two explicit stages. Animation pose
records arrive category-major. The runtime walks the category-order table and
scatters each local matrix to its original source-bone slot:

```text
k = 0
for c in 0..2:
    for j in 0..category_count[c] - 1:
        i = category_order[k]
        k += 1
        scratch[i] = make_local(category_pose[c][j], bone[i], c)
```

The category pose source is consumed sequentially with `j`; `i` is only the
original-order destination and the source-bone index used for defaults.
Indexing the category-major pose source with `i` is incorrect. `scratch` is
the original-order local-palette array denoted by `L` below.

The category-order table is the stable sort of source indices by
`(category, source_index)`; the serialized `bytes_b` and `bytes_d` arrays are
identical copies of that permutation. Local-matrix inputs are:

| Category | Quaternion | Translation | Scale |
|---:|---|---|---|
| 0 | `category_pose[0][j] + 0x00` | `bone[i] + 0x00` | unit |
| 1 | `category_pose[1][j] + 0x00` | `category_pose[1][j] + 0x10` | unit |
| 2 | `category_pose[2][j] + 0x00` | `category_pose[2][j] + 0x10` | `category_pose[2][j] + 0x20` |

The scatter is visible at EE `0x3AC468..0x3AC52C`. The hierarchy loop at
`0x3AC5E4..0x3AC67C` then walks the original-order parent array and overwrites
each nonroot slot using the resident VU0 matrix multiply:

```text
G[i] = L[i]                         if parent[i] == 0xFF
G[i] = G[parent[i]] * L[i]          otherwise
```

Matrices are four columns and use column-vector convention. The completed
`B * 0x40` scratch span is copied directly, without a gather or permutation,
to the allocation referenced by both `pose+0x04` and `draw+0x74`. Therefore:

```text
selected = G[transform_record.selector]
clip     = K * Object * selected * decoded_local_position

K_raw = M_90 * M_D0
K     = H * K_raw
H(x, y, z, w) = (x, -z, y, w)
```

`Object` is uploaded separately from `draw+0x10`; the camera-side `K` matrix
is also separate. The exact `M_90 * M_D0` order and axis conversion are
instruction-proven, while the semantic names "projection" and "view" for
those two state matrices remain strongly caller-supported rather than named
by the binary. The only missing static edge is the generic upload descriptor
that copies the completed camera cache to VU1 `TOP+0x1E4`; it does not affect
selector identity or palette space.

### 4.21 Bone sets and animation linkage

The outer bone-set string is empty for 245 Skeletons. Otherwise it contains:

```text
Set label="Bone_A;Bone_B;..."
```

Across 171 nonempty strings there are 2,609 set declarations and 43,690
member references. Exactly 43,687 match case-sensitive bone names. Three
authored missing-name references are valid retail warnings, not corruption.
Empty sets are legal.

`SkeletonAnimationBoneMap` uses a third typed-section directory form:
24-byte non-streaming entries with prefix `(1, 0)` and a section-level zero
word before the first aligned object. Each of 584 BoneMap objects is exactly:

```text
+0x00  u32 empty_string_index
+0x04  u32 zero
+0x08  u32 zero
+0x0C  u32 newline-separated track-name string index
```

The maps contain 39,845 track names. Names are unique within a map under
exact comparison, although two maps have case-insensitive collisions.

The corrected BoneMap token is at `SkeletonAnimationResource + 0x38`. All
17,586 tokens resolve uniquely in the same Core, and every animation's track
count equals its BoneMap line count. Packed animation data has two forms:

| Form | Objects | Origin | Owned-size rule |
|---|---:|---|---|
| Streamed | 16,863 | Object-owned CoreStream range | `align_up(8 + packed_bytes, 64)` |
| Inline | 723 | Serialized object `+0x40` | `align_up(8 + packed_bytes, 16)` |

Both begin:

```text
u32 track_count
u32 packed_byte_size
u8  packed_tracks[packed_byte_size]
u8  zero_padding[]
```

The ELF track-length routine walks all 1,589,420 records and ends exactly at
the declared packed size. A parser may keep track payloads opaque, but it
must reproduce that bounded walk rather than search for plausible headers.

#### Packed track records

The executable fixes the packed byte block into one pointer per track:

- `0x3B1868` allocates the pointer array and walks the packed block.
- `0x3BEA88` computes the exact end of one record.
- `0x3BE260` selects the two sample indices and interpolation fraction.
- `0x3BE4F0`, `0x3BE638`, and `0x3BE778` sample rotation, translation, and
  scale respectively.

Each record starts with two control bytes:

```text
byte 0:
  bits 0..3  T: time codec
  bits 4..6  P: translation codec
  bit  7     unassigned; ignored by the observed size and sample dispatches

byte 1:
  bits 0..3  R: rotation codec
  bits 4..5  unassigned; ignored by the observed size and sample dispatches
  bits 6..7  S: scale codec
```

A strict decoder accepts `T = 0..8`, `R = 0..9`, `P = 0..6`, and
`S = 0..3`. The executable's zero-payload fallbacks for `R = 10..15` and
`P = 7` may be retained in an explicit runtime-parity mode, but are not
evidence that those values are valid serialized codecs. `T = 9..15` must
always be rejected because the time sampler reaches its default path without
initializing the bracket outputs.

Bound the control and count reads against the declared packed block:

```text
remaining = packed_block_end - record
require remaining >= 2

if T == 0:
    N = 1
    cursor = record + 2
else:
    require remaining >= 3
    first = record[2]
    if (first & 0x80) == 0:
        N = first
        cursor = record + 3
    else:
        require remaining >= 4
        N = (first & 0x7F) | (record[3] << 7)
        cursor = record + 4

require 1 <= N <= 0x7FFF
```

Payloads always follow in this order:

```text
time, rotation, translation, scale
```

The boundary walker advances through those four payloads using the sizes
below. Records are tightly concatenated: there is no per-record alignment or
sentinel, and outer inline/stream padding is not part of a record. Before each
descriptor, metadata, fixed sample, or bit-window read, compute its size with
checked arithmetic and require the resulting cursor not to exceed
`packed_block_end`. After `track_count` records, require the cursor to equal
`packed_tracks + packed_byte_size`. This is enough to split a packed animation
block into records without understanding every quaternion reconstruction
formula.

##### Time codecs

| `T` | Payload bytes | Sampling behavior |
|---:|---:|---|
| 0 | `0` | One implicit sample |
| 1 | `4 + 2*N` | `f32` duration followed by `N` normalized 16-bit key times |
| 2 | `N` | `N` explicit byte key times at 24 Hz |
| 3 | `0` | Implicit uniform keys at 6 Hz |
| 4 | `0` | Implicit uniform keys at 12 Hz |
| 5 | `0` | Implicit uniform keys at 24 Hz |
| 6 | `L = cursor[0]` | Bit-packed variable deltas at a 12 Hz base rate |
| 7 | `L = cursor[0]` | Bit-packed variable deltas at a 24 Hz base rate |
| 8 | `L = cursor[0]` | Bit-packed variable deltas at a 6 Hz base rate |

The time jump table at guest `0x556730` routes `T == 6` to `0x3BE418`
(12 Hz), `T == 7` to `0x3BE448` (24 Hz), and `T == 8` to `0x3BE3E8`
(6 Hz). `T == 1..8` are implemented; `T == 9..15` reach `0x3BE470`
without initialized bracket outputs.

For `T == 1`, require checked size `4 + 2*N` before reading the duration or
keys. For `T == 2`, require `N` bytes. For `T == 6..8`, first require one
byte, then require `1 <= L <= remaining_time_bytes`; `L` includes its own
length byte. Bound every one of the `N - 1` delta decodes to the
`8*(L - 1)` available bitstream bits.

For `T == 1`, the duration is a native little-endian float, but each
normalized key time is assembled as a big-endian `u16`. A key value `k`
represents `duration * k / 65535`. The sampler clamps the requested time to
the duration and returns the bracketing indices plus their interpolation
fraction.

For `T == 2`, a key byte `k` represents `k / 24` seconds. The input is
clamped to the representable `0..255/24`-second range.

For `T == 6..8`, the first byte is the total timing-payload length, including
that length byte. The bitstream starts at the following byte and is read
least-significant bit first:

```text
0      -> advance 1 base-rate tick
1 vvv  -> advance 2 + vvv base-rate ticks
```

The latter form encodes deltas of two through nine ticks. One time lookup is
performed per track, and its lower index, upper index, and fraction are
reused by all channels in that track.

##### Rotation codecs

`R = byte1 & 0x0F`:

| `R` | Payload bytes | Observed representation |
|---:|---:|---|
| 0 | `0` | Default identity quaternion |
| 1 | `8` | One four-component packed-`u16` quaternion |
| 2 | `8*N` | `N` packed-`u16` quaternions |
| 3 | `0` | Default identity quaternion |
| 4 | `4*N` | `N` packed 10:10:10 quaternion samples |
| 5 | `4` | One packed 10:10:10 quaternion sample |
| 6 | `0` | Default identity quaternion |
| 7 | adaptive | Adaptive reconstructed-component quaternion |
| 8 | adaptive | Adaptive three-angle quaternion |
| 9 | adaptive | Fixed-axis, adaptive-angle quaternion |

The rotation sampler and size tables at guest `0x556750` and `0x5567A0`
dispatch `R == 1..9`. `R == 0` and `R == 10..15` return identity and consume
zero payload at runtime. Only `R == 0..9` belong to the strict documented
domain; the higher values are executable fallbacks, not proven serialized
aliases. For every fixed or adaptive codec, require the computed payload
within the packed block before sampling it. In particular, require two
descriptor bytes before reading `h0`/`h1` for `R == 7/8`, one before `h0` for
`R == 9`, and bounds-check every conditional metadata word and bit payload.

The constant `R == 1` path expands four little-endian `u16` components using:

```text
component = -1.0 + packed * bit_cast<f32>(0x38000101)
```

The `R == 2` keyed path consumes the same eight-byte samples. VU0
microprogram `0x4F0` expands both endpoints with the formula above, then
falls through into the shared interpolation program at `0x520`.

For `R == 4` and `R == 5`, bits `0..9`, `10..19`, and `20..29` are passed to
VU0 microprogram `0xD18`, which reconstructs the four-component quaternion.
Bits `30..31` are masked off and do not affect the decoder. Let:

```text
C10 = bit_cast<f32>(0x3B004020)
p = float32(vec3(field0, field1, field2) * C10 - 1)
s = dot(p, p)

if s < 1:
    q = (p.x, p.y, p.z, sqrt(1 - s))
else:
    q = (p / sqrt(s), 0)
```

The comparison is strict, so `s == 1` takes the normalization path.
`R == 4` decodes both selected endpoints this way and then uses VU0 `0x520`;
`R == 5` decodes one constant sample.

For `R == 7` and `R == 8`, let `h0 = cursor[0]` and `h1 = cursor[1]`:

```text
sample_bits = (h0 >> 4) + (h1 & 0x0F) + (h1 >> 4) + (h0 & 1)
size = 2 + ceil(sample_bits * N / 8)

if h0 & 0x02:
    size += 2
    if h1 & 0xF0:
        size += 2

if h0 & 0x04:
    size += 2
    if h1 & 0x0F:
        size += 2

if h0 & 0x08:
    size += 2
    if h0 & 0xF0:
        size += 2
```

Their shared two-byte descriptor means:

```text
wx = h1 >> 4
wy = h1 & 0x0F
wz = h0 >> 4

x_present = h0 & 0x02
y_present = h0 & 0x04
z_present = h0 & 0x08
```

Metadata follows the descriptor in x/y/z order. For each component:

```text
if present:
    lower = s16be(metadata) * bit_cast<f32>(0x38000100)
    metadata += 2

    if width != 0:
        upper = s16be(metadata) * bit_cast<f32>(0x38000100)
        metadata += 2
    else:
        upper = lower
else:
    lower = upper = 0
```

The sample bitstream begins immediately after that metadata. Samples are
consecutive and least-significant bit first. Within a sample, z occupies the
low `wz` bits, y the next `wy`, and x the next `wx`. A nonzero-width field is
dequantized as:

```text
aligned = code << (16 - width)
fraction = aligned * bit_cast<f32>(0x37800080)  # aligned / 65535
value = lower + (upper - lower) * fraction
```

`R == 7` appends the optional `h0 & 1` bit above the x field. It reconstructs
the fourth quaternion component as:

```text
w = sqrt(max(1 - x*x - y*y - z*z, 0))
if (h0 & 1) and sample_sign_bit:
    w = -w
q = (x, y, z, w)
```

Retail `R == 7` results are finite but are not guaranteed to be unit
quaternions. Preserve the decoded components and the retail interpolation
result without inserting a host normalization step.

`R == 8` instead treats the three dequantized values as normalized angles,
multiplies each by `pi`, and converts them to a quaternion. If the resulting
angles are `x`, `y`, and `z`, the composition is
`qz(z) * qy(y) * qx(x)`:

```text
sx, cx = sin(x/2), cos(x/2)
sy, cy = sin(y/2), cos(y/2)
sz, cz = sin(z/2), cos(z/2)

q.x = sx*cy*cz - cx*sy*sz
q.y = cx*sy*cz + sx*cy*sz
q.z = cx*cy*sz - sx*sy*cz
q.w = cx*cy*cz + sx*sy*sz
```

The shared size routine at `0x3DAD38..0x3DAD54` includes `h0 & 1`, but both
`R == 8` sample decoders at `0x3DAE28` and `0x3DB058` omit that bit from their
sample stride and never read it. A decoder that remains synchronized with
both executable paths must therefore require `(h0 & 1) == 0` for `R == 8`
and reject a set bit rather than choosing one of the disagreeing strides.
All 6,223 retail `R == 8` records clear the bit, confirming the serializer
invariant for the validated corpus. `R == 7` and `R == 8` both pass their
decoded endpoint quaternions to VU0 `0x520`.

For `R == 9`, with `h0 = cursor[0]`:

```text
size = ceil((h0 & 0x0F) * N / 8) + ((h0 & 0x10) ? 6 : 7)
```

This is a fixed-axis track whose low descriptor nibble is the per-sample
angle width. Descriptor bit 4 selects one of two axis encodings:

```text
if (h0 & 0x10) == 0:
    axis_code = u16be(cursor + 1)
    angle_metadata = cursor + 3
    sample_bits = cursor + 7
else:
    axis_code = cursor[1]
    angle_metadata = cursor + 2
    sample_bits = cursor + 6
```

The 16-bit axis code contains two folded magnitudes and three sign bits:

```text
mx = (axis_code >> 7) & 0x3F
my = axis_code & 0x7F

if mx + my >= 127:
    mx = 127 - mx
    my = 127 - my

mz = 126 - (mx + my)

if axis_code & 0x8000: mx = -mx
if axis_code & 0x4000: my = -my
if axis_code & 0x2000: mz = -mz

axis = normalize(vec3(mx, my, mz))
```

The eight-bit form uses this exact lookup table:

```text
L = {
    -1.0,
    bit_cast<f32>(0xBF3504F7),
    -0.5,
     0.0,
     0.5,
    bit_cast<f32>(0x3F3504F7),
     1.0,
     1.0
}

axis.x = L[axis_code & 7]
axis.y = L[(axis_code >> 3) & 7]
axis.z = sqrt(max(1 - axis.x*axis.x - axis.y*axis.y, 0))
if axis_code & 0x40:
    axis.z = -axis.z
```

Bit 7 is ignored, and this branch does not normalize the result after the
lookup; valid codes are expected to select a unit-sphere point.

The four angle-metadata bytes are two little-endian values:

```text
Cangle = bit_cast<f32>(0x38C910A4)  # float32(2*pi/65535)
extent = u16le(angle_metadata + 0) * Cangle
lower  = u16le(angle_metadata + 2) * Cangle - pi
```

For `width = h0 & 0x0F`, sample `i` is:

```text
code = read_bits_lsb(sample_bits, i * width, width)
aligned = code << (16 - width)
angle[i] = lower + extent * aligned * bit_cast<f32>(0x37800080)
```

If two distinct keys bracket the requested time, `0x3BCE30` interpolates the
angles. Its exact wrap behavior is:

```text
delta = angle1 - angle0

if delta > pi:
    angle = angle0 + (delta - 2*pi) * t
    if angle < -pi:
        angle += 2*pi
elif delta < -pi:
    angle = angle0 + (delta + 2*pi) * t
    if angle > pi:
        angle = 2*pi - angle
else:
    angle = angle0 + delta * t
```

The asymmetric `2*pi - angle` operation in the second branch is the retail
instruction sequence, not a transcription of a conventional wrap helper.
The final quaternion is formed directly on the CPU:

```text
q.xyz = axis * sin(angle / 2)
q.w   = cos(angle / 2)
```

###### VU0 quaternion interpolation

VU0 entry `0x520` is a shortest-path approximate SLERP, not exact SLERP or
normalized linear interpolation. It receives `q0`, `q1`, and `t` and first
computes:

```text
c = (q0.x*q1.x + q0.y*q1.y) + (q0.z*q1.z + q0.w*q1.w)
d = abs(c)
sign = -1 if c < 0 else 1
K = bit_cast<f32>(0x3F7FFF58)  # 0.9999899864196777
```

For `d >= K`, it uses an unnormalized linear fallback:

```text
q = (1 - t) * q0 + (sign * t) * q1
```

For `d < K`, it approximates `theta = acos(d)` as:

```text
P(d) = a3 + d*(a2 + d*(a0 + a1*d))
theta = sqrt(1 - d) * P(d)

a0 = bit_cast<f32>(0x3D981627)
a1 = bit_cast<f32>(0xBC996E30)
a2 = bit_cast<f32>(0xBE593484)
a3 = bit_cast<f32>(0x3FC90DA4)
```

Its sine and reciprocal-sine approximations are:

```text
S(u) = 1 + u*(s1 + u*(s2 + u*(s3 + u*(s4 + s5*u))))
sin_approx(x) = x * S(x*x)

s1 = -0.1666666716337204
s2 =  0.008333331905305386
s3 = -0.00019840900495182723
s4 =  0.0000027526000394573202
s5 = -0.000000023900000201138027

csc_approx(x) =
    1/x + x*(c0 + x*x*(c1 + x*x*(c2 + x*x*(c3 + c4*x*x))))

c0 = 0.1666666716337204
c1 = 0.01944444514811039
c2 = 0.0020502645056694746
c3 = 0.00020998677064199
c4 = 0.00002133604539267253
```

The final blend is:

```text
inverse_sine = csc_approx(theta)
w0 = sin_approx((1 - t) * theta) * inverse_sine
w1 = sign * sin_approx(t * theta) * inverse_sine
q = w0*q0 + w1*q1
```

The operation grouping above matches the VU program. Final low bits can
differ between VU implementations because of FMAC-pipeline rounding.

##### Translation codecs

`P = (byte0 >> 4) & 7`:

| `P` | Payload bytes | Observed representation |
|---:|---:|---|
| 0 | `0` | Default zero translation |
| 1 | `8` | One mantissa-truncated float vector |
| 2 | `8*N` | `N` mantissa-truncated float vectors |
| 3 | `4*N` | `N` fixed-range 10:10:10 vectors |
| 4 | `4` | One fixed-range 10:10:10 vector |
| 5 | adaptive | Adaptive per-axis bit-packed vectors |
| 6 | `2` | One sparse single-lane vector |
| 7 | `0` | Runtime zero-translation fallback for xyz |

The translation sampler and size tables at guest `0x556780` and `0x5567D0`
dispatch `P == 1..6`. `P == 0` and `P == 7` both consume zero payload and
return default zero xyz at runtime. Strict format validation accepts
`P == 0..6`; runtime-parity decoding may preserve the `P == 7` fallback but
must not present it as a proven serialized codec. Guard all fixed payload
sizes, checked `N` multiplications, adaptive metadata, and sample bits before
decoding.

The eight-byte vector used by `P == 1`, `P == 2`, and the scale channel is a
64-bit concatenation of truncated float32 bit patterns, not three
half-floats:

```text
x_bits = u32(packed >> 43) << 11
y_bits = u32(packed >> 22) << 11
z_bits = u32(packed)       << 10
```

For `P == 3` and `P == 4`, a little-endian `u32` stores three fields; bits
`30..31` are ignored:

```text
x = ((packed       & 0x3FF) - 512) / 1024
y = (((packed>>10) & 0x3FF) - 512) / 1024
z = (((packed>>20) & 0x3FF) - 512) / 1024
```

`P == 5` starts with a one-byte descriptor. It defines lower and upper
endpoints plus a bit width for each axis. If descriptor bit 0 is set, the
metadata is:

```text
+0      descriptor
+1      packed-float lower vector, 8 bytes
+9      packed-float upper vector, 8 bytes
+17     x bit width
+18     y bit width
+19     z bit width
+20     sample bitstream
```

Otherwise, descriptor bits `1..2`, `3..4`, and `5..6` select one metadata
mode for x, y, and z respectively. Axis metadata is concatenated in x/y/z
order:

| Mode | Bytes | Lower, upper, and width |
|---:|---:|---|
| 0 | `0` | `lower = upper = 0`, width `0` |
| 1 | `4` | LE `u32`: width `raw & 0x3F`; lower `((raw >> 6) & 0x1FFF) / 4096 - 1`; upper `(raw >> 19) / 4096 - 1` |
| 2 | `5` | LE `u32`: lower `(raw & 0xFFFF) / 4096 - 8`; upper `(raw >> 16) / 4096 - 8`; fifth byte is width |
| 3 | `2` | BE `u16`: `lower = upper = raw / 4096 - 8`, width `0` |

If the widths are `wx`, `wy`, and `wz`, each sample occupies
`wx + wy + wz` bits. Samples are consecutive and least-significant bit first;
within one sample, z occupies the low `wz` bits, y the next `wy`, and x the
next `wx`. For an axis code `q` of width `w`, dequantization is:

```text
value = lower + (upper - lower) * q / 2^w
```

The full `P == 5` payload size is:

```text
1 + metadata_bytes + ceil((wx + wy + wz) * N / 8)
```

Although the metadata fields can encode larger values, the runtime mantissa
construction at `0x3DB9FC..0x3DBA5C` requires each of `wx`, `wy`, and `wz`
to be at most 23. Let `W = wx + wy + wz`. The runtime reader at
`0x3DA4A4..0x3DA4D4` extracts each sample from one 64-bit window, so require:

```text
0 <= wx, wy, wz <= 23
for every i in 0..N-1:
    ((i * W) & 7) + W <= 64
```

The validated retail corpus is narrower than those runtime bounds: every
`P == 5` axis width is at most 16 and the largest observed bit-window width is
54. A strict retail-profile validator may enforce those observed maxima;
runtime-parity mode may admit the wider executable domain while retaining the
64-bit-window check.

Compute the metadata length, `W*N`, and `ceil(W*N/8)` with checked arithmetic,
and require the complete logical payload inside the packed block. A safe
implementation should extract from the bounded byte sequence, or a
zero-extended local window, rather than reproduce an unchecked native
64-bit load. Dequantization remains `q / 2^w`, not `q / (2^w - 1)`.

For `P == 6`, require two payload bytes and assemble them big-endian as at
`0x3BE71C..0x3BE734`. Bits `0..1` select one of the four float lanes in the
16-byte translation slot and bits `2..15` form a signed 14-bit value:

```text
packed = u16be(payload)
lane  = packed & 3
value = sign_extend_16(packed) >> 2
output[lane] = value / 1024
```

Only the xyz lanes are initialized to zero first. Lane 3 writes the fourth
float at slot `+0x0C`, as shown by `0x3BE098..0x3BE0EC`; it is neither an
alias for Z nor an out-of-bounds write. The `P == 0` and `P == 7` fallbacks
guarantee zero xyz only and must not be documented as initializing that
fourth lane. No validated retail `P == 6` record selects lane 3. Strict
retail-profile validation may reject it; runtime-parity decoding should
preserve the fourth lane without presenting it as an xyz translation.
Keyed translations are interpolated component by component in CPU code.

##### Scale codecs

`S = byte1 >> 6`:

| `S` | Payload bytes | Observed representation |
|---:|---:|---|
| 0 | `0` | Default unit scale |
| 1 | `8` | One mantissa-truncated float vector |
| 2 | `8*N` | `N` mantissa-truncated float vectors |
| 3 | `0` | Default unit scale |

Scale reuses the eight-byte vector decoder above. Keyed scale is interpolated
component by component.

#### Pose sampling and track scatter

Three sampling entry points match the Skeleton's category-major pose layout
independently of the later matrix builder:

| Entry point | Channels sampled | Output stride |
|---|---|---:|
| `0x3BE838` | rotation | `0x10` |
| `0x3BE8B0` | rotation, translation | `0x20` |
| `0x3BE948` | rotation, translation, scale | `0x30` |

Their category loops at `0x4E60F8`, `0x4E6190`, and `0x4E6228` consume
two-byte mapping entries:

```text
u8 output_slot
u8 track_index
```

`output_slot == 0xFF` terminates a category list. `track_index` selects the
fixed-up packed-track pointer, while `output_slot` selects the destination
record within that category. The resulting `0x10`, `0x20`, and `0x30` arrays
are the same category-major pose arrays scattered back into source-bone order
at `0x3AC468..0x3AC52C`.

Runtime mapping compares BoneMap track names with Skeleton bone names and
emits per-track bone mappings with `0xFF` sentinels; track order is BoneMap
order, not Skeleton order. For each BoneMap track, the loop at
`0x3B1500..0x3B157C` scans Skeleton records forward and compares through
`0x173C18 -> 0x2B9090`, which is `strcasecmp`. The first case-insensitive
Skeleton-name match wins. If that first duplicate has an incompatible
category, the runtime does not continue to a later same-name bone. Shared
animation-bank Cores often contain no Skeleton, so never bind by local object
order.

### 4.22 Retail animation playback contract

This section joins the serialized corpus, runtime playback, Skeleton palette,
inverse-bind, and Blender requirements into one importer contract. It applies
to the validated variant-5 retail corpus; separately encoded variant-1
frontend objects remain outside these totals and require their own validation.

#### Corpus, identity, and Skeleton linkage

The validated animation corpus spans 398 variant-5 Cores:

| Measurement | Value |
|---|---:|
| `SkeletonAnimationResource` clips | 17,586 |
| Packed tracks | 1,589,420 |
| Packed track bytes | 14,014,155 |
| Streamed / inline clips | 16,863 / 723 |
| Tracks per clip | 1 through 120 |

Every record boundary is valid, every packed walk ends at its declared byte
count, all outer alignment bytes are zero, and every decoded numeric value is
finite. There are 582 variant-5 BoneMaps and 584 when the two separately
scoped variant-1 maps are included.

For every variant-5 clip:

1. `SkeletonAnimationResource +0x38` resolves uniquely to a same-Core
   `SkeletonAnimationBoneMap`;
2. packed track count equals the BoneMap line count; and
3. the clip itself contains no Skeleton or cross-Core path token.

BoneMap names establish compatibility, not Skeleton identity. The 656
`SimpleAnimatingSkinnedMeshInstance` graphs provide explicit same-Core
resource, default-animation, and Skeleton edges and are the strongest
serialized Skeleton-selection evidence. Shared banks and clips without such
an owning graph require a caller-selected Skeleton; never choose one merely
because it is co-resident or name-compatible.

Tokens, strings, and display names are not global identities. Persist:

```text
CoreIdentity   = validated Core path and digest
ObjectIdentity = CoreIdentity + typed-section occurrence + directory ordinal
BoneIdentity   = Skeleton ObjectIdentity + source-order bone index
TrackIdentity  = clip ObjectIdentity + BoneMap ObjectIdentity + line index
```

Keep the bare Core-local token as a reference, not as the primary key. Legal
same-Core duplicate animation-token entries must remain distinguishable by
directory identity even when their bytes are equal.

#### Serialized clip fields

The variant-5 property block is:

| Offset | Type | Meaning |
|---:|---|---|
| `+0x00` | `u32` | Generic flags: `0` streamed, `0x100` inline |
| `+0x04` | `u32` | Core string index for the resource type, not a clip label |
| `+0x08` | `u32` | Parent token, always zero |
| `+0x0C` | `u32` | Linear-storage candidate, always zero |
| `+0x10` | `u32` | Playback mode |
| `+0x14` | `u32` | Nullable `AnimationEventResource` token |
| `+0x18` | `f32` | Translation threshold, always `0.001` |
| `+0x1C` | `f32` | Rotation-axis threshold, always `0.01` |
| `+0x20` | `f32` | Rotation-angle threshold, always `0.01` |
| `+0x24` | `f32` | Authoritative duration `D`, in seconds |
| `+0x28` | `bool` | Serialized Compensate Root value |
| `+0x2C` | `f32[3]` | Total Distance |
| `+0x38` | `u32` | BoneMap token |
| `+0x3C` | `u32` | Zero |
| `+0x40` | bytes | Inline packed header when flags select inline storage |

All 125 nonzero event tokens resolve uniquely in the same Core. The
serialized field name `Actions` refers to runtime animation events and must
not be confused with a Blender Action.

#### Runtime clock, modes, duration, root trajectory, and events

Let:

```text
D     = serialized clip duration
r     = playback rate
Deff  = D / abs(r)
u     = instance timeline in real seconds
tau   = u * abs(r)       # clip-domain sampling time
```

Changing rate preserves phase. Nonnegative playback starts at zero; negative
playback starts at `Deff`.

The corpus and executable establish:

| Mode | Corpus count | Behavior |
|---:|---:|---|
| 1 | 826 | Repeat with proper modulo into `[0, Deff)` |
| 2 | 16,760 | One-shot; crossing either end finishes and holds the boundary |

Mode 0 performs a stop/reset on crossing but is not serialized in the
validated corpus. Mode 2 uses strict crossing comparisons, so landing exactly
at zero or `Deff` remains active for that update; the next crossing enters the
finished state.

`D`, not maximum track key time, controls repeat, finish, root phase, and
event segmentation. The relationship is:

| Duration relationship | Clips |
|---|---:|
| `D == maxKeyTime` | 9,245 |
| `D > maxKeyTime` | 8,082 |
| `D < maxKeyTime` | 259 |

Thus 8,341 clips differ from their maximum key time. Each track clamps
independently to its own first or last sample. Preserve serialized keys after
`D`; they are unreachable during ordinary playback but remain part of the
asset. Do not replace `D` with a computed key range.

When the runtime instance compensation control is false and source bone zero
has writable translation, trajectory reconstruction occurs before hierarchy
accumulation:

```text
phase = tau / D
root_translation += TotalDistance * phase
```

Q-only roots skip the addition. A held one-shot endpoint uses phase one; a
repeating clip resets this contribution at wrap. The direct bridge from the
serialized `+0x28` Boolean to every raw-instance control path remains
unresolved, so preserve both the field and the effective import policy rather
than silently assuming they are identical in every graph.

Simple events are dispatched over duration-sized traversed segments. Normal
updates test a half-open interval; a finishing crossing includes the terminal
endpoint:

```text
normal:    low <= event_time <  high
finishing: low <= event_time <= high
```

Repeating updates are segmented at each wrap, including updates that cross
multiple cycles. Complex predicate-bearing event entries and their rate
interaction remain outside the proven simple-event contract.

#### Strict packed-codec validation

The retail selector histograms are:

```text
T: 0=1420131 1=354 2=3759 3=82912 4=56576 5=7541
   6=7472 7=7163 8=3512
R: 0=564035 1=1876 2=611 3=0 4=6430 5=882140
   6=0 7=102342 8=6223 9=25763
P: 0=1267376 1=17183 2=669 3=1145 4=233374 5=24846
   6=44827
S: 0=1588421 1=202 2=3 3=794
```

A strict decoder therefore accepts only `T=0..8`, `R=0..9`, `P=0..6`, and
`S=0..3`; executable fallbacks are runtime-parity behavior, not additional
serialized codecs. Additional retail invariants are:

- variable-time `T=6/7/8` use 12/24/6 Hz respectively;
- all 6,223 `R=8` descriptors clear bit zero;
- `R=7` samples may be non-unit and must not be normalized by the host;
- observed `R=9` widths are 1 through 12;
- `P=5` widths are at most 16 and the largest observed bit window is 54;
- `P=6` is big-endian and never selects the W lane; and
- every payload, reconstructed endpoint, key time, and sampled component is
  finite.

Reject a whole clip atomically on an unknown selector, malformed count,
out-of-bounds metadata/bit window, nonfinite result, nonzero padding, or final
cursor mismatch. Report the Core/object identity, track index, record offset,
and `(T,R,P,S)` tuple; retain the raw bytes instead of substituting an
identity track.

#### Direct animated locals and hierarchy

Pose buffers reset to:

```text
Q = (0, 0, 0, 1)
T = (0, 0, 0)
S = (1, 1, 1)
```

Unmatched BoneMap names are skipped and leave those defaults. For matched
tracks, Q/T/S are direct animated parent-local components, not deltas
multiplied onto rest. Quaternion memory order is `(x,y,z,w)`. The exact
right-hand-rule matrix is:

```text
Q(q) =
  [1-2(y*y+z*z)  2(x*y-z*w)    2(x*z+y*w)]
  [2(x*y+z*w)    1-2(x*x+z*z)  2(y*z-x*w)]
  [2(x*z-y*w)    2(y*z+x*w)    1-2(x*x+y*y)]
```

Local matrices are:

```text
category 0: L[i] = [Q(q[i]),                    bone[i].static_translation]
category 1: L[i] = [Q(q[i]),                    sampled_translation[i]]
category 2: L[i] = [Q(q[i]) * diag(scale[i]),   sampled_translation[i]]
```

Category zero ignores any serialized translation payload. Category two
scales the three rotation columns independently. Scatter category-major pose
records to source-bone order before hierarchy accumulation:

```text
G[i] = L[i]                         if parent[i] == 0xFF
G[i] = G[parent[i]] * L[i]          otherwise
```

Do not substitute `local * parent`, treat frame zero as bind pose, or insert a
hidden `rest * animation_delta` multiplication.

#### Palette, inverse bind, and deformation

The hardware selector chain is exact:

```text
packet palette byte
  -> object-local transform record
  -> transform_record.selector
  -> source-order Skeleton bone
  -> G_animated[source_bone]
```

There is no mesh-local bone remap and each decoded vertex has one implicit
weight of `1.0`. Let:

```text
I[i] = InverseBindGlobal[i]
R[i] = RestGlobal[i] = inverse(I[i])
```

Then:

```text
x_bind = R[i] * x_bone_local

hardware:
    x_pose = G[i] * x_bone_local

software:
    x_pose = (G[i] * I[i]) * x_bind
```

These are equivalent. Uploading `G` directly in the hardware path is not
evidence that inverse bind is absent; it follows from the bone-local vertex
representation.

#### Basis and Blender conversion

Geometry, quaternion, determinant, and camera evidence strongly supports a
right-handed model space with `+X` right and `+Z` up. `+Y` forward and
approximately one native unit per metre are strong inferences rather than
explicit serialized declarations. The camera conversion
`H(x,y,z)=(x,-z,y)` is clip/camera-side and must not be reused as a model
import basis.

Use exactly one homogeneous source-to-Blender basis:

```text
C = diag(s*A, 1)
```

The safest data-faithful choice is `A=I`, `s=1`. An optional proper
`RotZ(pi)` display basis maps inferred native `+Y` forward to Blender `-Y`
without reflection or winding reversal. Convert every model-space matrix by:

```text
M_blender = C * M_native * inverse(C)
```

and do not combine this with a second scene-root conversion.

For source bone `i`, let:

```text
R[i] = RestGlobal native matrix
G[i] = animated global native matrix
E[i] = actual Blender edit-bone rest matrix in armature space
```

Store the rigid bind vertex as:

```text
x_bind_blender = C * R[i] * x_bone_local
```

The Blender armature-space target pose matrix is:

```text
P[i] = C * G[i] * inverse(R[i]) * inverse(C) * E[i]
```

which guarantees:

```text
P[i] * inverse(E[i]) * x_bind_blender
    = C * G[i] * x_bone_local
```

Convert target global poses to Blender `matrix_basis` with:

```text
D[i] = inverse(E[i]) * P[i]                              # root
D[i] = inverse(E[i]) * E[parent] * inverse(P[parent])
       * P[i]                                            # child
```

If edit-bone rests exactly equal converted native rests, this reduces after
one basis conversion to:

```text
matrix_basis[i] = inverse(rest_local[i]) * L_animated[i]
```

Blender quaternion constructors and channels require `(w,x,y,z)`, while
source data remains `(x,y,z,w)`. When representable matrices are decomposed,
choose each quaternion hemisphere so consecutive baked keys have nonnegative
dot product. Do not normalize native `R=7` or retail interpolation results
before constructing the native local matrix.

Blender edit bones cannot represent arbitrary shear. Recompose every
decomposed `D[i]` and require a declared residual tolerance. If it fails,
reject ordinary Action generation or use a full-matrix proxy/custom
deformation path; never silently discard shear.

#### Blender Action and playback policy

For Blender 5.2 and later slotted Actions:

1. Keep one immutable source clip per serialized clip identity.
2. Generate one binding Action per
   `(clip, Skeleton, basis, rest fingerprint, bake rate, root policy)`.
3. Use one `OBJECT`-target slot for the Armature object; pose-bone data paths
   must use the actual Blender names reached from source bone indices.
4. Preserve duration, mode, event identity, BoneMap identity, basis, root
   policy, and decoder status as Action metadata.
5. Map seconds to frames as:

   ```text
   frame = action_start + seconds * scene_fps / scene_fps_base
   ```

   Twenty-four fps is a convenient default, not a universal clip property.
6. In native-evaluator mode, sample the packed tracks at the requested
   subframe and apply the matrix contract above.
7. In baked mode, sample every declared output frame plus the exact one-shot
   endpoint. Linear host interpolation is guaranteed only at baked samples;
   sparse Blender quaternion curves do not reproduce VU0 `0x520`.
8. Implement mode 1 as `[0,D)` repetition and mode 2 as a one-shot held at
   `D`. Do not infer loops from endpoint similarity.
9. Keep sampled/reconstructed root motion on source bone zero by default.
   Extraction to object motion is optional and must remove the same transform
   from the root so it is not applied twice.
10. Preserve events as duration-relative metadata or explicit dispatch
    records; do not conflate them with Blender Action identity.

#### Current external-project conformance

The reviewed `Killzone_Blender` 0.10.3 project already has bounded packed
track parsing, all channel decoders, retail VU0 interpolation helpers,
Skeleton parsing, rigid skinned-mesh import, source-order bone properties,
vertex groups, and an Armature modifier. Its current gaps against this
contract are:

- no complete `SkeletonAnimationResource` or BoneMap object loader;
- no explicit shared-bank Skeleton resolver;
- no direct-local pose builder, hierarchy-to-Blender conversion, or Action
  writer;
- variable-time `T=6/7/8` currently mapped as 6/12/24 Hz instead of
  12/24/6 Hz;
- `P=6` currently read little-endian instead of big-endian;
- strict parsing currently admits the `P=7` runtime fallback;
- no enforcement of the retail `R=8` bit-zero or `P=5` width/window
  invariants;
- the temporary transform is still labelled diagnostic rather than consumed
  as `InverseBindGlobal`;
- no authoritative-duration, playback-mode, root-trajectory, event, Action
  slot, or loop policy; and
- animation tests are synthetic and do not yet assert the retail corpus
  totals and selector histograms above.

Until these are corrected, an external importer may expose diagnostic
animation preview but must not label it retail-conformant.

#### Remaining animation unknowns

The remaining animation-specific unknowns are:

- the external owner/state path selecting a Skeleton for shared banks without
  an explicit instance graph;
- the complete serialized Compensate Root to runtime-instance-control bridge;
- complex event predicates and simple-event timestamp behavior at nonunit
  playback rates;
- graph-level crossfade scheduling, partial-body masks, additive quaternion
  multiplication order, and reference-removal quaternion order;
- whether all clips lacking a local Skeleton candidate are exercised;
- the exact source intent of the few rest corrections that are not
  scaled-orthogonal and therefore cannot become ordinary Blender channels;
- explicit confirmation of inferred `+Y` forward and metre units; and
- animation encoding and edited-object serialization.

### 4.23 Remaining model work

The generic framing, stream ownership, regular-mesh material bindings,
embedded static meshes, skinned packet directory, transform palette, and
static/skinned geometry decoding, placement instances, and Multi/LOD wrapper
graphs are now bounded. Skeleton packing, indexed hierarchy, collision hulls,
attachments, bone sets, BoneMaps, animation-track decoding, playback modes,
direct-local pose construction, inverse-bind semantics, and hardware/software
palette equations are also bounded. Remaining model work includes:

- material roles for signed coordinate pairs beyond proven UV0/UV1 and
  material-specific color/alpha continuations;
- the exact runtime consumer of the retained six-float bounds, remaining
  category/word-table flags, and attachment transform locality;
- the external resource path that selects a Skeleton for shared animation
  banks;
- graph-level animation blending, complex event predicates, exceptional
  nonrepresentable Blender rest corrections, and edited animation encoding;
- proximity-parent transform composition, blend-shape/linked-static behavior,
  conservative skinned bounds, and collision/render-mesh relationships;
- the caller-supplied value for the final static VIF unpack, the exact static
  VU program, and the exact dynamically uploaded skinned VU1 instructions
  that consume the control/ADC fields;
- the scene path that selects LightZones and populates the six cubic-light
  coefficient qwords;
- native front-face convention and explicit confirmation of inferred `+Y`
  forward and metre units.

Keep source-space values, transform selectors, packet groups, and strip state
available and make Blender conversion an explicit layer. A GS capture remains
a validation oracle, not a complete extractor: it sees post-VU triangles and
baked lighting, not necessarily source normals, weights, skeleton data, or
authoring topology.

No further selector watchpoint is required. A remaining static trace should
identify the value supplied to VU address `0x1EE`; separately, the generic
state-upload descriptor between the completed camera cache and VU1
`TOP+0x1E4` can close the last non-selector matrix-upload edge.

Native export remains blocked until animation encoding, unknown fields,
event/graph resources, exceptional transforms, and all edited stream layouts
round-trip safely.

The public
[`io_scene_killzone2`](https://github.com/0ther1/io_scene_killzone2/blob/e022e23cd9f570c6f29e316e9ebc3404dcd67a0a/README.md)
add-on targets Killzone 2/3 Core files, not Killzone 1 on PS2. Its structures
must not be reused without byte-level validation against this build.

## 5. Levels

### 5.1 Level resource set

The validated catalog contains 47 `level.*.core` / `.corestream` pairs:

- 39 campaign sublevels
- 8 multiplayer levels
- all 47 metadata `.core` files are in `FILES03.DAT`
- all 47 bulk `.corestream` files are in `FILES04.DAT`
- level Core metadata totals 64,378,700 bytes
- level streams total 456,126,272 bytes

There are also 41 self-contained variant-1 `*.missionscript.core` resources in
`FILES03.DAT`. They contain a `KillZoneScript` schema, a source-path string
ending in `mission_script.lua`, and a compiled/serialized payload. They are
separate from the spatial level lump.

### 5.2 Compact typed-section framing

**Verified across all applicable objects in all 47 level Cores.** Spatial
objects use a second typed-section directory form. Let `S` be the section
start, `P` the property count, and `N` the object count:

```text
S+0x00  u32 data_offset
S+0x04  u32 type_string_index
S+0x08  u32 object_count
S+0x0C  u32 inheritance_level_candidate
S+0x10  u32 property_count
S+0x14  property_descriptor property[P]       # 16 bytes each
         u32 prefix_a = 0
         u32 prefix_b = 0
         compact_directory_entry object[N]    # 8 bytes each
         [u32 zero, u32 zero]                  # optional alignment span
S+data_offset
         u32 zero_object_prefix
S+data_offset+4
         serialized object blobs, concatenated in directory order

compact_directory_entry:
    u32 blob_size
    u32 object_id
```

Define:

```text
base         = 0x1C + 0x10 * P + 0x08 * N
directory_end = S + base
first_object  = align_up(directory_end + 4, 0x10)
data_offset   = first_object - S - 4
```

Every validated spatial section has either `data_offset == base` or
`data_offset == base + 8`. The latter form has eight zero alignment bytes
between the directory and mandatory four-byte object prefix. Five classes
occur with and without the alignment span; `ZoneVisibilityInfo` has it in all
current samples. It is alignment, not a sentinel or directory entry.

This compact form has no identity words or CoreStream ownership. Do not parse
it as the 32-byte streamed-resource form, and do not accept a
streamed-resource section's `(1, 1)` prefix as compact. Require the zero word
at `S + data_offset`, begin object zero four bytes later, and advance
subsequent objects only by their declared sizes.

The complete spatial corpus is:

| Type | Sections / levels | Objects | Serialized blob sizes |
|---|---:|---:|---|
| `Zone` | 28 | 607 | formula-derived, `0x40`-`0x9A0` |
| `Portal` | 28 | 846 | 802 x `0x50`; 44 x `0x80` |
| `ZoneVisibilityInfo` | 28 | 28 | formula-derived, `0x40`-`0x2B30` |
| `RenderZone` | 46 | 93 | all `0xB0` |
| `LightZone` | 46 | 205 | formula-derived, `0x20`-`0x1B30` |
| `AmbientSoundZone` | 46 | 235 | all `0xA0` |

`Zone`, `Portal`, and `ZoneVisibilityInfo` occur together in the same 28
levels and are all absent from the other 19. Each applicable level has one
section of each type and exactly one `ZoneVisibilityInfo` object. The three
area-volume types have independent absences: `RenderZone` is absent only from
`level.2.research`, `LightZone` only from `level.1.trenches`, and
`AmbientSoundZone` only from `level.3.park`.

### 5.3 Zone and Portal

#### Zone

Let `R` be the member count at `+0x00`. The verified serialized layout is:

```text
+0x00  u32    member_count                         # R
+0x04  u32    type_string_index                    # "Zone"
+0x08  u32    zero
+0x0C  u32    zero
+0x10  u32    zone_id_token
+0x14  u32    member_object_id[R]
        f32    bounds_min[3]
        f32    bounds_max[3]
        u32    filename_string_index
        u8     zero_padding[align_up(current, 0x10)-current]
```

Offsets and size are:

```text
bounds_offset = 0x14 + 4 * R
terminal_word = 0x2C + 4 * R
payload_end   = 0x30 + 4 * R
blob_size     = align_up(payload_end, 0x10)
```

All 607 filename words resolve to the Core empty string. The old `+0x00`
`General.Name` candidate was the section-level prefix, not an object field.
Post-filename padding is zero and ranges from zero to 12 bytes.

`+0x10` is strongly identified as `ZoneID`: every value is nonzero and unique
within its Core, every portal endpoint resolves in this domain, and none
collides with a Zone directory object ID. ZoneID, compact-directory object
ID, and Core string index are separate token domains.

The 607 Zones contain 8,571 member references. Every token resolves exactly
once in the same Core, but the reflected `StaticMeshes` name is misleading:

| Target type | References |
|---|---:|
| `CollisionMeshInstance` | 5,007 |
| `StaticMeshInstance` | 2,529 |
| `ParticleSystemInstance` | 538 |
| `SimpleAnimatingSkinnedMeshInstance` | 252 |
| `Corona` | 174 |
| `PhysicsObjectInstance` | 59 |
| `ToggleObject` | 12 |

Treat this as a heterogeneous zone-membership list and resolve each token
against the complete per-Core directory with its actual target type.

#### Portal

Let `V` be the vertex count at `+0x10`:

```text
+0x00  u32      type_string_index               # "Portal"
+0x04  u32      zero
+0x08  u32      zero
+0x0C  u32      zero
+0x10  u32      vertex_count                    # V
+0x14  f32      vertex[V][3]
        u32      active
        u32      endpoint_zone_id_a
        u32      endpoint_zone_id_b
```

```text
active_offset = 0x14 + 12 * V
endpoint_a    = 0x18 + 12 * V
endpoint_b    = 0x1C + 12 * V
blob_size     = align_up(0x20 + 12 * V, 0x10)
```

All 846 retail portals are active. There are 802 four-vertex and 44
eight-vertex polygons, and all are planar within `0.000123` source units.
All 1,692 endpoint values are nonzero, resolve to ZoneIDs, and differ within
each portal. The former 28 null sides and 136 apparent self-edges came from
the section prefix and preceding object's terminal word under the obsolete
object origin.

Reflection strongly assigns endpoint A and B to `FrontZoneID` and
`BackZoneID`, respectively, but front/back orientation and polygon winding
still need one runtime comparison. All 1,692 endpoint/polygon pairs lie
inside the corresponding Zone AABB at epsilon `1e-3`, and every ZoneID is
used by at least one Portal. The corrected retail corpus provides no null,
equal-endpoint, or out-of-bounds exception.

### 5.4 Render, ambient-sound, and light zones

`RenderZone` and `AmbientSoundZone` share a verified serialized area-volume
base:

```text
+0x00  u32      point_count = 4
+0x04..+0x0C    class-specific header words
+0x10  f32      orientation[16]
+0x50  u32      closed = 1
+0x54  f32      point[4][2]
+0x74  f32      height = 1.0
+0x78            class-specific fields
```

All 328 objects use the exact local polygon
`(-1,+1), (-1,-1), (+1,-1), (+1,+1)`. Matrix elements 3, 7, and 11 are zero,
element 15 is one, and the non-unit world dimensions are carried by the
matrix. The header words differ and must not be normalized:

```text
RenderZone:       +0x04=0, +0x08=type_string_index, +0x0C=0
AmbientSoundZone: +0x04=type_string_index, +0x08=0, +0x0C=0
```

The remaining `RenderZone` fields are:

```text
+0x78  f32  far_plane
+0x7C  u32  fog_enabled
+0x80  f32  fog_start
+0x84  f32  fog_end
+0x88  f32  fog_start_density
+0x8C  f32  fog_end_density
+0x90  u32  fog_color_raw
+0x94  f32  fade_distance
+0x98  s32  priority
+0x9C  u32  filter_name_string_index
+0xA0  u32  0x00010001
+0xA4  u32  zero
+0xA8  u32  zero
+0xAC  u32  zero
```

The 93 objects include 89 with fog enabled. Far planes range from 35 to
1,601 source units, and only 49 have `far_plane == fog_end`; equality is not
an invariant. Filter names are case-sensitive Core strings. The four-word
trailer at `+0xA0` is exactly `(0x00010001, 0, 0, 0)`.

The `AmbientSoundZone` tail is:

```text
+0x78  f32  volume
+0x7C  f32  fade_distance
+0x80  s32  priority
+0x84  s32  stream_index
+0x88  u32  zero
+0x8C  u32  zero
+0x90  u32  0x00010001
+0x94  u32  zero
+0x98  u32  zero
+0x9C  u32  zero
```

Across 235 objects, volume ranges from 0.1 to 1.0, fade distance from 0 to
10, priority from 0 to 5, and stream index from 0 to 3. The six-word trailer
at `+0x88` is exactly `(0, 0, 0x00010001, 0, 0, 0)`.

`LightZone` uses a separate count-derived layout. Let `S` be the shape count
at `+0x00` and `L` the light-reference count at `+0x04`:

```text
+0x00  u32      shape_count                    # S
+0x04  u32      light_count                    # L
+0x08  u32      zero
+0x0C  u32      type_string_index              # "LightZone"
+0x10  u32      zero
+0x14  shape    shape[S]                       # 0x40 each
        u32      light_object_id[L]
        f32      fade_distance
        s32      priority
        u8       zero padding to 0x10

shape:
+0x00  u32    kind = 0
+0x04  f32    basis[3][3]
+0x28  f32    center[3]
+0x34  f32    extents[3]
```

Every shape has finite values, nonnegative extents, and an orthonormal basis
within maximum Gram-matrix error `2.03e-7`, strongly supporting an
oriented-box interpretation. Matrix convention and the mapping from
serialized kind zero to the reflected AABB/OBB subclasses remain unknown.

The 205 objects contain 2,061 shapes and 478 light references. Every light
token resolves in the same Core: 284 to `DirectionalLight` and 194 to
`OmniLight`. Zero shapes and zero lights both occur in retail data. Priority
is always present and ranges from zero to seven. If:

```text
light_offset = 0x14 + 0x40 * S
fade_offset  = light_offset + 4 * L
priority_off = fade_offset + 4
payload_end  = fade_offset + 8
```

then `blob_size == align_up(payload_end, 0x10)`. Fourteen objects have no
post-priority padding; their priority was the terminal word omitted by the
obsolete object slice. The old leading unknown value was the section prefix,
not a LightZone field.

### 5.5 Zone visibility table

Each zoned level has one `ZoneVisibilityInfo` object. Let `N` be the zone
count at `+0x00`:

```text
zone_ref_end = 0x10 + 4 * N
count2_off   = align_up(zone_ref_end, 0x10)
matrix_off   = count2_off + 4
matrix_end   = matrix_off + 4 * N * N
blob_size    = align_up(matrix_end + 4, 0x10)
```

The verified layout is:

```text
+0x00             u32 zone_count = N
+0x04             u32 zero
+0x08             u32 empty_string_table_index
+0x0C             u32 zero
+0x10             u32 zone_object_id[N]
+zone_ref_end      zero padding to count2_off
+count2_off        u32 zone_count_again = N
+matrix_off        u32 opaque[N][N]
+matrix_end        zero padding to blob_size
```

The zone-object list exactly equals the corresponding Zone compact-directory
order in every level. Across the 28 tables, `N` ranges from 2 to 52 and the
tables contain 17,841 words. The post-matrix tail is 8 or 12 zero bytes; the
newly exposed terminal word is zero in every object.

The table is not a symmetric boolean PVS. Of 8,617 off-diagonal transpose
pairs, only 4,263 full words match, but all 8,617 have equal low 24 bits:

```text
matrix[i][j] & 0x00FFFFFF == matrix[j][i] & 0x00FFFFFF
```

The high byte is directional and takes many values. Preserve all 32 bits; do
not coerce the table to booleans, require full symmetry, interpret the low 24
bits as object IDs, or infer diagonal visibility from a constant. The next
proof gate is a trace of one table consumer during a portal-visibility
decision.

### 5.6 Level object graph and import policy

The verified direct spatial relationships are:

```text
Zone compact-directory object_id ── ZoneVisibilityInfo zone list
Zone blob ZoneID                  ── Portal endpoint pair
Zone member object_id[]           ── heterogeneous instance objects
LightZone light_object_id[]       ── DirectionalLight / OmniLight
```

No Zone member list points to `RenderZone`, `LightZone`, or
`AmbientSoundZone`, and no reflected property in another section directly
names Zone or Portal. Static evidence supports manager construction by type
scan rather than a single serialized level-root object; runtime code may
still establish additional relationships after deserialization.

The render-resource graph has the same property: `StaticMeshInstance` is not
the owner or enumerator of all visible level geometry. Build a directed graph
whose nodes are `RegularStaticMeshResource`, `MultiStaticMeshResource`, and
`LodStaticMeshResource`, and whose edges are wrapper child references. A
resource with no incoming wrapper edge is a resource root. Across all 47
level Cores:

| Measure | Count |
|---|---:|
| Regular static leaves | 9,327 |
| Multi-static wrappers | 137 |
| LOD-static wrappers | 2,793 |
| Resource roots | 5,896 |
| Roots referenced by static instances | 3,158 |
| Direct world roots, with no instance reference | 2,738 |
| Direct roots that are Regular / Multi / LOD | 1,761 / 62 / 915 |

All 21,761 non-null static-instance references resolve. They name 3,160
distinct resources: 3,158 graph roots and two wrapper children, one each in
Mall and Docks1. The one remaining instance has the documented legal null
token. Process every instance reference whether or not it is a graph root:
recursively expand it, select one LOD, and apply the instance matrix. A graph
root not itself named by an instance is direct world geometry and is expanded
at identity. Direct regular leaves already decode into level/world
coordinates. A shared descendant can consequently be used by both paths.

For `MultiStaticMeshResource`, expand every child. For
`LodStaticMeshResource`, child zero is the baseline/default import; importing
every child creates overlapping copies. Do not independently import every
regular leaf, because doing so duplicates wrapper children and alternate
LODs. Use directory identity as the graph-node key, type-check token
resolution within the Core, and retain recursion-stack cycle detection.

The full LOD0 selection produces 2,924 direct leaf occurrences and 22,109
instance leaf occurrences. Their render totals are:

| Path | Packet occurrences | Triangle occurrences |
|---|---:|---:|
| Direct world roots | 91,812 | 2,576,133 |
| Static instances | 91,913 | 2,326,919 |

Packet/triangle occurrences are deliberately distinct from the 164,630
physical packets and 4,567,004 physically stored triangles: instance
definitions can be drawn many times, while unselected LOD children remain
stored but are not part of a default import.

The two focused levels make the omission especially clear:

| Level | Static nodes | Graph roots | Direct roots | Instance-definition roots | Direct LOD0 triangles | Instanced triangles |
|---|---:|---:|---:|---:|---:|---:|
| SDDocking | 69 | 61 | 43 | 18 | 91,016 | 11,802 |
| Street2 | 112 | 85 | 54 | 31 | 72,276 | 60,477 |

SDDocking resources `0x061005A8` and `0x0615026C`, used by the focused
materials `0x061763EA` and `0x060E9986`, are both direct world roots and are
not reachable from any `StaticMeshInstance`. The GS trace independently
shows their geometry family rendering. This is positive runtime evidence for
the direct-root path rather than merely an orphan-resource heuristic.

A static importer should therefore use this policy:

```python
nodes = parse_static_resource_directories(core)
incoming = count_wrapper_child_edges(nodes)
instance_refs = collect_non_null_static_instance_tokens(core)
roots = [node for node in nodes if incoming[node] == 0]

for instance in static_instances:
    for leaf in expand(instance.resource, multi="all", lod=0):
        emit(leaf, transform=instance.row_vector_matrix)

for root in roots:
    if root.token not in instance_refs:
        for leaf in expand(root, multi="all", lod=0):
            emit(leaf, transform=identity)
```

An importer that starts only from `StaticMeshInstance` can appear to work on
decal cards and props while dropping most level geometry. Requiring a final
V2-16 UV command compounds the problem: the dominant direct-world packet
shape is `69 6F 6E 6D`, not the card-style `69 6F 6E 65`.

The level string tables also identify static/skinned and LOD resources,
collision instances and poly soups, paths and AI navigation objects, lights,
particles, sounds, pickups, weapons, destructibles, effects, and general game
entities. Therefore "level format" is a compound Core object graph plus a
CoreStream, not a geometry-only file.

A useful Blender import can be layered:

1. zones, portal polygons, bounds, visibility words, and area volumes;
2. static/skinned render instances and materials;
3. collision and path/AI debug geometry;
4. lights, audio zones, effects, and entity empties with preserved properties;
5. mission-script references and other opaque gameplay records.

Apply the proven source row-vector convention to instance definitions and
identity to direct roots, then perform any host-axis conversion as a separate
step. Custom-level export is not credible until
zone/portal visibility, collision, entity lifecycle, mission scripts, and
unknown records round-trip safely. A visually correct mesh-only scene is not
a round-trippable level.

## 6. Runtime decals

This section describes the runtime decal path in the matching retail
executable. The resource schema, geometry builder, maintenance pass, render
callback, shared queue, and generic material submission are
instruction-verified. The executable does not contain the retail Core/DAT
payloads, so it proves how any `DecalResource` is interpreted but does not
provide a frequency table of the property values used by shipped assets.

Decals are not static card meshes and do not use a fixed blend/depth recipe.
One `DecalInstance` owns receiver-local geometry, enters the ordinary render
queue as one aggregate item, and submits its geometry through the ordinary
Material renderer.

### 6.1 Resource and runtime layouts

The reflected resource is `0x48` bytes:

| Offset | Type | Reflected role | Retail default |
|---:|---|---|---:|
| `+0x14` | `s32` | Max Decals | 200 |
| `+0x18` | `s32` | Decal Threshold | 100 |
| `+0x1C` | `bool` | Random Rotation | true |
| `+0x20` | `f32` | Min Size | resource-selected |
| `+0x24` | `f32` | Max Size | resource-selected |
| `+0x28` | `f32` | Fade In Time | 0 |
| `+0x2C` | `f32` | derived inverse fade-in time | derived |
| `+0x30` | `f32` | Fade Time | 7 |
| `+0x34` | `f32` | derived inverse fade time | derived |
| `+0x38` | `bool` | Clip Decal | false |
| `+0x3C` | `s32` | Sort Order | 11 |
| `+0x40` | `s32` | Sort Mode | 2 |
| `+0x44` | pointer | Material | resource-selected |

The sort modes are:

```text
0  Off
1  FrontToBack
2  BackToFront
```

The fade-time reciprocal can be calculated without a zero guard. The
maintenance path separately recognizes a non-positive fade time and retires
selected marks immediately, so a reimplementation must not rely on an
infinite reciprocal to produce that behavior.

`DecalInstance` is `0xE0` bytes. Its relevant tail is:

```text
+0x60  embedded generic render interface
+0x90  interface dispatch pointer
+0xC0  DecalResource*
+0xC4  sort/material descriptor
+0xC8  receiver-group count
+0xCC  receiver-group capacity
+0xD0  receiver-group array
```

The descriptor packs:

```text
bits 0..1  Sort Mode
bits 2..5  Sort Order
bit  6     render-interface enabled
higher     flags aggregated from the Material layers
```

The material aggregation includes bit `0x0400`, selected from runtime
`Material+0x29`; not every higher descriptor bit has a semantic name yet.

A receiver group is `0x58` bytes:

```text
+0x00  primary receiver/object key
+0x04  secondary receiver/submesh key
+0x08  geometry-dirty byte
+0x0C  mark count
+0x10  mark capacity
+0x14  mark array
+0x18..+0x2C  receiver-local bounds
+0x30  circular vertex-buffer cursor
+0x34..+0x50  generated geometry/material-buffer state
+0x3C  generated vertex-buffer pointer within that state
+0x54  active/visible byte
```

A mark is `0x2C` bytes:

```text
+0x00..+0x08  projection position
+0x0C..+0x14  normalized projection direction
+0x18         opacity
+0x1C         random scalar
+0x20         caller placement/triangle identifier
+0x24         u16 generated-vertex start, or 0xFFFF while invalid
+0x26         u16 receiver/submesh tag
+0x28         u8 provisional budget or exact generated-vertex span
```

The `DecalManager` reflection size is `0x28`. No traced path establishes a
second manager-wide capacity, lifetime, or fade policy. Preserve its unknown
state rather than duplicating the per-resource thresholds as global rules.

### 6.2 Insertion, sizing, grouping, and capacity

Marks are grouped by the receiver/object and receiver/submesh key pair. New
marks initialize as:

```text
opacity      = FadeInTime > 0 ? 0 : 1
random       = engine_random()
vertex_start = 0xFFFF
budget       = MaxSize > 0.5 ? 128 : 16
```

The provisional `0x28` budget is replaced after geometry generation:

```text
unclipped mark  3
clipped mark    actual fan-triangle vertex count
```

One random scalar controls both size and orientation:

```text
size  = MinSize + random * (MaxSize - MinSize)
angle = random * 2*pi
```

Retail constructs an axis-angle quaternion around the mark's projection
direction and rotates a deterministic perpendicular vector to obtain its
oriented projector basis. None of the traced insertion or geometry-builder
paths reads `DecalResource+0x1C Random Rotation`: the angle is applied
unconditionally. This is strong evidence that the reflected property is
ignored by this retail path, but not a whole-program proof that no alternate
path consumes it.

For one receiver group, let `B` be the sum of the marks' current
`span_or_provisional_budget` values. Buffer growth uses:

```text
grown    = 3 * floor((B + floor(B / 2)) / 3)
capacity = min(grown, 3 * MaxDecals)
```

Consequently:

- compatible buffers grow to roughly `1.5 * B` and are not routinely shrunk;
- capacity is rounded down to a multiple of three vertices;
- `Max Decals` is a per-receiver triangle-capacity limit, not an instance-wide
  mark count;
- the unclipped path consumes one triangle per mark, while one clipped mark
  may consume several triangles.

Generated geometry is a circular vertex buffer. If the current build lacks a
large enough contiguous span, retail immediately erases the smallest oldest
receiver-local mark prefix that frees one. This hard eviction does not fade,
does not compare ages across receiver groups, and never removes newer marks to
preserve an older current build.

### 6.3 Unclipped projector triangle

With `Clip Decal == false`, every valid mark emits one oversized triangle. In
the rotated projector basis its position and UV coordinates are:

```text
(-size, -size) -> (0, 0)
( 3size, -size) -> (2, 0)
(-size,  3size) -> (0, 2)
```

The triangle encloses the nominal square `[-size,+size]^2`; interpolation
maps that square to `[0,1]^2`. It is deliberately not a four-vertex quad.

Runtime `Material+0x2A != 0` swaps the second and third emitted vertices. The
behavior is proven to select output winding, but the reflected property name
for that byte remains unresolved.

### 6.4 Clipped receiver projection

Each receiver query candidate has this effective representation:

```text
+0x04  u16 receiver/submesh tag
+0x08  s32 polygon vertex count
+0x10  f32 plane normal x
+0x14  f32 plane normal y
+0x18  f32 plane normal z
+0x1C  f32 plane constant d
+0x20  Vec2 plane-space polygon vertices[count]
```

The normalized plane is:

```text
dot(N, P) + d = 0
```

Retail deterministically constructs orthonormal tangents `T` and `B` from
`N`. It reconstructs a plane-space point `(x,y)` as:

```text
P = T*x + B*y - d*N
```

The mark position and projection direction are first transformed into
receiver-local space. A candidate is skipped unless:

```text
candidate.tag == mark.tag
dot(mark_direction, N) >= -0.1
```

Let `C` be the local mark center and:

```text
q = dot(N, C) + d
```

The sign of `q` controls the direction used to move the footprint center
along the mark projection direction. Retail reconstructs the corresponding
point `P` on the receiver plane, computes `L = length(P-C)`, and uses:

```text
signed_distance = q < 0 ? +L : -L
projected_center = C + signed_distance * mark_direction
```

Zero follows the non-negative branch. For the normalized plane and its
orthogonal reconstructed point, `L == abs(q)`.

For marks with `size >= 0.5`, retail adds an asymmetric back-side check:

```text
if q < 0 and dot(N, P-C) > 0.5:
    reject
```

This is equivalently:

```text
reject if q < -0.5
```

The comparison is strict, so `q == -0.5` is accepted. There is no matching
positive-side distance cap. The `0.5` threshold also selects the provisional
budget from `MaxSize`, but those are otherwise separate decisions.

The footprint is rotated by the same per-mark `angle` used by the projector
basis. Its pre-clip polygon depends on the actual mark size:

```text
size < 0.2:
    (-size, -size) -> (0, 0)
    ( 3size, -size) -> (2, 0)
    (-size,  3size) -> (0, 2)

size >= 0.2:
    (-size, -size) -> (0, 0)
    (+size, -size) -> (1, 0)
    (+size, +size) -> (1, 1)
    (-size, +size) -> (0, 1)
```

The `0.2` footprint threshold is distinct from both uses of `0.5`.

Retail clips this polygon against each edge of the convex receiver polygon
using Sutherland-Hodgman clipping. Position and UV vectors are carried in
parallel; crossing vertices linearly interpolate both. Signed edge
classification uses an approximately `1e-5` epsilon, and an output with fewer
than three vertices is discarded.

A surviving `n`-vertex polygon becomes a triangle fan:

```text
(0, 1, 2), (0, 2, 3), ... (0, n-2, n-1)
triangle count = n - 2
vertex span    = 3 * (n - 2)
```

Each clipped 2D vertex is reconstructed on the candidate plane, transformed
into receiver-local vertex space, and emitted with its interpolated UV.
`Material+0x2A` applies the same final-winding swap as in the unclipped path.

### 6.5 Fade, retirement, and vertex alpha

There is no independent creation timestamp, fixed lifetime, or age-expiry
test in the traced mark path. Two different capacity mechanisms exist:

1. circular-buffer pressure causes immediate hard eviction during a geometry
   build;
2. the maintenance pass gradually fades an oldest prefix under soft pressure.

For the soft-pressure pass:

```text
excess_vertices =
    max(total_instance_vertices - 3 * DecalThreshold, 0)
```

Every receiver group independently starts at its oldest mark and selects full
mark spans until their sum covers `excess_vertices`. The excess is not one
global age-sorted work list. A selected mark's entire span is charged even
while its opacity is only partly reduced:

```text
opacity = clamp(opacity - dt * inverse_fade_time, 0, 1)
```

Non-positive Fade Time retires selected marks immediately. Fade-in starts a
new mark at zero and advances it toward one using the inverse fade-in time;
the threshold also bounds the newest-mark region visited by that pass.
Maintenance runs before receiver visibility selection, so culled groups
continue to fade.

Opacity updates only the generated vertex alpha byte:

```text
rgba = (rgba & 0x00FFFFFF)
     | (clamp(opacity * 255, 0, 255) << 24)
```

The generated RGB bytes are unchanged. Whether that alpha blends, tests,
masks, or has no visible effect depends on the Material state.

### 6.6 Receiver, ring, and queue draw order

The decal render callback visits receiver groups in receiver-group array
order and skips a group when its `+0x54 active/visible` byte is zero. There is
no global chronological merge between groups.

Within one group, let:

```text
first  = oldest surviving mark.vertex_start
cursor = receiver_group.cursor
limit  = generated vertex-buffer capacity
```

Retail submits:

```text
if first < cursor:
    draw(start=first, count=cursor-first)
else:
    draw(start=first, count=limit-first)  # older tail
    draw(start=0,     count=cursor)       # newer head
```

Thus the circular buffer is submitted oldest-to-newest: one span when
unwrapped, or the old tail followed by the new head when wrapped. Each span
is a separate call through the same generic Material renderer.

Individual marks and receiver groups are not queue items. The entire
`DecalInstance` contributes one generic render item, with one aggregate bound
and the descriptor from section 6.1. The shared queue contains 48 buckets:

```text
16 SortMode::Off buckets
16 FrontToBack buckets
16 BackToFront buckets
```

Queue traversal is:

```text
for sort_order in 0..15:
    draw Off[sort_order]
    draw FrontToBack[sort_order]
    draw BackToFront[sort_order]
```

Lower order therefore draws first. At one order, Off precedes FrontToBack,
which precedes BackToFront. Decals default to order 11 and BackToFront, so
other generic items in that same bucket are intermingled by depth.

`SortMode::Off` constructs sequential collection indices and orders those
indices by their unsigned integer value before dispatch. The post-sort walk
uses that ascending index array, so Off preserves collection order.

The sorted modes use the aggregate bounds center:

```text
center = 0.5 * (bounds_min + bounds_max)
delta  = center - camera_position
d2     = dot(delta, delta)

key = dot(camera_forward, delta) < 0 ? -d2 : d2
```

FrontToBack orders the float key ascending; BackToFront orders it descending.
Objects behind the camera have negative squared-distance keys. The in-place
quicksort is not stable for equal keys. Its deterministic pivot generator is:

```text
seed = seed * 0x0019660D + 0x3C6EF35F
pivot_index = (seed >> 8) % element_count
```

The seed starts at zero for each invocation. Preserve this behavior if exact
equal-depth replay matters; do not add an insertion-order tie breaker.

No dedicated decal pass was found: decals participate in this shared generic
interface queue. The current trace does not establish where the whole queue
flush sits relative to separate opaque-world, particle, projector, and
post-effect passes. That outer frame-graph boundary remains unknown and must
not be replaced with a guessed cross-pass order.

### 6.7 Material-selected blend and depth policy

Both unwrapped and wrapped ring spans flow through the generic Material
submission described in section 3.7. The traced decal setup does not mutate
the resource Material, `Material::Layer`, or `HwLayer`, and it makes no
decal-specific override for:

- `ALPHA_1` blending;
- alpha-test function, reference, or alpha-fail action;
- depth comparison or `ZBUF_1.ZMSK`;
- framebuffer mask;
- PABE or FBA;
- TCC, TFX, or texture-alpha interpretation;
- wrap or filtering state.

Material layers render in ascending serialized order. The only
decal-lifecycle value introduced into that pipeline is generated vertex
alpha. A faithful implementation must therefore submit decal geometry
through the ordinary Material path. Hard-coding the common convention
"alpha blend, depth test, no depth write" is not retail behavior: any of
those choices may differ per decal Material and layer.

## 7. `.mss` is audio

All 8,704 `.mss` entries are under localized speech, sound banks, music, menu,
or ambient-stream paths. Every one is 2,048-byte aligned and sized to a
multiple of 2,048. Representative headers are:

```text
0x00  char[4]  "MCSS"
0x04  u32      0x00010000
0x08  u32      0x00000800
0x0C  u32      payload bytes after the 0x800-byte header
0x10  u32      likely sample rate (observed 14000, 25000, and 44100)
0x14  u32      packed channel/stream/codec fields, unresolved
0x18  u32      0x00000800
0x1C  u32      likely sample/frame count, unresolved
0x20  char[]   "Guerrilla MSS"
```

For all checked samples, `file_size == 0x800 + value_at_0x0C`. This format is
outside the Blender geometry importer except for preserving level sound
references.

## 8. What the ELF can provide

`SCUS_974.02` is ELF32, little-endian MIPS (`e_machine = 8`) with entry point
`0x00146F30`. Its main load segment maps file offset `0x1000` to EE virtual
address `0x00140000`, so loaded bytes use:

```text
EE address = file offset + 0x0013F000
```

The executable contains:

- the literal `cdrom0:FILES.DAT` at file offset `0x402BF8`
  (EE address `0x00541BF8`);
- Core magic/version checks and extensive type/property reflection strings;
- model, texture, material, collision, zone, portal, and resource class names;
- loader/decompression/streaming code needed to resolve unknown fields;
- two DVP overlay sections containing vector-unit programs.

It does not contain the DAT catalog or bulk asset records. There is no
`<CoreStreamData>` or `MCSS` payload signature in the ELF, and no catalogued
`.zone` or `.col` format to extract from it. The one
`Core<1.79><PSX2>` occurrence is an executable string used by Core handling,
not a retail Core asset.

An ELF mode is still useful:

1. validate the exact game build;
2. build a reflection/type dictionary from read-only data and xrefs;
3. locate the Core and stream loader functions;
4. extract and disassemble DVP/VU programs;
5. combine that metadata with assets read from a separately supplied
   `ArchiveSource`.

If the UI accepts an ELF path and no DAT source is available, it should fail
with a precise "executable metadata loaded; asset archives missing" message,
not return an empty asset list.

## 9. Recommended importer architecture

```text
InputResolver
  detect by magic, not extension
  ISO  -> IsoArchiveSource + ElfMetadata from ISO/SCUS_974.02
  dir  -> DatArchiveSource
  ELF  -> ElfMetadata + exact sibling DAT check

DatCatalog
  immutable validated tree
  read_entry_range(path, offset, size)

CoreReader
  raw string table
  schema descriptors
  8-byte compact, 24-byte BoneMap, and 32-byte streamed directories
  directory-specific object-prefix and alignment rules
  per-Core type-checked reference multimap
  opaque-byte preservation

CoreStreamReader
  64-byte framing
  object-owned bounded slices

TextureDecoder
  PSM/TBW-aware texel addressing
  CPSM/CSM/CSA-aware CLUT
  mip and alpha policy

ModelDecoder
  leading-static root and packet-directory parser
  generic static-tail grammar and DMA/VIF strip decode
  exact skinned quantization and transform-group decode
  instance matrices and Multi/LOD resource graph
  materials and collision

SkeletonDecoder
  packed source/runtime span calculation
  hierarchy, transform indices, hulls, attachments, and bone sets

AnimationDecoder
  serialized clip fields, BoneMaps, and explicit/default linkage
  exact packed-track boundary walk, decode, and corpus validation
  authoritative duration, rate, playback modes, root trajectory, and events
  direct-local category sampling and source-order hierarchy construction

BlenderAnimationAdapter
  persistent Core/object/Skeleton/bone/track identities
  one explicit native-to-Blender basis conversion
  inverse-bind/rest and matrix-basis conversion
  slotted Action binding, native evaluation, bounded baking, and loop policy

LevelDecoder
  compact zones, portals, visibility tables, and area volumes
  direct static roots, wrapper definitions, and mesh instances
  collision/paths/entities
  script and opaque-record preservation

DecalRuntime
  receiver grouping and local projection/clipping
  circular geometry capacity, eviction, fade, and alpha maintenance
  aggregate queue submission through the ordinary Material renderer
```

All readers should be bounded and immutable. Unknown records should carry
their original bytes, source path, package, archive offset, and digest.
Editing one understood object must not silently discard adjacent unknown
objects from the same compound lump.

## 10. Validation gates

### Archive

- Parse every record exactly once.
- Confirm file count, case-insensitive uniqueness, and package bounds.
- Re-read representative prefixes directly from both ISO and extracted DAT
  sources and compare hashes.

### Texture

- Decode all 19 proven standalone samples identically.
- Reproduce the complete variant-5 totals: 111 Texture-bearing Cores, 6,907
  Texture sections, 13,891 physical objects, 13,718 logical Core-local
  tokens, 305,231,104 payload bytes, and the exact per-format counts in
  section 3.5. Preserve the 47-level subset totals independently.
- Reproduce all 23 descriptor bit counts, masks, and loader sampling-table
  entries. Require the complete catalog to contain only codes 0-5, 13, and
  22; do not add serialized decoders for the 15 zero-count formats.
- Reproduce the descriptor-table sampling PSMs and the large-buffer
  PSMT8-to-PSMCT32 and PSMT4-to-PSMCT16 upload aliases without adding a
  CPU-side transform.
- Parse `RGBA_8888` as packed RGBA bytes and `RGB_888` as packed RGB bytes;
  do not swizzle, rescale alpha, or claim a stored RGB888 alpha channel.
- Require each of the 13,865 valid streamed chunk sums to equal its
  directory-owned CoreStream size. Recompute every mip byte count, require
  `0x100` pixel-storage alignment, and require every alignment byte to be
  zero.
- Parse all 36,752 serialized span records, require identical halves, and
  reconstruct all 13,887 valid raw mip prefixes byte-for-byte from the first
  18,376 runtime-consumed records.
- Reproduce the exact PSMT8 and PSMT4 equations over all 98 validated
  dimension/format cases and 4,129,024 texels, including low-nibble-first
  PSMT4 and row-major native 8x8 mips.
- Reproduce all 3,175 complementary-alpha `INDEX_4` palettes and all 7,383
  standard `INDEX_8` palettes byte-for-byte.
- Reproduce ALPHA_4/8 normal and complement palette equations, the 256-entry
  CSM1 permutation, and CPSM/CSM/CSA/CLD = PSMCT32/CSM1/0/1.
- Reproduce the GLOW lower-half zeros, upper-half alpha ramp, reversed RGB
  source walk, and CSM1 upload permutation without assigning semantics to the
  dynamic source buffer.
- Reproduce all 22 inline objects without consuming their first payload word
  as a palette index or their footer as pixel data. Require the exact
  ten-`INDEX_8`, six-`ALPHA_8`, six-`RGBA_8888` split and retain Street1
  `0x01F67930` as the only level-inline example.
- Scope Texture tokens to their Core, retain all 173 equivalent extra
  physical copies in 66 duplicate groups, and reject any group whose
  metadata or payload differs.
- Resolve the selected 11,870 Materials into exactly 30,814 Texture
  providers, 134 AnimatedTexture providers, and 46 null providers. Consume
  all 1,559 flag-`0x20` optional transforms without creating any
  opaque/procedural provider. Reproduce 5,067 direct and 3,718 instance
  Texture definitions, all 85 animations, and 1,302 frames.
- Compare native decoding with a GS-memory capture for the same texture.
- Reproduce the captured PSMCT16-to-PSMT4 alias path, including TBW/DBW and
  CSM1/CSA/CLD state.
- Decode the four Street2 `INDEX_4` planes with the exact mapping, but keep
  named-asset capture attribution separate until a continuous positive trace.
  Require all seven selected references to use operation 5 and the normal
  palette descriptor.
- Resolve Street2 Material `0x01F7E8D4` to `INDEX_8` Texture `0x01F70942`.
  Require its single direct-UV0 `ALPHAADD` layer, exact render-state table,
  normal palette, 24,157 zero / 22,268 partial / 19,111 full-alpha texels,
  11 instanced users, 404 triangle occurrences, and target V4-8 alpha values
  121 or 128.
- Resolve Mountain Pass foliage Material `0x00FB0B4A` to both same-texture
  passes and require its exact alpha coverage, 411 instance occurrences, and
  6,576 triangle occurrences. Resolve sandbag/rockwork Material `0x005436FE`
  to its opaque Texture and 14 selected direct occurrences. Keep both
  diagnostic color graphs explicitly separate from native multi-pass
  reconstruction.
- Reproduce Direct 0001 token `0x00566D60` as four records and 2,266
  triangles. Connect the structurally selected diagnostic color source for
  each record, and require record 0 to disclose that its six-float transform
  was ignored.
- Reproduce the three-pass candidate family's `TEX0`, `TEX1`, `TEXA`,
  `CLAMP`, `ALPHA`, `TEST`, `TEXCLUT`, and mip state without assigning it
  uniquely to either colliding SDDocking material or overriding the static
  `0x54`/HIGHLIGHT predictions.
- Test non-square dimensions, TBW greater than logical width, all CLUT modes,
  alpha, mip levels, and malformed chunk tables.

### Model

- Reproduce the seven-resource corpus totals: 9 records, 17 packets, 709 strip
  vertices, and 415 emitted triangles.
- Reproduce the 47-level static totals: 9,327 regular resources, 164,630
  physical packets, 4,567,004 physically stored triangles, and all 18 packet
  shapes.
- Accept zero or more V2-16/V4-16 coordinate commands plus the optional
  terminal V1-8/V2-8/V3-8 stream; do not require a final V2-16 command.
- Reproduce the full skinned totals: 1,244 objects, 29,680 packets, 52,249
  groups, 1,374,527 position/normal pairs, and 1,661,564 targets.
- Require the ordinary section-level zero word and begin expanded-directory
  objects at `section + data_offset + 4`.
- Require every packet/record quantized bound to equal its decoded extrema.
- Keep decoded positions within four largest-axis quantization steps of each
  serialized floating static-resource AABB.
- Require each `RET` transfer length to stay within its directory-derived
  packet and declared stream chunk.
- Require the exact skinned header-address equations, pair ownership,
  control divisibility, target coverage, and ten tail shapes.
- Resolve all 11,322 transform-record selectors against the packed Skeleton
  bone count, including the 26 records on wrapper-associated null-token
  meshes, and require source-order indexing.
- Replay representative packets and match the documented position, normal,
  `/4096` UV, duplication, and source-ADC equations.
- Reproduce all 35 target and 35 confounder V4-16 packets, both signed UV
  pairs, constant target V4-8 alpha 127, and their 928 emitted triangles.
- Match serialized layer `+0x31` through `HwLayer+0x54` and
  renderer `+0x260` to all coordinate handlers; retain optional six-float and
  4x4 payloads. Validate the cubic-light V4-8 RGB equation independently of
  runtime material attribution.
- Match CoreStream source ranges and the pending final unpack to one runtime
  VIF submission.
- Match source, runtime, and imported floating-point bounds and primitive
  counts.
- Reproduce 21,762 static and 656 simple-skinned instance layouts, then
  type-check every resource, parent, and default-animation reference.
- Parse all Multi/LOD arrays linearly, require positive monotonic boundaries
  after the zero baseline, and expand the shared resource graph with
  defensive cycle detection.
- Reproduce 416 Skeletons, 18,037 bones, 3,480 hull slots, and 503 attachment
  records; require both packed-size formulae, hierarchy/name agreement,
  category permutations, hull-slot mappings, and sentinels.
- Resolve all 584 BoneMaps and 17,586 base-animation links, and require the
  ELF-derived boundary walk to consume all 1,589,420 track records and
  14,014,155 packed bytes exactly.
- Reproduce the exact `T`, `R`, `P`, and `S` selector histograms in section
  4.22; require `T=6/7/8` rates 12/24/6 Hz, all 6,223 `R=8` bit-zero
  descriptors, `P=5` maximum width/window 16/54, big-endian `P=6`, and no
  emitted W lane.
- Parse and preserve every clip's playback mode, event token, duration,
  Compensate Root field, Total Distance, and BoneMap token. Require 826
  repeating and 16,760 one-shot/held clips, and preserve the 8,341 clips whose
  duration differs from maximum key time.
- Reproduce category reset defaults, case-insensitive first-match BoneMap
  binding, direct Q/QT/QTS locals, category-zero static translation,
  category-two column scale, and `parent_global * local` accumulation.
- Interpret each serialized temporary transform as `InverseBindGlobal`,
  require `RestGlobal=inverse(InverseBindGlobal)`, and match both hardware
  `G_animated * bone_local` and software
  `(G_animated * InverseBindGlobal) * bind_vertex` deformation.
- Validate Blender conversion with one explicit basis, the documented
  `P[i]` and `matrix_basis[i]` equations, quaternion XYZW-to-WXYZ ordering,
  sign-continuous baked keys, authoritative duration, and a decomposition
  residual gate.
- Validate strip winding and restart/degenerate handling. Require direct
  STQ0/STQ1 to preserve V, model GS linear neighbors from `U-0.5/V-0.5`, and
  apply exactly one host V conversion. Validate Skeleton
  hierarchy/inverse-bind semantics, source-order palette composition, LOD
  metric, and host coordinate conversion independently.

### Level

- Reproduce the 47-level totals: 607 Zones, 846 Portals, 28 visibility
  tables, 93 RenderZones, 205 LightZones, and 235 AmbientSoundZones.
- Reproduce 5,896 static resource roots, including 2,738 direct world roots;
  expand all Multi children, select LOD child zero by default, and process the
  two legal instance targets that are wrapper children.
- Match the default render totals of 2,576,133 direct-world and 2,326,919
  instanced triangle occurrences without duplicating wrapper children or
  alternate LODs.
- Preserve separate definition, wrapper, authored-occurrence, and
  direct-world identities. Repeated occurrences may share Blender mesh data,
  but explicit identity-matrix instances must not collapse into implicit
  direct roots.
- Verify `C @ transpose(M_src) @ inverse(C)` against transformed source
  points and AABBs, retain all 16 source floats, and apply each occurrence
  matrix once regardless of wrapper depth.
- Require compact-section prefixes, directory-to-object alignment, mandatory
  zero object prefixes, blob-size equations, repeated visibility counts, and
  all zero padding to match.
- Resolve Zone member IDs, visibility Zone IDs, LightZone light IDs, and
  Portal ZoneIDs in their separate, type-checked per-Core domains.
- Keep Zone membership separate from transform parenting. Preserve the 132
  simple-skinned proximity-parent links as unresolved metadata until runtime
  evidence proves whether their matrices are local or world-space.
- Trace the active LightZone-to-VU cubic-light coefficients rather than
  inferring mesh lighting from nearby serialized light colors.
- Match portal front/back orientation, area/light matrix convention, and Zone
  bounds against a runtime dump.
- Trace one `ZoneVisibilityInfo` consumer before assigning semantics to either
  the symmetric low 24 bits or directional high byte.
- Preserve cross-object references, raw visibility words, and unknown records.
- Import collision and render meshes as distinct collections.
- Do not enable native export until an unchanged parse/serialize is
  byte-identical or has a documented canonical equivalence.

### Decal

- Match every `DecalResource`, `DecalInstance`, receiver-group, and mark
  offset in section 6.1, including descriptor bit packing and defaults.
- Reproduce the shared random scalar's size and angle, and verify the current
  retail path both with the reflected Random Rotation property set and
  cleared before assigning that property behavior.
- Emit the exact unclipped `(-1,-1)/(3,-1)/(-1,3)` position shape and
  `(0,0)/(2,0)/(0,2)` UVs, with both `Material+0x2A` winding states.
- Exercise clipped marks on both sides of a receiver plane. Accept exactly
  `q == -0.5`, reject `q < -0.5` only for `size >= 0.5`, and confirm that the
  positive side has no equivalent cap.
- Test both sides of the `size == 0.2` footprint boundary, every polygon-edge
  crossing class around the clipping epsilon, interpolated UVs, rejection
  below three vertices, and `3*(n-2)` fan output.
- Reproduce provisional budgets, the approximately 1.5x growth equation,
  triangle rounding, the per-receiver `3*MaxDecals` cap, wraparound, and the
  smallest-oldest-prefix hard eviction.
- Test soft pressure across multiple receiver groups to ensure each group
  fades its own oldest prefix. Cover partial opacity, non-positive Fade Time,
  fade-in, invisible receivers, dead-mark retirement, and RGB-preserving
  alpha-byte updates.
- Verify group-array draw order and the unwrapped, wrapped-tail, then
  wrapped-head span order. Ensure marks or groups never become independent
  queue-sort items.
- Cover all 16 sort orders and three modes. Match Off collection order,
  ascending FrontToBack keys, descending BackToFront keys, negative keys
  behind the camera, and deterministic but unstable equal-depth quicksort.
- Run each decal span through the ordinary multi-layer Material renderer and
  compare `ALPHA_1`, `TEST_1`, `ZBUF_1`, `FRAME_1`, PABE/FBA, TCC/TFX,
  texture, wrap, and filter state. A special decal blend/depth preset is a
  validation failure.
- Keep ordering against dedicated world, particle, projector, and post-effect
  passes explicitly unverified until an instruction-complete outer queue
  flush trace or runtime frame capture establishes it.

## 11. References

Repository evidence and implementations:

- [`src/kz_iso.cpp`](../src/kz_iso.cpp) - minimal ISO 9660 reader and supported
  disc validation.
- [`docs/findings.md`](findings.md) - FilePS2, VIF1/VU1, runtime texture, mesh,
  zone, and rendering measurements.
- [`ext/pcsx2/pcsx2/GS/GSRegs.h`](../ext/pcsx2/pcsx2/GS/GSRegs.h) - PSM
  constants.
- [`ext/pcsx2/pcsx2/GS/GSLocalMemory.cpp`](../ext/pcsx2/pcsx2/GS/GSLocalMemory.cpp)
  and [`GSTables.h`](../ext/pcsx2/pcsx2/GS/GSTables.h) - authoritative page,
  block, column, pixel, and palette addressing used by this project.
- [`ext/pcsx2/pcsx2/GS/GSClut.cpp`](../ext/pcsx2/pcsx2/GS/GSClut.cpp) - CSM/CPSM
  palette handling.
- [`ext/kzgs/tools/kzgs_replay.cpp`](../ext/kzgs/tools/kzgs_replay.cpp) -
  runtime PSMT8/PSMT4/CT32 texture capture and alpha handling.
- [`ext/pcsx2/pcsx2/Dmac.h`](../ext/pcsx2/pcsx2/Dmac.h) - PS2 DMA tag IDs and
  `RET` chain behavior.
- [`ext/pcsx2/pcsx2/Vif_Unpack.cpp`](../ext/pcsx2/pcsx2/Vif_Unpack.cpp) -
  VIF unpack widths and V4-5 channel expansion.
- [`tools/scripts/vifparse.py`](../tools/scripts/vifparse.py) - VIF command
  inspection used for runtime evidence.

General PS2 hardware references should be used only for GS/VIF mechanics.
Killzone-specific container and Core claims above come from the validated
retail build, not from generic PS2 documentation.

Public archive research:

- [`killzone.bms`](https://www.aluigi.altervista.org/bms/killzone.bms) and the
  associated
  [ResHax format discussion](https://reshax.com/topic/13231-killzone-ps2-dat-files/#comment-63229)
  corroborate the recursive catalog bit fields and use of all five DAT files,
  with the retail/PSP qualification noted above.
- A later
  [ResHax clarification](https://reshax.com/topic/13231-killzone-ps2-dat-files/#comment-63235)
  notes that `FILES01-04.DAT` are payload packages whose index lives in
  `FILES.DAT`.
- No public SCUS-974.02 model/skeleton serializer, vertex declaration,
  world/level parser, or working Killzone 1 Blender/Noesis importer was found
  as of 2026-10-17.
