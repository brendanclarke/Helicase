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
              WT (Tri): w = 1/2 - pos (1/2 - 26/1,024) / 127 (.. 2.54 %).
            S099 P1 §15.10 (user, 2026-11-19): pwm Sin (form WS, w down to
            6.25 %) is dropped from every engine, and its tables with it.
            Tables:
              oscPwmDRec[128], oscPwmDSaw[128]   uint16 d (points)
              oscPwmLevelRecQ14[128], oscPwmLevelSawQ14[128]
                                 int16 round(16384 x 512 / (1024 - d)): the
                                 level that keeps the two-saw difference and
                                 sum at the table's peak (a ramp difference at
                                 width w peaks at 2 max(w, 1 - w));
              oscPwmTriW[128]    uint32 W = round(w x 2^32), the warp's
                                 split point;
              oscPwmTriRa/Rb[128]
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
            T3 W strictly decreasing, TriW[127] = 26 x 2^22;
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
S099 P1:    the chopped wave R-S (DRAFT_S-R_PWM_WAVE.md §4, §6.4) goes in
            its own pair, Core/DSPAudio/oscChopTables.h/.c, so the tables
            above (and T1-T9) stay byte-identical. S099 P2 (user,
            2026-11-19): REx (the same chop on a rising exponential) is
            dropped, too close to R-S by ear, and R-S's widths are Rec's
            (oscPwmDRec, 512 .. 4 points, 0.39 % at pos 127; it was
            oscPwmDSaw, 2.54 %, D-5), so it narrows further:
              oscChopCurveRs[1024]
                                 int16 round(32767 c(x)), x = i/1024 - 1/8:
                                 the 1x ramp wrapped at 7/8 of the cycle
                                 (index (ph + 2^29) >> 22), so the wrap sits
                                 inside the last chopped section for every
                                 width <= 1/2. c(x) = x.
              oscChopGainQ14[11] int16 round(16384 x 65536 / span_r): the
                                 gate's normalisation per sawTable row r
                                 (span_r = 2 A_r 32767, A_r the row's naive
                                 saw amplitude from its middle slope).
              oscChopSpanF[11], oscChopInvSpanF[11]
                                 float span_r and 1 / span_r (the
                                 Alkaline's float form; no VDIV).
              oscChopDcRsQ15[128]
                                 int16 round(32767 dc(w)), w = oscPwmDRec[k]
                                 / 1024: the wave's mean, the integral of c
                                 over the two passed segments [0, w/2) and
                                 [1/2, 1/2 + w/2).
              oscChopLevelRs[128]
                                 float 1 / P(w), P = c((1 + w)/2) - dc(w):
                                 the peak-keeping level (D-5, Q-14).
              oscChopRelRsQ15[128]
                                 int16 round(32767 level / Lref), Lref = the
                                 largest level of the wave (written as
                                 OSC_CHOP_LREF_RS in the header): the
                                 Colloid's per-oscillator level.
            Checks T10-T15 (below); T8 covers the new pair too.
"""
import math
import re
import sys

H_PATH = "Core/DSPAudio/oscPwmTables.h"
C_PATH = "Core/DSPAudio/oscPwmTables.c"

# --- S099 P1: the chopped waves (oscChopTables.h/.c) ---
CHOP_H_PATH = "Core/DSPAudio/oscChopTables.h"
CHOP_C_PATH = "Core/DSPAudio/oscChopTables.c"
# S101: the full [11][1024] sawTable is archived (the firmware stores levels
# 1..10 in stockTableRows[]); levels 1..10 are byte-identical (STOCK-ROWS).
WAVETABLE_C = "tools/plots/archive/stock_tables_s100.c"
CHOP_POINTS = 1024
CHOP_WRAP_INDEX = 128          # (ph + 2^29) >> 22 puts x = 0 at index 128
CHOP_ROWS = 11

N = 128
D_REC_STEP = 4            # points per pos step: 512 -> 4 (0.39 %)
D_SAW_END = 26            # 2.54 %
TRI_W_END = 26.0 / 1024.0 # 2.54 %
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


def saw_rows():
    """sawTable[11][1024] from wavetable.c (comments stripped), as
    gen_stock_tables.py read_table() reads it."""
    text = open(WAVETABLE_C).read()
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    text = re.sub(r"//[^\n]*", "", text)
    m = re.search(r"sawTable\[11\]\[1024\]\s*=\s*\{(.*?)\};", text, re.S)
    v = [int(x) for x in re.findall(r"-?\d+", m.group(1))]
    if len(v) != CHOP_ROWS * CHOP_POINTS:
        sys.exit("gen_pwm_tables.py: sawTable has %d values" % len(v))
    return [v[r * CHOP_POINTS:(r + 1) * CHOP_POINTS] for r in range(CHOP_ROWS)]


def row_amplitude(row):
    """A_r: minus half the least-squares slope of the row's middle half
    (points 256..767) in full-scale units per cycle (S-R draft §4.1,
    Appendix A). About 1 for rows 0-5, up to 2.43 for row 10."""
    xs = [i / 1024.0 for i in range(256, 768)]
    ys = [row[i] / 32767.0 for i in range(256, 768)]
    mx = sum(xs) / len(xs)
    my = sum(ys) / len(ys)
    slope = (sum((x - mx) * (y - my) for x, y in zip(xs, ys)) /
             sum((x - mx) ** 2 for x in xs))
    return -slope / 2.0


def curve_fns(wave):
    """(c, C): the wave's curve c(x) and its integral C(x) from 0, in the
    table's units: R-S, c(x) = x (S099 P2: REx's exponential dropped)."""
    assert wave == "rs"
    return (lambda x: x), (lambda x: x * x / 2.0)


def chop_curve(wave):
    c, _ = curve_fns(wave)
    return [int(round(32767.0 * c(i / 1024.0 - 1.0 / 8.0))) for i in range(CHOP_POINTS)]


def chop_dc_level(wave, d_v):
    """dc(w) and level(w) = 1 / P(w) at every pos (w = d / 1024)."""
    c, C = curve_fns(wave)
    dcs, levels = [], []
    for d in d_v:
        w = d / 1024.0
        dc = (C(w / 2.0) - C(0.0)) + (C(0.5 + w / 2.0) - C(0.5))
        peak = c(0.5 + w / 2.0) - dc
        dcs.append(dc)
        levels.append(1.0 / peak)
    return dcs, levels


def chop_tables(d_v):
    rows = saw_rows()
    amp = [row_amplitude(r) for r in rows]
    span = [2.0 * a * 32767.0 for a in amp]
    gain = [int(round(16384.0 * 65536.0 / s)) for s in span]
    out = {"rows": rows, "span": span, "gain": gain}
    for wave in ("rs",):
        dcs, lv = chop_dc_level(wave, d_v)
        lref = max(lv)
        out[wave] = {
            "curve": chop_curve(wave),
            "dc_q15": [int(round(32767.0 * x)) for x in dcs],
            "dc": dcs,
            "level": lv,
            "lref": lref,
            "rel": [int(round(32767.0 * x / lref)) for x in lv],
        }
    return out


def chop_checks(t, d_v):
    """T10-T15 (S099 P1). Each returns False with a printed reason."""
    ok = True
    def bad(msg):
        print("chop tables:", msg)
        return False
    for wave in ("rs",):
        cv = t[wave]["curve"]
        # T10: the curve is 0 at x = 0 (index 128), strictly increasing,
        # inside int16, and below full scale over the read range
        if cv[CHOP_WRAP_INDEX] != 0:
            ok = bad("T10 %s curve(0) = %d" % (wave, cv[CHOP_WRAP_INDEX]))
        if any(b <= a for a, b in zip(cv, cv[1:])):
            ok = bad("T10 %s curve not strictly increasing" % wave)
        if max(cv) >= 32767 or min(cv) < -32768:
            ok = bad("T10 %s curve out of range" % wave)
        # T13: the positive peak after the DC dominates (the level keeps the
        # peak, not the negative excursion -dc), and level x peak = 1
        for k, (dc, lv) in enumerate(zip(t[wave]["dc"], t[wave]["level"])):
            if not (1.0 / lv > dc > 0.0):
                ok = bad("T13 %s pos %d: peak %.4f dc %.4f" % (wave, k, 1.0 / lv, dc))
        # T14: the Colloid's relative levels fit Q15 and are >= 1
        rel = t[wave]["rel"]
        top = t[wave]["level"].index(t[wave]["lref"])
        if max(rel) > 32767 or min(rel) < 1 or rel[top] != 32767:
            ok = bad("T14 %s rel range" % wave)
    # T11: every gate gain fits a signed halfword and is positive
    if any(not (0 < g <= 32767) for g in t["gain"]):
        ok = bad("T11 gain %s" % t["gain"])
    # T12: the Colloid loop's range (col_chopPairAdd()), swept exactly as the
    # loop computes it: every row, every width, every 2x gate point of a cycle
    # (ph in steps of 2^21, so the gate and the curve indices cover their whole
    # grid and their pairing is the loop's). The gate stays under 2^17 and its
    # product with the curve, >> 16, inside int16 x 1.1 (the Gibbs overshoot).
    # A closed form on span x g14 alone is too loose: the largest gate and the
    # largest curve never meet (the ramp's peak sits in a chopped section).
    for wave in ("rs",):
        gmax, ymax, at = chop_range(t, d_v, wave)
        if gmax >= (1 << 17) or ymax > int(32767 * 1.1):
            ok = bad("T12 %s gate %d y %d at %s" % (wave, gmax, ymax, at))
    # T15: pos 0 is the Alpha Juno Saw 2 (w = 1/2): R-S dc = 3/16, level
    # 1 / 0.5625 (S-R draft §2), to the table's rounding
    if abs(t["rs"]["dc"][0] - 0.1875) > 1e-9 or abs(t["rs"]["level"][0] - 1.0 / 0.5625) > 1e-9:
        ok = bad("T15 R-S pos 0 is not Saw 2")
    return ok


def chop_range(t, d_v, wave):
    """(largest |gate|, largest |y|, where) over rows, widths and the 2x gate
    grid, computed exactly as col_chopPairAdd() computes them (T12)."""
    cv = t[wave]["curve"]
    gmax = ymax = 0
    at = None
    for r, row in enumerate(t["rows"]):
        g14 = t["gain"][r]
        for d in d_v:
            for ph in range(0, 1 << 32, 1 << 21):
                itg = (ph >> 21) & 0x3FF
                g = (((row[itg] - row[(itg - d) & 0x3FF]) << 2) * g14 >> 16) + (d << 6)
                y = (g * cv[((ph + 0x20000000) & 0xFFFFFFFF) >> 22]) >> 16
                if abs(g) > gmax:
                    gmax = abs(g)
                if abs(y) > ymax:
                    ymax = abs(y)
                    at = (r, d, itg)
    return gmax, ymax, at


def finite_values(ws, W, ra, rb):
    """Every value fits its C type and every W is a live phase split."""
    return (all(0 < x < 2**32 for x in W) and
            all(0 < x <= 0xFFFFFFFF for x in ra) and
            all(0 < x <= 0xFFFFFFFF for x in rb))


def checks(d_rec_v, d_saw_v, l_rec, l_saw, tri_ws, tri_w, tri_ra, tri_rb,
           tri_lift):
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
    ok = ok and tri_w[N - 1] == 26 * 2**22
    # T4: the reciprocal floors
    ok = ok and all(ra * w <= M25 < (ra + 1) * w for ra, w in zip(tri_ra, tri_w))
    ok = ok and all(rb * (2**32 - w) <= M25 < (rb + 1) * (2**32 - w)
                    for rb, w in zip(tri_rb, tri_w))
    # T5: the half-cycle ends within 64 phase units
    ok = ok and all(2**31 - (((w - 1) * ra >> 32) << 6) <= 64
                    for w, ra in zip(tri_w, tri_ra))
    ok = ok and all(2**32 - ((((2**32 - 1 - w) * rb >> 32) << 6) + 2**31) <= 64
                    for w, rb in zip(tri_w, tri_rb))
    # T6: pos 0 is the S094 P1 width-1/2 constant set
    ok = ok and d_rec_v[0] == 512 and d_saw_v[0] == 512
    ok = ok and l_rec[0] == 16384 and l_saw[0] == 16384
    ok = ok and tri_w[0] == 2**31
    ok = ok and tri_ra[0] == P1_RECIP_HALF and tri_rb[0] == P1_RECIP_HALF
    ok = ok and tri_lift[0] == 0
    # T7: the lift
    ok = ok and all(0 <= x <= 4 for x in tri_lift)
    ok = ok and all(b >= a for a, b in zip(tri_lift, tri_lift[1:]))
    # T9: every value fits its type
    ok = ok and all(0 <= x <= 0xFFFF for x in d_rec_v + d_saw_v)
    ok = ok and all(-0x8000 <= x <= 0x7FFF for x in l_rec + l_saw)
    ok = ok and all(0 <= x <= 0xFF for x in tri_lift)
    ok = ok and finite_values(tri_ws, tri_w, tri_ra, tri_rb)
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
        " *               oscPwmTriW: the warp's split point W (down to 2.54 % of",
        " *                 the cycle);",
        " *               oscPwmTriRa/Rb: floor((2^32 - 1) 2^25 / W)",
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
        "",
        "#endif /* OSC_PWM_TABLES_H_ */",
        "",
    ])


def rows(vals, fmt, per):
    return ["    " + ", ".join(fmt % v for v in vals[i:i + per]) + ","
            for i in range(0, len(vals), per)]


def render_c(d_rec_v, d_saw_v, l_rec, l_saw, tri_w, tri_ra, tri_rb, tri_lift):
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
    parts += rows(tri_lift, "%du", 16) + ["};", ""]
    return "\n".join(parts)


def render_chop_h(t):
    return "\n".join([
        "/* Generated by tools/plots/gen_pwm_tables.py (S099 P1). Do not edit. */",
        "#ifndef OSC_CHOP_TABLES_H_",
        "#define OSC_CHOP_TABLES_H_",
        "",
        "#include <stdint.h>",
        "",
        "/*",
        " * The chopped pwm wave R-S (S099 P1; DRAFT_S-R_PWM_WAVE.md §4, §6.4;",
        " * S099 P2: REx dropped, the widths Rec's), flash.",
        " *",
        " * What:       y = c(ramp_w(phi)) x gate(phi) - dc: the 1x ramp, wrapped",
        " *             at 7/8 of the cycle and read through the wave's curve",
        " *             table, times a band-limited pulse an octave up (the",
        " *             shipped Rec two-read at twice the phase, on the sawTable",
        " *             row OSC_CHOP_LIFT octaves above the note's), minus the",
        " *             wave's mean. Tables by pos (positionApplied 0..127; the",
        " *             width w = oscPwmDRec[pos] / 1,024 of each gate cycle,",
        " *             50 % .. 0.39 %):",
        " *               oscChopCurveRs: the curve on x = i / 1,024 - 1/8",
        " *                 (index (ph + 2^29) >> 22), linear;",
        " *               oscChopGainQ14[row]: the gate's normalisation,",
        " *                 16384 x 65536 / span (D x g14 / 16384 spans 65,536);",
        " *               oscChopSpanF / InvSpanF[row]: span and 1 / span (float);",
        " *               oscChopDcRsQ15[pos]: 32767 x the mean;",
        " *               oscChopLevelRs[pos]: 1 / the peak after the mean;",
        " *               oscChopRelRsQ15[pos]: 32767 x level / OSC_CHOP_LREF_RS.",
        " * Why:        the Alpha Juno's PWM saw is a ramp chopped to its floor by",
        " *             a pulse at +1 octave; no sum of saw reads makes its kinks,",
        " *             a product does. Per-block lookups, no division.",
        " * Accessors:  AlkalineVoice.c alk_renderChop(); ColloidVoice.c",
        " *             col_chopBlock(); ColloidOsc.c col_chopPairAdd().",
        " * Affiliates: oscPwmTables.h (oscPwmDRec, the widths);",
        " *             tools/plots/gen_pwm_tables.py (the law, T10-T15).",
        " */",
        "#define OSC_CHOP_LIFT 2u          /* Q-15: the gate row is the note's + 2 */",
        "#define OSC_CHOP_LREF_RS %.9ef  /* the largest R-S level */" % t["rs"]["lref"],
        "extern const int16_t oscChopCurveRs[1024];",
        "extern const int16_t oscChopGainQ14[11];",
        "extern const float   oscChopSpanF[11];",
        "extern const float   oscChopInvSpanF[11];",
        "extern const int16_t oscChopDcRsQ15[128];",
        "extern const float   oscChopLevelRs[128];",
        "extern const int16_t oscChopRelRsQ15[128];",
        "",
        "#endif /* OSC_CHOP_TABLES_H_ */",
        "",
    ])


def render_chop_c(t):
    f = lambda vals: ["    " + ", ".join("%.9ef" % v for v in vals[i:i + 4]) + ","
                      for i in range(0, len(vals), 4)]
    parts = ["/* Generated by tools/plots/gen_pwm_tables.py (S099 P1). Do not edit. */",
             '#include "oscChopTables.h"', ""]
    for name, wave in (("oscChopCurveRs", "rs"),):
        parts += ["const int16_t %s[1024] = {" % name]
        parts += rows(t[wave]["curve"], "%6d", 8) + ["};", ""]
    parts += ["const int16_t oscChopGainQ14[11] = {"] + rows(t["gain"], "%6d", 11) + ["};", ""]
    parts += ["const float oscChopSpanF[11] = {"] + f(t["span"]) + ["};", ""]
    parts += ["const float oscChopInvSpanF[11] = {"] + f([1.0 / s for s in t["span"]]) + ["};", ""]
    for wave, suffix in (("rs", "Rs"),):
        parts += ["const int16_t oscChopDc%sQ15[128] = {" % suffix] + rows(t[wave]["dc_q15"], "%6d", 8) + ["};", ""]
        parts += ["const float oscChopLevel%s[128] = {" % suffix] + f(t[wave]["level"]) + ["};", ""]
        parts += ["const int16_t oscChopRel%sQ15[128] = {" % suffix] + rows(t[wave]["rel"], "%6d", 8) + ["};", ""]
    return "\n".join(parts)


def main():
    d_rec_v, d_saw_v = d_rec(), d_saw()
    l_rec, l_saw = level(d_rec_v), level(d_saw_v)
    tri_ws, tri_w, tri_ra, tri_rb = split(TRI_W_END)
    tri_lift = lift(tri_ws)
    ok = checks(d_rec_v, d_saw_v, l_rec, l_saw, tri_ws, tri_w, tri_ra, tri_rb,
                tri_lift)
    # S099 P1: the chop tables; S099 P2: on Rec's widths (oscPwmDRec)
    chop = chop_tables(d_rec_v)
    ok = chop_checks(chop, d_rec_v) and ok
    h = render_h()
    c = render_c(d_rec_v, d_saw_v, l_rec, l_saw, tri_w, tri_ra, tri_rb, tri_lift)
    ch, cc = render_chop_h(chop), render_chop_c(chop)
    if "--check" in sys.argv:
        ok = (ok and open(H_PATH).read() == h and open(C_PATH).read() == c and
              open(CHOP_H_PATH).read() == ch and open(CHOP_C_PATH).read() == cc)
        print("pwm tables", "OK" if ok else "FAILED")
        return 0 if ok else 1
    if not ok:
        print("pwm tables: T1-T7/T9 or T10-T15 failed")
        return 1
    for path, text in ((H_PATH, h), (C_PATH, c), (CHOP_H_PATH, ch), (CHOP_C_PATH, cc)):
        open(path, "w").write(text)
    print("wrote", H_PATH, C_PATH, CHOP_H_PATH, CHOP_C_PATH)
    return 0


if __name__ == "__main__":
    sys.exit(main())
