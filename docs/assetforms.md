# Killzone PS2 asset formats

This document records the evidence needed by a Blender importer for the NTSC-U
Killzone executable `SCUS_974.02` (XOR CRC `0xCAAEC49C`). It separates verified
layout from working hypotheses so that the importer does not silently turn one
sample into a purported general format.

No game data belongs in this repository. Measurements below came from an
authorized NTSC-U disc image and the matching executable; only aggregate
counts, offsets, signatures, and format descriptions are recorded here.

## Evidence labels

- **Verified**: observed across the stated sample set, bounds-checked, and
  internally consistent.
- **Strong inference**: fits every current observation and PS2 hardware
  behavior, but still needs a runtime trace or more samples.
- **Unknown**: preserve the original bytes and reject editing until resolved.

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
| `0x08` | `u32` | Unknown; `0x00416305` in the validated build |
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

### 3.5 Embedded level Texture containers

All 47 retail level Cores were parsed through their object directories and
owned CoreStream ranges:

```text
Texture sections:                 6,503
Physical Texture objects:        12,613
Logical Core-local tokens:       12,566
Texture payload bytes:      273,141,504
```

Every Texture section has the same reflected property schema. The stable
serialized prefix is:

| Texture offset | Type | Meaning |
|---:|---|---|
| `+0x00` | `u32` | no-streaming flag |
| `+0x04` | `u32` | linear-storage flag |
| `+0x20` | `u32` | width |
| `+0x24` | `u32` | height |
| `+0x28` | `u32` | engine pixel-format enum |
| `+0x2C` | `u32` | zero in the retail corpus |
| `+0x30` | `u32` | mip count |
| `+0x34` | `u32` | chunk count |
| `+0x38` | `u32[chunk_count]` | chunk sizes |

In particular, `+0x34` is a count, not a storage-format code. All 12,612
non-inline objects have `no_streaming == 0`, and all 12,613 objects have
`linear_storage == 0`. For each of the 12,608 valid streamed objects:

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
until the requested mip QWC is satisfied. Across all 12,609 valid textures:

```text
serialized span records          33,084
runtime-consumed records         16,542
multi-span textures               1,934
records with nonzero offsets      1,350
span reconstruction anomalies         0
```

Concatenating:

```python
chunk[byte_offset : byte_offset + QWC * 16]
```

reconstructs every raw mip prefix byte-for-byte. The spans exclude final
pixel-storage padding and indexed palette chunks. The single inline texture
places its payload between the chunk sizes and this same footer.

The matching retail ELF has a 23-entry, `0x34`-byte pixel-format table at VA
`0x00519CD8`, file offset `0x003DACD8`. File offset `0x003DAD00` is record
0's `+0x28` name pointer, not the table base. Its ordered names are:

```text
 0 INDEX_4               12 RGB_888_32_REV
 1 INDEX_8               13 RGB_888
 2 ALPHA_4               14 RGB_888_REV
 3 ALPHA_8               15 RGB_565
 4 GLOW_8                16 RGB_565_REV
 5 RGBA_8888             17 RGB_555
 6 RGBA_8888_REV         18 RGB_555_REV
 7 RGBA_5551             19 RGBE
 8 RGBA_5551_REV         20 INDEX_2X2
 9 RGBA_4444             21 INDEX_2
10 RGBA_4444_REV         22 INVALID_PIXELFORMAT
11 RGB_888_32
```

Only eight codes occur in the 47 level Cores:

| Code | Name | Bits/texel | Sampling PSM | Objects | Selected direct | Selected instance |
|---:|---|---:|---|---:|---:|---:|
| 0 | `INDEX_4` | 4 | PSMT4 | 3,034 | 897 | 697 |
| 1 | `INDEX_8` | 8 | PSMT8 | 6,320 | 2,898 | 1,040 |
| 2 | `ALPHA_4` | 4 | PSMT4 | 494 | 281 | 338 |
| 3 | `ALPHA_8` | 8 | PSMT8 | 2,564 | 902 | 1,579 |
| 4 | `GLOW_8` | 8 | PSMT8 | 31 | 9 | 22 |
| 5 | `RGBA_8888` | 32 | PSMCT32 | 134 | 56 | 31 |
| 13 | `RGB_888` | 24 | PSMCT24 | 32 | 21 | 11 |
| 22 | `INVALID_PIXELFORMAT` | 0 | invalid | 4 | 3 | 0 |

The selected counts are unique Core-local tokens reached from the default-LOD
static scene. Direct and instance columns are independent.

`FUN_002DDC98` at VA/file `0x002DDC98/0x0019EC98` returns the descriptor's
bits-per-texel field. The sampling-PSM table is at VA/file
`0x00513658/0x003D4658`; the loaders at `0x15B1C0` and `0x15B730` index it
directly.

The runtime upload path is also exact. `FUN_00159BB8` at VA/file
`0x00159BB8/0x0001ABB8` selects:

```text
runtime width >= 16:
  sample PSMT8  -> upload PSMCT32 at width/2, height/2
  sample PSMT4  -> upload PSMCT16 at width/2, height/2
  sample PSMCT32/24 -> retain PSM and dimensions

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
are 12,060 one-level, 49 two-level, 19 three-level, and 481 four-level
objects. The chunk array partitions the combined payload; it is not one entry
per mip. Concatenate pixel chunks, walk mip lengths from dimensions and
format, consume the final alignment, and only then read an indexed palette.

The runtime loader independently confirms the mip walk. Instructions at
`0x15B324..0x15B3B8` halve dimensions and accumulate exact
`ceil(width * height * bpp / 8)` lengths into four reserved mip descriptors.
There is no per-mip alignment. Runtime span vectors and archive chunks are
transport partitions rather than semantic mip records.

All 3,034 `INDEX_4` palette chunks are exactly:

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

All 6,320 `INDEX_8` palette chunks are exactly:

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

Street1 Texture `0x01F67930` is the sole inline object:

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
in Street2 and `INDEX_8` in Park2. Thirteen Core-local tokens also have
duplicate directory definitions, for 47 extra physical copies. Each group
has identical metadata and payload hashes. Preserve the physical entries for
round trip, but resolve a verified equivalent group once as a logical token.

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
| `0x18` | `u32` | Absolute offset in the paired CoreStream |
| `0x1C` | `u32` | Owned CoreStream byte size |

Require the zero word at `S + data_offset`. Object blobs begin at
`S + data_offset + 4` and are concatenated using their declared sizes. The
stream offset is relative to the start of the complete CoreStream entry,
including its 64-byte header. Objects without stream data normally carry size
zero and an offset at the end of the CoreStream. Treat an entry's
`[stream_offset, stream_offset + stream_size)` as its ownership bound; do not
infer ownership from the next DMA-looking signature.

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
u = signed16_x / 4096.0
v = signed16_y / 4096.0
GS_ST = (u * Q, v * Q, Q, Q)
```

For V4-16, XY supplies UV0 and ZW supplies UV1. The target SDDocking mesh has
UV0 ranges `U=-1.326904..1.073975`, `V=-3.712646..3.794434`; UV1 stays near
the unit square at `U=0.000488..0.918701`,
`V=0.001953..0.996094`. The large UV0 range and captured repeat/repeat
addressing establish intentional tiling.

STQ0 applies `ITOF12` to XY and multiplies by Q. STQ1 routes ZW to XY,
applies `ITOF12`, and multiplies by Q. Neither direct path negates V. GS
normalized coordinates are:

```text
U = (S/Q) * texture_width
V = (T/Q) * texture_height
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

For a record with `P` packets, seek to
`record.directory_offset_and_flags >> 16` within the object's owned
CoreStream range:

```text
u32 packet_key[P]             # semantics unknown
u32 packet_chunk_offset[P]
u8  packet_chunk_index[P]
align 4
u8  packet_metadata_a[P]      # semantics unknown
align 4
u8  packet_metadata_b[P - 1]  # semantics unknown
align 4
zero padding to the next record directory or auxiliary area
```

`packet_chunk_index` is proven: adding the selected chunk's cumulative start
to `packet_chunk_offset` lands on the corresponding DMA `RET` packet for all
29,680 entries. Packet offsets are 16-byte aligned, may reset to zero in a
later chunk, and must be bounded by that chunk rather than by directory order
alone.

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
Q = align_up(N, 4)
C = packet has a V4-8u color/prelight tail
```

All 29,680 retail packet headers satisfy:

```text
header_a = {
    N,
    3 * N,
    3 * Q,
    G,
}

header_b = {
    v3_start       = 0x0B5 + 6 * G,
    normal_scratch = 0x0B5 + Q,
    tail_base      = max(0x0B5 + 2 * Q, v3_start + 2 * P),
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
qword_offset = control & 0x7FFF
assert qword_offset % 3 == 0
target = qword_offset // 3
adc = control >> 15
```

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
GS_ST = (u * Q, v * Q, Q, Q)
```

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
byte-distinct matrices. Their finite, nonsingular, overwhelmingly
scaled-orthogonal shape strongly supports row-vector global inverse-bind
transforms, but scale/counterexample cases still block calling that semantics
verified. Critically, the active skinned-palette path does not read the
patched bone `+0x18` transform-pool index, dereference the global pool, or
perform a post-hierarchy inverse-bind multiply. These interned transforms are
not part of the selector lookup.

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
i = category_order[k]
scratch[i] = L[i]
```

The category-order table is the stable sort of source indices by
`(category, source_index)`; the serialized `bytes_b` and `bytes_d` arrays are
identical copies of that permutation. Local-matrix inputs are:

| Category | Quaternion | Translation | Scale |
|---:|---|---|---|
| 0 | pose record `+0x00` | bone record `+0x00` | unit |
| 1 | pose record `+0x00` | pose record `+0x10` | unit |
| 2 | pose record `+0x00` | pose record `+0x10` | pose record `+0x20` |

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
Runtime mapping compares BoneMap track names with Skeleton bone names and
emits per-track bone mappings with `0xFF` sentinels; track order is BoneMap
order, not Skeleton order. Shared animation-bank Cores often contain no
Skeleton, so never bind by local object order.

### 4.22 Remaining model work

The generic framing, stream ownership, regular-mesh material bindings,
embedded static meshes, skinned packet directory, transform palette, and
static/skinned geometry decoding, placement instances, and Multi/LOD wrapper
graphs are now bounded. Skeleton packing, indexed hierarchy, collision hulls,
attachments, bone sets, BoneMaps, and animation-track boundaries are also
bounded. Remaining model work includes:

- material roles for signed coordinate pairs beyond proven UV0/UV1 and
  material-specific color/alpha continuations;
- the semantic role of the serialized global transform pool, meanings of the
  bone XYZ, retained six-float bounds, categories/word tables, and attachment
  transform locality;
- compressed animation-channel decoding, case-comparison behavior, and the
  external resource path that selects a Skeleton for shared animation banks;
- proximity-parent transform composition, blend-shape/linked-static behavior,
  conservative skinned bounds, and collision/render-mesh relationships;
- the caller-supplied value for the final static VIF unpack and the exact
  static VU program;
- the scene path that selects LightZones and populates the six cubic-light
  coefficient qwords;
- native handedness, up axis, front-face convention, and unit scale.

Keep source-space values, transform selectors, packet groups, and strip state
available and make Blender conversion an explicit layer. A GS capture remains
a validation oracle, not a complete extractor: it sees post-VU triangles and
baked lighting, not necessarily source normals, weights, skeleton data, or
authoring topology.

No further selector watchpoint is required. A remaining static trace should
identify the value supplied to VU address `0x1EE`; separately, the generic
state-upload descriptor between the completed camera cache and VU1
`TOP+0x1E4` can close the last non-selector matrix-upload edge.

Native export remains blocked until the remaining animation/transform
semantics and all edited stream layouts round-trip safely.

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

## 6. `.mss` is audio

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

## 7. What the ELF can provide

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

## 8. Recommended importer architecture

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
  BoneMaps and base-animation linkage
  exact opaque track-boundary walk

LevelDecoder
  compact zones, portals, visibility tables, and area volumes
  direct static roots, wrapper definitions, and mesh instances
  collision/paths/entities
  script and opaque-record preservation
```

All readers should be bounded and immutable. Unknown records should carry
their original bytes, source path, package, archive offset, and digest.
Editing one understood object must not silently discard adjacent unknown
objects from the same compound lump.

## 9. Validation gates

### Archive

- Parse every record exactly once.
- Confirm file count, case-insensitive uniqueness, and package bounds.
- Re-read representative prefixes directly from both ISO and extracted DAT
  sources and compare hashes.

### Texture

- Decode all 19 proven standalone samples identically.
- Reproduce the 47-level totals: 12,613 physical Texture objects, 12,566
  logical Core-local tokens, 273,141,504 payload bytes, and the exact
  per-format counts in section 3.5.
- Reproduce the descriptor-table sampling PSMs and the large-buffer
  PSMT8-to-PSMCT32 and PSMT4-to-PSMCT16 upload aliases without adding a
  CPU-side transform.
- Parse `RGBA_8888` as packed RGBA bytes and `RGB_888` as packed RGB bytes;
  do not swizzle, rescale alpha, or claim a stored RGB888 alpha channel.
- Require each streamed chunk sum to equal its directory-owned CoreStream
  size. Recompute every mip byte count, require `0x100` pixel-storage
  alignment, and require every alignment byte to be zero.
- Parse all 33,084 serialized span records, require identical halves, and
  reconstruct all 12,609 valid raw mip prefixes byte-for-byte from the first
  16,542 runtime-consumed records.
- Reproduce the exact PSMT8 and PSMT4 equations over all 98 validated
  dimension/format cases and 4,129,024 texels, including low-nibble-first
  PSMT4 and row-major native 8x8 mips.
- Reproduce all 3,034 complementary-alpha `INDEX_4` palettes and all 6,320
  standard `INDEX_8` palettes byte-for-byte.
- Reproduce ALPHA_4/8 normal and complement palette equations, the 256-entry
  CSM1 permutation, and CPSM/CSM/CSA/CLD = PSMCT32/CSM1/0/1.
- Reproduce the GLOW lower-half zeros, upper-half alpha ramp, reversed RGB
  source walk, and CSM1 upload permutation without assigning semantics to the
  dynamic source buffer.
- Reproduce Street1's inline `0x01F67930` framing without consuming its first
  payload word as a palette index or its footer as pixel data.
- Scope Texture tokens to their Core, retain all 47 equivalent duplicate
  copies, and reject any duplicate group whose metadata or payload differs.
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
  ELF-derived boundary walk to consume all 1,589,420 opaque track records
  exactly.
- Validate strip winding and restart/degenerate handling. Require direct
  STQ0/STQ1 to preserve V, model GS linear neighbors from `U-0.5/V-0.5`, and
  apply exactly one host V conversion. Validate Skeleton
  hierarchy/transform-pool semantics, source-order palette composition, LOD
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
- Require compact-section prefixes, directory-to-object alignment, mandatory
  zero object prefixes, blob-size equations, repeated visibility counts, and
  all zero padding to match.
- Resolve Zone member IDs, visibility Zone IDs, LightZone light IDs, and
  Portal ZoneIDs in their separate, type-checked per-Core domains.
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

## 10. References

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
