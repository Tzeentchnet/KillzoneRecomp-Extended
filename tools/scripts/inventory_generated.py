from __future__ import annotations

import argparse
import csv
import json
import os
import re
import struct
import sys
import tomllib
import zlib
from pathlib import Path
from typing import Any

from kz_generated_manifest import (
    ROOT,
    ElfImage,
    format_errors,
    inspect_resume_cases,
    inspect_resume_table,
    load_manifest,
    parse_int,
    sha256_bytes,
    sha256_file,
)


def expect(errors: list[str], label: str, actual: Any, locked: Any) -> None:
    if actual != locked:
        errors.append(f"{label} is {actual!r}, locked manifest requires {locked!r}")


def parse_function_map(path: Path) -> tuple[list[dict[str, str]], list[str]]:
    errors: list[str] = []
    with path.open(newline="", encoding="utf-8-sig") as stream:
        reader = csv.DictReader(stream)
        if reader.fieldnames != ["Name", "Start", "End", "Size"]:
            raise ValueError(f"{path} has unexpected columns: {reader.fieldnames}")
        records = list(reader)
    starts: list[int] = []
    for row_number, row in enumerate(records, 2):
        try:
            start = int(row["Start"], 16)
            end = int(row["End"], 16)
            size = int(row["Size"])
        except (KeyError, ValueError) as error:
            errors.append(f"{path}:{row_number}: invalid record: {error}")
            continue
        starts.append(start)
        if start % 4 or end < start or size <= 0 or end - start != size:
            errors.append(
                f"{path}:{row_number}: invalid normalized range "
                f"0x{start:08X}..0x{end:08X} size {size}"
            )
    duplicate_starts = len(starts) - len(set(starts))
    if duplicate_starts:
        errors.append(f"function map has {duplicate_starts} duplicate start addresses")
    return records, errors


def parse_hook_header(path: Path) -> dict[str, int]:
    text = path.read_text(encoding="utf-8")
    matches = re.findall(
        r"^\s*inline constexpr uint32_t\s+([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(0x[0-9A-Fa-f]+)u;",
        text,
        re.MULTILINE,
    )
    symbols = {name: int(address, 16) for name, address in matches}
    if len(symbols) != len(matches):
        raise ValueError(f"{path} contains duplicate hook symbols")
    return symbols


def parse_recompiler_log(path: Path) -> dict[str, int]:
    text = path.read_text(encoding="utf-8", errors="replace")
    patterns = {
        "recompiler_input_record_count": r"\[recompiler\] extracted (\d+) functions",
        "synthesized_entry_point_count": r"synthesized (\d+) standalone configured guest entry point",
        "folded_nested_entry_block_count": r"folded (\d+) entry_\* blocks nested",
        "folded_standalone_entry_block_count": r"and (\d+) standalone entry_\* blocks",
        "generated_function_count": r"; (\d+) functions remain",
        "resumable_entry_point_count": r"collected (\d+) resumable entry point",
        "resume_owner_function_count": r"across (\d+) owner function",
        "recompiled_record_count": r"Functions processed: \d+, recompiled: (\d+)",
        "stub_record_count": r"Functions processed: \d+, recompiled: \d+, stubs: (\d+)",
        "skipped_record_count": r"Functions processed: \d+, recompiled: \d+, stubs: \d+, skipped: (\d+)",
        "decode_failure_count": r"Functions processed: .* decode failures: (\d+)",
        "additional_entry_point_count": r"Additional entrypoints: (\d+)",
        "indirect_fallback_promotion_count": r"Indirect fallback promotions: (\d+)",
        "indirect_fallback_entry_count": r"Indirect fallback promotions: \d+ \((\d+) fallback entries\)",
        "unhandled_instruction_count": r"Unhandled instructions: (\d+)",
        "correctness_critical_fallback_count": r"Correctness-critical guest fallbacks: (\d+)",
        "correctness_critical_failure_count": r"Correctness-critical guest fallbacks: \d+, failures: (\d+)",
    }
    values: dict[str, int] = {}
    missing: list[str] = []
    for key, pattern in patterns.items():
        match = re.search(pattern, text)
        if match:
            values[key] = int(match.group(1))
        else:
            missing.append(key)
    if missing:
        raise ValueError(f"{path} is missing report fields: {', '.join(missing)}")
    return values


def env_path(name: str, fallback: Path) -> Path:
    value = os.environ.get(name)
    return Path(value).expanduser() if value else fallback


def main() -> int:
    parser = argparse.ArgumentParser(description="Inventory and validate private generated-code provenance.")
    parser.add_argument("--check", action="store_true", help="Return nonzero when any locked value differs.")
    parser.add_argument("--manifest", type=Path, default=ROOT / "config" / "generated-manifest.json")
    parser.add_argument("--generated", type=Path, default=env_path("KZ_GENERATED_DIR", ROOT / "generated"))
    parser.add_argument("--elf", type=Path, default=env_path("KZ_GAME_ELF", ROOT / "game" / "SCUS_974.02"))
    parser.add_argument(
        "--functions",
        type=Path,
        default=env_path("KZ_FUNCTIONS_CSV", ROOT / "work" / "functions.csv"),
    )
    parser.add_argument("--recompiler-log", type=Path, default=ROOT / "work" / "recomp.log")
    args = parser.parse_args()

    errors: list[str] = []
    try:
        manifest = load_manifest(args.manifest)
        resume = inspect_resume_table(args.generated)
        resume_cases = inspect_resume_cases(resume)
        elf = ElfImage(args.elf)
        records, map_errors = parse_function_map(args.functions)
        metadata_path = Path(f"{args.functions}.meta.json")
        metadata = json.loads(metadata_path.read_text(encoding="utf-8-sig"))
        recompiler = parse_recompiler_log(args.recompiler_log)
        hook_header = ROOT / "src" / "kz_hook_addresses.h"
        hook_symbols = parse_hook_header(hook_header)
    except (FileNotFoundError, OSError, ValueError, json.JSONDecodeError, struct.error) as error:
        print(f"[FAIL] {error}", file=sys.stderr)
        return 1
    errors.extend(resume.errors)
    errors.extend(resume_cases.errors)
    errors.extend(map_errors)

    executable = manifest["executable"]
    expect(errors, "executable size", len(elf.data), executable["size"])
    expect(errors, "executable SHA-256", sha256_bytes(elf.data), executable["sha256"])
    expect(errors, "executable CRC32", f"{zlib.crc32(elf.data) & 0xFFFFFFFF:08x}", executable["crc32"])
    expect(errors, "ELF entry", elf.entry, parse_int(executable["entry"]))
    text = elf.section(".text")
    locked_text = executable["text"]
    expect(errors, ".text address", text.address, parse_int(locked_text["address"]))
    expect(errors, ".text offset", text.offset, parse_int(locked_text["offset"]))
    expect(errors, ".text size", text.size, parse_int(locked_text["size"]))
    expect(
        errors,
        ".text SHA-256",
        sha256_bytes(elf.data[text.offset : text.offset + text.size]),
        locked_text["sha256"],
    )

    function_map = manifest["function_map"]
    function_count = sum(not row["Name"].startswith("entry_") for row in records)
    label_count = sum(row["Name"].startswith("entry_") for row in records)
    expect(errors, "Ghidra function count", function_count, function_map["function_count"])
    expect(errors, "Ghidra code-label count", label_count, function_map["code_label_count"])
    expect(errors, "function-map record count", len(records), function_map["csv_record_count"])
    metadata_checks = {
        "schema_version": function_map["schema_version"],
        "elf_sha256": executable["sha256"],
        "exporter_sha256": function_map["exporter_sha256"],
        "ghidra_version": function_map["ghidra_version"],
        "normalization": function_map["normalization"],
        "clamped_records": function_map["clamped_record_count"],
        "instruction_aligned_records": function_map["instruction_aligned_record_count"],
        "delay_slot_extended_records": function_map["delay_slot_extended_record_count"],
        "csv_records": function_map["csv_record_count"],
    }
    for key, locked in metadata_checks.items():
        expect(errors, f"function-map metadata {key}", metadata.get(key), locked)
    exporter = ROOT / "ext" / "PS2Recomp" / "ps2xRecomp" / "tools" / "ghidra" / "ExportPS2Functions.java"
    expect(errors, "Ghidra exporter SHA-256", sha256_file(exporter), function_map["exporter_sha256"])

    config = tomllib.loads((ROOT / "config" / "killzone.toml").read_text(encoding="utf-8"))
    export_config = config["ghidra_export"]
    for key in ("function_count", "code_label_count", "csv_record_count"):
        expect(errors, f"killzone.toml ghidra_export.{key}", export_config[key], function_map[key])
    stubs = config["general"]["stubs"]
    expect(errors, "killzone.toml stub count", export_config["stub_count"], len(stubs))
    expect(
        errors,
        "killzone.toml uncategorized count",
        export_config["uncategorized_count"],
        function_map["function_count"] - len(stubs),
    )
    map_ranges = [
        (int(row["Start"], 16), int(row["End"], 16))
        for row in records
    ]
    stub_addresses: list[int] = []
    for stub in stubs:
        try:
            stub_addresses.append(int(stub.rsplit("@", 1)[1], 16))
        except (IndexError, ValueError):
            errors.append(f"invalid stub selector in killzone.toml: {stub!r}")
    if len(stub_addresses) != len(set(stub_addresses)):
        errors.append("killzone.toml contains duplicate stub addresses")
    for address in stub_addresses:
        owners = [(start, end) for start, end in map_ranges if start <= address < end]
        if len(owners) > 1:
            errors.append(f"stub address 0x{address:08X} resolves to {len(owners)} function-map ranges")
        try:
            elf.bytes_at(address, 4)
        except ValueError as error:
            errors.append(f"stub address 0x{address:08X}: {error}")

    generated = manifest["generated"]
    cpp_names = [path.name for path in resume.cpp_files]
    generated_actual = {
        "file_count": len(resume.all_files),
        "translation_unit_count": len(resume.cpp_files),
        "function_translation_unit_count": sum(name.startswith("FUN_") for name in cpp_names),
        "entry_translation_unit_count": sum(name.startswith("entry_") for name in cpp_names),
        "thunk_translation_unit_count": sum(name.startswith("thunk_") for name in cpp_names),
        "registration_translation_unit_count": sum(name == "register_functions.cpp" for name in cpp_names),
        "generated_function_count": len(resume.declarations),
        "generated_stub_function_count": len(resume.stub_declarations),
        "dispatch_entry_count": len(resume.entries),
        "dispatch_unique_target_count": len(resume.dispatch_targets),
        "resume_case_dispatch_entry_count": resume_cases.resumable_entry_count,
        "resume_case_owner_function_count": resume_cases.owner_function_count,
    }
    for key, actual in generated_actual.items():
        expect(errors, key.replace("_", " "), actual, generated[key])
    table = generated["function_table"]
    expect(errors, "function-table base", resume.base, parse_int(table["base"]))
    expect(errors, "function-table end", resume.end, parse_int(table["end"]))
    expect(errors, "function-table slot count", resume.slot_count, table["slot_count"])
    for name, lock in generated["files"].items():
        path = args.generated / name
        if not path.is_file():
            errors.append(f"locked generated file is missing: {path}")
            continue
        expect(errors, f"{name} size", path.stat().st_size, lock["size"])
        expect(errors, f"{name} SHA-256", sha256_file(path), lock["sha256"])

    code_pointers_path = args.functions.parent / "code_pointers.txt"
    code_pointers = [
        int(line, 16)
        for line in code_pointers_path.read_text(encoding="utf-8").splitlines()
        if line.strip()
    ]
    expect(
        errors,
        "supplemental entry-point count",
        len(code_pointers),
        generated["supplemental_entry_point_count"],
    )
    if len(code_pointers) != len(set(code_pointers)):
        errors.append("supplemental entry-point list contains duplicates")
    missing_code_pointers = set(code_pointers) - resume.dispatch_addresses
    if missing_code_pointers:
        errors.append(f"{len(missing_code_pointers)} supplemental entry points lack dispatch entries")
    for key, actual in recompiler.items():
        expect(errors, key.replace("_", " "), actual, generated[key])

    manifest_hooks = manifest["hooks"]
    manifest_symbols = {hook["symbol"]: parse_int(hook["address"]) for hook in manifest_hooks}
    if len(manifest_symbols) != len(manifest_hooks):
        errors.append("hook manifest contains duplicate symbols")
    if len(set(manifest_symbols.values())) != len(manifest_symbols):
        errors.append("hook manifest contains duplicate addresses")
    expect(errors, "hook-header symbol map", hook_symbols, manifest_symbols)
    source_text = "\n".join(
        path.read_text(encoding="utf-8", errors="replace")
        for path in (ROOT / "src").glob("*.cpp")
    )
    for symbol in hook_symbols:
        if f"kz_hook::{symbol}" not in source_text:
            errors.append(f"hook symbol {symbol} is not used by product source")
    direct_hook_literal = re.compile(
        r"(?:replaceFunction|hasFunction|lookupFunction|hook|tlInstall(?:<[^>]+>)?)\s*"
        r"\([^;\n]*0x[0-9A-Fa-f]+"
    )
    if match := direct_hook_literal.search(source_text):
        errors.append(f"guest hook address bypasses kz_hook_addresses.h: {match.group(0)!r}")

    valid_kinds = {"executable_entry", "hook", "debug_hook", "lookup"}
    for hook in manifest_hooks:
        symbol = hook["symbol"]
        address = parse_int(hook["address"])
        size = hook["signature_size"]
        if hook.get("kind") not in valid_kinds:
            errors.append(f"{symbol} has invalid hook kind {hook.get('kind')!r}")
        if address % 4 or size < 16 or size % 4:
            errors.append(f"{symbol} has invalid aligned address/signature size")
            continue
        try:
            signature = elf.bytes_at(address, size)
        except ValueError as error:
            errors.append(f"{symbol}: {error}")
            continue
        expect(errors, f"{symbol} signature SHA-256", sha256_bytes(signature), hook["signature_sha256"])
        occurrences = elf.aligned_occurrences(signature)
        if occurrences != [address]:
            formatted = ", ".join(f"0x{item:08X}" for item in occurrences)
            errors.append(f"{symbol} signature resolves to [{formatted}], expected only 0x{address:08X}")
        if address not in resume.dispatch_addresses:
            errors.append(f"{symbol} address 0x{address:08X} has no generated dispatch entry")
    expect(errors, "GameEntry address", manifest_symbols.get("GameEntry"), elf.entry)

    locked_patches = {
        parse_int(patch["address"]): parse_int(patch["replacement"])
        for patch in manifest["instruction_patches"]
    }
    configured_patches = {
        parse_int(patch["address"]): parse_int(patch["value"])
        for patch in config["patches"]["instructions"]
    }
    expect(errors, "instruction patch address/value map", configured_patches, locked_patches)
    for patch in manifest["instruction_patches"]:
        address = parse_int(patch["address"])
        original = elf.bytes_at(address, 4)
        expect(
            errors,
            f"patch 0x{address:08X} original word",
            struct.unpack("<I", original)[0],
            parse_int(patch["original"]),
        )
        expect(
            errors,
            f"patch 0x{address:08X} original SHA-256",
            sha256_bytes(original),
            patch["original_sha256"],
        )

    if errors:
        stream = sys.stderr if args.check else sys.stdout
        print(format_errors(errors), file=stream)
        return 1 if args.check else 0

    summary = {
        "executable_sha256": executable["sha256"],
        "ghidra_functions": function_count,
        "ghidra_labels": label_count,
        "generated_translation_units": len(resume.cpp_files),
        "generated_functions": len(resume.declarations),
        "dispatch_entries": len(resume.entries),
        "resume_case_dispatch_entries": resume_cases.resumable_entry_count,
        "recompiler_resumable_entries": recompiler["resumable_entry_point_count"],
        "hook_signatures": len(manifest_hooks),
    }
    print("Generated inventory: PASS")
    print(json.dumps(summary, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
