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
                the callees add their own counts: `dly` the Sync search
                crumpBit_divisionFor() (straight-line count); the manager
                crumpBit_effectiveMax() (per-block path) plus the unrolled
                crumpBit_tableLimit() (straight-line count), run once per
                block for the BUFFER_DEPENDENT `rte`.
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
# Sub-type: (engine symbol, filter loop rank in process, Sync search).
SUBTYPES = [
    ('dly', 'crumpBit_runDelay', 1, True),
    ('cmb', 'crumpBit_runComb', 1, False),
    ('pit', 'crumpBit_runPitch', 1, False),
    ('grn', 'crumpBit_runGrain', 2, False),
    ('rvb', 'crumpBit_runReverb', 2, False),
]


def longest_loop(elf, sym):
    r = E.rows(elf, sym)
    loops = B.frame_loops(r, {a: i for i, (a, _, _) in enumerate(r)})
    if not loops:
        sys.exit(f'crumpbit_cpu.py: no frame loop in {sym}')
    return loops


def straight(elf, sym):
    return len(E.rows(elf, sym))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--elf', required=True)
    ap.add_argument('--cap', type=float, default=170.0)
    a = ap.parse_args()
    proc = longest_loop(a.elf, 'crumpBit_process')
    if len(proc) != 2:
        sys.exit(f'crumpbit_cpu.py: crumpBit_process has {len(proc)} frame '
                 f'loops, expected 2 (SVF, one-pole)')
    manager = (B.block_path(a.elf, 'crumpBit_effectiveMax')[0] +
               straight(a.elf, 'crumpBit_tableLimit'))
    sync = straight(a.elf, 'crumpBit_divisionFor')
    worst = 0.0
    for name, sym, rank, has_sync in SUBTYPES:
        loop = longest_loop(a.elf, sym)[0][0]
        filt = proc[rank - 1][0]
        engine = B.block_path(a.elf, sym)[0] + (sync if has_sync else 0)
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
