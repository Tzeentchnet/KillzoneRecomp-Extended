"""Verify the repository's locked source and dependency inputs without network access."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_LOCK = ROOT / "config" / "dependencies.lock.json"
HEX40 = re.compile(r"^[0-9a-f]{40}$")
HEX64 = re.compile(r"^[0-9a-f]{64}$")


class VerificationError(RuntimeError):
    pass


class Report:
    def __init__(self, strict: bool) -> None:
        self.strict = strict
        self.passes = 0
        self.warnings = 0
        self.failures = 0

    def passed(self, message: str) -> None:
        self.passes += 1
        print(f"[PASS] {message}")

    def warning(self, message: str) -> None:
        self.warnings += 1
        print(f"[WARN] {message}")

    def failed(self, message: str) -> None:
        self.failures += 1
        print(f"[FAIL] {message}")

    def missing(self, message: str, required_in_strict: bool) -> None:
        if self.strict and required_in_strict:
            self.failed(message)
        else:
            self.warning(message)

    def finish(self) -> int:
        print(
            f"Dependency verification: {self.passes} passed, "
            f"{self.warnings} warnings, {self.failures} failures"
        )
        return 1 if self.failures else 0


def normalized_bytes(path: Path) -> bytes:
    return path.read_bytes().replace(b"\r\n", b"\n").replace(b"\r", b"\n")


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def run_git(
    repository: Path,
    *arguments: str,
    env: dict[str, str] | None = None,
    stdin: bytes | None = None,
) -> str:
    process = subprocess.run(
        ["git", "-C", str(repository), *arguments],
        input=stdin,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        env=env,
        check=False,
    )
    if process.returncode != 0:
        detail = process.stderr.decode(errors="replace").strip()
        raise VerificationError(
            f"git {' '.join(arguments)} failed in {repository}: {detail}"
        )
    return process.stdout.decode(errors="replace").strip()


def patch_index_environment(index_path: Path) -> dict[str, str]:
    environment = os.environ.copy()
    environment["GIT_INDEX_FILE"] = str(index_path)
    environment["GIT_CONFIG_COUNT"] = "3"
    environment["GIT_CONFIG_KEY_0"] = "core.autocrlf"
    environment["GIT_CONFIG_VALUE_0"] = "false"
    environment["GIT_CONFIG_KEY_1"] = "core.eol"
    environment["GIT_CONFIG_VALUE_1"] = "lf"
    environment["GIT_CONFIG_KEY_2"] = "core.safecrlf"
    environment["GIT_CONFIG_VALUE_2"] = "false"
    return environment


def load_lock(path: Path) -> dict[str, Any]:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except OSError as error:
        raise VerificationError(f"cannot read lock file {path}: {error}") from error
    except json.JSONDecodeError as error:
        raise VerificationError(f"invalid JSON in {path}: {error}") from error


def verify_lock_schema(lock: dict[str, Any], report: Report) -> None:
    if lock.get("schema_version") != 1:
        report.failed("lock schema_version must be 1")
        return

    dependencies = lock.get("dependencies")
    if not isinstance(dependencies, list) or not dependencies:
        report.failed("lock must contain a non-empty dependencies array")
        return

    ids: set[str] = set()
    for dependency in dependencies:
        dependency_id = dependency.get("id")
        if not isinstance(dependency_id, str) or not dependency_id:
            report.failed("every dependency must have a non-empty id")
            continue
        if dependency_id in ids:
            report.failed(f"duplicate dependency id: {dependency_id}")
        ids.add(dependency_id)

        for field in (
            "kind",
            "source_url",
            "revision",
            "license",
            "redistribution",
            "acquisition",
        ):
            if not dependency.get(field):
                report.failed(f"{dependency_id}: missing {field}")

        kind = dependency.get("kind")
        revision = dependency.get("revision", "")
        if kind in {"git-submodule", "git-checkout", "cmake-git"} and not HEX40.fullmatch(
            revision
        ):
            report.failed(f"{dependency_id}: git revision is not a full 40-digit commit")
        if kind == "archive":
            checksum = dependency.get("sha256", "")
            if not HEX64.fullmatch(checksum):
                report.failed(f"{dependency_id}: archive sha256 is not 64 lowercase hex digits")
            if not isinstance(dependency.get("size"), int) or dependency["size"] <= 0:
                report.failed(f"{dependency_id}: archive size must be a positive integer")

    if report.failures == 0:
        report.passed(f"lock schema and {len(dependencies)} dependency records")


def verify_project_baseline(lock: dict[str, Any], report: Report) -> None:
    baseline = lock["project"]["audited_baseline"]
    try:
        run_git(ROOT, "cat-file", "-e", f"{baseline}^{{commit}}")
        ancestor = run_git(ROOT, "merge-base", "--is-ancestor", baseline, "HEAD")
        del ancestor
    except VerificationError as error:
        report.failed(str(error))
        return
    report.passed(f"audited baseline is present and is an ancestor of HEAD ({baseline[:12]})")


def verify_git_dependency(dependency: dict[str, Any], report: Report) -> None:
    local = dependency.get("local")
    if not local:
        return

    path = ROOT / local["path"]
    required = bool(local.get("required_in_strict"))
    if not path.is_dir():
        report.missing(
            f"{dependency['id']}: checkout is missing at {path}",
            required,
        )
        return

    try:
        head = run_git(path, "rev-parse", "HEAD")
    except VerificationError as error:
        report.failed(f"{dependency['id']}: {error}")
        return

    if head != dependency["revision"]:
        report.failed(
            f"{dependency['id']}: expected {dependency['revision']}, found {head}"
        )
        return

    if local.get("clean"):
        try:
            status = run_git(path, "status", "--porcelain", "--untracked-files=no")
        except VerificationError as error:
            report.failed(f"{dependency['id']}: {error}")
            return
        if status:
            report.failed(f"{dependency['id']}: tracked files have local modifications")
            return

    report.passed(f"{dependency['id']}: locked checkout {head[:12]}")


def verify_archive_dependency(dependency: dict[str, Any], report: Report) -> None:
    local = dependency.get("local")
    if not local:
        return

    required = bool(local.get("required_in_strict"))
    archive = ROOT / local["archive"]
    directory = ROOT / local["directory"]
    dependency_id = dependency["id"]

    if not archive.is_file():
        report.missing(
            f"{dependency_id}: locked archive is missing at {archive}",
            required,
        )
    else:
        size = archive.stat().st_size
        if size != dependency["size"]:
            report.failed(
                f"{dependency_id}: archive size is {size}, expected {dependency['size']}"
            )
        else:
            checksum = sha256_file(archive)
            if checksum != dependency["sha256"]:
                report.failed(
                    f"{dependency_id}: archive sha256 is {checksum}, "
                    f"expected {dependency['sha256']}"
                )
            else:
                report.passed(f"{dependency_id}: archive checksum")

    if not directory.is_dir():
        report.missing(
            f"{dependency_id}: extracted directory is missing at {directory}",
            required,
        )
        return

    missing_markers = [
        marker for marker in local.get("markers", []) if not (directory / marker).is_file()
    ]
    if missing_markers:
        report.failed(
            f"{dependency_id}: extracted directory is missing markers: "
            + ", ".join(missing_markers)
        )
        return
    report.passed(f"{dependency_id}: extracted directory markers")


def fetchcontent_blocks(text: str) -> dict[str, str]:
    blocks: dict[str, str] = {}
    pattern = re.compile(
        r"FetchContent_Declare\s*\(\s*([A-Za-z0-9_]+)\b(.*?)\n\s*\)",
        re.DOTALL,
    )
    for match in pattern.finditer(text):
        blocks[match.group(1).lower()] = match.group(2)
    return blocks


def verify_declarations(lock: dict[str, Any], report: Report) -> None:
    expected_by_file: dict[str, set[str]] = {}
    cached_text: dict[str, str] = {}

    for dependency in lock["dependencies"]:
        declaration = dependency.get("declaration")
        if not declaration:
            continue

        relative_file = declaration["file"]
        expected_by_file.setdefault(relative_file, set()).add(
            declaration["name"].lower()
        )
        path = ROOT / relative_file
        try:
            text = cached_text.setdefault(
                relative_file, path.read_text(encoding="utf-8")
            )
        except OSError as error:
            report.failed(f"{dependency['id']}: cannot read {path}: {error}")
            continue

        block = fetchcontent_blocks(text).get(declaration["name"].lower())
        if block is None:
            report.failed(
                f"{dependency['id']}: FetchContent declaration "
                f"{declaration['name']} is missing from {relative_file}"
            )
            continue
        if dependency["source_url"] not in block:
            report.failed(
                f"{dependency['id']}: source URL drift in {relative_file}"
            )
            continue
        if dependency["declared_ref"] not in block:
            report.failed(
                f"{dependency['id']}: declared ref drift in {relative_file}"
            )
            continue
        report.passed(f"{dependency['id']}: CMake declaration matches lock")

    for relative_file in lock.get("cmake_fetchcontent_scan", []):
        path = ROOT / relative_file
        try:
            text = cached_text.setdefault(
                relative_file, path.read_text(encoding="utf-8")
            )
        except OSError as error:
            report.failed(f"cannot scan {path}: {error}")
            continue
        actual = set(fetchcontent_blocks(text))
        expected = expected_by_file.get(relative_file, set())
        if actual != expected:
            missing = sorted(expected - actual)
            unlocked = sorted(actual - expected)
            details: list[str] = []
            if missing:
                details.append("missing: " + ", ".join(missing))
            if unlocked:
                details.append("unlocked: " + ", ".join(unlocked))
            report.failed(
                f"{relative_file}: FetchContent inventory mismatch ({'; '.join(details)})"
            )
        else:
            report.passed(
                f"{relative_file}: all {len(actual)} FetchContent declarations are locked"
            )

    checks = list(lock.get("declaration_checks", []))
    for dependency in lock["dependencies"]:
        checks.extend(dependency.get("declaration_checks", []))
    for check in checks:
        path = ROOT / check["file"]
        try:
            text = path.read_text(encoding="utf-8")
        except OSError as error:
            report.failed(f"cannot read declaration file {path}: {error}")
            continue
        missing = [value for value in check["contains"] if value not in text]
        if missing:
            report.failed(
                f"{check['file']}: locked declaration text is missing: "
                + ", ".join(missing)
            )
        else:
            report.passed(f"{check['file']}: non-FetchContent declarations match lock")


def verify_patch_stack(lock: dict[str, Any], report: Report) -> None:
    stack = lock["patch_stack"]
    repository = ROOT / stack["repository_path"]
    patch_directory = ROOT / stack["patch_directory"]
    expected_patches = stack["patches"]
    expected_names = [patch["path"] for patch in expected_patches]

    if not repository.is_dir():
        report.failed(f"PS2Recomp repository is missing at {repository}")
        return
    if not patch_directory.is_dir():
        report.failed(f"patch directory is missing at {patch_directory}")
        return

    actual_names = sorted(path.name for path in patch_directory.glob("*.patch"))
    if actual_names != expected_names:
        report.failed(
            "PS2Recomp patch inventory differs from the lock: "
            f"expected {expected_names}, found {actual_names}"
        )
        return

    patch_bytes: list[tuple[Path, bytes]] = []
    for expected in expected_patches:
        path = patch_directory / expected["path"]
        data = normalized_bytes(path)
        checksum = hashlib.sha256(data).hexdigest()
        if checksum != expected["sha256"]:
            report.failed(
                f"{expected['path']}: normalized sha256 is {checksum}, "
                f"expected {expected['sha256']}"
            )
            return
        patch_bytes.append((path, data))
    report.passed(f"all {len(patch_bytes)} normalized patch checksums")

    try:
        target_commit = run_git(repository, "rev-parse", "HEAD")
        target_tree = run_git(repository, "rev-parse", f"{stack['target_commit']}^{{tree}}")
        gitlink = run_git(
            ROOT,
            "ls-files",
            "--stage",
            "--",
            stack["repository_path"],
        ).split()
    except VerificationError as error:
        report.failed(str(error))
        return

    if target_commit != stack["target_commit"]:
        report.failed(
            f"PS2Recomp HEAD is {target_commit}, expected {stack['target_commit']}"
        )
        return
    if target_tree != stack["target_tree"]:
        report.failed(
            f"PS2Recomp target tree is {target_tree}, expected {stack['target_tree']}"
        )
        return
    if len(gitlink) < 2 or gitlink[1] != stack["target_commit"]:
        found = gitlink[1] if len(gitlink) >= 2 else "missing"
        report.failed(
            f"PS2Recomp gitlink is {found}, expected {stack['target_commit']}"
        )
        return
    report.passed(f"PS2Recomp gitlink and target tree ({target_commit[:12]})")

    canonical = stack["canonical_patch"]
    canonical_path = ROOT / canonical["path"]
    if not canonical_path.is_file():
        report.failed(f"canonical PS2Recomp patch is missing at {canonical_path}")
        return
    canonical_data = normalized_bytes(canonical_path)
    canonical_checksum = hashlib.sha256(canonical_data).hexdigest()
    if canonical_checksum != canonical["sha256"]:
        report.failed(
            f"canonical PS2Recomp patch sha256 is {canonical_checksum}, "
            f"expected {canonical['sha256']}"
        )
        return
    changed_paths = canonical_data.count(b"diff --git ")
    if changed_paths != canonical["changed_paths"]:
        report.failed(
            f"canonical PS2Recomp patch has {changed_paths} changed paths, "
            f"expected {canonical['changed_paths']}"
        )
        return
    report.passed(
        f"canonical PS2Recomp patch checksum and {changed_paths}-path inventory"
    )

    with tempfile.TemporaryDirectory(prefix="kz-deps-") as temporary:
        environment = patch_index_environment(Path(temporary) / "index")
        try:
            run_git(
                repository,
                "read-tree",
                stack["base_commit"],
                env=environment,
            )
            run_git(
                repository,
                "apply",
                "--cached",
                "--whitespace=nowarn",
                "-",
                env=environment,
                stdin=canonical_data,
            )
            reconstructed_tree = run_git(repository, "write-tree", env=environment)
        except VerificationError as error:
            report.failed(f"PS2Recomp patch reconstruction failed: {error}")
            return

    if reconstructed_tree != stack["target_tree"]:
        report.failed(
            f"PS2Recomp patches reconstruct {reconstructed_tree}, "
            f"expected {stack['target_tree']}"
        )
        return
    report.passed("canonical PS2Recomp patch reconstructs the locked submodule tree")


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Verify locked dependencies without accessing the network."
    )
    parser.add_argument(
        "--strict",
        action="store_true",
        help="Fail when a manually provisioned dependency or archive is missing.",
    )
    parser.add_argument(
        "--lock",
        type=Path,
        default=DEFAULT_LOCK,
        help=f"Dependency lock path (default: {DEFAULT_LOCK})",
    )
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    report = Report(arguments.strict)
    try:
        lock = load_lock(arguments.lock.resolve())
    except VerificationError as error:
        report.failed(str(error))
        return report.finish()

    verify_lock_schema(lock, report)
    if report.failures:
        return report.finish()

    verify_project_baseline(lock, report)
    verify_declarations(lock, report)
    for dependency in lock["dependencies"]:
        if dependency["kind"] in {"git-submodule", "git-checkout"}:
            verify_git_dependency(dependency, report)
        elif dependency["kind"] == "archive":
            verify_archive_dependency(dependency, report)
    verify_patch_stack(lock, report)
    return report.finish()


if __name__ == "__main__":
    sys.exit(main())
