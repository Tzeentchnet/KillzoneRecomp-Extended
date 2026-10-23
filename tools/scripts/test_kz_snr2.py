from __future__ import annotations

import struct
import unittest
from pathlib import Path

import kz_snr2


def make_module(
    *,
    symbol_count: int = 3,
    relocation_symbol: int = 1,
    packed: bytes | None = None,
) -> bytes:
    relocation_offset = 0x100
    symbol_offset = relocation_offset + kz_snr2.RELOCATION.size
    symbol_end = symbol_offset + symbol_count * kz_snr2.SYMBOL.size
    packed_data = (
        bytes((1,))
        + struct.pack("<I", 0x40)
        + bytes((0x04, 0x06, 0x07, 0))
        + struct.pack("<I", 0)
        if packed is None
        else packed
    )
    packed_offset = symbol_end
    size = packed_offset + len(packed_data)
    data = bytearray(size)
    data[0x80:0x8A] = b"external\0"
    data[0x90:0x99] = b"exported\0"

    struct.pack_into("<I", data, 0x40, 0x3C080000)
    struct.pack_into("<I", data, 0x44, 0x25080080)
    struct.pack_into("<I", data, 0x48, 0x0C000008)
    struct.pack_into("<I", data, 0x4C, 0x00000090)
    struct.pack_into("<I", data, 0x50, 0)
    struct.pack_into(
        "<IIi",
        data,
        relocation_offset,
        0x50,
        (relocation_symbol << 8) | 2,
        4,
    )

    kz_snr2.SYMBOL.pack_into(data, symbol_offset, 0x80, 0, 0, 0, 0)
    for index in range(1, symbol_count):
        kz_snr2.SYMBOL.pack_into(data, symbol_offset + index * 12, 0x80, 0, 0, 1, 0)
    if symbol_count > 2:
        kz_snr2.SYMBOL.pack_into(data, symbol_offset + 2 * 12, 0x90, 0x70, 0, 2, 0)
    data[packed_offset:] = packed_data
    kz_snr2.HEADER.pack_into(
        data,
        0,
        kz_snr2.MAGIC,
        relocation_offset,
        1,
        symbol_offset,
        symbol_count,
        0,
        0x70,
        0,
        0,
        0,
        0x40,
        size,
        0,
        packed_offset,
        packed_offset,
    )
    return bytes(data)


class Snr2Tests(unittest.TestCase):
    def test_relocates_named_and_packed_records(self) -> None:
        source = make_module()
        module = kz_snr2.parse_module(Path("synthetic.rel"), source)
        image = kz_snr2.relocate_module(
            kz_snr2.ModulePlacement(module, 0x2000),
            {"external": 0x00345678},
        )

        self.assertEqual(struct.unpack_from("<I", image, 0x40)[0], 0x3C080000)
        self.assertEqual(struct.unpack_from("<I", image, 0x44)[0], 0x25082080)
        self.assertEqual(struct.unpack_from("<I", image, 0x48)[0], 0x0C000808)
        self.assertEqual(struct.unpack_from("<I", image, 0x4C)[0], 0x00002090)
        self.assertEqual(struct.unpack_from("<I", image, 0x50)[0], 0x0034567C)
        self.assertEqual(len(image), 0x100)

    def test_uses_full_24_bit_symbol_index(self) -> None:
        source = make_module(symbol_count=257, relocation_symbol=256, packed=b"\0\0\0\0\0")
        module = kz_snr2.parse_module(Path("wide-symbol.rel"), source)
        self.assertEqual(module.named_relocations[0].symbol_index, 256)
        image = kz_snr2.relocate_module(
            kz_snr2.ModulePlacement(module, 0x4000),
            {"external": 0x00123450},
        )
        self.assertEqual(struct.unpack_from("<I", image, 0x50)[0], 0x00123454)

    def test_rejects_out_of_range_symbol_index(self) -> None:
        source = make_module(relocation_symbol=3)
        with self.assertRaisesRegex(ValueError, "only 3 exist"):
            kz_snr2.parse_module(Path("bad-index.rel"), source)

    def test_rejects_invalid_table_extent(self) -> None:
        source = bytearray(make_module())
        struct.pack_into("<I", source, 0x0C, 0x120)
        with self.assertRaisesRegex(ValueError, "relocation table extent"):
            kz_snr2.parse_module(Path("bad-table.rel"), bytes(source))

    def test_rejects_hi16_without_following_lo16(self) -> None:
        packed = bytes((1,)) + struct.pack("<I", 0x40) + b"\0\0\0\0\0"
        source = make_module(packed=packed)
        with self.assertRaisesRegex(ValueError, "has no following LO16"):
            kz_snr2.parse_module(Path("bad-hi.rel"), source)

    def test_rejects_missing_magic(self) -> None:
        source = bytearray(make_module())
        struct.pack_into("<I", source, 0, 0)
        with self.assertRaisesRegex(ValueError, "missing SNR2 magic"):
            kz_snr2.parse_module(Path("bad-magic.rel"), bytes(source))

    def test_requires_all_imports(self) -> None:
        module = kz_snr2.parse_module(Path("missing.rel"), make_module())
        with self.assertRaisesRegex(ValueError, "unresolved imports: external"):
            kz_snr2.relocate_module(kz_snr2.ModulePlacement(module, 0x2000), {})

    def test_can_leave_staged_imports_unresolved(self) -> None:
        module = kz_snr2.parse_module(Path("staged.rel"), make_module())
        image = kz_snr2.relocate_module(
            kz_snr2.ModulePlacement(module, 0x2000),
            {},
            allow_unresolved=True,
        )

        self.assertEqual(struct.unpack_from("<I", image, 0x48)[0], 0x0C000808)
        self.assertEqual(struct.unpack_from("<I", image, 0x50)[0], 0)

    def test_accepts_indexed_export_metadata(self) -> None:
        source = bytearray(make_module())
        struct.pack_into("<I", source, 0x20, 0xF0)
        struct.pack_into("<I", source, 0x24, 1)
        struct.pack_into("<I", source, 0xF0, 0x70)
        module = kz_snr2.parse_module(Path("indexed.rel"), bytes(source))

        image = kz_snr2.relocate_module(
            kz_snr2.ModulePlacement(module, 0x2000),
            {"external": 0x00345678},
        )
        entries = kz_snr2.module_entry_points(
            [kz_snr2.ModulePlacement(module, 0x2000)]
        )

        self.assertEqual(struct.unpack_from("<I", image, 0x50)[0], 0x0034567C)
        self.assertIn(
            {
                "address": "0x00002070",
                "module": "indexed.rel",
                "reason": "indexed export 0",
            },
            entries,
        )


if __name__ == "__main__":
    unittest.main()
