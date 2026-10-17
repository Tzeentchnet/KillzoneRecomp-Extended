"""Find function starts Ghidra missed, for regen.py to pass to ps2_recomp as entry_points.

Two sources:
  1. data pointers: aligned words in .data/.rodata/.sdata pointing into .text at a non-function address
     (vtables, callback tables, jump tables);
  2. direct calls: in-text `jal` targets that Ghidra did not promote to function starts;
  3. prologues after a return: `addiu sp,sp,-N` that follows `jr ra` + delay slot (+ nop / `addiu v0,v0,0` padding).
     Ghidra sometimes extends a tiny function over the next one (e.g. entry_0043ffd0 swallowing the static
     constructor at 0x43FFE8, reached only via a runtime-built list), so these starts never appear as pointers.
  4. materialized callbacks: `lui` plus `addiu`/`daddiu`/`ori` sequences that take the address of a leaf thunk,
     stack-frame prologue, or post-return code boundary omitted from the map;
  5. call continuations: an exported range can end with a call and omit the caller's resume address;
  6. omitted return tails: normalizing a discontiguous Ghidra body can leave its terminal `jr ra` just beyond the
     exported range, even though generated fallthroughs still need to dispatch to that return.
  7. covered boundaries: an exported range can end inside another executable range, so its fallthrough address is
     known code even when Ghidra did not promote that address to a function or label.
  8. remaining range boundaries: generated functions publish an end-exclusive fallthrough, so every in-text end
     that is not already a function start must be dispatchable if that path is reached.
Writes work/code_pointers.txt (one 0x... per line).

Usage: python tools/scripts/find_code_pointers.py --elf <path-to-SCUS_974.02>
"""
import argparse, bisect, csv, os, struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
# Candidates whose recompiled body hit undecodable instructions (VU microcode / tables embedded in .text);
# written by regen.py after each recompile and excluded here.
REJECT_FILES = (
    ROOT / "config" / "code_pointers_reject.txt",
    Path(os.environ.get("KZ_CODE_POINTER_REJECT", ROOT / "work" / "code_pointers_reject.txt")),
)


def sections(d):
    shoff, = struct.unpack_from("<I", d, 0x20)
    shnum, shstrndx = struct.unpack_from("<HH", d, 0x30)
    sh = [struct.unpack_from("<10I", d, shoff + i * 40) for i in range(shnum)]
    stro = sh[shstrndx][4]
    for s in sh:
        name = d[stro + s[0]:d.index(b"\0", stro + s[0])].decode()
        yield name, s[1], s[3], s[4], s[5]  # name, type, addr, offset, size


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--elf", default=os.environ.get("KZ_GAME_ELF"))
    parser.add_argument("--functions", type=Path, default=ROOT / "work" / "functions.csv")
    parser.add_argument("--output", type=Path, default=ROOT / "work" / "code_pointers.txt")
    args = parser.parse_args()
    if not args.elf:
        parser.error("specify --elf or set KZ_GAME_ELF")
    elf = Path(args.elf).expanduser().resolve()
    if not elf.is_file():
        parser.error(f"ELF does not exist: {elf}")

    d = elf.read_bytes()
    secs = {n: (t, a, o, sz) for n, t, a, o, sz in sections(d)}
    _, tstart, toff, tsize = secs[".text"]
    tend = tstart + tsize
    starts = set()
    ends = set()
    ranges = []
    with args.functions.open() as f:
        for row in csv.DictReader(f):
            start = int(row["Start"], 16)
            end = int(row["End"], 16)
            starts.add(start)
            ends.add(end)
            ranges.append((start, end))
    found = {}
    for name in (".data", ".rodata", ".sdata"):
        stype, addr, off, size = secs[name]
        for i in range(0, size - 3, 4):
            v, = struct.unpack_from("<I", d, off + i)
            if tstart <= v < tend and v % 4 == 0 and v not in starts:
                insn, = struct.unpack_from("<I", d, toff + (v - tstart))
                if insn == 0:
                    continue  # padding, not code
                found.setdefault(v, name)
    JR_RA = 0x03E00008
    PADDING = {0x00000000, 0x24420000}
    words = struct.unpack_from(f"<{tsize // 4}I", d, toff)

    def follows_return_boundary(index):
        j = index - 1
        while j >= 0 and words[j] in PADDING:
            j -= 1
        return (j >= 0 and words[j] == JR_RA) or (j >= 1 and words[j - 1] == JR_RA)

    for i, word in enumerate(words):
        if word >> 26 != 3:
            continue
        source = tstart + i * 4
        target = ((source + 4) & 0xF0000000) | ((word & 0x03FFFFFF) << 2)
        if tstart <= target < tend and target not in starts:
            found.setdefault(target, "direct-call")
    for i, word in enumerate(words):
        if word >> 26 != 15:
            continue
        register = (word >> 16) & 0x1F
        high = word & 0xFFFF
        for low_index in range(i + 1, min(i + 9, len(words))):
            low_word = words[low_index]
            opcode = low_word >> 26
            if (
                opcode not in {9, 13, 25}
                or (low_word >> 21) & 0x1F != register
                or (low_word >> 16) & 0x1F != register
            ):
                continue
            low = low_word & 0xFFFF
            if opcode == 13:
                target = (high << 16) | low
            else:
                target = ((high << 16) + (low if low < 0x8000 else low - 0x10000)) & 0xFFFFFFFF
            if tstart <= target < tend and target % 4 == 0 and target not in starts:
                target_index = (target - tstart) // 4
                target_word = words[target_index]
                if target_word == JR_RA:
                    found.setdefault(target, "materialized-leaf")
                elif (target_word >> 16) == 0x27BD and target_word & 0x8000:
                    found.setdefault(target, "materialized-prologue")
                elif follows_return_boundary(target_index):
                    found.setdefault(target, "materialized-boundary")
            break
    for end in ends:
        i = (end - tstart) // 4
        if end % 4 != 0 or i < 2 or i > len(words) or end in starts:
            continue
        call = words[i - 2]
        is_jal = call >> 26 == 3
        is_linking_jalr = call >> 26 == 0 and call & 0x3F == 9 and (call >> 11) & 0x1F != 0
        if is_jal or is_linking_jalr:
            found.setdefault(end, "call-return")
    for end in ends:
        i = (end - tstart) // 4
        if (
            end % 4 == 0
            and 0 <= i < len(words)
            and words[i] == JR_RA
            and end not in starts
        ):
            found.setdefault(end, "return-tail")
    range_starts = []
    range_max_ends = []
    max_end = 0
    for start, end in sorted(ranges):
        max_end = max(max_end, end)
        range_starts.append(start)
        range_max_ends.append(max_end)
    for end in ends:
        i = bisect.bisect_right(range_starts, end) - 1
        if (
            tstart <= end < tend
            and end % 4 == 0
            and end not in starts
            and i >= 0
            and range_max_ends[i] > end
        ):
            found.setdefault(end, "covered-boundary")
    for end in ends:
        if tstart <= end < tend and end % 4 == 0 and end not in starts:
            found.setdefault(end, "range-boundary")
    for i, w in enumerate(words):
        if (w >> 16) != 0x27BD or not (w & 0x8000):
            continue  # not addiu sp,sp,-N
        a = tstart + i * 4
        if a in starts:
            continue
        if follows_return_boundary(i):
            if found.get(a) == "materialized-prologue":
                found[a] = "prologue"
            else:
                found.setdefault(a, "prologue")
    rejected = set()
    for reject_file in REJECT_FILES:
        if reject_file.exists():
            rejected.update(
                int(line.split()[0], 16)
                for line in reject_file.read_text().splitlines()
                if line.strip() and not line.startswith("#")
            )
    for v in rejected:
        found.pop(v, None)
    by = {}
    for v, n in found.items():
        by[n] = by.get(n, 0) + 1
    args.output.write_text("".join(f"0x{v:08X}\n" for v in sorted(found)))
    print(f"[code-pointers] {len(found)} targets not in function map {by} ({len(rejected)} rejected as data) -> {args.output}")
    print("0x43FFE8 present:", 0x43FFE8 in found)


if __name__ == "__main__":
    main()
