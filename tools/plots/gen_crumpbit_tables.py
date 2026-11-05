#!/usr/bin/env python3
"""Generate the CrumpBit control tables (S091 P2; sub-type tables S091 P3;
the `grn` rework tables S091 P4; the one-pole output filter S091 P5).

What:       writes Core/DSP/Effects/CrumpBit/CrumpBitTables.c with three
            128-entry float tables, one per 0..127 row value:
              crumpBit_rateTable[r]  delay length in samples for `rte` r:
                                     L(r) = 882 x (126654 / 882)^(r/127),
                                     20 ms at 0 .. 2.871 s (the
                                     arena less the 2-sample guard) at 127,
                                     exponential (equal steps = equal time
                                     ratios); higher r = longer (P2 Q2).
                                     S091 P3 N1: the top assumes the arena
                                     less one 32 B granule (126,656 B; the
                                     92 B runtime may cost a granule,
                                     depending on LTO's DTCM packing);
              crumpBit_toneK[t]      one-pole coefficient of the `ton`
                                     tilt, k = 1 - exp(-2 pi fc / fs):
                                     t 0..62 low-pass, fc 150 Hz .. 18 kHz;
                                     t 63 exactly 1 (flat: the pole follows
                                     its input, a bit-exact pass-through);
                                     t 64..127 high-pass, fc 20 Hz .. 2 kHz;
              crumpBit_filterG[v]    TPT pre-warped cutoff g of `frq` v,
                                     g = fastTan(pi x 0.45 x shaper(v/127)),
                                     the voice-filter law of
                                     SVF_directSetFilterValue() with
                                     valueShaperF2F(v/127, FILTER_SHAPER);
            S091 P3 sub-type tables (one entry per row value unless noted):
              crumpBit_combTable[n]  `cmb` comb length in samples: rte n
                                     plays MIDI note 112 - n (5.27 kHz at 0
                                     .. 20.6 Hz at 96), L = fs / f(note),
                                     so a higher rte is a longer comb and a
                                     lower note (A1); n > 96 repeats 96;
              crumpBit_pitchWindow[r] `pit` window W in samples, 10 ms x
                                     20^(r/127): 441 .. 8,822 (10..200 ms);
              crumpBit_semitoneRatio[s] (49 entries) 2^((s - 24)/12), the
                                     pitch ratio of s - 24 semitones;
              crumpBit_grainFraction[m] (65 entries) 2^(-8 (1 - m/64)):
                                     S091 P4: the `grn` grain Length as a
                                     fraction of the record line N, 1/256
                                     .. 1 (eight octaves, equal ratios),
                                     m = |len - 63|; the DSP holds it at
                                     least 220 samples (~5 ms, Q1);
              crumpBit_reverbSize[r] `rvb` size factor 0.25 x 10^(r/127)
                                     (0.25 .. 2.5) for the four base delays;
              crumpBit_reverbDecay[f] `rvb` log2 of the per-sample line gain
                                     for RT60 T(f) = 0.1 s x 100^(f/127)
                                     (0.1 .. 10 s): -3 log2(10) / (T fs).
            S091 P4 `grn` rework tables:
              crumpBit_grainTime[c]  the `grn` delay time in 96-PPQ ticks
                                     for `rte` c (Q8, Q10): a continuous
                                     log curve from 6 ticks (1/64) to 384
                                     (one bar), six octaves over 0..127,
                                     with the 16 rhythmic stops pinned to
                                     exact tick counts at the positions
                                     GRAIN_STOP_CC and equal ratios
                                     between neighbouring stops (the
                                     StepScale "nudged" curve, extended
                                     past 1/2 by d/2 and 1br);
              crumpBit_grainStopCc[k] (16 entries, uint8) the `rte`
                                     position of stop k, shortest first;
                                     Position and Density index stops by k;
              crumpBit_grainDensity[d] (64 entries) the mean trigger
                                     interval of negative Density d = 1..63
                                     as a fraction of N: (882/63328)^((d-1)
                                     /62), N at -1 down to ~20 ms (882
                                     samples) at -63 with the whole arena
                                     (Q6); entry 0 (unused) is 1.
            S091 P5 one-pole output filter (`grn`, `rvb`; plan F1, Q6):
              crumpBit_onePoleK[v]   the coefficient k of s += k (x - s) for
                                     `frq` index v, matched to the SVF at
                                     `res` 0: that filter (R = 1, two equal
                                     poles) is -3 dB where
                                     tan(w/2) = g sqrt(sqrt(2) - 1), g =
                                     crumpBit_filterG[v]; the one-pole with
                                     pole a = 1 - k is -3 dB at w where
                                     a = (2 - cos w) - sqrt((2 - cos w)^2 - 1).
                                     0 at v 0 (closed, as filterG[0] = 0),
                                     strictly increasing, below 1.
Why:        crumpBit_process() runs inside mixer_calcNextSampleBlock(), so
            per-block expf/logf/fastTan cost render time. Lookups are
            constant-cost, and the Rate table also lets the share clamp
            (crumpBit_effectiveMax) and the DSP use the identical lengths.
Checks:     C1 rate[0] = 882 and rate[127] = 126654 (to 1e-3), strictly
            increasing; C2 toneK in (0, 1], toneK[63] == 1, low-pass side
            increasing, high-pass side increasing; C3 filterG[0] = 0
            (frq 0 closes the filter, as on a voice) and strictly
            increasing; C4 (S091 P3) comb lengths non-decreasing and
            >= 8 samples, pitch windows strictly increasing, semitone
            ratio 1.0 at index 24, grain fraction 1.0 at 64, reverb sizes
            strictly increasing, decay log-gains negative and increasing
            (longer RT60, gain nearer 1); C6 (S091 P4) grain time strictly
            increasing, equal to the stop's tick count at every stop
            position, 6 at 0 and 384 at 127; grain density 1 at d = 1
            (entry 0 unused, 1) and strictly decreasing from there; C7
            (S091 P5) one-pole k: 0 at index 0, strictly increasing, below
            1, and |H(w3)|^2 = 1/2 within 1e-9 at every index's SVF -3 dB
            frequency; C5 --check: regenerate in memory and
            compare with the committed file byte for byte (exit 1 on any
            failure).
Inputs:     the constants below (FS matches config.h REAL_FS; FILTER_SHAPER
            matches Core/globals.h).
Outputs:    Core/DSP/Effects/CrumpBit/CrumpBitTables.c, or a pass/fail line
            with --check.
Accessors:  tools/dsp_test/Makefile crumpbit_tables target (run from the
            repository root).
Affiliates: CrumpBitTables.h; CrumpBitEffect.c; ResonantFilter.c fastTan();
            valueShaper.h valueShaperF2F(); gen_fold_tables.py (pattern).
"""
import math
import sys

FS = 44108.07291666667
RATE_MIN, RATE_MAX = 882.0, 126654.0
TONE_LP_LO, TONE_LP_HI = 150.0, 18000.0
TONE_HP_LO, TONE_HP_HI = 20.0, 2000.0
TONE_FLAT = 63
FILTER_SHAPER = -0.9
PATH = "Core/DSP/Effects/CrumpBit/CrumpBitTables.c"
COMB_TOP_NOTE, COMB_STEPS = 112, 96
PIT_W_MIN, PIT_W_RATIO = 0.010, 20.0
RVB_SIZE_MIN, RVB_SIZE_RATIO = 0.25, 10.0
RVB_T_MIN, RVB_T_RATIO = 0.1, 100.0
# S091 P4: the `grn` rhythmic stops in 96-PPQ ticks, shortest first:
# /64 32t /32 16t d32 /16 8Tr d16 /8 4Tr d/8 /4 d/4 /2 d/2 1br.
GRAIN_STOP_TICKS = [6, 8, 12, 16, 18, 24, 32, 36, 48, 64, 72, 96, 144, 192,
                    288, 384]
GRAIN_LEN_OCTAVES = 8.0
GRAIN_DENSITY_MIN, GRAIN_DENSITY_N = 882.0, 63328.0


def rate_table():
    return [RATE_MIN * (RATE_MAX / RATE_MIN) ** (r / 127.0) for r in range(128)]


def one_pole_k(fc):
    return 1.0 - math.exp(-2.0 * math.pi * fc / FS)


def tone_table():
    vals = []
    for t in range(128):
        if t < TONE_FLAT:
            fc = TONE_LP_LO * (TONE_LP_HI / TONE_LP_LO) ** (t / 62.0)
            vals.append(one_pole_k(fc))
        elif t == TONE_FLAT:
            vals.append(1.0)
        else:
            fc = TONE_HP_LO * (TONE_HP_HI / TONE_HP_LO) ** ((t - 64) / 63.0)
            vals.append(one_pole_k(fc))
    return vals


def value_shaper(val, shape):
    k = 2.0 * shape / (1.0001 - shape)
    return (1.0 + k) * val / (1.0 + k * abs(val))


def fast_tan(x):
    a = -15.0 * x + x * x * x
    b = 3.0 * (-5.0 + 2.0 * x * x)
    return a / b


def filter_table():
    return [fast_tan(math.pi * 0.45 * value_shaper(v / 127.0, FILTER_SHAPER))
            for v in range(128)]


def comb_table():
    vals = []
    for n in range(128):
        note = COMB_TOP_NOTE - min(n, COMB_STEPS)
        vals.append(FS / (440.0 * 2.0 ** ((note - 69) / 12.0)))
    return vals


def pitch_window_table():
    return [PIT_W_MIN * PIT_W_RATIO ** (r / 127.0) * FS for r in range(128)]


def semitone_table():
    return [2.0 ** ((s - 24) / 12.0) for s in range(49)]


def grain_fraction_table():
    return [2.0 ** (-GRAIN_LEN_OCTAVES * (1.0 - m / 64.0)) for m in range(65)]


def grain_stop_cc():
    """Stop k at the position of its tick count on the six-octave log curve
    (Python round: half to even), so the curve passes exactly through every
    division."""
    t0, t1 = GRAIN_STOP_TICKS[0], GRAIN_STOP_TICKS[-1]
    return [int(round(127.0 * math.log2(t / t0) / math.log2(t1 / t0)))
            for t in GRAIN_STOP_TICKS]


def grain_time_table():
    """Equal ratios between neighbouring stops: piecewise log, exact at
    every stop."""
    cc, ticks = grain_stop_cc(), GRAIN_STOP_TICKS
    vals = []
    for c in range(128):
        k = max(i for i in range(len(cc)) if cc[i] <= c)
        if cc[k] == c or k == len(cc) - 1:
            vals.append(float(ticks[k]))
            continue
        frac = (c - cc[k]) / (cc[k + 1] - cc[k])
        vals.append(ticks[k] * (ticks[k + 1] / ticks[k]) ** frac)
    return vals


def grain_density_table():
    ratio = GRAIN_DENSITY_MIN / GRAIN_DENSITY_N
    return [1.0] + [ratio ** ((d - 1) / 62.0) for d in range(1, 64)]


def svf_w3(g):
    """The -3 dB angular frequency (radians per sample) of the TPT SVF
    low-pass at R = 1 (`res` 0) with pre-warped cutoff g: the prototype
    1/(s + 1)^2 is -3 dB at s = sqrt(sqrt(2) - 1), mapped back through
    tan(w/2) = g s."""
    return 2.0 * math.atan(g * math.sqrt(math.sqrt(2.0) - 1.0))


def one_pole_k_table(filt):
    """k for each `frq` index: the one-pole y += k (x - y) whose -3 dB point
    is the SVF's at `res` 0 (S091 P5 F1, Q6)."""
    vals = []
    for g in filt:
        if g == 0.0:
            vals.append(0.0)
            continue
        c = math.cos(svf_w3(g))
        a = (2.0 - c) - math.sqrt((2.0 - c) ** 2 - 1.0)
        vals.append(1.0 - a)
    return vals


def one_pole_gain2(k, w):
    """|H(w)|^2 of y += k (x - y): k^2 / (1 - 2 a cos w + a^2), a = 1 - k."""
    a = 1.0 - k
    return k * k / (1.0 - 2.0 * a * math.cos(w) + a * a)


def reverb_size_table():
    return [RVB_SIZE_MIN * RVB_SIZE_RATIO ** (r / 127.0) for r in range(128)]


def reverb_decay_table():
    return [-3.0 * math.log2(10.0) / (RVB_T_MIN * RVB_T_RATIO ** (f / 127.0) * FS)
            for f in range(128)]


def render_table(name, comment, vals):
    n = len(vals)
    lines = ["", "/* %s */" % comment, "const float %s[%d] = {" % (name, n)]
    for i in range(0, n, 4):
        last = min(i + 3, n - 1)
        lines.append("    " + ", ".join("%.9ef" % x for x in vals[i:i + 4]) +
                     ",  /* %3d..%3d */" % (i, last))
    lines.append("};")
    return lines


def render_u8(name, comment, vals):
    lines = ["", "/* %s */" % comment,
             "const uint8_t %s[%d] = {" % (name, len(vals))]
    lines.append("    " + ", ".join("%du" % v for v in vals) + ",")
    lines.append("};")
    return lines


def render(rate, tone, filt, extra):
    lines = ["/* Generated by tools/plots/gen_crumpbit_tables.py (S091 P2). Do not edit. */",
             '#include "CrumpBitTables.h"']
    lines += render_table("crumpBit_rateTable",
                          "Delay length in samples, rte 0..127: 20 ms .. 2.871 s.", rate)
    lines += render_table("crumpBit_toneK",
                          "Tone tilt pole, ton 0..127: LP 150 Hz..18 kHz, 63 flat, HP 20 Hz..2 kHz.", tone)
    lines += render_table("crumpBit_filterG",
                          "Output filter TPT g, frq 0..127 (voice cutoff law).", filt)
    comb, win, semi, frac, size, decay, gtime, gstop, gdens, opk = extra
    lines += render_table("crumpBit_combTable",
                          "cmb comb length in samples, rte 0..127: note 112 - rte, clamped at 96.", comb)
    lines += render_table("crumpBit_pitchWindow",
                          "pit window in samples, rte 0..127: 10 ms .. 200 ms.", win)
    lines += render_table("crumpBit_semitoneRatio",
                          "Pitch ratio of (index - 24) semitones, -24 .. +24.", semi)
    lines += render_table("crumpBit_grainFraction",
                          "grn grain Length as a fraction of N, |len - 63| 0..64: 1/256 .. 1.", frac)
    lines += render_table("crumpBit_reverbSize",
                          "rvb size factor, rte 0..127: 0.25 .. 2.5.", size)
    lines += render_table("crumpBit_reverbDecay",
                          "rvb log2 line gain per sample, fbk 0..127: RT60 0.1 s .. 10 s.", decay)
    lines += render_table("crumpBit_grainTime",
                          "grn delay time in 96-PPQ ticks, rte 0..127: 1/64 .. 1 bar, stops exact.", gtime)
    lines += render_u8("crumpBit_grainStopCc",
                       "grn rhythmic stop positions on the rte curve, /64 .. 1br.", gstop)
    lines += render_table("crumpBit_grainDensity",
                          "grn mean trigger interval / N, negative Density 1..63 (entry 0 unused).", gdens)
    lines += render_table("crumpBit_onePoleK",
                          "grn/rvb one-pole output filter k, frq 0..127: -3 dB at the SVF's (res 0).", opk)
    return "\n".join(lines) + "\n"


def main():
    rate, tone, filt = rate_table(), tone_table(), filter_table()
    comb, win, semi = comb_table(), pitch_window_table(), semitone_table()
    frac, size, decay = grain_fraction_table(), reverb_size_table(), reverb_decay_table()
    gtime, gstop, gdens = grain_time_table(), grain_stop_cc(), grain_density_table()
    opk = one_pole_k_table(filt)
    ok = abs(rate[0] - RATE_MIN) < 1e-3 and abs(rate[127] - RATE_MAX) < 1e-3
    ok = ok and all(a < b for a, b in zip(rate, rate[1:]))
    ok = ok and all(0.0 < k <= 1.0 for k in tone) and tone[TONE_FLAT] == 1.0
    ok = ok and all(a < b for a, b in zip(tone[:TONE_FLAT], tone[1:TONE_FLAT]))
    ok = ok and all(a < b for a, b in zip(tone[64:], tone[65:]))
    ok = ok and filt[0] == 0.0 and all(a < b for a, b in zip(filt, filt[1:]))
    ok = ok and comb[0] >= 8.0 and all(a <= b for a, b in zip(comb, comb[1:]))
    ok = ok and all(a < b for a, b in zip(win, win[1:]))
    ok = ok and semi[24] == 1.0 and frac[64] == 1.0
    ok = ok and all(a < b for a, b in zip(size, size[1:]))
    ok = ok and all(x < 0.0 for x in decay) and all(a < b for a, b in zip(decay, decay[1:]))
    ok = ok and all(a < b for a, b in zip(gtime, gtime[1:]))
    ok = ok and gtime[0] == 6.0 and gtime[127] == 384.0
    ok = ok and all(gtime[c] == float(t) for c, t in zip(gstop, GRAIN_STOP_TICKS))
    ok = ok and len(set(gstop)) == len(gstop)
    ok = ok and gdens[0] == 1.0 and gdens[1] == 1.0
    ok = ok and all(a > b for a, b in zip(gdens[1:], gdens[2:]))
    ok = ok and opk[0] == 0.0 and all(a < b for a, b in zip(opk, opk[1:]))
    ok = ok and all(0.0 <= k < 1.0 for k in opk)
    ok = ok and all(abs(one_pole_gain2(k, svf_w3(g)) - 0.5) < 1e-9
                    for k, g in zip(opk[1:], filt[1:]))
    text = render(rate, tone, filt,
                  (comb, win, semi, frac, size, decay, gtime, gstop, gdens,
                   opk))
    if "--check" in sys.argv:
        ok = ok and open(PATH).read() == text
        print("crumpbit tables", "OK" if ok else "FAILED")
        return 0 if ok else 1
    if not ok:
        print("crumpbit tables: C1-C4/C6/C7 failed")
        return 1
    open(PATH, "w").write(text)
    print("wrote", PATH)
    return 0


if __name__ == "__main__":
    sys.exit(main())
