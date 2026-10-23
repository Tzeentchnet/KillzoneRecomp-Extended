from __future__ import annotations

import os
import tempfile
import unittest
from pathlib import Path

import sync_recompiled_output


class LocalizeDeclarationsTests(unittest.TestCase):
    def test_removes_unused_global_header(self) -> None:
        source = (
            b'#include "ps2_runtime.h"\r\n'
            b"#include <ps2_recompiled_functions.h>\r\n"
            b"#include <ps2_recompiled_stubs.h>\r\n"
            b"\r\n"
            b"void current(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime) {}\r\n"
        )

        localized, declaration_count = (
            sync_recompiled_output.localize_function_declarations(source)
        )

        self.assertEqual(declaration_count, 0)
        self.assertNotIn(b"ps2_recompiled_functions.h", localized)

    def test_emits_unique_local_declarations_for_direct_calls(self) -> None:
        source = (
            b'#include "ps2_runtime.h"\n'
            b"#include <ps2_recompiled_functions.h>\n"
            b"#include <ps2_recompiled_stubs.h>\n"
            b"\n"
            b"void current(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime) {\n"
            b"    first(rdram, ctx, runtime); return;\n"
            b"        second(rdram, ctx,  runtime);\n"
            b"    first(rdram, ctx, runtime); return;\n"
            b"}\n"
        )

        localized, declaration_count = (
            sync_recompiled_output.localize_function_declarations(source)
        )

        self.assertEqual(declaration_count, 2)
        self.assertIn(
            b"void first(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime);\n",
            localized,
        )
        self.assertIn(
            b"void second(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime);\n",
            localized,
        )
        self.assertEqual(localized.count(b"void first("), 1)
        self.assertNotIn(b"ps2_recompiled_functions.h", localized)


class SyncGeneratedTreeTests(unittest.TestCase):
    def test_rejects_repository_root_as_destination(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            staging = Path(directory)

            with self.assertRaisesRegex(ValueError, "protected directory"):
                sync_recompiled_output.sync_generated_tree(
                    staging,
                    sync_recompiled_output.ROOT,
                )

    def test_preserves_unchanged_timestamps_and_removes_stale_files(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            staging = root / "staging"
            destination = root / "output"
            staging.mkdir()
            destination.mkdir()

            source = staging / "guest_0x1000.cpp"
            source.write_bytes(
                b'#include "ps2_runtime.h"\n'
                b"#include <ps2_recompiled_functions.h>\n"
                b"void guest(uint8_t*, R5900Context*, PS2Runtime*) {}\n"
            )
            stale = destination / "stale.cpp"
            stale.write_text("stale", encoding="utf-8")

            first = sync_recompiled_output.sync_generated_tree(staging, destination)
            target = destination / source.name
            preserved_time = 1_700_000_000_000_000_000
            os.utime(target, ns=(preserved_time, preserved_time))

            second = sync_recompiled_output.sync_generated_tree(staging, destination)

            self.assertEqual(first.changed, 1)
            self.assertEqual(first.removed, 1)
            self.assertEqual(second.changed, 0)
            self.assertEqual(second.removed, 0)
            self.assertEqual(target.stat().st_mtime_ns, preserved_time)

    def test_keeps_registration_header_dependency(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            staging = root / "staging"
            destination = root / "output"
            staging.mkdir()
            registration = staging / "register_functions.cpp"
            registration.write_bytes(b"#include <ps2_recompiled_functions.h>\n")

            sync_recompiled_output.sync_generated_tree(staging, destination)

            self.assertEqual(
                (destination / registration.name).read_bytes(),
                registration.read_bytes(),
            )


if __name__ == "__main__":
    unittest.main()
