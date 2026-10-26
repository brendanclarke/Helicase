#!/usr/bin/env python3
"""Generate the stock-wave recipes and the device reference (S084 VX2 VX2-4.1).

What:       reads tools/plots/stock_wave_harmonics.csv and the firmware sine
            table, quantises each wave's cosine/sine coefficients to int16,
            emulates the device's additive sum in float32 for every level, and
            writes Core/SampleRom/StockWaveRecipes.c (A, B and the per-level
            peak scale). With --emit-ref it also writes the expected int16
            points, little-endian, for tools/dsp_test/test_stock_gen.c.
Why:        the tables are generated at install from about 10 KB of flash
            instead of storing 61 KB twice (plan §4.1.2 / W4).
Inputs:     tools/plots/stock_wave_harmonics.csv, Core/DSPAudio/wavetable.c.
Outputs:    Core/SampleRom/StockWaveRecipes.c; optional reference binary.
Accessors:  python3 tools/gen_stock_recipes.py [--emit-ref PATH].
Affiliates: Core/SampleRom/StockWaveGen.c (the same arithmetic),
            tools/dsp_test/test_stock_gen.c.
"""
import argparse
import csv
import hashlib
import math
import os
import re
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
CSV_PATH = os.path.join(HERE, "plots", "stock_wave_harmonics.csv")
TABLE_PATH = os.path.join(ROOT, "Core", "DSPAudio", "wavetable.c")
OUT_PATH = os.path.join(ROOT, "Core", "SampleRom", "StockWaveRecipes.c")

WAVES = ["sq3", "sw3", "sqa", "tra", "swa"]
H_MAX = 511
LEVELS = 11
LEVEL_LEN = [1024, 1024, 1024, 1024, 1024, 512, 256, 128, 64, 16, 16]
LEVEL_H = [511, 511, 337, 168, 84, 42, 21, 10, 5, 2, 1]
LEVEL_OFF = [0, 1024, 2048, 3072, 4096, 5120, 5632, 5888, 6016, 6080, 6096]
POINTS_PER_WAVE = 6112


def read_sine_table():
    text = open(TABLE_PATH).read()
    m = re.search(r"sine_table\[TABLESIZE\s*\+\s*1\]\s*=\s*\{(.*?)\};",
                  text, re.S)
    if not m:
        sys.exit("gen_stock_recipes.py: sine_table not found")
    values = [int(v) for v in re.findall(r"-?\d+", m.group(1))]
    if len(values) != 4097:
        sys.exit("gen_stock_recipes.py: sine_table has %d values" % len(values))
    return np.array(values, dtype=np.float32)


def read_harmonics():
    waves = {name: (np.zeros(H_MAX + 1), np.zeros(H_MAX + 1))
             for name in WAVES}
    with open(CSV_PATH) as fh:
        for row in csv.DictReader(fh):
            name = row["wave"]
            k = int(row["harmonic"])
            mag = float(row["magnitude"])
            phase = float(row["phase_rad"])
            a, b = waves[name]
            a[k] = 2.0 * mag * math.cos(phase)
            b[k] = -2.0 * mag * math.sin(phase)
    return waves


def quantise(a, b):
    amax = max(float(np.max(np.abs(a[1:]))), float(np.max(np.abs(b[1:]))))
    scale = 32767.0 / amax
    aq = np.rint(a * scale).astype(np.int32)
    bq = np.rint(b * scale).astype(np.int32)
    return aq, bq


def device_level(aq, bq, length, harmonics, sine):
    """Float32 emulation of StockWaveGen.c for one level."""
    k = np.arange(1, harmonics + 1, dtype=np.int64)
    m = np.arange(length, dtype=np.int64)
    step = (k[None, :] * m[:, None] * 4096) // length
    # The firmware table stores -cos(2*pi*i/4096) (sine_table[0] = -32767),
    # so cos = -table[i] and sin = table[i + 1024].
    c = -sine[step & 4095]
    s = sine[(step + 1024) & 4095]
    total = np.zeros(length, dtype=np.float32)
    for i in range(len(k)):
        total = total + (np.float32(aq[k[i]]) * c[:, i] +
                         np.float32(bq[k[i]]) * s[:, i])
    peak = float(np.max(np.abs(total))) if length else 0.0
    scale = (32767.0 / peak) if peak > 0.0 else 0.0
    return total * np.float32(scale), np.float32(scale)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--emit-ref")
    args = ap.parse_args()

    sine = read_sine_table()
    harmonics = read_harmonics()
    sha = hashlib.sha256(open(CSV_PATH, "rb").read()).hexdigest()

    aq_all = np.zeros((len(WAVES), H_MAX), dtype=np.int16)
    bq_all = np.zeros((len(WAVES), H_MAX), dtype=np.int16)
    scale_all = np.zeros((len(WAVES), LEVELS), dtype=np.float32)
    ref = np.zeros((len(WAVES), POINTS_PER_WAVE), dtype=np.int16)

    for w, name in enumerate(WAVES):
        a, b = harmonics[name]
        aq, bq = quantise(a, b)
        aq_all[w] = aq[1:].astype(np.int16)
        bq_all[w] = bq[1:].astype(np.int16)
        worst_rel = -999.0

        # Recipe error check against the unquantised normalised recipe.
        amax = max(float(np.max(np.abs(a[1:]))), float(np.max(np.abs(b[1:]))))
        ideal = a[1:] / amax
        got = aq[1:].astype(np.float64) / 32767.0
        err_db = 10 * math.log10(max(np.mean((ideal - got) ** 2), 1e-30) /
                                 max(np.mean(ideal ** 2), 1e-30))
        if err_db > -60.0:
            print("warning: %s quantised recipe error %.1f dB" % (name, err_db))

        for lv in range(LEVELS):
            level, scale = device_level(aq, bq, LEVEL_LEN[lv],
                                        LEVEL_H[lv], sine)
            scale_all[w, lv] = scale
            points = np.clip(np.rint(level), -32768, 32767).astype(np.int16)
            ref[w, LEVEL_OFF[lv]:LEVEL_OFF[lv] + LEVEL_LEN[lv]] = points
            # Emulated level against a float64 ideal of the same recipe.
            k = np.arange(1, LEVEL_H[lv] + 1)
            mm = np.arange(LEVEL_LEN[lv])
            theta = 2.0 * math.pi * (mm[:, None] * k[None, :]) / LEVEL_LEN[lv]
            ideal64 = (aq[1:LEVEL_H[lv] + 1][None, :] * np.cos(theta) +
                       bq[1:LEVEL_H[lv] + 1][None, :] * np.sin(theta)).sum(axis=1)
            if np.max(np.abs(ideal64)) > 0:
                ideal64 = ideal64 * (32767.0 /
                                     float(np.max(np.abs(ideal64))))
                rms = math.sqrt(float(np.mean((points - ideal64) ** 2)))
                rel = 20 * math.log10(max(rms, 1e-9) / 32767.0)
                if rel > worst_rel:
                    worst_rel = rel
                if rel > -60.0:
                    print("warning: %s level %d emulation %.1f dB" %
                          (name, lv, rel))
        print("%s: quantised recipe %.1f dB, emulation worst %.1f dB" %
              (name, err_db, worst_rel))

    out = []
    out.append("/*")
    out.append(" * Stock-wave harmonic recipes (S084 VX2 VX2-4.2) - GENERATED.")
    out.append(" *")
    out.append(" * What:       per wave (sq3 sw3 sqa tra swa) the quantised")
    out.append(" *             cosine/sine coefficients of harmonics 1..511 and the")
    out.append(" *             peak scale of each of the 11 octave levels.")
    out.append(" * Why:        W4 / plan 4.1.2: the tables are generated at install")
    out.append(" *             from about 10 KB of flash instead of storing 61 KB twice.")
    out.append(" * Inputs:     tools/plots/stock_wave_harmonics.csv")
    out.append(" *             SHA-256 %s" % sha)
    out.append(" * Outputs:    read by StockWaveGen.c.")
    out.append(" * Accessors:  stockWaveGen_programAll().")
    out.append(" * Affiliates: tools/gen_stock_recipes.py (regenerate; do not edit),")
    out.append(" *             tools/dsp_test/test_stock_gen.c.")
    out.append(" */")
    out.append("")
    out.append("#include <stdint.h>")
    out.append("")
    out.append("const int16_t stockWaveRecipeA[5][511] = {")
    for w, name in enumerate(WAVES):
        out.append("    { /* %s */" % name)
        vals = aq_all[w]
        for i in range(0, H_MAX, 12):
            out.append("        " + ", ".join("%d" % v for v in vals[i:i + 12]) + ",")
        out.append("    },")
    out.append("};")
    out.append("")
    out.append("const int16_t stockWaveRecipeB[5][511] = {")
    for w, name in enumerate(WAVES):
        out.append("    { /* %s */" % name)
        vals = bq_all[w]
        for i in range(0, H_MAX, 12):
            out.append("        " + ", ".join("%d" % v for v in vals[i:i + 12]) + ",")
        out.append("    },")
    out.append("};")
    out.append("")
    out.append("const float stockWaveLevelScale[5][11] = {")
    for w, name in enumerate(WAVES):
        out.append("    { /* %s */" % name)
        vals = scale_all[w]
        for i in range(0, LEVELS, 4):
            out.append("        " + ", ".join("%.9gf" % float(v) for v in vals[i:i + 4]) + ",")
        out.append("    },")
    out.append("};")
    out.append("")
    open(OUT_PATH, "w").write("\n".join(out))
    print("wrote", OUT_PATH)

    if args.emit_ref:
        path = args.emit_ref
        os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
        open(path, "wb").write(ref.astype("<i2").tobytes())
        print("wrote", path, ref.size * 2, "bytes")


if __name__ == "__main__":
    main()
