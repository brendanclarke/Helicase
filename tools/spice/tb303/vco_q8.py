#!/usr/bin/env python3
"""TB-303 VCO core + Q8 square shaper: ngspice runs and analysis (S092 P1).

What:       builds an ngspice netlist of the 303 saw core (Q26 output
            transistor as a current mirror into C33, behavioural reset,
            Q28 JFET follower into R105) and the Q8 square shaper with its
            [SN] p5 network, loaded by the VCF input (R62/C17/R70). Runs it
            at several pitches, then measures saw straightness, square duty,
            levels, and the drive reaching the Q12 base.
Why:        S092_P0 §3.3: duty against pitch, ramp straightness, saw/square
            level ratio and filter drive are behaviour the schematic does
            not show directly. Record: S092_P1_VCB_MODEL_VCO.md.
Inputs:     component values from the Roland TB-303 Service Notes p5 (see
            the document); generic device models (the netlist header says
            which parameters are assumed).
Outputs:    tools/spice/tb303/out/*.cir (netlists; the raw ngspice data,
            ~60 MB per run, goes to a temp folder and is deleted after it
            is read), tools/spice/tb303/vco_q8_results.json,
            tools/plots/303_vco_q8_spice.png.
Accessors:  python3 tools/spice/tb303/vco_q8.py   (needs ngspice, numpy,
            matplotlib)
"""
import json
import os
import subprocess
import sys
import tempfile

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "out")                  # netlists only (small)
RAW = os.path.join(tempfile.gettempdir(), "lxr_spice_tb303")  # raw data, deleted after load
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
sys.path.insert(0, os.path.join(ROOT, "tools", "plots"))

C33 = 0.01e-6
V_TOP, V_BOT = 12.0, 5.5          # [SN] p5 scope sketch at the Q28 source

# name, Hz. C1..C5 is the 303's full CV range (+1 V..+5 V, [SN] p1/p7).
PITCHES = [("C1", 32.703), ("C2", 65.406), ("A2", 110.0), ("C3", 130.81),
           ("C4", 261.63), ("C5", 523.25)]

# Generic device models. NOT vendor models: the parameters are typical for
# the device class and are swept where the result depends on them.
MODELS = """
* Q26 (2SC1583 dual NPN): generic small-signal NPN; VAF swept separately.
.model QN NPN(IS=1e-14 BF=300 VAF={vaf} CJE=8p CJC=4p TF=0.4n RB=50)
* Q8 (2SA733 P grade, hFE 200-400): generic high-gain PNP.
.model QP PNP(IS=1e-14 BF={bfp} VAF=80 CJE=8p CJC=5p TF=0.6n RB=50)
* Q28 (2SK30A-O, IDSS 0.6-1.4 mA): generic N-JFET, IDSS = BETA*VTO^2 (swept).
.model JN NJF(VTO=-1.0 BETA={idss} LAMBDA=0.005 CGS=8p CGD=3p)
* Reset: behavioural switch (Q24/Q25/Q27/D25 comparator, not modelled):
* closes when C33 falls to V_BOT, opens 50 mV below the reset level.
.model SWR SW(VT={swvt} VH={swvh} RON={ron} ROFF=1e12)
"""


def netlist(f_hz, load, vaf=100.0, bfp=300.0, ron=100.0, idss=1.0e-3,
            vreset=11.3, t_stop=None):
    """load: 'saw', 'sq' or 'none' (which output S1 connects to the VCF).
    idss: Q28 IDSS (A); vreset: level the reset recharges C33 to (V). The
    default 11.3 V is 12 V less D25 and Q27's saturation (inferred); the
    [SN] sketch says 12 V, which is run as the bound."""
    swvt = 0.5 * (vreset - V_BOT)
    swvh = swvt - 0.05
    # IC11a sets the expo reference current; here it is sized so the ramp
    # (vreset - V_BOT across C33, plus the JFET/Early losses ~5 %) lands near f.
    i_ramp = C33 * (vreset - V_BOT) * f_hz * 0.95
    period = 1.0 / f_hz
    t_stop = t_stop or max(0.45, 30 * period)  # C11 (22 ms) needs to settle
    t_step = min(period / 2000.0, 2e-6)
    s1 = {"saw": "src", "sq": "col", "none": None}[load]
    load_lines = ""
    if s1:
        # S1 -> R62 220k -> C17 1u -> Q12 base node, R70 2.2k to the bias node
        # (C16 10u = AC ground). Q12's base current is neglected.
        load_lines = f"""
R62 {s1} n62 220k
C17 n62 vq12 1u
R70 vq12 0 2.2k
"""
    return f"""* TB-303 VCO core + Q8 shaper, f = {f_hz:.3f} Hz, load = {load}
.param vaf={vaf} bfp={bfp} ron={ron} idss={idss} swvt={swvt} swvh={swvh}
{MODELS}
VCC vcc 0 12
* --- Q26: reference side forced to I_ramp (IC11a's job), output side into C33
IREF vcc nref {i_ramp:.6e}
Q26A nref nref 0 QN
Q26B c33 nref 0 QN
C33 c33 0 0.01u IC={vreset}
* --- reset to vreset when C33 reaches V_BOT, release near the top
VRST vrst 0 {vreset}
BCTL ctl 0 V = {vreset} - V(c33)
SRST vrst c33 ctl 0 SWR
* --- Q28 2SK30 source follower into R105 (VG senses the gate current)
VG c33 g28 0
J28 vcc g28 src JN
R105 src 0 10k
* --- Q8 square shaper ([SN] p5)
R35 src nb 100k
C10 src n10 0.01u
R34 n10 nb 10k
Q8 col nb q8e QP
R45 vcc q8e 22k
C11 q8e 0 1u
R36 col 0 10k
{load_lines}
.options method=gear reltol=1e-4 abstol=1e-12 vntol=1e-6
.tran {t_step:.3e} {t_stop:.4f} 0 {t_step:.3e} uic
.control
run
wrdata {{OUTFILE}} v(c33) v(src) v(col) v(q8e) v(nb) i(vg) {'v(vq12)' if s1 else ''}
quit
.endc
.end
"""


def run(tag, text):
    os.makedirs(OUT, exist_ok=True)
    os.makedirs(RAW, exist_ok=True)
    text = text.replace("{OUTFILE}", tag + ".txt")
    with open(os.path.join(OUT, tag + ".cir"), "w") as fh:
        fh.write(text)
    cir = os.path.join(RAW, tag + ".cir")
    dat = os.path.join(RAW, tag + ".txt")
    with open(cir, "w") as fh:
        fh.write(text)
    # ngspice's wrdata cannot take a path with spaces: run inside RAW.
    r = subprocess.run(["ngspice", "-b", tag + ".cir"], cwd=RAW,
                       capture_output=True, text=True)
    if r.returncode != 0 or not os.path.exists(dat):
        sys.exit(f"ngspice failed for {tag}:\n{r.stdout[-2000:]}\n{r.stderr[-2000:]}")
    raw = np.loadtxt(dat)
    os.remove(dat)
    os.remove(cir)
    t = raw[:, 0]
    cols = raw[:, 1::2]
    names = ["c33", "src", "col", "ne", "nb", "ig", "vq12"][: cols.shape[1]]
    return t, dict(zip(names, cols.T))


# ------------------------------------------------------------------ analysis
def last_cycles(t, v, n=4):
    """Indices of the last n full cycles, split at reset (rising) edges."""
    d = np.diff(v)
    edges = np.where((d > 0.5 * (V_TOP - V_BOT)) |
                     ((v[1:] > 9.0) & (v[:-1] <= 9.0) & (d > 0)))[0]
    # collapse an edge that spans several samples into its first sample
    keep = [edges[0]] if len(edges) else []
    for e in edges[1:]:
        if t[e] - t[keep[-1]] > 1e-4:
            keep.append(e)
    keep = np.array(keep)
    if len(keep) < n + 1:
        return None
    return keep[-(n + 1):]


def resample_cycle(t, v, a, b, n=1024):
    tt = np.linspace(t[a], t[b], n, endpoint=False)
    return np.interp(tt, t, v)


def saw_metrics(t, v, edges):
    a, b = edges[-2], edges[-1]
    cyc = resample_cycle(t, v, a + 1, b + 1)
    T = t[b] - t[a]
    # the ramp: from just after the reset to just before the next one
    lo, hi = int(0.05 * len(cyc)), int(0.95 * len(cyc))
    x = np.arange(lo, hi)
    p = np.polyfit(x, cyc[lo:hi], 1)
    resid = cyc[lo:hi] - np.polyval(p, x)
    pp = cyc.max() - cyc.min()
    # slope change start -> end of the ramp (Early effect / follower)
    s0 = np.polyfit(x[:100], cyc[lo:lo + 100], 1)[0]
    s1 = np.polyfit(x[-100:], cyc[hi - 100:hi], 1)[0]
    # reset duration: samples above the 90 % line climbing back
    seg = (t >= t[b] - 2e-4) & (t <= t[b] + 2e-4)
    vs, ts = v[seg], t[seg]
    try:
        lo_v, hi_v = vs.min(), vs.max()
        i10 = np.where(vs > lo_v + 0.1 * (hi_v - lo_v))[0]
        i90 = np.where(vs > lo_v + 0.9 * (hi_v - lo_v))[0]
        t_reset = ts[i90[i90 > np.argmin(vs)][0]] - ts[i10[i10 > np.argmin(vs)][0]]
    except IndexError:
        t_reset = float("nan")
    return dict(freq=1.0 / T, vmax=float(cyc.max()), vmin=float(cyc.min()),
                vpp=float(pp), bow_pct=float(100 * np.max(np.abs(resid)) / pp),
                slope_change_pct=float(100 * (s1 - s0) / s0),
                t_reset_us=float(1e6 * t_reset)), cyc


def square_metrics(t, v, edges):
    a, b = edges[-2], edges[-1]
    cyc = resample_cycle(t, v, a + 1, b + 1)
    hi, lo = cyc.max(), cyc.min()
    mid = 0.5 * (hi + lo)
    return dict(vmax=float(hi), vmin=float(lo), vpp=float(hi - lo),
                duty_pct=float(100 * np.mean(cyc > mid))), cyc


def harm_db(cyc, k=40):
    X = np.abs(np.fft.rfft(cyc - cyc.mean()))[1:k + 1]
    return 20 * np.log10(np.maximum(X, 1e-12) / X[0])


def main():
    results = {"pitches": [], "sensitivity": []}
    cycles = {}
    for name, f in PITCHES:
        row = {"note": name, "f_nominal": f}
        for load in ("saw", "sq"):
            t, d = run(f"vco_{name}_{load}", netlist(f, load))
            e = last_cycles(t, d["c33"])
            if e is None:
                sys.exit(f"no stable cycles at {name}")
            sm, saw_c = saw_metrics(t, d["src"], e)
            qm, sq_c = square_metrics(t, d["col"], e)
            _, vq = square_metrics(t, d["vq12"], e)
            core, _ = saw_metrics(t, d["c33"], e)
            a, b = e[-2], e[-1]
            ne = resample_cycle(t, d["ne"], a + 1, b + 1)
            row[load] = dict(saw_src=sm, saw_core=core, square=qm,
                             q12_pp_mV=float(1e3 * (vq.max() - vq.min())),
                             q12_cycle=list(np.round(resample_cycle(
                                 t, d["vq12"], a + 1, b + 1, 256), 6)),
                             emitter_ripple_V=float(ne.max() - ne.min()),
                             emitter_mean_V=float(ne.mean()))
            if load == "saw":
                cycles[name] = dict(saw=saw_c, core=resample_cycle(
                    t, d["c33"], a + 1, b + 1), ne=ne, nb=resample_cycle(
                    t, d["nb"], a + 1, b + 1))
            else:
                cycles[name]["sq"] = sq_c
        results["pitches"].append(row)
        print(f"{name:3s} f={row['saw']['saw_src']['freq']:8.2f} Hz  "
              f"saw {row['saw']['saw_src']['vmin']:.2f}..{row['saw']['saw_src']['vmax']:.2f} V "
              f"bow {row['saw']['saw_src']['bow_pct']:.2f}%  "
              f"slope {row['saw']['saw_src']['slope_change_pct']:+.1f}%  "
              f"reset {row['saw']['saw_core']['t_reset_us']:.1f} us | "
              f"sq {row['sq']['square']['vmin']:.2f}..{row['sq']['square']['vmax']:.2f} V "
              f"duty {row['sq']['square']['duty_pct']:.1f}% | "
              f"Q12 saw {row['saw']['q12_pp_mV']:.1f} mVpp sq {row['sq']['q12_pp_mV']:.1f} mVpp")

    # sensitivity of the duty and the saw to the assumed device parameters
    for name, f in (("C1", 32.703), ("C2", 65.406), ("C5", 523.25)):
        for bfp in (150.0, 300.0, 600.0):
            t, d = run(f"sens_{name}_bf{int(bfp)}", netlist(f, "sq", bfp=bfp))
            e = last_cycles(t, d["c33"])
            qm, _ = square_metrics(t, d["col"], e)
            results["sensitivity"].append(dict(note=name, param="Q8 BF", value=bfp,
                                               duty_pct=qm["duty_pct"], vmax=qm["vmax"]))
        for vaf in (30.0, 100.0, 300.0):
            t, d = run(f"sens_{name}_vaf{int(vaf)}", netlist(f, "none", vaf=vaf))
            e = last_cycles(t, d["c33"])
            sm, _ = saw_metrics(t, d["src"], e)
            results["sensitivity"].append(dict(note=name, param="Q26 VAF", value=vaf,
                                               bow_pct=sm["bow_pct"],
                                               slope_change_pct=sm["slope_change_pct"]))
    # the saw at the Q28 source against Q28 IDSS and the reset level (C2)
    for vr in (11.3, 12.0):
        for idss in (0.6e-3, 1.0e-3, 1.4e-3, 3.0e-3):
            t, d = run(f"sens_C2_idss{idss*1e4:.0f}_vr{vr*10:.0f}",
                       netlist(65.406, "none", idss=idss, vreset=vr))
            e = last_cycles(t, d["c33"])
            sm, _ = saw_metrics(t, d["src"], e)
            results["sensitivity"].append(dict(
                note="C2", param="Q28 IDSS / reset level", value=[idss, vr],
                src_min=sm["vmin"], src_max=sm["vmax"], bow_pct=sm["bow_pct"],
                slope_change_pct=sm["slope_change_pct"]))
    for s in results["sensitivity"]:
        print("  sens", s)

    with open(os.path.join(HERE, "vco_q8_results.json"), "w") as fh:
        json.dump(results, fh, indent=1)
    plot(results, cycles)


def plot(results, cycles):
    import lxr_plotlib as L
    plt = L.setup_matplotlib()
    fig, ax = plt.subplots(3, 2, figsize=(13, 12))
    ph = np.arange(1024) / 1024
    cols = dict(zip([p[0] for p in PITCHES], L.PALETTE[1:]))

    a = ax[0, 0]
    for n in ("C1", "C2", "C5"):
        a.plot(ph, cycles[n]["saw"], color=cols[n], lw=1.4, label=f"{n} Q28 source")
    a.set_title("Saw at the Q28 source (one cycle from reset)")
    a.set_xlabel("phase"); a.set_ylabel("V"); a.legend()

    a = ax[0, 1]
    for n in ("C1", "C2", "C5"):
        c = cycles[n]["saw"]
        x = np.arange(51, 973)
        p = np.polyfit(x, c[51:973], 1)
        a.plot(ph[51:973], 1e3 * (c[51:973] - np.polyval(p, x)), color=cols[n], label=n)
    a.set_title("Saw: deviation from a straight line (mV)")
    a.set_xlabel("phase"); a.set_ylabel("mV"); a.legend()

    a = ax[1, 0]
    for n, _ in PITCHES:
        a.plot(ph, cycles[n]["sq"], color=cols[n], lw=1.3, label=n)
    a.set_title("Square at the Q8 collector (same cycle alignment as the saw)")
    a.set_xlabel("phase"); a.set_ylabel("V"); a.legend(ncol=3)

    a = ax[1, 1]
    f = [r["saw"]["saw_src"]["freq"] for r in results["pitches"]]
    duty = [r["sq"]["square"]["duty_pct"] for r in results["pitches"]]
    a.semilogx(f, duty, "o-", color=L.PALETTE[1], label="SPICE (Q8 BF 300)")
    for bf, mk in ((150.0, "v"), (600.0, "^")):
        pts = [(s["note"], s["duty_pct"]) for s in results["sensitivity"]
               if s["param"] == "Q8 BF" and s["value"] == bf]
        fx = [dict(PITCHES)[n] for n, _ in pts]
        a.semilogx(fx, [d for _, d in pts], mk, color=L.PALETTE[3], label=f"BF {bf:.0f}")
    a.axhline(71, color=L.PALETTE[4], ls=":", lw=1)
    a.axhline(45, color=L.PALETTE[4], ls=":", lw=1)
    a.text(f[0], 72, "[OL] lowest ≈ 71 %", fontsize=8, color=L.PALETTE[4])
    a.text(f[-2], 46, "[OL] high ≈ 45 %", fontsize=8, color=L.PALETTE[4])
    a.set_title("Square duty (time above mid-level) against pitch")
    a.set_xlabel("Hz"); a.set_ylabel("%"); a.legend(loc="center left")

    a = ax[2, 0]
    for n in ("C1", "C2", "C5"):
        a.plot(ph, cycles[n]["ne"], color=cols[n], label=f"{n} emitter")
        a.plot(ph, cycles[n]["nb"], color=cols[n], ls="--", lw=0.9, label=f"{n} base")
    a.set_title("Q8 emitter (C11) and base over one cycle")
    a.set_xlabel("phase"); a.set_ylabel("V"); a.legend(ncol=2, fontsize=7)

    a = ax[2, 1]
    k = np.arange(1, 41)
    for n in ("C1", "C2", "C5"):
        a.plot(k, harm_db(cycles[n]["sq"]), "o-", ms=2.5, color=cols[n], label=f"square {n}")
    a.plot(k, harm_db(cycles["C2"]["saw"]), "-", color="#8a96a3", label="saw C2")
    a.plot(k, -20 * np.log10(k), ":", color="#3a3a3a", lw=1, label="1/k")
    a.set_ylim(-60, 5)
    a.set_title("Harmonics re. fundamental (dB)")
    a.set_xlabel("harmonic"); a.legend(ncol=2, fontsize=7)

    fig.suptitle("TB-303 VCO core + Q8 shaper — ngspice, [SN] p5 values, generic devices (S092 P1)",
                 fontsize=13, fontweight="bold")
    fig.tight_layout(rect=(0, 0, 1, 0.97))
    out = os.path.join(ROOT, "tools", "plots", "303_vco_q8_spice.png")
    fig.savefig(out, dpi=105)
    print("wrote", out)


if __name__ == "__main__":
    main()
