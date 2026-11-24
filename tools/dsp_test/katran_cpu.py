#!/usr/bin/env python3
"""
katran_cpu.py: the Katran's instructions per sample (S103 P1; S103 P2: the
full-rate core and the half-rate core; S103 P4: named core A and core B).

What:       from the linked ELF (the tetrakt_cpu.py / colloid_cpu.py method):
              per-sample loops, each at its longest path, per full-rate
              output sample:
                kat_delayReadA + kat_delayReadB / 2
                + kat_coreRenderA + kat_coreRenderB / 2
                + kat_outRender
                + the dearest OSC B form
                + the dearest core A OSC A form (kat_a*CoreA)
                + the dearest core B OSC A form (kat_a*CoreB) / 2;
              a half-rate loop runs 16 iterations for 32 output samples, so
              it counts half its per-iteration path.
              per-block paths / 32: Katran_calcVoiceAsync (straight path) +
              Katran_calcVoiceSyncBlock (straight path) + the out-of-line
              helpers it calls (kat_oscControl, kat_delayControl,
              kat_runControl, kat_plfoStep, kat_ceStep x2, kat_ceStart x2,
              kat_svfControl x2 (F1 only: F3 is inline since L1),
              kat_stereoTargets (S103 P4),
              kat_guardUpdate x3 with its 4 + 4 + 2 loop iterations) + the
              entry/exit path of each loop function that runs in a block: the
              four delay/core/output functions, and of the forms only the
              dearest entry of each set (one B, one A core A, one A core B run).
Why:        the schedule's gate G-CPU1 and G-CPU2 (the arena form is never
            dearer than the PWM form it replaces). S103 P2: each loop function
            holds exactly one per-sample loop, so a count is one sample of one
            core (the P1 two-core kat_delayRead left that open, review M1);
            and the per-block sum no longer adds the entries of forms that do
            not run (review §16.4).
Inputs:     --elf PATH; --baseline FILE (optional: fail on a growth > 0.5).
Outputs:    one line "katran: loops L + per block B/32 = T"; exit 1 on a
            missing symbol, a growth, a failed gate, or an arena form over
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
B_FORMS = ('kat_bPlain', 'kat_bPwmD', 'kat_bWarp', 'kat_bArena')
# S103 P4: the cores are A (full rate) and B (half rate); formerly *L / *H
AL_FORMS = ('kat_aPlainCoreA', 'kat_aPwmDCoreA', 'kat_aWarpCoreA', 'kat_aArenaCoreA')
AH_FORMS = ('kat_aPlainCoreB', 'kat_aPwmDCoreB', 'kat_aWarpCoreB', 'kat_aArenaCoreB')
CORE = ('kat_delayReadA', 'kat_delayReadB', 'kat_coreRenderA', 'kat_coreRenderB',
        'kat_outRender')
PER_SAMPLE = CORE + B_FORMS + AL_FORMS + AH_FORMS


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


def forms(elf, names):
    """(plain, pwm, warp, arena) longest loops of one form set."""
    return tuple(longest(elf, s) for s in names)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--elf', required=True)
    ap.add_argument('--baseline')
    a = ap.parse_args()
    for s in PER_SAMPLE + ('Katran_calcVoiceAsync', 'Katran_calcVoiceSyncBlock'):
        if not present(a.elf, s):
            sys.exit(f'katran_cpu.py: {s} not found (inlined or renamed)')

    b = forms(a.elf, B_FORMS)
    al = forms(a.elf, AL_FORMS)
    ah = forms(a.elf, AH_FORMS)
    b_max, al_max, ah_max = max(b[:3]), max(al[:3]), max(ah[:3])
    arena_ok = all(s[3] <= max(s[1], s[2]) for s in (b, al, ah))

    per_sample = (longest(a.elf, 'kat_delayReadA')
                  + longest(a.elf, 'kat_delayReadB') / 2.0
                  + longest(a.elf, 'kat_coreRenderA')
                  + longest(a.elf, 'kat_coreRenderB') / 2.0
                  + longest(a.elf, 'kat_outRender')
                  + b_max + al_max + ah_max / 2.0)
    blk = (B.block_path(a.elf, 'Katran_calcVoiceAsync')[0]
           + B.block_path(a.elf, 'Katran_calcVoiceSyncBlock')[0])
    # An out-of-line helper's straight path plus the iterations of its own
    # loop (elf_blockpath weights loop instructions 0): kat_oscControl walks
    # three width entries a block; kat_guardUpdate 4 + 4 + 2 samples (B, A core A,
    # A core B at 32, 32, 16 samples, every 8th).
    for s, mult in (('kat_oscControl', 1), ('kat_delayControl', 1),
                    ('kat_runControl', 1), ('kat_plfoStep', 1),
                    ('kat_ceStep', 2), ('kat_ceStart', 2),
                    ('kat_svfControl', 2), ('kat_guardUpdate', 3),
                    # S103 P4: the stereo matrix targets (out of line: the
                    # init shares it), once a block from the async
                    ('kat_stereoTargets', 1)):
        if present(a.elf, s):
            blk += mult * B.block_path(a.elf, s)[0]
            if s == 'kat_guardUpdate':
                if loops(a.elf, s):
                    blk += 10 * longest(a.elf, s)
            elif s == 'kat_oscControl':
                if loops(a.elf, s):
                    blk += 3 * longest(a.elf, s)
    for s in CORE:
        blk += B.block_path(a.elf, s)[0]
    for names in (B_FORMS, AL_FORMS, AH_FORMS):
        blk += max(B.block_path(a.elf, s)[0] for s in names)
    total = per_sample + blk / FRAMES

    note = ''
    fail = total > GATE or not arena_ok
    if total > GATE:
        note = f'  OVER the {GATE:.0f} gate'
    if not arena_ok:
        note += '  ARENA FORM DEARER than its PWM sibling'
    if a.baseline:
        # S103 P2: the P1 pattern read `d+` where it meant `\d+`, so it never
        # matched and the baseline check never ran.
        m = re.search(r'katran: loops [0-9.]+ \+ per block \d+/32 = ([0-9.]+)',
                      open(a.baseline).read())
        if m and total > float(m.group(1)) + 0.5:
            note = f'  GREW (baseline {m.group(1)})'
            fail = True
    print(f'katran: loops {per_sample:.1f} + per block {blk}/{FRAMES} = {total:.1f}{note}')
    print(f'  OSC B {b[0]}/{b[1]}/{b[2]}/{b[3]}  '
          f'OSC A core A {al[0]}/{al[1]}/{al[2]}/{al[3]}  '
          f'OSC A core B {ah[0]}/{ah[1]}/{ah[2]}/{ah[3]}  (plain/pwmD/warp/arena)')
    print(f'  delay A/B {longest(a.elf, "kat_delayReadA")}/{longest(a.elf, "kat_delayReadB")}  '
          f'core A/B {longest(a.elf, "kat_coreRenderA")}/{longest(a.elf, "kat_coreRenderB")}  '
          f'out {longest(a.elf, "kat_outRender")}  (per iteration)')
    sys.exit(1 if fail else 0)


if __name__ == '__main__':
    main()
