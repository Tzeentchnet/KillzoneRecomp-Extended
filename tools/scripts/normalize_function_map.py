"""Normalize Ghidra function ranges to whole MIPS instructions and preserve delay slots."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import struct
from pathlib import Path


INSTRUCTION_SIZE = 4
NORMALIZATION = "clamp_bodies_align_instructions_extend_delay_slots_v4"


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def sections(data: bytes):
    section_offset = struct.unpack_from("<I", data, 0x20)[0]
    section_count, string_index = struct.unpack_from("<HH", data, 0x30)
    headers = [
        struct.unpack_from("<10I", data, section_offset + index * 40)
        for index in range(section_count)
    ]
    strings_offset = headers[string_index][4]
    for header in headers:
        name_offset = strings_offset + header[0]
        name = data[name_offset : data.index(b"\0", name_offset)].decode()
        yield name, header[3], header[4], header[5], header[2]


def has_delay_slot(instruction: int) -> bool:
    opcode = instruction >> 26
    function = instruction & 0x3F
    coprocessor_format = (instruction >> 21) & 0x1F
    return (
        opcode in {1, 2, 3, 4, 5, 6, 7, 20, 21, 22, 23}
        or (opcode == 0 and function in {8, 9})
        or (opcode in {16, 17, 18} and coprocessor_format == 8)
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", required=True, type=Path)
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--metadata", required=True, type=Path)
    parser.add_argument("--exporter", required=True, type=Path)
    args = parser.parse_args()

    elf_data = args.elf.read_bytes()
    executable_sections = [
        (address, address + size, offset)
        for _name, address, offset, size, flags in sections(elf_data)
        if flags & 0x4 and size
    ]

    with args.input.open(newline="", encoding="utf-8-sig") as stream:
        rows = list(csv.DictReader(stream))

    normalized = []
    clamped_count = 0
    instruction_aligned_count = 0
    delay_slot_count = 0
    for row in rows:
        start = int(row["Start"], 16)
        raw_end = int(row["End"], 16)
        body_size = int(row["Size"])
        if start % INSTRUCTION_SIZE != 0:
            raise ValueError(f"{row['Name']} has unaligned start address 0x{start:08X}")

        end = raw_end
        if raw_end - start != body_size:
            end = start + body_size
            clamped_count += 1

        aligned_end = (end + INSTRUCTION_SIZE - 1) & ~(INSTRUCTION_SIZE - 1)
        if aligned_end != end:
            end = aligned_end
            instruction_aligned_count += 1

        for section_start, section_end, section_offset in executable_sections:
            if section_start + INSTRUCTION_SIZE <= end < section_end:
                instruction = struct.unpack_from(
                    "<I",
                    elf_data,
                    section_offset + end - section_start - INSTRUCTION_SIZE,
                )[0]
                if has_delay_slot(instruction):
                    end += INSTRUCTION_SIZE
                    delay_slot_count += 1
                break

        normalized.append(
            {
                "Name": row["Name"],
                "Start": f"0x{start:08X}",
                "End": f"0x{end:08X}",
                "Size": str(end - start),
            }
        )

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=("Name", "Start", "End", "Size"), lineterminator="\n")
        writer.writeheader()
        writer.writerows(normalized)

    metadata = {
        "schema_version": 3,
        "elf_sha256": sha256_file(args.elf),
        "exporter_sha256": sha256_file(args.exporter),
        "ghidra_version": "12.1.3",
        "normalization": NORMALIZATION,
        "clamped_records": clamped_count,
        "instruction_aligned_records": instruction_aligned_count,
        "delay_slot_extended_records": delay_slot_count,
        "csv_records": len(normalized),
    }
    args.metadata.write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
    print(
        f"Generated {args.output} with {len(normalized)} records; "
        f"clamped {clamped_count} discontiguous bodies, aligned "
        f"{instruction_aligned_count} partial instructions, and extended "
        f"{delay_slot_count} delay slots."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
