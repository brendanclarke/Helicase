#!/usr/bin/env python3
"""Generate the stock-wave octave tables (S086 P2).

What:       reads the harmonic recipe tools/plots/stock_wave_harmonics.csv
            (written by tools/plots/plot_stock_waves.py) and writes
            Core/DSPAudio/wavetableStock.c: one const int16_t [11][1024]
            table per wave, in the layout of sawTable/recTable/triTable
            (wavetable.c), read by the same oscillator loops.
            The band limit of each octave level is measured from sawTable:
            g_lv(k) = |SAW_lv(k)| * k / |SAW_lv(1)|, set to 0 below -80 dB
            (the int16 floor). The saw has every harmonic at 1/k, so g_lv is
            the existing tables' taper, by construction. Each level is
            sum_k g_lv(k) * c_k, evaluated at 1,024 points, scaled to a peak
            of 32,767 (as every existing level is), rounded.
Why:        S086_P2_ADD_STOCK_WAVES_CORRECTLY.md §3.2: the new waves are added
            the same way as Tri/Saw/Rec, so they band-limit, switch octaves
            and interpolate against the existing waves alike.
Checks:     (the script exits 1 if one fails)
            C1 calibration: sawTable rebuilt from its ideal series (1/k)
               through g_lv; every level must be within C1_LIMIT_LSB. This
               checks the method (series, orientation, scale, rounding).
               recTable and triTable are printed for information only: they
               use another band limit (a slower roll-off with a tail above
               H, measured in S086 P2), so the new waves follow the saw's.
            C2 every new level peaks at 32,767; |DC| < 1 LSB.
            C3 above the level's Nyquist harmonic count H, no new harmonic is
               larger than max(2 x the saw's largest there, 0.5 LSB).
            C4 --check: regenerate in memory and compare with the committed
               file byte for byte.
Inputs:     tools/plots/stock_wave_harmonics.csv, Core/DSPAudio/wavetable.c.
Outputs:    Core/DSPAudio/wavetableStock.c (or --out PATH).
Accessors:  python3 tools/gen_stock_tables.py [--check] [--out PATH];
            tools/dsp_test/Makefile stock_tables.
Affiliates: wavetable.h (the externs), Oscillator.c osc_stockTables[],
            tools/plots/plot_stock_tables.py (the review plot).
"""
import argparse
import csv
import hashlib
import os
import re
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
CSV_PATH = os.path.join(HERE, "plots", "stock_wave_harmonics.csv")
TABLE_PATH = os.path.join(ROOT, "Core", "DSPAudio", "wavetable.c")
OUT_PATH = os.path.join(ROOT, "Core", "DSPAudio", "wavetableStock.c")

# (recipe name in the CSV, C table name, display name). Order = wav order
# from SQ3 on (Oscillator.h). ReA/TrA/SwA (sqa, tra, swa) come later.
WAVES = [
    ("sq3", "sq3Table", "Re3"),
    ("sw3", "sw3Table", "Sw3"),
]

N = 1024
LEVELS = 11
FS = 44108.07291666667          # REAL_FS (config.h)
FLOOR_DB = -80.0                # g_lv below this is set to 0
C1_LIMIT_LSB = 6                # sawTable, every level
# Top frequency of each level (osc_octaveEdgeHz: level lv plays below
# 16.3516 * 2^lv Hz; level 10 has no upper edge, its H is 1).
LEVEL_TOP_HZ = [16.3515987 * 2 ** lv for lv in range(LEVELS)]
LEVEL_H = [min(511, max(1, int((FS / 2) / f))) for f in LEVEL_TOP_HZ]
LEVEL_H[10] = 1


def read_table(name, text):
    m = re.search(name + r"\[11\]\[1024\]\s*=\s*\{(.*?)\};", text, re.S)
    if not m:
        sys.exit("gen_stock_tables.py: %s not found in wavetable.c" % name)
    v = [int(x) for x in re.findall(r"-?\d+", m.group(1))]
    if len(v) != LEVELS * N:
        sys.exit("gen_stock_tables.py: %s has %d values" % (name, len(v)))
    return np.array(v, dtype=np.int64).reshape(LEVELS, N)


def read_existing():
    text = open(TABLE_PATH).read()
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    text = re.sub(r"//[^\n]*", "", text)
    return {n: read_table(n, text) for n in ("sawTable", "recTable", "triTable")}


def taper(saw):
    """g[lv][k], k = 0..512 (g[lv][0] = 0), measured from sawTable."""
    g = np.zeros((LEVELS, N // 2 + 1))
    k = np.arange(N // 2 + 1)
    for lv in range(LEVELS):
        X = np.abs(np.fft.rfft(saw[lv].astype(float)))
        gl = X * k / X[1]
        gl[0] = 0.0
        gl[gl < 10 ** (FLOOR_DB / 20)] = 0.0
        g[lv] = gl
    return g


def level(c, g_lv):
    """One level: harmonics c[k] (k = 0..512) through g_lv, peak 32,767."""
    full = np.asarray(c, dtype=complex) * g_lv
    full[0] = 0.0
    x = np.fft.irfft(full * N, N)
    x = x * (32767.0 / np.max(np.abs(x)))
    return np.clip(np.rint(x), -32767, 32767).astype(np.int64)


def read_recipe():
    waves = {}
    with open(CSV_PATH) as fh:
        for row in csv.DictReader(fh):
            c = waves.setdefault(row["wave"], np.zeros(N // 2 + 1, dtype=complex))
            c[int(row["harmonic"])] = float(row["magnitude"]) * np.exp(1j * float(row["phase_rad"]))
    return waves


def ideal(name):
    """Ideal series in the stock orientation (plot_stock_waves.stock())."""
    k = np.arange(N // 2 + 1)
    c = np.zeros(N // 2 + 1, dtype=complex)
    kk = k[1:]
    if name == "sawTable":                      # 0, then +1 falling to -1
        c[1:] = -1j / kk
    elif name == "recTable":                    # high for the first half
        c[1:] = np.where(kk % 2 == 1, -1j / kk, 0)
    else:                                       # triTable: -1 at 0, +1 at 0.5
        c[1:] = np.where(kk % 2 == 1, -1.0 / kk ** 2, 0)
    return c


def amp(row):
    return np.abs(np.fft.rfft(row.astype(float))) / (N / 2)


def checks(existing, g, tables):
    ok = True
    print("C1 calibration (max |error| LSB per level 0..10; sawTable limit %d):" % C1_LIMIT_LSB)
    for name, ref in existing.items():
        errs = [int(np.max(np.abs(level(ideal(name), g[lv]) - ref[lv]))) for lv in range(LEVELS)]
        gate = name == "sawTable"
        bad = gate and any(e > C1_LIMIT_LSB for e in errs)
        ok &= not bad
        print("   %-8s %s%s" % (name, " ".join("%5d" % e for e in errs),
                                 "  FAIL" if bad else ("" if gate else "  (information)")))
    saw = existing["sawTable"]
    for (_, cname, _), tab in zip(WAVES, tables):
        for lv in range(LEVELS):
            row = tab[lv]
            if int(np.max(np.abs(row))) != 32767 or abs(float(np.mean(row))) >= 1.0:
                print("C2 FAIL %s level %d: peak %d, DC %.3f" %
                      (cname, lv, int(np.max(np.abs(row))), float(np.mean(row))))
                ok = False
            H = LEVEL_H[lv]
            if H < N // 2:
                lim = max(2.0 * float(np.max(amp(saw[lv])[H + 1:])), 0.5)
                got = float(np.max(amp(row)[H + 1:]))
                if got > lim:
                    print("C3 FAIL %s level %d: %.2f LSB above H=%d (limit %.2f)" %
                          (cname, lv, got, H, lim))
                    ok = False
    print("C2 peak/DC and C3 out-of-band: %s" % ("OK" if ok else "see above"))
    return ok


def emit(tables, sha):
    out = []
    out.append("/*")
    out.append(" * Stock-wave octave tables (S086 P2) - GENERATED, do not edit.")
    out.append(" *")
    out.append(" * What:       one [11][1024] band-limited table per wave, in the layout of")
    out.append(" *             sawTable/recTable/triTable (wavetable.c): level n is the")
    out.append(" *             octave below 16.3516 * 2^n Hz (osc_octaveEdgeHz), peak 32767.")
    out.append(" *             Waves: %s." % ", ".join("%s = %s" % (c, d) for _, c, d in WAVES))
    out.append(" * Why:        S086_P2_ADD_STOCK_WAVES_CORRECTLY.md: the new stock waves are")
    out.append(" *             fixed tables in program flash, read by the Tri/Saw/Rec loops.")
    out.append(" * Inputs:     tools/plots/stock_wave_harmonics.csv")
    out.append(" *             SHA-256 %s" % sha)
    out.append(" *             band limit per level measured from sawTable (wavetable.c).")
    out.append(" * Outputs:    the tables below (.rodata, program flash).")
    out.append(" * Accessors:  Oscillator.c osc_stockTables[] (via wavetable.h).")
    out.append(" * Affiliates: tools/gen_stock_tables.py (regenerate: python3")
    out.append(" *             tools/gen_stock_tables.py), tools/dsp_test stock_tables.")
    out.append(" */")
    out.append("")
    out.append('#include "wavetable.h"')
    for (_, cname, dname), tab in zip(WAVES, tables):
        out.append("")
        out.append("/*")
        out.append("---- %s ----" % dname)
        out.append("Number of tables")
        out.append("11")
        out.append("waveform size")
        out.append("1024")
        out.append("*/")
        out.append("")
        out.append("const int16_t %s[11][1024] =" % cname)
        out.append("{")
        for lv in range(LEVELS):
            out.append("")
            out.append("//---- Table %d ----" % lv)
            out.append("// midi note number %d freq: %.4f" % (12 * lv, LEVEL_TOP_HZ[lv]))
            out.append("{")
            row = tab[lv]
            lines = [", ".join("%d" % v for v in row[i:i + 5]) for i in range(0, N, 5)]
            out.append(",\n".join(lines) + ("}," if lv < LEVELS - 1 else "}"))
        out.append("};")
    out.append("")
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=OUT_PATH)
    ap.add_argument("--check", action="store_true",
                    help="compare with the committed file instead of writing")
    args = ap.parse_args()

    existing = read_existing()
    g = taper(existing["sawTable"])
    recipe = read_recipe()
    tables = []
    for wave, cname, _ in WAVES:
        if wave not in recipe:
            sys.exit("gen_stock_tables.py: %s not in the CSV" % wave)
        tables.append(np.array([level(recipe[wave], g[lv]) for lv in range(LEVELS)]))
    print("level H (Nyquist harmonic count):", LEVEL_H)

    ok = checks(existing, g, tables)
    sha = hashlib.sha256(open(CSV_PATH, "rb").read()).hexdigest()
    text = emit(tables, sha)

    if args.check:
        same = os.path.exists(args.out) and open(args.out).read() == text
        print("C4 regenerate == %s: %s" % (os.path.relpath(args.out, ROOT), "OK" if same else "FAIL"))
        ok &= same
    else:
        open(args.out, "w").write(text)
        print("wrote", os.path.relpath(args.out, ROOT))
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
