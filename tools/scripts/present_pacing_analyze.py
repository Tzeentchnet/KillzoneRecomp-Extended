"""Summarize GS-vsync completion pacing from a KZGS_PRESENT_LOG file.

The log records the time GS vsync finished (QPC ns), the field, and the GS vsync duration (us). A submission can be
triggered by a display-register change or GIF packets without a new world image. These intervals therefore measure
presentation-side cadence, not unique game frames or necessarily actual display flips. Bunched or long intervals can
contribute to judder in external frame generation (Lossless Scaling, AFMF, Smooth Motion).

The summary contains only timing statistics. The verdict thresholds are heuristics for comparing runs, not a
qualification of perceived smoothness.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import statistics
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Any

SCHEMA_VERSION = 1
BUNCHED_RATIO = 0.5  # interval below half the nominal period: two completions close together
LONG_RATIO = 1.5  # interval above 1.5 nominal periods: a hitch or a dropped presentation
EVEN_TOLERANCE = 0.10  # within +/-10% of the nominal period
EVEN_P95_RATIO = 1.25
EVEN_MIN_WITHIN = 0.90
EVEN_MAX_IRREGULAR = 0.01
CAPTURE_BOUNDARY_TOLERANCE_S = 0.5


@dataclass(frozen=True)
class CaptureWindow:
    first_present_seq: int
    last_present_seq: int
    first_guest_s: float
    last_guest_s: float


def load_times_ns(path: Path) -> list[int]:
    try:
        with path.open(newline="", encoding="utf-8") as handle:
            reader = csv.DictReader(handle)
            if reader.fieldnames is None or "ns" not in reader.fieldnames:
                raise ValueError("present log must have an 'ns' column (KZGS_PRESENT_LOG format)")
            times = []
            for line, row in enumerate(reader, start=2):
                try:
                    times.append(int(row["ns"]))
                except (TypeError, ValueError) as error:
                    raise ValueError(f"line {line}: invalid ns value") from error
    except OSError as error:
        raise ValueError(f"could not read present log: {error}") from error
    return times


def capture_window_rows(path: Path, from_s: float, to_s: float) -> CaptureWindow:
    """Map a guest-time window to present-log rows using capture.jsonl frame or timing-sample records.

    Each record carries the guest time and present_seq, the number of presentations so far, which is also the
    one-based present-log row number. Both boundaries must have a nearby sample.
    """
    if not (math.isfinite(from_s) and math.isfinite(to_s)) or to_s <= from_s:
        raise ValueError("the guest-time window must have from < to")
    points = []
    try:
        with path.open(encoding="utf-8") as handle:
            for line in handle:
                record = json.loads(line)
                if record.get("record") in ("frame", "timing_sample"):
                    guest_s = float(record["guest_time_s"])
                    present_seq = int(record["present_seq"])
                    if not math.isfinite(guest_s) or present_seq < 0:
                        raise ValueError("invalid guest time or presentation sequence")
                    points.append((guest_s, present_seq))
    except (OSError, ValueError, KeyError, TypeError) as error:
        raise ValueError(f"could not read capture log: {error}") from error
    if len(points) < 2:
        raise ValueError("the capture log has fewer than two timing records")
    first = min(points, key=lambda point: abs(point[0] - from_s))
    last = min(points, key=lambda point: abs(point[0] - to_s))
    for label, target, point in (("start", from_s, first), ("end", to_s, last)):
        if abs(point[0] - target) > CAPTURE_BOUNDARY_TOLERANCE_S:
            raise ValueError(
                f"no timing sample within {CAPTURE_BOUNDARY_TOLERANCE_S:g} s of the requested "
                f"guest-time {label} ({target:g} s); nearest is {point[0]:g} s"
            )
    if first[0] >= last[0] or first[1] >= last[1]:
        raise ValueError("capture boundaries have an invalid presentation sequence")
    return CaptureWindow(first[1], last[1], first[0], last[0])


def select_capture_window(times_ns: list[int], window: CaptureWindow) -> list[int]:
    if window.last_present_seq > len(times_ns):
        raise ValueError("capture presentation sequence exceeds the present log")
    # Start after the first capture so its readback stall is outside the measurement.
    return times_ns[window.first_present_seq : window.last_present_seq]


def percentile(sorted_values: list[float], fraction: float) -> float:
    if not sorted_values:
        return 0.0
    index = min(len(sorted_values) - 1, max(0, math.ceil(fraction * len(sorted_values)) - 1))
    return sorted_values[index]


def analyze(
    times_ns: list[int],
    target_hz: float | None = None,
    skip_seconds: float = 0.0,
    last_seconds: float | None = None,
) -> dict[str, Any]:
    if target_hz is not None and (not math.isfinite(target_hz) or target_hz <= 0.0):
        raise ValueError("target rate must be a positive number")
    if skip_seconds < 0.0 or not math.isfinite(skip_seconds):
        raise ValueError("skip seconds must be a non-negative number")
    if last_seconds is not None and (not math.isfinite(last_seconds) or last_seconds <= 0.0):
        raise ValueError("last seconds must be a positive number")
    if any(later < earlier for earlier, later in zip(times_ns, times_ns[1:])):
        raise ValueError("present log timestamps are not monotonic")
    if times_ns and skip_seconds > 0.0:
        start = times_ns[0] + int(skip_seconds * 1e9)
        times_ns = [value for value in times_ns if value >= start]
    if times_ns and last_seconds is not None:
        start = times_ns[-1] - int(last_seconds * 1e9)
        times_ns = [value for value in times_ns if value >= start]
    if len(times_ns) < 3:
        raise ValueError("need at least three presented frames after skipping")

    intervals = [(later - earlier) / 1e6 for earlier, later in zip(times_ns, times_ns[1:])]
    ordered = sorted(intervals)
    median = statistics.median(intervals)
    nominal = 1000.0 / target_hz if target_hz else median
    if nominal <= 0.0:
        raise ValueError("nominal frame period is zero; the log has duplicate timestamps")
    duration_s = (times_ns[-1] - times_ns[0]) / 1e9
    within = sum(1 for value in intervals if abs(value - nominal) <= EVEN_TOLERANCE * nominal)
    bunched = sum(1 for value in intervals if value < BUNCHED_RATIO * nominal)
    long = sum(1 for value in intervals if value > LONG_RATIO * nominal)
    successive = [abs(later - earlier) for earlier, later in zip(intervals, intervals[1:])]
    p95 = percentile(ordered, 0.95)
    within_fraction = within / len(intervals)
    irregular_fraction = (bunched + long) / len(intervals)
    even = (
        p95 <= EVEN_P95_RATIO * nominal
        and within_fraction >= EVEN_MIN_WITHIN
        and irregular_fraction <= EVEN_MAX_IRREGULAR
    )
    return {
        "schema_version": SCHEMA_VERSION,
        "frames": len(times_ns),
        "duration_s": round(duration_s, 3),
        "mean_fps": round((len(times_ns) - 1) / duration_s, 3) if duration_s > 0 else 0.0,
        "nominal_ms": round(nominal, 4),
        "nominal_source": "target" if target_hz else "median",
        "interval_ms": {
            "mean": round(statistics.fmean(intervals), 4),
            "stdev": round(statistics.pstdev(intervals), 4),
            "min": round(ordered[0], 4),
            "p50": round(median, 4),
            "p95": round(p95, 4),
            "p99": round(percentile(ordered, 0.99), 4),
            "max": round(ordered[-1], 4),
        },
        "within_10pct": round(within_fraction, 4),
        "bunched": bunched,
        "long": long,
        "mean_successive_delta_ms": round(statistics.fmean(successive), 4) if successive else 0.0,
        "verdict": "even" if even else "uneven",
    }


def synthetic(intervals_ms: list[float]) -> list[int]:
    times = [1_000_000_000]
    for value in intervals_ms:
        times.append(times[-1] + int(round(value * 1e6)))
    return times


def self_test() -> None:
    steady = analyze(synthetic([16.6667] * 600), target_hz=60.0)
    assert steady["verdict"] == "even", steady
    assert steady["bunched"] == 0 and steady["long"] == 0
    assert abs(steady["mean_fps"] - 60.0) < 0.01, steady

    # A hitch followed by a catch-up frame: one long and one bunched interval every 60 frames.
    hitchy = ([16.6667] * 58 + [33.3333, 2.0]) * 10
    uneven = analyze(synthetic(hitchy), target_hz=60.0)
    assert uneven["verdict"] == "uneven", uneven
    assert uneven["bunched"] == 10 and uneven["long"] == 10, uneven

    # The median is the nominal period when no target is given (30 fps content on a 60 Hz clock).
    half = analyze(synthetic([33.3333] * 300))
    assert half["nominal_source"] == "median" and abs(half["nominal_ms"] - 33.3333) < 0.001, half
    assert half["verdict"] == "even", half

    skipped = analyze(synthetic([100.0] * 5 + [16.6667] * 300), target_hz=60.0, skip_seconds=0.5)
    assert skipped["long"] == 0 and skipped["verdict"] == "even", skipped

    with tempfile.TemporaryDirectory() as directory:
        capture = Path(directory) / "capture.jsonl"
        records = [{"record": "run"}] + [
            {
                "record": "timing_sample" if t in (20, 60) else "frame",
                "guest_time_s": t + 0.01,
                "present_seq": t * 60,
            }
            for t in range(0, 100, 10)
        ]
        capture.write_text("".join(json.dumps(r) + "\n" for r in records), encoding="utf-8")
        window = capture_window_rows(capture, 20, 60)
        assert window == CaptureWindow(1200, 3600, 20.01, 60.01)
        assert select_capture_window(list(range(1, 3601)), window) == list(range(1201, 3601))
        for from_s, to_s in ((21, 60), (20, 59), (91, 99)):
            try:
                capture_window_rows(capture, from_s, to_s)
            except ValueError:
                pass
            else:
                raise AssertionError("a window with a missing boundary capture must be rejected")
        try:
            select_capture_window([1, 2, 3], window)
        except ValueError:
            pass
        else:
            raise AssertionError("capture sequences beyond the present log must be rejected")

    tail = analyze(synthetic([100.0] * 50 + [16.6667] * 600), target_hz=60.0, last_seconds=5.0)
    assert tail["long"] == 0 and tail["frames"] == 300, tail

    for bad in ([], [5]):
        try:
            analyze(synthetic(bad))
        except ValueError:
            pass
        else:
            raise AssertionError("short logs must be rejected")
    try:
        analyze([3, 2, 1, 0])
    except ValueError:
        pass
    else:
        raise AssertionError("non-monotonic logs must be rejected")

    with tempfile.TemporaryDirectory() as directory:
        log = Path(directory) / "present.csv"
        rows = "".join(f"{value},0,1000\n" for value in synthetic([16.6667] * 10))
        log.write_text("ns,field,vsync_us\n" + rows, encoding="utf-8")
        assert len(load_times_ns(log)) == 11
        log.write_text("time\n1\n", encoding="utf-8")
        try:
            load_times_ns(log)
        except ValueError:
            pass
        else:
            raise AssertionError("a log without an ns column must be rejected")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("log", nargs="?", type=Path, help="KZGS_PRESENT_LOG CSV file")
    parser.add_argument("--target-hz", type=float, help="nominal frame rate; default: the median interval")
    parser.add_argument("--skip-seconds", type=float, default=0.0, help="ignore this much of the start")
    parser.add_argument("--last-seconds", type=float, help="analyze only this much of the end (after --skip-seconds)")
    parser.add_argument("--capture-log", type=Path, help="capture.jsonl from the same run, for a guest-time window")
    parser.add_argument("--from-guest-s", type=float, help="window start in guest seconds (needs --capture-log)")
    parser.add_argument("--to-guest-s", type=float, help="window end in guest seconds (needs --capture-log)")
    parser.add_argument("--output", type=Path, help="also write the JSON summary to this file")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        print("present pacing analyzer self-test passed")
        return 0
    if args.log is None:
        parser.error("a present log is required")
    try:
        times = load_times_ns(args.log)
        window = (args.capture_log, args.from_guest_s, args.to_guest_s)
        capture_window = None
        if any(value is not None for value in window):
            if any(value is None for value in window):
                raise ValueError("--capture-log, --from-guest-s and --to-guest-s are used together")
            capture_window = capture_window_rows(args.capture_log, args.from_guest_s, args.to_guest_s)
            times = select_capture_window(times, capture_window)
        summary = analyze(times, args.target_hz, args.skip_seconds, args.last_seconds)
        if capture_window is not None:
            summary["capture_window"] = {
                "requested_from_guest_s": args.from_guest_s,
                "requested_to_guest_s": args.to_guest_s,
                "first_guest_s": capture_window.first_guest_s,
                "last_guest_s": capture_window.last_guest_s,
                "first_present_seq": capture_window.first_present_seq,
                "last_present_seq": capture_window.last_present_seq,
            }
    except ValueError as error:
        parser.error(str(error))
    text = json.dumps(summary, indent=2)
    print(text)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text + "\n", encoding="utf-8")
    return 0 if summary["verdict"] == "even" else 1


if __name__ == "__main__":
    raise SystemExit(main())
