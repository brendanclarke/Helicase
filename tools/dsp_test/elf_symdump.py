#!/usr/bin/env python3
"""Dump and compare normalised disassembly of firmware symbols (S085 P1 G-A).

What:       disassembles the linked firmware ELF and writes, for every
            function whose base name is listed (any .constprop/.isra/.part
            suffix included), its instructions with absolute addresses
            removed. --compare reports baseline symbols that are missing or
            changed, and new symbols.
Why:        S085 P1 rule: fm and mix may not change. After LTO the oscillator
            loops live in clones such as calcNextOscSampleBlock.constprop.0.
            Their normalised text before and after the change proves the
            compiled loops are identical even though their addresses moved.
            New clones are allowed (new-code callers) and are listed.
Inputs:     --elf PATH --sym NAME ... --out PATH, or --compare BASE NOW.
Outputs:    the dump file; with --compare, a report, and exit 1 when a
            baseline symbol is missing or changed.
Note:       a trailing run of literal-pool words (".word ADDR") is padding
            tied to the section layout, not code. The comparison strips it
            from both sides and reports a count difference as PADDING only;
            any instruction, branch or middle pool entry that differs is
            still CHANGED. This is S085 P1 deviation D7: the S085 code is
            placed in .itcm.s085, and one previously unused pool word at the
            end of calcNextOscSampleBlock.constprop.0 is no longer emitted.
Accessors:  tools/dsp_test/Makefile elf_osc_baseline, elf_osc_check.
Affiliates: fpseq.py (static loop counts); frozen85/elf_osc.txt.
"""
import argparse
import re
import subprocess
import sys

HEAD = re.compile(r'^[0-9a-f]+ <([^>]+)>:$')
ROW = re.compile(r'^\s*[0-9a-f]+:\s+(.*)$')
TARGET = re.compile(r'\b[0-9a-f]+ (<[^>]+>)')
WORD = re.compile(r'\b0x[0-9a-f]{8}\b|\b[0-9a-f]{8}\b')
SUFFIX = re.compile(r'(\.(constprop|isra|part|cold)\.\d+)+$')


def base(name):
    return SUFFIX.sub('', name)


def dump(elf, wanted):
    text = subprocess.run(['arm-none-eabi-objdump', '-d', '--no-show-raw-insn', elf],
                          check=True, capture_output=True, text=True).stdout
    out, cur = {}, None
    for line in text.splitlines():
        m = HEAD.match(line)
        if m:
            cur = m.group(1) if base(m.group(1)) in wanted else None
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
            ins = TARGET.sub(r'\1', r.group(1))
            ins = WORD.sub('ADDR', ins)
            out[cur].append(re.sub(r'\s+', ' ', ins).strip())
    return out


def write(path, syms):
    with open(path, 'w') as f:
        for name in sorted(syms):
            f.write(f'== {name}\n')
            for ins in syms[name]:
                f.write(ins + '\n')


def read(path):
    syms, cur = {}, None
    with open(path) as f:
        for line in f:
            line = line.rstrip('\n')
            if line.startswith('== '):
                cur = line[3:]
                syms[cur] = []
            elif cur is not None:
                syms[cur].append(line)
    return syms


def strip_pool(lines):
    n = len(lines)
    while n and lines[n - 1] == '.word ADDR':
        n -= 1
    return lines[:n]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--elf')
    ap.add_argument('--sym', action='append', default=[])
    ap.add_argument('--out')
    ap.add_argument('--compare', nargs=2)
    a = ap.parse_args()
    if a.compare:
        old, new = read(a.compare[0]), read(a.compare[1])
        missing = sorted(set(old) - set(new))
        stripped = {n: strip_pool(old[n]) for n in old}
        padding = sorted(n for n in old if n in new and
                         len(old[n]) != len(new[n]) and
                         stripped[n] == strip_pool(new[n]))
        changed = sorted(n for n in old if n in new and
                         stripped[n] != strip_pool(new[n]))
        added = sorted(set(new) - set(old))
        for n in missing:
            print(f'MISSING {n}')
        for n in changed:
            print(f'CHANGED {n}')
        for n in padding:
            print(f'PADDING {n} (trailing literal-pool words only)')
        for n in added:
            print(f'ADDED   {n} (new code only; check its callers)')
        print(f'elf_osc: {len(old)} baseline symbols, {len(missing)} missing, '
              f'{len(changed)} changed, {len(added)} added')
        return 1 if (missing or changed) else 0
    if not (a.elf and a.out and a.sym):
        ap.error('--elf, --out and at least one --sym are required')
    syms = dump(a.elf, set(a.sym))
    if not syms:
        sys.exit('elf_symdump.py: no listed symbol found')
    write(a.out, syms)
    print(f'elf_symdump: {len(syms)} symbols -> {a.out}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
