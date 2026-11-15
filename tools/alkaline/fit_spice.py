#!/usr/bin/env python3
"""Fit the Alkaline DSP constants from the S092 SPICE models (S092 P6).

What:       runs the P3/P4 netlists (tools/spice/tb303) for the MEG decays,
            the VEG decay and attack, and the P1 VCO at each table octave
            level, then fits: the MEG peak, rest, decay floor (V_inf) and
            Darlington transfer; the VEG current-vs-C42 table and attack;
            the square wave's cycle per octave level (P0 D8).
Why:        the DSP equations (S092_P6_DSP_EQUATIONS.md) take these from the
            circuit, not from guesses.
Outputs:    tools/alkaline/alk_constants.json
Accessors:  python3 tools/alkaline/fit_spice.py   (ngspice, numpy)
"""
import json
import math
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, "tools", "spice", "tb303"))
import control as CTL  # noqa: E402
import vca as VCA      # noqa: E402
import vco_q8 as VCO   # noqa: E402

VT = 0.02585


def fit_meg():
    curves = []
    for dec in (0.0, 0.5, 1.0):
        raw = CTL.run(f"fit_meg_{int(dec * 100)}",
                      CTL.netlist(0.5, 0.5, dec, 0.0, 0.0, CTL_TM3, [(0.01, False)], 3.5,
                                  t_step=200e-6))
        t, c62, n53 = raw[:, 0], raw[:, 3], raw[:, 5]
        curves.append((dec, t, c62, n53))
    t, c62, n53 = curves[0][1:]
    v_rest = float(c62[t < 0.009][-1])
    v53_rest = float(n53[t < 0.009][-1])
    v_peak = float(np.max([c[2].max() for c in curves]))
    t_strike = 1.3e-3
    # V_inf: least squares over the three decays (after the strike)
    best = None
    for v_inf in np.linspace(0.0, 2.5, 251):
        err = 0.0
        for dec, t, c62, _ in curves:
            r = 68e3 + 1e6 * (16.0 ** dec - 1) / 15.0
            m = t > 0.01 + t_strike + 1e-4
            t0 = t[m][0]
            v0 = c62[m][0]
            model = v_inf + (v0 - v_inf) * np.exp(-(t[m] - t0) / (r * 1e-6))
            sel = c62[m] > 1.0          # the decay above the stack's knee
            err += float(np.mean((model[sel] - c62[m][sel]) ** 2))
        if best is None or err < best[0]:
            best = (err, v_inf)
    v_inf = float(best[1])
    # Darlington: N53 = v53_min + soft * softplus((c62 - drop) / soft)
    allc = np.concatenate([c[2] for c in curves])
    alln = np.concatenate([c[3] for c in curves])
    bestd = None
    for drop in np.linspace(0.9, 1.4, 51):
        for soft in (0.03, 0.045, 0.06, 0.08, 0.1, 0.12):
            x = (allc - drop) / soft
            sp = np.where(x > 30, x, np.log1p(np.exp(np.minimum(x, 30))))
            pred = v53_rest + soft * sp
            e = float(np.sqrt(np.mean((pred - alln) ** 2)))
            if bestd is None or e < bestd[0]:
                bestd = (e, drop, soft)
    # decay times to 10 % with the fitted model, for the record
    return dict(v_peak=v_peak, v_rest=v_rest, v_inf=v_inf, t_strike=t_strike,
                drop=float(bestd[1]), soft=float(bestd[2]), v53_min=v53_rest,
                darlington_rms_err_V=bestd[0], v_inf_fit_mse=best[0])


def fit_veg():
    # long decay: the current-vs-C42 table
    raw = VCA.run("fit_veg_long", VCA.veg_netlist(12.0, 0.70e-3, 5e-3, 6.0, 6.0, t_step=200e-6))
    t, iabc, v42 = raw[:, 0], np.abs(raw[:, 1]), raw[:, 3]
    m = t > 0.02
    order = np.argsort(v42[m])
    vv, ii = v42[m][order], iabc[m][order]
    grid = np.linspace(float(vv.min()), 12.0, 64)
    itab = np.interp(grid, vv, ii)
    itab[grid > vv.max()] = 0.0
    # attack (S093 P6): fit delay + exponential discharge of C42 per CPU
    # trigger width, 0.70 ms (S092 P3 §3.2: today's attack) down to 0.55 ms in
    # nine steps; the knob alk_attack interpolates this table.
    #   model: v42(t) = v0 for t < delay, v0 exp(-(t - delay) / tau) after,
    #   grid-searched for least squares on the SPICE C42 voltage after the
    #   trigger edge (t = 10 ms). Point 0 is the S092 fit exactly (same width,
    #   netlist and grid), so att_delay / tau_att keep their values.
    def fit_att(tw):
        raw = VCA.run(f"fit_veg_att_{int(round(tw * 1e7))}",
                      VCA.veg_netlist(12.0, tw, 5e-3, 0.5, 0.04, t_step=5e-6))
        t, v42 = raw[:, 0], raw[:, 3]
        m = t >= 0.01
        tt, vv2 = t[m] - 0.01, v42[m]
        best = None
        # the delay is searched to 4 ms and tau to 6 ms: a 0.55 ms trigger
        # turns Q32 only just on (5.15 ms 10->90 %)
        for delay in np.linspace(0.0, 4.0e-3, 81):
            for tau in np.linspace(0.1e-3, 6.0e-3, 119):
                mod = np.where(tt < delay, vv2[0], vv2[0] * np.exp(-(tt - delay) / tau))
                e = float(np.mean((mod - vv2) ** 2))
                if best is None or e < best[0]:
                    best = (e, delay, tau)
        return best
    widths = [round(0.70e-3 - q * (0.15e-3 / 8.0), 9) for q in range(9)]
    att = [fit_att(tw) for tw in widths]
    best = att[0]
    # recharge: fit v42(t) = v_rec - (v_rec - v0) exp(-(t - t1) / tau) after the attack
    raw = VCA.run("fit_veg_long2", VCA.veg_netlist(12.0, 0.70e-3, 5e-3, 6.0, 6.0, t_step=200e-6))
    t3, v3 = raw[:, 0], raw[:, 3]
    m3 = t3 > 0.03
    bestr = None
    for v_rec in np.linspace(11.0, 12.5, 61):
        for tau in np.linspace(1.2, 2.6, 71):
            mod = v_rec - (v_rec - v3[m3][0]) * np.exp(-(t3[m3] - t3[m3][0]) / tau)
            e = float(np.mean((mod - v3[m3]) ** 2))
            if bestr is None or e < bestr[0]:
                bestr = (e, v_rec, tau)
    return dict(v42=list(map(float, grid)), i_uA=list(map(float, 1e6 * itab)),
                v_rec=float(bestr[1]), tau_rec=float(bestr[2]), rec_fit_mse=bestr[0],
                att_delay=float(best[1]), tau_att=float(best[2]), att_fit_mse=best[0],
                # S093 P6: the knob table, point 0 = the two values above
                att_table=[dict(trigger_ms=1e3 * tw, att_delay=float(b[1]),
                                tau_att=float(b[2]), fit_mse=b[0])
                           for tw, b in zip(widths, att)])


def square_levels():
    """One cycle of the Q8 output and the saw at each table octave level
    (centre frequency 16.35 x 2^lv x sqrt 2), from the P1 netlist."""
    out = []
    for lv in range(0, 7):
        f = 16.3516 * 2 ** lv * math.sqrt(2)
        t, d = VCO.run(f"fit_sq_lv{lv}", VCO.netlist(f, "sq"))
        e = VCO.last_cycles(t, d["c33"])
        a, b = e[-2], e[-1]
        sq = VCO.resample_cycle(t, d["col"], a + 1, b + 1, 1024)
        saw = VCO.resample_cycle(t, d["src"], a + 1, b + 1, 1024)
        out.append(dict(level=lv, f_hz=f, f_meas=float(1.0 / (t[b] - t[a])),
                        sq=list(np.round(sq, 5)), saw=list(np.round(saw, 5))))
        print(f"level {lv}: {f:.1f} Hz, square {sq.min():.2f}..{sq.max():.2f} V, "
              f"on-time {100 * np.mean(sq > 0.5 * (sq.max() + sq.min())):.1f} %")
    return out


CTL_TM3 = 152.74e3


def main():
    c = {}
    c["meg"] = fit_meg()
    print("MEG:", c["meg"])
    c["veg"] = fit_veg()
    print("VEG attack:", c["veg"]["att_delay"], c["veg"]["tau_att"])
    # D27 (1N4148 model) + pin-1 mirror Vbe at the ~125 uA accent current
    c["vca_accent"] = dict(v_drop=1.752 * VT * math.log(125e-6 / 2.52e-9) +
                           VT * math.log(170e-6 / 1e-14))
    c["accent"] = dict(v13_rest=3.2487)      # S092 P4 run operating point
    c["square_levels"] = square_levels()
    with open(os.path.join(HERE, "alk_constants.json"), "w") as fh:
        json.dump(c, fh, indent=1)
    print("wrote alk_constants.json")


if __name__ == "__main__":
    main()
