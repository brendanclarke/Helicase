#!/usr/bin/env python3
"""Generate the PWM oscillator tables (S094 P1 S3; S094 P4 law).

What:       writes Core/DSPAudio/oscPwmTables.h and oscPwmTables.c: every
            table is indexed by pos (0..127, the applied width,
            OscInfo.positionApplied). Under the S094 P4 law pos 0 is the
            unmodified wave (width 1/2) and pos 127 the narrowest; the width
            is linear in pos between the form's two ends:
              D difference (Rec; Alkaline pwm Sqr): d = 512 - 4 pos points of
                  1,024 (exact; 512 .. 4, 0.39 % at pos 127, user P4-2);
              D sum (Saw): d = round(512 - pos x 486 / 127) (512 .. 26,
                  2.54 %);
              WT (Tri): w = 1/2 - pos (1/2 - 26/1,024) / 127 (.. 2.54 %);
              WS (Sin): w = 1/2 - pos (1/2 - 1/16) / 127 (.. 6.25 %).
            Tables:
              oscPwmDRec[128], oscPwmDSaw[128]   uint16 d (points)
              oscPwmLevelRecQ14[128], oscPwmLevelSawQ14[128]
                                 int16 round(16384 x 512 / (1024 - d)): the
                                 level that keeps the two-saw difference and
                                 sum at the table's peak (a ramp difference at
                                 width w peaks at 2 max(w, 1 - w));
              oscPwmTriW[128], oscPwmSinW[128]  uint32 W = round(w x 2^32), the
                                 warp's split point;
              oscPwmTriRa/Rb[128], oscPwmSinRa/Rb[128]
                                 uint32 floor((2^32 - 1) 2^25 / W) and the
                                 same for 2^32 - W: umulh(ph, ra) << 6 is
                                 ph x 2^31 / W (the first half-cycle onto
                                 [0, 2^31)), no division per block;
              oscPwmTriLift[128] uint8 round(log2(1/2 / w)): the octave rows
                                 the warped triangle moves up for its
                                 compressed half-cycle.
Why:        S094 P4 (user): pos 0 = 50 %, pos 127 = the narrowest, no wrap.
            The S094 P1 law (width = pos/128, 64 the centre) mirrored about
            64: p and 128 - p played the same spectrum (P4 investigation
            §2.2). Linear in width keeps every d a distinct whole point (both
            D reads share one fraction, so d cannot be fractional); a curve
            would repeat widths at the narrow end. pos 0 equals the S094 P1
            p = 64 constants exactly (T6), so the identity laws carry over.
Checks:     T1 dRec = 512 - 4 pos; dSaw strictly decreasing 512 .. 26, all
            128 distinct; T2 levels 16384 at pos 0, strictly decreasing;
            T3 W strictly decreasing, TriW[127] = 26 x 2^22, SinW[127] = 2^28;
            T4 ra x W <= (2^32 - 1) 2^25 < (ra + 1) x W, the same for rb
            with 2^32 - W (the floor); T5 the half-cycle ends: 2^31 -
            (umulh(W - 1, ra) << 6) <= 64 and 2^32 - ((umulh(2^32 - 1 - W,
            rb) << 6) + 2^31) <= 64 at every pos; T6 pos 0: d 512, levels
            16384, W 2^31, ra = rb = 67,108,863, lift 0 (= S094 P1 p 64);
            T7 lift non-decreasing, 0 .. 4; T8 --check compares both files
            byte for byte; T9 every value fits its type. Exit 1 on any
            failure.
Inputs:     none.
Outputs:    the two files, or a pass/fail line with --check.
Accessors:  tools/dsp_test/Makefile target pwm_tables.
Affiliates: Oscillator.c osc_pwmPlan(); AlkalineVoice.c alk_renderPwm();
            tools/dsp_test/modules/oscillator.c group pwm; modules/alkaline.c
            AL-PW / AL-PWW; S094_P4_IMPLEMENTATION.md §2.
"""
import math
import sys

H_PATH = "Core/DSPAudio/oscPwmTables.h"
C_PATH = "Core/DSPAudio/oscPwmTables.c"

N = 128
D_REC_STEP = 4            # points per pos step: 512 -> 4 (0.39 %)
D_SAW_END = 26            # 2.54 %
TRI_W_END = 26.0 / 1024.0 # 2.54 %
SIN_W_END = 1.0 / 16.0    # 6.25 %
M25 = (2**32 - 1) << 25
P1_RECIP_HALF = 0xFFFFFFFF // 64   # the S094 P1 p = 64 constant: 67,108,863


def d_rec():
    return [512 - D_REC_STEP * k for k in range(N)]


def d_saw():
    return [int(round(512 - k * (512 - D_SAW_END) / 127.0)) for k in range(N)]


def level(ds):
    return [int(round(16384.0 * 512.0 / (1024 - d))) for d in ds]


def split(w_end):
    """W, ra, rb for a warp form whose width falls linearly from 1/2."""
    ws = [0.5 - k * (0.5 - w_end) / 127.0 for k in range(N)]
    W = [int(round(w * 2**32)) for w in ws]
    ra = [M25 // x for x in W]
    rb = [M25 // (2**32 - x) for x in W]
    return ws, W, ra, rb


def lift(ws):
    return [int(round(math.log2(0.5 / w))) for w in ws]


def finite_values(ws, W, ra, rb):
    """Every value fits its C type and every W is a live phase split."""
    return (all(0 < x < 2**32 for x in W) and
            all(0 < x <= 0xFFFFFFFF for x in ra) and
            all(0 < x <= 0xFFFFFFFF for x in rb))


def checks(d_rec_v, d_saw_v, l_rec, l_saw, tri_ws, tri_w, tri_ra, tri_rb,
           tri_lift, sin_ws, sin_w, sin_ra, sin_rb):
    # T1: width tables
    ok = d_rec_v == [512 - D_REC_STEP * k for k in range(N)]
    ok = ok and d_saw_v[0] == 512 and d_saw_v[N - 1] == D_SAW_END
    ok = ok and all(b < a for a, b in zip(d_saw_v, d_saw_v[1:]))
    ok = ok and len(set(d_saw_v)) == N
    # T2: levels 16384 at pos 0, strictly decreasing (growing width index)
    ok = ok and l_rec[0] == 16384 and l_saw[0] == 16384
    ok = ok and all(b < a for a, b in zip(l_rec, l_rec[1:]))
    ok = ok and all(b < a for a, b in zip(l_saw, l_saw[1:]))
    # T3: the split points
    ok = ok and all(b < a for a, b in zip(tri_w, tri_w[1:]))
    ok = ok and all(b < a for a, b in zip(sin_w, sin_w[1:]))
    ok = ok and tri_w[N - 1] == 26 * 2**22
    ok = ok and sin_w[N - 1] == 2**28
    # T4: the reciprocal floors
    ok = ok and all(ra * w <= M25 < (ra + 1) * w for ra, w in zip(tri_ra, tri_w))
    ok = ok and all(rb * (2**32 - w) <= M25 < (rb + 1) * (2**32 - w)
                    for rb, w in zip(tri_rb, tri_w))
    ok = ok and all(ra * w <= M25 < (ra + 1) * w for ra, w in zip(sin_ra, sin_w))
    ok = ok and all(rb * (2**32 - w) <= M25 < (rb + 1) * (2**32 - w)
                    for rb, w in zip(sin_rb, sin_w))
    # T5: the half-cycle ends within 64 phase units
    ok = ok and all(2**31 - (((w - 1) * ra >> 32) << 6) <= 64
                    for w, ra in zip(tri_w, tri_ra))
    ok = ok and all(2**32 - ((((2**32 - 1 - w) * rb >> 32) << 6) + 2**31) <= 64
                    for w, rb in zip(tri_w, tri_rb))
    ok = ok and all(2**31 - (((w - 1) * ra >> 32) << 6) <= 64
                    for w, ra in zip(sin_w, sin_ra))
    ok = ok and all(2**32 - ((((2**32 - 1 - w) * rb >> 32) << 6) + 2**31) <= 64
                    for w, rb in zip(sin_w, sin_rb))
    # T6: pos 0 is the S094 P1 width-1/2 constant set
    ok = ok and d_rec_v[0] == 512 and d_saw_v[0] == 512
    ok = ok and l_rec[0] == 16384 and l_saw[0] == 16384
    ok = ok and tri_w[0] == 2**31 and sin_w[0] == 2**31
    ok = ok and tri_ra[0] == P1_RECIP_HALF and tri_rb[0] == P1_RECIP_HALF
    ok = ok and sin_ra[0] == P1_RECIP_HALF and sin_rb[0] == P1_RECIP_HALF
    ok = ok and tri_lift[0] == 0
    # T7: the lift
    ok = ok and all(0 <= x <= 4 for x in tri_lift)
    ok = ok and all(b >= a for a, b in zip(tri_lift, tri_lift[1:]))
    # T9: every value fits its type
    ok = ok and all(0 <= x <= 0xFFFF for x in d_rec_v + d_saw_v)
    ok = ok and all(-0x8000 <= x <= 0x7FFF for x in l_rec + l_saw)
    ok = ok and all(0 <= x <= 0xFF for x in tri_lift)
    ok = ok and finite_values(tri_ws, tri_w, tri_ra, tri_rb)
    ok = ok and finite_values(sin_ws, sin_w, sin_ra, sin_rb)
    return ok


def render_h():
    return "\n".join([
        "/* Generated by tools/plots/gen_pwm_tables.py (S094 P1 S3; S094 P4 law). Do not edit. */",
        "#ifndef OSC_PWM_TABLES_H_",
        "#define OSC_PWM_TABLES_H_",
        "",
        "#include <stdint.h>",
        "",
        "/*",
        " * PWM oscillator constants (S094 P1 S3; S094 P4 law), flash.",
        " *",
        " * What:       every table is indexed by pos = positionApplied (0..127):",
        " *             pos 0 is the unmodified wave (width 1/2), pos 127 the",
        " *             narrowest; the width falls linearly in pos.",
        " *               oscPwmDRec / oscPwmDSaw: the D form's second-read offset in",
        " *                 points of 1,024 (Rec 512 .. 4, Saw 512 .. 26);",
        " *               oscPwmLevelRecQ14 / oscPwmLevelSawQ14: 16384 x 512 /",
        " *                 (1024 - d), the D level that keeps the table's peak;",
        " *               oscPwmTriW / oscPwmSinW: the warp's split point W (Tri down",
        " *                 to 2.54 %, Sin down to 6.25 % of the cycle);",
        " *               oscPwmTriRa/Rb, oscPwmSinRa/Rb: floor((2^32 - 1) 2^25 / W)",
        " *                 and the same for 2^32 - W (osc_pwmWarp()'s half-cycle",
        " *                 scales);",
        " *               oscPwmTriLift: octave rows the warped triangle moves up.",
        " * Why:        S094 P4 (user): pos 0 = 50 %, pos 127 = the narrowest, no",
        " *             wrap; per-block lookups, no division, loops unchanged. pos 0",
        " *             equals the S094 P1 width-1/2 constants exactly.",
        " * Accessors:  Oscillator.c osc_pwmPlan(); AlkalineVoice.c alk_renderPwm().",
        " * Affiliates: oscPwmWarp.h (the warp law these scales serve);",
        " *             tools/plots/gen_pwm_tables.py (the law and checks T1-T9).",
        " */",
        "#define OSC_PWM_POS_COUNT 128u",
        "extern const uint16_t oscPwmDRec[OSC_PWM_POS_COUNT];",
        "extern const uint16_t oscPwmDSaw[OSC_PWM_POS_COUNT];",
        "extern const int16_t  oscPwmLevelRecQ14[OSC_PWM_POS_COUNT];",
        "extern const int16_t  oscPwmLevelSawQ14[OSC_PWM_POS_COUNT];",
        "extern const uint32_t oscPwmTriW[OSC_PWM_POS_COUNT];",
        "extern const uint32_t oscPwmTriRa[OSC_PWM_POS_COUNT];",
        "extern const uint32_t oscPwmTriRb[OSC_PWM_POS_COUNT];",
        "extern const uint8_t  oscPwmTriLift[OSC_PWM_POS_COUNT];",
        "extern const uint32_t oscPwmSinW[OSC_PWM_POS_COUNT];",
        "extern const uint32_t oscPwmSinRa[OSC_PWM_POS_COUNT];",
        "extern const uint32_t oscPwmSinRb[OSC_PWM_POS_COUNT];",
        "",
        "#endif /* OSC_PWM_TABLES_H_ */",
        "",
    ])


def rows(vals, fmt, per):
    return ["    " + ", ".join(fmt % v for v in vals[i:i + per]) + ","
            for i in range(0, len(vals), per)]


def render_c(d_rec_v, d_saw_v, l_rec, l_saw, tri_w, tri_ra, tri_rb, tri_lift,
             sin_w, sin_ra, sin_rb):
    parts = [
        "/* Generated by tools/plots/gen_pwm_tables.py (S094 P1 S3; S094 P4 law). Do not edit. */",
        '#include "oscPwmTables.h"', "",
        "/* D difference: d = 512 - 4 pos, points of 1,024 (512 .. 4) */",
        "const uint16_t oscPwmDRec[OSC_PWM_POS_COUNT] = {"]
    parts += rows(d_rec_v, "%4du", 8) + ["};", "",
        "/* D sum: d = round(512 - pos x 486 / 127) points (512 .. 26) */",
        "const uint16_t oscPwmDSaw[OSC_PWM_POS_COUNT] = {"]
    parts += rows(d_saw_v, "%4du", 8) + ["};", "",
        "/* round(16384 x 512 / (1024 - d)) for oscPwmDRec */",
        "const int16_t oscPwmLevelRecQ14[OSC_PWM_POS_COUNT] = {"]
    parts += rows(l_rec, "%6d", 8) + ["};", "",
        "/* round(16384 x 512 / (1024 - d)) for oscPwmDSaw */",
        "const int16_t oscPwmLevelSawQ14[OSC_PWM_POS_COUNT] = {"]
    parts += rows(l_saw, "%6d", 8) + ["};", "",
        "/* the warp's split point: W = round(w x 2^32), Tri 1/2 .. 26/1,024 */",
        "const uint32_t oscPwmTriW[OSC_PWM_POS_COUNT] = {"]
    parts += rows(tri_w, "0x%08Xu", 4) + ["};", "",
        "/* floor((2^32 - 1) 2^25 / W) */",
        "const uint32_t oscPwmTriRa[OSC_PWM_POS_COUNT] = {"]
    parts += rows(tri_ra, "0x%08Xu", 4) + ["};", "",
        "/* floor((2^32 - 1) 2^25 / (2^32 - W)) */",
        "const uint32_t oscPwmTriRb[OSC_PWM_POS_COUNT] = {"]
    parts += rows(tri_rb, "0x%08Xu", 4) + ["};", "",
        "/* round(log2(1/2 / w)), the octave rows the warped triangle lifts */",
        "const uint8_t oscPwmTriLift[OSC_PWM_POS_COUNT] = {"]
    parts += rows(tri_lift, "%du", 16) + ["};", "",
        "/* the warp's split point: W = round(w x 2^32), Sin 1/2 .. 1/16 */",
        "const uint32_t oscPwmSinW[OSC_PWM_POS_COUNT] = {"]
    parts += rows(sin_w, "0x%08Xu", 4) + ["};", "",
        "/* floor((2^32 - 1) 2^25 / W) */",
        "const uint32_t oscPwmSinRa[OSC_PWM_POS_COUNT] = {"]
    parts += rows(sin_ra, "0x%08Xu", 4) + ["};", "",
        "/* floor((2^32 - 1) 2^25 / (2^32 - W)) */",
        "const uint32_t oscPwmSinRb[OSC_PWM_POS_COUNT] = {"]
    parts += rows(sin_rb, "0x%08Xu", 4) + ["};", ""]
    return "\n".join(parts)


def main():
    d_rec_v, d_saw_v = d_rec(), d_saw()
    l_rec, l_saw = level(d_rec_v), level(d_saw_v)
    tri_ws, tri_w, tri_ra, tri_rb = split(TRI_W_END)
    tri_lift = lift(tri_ws)
    sin_ws, sin_w, sin_ra, sin_rb = split(SIN_W_END)
    ok = checks(d_rec_v, d_saw_v, l_rec, l_saw, tri_ws, tri_w, tri_ra, tri_rb,
                tri_lift, sin_ws, sin_w, sin_ra, sin_rb)
    h = render_h()
    c = render_c(d_rec_v, d_saw_v, l_rec, l_saw, tri_w, tri_ra, tri_rb, tri_lift,
                 sin_w, sin_ra, sin_rb)
    if "--check" in sys.argv:
        ok = ok and open(H_PATH).read() == h and open(C_PATH).read() == c
        print("pwm tables", "OK" if ok else "FAILED")
        return 0 if ok else 1
    if not ok:
        print("pwm tables: T1-T7/T9 failed")
        return 1
    open(H_PATH, "w").write(h)
    open(C_PATH, "w").write(c)
    print("wrote", H_PATH, C_PATH)
    return 0


if __name__ == "__main__":
    sys.exit(main())
