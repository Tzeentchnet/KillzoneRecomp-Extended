"""Validate and summarize AUDIO-01 SPU2/SDL callback diagnostic captures."""

from __future__ import annotations

import argparse
import csv
import json
from pathlib import Path
from typing import Any


SCHEMA_VERSION = 1
RATE = 48_000
FIELDS = (
    "schema_version",
    "callback",
    "host_us",
    "callback_gap_us",
    "additional_bytes",
    "total_bytes",
    "spu_produced_frames",
    "spu_dropped_frames",
    "diagnostic_dropped_records",
    "spu_queue_before_frames",
    "spu_skipped_frames",
    "spu_read_frames",
    "prime_silence_frames",
    "underrun_silence_frames",
    "spu_queue_after_frames",
    "sdl_queued_before_bytes",
    "sdl_queued_after_bytes",
    "sdl_available_before_bytes",
    "sdl_available_after_bytes",
    "put_failures",
)
SDL_FIELDS = (
    "sdl_queued_before_bytes",
    "sdl_queued_after_bytes",
    "sdl_available_before_bytes",
    "sdl_available_after_bytes",
)


def load_trace(path: Path) -> list[dict[str, int]]:
    try:
        stream = path.open(newline="", encoding="utf-8")
    except OSError as error:
        raise ValueError(f"cannot open trace {path}: {error}") from error
    with stream:
        reader = csv.DictReader(stream)
        missing = [field for field in FIELDS if field not in (reader.fieldnames or ())]
        if missing:
            raise ValueError(f"trace is missing columns: {', '.join(missing)}")
        rows: list[dict[str, int]] = []
        for line_number, source in enumerate(reader, 2):
            try:
                row = {field: int(source[field]) for field in FIELDS}
            except (TypeError, ValueError) as error:
                raise ValueError(f"{path}:{line_number}: every diagnostic field must be an integer") from error
            if row["schema_version"] != SCHEMA_VERSION:
                raise ValueError(
                    f"{path}:{line_number}: unsupported schema {row['schema_version']}"
                )
            for field, value in row.items():
                if field in SDL_FIELDS:
                    if value < -1:
                        raise ValueError(f"{path}:{line_number}: {field} must be at least -1")
                elif value < 0:
                    raise ValueError(f"{path}:{line_number}: {field} must be nonnegative")
            if rows:
                if row["callback"] != rows[-1]["callback"] + 1:
                    raise ValueError(f"{path}:{line_number}: callback sequence is not contiguous")
                if row["host_us"] < rows[-1]["host_us"]:
                    raise ValueError(f"{path}:{line_number}: host_us moved backwards")
                for field in (
                    "spu_produced_frames",
                    "spu_dropped_frames",
                    "diagnostic_dropped_records",
                ):
                    if row[field] < rows[-1][field]:
                        raise ValueError(f"{path}:{line_number}: {field} moved backwards")
            rows.append(row)
    if not rows:
        raise ValueError(f"trace is empty: {path}")
    return rows


def range_summary(
    rows: list[dict[str, int]], dropped_before: int, diagnostic_dropped_before: int
) -> dict[str, Any]:
    first = rows[0]
    last = rows[-1]
    dropped = last["spu_dropped_frames"] - dropped_before
    diagnostic_dropped = (
        last["diagnostic_dropped_records"] - diagnostic_dropped_before
    )
    skipped = sum(row["spu_skipped_frames"] for row in rows)
    underrun = sum(row["underrun_silence_frames"] for row in rows)
    put_failures = sum(row["put_failures"] for row in rows)
    previous_dropped = dropped_before
    loss_events = 0
    for row in rows:
        if (
            row["spu_dropped_frames"] > previous_dropped
            or row["spu_skipped_frames"] > 0
            or row["underrun_silence_frames"] > 0
            or row["put_failures"] > 0
        ):
            loss_events += 1
        previous_dropped = row["spu_dropped_frames"]
    query_failures = sum(any(row[field] < 0 for field in SDL_FIELDS) for row in rows)
    return {
        "host_seconds": {
            "first": round(first["host_us"] / 1_000_000, 6),
            "last": round(last["host_us"] / 1_000_000, 6),
            "span": round((last["host_us"] - first["host_us"]) / 1_000_000, 6),
        },
        "callbacks": len(rows),
        "max_callback_gap_ms": round(max(row["callback_gap_us"] for row in rows) / 1000, 3),
        "spu_produced_frames": {
            "first": first["spu_produced_frames"],
            "last": last["spu_produced_frames"],
            "raw_wav_seconds_first": round(first["spu_produced_frames"] / RATE, 6),
            "raw_wav_seconds_last": round(last["spu_produced_frames"] / RATE, 6),
        },
        "spu_ring_frames": {
            "minimum": min(row["spu_queue_after_frames"] for row in rows),
            "maximum": max(row["spu_queue_before_frames"] for row in rows),
        },
        "sdl_input_queued_bytes": {
            "minimum": min(row["sdl_queued_before_bytes"] for row in rows),
            "maximum": max(row["sdl_queued_after_bytes"] for row in rows),
        },
        "sdl_output_available_bytes": {
            "minimum": min(row["sdl_available_before_bytes"] for row in rows),
            "maximum": max(row["sdl_available_after_bytes"] for row in rows),
        },
        "frames": {
            "requested": sum(row["additional_bytes"] // 4 for row in rows),
            "read": sum(row["spu_read_frames"] for row in rows),
            "prime_silence": sum(row["prime_silence_frames"] for row in rows),
            "underrun_silence": underrun,
            "skipped": skipped,
            "producer_dropped": dropped,
        },
        "put_failures": put_failures,
        "loss_events": loss_events,
        "sdl_query_failures": query_failures,
        "diagnostic_dropped_records": diagnostic_dropped,
        "host_path_result": (
            "diagnostic_records_lost"
            if diagnostic_dropped
            else "sdl_queue_metrics_failed"
            if query_failures
            else "loss_observed"
            if dropped or skipped or underrun or put_failures
            else "no_loss_counters_observed"
        ),
    }


def summarize(
    rows: list[dict[str, int]], from_seconds: float | None, to_seconds: float | None
) -> dict[str, Any]:
    lower_us = 0 if from_seconds is None else round(from_seconds * 1_000_000)
    upper_us = None if to_seconds is None else round(to_seconds * 1_000_000)
    selected = [
        row
        for row in rows
        if row["host_us"] >= lower_us and (upper_us is None or row["host_us"] <= upper_us)
    ]
    if not selected:
        raise ValueError("the requested host-time range contains no callbacks")
    first_index = rows.index(selected[0])
    dropped_before = rows[first_index - 1]["spu_dropped_frames"] if first_index else 0
    diagnostic_dropped_before = (
        rows[first_index - 1]["diagnostic_dropped_records"] if first_index else 0
    )
    return {
        "schema_version": SCHEMA_VERSION,
        "trace_callbacks": len(rows),
        "selected": range_summary(
            selected, dropped_before, diagnostic_dropped_before
        ),
        "interpretation": {
            "producer_or_guest": (
                "Inspect KZ_AUDIO_WAV at selected.spu_produced_frames raw-WAV times. "
                "If the heard truncation is already present there, the loss precedes the host callback."
            ),
            "host_callback": (
                "host_path_result=loss_observed identifies ring overflow/skip, inserted underrun silence, "
                "or SDL submission failure in the selected range. diagnostic_records_lost means the capture "
                "itself is incomplete and must be repeated."
            ),
            "output_device": (
                "If the raw WAV is intact and host_path_result reports no loss counters, the remaining boundary "
                "is SDL conversion/backend or the output device. A queue-metrics failure makes that conclusion "
                "incomplete."
            ),
        },
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=Path, help="KZ_AUDIO_DIAG schema-1 CSV")
    parser.add_argument("--from-seconds", type=float, help="include callbacks at or after this host time")
    parser.add_argument("--to-seconds", type=float, help="include callbacks at or before this host time")
    parser.add_argument("--summary", type=Path, help="also write the JSON summary to this path")
    args = parser.parse_args()
    if args.from_seconds is not None and args.from_seconds < 0:
        parser.error("--from-seconds must be nonnegative")
    if args.to_seconds is not None and args.to_seconds < 0:
        parser.error("--to-seconds must be nonnegative")
    if (
        args.from_seconds is not None
        and args.to_seconds is not None
        and args.from_seconds > args.to_seconds
    ):
        parser.error("--from-seconds must not exceed --to-seconds")
    try:
        result = summarize(load_trace(args.trace), args.from_seconds, args.to_seconds)
    except ValueError as error:
        parser.error(str(error))
    text = json.dumps(result, indent=2)
    print(text)
    if args.summary:
        args.summary.parent.mkdir(parents=True, exist_ok=True)
        args.summary.write_text(text + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
