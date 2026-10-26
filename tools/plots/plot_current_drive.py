#!/usr/bin/env python3
"""Current (S084) drive and distortion profiles of the LXR-02 voices.

Output: current_drive_profiles.png

Panels:
  A  filter `drv`: the ZDF input soft clip x = u*N(u^2/4)/D(u^2/4) ~ 2*tanh(u/2),
     u = in * (0.4 + 6*(drv/127)^2)
  B  Voice/Mix-page `drv`: y = (1+k)x/(1+k|x|), k = 2(d/128)/(1-d/128)
  C  the real ZDF LP loop (float64 port), cutoff 127, reso 0, 110 Hz full-scale sine
  D  the real ZDF HP loop, cutoff 10, reso 0, 1 kHz full-scale sine (passband):
     x 0.882 output gain and __SSAT hard clip
  E  Voice drive on one sine cycle
  F  small-signal gain against the control value for both drives
All formulas are ported from ResonantFilter.c, distortion.h and the
IM_SPECIAL_FILTER_* writers in InstrumentManager.c.
"""
import numpy as np

import lxr_plotlib as L


def main():
    plt = L.setup_matplotlib()
    fig, ax = plt.subplots(3, 2, figsize=(13, 13))
    xs = np.linspace(-1, 1, 801)
    filt_bytes = [0, 20, 40, 64, 96, 127]
    dist_bytes = [0, 16, 32, 64, 96, 127]
    col = L.PALETTE

    a = ax[0, 0]
    for i, b in enumerate(filt_bytes):
        d = L.filt_drive(b)
        a.plot(xs, L.filter_input_stage(xs * d), color=col[i],
               label=f"drv {b:3d}  (×{d:.2f}, {20 * np.log10(d):+.1f} dB)")
    a.set_title("A. Filter drv — input soft clip (all ZDF types)")
    a.set_xlabel("input sample (1 = int16 full scale)")
    a.set_ylabel("x, filter input after the clip")
    a.legend(loc="upper left")

    a = ax[0, 1]
    for i, b in enumerate(dist_bytes):
        k = L.dist_k(b)
        a.plot(xs, L.dist_curve(xs, b), color=col[i], label=f"drv {b:3d}  (k {k:.2f}, small-signal ×{1 + k:.1f})")
    a.set_title("B. Voice/Mix drv — post-amp distortion")
    a.set_xlabel("input sample")
    a.set_ylabel("output")
    a.legend(loc="upper left")

    a = ax[1, 0]
    for i, b in enumerate(filt_bytes):
        t, y = L.filter_one_cycle("LP", 127, 0, b, 110.0)
        a.plot(t, y, color=col[i], label=f"drv {b}")
    a.set_title("C. LP output, cutoff 127, reso 0 (simulated ZDF loop)")
    a.set_xlabel("one cycle of a 110 Hz full-scale sine")
    a.set_ylabel("filter output")
    a.legend(loc="lower left", ncol=3)

    a = ax[1, 1]
    for i, b in enumerate(filt_bytes):
        t, y = L.filter_one_cycle("HP", 10, 0, b, 1000.0)
        a.plot(t, y, color=col[i], label=f"drv {b}")
    a.set_title("D. HP output, cutoff 10, reso 0 (passband): ×0.882 then __SSAT")
    a.set_xlabel("one cycle of a 1 kHz full-scale sine")
    a.set_ylabel("filter output")
    a.legend(loc="lower left", ncol=3)

    a = ax[2, 0]
    t = np.linspace(0, 1, 721)
    for i, b in enumerate(dist_bytes):
        a.plot(t, L.dist_curve(np.sin(2 * np.pi * t), b), color=col[i], label=f"drv {b}")
    a.set_title("E. Voice drv on one cycle of a full-scale sine")
    a.set_xlabel("phase")
    a.set_ylabel("output")
    a.legend(loc="lower left", ncol=3)

    a = ax[2, 1]
    bs = np.arange(128)
    a.plot(bs, 20 * np.log10(L.filt_drive(bs)), color=col[1], label="filter drv: 20·log10(0.4 + 6(v/127)²)")
    a.plot(bs, 20 * np.log10(1 + L.dist_k(bs)), color=col[4], label="voice drv: 20·log10(1 + k)")
    a.axhline(0, color=col[0], ls="--", lw=1, label="unity (filter drv ≈ 40)")
    a.set_ylim(-10, 50)
    a.set_xlim(0, 127)
    a.set_title("F. Small-signal gain against the control value")
    a.set_xlabel("control value")
    a.set_ylabel("gain at low level (dB)")
    a.legend(loc="upper left")

    for a in ax.flat[:2]:
        a.set_xlim(-1, 1)
    fig.suptitle("LXR-02 drive stages as implemented today (S084 source)", fontsize=14, fontweight="bold")
    fig.text(0.5, 0.005,
             "LP ends in fastTanh, so it never hard-clips; HP/BP/UBP/notch/peak scale by 0x70ff/32767 and __SSAT, "
             "so a full-scale input hard-clips above drv ≈ 49. Resonance adds the integrator nonlinearities on top.",
             ha="center", fontsize=8.5, color="#5b6672")
    fig.tight_layout(rect=(0, 0.015, 1, 0.98))
    fig.savefig(L.out_path("current_drive_profiles.png"), dpi=110)
    print("wrote current_drive_profiles.png")


if __name__ == "__main__":
    main()
