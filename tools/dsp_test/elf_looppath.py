#!/usr/bin/env python3
"""Longest per-iteration path of a function's loops, and straight-line counts.

What:       disassembles one function of the linked firmware ELF and, for
            every loop head (the target of a backward branch), walks every
            control-flow path from the head back to itself and reports the
            longest and shortest path in instructions, and the VDIVs on the
            longest. Out-of-line blocks are followed through their branches,
            so a loop whose body jumps around a literal pool (which
            elf_loopdump.py skips as dispatch code) is still measured. With
            --straight it instead counts the instructions of a function that
            has no loop (for example a fully unrolled search).
Why:        S091 P1: crumpBit_process() keeps a literal pool inside its
            per-sample loop and the unrolled Sync search has no loop at all,
            so neither elf_loopdump.py nor fpseq.py can gate them.
Inputs:     --elf PATH, --sym NAME (the base name; .lto_priv/.constprop
            suffixes are matched), optional --max N (fail if the longest
            loop path is above N), --straight (count the whole function),
            --top K (report the K longest loops), --show (print the longest
            path), --cond-heads (S091 P3: only targets of CONDITIONAL
            backward branches are loop heads; a function whose cold blocks
            are placed after its loops and jump back with an unconditional
            `b` would otherwise seed a path search from every such target,
            which explodes combinatorially; a frame loop always closes
            with a conditional branch), --in-range (S091 P4: a loop's path
            search stays between its head and the furthest backward branch
            that closes it; a cold block that closes a conditional branch
            back into the function, which is not a frame loop, would
            otherwise search every path of the whole function. Frame loops
            laid out contiguously measure the same with it), --rank K
            (S091 P5: --max applies to the K-th longest loop, default 1;
            crumpBit_process() has two filter loops, the SVF and the
            one-pole, each with its own limit).
Outputs:    one line per loop; exit 1 when --max is exceeded.
Accessors:  tools/dsp_test/Makefile crumpbit_elf_check; by hand; S091 P5:
            imported by elf_blockpath.py and crumpbit_cpu.py (rows(),
            successors(), loop_paths(); disassembly() runs objdump once per
            ELF and process).
Affiliates: elf_loopdump.py (loop text compare), fpseq.py (value ops).
"""
import argparse
import functools
import re
import subprocess
import sys

BRANCH = re.compile(
    r'^(b|beq|bne|bcs|bhs|bcc|blo|bmi|bpl|bvs|bvc|bhi|bls|bge|blt|bgt|ble)'
    r'(\.[nw])?$|^cbn?z$')
UNCOND = {'b', 'b.n', 'b.w'}
SUFFIX = r'(\.(lto_priv|constprop|isra|part|cold)\.\d+)*'


@functools.lru_cache(maxsize=None)
def disassembly(elf):
    """The ELF's whole `objdump -d` text, once per process (S091 P5: the CPU
    gate reads about twenty functions of one image)."""
    return subprocess.run(['arm-none-eabi-objdump', '-d', '--no-show-raw-insn',
                           elf], check=True, capture_output=True,
                          text=True).stdout


# Jump tables (S095 P1): the targets of each `tbb`/`tbh`, by its address.
# objdump prints a table as data lines (.byte/.short/.word) after the
# instruction; rows() decodes them so successors() follows the table instead
# of falling through into it (the S095 P1 crumpBit_process layout placed
# out-of-line code after its engine switch's table, and the fall-through read
# as a loop).
JUMP_TABLES = {}


def _table_targets(addr, op, data):
    """Branch targets of a tbb/tbh at addr from its table's data lines
    [(address, directive, value)]: the table's bytes in address order, read
    as bytes (tbb) or little-endian halfwords (tbh); target = addr + 4 +
    2 x entry."""
    raw = bytearray()
    for _, d, v in sorted(data):
        n = {'.byte': 1, '.short': 2, '.word': 4}.get(d)
        if n is None:
            continue
        raw += int(v, 16).to_bytes(n, 'little')
    if op.startswith('tbh'):
        entries = [raw[k] | (raw[k + 1] << 8) for k in range(0, len(raw) - 1, 2)]
    else:
        entries = list(raw)
    return [addr + 4 + 2 * e for e in entries]


def rows(elf, sym):
    text = disassembly(elf)
    m = re.search(r'^[0-9a-f]+ <' + re.escape(sym) + SUFFIX +
                  r'>:\n(?P<body>.*?)(?:\n\n|\Z)', text, re.S | re.M)
    if not m:
        sys.exit(f'elf_looppath.py: {sym} not found')
    out = []
    table = None
    for line in m.group('body').splitlines():
        p = re.match(r'\s*([0-9a-f]+):\s+(\S+)\s*(.*)', line)
        if not p:
            continue
        if p.group(2).startswith('.'):
            if table is not None:
                v = re.match(r'(0x[0-9a-f]+)', p.group(3))
                if v:
                    table[2].append((int(p.group(1), 16), p.group(2),
                                     v.group(1)))
            continue
        if table is not None:
            JUMP_TABLES[table[0]] = _table_targets(*table)
            table = None
        out.append((int(p.group(1), 16), p.group(2), p.group(3)))
        if p.group(2).startswith(('tbb', 'tbh')):
            table = (int(p.group(1), 16), p.group(2), [])
    if table is not None:
        JUMP_TABLES[table[0]] = _table_targets(*table)
    return out


def target(ops):
    m = re.search(r'\b([0-9a-f]+) <', ops)
    return int(m.group(1), 16) if m else None


def is_return(op, ops):
    return op == 'bx' or op.startswith('pop') or \
        (op.startswith('ldmia') and 'pc' in ops)


def successors(r, idx, i):
    addr, op, ops = r[i]
    if op.startswith(('tbb', 'tbh')) and addr in JUMP_TABLES:
        # A jump table: its targets, never a fall-through into the data.
        return sorted({idx[t] for t in JUMP_TABLES[addr] if t in idx})
    if BRANCH.match(op):
        out = []
        t = target(ops)
        if t is not None and t in idx:
            out.append(idx[t])
        if op not in UNCOND and i + 1 < len(r):
            out.append(i + 1)
        return out
    if is_return(op, ops):
        return []
    return [i + 1] if i + 1 < len(r) else []


def cycle_nodes(r, idx, h, inside):
    """The instructions on some cycle through index h: reachable from h and
    able to reach h, both through instructions accepted by inside(j). A
    path h -> ... -> h can only use these, so loop_paths() searches no
    other (S091 P5: the S091 P5 crumpBit_runGrain has cold blocks whose
    conditional branches close far back into the function; the old search
    walked every path through them that never returned, for minutes)."""
    n = len(r)
    succ = [[j for j in successors(r, idx, i) if inside(j)] if inside(i)
            else [] for i in range(n)]
    pred = [[] for _ in range(n)]
    for i in range(n):
        for j in succ[i]:
            pred[j].append(i)
    seen = []
    for edges in (succ, pred):
        mark = {h}
        todo = [h]
        while todo:
            i = todo.pop()
            for j in edges[i]:
                if j not in mark:
                    mark.add(j)
                    todo.append(j)
        seen.append(mark)
    return seen[0] & seen[1]


def loop_paths(r, idx, head, last=None):
    """Every simple path head -> ... -> head, as (length, vdivs, indices).
    With `last` (an index), only instructions idx[head]..last are followed.
    Only instructions on a cycle through the head are followed
    (cycle_nodes()): the result is the same, the search far smaller."""
    found = []
    h = idx[head]
    on_cycle = cycle_nodes(r, idx, h,
                           lambda j: last is None or h <= j <= last)
    stack = [(h, [h])]
    while stack:
        i, path = stack.pop()
        for j in successors(r, idx, i):
            if j not in on_cycle:
                continue
            if r[j][0] == head:
                found.append((len(path),
                              sum('vdiv' in r[k][1] for k in path), path))
            elif j not in path and len(found) < 50000:
                stack.append((j, path + [j]))
    return found


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--elf', required=True)
    ap.add_argument('--sym', required=True)
    ap.add_argument('--max', type=int)
    ap.add_argument('--straight', action='store_true')
    ap.add_argument('--top', type=int, default=1)
    ap.add_argument('--show', action='store_true')
    ap.add_argument('--cond-heads', action='store_true')
    ap.add_argument('--in-range', action='store_true')
    ap.add_argument('--rank', type=int, default=1)
    a = ap.parse_args()
    r = rows(a.elf, a.sym)
    if a.straight:
        n = len(r)
        print(f'{a.sym}: {n} instructions (straight-line count)')
        sys.exit(1 if a.max is not None and n > a.max else 0)
    idx = {addr: i for i, (addr, _, _) in enumerate(r)}
    heads = sorted({target(ops) for (addr, op, ops) in r
                    if BRANCH.match(op) and target(ops) is not None
                    and target(ops) <= addr and target(ops) in idx
                    and not (a.cond_heads and op in UNCOND)})
    loops = []
    # closer[h]: the index of the furthest backward branch to head h.
    closer = {}
    for i, (addr, op, ops) in enumerate(r):
        t = target(ops) if BRANCH.match(op) else None
        if t in heads and t <= addr:
            closer[t] = max(closer.get(t, 0), i)
    for h in heads:
        p = loop_paths(r, idx, h, closer[h] if a.in_range else None)
        if p:
            p.sort(key=lambda x: -x[0])
            loops.append((p[0][0], h, p))
    loops.sort(key=lambda x: -x[0])
    if not loops:
        sys.exit(f'elf_looppath.py: no loop in {a.sym}')
    for longest, h, p in loops[:a.top]:
        print(f'{a.sym}: loop head {h:#x}: longest {longest} '
              f'(vdiv {p[0][1]}), shortest {p[-1][0]}, paths {len(p)}')
    if a.show:
        for k in loops[0][2][0][2]:
            print('    ', r[k][1], r[k][2])
    if a.rank < 1 or a.rank > len(loops):
        sys.exit(f'elf_looppath.py: {a.sym} has {len(loops)} loops, '
                 f'no rank {a.rank}')
    if a.max is not None and loops[a.rank - 1][0] > a.max:
        print(f'elf_looppath.py: FAIL {a.sym} loop {a.rank} '
              f'{loops[a.rank - 1][0]} > {a.max}')
        sys.exit(1)


if __name__ == '__main__':
    main()
