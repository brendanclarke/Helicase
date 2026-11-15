#!/usr/bin/env python3
"""S094 P4 PWM investigation plots, from the shipped firmware render.

Input:  tools/dsp_test/build/pwm_dump.csv (make -C tools/dsp_test pwm_dump):
        the real osc_pwmRenderPlain() on the real stock tables, every pos
        0..127, one cycle = 512 samples (about 86 Hz, octave row 2), plus the
        stock rows and the Alkaline 303 square row.
Output: s094p4_pwm_shapes.png   one cycle per wave at nine pos values
        s094p4_pwm_harmonics.png harmonic level against pos (every pos), and
                                 the p / 128 - p check (S094 P4: not a mirror)
        s094p4_alk_waves.png     Alkaline wav (Saw, 303 Sqr, Tri) and pwm
                                 (the D form, and Tri's WT form) beside the
                                 four stock waves
        s094p4_pwm_law.png       S094 P1 (old) against S094 P4 (implemented)
Prints: the numbers quoted in S094_P4_PWM_OSC_INVESTIGATION.md.
"""
import csv
import os
from collections import defaultdict

import numpy as np

import lxr_plotlib as L
import gen_pwm_tables as G

HERE = os.path.dirname(os.path.abspath(__file__))
CSV = os.path.join(HERE, "..", "dsp_test", "build", "pwm_dump.csv")
CYCLE = 512
WAVES = ["Sin", "Tri", "Saw", "Rec"]
FORM = {"Sin": "warp (WS) on the sine",
        "Tri": "warp (WT) on the triangle row",
        "Saw": "two-read sum (D): saw + delayed saw",
        "Rec": "two-read difference (D): the pulse"}
SHOW_POS = [0, 16, 32, 48, 64, 80, 96, 112, 127]
NH = 24


def load():
    pwm = defaultdict(lambda: np.zeros(2 * CYCLE))
    rows = defaultdict(lambda: np.zeros(2 * CYCLE))
    with open(CSV) as f:
        for r in csv.DictReader(f):
            i, v = int(r["i"]), int(r["value"])
            if r["kind"] == "pwm":
                pwm[(r["wave"], int(r["pos"]))][i] = v
            else:
                rows[(r["kind"], r["wave"])][i] = v
    return pwm, rows


def harmonics(x):
    """|H_n| of one exact cycle, n = 0..NH, as a fraction of full scale."""
    X = np.fft.rfft(x[:CYCLE]) / (CYCLE / 2) / 32767.0
    return np.abs(X[:NH + 1])


def db(a):
    return 20 * np.log10(np.maximum(a, 1e-6))


def main():
    plt = L.setup_matplotlib()
    pwm, rows = load()
    t = np.arange(CYCLE) / CYCLE

    # ---- 1. shapes ------------------------------------------------------
    fig, ax = plt.subplots(4, len(SHOW_POS), figsize=(17, 8.2), sharex=True,
                           sharey=True)
    for r, w in enumerate(WAVES):
        for c, p in enumerate(SHOW_POS):
            a = ax[r][c]
            y = pwm[(w, p)][:CYCLE] / 32767.0
            colour = L.PALETTE[1] if p != 0 else L.PALETTE[3]
            a.plot(t, y, color=colour, lw=1.2)
            a.axhline(0, color="#9aa3ad", lw=0.6)
            if w == "Rec":
                duty = float(np.mean(y > 0))
                a.set_title(f"pos {p}: high {duty:.0%}", fontsize=8)
            else:
                a.set_title(f"pos {p}", fontsize=8)
            if c == 0:
                a.set_ylabel(f"{w}\n{FORM[w]}", fontsize=7.5)
            a.set_ylim(-1.15, 1.15)
            a.set_xticks([0, 0.5, 1])
    fig.suptitle("typ pwm, S094 P4: one cycle at nine pos values "
                 "(firmware render, octave row 2; pos 0, the unmodified "
                 "wave, highlighted)",
                 fontweight="bold")
    fig.tight_layout()
    fig.savefig(L.out_path("s094p4_pwm_shapes.png"), dpi=110)
    plt.close(fig)

    # ---- 2. harmonics against pos, and the mirror ------------------------
    H = {w: np.array([harmonics(pwm[(w, p)]) for p in range(128)])
         for w in WAVES}
    fig, ax = plt.subplots(2, 4, figsize=(17, 7.6))
    for c, w in enumerate(WAVES):
        a = ax[0][c]
        img = db(H[w][:, 1:NH + 1]).T
        im = a.imshow(img, aspect="auto", origin="lower", cmap="magma",
                      vmin=-60, vmax=0, extent=[-0.5, 127.5, 0.5, NH + 0.5])
        a.axvline(0, color="white", lw=0.8, ls="--")
        a.set_title(f"{w}: harmonic level (dB FS) against pos", fontsize=9)
        a.set_xlabel("pos")
        if c == 0:
            a.set_ylabel("harmonic")
        fig.colorbar(im, ax=a, fraction=0.046, pad=0.02)
        b = ax[1][c]
        for k, (p, q) in enumerate([(16, 112), (32, 96), (48, 80)]):
            n = np.arange(1, NH + 1)
            b.plot(n, db(H[w][p, 1:]), "o-", ms=3, lw=1,
                   color=L.PALETTE[1 + k], label=f"pos {p}")
            b.plot(n, db(H[w][q, 1:]), "x--", ms=4, lw=0.8,
                   color=L.PALETTE[1 + k], label=f"pos {q}")
        b.set_ylim(-70, 3)
        b.set_xlabel("harmonic")
        if c == 0:
            b.set_ylabel("dB FS")
        b.set_title(f"{w}: pos p (o) against 128 - p (x), not a mirror",
                    fontsize=9)
        b.legend(ncol=3, fontsize=6.5)
    fig.suptitle("S094 P4 law: the harmonic content is monotonic in pos; "
                 "S094 P1's p / 128 - p mirror is gone", fontweight="bold")
    fig.tight_layout()
    fig.savefig(L.out_path("s094p4_pwm_harmonics.png"), dpi=110)
    plt.close(fig)

    # ---- 3. Alkaline and the stock waves ---------------------------------
    fig, ax = plt.subplots(2, 4, figsize=(15, 6.4), sharex=True, sharey=True)
    for c, w in enumerate(WAVES):
        a = ax[0][c]
        a.plot(t, rows[("wav", w)][:CYCLE] / 32767.0, color=L.PALETTE[0], lw=1.2)
        on = w in ("Saw", "Tri")
        a.set_title(f"stock {w} (typ wav)" + ("  — Alkaline: yes" if on else
                    "  — Alkaline: no"), fontsize=9)
        a.axhline(0, color="#9aa3ad", lw=0.6)
    a = ax[1][0]
    a.plot(t, rows[("alk", "Sqr")][:CYCLE] / 32767.0, color=L.PALETTE[4], lw=1.2)
    a.set_title("Alkaline Sqr (typ wav): the 303 square table", fontsize=9)
    a.text(0.02, 0.95, "Tri (S094 P4): the stock triangle rows",
           transform=a.transAxes, fontsize=7.5, va="top")
    a.axhline(0, color="#9aa3ad", lw=0.6)
    for c, (w, lab) in enumerate([("Saw", "Alkaline pwm Saw"),
                                  ("Rec", "Alkaline pwm Sqr")], start=1):
        a = ax[1][c]
        for k, p in enumerate([16, 64, 112]):
            a.plot(t, pwm[(w, p)][:CYCLE] / 32767.0, lw=1.1,
                   color=L.PALETTE[1 + k], label=f"pos {p}")
        a.set_title(f"{lab} (the D form, pre-ladder)", fontsize=9)
        a.legend(fontsize=7)
        a.axhline(0, color="#9aa3ad", lw=0.6)
    ax[1][3].axis("off")
    ax[1][3].text(0.02, 0.5,
                  "Alkaline, S094 P4:\n"
                  "  typ wav: Saw, Sqr (303 table), Tri\n"
                  "  typ pwm: Saw (two-saw sum),\n"
                  "           Sqr (two-saw pulse), Tri (warp)\n"
                  "  typ wtb: installed wavetables\n\n"
                  "Not offered: Sin, Rec and Noi\n"
                  "under wav; Sin and Noi under pwm.",
                  fontsize=9, va="center", family="DejaVu Sans Mono")
    for a in ax[1]:
        a.set_xlabel("cycle")
    fig.suptitle("Alkaline oscillator waves against the stock waves "
                 "(firmware tables, octave row 2)", fontweight="bold")
    fig.tight_layout()
    fig.savefig(L.out_path("s094p4_alk_waves.png"), dpi=110)
    plt.close(fig)

    # ---- 4. the law ------------------------------------------------------
    pos = np.arange(128)
    p_now = np.clip(pos, 1, 127)
    d_rec_v = np.array(G.d_rec(), dtype=float)
    d_saw_v = np.array(G.d_saw(), dtype=float)
    tri_w = np.array(G.split(G.TRI_W_END)[0])
    sin_w = np.array(G.split(G.SIN_W_END)[0])
    fig, ax = plt.subplots(1, 2, figsize=(13, 4.4))
    a = ax[0]
    a.plot(pos, p_now / 128, color=L.PALETTE[4], lw=1.6,
           label="S094 P1 (old): clamp(pos, 1, 127) / 128")
    a.plot(pos, d_rec_v / 1024, color=L.PALETTE[1], lw=1.6,
           label="S094 P4 Rec (implemented): (512 - 4 pos) / 1,024")
    a.plot(pos, d_saw_v / 1024, color=L.PALETTE[2], lw=1.2, ls="--",
           label="S094 P4 Saw: round(512 - pos x 486 / 127) / 1,024")
    a.plot(pos, tri_w, color=L.PALETTE[3], lw=1.2, ls=":",
           label="S094 P4 Tri: w from 1/2 to 26/1,024")
    a.plot(pos, sin_w, color=L.PALETTE[0], lw=1.0, ls="-.",
           label="S094 P4 Sin: w from 1/2 to 1/16")
    a.axhline(0.5, color="#9aa3ad", lw=0.8, ls="--")
    a.set_xlabel("pos")
    a.set_ylabel("width (Rec: fraction of the cycle high)")
    a.set_title("pos -> width (the law in oscPwmTables.h)")
    a.legend()
    a = ax[1]
    narrow_now = np.minimum(p_now, 128 - p_now) / 128
    narrow_new = np.minimum(d_rec_v / 1024, 1 - d_rec_v / 1024)
    a.plot(pos, narrow_now, color=L.PALETTE[4], lw=1.6, label="today")
    a.plot(pos, narrow_new, color=L.PALETTE[1], lw=1.6,
           label="S094 P4 (implemented)")
    a.set_xlabel("pos")
    a.set_ylabel("min(width, 1 - width): what the ear hears")
    a.set_title("Audible pulse width: S094 P1 folded at pos 64; S094 P4 falls")
    a.legend()
    fig.tight_layout()
    fig.savefig(L.out_path("s094p4_pwm_law.png"), dpi=110)
    plt.close(fig)

    # ---- numbers for the document ----------------------------------------
    print("pos 0 == pos 1 (all waves):",
          all(np.array_equal(pwm[(w, 0)], pwm[(w, 1)]) for w in WAVES),
          "(the S094 P1 clamp; S094 P4: False)")
    for w in WAVES:
        worst = 0.0
        for p in range(1, 64):
            a, b = H[w][p, 1:NH + 1], H[w][128 - p, 1:NH + 1]
            strong = np.maximum(a, b) > 10 ** (-60 / 20)
            if strong.any():
                worst = max(worst, float(np.max(np.abs(db(a[strong]) -
                                                       db(b[strong])))))
        rms = [float(np.sqrt(np.mean((pwm[(w, p)][:CYCLE] / 32767.0) ** 2)))
               for p in (0, 16, 32, 64)]
        print(f"{w}: |dB(p) - dB(128-p)| max over p 1..63 (S094 P4: not a "
              f"mirror), "
              f"harmonics above -60 dB: {worst:.2f} dB; "
              f"RMS at pos 0/16/32/64: " + " / ".join(f"{v:.3f}" for v in rms))
    for p in (0, 32, 64, 96, 127):
        rec = pwm[("Rec", p)][:CYCLE] / 32767.0
        print(f"Rec pos {p} high fraction:", float(np.mean(rec > 0)))


if __name__ == "__main__":
    main()
