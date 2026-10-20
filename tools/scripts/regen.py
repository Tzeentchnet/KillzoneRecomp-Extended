"""Regenerate recompiled C++ and sync only changed files into generated/ (keeps rebuilds incremental).

Steps: merge SCE library bindings from ps2_analyzer and missed function starts from find_code_pointers.py into the
config (work/killzone.merged.toml), run ps2_recomp into work/gen_tmp, close emitted tail and resume dispatch targets
to a fixed point, then copy changed/new files to generated/ and delete stale ones.
Usage: python tools/scripts/regen.py --elf <path-to-SCUS_974.02>
"""
import argparse, filecmp, hashlib, json, os, re, shutil, subprocess, sys, tomllib

# Killzone ships the register-locals code generator (patch 0017); PS2X_CODEGEN=classic regenerates the old style.
os.environ.setdefault('PS2X_CODEGEN', 'locals')
# Guest control flow without the scheduler (patch 0023): tail jumps followed by the caller (ps2_cg_dsp.h) and the entry_*
# fragments folded into their functions. PS2X_CODEGEN_DSP=0 / PS2X_CODEGEN_FOLD=0 give the patch-0017 output.
os.environ.setdefault('PS2X_CODEGEN_DSP', '1')
os.environ.setdefault('PS2X_CODEGEN_FOLD', '1')
from pathlib import Path
from normalize_function_map import sections

ROOT = Path(__file__).resolve().parents[2]
CFG = ROOT / "config" / "killzone.toml"
GENERATED_MANIFEST = ROOT / "config" / "generated-manifest.json"
ANALYZER_TOML = ROOT / "work" / "analyzer.toml"
FUNCTIONS_CSV = Path(os.environ.get("KZ_FUNCTIONS_CSV", ROOT / "work" / "functions.csv"))
FUNCTIONS_METADATA = Path(f"{FUNCTIONS_CSV}.meta.json")
FUNCTION_MAP_SCRIPT = ROOT / "tools" / "scripts" / "generate_function_map.ps1"
FUNCTION_EXPORTER = ROOT / "ext" / "PS2Recomp" / "ps2xRecomp" / "tools" / "ghidra" / "ExportPS2Functions.java"
CODE_POINTER_REJECT = Path(
    os.environ.get("KZ_CODE_POINTER_REJECT", ROOT / "work" / "code_pointers_reject.txt")
)
# KZ_GEN_DIR / KZ_GEN_TMP / KZ_MERGED / KZ_RECOMP_EXE redirect the run (used for alternative code generators, e.g.
# PS2X_CODEGEN=locals into work/generated_cg) without touching generated/.
MERGED = Path(os.environ.get("KZ_MERGED", ROOT / "work" / "killzone.merged.toml"))
TMP = Path(os.environ.get("KZ_GEN_TMP", ROOT / "work" / "gen_tmp"))
GEN = Path(os.environ.get("KZ_GEN_DIR", ROOT / "generated"))
RECOMP = Path(os.environ.get("KZ_RECOMP_EXE", ROOT / "build" / "ps2recomp" / "ps2xRecomp" / "ps2_recomp.exe"))
ANALYZER = ROOT / "build" / "ps2recomp" / "ps2xAnalyzer" / "ps2_analyzer.exe"


def merged_config(elf: Path, closure_entries: set[int]) -> str:
    text = CFG.read_text()
    base_config = tomllib.loads(text)
    if not ANALYZER_TOML.exists():
        subprocess.run([str(ANALYZER), str(elf), str(ANALYZER_TOML)],
                       check=True, stdout=subprocess.DEVNULL)
    have = {s.split("@")[1].upper() for s in base_config["general"]["stubs"] if "@" in s}
    extra = [s for s in tomllib.loads(ANALYZER_TOML.read_text())["general"].get("stubs", [])
             if "@" in s and s.split("@")[1].upper() not in have]
    lines = "".join(f'  "{s}",\n' for s in extra)
    text = re.sub(r"(stubs = \[\n)", lambda m: m.group(1) + lines, text, count=1)
    text = re.sub(r'^input = .*$', f'input = "{elf.as_posix()}"', text, count=1, flags=re.M)
    text = re.sub(r'^output = .*$', f'output = "{TMP.as_posix()}"', text, count=1, flags=re.M)
    text = re.sub(
        r'^ghidra_output = .*$',
        f'ghidra_output = "{FUNCTIONS_CSV.as_posix()}"',
        text,
        count=1,
        flags=re.M,
    )
    print(f"[regen] +{len(extra)} SCE library bindings from ps2_analyzer")
    subprocess.run(
        [
            sys.executable,
            str(ROOT / "tools" / "scripts" / "find_code_pointers.py"),
            "--elf",
            str(elf),
            "--functions",
            str(FUNCTIONS_CSV),
        ],
        check=True,
    )
    discovered_entries = {
        int(line, 16)
        for line in (ROOT / "work" / "code_pointers.txt").read_text().splitlines()
        if line.strip()
    }
    manifest = json.loads(GENERATED_MANIFEST.read_text(encoding="utf-8"))
    hook_entries = {int(hook["address"], 0) for hook in manifest["hooks"]}
    configured_entries = {
        int(entry, 0)
        for entry in base_config["general"].get("entry_points", [])
    }
    entries = sorted(configured_entries | discovered_entries | hook_entries | closure_entries)
    entry_line = "entry_points = [" + ", ".join(f'"0x{entry:08X}"' for entry in entries) + "]\n"
    entry_pattern = r"^entry_points\s*=\s*\[.*?\]\s*(?:#.*)?$"
    if re.search(entry_pattern, text, flags=re.M | re.S):
        text = re.sub(entry_pattern, entry_line.rstrip(), text, count=1, flags=re.M | re.S)
    else:
        text = re.sub(r"(\[general\]\n)", lambda m: m.group(1) + entry_line, text, count=1)
    print(
        f"[regen] {len(entries)} configured entry points "
        f"({len(configured_entries)} explicit, {len(discovered_entries)} discovered, "
        f"{len(hook_entries)} locked hooks, {len(closure_entries)} generated closure)"
    )
    return text


def sync() -> None:
    GEN.mkdir(exist_ok=True)
    new = {p.name for p in TMP.iterdir()}
    changed = 0
    for name in new:
        src, dst = TMP / name, GEN / name
        if not dst.exists() or not filecmp.cmp(src, dst, shallow=False):
            shutil.copy2(src, dst)
            changed += 1
    stale = [p for p in GEN.iterdir() if p.name not in new]
    for p in stale:
        p.unlink()
    print(f"[regen] {len(new)} files: {changed} changed/new, {len(stale)} removed")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--elf", default=os.environ.get("KZ_GAME_ELF"))
    args = parser.parse_args()
    if not args.elf:
        parser.error("specify --elf or set KZ_GAME_ELF")
    elf = Path(args.elf).expanduser().resolve()
    if not elf.is_file():
        parser.error(f"ELF does not exist: {elf}")

    for directory in {ANALYZER_TOML.parent, FUNCTIONS_CSV.parent, MERGED.parent, TMP.parent, GEN.parent}:
        directory.mkdir(parents=True, exist_ok=True)
    ensure_function_map(elf)
    log = Path(os.environ.get("KZ_RECOMP_LOG", ROOT / "work" / "recomp.log"))
    log.parent.mkdir(parents=True, exist_ok=True)
    closure_entries: set[int] = set()
    for attempt in range(1, 9):
        MERGED.write_text(merged_config(elf, closure_entries))
        if TMP.exists():
            shutil.rmtree(TMP)
        with log.open("w") as f:
            result = subprocess.run([str(RECOMP), str(MERGED)], stdout=f, stderr=subprocess.STDOUT)
        rejected = reject_data_candidates(log, closure_entries)
        if rejected:
            closure_entries.difference_update(rejected)
            print(
                f"[regen] retrying after rejecting {len(rejected)} data candidate(s), "
                f"attempt {attempt}/8"
            )
            continue
        if result.returncode != 0:
            print(f"[regen] ps2_recomp failed ({result.returncode}); see {log}")
            return result.returncode
        rejected_entries = load_rejected_entries()
        new_tails = (
            find_missing_tail_entries(TMP, elf)
            - rejected_entries
            - closure_entries
        )
        new_resumes = (
            find_missing_resume_entries(TMP)
            - rejected_entries
            - closure_entries
        )
        new_entries = new_tails | new_resumes
        if new_entries:
            closure_entries.update(new_entries)
            print(
                f"[regen] closing {len(new_tails)} tail and "
                f"{len(new_resumes)} resume target(s), "
                f"attempt {attempt}/8"
            )
            continue
        sync()
        return 0
    print(f"[regen] dispatch closure did not converge after 8 attempts; see {log}")
    return 1


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def ensure_function_map(elf: Path) -> None:
    current = False
    if FUNCTIONS_CSV.is_file() and FUNCTIONS_METADATA.is_file():
        try:
            metadata = json.loads(FUNCTIONS_METADATA.read_text(encoding="utf-8-sig"))
        except json.JSONDecodeError as error:
            print(f"[regen] invalid function-map metadata; regenerating: {error}")
        else:
            if not isinstance(metadata, dict):
                print("[regen] invalid function-map metadata shape; regenerating")
            else:
                current = (
                    metadata.get("schema_version") == 3
                    and metadata.get("elf_sha256") == file_sha256(elf)
                    and metadata.get("exporter_sha256") == file_sha256(FUNCTION_EXPORTER)
                    and metadata.get("normalization")
                    == "clamp_bodies_align_instructions_extend_delay_slots_v3"
                )
    if current:
        return
    subprocess.run(
        [
            "powershell.exe",
            "-NoProfile",
            "-ExecutionPolicy",
            "Bypass",
            "-File",
            str(FUNCTION_MAP_SCRIPT),
            "-Elf",
            str(elf),
            "-Output",
            str(FUNCTIONS_CSV),
            "-Metadata",
            str(FUNCTIONS_METADATA),
        ],
        check=True,
    )


def find_missing_tail_entries(generated: Path, elf: Path) -> set[int]:
    register_text = (generated / "register_functions.cpp").read_text(encoding="utf-8")
    dispatch_addresses = {
        int(address, 16)
        for address in re.findall(r"// 0x([0-9A-Fa-f]+)", register_text)
    }
    tail_addresses = set()
    pattern = re.compile(r"ps2DspTail\(ctx,\s*0x([0-9A-Fa-f]+)u\)")
    legacy_pattern = re.compile(r"ctx->pc = 0x([0-9A-Fa-f]+)u;\s*return;")
    legacy_dispatch_pattern = re.compile(r"dispatchGuestBranch\(rdram,\s*ctx,\s*0x([0-9A-Fa-f]+)u")
    for source in generated.glob("*.cpp"):
        if source.name == "register_functions.cpp":
            continue
        text = source.read_text(encoding="utf-8", errors="replace")
        tail_addresses.update(int(address, 16) for address in pattern.findall(text))
        if os.environ.get("PS2X_CODEGEN_DSP", "1") == "0":
            tail_addresses.update(int(address, 16) for address in legacy_pattern.findall(text))
            tail_addresses.update(int(address, 16) for address in legacy_dispatch_pattern.findall(text))

    elf_data = elf.read_bytes()
    text_address, _, text_size = next(
        (address, offset, size)
        for name, address, offset, size in sections(elf_data)
        if name == ".text"
    )
    text_end = text_address + text_size
    return {
        address
        for address in tail_addresses - dispatch_addresses
        if address % 4 == 0 and text_address <= address < text_end
    }


def find_missing_resume_entries(generated: Path) -> set[int]:
    register_text = (generated / "register_functions.cpp").read_text(encoding="utf-8")
    dispatch_addresses = {
        int(address, 16)
        for address in re.findall(r"// 0x([0-9A-Fa-f]+)", register_text)
    }
    resume_addresses = set()
    pattern = re.compile(r"case 0x([0-9A-Fa-f]+)u:\s*goto label_")
    return_pattern = re.compile(r"// 0x([0-9A-Fa-f]+):[^\n]*\bjr\s+\$ra\b")
    for source in generated.glob("*.cpp"):
        if source.name == "register_functions.cpp":
            continue
        text = source.read_text(encoding="utf-8", errors="replace")
        resume_addresses.update(int(address, 16) for address in pattern.findall(text))
        if os.environ.get("PS2X_CODEGEN_DSP", "1") == "0":
            resume_addresses.update(int(address, 16) for address in return_pattern.findall(text))

    return {
        address
        for address in resume_addresses - dispatch_addresses
        if address % 4 == 0
    }


def load_rejected_entries() -> set[int]:
    if not CODE_POINTER_REJECT.exists():
        return set()
    return {
        int(line.split()[0], 16)
        for line in CODE_POINTER_REJECT.read_text().splitlines()
        if line.strip() and not line.startswith("#")
    }


def reject_data_candidates(log: Path, closure_entries: set[int]) -> set[int]:
    """Record added entry points whose bodies failed to decode (data embedded in .text) so the next regen skips them."""
    cands = {
        int(line, 16)
        for line in (ROOT / "work" / "code_pointers.txt").read_text().split()
    } | closure_entries
    bad = set()
    for line in log.read_text(errors="replace").splitlines():
        m = re.search(r"\[error\] unhandled-instruction function=entry_([0-9a-f]+) ", line)
        if m and int(m.group(1), 16) in cands:
            bad.add(int(m.group(1), 16))
    reject = CODE_POINTER_REJECT
    reject.parent.mkdir(parents=True, exist_ok=True)
    old = reject.read_text().splitlines() if reject.exists() else ["# data mistaken for code; generated by regen.py"]
    have = {l.split()[0].lower() for l in old if l.strip() and not l.startswith("#")}
    new = [f"0x{v:08X}" for v in sorted(bad) if f"0x{v:08x}" not in have]
    if new:
        reject.write_text("\n".join(old + new) + "\n")
        print(f"[regen] {len(new)} entry points rejected as data (take effect next regen)")
    return {int(value, 16) for value in new}


if __name__ == "__main__":
    sys.exit(main())
