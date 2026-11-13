#!/usr/bin/env python3
"""
colloid_cpu.py: the Colloid's instructions per sample (S095 P2; S095 P3).

What:       for each mode, from the linked ELF (the space_cpu.py method,
            S095 P1):
              per-sample loops: the first pair loop (store) + (pairs - 1)
              add loops + the filter/output loop of the mode, each at its
              longest path;
              per-block paths / 32: Colloid_calcVoiceAsync() (its straight
              path) + the oscillator-control loop x the oscillator count (the
              longest path: the pwm branch is an upper bound for the plain
              modes; S095 P3: the loop is col_oscControl(), out of line, whose
              straight path is added once) + every call the async and the
              control make to the voice's own helpers kept out of line
              (col_svfControl, col_egStep, col_wtbBind, col_pspK: the bind
              counted every block, its dearest case) + the sync block's
              path + each oscillator loop's entry/exit path + the filter
              loop's entry/exit path.
            Modes: plain (COL_OSC_COUNT oscillators, from --count), pwm (the
            Saw/Rec pair loops, COL_PWM_COUNT 6) and pwmw (S095 P3: the
            Sin/Tri warp loops, one oscillator per loop, 6), each stereo (rte
            str, `par`, the dearest) and mono (`ser`).
Why:        the device run is the gate (S094 P2: measured cycles per block,
            cold, dearest, stereo send, against the Basic 19,620); these
            counts are the host regression baseline, recorded at step 5 in
            tools/dsp_test/baselines/s095p2/ and compared by --baseline
            (S095 P3 grows them by design: its baseline is
            tools/dsp_test/baselines/s095p3/colloid_cpu.txt).
Inputs:     --elf PATH; --count N (COL_OSC_COUNT, 12 or 10); --baseline FILE
            (optional: fail when a mode grows by more than 0.5 per sample).
Outputs:    one line per mode; exit 1 on a missing symbol or a growth.
Accessors:  tools/dsp_test/Makefile colloid_cpu_check (after `make all`).
Affiliates: elf_looppath.py, elf_blockpath.py, space_cpu.py; ColloidVoice.c,
            ColloidOsc.c.
"""
import argparse
import re
import sys

import elf_blockpath as B
import elf_looppath as E

FRAMES = 32
PWM_COUNT = 6
HELPERS = ('col_svfControl', 'col_egStep', 'col_wtbBind', 'col_pspK')
CTL = 'col_oscControl'   # S095 P3: the oscillator-control loop's own function


def loops(elf, sym):
    """[(longest, shortest)] of the function's distinct loops, longest first
    (space_cpu.py's rule: a cycle found again through an inner head counts
    once)."""
    r = E.rows(elf, sym)
    idx = {a: i for i, (a, _, _) in enumerate(r)}
    heads = sorted({E.target(ops) for (addr, op, ops) in r
                    if E.BRANCH.match(op) and E.target(ops) is not None
                    and E.target(ops) <= addr and E.target(ops) in idx})
    out = {}
    for h in heads:
        p = E.loop_paths(r, idx, h)
        if p:
            p.sort(key=lambda x: -x[0])
            key = frozenset(p[0][2])
            if key not in out:
                out[key] = (p[0][0], p[-1][0])
    return sorted(out.values(), key=lambda x: -x[0])


def calls_to(elf, sym, callee):
    """How many `bl` instructions of sym call callee (any LTO suffix)."""
    return sum(1 for (_, op, ops) in E.rows(elf, sym)
               if op == 'bl' and re.search(r'<' + re.escape(callee) + r'[.>]', ops))


def present(elf, sym):
    try:
        E.rows(elf, sym)
        return True
    except SystemExit:
        return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--elf', required=True)
    ap.add_argument('--count', type=int, default=12)
    ap.add_argument('--baseline')
    a = ap.parse_args()
    for s in ('Colloid_calcVoiceAsync', 'Colloid_calcVoiceSyncBlock',
              'col_oscPairStore', 'col_oscPairAdd', 'col_pwmPairStore',
              'col_pwmPairAdd', 'col_renderPar', 'col_renderSer'):
        if not present(a.elf, s):
            sys.exit(f'colloid_cpu.py: {s} not found (inlined or renamed)')
    ctl_sym = CTL if present(a.elf, CTL) else 'Colloid_calcVoiceAsync'
    ctl = loops(a.elf, ctl_sym)
    if not ctl:
        sys.exit(f'colloid_cpu.py: no oscillator-control loop in {ctl_sym}')
    ctl_len = ctl[0][0]
    helpers = 0
    callers = ['Colloid_calcVoiceAsync'] + ([CTL] if ctl_sym == CTL else [])
    for h in HELPERS:
        if present(a.elf, h):
            helpers += sum(calls_to(a.elf, c, h) for c in callers) * B.block_path(a.elf, h)[0]
    base_blk = (B.block_path(a.elf, 'Colloid_calcVoiceAsync')[0] + helpers +
                B.block_path(a.elf, 'Colloid_calcVoiceSyncBlock')[0] +
                (B.block_path(a.elf, CTL)[0] if ctl_sym == CTL else 0))
    warp = present(a.elf, 'col_warpStore') and present(a.elf, 'col_warpAdd')
    want = {}
    if a.baseline:
        for line in open(a.baseline):
            m = re.match(r'(\S+): .* = ([0-9.]+)', line)
            if m:
                want[m.group(1)] = float(m.group(2))
    fail = False
    modes = [('plain', a.count), ('pwm', PWM_COUNT)] + ([('pwmw', PWM_COUNT)] if warp else [])
    for typ, n in modes:
        store, add = {'plain': ('col_oscPairStore', 'col_oscPairAdd'),
                      'pwm': ('col_pwmPairStore', 'col_pwmPairAdd'),
                      'pwmw': ('col_warpStore', 'col_warpAdd')}[typ]
        # the pair loops render two oscillators per call, the warp loops one
        pairs = n if typ == 'pwmw' else n // 2
        for mode, render in (('par', 'col_renderPar'), ('ser', 'col_renderSer')):
            per_sample = (loops(a.elf, store)[0][0] + (pairs - 1) * loops(a.elf, add)[0][0]
                          + loops(a.elf, render)[0][0])
            blk = (base_blk + n * ctl_len + B.block_path(a.elf, store)[0]
                   + (pairs - 1) * B.block_path(a.elf, add)[0]
                   + B.block_path(a.elf, render)[0])
            total = per_sample + blk / FRAMES
            name = f'{typ}{n}-{mode}'
            note = ''
            if name in want and total > want[name] + 0.5:
                note = f'  GREW (baseline {want[name]:.1f})'
                fail = True
            print(f'{name}: loops {per_sample} + per block {blk}/{FRAMES} = {total:.1f}{note}')
    sys.exit(1 if fail else 0)


if __name__ == '__main__':
    main()
