#!/usr/bin/env python3
"""Interpretation of the `pwm` oscillator type (S084 P1).

Output: pwm_families.png

Families (the oscillator `src` row) and how each is computed from tables the
firmware already has or will generate (no width tables are stored):

  prc  default square   difference of two band-limited falling saws, w apart:
                        s(p) - s(p - w). Exactly band-limited. DC-free (like an
                        AC-coupled analog pulse) and scaled so its peak is 1
                        (one division per block).
  pq3  303 square       the sq3 table read through a two-slope phase warp:
                        its natural split point s0 (hump | dip) moves to w.
  ptr  default triangle the triangle table through the same warp (s0 = 0.5):
                        triangle -> skewed -> ramp.
  psw  default saw      sum of two band-limited saws d apart, d 0 -> 0.5:
                        saw -> "phasing" saw -> saw an octave up. Exactly
                        band-limited; scaled by 1/(1-d) to keep the peak at 1.
  pw3  303 saw          the sw3 table through the phase warp.
  swp  sweep            one control sweeps sine -> tri -> saw -> square ->
                        narrow pulse: a crossfade of two aligned band-limited
                        reads, then the prc pulse narrowing from 50 % to 4 %.

Warped families pick the mip level for the warp's worst compression
(max(s0/w, (1-s0)/(1-w))), so a narrow width never aliases: the cost is a
duller sound at extreme widths. Curves are drawn as played at 220 Hz.
"""
import numpy as np

import lxr_plotlib as L
from plot_stock_waves import N, build

F_NOTE = 220.0
CTRL = [0.0, 0.33, 0.66, 1.0]


def saw_bl(ph, h):
    """Band-limited falling saw (sawTable orientation) evaluated at phases ph."""
    k = np.arange(1, h + 1)
    return (2 / np.pi) * np.sum(np.sin(2 * np.pi * np.outer(ph, k)) / k, axis=1)


def table_from_coeffs(coeffs, h):
    return L.from_harmonics(coeffs, N, h)


def read(table, ph):
    x = np.arange(len(table)) / len(table)
    return np.interp(ph % 1.0, x, table, period=1.0)


def warp(ph, s0, w):
    ph = ph % 1.0
    return np.where(ph < w, ph * s0 / w, s0 + (ph - w) * (1 - s0) / (1 - w))


def warped_family(coeffs, s0, w_end, c, ph):
    w = s0 + (w_end - s0) * c
    comp = max(s0 / w, (1 - s0) / (1 - w))
    h = L.harmonics_for_note(F_NOTE, comp)
    tab = table_from_coeffs(coeffs, h)
    tab = tab / np.max(np.abs(table_from_coeffs(coeffs, L.harmonics_for_note(F_NOTE))))
    return read(tab, warp(ph, s0, w)), w, h


def pulse(ph, w, h, shift=0.0):
    p = saw_bl(ph - shift, h) - saw_bl(ph - shift - w, h)   # levels 2(1-w), -2w
    return p / (2 * max(w, 1 - w))


def main():
    plt = L.setup_matplotlib()
    waves = build()
    ph = np.linspace(0, 2, 1600, endpoint=False)          # two cycles
    h_note = L.harmonics_for_note(F_NOTE)
    ph_tab = np.arange(N) / N
    tri_coeffs = L.harmonics_of(L.bandlimit(1 - 4 * np.abs(ph_tab - 0.5), 511))

    fig, ax = plt.subplots(3, 2, figsize=(13, 12))
    cols = [L.PALETTE[1], L.PALETTE[2], L.PALETTE[3], L.PALETTE[4]]

    a = ax[0, 0]
    for c, colr in zip(CTRL, cols):
        w = 0.5 + (0.04 - 0.5) * c
        a.plot(ph, pulse(ph, w, h_note), color=colr, label=f"wav {round(c * 127):3d}: width {w * 100:.0f} %")
    a.set_title("prc — default square PWM (saw difference, exact band-limit)")

    a = ax[0, 1]
    for c, colr in zip(CTRL, cols):
        d = 0.5 * c
        y = 0.5 * (saw_bl(ph, h_note) + saw_bl(ph - d, h_note))
        a.plot(ph, y / (1 - d), color=colr, label=f"wav {round(c * 127):3d}: offset {d:.2f} cycle")
    a.set_title("psw — default saw PWM (sum of two saws, exact band-limit)")

    for a, name, coeffs, s0, w_end, title in (
            (ax[1, 0], "tri", tri_coeffs, 0.5, 0.03, "ptr — default triangle PWM (phase warp, s0 0.50)"),
            (ax[1, 1], "sq3", waves["sq3"]["coeffs"], 0.78, 0.12, "pq3 — 303 square PWM (phase warp, s0 0.78)"),
            (ax[2, 0], "sw3", waves["sw3"]["coeffs"], 0.42, 0.04, "pw3 — 303 saw PWM (phase warp, s0 0.42)")):
        for c, colr in zip(CTRL, cols):
            y, w, h = warped_family(coeffs, s0, w_end, c, ph)
            a.plot(ph, y, color=colr, label=f"wav {round(c * 127):3d}: split at {w:.2f}, ≤ {h} harmonics")
        a.set_title(title)

    a = ax[2, 1]
    pos = [0, 0.5, 1, 1.5, 2, 2.5, 3, 3.5, 4]
    cmap = plt.get_cmap("viridis")
    sine = np.sin(2 * np.pi * ph)
    tri = read(table_from_coeffs(tri_coeffs, h_note), ph + 0.25)
    saw = saw_bl(ph - 0.25, h_note)
    sq = pulse(ph, 0.5, h_note, shift=0.25)
    shapes = [sine, tri, saw, sq]
    names = ["sine", "tri", "saw", "square", "pulse 4 %"]
    for i, p in enumerate(pos):
        seg = min(int(p), 3)
        t = p - seg
        if seg < 3:
            y = shapes[seg] + t * (shapes[seg + 1] - shapes[seg])
        else:
            y = pulse(ph, 0.5 - 0.46 * t, h_note, shift=0.25)
        lab = names[int(p)] if p == int(p) else None
        a.plot(ph, y, color=cmap(i / (len(pos) - 1)), lw=1.8 if lab else 1.0,
               label=f"wav {round(p / 4 * 127):3d}: {lab}" if lab else None)
    a.set_title("swp — sine → tri → saw → square → pulse (one control)")

    for a in ax.flat:
        a.set_xlim(0, 2)
        a.set_ylim(-1.35, 1.75)
        a.set_xlabel("phase (two cycles), as played at 220 Hz")
        a.legend(loc="upper right", ncol=2)
    fig.suptitle("pwm oscillator type — families as computed (S084 P1)", fontsize=14, fontweight="bold")
    fig.tight_layout(rect=(0, 0, 1, 0.975))
    fig.savefig(L.out_path("pwm_families.png"), dpi=110)
    print("wrote pwm_families.png")


if __name__ == "__main__":
    main()
