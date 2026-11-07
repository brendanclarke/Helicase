#!/usr/bin/env python3
"""Longest per-block path through a function, its frame loops weighted 0.

What:       disassembles one function of the linked firmware ELF (as
            elf_looppath.py does) and reports the longest acyclic path from
            its entry to any exit, in instructions, where every instruction
            on a frame loop counts 0: the per-block work on the worst branch
            choices, which the per-frame loop counts (elf_looppath.py) do not
            include. Loops are found as elf_looppath.py --cond-heads
            --in-range finds them (the targets of conditional backward
            branches, each searched between its head and its furthest
            closing branch). The DFS is iterative: an edge back to a node on
            the current stack is dropped, which removes every cycle; the
            longest path is then a pass over the post-order. Calls (`bl`)
            count 1 and are listed, so a caller adds the callee's own count.
            Options:
              --via-loop K  the longest path forced through the head of the
                            K-th longest frame loop (1 = longest): with two
                            branches that each set up and run their own loop
                            (crumpBit_process() stage 4: SVF, one-pole), the
                            path through one loop is that branch's per-block
                            cost;
              --max N       exit 1 when the count is above N.
Why:        S091 P5: the 170-per-frame cap is engine loop + filter loop +
            (per-block work) / 32, and the per-block work had been counted by
            session scratch scripts (blockpath.py, throughpath.py), not a
            repository tool. crumpbit_cpu.py uses this module.
Inputs:     --elf PATH, --sym NAME (the base name; .lto_priv/.constprop/.isra
            suffixes are matched), optional --via-loop K, --max N.
Outputs:    one line: the per-block count and the calls; exit 1 when --max
            is exceeded.
Accessors:  crumpbit_cpu.py (import), tools/dsp_test/Makefile
            crumpbit_cpu_check; by hand.
Affiliates: elf_looppath.py (rows(), successors(), loop_paths()).
"""
import argparse
import re
import sys

import elf_looppath as E


def frame_loops(r, idx):
    """[(longest, head, members)] of the function's frame loops, longest
    first. members: every instruction index on any of the loop's paths."""
    heads = sorted({E.target(ops) for (addr, op, ops) in r
                    if E.BRANCH.match(op) and E.target(ops) is not None
                    and E.target(ops) <= addr and E.target(ops) in idx
                    and op not in E.UNCOND})
    closer = {}
    for i, (addr, op, ops) in enumerate(r):
        t = E.target(ops) if E.BRANCH.match(op) else None
        if t in heads and t <= addr:
            closer[t] = max(closer.get(t, 0), i)
    loops = []
    for h in heads:
        paths = E.loop_paths(r, idx, h, closer[h])
        if paths:
            members = set()
            for p in paths:
                members.update(p[2])
            loops.append((max(p[0] for p in paths), h, members))
    loops.sort(key=lambda x: -x[0])
    return loops


def block_path(elf, sym, via_loop=None):
    """(count, calls): the longest entry-to-exit path with loop
    instructions weighted 0; through the head of the via_loop-th longest
    loop (1-based) when given."""
    r = E.rows(elf, sym)
    idx = {addr: i for i, (addr, _, _) in enumerate(r)}
    loops = frame_loops(r, idx)
    in_loop = set()
    for _, _, members in loops:
        in_loop.update(members)
    n = len(r)
    succ = [E.successors(r, idx, i) for i in range(n)]
    # Iterative DFS from the entry: tree/forward/cross edges form the DAG,
    # back edges (to a node still on the stack) are dropped.
    state = [0] * n
    order = []
    dag = [[] for _ in range(n)]
    stack = [(0, 0)]
    state[0] = 1
    while stack:
        i, k = stack.pop()
        if k < len(succ[i]):
            stack.append((i, k + 1))
            j = succ[i][k]
            if state[j] == 1:
                continue
            dag[i].append(j)
            if state[j] == 0:
                state[j] = 1
                stack.append((j, 0))
        else:
            state[i] = 2
            order.append(i)
    w = [0 if i in in_loop else 1 for i in range(n)]
    # down[i]: the longest path from i to an exit, i included.
    down = [0] * n
    for i in order:
        down[i] = w[i] + max((down[j] for j in dag[i]), default=0)
    calls = sorted({re.sub(r'.*<|>.*', '', ops)
                    for (_, op, ops) in r if op == 'bl'})
    if via_loop is None:
        return down[0], calls
    if via_loop < 1 or via_loop > len(loops):
        sys.exit(f'elf_blockpath.py: {sym} has {len(loops)} loops, '
                 f'not {via_loop}')
    head = idx[loops[via_loop - 1][1]]
    # up[i]: the longest path from the entry to i, i included.
    neg = -10 ** 9
    up = [neg] * n
    up[0] = w[0]
    for i in reversed(order):
        if up[i] == neg:
            continue
        for j in dag[i]:
            up[j] = max(up[j], up[i] + w[j])
    return up[head] + down[head] - w[head], calls


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--elf', required=True)
    ap.add_argument('--sym', required=True)
    ap.add_argument('--via-loop', type=int)
    ap.add_argument('--max', type=int)
    a = ap.parse_args()
    count, calls = block_path(a.elf, a.sym, a.via_loop)
    via = f' (through loop {a.via_loop})' if a.via_loop else ''
    print(f'{a.sym}: per-block longest {count}{via}, loops 0; calls {calls}')
    if a.max is not None and count > a.max:
        print(f'elf_blockpath.py: FAIL {a.sym} per block {count} > {a.max}')
        sys.exit(1)


if __name__ == '__main__':
    main()
