"""Validate and summarize TEST-01 capture.jsonl files without exposing protected content."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
import tempfile
from collections import Counter
from pathlib import Path
from typing import Any


SCHEMA_VERSION = 1
CAPTURE_POINTS = {
    "gs_internal",
    "gs_internal_burst",
    "presented_output",
    "swapchain_backbuffer",
}
PIPELINE_ORDER = [
    "gs_merge",
    "aa",
    "sharp_prescale_if_upscaling",
    "crop_aspect",
    "cas",
    "present_filter",
]


def load_records(directory: Path) -> list[dict[str, Any]]:
    metadata = directory / "capture.jsonl"
    if not metadata.is_file():
        raise ValueError(f"missing metadata file: {metadata}")
    records: list[dict[str, Any]] = []
    for line_number, line in enumerate(metadata.read_text(encoding="utf-8").splitlines(), 1):
        if not line.strip():
            continue
        try:
            record = json.loads(line)
        except json.JSONDecodeError as error:
            raise ValueError(f"{metadata}:{line_number}: invalid JSON: {error}") from error
        if not isinstance(record, dict):
            raise ValueError(f"{metadata}:{line_number}: record must be an object")
        if record.get("schema_version") != SCHEMA_VERSION:
            raise ValueError(
                f"{metadata}:{line_number}: unsupported schema {record.get('schema_version')!r}"
            )
        records.append(record)
    if not records:
        raise ValueError(f"metadata file is empty: {metadata}")
    return records


def is_path_like(value: str) -> bool:
    return bool(re.match(r"^[A-Za-z]:[\\/]", value)) or value.startswith(("/", "\\\\"))


def image_digest(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def validate(directory: Path, require_fire: bool) -> tuple[list[dict[str, Any]], dict[str, Any]]:
    records = load_records(directory)
    runs = [record for record in records if record.get("record") == "run"]
    if len(runs) != 1 or records[0].get("record") != "run":
        raise ValueError("capture must begin with exactly one run record")
    run = runs[0]
    for key in ("source_id", "clock", "timing_rate_hz", "renderer", "adapter", "effective_config", "testing"):
        if key not in run:
            raise ValueError(f"run record is missing {key}")
    if run["clock"] != "guest_vsync" or not isinstance(run["timing_rate_hz"], int) or run["timing_rate_hz"] <= 0:
        raise ValueError("run clock must be guest_vsync with a positive timing_rate_hz")
    if is_path_like(str(run["adapter"])) or any(
        is_path_like(str(value))
        for values in (run["effective_config"], run["testing"])
        for value in values.values()
        if isinstance(value, str)
    ):
        raise ValueError("run metadata contains an absolute path")
    effective_config = run["effective_config"]
    render_scale = effective_config.get("render_scale")
    output_resolution = effective_config.get("output_resolution")
    if render_scale is not None or output_resolution is not None:
        if not isinstance(render_scale, dict) or not isinstance(output_resolution, dict):
            raise ValueError("render_scale and output_resolution must both be objects")
        if render_scale.get("mode") not in {
            "auto_output_height",
            "fixed_integer",
            "automation_override",
        }:
            raise ValueError("run record has an invalid render scale mode")
        for key in ("configured", "effective", "native_height", "target_height"):
            if not isinstance(render_scale.get(key), int):
                raise ValueError(f"render_scale.{key} must be an integer")
        if not 1 <= render_scale["effective"] <= 8:
            raise ValueError("render_scale.effective must be between 1 and 8")
        if (
            render_scale["native_height"] != 448
            or render_scale["target_height"] != 448 * render_scale["effective"]
        ):
            raise ValueError("render_scale target height does not match its effective scale")
        if (
            not isinstance(output_resolution.get("width"), int)
            or output_resolution["width"] <= 0
            or not isinstance(output_resolution.get("height"), int)
            or output_resolution["height"] <= 0
        ):
            raise ValueError("output_resolution dimensions must be positive integers")
        if effective_config.get("pipeline") != PIPELINE_ORDER:
            raise ValueError("effective pipeline order is missing or invalid")
    for key, minimum in (("render_distance", 1.0), ("lod_scale", 0.25)):
        value = effective_config.get(key)
        if value is not None and (
            isinstance(value, bool)
            or not isinstance(value, (int, float))
            or not math.isfinite(value)
            or not minimum <= value <= 16.0
        ):
            raise ValueError(f"effective_config.{key} is outside its supported range")

    frames = [record for record in records if record.get("record") == "frame"]
    events = [record for record in records if record.get("record") == "event"]
    resolutions = [record for record in records if record.get("record") == "resolution"]
    if not frames:
        raise ValueError("capture has no frame records")

    image_rows: list[dict[str, Any]] = []
    previous_vsync = -1
    for index, frame in enumerate(frames):
        for key in ("frame_id", "guest_vsync", "present_seq", "capture_point", "file", "width", "height"):
            if key not in frame:
                raise ValueError(f"frame {index} is missing {key}")
        if frame["frame_id"] != frame["guest_vsync"]:
            raise ValueError(f"frame {index} frame_id must equal guest_vsync")
        if not isinstance(frame["guest_vsync"], int) or frame["guest_vsync"] < previous_vsync:
            raise ValueError("frame guest_vsync values must be nondecreasing integers")
        previous_vsync = frame["guest_vsync"]
        if frame["capture_point"] not in CAPTURE_POINTS:
            raise ValueError(f"frame {index} has unknown capture point {frame['capture_point']!r}")
        filename = frame["file"]
        if not isinstance(filename, str) or Path(filename).name != filename:
            raise ValueError(f"frame {index} file must be a basename")
        image = directory / filename
        if not image.is_file():
            raise ValueError(f"frame {index} image is missing: {image}")
        if not isinstance(frame["width"], int) or frame["width"] <= 0 or not isinstance(frame["height"], int) or frame["height"] <= 0:
            raise ValueError(f"frame {index} dimensions must be positive integers")
        image_rows.append(
            {
                "frame_id": frame["frame_id"],
                "capture_point": frame["capture_point"],
                "width": frame["width"],
                "height": frame["height"],
                "sha256": image_digest(image),
            }
        )

    fire_events = [event for event in events if event.get("event") == "player_fire_input"]
    if require_fire and not any(event.get("edge") == "press" for event in fire_events):
        raise ValueError("capture has no player_fire_input press event")
    pressed = False
    event_rows: list[dict[str, Any]] = []
    for index, event in enumerate(fire_events):
        if event.get("edge") not in {"press", "release"}:
            raise ValueError(f"fire event {index} has an invalid edge")
        if not isinstance(event.get("guest_vsync"), int) or not isinstance(event.get("present_seq"), int):
            raise ValueError(f"fire event {index} is missing integer frame linkage")
        if event["edge"] == "press":
            if pressed:
                raise ValueError("fire event stream contains two presses without a release")
            pressed = True
        else:
            if not pressed:
                raise ValueError("fire event stream contains a release without a press")
            pressed = False
        event_rows.append(
            {
                "event": event["event"],
                "edge": event["edge"],
                "guest_vsync": event["guest_vsync"],
                "present_seq": event["present_seq"],
            }
        )

    resolution_rows: list[dict[str, Any]] = []
    for index, resolution in enumerate(resolutions):
        for key in ("guest_vsync", "render_width", "render_height", "output_width", "output_height"):
            if not isinstance(resolution.get(key), int):
                raise ValueError(f"resolution {index} is missing integer {key}")
        if any(
            resolution[key] <= 0
            for key in ("render_width", "render_height", "output_width", "output_height")
        ):
            raise ValueError(f"resolution {index} dimensions must be positive")
        output_upscale = (
            resolution["output_width"] > resolution["render_width"]
            or resolution["output_height"] > resolution["render_height"]
        )
        if resolution.get("output_upscale") is not output_upscale:
            raise ValueError(f"resolution {index} has inconsistent output_upscale")
        resolution_rows.append(
            {
                "guest_vsync": resolution["guest_vsync"],
                "render_width": resolution["render_width"],
                "render_height": resolution["render_height"],
                "output_width": resolution["output_width"],
                "output_height": resolution["output_height"],
                "output_upscale": output_upscale,
            }
        )
    if render_scale is not None:
        if not resolution_rows:
            raise ValueError("capture with a render/output contract has no exact resolution record")
        initial_resolution = resolution_rows[0]
        if (
            initial_resolution["output_width"] != output_resolution["width"]
            or initial_resolution["output_height"] != output_resolution["height"]
        ):
            raise ValueError("initial exact output resolution does not match the run contract")
        if initial_resolution["render_height"] != render_scale["target_height"]:
            raise ValueError("initial exact render height does not match the run contract")
        if (
            render_scale["mode"] == "fixed_integer"
            and render_scale["configured"] != render_scale["effective"]
        ):
            raise ValueError("fixed render scale changed from its configured value")
        expected_auto_scale = min(
            8, max(1, (output_resolution["height"] + 447) // 448)
        )
        if (
            render_scale["mode"] == "auto_output_height"
            and render_scale["effective"] != expected_auto_scale
        ):
            raise ValueError("automatic render scale does not cover the declared output height")

    summary = {
        "schema_version": SCHEMA_VERSION,
        "source_id": run["source_id"],
        "clock": run["clock"],
        "timing_rate_hz": run["timing_rate_hz"],
        "renderer": run["renderer"],
        "adapter": run["adapter"],
        "effective_config": run["effective_config"],
        "testing": run["testing"],
        "frame_count": len(frames),
        "event_count": len(events),
        "capture_points": dict(sorted(Counter(frame["capture_point"] for frame in frames).items())),
        "images": image_rows,
        "events": event_rows,
        "resolutions": resolution_rows,
    }
    return records, summary


def compare_summaries(left: dict[str, Any], right: dict[str, Any]) -> dict[str, Any]:
    if left["schema_version"] != right["schema_version"]:
        raise ValueError("captures use different schema versions")
    if left["renderer"] != right["renderer"] or left["effective_config"] != right["effective_config"]:
        raise ValueError("captures use different effective renderer configurations")

    left_events = left["events"]
    right_events = right["events"]
    remaining_right = set(range(len(right_events)))
    event_deltas: list[int] = []
    matched_left: set[int] = set()
    tolerance = max(left["timing_rate_hz"], right["timing_rate_hz"])
    for left_index, left_event in enumerate(left_events):
        candidates = [
            right_index
            for right_index in remaining_right
            if right_events[right_index]["event"] == left_event["event"]
            and right_events[right_index]["edge"] == left_event["edge"]
        ]
        if not candidates:
            continue
        right_index = min(
            candidates,
            key=lambda index: abs(right_events[index]["guest_vsync"] - left_event["guest_vsync"]),
        )
        delta = right_events[right_index]["guest_vsync"] - left_event["guest_vsync"]
        if abs(delta) > tolerance:
            continue
        event_deltas.append(delta)
        matched_left.add(left_index)
        remaining_right.remove(right_index)
    if left_events and right_events and not event_deltas:
        raise ValueError("captures have no events alignable within one guest second")

    left_frames = left["images"]
    right_frames = right["images"]
    paired_frames = min(len(left_frames), len(right_frames))
    frame_deltas = [
        right_frames[index]["frame_id"] - left_frames[index]["frame_id"]
        for index in range(paired_frames)
        if right_frames[index]["capture_point"] == left_frames[index]["capture_point"]
    ]
    return {
        "event_count": len(event_deltas),
        "event_guest_vsync_deltas": event_deltas,
        "max_abs_event_delta": max((abs(delta) for delta in event_deltas), default=0),
        "unmatched_left_events": len(left_events) - len(matched_left),
        "unmatched_right_events": len(remaining_right),
        "paired_frame_count": len(frame_deltas),
        "frame_id_deltas": frame_deltas,
    }


def self_test() -> None:
    with tempfile.TemporaryDirectory(prefix="kz-capture-metadata-") as temp:
        roots = [Path(temp) / "a", Path(temp) / "b"]
        for index, root in enumerate(roots):
            root.mkdir()
            (root / "frame.png").write_bytes(b"png fixture")
            records = [
                {
                    "schema_version": 1,
                    "record": "run",
                    "source_id": "SCUS-97402",
                    "clock": "guest_vsync",
                    "timing_rate_hz": 120,
                    "renderer": "d3d12",
                    "adapter": "fixture",
                    "effective_config": {
                        "upscale": 2,
                        "render_scale": {
                            "mode": "fixed_integer",
                            "configured": 2,
                            "effective": 2,
                            "native_height": 448,
                            "target_height": 896,
                        },
                        "output_resolution": {"width": 1920, "height": 1080},
                        "pipeline": PIPELINE_ORDER,
                        "render_distance": 1.0,
                        "lod_scale": 1.0,
                    },
                    "testing": {"start_level": 0},
                },
                {
                    "schema_version": 1,
                    "record": "resolution",
                    "guest_vsync": 80 + index,
                    "render_width": 1280,
                    "render_height": 896,
                    "output_width": 1920,
                    "output_height": 1080,
                    "output_upscale": True,
                },
                {
                    "schema_version": 1,
                    "record": "event",
                    "event": "player_fire_input",
                    "edge": "press",
                    "guest_vsync": 100 + index,
                    "present_seq": 90 + index,
                },
                {
                    "schema_version": 1,
                    "record": "event",
                    "event": "player_fire_input",
                    "edge": "release",
                    "guest_vsync": 101 + index,
                    "present_seq": 91 + index,
                },
                {
                    "schema_version": 1,
                    "record": "frame",
                    "frame_id": 120 + index,
                    "guest_vsync": 120 + index,
                    "present_seq": 100 + index,
                    "capture_point": "gs_internal",
                    "file": "frame.png",
                    "width": 1280,
                    "height": 896,
                },
                {"schema_version": 1, "record": "run_end", "guest_vsync": 121 + index, "present_seq": 101 + index},
            ]
            (root / "capture.jsonl").write_text(
                "".join(json.dumps(record, separators=(",", ":")) + "\n" for record in records),
                encoding="utf-8",
            )
        _, left = validate(roots[0], True)
        _, right = validate(roots[1], True)
        comparison = compare_summaries(left, right)
        if comparison["max_abs_event_delta"] != 1 or comparison["paired_frame_count"] != 1:
            raise ValueError("self-test comparison produced unexpected alignment")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", nargs="?", type=Path)
    parser.add_argument("--compare", type=Path, help="validate and align a second capture")
    parser.add_argument("--require-fire", action="store_true")
    parser.add_argument("--summary", type=Path, help="write a protected-content-safe JSON summary")
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()

    try:
        if args.selftest:
            self_test()
            print("Capture metadata self-test: PASS")
            return 0
        if args.directory is None:
            parser.error("directory is required unless --selftest is used")
        _, summary = validate(args.directory.resolve(), args.require_fire)
        comparison = None
        if args.compare:
            _, other = validate(args.compare.resolve(), args.require_fire)
            comparison = compare_summaries(summary, other)
        if args.summary:
            args.summary.parent.mkdir(parents=True, exist_ok=True)
            output = dict(summary)
            if comparison is not None:
                output["comparison"] = comparison
            args.summary.write_text(json.dumps(output, indent=2) + "\n", encoding="utf-8")
        message = (
            f"Capture metadata: PASS ({summary['frame_count']} frames, "
            f"{len(summary['events'])} fire events, {summary['renderer']})"
        )
        if comparison is not None:
            message += (
                f"; aligned {comparison['event_count']} events, "
                f"max |guest_vsync delta| {comparison['max_abs_event_delta']}, "
                f"unmatched {comparison['unmatched_left_events']}/{comparison['unmatched_right_events']}"
            )
        print(message)
        return 0
    except (OSError, ValueError) as error:
        print(f"Capture metadata: FAIL: {error}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
