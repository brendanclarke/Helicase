#!/usr/bin/env python3
"""Dump and compare the per-sample loop bodies of firmware symbols (S086 S1).

What:       disassembles the linked firmware ELF and, for every function whose
            base name is listed (any .constprop/.isra/.part suffix included),
            extracts each backward-branch loop body (the instructions from the
            branch target up to the branch). Addresses are removed and branch
            targets are written as offsets from the loop start, so a loop
            that only moved compares equal. Only per-sample loops are kept:
            a body holding a jump table, literal data, a call or a function
            return is dispatch code, not a loop. --compare reports every
            baseline loop that no longer exists under the same base name in
            the new image: first with the exact text, then with the register
            names normalised (r*, s*, d* -> R, S, D). A loop found only after
            normalisation is RENAMED: the same instructions in the same order
            with a different register allocation (the same cost; LTO
            re-inlines the leaf loops into a changed dispatcher). The
            normalisation also folds Thumb encoding choices that follow from
            the register choice: the .w/.n width suffix, the flag-setting
            16-bit forms (adds/add, lsrs/lsr, ...), and mov with a shifted
            register (mov.w lr, r1, lsr #22 = lsrs r7, r2, #22). One found
            neither way is MISSING.
Why:        S086 S1 adds stock-wave cases to the oscillator dispatchers
            (calcNextOscSampleBlock(), calcNextOscSampleFmBlock(), the S085
            sync/ring dispatchers). The leaf loops are inlined into those
            dispatchers, so elf_symdump.py reports the dispatchers as CHANGED
            even when no loop changed. This check is the loop-level half of
            the gate: the dispatch code may change, the loops may not.
Inputs:     --elf PATH --sym NAME ... --out PATH, or --compare BASE NOW.
Outputs:    the dump file; with --compare, a report, and exit 1 when a
            baseline loop is missing (RENAMED alone passes).
Accessors:  tools/dsp_test/Makefile elf_loops86_baseline, elf_loops86_check.
Affiliates: elf_symdump.py (whole-function text), fpseq.py (op counts).
"""
import argparse
import collections
import re
import subprocess
import sys

HEAD = re.compile(r'^([0-9a-f]+) <([^>]+)>:$')
ROW = re.compile(r'^\s*([0-9a-f]+):\s+(\S+)\s*(.*)$')
TARGET = re.compile(r'\b([0-9a-f]+) <[^>]+>')
WORD = re.compile(r'\b0x[0-9a-f]{8}\b|\b[0-9a-f]{8}\b')
SUFFIX = re.compile(r'(\.(constprop|isra|part|cold)\.\d+)+$')
DISPATCH = re.compile(r'(\btbb\b|\btbh\b|\.word|\.short|\bbl\b|\bbx\b|\bpop\b|ldmia\.w sp!)')
REG = re.compile(r'\b(r\d+|ip|lr|sl|fp|s\d+|d\d+)\b')
BRANCH = re.compile(
    r'^(b|beq|bne|bcs|bhs|bcc|blo|bmi|bpl|bvs|bvc|bhi|bls|bge|blt|bgt|ble)'
    r'(\.[nw])?$|^cbn?z$')


def base(name):
    return SUFFIX.sub('', name)


def functions(elf, wanted):
    text = subprocess.run(['arm-none-eabi-objdump', '-d', '--no-show-raw-insn', elf],
                          check=True, capture_output=True, text=True).stdout
    out, cur = {}, None
    for line in text.splitlines():
        m = HEAD.match(line)
        if m:
            cur = m.group(2) if base(m.group(2)) in wanted else None
            if cur:
                out[cur] = []
            continue
        if cur is None:
            continue
        if not line.strip():
            cur = None
            continue
        r = ROW.match(line)
        if r:
            out[cur].append((int(r.group(1), 16), r.group(2), r.group(3)))
    return out


def loops(rows):
    found = []
    for addr, op, args in rows:
        if not BRANCH.match(op):
            continue
        t = TARGET.search(args)
        if not t or int(t.group(1), 16) >= addr:
            continue
        lo = int(t.group(1), 16)
        body = []
        for a, o, g in rows:
            if lo <= a <= addr:
                g = TARGET.sub(lambda x: '+%d' % (int(x.group(1), 16) - lo), g)
                g = WORD.sub('ADDR', g)
                body.append(re.sub(r'\s+', ' ', (o + ' ' + g)).strip())
        if any(DISPATCH.search(b) for b in body):
            continue
        found.append(' ; '.join(body))
    return found


def write(path, syms):
    with open(path, 'w') as f:
        for name in sorted(syms):
            for body in syms[name]:
                f.write(f'{name}\t{body}\n')


FLAGSET = re.compile(r'^(add|sub|lsr|lsl|asr|mov|and|orr|eor|mul|rsb|bic|mvn)s\b')
MOVSHIFT = re.compile(r'^mov (R), (R), (lsl|lsr|asr) (#\d+)$')


def regnorm(body):
    """Register names and Thumb encoding choices normalised (see What)."""
    out = []
    for ins in body.split(' ; '):
        ins = REG.sub(lambda m: m.group(1)[0].upper() if m.group(1)[0] in 'sd'
                      else 'R', ins)
        ins = re.sub(r'^(\S+?)\.(w|n)\b', r'\1', ins)
        ins = FLAGSET.sub(r'\1', ins)
        ins = MOVSHIFT.sub(r'\3 \1, \2, \4', ins)
        ins = re.sub(r'\s*@.*$', '', ins)
        out.append(ins)
    return ' ; '.join(out)


def read(path):
    out = collections.defaultdict(collections.Counter)
    with open(path) as f:
        for line in f:
            name, _, body = line.rstrip('\n').partition('\t')
            out[base(name)][body] += 1
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--elf')
    ap.add_argument('--sym', action='append', default=[])
    ap.add_argument('--out')
    ap.add_argument('--compare', nargs=2)
    a = ap.parse_args()
    if a.compare:
        old, new = read(a.compare[0]), read(a.compare[1])
        nold = sum(sum(c.values()) for c in old.values())
        missing = renamed = 0
        for name in sorted(old):
            now = new.get(name, collections.Counter())
            lost = old[name] - now
            if not lost:
                continue
            spare = collections.Counter()
            for body, n in (now - old[name]).items():
                spare[regnorm(body)] += n
            for body, n in lost.items():
                k = regnorm(body)
                hit = min(n, spare[k])
                spare[k] -= hit
                if hit:
                    renamed += hit
                    print(f'RENAMED {name} x{hit} ({len(body.split(" ; "))} '
                          f'instructions): {body[:110]}...')
                if n - hit:
                    missing += n - hit
                    print(f'MISSING {name} x{n - hit} '
                          f'({len(body.split(" ; "))} instructions): '
                          f'{body[:160]}...')
        nnew = sum(sum(c.values()) for c in new.values())
        print(f'elf_loops: {nold} baseline loops, {nold - missing - renamed} '
              f'exact, {renamed} renamed, {missing} missing; {nnew} loops now')
        return 1 if missing else 0
    if not (a.elf and a.out and a.sym):
        ap.error('--elf, --out and at least one --sym are required')
    syms = {n: loops(r) for n, r in functions(a.elf, set(a.sym)).items()}
    if not syms:
        sys.exit('elf_loopdump.py: no listed symbol found')
    write(a.out, syms)
    print(f'elf_loopdump: {sum(len(v) for v in syms.values())} loops in '
          f'{len(syms)} symbols -> {a.out}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
