#!/usr/bin/env python3
"""Name the source of every per-sample loop in a function (S093 P4).

What:       for each backward-branch loop of the listed functions (the loops
            elf_loopdump.py keeps: no jump table, literal, call or return
            inside), asks arm-none-eabi-addr2line -f -i for the loop head's
            inline chain on a -g build and prints
              <function>  <length>  <innermost function> <file>:<line>  [<outer chain>]
            With --compare BASE NOW (two -g images), measures every loop's
            cost as its longest cycle through the head inside the loop's own
            address range (elf_looppath.loop_paths(), the executed per-sample
            length), groups the costs by (innermost function, file:line,
            outer chain) and compares the groups as multisets; a group whose
            largest cost rose is GREW (exit 1). Groups left unpaired (an
            edited file shifts its lines) are compared again without the line
            number. A span with no cycle in its range is a layout fragment,
            listed for information only.
Why:        S093 P4 gates G6/G7: LTO re-lays out mixer_calcNextSampleBlock()
            when the set of inlined engines changes (P3 §2.3), so pairing by
            opcode similarity (elf_hotloops.py) can mispair. The source of a
            loop does not move. A -g build has the same code as the release
            build (P3 §1: 168 bytes of veneer branch offsets differ, every
            loop length is identical). The outer chain is part of the key
            because shared leaves (bufferTool_satAdd32, calcNoiseBlock) are
            inlined into many callers. S093 P5 §9 B1: the first form paired by
            occurrence order and compared address spans; LTO reordering and
            out-of-line fragments made it report GREW where no executed loop
            grew, so costs are cyclic and pairing is by source line.
Inputs:     --elf PATH --sym NAME ... (a -g image), or --compare BASE NOW
            --sym NAME ...
Outputs:    the listing; with --compare, exit 1 when a group's largest
            cyclic cost grew.
Accessors:  python3 tools/dsp_test/elf_loopsrc.py ...; S093_P5 §3.4 G6/G7,
            §3.3 (the Drum ceiling's mixer-inlined parts).
Affiliates: elf_loopdump.py (functions(), the loop rule), elf_hotloops.py
            (the opcode pairing it checks).
"""
import argparse
import collections
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import elf_loopdump as L   # noqa: E402
import elf_looppath as P   # noqa: E402


def loops_with_heads(rows):
    """(head address, length, closing branch address) for every loop
    elf_loopdump.loops() keeps.

    The same scan as loops(): a backward branch at addr to lo closes the body
    lo..addr; a body holding dispatch code is skipped. The head address is
    kept because addr2line needs it."""
    out = []
    for addr, op, args in rows:
        if not L.BRANCH.match(op):
            continue
        t = L.TARGET.search(args)
        if not t or int(t.group(1), 16) >= addr:
            continue
        lo = int(t.group(1), 16)
        body = [o + " " + g for a, o, g in rows if lo <= a <= addr]
        if any(L.DISPATCH.search(b) for b in body):
            continue
        out.append((lo, len(body), addr))
    return out


def inline_chain(elf, addrs):
    """addr -> [(function, file:line), ...], innermost first.

    addr2line -a -f -i prints, per address, a 0x... line and then a
    (function, path:line) pair for each inline level, innermost first. The
    path may contain spaces, so only a trailing " (discriminator N)" is cut
    before the basename is taken."""
    if not addrs:
        return {}
    res = subprocess.run(["arm-none-eabi-addr2line", "-e", elf, "-f", "-i", "-a", "-C"]
                         + ["0x%x" % a for a in addrs],
                         check=True, capture_output=True, text=True).stdout.splitlines()
    out, cur, i = {}, None, 0
    while i < len(res):
        line = res[i]
        if line.startswith("0x"):
            cur = int(line, 16)
            out[cur] = []
            i += 1
            continue
        # one inline level: the function name, then its path:line
        func = line
        where = res[i + 1] if i + 1 < len(res) else "??:0"
        where = re.sub(r" \(discriminator \d+\)$", "", where)
        out[cur].append((func, os.path.basename(where)))
        i += 2
    return out


def table(elf, syms):
    """function base -> [(innermost function, file:line, length, outer chain)]."""
    out = {}
    for name, rows in L.functions(elf, set(syms)).items():
        lh = loops_with_heads(rows)
        ch = inline_chain(elf, [h for h, _, _ in lh])
        out[L.base(name)] = [(ch[h][0][0], ch[h][0][1], n, ch[h][1:]) for h, n, _ in lh]
    return out


def cyclic_groups(elf, syms):
    """function base -> {(func, file:line, outer chain): sorted cyclic costs},
    plus the spans that hold no cycle, for --compare.

    The cost of a loop is the longest cycle through its head that stays
    inside head..closing branch (elf_looppath.loop_paths() with `last`):
    the instructions one iteration executes, not the address span, which may
    hold out-of-line code. Restricting the walk to the range keeps a large
    function (the mixer) to minutes."""
    out, frag = {}, {}
    for name, rows in L.functions(elf, set(syms)).items():
        lh = loops_with_heads(rows)
        ch = inline_chain(elf, [h for h, _, _ in lh])
        r = P.rows(elf, name)
        idx = {a: i for i, (a, _, _) in enumerate(r)}
        groups = collections.defaultdict(list)
        frags = []
        for h, span, close in lh:
            chain = ch[h]
            key = (chain[0][0], chain[0][1], " < ".join(f for f, _ in chain[1:]))
            paths = P.loop_paths(r, idx, h, idx[close]) if h in idx and close in idx else []
            if paths:
                groups[key].append(max(p[0] for p in paths))
            else:
                frags.append((key, span))
        out[L.base(name)] = {k: sorted(v) for k, v in groups.items()}
        frag[L.base(name)] = frags
    return out, frag


def compare_groups(fn, b, c):
    """Print the differing groups of one function; return the GREW count.

    Three stages:
      1. groups with the same full key (source line known on both sides);
      2. groups still unpaired, merged by the key without the line number
         (an edited file shifts its lines);
      3. everything still unpaired, plus loops without source information
         (file "??": attributed in one image and not the other), as one pool
         per function: each NOW cost must be covered by a distinct BASE cost
         at least as large.
    GREW = a paired group's largest cyclic cost rose, or a pooled NOW cost has
    no BASE cost to cover it. An added loop of lower or equal cost in a paired
    group is a copy, reported as CHANGED."""
    grew = 0

    def report(key, bv, cv):
        nonlocal grew
        if bv == cv:
            return
        if bv and cv and max(cv) > max(bv):
            tag = "GREW"
            grew += 1
        elif not bv:
            tag = "NEW "
        elif not cv:
            tag = "GONE"
        else:
            tag = "CHANGED" if max(cv) == max(bv) else "SHRANK"
        print(f"{tag} {fn}: {key[0]} {key[1]} [{key[2]}]: {bv} -> {cv}")

    def known(k):
        return not k[1].startswith("??")

    left_b = {k: v for k, v in b.items() if k not in c or not known(k)}
    left_c = {k: v for k, v in c.items() if k not in b or not known(k)}
    for k in sorted(set(b) & set(c), key=str):
        if known(k):
            report(k, b[k], c[k])

    def noline(d):
        m = collections.defaultdict(list)
        for (func, where, chain), v in d.items():
            m[(func, where.split(":")[0], chain)] += v
        return {k: sorted(v) for k, v in m.items()}

    nb, nc = noline({k: v for k, v in left_b.items() if known(k)}), \
        noline({k: v for k, v in left_c.items() if known(k)})
    pool_b = [x for k, v in left_b.items() if not known(k) for x in v]
    pool_c = [x for k, v in left_c.items() if not known(k) for x in v]
    for k in sorted(set(nb) & set(nc), key=str):
        report(k, nb[k], nc[k])
    for k in sorted(set(nb) - set(nc), key=str):
        pool_b += nb[k]
        print(f"pool {fn}: base {k[0]} {k[1]} [{k[2]}] {nb[k]}")
    for k in sorted(set(nc) - set(nb), key=str):
        pool_c += nc[k]
        print(f"pool {fn}: now  {k[0]} {k[1]} [{k[2]}] {nc[k]}")
    # stage 3: largest first, each NOW cost against the largest BASE cost left
    pb, pc = sorted(pool_b, reverse=True), sorted(pool_c, reverse=True)
    uncovered = [x for i, x in enumerate(pc) if i >= len(pb) or x > pb[i]]
    if pb or pc:
        print(f"pool {fn}: base {pb} -> now {pc}"
              + (f": GREW, uncovered {uncovered}" if uncovered else ": covered"))
    if uncovered:
        grew += 1
    return grew


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--elf")
    ap.add_argument("--sym", action="append", default=[])
    ap.add_argument("--compare", nargs=2)
    a = ap.parse_args()
    if not a.sym:
        ap.error("at least one --sym is required")
    if not a.compare:
        if not a.elf:
            ap.error("--elf or --compare is required")
        for fn, entries in sorted(table(a.elf, a.sym).items()):
            for func, where, n, outer in entries:
                chain = " < ".join(f for f, _ in outer)
                print(f"{fn}\t{n:4d}\t{func} {where}\t[{chain}]")
        return 0
    (base, bfrag), (now, nfrag) = (cyclic_groups(a.compare[0], a.sym),
                                   cyclic_groups(a.compare[1], a.sym))
    grew = 0
    for fn in sorted(set(base) | set(now)):
        grew += compare_groups(fn, base.get(fn, {}), now.get(fn, {}))
        nb, nn = len(bfrag.get(fn, [])), len(nfrag.get(fn, []))
        if nb or nn:
            print(f"info {fn}: {nb} -> {nn} spans without a cycle (layout fragments, not compared)")
        cb = sum(len(v) for v in base.get(fn, {}).values())
        cn = sum(len(v) for v in now.get(fn, {}).values())
        print(f"{fn}: {cb} -> {cn} cyclic loops")
    print(f"elf_loopsrc: {grew} loop groups grew")
    return 1 if grew else 0


if __name__ == "__main__":
    sys.exit(main())
