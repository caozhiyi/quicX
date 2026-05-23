#!/usr/bin/env python3
"""
resolve_stacks.py -- Symbolize raw stacks from sampling_profiler.h.

Input:
    <raw>        one line per sample:  "0xADDR;0xADDR;...;0xADDR"
                 innermost (leaf) first.
    <raw>.maps   /proc/self/maps of the sampled process (same run):
                 "start-end perms offset dev ino path"

Output:
    <out>.collapsed  -- Brendan Gregg collapsed stacks   "root;...;leaf N"
    stdout           -- top self/inclusive/full-stack tables

For every address we find the matching maps entry.  For file-backed
executable regions we compute  file_off = addr - region_start + region_offset
and batch-call addr2line on that file.  Unknown regions are shown as
"[vdso]"/"[heap]"/hex fallback.
"""

import argparse
import bisect
import collections
import os
import re
import subprocess
import sys


# ---------------------------------------------------------------------------
# /proc/self/maps parsing
# ---------------------------------------------------------------------------

class MapRegion:
    __slots__ = ("start", "end", "offset", "path")

    def __init__(self, start, end, offset, path):
        self.start = start
        self.end = end
        self.offset = offset
        self.path = path

    def __repr__(self):
        return f"[{hex(self.start)}-{hex(self.end)} off={hex(self.offset)} {self.path}]"


MAPS_RE = re.compile(
    r"^([0-9a-f]+)-([0-9a-f]+)\s+(\S+)\s+([0-9a-f]+)\s+\S+\s+\S+\s*(.*)$"
)


def load_maps(path):
    regions = []
    if not os.path.exists(path):
        return regions
    with open(path) as fp:
        for line in fp:
            m = MAPS_RE.match(line.strip())
            if not m:
                continue
            start = int(m.group(1), 16)
            end = int(m.group(2), 16)
            perms = m.group(3)
            offset = int(m.group(4), 16)
            p = m.group(5).strip() or "[anon]"
            # Only keep executable mappings.
            if "x" not in perms:
                continue
            regions.append(MapRegion(start, end, offset, p))
    regions.sort(key=lambda r: r.start)
    return regions


def find_region(regions, addr, starts):
    """Binary search."""
    i = bisect.bisect_right(starts, addr) - 1
    if i < 0:
        return None
    r = regions[i]
    if r.start <= addr < r.end:
        return r
    return None


# ---------------------------------------------------------------------------
# Detect whether a binary is PIE / shared-object or a plain non-PIE executable.
# For non-PIE ET_EXEC files, the runtime PC is already the file offset that
# addr2line expects; subtracting the mapping base would silently give
# addr2line a nonsense offset that happens to be valid-looking but points
# inside .rodata/.init, yielding "??" for every frame.
# ---------------------------------------------------------------------------

_PIE_CACHE = {}

def is_pie_or_shared(binary):
    if binary in _PIE_CACHE:
        return _PIE_CACHE[binary]
    pie = True  # conservative default
    if os.path.exists(binary):
        try:
            out = subprocess.check_output(
                ["file", "-b", binary],
                stderr=subprocess.DEVNULL, text=True,
            )
            # "ELF 64-bit LSB executable, ..." => non-PIE
            # "ELF 64-bit LSB pie executable, ..." => PIE
            # "ELF 64-bit LSB shared object, ..." => .so or PIE
            lo = out.lower()
            if "pie executable" in lo or "shared object" in lo:
                pie = True
            elif "executable" in lo:
                pie = False
        except Exception:
            pass
    _PIE_CACHE[binary] = pie
    return pie


# ---------------------------------------------------------------------------
# addr2line per-binary batch
# ---------------------------------------------------------------------------

def addr2line_for_binary(binary, offsets):
    if not offsets:
        return {}
    if not os.path.exists(binary):
        return {o: "[external:%s]" % os.path.basename(binary) for o in offsets}
    hex_offs = [hex(o) for o in offsets]
    try:
        p = subprocess.Popen(
            ["addr2line", "-e", binary, "-f", "-C", "-i", "-p"],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL, text=True,
        )
        out, _ = p.communicate("\n".join(hex_offs))
    except FileNotFoundError:
        return {o: hex(o) for o in offsets}

    # Group lines per input: the first non-"(inlined by)" line starts a new
    # input group.
    groups, cur = [], []
    for line in out.splitlines():
        if line.startswith(" (inlined by)"):
            cur.append(line.strip())
        else:
            if cur:
                groups.append(cur)
            cur = [line.strip()]
    if cur:
        groups.append(cur)
    while len(groups) < len(offsets):
        groups.append(["??"])

    res = {}
    for off, grp in zip(offsets, groups):
        # Primary line = last one (outermost), rest are inlined callees.
        primary = grp[-1] if grp else "??"
        res[off] = sym_clean(primary, os.path.basename(binary))
    return res


def sym_clean(entry, binname):
    entry = entry.strip()
    if entry.startswith("?? ") or entry == "??":
        return f"[{binname}:??]"
    m = re.match(r"^(.*?)\s+at\s+(.*)$", entry)
    if not m:
        return entry
    func = m.group(1)
    # Shorten overly long template spam.
    func = re.sub(r"<[^<>]{60,}?>", "<...>", func)
    return func


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("raw", help="raw stacks file")
    ap.add_argument("--maps", default=None,
                    help="maps file (default: <raw>.maps)")
    ap.add_argument("--out", default=None,
                    help="output prefix (default: <raw>)")
    ap.add_argument("--top", type=int, default=25)
    args = ap.parse_args()

    maps_path = args.maps or (args.raw + ".maps")
    out_prefix = args.out or args.raw

    regions = load_maps(maps_path)
    region_starts = [r.start for r in regions]
    print(f"[resolve] loaded {len(regions)} executable mappings", file=sys.stderr)

    samples = []
    all_addrs = set()
    with open(args.raw) as fp:
        for line in fp:
            toks = [t for t in line.strip().split(";") if t]
            stack = []
            for t in toks:
                try:
                    a = int(t, 16)
                except ValueError:
                    continue
                stack.append(a)
                all_addrs.add(a)
            if stack:
                samples.append(stack)
    print(f"[resolve] loaded {len(samples)} samples, "
          f"{len(all_addrs)} unique addrs", file=sys.stderr)

    # Map every address to (binary, file_offset).
    # For non-PIE ET_EXEC binaries, runtime PC == file offset expected by
    # addr2line (load address is baked into the ELF).  Subtracting the
    # mapping base in that case would destroy symbolisation.
    by_binary = collections.defaultdict(list)
    addr_to_bin_off = {}
    for a in all_addrs:
        r = find_region(regions, a, region_starts)
        if r is None:
            addr_to_bin_off[a] = (None, None)
            continue
        binary = r.path
        if binary.startswith("[") or not os.path.exists(binary):
            file_off = a - r.start + r.offset
        elif is_pie_or_shared(binary):
            file_off = a - r.start + r.offset
        else:
            # non-PIE executable: use the runtime PC directly.
            file_off = a
        by_binary[binary].append(file_off)
        addr_to_bin_off[a] = (binary, file_off)

    # Resolve each binary in one addr2line run.
    offset_to_sym = {}  # (binary, file_off) -> symbol
    for binary, offs in by_binary.items():
        if binary in ("[heap]", "[stack]", "[vdso]", "[anon]", "[vvar]"):
            continue
        uniq = sorted(set(offs))
        print(f"[resolve] addr2line {binary}  ({len(uniq)} addrs)", file=sys.stderr)
        res = addr2line_for_binary(binary, uniq)
        for off, sym in res.items():
            offset_to_sym[(binary, off)] = sym

    def addr_sym(a):
        binary, off = addr_to_bin_off.get(a, (None, None))
        if binary is None:
            return f"[?{hex(a)}]"
        sym = offset_to_sym.get((binary, off))
        if sym:
            return sym
        return f"[{os.path.basename(binary)}+{hex(off)}]"

    # Build aggregates.
    stack_counts = collections.Counter()
    self_counts = collections.Counter()
    incl_counts = collections.Counter()
    for stk in samples:
        leaf = stk[0]
        self_counts[leaf] += 1
        for a in set(stk):
            incl_counts[a] += 1
        # flamegraph wants root-first
        key = ";".join(addr_sym(a) for a in reversed(stk))
        stack_counts[key] += 1

    collapsed = out_prefix + ".collapsed"
    with open(collapsed, "w") as fp:
        for k, c in stack_counts.most_common():
            fp.write(f"{k} {c}\n")
    print(f"[resolve] wrote {collapsed}")

    total = sum(stack_counts.values())
    def p(n): return 100.0 * n / total if total else 0.0

    print("\n" + "=" * 78)
    print(f"Total samples: {total}")
    print("=" * 78)

    print(f"\n-- TOP {args.top} self-time (leaf frame) --")
    for a, c in self_counts.most_common(args.top):
        print(f"  {p(c):6.2f}%  {c:5}   {addr_sym(a)}")

    print(f"\n-- TOP {args.top} inclusive-time (any frame in stack) --")
    for a, c in incl_counts.most_common(args.top):
        print(f"  {p(c):6.2f}%  {c:5}   {addr_sym(a)}")

    print(f"\n-- TOP {args.top} full call stacks (innermost 6 frames) --")
    for stk, c in stack_counts.most_common(args.top):
        frames = stk.split(";")
        tail = " -> ".join(frames[-6:])
        print(f"  {p(c):6.2f}%  {c:5}   {tail}")


if __name__ == "__main__":
    main()
