#!/usr/bin/env python3
"""
space_cpu.py: the Space types' instructions per frame (S095 P1, §S18).

What:       for each type, from the linked ELF:
              sum over its frame loops of (longest path x iterations per
              frame) + its per-block paths / 32,
            and the same plus the type-change feed (sk_feedMix's loop + its
            per-block path / 32). Every Space frame loop is built with one
            iteration per body (SPACE_ONCE, no unrolling, peeling,
            unswitching or predictive commoning), so a loop's length is one
            frame or one tick; the weights below are its iterations per
            output frame:
              1     a frame loop (32 per block);
              0.5   a loop per half-rate tick (16 per block; the Reverb's
                    plate and output, two frames per iteration);
              0.25  a loop per quarter-rate tick (8 per block: the Echo's
                    plate and output, four frames per iteration);
              0.0625 the Reverb's shimmer (both voices every 8th tick).
            The listed loops of a function count (TYPES below), also those
            of routes that exclude each other in one block (the Echo's): the
            total is an upper bound there. The plate's Echo path skips two input
            allpasses: the Echo takes that function's shortest loop path,
            the Reverb its longest.
            Per-block paths count calls as one instruction; the callees are
            listed separately. crumpBit_delaySamples() runs only when `tim`
            or the tempo moves and is left out of the steady total.
            Every type also carries what the stopped-transport idle adds per
            block: sk_idleBefore() and sk_idleAfter() with its four-sample
            meter (effects_process()'s own dispatch predates S095 and is in
            the measured service the caps come from, as for CrumpBit).
Why:        the caps are gates, not hand counts (plan Q15). Caps per type
            (16,634 - its estimated service) / 67.5, as CrumpBit's (plan §1),
            the service 1,070 + 124.5 cycles per row: Delay (15 rows) 202.9,
            Reverb (21) 191.8, Echo (17) 199.2; a feed block +20
            (CrumpBit's 184 / 204 rule).
Inputs:     --elf PATH.
Outputs:    one line per type; exit 1 when a total is above its cap.
Accessors:  tools/dsp_test/Makefile space_cpu_check (after `make all`).
Affiliates: elf_looppath.py, elf_blockpath.py, SpaceEffect.c.
"""
import argparse
import sys

import elf_blockpath as B
import elf_looppath as E

FRAMES = 32


def CAP(rows):
    """The type's cap (plan §1): (16,634 - its service) / 67.5, the service
    1,070 + 124.5 per row (the measured OFF and CrumpBit points, plan §1)."""
    return (16634.0 - (1070.0 + 124.5 * rows)) / 67.5
# Type: (cap, [(sym, ranks, weight, 'longest' | 'shortest')], [per-block
# syms], [(sym, rank, iterations per block)]). ranks: which of the
# function's loops (1 = the longest) are its frame loops; the others are the
# same loop seen from an inner head, or per-block loops, listed last with
# their iterations (the tape set-up's channels; the halfband history and
# the ring-guard copies, 16 B an iteration).
TYPES = {
    'dly': (CAP(15), [('space_delayRun', (1,), 1.0, 'longest')],
            ['space_delayProcess', 'space_tapeBegin', 'space_tapeEnd',
             'space_delayRun'],
            [('space_tapeBegin', 1, 2)]),
    'rvb': (CAP(21), [('space_hbDown', (1,), 0.5, 'longest'),
                  ('space_hbDown', (4,), 1.0, 'longest'),
                  ('space_reverbProcess', (1,), 0.5, 'longest'),
                  ('space_plateRun', (1,), 0.5, 'longest'),
                  ('space_shimmerRun', (1,), 0.0625, 'longest'),
                  ('space_rvbOut', (1,), 0.5, 'longest')],
            ['space_reverbProcess', 'space_plateSetup', 'space_plateRun',
             'space_shimmerRun', 'space_hbDown', 'space_rvbOut'],
            [('space_hbDown', 2, 3), ('space_hbDown', 3, 3),
             ('space_plateRun', 2, 4)]),
    'ech': (CAP(17), [('space_monoPeak', (1,), 1.0, 'longest'),
                  ('space_echoTapeRun', (1,), 1.0, 'longest'),
                  ('space_echoProcess', (1,), 0.25, 'longest'),
                  ('space_plateRun', (1,), 0.25, 'shortest'),
                  ('space_echoOut', (1,), 0.25, 'longest')],
            ['space_echoProcess', 'space_tapeBegin', 'space_tapeEnd',
             'space_echoTapeRun', 'space_plateSetup', 'space_plateRun',
             'space_monoPeak', 'space_echoOut'],
            [('space_tapeBegin', 1, 1), ('space_plateRun', 2, 4)]),
}
# The plate geometry runs every 8th block (space_plateSetup()'s counter):
# its path and its loops (11 lines, 6 taps) at 1/8 a block, both plate types.
GEOM = 1.0 / 8.0


def loops(elf, sym):
    """[(longest, shortest, key)] of the function's distinct frame loops: a
    cycle the tool finds again through an inner head (the same instructions
    on its longest path) counts once."""
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
                out[key] = (p[0][0], p[-1][0], key)
    return sorted(out.values(), key=lambda x: -x[0])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--elf', required=True)
    a = ap.parse_args()
    fl = loops(a.elf, 'sk_feedMix')
    feed = fl[0][0] + B.block_path(a.elf, 'sk_feedMix')[0] / FRAMES
    idle = sum(B.block_path(a.elf, s)[0] for s in
               ('sk_idleBefore', 'sk_idleAfter')) / FRAMES
    fail = False
    for name, (cap, ls, bs, bl) in TYPES.items():
        total = 0.0
        parts = []
        for sym, ranks, w, pick in ls:
            found = loops(a.elf, sym)
            if len(found) < max(ranks):
                sys.exit(f'space_cpu.py: {sym} has {len(found)} loops, '
                         f'expected at least {max(ranks)} (re-check the spec)')
            for k in ranks:
                longest, shortest, _ = found[k - 1]
                n = longest if pick == 'longest' else shortest
                total += n * w
                parts.append(f'{sym.replace("space_", "")} {n}x{w:g}')
        blk = sum(B.block_path(a.elf, s)[0] for s in bs)
        # Loops inside the per-block code, at their iterations per block.
        for sym, k, it in bl:
            blk += loops(a.elf, sym)[k - 1][0] * it
        if name != 'dly':
            gl = loops(a.elf, 'space_plateGeom')
            blk += GEOM * (B.block_path(a.elf, 'space_plateGeom')[0] +
                           11 * gl[0][0] + 6 * gl[1][0])
        total += blk / FRAMES
        # The stopped-transport idle's meter (effects_process()), every
        # block while the transport is stopped: the worst block carries it.
        total += idle
        ok = total <= cap and total + feed <= cap + 20
        fail |= not ok
        print(f'{name}: {" + ".join(parts)} + per block {blk}/{FRAMES} + '
              f'idle {idle:.1f} = {total:.1f} (cap {cap:.1f}); with the feed '
              f'{total + feed:.1f} (cap {cap + 20:.1f}){"" if ok else "  OVER"}')
    sys.exit(1 if fail else 0)


if __name__ == '__main__':
    main()
