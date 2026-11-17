#!/usr/bin/env python3
"""Write and check the Colloid's constant tables, ColloidTables.c (S095 P2;
S095 P3 F4: the interval sets removed, detune and home laid out per group).

What:       computes every table declared in ColloidTables.h and writes
            Core/DSP/Instruments/Colloid/ColloidTables.c:
              colEgK[r]         1 - exp(-32 / (T(r) fs)), T(r) = 1 ms x
                                10^(4 r / 127): 1 ms at r 0, 10 s at r 127
                                (log-spaced; the per-block one-pole of both
                                envelopes);
              colDetC[o]        DETC[o]: P2's twelve magnitudes 0.06..1.0,
                                signed and ordered so that each pitch group
                                (o mod 4, S095 P3 F4) holds a low, a near-
                                centre and a high member, and each pwm
                                stack (S095 P5: 0..7 Saw/Rec, 0..5 Sin/Tri)
                                sums to 0; in the eight, two members per
                                group, one either side;
              colHome[o]        HOME[o]: P2's twelve magnitudes 0.30..1.0,
                                ordered so that each group has members on
                                both sides and each pwm stack (0..7, 0..5,
                                S095 P5) sums to 0;
              colMotK[o]        MOTK (0.71..1.37, no simple ratios).
            fs is config.h REAL_FS (44,108.0729 Hz), 32 is OUTPUT_DMA_SIZE.
            Floats are written with 9 significant digits (float32 exact).
Why:        one owner for every constant (S095 P2 draft §2.2, §2.6, §4.1);
            --check keeps the committed file equal to this script.
Inputs:     --check: compare instead of write; ColloidVoice.h COL_PWM_COUNT
            and COL_PWM_WARP_COUNT (read, S095 P5: the pwm stacks the
            asserts cover).
Outputs:    the file; with --check a message, exit 1 on a difference.
Accessors:  python3 tools/colloid/gen_col_tables.py [--check];
            make -C tools/dsp_test colloid_tables.
Affiliates: ColloidTables.h (sizes); ColloidVoice.c (readers);
            S095_P3_COLLOID_FOLLOW_UPS.md F4.
"""
import argparse
import math
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
OUT = os.path.join(ROOT, "Core", "DSP", "Instruments", "Colloid", "ColloidTables.c")

FS = 44108.07291666667      # config.h REAL_FS
BLOCK = 32                  # config.h OUTPUT_DMA_SIZE
OSC_MAX = 12                # ColloidTables.h COL_OSC_MAX
EG_ROWS = 128

# S095 P3 F4: per oscillator o (group o mod 4). Detune: groups
# {-0.40, -0.06, +0.58}, {-0.58, +0.06, +0.40}, {-0.78, -0.22, +1.0},
# {-1.0, +0.22, +0.78}; oscillators 0..5 and 0..7 (pwm, S095 P5) sum to 0.
# Home: groups {-0.88, -0.30, +0.63}, {-0.63, +0.30, +0.88},
# {-0.76, +0.48, +1.0}, {-1.0, -0.48, +0.76}; oscillators 0..5 and 0..7 sum
# to 0. The values are those of S095 P3: eight pwm oscillators needed no
# change, only the asserts in text().
DETC = [-0.06, 0.06, -0.22, 0.22, 0.58, -0.58, 1.0, -1.0, -0.40, 0.40, -0.78, 0.78]
HOME = [-0.30, 0.30, 0.48, -0.48, 0.63, -0.63, -0.76, 0.76, -0.88, 0.88, 1.0, -1.0]
# motion rate ratios: none a simple ratio of another
MOTK = [0.71, 1.37, 0.83, 1.23, 0.91, 1.13, 0.77, 1.31, 0.97, 1.07, 0.87, 1.19]


def voice_define(name):
    """A count read from ColloidVoice.h (S095 P5, user 2026-11-14).

    What:  the value of `#define <name> <N>u` in ColloidVoice.h. Used for
           COL_PWM_COUNT (the pwm Saw/Rec stack, 8) and COL_PWM_WARP_COUNT
           (the pwm Sin/Tri stack, 6): each pwm stack is oscillators
           0..N-1, and text() asserts the balance and the per-group sides
           over both prefixes as well as over all twelve.
    Why:   read from the header, not repeated here, so a later count change
           is checked against these tables (text()'s asserts stop the
           write and --check) instead of passing on a stale copy.
    """
    path = os.path.join(ROOT, "Core", "DSP", "Instruments", "Colloid", "ColloidVoice.h")
    m = re.search(r"^#define\s+" + name + r"\s+(\d+)u", open(path).read(), re.M)
    if not m:
        sys.exit(f"gen_col_tables: {name} not found in ColloidVoice.h")
    return int(m.group(1))


PWM_COUNTS = (voice_define("COL_PWM_COUNT"), voice_define("COL_PWM_WARP_COUNT"))
# S096 P3: the plain/wtb count (10 since S096 P3) and the wts count must
# balance too; asserted with the pwm counts in text()
PWM_COUNTS = PWM_COUNTS + (voice_define("COL_OSC_COUNT"), voice_define("COL_SCAN_COUNT"))


def f32(x):
    """A C float literal with 9 significant digits."""
    s = f"{x:.9g}"
    if "." not in s and "e" not in s:
        s += ".0"
    return s + "f"


def eg_k(r):
    t = 0.001 * 10.0 ** (4.0 * r / 127.0)
    return 1.0 - math.exp(-BLOCK / (t * FS))


def rows(values, per=4):
    out = []
    for i in range(0, len(values), per):
        out.append("    " + ", ".join(values[i:i + per]) + ",")
    return out


def text():
    assert len(MOTK) == OSC_MAX and len(DETC) == OSC_MAX and len(HOME) == OSC_MAX
    # every magnitude of P2 once; each pwm stack (PWM_COUNTS, S095 P5: Saw/Rec
    # and Sin/Tri) balanced; each group two-sided among the twelve and among
    # each pwm stack that gives every group two members (n >= 8: the six of
    # Sin/Tri leave groups 2 and 3 one member each, as in S095 P3)
    assert sorted(abs(x) for x in DETC) == sorted([0.06, 0.06, 0.22, 0.22, 0.40, 0.40,
                                                   0.58, 0.58, 0.78, 0.78, 1.0, 1.0])
    for n in PWM_COUNTS:
        assert n % 2 == 0 and 2 <= n <= OSC_MAX, n
        assert abs(sum(DETC[:n])) < 1e-9 and abs(sum(HOME[:n])) < 1e-9, n
    for g in range(4):
        for t in (DETC, HOME):
            for n in (OSC_MAX,) + tuple(n for n in PWM_COUNTS if n >= 8):
                m = [t[o] for o in range(g, n, 4)]
                assert min(m) < 0.0 < max(m), (g, n, m)
    L = ["/*",
         " * ColloidTables.c - generated by tools/colloid/gen_col_tables.py",
         " * (S095 P2; S095 P3). Do not edit: change the script and run it again.",
         " * Contract and meaning of every table: ColloidTables.h.",
         " */",
         '#include "ColloidTables.h"',
         "",
         "const float colEgK[COL_EG_ROWS] = {"]
    L += rows([f32(eg_k(r)) for r in range(EG_ROWS)])
    L += ["};", "", "const float colDetC[COL_OSC_MAX] = {"]
    L += rows([f32(x) for x in DETC])
    L += ["};", "", "const float colHome[COL_OSC_MAX] = {"]
    L += rows([f32(x) for x in HOME])
    L += ["};", "", "const float colMotK[COL_OSC_MAX] = {"]
    L += rows([f32(k) for k in MOTK])
    L += ["};", ""]
    return "\n".join(L)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()
    want = text()
    if a.check:
        have = open(OUT, newline="").read() if os.path.exists(OUT) else None
        if have != want:
            sys.exit(f"gen_col_tables: {OUT} differs (rerun without --check)")
        print("gen_col_tables: ColloidTables.c matches")
        return
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", newline="\n") as fh:
        fh.write(want)
    print("wrote", OUT)


if __name__ == "__main__":
    main()
