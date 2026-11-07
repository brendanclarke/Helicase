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
              - crumpBit_process() has exactly two frame loops, the SVF
                (longer, `dly`/`cmb`/`pit`) and the one-pole (`grn`/`rvb`);
                its per-block count is the longest path through that loop's
                head (elf_blockpath.py --via-loop), so it includes that
                branch's coefficient work only;
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
            It fails when any sub-type is above --cap (170, the user's cap,
            S091_P5_SUBTYPE_EFFICIENCIES.md §1).
Why:        the cap is a gate, not a manual calculation (plan Q15).
Inputs:     --elf PATH, optional --cap N (default 170).
Outputs:    one line per sub-type with its terms and total; exit 1 when a
            total is above the cap, or when crumpBit_process() does not have
            exactly two frame loops (the method no longer applies).
Accessors:  tools/dsp_test/Makefile crumpbit_cpu_check (after `make all`).
Affiliates: elf_looppath.py, elf_blockpath.py, CrumpBitEffect.c.
"""
import argparse
import sys

import elf_blockpath as B
import elf_looppath as E

FRAMES = 32
# Sub-type: (engine symbol, filter loop rank in process, Rate-limit search).
SUBTYPES = [
    ('dly', 'crumpBit_runDelay', 1, 'crumpBit_delayLimit'),
    ('cmb', 'crumpBit_runComb', 1, 'crumpBit_tableLimit'),
    ('pit', 'crumpBit_runPitch', 1, 'crumpBit_tableLimit'),
    ('grn', 'crumpBit_runGrain', 2, 'crumpBit_tableLimit'),
    ('rvb', 'crumpBit_runReverb', 2, 'crumpBit_tableLimit'),
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
    a = ap.parse_args()
    proc = longest_loop(a.elf, 'crumpBit_process')
    if len(proc) != 2:
        sys.exit(f'crumpbit_cpu.py: crumpBit_process has {len(proc)} frame '
                 f'loops, expected 2 (SVF, one-pole)')
    effmax = B.block_path(a.elf, 'crumpBit_effectiveMax')[0]
    worst = 0.0
    for name, sym, rank, limit in SUBTYPES:
        loop = longest_loop(a.elf, sym)[0][0]
        filt = proc[rank - 1][0]
        engine = B.block_path(a.elf, sym)[0]
        manager = effmax + limit_cost(a.elf, limit)
        process = B.block_path(a.elf, 'crumpBit_process', rank)[0]
        total = loop + filt + (engine + process + manager) / FRAMES
        worst = max(worst, total)
        print(f'{name}: loop {loop} + filter {filt} + (engine {engine} + '
              f'process {process} + manager {manager}) / {FRAMES} = '
              f'{total:.1f}')
    if worst > a.cap:
        print(f'crumpbit_cpu.py: FAIL worst {worst:.1f} > cap {a.cap:g}')
        sys.exit(1)
    print(f'crumpbit cpu: worst {worst:.1f} <= cap {a.cap:g}')


if __name__ == '__main__':
    main()
