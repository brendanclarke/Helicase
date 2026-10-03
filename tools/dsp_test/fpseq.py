#!/usr/bin/env python3
"""Compare value-affecting ARM instructions in extracted DSP loops.

What:       disassembles an ARM object and compares backward-branch loop
            bodies by their VFP/conversion/saturation instruction multisets.
Why:        an S0 host result must also retain the target's per-sample float
            operation sequence under -Ofast.
Inputs:     --obj, repeated --ref/--new function[:loop], optional --report and
            --all, and explicit --shared-op allowances for fused inputs (each
            removes one instance of an operation that at least two --ref
            loops perform; see main()).
Outputs:    loop multisets and MATCH/DIFF; DIFF exits 1 unless --report.
Accessors:  the armcheck targets in the harness Makefile.
Affiliates: each armcheck translation unit and the frozen source snapshot.
"""
import argparse
import collections
import re
import subprocess
import sys

VALUE_OPS = re.compile(
    r'^(vadd|vsub|vmul|vnmul|vfma|vfms|vfnma|vfnms|vdiv|vabs|vneg|vsqrt|vcvt|'
    r'vcmp|vcmpe|ssat|usat|qadd|qadd16|qsub|qsub16)(\.|$)')
NARROW_OPS = re.compile(r'^(sxth|sxtb|uxth)$')
BRANCH = re.compile(
    r'^(b|beq|bne|bcs|bhs|bcc|blo|bmi|bpl|bvs|bvc|bhi|bls|bge|blt|bgt|ble)'
    r'(\.[nw])?$|^cbn?z$')


def norm(op):
    return re.sub(r'\.(w|n)$', '', op)


def disasm(obj):
    return subprocess.run(['arm-none-eabi-objdump', '-d', '--no-show-raw-insn', obj],
                          check=True, capture_output=True, text=True).stdout


def function_rows(text, name):
    m = re.search(r'^[0-9a-f]+ <' + re.escape(name) + r'>:\n(.*?)(?:\n\n|\Z)',
                  text, re.S | re.M)
    if not m:
        sys.exit(f'fpseq.py: {name} not found')
    rows = []
    for line in m.group(1).splitlines():
        p = re.match(r'\s*([0-9a-f]+):\s+(\S+)\s*(.*)', line)
        if p:
            rows.append((int(p.group(1), 16), p.group(2), p.group(3)))
    return rows


def loop_counters(rows, count_all):
    out = []
    for addr, op, args in rows:
        if BRANCH.match(norm(op)):
            t = re.search(r'\b([0-9a-f]+)\s*<', args)
            if t and int(t.group(1), 16) < addr:
                lo = int(t.group(1), 16)
                body = [norm(o) for a, o, _ in rows if lo <= a <= addr]
                values = collections.Counter(o for o in body
                                             if count_all or VALUE_OPS.match(o))
                narrow = collections.Counter(o for o in body if NARROW_OPS.match(o))
                out.append((values, narrow))
    return out


def total(text, items, count_all, label):
    acc = collections.Counter()
    per_item = []
    for item in items:
        name, _, k = item.partition(':')
        loops = loop_counters(function_rows(text, name), count_all)
        chosen = [loops[int(k)]] if k else loops
        item_acc = collections.Counter()
        for i, (c, narrow) in enumerate(chosen):
            print(f'{label} {item} loop{i}: {dict(sorted(c.items()))} narrowing {dict(narrow)}')
            item_acc += c
        acc += item_acc
        per_item.append(item_acc)
    return acc, per_item


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--obj', required=True)
    ap.add_argument('--ref', action='append', required=True)
    ap.add_argument('--new', action='append', required=True)
    ap.add_argument('--report', action='store_true')
    ap.add_argument('--all', action='store_true')
    ap.add_argument('--shared-op', action='append', default=[])
    a = ap.parse_args()
    text = disasm(a.obj)
    ref, ref_items = total(text, a.ref, a.all, 'ref')
    new, _ = total(text, a.new, a.all, 'new')
    # A fused loop can share an input conversion that two old loops performed
    # independently (the mixer: the input sample and the loop index, each
    # converted once instead of twice). Each --shared-op removes one instance
    # of the named operation from the reference, and only while at least one
    # other --ref loop still performs it, so an allowance can never hide an
    # operation that only one old loop had. The multisets carry no operands:
    # that the shared conversions read the same value is the caller's claim,
    # backed by the host harness. --report remains unadjusted so it still
    # exposes the total loop-instruction saving.
    for op, n in collections.Counter(a.shared_op).items():
        users = sum(1 for c in ref_items if c[op] > 0)
        if n > users - 1:
            sys.exit(f'fpseq.py: shared op {op} x{n} needs {n + 1} reference '
                     f'loops performing it; {users} do')
        ref[op] -= n
    print(f'ref total {sum(ref.values())}  new total {sum(new.values())}')
    if a.report:
        return 0
    if ref == new:
        print('MATCH')
        return 0
    print('DIFF', dict((ref - new) + (new - ref)))
    return 1


if __name__ == '__main__':
    sys.exit(main())
