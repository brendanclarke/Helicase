#!/usr/bin/env python3
"""Compare the per-sample hot paths of two elf_loopdump.py dumps (S086 P3).

What:       reads two loop dumps of the same large function (BASE, NOW), for
            example mixer_calcNextSampleBlock(), and pairs every BASE loop with
            its most similar NOW loop by opcode sequence (registers, branch
            offsets, literal addresses and the .w/.n and flag-setting forms
            folded away). A hot loop is a fragment that ends in a
            conditional branch back to its own start (elf_loopdump.py writes
            that target as "+0"): the per-sample do-while body, whose length
            is the per-sample cost. It reports every hot loop whose length
            changed and fails when one got longer. Fragments that close with
            an unconditional branch (jump tables, per-block setup paths, cold
            saturation stubs) are listed for information only.
Why:        S086 P3 assessment F1: adding code around the voice renders made
            LTO re-allocate registers in mixer_calcNextSampleBlock(), where the
            renders are inlined, and two inlined per-sample loops gained loads.
            elf_symdump.py/elf_loopdump.py --compare cannot see that: the
            function's layout moves, so nearly every loop "changes" textually.
            The hot-path length is the cost that matters and survives layout.
Inputs:     --compare BASE NOW (elf_loopdump.py outputs).
Outputs:    a report; exit 1 when a hot path grew, else 0.
Accessors:  tools/dsp_test/Makefile elf86_mixer_check.
Affiliates: elf_loopdump.py (the dumps); 086_SESSION_HANDOFF_LOG.md §8-§9.
"""
import argparse
import difflib
import re
import sys

FOLD = {"adds": "add", "subs": "sub", "movs": "mov", "lsrs": "lsr",
        "lsls": "lsl", "asrs": "asr", "ands": "and", "orrs": "orr",
        "eors": "eor", "adcs": "adc", "sbcs": "sbc"}
COND = ("bne", "beq", "blt", "bge", "bgt", "ble", "bcc", "bcs", "bls",
        "bhi", "bmi", "bpl")


def opcode(ins):
    op = re.sub(r"\.(w|n)$", "", ins.strip().split()[0])
    return FOLD.get(op, op)


def load(path):
    loops = []
    for line in open(path):
        name, _, body = line.rstrip("\n").partition("\t")
        raw = [i for i in body.split(" ; ") if i.strip()]
        loops.append((name, raw, [opcode(i) for i in raw]))
    return loops


def hot_len(raw):
    """Length of a per-sample do-while loop (closes with a conditional
    branch to its own start, "+0"), or None for any other fragment."""
    if raw and opcode(raw[-1]) in COND and raw[-1].split()[-1] == "+0":
        return len(raw)
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--compare", nargs=2, required=True)
    args = ap.parse_args()
    base, now = load(args.compare[0]), load(args.compare[1])
    grew = shrank = same = other = 0
    hot_now = [k for k in range(len(now)) if hot_len(now[k][1]) is not None]
    for i, (name, raw, ops) in enumerate(base):
        hb = hot_len(raw)
        if hb is None:
            other += 1
            continue
        j = max(hot_now,
                key=lambda k: difflib.SequenceMatcher(None, ops, now[k][2]).ratio())
        hn = hot_len(now[j][1])
        if hn == hb:
            same += 1
            continue
        tag = "GREW" if hn > hb else "shrank"
        grew += hn > hb
        shrank += hn < hb
        print("%s %s base#%d hot %d -> now#%d hot %d: %s" %
              (tag, name, i, hb, j, hn, " ; ".join(now[j][1][:hn])[:160]))
    print("elf_hotloops: %d hot loops equal, %d shrank, %d grew "
          "(%d other fragments not compared)" % (same, shrank, grew, other))
    return 1 if grew else 0


if __name__ == "__main__":
    sys.exit(main())
