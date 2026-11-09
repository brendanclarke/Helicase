#!/usr/bin/env python3
"""TB-303 VCA: envelope generator (Q31/Q32) and IC15 networks, ngspice (S092 P3).

What:       transistor-level VEG from the Roland Service Notes p5 (trigger
            D36/R134/C41 -> Q32 -> R132 -> C42 1u <- R123 1.5M; Q31 PNP
            through R131 220k / R133 2.2k from the gate network D34/R137/D35)
            delivering IC15's control current (pin 1: a diode-connected input); the
            IC15 input networks (R121/C21, R122/C22 into R124) and output
            load (R127/R128/R129 to the C40 node, buffer, C38); a tanh OTA
            (BA662A) for the linearity check.
Why:        S092_P1 §8 step 3; record: S092_P3_VCB_MODEL_VCA.md.
Inputs:     values from [SN] p5 (see the document); generic devices.
Outputs:    tools/spice/tb303/out/vca_*.cir, vca_results.json,
            tools/plots/303_vca_spice.png (raw data deleted after load).
Accessors:  python3 tools/spice/tb303/vca.py   (ngspice, numpy, matplotlib)
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

MODELS = """
.model QN NPN(IS=1e-14 BF=300 VAF=100 CJE=8p CJC=4p TF=0.4n RB=50)
.model QP PNP(IS=1e-14 BF=200 VAF=80 CJE=8p CJC=5p TF=0.6n RB=50)
.model DS D(IS=2.5n N=1.8 RS=0.5 CJO=2p)
"""


def run(tag, text):
    os.makedirs(OUT, exist_ok=True)
    os.makedirs(RAW, exist_ok=True)
    text = text.replace("{OUTFILE}", tag + ".txt")
    with open(os.path.join(OUT, tag + ".cir"), "w") as fh:
        fh.write(text)
    cir, dat = os.path.join(RAW, tag + ".cir"), os.path.join(RAW, tag + ".txt")
    with open(cir, "w") as fh:
        fh.write(text)
    r = subprocess.run(["ngspice", "-b", tag + ".cir"], cwd=RAW, capture_output=True, text=True)
    if r.returncode != 0 or not os.path.exists(dat):
        sys.exit(f"ngspice failed for {tag}:\n{r.stdout[-2500:]}\n{r.stderr[-1500:]}")
    raw = np.loadtxt(dat)
    os.remove(dat)
    os.remove(cir)
    return raw


def veg_netlist(vg, t_trig, gate_on, gate_off, t_stop, retrig=None, t_step=20e-6, acc=None):
    """vg: gate high level (V). Trigger 0 -> 5.333 V for t_trig at t=10 ms
    (and again at `retrig` if given); gate high from gate_on to gate_off.
    acc: None, or the Accent pot (VR7) position 0..1. The accented MEG
    (10.2 V peak, [XB]; decaying 200 ms to 10 %, P0 A5/A6) drives VR7; the
    wiper feeds R119 47k -> C36 0.033u -> R120 22k -> D27 into pin 1."""
    acc_lines = ""
    if acc is not None:
        r_top = max(50e3 * (1.0 - acc), 1.0)
        r_bot = max(50e3 * acc, 1.0)
        acc_lines = f"""
* --- accent: accented MEG -> VR7 (50k B) -> R119 -> C36 -> R120 -> D27 -> pin 1
BMEG meg 0 V = (time > 10.5m) ? 10.2 * exp(-(time - 10.5m) / 86.9m) : 0
RV7T meg wip7 {r_top:.3f}
RV7B wip7 0 {r_bot:.3f}
R119 wip7 nc36 47k
C36 nc36 0 0.033u
R120 nc36 n120 22k
D27 n120 abc DS
"""
    trig = f"PWL(0 0 10m 0 {10e-3 + 1e-6:.7f} 5.333 {10e-3 + t_trig:.7f} 5.333 {10e-3 + t_trig + 1e-6:.7f} 0"
    if retrig:
        trig += (f" {retrig:.7f} 0 {retrig + 1e-6:.7f} 5.333 {retrig + t_trig:.7f} 5.333 "
                 f"{retrig + t_trig + 1e-6:.7f} 0")
    trig += ")"
    gate = (f"PWL(0 0 {gate_on:.7f} 0 {gate_on + 1e-6:.7f} {vg} {gate_off:.7f} {vg} "
            f"{gate_off + 1e-6:.7f} 0)")
    return f"""* TB-303 VEG: vg={vg} t_trig={t_trig} gate {gate_on}-{gate_off}
{MODELS}
VCC vcc 0 12
* --- trigger -> D36 -> R134 22k -> Q32 base (C41 0.1u)
VTRIG trig 0 {trig}
D36 trig n36 DS
R134 n36 b32 22k
C41 b32 0 0.1u
Q32 c32 b32 e32 QN
R130 e32 0 100
R132 c32 n42 100
* --- C42 1u, recharged through R123 1.5M from +12 V (assumed rail)
C42 n42 0 1u IC=11.9
R123 vcc n42 1.5Meg
* --- gate -> D34 -> R137 1k -> D35 -> R133 2.2k -> R131 220k -> Q31 emitter
VGATE gate 0 {gate}
D34 gate n34 DS
R137 n34 n137 1k
D35 n137 n35 DS
R133 n35 n133 2.2k
R131 n133 e31 220k
Q31 abc n42 e31 QP
* --- IC15 pin 1 (amplifier bias input): a current-mirror input, i.e. a
*     diode-connected transistor to V- (pin 5, ground). It can only sink
*     current; VABC (0 V) measures it.
VABC abc abc2 0
QABC abc2 abc2 0 QN
{acc_lines}
.options method=gear reltol=1e-4
.tran {t_step:.3e} {t_stop:.4f} 0 {t_step:.3e} uic
.control
run
wrdata {{OUTFILE}} i(VABC) v(n42) v(b32) v(e31)
quit
.endc
.end
"""


def ac_networks():
    return f"""* IC15 input and output networks
{MODELS}
VREF vref 0 5.333
* inputs: A via R121 220k + C21 0.01u, B via R122 100k + C22 0.01u, into pin 3
VA sa 0 DC 0 AC 1
VB sb 0 DC 0 AC 0
R121 sa n121 220k
C21 n121 p3 0.01u
R122 sb n122 100k
C22 n122 p3 0.01u
R124 p3 nb 2.2k
R125 p2 nb 2.2k
R126 vref nb 2.2k
C37 nb 0 10u
* output: OTA current into R127 47k + R129 22k || R128 220k to the C40 node
IOTA 0 p6 DC 0 AC 1e-6
R127 p6 n127 47k
R129 n127 n40 22k
R128 p6 n40 220k
R162 vref n40 2.2k
C40 n40 0 10u
* buffer (pins 7 -> 8), C38 1u into VR8 volume 50k (A) at full
EBUF p8 0 p6 0 1
C38 p8 nv 1u
RVOL nv 0 50k
.ac dec 60 2 22k
.control
run
wrdata {{OUTFILE}} vdb(p3) vdb(nv) v(p3)
alter VA ac=0
alter VB ac=1
run
wrdata {{OUTFILE}}b vdb(p3) v(p3)
quit
.endc
.end
"""


def main():
    res = {}
    curves = {}

    # ---------- 1. input / output networks
    text = ac_networks().replace("{OUTFILE}b", "vca_acb.txt")
    os.makedirs(RAW, exist_ok=True)
    raw = run("vca_ac", text)
    rawb = np.loadtxt(os.path.join(RAW, "vca_acb.txt"))
    os.remove(os.path.join(RAW, "vca_acb.txt"))
    f = raw[:, 0]
    a_db, out_db, b_db = raw[:, 1], raw[:, 3], rawb[:, 1]
    ha = raw[:, 5] + 1j * raw[:, 6]          # A -> pin 3, complex
    hb = rawb[:, 3] + 1j * rawb[:, 4]        # B -> pin 3, complex

    def corner(f, db):
        top = db[-1]
        i = np.where(db < top - 3.0)[0]
        return float(f[i[-1]]), float(top)

    ca, ga = corner(f, a_db)
    cb, gb = corner(f, b_db)
    co, go = corner(f, out_db)
    res["networks"] = dict(input_A_hp_hz=ca, input_A_gain_db=ga, input_B_hp_hz=cb,
                           input_B_gain_db=gb, output_hp_hz=co,
                           output_transimpedance_db_ohm=float(go + 120.0))
    # A = TP6 (filter output), B = VR4's wiper = k x TP6 (user's trace; pot
    # loading neglected): the VCA input is (H_A + k H_B) x TP6
    res["combined"] = []
    curves["comb"] = {}
    for k in (0.0, 0.25, 0.5, 1.0):
        h = ha + k * hb
        db = 20 * np.log10(np.abs(h))
        c_k, g_k = corner(f, db)
        res["combined"].append(dict(k_res=k, hp_3db_hz=c_k, level_db=g_k,
                                    db_at_65hz=float(np.interp(65.4, f, db) - g_k),
                                    db_at_33hz=float(np.interp(32.7, f, db) - g_k)))
        curves["comb"][k] = db - g_k
    print("combined VCA input:", res["combined"])
    curves["ac"] = (f, a_db - ga, b_db - gb, out_db - go)
    print("networks:", res["networks"])

    # ---------- 2. VEG: one long note, gate level 12 V and 5.333 V
    res["veg"] = {}
    for vg in (12.0, 5.333):
        raw = run(f"vca_veg_long_vg{int(vg * 10)}", veg_netlist(vg, 1e-3, 5e-3, 6.0, 6.0, t_step=200e-6))
        t, iabc = raw[:, 0], np.abs(raw[:, 1])
        m = t > 0.0105
        pk = float(iabc[m].max())
        tpk = float(t[m][np.argmax(iabc[m])])
        after = (t > tpk)
        end = np.where(after & (iabc < pk * 1e-3))[0]
        t_end = float(t[end[0]] - tpk) if len(end) else float("nan")
        db = 20 * np.log10(np.maximum(iabc / pk, 1e-6))
        def t_at(level_db):
            j = np.where(after & (db < level_db))[0]
            return float(t[j[0]] - tpk) if len(j) else float("nan")
        res["veg"][f"long_vg{vg}"] = dict(gate_v=vg, peak_uA=1e6 * pk, t_m6db_s=t_at(-6),
                                          t_m20db_s=t_at(-20), t_m40db_s=t_at(-40),
                                          t_m60db_s=t_at(-60), t_silent_s=t_end)
        curves[f"long_{vg}"] = (t - tpk, iabc / pk)
        print(f"VEG long, gate {vg} V:", res["veg"][f"long_vg{vg}"])

    # ---------- 3. attack against trigger width (gate 12 V)
    # S093 P6: the 0.70 -> 0.55 ms range of the Alkaline atk knob (nine
    # points, alk_constants.json veg.att_table) beside the coarse S092 widths
    res["attack"] = []
    widths = (50e-6, 200e-6, 500e-6, 1e-3, 2e-3, 5e-3) + tuple(
        round(0.70e-3 - q * (0.15e-3 / 8.0), 9) for q in range(9))
    for tw in widths:
        raw = run(f"vca_att_{int(tw * 1e6)}us", veg_netlist(12.0, tw, 5e-3, 0.5, 0.06, t_step=5e-6))
        t, iabc, v42 = raw[:, 0], np.abs(raw[:, 1]), raw[:, 3]
        m = t > 0.01
        pk = float(iabc[m].max())
        i10 = np.where(m & (iabc > 0.1 * pk))[0][0]
        i90 = np.where(m & (iabc > 0.9 * pk))[0][0]
        res["attack"].append(dict(trigger_ms=tw * 1e3, peak_uA=1e6 * pk,
                                  attack_10_90_ms=float(1e3 * (t[i90] - t[i10])),
                                  c42_min_V=float(v42[m].min())))
        curves[f"att_{tw}"] = (t - 0.01, iabc)
        print("attack:", res["attack"][-1])

    # ---------- 4. release: a 16th at 120 BPM with the 58 % gate (72.5 ms)
    gate_len = 0.58 * 0.125
    raw = run("vca_release", veg_netlist(12.0, 1e-3, 9e-3, 9e-3 + gate_len, 0.2, t_step=5e-6))
    t, iabc = raw[:, 0], np.abs(raw[:, 1])
    t_off = 9e-3 + gate_len
    before = float(np.interp(t_off - 1e-4, t, iabc))
    j90 = np.where((t > t_off) & (iabc < 0.9 * before))[0][0]
    j10 = np.where((t > t_off) & (iabc < 0.1 * before))[0][0]
    res["release"] = dict(gate_ms=1e3 * gate_len, level_at_gate_off_uA=1e6 * before,
                          release_90_10_us=float(1e6 * (t[j10] - t[j90])))
    curves["release"] = (t, iabc)
    print("release:", res["release"])

    # ---------- 5. retrigger: two 16ths 125 ms apart, gate held (legato) and not
    raw = run("vca_retrig", veg_netlist(12.0, 1e-3, 5e-3, 0.30, 0.30, retrig=0.135, t_step=20e-6))
    curves["retrig"] = (raw[:, 0], np.abs(raw[:, 1]))

    # ---------- 5b. accent into pin 1 (gate 12 V, 1 ms trigger)
    res["accent"] = []
    curves["acc"] = {}
    for a in (None, 0.5, 1.0):
        raw = run(f"vca_acc_{'none' if a is None else int(a * 100)}",
                  veg_netlist(12.0, 1e-3, 5e-3, 1.5, 1.0, t_step=50e-6, acc=a))
        t, iabc = raw[:, 0], np.abs(raw[:, 1])
        m = t > 0.0105
        pk = float(iabc[m].max())
        i200 = float(np.interp(0.2105, t, iabc))
        res["accent"].append(dict(vr7=a, peak_uA=1e6 * pk, at_200ms_uA=1e6 * i200))
        curves["acc"][a] = (t - 0.0105, iabc)
    base = res["accent"][0]["peak_uA"]
    for d in res["accent"]:
        d["peak_re_normal_db"] = float(20 * np.log10(d["peak_uA"] / base))
    print("accent:", res["accent"])

    # ---------- 6. OTA linearity at the [SN] p5 output level (0.6 V p-p at TP6)
    vin = 0.6 / 2 * 10 ** (res["networks"]["input_A_gain_db"] / 20)   # peak at pin 3
    x = np.linspace(0, 2 * np.pi, 4096, endpoint=False)
    y = np.tanh(vin * np.sin(x) / (2 * 0.02585))
    X = np.abs(np.fft.rfft(y))
    res["ota"] = dict(input_peak_mV=1e3 * vin, thd3_db=float(20 * np.log10(X[3] / X[1])),
                      gain_compression_db=float(20 * np.log10(X[1] / (len(x) / 2) /
                                                              (vin / (2 * 0.02585)))))
    print("OTA:", res["ota"])

    with open(os.path.join(HERE, "vca_results.json"), "w") as fh:
        json.dump(res, fh, indent=1)
    plot(res, curves)


def plot(res, curves):
    import lxr_plotlib as L
    plt = L.setup_matplotlib()
    fig, ax = plt.subplots(3, 2, figsize=(13, 12))
    c = L.PALETTE[1:]

    a = ax[0, 0]
    for vg, col in ((12.0, c[0]), (5.333, c[1])):
        t, g = curves[f"long_{vg}"]
        a.plot(t, 20 * np.log10(np.maximum(g, 1e-4)), color=col, label=f"gate {vg:g} V")
    a.set_xlim(-0.05, 4.0); a.set_ylim(-70, 3)
    a.set_title("VEG: control current re. peak (dB), gate held")
    a.set_xlabel("s after the peak"); a.legend()

    a = ax[0, 1]
    for vg, col in ((12.0, c[0]), (5.333, c[1])):
        t, g = curves[f"long_{vg}"]
        a.plot(t, g, color=col, label=f"gate {vg:g} V")
    a.set_xlim(-0.05, 4.0)
    a.set_title("VEG: control current re. peak (linear)")
    a.set_xlabel("s after the peak"); a.legend()

    a = ax[1, 0]
    for tw, col in zip((50e-6, 200e-6, 500e-6, 1e-3, 2e-3, 5e-3), c):
        t, i = curves[f"att_{tw}"]
        a.plot(1e3 * t, 1e6 * i, color=col, label=f"trigger {tw * 1e3:g} ms")
    a.set_xlim(-1, 15)
    a.set_title("Attack against trigger width (gate 12 V)")
    a.set_xlabel("ms after the trigger"); a.set_ylabel("µA"); a.legend(fontsize=7)

    a = ax[1, 1]
    t, i = curves["release"]
    m = t > 5e-3                     # skip the initial-condition start (uic)
    a.plot(1e3 * t[m], 1e6 * i[m], color=c[0])
    a.set_title(f"Release: 58 % gate of a 16th at 120 BPM; 90→10 % in "
                f"{res['release']['release_90_10_us']:.0f} µs", fontsize=10)
    a.set_xlabel("ms"); a.set_ylabel("µA")

    a = ax[2, 0]
    for (acc, (t, i)), col in zip(curves["acc"].items(), c):
        m = t > -2e-3
        a.plot(1e3 * t[m], 1e6 * i[m], color=col,
               label="no accent" if acc is None else f"accent, VR7 {acc:g}")
    a.set_xlim(-5, 600)
    a.set_title("Pin-1 current with accent (MEG 10.2 V, 200 ms decay)")
    a.set_xlabel("ms after the trigger"); a.set_ylabel("µA"); a.legend(fontsize=7)

    a = ax[2, 1]
    f, adb, bdb, odb = curves["ac"]
    a.semilogx(f, adb, color=c[0], label=f"input A (R121/C21): HP {res['networks']['input_A_hp_hz']:.0f} Hz")
    a.semilogx(f, bdb, color=c[1], label=f"input B (R122/C22): HP {res['networks']['input_B_hp_hz']:.0f} Hz")
    a.semilogx(f, odb, color=c[3], label=f"output (C38 → volume): HP {res['networks']['output_hp_hz']:.1f} Hz")
    for k, ls in ((0.5, "--"), (1.0, ":")):
        hp = [d["hp_3db_hz"] for d in res["combined"] if d["k_res"] == k][0]
        a.semilogx(f, curves["comb"][k], color="#3a3a3a", ls=ls, lw=1.2,
                   label=f"A + B at Res {k:g} (TP6 → pin 3): HP {hp:.0f} Hz")
    a.set_ylim(-30, 2)
    a.set_title("IC15 input and output networks (dB re. passband)")
    a.set_xlabel("Hz"); a.legend(fontsize=7)

    fig.suptitle("TB-303 VCA — ngspice, [SN] p5 values, generic devices (S092 P3)",
                 fontsize=13, fontweight="bold")
    fig.tight_layout(rect=(0, 0, 1, 0.97))
    out = os.path.join(ROOT, "tools", "plots", "303_vca_spice.png")
    fig.savefig(out, dpi=105)
    print("wrote", out)


if __name__ == "__main__":
    main()
