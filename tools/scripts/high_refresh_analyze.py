"""Analyze protected-content-private high-refresh campaign runs into a safe summary."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import re
import tempfile
from pathlib import Path
from typing import Any


SCHEMA_VERSION = 1
ERROR_PATTERN = re.compile(
    r"missing-target|fatal exception|unhandled exception|0xC0000", re.IGNORECASE
)


def load_manifest(path: Path) -> tuple[dict[int, dict[str, Any]], str]:
    try:
        raw = path.read_bytes()
        document = json.loads(raw)
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError(f"could not read scenario manifest: {error}") from error
    if not isinstance(document, dict) or document.get("schema_version") != SCHEMA_VERSION:
        raise ValueError("scenario manifest must be an object with schema_version 1")
    chapters = document.get("chapters")
    if not isinstance(chapters, list):
        raise ValueError("scenario manifest chapters must be an array")
    result: dict[int, dict[str, Any]] = {}
    for index, scenario in enumerate(chapters):
        if not isinstance(scenario, dict):
            raise ValueError(f"scenario chapters[{index}] must be an object")
        chapter = scenario.get("chapter")
        if type(chapter) is not int or not 1 <= chapter <= 11:
            raise ValueError(f"scenario chapters[{index}].chapter must be an integer from 1 to 11")
        if chapter in result:
            raise ValueError(f"scenario chapter {chapter} is duplicated")
        parity_words = scenario.get("parity_words", [])
        if not isinstance(parity_words, list) or not all(
            isinstance(word, str) and word for word in parity_words
        ):
            raise ValueError(f"scenario chapter {chapter} parity_words must be an array of names")
        result[chapter] = scenario
    return result, hashlib.sha256(raw).hexdigest()


def audit_word_specs(scenario: dict[str, Any]) -> list[dict[str, Any]]:
    raw = scenario.get("audit_words", [])
    if not isinstance(raw, list):
        raise ValueError("audit_words must be an array")
    parity_words = set(scenario.get("parity_words", []))
    specs: list[dict[str, Any]] = []
    for index, item in enumerate(raw):
        if isinstance(item, str):
            spec = {"name": item}
        elif isinstance(item, dict):
            spec = dict(item)
        else:
            raise ValueError(f"audit_words[{index}] must be a name or object")
        name = spec.get("name")
        if not isinstance(name, str) or not name:
            raise ValueError(f"audit_words[{index}].name must be a non-empty string")
        if name not in parity_words:
            raise ValueError(f"audit word {name!r} is not listed in parity_words")
        tolerance = spec.get("time_tolerance_s", 0.25)
        minimum = spec.get("min_transitions", 1)
        if type(tolerance) not in (int, float) or not math.isfinite(tolerance) or tolerance < 0:
            raise ValueError(f"audit word {name!r} has an invalid time_tolerance_s")
        if type(minimum) is not int or minimum < 0:
            raise ValueError(f"audit word {name!r} has an invalid min_transitions")
        specs.append(
            {
                "name": name,
                "time_tolerance_s": float(tolerance),
                "min_transitions": minimum,
            }
        )
    return specs


def load_parity(path: Path, rate: int) -> dict[str, Any]:
    if not path.is_file():
        raise ValueError("parity.csv is missing")
    rows: list[dict[str, Any]] = []
    with path.open(newline="", encoding="utf-8") as stream:
        reader = csv.reader(stream)
        try:
            header = next(reader)
        except StopIteration as error:
            raise ValueError("parity.csv is empty") from error
        expected = [
            "wall_s",
            "vsync",
            "now",
            "elapsed",
            "sec_per_tick",
            "player",
            "ctl",
            "x",
            "y",
            "z",
            "heading",
        ]
        if header[: len(expected)] != expected:
            raise ValueError("parity.csv has an unsupported header")
        words = header[len(expected) :]
        for line_number, values in enumerate(reader, 2):
            if not values or values[0].startswith("#ev"):
                continue
            if len(values) != len(header):
                raise ValueError(f"parity.csv line {line_number} has the wrong column count")
            try:
                rows.append(
                    {
                        "wall": float(values[0]),
                        "vsync": int(values[1]),
                        "now": int(values[2]),
                        "spt": float(values[4]),
                        "player": int(values[5], 16),
                        "ctl": int(values[6], 16),
                        "position": tuple(float(value) for value in values[7:10]),
                        "words": {
                            name: int(values[len(expected) + index], 16)
                            for index, name in enumerate(words)
                        },
                    }
                )
            except ValueError as error:
                raise ValueError(f"parity.csv line {line_number} contains an invalid value") from error
    if len(rows) < 2:
        raise ValueError("parity.csv has fewer than two samples")

    game_times: list[float] = []
    game_time = 0.0
    previous_now: int | None = None
    for row in rows:
        now = row["now"]
        if previous_now is not None and now != previous_now:
            delta = (now - previous_now) & 0xFFFFFFFF
            if delta <= rate * 10:
                game_time += min(delta * row["spt"], 0.1337)
        previous_now = now
        game_times.append(game_time)

    gameplay_index = next((index for index, row in enumerate(rows) if row["ctl"] != 0), None)
    if gameplay_index is None:
        raise ValueError("parity.csv never observed a local-player controller")
    gameplay_origin = game_times[gameplay_index]
    timelines: dict[str, list[tuple[int, float]]] = {}
    for name in words:
        timeline: list[tuple[int, float]] = []
        previous: int | None = None
        for index in range(gameplay_index, len(rows)):
            value = rows[index]["words"][name]
            if previous is None or value != previous:
                timeline.append((value, game_times[index] - gameplay_origin))
                previous = value
        timelines[name] = timeline

    gameplay_seconds = game_times[-1] - gameplay_origin
    emulated_seconds = (rows[-1]["vsync"] - rows[gameplay_index]["vsync"]) / rate
    return {
        "samples": len(rows),
        "gameplay_samples": len(rows) - gameplay_index,
        "gameplay_seconds": gameplay_seconds,
        "emulated_seconds": emulated_seconds,
        "game_time_ratio": gameplay_seconds / emulated_seconds if emulated_seconds > 0 else 0.0,
        "final_position": rows[-1]["position"],
        "timelines": timelines,
    }


def load_capture_run(path: Path) -> dict[str, Any]:
    metadata = path / "capture.jsonl"
    if not metadata.is_file():
        raise ValueError("capture.jsonl is missing")
    frame_count = 0
    run: dict[str, Any] | None = None
    for line_number, line in enumerate(metadata.read_text(encoding="utf-8").splitlines(), 1):
        if not line.strip():
            continue
        try:
            record = json.loads(line)
        except json.JSONDecodeError as error:
            raise ValueError(f"capture.jsonl line {line_number} is invalid JSON") from error
        if not isinstance(record, dict) or record.get("schema_version") != SCHEMA_VERSION:
            raise ValueError(f"capture.jsonl line {line_number} has an unsupported record")
        if record.get("record") == "run" and run is None:
            run = record
        elif record.get("record") == "frame":
            frame_count += 1
    if run is None:
        raise ValueError("capture.jsonl has no run record")
    return {"run": run, "frame_count": frame_count}


def analyze_run(
    directory: Path,
    chapter: int,
    rate: int,
    scenario: dict[str, Any],
    default_gameplay_seconds: float,
) -> dict[str, Any]:
    problems: list[str] = []
    try:
        process = json.loads((directory / "process.json").read_text(encoding="utf-8"))
        if not isinstance(process, dict):
            raise ValueError
    except (OSError, json.JSONDecodeError, ValueError):
        process = {}
        problems.append("process.json is missing or invalid")
    if process.get("exit_code") != 0:
        problems.append("process did not exit with code 0")
    if process.get("timed_out") is not False:
        problems.append("process timed out or did not report timeout state")

    stdout = (directory / "stdout.log").read_text(encoding="utf-8", errors="replace") if (
        directory / "stdout.log"
    ).is_file() else ""
    stderr = (directory / "stderr.log").read_text(encoding="utf-8", errors="replace") if (
        directory / "stderr.log"
    ).is_file() else ""
    combined = stdout + "\n" + stderr
    if not re.search(rf"front end: level {chapter}\.", stdout):
        problems.append("requested chapter was not reached")
    if not re.search(r"headless gameplay limit reached at [0-9]+ rendered frames", stdout):
        problems.append("gameplay duration marker is missing")
    if ERROR_PATTERN.search(combined):
        problems.append("guest or host exception signature was reported")
    for index, pattern in enumerate(scenario.get("required_log_patterns", []), 1):
        if not isinstance(pattern, str):
            problems.append(f"required log pattern {index} is not a string")
        elif re.search(pattern, combined) is None:
            problems.append(f"required log pattern {index} was not observed")
    for index, pattern in enumerate(scenario.get("forbidden_log_patterns", []), 1):
        if not isinstance(pattern, str):
            problems.append(f"forbidden log pattern {index} is not a string")
        elif re.search(pattern, combined) is not None:
            problems.append(f"forbidden log pattern {index} was observed")

    try:
        capture = load_capture_run(directory)
        run_record = capture["run"]
        if run_record.get("timing_rate_hz") != rate:
            problems.append("capture metadata timing rate does not match the requested rate")
        testing = run_record.get("testing")
        if not isinstance(testing, dict) or testing.get("start_level") != chapter:
            problems.append("capture metadata does not identify the requested chapter")
        if capture["frame_count"] == 0:
            problems.append("capture metadata contains no frames")
    except ValueError as error:
        capture = {"frame_count": 0}
        problems.append(str(error))

    try:
        parity = load_parity(directory / "parity.csv", rate)
        minimum = scenario.get("min_gameplay_seconds", default_gameplay_seconds)
        if type(minimum) not in (int, float) or not math.isfinite(minimum) or minimum <= 0:
            problems.append("scenario min_gameplay_seconds is invalid")
        elif parity["gameplay_seconds"] + 2.0 < float(minimum):
            problems.append("parity trace is shorter than the required gameplay duration")
        ratio_error = scenario.get("max_game_time_ratio_error", 0.01)
        if type(ratio_error) not in (int, float) or not math.isfinite(ratio_error) or ratio_error < 0:
            problems.append("scenario max_game_time_ratio_error is invalid")
        elif abs(parity["game_time_ratio"] - 1.0) > float(ratio_error):
            problems.append("game time diverged from emulated time")
    except ValueError as error:
        parity = {
            "samples": 0,
            "gameplay_samples": 0,
            "gameplay_seconds": 0.0,
            "emulated_seconds": 0.0,
            "game_time_ratio": 0.0,
            "final_position": None,
            "timelines": {},
        }
        problems.append(str(error))

    return {
        "chapter": chapter,
        "rate_hz": rate,
        "passed": not problems,
        "problems": problems,
        "capture_frames": capture["frame_count"],
        "parity_samples": parity["samples"],
        "gameplay_seconds": round(parity["gameplay_seconds"], 6),
        "game_time_ratio": round(parity["game_time_ratio"], 6),
        "peak_working_set_bytes": process.get("peak_working_set_bytes"),
        "_final_position": parity["final_position"],
        "_timelines": parity["timelines"],
    }


def scenario_has_evidence(scenario: dict[str, Any]) -> bool:
    return bool(
        scenario.get("audit_words")
        or scenario.get("required_log_patterns")
        or scenario.get("max_final_position_delta") is not None
    )


def compare_to_baseline(
    baseline: dict[str, Any],
    variant: dict[str, Any],
    scenario: dict[str, Any],
    specs: list[dict[str, Any]],
) -> dict[str, Any]:
    comparison: dict[str, Any] = {"baseline_rate_hz": baseline["rate_hz"], "word_checks": []}
    for spec in specs:
        name = spec["name"]
        base_timeline = baseline["_timelines"].get(name, [])
        variant_timeline = variant["_timelines"].get(name, [])
        check = {
            "name": name,
            "baseline_transitions": max(0, len(base_timeline) - 1),
            "variant_transitions": max(0, len(variant_timeline) - 1),
            "sequence_match": False,
            "max_transition_time_delta_s": None,
        }
        if len(base_timeline) - 1 < spec["min_transitions"]:
            baseline["problems"].append(f"audit word {name!r} has insufficient baseline transitions")
            baseline["passed"] = False
        base_values = [value for value, _ in base_timeline]
        variant_values = [value for value, _ in variant_timeline]
        check["sequence_match"] = base_values == variant_values
        if not check["sequence_match"]:
            variant["problems"].append(f"audit word {name!r} transition sequence differs from 30 Hz")
            variant["passed"] = False
        elif base_timeline:
            maximum = max(
                abs(base_time - variant_time)
                for (_, base_time), (_, variant_time) in zip(base_timeline, variant_timeline)
            )
            check["max_transition_time_delta_s"] = round(maximum, 6)
            if maximum > spec["time_tolerance_s"]:
                variant["problems"].append(
                    f"audit word {name!r} transition timing differs from 30 Hz"
                )
                variant["passed"] = False
        comparison["word_checks"].append(check)

    max_position_delta = scenario.get("max_final_position_delta")
    if max_position_delta is not None:
        if (
            type(max_position_delta) not in (int, float)
            or not math.isfinite(max_position_delta)
            or max_position_delta < 0
        ):
            baseline["problems"].append("scenario max_final_position_delta is invalid")
            baseline["passed"] = False
        elif baseline["_final_position"] is None or variant["_final_position"] is None:
            variant["problems"].append("final player position is unavailable")
            variant["passed"] = False
        else:
            delta = math.dist(baseline["_final_position"], variant["_final_position"])
            comparison["final_position_delta"] = round(delta, 6)
            if delta > float(max_position_delta):
                variant["problems"].append("final player position differs from 30 Hz")
                variant["passed"] = False
    return comparison


def analyze(
    work_root: Path,
    name: str,
    manifest_path: Path,
    rates: list[int],
    chapters: list[int],
    gameplay_seconds: float,
) -> dict[str, Any]:
    scenarios, manifest_hash = load_manifest(manifest_path)
    if 30 not in rates:
        raise ValueError("rates must include the 30 Hz reference")
    if len(rates) != len(set(rates)) or any(rate < 30 or rate > 480 for rate in rates):
        raise ValueError("rates must be unique integers from 30 to 480")
    if len(chapters) != len(set(chapters)) or any(chapter < 1 or chapter > 11 for chapter in chapters):
        raise ValueError("chapters must be unique integers from 1 to 11")

    rows: list[dict[str, Any]] = []
    incomplete: list[str] = []
    for chapter in chapters:
        scenario = scenarios.get(chapter)
        if scenario is None:
            raise ValueError(f"scenario manifest has no entry for chapter {chapter}")
        specs = audit_word_specs(scenario)
        if not scenario_has_evidence(scenario):
            incomplete.append(f"chapter {chapter} has no scenario-specific parity evidence")
        chapter_rows: dict[int, dict[str, Any]] = {}
        for rate in rates:
            directory = work_root / f"{name}_c{chapter:02d}_{rate}hz"
            row = analyze_run(directory, chapter, rate, scenario, gameplay_seconds)
            chapter_rows[rate] = row
            rows.append(row)
        baseline = chapter_rows[30]
        baseline["comparison"] = {"reference": True}
        for rate in rates:
            if rate != 30:
                chapter_rows[rate]["comparison"] = compare_to_baseline(
                    baseline, chapter_rows[rate], scenario, specs
                )

    failed = any(not row["passed"] for row in rows)
    status = "fail" if failed else "incomplete" if incomplete else "pass"
    public_rows = [
        {key: value for key, value in row.items() if not key.startswith("_")} for row in rows
    ]
    return {
        "schema_version": SCHEMA_VERSION,
        "status": status,
        "scenario_sha256": manifest_hash,
        "rates_hz": rates,
        "chapters": chapters,
        "coverage_notes": incomplete,
        "rows": public_rows,
    }


def write_fixture_run(
    root: Path, name: str, chapter: int, rate: int, transition_s: float
) -> None:
    directory = root / f"{name}_c{chapter:02d}_{rate}hz"
    directory.mkdir()
    (directory / "process.json").write_text(
        json.dumps(
            {
                "exit_code": 0,
                "timed_out": False,
                "peak_working_set_bytes": 123456,
            }
        ),
        encoding="utf-8",
    )
    (directory / "stdout.log").write_text(
        f"[kz] front end: level {chapter}.\n"
        "[kz] headless gameplay limit reached at 100 rendered frames\n",
        encoding="utf-8",
    )
    (directory / "stderr.log").write_text("", encoding="utf-8")
    records = [
        {
            "schema_version": 1,
            "record": "run",
            "timing_rate_hz": rate,
            "testing": {"start_level": chapter},
        },
        {
            "schema_version": 1,
            "record": "frame",
            "guest_vsync": rate,
            "file": "gs.png",
        },
    ]
    (directory / "capture.jsonl").write_text(
        "".join(json.dumps(record) + "\n" for record in records), encoding="utf-8"
    )
    (directory / "gs.png").write_bytes(b"fixture")
    with (directory / "parity.csv").open("w", newline="", encoding="utf-8") as stream:
        writer = csv.writer(stream)
        writer.writerow(
            [
                "wall_s",
                "vsync",
                "now",
                "elapsed",
                "sec_per_tick",
                "player",
                "ctl",
                "x",
                "y",
                "z",
                "heading",
                "c:0x100",
            ]
        )
        for tick in range(rate * 3 + 1):
            time = tick / rate
            writer.writerow(
                [
                    f"{time:.6f}",
                    tick,
                    tick,
                    1,
                    f"{1.0 / rate:.9f}",
                    "1000",
                    "2000",
                    "1.0",
                    "2.0",
                    "3.0",
                    "0.0",
                    "1" if time >= transition_s else "0",
                ]
            )


def self_test() -> None:
    with tempfile.TemporaryDirectory(prefix="kz-high-refresh-") as temp:
        root = Path(temp)
        manifest = root / "scenario.json"
        manifest.write_text(
            json.dumps(
                {
                    "schema_version": 1,
                    "chapters": [
                        {
                            "chapter": 1,
                            "input_script": "g0.5:ly=-1:1",
                            "parity_words": ["c:0x100"],
                            "audit_words": [
                                {
                                    "name": "c:0x100",
                                    "time_tolerance_s": 0.05,
                                    "min_transitions": 1,
                                }
                            ],
                            "max_final_position_delta": 0.1,
                            "min_gameplay_seconds": 2,
                        }
                    ],
                }
            ),
            encoding="utf-8",
        )
        write_fixture_run(root, "fixture", 1, 30, 1.0)
        write_fixture_run(root, "fixture", 1, 144, 1.0)
        passed = analyze(root, "fixture", manifest, [30, 144], [1], 2.0)
        if passed["status"] != "pass":
            raise ValueError(f"matching fixture did not pass: {passed}")

        high = root / "fixture_c01_144hz" / "parity.csv"
        high.unlink()
        write_directory = root / "replacement"
        write_directory.mkdir()
        write_fixture_run(write_directory, "fixture", 1, 144, 2.0)
        high.write_bytes((write_directory / "fixture_c01_144hz" / "parity.csv").read_bytes())
        failed = analyze(root, "fixture", manifest, [30, 144], [1], 2.0)
        if failed["status"] != "fail":
            raise ValueError("transition-timing mismatch did not fail")

        document = json.loads(manifest.read_text(encoding="utf-8"))
        document["chapters"][0].pop("audit_words")
        document["chapters"][0].pop("max_final_position_delta")
        manifest.write_text(json.dumps(document), encoding="utf-8")
        incomplete = analyze(root, "fixture", manifest, [30], [1], 2.0)
        if incomplete["status"] != "incomplete":
            raise ValueError("smoke-only scenario was not marked incomplete")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--work-root", type=Path)
    parser.add_argument("--name")
    parser.add_argument("--scenario", type=Path)
    parser.add_argument("--rates", nargs="+", type=int)
    parser.add_argument("--chapters", nargs="+", type=int)
    parser.add_argument("--gameplay-seconds", type=float, default=30.0)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        print("high-refresh analyzer self-test passed")
        return 0
    required = {
        "--work-root": args.work_root,
        "--name": args.name,
        "--scenario": args.scenario,
        "--rates": args.rates,
        "--chapters": args.chapters,
        "--output": args.output,
    }
    missing = [name for name, value in required.items() if value is None]
    if missing:
        parser.error("missing required arguments: " + ", ".join(missing))
    try:
        summary = analyze(
            args.work_root,
            args.name,
            args.scenario,
            args.rates,
            args.chapters,
            args.gameplay_seconds,
        )
    except ValueError as error:
        parser.error(str(error))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    passed = sum(1 for row in summary["rows"] if row["passed"])
    print(
        f"high-refresh campaign audit: {summary['status']} "
        f"({passed}/{len(summary['rows'])} runs passed); summary {args.output}"
    )
    return 0 if summary["status"] == "pass" else 2 if summary["status"] == "incomplete" else 1


if __name__ == "__main__":
    raise SystemExit(main())
