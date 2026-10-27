#!/usr/bin/env python3
"""Report a function's straight-line instruction count (S085 C17.4).

What:       reads arm-none-eabi-objdump and counts the instructions of one
            function that are outside every backward-branch loop. Used to price
            the fixed overhead of a renderer call in the snc worst case.
Why:        the S085 cost record needs the non-loop part separately from the
            per-sample loop body; no profiler is installed.
Inputs:     --obj FILE --func NAME.
Outputs:    total / loop / straight-line instruction counts.
Accessors:  make -C tools/dsp_test armcheck-osc.
Affiliates: fpseq.py (per-loop counts).
"""
import argparse
import re
import subprocess
import sys

INSN = re.compile(r'^\s*([0-9a-f]+):\s+[0-9a-f]+\s+([a-z.][a-zA-Z0-9.]*)')
BRANCH = re.compile(r'^(b|b[a-z]{1,3}|cbz|cbnz)$')


def parse(obj, func):
    out = subprocess.check_output(
        ['arm-none-eabi-objdump', '-d', obj], text=True)
    lines = out.splitlines()
    start = None
    for i, line in enumerate(lines):
        if re.match(r'^[0-9a-f]+ <' + re.escape(func) + r'>:$', line.strip()):
            start = i + 1
            break
    if start is None:
        sys.exit('looplen: function %s not found' % func)
    insns = []
    for line in lines[start:]:
        if re.match(r'^[0-9a-f]+ <', line.strip()):
            break
        m = INSN.match(line)
        if m:
            insns.append((int(m.group(1), 16), m.group(2), line))
    return insns


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--obj', required=True)
    ap.add_argument('--func', required=True)
    a = ap.parse_args()
    insns = parse(a.obj, a.func)
    marks = [False] * len(insns)
    index_of = {addr: i for i, (addr, _, _) in enumerate(insns)}
    for i, (addr, mnem, line) in enumerate(insns):
        if not BRANCH.match(mnem):
            continue
        m = re.search(r'\b([0-9a-f]+)\b', line.split('\t')[-1])
        if not m:
            continue
        target = int(m.group(1), 16)
        if target in index_of and index_of[target] <= i:
            for k in range(index_of[target], i + 1):
                marks[k] = True
    loops = sum(marks)
    print('%-28s total %3d  loop %3d  straight %3d' %
          (a.func, len(insns), loops, len(insns) - loops))


if __name__ == '__main__':
    main()
