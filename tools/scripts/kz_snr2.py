"""Inspect, relocate, and bundle Killzone's SN ProDG SNR2 modules.

Retail inputs and generated bundles must remain under ignored work/. The
implementation follows the SNR2 metadata and relocation behavior documented
by OpenRAC and the public BSD-3-Clause ProDG loader by emoose.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from dataclasses import dataclass
from pathlib import Path
from typing import Mapping, Sequence


ROOT = Path(__file__).resolve().parents[2]
WORK_ROOT = (ROOT / "work").resolve()

MAGIC = 0x32524E53
HEADER = struct.Struct("<15I")
RELOCATION = struct.Struct("<IIi")
SYMBOL = struct.Struct("<IIHBB")
ELF_HEADER = struct.Struct("<16sHHIIIIIHHHHHH")
PROGRAM_HEADER = struct.Struct("<8I")
SECTION_HEADER = struct.Struct("<10I")
POINTER_FIELDS = (0x04, 0x0C, 0x14, 0x18, 0x1C, 0x20, 0x34, 0x38)
SUPPORTED_RELOCATION_TYPES = frozenset((2, 4, 5, 6))


@dataclass(frozen=True)
class Symbol:
    name: str
    address: int
    auxiliary: int
    kind: int
    flags: int
    name_offset: int


@dataclass(frozen=True)
class NamedRelocation:
    site: int
    kind: int
    symbol_index: int
    addend: int


@dataclass(frozen=True)
class PackedRelocation:
    kind: int
    site: int
    low_site: int | None = None


@dataclass(frozen=True)
class Snr2Module:
    path: Path
    data: bytes
    header: tuple[int, ...]
    symbols: tuple[Symbol, ...]
    named_relocations: tuple[NamedRelocation, ...]
    packed_relocations: tuple[PackedRelocation, ...]

    @property
    def alignment(self) -> int:
        return self.header[10]

    @property
    def image_size(self) -> int:
        return self.header[1]

    @property
    def exports(self) -> tuple[Symbol, ...]:
        return tuple(symbol for symbol in self.symbols if symbol.kind == 2 and symbol.name)

    @property
    def imports(self) -> tuple[Symbol, ...]:
        return tuple(symbol for symbol in self.symbols if symbol.kind == 1 and symbol.name)

    @property
    def indexed_exports(self) -> tuple[int, ...]:
        table_offset = self.header[8]
        count = self.header[9]
        return (
            struct.unpack_from(f"<{count}I", self.data, table_offset)
            if count
            else ()
        )


@dataclass(frozen=True)
class ElfSegment:
    offset: int
    vaddr: int
    paddr: int
    filesz: int
    memsz: int
    flags: int
    alignment: int


@dataclass(frozen=True)
class ElfImage:
    data: bytes
    header: tuple[object, ...]
    segments: tuple[ElfSegment, ...]

    @property
    def entry(self) -> int:
        return int(self.header[4])

    @property
    def program_header_offset(self) -> int:
        return int(self.header[5])

    @property
    def program_header_entry_size(self) -> int:
        return int(self.header[9])

    def vaddr_to_offset(self, address: int, size: int = 1) -> int:
        for segment in self.segments:
            delta = address - segment.vaddr
            if 0 <= delta and delta + size <= segment.filesz:
                return segment.offset + delta
        raise ValueError(f"ELF address 0x{address:08X} is not file-backed")

    def offset_to_vaddr(self, offset: int, size: int = 1) -> int:
        for segment in self.segments:
            delta = offset - segment.offset
            if 0 <= delta and delta + size <= segment.filesz:
                return segment.vaddr + delta
        raise ValueError(f"ELF offset 0x{offset:X} is not in a load segment")


@dataclass(frozen=True)
class ModulePlacement:
    module: Snr2Module
    base: int


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def read_c_string(data: bytes, offset: int, limit: int, description: str) -> str:
    if offset < 0 or offset >= limit:
        raise ValueError(f"{description} offset 0x{offset:X} is out of range")
    end = data.find(b"\0", offset, min(limit, offset + 1024))
    if end < 0:
        raise ValueError(f"{description} at 0x{offset:X} is not terminated")
    return data[offset:end].decode("ascii", "strict")


def unpack_relocation(data: bytes, offset: int) -> NamedRelocation:
    site, info, addend = RELOCATION.unpack_from(data, offset)
    return NamedRelocation(site, info & 0xFF, info >> 8, addend)


def decode_packed_relocations(data: bytes, start: int, payload_end: int) -> tuple[PackedRelocation, ...]:
    if start == 0:
        return ()
    if not 0 < start < len(data):
        raise ValueError("packed relocation stream starts outside the module")

    def next_site(cursor: int, site: int) -> tuple[int, int, int, bool]:
        if cursor >= len(data):
            raise ValueError("packed relocation stream is unterminated")
        descriptor = data[cursor]
        cursor += 1
        kind = descriptor & 3
        delta = descriptor & 0xFC
        if delta == 0:
            if cursor + 4 > len(data):
                raise ValueError("packed relocation absolute offset is truncated")
            site = struct.unpack_from("<I", data, cursor)[0]
            cursor += 4
            if site == 0:
                return kind, site, cursor, True
        else:
            site += delta
        if site < HEADER.size or site + 4 > payload_end or site & 3:
            raise ValueError(f"packed relocation site 0x{site:X} is invalid")
        return kind, site, cursor, False

    result: list[PackedRelocation] = []
    cursor = start
    site = 0
    seen_sites: set[int] = set()
    while True:
        kind, site, cursor, done = next_site(cursor, site)
        if done:
            if any(data[cursor:]):
                raise ValueError("nonzero bytes follow the packed relocation terminator")
            return tuple(result)
        if site in seen_sites:
            raise ValueError(f"duplicate packed relocation site 0x{site:X}")
        seen_sites.add(site)
        low_site = None
        if kind == 1:
            look_cursor = cursor
            look_site = site
            while True:
                look_kind, look_site, look_cursor, look_done = next_site(look_cursor, look_site)
                if look_done:
                    raise ValueError(f"packed HI16 at 0x{site:X} has no following LO16")
                if look_kind == 0:
                    low_site = look_site
                    break
        result.append(PackedRelocation(kind, site, low_site))


def parse_module(path: Path, data: bytes | None = None) -> Snr2Module:
    source = path.read_bytes() if data is None else data
    if len(source) < HEADER.size:
        raise ValueError(f"{path}: truncated SNR2 header")
    header = HEADER.unpack_from(source)
    (
        magic,
        relocation_offset,
        relocation_count,
        symbol_offset,
        symbol_count,
        _image_name,
        _constructors,
        _destructors,
        export_table,
        export_count,
        alignment,
        declared_size,
        _unknown_30,
        packed_offset,
        _unknown_38,
    ) = header
    if magic != MAGIC:
        raise ValueError(f"{path}: missing SNR2 magic")
    if declared_size != len(source):
        raise ValueError(
            f"{path}: declared size 0x{declared_size:X} does not match 0x{len(source):X}"
        )
    if alignment == 0 or alignment & (alignment - 1):
        raise ValueError(f"{path}: invalid alignment 0x{alignment:X}")
    relocation_end = relocation_offset + relocation_count * RELOCATION.size
    symbol_end = symbol_offset + symbol_count * SYMBOL.size
    if relocation_offset < HEADER.size or relocation_end != symbol_offset:
        raise ValueError(f"{path}: invalid named relocation table extent")
    if symbol_count == 0 or symbol_end > len(source):
        raise ValueError(f"{path}: invalid symbol table extent")
    if packed_offset and packed_offset < symbol_end:
        raise ValueError(f"{path}: packed relocation stream overlaps the symbol table")
    if export_count:
        export_end = export_table + export_count * 4
        if export_table < HEADER.size or export_end > len(source):
            raise ValueError(f"{path}: invalid indexed export table extent")
        for index, address in enumerate(
            struct.unpack_from(f"<{export_count}I", source, export_table)
        ):
            if address < HEADER.size or address >= relocation_offset or address & 3:
                raise ValueError(
                    f"{path}: indexed export {index} has invalid address 0x{address:X}"
                )

    symbols: list[Symbol] = []
    for index in range(symbol_count):
        entry_offset = symbol_offset + index * SYMBOL.size
        name_offset, address, auxiliary, kind, flags = SYMBOL.unpack_from(source, entry_offset)
        if kind not in (0, 1, 2):
            raise ValueError(f"{path}: symbol {index} has unsupported kind {kind}")
        if name_offset < HEADER.size or name_offset >= relocation_offset:
            raise ValueError(f"{path}: symbol {index} has invalid name offset")
        if address >= relocation_offset:
            raise ValueError(f"{path}: symbol {index} has invalid address 0x{address:X}")
        name = read_c_string(source, name_offset, relocation_offset, f"symbol {index} name")
        if kind == 1 and address != 0:
            raise ValueError(f"{path}: import {name!r} has a nonzero address")
        if kind == 2 and (not name or address == 0):
            raise ValueError(f"{path}: export symbol {index} is incomplete")
        symbols.append(Symbol(name, address, auxiliary, kind, flags, name_offset))

    named_relocations: list[NamedRelocation] = []
    for index in range(relocation_count):
        relocation = unpack_relocation(source, relocation_offset + index * RELOCATION.size)
        if relocation.site < HEADER.size or relocation.site + 4 > relocation_offset or relocation.site & 3:
            raise ValueError(f"{path}: named relocation {index} has invalid site")
        if relocation.kind not in SUPPORTED_RELOCATION_TYPES:
            raise ValueError(
                f"{path}: named relocation {index} has unsupported type {relocation.kind}"
            )
        if relocation.symbol_index >= symbol_count:
            raise ValueError(
                f"{path}: named relocation {index} references symbol "
                f"{relocation.symbol_index}, but only {symbol_count} exist"
            )
        named_relocations.append(relocation)

    try:
        packed_relocations = decode_packed_relocations(source, packed_offset, relocation_offset)
    except ValueError as error:
        raise ValueError(f"{path}: {error}") from error
    return Snr2Module(
        path,
        source,
        header,
        tuple(symbols),
        tuple(named_relocations),
        packed_relocations,
    )


def patch_word(word: int, kind: int, target: int) -> int:
    target &= 0xFFFFFFFF
    if kind == 2:
        return target
    if (word & 0xFC00003F) == 0x0D:
        raise ValueError("relocation targets a break instruction")
    if kind == 4:
        return (word & 0xFC000000) | ((target >> 2) & 0x03FFFFFF)
    if kind == 5:
        return (word & 0xFFFF0000) | (((target + 0x8000) >> 16) & 0xFFFF)
    if kind == 6:
        return (word & 0xFFFF0000) | (target & 0xFFFF)
    raise ValueError(f"unsupported MIPS relocation type {kind}")


def relocate_module(
    placement: ModulePlacement,
    external_symbols: Mapping[str, int],
    *,
    allow_unresolved: bool = False,
) -> bytes:
    module = placement.module
    base = placement.base
    if base < 0 or base + module.image_size > 0x1_0000_0000:
        raise ValueError(f"{module.path}: placement exceeds the 32-bit address space")
    if base % module.alignment:
        raise ValueError(
            f"{module.path}: base 0x{base:08X} is not aligned to 0x{module.alignment:X}"
        )
    missing = sorted(
        symbol.name
        for symbol in module.imports
        if not external_symbols.get(symbol.name)
    )
    if missing and not allow_unresolved:
        raise ValueError(f"{module.path}: unresolved imports: {', '.join(missing)}")

    image = bytearray(module.data)
    for field_offset in POINTER_FIELDS:
        value = struct.unpack_from("<I", module.data, field_offset)[0]
        if value:
            struct.pack_into("<I", image, field_offset, base + value)
    struct.pack_into("<I", image, 0x30, 0)

    symbol_values: list[int | None] = []
    symbol_offset = module.header[3]
    for index, symbol in enumerate(module.symbols):
        if symbol.kind in (0, 2):
            value = base + symbol.address
        else:
            value = external_symbols.get(symbol.name)
        entry_offset = symbol_offset + index * SYMBOL.size
        struct.pack_into("<I", image, entry_offset, base + symbol.name_offset)
        if value is not None:
            struct.pack_into("<I", image, entry_offset + 4, value)
        image[entry_offset + 11] = 1 if symbol.kind == 2 else 0
        symbol_values.append(value)

    for relocation in module.packed_relocations:
        word = struct.unpack_from("<I", image, relocation.site)[0]
        if relocation.kind == 0:
            kind = 6
            target = base + (word & 0xFFFF)
        elif relocation.kind == 1:
            if relocation.low_site is None:
                raise ValueError("packed HI16 relocation is missing its LO16 pair")
            low = struct.unpack_from("<I", image, relocation.low_site)[0] & 0xFFFF
            if low & 0x8000:
                low -= 0x10000
            kind = 5
            target = base + ((word & 0xFFFF) << 16) + low
        elif relocation.kind == 2:
            kind = 4
            target = base + ((word & 0x03FFFFFF) << 2)
        else:
            kind = 2
            target = base + word
        struct.pack_into("<I", image, relocation.site, patch_word(word, kind, target))

    relocation_offset = module.header[1]
    for index, relocation in enumerate(module.named_relocations):
        entry_offset = relocation_offset + index * RELOCATION.size
        struct.pack_into("<I", image, entry_offset, base + relocation.site)
        word = struct.unpack_from("<I", image, relocation.site)[0]
        symbol_value = symbol_values[relocation.symbol_index]
        if symbol_value is None:
            continue
        target = symbol_value + relocation.addend
        struct.pack_into(
            "<I",
            image,
            relocation.site,
            patch_word(word, relocation.kind, target),
        )
    return bytes(image[: module.image_size])


def parse_elf(data: bytes) -> ElfImage:
    if len(data) < ELF_HEADER.size:
        raise ValueError("truncated ELF header")
    header = ELF_HEADER.unpack_from(data)
    ident = bytes(header[0])
    if ident[:4] != b"\x7fELF" or ident[4] != 1 or ident[5] != 1:
        raise ValueError("expected a 32-bit little-endian ELF")
    if int(header[2]) != 8:
        raise ValueError("expected a MIPS ELF")
    program_offset = int(header[5])
    entry_size = int(header[9])
    entry_count = int(header[10])
    if entry_size < PROGRAM_HEADER.size:
        raise ValueError("ELF program header entries are too small")
    if program_offset + entry_size * entry_count > len(data):
        raise ValueError("ELF program header table is truncated")

    segments: list[ElfSegment] = []
    for index in range(entry_count):
        values = PROGRAM_HEADER.unpack_from(data, program_offset + index * entry_size)
        kind, offset, vaddr, paddr, filesz, memsz, flags, alignment = values
        if kind != 1:
            continue
        if filesz > memsz or offset + filesz > len(data):
            raise ValueError(f"ELF load segment {index} is invalid")
        segments.append(ElfSegment(offset, vaddr, paddr, filesz, memsz, flags, alignment))
    if not segments:
        raise ValueError("ELF contains no load segments")
    return ElfImage(data, header, tuple(segments))


def find_linked_loader_symbols(elf: ElfImage) -> tuple[int, tuple[Symbol, ...]]:
    candidates: list[tuple[int, tuple[Symbol, ...]]] = []
    magic = struct.pack("<I", MAGIC)
    cursor = 0
    while True:
        offset = elf.data.find(magic, cursor)
        if offset < 0:
            break
        cursor = offset + 1
        try:
            header_address = elf.offset_to_vaddr(offset, HEADER.size)
            header = HEADER.unpack_from(elf.data, offset)
            symbol_address = header[3]
            symbol_count = header[4]
            if not 0 < symbol_count < 100_000:
                continue
            symbol_offset = elf.vaddr_to_offset(symbol_address, symbol_count * SYMBOL.size)
            symbols: list[Symbol] = []
            for index in range(symbol_count):
                entry_offset = symbol_offset + index * SYMBOL.size
                name_address, address, auxiliary, kind, flags = SYMBOL.unpack_from(
                    elf.data, entry_offset
                )
                name_offset = elf.vaddr_to_offset(name_address)
                name = read_c_string(
                    elf.data,
                    name_offset,
                    len(elf.data),
                    f"linked symbol {index} name",
                )
                if kind not in (0, 1, 2, 4):
                    raise ValueError("invalid linked symbol kind")
                symbols.append(Symbol(name, address, auxiliary, kind, flags, name_address))
            candidates.append((header_address, tuple(symbols)))
        except (UnicodeDecodeError, ValueError, struct.error):
            continue
    if not candidates:
        raise ValueError("no linked SNR2 symbol table was found in the loader ELF")
    return max(candidates, key=lambda candidate: len(candidate[1]))


def merge_exports(
    loader_symbols: Sequence[Symbol],
    placements: Sequence[ModulePlacement],
) -> tuple[dict[str, int], dict[str, str]]:
    addresses: dict[str, int] = {}
    owners: dict[str, str] = {}

    def add(name: str, address: int, owner: str) -> None:
        if not name or not address:
            return
        if name in addresses and addresses[name] != address:
            raise ValueError(
                f"symbol {name!r} is exported by both {owners[name]} and {owner}"
            )
        addresses[name] = address
        owners[name] = owner

    for symbol in loader_symbols:
        if symbol.kind in (2, 4):
            add(symbol.name, symbol.address, "LOADER.ELF")
    for placement in placements:
        for symbol in placement.module.exports:
            add(symbol.name, placement.base + symbol.address, placement.module.path.name)
    return addresses, owners


def patch_linked_loader_relocations(
    elf: ElfImage,
    header_address: int,
    symbols: Sequence[Symbol],
    external_symbols: Mapping[str, int],
) -> tuple[ElfImage, int, tuple[str, ...]]:
    header_offset = elf.vaddr_to_offset(header_address, HEADER.size)
    header = HEADER.unpack_from(elf.data, header_offset)
    relocation_address = header[1]
    relocation_count = header[2]
    relocation_offset = elf.vaddr_to_offset(
        relocation_address,
        relocation_count * RELOCATION.size,
    )
    image = bytearray(elf.data)
    applied = 0
    unresolved: set[str] = set()
    for index in range(relocation_count):
        relocation = unpack_relocation(
            elf.data,
            relocation_offset + index * RELOCATION.size,
        )
        if relocation.kind not in SUPPORTED_RELOCATION_TYPES:
            raise ValueError(
                f"loader relocation {index} has unsupported type {relocation.kind}"
            )
        if relocation.symbol_index >= len(symbols):
            raise ValueError(
                f"loader relocation {index} references invalid symbol "
                f"{relocation.symbol_index}"
            )
        symbol = symbols[relocation.symbol_index]
        if symbol.kind == 1:
            target = external_symbols.get(symbol.name)
            if not target:
                unresolved.add(symbol.name)
                continue
        else:
            target = symbol.address
        site_offset = elf.vaddr_to_offset(relocation.site, 4)
        word = struct.unpack_from("<I", image, site_offset)[0]
        struct.pack_into(
            "<I",
            image,
            site_offset,
            patch_word(word, relocation.kind, target + relocation.addend),
        )
        applied += 1
    return parse_elf(bytes(image)), applied, tuple(sorted(unresolved))


def ensure_nonoverlapping(elf: ElfImage, placements: Sequence[ModulePlacement]) -> None:
    ranges = [
        (segment.vaddr, segment.vaddr + segment.memsz, "LOADER.ELF")
        for segment in elf.segments
    ]
    ranges.extend(
        (
            placement.base,
            placement.base + placement.module.image_size,
            placement.module.path.name,
        )
        for placement in placements
    )
    ranges.sort()
    for left, right in zip(ranges, ranges[1:]):
        if left[1] > right[0]:
            raise ValueError(
                f"{left[2]} [0x{left[0]:08X}, 0x{left[1]:08X}) overlaps "
                f"{right[2]} [0x{right[0]:08X}, 0x{right[1]:08X})"
            )


def append_bundle_segments(
    elf: ElfImage,
    relocated: Sequence[tuple[ModulePlacement, bytes]],
) -> bytes:
    image = bytearray(elf.data)
    new_headers: list[bytes] = []
    module_offsets: list[int] = []
    for placement, module_image in relocated:
        alignment = placement.module.alignment
        padding = (placement.base - len(image)) % alignment
        image.extend(b"\0" * padding)
        offset = len(image)
        module_offsets.append(offset)
        image.extend(module_image)
        new_headers.append(
            PROGRAM_HEADER.pack(
                1,
                offset,
                placement.base,
                placement.base,
                len(module_image),
                len(module_image),
                7,
                alignment,
            )
        )

    original_count = int(elf.header[10])
    entry_size = elf.program_header_entry_size
    table_end = elf.program_header_offset + (original_count + len(new_headers)) * entry_size
    first_segment = min(segment.offset for segment in elf.segments if segment.filesz)
    if table_end > first_segment:
        raise ValueError("ELF has no room to extend its program header table")
    for index, packed in enumerate(new_headers, start=original_count):
        offset = elf.program_header_offset + index * entry_size
        image[offset : offset + PROGRAM_HEADER.size] = packed
        if entry_size > PROGRAM_HEADER.size:
            image[offset + PROGRAM_HEADER.size : offset + entry_size] = b"\0" * (
                entry_size - PROGRAM_HEADER.size
            )
    struct.pack_into("<H", image, 44, original_count + len(new_headers))

    section_offset = int(elf.header[6])
    section_entry_size = int(elf.header[11])
    section_count = int(elf.header[12])
    string_index = int(elf.header[13])
    if (
        section_entry_size < SECTION_HEADER.size
        or section_offset + section_entry_size * section_count > len(elf.data)
        or not 0 < string_index < section_count
    ):
        raise ValueError("ELF section table cannot be extended")
    section_headers = [
        list(
            SECTION_HEADER.unpack_from(
                elf.data,
                section_offset + index * section_entry_size,
            )
        )
        for index in range(section_count)
    ]
    string_header = section_headers[string_index]
    string_offset = string_header[4]
    string_size = string_header[5]
    if string_offset + string_size > len(elf.data):
        raise ValueError("ELF section-name table is truncated")
    section_names = bytearray(elf.data[string_offset : string_offset + string_size])
    module_name_offsets: list[int] = []
    used_names: set[str] = set()
    for placement, _module_image in relocated:
        stem = "".join(
            character.lower() if character.isalnum() else "_"
            for character in placement.module.path.stem
        )
        name = f".kz_{stem}"
        suffix = 2
        while name in used_names:
            name = f".kz_{stem}_{suffix}"
            suffix += 1
        used_names.add(name)
        module_name_offsets.append(len(section_names))
        section_names.extend(name.encode("ascii") + b"\0")

    new_string_offset = len(image)
    image.extend(section_names)
    while len(image) & 3:
        image.append(0)
    new_section_offset = len(image)
    string_header[4] = new_string_offset
    string_header[5] = len(section_names)
    for header in section_headers:
        image.extend(SECTION_HEADER.pack(*header))
        if section_entry_size > SECTION_HEADER.size:
            image.extend(b"\0" * (section_entry_size - SECTION_HEADER.size))
    for index, ((placement, module_image), file_offset) in enumerate(
        zip(relocated, module_offsets)
    ):
        image.extend(
            SECTION_HEADER.pack(
                module_name_offsets[index],
                1,
                7,
                placement.base,
                file_offset,
                len(module_image),
                0,
                0,
                placement.module.alignment,
                0,
            )
        )
        if section_entry_size > SECTION_HEADER.size:
            image.extend(b"\0" * (section_entry_size - SECTION_HEADER.size))
    struct.pack_into("<I", image, 32, new_section_offset)
    struct.pack_into("<H", image, 48, section_count + len(relocated))
    return bytes(image)


def module_entry_points(
    placements: Sequence[ModulePlacement],
) -> list[dict[str, object]]:
    function_imports = {
        placement.module.symbols[relocation.symbol_index].name
        for placement in placements
        for relocation in placement.module.named_relocations
        if relocation.kind == 4
        and placement.module.symbols[relocation.symbol_index].kind == 1
    }
    entries: dict[int, dict[str, object]] = {}
    for placement in placements:
        for label, field in (("constructors", 6), ("destructors", 7)):
            offset = placement.module.header[field]
            if offset:
                address = placement.base + offset
                entries[address] = {
                    "address": f"0x{address:08X}",
                    "module": placement.module.path.name,
                    "reason": label,
                }
        for index, offset in enumerate(placement.module.indexed_exports):
            address = placement.base + offset
            entries[address] = {
                "address": f"0x{address:08X}",
                "module": placement.module.path.name,
                "reason": f"indexed export {index}",
            }
        for symbol in placement.module.exports:
            if symbol.name in function_imports:
                address = placement.base + symbol.address
                entries[address] = {
                    "address": f"0x{address:08X}",
                    "module": placement.module.path.name,
                    "reason": "function export",
                    "symbol": symbol.name,
                }
    return [entries[address] for address in sorted(entries)]


def parse_module_spec(value: str) -> tuple[Path, int]:
    try:
        path_text, base_text = value.rsplit("@", 1)
        base = int(base_text, 0)
    except ValueError as error:
        raise argparse.ArgumentTypeError(
            "module placement must be PATH@BASE, for example BLGFX.REL@0x21C700"
        ) from error
    path = Path(path_text).expanduser().resolve()
    if not path.is_file():
        raise argparse.ArgumentTypeError(f"module does not exist: {path}")
    return path, base


def require_private_output(path: Path) -> Path:
    resolved = path.expanduser().resolve()
    try:
        resolved.relative_to(WORK_ROOT)
    except ValueError as error:
        raise ValueError(f"retail-derived output must stay under {WORK_ROOT}") from error
    return resolved


def inspect_command(args: argparse.Namespace) -> int:
    loader_data = args.loader.read_bytes()
    elf = parse_elf(loader_data)
    header_address, loader_symbols = find_linked_loader_symbols(elf)
    modules = [parse_module(path) for path in args.modules]
    print(
        f"{args.loader.name}: entry=0x{elf.entry:08X}, linked SNR2=0x{header_address:08X}, "
        f"symbols={len(loader_symbols)}"
    )
    loader_exports = {
        symbol.name for symbol in loader_symbols if symbol.kind in (2, 4) and symbol.name
    }
    available = set(loader_exports)
    for module in modules:
        available.update(symbol.name for symbol in module.exports)
    for module in modules:
        unresolved = sorted(symbol.name for symbol in module.imports if symbol.name not in available)
        print(
            f"{module.path.name}: file=0x{len(module.data):X}, "
            f"image=0x{module.image_size:X}, align=0x{module.alignment:X}, "
            f"symbols={len(module.symbols)}, named={len(module.named_relocations)}, "
            f"packed={len(module.packed_relocations)}, unresolved={len(unresolved)}"
        )
        if unresolved:
            print("  " + ", ".join(unresolved))
    return 0


def bundle_command(args: argparse.Namespace) -> int:
    output = require_private_output(args.out)
    manifest_path = require_private_output(
        args.manifest if args.manifest else output.with_suffix(output.suffix + ".json")
    )
    loader_data = args.loader.read_bytes()
    elf = parse_elf(loader_data)
    header_address, loader_symbols = find_linked_loader_symbols(elf)
    placements = [
        ModulePlacement(parse_module(path), base)
        for path, base in args.module
    ]
    ensure_nonoverlapping(elf, placements)
    external_symbols, owners = merge_exports(loader_symbols, placements)
    patched_elf, loader_relocations, unresolved_loader_imports = (
        patch_linked_loader_relocations(
            elf,
            header_address,
            loader_symbols,
            external_symbols,
        )
    )
    unresolved_by_module = {
        placement.module.path.name: sorted(
            {
                symbol.name
                for symbol in placement.module.imports
                if not external_symbols.get(symbol.name)
            }
        )
        for placement in placements
    }
    relocated = [
        (
            placement,
            relocate_module(
                placement,
                external_symbols,
                allow_unresolved=args.allow_unresolved,
            ),
        )
        for placement in placements
    ]
    bundle = append_bundle_segments(patched_elf, relocated)
    entries = module_entry_points(placements)

    output.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(bundle)
    manifest = {
        "schema_version": 1,
        "loader": {
            "path": str(args.loader),
            "sha256": sha256_bytes(loader_data),
            "entry": f"0x{elf.entry:08X}",
            "linked_snr2_header": f"0x{header_address:08X}",
            "exports": sum(
                1 for symbol in loader_symbols if symbol.kind in (2, 4) and symbol.name
            ),
            "applied_import_relocations": loader_relocations,
            "unresolved_imports": list(unresolved_loader_imports),
        },
        "modules": [
            {
                "name": placement.module.path.name,
                "source_sha256": sha256_bytes(placement.module.data),
                "base": f"0x{placement.base:08X}",
                "end": f"0x{placement.base + placement.module.image_size:08X}",
                "image_size": placement.module.image_size,
                "source_size": len(placement.module.data),
                "alignment": placement.module.alignment,
                "named_relocations": len(placement.module.named_relocations),
                "packed_relocations": len(placement.module.packed_relocations),
                "imports": len(placement.module.imports),
                "unresolved_imports": unresolved_by_module[
                    placement.module.path.name
                ],
                "exports": len(placement.module.exports),
                "indexed_exports": len(placement.module.indexed_exports),
            }
            for placement in placements
        ],
        "resolved_symbols": len(external_symbols),
        "symbol_owners": owners,
        "entry_points": entries,
        "bundle_sha256": sha256_bytes(bundle),
    }
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(
        f"Wrote {output} ({len(bundle)} bytes, SHA-256 {manifest['bundle_sha256']})"
    )
    for placement in placements:
        unresolved = unresolved_by_module[placement.module.path.name]
        print(
            f"  {placement.module.path.name}: "
            f"0x{placement.base:08X}-"
            f"0x{placement.base + placement.module.image_size:08X}, "
            f"unresolved={len(unresolved)}"
        )
    print(f"Recompiler entry points: {len(entries)} (manifest: {manifest_path})")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)

    inspect_parser = commands.add_parser("inspect", help="validate SNR2 inputs and import closure")
    inspect_parser.add_argument("--loader", required=True, type=Path)
    inspect_parser.add_argument("modules", nargs="+", type=Path)
    inspect_parser.set_defaults(run=inspect_command)

    bundle_parser = commands.add_parser(
        "bundle",
        help="relocate modules and append them as executable ELF load segments",
    )
    bundle_parser.add_argument("--loader", required=True, type=Path)
    bundle_parser.add_argument(
        "--allow-unresolved",
        action="store_true",
        help="leave unavailable staged-module imports unpatched and report them",
    )
    bundle_parser.add_argument(
        "--module",
        required=True,
        action="append",
        type=parse_module_spec,
        help="repeatable PATH@BASE placement",
    )
    bundle_parser.add_argument("--out", required=True, type=Path)
    bundle_parser.add_argument("--manifest", type=Path)
    bundle_parser.set_defaults(run=bundle_command)
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    args.loader = args.loader.expanduser().resolve()
    if not args.loader.is_file():
        parser.error(f"loader does not exist: {args.loader}")
    if args.command == "inspect":
        args.modules = [path.expanduser().resolve() for path in args.modules]
        missing = [path for path in args.modules if not path.is_file()]
        if missing:
            parser.error(f"module does not exist: {missing[0]}")
    try:
        return args.run(args)
    except (OSError, UnicodeDecodeError, ValueError) as error:
        parser.error(str(error))
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
