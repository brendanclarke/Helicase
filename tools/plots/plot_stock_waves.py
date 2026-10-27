#!/usr/bin/env python3
"""Interpretation of the new stock oscillator waves: sq3 sw3 sqa tra swa.

Outputs (in tools/plots/):
  stock_waves_303_analog.png   one cycle of each wave, as the octave tables
                               would hold it (level 0, ~1 kHz, ~4 kHz), next to
                               the reference trace and the stock wave it
                               replaces; plus the harmonic spectra.
  stock_wave_harmonics.csv     the harmonic recipe (k = 1..511: magnitude,
                               phase) the install-time generator would use.

How each wave is made:
  sq3, sw3  digitised from reference/303_square_saw.png (left / right
            panel), cycles averaged, first 40 harmonics kept (the screenshot
            resolves about 90), then extended to 511 with a fitted roll-off.
  tra       digitised from reference/tra_dirty_triangle.png, first 30
            harmonics kept, then extended the same way.
  sqa       a 50 % pulse through an analog-style chain: 2-pole low-pass at the
            40th harmonic with Q 0.9 (edge rounding, slight overshoot) and a
            one-pole high-pass at 0.1 x f0 (droop/tilt on the flat tops).
  swa       a falling ramp with exponential (capacitor-charge) curvature and
            a 1.5 %-of-cycle reset, through the same chain (LP at the 48th
            harmonic, Q 0.75; HP at 0.08 x f0).
Orientation follows the stock tables: saw falls (index 0 = middle of the
reset jump), square is high for the first half, triangle starts at its
trough.
"""
import csv
import math

import numpy as np

import lxr_plotlib as L

N = 4096          # analysis resolution per cycle
H_TABLE = 511     # level-0 table limit (1,024-point tables)


# ----------------------------------------------------------------- models
def biquad_lp(fc_rel, q):
    """RBJ low-pass, fc relative to the sample rate (cycle = N samples)."""
    w0 = 2 * math.pi * fc_rel
    alpha = math.sin(w0) / (2 * q)
    c = math.cos(w0)
    b0, b1, b2 = (1 - c) / 2, 1 - c, (1 - c) / 2
    a0, a1, a2 = 1 + alpha, -2 * c, 1 - alpha
    return [b0 / a0, b1 / a0, b2 / a0], [1.0, a1 / a0, a2 / a0]


def periodic_chain(src, lp_harm, lp_q, hp_harm, periods=12):
    """Run one source cycle repeatedly through LP biquad + 1-pole HP."""
    n = len(src)
    b, a = biquad_lp(lp_harm / n, lp_q)
    k_hp = math.exp(-2 * math.pi * hp_harm / n)
    x1 = x2 = y1 = y2 = 0.0
    hp_x = hp_y = 0.0
    out = np.empty(n)
    for p in range(periods):
        for i, x in enumerate(src):
            y = b[0] * x + b[1] * x1 + b[2] * x2 - a[1] * y1 - a[2] * y2
            x2, x1, y2, y1 = x1, x, y1, y
            h = k_hp * (hp_y + y - hp_x)          # one-pole DC-blocking HP
            hp_x, hp_y = y, h
            if p == periods - 1:
                out[i] = h
    return out


def model_sqa():
    ph = np.arange(N) / N
    src = np.where(ph < 0.5, 1.0, -1.0)
    return periodic_chain(src, 40, 0.9, 0.1)


def model_swa():
    ph = np.arange(N) / N
    r, tau = 0.015, 1.1
    ramp_len = 1 - r
    t = np.clip(ph / ramp_len, 0, 1)
    curved = (1 - np.exp(-t / tau)) / (1 - np.exp(-1 / tau))
    src = 1 - 2 * curved                           # falling, concave
    reset = ph >= ramp_len
    u = (ph[reset] - ramp_len) / r
    src[reset] = -1 + 2 * (1 - np.exp(-5 * u)) / (1 - np.exp(-5))
    return periodic_chain(np.roll(src, int(N * r / 2)), 48, 0.75, 0.08)


def stock(name):
    ph = np.arange(N) / N
    if name == "rec":
        return L.bandlimit(np.where(ph < 0.5, 1.0, -1.0), H_TABLE)
    if name == "saw":                              # sawTable: 0, then +1 falling to -1
        return L.bandlimit(1 - 2 * ph, H_TABLE)
    return L.bandlimit(1 - 4 * np.abs(ph - 0.5), H_TABLE)   # triTable: -1 at 0, +1 at 0.5


def align_rising_edge(cycle):
    """Rotate so index 0 is the zero crossing on the steepest rise."""
    bl = L.bandlimit(cycle, 24)
    i = int(np.argmax(np.roll(bl, -1) - bl))
    # walk back to the zero crossing (value changes sign)
    j = i
    for _ in range(len(cycle)):
        if bl[j - 1] <= 0 < bl[j] or bl[j - 1] < 0 <= bl[j]:
            break
        j = (j - 1) % len(cycle)
    return L.rotate_to(cycle, j)


def resample(cycle, n):
    x = np.arange(len(cycle)) / len(cycle)
    xn = np.arange(n) / n
    return np.interp(xn, x, cycle, period=1.0)


def build():
    waves = {}

    for name, h_meas in (("sq3", 40), ("sw3", 40)):
        cyc, _, _, _ = L.digitise_303(name, n_out=2048)
        cyc = align_rising_edge(L.normalise(resample(cyc, N)))
        coeffs = L.extend_harmonics(L.harmonics_of(cyc), h_meas, H_TABLE)
        table = L.normalise(L.from_harmonics(coeffs, N))
        waves[name] = dict(ref=cyc, coeffs=coeffs, table=table,
                           stock="rec" if name == "sq3" else "saw",
                           note=f"digitised 303 {'square' if name == 'sq3' else 'saw'}; "
                                f"harmonics 1–{h_meas} measured, 1/n^a extension to {H_TABLE}")

    cyc, _, _, _ = L.digitise_dirty_triangle(n_out=2048)
    cyc = L.normalise(resample(cyc, N))
    cyc = L.rotate_to(cyc, int(np.argmin(L.bandlimit(cyc, 12))))
    coeffs = L.extend_harmonics(L.harmonics_of(cyc), 30, H_TABLE)
    waves["tra"] = dict(ref=cyc, coeffs=coeffs, table=L.normalise(L.from_harmonics(coeffs, N)),
                        stock="tri", note="digitised dirty triangle; harmonics 1–30 measured, extended")

    for name, fn, stk, note in (("sqa", model_sqa, "rec", "pulse → 2-pole LP (h40, Q0.9) → HP 0.1·f0"),
                                ("swa", model_swa, "saw", "curved falling ramp (τ 1.1) → 2-pole LP (h48, Q0.75) → HP 0.08·f0")):
        cyc = L.normalise(fn())
        coeffs = L.harmonics_of(cyc)[: H_TABLE + 1]
        waves[name] = dict(ref=None, coeffs=coeffs, table=L.normalise(L.from_harmonics(coeffs, N)),
                           stock=stk, note=note)
    return waves


def main():
    plt = L.setup_matplotlib()
    waves = build()
    order = ["sq3", "sw3", "sqa", "tra", "swa"]
    titles = {"sq3": "sq3 — 303 square", "sw3": "sw3 — 303 saw", "sqa": "sqa — analog square",
              "tra": "tra — analog (dirty) triangle", "swa": "swa — analog saw"}

    fig, axes = plt.subplots(len(order), 2, figsize=(13, 3.0 * len(order)),
                             gridspec_kw={"width_ratios": [1.9, 1]})
    ph = np.arange(N) / N
    notes = [(None, H_TABLE, L.PALETTE[1], "level 0 table (≤ 511 harmonics)"),
             (1000.0, L.harmonics_for_note(1000.0), L.PALETTE[3], "as played at 1 kHz (≤ 22 harmonics)"),
             (4000.0, L.harmonics_for_note(4000.0), L.PALETTE[4], "as played at 4 kHz (≤ 5 harmonics)")]

    for row, name in enumerate(order):
        w = waves[name]
        ax = axes[row, 0]
        ax.plot(ph, stock(w["stock"]), color="#c3cad2", lw=1.4, ls="--",
                label=f"stock {w['stock']} (for comparison)")
        if w["ref"] is not None:
            ax.plot(ph[::12], w["ref"][::12], ".", color=L.PALETTE[6], ms=2.5,
                    label="reference trace (digitised)")
        for f, h, col, lab in notes:
            y = L.from_harmonics(w["coeffs"], N, h)
            y = y / np.max(np.abs(L.from_harmonics(w["coeffs"], N)))
            ax.plot(ph, y, color=col, lw=1.8 if f is None else 1.2, label=lab)
        ax.set_title(titles[name])
        ax.text(0.01, 0.02, w["note"], transform=ax.transAxes, fontsize=7.5, color="#5b6672")
        ax.set_xlim(0, 1)
        ax.set_ylim(-1.3, 1.85)
        ax.set_xlabel("phase (one cycle)")
        ax.legend(loc="upper right", ncol=3)

        ax = axes[row, 1]
        k = np.arange(1, 65)
        ref = np.abs(w["coeffs"][1:65])
        st = np.abs(L.harmonics_of(stock(w["stock"]))[1:65])
        norm = max(np.max(ref), np.max(st))
        ax.plot(k, 20 * np.log10(st / norm + 1e-9), color="#c3cad2", lw=1.4, ls="--", label=f"stock {w['stock']}")
        ax.plot(k, 20 * np.log10(ref / norm + 1e-9), "o-", color=L.PALETTE[1], ms=2.5, lw=1, label=name)
        ax.set_ylim(-70, 3)
        ax.set_xlim(0, 65)
        ax.set_xlabel("harmonic")
        ax.set_ylabel("level (dB)")
        ax.set_title(f"{name} harmonics 1–64")
        ax.legend(loc="upper right")

    fig.suptitle("New stock oscillator waves — interpretation (S084 P1)", fontsize=14, fontweight="bold", y=0.997)
    fig.tight_layout(rect=(0, 0, 1, 0.99))
    fig.savefig(L.out_path("stock_waves_303_analog.png"), dpi=110)

    with open(L.out_path("stock_wave_harmonics.csv"), "w", newline="") as fh:
        wr = csv.writer(fh)
        wr.writerow(["wave", "harmonic", "magnitude", "phase_rad"])
        for name in order:
            c = waves[name]["coeffs"]
            scale = 1.0 / np.max(np.abs(L.from_harmonics(c, N)))
            for kk in range(1, H_TABLE + 1):
                wr.writerow([name, kk, f"{abs(c[kk]) * scale:.6e}", f"{np.angle(c[kk]):.6f}"])
    print("wrote stock_waves_303_analog.png and stock_wave_harmonics.csv")


if __name__ == "__main__":
    main()
