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

The current evidence covers positive dimensions divisible by 16 and a storage
stride equal to `width`. A general GS decoder must instead honor the texture
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

### 3.4 Other observed texture shapes

The first Texture record in several compound variant-5 lumps exposes these
chunk-size patterns:

| Dimensions | Index bytes | Palette/aux bytes | Strong inference |
|---:|---:|---:|---|
| 64x64 | `0x1000` | `0x500` | PSMT8 + 256-entry CT32 CLUT |
| 128x128 | `0x4000` | `0x500` | PSMT8 + 256-entry CT32 CLUT |
| 256x256 | `2 x 0x8000` | `0x500` | PSMT8 + 256-entry CT32 CLUT |
| 512x256 | `4 x 0x8000` | `0x500` | PSMT8 + 256-entry CT32 CLUT |
| 128x64 | `0x1000` | `0x100` | PSMT4 candidate + 16-entry CT32 CLUT |

The size relationships are compelling (`width * height` for PSMT8 and half
that for PSMT4), but runtime `TEX0` capture must confirm the PSM, CPSM, CSM,
CSA, TBW, mip level, and alpha rules before these streamed cases are declared
decoded.

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

### 3.5 Blender texture policy

- Keep stored indices, raw palette, decoded RGBA, unknown auxiliary data, and
  unknown footer as separate fields.
- Record PSM/CPSM/CSM/CSA/TBW and mip metadata when known.
- Mark color textures as sRGB and masks/data textures as non-color only from
  material semantics, never from filename alone.
- Keep alpha unchanged for file-decoded RGBA. Apply GS alpha expansion only to
  values captured from GS memory under the corresponding `TEXA` behavior.
- Do not flip both pixels and UVs. Establish the native UV origin from a
  runtime mesh/texture pair, then perform exactly one documented conversion.
- Export remains blocked until swizzle, palette, mip, padding, and unknown
  fields round-trip byte-identically.

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
not-yet-classified data. Let `S` be the start of a validated typed section,
`P` its property count, and `N` its object count:

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
         serialized object blobs, concatenated in directory order
```

The exact framing invariant in the retail data is:

```text
data_offset = 0x1C + 0x10 * P + 0x20 * N
```

In other words, `data_offset` includes the five header words, property
descriptors, two prefix words, and the complete object directory. Strict
scanning also validates the type/property string indices, both prefix words,
all declared object sizes, and Core/CoreStream bounds. Merely finding a
plausible type index or accepting arbitrary zero-property candidates produces
false positives.

Each property descriptor is four `u32` words. The first is a Core string-table
index and the second is a four-byte property type tag. The last two carry
flags, size/default information, or a runtime property offset depending on the
type. For example, `General.Skeleton` advertises runtime offset `0x24`, while
its serialized value in the skinned object is at `+0x28`; runtime reflection
offsets must not be used directly as file offsets.

The fourth section-header word is consistently:

- `0` for `RegularStaticMeshResource` and
  `RegularSkinnedMeshResource`;
- `1` for `Skeleton`, `Material`, and both `Multi*MeshResource` types;
- `2` for `LodStaticMeshResource` and `LodSkinnedMeshResource`.

This is strongly consistent with inheritance depth, but the engine's exact
field name is not yet proven.

### 4.3 Object directory, stream ownership, and references

Each 32-byte object-directory entry is:

| Entry offset | Type | Meaning |
|---:|---|---|
| `0x00` | `u32` | Serialized object-blob size |
| `0x04` | `u32` | Object ID/reference token |
| `0x08` | `u32[4]` | Stable identity/GUID-like words; exact algorithm unknown |
| `0x18` | `u32` | Absolute offset in the paired CoreStream |
| `0x1C` | `u32` | Owned CoreStream byte size |

Object blobs begin at `S + data_offset` and are concatenated using their
declared sizes. The stream offset is relative to the start of the complete
CoreStream entry, including its 64-byte header. Objects without stream data
normally carry size zero and an offset at the end of the CoreStream. Treat an
entry's `[stream_offset, stream_offset + stream_size)` as its ownership bound;
do not infer ownership from the next DMA-looking signature.

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
skinned-object value at `+0x28` resolves to a `Skeleton` entry for 1,233 of
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
| `0x11C` | `u8[0x44]` | Root property-value block |
| `0x138` | `f32` | `Lighting.Lightmap Density Factor` |
| `0x13C` | `f32` | `Visibility.Maximum visible distance` |
| `0x144` | `f32[6]` | Resource bounding box: min XYZ, max XYZ |
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
    end_of_attribute_arrays_in_VU_memory,
}

header_b = {
    position_VU_address,       # always 0xB7 here
    normal_ADC_VU_address,
    color_or_UV_VU_address,
    UV_VU_address,
}
```

Each present attribute reserves one VU quadword per vertex, so successive VU
addresses differ by `vertex_count`. Without the optional V4-8 stream, the
last two addresses are equal. `header_a[3]` equals
`UV_VU_address + vertex_count`.

Starting at packet offset `0x30`, the VIF sequence is:

| Order | VIF opcode | Stored element | Bytes/vertex |
|---:|---|---|---:|
| 1 | `0x69`, `UNPACK V3-16` | `s16 position[3]` | 6 |
| 2 | `0x6F`, `UNPACK V4-5` | packed normal XYZ plus ADC bit | 2 |
| 3, optional | `0x6E`, `UNPACK V4-8` | prelight/color candidate | 4 |
| Last | `0x65`, `UNPACK V2-16` | `s16 UV[2]` | 4 |

Each command has `NUM == vertex_count` and targets the corresponding address
from `header_b`. Its payload is rounded up to the next 4-byte command
boundary. The optional V4-8 stream is present in exactly the three packets
whose mesh-record flags include `0x10`.

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

#### UVs and optional color

The UV stream is signed. A scale of `1 / 4096` is strongly supported; the
planar four-vertex sample contains exact `0x1000` and `-0x2000` landmarks.
Native V orientation is still unresolved, so do not also flip the texture.

The optional V4-8 stream is strongly consistent with prelit GS-range RGBA:
`custom.mortarshell` is uniformly `{0x80, 0x80, 0x80, 0x80}`, while
`level.7.1` contains equal RGB grayscale values and a varying fourth channel.
Preserve these bytes. Do not silently normalize them as ordinary 0-255 sRGB;
`0x80` commonly represents full intensity in the GS vertex-color path.

### 4.9 Embedded static resources

The static custom block begins at serialized object offset `+0x44`, not at an
absolute Core-body location. The leading-root address `body + 0x160` is simply
`body + 0x11C + 0x44`. To parse an embedded object:

1. take its blob from the typed section's object directory;
2. parse the custom block at object `+0x44`;
3. start its chunks at the directory entry's absolute CoreStream offset;
4. reject the object unless the chunk sum equals its directory stream size;
5. resolve every mesh-record token to a `Material` in the same Core.

`level.11.sdplatform_templar` contains seven separate
`RegularStaticMeshResource` sections, including one section with 34 objects.
The directory-owned parse validates 40 static objects, 357 mesh records,
2,251 DMA packets, 114,912 strip vertices, and 62,014 emitted triangles in
that one level. Every record resolves to a material, and the same bounds,
position, normal/ADC, optional color, UV, and triangle-count invariants used
for the leading roots hold. This closes the embedded-static discovery and
stream-ownership gap; `MultiStaticMeshResource`, `LodStaticMeshResource`, and
instance transforms are still separate wrapper formats.

### 4.10 Regular skinned Core object

The serialized `RegularSkinnedMeshResource` property block stores its nullable
`Skeleton` object ID at `+0x28`. Its PS2 custom block starts at `+0x44`:

```text
object+0x44  u32 chunk_count
             u32 chunk_size[chunk_count]
             u32 mesh_record_count
             skinned_record record[mesh_record_count]  # 16 bytes each
             u32 packed_resource_flags
             [u32 auxiliary_offset]                    # present in 1,215/1,244
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

The auxiliary offset is serialized for 1,215 objects and omitted in 29. When
omitted, it can be recovered uniquely from the aligned end of the final
record directory, the transform-record count, packet group counts, first
packet offset, and required zero padding. Do not assume that simply rounding
the minimum directory size to 16 or 32 bytes is sufficient in every object.

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
    f32 vector_a[3]
    u32 selector_or_key
    f32 vector_b[3]
    u32 zero
```

These records are strongly associated with the grouped skinned vectors and
look like per-selector decode/transform parameters. The fourth word is not
safe to use directly as a zero-based Skeleton bone index: it exceeds the
simple Skeleton count in many otherwise valid retail objects. Preserve the
two vectors and selector until the runtime lookup is traced.

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
index were validated while recovering the 29 omitted auxiliary offsets.

### 4.12 Skinned DMA/VIF packets and topology

Skinned packets use the same callable DMA prefix as static packets:

```text
0x60000000 OR QWC, 0x80000000,
STCYCL 4,4, UNPACK V4-32u count=2 address=0x1D9
```

The two header vectors have the following verified relationships:

```text
header_a = {
    output_strip_vertex_count,              # N
    3 * N,
    3 * align_up(N, 4),
    transform_group_count,                  # G
}

header_b = {
    scratch_address_0,
    scratch_address_1,
    tail_attribute_address,
    tail_attribute_address,
}
```

The remaining address semantics depend on the VU program. After the header,
the common sequence is:

1. `STMASK`, then `STCYCL 1,6`;
2. masked unsigned `S8` unpack of `G` group sizes to address `0x0BA`;
3. `STCYCL 4,4`;
4. signed `V3-8` unpack of `M` vectors;
5. masked unsigned `S16` unpack of `M` control words to the same VU address;
6. zero or more signed `V2-16`/`V4-16` attribute unpacks and an optional
   unsigned `V4-8` unpack.

The mask causes the S16 scalar to supply the W lane for the V3 XYZ value. For
every retail packet:

```text
M = 2 * sum(S8_group_size[0:G])
```

Each S16 control word is:

```python
target = (control & 0x7FFF) // 3
adc = (control >> 15) & 1
```

The low 15 bits are always divisible by three. After duplicate targets are
coalesced, the targets cover every integer from `0` through `N - 1`; each
target appears once or twice and duplicate entries always agree on ADC.
Across the full corpus:

- 1,661,564 output strip vertices were covered;
- 574,074 targets appeared once and 1,087,490 appeared twice;
- the record triangle count equals the number of unique targets with
  `adc == 0` for every one of the 2,386 records;
- the aggregate ADC-clear count is exactly 1,029,286.

This proves skinned strip topology without guessing an index buffer. It does
not yet prove whether repeated V3 records are position/normal pairs,
preweighted transform contributions, or another VU-specific representation.

### 4.13 Skinned tail attributes

The 29,680 packet tails fall into nine exact VIF-unpack shapes:

| Packet count | Tail after V3/S16 |
|---:|---|
| 20,558 | `V2-16` |
| 4,877 | `V4-16` |
| 1,318 | `V2-16`, `V4-8u` |
| 931 | `V4-16`, `V4-8u` |
| 852 | `V4-16`, `V2-16` |
| 564 | `V4-16`, `V2-16`, `V4-8u` |
| 440 | `V4-16`, `V4-16`, `V4-8u` |
| 88 | `V4-8u` |
| 46 | `V4-16`, `V4-16` |
| 6 | no tail attribute unpack |

The signed V2 stream is strongly consistent with one fixed-point
texture-coordinate pair. A signed V4 stream naturally carries two such pairs,
and packets can carry up to four candidate pairs, but material-layer tracing
is still required before naming every lane as a UV set. Preserve the signed
values; `/4096` is the leading scale candidate shared with static UVs.

The optional unsigned V4-8 stream matches the static prelight/color shape and
is a strong prelight candidate. Preserve raw GS-range bytes and do not
normalize them as ordinary 0-255 sRGB without material evidence.

### 4.14 Bounded Skeleton observations

The typed-section scan finds 416 `Skeleton` objects. Their schema includes
`General.Hulls` at runtime offset `0x38` and `General.Bone Sets` at `0x44`,
but object layouts vary substantially and must not be inferred from one
character.

In `entity.helghast.3rd.mrl`, the `Skeleton` object:

- has object ID `0x0053D72E`, serialized size `0x820`, and no stream payload;
- stores `21` at serialized `+0x08` and has 21 trailing names, from
  `Root_Bone` through the case- and bullet-ejection names;
- contains 17 consecutive 0x50-byte sentinel records at `+0x160`.

The observed 0x50-byte record is:

```text
+0x00  f32  scale_or_weight       # 1.0 in all 17 sample records
+0x04  u32  name_or_hull_key
+0x08  u32  owner_or_parent_candidate
+0x0C  u32  0xDEADBEEF
+0x10  u32  0xDEADC0DE
+0x14  f32  basis[3][4]           # padded three-vector basis
+0x44  f32  translation[3]
```

Most records form an affine basis plus translation and are strongly
consistent with optional hull/attachment transforms exposed by
`General.Hulls`, not automatically with the skeleton bind pose. There are
more trailing names than sentinel records, and the `+0x04` key is not a
direct byte offset into the raw name tail. A safe importer may expose these
records and names as opaque diagnostics, but must not yet manufacture a bone
hierarchy from them.

### 4.15 Remaining model work

The generic framing, stream ownership, regular-mesh material bindings,
embedded static meshes, skinned packet directory, transform palette, and
static/skinned ADC topology are now bounded. Remaining model work includes:

- exact skinned V3-8 vector semantics, transform-table math, and any implicit
  weight/normal reconstruction performed by the VU program;
- Skeleton name/key mapping, hierarchy, bind and inverse-bind transforms,
  bone sets, hull ownership, and animation-track linkage;
- `Multi*MeshResource`, `Lod*MeshResource`, instance transforms, blend shapes,
  and collision/render-mesh relationships;
- the caller-supplied value for the final static VIF unpack and the exact
  static/skinned VU programs;
- native handedness, up axis, front-face convention, transform order, and
  unit scale.

Keep source-space values, transform selectors, packet groups, and strip state
available and make Blender conversion an explicit layer. A GS capture remains
a validation oracle, not a complete extractor: it sees post-VU triangles and
baked lighting, not necessarily source normals, weights, skeleton data, or
authoring topology.

The next runtime trace should follow one skinned packet's S8 group palette
through the VU loop, identify how each 32-byte transform record acts on its
V3-8 vectors, and compare the resulting positions and normals with a GS
capture. A separate static trace should identify the value supplied to VU
address `0x1EE`.

Native export remains blocked until those transform and Skeleton rules and
all edited stream layouts round-trip safely.

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

### 5.2 Embedded level objects

A representative campaign level's Core string table identifies:

- `Zone`, zone ID, zone static-mesh list, zone bounds, and filename
- `Portal`, polygon, active flag, front-zone ID, and back-zone ID
- `RenderZone`, `LightZone`, `AmbientSoundZone`, and `ZoneVisibilityInfo`
- `StaticMeshInstance`, skinned mesh instances, and LOD resources
- collision instances, poly soups, boxes, multi-resources, and collision rays
- paths/path instances, AI waypoint/terrain managers, ladders, vault/crawl/
  peek/play-animation areas
- lights, particles, sounds, pickups, weapons, destructibles, effects, and
  general game entities

Therefore "level format" is a compound Core object graph plus a CoreStream,
not a geometry-only file. A useful Blender import can be layered:

1. zones, portal polygons, bounds, and transforms;
2. static/skinned render instances and materials;
3. collision and path/AI debug geometry;
4. lights, audio zones, effects, and entity empties with preserved properties;
5. mission-script references and other opaque gameplay records.

Custom-level export is not credible until object identity/reference rules,
zone/portal visibility, collision, entity lifecycle, and mission scripts are
understood. A visually correct mesh-only scene is not a round-trippable level.

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
  object/reference table (next research gate)
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
  DMA/VIF strip and packed-vertex decode
  embedded static/skinned/LOD resources
  materials/skeleton/collision

LevelDecoder
  zones/portals/instances
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
- Compare native decoding with a GS-memory capture for the same texture.
- Add PSMT4 only after a captured `TEX0` confirms it.
- Test non-square dimensions, TBW greater than logical width, all CLUT modes,
  alpha, mip levels, and malformed chunk tables.

### Model

- Reproduce the seven-resource corpus totals: 9 records, 17 packets, 709 strip
  vertices, and 415 emitted triangles.
- Require every packet/record quantized bound to equal its decoded extrema.
- Require each `RET` transfer length to stay within its directory-derived
  packet and declared stream chunk.
- Match CoreStream source ranges and the pending final unpack to one runtime
  VIF submission.
- Match source, runtime, and imported floating-point bounds and primitive
  counts.
- Validate strip winding, restart/degenerate handling, UV orientation,
  normals, LODs, skeleton hierarchy, bind pose, and weights independently.

### Level

- Match zone and portal counts/IDs/bounds against a runtime dump.
- Preserve cross-object references and unknown records.
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
