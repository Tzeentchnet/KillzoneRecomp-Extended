"""Compare matching PNG frames and emit protected-content-safe pixel-delta metrics."""

from __future__ import annotations

import argparse
import hashlib
import json
import tempfile
from pathlib import Path

import numpy as np
from PIL import Image


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


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

    frames = []
    for name in names:
        left = np.asarray(Image.open(reference_files[name]).convert("RGB"), dtype=np.int16)
        right = np.asarray(Image.open(variant_files[name]).convert("RGB"), dtype=np.int16)
        if left.shape != right.shape:
            raise ValueError(f"{name}: dimensions differ: {left.shape} vs {right.shape}")
        delta = np.abs(right - left)
        changed = np.any(delta != 0, axis=2)
        frames.append(
            {
                "frame": name,
                "width": int(left.shape[1]),
                "height": int(left.shape[0]),
                "reference_sha256": sha256(reference_files[name]),
                "variant_sha256": sha256(variant_files[name]),
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


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reference", nargs="?", type=Path)
    parser.add_argument("variant", nargs="?", type=Path)
    parser.add_argument("--glob", default="*.png")
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
        summary = compare(args.reference.resolve(), args.variant.resolve(), args.glob)
        if args.summary:
            args.summary.parent.mkdir(parents=True, exist_ok=True)
            args.summary.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
        print(
            f"Image compare: PASS ({summary['frame_count']} frames, "
            f"identical={summary['identical_frames']}, mean_abs_rgb={summary['mean_abs_rgb']:.4f}, "
            f"changed={summary['mean_changed_pixels_percent']:.3f}%, max={summary['max_abs_rgb']})"
        )
        return 0
    except (OSError, ValueError) as error:
        print(f"Image compare: FAIL: {error}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
