from __future__ import annotations

import hashlib
import json
import re
import struct
from collections import Counter
from dataclasses import dataclass
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[2]

_TABLE_CONSTANT = re.compile(
    r"g_ps2RecompiledFunctionTable(?P<name>Base|End|SlotCount)\s*=\s*(?P<value>0x[0-9A-Fa-f]+|\d+)u"
)
_TABLE_ENTRY = re.compile(
    r"g_ps2RecompiledFunctionTable\[(?P<slot>\d+)\]\s*=\s*"
    r"(?P<target>[A-Za-z_][A-Za-z0-9_]*);\s*//\s*0x(?P<address>[0-9A-Fa-f]+)"
)
_DECLARATION = re.compile(r"^void\s+([A-Za-z_][A-Za-z0-9_]*)\(", re.MULTILINE)


def parse_int(value: int | str) -> int:
    if isinstance(value, int):
        return value
    return int(value, 0)


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def load_manifest(path: Path) -> dict[str, Any]:
    with path.open(encoding="utf-8") as stream:
        manifest = json.load(stream)
    if not isinstance(manifest, dict) or manifest.get("schema_version") != 1:
        raise ValueError(f"unsupported generated manifest schema in {path}")
    return manifest


@dataclass(frozen=True)
class DispatchEntry:
    slot: int
    target: str
    address: int


@dataclass
class ResumeInventory:
    generated_dir: Path
    base: int
    end: int
    slot_count: int
    entries: list[DispatchEntry]
    declarations: set[str]
    stub_declarations: set[str]
    source_symbols: set[str]
    all_files: list[Path]
    errors: list[str]

    @property
    def dispatch_addresses(self) -> set[int]:
        return {entry.address for entry in self.entries}

    @property
    def dispatch_targets(self) -> set[str]:
        return {entry.target for entry in self.entries}

    @property
    def cpp_files(self) -> list[Path]:
        return [path for path in self.all_files if path.suffix == ".cpp"]


def inspect_resume_table(generated_dir: Path) -> ResumeInventory:
    generated_dir = generated_dir.resolve()
    register_path = generated_dir / "register_functions.cpp"
    header_path = generated_dir / "ps2_recompiled_functions.h"
    stubs_path = generated_dir / "ps2_recompiled_stubs.h"
    missing = [path.name for path in (register_path, header_path, stubs_path) if not path.is_file()]
    if missing:
        raise FileNotFoundError(f"generated tree is missing: {', '.join(missing)}")

    register_text = register_path.read_text(encoding="utf-8")
    header_text = header_path.read_text(encoding="utf-8")
    stubs_text = stubs_path.read_text(encoding="utf-8")
    constants = {
        match.group("name"): parse_int(match.group("value"))
        for match in _TABLE_CONSTANT.finditer(register_text)
    }
    missing_constants = {"Base", "End", "SlotCount"} - constants.keys()
    if missing_constants:
        raise ValueError(f"registration file is missing constants: {', '.join(sorted(missing_constants))}")

    entries = [
        DispatchEntry(
            slot=int(match.group("slot")),
            target=match.group("target"),
            address=int(match.group("address"), 16),
        )
        for match in _TABLE_ENTRY.finditer(register_text)
    ]
    declarations = set(_DECLARATION.findall(header_text))
    stub_declarations = set(_DECLARATION.findall(stubs_text))
    all_files = sorted(path for path in generated_dir.iterdir() if path.is_file())
    source_symbols = {
        path.stem
        for path in all_files
        if path.suffix == ".cpp" and path.name != "register_functions.cpp"
    }

    base = constants["Base"]
    end = constants["End"]
    slot_count = constants["SlotCount"]
    errors: list[str] = []
    if base % 4 or end % 4 or end <= base:
        errors.append(f"invalid function-table bounds 0x{base:08X}..0x{end:08X}")
    expected_slots = (end - base) // 4
    if slot_count != expected_slots:
        errors.append(f"slot count {slot_count} does not match bounds-derived count {expected_slots}")
    if not entries:
        errors.append("registration table has no entries")

    slots = [entry.slot for entry in entries]
    addresses = [entry.address for entry in entries]
    duplicate_slots = [slot for slot, count in Counter(slots).items() if count != 1]
    duplicate_addresses = [address for address, count in Counter(addresses).items() if count != 1]
    if duplicate_slots:
        errors.append(f"registration table has {len(duplicate_slots)} duplicate slots")
    if duplicate_addresses:
        errors.append(f"registration table has {len(duplicate_addresses)} duplicate addresses")
    if slots != sorted(slots):
        errors.append("registration slots are not strictly ordered")

    for entry in entries:
        if not 0 <= entry.slot < slot_count:
            errors.append(f"slot {entry.slot} is outside the {slot_count}-slot table")
            break
        expected_address = base + entry.slot * 4
        if entry.address != expected_address:
            errors.append(
                f"slot {entry.slot} comments address 0x{entry.address:08X}, expected 0x{expected_address:08X}"
            )
            break

    dispatch_targets = {entry.target for entry in entries}
    if unknown := dispatch_targets - declarations:
        errors.append(f"{len(unknown)} dispatch targets are missing from the generated header")
    if unregistered := declarations - dispatch_targets:
        errors.append(f"{len(unregistered)} generated declarations have no dispatch entry")
    if missing_sources := declarations - source_symbols:
        errors.append(f"{len(missing_sources)} generated declarations have no translation unit")
    if extra_sources := source_symbols - declarations:
        errors.append(f"{len(extra_sources)} generated translation units have no declaration")

    if unknown_stubs := stub_declarations - declarations:
        errors.append(f"{len(unknown_stubs)} generated stubs are missing from the function header")

    return ResumeInventory(
        generated_dir=generated_dir,
        base=base,
        end=end,
        slot_count=slot_count,
        entries=entries,
        declarations=declarations,
        stub_declarations=stub_declarations,
        source_symbols=source_symbols,
        all_files=all_files,
        errors=errors,
    )


@dataclass
class ResumeCaseInventory:
    resumable_entry_count: int
    owner_function_count: int
    missing_case_count: int
    unregistered_case_count: int
    missing_label_count: int
    errors: list[str]


def inspect_resume_cases(inventory: ResumeInventory) -> ResumeCaseInventory:
    entries_by_target: dict[str, set[int]] = {}
    dispatch_addresses = set()
    for entry in inventory.entries:
        entries_by_target.setdefault(entry.target, set()).add(entry.address)
        dispatch_addresses.add(entry.address)

    resumable_entry_count = 0
    owner_function_count = 0
    missing_case_count = 0
    missing_label_count = 0
    resume_cases: set[int] = set()
    errors: list[str] = []
    for target, addresses in entries_by_target.items():
        source_path = inventory.generated_dir / f"{target}.cpp"
        if not source_path.is_file():
            errors.append(f"dispatch target has no source file: {target}")
            continue
        text = source_path.read_text(encoding="utf-8", errors="replace")
        address_range = re.search(
            r"// Address:\s*0x([0-9A-Fa-f]+)\s*-\s*0x([0-9A-Fa-f]+)",
            text,
        )
        if not address_range:
            if target not in inventory.stub_declarations:
                errors.append(f"{target} has no generated address range and is not a declared stub")
                continue
            suffix = re.search(r"_0x([0-9A-Fa-f]+)$", target)
            if not suffix or int(suffix.group(1), 16) not in addresses:
                errors.append(f"stub {target} is not registered at its declared address")
            continue
        start = int(address_range.group(1), 16)
        if start not in addresses:
            errors.append(f"{target} is not registered at its generated start 0x{start:08X}")
        cases = {
            int(address, 16)
            for address in re.findall(r"case 0x([0-9A-Fa-f]+)u:\s*goto label_", text)
        }
        resume_cases.update(cases)
        labels = set(re.findall(r"^label_([0-9A-Fa-f]+):", text, re.MULTILINE))
        gotos = set(re.findall(r"goto label_([0-9A-Fa-f]+);", text))
        missing_labels = gotos - labels
        missing_label_count += len(missing_labels)
        if missing_labels and len(errors) < 20:
            sample = ", ".join(sorted(missing_labels)[:5])
            errors.append(f"{target} has missing generated labels: {sample}")

        internal = addresses - {start}
        if internal:
            owner_function_count += 1
            resumable_entry_count += len(internal)
        missing_cases = internal - cases
        missing_case_count += len(missing_cases)
        if missing_cases and len(errors) < 20:
            sample = ", ".join(f"0x{address:08X}" for address in sorted(missing_cases)[:5])
            errors.append(f"{target} has dispatch entries without resume cases: {sample}")
    if missing_case_count:
        errors.append(f"{missing_case_count} dispatch entries have no resume case")
    unregistered_cases = resume_cases - dispatch_addresses
    unregistered_case_count = len(unregistered_cases)
    if unregistered_case_count:
        sample = ", ".join(
            f"0x{address:08X}" for address in sorted(unregistered_cases)[:10]
        )
        errors.append(f"resume cases without dispatch entries: {sample}")
        errors.append(f"{unregistered_case_count} resume cases have no dispatch entry")
    if missing_label_count:
        errors.append(f"{missing_label_count} generated gotos have no label")
    return ResumeCaseInventory(
        resumable_entry_count=resumable_entry_count,
        owner_function_count=owner_function_count,
        missing_case_count=missing_case_count,
        unregistered_case_count=unregistered_case_count,
        missing_label_count=missing_label_count,
        errors=errors,
    )


@dataclass(frozen=True)
class ElfSection:
    name: str
    section_type: int
    address: int
    offset: int
    size: int
    flags: int


class ElfImage:
    def __init__(self, path: Path):
        self.path = path.resolve()
        self.data = self.path.read_bytes()
        if self.data[:7] != b"\x7fELF\x01\x01\x01":
            raise ValueError(f"{path} is not a 32-bit little-endian ELF")
        self.entry = struct.unpack_from("<I", self.data, 24)[0]
        section_offset = struct.unpack_from("<I", self.data, 32)[0]
        section_entry_size = struct.unpack_from("<H", self.data, 46)[0]
        section_count = struct.unpack_from("<H", self.data, 48)[0]
        string_section_index = struct.unpack_from("<H", self.data, 50)[0]
        if section_entry_size < 40 or string_section_index >= section_count:
            raise ValueError(f"{path} has an invalid section table")
        raw_sections = [
            struct.unpack_from("<10I", self.data, section_offset + index * section_entry_size)
            for index in range(section_count)
        ]
        string_section = raw_sections[string_section_index]
        strings = self.data[string_section[4] : string_section[4] + string_section[5]]

        def section_name(offset: int) -> str:
            end = strings.find(b"\0", offset)
            if offset >= len(strings) or end < 0:
                raise ValueError(f"{path} has an invalid section name offset")
            return strings[offset:end].decode("ascii")

        self.sections = [
            ElfSection(
                name=section_name(raw[0]),
                section_type=raw[1],
                flags=raw[2],
                address=raw[3],
                offset=raw[4],
                size=raw[5],
            )
            for raw in raw_sections
        ]
        for section in self.sections:
            if section.section_type != 8 and section.offset + section.size > len(self.data):
                raise ValueError(f"{path} section {section.name} exceeds the file")

    @property
    def executable_sections(self) -> list[ElfSection]:
        return [
            section
            for section in self.sections
            if section.section_type != 8 and section.flags & 0x4 and section.size
        ]

    def section(self, name: str) -> ElfSection:
        matches = [section for section in self.sections if section.name == name]
        if len(matches) != 1:
            raise ValueError(f"expected one {name} section in {self.path}, found {len(matches)}")
        return matches[0]

    def bytes_at(self, address: int, size: int) -> bytes:
        matches = [
            section
            for section in self.executable_sections
            if section.address <= address and address + size <= section.address + section.size
        ]
        if len(matches) != 1:
            raise ValueError(
                f"0x{address:08X}..0x{address + size:08X} maps to {len(matches)} executable sections"
            )
        section = matches[0]
        offset = section.offset + address - section.address
        return self.data[offset : offset + size]

    def aligned_occurrences(self, signature: bytes) -> list[int]:
        matches: list[int] = []
        for section in self.executable_sections:
            data = self.data[section.offset : section.offset + section.size]
            for offset in range(0, len(data) - len(signature) + 1, 4):
                if data[offset : offset + len(signature)] == signature:
                    matches.append(section.address + offset)
        return matches


def format_errors(errors: list[str]) -> str:
    return "\n".join(f"[FAIL] {error}" for error in errors)
