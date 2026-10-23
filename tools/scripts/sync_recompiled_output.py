from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
import tomllib
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
FUNCTION_HEADER_INCLUDE = re.compile(
    rb"(?m)^#include <ps2_recompiled_functions\.h>(?P<newline>\r?\n)"
)
DIRECT_RECOMPILED_CALL = re.compile(
    rb"(?m)^[ \t]+(?P<name>[A-Za-z_][A-Za-z0-9_]*)"
    rb"\(rdram,[ \t]*ctx,[ \t]*runtime\);[ \t]*(?:return;)?[ \t]*\r?$"
)


@dataclass(frozen=True)
class SyncStats:
    total: int
    changed: int
    removed: int
    localized_sources: int
    localized_declarations: int


def localize_function_declarations(content: bytes) -> tuple[bytes, int]:
    include = FUNCTION_HEADER_INCLUDE.search(content)
    if include is None:
        return content, 0

    names = list(
        dict.fromkeys(match.group("name") for match in DIRECT_RECOMPILED_CALL.finditer(content))
    )
    newline = include.group("newline")
    declarations = newline.join(
        b"void "
        + name
        + b"(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime);"
        for name in names
    )
    if declarations:
        declarations += newline
    localized = content[: include.start()] + declarations + content[include.end() :]
    return localized, len(names)


def _same_bytes(path: Path, content: bytes) -> bool:
    if not path.is_file() or path.stat().st_size != len(content):
        return False
    with path.open("rb") as stream:
        offset = 0
        while chunk := stream.read(1024 * 1024):
            if chunk != content[offset : offset + len(chunk)]:
                return False
            offset += len(chunk)
    return True


def _write_atomic(path: Path, content: bytes, source: Path) -> None:
    with tempfile.NamedTemporaryFile(
        dir=path.parent,
        prefix=f".{path.name}.",
        suffix=".tmp",
        delete=False,
    ) as stream:
        temporary = Path(stream.name)
        stream.write(content)
    try:
        shutil.copystat(source, temporary)
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def _validate_sync_paths(staging: Path, destination: Path) -> None:
    if staging == destination:
        raise ValueError("staging and destination directories must differ")
    if staging in destination.parents or destination in staging.parents:
        raise ValueError("staging and destination directories must not contain each other")
    forbidden = {
        Path(destination.anchor).resolve(),
        Path.home().resolve(),
        ROOT.resolve(),
    }
    if destination in forbidden:
        raise ValueError(f"refusing to synchronize into protected directory: {destination}")


def sync_generated_tree(staging: Path, destination: Path) -> SyncStats:
    staging = staging.resolve()
    destination = destination.resolve()
    _validate_sync_paths(staging, destination)
    if not staging.is_dir():
        raise FileNotFoundError(f"staging directory does not exist: {staging}")

    destination.mkdir(parents=True, exist_ok=True)
    staged_entries = list(staging.iterdir())
    staged_directories = [path for path in staged_entries if not path.is_file()]
    if staged_directories:
        raise ValueError(
            "PS2Recomp output must be flat; unexpected entry: "
            f"{staged_directories[0]}"
        )
    destination_directories = [
        path for path in destination.iterdir() if not path.is_file()
    ]
    if destination_directories:
        raise ValueError(
            "PS2Recomp output must be flat; refusing to modify destination with "
            f"unexpected entry: {destination_directories[0]}"
        )

    changed = 0
    localized_sources = 0
    localized_declarations = 0
    staged_names: set[str] = set()
    for source in staged_entries:
        staged_names.add(source.name)
        content = source.read_bytes()
        declaration_count = 0
        if source.suffix == ".cpp" and source.name != "register_functions.cpp":
            content, declaration_count = localize_function_declarations(content)
            if FUNCTION_HEADER_INCLUDE.search(content):
                raise ValueError(f"failed to localize generated declarations in {source}")
            if declaration_count:
                localized_sources += 1
                localized_declarations += declaration_count

        target = destination / source.name
        if not _same_bytes(target, content):
            _write_atomic(target, content, source)
            changed += 1

    removed = 0
    for stale in destination.iterdir():
        if stale.name in staged_names:
            continue
        if not stale.is_file():
            raise ValueError(f"refusing to remove unexpected output directory: {stale}")
        stale.unlink()
        removed += 1

    return SyncStats(
        total=len(staged_entries),
        changed=changed,
        removed=removed,
        localized_sources=localized_sources,
        localized_declarations=localized_declarations,
    )


def _resolve_from_root(path: Path) -> Path:
    return path.resolve() if path.is_absolute() else (ROOT / path).resolve()


def _configured_output(config: Path) -> Path:
    with config.open("rb") as stream:
        data = tomllib.load(stream)
    try:
        value = data["general"]["output"]
    except (KeyError, TypeError) as error:
        raise ValueError(f"{config} has no [general].output setting") from error
    if not isinstance(value, str) or not value:
        raise ValueError(f"{config} has an invalid [general].output setting")
    return _resolve_from_root(Path(value))


def _staged_config(config: Path, staging: Path) -> Path:
    text = config.read_text(encoding="utf-8")
    replacement = f'output = "{staging.as_posix()}"'
    staged_text, replacements = re.subn(
        r"(?m)^output\s*=\s*.*$",
        lambda _: replacement,
        text,
        count=1,
    )
    if replacements != 1:
        raise ValueError(f"{config} has no output assignment to redirect")
    with tempfile.NamedTemporaryFile(
        mode="w",
        encoding="utf-8",
        newline="",
        dir=config.parent,
        prefix=f".{config.stem}.",
        suffix=".incremental.toml",
        delete=False,
    ) as stream:
        stream.write(staged_text)
        return Path(stream.name)


def run_incremental_recompiler(
    recompiler: Path,
    config: Path,
    destination: Path,
    log: Path | None,
) -> tuple[int, SyncStats | None]:
    recompiler = recompiler.resolve()
    config = config.resolve()
    destination = destination.resolve()
    if not recompiler.is_file():
        raise FileNotFoundError(f"recompiler does not exist: {recompiler}")
    if not config.is_file():
        raise FileNotFoundError(f"configuration does not exist: {config}")

    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(
        dir=destination.parent,
        prefix=f".{destination.name}.staging-",
    ) as staging_name:
        staging = Path(staging_name)
        staged_config = _staged_config(config, staging)
        try:
            if log is None:
                result = subprocess.run(
                    [str(recompiler), str(staged_config)],
                    cwd=ROOT,
                    check=False,
                )
            else:
                log = log.resolve()
                log.parent.mkdir(parents=True, exist_ok=True)
                with log.open("wb") as stream:
                    result = subprocess.run(
                        [str(recompiler), str(staged_config)],
                        cwd=ROOT,
                        stdout=stream,
                        stderr=subprocess.STDOUT,
                        check=False,
                    )
            if result.returncode != 0:
                return result.returncode, None
            return 0, sync_generated_tree(staging, destination)
        finally:
            staged_config.unlink(missing_ok=True)


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Run PS2Recomp in a staging directory and content-sync its output without "
            "invalidating unchanged build inputs."
        )
    )
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument(
        "--recompiler",
        type=Path,
        default=ROOT / "build" / "ps2recomp" / "ps2xRecomp" / "ps2_recomp.exe",
    )
    parser.add_argument(
        "--output",
        type=Path,
        help="Destination generated tree; defaults to [general].output from --config.",
    )
    parser.add_argument("--log", type=Path)
    args = parser.parse_args()

    try:
        config = _resolve_from_root(args.config)
        destination = (
            _resolve_from_root(args.output)
            if args.output is not None
            else _configured_output(config)
        )
        return_code, stats = run_incremental_recompiler(
            _resolve_from_root(args.recompiler),
            config,
            destination,
            _resolve_from_root(args.log) if args.log is not None else None,
        )
    except (OSError, ValueError, tomllib.TOMLDecodeError) as error:
        print(f"[recomp-sync] {error}", file=sys.stderr)
        return 1

    if return_code != 0:
        if args.log is None:
            print(f"[recomp-sync] PS2Recomp failed with exit code {return_code}", file=sys.stderr)
        else:
            print(
                f"[recomp-sync] PS2Recomp failed with exit code {return_code}; "
                f"see {_resolve_from_root(args.log)}",
                file=sys.stderr,
            )
        return return_code

    assert stats is not None
    print(
        f"[recomp-sync] {stats.total} files: {stats.changed} changed/new, "
        f"{stats.removed} removed; localized {stats.localized_declarations} declaration(s) "
        f"across {stats.localized_sources} source file(s)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
