#!/usr/bin/env python3
"""Render the S099 P1 chop audition files (offline; no firmware change).

What:       writes 16-bit mono 44,108 Hz WAV files into --out (default: the
            path given; nothing is written into the tree):
              <wave>_<f>Hz_<form>.wav for wave in rs, rexk4, rexk5, rexk6,
              rec (the shipped Rec, the shipped D form) and rs-lowfirst (the
              other gate orientation, D-4); f in 55, 110, 440, 1760; form in
              T (both gate points truncated, the Colloid's form T) and I (both
              interpolated, the reference). Each file is 1 s at pos 0, 24, 48
              and 96, then a 4 s pos sweep 0 -> 127 -> 0 at the shipped slew
              (2 index steps per 32-sample block).
            The gate is the shipped two-read on the sawTable row lifted by
            OSC_CHOP_LIFT, read at twice the phase (osc_pwmReadD(), sign -1),
            floored by w; the curve is the table law of gen_pwm_tables.py
            (rex_s with k 4/5/6), read truncated (form T) or interpolated
            (form I); the DC is removed and the peak kept (the level law).
Why:        D-11 (k by ear), D-4 (orientation), and the form T low-note
            question (S-R draft §6.3) can be heard before any firmware work.
Inputs:     --out DIR (required).
Outputs:    the WAV files and a one-line summary each (RMS, peak).
Accessors:  python3 tools/plots/render_chop_audition.py --out <scratchpad>.
Affiliates: gen_pwm_tables.py (saw_rows(), curve_fns(), d_saw()).
"""
import argparse
import math
import os
import struct
import sys
import wave

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_pwm_tables as G   # noqa: E402

FS = 44108
BLOCK = 32


def octave(f):
    return min(10, max(0, int(math.floor(math.log2(f / 16.3516)))))


def read_d(row, p, d, interp):
    ia = (p >> 22) & 1023
    ib = (ia - d) & 1023
    if not interp:
        return row[ia] - row[ib]
    fr = ((p >> 7) & 0x7FFF) / 32768.0
    a = row[ia] + fr * (row[(ia + 1) & 1023] - row[ia])
    b = row[ib] + fr * (row[(ib + 1) & 1023] - row[ib])
    return a - b


def render(wave_name, f, form, k):
    rows = G.saw_rows()
    d_saw = G.d_saw()
    if wave_name.startswith("rex"):
        G.CHOP_REX_K = k
    curve_wave = "rs" if wave_name.startswith("rs") else "rex"
    curve = G.chop_curve(curve_wave)
    dcs, levels = G.chop_dc_level(curve_wave, d_saw)
    r = min(octave(f) + 2, 10)
    row = rows[r]
    span = 2.0 * G.row_amplitude(row) * 32767.0
    inc = int(round(f / FS * 2 ** 32)) & 0xFFFFFFFF
    plan = [0] * FS + [24] * FS + [48] * FS + [96] * FS
    sweep = []
    pos = 0
    for _ in range(4 * FS // BLOCK):
        target = 127 if len(sweep) < 2 * FS else 0
        pos += max(-2, min(2, target - pos))
        sweep += [pos] * BLOCK
    plan += sweep
    out = np.zeros(len(plan))
    p = 0
    interp = (form == "I")
    lowfirst = wave_name.endswith("lowfirst")
    for n, k_pos in enumerate(plan):
        d = d_saw[k_pos]
        w = d / 1024.0
        p2 = (p << 1) & 0xFFFFFFFF
        if lowfirst:
            # D-4's other orientation (R-S only): the gate passes the upper w
            # of each half-cycle (B read at +w: the shipped pulse shifted by
            # its own width), so the open parts are [1/2 - w/2, 1/2) and
            # [1 - w/2, 1). The ramp's wrap moves into the FIRST chopped
            # section, at 1/8 (inside [0, 1/2 - w/2) for every w <= 1/2): x =
            # phi, or phi + 1 below 1/8. The curve is evaluated exactly (the
            # firmware would need a table on [1/8, 9/8), A-G3), and the mean
            # and peak are this orientation's own.
            p2 = (p2 + (d << 22)) & 0xFFFFFFFF
            g = read_d(row, p2, d, interp) / span + w
            phi = p / 4294967296.0
            x = phi + 1.0 if phi < 0.125 else phi
            dc = ((0.25 - (0.5 - w / 2.0) ** 2) + (1.0 - (1.0 - w / 2.0) ** 2)) / 2.0
            out[n] = (x * g - dc) / (1.0 - dc)
            p = (p + inc) & 0xFFFFFFFF
            continue
        g = read_d(row, p2, d, interp) / span + w
        iv = ((p + 0x20000000) & 0xFFFFFFFF) >> 22
        if interp:
            fv = ((p + 0x20000000) & 0x3FFFFF) / 4194304.0
            v = (curve[iv] + fv * (curve[(iv + 1) & 1023] - curve[iv])) / 32767.0
        else:
            v = curve[iv] / 32767.0
        out[n] = (v * g - dcs[k_pos]) * levels[k_pos]
        p = (p + inc) & 0xFFFFFFFF
    return out


def render_rec(f):
    rows = G.saw_rows()
    d_rec = G.d_rec()
    row = rows[octave(f)]
    inc = int(round(f / FS * 2 ** 32)) & 0xFFFFFFFF
    out = np.zeros(8 * FS)
    p = 0
    for n in range(len(out)):
        pos = [0, 24, 48, 96][min(3, n // FS)] if n < 4 * FS else 64
        out[n] = read_d(row, p, d_rec[pos], True) / 65534.0
        p = (p + inc) & 0xFFFFFFFF
    return out


def write(path, x):
    x = np.clip(x * 0.5, -1.0, 1.0)
    with wave.open(path, "wb") as fh:
        fh.setnchannels(1)
        fh.setsampwidth(2)
        fh.setframerate(FS)
        fh.writeframes(b"".join(struct.pack("<h", int(round(v * 32767))) for v in x))
    print("%s rms %.3f peak %.3f" % (os.path.basename(path), float(np.sqrt(np.mean(x * x))),
                                     float(np.max(np.abs(x)))))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    for f in (55, 110, 440, 1760):
        write(os.path.join(a.out, "rec_%dHz.wav" % f), render_rec(f))
        for form in ("T", "I"):
            write(os.path.join(a.out, "rs_%dHz_%s.wav" % (f, form)), render("rs", f, form, 0))
            write(os.path.join(a.out, "rs-lowfirst_%dHz_%s.wav" % (f, form)),
                  render("rs-lowfirst", f, form, 0))
            for k in (4, 5, 6):
                write(os.path.join(a.out, "rexk%d_%dHz_%s.wav" % (k, f, form)),
                      render("rexk%d" % k, f, form, float(k)))


if __name__ == "__main__":
    main()
