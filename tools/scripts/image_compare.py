"""Compare matching PNG frames and emit protected-content-safe pixel-delta metrics."""

from __future__ import annotations

import argparse
import fnmatch
import hashlib
import json
import tempfile
from pathlib import Path

import numpy as np
from PIL import Image


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def compare_pairs(pairs: list[tuple[Path, Path, dict]]) -> dict:
    frames = []
    for reference_file, variant_file, identity in pairs:
        left = np.asarray(Image.open(reference_file).convert("RGB"), dtype=np.int16)
        right = np.asarray(Image.open(variant_file).convert("RGB"), dtype=np.int16)
        if left.shape != right.shape:
            raise ValueError(
                f"{reference_file.name}/{variant_file.name}: dimensions differ: "
                f"{left.shape} vs {right.shape}"
            )
        delta = np.abs(right - left)
        changed = np.any(delta != 0, axis=2)
        frames.append(
            {
                **identity,
                "width": int(left.shape[1]),
                "height": int(left.shape[0]),
                "reference_sha256": sha256(reference_file),
                "variant_sha256": sha256(variant_file),
                "mean_abs_rgb": float(delta.mean()),
                "max_abs_rgb": int(delta.max()),
                "changed_pixels_percent": float(changed.mean() * 100.0),
            }
        )
    return {
        "schema_version": 1,
        "frame_count": len(frames),
        "mean_abs_rgb": float(np.mean([frame["mean_abs_rgb"] for frame in frames])),
        "max_abs_rgb": max(frame["max_abs_rgb"] for frame in frames),
        "mean_changed_pixels_percent": float(
            np.mean([frame["changed_pixels_percent"] for frame in frames])
        ),
        "identical_frames": sum(
            frame["reference_sha256"] == frame["variant_sha256"] for frame in frames
        ),
        "frames": frames,
    }


def compare(reference: Path, variant: Path, pattern: str) -> dict:
    reference_files = {path.name: path for path in reference.glob(pattern)}
    variant_files = {path.name: path for path in variant.glob(pattern)}
    names = sorted(reference_files.keys() & variant_files.keys())
    if not names:
        raise ValueError("no matching images")
    missing_reference = sorted(variant_files.keys() - reference_files.keys())
    missing_variant = sorted(reference_files.keys() - variant_files.keys())
    if missing_reference or missing_variant:
        raise ValueError(
            f"image sets differ: missing reference={len(missing_reference)} missing variant={len(missing_variant)}"
        )
    return compare_pairs(
        [
            (reference_files[name], variant_files[name], {"frame": name})
            for name in names
        ]
    )


def metadata_frames(directory: Path, pattern: str) -> list[dict]:
    metadata = directory / "capture.jsonl"
    if not metadata.is_file():
        raise ValueError(f"missing metadata file: {metadata}")
    frames = []
    for line_number, line in enumerate(metadata.read_text(encoding="utf-8").splitlines(), 1):
        if not line.strip():
            continue
        try:
            record = json.loads(line)
        except json.JSONDecodeError as error:
            raise ValueError(f"{metadata}:{line_number}: invalid JSON: {error}") from error
        if record.get("record") != "frame":
            continue
        filename = record.get("file")
        if (
            not isinstance(filename, str)
            or Path(filename).name != filename
            or not fnmatch.fnmatch(filename, pattern)
        ):
            continue
        if not isinstance(record.get("guest_vsync"), int):
            raise ValueError(f"{metadata}:{line_number}: frame has no integer guest_vsync")
        image = directory / filename
        if not image.is_file():
            raise ValueError(f"{metadata}:{line_number}: missing image {filename}")
        frames.append({**record, "path": image})
    if not frames:
        raise ValueError(f"metadata has no frames matching {pattern!r}")
    return frames


def compare_aligned(reference: Path, variant: Path, pattern: str, max_vsync_delta: int) -> dict:
    reference_frames = metadata_frames(reference, pattern)
    variant_frames = metadata_frames(variant, pattern)
    relative_clock = all(
        isinstance(frame.get("gameplay_offset_vsync"), int)
        for frame in reference_frames + variant_frames
    )
    clock = "gameplay_offset_vsync" if relative_clock else "guest_vsync"
    remaining_variant = set(range(len(variant_frames)))
    pairs: list[tuple[Path, Path, dict]] = []
    unmatched_reference = 0
    deltas: list[int] = []
    for reference_frame in reference_frames:
        candidates = [
            index
            for index in remaining_variant
            if variant_frames[index].get("capture_point") == reference_frame.get("capture_point")
        ]
        if not candidates:
            unmatched_reference += 1
            continue
        variant_index = min(
            candidates,
            key=lambda index: abs(variant_frames[index][clock] - reference_frame[clock]),
        )
        variant_frame = variant_frames[variant_index]
        delta = variant_frame[clock] - reference_frame[clock]
        if abs(delta) > max_vsync_delta:
            unmatched_reference += 1
            continue
        remaining_variant.remove(variant_index)
        deltas.append(delta)
        pairs.append(
            (
                reference_frame["path"],
                variant_frame["path"],
                {
                    "reference_frame": reference_frame["file"],
                    "variant_frame": variant_frame["file"],
                    "reference_guest_vsync": reference_frame["guest_vsync"],
                    "variant_guest_vsync": variant_frame["guest_vsync"],
                    "guest_vsync_delta": variant_frame["guest_vsync"] - reference_frame["guest_vsync"],
                    "alignment_delta": delta,
                    "reference_gameplay_offset_vsync": reference_frame.get("gameplay_offset_vsync"),
                    "variant_gameplay_offset_vsync": variant_frame.get("gameplay_offset_vsync"),
                    "capture_point": reference_frame.get("capture_point"),
                },
            )
        )
    if not pairs:
        raise ValueError("no metadata-linked frames aligned within the requested tolerance")
    summary = compare_pairs(pairs)
    summary["alignment"] = {
        "clock": clock,
        "max_allowed_delta": max_vsync_delta,
        "max_abs_delta": max(abs(delta) for delta in deltas),
        "unmatched_reference": unmatched_reference,
        "unmatched_variant": len(remaining_variant),
    }
    return summary


def self_test() -> None:
    with tempfile.TemporaryDirectory(prefix="kz-image-compare-") as temp:
        root = Path(temp)
        reference = root / "reference"
        variant = root / "variant"
        reference.mkdir()
        variant.mkdir()
        left = np.zeros((2, 2, 3), dtype=np.uint8)
        right = left.copy()
        right[0, 0] = [3, 6, 9]
        Image.fromarray(left, "RGB").save(reference / "frame.png")
        Image.fromarray(right, "RGB").save(variant / "frame.png")
        summary = compare(reference, variant, "*.png")
        if summary["frame_count"] != 1 or summary["identical_frames"] != 0:
            raise ValueError("self-test frame accounting failed")
        if summary["max_abs_rgb"] != 9 or summary["mean_changed_pixels_percent"] != 25.0:
            raise ValueError("self-test pixel metrics failed")
        Image.fromarray(left, "RGB").save(reference / "aligned_left.png")
        Image.fromarray(right, "RGB").save(variant / "aligned_right.png")
        (reference / "capture.jsonl").write_text(
            json.dumps(
                {
                    "record": "frame",
                    "file": "aligned_left.png",
                    "guest_vsync": 100,
                    "gameplay_offset_vsync": 10,
                    "capture_point": "gs_internal",
                }
            )
            + "\n",
            encoding="utf-8",
        )
        (variant / "capture.jsonl").write_text(
            json.dumps(
                {
                    "record": "frame",
                    "file": "aligned_right.png",
                    "guest_vsync": 102,
                    "gameplay_offset_vsync": 10,
                    "capture_point": "gs_internal",
                }
            )
            + "\n",
            encoding="utf-8",
        )
        aligned = compare_aligned(reference, variant, "aligned_*.png", 2)
        if (
            aligned["frame_count"] != 1
            or aligned["alignment"]["clock"] != "gameplay_offset_vsync"
            or aligned["alignment"]["max_abs_delta"] != 0
            or aligned["alignment"]["unmatched_reference"] != 0
            or aligned["alignment"]["unmatched_variant"] != 0
        ):
            raise ValueError("self-test metadata alignment failed")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reference", nargs="?", type=Path)
    parser.add_argument("variant", nargs="?", type=Path)
    parser.add_argument("--glob", default="*.png")
    parser.add_argument(
        "--align-metadata",
        action="store_true",
        help="pair frames by capture point and nearest guest_vsync instead of basename",
    )
    parser.add_argument(
        "--max-vsync-delta",
        type=int,
        default=120,
        help="maximum guest-vsync distance for --align-metadata (default: 120)",
    )
    parser.add_argument("--summary", type=Path)
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()
    try:
        if args.selftest:
            self_test()
            print("Image compare self-test: PASS")
            return 0
        if args.reference is None or args.variant is None:
            parser.error("reference and variant directories are required")
        if args.max_vsync_delta < 0:
            raise ValueError("--max-vsync-delta must be nonnegative")
        if args.align_metadata:
            summary = compare_aligned(
                args.reference.resolve(),
                args.variant.resolve(),
                args.glob,
                args.max_vsync_delta,
            )
        else:
            summary = compare(args.reference.resolve(), args.variant.resolve(), args.glob)
        if args.summary:
            args.summary.parent.mkdir(parents=True, exist_ok=True)
            args.summary.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
        print(
            f"Image compare: PASS ({summary['frame_count']} frames, "
            f"identical={summary['identical_frames']}, mean_abs_rgb={summary['mean_abs_rgb']:.4f}, "
            f"changed={summary['mean_changed_pixels_percent']:.3f}%, max={summary['max_abs_rgb']})"
        )
        if "alignment" in summary:
            alignment = summary["alignment"]
            print(
                "Alignment: "
                f"max |guest_vsync delta|={alignment['max_abs_delta']}, "
                f"unmatched={alignment['unmatched_reference']}/{alignment['unmatched_variant']}"
            )
        return 0
    except (OSError, ValueError) as error:
        print(f"Image compare: FAIL: {error}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
