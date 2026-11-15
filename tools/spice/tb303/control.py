#!/usr/bin/env python3
"""TB-303 control network: knobs, MEG, accent sweep, expo -> ladder current (S092 P4).

What:       transistor-level netlist of the 303's cutoff control path from
            the Roland Service Notes p5: the Cutoff / Env Mod resistor chain
            (+12 V - VR3 - R47 - R61 - VR5 - MEG), TM3 + R73 into the
            Q10/Q11 exponential pair with Q9's reference and R71 (R47 and R61
            return to Q9's node, as in [XB]); the MEG
            (C62 struck through D37/R152, discharged through R138 + VR6 into
            the D28/D27/D26 + R117 stack, IC12 bypassing VR6 on accent,
            Darlington Q41/Q40 with Q39 as its load); the accent sweep (IC12
            -> VR7 -> D24 -> R46 -> VR4b with C13 -> R72). Output: Q11's
            collector current (the ladder current), mapped to the
            resonance frequency with the S092 P2 law.
Why:        S092_P2 §7; record: S092_P4_CONTROL_NET.md.
Inputs:     [SN] p5 values; the strike source and the knob tapers are
            stated in the document; generic devices.
Outputs:    tools/spice/tb303/out/ctl_*.cir, control_results.json,
            tools/plots/303_control_spice.png (raw data deleted after load).
Accessors:  python3 tools/spice/tb303/control.py   (ngspice, numpy, matplotlib)
"""
import json
import os
import subprocess
import sys
import tempfile

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
OUT = os.path.join(HERE, "out")
RAW = os.path.join(tempfile.gettempdir(), "lxr_spice_tb303")
sys.path.insert(0, os.path.join(ROOT, "tools", "plots"))

I_CAL = 7.375e-6          # S092 P2 §3.2: ladder current for the [SN] p7 2.0 ms ring
STEP = 0.125              # one 16th at 120 BPM (s)
GATE = 3.5 / 6 * STEP     # P0 D11

MODELS = """
.model QN NPN(IS=1e-14 BF=240 VAF=100 CJE=8p CJC=4p TF=0.4n RB=50)
.model QP PNP(IS=1e-14 BF=300 VAF=60 CJE=8p CJC=5p TF=0.6n RB=50)
.model DS D(IS=2.52n N=1.752 RS=0.5 CJO=2p)
* Q39 2SK30(Y), IDSS 1.2-3 mA: 2 mA used
.model JY NJF(VTO=-1.5 BETA=0.889e-3 LAMBDA=0.005)
* IC12 (4066) switch, R_on 250 ohm
.model S4066 SW(VT=2.5 VH=0.1 RON=250 ROFF=1e10)
"""


def taper_a(r):
    """Audio (A) taper: track fraction from pin 1 at rotation r; 20 % at
    half rotation (B = 16: (16^r - 1) / 15)."""
    return (16.0 ** r - 1.0) / 15.0


def pot(name, p1, p2, p3, total, frac):
    """A pot as two resistors: pin 1 - wiper (frac of the track) - pin 3."""
    a = max(total * frac, 1.0)
    b = max(total * (1.0 - frac), 1.0)
    return f"R{name}A {p1} {p2} {a:.3f}\nR{name}B {p2} {p3} {b:.3f}\n"


def netlist(cut, envmod, decay, accent_knob, res, tm3, events, t_stop, t_step=50e-6,
            dc=False, kexp=1.0):
    """cut, envmod, decay: knob rotations 0..1 (A taper); accent_knob, res:
    0..1 (B taper). tm3: TM3 trim resistance (ohm). events: list of
    (t, accented) note starts; each strikes the MEG for 1.3 ms and holds
    ACCENT high for the step when accented. kexp: Q11/Q10 saturation-current
    ratio (the PNP/NPN mismatch TM3 trims out; calibrated, S092 P4 §3.1)."""
    strike, acc = ["0 0"], ["0 0"]
    for t0, a in events:
        strike += [f"{t0:.6f} 0", f"{t0 + 1e-6:.6f} 11.85", f"{t0 + 1.3e-3:.6f} 11.85",
                   f"{t0 + 1.3e-3 + 1e-6:.6f} 0"]
        if a:
            acc += [f"{t0:.6f} 0", f"{t0 + 1e-6:.6f} 5", f"{t0 + STEP - 2e-6:.6f} 5",
                    f"{t0 + STEP - 1e-6:.6f} 0"]
    analysis = (".op" if dc else
                f".options method=gear reltol=1e-4\n.tran {t_step:.3e} {t_stop:.4f} 0 {t_step:.3e}")
    return f"""* TB-303 control network cut={cut} env={envmod} dec={decay} acc={accent_knob} res={res}
{MODELS}
.model QN11 NPN(IS={1e-14 * kexp:.4e} BF=240 VAF=100 CJE=8p CJC=4p TF=0.4n RB=50)
VCC vcc 0 12
VREF vref 0 5.333
* --- MEG: strike (1.3 ms, 11.85 V, behavioural) -> D37 -> R152 100 -> C62 1u
VSTR str 0 PWL({' '.join(strike)})
D37 str n37 DS
R152 n37 c62 100
C62 c62 0 1u
* --- discharge: R138 68k -> X; VR6 1M(A) rheostat X -> Y; IC12 (3-4) shorts X-Y on accent
R138 c62 nx 68k
RVR6 nx ny {max(1e6 * taper_a(decay), 1.0):.3f}
VACC acc 0 PWL({' '.join(acc)})
SDEC nx ny acc 0 S4066
* --- diode stack: Y -> D28 -> J -> D27 -> D26 -> ground; R117 22k from the reference to J
D28 ny nj DS
D27 nj nk DS
D26 nk 0 DS
R117 vref nj 22k
* --- Darlington Q41/Q40 -> N53 (MEG out), Q39 2SK30(Y) current-source load
Q41 vcc c62 e41 QN
Q40 vcc e41 n53 QN
J39 n53 0 0 JY
* --- knobs: VR3 (Cutoff, A) from +12 V to R47 -> n561; VR5 (Env Mod, A) from
*     R61 -> n561 to the MEG (N53). R47 and R61 both return to Q9's node n561 ([XB]).
{pot("VR3", "cutn", "w3", "vcc", 50e3, taper_a(cut))}R47 cutn n561 10k
R61 v5p1 n561 10k
{pot("VR5", "v5p1", "w5", "n53", 50e3, taper_a(envmod))}* --- Cutoff wiper -> TM3 -> R73 -> Q10 emitter / Q11 base
RTM3 w3 ntm {tm3:.3f}
R73 ntm nb11 100k
* --- expo: Q10 PNP (base = summing node n134), Q11 NPN, Q9 PNP holds n561
Q10 n561 n134 nb11 QP
Q11 nc11 nb11 n561 QN11
Q9 0 nb9 n561 QP
R65 vref nb9 10k
R64 nb9 0 10k
R71 n134 n561 2.2k
* --- Env Mod: VR5 wiper -> R63 220k -> n134
R63 w5 n134 220k
* --- accent: IC12 (1-2) N53 -> VR7 top; VR7 (B) wiper -> D24 -> R46 47k -> VR4b pin 1;
*     VR4b (B, the Resonance second gang) pin 3 = C13 1u; wiper -> R72 100k -> n134
SACC n53 v7p3 acc 0 S4066
{pot("VR7", "0", "w7", "v7p3", 50e3, accent_knob)}D24 w7 n24 DS
R46 n24 v4p1 47k
{pot("VR4B", "v4p1", "w4", "c13", 50e3, res)}C13 c13 0 1u
R72 w4 n134 100k
* --- Q11 collector: R69 2.2k to the Q12 emitters (about 4.7 V)
R69 nc11 tailv 2.2k
VTAIL tailv 0 4.7
VSENSE nc11x nc11 0
{analysis}
.control
run
{"print v(n53) v(c62) v(n134) v(n561) v(nb11) v(w3) v(w5) i(VTAIL)" if dc else
 "wrdata {OUTFILE} i(VTAIL) v(c62) v(n53) v(n134) v(c13) v(w4)"}
quit
.endc
.end
"""


def run(tag, text, dc=False):
    os.makedirs(OUT, exist_ok=True)
    os.makedirs(RAW, exist_ok=True)
    text = text.replace("{OUTFILE}", tag + ".txt")
    with open(os.path.join(OUT, tag + ".cir"), "w") as fh:
        fh.write(text)
    cir, dat = os.path.join(RAW, tag + ".cir"), os.path.join(RAW, tag + ".txt")
    with open(cir, "w") as fh:
        fh.write(text)
    r = subprocess.run(["ngspice", "-b", tag + ".cir"], cwd=RAW, capture_output=True, text=True)
    os.remove(cir)
    if dc:
        vals = {}
        for line in r.stdout.splitlines():
            if "=" in line and line.strip().startswith(("v(", "i(")):
                k, v = line.split("=")
                vals[k.strip()] = float(v.split()[0])
        if not vals:
            sys.exit(f"ngspice failed for {tag}:\n{r.stdout[-2000:]}")
        return vals
    if r.returncode != 0 or not os.path.exists(dat):
        sys.exit(f"ngspice failed for {tag}:\n{r.stdout[-2500:]}\n{r.stderr[-1500:]}")
    raw = np.loadtxt(dat)
    os.remove(dat)
    return raw


def ladder_current(vals):
    return -vals["i(vtail)"]          # current from R69 into VTAIL = Q11's collector current


# P2 law: resonance frequency (Res full) against ladder current
def load_p2_law():
    r = json.load(open(os.path.join(HERE, "vcf_results.json")))["model"]["ac"]
    pts = sorted((m["itail"], m["boost_hz"]) for m in r if m["k_res"] == 1.0 and m["boost_hz"] > 50)
    return np.array([p[0] for p in pts]), np.array([p[1] for p in pts])


def f_res(i, law):
    li, lf = np.log(law[0]), np.log(law[1])
    return float(np.exp(np.interp(np.log(max(i, 1e-12)), li, lf,
                                  left=None, right=None))) if i > 0 else 0.0


def f_res_extrap(i, law):
    """Log-log interpolation with linear extrapolation outside P2's range."""
    li, lf = np.log(law[0]), np.log(law[1])
    x = np.log(max(i, 1e-15))
    if x < li[0]:
        s = (lf[1] - lf[0]) / (li[1] - li[0])
        return float(np.exp(lf[0] + s * (x - li[0])))
    if x > li[-1]:
        s = (lf[-1] - lf[-2]) / (li[-1] - li[-2])
        return float(np.exp(lf[-1] + s * (x - li[-1])))
    return float(np.exp(np.interp(x, li, lf)))


def main():
    law = load_p2_law()
    res = {}
    curves = {}

    # ---------- 1. calibrate TM3 (the [SN] p7 "FREQ" trim): Cutoff centre,
    # Env Mod 0, MEG at rest -> I_CAL. Matched generic devices (kexp = 1).
    kexp = 1.0
    tm3 = 470e3

    def i_dc(cut, env, kexp, dec=0.5, tm3_=None):
        v = run("ctl_dc", netlist(cut, env, dec, 0.0, 0.0, tm3_ if tm3_ else tm3, [], 0,
                                  dc=True, kexp=kexp), dc=True)
        return ladder_current(v), v
    lo, hi = 1e3, 470e3
    for _ in range(40):
        mid = np.sqrt(lo * hi)
        i, _ = i_dc(0.5, 0.0, kexp, tm3_=mid)
        if i > I_CAL:
            lo = mid
        else:
            hi = mid
    tm3 = float(np.sqrt(lo * hi))
    i_c, vals = i_dc(0.5, 0.0, kexp)
    res["tm3_ohm"] = tm3
    res["kexp"] = kexp
    res["rest_point"] = {k: float(v) for k, v in vals.items()}
    print(f"TM3 calibrated to {tm3 / 1e3:.1f} kohm -> {i_c * 1e6:.3f} uA; rest: {vals}")

    # ---------- 2. DC maps: Cutoff knob x Env Mod knob (MEG at rest)
    res["dc_map"] = []
    for env in (0.0, 0.25, 0.5, 0.75, 1.0):
        row = []
        for cut in np.linspace(0, 1, 11):
            i, _ = i_dc(cut, env, kexp)
            row.append(dict(cut=float(cut), env=env, i_uA=1e6 * i, f_res_hz=f_res_extrap(i, law)))
        res["dc_map"].append(row)
        print(f"Env Mod {env:.2f}: " + "  ".join(f"{d['cut']:.1f}:{d['f_res_hz']:.0f}" for d in row))

    # ---------- 3. MEG decay against the Decay knob (normal note; accent forces minimum)
    res["meg"] = []
    for dec, acc in ((0.0, False), (0.5, False), (1.0, False), (1.0, True)):
        raw = run(f"ctl_meg_d{int(dec * 100)}_{'acc' if acc else 'n'}",
                  netlist(0.5, 0.5, dec, 0.0, 0.0, tm3, [(0.01, acc)], 4.0, t_step=200e-6, kexp=kexp))
        t, c62, n53 = raw[:, 0], raw[:, 3], raw[:, 5]
        m = t > 0.0105
        pk = float(n53[m].max())
        rest = float(n53[t < 0.009][-1])
        tp = float(t[m][np.argmax(n53[m])])
        thr = rest + 0.1 * (pk - rest)
        j = np.where((t > tp) & (n53 < thr))[0]
        t10 = float(t[j[0]] - tp) if len(j) else float("nan")
        res["meg"].append(dict(decay_knob=dec, accent=acc, n53_peak_V=pk, n53_rest_V=rest,
                               c62_peak_V=float(c62[m].max()), t_to_10pct_s=t10))
        curves[("meg", dec, acc)] = (t - tp, (n53 - rest) / (pk - rest))
        print("MEG:", res["meg"][-1])

    # ---------- 4. cutoff against time: one note, Env Mod 0 / 0.5 / 1 (Cutoff centre, Decay 0.5)
    res["note"] = []
    for env in (0.0, 0.5, 1.0):
        raw = run(f"ctl_note_e{int(env * 100)}",
                  netlist(0.5, env, 0.5, 0.0, 0.0, tm3, [(0.01, False)], 1.2, t_step=200e-6, kexp=kexp))
        t, i = raw[:, 0], -raw[:, 1]
        f = np.array([f_res_extrap(x, law) for x in i])
        m = t > 0.0105
        res["note"].append(dict(env=env, f_rest_hz=float(f[t < 0.009][-1]), f_peak_hz=float(f[m].max()),
                                f_at_125ms_hz=float(np.interp(0.135, t, f))))
        curves[("note", env)] = (t - 0.01, f)
        print("note:", res["note"][-1])

    # ---------- 5. accent runs: four accented 16ths at 120 BPM, Res 0 and Res full
    res["accent_run"] = []
    ev = [(0.01 + k * STEP, True) for k in range(4)] + [(0.01 + 4 * STEP, False)]
    for rk in (0.0, 0.5, 1.0):
        raw = run(f"ctl_accrun_r{int(rk * 100)}",
                  netlist(0.5, 0.5, 0.5, 1.0, rk, tm3, ev, 0.9, t_step=100e-6, kexp=kexp))
        t, i, c13 = raw[:, 0], -raw[:, 1], raw[:, 9]
        f = np.array([f_res_extrap(x, law) for x in i])
        peaks, ipk = [], []
        for k in range(5):
            a, b = 0.01 + k * STEP, 0.01 + (k + 1) * STEP
            mm = (t >= a) & (t < b)
            peaks.append(float(f[mm].max()))
            ipk.append(float(1e6 * i[mm].max()))
        res["accent_run"].append(dict(res=rk, peak_f_hz=peaks, peak_i_uA=ipk,
                                      c13_max_V=float(c13.max())))
        curves[("acc", rk)] = (t - 0.01, f, c13)
        print("accent run:", res["accent_run"][-1])

    with open(os.path.join(HERE, "control_results.json"), "w") as fh:
        json.dump(res, fh, indent=1)
    plot(res, curves)


def plot(res, curves):
    import lxr_plotlib as L
    plt = L.setup_matplotlib()
    fig, ax = plt.subplots(2, 2, figsize=(13, 9.5))
    c = L.PALETTE[1:]

    a = ax[0, 0]
    for row, col in zip(res["dc_map"], c):
        a.semilogy([d["cut"] for d in row], [d["f_res_hz"] for d in row], "o-", color=col,
                   ms=3, label=f"Env Mod {row[0]['env']:.2f}")
    a.axhline(500, color="#8a96a3", ls=":", lw=1)
    a.set_title("Resonance frequency at rest against the Cutoff knob")
    a.set_xlabel("Cutoff knob rotation"); a.set_ylabel("Hz"); a.legend(fontsize=7)

    a = ax[0, 1]
    for (key, val), col in zip([(k, v) for k, v in curves.items() if k[0] == "meg"], c):
        t, y = val
        _, dec, acc = key
        a.plot(t, y, color=col, label=f"Decay {dec:.1f}{' + accent' if acc else ''}")
    a.axhline(0.1, color="#8a96a3", ls=":", lw=1)
    a.set_xlim(-0.05, 3.0)
    a.set_title("MEG (N53) re. its peak; [SN]: 200 ms / 2.5 s to 10 %")
    a.set_xlabel("s after the peak"); a.legend(fontsize=7)

    a = ax[1, 0]
    for env, col in zip((0.0, 0.5, 1.0), c):
        t, f = curves[("note", env)]
        a.semilogy(1e3 * t, f, color=col, label=f"Env Mod {env:g}")
    a.set_xlim(-10, 600)
    a.set_title("One note: resonance frequency (Cutoff centre, Decay 0.5)")
    a.set_xlabel("ms"); a.set_ylabel("Hz"); a.legend(fontsize=7)

    a = ax[1, 1]
    for rk, col in zip((0.0, 0.5, 1.0), c):
        t, f, c13 = curves[("acc", rk)]
        a.semilogy(1e3 * t, f, color=col, label=f"Res {rk:g}")
    a.axhline(20000, color="#8a96a3", ls=":", lw=1)
    for k in range(5):
        a.axvline(1e3 * k * STEP, color="#c3cad2", lw=0.8)
    a.set_title("Four accented 16ths then a normal one (Accent full, Env Mod 0.5)")
    a.set_xlabel("ms"); a.set_ylabel("Hz"); a.legend(fontsize=7)

    fig.suptitle("TB-303 control network — ngspice, [SN] p5 values (S092 P4)",
                 fontsize=13, fontweight="bold")
    fig.tight_layout(rect=(0, 0, 1, 0.96))
    out = os.path.join(ROOT, "tools", "plots", "303_control_spice.png")
    fig.savefig(out, dpi=105)
    print("wrote", out)


if __name__ == "__main__":
    main()
