"""Validate the generated dispatch table, source ownership, and resume switches."""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

from kz_generated_manifest import (
    ROOT,
    format_errors,
    inspect_resume_cases,
    inspect_resume_table,
    load_manifest,
    parse_int,
)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("generated", nargs="?", type=Path, default=ROOT / "generated")
    parser.add_argument("--manifest", type=Path, default=ROOT / "config" / "generated-manifest.json")
    args = parser.parse_args()

    try:
        inventory = inspect_resume_table(args.generated)
        resume_cases = inspect_resume_cases(inventory)
        manifest = load_manifest(args.manifest)
    except (FileNotFoundError, OSError, ValueError) as error:
        print(f"[FAIL] {error}", file=sys.stderr)
        return 1

    errors = [*inventory.errors, *resume_cases.errors]
    expected = manifest["generated"]
    expected_table = expected["function_table"]
    executable_entry = parse_int(manifest["executable"]["entry"])
    startup_entries = [
        entry for entry in inventory.entries if entry.address == executable_entry
    ]
    if len(startup_entries) != 1:
        errors.append(
            f"ELF entry 0x{executable_entry:08X} has {len(startup_entries)} dispatch entries"
        )
    else:
        startup_source = args.generated / f"{startup_entries[0].target}.cpp"
        startup_text = startup_source.read_text(encoding="utf-8", errors="replace")
        startup_range = re.search(
            r"// Address:\s*0x([0-9A-Fa-f]+)\s*-\s*0x([0-9A-Fa-f]+)",
            startup_text,
        )
        if not startup_range:
            errors.append(f"ELF entry source has no generated address range: {startup_source.name}")
        else:
            startup_end = int(startup_range.group(2), 16)
            publishes_fallthrough = re.search(
                rf"ps2DspTail\(ctx,\s*0x{startup_end:X}u\)",
                startup_text,
                flags=re.IGNORECASE,
            )
            if publishes_fallthrough and startup_end not in inventory.dispatch_addresses:
                errors.append(
                    f"ELF entry fallthrough 0x{startup_end:08X} has no dispatch entry"
                )
    comparisons = {
        "generated file count": (len(inventory.all_files), expected["file_count"]),
        "translation unit count": (len(inventory.cpp_files), expected["translation_unit_count"]),
        "generated function count": (len(inventory.declarations), expected["generated_function_count"]),
        "generated stub function count": (
            len(inventory.stub_declarations),
            expected["generated_stub_function_count"],
        ),
        "dispatch entry count": (len(inventory.entries), expected["dispatch_entry_count"]),
        "dispatch target count": (len(inventory.dispatch_targets), expected["dispatch_unique_target_count"]),
        "resume-case dispatch entry count": (
            resume_cases.resumable_entry_count,
            expected["resume_case_dispatch_entry_count"],
        ),
        "resume-case owner count": (
            resume_cases.owner_function_count,
            expected["resume_case_owner_function_count"],
        ),
        "function table base": (inventory.base, parse_int(expected_table["base"])),
        "function table end": (inventory.end, parse_int(expected_table["end"])),
        "function table slot count": (inventory.slot_count, expected_table["slot_count"]),
    }
    for label, (actual, locked) in comparisons.items():
        if actual != locked:
            errors.append(f"{label} is {actual}, locked manifest requires {locked}")

    if errors:
        print(format_errors(errors), file=sys.stderr)
        return 1
    print(
        "Resume table verification: PASS "
        f"({len(inventory.entries)} dispatch entries, "
        f"{resume_cases.resumable_entry_count} resume cases, "
        f"{len(inventory.dispatch_targets)} generated targets)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
