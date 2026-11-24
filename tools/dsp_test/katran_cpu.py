#!/usr/bin/env python3
"""
katran_cpu.py: the Katran's instructions per sample (S103 P1).

What:       from the linked ELF (the tetrakt_cpu.py / colloid_cpu.py method):
              per-sample loops: kat_delayRead (both cores) + 2 x
              kat_coreRender + kat_outRender + the dearest OSC B form + the
              dearest OSC A form twice (the L/R pair), each at its longest
              path;
              per-block paths / 32: Katran_calcVoiceAsync (straight path) +
              Katran_calcVoiceSyncBlock (straight path) + the out-of-line
              helpers it calls (kat_oscControl, kat_delayControl,
              kat_runControl, kat_plfoStep, kat_ceStep x2, kat_svfControl x3,
              kat_guardUpdate x3) + every per-sample loop's entry/exit path.
Why:        the schedule's gate G-CPU1 (<= 380 all-in) and G-CPU2 (the arena
            form is never dearer than the PWM form it replaces).
Inputs:     --elf PATH; --baseline FILE (optional: fail on a growth > 0.5).
Outputs:    one line "katran: loops L + per block B/32 = T"; exit 1 on a
            missing symbol, a growth, a failed gate, or the arena form over
            its PWM sibling.
Accessors:  make -C tools/dsp_test katran_cpu_check (after the root build).
"""
import argparse
import re
import sys

import elf_blockpath as B
import elf_looppath as E

FRAMES = 32
GATE = 380.0
PER_SAMPLE = ('kat_delayRead', 'kat_coreRender', 'kat_outRender',
              'kat_bPlain', 'kat_bPwmD', 'kat_bWarp', 'kat_bArena',
              'kat_aPlain', 'kat_aPwmD', 'kat_aWarp', 'kat_aArena')
OPTIONAL = ('kat_oscControl', 'kat_delayControl', 'kat_runControl',
            'kat_plfoStep', 'kat_ceStep', 'kat_svfControl', 'kat_guardUpdate',
            'kat_ceStart')


def loops(elf, sym):
    """The function's distinct loops, longest first (tetrakt_cpu.py's rule)."""
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
                out[key] = p[0][0]
    return sorted(out.values(), key=lambda x: -x)


def present(elf, sym):
    try:
        E.rows(elf, sym)
        return True
    except SystemExit:
        return False


def longest(elf, sym):
    got = loops(elf, sym)
    if not got:
        sys.exit(f'katran_cpu.py: no loop in {sym}')
    return got[0]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--elf', required=True)
    ap.add_argument('--baseline')
    a = ap.parse_args()
    for s in PER_SAMPLE + ('Katran_calcVoiceAsync', 'Katran_calcVoiceSyncBlock'):
        if not present(a.elf, s):
            sys.exit(f'katran_cpu.py: {s} not found (inlined or renamed)')

    b_plain = longest(a.elf, 'kat_bPlain')
    b_pwm = longest(a.elf, 'kat_bPwmD')
    b_warp = longest(a.elf, 'kat_bWarp')
    b_arena = longest(a.elf, 'kat_bArena')
    a_plain = longest(a.elf, 'kat_aPlain')
    a_pwm = longest(a.elf, 'kat_aPwmD')
    a_warp = longest(a.elf, 'kat_aWarp')
    a_arena = longest(a.elf, 'kat_aArena')
    b_max = max(b_plain, b_pwm, b_warp)
    a_max = max(a_plain, a_pwm, a_warp)
    arena_ok = (b_arena <= max(b_pwm, b_warp) and a_arena <= max(a_pwm, a_warp))

    per_sample = (longest(a.elf, 'kat_delayRead')
                  + 2 * longest(a.elf, 'kat_coreRender')
                  + longest(a.elf, 'kat_outRender')
                  + b_max + 2 * a_max)
    blk = (B.block_path(a.elf, 'Katran_calcVoiceAsync')[0]
           + B.block_path(a.elf, 'Katran_calcVoiceSyncBlock')[0])
    # An out-of-line helper's straight path plus the iterations of its own
    # loop (elf_blockpath weights loop instructions 0): kat_oscControl walks
    # three width entries a block, kat_guardUpdate four samples.
    for s, mult in (('kat_oscControl', 1), ('kat_delayControl', 1),
                    ('kat_runControl', 1), ('kat_plfoStep', 1),
                    ('kat_ceStep', 2), ('kat_ceStart', 2),
                    ('kat_svfControl', 3), ('kat_guardUpdate', 3)):
        if present(a.elf, s):
            blk += mult * B.block_path(a.elf, s)[0]
            if s == 'kat_guardUpdate':
                if loops(a.elf, s):
                    blk += mult * 4 * longest(a.elf, s)
            elif s == 'kat_oscControl':
                if loops(a.elf, s):
                    blk += mult * 3 * longest(a.elf, s)
    for s in ('kat_delayRead', 'kat_coreRender', 'kat_outRender', 'kat_bPlain',
              'kat_bPwmD', 'kat_bWarp', 'kat_bArena', 'kat_aPlain', 'kat_aPwmD',
              'kat_aWarp', 'kat_aArena'):
        blk += B.block_path(a.elf, s)[0]
    total = per_sample + blk / FRAMES

    note = ''
    fail = total > GATE or not arena_ok
    if total > GATE:
        note = f'  OVER the {GATE:.0f} gate'
    if not arena_ok:
        note += '  ARENA FORM DEARER than its PWM sibling'
    if a.baseline:
        m = re.match(r'katran: loops d+ + per block d+/32 = ([0-9.]+)',
                     open(a.baseline).read())
        if m and total > float(m.group(1)) + 0.5:
            note = f'  GREW (baseline {m.group(1)})'
            fail = True
    print(f'katran: loops {per_sample} + per block {blk}/{FRAMES} = {total:.1f}{note}')
    print(f'  OSC B {b_plain}/{b_pwm}/{b_warp}/{b_arena}  '
          f'OSC A {a_plain}/{a_pwm}/{a_warp}/{a_arena}')
    sys.exit(1 if fail else 0)


if __name__ == '__main__':
    main()
