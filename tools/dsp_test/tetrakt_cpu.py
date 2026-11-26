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
              S104 P1: the counted form per layer is the dearer of plain
              and the early-wrap seam form B; form A runs only with an
              interpolation slot, whose 45-a-sample charge
              (CPU_USE_DSP_AUDIT.md item 75) covers its premium, printed
              and checked separately.
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
            'tkt_layerPostStoreSeamA', 'tkt_layerPostAddSeamA',
            'tkt_layerPostStoreSeamB', 'tkt_layerPostAddSeamB',
            'tkt_spaceOut', 'osc_userBind', 'tkt_bindLayer', 'tkt_seamClaims')

# S104 P1 assessment: elf_blockpath.py counts a `bl` as 1 and lists
# the callee; the caller must add the callee's own path. Every callee of the
# composed functions is named here: None = composed explicitly below; a
# number = add that many times (callee path - 1) per call of the caller.
# A callee missing from this table stops the tool, so a new call is never
# silently uncounted (the S104 P1 seam claims were).
CALLEES = {
    'Tetrakt_calcVoiceAsync': {'sampleMemory_getGeneration': 1,
                               'tkt_layerControl': None,
                               'tkt_seamClaims': None},
    'tkt_layerControl': {'tkt_bindLayer': None},
    'tkt_bindLayer': {'osc_userBind': None, 'sampleMemory_sampleEntry': 1},
    'tkt_seamClaims': {'modNode_interpClaim': 4},
    'Tetrakt_calcVoiceSyncBlock': {'tkt_layerPostStore': None,
                                   'tkt_layerPostAdd': None,
                                   'tkt_layerPostStoreSeamA': None,
                                   'tkt_layerPostAddSeamA': None,
                                   'tkt_layerPostStoreSeamB': None,
                                   'tkt_layerPostAddSeamB': None,
                                   'tkt_spaceOut': None},
}


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


def post_per_sample(elf, sym):
    """S101 P2 B1 (sub-block form): the post loop runs inside a sub-block
    loop (the tone coefficients step every TKT_SUB_BLOCK samples), so a
    sample costs the inner loop plus the outer loop's own part once per
    sub-block: inner + (outer - inner) / SUB_BLOCK. Each loop is measured
    within its own range."""
    r = E.rows(elf, sym)
    idx = {a: i for i, (a, _, _) in enumerate(r)}
    heads = sorted({E.target(ops) for (addr, op, ops) in r
                    if E.BRANCH.match(op) and E.target(ops) is not None
                    and E.target(ops) <= addr and E.target(ops) in idx})
    closer = {}
    for i, (addr, op, ops) in enumerate(r):
        t = E.target(ops) if E.BRANCH.match(op) else None
        if t in heads and t <= addr:
            closer[t] = max(closer.get(t, 0), i)
    found = []
    for h in heads:
        p = E.loop_paths(r, idx, h, closer[h])
        if p:
            found.append(max(x[0] for x in p))
    if len(found) < 2:
        return found[0]
    inner, outer = min(found), max(found)
    return inner + (outer - inner) / SUB_BLOCK


SUB_BLOCK = 8   # TKT_SUB_BLOCK (TetraktVoice.c)


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


def callee_extra(elf, sym):
    """S104 P1 assessment: the uncounted callee cost of one call of
    `sym` (CALLEES); exits on a callee the table does not name."""
    count, calls = B.block_path(elf, sym)
    table = CALLEES.get(sym, {})
    extra = 0
    for c in calls:
        if c not in table:
            sys.exit(f'tetrakt_cpu.py: {sym} calls {c}, not in CALLEES '
                     f'(add its cost or compose it)')
        if table[c] is not None:
            extra += table[c] * (B.block_path(elf, c)[0] - 1)
    return extra


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
    # S104 P1: the counted form per layer is the dearer of plain and the
    # early-wrap seam form B; form A runs only with an interpolation slot,
    # whose 45-a-sample charge (CPU_USE_DSP_AUDIT.md item 75) covers its
    # premium, printed and checked separately.
    store_ps = max(post_per_sample(a.elf, 'tkt_layerPostStore'),
                   post_per_sample(a.elf, 'tkt_layerPostStoreSeamB'))
    add_ps = max(post_per_sample(a.elf, 'tkt_layerPostAdd'),
                 post_per_sample(a.elf, 'tkt_layerPostAddSeamB'))
    per_sample = store_ps + 3 * add_ps + frame
    # S104 P1 assessment: the premium of both instances, the larger.
    premium = max(post_per_sample(a.elf, 'tkt_layerPostStoreSeamA')
                  - post_per_sample(a.elf, 'tkt_layerPostStore'),
                  post_per_sample(a.elf, 'tkt_layerPostAddSeamA')
                  - post_per_sample(a.elf, 'tkt_layerPostAdd'))
    if premium > 45.0:
        sys.exit(f'tetrakt_cpu.py: seam A premium {premium:.1f} > 45 (one slot charge)')
    store_blk = max(B.block_path(a.elf, 'tkt_layerPostStore')[0],
                    B.block_path(a.elf, 'tkt_layerPostStoreSeamB')[0])
    add_blk = max(B.block_path(a.elf, 'tkt_layerPostAdd')[0],
                  B.block_path(a.elf, 'tkt_layerPostAddSeamB')[0])
    blk = (B.block_path(a.elf, 'Tetrakt_calcVoiceAsync')[0] +
           4 * B.block_path(a.elf, 'tkt_layerControl')[0] +
           B.block_path(a.elf, 'Tetrakt_calcVoiceSyncBlock')[0] +
           store_blk + 3 * add_blk +
           B.block_path(a.elf, 'tkt_spaceOut')[0] +
           SPACE_SEGMENTS * seg +
           BIND_LAYERS * (B.block_path(a.elf, 'tkt_bindLayer')[0] +
                          B.block_path(a.elf, 'osc_userBind')[0]))
    if present(a.elf, 'tkt_spaceControl'):
        blk += B.block_path(a.elf, 'tkt_spaceControl')[0]
    # S104 P1 assessment: the callees elf_blockpath counts as 1.
    async_x = callee_extra(a.elf, 'Tetrakt_calcVoiceAsync')
    ctl_x = callee_extra(a.elf, 'tkt_layerControl')
    bind_x = callee_extra(a.elf, 'tkt_bindLayer')
    callee_extra(a.elf, 'Tetrakt_calcVoiceSyncBlock')   # table check only
    blk += async_x + 4 * ctl_x + BIND_LAYERS * bind_x
    total = per_sample + blk / FRAMES
    # S101 P2 A2q: one layer's control a block in the steady state (the other
    # three carried, inlined into the async); a block where every layer
    # STARTs (a hit) runs all four controls and the binds: `blk` above.
    steady = (blk - 3 * (B.block_path(a.elf, 'tkt_layerControl')[0] + ctl_x)
              - BIND_LAYERS * (B.block_path(a.elf, 'tkt_bindLayer')[0] +
                               B.block_path(a.elf, 'osc_userBind')[0] + bind_x))
    # S104 P1 assessment: seam claims (tkt_seamClaims(), at most four
    # modNode_interpClaim() calls) run only in a block with no layer restart,
    # so they are added to the steady block, never to the START block above.
    seam = steady + B.block_path(a.elf, 'tkt_seamClaims')[0] + \
        callee_extra(a.elf, 'tkt_seamClaims')
    seam_total = per_sample + seam / FRAMES
    worst = max(total, seam_total)
    note = ''
    fail = worst > GATE
    if fail:
        note = f'  OVER the {GATE:.0f} gate'
    if a.baseline:
        # S104 P1 assessment: `loops` is a decimal since S101 P2; the
        # former \d+ never matched, so the growth check never ran.
        m = re.match(r'tetrakt: loops [0-9.]+ \+ per block \d+/32 = ([0-9.]+)',
                     open(a.baseline).read())
        if not m:
            sys.exit('tetrakt_cpu.py: the baseline file has no tetrakt line')
        if total > float(m.group(1)) + 0.5:
            note = f'  GREW (baseline {m.group(1)})'
            fail = True
    print(f'tetrakt: loops {per_sample} + per block {blk}/{FRAMES} = {total:.1f}{note}')
    print(f'  spaceOut frame {frame}, segment {seg} x {SPACE_SEGMENTS}; '
          f'binds {BIND_LAYERS} x START')
    print(f'  S101 P2 A2q steady (one layer control a block, no START): '
          f'loops {per_sample} + per block {steady}/{FRAMES} = '
          f'{per_sample + steady / FRAMES:.1f}')
    print(f'  S104 P1 seam block (one control, four slot claims, no START): '
          f'loops {per_sample} + per block {seam}/{FRAMES} = {seam_total:.1f}')
    print(f'  S104 P1 seam A premium per held slot: {premium:.1f} '
          f'(pool charge 45)')
    print(f'  gate: the larger, {worst:.1f} / {GATE:.0f} (callees included)')
    sys.exit(1 if fail else 0)


if __name__ == '__main__':
    main()
