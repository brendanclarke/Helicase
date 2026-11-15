#!/usr/bin/env python3
"""Generate the Alkaline post-filter and drive tables (S094 P1 S4).

What:       writes Core/DSP/Instruments/Alkaline/AlkalinePostTables.h and .c,
            five float tables of ALK_POST_POINTS (128) entries indexed by a
            descriptor byte 0..127:
              alkOvdGain[k]     the VCA overdrive's OTA input gain,
                                2^(5k/127): x1 .. x32 (S094 P2: twice the
                                S4 x16 at 127, user); [0] = 1.0f exactly;
              alkOvdMakeup[k]   its output make-up, 2^(-2.5k/127) (a starting
                                curve, 1/sqrt of the gain, P9-D4: set by ear
                                on the device); [0] = 1.0f exactly;
              alkToneC[k]       the tone one-pole's coefficient
                                c = exp(-2 pi fc / fs), fc = 800 Hz x
                                (20 kHz / 800 Hz)^(k/127) for k < 127, and
                                [127] = 0.0f exactly (open: bit-identical);
              alkDriveGain[k]   A3: the input drive 16^(k/127), evaluated as
                                the firmware does: t = alk_exp2f(k/127.0f),
                                (t x t) x (t x t) in float32;
              alkDriveMakeup[k] A3: alk_exp2f(ALK_MAKEUP_OCT x (1 - k/127.0f)).
            alk_exp2f is emulated in float32 with the firmware's Horner order
            and one fused multiply-add per step (GCC -Ofast contracts each
            a + f x b; checked against the S0 image's disassembly of
            Alkaline_calcVoiceAsync in S4.13 step 1).
Why:        post-FX draft P9-D1 (A3) and §3: per-block table reads replace two
            exp2 evaluations and give the overdrive and tone their curves; the
            neutral entries (overdrive 0, tone 127, drive 0) are exact, so
            Overdrive 0 / Tone 127 is bit-identical to the S0 voice (P9-D7).
Checks:     Q1 alkOvdGain[0] == alkOvdMakeup[0] == 1.0 and alkToneC[127] == 0.0;
            Q2 alkOvdGain increasing, alkOvdMakeup decreasing, alkToneC
            decreasing in k, every entry finite and positive except
            alkToneC[127]; Q3 alkDriveGain[0] and alkDriveMakeup[0] equal the
            exact f = 0 evaluation; Q4 --check compares both files byte for
            byte. Exit 1 on any failure.
Inputs:     the constants below and AlkalineControl.h's alk_exp2f
            coefficients (copied here; Q3 and the bench law AL-A3 catch a
            mismatch).
Outputs:    the two files, or a pass/fail line with --check.
Accessors:  tools/dsp_test/Makefile target alk_post_tables.
Affiliates: AlkalineVoice.c Alkaline_calcVoiceAsync(); DiodeLadder.h
            DiodeLadder_setPost(); tools/dsp_test/modules/alkaline.c laws
            AL-A3, AL-OV, AL-TN, AL-OD0.
"""
import math
import sys

import numpy as np

F = np.float32
H_PATH = "Core/DSP/Instruments/Alkaline/AlkalinePostTables.h"
C_PATH = "Core/DSP/Instruments/Alkaline/AlkalinePostTables.c"
N = 128
FS = 44108.07291666667
OVD_OCT = 5.0                    # x32 at 127 (S094 P2, user: S4's x16 doubled)
OVD_MAKEUP_OCT = -2.5            # 1 / sqrt(gain): starting curve (P9-D4)
TONE_FC_MIN, TONE_FC_MAX = 800.0, 20000.0
ALK_MAKEUP_OCT = F(1.0)          # AlkalineVoice.h
# alk_exp2f's coefficients, c0..c5 (AlkalineControl.h)
EXP2_C = [F(0.999999762), F(0.693156779), F(0.240131676),
          F(0.055876568), F(0.00894057751), F(0.00189437857)]


def fma32(a, b, c):
    """float32 fused multiply-add: a x b + c rounded once (via double)."""
    return F(math.fma(float(a), float(b), float(c)))


def alk_exp2f(x):
    """alk_exp2f() in float32, the firmware's Horner order with fused steps."""
    x = F(min(max(float(x), -126.0), 127.0))
    xi = F(math.floor(float(x)))
    f = F(x - xi)
    r = EXP2_C[5]
    for c in reversed(EXP2_C[:5]):     # c4, c3, c2, c1, c0
        r = fma32(f, r, c)
    return F(F(2.0 ** int(xi)) * r)


def tables():
    k = np.arange(N)
    ovd_gain = [F(2.0 ** (OVD_OCT * i / 127.0)) for i in k]
    ovd_makeup = [F(2.0 ** (OVD_MAKEUP_OCT * i / 127.0)) for i in k]
    tone = []
    for i in k:
        if i == N - 1:
            tone.append(F(0.0))
        else:
            fc = TONE_FC_MIN * (TONE_FC_MAX / TONE_FC_MIN) ** (i / 127.0)
            tone.append(F(math.exp(-2.0 * math.pi * fc / FS)))
    drv_gain, drv_makeup = [], []
    for i in k:
        drv = F(F(i) / F(127.0))           # the descriptor's byte / 127.0f
        t = alk_exp2f(drv)
        t2 = F(t * t)
        drv_gain.append(F(t2 * t2))
        drv_makeup.append(alk_exp2f(F(ALK_MAKEUP_OCT * F(F(1.0) - drv))))
    return ovd_gain, ovd_makeup, tone, drv_gain, drv_makeup


def checks(og, om, tc, dg, dm):
    ok = og[0] == 1.0 and om[0] == 1.0 and tc[N - 1] == 0.0
    ok = ok and all(b > a for a, b in zip(og, og[1:]))
    ok = ok and all(b < a for a, b in zip(om, om[1:]))
    ok = ok and all(b < a for a, b in zip(tc, tc[1:]))
    ok = ok and all(np.isfinite(v) and v > 0 for v in og + om + dg + dm + tc[:-1])
    t0 = F(EXP2_C[0])                     # alk_exp2f(0) = 1 x c0
    ok = ok and dg[0] == F(F(t0 * t0) * F(t0 * t0))
    ok = ok and dm[0] == F(F(2.0) * EXP2_C[0])   # alk_exp2f(1) = 2 x c0
    return ok


def render_h():
    return "\n".join([
        "/* Generated by tools/alkaline/gen_alk_post_tables.py (S094 P1 S4). Do not edit. */",
        "#ifndef ALKALINE_POST_TABLES_H_",
        "#define ALKALINE_POST_TABLES_H_",
        "",
        "/*",
        " * Alkaline post-filter and drive tables (S094 P1 S4), flash, indexed by a",
        " * descriptor byte 0..127 (the knob's 0..1 value x 127, rounded).",
        " *",
        " * What:       alkOvdGain / alkOvdMakeup: the VCA overdrive's OTA input gain",
        " *             (x1..x32) and output make-up; alkToneC: the tone one-pole's c",
        " *             (0 = open at 127); alkDriveGain / alkDriveMakeup: A3, the input",
        " *             drive x1..x16 and its make-up (alk_exp2f() evaluated offline).",
        " * Accessors:  AlkalineVoice.c Alkaline_calcVoiceAsync().",
        " */",
        "#define ALK_POST_POINTS 128u",
        "extern const float alkOvdGain[ALK_POST_POINTS];",
        "extern const float alkOvdMakeup[ALK_POST_POINTS];",
        "extern const float alkToneC[ALK_POST_POINTS];",
        "extern const float alkDriveGain[ALK_POST_POINTS];",
        "extern const float alkDriveMakeup[ALK_POST_POINTS];",
        "",
        "#endif /* ALKALINE_POST_TABLES_H_ */",
        ""])


def arr(name, vals, note):
    lines = [f"/* {note} */", f"const float {name}[ALK_POST_POINTS] = {{"]
    for i in range(0, N, 4):
        lines.append("    " + ", ".join("%.9ef" % float(v) for v in vals[i:i + 4]) +
                     ",  /* %3d..%3d */" % (i, i + 3))
    return lines + ["};", ""]


def render_c(og, om, tc, dg, dm):
    lines = ["/* Generated by tools/alkaline/gen_alk_post_tables.py (S094 P1 S4). Do not edit. */",
             '#include "AlkalinePostTables.h"', ""]
    lines += arr("alkOvdGain", og, "2^(4k/127): the OTA input gain; [0] = 1 exactly")
    lines += arr("alkOvdMakeup", om, "2^(-2k/127): the overdrive make-up (starting curve); [0] = 1")
    lines += arr("alkToneC", tc, "exp(-2 pi fc / fs), fc 800 Hz .. 20 kHz; [127] = 0 (open)")
    lines += arr("alkDriveGain", dg, "A3: (alk_exp2f(k/127)^2)^2 in float32; [0] exact")
    lines += arr("alkDriveMakeup", dm, "A3: alk_exp2f(1 - k/127) in float32; [0] exact")
    return "\n".join(lines)


def main():
    og, om, tc, dg, dm = tables()
    ok = checks(og, om, tc, dg, dm)
    h, c = render_h(), render_c(og, om, tc, dg, dm)
    if "--check" in sys.argv:
        ok = ok and open(H_PATH).read() == h and open(C_PATH).read() == c
        print("alkaline post tables", "OK" if ok else "FAILED")
        return 0 if ok else 1
    if not ok:
        print("alkaline post tables: Q1-Q3 failed")
        return 1
    open(H_PATH, "w").write(h)
    open(C_PATH, "w").write(c)
    print("wrote", H_PATH, C_PATH)
    return 0


if __name__ == "__main__":
    sys.exit(main())
