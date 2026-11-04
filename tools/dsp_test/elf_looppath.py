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
            with a conditional branch).
Outputs:    one line per loop; exit 1 when --max is exceeded.
Accessors:  tools/dsp_test/Makefile crumpbit_elf_check; by hand.
Affiliates: elf_loopdump.py (loop text compare), fpseq.py (value ops).
"""
import argparse
import re
import subprocess
import sys

BRANCH = re.compile(
    r'^(b|beq|bne|bcs|bhs|bcc|blo|bmi|bpl|bvs|bvc|bhi|bls|bge|blt|bgt|ble)'
    r'(\.[nw])?$|^cbn?z$')
UNCOND = {'b', 'b.n', 'b.w'}
SUFFIX = r'(\.(lto_priv|constprop|isra|part|cold)\.\d+)*'


def rows(elf, sym):
    text = subprocess.run(['arm-none-eabi-objdump', '-d', '--no-show-raw-insn',
                           elf], check=True, capture_output=True,
                          text=True).stdout
    m = re.search(r'^[0-9a-f]+ <' + re.escape(sym) + SUFFIX +
                  r'>:\n(?P<body>.*?)(?:\n\n|\Z)', text, re.S | re.M)
    if not m:
        sys.exit(f'elf_looppath.py: {sym} not found')
    out = []
    for line in m.group('body').splitlines():
        p = re.match(r'\s*([0-9a-f]+):\s+(\S+)\s*(.*)', line)
        if p and not p.group(2).startswith('.'):
            out.append((int(p.group(1), 16), p.group(2), p.group(3)))
    return out


def target(ops):
    m = re.search(r'\b([0-9a-f]+) <', ops)
    return int(m.group(1), 16) if m else None


def is_return(op, ops):
    return op == 'bx' or op.startswith('pop') or \
        (op.startswith('ldmia') and 'pc' in ops)


def successors(r, idx, i):
    _, op, ops = r[i]
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


def loop_paths(r, idx, head):
    """Every simple path head -> ... -> head, as (length, vdivs, indices)."""
    found = []
    stack = [(idx[head], [idx[head]])]
    while stack:
        i, path = stack.pop()
        for j in successors(r, idx, i):
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
    for h in heads:
        p = loop_paths(r, idx, h)
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
    if a.max is not None and loops[0][0] > a.max:
        print(f'elf_looppath.py: FAIL {a.sym} longest {loops[0][0]} > '
              f'{a.max}')
        sys.exit(1)


if __name__ == '__main__':
    main()
