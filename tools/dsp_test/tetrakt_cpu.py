#!/usr/bin/env python3
"""
tetrakt_cpu.py: the Tetrakt's instructions per sample (S096 P1).

What:       from the linked ELF (the colloid_cpu.py / space_cpu.py method):
              per-sample loops: 1 x tkt_layerPostStore's loop + 3 x
              tkt_layerPostAdd's loop (each reads its layer's sample itself
              since S096 P1 §19; no osc_smpPlainBlock read)
              + tkt_spaceOut's loop, each at its longest path;
              per-block paths / 32: Tetrakt_calcVoiceAsync (straight path)
              + 4 x tkt_layerControl + tkt_spaceControl (if out of line)
              + the catalogue generation test + Tetrakt_calcVoiceSyncBlock's path + each loop's
              entry/exit path; alk_exp2f is inline (counted in the control).
              The worst block (S096 P1 §17): with lon = all, one trigger
              STARTs all four layers in the same block, so 4 x
              (tkt_bindLayer + osc_userBind) are counted per block (the
              constant-CPU rule counts the worst block; §16 F1).
              tkt_spaceOut (S096 P1 §17) runs its 32 frames in at most
              SPACE_SEGMENTS segments: its frame loop (the tightest loop
              inside the segment cycle) counts per sample, and the segment
              cycle less one frame counts SPACE_SEGMENTS times per block.
Why:        the schedule's S10 gate (SD12: <= 390) and the host baseline the
            device run is compared with (S12).
Inputs:     --elf PATH; --baseline FILE (optional: fail on growth > 0.5).
Outputs:    one line "tetrakt: loops L + per block B/32 = T"; exit 1 on a
            missing symbol, a growth, or T > 390.
Accessors:  make -C tools/dsp_test tetrakt_cpu_check (after the root build).
"""
import argparse
import re
import sys

import elf_blockpath as B
import elf_looppath as E

FRAMES = 32
GATE = 390.0
SPACE_SEGMENTS = 4   # TetraktVoice.c tkt_spaceOut(): one per wrap (w, rl, rr) + 1
BIND_LAYERS = 4      # the worst block: every layer STARTs
REQUIRED = ('Tetrakt_calcVoiceAsync', 'Tetrakt_calcVoiceSyncBlock',
            'tkt_layerControl', 'tkt_layerPostStore', 'tkt_layerPostAdd',
            'tkt_spaceOut', 'osc_userBind', 'tkt_bindLayer')


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


def segmented(elf, sym):
    """(frame, segment) of a function whose frame loop runs inside a
    segment loop: frame = the tightest backward branch's own cycle within
    the longest (segment) cycle; segment = that cycle's longest path less
    one frame (the per-segment limits, wraps and loop entry)."""
    r = E.rows(elf, sym)
    idx = {a: i for i, (a, _, _) in enumerate(r)}
    outer = loops(elf, sym)[0]
    full = max((p for h in sorted({E.target(ops) for (addr, op, ops) in r
                                   if E.BRANCH.match(op) and E.target(ops) is not None
                                   and E.target(ops) <= addr and E.target(ops) in idx})
                for p in E.loop_paths(r, idx, h)), key=lambda x: x[0])
    cyc = set(full[2])
    best = None
    for (addr, op, ops) in r:
        t = E.target(ops) if E.BRANCH.match(op) else None
        if t is None or t > addr or t not in idx or idx[addr] not in cyc:
            continue
        p = E.loop_paths(r, idx, t, last=idx[addr])
        if not p or not set(p[0][2]) <= cyc:
            continue
        span = addr - t
        if best is None or span < best[0]:
            best = (span, max(x[0] for x in p))
    if best is None or best[1] >= outer[0]:
        sys.exit(f'tetrakt_cpu.py: no frame loop inside the segment loop of {sym}')
    return best[1], outer[0] - best[1]


def present(elf, sym):
    try:
        E.rows(elf, sym)
        return True
    except SystemExit:
        return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--elf', required=True)
    ap.add_argument('--baseline')
    a = ap.parse_args()
    for s in REQUIRED:
        if not present(a.elf, s):
            sys.exit(f'tetrakt_cpu.py: {s} not found (inlined or renamed)')
    store = loops(a.elf, 'tkt_layerPostStore')
    add = loops(a.elf, 'tkt_layerPostAdd')
    space = loops(a.elf, 'tkt_spaceOut')
    for name, got in (('tkt_layerPostStore', store),
                      ('tkt_layerPostAdd', add), ('tkt_spaceOut', space)):
        if not got:
            sys.exit(f'tetrakt_cpu.py: no loop in {name}')

    frame, seg = segmented(a.elf, 'tkt_spaceOut')
    per_sample = (store[0][0] + 3 * add[0][0] + frame)
    blk = (B.block_path(a.elf, 'Tetrakt_calcVoiceAsync')[0] +
           4 * B.block_path(a.elf, 'tkt_layerControl')[0] +
           B.block_path(a.elf, 'Tetrakt_calcVoiceSyncBlock')[0] +
           B.block_path(a.elf, 'tkt_layerPostStore')[0] +
           3 * B.block_path(a.elf, 'tkt_layerPostAdd')[0] +
           B.block_path(a.elf, 'tkt_spaceOut')[0] +
           SPACE_SEGMENTS * seg +
           BIND_LAYERS * (B.block_path(a.elf, 'tkt_bindLayer')[0] +
                          B.block_path(a.elf, 'osc_userBind')[0]))
    if present(a.elf, 'tkt_spaceControl'):
        blk += B.block_path(a.elf, 'tkt_spaceControl')[0]
    total = per_sample + blk / FRAMES
    note = ''
    fail = total > GATE
    if fail:
        note = f'  OVER the {GATE:.0f} gate'
    if a.baseline:
        m = re.match(r'tetrakt: loops \d+ \+ per block \d+/32 = ([0-9.]+)',
                     open(a.baseline).read())
        if m and total > float(m.group(1)) + 0.5:
            note = f'  GREW (baseline {m.group(1)})'
            fail = True
    print(f'tetrakt: loops {per_sample} + per block {blk}/{FRAMES} = {total:.1f}{note}')
    print(f'  spaceOut frame {frame}, segment {seg} x {SPACE_SEGMENTS}; '
          f'binds {BIND_LAYERS} x START')
    sys.exit(1 if fail else 0)


if __name__ == '__main__':
    main()
