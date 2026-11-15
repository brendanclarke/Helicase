#!/usr/bin/env python3
"""CrumpBit whole-Effect CPU per frame, worst case, for every sub-type.

What:       measures the linked firmware ELF and prints, per sub-type, the
            whole CrumpBit Effect's instructions per frame on its longest
            path (S091 P5 method):
              engine frame loop + output-filter frame loop
              + (engine per block + crumpBit_process() per block on that
                 sub-type's filter branch + the manager's clamps per block)
                / frames per block (32)
            where:
              - the frame loops are elf_looppath.py's longest paths
                (crumpBit_runGrain with --cond-heads --in-range: the longest
                of its two span loops);
              - crumpBit_process() has exactly one frame loop, the SVF
                output filter, shared by every sub-type since S094 P2 AS-13
                (until then a second loop, the S091 P5 one-pole, served
                `grn`/`rvb`); its per-block count is the longest path through
                that loop's head (elf_blockpath.py --via-loop);
              - per block, every frame loop counts 0 (elf_blockpath.py), and
                the callees add their own counts: the manager
                crumpBit_effectiveMax() (per-block path) plus its sub-type's
                Rate-limit search, run once per block for the
                BUFFER_DEPENDENT `rte`: the unrolled crumpBit_tableLimit()
                (straight-line count) for `cmb`/`pit`/`grn`/`rvb`, and
                (S091 P6) `dly`'s crumpBit_delayLimit() (per-block path:
                it is loop-free, and its straight-line count adds up every
                row branch of its eight probes); `dly`'s own delay target
                (crumpBit_delaySamples(), inlined; S091 P6 replaced the
                Sync search crumpBit_divisionFor()) is in its engine path.
            It fails when any sub-type is above --cap (S091 P5: 170, the
            user's cap, S091_P5_SUBTYPE_EFFICIENCIES.md §1; S094 P2: the
            Makefile passes the cap measured with the SVF back in `grn`/`rvb`,
            under the Effect's cycle share, S094_P2_ALKALINE_SAVINGS.md §21).
Why:        the cap is a gate, not a manual calculation (plan Q15).
            S095 P1 (§S18): --feed-cap adds the type-change feed to each
            sub-type's total, worst case: sk_feedMix's longest frame loop
            (its int16 or int8 form, one mono head, a stepped read added
            straight into the wet) + its per-block path / 32; it fails when
            a sub-type's feed total is above --feed-cap. The feed's limit
            against the write heads (crumpBit_feedLimit()) runs once per
            init or sub-type entry, like crumpBit_enter(), and is not a
            steady or feed-block cost.
            The jump-table fix in elf_looppath.py (S095 P1) also made the
            per-block paths through switch tables real: the baseline's `grn`
            reads 181.5 with it (180.8 before).
Inputs:     --elf PATH, optional --cap N (default 170), --feed-cap N.
Outputs:    one line per sub-type with its terms and total; exit 1 when a
            total is above the cap, or when crumpBit_process() does not have
            exactly one frame loop (the method no longer applies).
Accessors:  tools/dsp_test/Makefile crumpbit_cpu_check (after `make all`).
Affiliates: elf_looppath.py, elf_blockpath.py, CrumpBitEffect.c.
"""
import argparse
import sys

import elf_blockpath as B
import elf_looppath as E

FRAMES = 32
# Sub-type: (engine symbol, filter loop rank in process, Rate-limit search).
# S094 P2 AS-13: every sub-type runs the one SVF loop (rank 1).
SUBTYPES = [
    ('dly', 'crumpBit_runDelay', 1, 'crumpBit_delayLimit'),
    ('cmb', 'crumpBit_runComb', 1, 'crumpBit_tableLimit'),
    ('pit', 'crumpBit_runPitch', 1, 'crumpBit_tableLimit'),
    ('grn', 'crumpBit_runGrain', 1, 'crumpBit_tableLimit'),
    ('rvb', 'crumpBit_runReverb', 1, 'crumpBit_tableLimit'),
]


def longest_loop(elf, sym):
    r = E.rows(elf, sym)
    loops = B.frame_loops(r, {a: i for i, (a, _, _) in enumerate(r)})
    if not loops:
        sys.exit(f'crumpbit_cpu.py: no frame loop in {sym}')
    return loops


def straight(elf, sym):
    return len(E.rows(elf, sym))


def limit_cost(elf, sym):
    """A Rate-limit search's per-block cost (see the module docstring)."""
    if sym == 'crumpBit_tableLimit':
        return straight(elf, sym)
    r = E.rows(elf, sym)
    if B.frame_loops(r, {a: i for i, (a, _, _) in enumerate(r)}):
        sys.exit(f'crumpbit_cpu.py: {sym} has a loop; the per-block path '
                 f'no longer bounds it')
    return B.block_path(elf, sym)[0]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--elf', required=True)
    ap.add_argument('--cap', type=float, default=170.0)
    ap.add_argument('--feed-cap', type=float, default=None)
    a = ap.parse_args()
    proc = longest_loop(a.elf, 'crumpBit_process')
    if len(proc) != 1:
        sys.exit(f'crumpbit_cpu.py: crumpBit_process has {len(proc)} frame '
                 f'loops, expected 1 (the SVF, S094 P2 AS-13)')
    effmax = B.block_path(a.elf, 'crumpBit_effectiveMax')[0]
    worst = 0.0
    feed_worst = 0.0
    feed = 0.0
    if a.feed_cap is not None:
        head = max(l for l, _, _ in longest_loop(a.elf, 'sk_feedMix'))
        per_block = B.block_path(a.elf, 'sk_feedMix')[0]
        feed = head + per_block / FRAMES
        print(f'feed: mix {head} + per block {per_block} / {FRAMES} = '
              f'{feed:.1f}')
    for name, sym, rank, limit in SUBTYPES:
        loop = longest_loop(a.elf, sym)[0][0]
        filt = proc[rank - 1][0]
        engine = B.block_path(a.elf, sym)[0]
        manager = effmax + limit_cost(a.elf, limit)
        process = B.block_path(a.elf, 'crumpBit_process', rank)[0]
        total = loop + filt + (engine + process + manager) / FRAMES
        worst = max(worst, total)
        feed_worst = max(feed_worst, total + feed)
        print(f'{name}: loop {loop} + filter {filt} + (engine {engine} + '
              f'process {process} + manager {manager}) / {FRAMES} = '
              f'{total:.1f}' + (f'; with the feed {total + feed:.1f}'
                                if a.feed_cap is not None else ''))
    if worst > a.cap:
        print(f'crumpbit_cpu.py: FAIL worst {worst:.1f} > cap {a.cap:g}')
        sys.exit(1)
    print(f'crumpbit cpu: worst {worst:.1f} <= cap {a.cap:g}')
    if a.feed_cap is not None:
        if feed_worst > a.feed_cap:
            print(f'crumpbit_cpu.py: FAIL feed worst {feed_worst:.1f} > '
                  f'feed cap {a.feed_cap:g}')
            sys.exit(1)
        print(f'crumpbit cpu: feed worst {feed_worst:.1f} <= feed cap '
              f'{a.feed_cap:g}')


if __name__ == '__main__':
    main()
