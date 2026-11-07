#!/usr/bin/env python3
"""Proposed raw (unfiltered) TB-303 VCO waves against today's Re3/Sw3 (S092 P0).

What:       models the 303 VCO saw and the Q8 transistor-shaped square, and
            plots them beside the Re3/Sw3 tables the firmware plays now
            (Core/DSPAudio/wavetableStock.c), all at the octave level that
            plays C2 (65.4 Hz, the proposed Alkaline root) and band-limited
            with the same per-level taper (measured from sawTable).
              row 1  saw: one cycle + harmonics 1-64
              row 2  square: one cycle + harmonics 1-64
              row 3  how the square is made from the saw (Q8 knee spread),
                     and Sw3 fitted as "raw saw through a resonant low-pass"
              row 4  three cycles at C2 as the VCF input would receive them
                     (assumed coupling high-pass)
Why:        S092_P0_ALKALINE_INSTRUMENT_PLAN.md §2: Re3/Sw3 were digitised
            from a 303's filtered output, so they cannot serve as the
            Alkaline VCO. These are MODELS from circuit reasoning, not
            measurements; the open constants are marked in the figure.
Inputs:     Core/DSPAudio/wavetableStock.c, Core/DSPAudio/wavetable.c.
Outputs:    tools/plots/303_raw_vco_vs_stock.png.
Accessors:  python3 tools/plots/plot_303_raw_vco.py (numpy, matplotlib).
Affiliates: plot_stock_waves.py (how Re3/Sw3 were made), gen_stock_tables.py.
"""
import os
import re

import numpy as np

import lxr_plotlib as L

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
DSP = os.path.join(ROOT, "Core", "DSPAudio")

N = 1024
F0 = 65.406                     # C2, MIDI 36 (proposed Alkaline coarse default)
LEVEL = 3                       # freqToTableIndex(): edges <= f0 are 16.35, 32.70, 65.406
H = int((L.FS / 2) / (16.3515987 * 2 ** LEVEL))   # 168 harmonics on level 3

# Open model constants (S092 P0 §3.2, "verify").
RESET_US = 8.0                  # saw discharge time
Q8_GAIN = (5.0, 10.0, 20.0)     # slope of the shaper's active region (edge width = 1/G cycle)
Q8_GAIN_MID = 10.0
Q8_VTH = 0.05                   # switching point on the saw (sets the duty, ~47.5 %)
KNEE = 0.02                     # corner rounding of cut-off / saturation
COUPLING_HZ = (10.0, 40.0)      # assumed VCF input coupling high-pass (bracket)


# ----------------------------------------------------------------- tables
def read_table(name, path):
    text = open(path).read()
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    text = re.sub(r"//[^\n]*", "", text)
    m = re.search(name + r"\[11\]\[1024\]\s*=\s*\{(.*?)\};", text, re.S)
    v = np.array([int(x) for x in re.findall(r"-?\d+", m.group(1))], dtype=float)
    return v.reshape(11, N)


def level_taper(saw_level):
    """Per-harmonic gain of one sawTable level relative to an ideal saw."""
    X = np.abs(np.fft.rfft(saw_level))
    k = np.arange(len(X))
    g = X * k / X[1]
    g[0] = 0.0
    return g


def as_played(cycle, taper):
    """Band-limit a model cycle the way the table generator does."""
    c = np.fft.rfft(cycle) * taper[: N // 2 + 1]
    c[H + 1:] = 0.0
    c[0] = 0.0
    return L.normalise(np.fft.irfft(c, N))


# ----------------------------------------------------------------- models
def saw_model(n=N * 16):
    """Linear capacitor ramp with a short reset; sawTable orientation
    (falling, index 0 = middle of the reset)."""
    ph = np.arange(n) / n
    r = RESET_US * 1e-6 * F0
    ph = (ph + r / 2) % 1.0
    return np.where(ph < r, -1 + 2 * ph / r, 1 - 2 * (ph - r) / (1 - r))


def smooth_clip(x):
    return 0.5 * (np.sqrt((x + 1) ** 2 + KNEE) - np.sqrt((x - 1) ** 2 + KNEE))


def square_model(gain, n=N * 16):
    """Q8: cut off below the switching point, a linear active region that
    follows the saw ramp, then saturation. The reset edge stays sharp; the
    threshold edge has the ramp's slope."""
    s = saw_model(n)
    return smooth_clip(gain * (s - Q8_VTH) + 1.0) * 1.0


def down(cycle):
    return cycle.reshape(N, -1).mean(axis=1)


def harm_db(cycle, kmax=64, ref=None):
    X = np.abs(np.fft.rfft(cycle))[1:kmax + 1]
    return 20 * np.log10(np.maximum(X, 1e-9) / (ref if ref else X[0]))


def coupling(cycle, corner_hz, cycles=8):
    """One-pole high-pass at corner_hz, run to steady state."""
    fs = F0 * N
    a = np.exp(-2 * np.pi * corner_hz / fs)
    x1 = y1 = 0.0
    out = np.empty(N * 3)
    seq = np.tile(cycle, cycles)
    for i, x in enumerate(seq):
        y = a * (y1 + x - x1)
        x1, y1 = x, y
        if i >= len(seq) - N * 3:
            out[i - (len(seq) - N * 3)] = y
    return out


def fit_filtered_saw(target_db, raw_db):
    """Fit Sw3 harmonics 1..40 as raw saw x one extra low-pass pole. A 2-pole
    resonant, 3-pole or ladder fit does no better (all ~5.8 dB RMS, Q driven
    to the grid floor): the residual is digitisation scatter, so the
    magnitudes show the extra roll-off but cannot show resonance."""
    k = np.arange(1, 41)
    best = None
    for kc in np.linspace(1, 40, 157):
        m = raw_db[:40] + 20 * np.log10(np.abs(1 / (1 + 1j * k / kc)))
        m -= m[0] - target_db[0]
        err = np.sqrt(np.mean((m - target_db[:40]) ** 2))
        if best is None or err < best[0]:
            best = (err, kc, m)
    return best


# ------------------------------------------------------------------ plot
def main():
    plt = L.setup_matplotlib()
    saw_t = read_table("sawTable", os.path.join(DSP, "wavetable.c"))
    rec_t = read_table("recTable", os.path.join(DSP, "wavetable.c"))
    sq3_t = read_table("sq3Table", os.path.join(DSP, "wavetableStock.c"))
    sw3_t = read_table("sw3Table", os.path.join(DSP, "wavetableStock.c"))
    taper = level_taper(saw_t[LEVEL])

    raw_saw = down(saw_model())
    raw_sq = {g: down(square_model(g)) for g in Q8_GAIN}
    saw_p = as_played(raw_saw, taper)
    sq_p = {g: as_played(c, taper) for g, c in raw_sq.items()}
    sw3 = L.normalise(sw3_t[LEVEL])
    sq3 = L.normalise(sq3_t[LEVEL])
    saw_stock = L.normalise(saw_t[LEVEL])
    rec_stock = L.normalise(rec_t[LEVEL])
    ph = np.arange(N) / N

    col_raw, col_cur, col_stock, col_alt = L.PALETTE[1], L.PALETTE[4], "#c3cad2", L.PALETTE[2]
    fig, axes = plt.subplots(4, 2, figsize=(13.5, 15.5),
                             gridspec_kw={"width_ratios": [1.8, 1]})

    def rms_db(x):
        return 20 * np.log10(np.sqrt(np.mean(x ** 2)))

    # ---- row 1: saw
    ax = axes[0, 0]
    ax.plot(ph, saw_stock, color=col_stock, lw=1.4, ls="--", label="stock Saw (sawTable)")
    ax.plot(ph, sw3, color=col_cur, lw=1.6, label="current Sw3 (sw3Table)")
    ax.plot(ph, saw_p, color=col_raw, lw=2.0, label="proposed raw 303 saw")
    ax.set_title(f"Saw — one cycle as played at C2 ({F0:.1f} Hz, table level {LEVEL}, ≤ {H} harmonics)")
    ax.text(0.01, 0.03,
            f"raw saw = linear capacitor ramp, {RESET_US:.0f} µs reset (invisible at this pitch). "
            f"RMS at equal peak: raw {rms_db(saw_p):+.1f} dB, Sw3 {rms_db(sw3):+.1f} dB",
            transform=ax.transAxes, fontsize=7.5, color="#5b6672")
    ax.set_xlim(0, 1); ax.set_ylim(-1.35, 1.6); ax.set_xlabel("phase (one cycle)")
    ax.legend(loc="upper right", ncol=3)

    ax = axes[0, 1]
    k = np.arange(1, 65)
    ax.plot(k, harm_db(saw_p), "o-", color=col_raw, ms=2.5, lw=1, label="raw 303 saw (≈ 1/k)")
    ax.plot(k, harm_db(sw3), "o-", color=col_cur, ms=2.5, lw=1, label="Sw3")
    ax.set_title("Saw harmonics 1–64 (dB re. fundamental)")
    ax.set_ylim(-75, 5); ax.set_xlim(0, 65); ax.set_xlabel("harmonic"); ax.set_ylabel("dB")
    ax.legend(loc="upper right")

    # ---- row 2: square
    ax = axes[1, 0]
    ax.plot(ph, rec_stock, color=col_stock, lw=1.4, ls="--", label="stock Rec (recTable)")
    ax.plot(ph, sq3, color=col_cur, lw=1.6, label="current Re3 (sq3Table)")
    ax.fill_between(ph, np.minimum(sq_p[Q8_GAIN[0]], sq_p[Q8_GAIN[-1]]),
                    np.maximum(sq_p[Q8_GAIN[0]], sq_p[Q8_GAIN[-1]]),
                    color=col_raw, alpha=0.15, lw=0,
                    label=f"model spread, Q8 gain {Q8_GAIN[0]:.0f}–{Q8_GAIN[-1]:.0f}")
    ax.plot(ph, sq_p[Q8_GAIN_MID], color=col_raw, lw=2.0, label="proposed raw 303 square")
    ax.annotate("sharp edge:\nthe saw reset", xy=(0.004, 0.0), xytext=(0.06, -0.55),
                fontsize=8, arrowprops=dict(arrowstyle="->", color="#5b6672"), color="#5b6672")
    ax.annotate("sloped edge: Q8's active\nregion follows the ramp", xy=(0.5, 0.0),
                xytext=(0.58, 0.45), fontsize=8,
                arrowprops=dict(arrowstyle="->", color="#5b6672"), color="#5b6672")
    ax.set_title("Square — one cycle as played at C2 (same level and taper)")
    ax.text(0.01, 0.03,
            f"raw square = saw → Q8 (cut-off | linear active region | saturation), switch point "
            f"{Q8_VTH:+.2f} → duty ≈ {50 * (1 - Q8_VTH):.1f} %. RMS at equal peak: raw "
            f"{rms_db(sq_p[Q8_GAIN_MID]):+.1f} dB, Re3 {rms_db(sq3):+.1f} dB",
            transform=ax.transAxes, fontsize=7.5, color="#5b6672")
    ax.set_xlim(0, 1); ax.set_ylim(-1.35, 1.6); ax.set_xlabel("phase (one cycle)")
    ax.legend(loc="upper right", ncol=2)

    ax = axes[1, 1]
    for g, a in zip(Q8_GAIN, (0.35, 1.0, 0.35)):
        ax.plot(k, harm_db(sq_p[g]), "o-" if g == Q8_GAIN_MID else "-", color=col_raw,
                alpha=a, ms=2.5, lw=1, label=f"raw square, gain {g:.0f}")
    ax.plot(k, harm_db(sq3), "o-", color=col_cur, ms=2.5, lw=1, label="Re3")
    ax.set_title("Square harmonics 1–64 (dB re. fundamental)")
    ax.set_ylim(-75, 5); ax.set_xlim(0, 65); ax.set_xlabel("harmonic"); ax.set_ylabel("dB")
    ax.legend(loc="upper right", fontsize=7)

    # ---- row 3: square construction, and Sw3 as a filtered saw
    ax = axes[2, 0]
    fine = np.arange(N * 16) / (N * 16)
    s = saw_model()
    ax.plot(fine, s, color=col_stock, lw=1.4, label="saw at Q8 input")
    ax.axhline(Q8_VTH, color="#5b6672", lw=0.8, ls=":")
    ax.text(0.80, Q8_VTH + 0.05, "switching point", fontsize=7.5, color="#5b6672")
    for g, a in zip(Q8_GAIN, (0.45, 1.0, 0.45)):
        ax.plot(fine, square_model(g), color=col_raw, alpha=a, lw=1.8 if g == Q8_GAIN_MID else 1.1,
                label=f"Q8 output, gain {g:.0f} (edge ≈ {100 / g:.0f} % of the cycle)")
    ax.set_title("How the 303 square comes from the saw (unbanded, before the table level)")
    ax.set_xlim(0, 1); ax.set_ylim(-1.35, 1.6); ax.set_xlabel("phase (one cycle)")
    ax.legend(loc="upper right", fontsize=7.5, ncol=2)

    ax = axes[2, 1]
    raw_db = harm_db(L.normalise(raw_saw), 64)
    sw3_db = harm_db(sw3, 64)
    err, kc, m = fit_filtered_saw(sw3_db, raw_db)
    kk = np.arange(1, 41)
    ax.plot(k, sw3_db, "o", color=col_cur, ms=3, label="Sw3")
    ax.plot(kk, m, "-", color=col_alt, lw=1.6,
            label=f"raw saw → one extra LP pole at ≈ {kc:.1f}·f0\n"
                  f"(≈ −12 dB/oct overall; resonant fits no better)")
    ax.plot(k, raw_db, "-", color=col_raw, lw=1, alpha=0.6, label="raw saw")
    ax.axvline(40, color="#5b6672", lw=0.8, ls=":")
    ax.text(41, -8, "above h40: fitted\nextension, not data", fontsize=7, color="#5b6672")
    ax.set_title(f"Sw3 = a low-passed saw (scatter {err:.1f} dB)")
    ax.set_ylim(-75, 5); ax.set_xlim(0, 65); ax.set_xlabel("harmonic"); ax.set_ylabel("dB")
    ax.legend(loc="lower left", fontsize=7.5)

    # ---- row 4: what the VCF input sees
    ph3 = np.arange(N * 3) / N
    for col, (name, raw, cur) in enumerate((("Saw", saw_p, sw3),
                                            ("Square", sq_p[Q8_GAIN_MID], sq3))):
        ax = axes[3, 0] if col == 0 else axes[3, 1]
        ax.plot(ph3, np.tile(raw, 3), color=col_stock, lw=1.2, ls="--", label=f"raw {name.lower()} (DC-coupled)")
        ax.plot(ph3, coupling(raw, COUPLING_HZ[0]), color=col_raw, lw=1.1, alpha=0.45,
                label=f"after {COUPLING_HZ[0]:.0f} Hz coupling HP")
        ax.plot(ph3, coupling(raw, COUPLING_HZ[1]), color=col_raw, lw=1.8,
                label=f"after {COUPLING_HZ[1]:.0f} Hz coupling HP")
        ax.plot(ph3, np.tile(cur, 3), color=col_cur, lw=1.0, alpha=0.8,
                label="current Re3" if name == "Square" else "current Sw3")
        ax.set_title(f"{name} at the VCF input, C2 (corner assumed)")
        ax.set_xlim(0, 3); ax.set_xlabel("cycles")
        ax.legend(loc="lower center", fontsize=7, ncol=2)
        ax.set_ylim(-2.1, 1.9)

    fig.suptitle("Raw TB-303 VCO (model) vs current Re3 / Sw3 — S092 P0", fontsize=14,
                 fontweight="bold", y=0.997)
    fig.text(0.5, 0.004,
             "Models from circuit reasoning, not measurements. Open: Q8 gain and switching point, "
             "saw reset time and curvature, coupling corner, saw/square relative level. "
             "All traces peak-normalised for shape.",
             ha="center", fontsize=8, color="#5b6672")
    fig.tight_layout(rect=(0, 0.012, 1, 0.99))
    fig.savefig(L.out_path("303_raw_vco_vs_stock.png"), dpi=110)
    print(f"wrote 303_raw_vco_vs_stock.png  (Sw3 fit: one pole at {kc:.2f} f0, scatter {err:.2f} dB)")


if __name__ == "__main__":
    main()
