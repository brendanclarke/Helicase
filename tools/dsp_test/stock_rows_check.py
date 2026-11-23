#!/usr/bin/env python3
"""Law STOCK-ROWS (S101): the firmware's combined stock-wave array.

What:       Core/DSPAudio/wavetable.c's stockTableRows[31][1024] must equal,
            byte for byte and in this order: triTable levels 0..10,
            recTable levels 1..10, sawTable levels 1..10 of the archived
            pre-S101 tables (tools/plots/archive/stock_tables_s100.c).
Why:        S101 F3 dropped level 0 of sawTable/recTable (unreachable: the
            octave index is clamped to 1). The host benches and generators
            read the archive's whole [11][1024] tables; this law ties them to
            the firmware's data for every reachable level.
Inputs:     --fw wavetable.c, --archive stock_tables_s100.c; --break swaps two
            firmware rows first (the break proof: must fail).
Outputs:    one line; exit 1 on a mismatch.
Accessors:  tools/dsp_test/Makefile stock_rows.
"""
import argparse
import re
import sys


def strip(text):
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def array(text, name, levels):
    m = re.search(name + r"\[[A-Z_0-9u]+\]\[1024\]\s*=\s*\{(.*?)\n\};", text, re.S)
    if not m:
        sys.exit("stock_rows_check.py: %s not found" % name)
    v = [int(x) for x in re.findall(r"-?\d+", m.group(1))]
    if len(v) != levels * 1024:
        sys.exit("stock_rows_check.py: %s has %d values" % (name, len(v)))
    return [v[i * 1024:(i + 1) * 1024] for i in range(levels)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--fw", required=True)
    ap.add_argument("--archive", required=True)
    ap.add_argument("--break", dest="brk", action="store_true")
    a = ap.parse_args()
    fw = array(strip(open(a.fw).read()), "stockTableRows", 31)
    arc = strip(open(a.archive).read())
    tri, rec, saw = (array(arc, n, 11) for n in ("triTable", "recTable", "sawTable"))
    want = tri[0:11] + rec[1:11] + saw[1:11]
    if a.brk:
        fw[11], fw[12] = fw[12], fw[11]
    bad = [i for i in range(31) if fw[i] != want[i]]
    if bad:
        print("STOCK-ROWS FAIL: stockTableRows rows %s differ from the archive" % bad)
        sys.exit(1)
    print("STOCK-ROWS: stockTableRows = tri 0..10 + rec 1..10 + saw 1..10 of the archive (31 rows, exact)")


if __name__ == "__main__":
    main()
