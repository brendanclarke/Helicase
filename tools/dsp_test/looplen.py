#!/usr/bin/env python3
"""Per-loop instruction count report for the S084 insert types.

What:       disassembles ARM objects and prints, per function, every loop
            (a backward branch and the instructions it spans) with its
            instruction, division and call counts.
Why:        S084 VX3 remediation §18.4: the insert-type cost caps (Mix: the
            D+S worst path; transient buffer: 22, each +25 %) are checked on
            the real -Ofast code, one specialised probe per type.
Inputs:     object files. Outputs: one line per probe function.
Accessors:  make -C tools/dsp_test armcheck-vx3.
Affiliates: armcheck/vx3_probe.sh, fpseq.py.
"""
import re
import subprocess
import sys

BR = re.compile(r'^(b|beq|bne|bcs|bhs|bcc|blo|bmi|bpl|bvs|bvc|bhi|bls|bge|'
                r'blt|bgt|ble)(\.[nw])?$')


def functions(obj):
    out = subprocess.run(['arm-none-eabi-objdump', '-d', '--no-show-raw-insn',
                          obj], capture_output=True, text=True,
                         check=True).stdout
    funcs = {}
    cur = None
    for line in out.splitlines():
        m = re.match(r'^[0-9a-f]+ <(.+)>:', line)
        if m:
            cur = m.group(1)
            funcs[cur] = []
            continue
        p = re.match(r'\s*([0-9a-f]+):\s+(\S+)\s*(.*)', line)
        if p and cur:
            funcs[cur].append((int(p.group(1), 16), p.group(2), p.group(3)))
    return funcs


def main():
    for obj in sys.argv[1:]:
        for name, rows in functions(obj).items():
            if not (name.startswith('probe_') or '.constprop' in name):
                continue
            loops = []
            for addr, op, args in rows:
                if not BR.match(op):
                    continue
                t = re.search(r'\b([0-9a-f]+)\s*<', args)
                if not t or int(t.group(1), 16) >= addr:
                    continue
                lo = int(t.group(1), 16)
                body = [o for a, o, _ in rows if lo <= a <= addr]
                divs = sum(1 for o in body if re.match(r'(u|s)div|vdiv', o))
                calls = sum(1 for o in body if o in ('bl', 'blx'))
                loops.append(f'[{len(body)} ins, {divs} div, {calls} call]')
            print(f'{name:30s} ' + '  '.join(loops))


if __name__ == '__main__':
    main()
