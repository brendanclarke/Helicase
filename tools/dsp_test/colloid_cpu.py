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
            Saw/Rec pair loops, COL_PWM_COUNT) and pwmw (S095 P3: the
            Sin/Tri warp loops, one oscillator per loop, COL_PWM_WARP_COUNT), each
            stereo (rte str, `par`, the dearest) and mono (`ser`); S096 P3
            adds scan (the wts two-frame pair loops, COL_SCAN_COUNT), which
            also charges col_scanControl() (the frame positions: its loop x
            the count + its straight path).
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
import os
import re
import sys

import elf_blockpath as B
import elf_looppath as E

FRAMES = 32
# S095 P5 (user, 2026-11-14): the pwm counts are read from ColloidVoice.h,
# not repeated here, so these counts follow the firmware: COL_PWM_COUNT (8)
# for the Saw/Rec pair loops (`pwm` modes) and COL_PWM_WARP_COUNT (6) for the
# Sin/Tri warp loops (`pwmw` modes); before S095 P5 both were 6. The plain
# count is still --count (it can be overridden with -DCOL_OSC_COUNT; the pwm
# counts cannot).
def _voice_define(name):
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..',
                        'Core', 'DSP', 'Instruments', 'Colloid', 'ColloidVoice.h')
    m = re.search(r'^#define\s+' + name + r'\s+(\d+)u', open(path).read(), re.M)
    if not m:
        sys.exit(f'colloid_cpu.py: {name} not found in ColloidVoice.h')
    return int(m.group(1))


def _c_define(name):
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..',
                        'Core', 'DSP', 'Instruments', 'Colloid', 'ColloidVoice.c')
    m = re.search(r'^#define\s+' + name + r'\s+(\d+)u', open(path).read(), re.M)
    return int(m.group(1)) if m else 1


SUB_BLOCK = _c_define('COL_SUB_BLOCK')   # S096 P3: the stereo loop's coefficient step
PWM_COUNT = _voice_define('COL_PWM_COUNT')
PWM_WARP_COUNT = _voice_define('COL_PWM_WARP_COUNT')
SCAN_COUNT = _voice_define('COL_SCAN_COUNT')   # S096 P3
HELPERS = ('col_svfControl', 'col_egStep', 'col_wtbBind', 'col_pspK',
           'col_noiseControl')                 # S096 P3: the noise control
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


def render_per_sample(elf, sym):
    """S096 P3: a filter/output loop with a nested sub-block (the stereo
    loop steps its filter coefficients every COL_SUB_BLOCK samples) costs
    the inner loop every sample plus the outer loop's own part once per
    sub-block: inner + (outer - inner) / COL_SUB_BLOCK. A single loop is
    charged as before. Returns a float."""
    r = E.rows(elf, sym)
    spans = []
    for (addr, op, ops) in r:
        t = E.target(ops) if E.BRANCH.match(op) else None
        if t is not None and t < addr and op not in ('b', 'b.n', 'b.w'):
            spans.append(sum(1 for (x, _, _) in r if t <= x <= addr))
    if len(spans) < 2 or SUB_BLOCK <= 1:
        return loops(elf, sym)[0][0]
    inner, outer = min(spans), max(spans)
    return inner + (outer - inner) / SUB_BLOCK


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
    scan = present(a.elf, 'col_scanPairStore') and present(a.elf, 'col_scanPairAdd')
    want = {}
    if a.baseline:
        for line in open(a.baseline):
            m = re.match(r'(\S+): .* = ([0-9.]+)', line)
            if m:
                want[m.group(1)] = float(m.group(2))
    fail = False
    modes = ([('plain', a.count), ('pwm', PWM_COUNT)]
             + ([('pwmw', PWM_WARP_COUNT)] if warp else [])
             + ([('scan', SCAN_COUNT)] if scan else []))
    for typ, n in modes:
        store, add = {'plain': ('col_oscPairStore', 'col_oscPairAdd'),
                      'pwm': ('col_pwmPairStore', 'col_pwmPairAdd'),
                      'pwmw': ('col_warpStore', 'col_warpAdd'),
                      'scan': ('col_scanPairStore', 'col_scanPairAdd')}[typ]
        # the pair loops render two oscillators per call, the warp loops one
        pairs = n if typ == 'pwmw' else n // 2
        for mode, render in (('par', 'col_renderPar'), ('ser', 'col_renderSer')):
            per_sample = (loops(a.elf, store)[0][0] + (pairs - 1) * loops(a.elf, add)[0][0]
                          + render_per_sample(a.elf, render))
            blk = (base_blk + n * ctl_len + B.block_path(a.elf, store)[0]
                   + (pairs - 1) * B.block_path(a.elf, add)[0]
                   + B.block_path(a.elf, render)[0])
            # S096 P3: the wts frame positions run in their own per-block
            # pass (col_scanControl(), one loop iteration per oscillator)
            if typ == 'scan' and present(a.elf, 'col_scanControl'):
                blk += (n * loops(a.elf, 'col_scanControl')[0][0]
                        + B.block_path(a.elf, 'col_scanControl')[0])
            total = per_sample + blk / FRAMES
            name = f'{typ}{n}-{mode}'
            note = ''
            if name in want and total > want[name] + 0.5:
                note = f'  GREW (baseline {want[name]:.1f})'
                fail = True
            print(f'{name}: loops {per_sample:.1f} + per block {blk}/{FRAMES} = {total:.1f}{note}')
    sys.exit(1 if fail else 0)


if __name__ == '__main__':
    main()
