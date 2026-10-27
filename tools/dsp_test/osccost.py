#!/usr/bin/env python3
"""Worst-case per-sample instruction counts of DSP loops (S085 A3).

What:       disassembles an ARM object and, for each named function, builds
            the instruction-level control-flow graph, finds every natural
            loop (a back edge latch -> header and the instructions that reach
            the latch without passing the header), and reports the longest
            path from the header round to the latch: the instructions one
            iteration executes in its most expensive case. Inner loops are
            reported on their own; an outer loop that contains one is marked
            "outer" and its path excludes the inner body.
Why:        the C19.2 cost record needs per-sample worst cases for loops with
            branches (pwm families, smp/slp end and wrap tests, the wtb depth
            switch, the lane test). looplen.py counts only instructions
            outside loops; fpseq.py --report sums every instruction inside a
            backward-branch address range, which overcounts both arms of an
            if/else and misses blocks placed after the latch. A probe wrapper
            (armcheck/probe_*.c) fixes its block constants; a loop the
            compiler versions (e.g. FM buffer NULL or not) shows as several
            loops, and the case's worst case is the largest.
Inputs:     --obj OBJ, --func NAME (repeat).
Outputs:    "<func> loop@<addr> worst <n>, best <m>[, vdiv <d>][, calls <c>]".
            Calls count as one instruction (the bl); callees are separate.
Accessors:  make -C tools/dsp_test armcheck-osc / armcheck-osc-base.
Affiliates: fpseq.py (disassembly helpers), armcheck/probe_*.c.
"""
import argparse
import re
import sys

sys.dont_write_bytecode = True   # importing fpseq must not leave __pycache__
import fpseq  # noqa: E402

UNCOND = re.compile(r'^b(\.w|\.n)?$')
COND = re.compile(r'^(b(eq|ne|cs|hs|cc|lo|mi|pl|vs|vc|hi|ls|ge|lt|gt|le)'
                  r'(\.w|\.n)?|cbn?z)$')


def is_return(op, args):
    if op in ('bx',) and 'lr' in args:
        return True
    if op.startswith('pop') and 'pc' in args:
        return True
    if op.startswith('ldmia') and 'pc' in args:
        return True
    return False


def graph(rows):
    addrs = [a for a, _, _ in rows]
    index = {a: i for i, a in enumerate(addrs)}
    succ = [[] for _ in rows]
    for i, (a, op, args) in enumerate(rows):
        t = re.search(r'\b([0-9a-f]+)\s*<', args)
        target = int(t.group(1), 16) if t else None
        nxt = i + 1 if i + 1 < len(rows) else None
        if is_return(op, args):
            continue
        if UNCOND.match(op):
            if target in index:
                succ[i].append(index[target])
            continue
        if COND.match(op):
            if target in index:
                succ[i].append(index[target])
            if nxt is not None:
                succ[i].append(nxt)
            continue
        if nxt is not None:
            succ[i].append(nxt)
    return succ


def dominators(succ):
    """Iterative dominator sets from the function entry (instruction 0)."""
    n = len(succ)
    pred = [[] for _ in succ]
    for i, ss in enumerate(succ):
        for j in ss:
            pred[j].append(i)
    reach = set()
    stack = [0]
    while stack:
        x = stack.pop()
        if x in reach:
            continue
        reach.add(x)
        stack.extend(succ[x])
    full = frozenset(reach)
    dom = {i: full for i in reach}
    dom[0] = frozenset([0])
    changed = True
    while changed:
        changed = False
        for i in sorted(reach):
            if i == 0:
                continue
            ps = [p for p in pred[i] if p in reach]
            new = frozenset.intersection(*[dom[p] for p in ps]) | {i} \
                if ps else frozenset([i])
            if new != dom[i]:
                dom[i] = new
                changed = True
    return dom, pred


def natural_loops(succ):
    """Loops from back edges i -> h where h dominates i."""
    dom, pred = dominators(succ)
    loops = {}
    for i in dom:
        for h in succ[i]:
            if h in dom.get(i, ()):
                body = {h, i}
                stack = [i]
                while stack:
                    x = stack.pop()
                    for p in pred[x]:
                        if p not in body and p in dom:
                            body.add(p)
                            stack.append(p)
                loops.setdefault(h, (set(), set()))
                loops[h][0].update(body)
                loops[h][1].add(i)
    return loops


def path_lengths(succ, header, body, latches, skip):
    """Longest/shortest header -> latch path inside body, no back edges.

    Edges into the header are the back edges; the remaining subgraph of the
    body (inner-loop bodies removed) must be acyclic. Returns (None, None)
    when it is not (an irreducible region) or no latch is reachable.
    """
    nodes = [n for n in body if n not in skip or n == header]
    nodeset = set(nodes)
    edges = {n: [s for s in succ[n] if s in nodeset and s != header]
             for n in nodes}
    state = {}
    order = []

    def visit(n):
        stack = [(n, iter(edges[n]))]
        state[n] = 1
        while stack:
            node, it = stack[-1]
            nxt = next(it, None)
            if nxt is None:
                state[node] = 2
                order.append(node)
                stack.pop()
            elif state.get(nxt) == 1:
                raise ValueError('cycle')
            elif nxt not in state:
                state[nxt] = 1
                stack.append((nxt, iter(edges[nxt])))
    try:
        visit(header)
    except ValueError:
        return None, None, set()
    order.reverse()
    longest = {header: 1}
    shortest = {header: 1}
    for n in order:
        if n not in longest:
            continue
        for s2 in edges[n]:
            longest[s2] = max(longest.get(s2, 0), longest[n] + 1)
            shortest[s2] = min(shortest.get(s2, 1 << 30), shortest[n] + 1)
    ends = [l for l in latches if l in longest]
    if not ends:
        return None, None, set()
    return (max(longest[l] for l in ends), min(shortest[l] for l in ends),
            set(longest))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--obj', required=True)
    ap.add_argument('--func', action='append', required=True)
    a = ap.parse_args()
    text = fpseq.disasm(a.obj)
    for name in a.func:
        rows = [(ad, fpseq.norm(op), args)
                for ad, op, args in fpseq.function_rows(text, name)]
        succ = graph(rows)
        loops = natural_loops(succ)
        if not loops:
            print(f'{name:28s} no loop')
            continue
        for h in sorted(loops):
            body, latches = loops[h]
            inner = [h2 for h2 in loops if h2 != h and h2 in body]
            skip = set()
            for h2 in inner:
                skip |= loops[h2][0]
            worst, best, reached = path_lengths(succ, h, body, latches,
                                                skip - {h})
            if worst is None:
                continue                 # not a loop iteration path
            ops = [rows[i][1] for i in reached]
            inner = [h2 for h2 in inner if h2 in reached]
            vdiv = sum(1 for o in ops if o.startswith('vdiv'))
            calls = sum(1 for o in ops if o in ('bl', 'blx'))
            tag = ' (outer)' if inner else ''
            print(f'{name:28s} loop@{rows[h][0]:04x} worst {worst}, best {best}'
                  + (f', vdiv {vdiv}' if vdiv else '')
                  + (f', calls {calls}' if calls else '') + tag)
    return 0


if __name__ == '__main__':
    sys.exit(main())
