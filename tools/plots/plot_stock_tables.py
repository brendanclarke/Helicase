#!/usr/bin/env python3
"""Review plot of the generated stock-wave octave tables (S086 P2).

What:       reads the tables the device plays (Core/DSPAudio/wavetableStock.c,
            written by tools/gen_stock_tables.py) and the existing
            sawTable/recTable (wavetable.c), and plots for each new wave:
              left   one cycle at levels 0, 4, 8 and 10, with its stock
                     counterpart (Re3 -> Rec, Sw3 -> Saw) at level 0 dashed;
              right  the harmonic levels (dB re. the fundamental) of each
                     level, the counterpart's in grey, and each level's
                     Nyquist harmonic count H as a tick.
Why:        S086_P2_ADD_STOCK_WAVES_CORRECTLY.md §3.2: the review picture of
            what the firmware will play, level by level.
Inputs:     Core/DSPAudio/wavetableStock.c, Core/DSPAudio/wavetable.c.
Outputs:    tools/plots/stock_tables_levels.png.
Accessors:  python3 tools/plots/plot_stock_tables.py (numpy, matplotlib).
Affiliates: tools/gen_stock_tables.py (WAVES, LEVEL_H, the table parser).
"""
import os
import re
import sys

import numpy as np

import lxr_plotlib as L

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
import gen_stock_tables as G  # noqa: E402

COUNTERPART = {"sq3": "recTable", "sw3": "sawTable", "sqa": "recTable",
               "tra": "triTable", "swa": "sawTable"}
SHOW_LEVELS = (0, 4, 8, 10)


def read_generated():
    text = open(G.OUT_PATH).read()
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    text = re.sub(r"//[^\n]*", "", text)
    return {cname: G.read_table(cname, text) for _, cname, _ in G.WAVES}


def harm_db(row, kmax):
    X = np.abs(np.fft.rfft(row.astype(float)))
    return 20 * np.log10(np.maximum(X[1:kmax + 1], 1e-9) / X[1])


def main():
    plt = L.setup_matplotlib()
    new = read_generated()
    old = G.read_existing()
    rows = len(G.WAVES)
    fig, axes = plt.subplots(rows, 2, figsize=(13, 3.6 * rows),
                             gridspec_kw={"width_ratios": [1.6, 1]})
    axes = np.atleast_2d(axes)
    ph = np.arange(G.N) / G.N
    cols = [L.PALETTE[1], L.PALETTE[2], L.PALETTE[3], L.PALETTE[4]]

    for r, (wave, cname, dname) in enumerate(G.WAVES):
        tab = new[cname]
        ref = old[COUNTERPART[wave]]
        ax = axes[r, 0]
        ax.plot(ph, ref[0] / 32767.0, color="#c3cad2", lw=1.4, ls="--",
                label="%s level 0" % COUNTERPART[wave].replace("Table", "").capitalize())
        for c, lv in zip(cols, SHOW_LEVELS):
            ax.plot(ph, tab[lv] / 32767.0, color=c, lw=1.6 if lv == 0 else 1.1,
                    label="level %d (H %d)" % (lv, G.LEVEL_H[lv]))
        ax.set_title("%s (%s) - levels as the oscillator reads them" % (dname, cname))
        ax.set_xlim(0, 1)
        ax.set_ylim(-1.15, 1.45)
        ax.set_xlabel("table index / 1024")
        ax.legend(loc="upper right", ncol=3)

        ax = axes[r, 1]
        kmax = 64
        k = np.arange(1, kmax + 1)
        for c, lv in zip(cols, SHOW_LEVELS[:3]):
            ax.plot(k, harm_db(ref[lv], kmax), color="#c3cad2", lw=1.0)
            ax.plot(k, harm_db(tab[lv], kmax), color=c, lw=1.2, marker="o", ms=2,
                    label="level %d" % lv)
            if G.LEVEL_H[lv] <= kmax:
                ax.axvline(G.LEVEL_H[lv], color=c, lw=0.8, ls=":")
        ax.set_ylim(-80, 5)
        ax.set_xlim(0, kmax + 1)
        ax.set_xlabel("harmonic")
        ax.set_ylabel("dB re. fundamental")
        ax.set_title("%s harmonics 1-%d (grey: %s, same levels)" %
                     (dname, kmax, COUNTERPART[wave].replace("Table", "")))
        ax.legend(loc="upper right")

    fig.suptitle("Stock-wave octave tables (S086 P2, wavetableStock.c)",
                 fontsize=14, fontweight="bold", y=0.995)
    fig.tight_layout(rect=(0, 0, 1, 0.98))
    fig.savefig(L.out_path("stock_tables_levels.png"), dpi=110)
    print("wrote stock_tables_levels.png")


if __name__ == "__main__":
    main()
