#!/usr/bin/env python3
"""TB-303 end-to-end run: sequencer -> CV/slide -> VCO -> VCF -> VCA, ngspice (S092 P4).

What:       one netlist joining the S092 P1-P4 circuit models: the pitch DAC
            output impedance and the slide switches with C35 (P0 A9), a VCO
            whose ramp current follows the CV at 1 V/oct (behavioural; the
            core, reset, Q28 and Q8 are as P1), the VCF (P2, R110 22k), the
            control network (P4) driving the ladder through R69, and the VCA
            (P3) with its accent branch from VR7. An 8-step 120 BPM pattern
            with normal, accented, slid and rested steps runs after a 1 s
            settling pre-roll. Writes per-step measurements, a plot, the raw
            node data summary and a WAV file of the output.
Why:        S092_P4_CONTROL_NET.md §6; record: S092_P4_SLIDE_ACCENT_RUN.md.
Inputs:     the part values of the P1-P4 models (see those documents).
Outputs:    tools/spice/tb303/out/e2e_*.cir, e2e_results.json, traces
            (<tempdir>/lxr_spice_tb303/e2e_<tag>_traces.npz, for S092 P6),
            tools/plots/303_e2e_run_<tag>.png, tools/spice/tb303/e2e_<tag>.wav.
Accessors:  python3 tools/spice/tb303/e2e.py   (ngspice, numpy, matplotlib)
"""
import json
import os
import subprocess
import sys
import tempfile
import wave

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
OUT = os.path.join(HERE, "out")
RAW = os.path.join(tempfile.gettempdir(), "lxr_spice_tb303")
sys.path.insert(0, os.path.join(ROOT, "tools", "plots"))
sys.path.insert(0, HERE)
from control import taper_a  # noqa: E402

BPM = 120.0
STEP = 60.0 / BPM / 4.0           # a 16th: 0.125 s
GATE = 3.5 / 6.0 * STEP            # P0 D11
TRIG = 0.70e-3                     # P3 §3.2: gives the decided 2.5-3 ms VCA attack
STRIKE = 1.3e-3                    # P4: MEG strike (behavioural, [XB])
T0 = 1.0                           # settling pre-roll
TM3 = 152.74e3                     # P4 §3.1 calibration
# Ramp current for C2 (CV 2.0 V): the P1 estimate, trimmed once so C2 measures 65.4 Hz
# in this netlist (the first run read 60.95 Hz: x 1.0731)
I_C2 = 0.01e-6 * (11.3 - 5.5) * 65.406 * 0.9182 * 1.0731
FS_OUT = 88200.0                   # output sampling of the transient (WAV at 44,100)

NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]

# (semitones above C2, accent, slide) per step; None = rest
PATTERN = [
    (0, False, False),     # 1  C2
    (0, True, False),      # 2  C2  accent
    (3, False, True),      # 3  D#2 slide -> 4
    (7, False, False),     # 4  G2  (slid into)
    (12, True, True),      # 5  C3  accent, slide -> 6
    (10, True, False),     # 6  A#2 accent (slid into)
    None,                  # 7  rest
    (0, True, False),      # 8  C2  accent
]

MODELS = """
.model QN NPN(IS=1e-14 BF=300 VAF=100 CJE=8p CJC=4p TF=0.4n RB=50)
.model QN21 NPN(IS=1e-14 BF=300 VAF=100 CJE=8p CJC=4p TF=0.4n RB=50)
.model QNC NPN(IS=1e-14 BF=240 VAF=100 CJE=8p CJC=4p TF=0.4n RB=50)
.model QP PNP(IS=1e-14 BF=300 VAF=60 CJE=8p CJC=5p TF=0.6n RB=50)
.model QP8 PNP(IS=1e-14 BF=300 VAF=80 CJE=8p CJC=5p TF=0.6n RB=50)
.model QPV PNP(IS=1e-14 BF=200 VAF=80 CJE=8p CJC=5p TF=0.6n RB=50)
.model DS D(IS=2.52n N=1.752 RS=0.5 CJO=2p)
.model JN NJF(VTO=-1.0 BETA=1.0e-3 LAMBDA=0.005 CGS=8p CGD=3p)
.model JY NJF(VTO=-1.5 BETA=0.889e-3 LAMBDA=0.005)
.model S4066 SW(VT=2.5 VH=0.1 RON=250 ROFF=1e10)
.model SWR SW(VT=2.9 VH=2.85 RON=100 ROFF=1e12)
"""


def pot(name, p1, w, p3, total, frac):
    a = max(total * frac, 1.0)
    b = max(total * (1.0 - frac), 1.0)
    return f"R{name}A {p1} {w} {a:.3f}\nR{name}B {w} {p3} {b:.3f}\n"


def vr4a(res):
    """S093 P6: VR4A for res <= 1 as before; above 1 the pot at its top plus
    an ideal transformer at the wiper (alk_model.FeedbackNet): E sets
    V(f_wip) = res V(tp6), V_X senses the secondary current, F_X draws res x
    that current from tp6; 50 k (the pot) loads the wiper."""
    if res <= 1.0:
        return pot("VR4A", "0", "f_wip", "tp6", 50e3, res)
    return (f"E_X4 f_wipx 0 tp6 0 {res:.6f}\nV_X4 f_wipx f_wip 0\n"
            f"F_X4 tp6 0 V_X4 {res:.6f}\nR_X4 f_wip 0 50k\n")


def pwl(points):
    """points: list of (t, v), sorted; returns a PWL() string with steps of 1 us."""
    out = []
    for t, v in points:
        out.append(f"{t:.7f} {v:.5g}")
    return "PWL(" + " ".join(out) + ")"


def build_events():
    """Returns the PWL point lists for the DAC CV, gate, trigger, MEG strike,
    accent and slide, plus a per-step description."""
    cv, gate, trig, strike, acc, slide = [(0, 2.0)], [(0, 0)], [(0, 0)], [(0, 0)], [(0, 0)], [(0, 0)]
    steps = []
    gate_on_until = None
    prev_slide = False
    cur_cv = 2.0
    eps = 1e-6
    gate_intervals = []
    acc_iv, slide_iv = [], []
    for k, st in enumerate(PATTERN):
        t = T0 + k * STEP
        if st is None:
            steps.append(dict(step=k + 1, rest=True))
            prev_slide = False
            continue
        semi, a, s = st
        new_cv = 2.0 + semi / 12.0
        cv += [(t - eps, cur_cv), (t, new_cv)]
        cur_cv = new_cv
        legato = prev_slide
        if not legato:
            trig += [(t, 0), (t + eps, 5.333), (t + TRIG, 5.333), (t + TRIG + eps, 0)]
            strike += [(t, 0), (t + eps, 11.85), (t + STRIKE, 11.85), (t + STRIKE + eps, 0)]
        if a:
            acc_iv.append([t, t + STEP])
        if legato:
            # the slide switch engages just before the DAC steps, so C35 holds the
            # old CV when it meets the DAC node (no edge spike)
            slide_iv.append([t - 1e-4, t + STEP - 1e-4])
        g_end = t + STEP + GATE if s else t + GATE      # slide: held into the next step's gate
        if legato and gate_intervals:
            gate_intervals[-1][1] = max(gate_intervals[-1][1], g_end)
        else:
            gate_intervals.append([t, g_end])
        name = NOTE_NAMES[semi % 12] + str(2 + semi // 12)
        steps.append(dict(step=k + 1, rest=False, note=name, semi=semi, accent=a, slide=s,
                          legato=legato, target_hz=65.406 * 2 ** (semi / 12.0)))
        prev_slide = s
    def merged(iv):
        out_ = []
        for a_, b_ in iv:
            if out_ and a_ <= out_[-1][1] + 1e-6:
                out_[-1][1] = max(out_[-1][1], b_)
            else:
                out_.append([a_, b_])
        return out_
    for a_, b_ in gate_intervals:
        gate += [(a_, 0), (a_ + eps, 12.0), (b_, 12.0), (b_ + eps, 0)]
    for a_, b_ in merged(acc_iv):
        acc += [(a_, 0), (a_ + eps, 5.0), (b_ - eps, 5.0), (b_, 0)]
    for a_, b_ in merged(slide_iv):
        slide += [(a_, 0), (a_ + eps, 5.0), (b_ - eps, 5.0), (b_, 0)]
    return dict(cv=cv, gate=gate, trig=trig, strike=strike, acc=acc, slide=slide), steps, gate_intervals


def netlist(ev, knobs, t_stop, tag):
    cut, res, env, dec, accent, wave_ = (knobs[k] for k in ("cut", "res", "env", "dec", "acc", "wave"))
    s1 = "v_src" if wave_ == "saw" else "v_col"
    return f"""* TB-303 end-to-end run {tag}: {knobs}
{MODELS}
VCC vcc 0 12
VREF vref 0 5.333
* ===== sequencer outputs (behavioural)
VDAC dac 0 {pwl(ev['cv'])}
VGATE gate 0 {pwl(ev['gate'])}
VTRIG trig 0 {pwl(ev['trig'])}
VSTR c_str 0 {pwl(ev['strike'])}
VACC acc 0 {pwl(ev['acc'])}
VSLIDE slide 0 {pwl(ev['slide'])}
BSLN slide_n 0 V = 5 - V(slide)
* ===== pitch CV and slide (P0 A9): DAC (R-2R, ~100k) -> IC11b follower; C35 0.22u
*       on the follower output (slide off) or on the DAC node (slide on)
RDAC dac ndac 100k
ECV cvout 0 ndac 0 1
SSL1 ndac nc35 slide 0 S4066
SSL2 cvout nc35 slide_n 0 S4066
C35 nc35 0 0.22u
* ===== VCO (P1): ramp current at 1 V/oct from the CV (behavioural expo), C33,
*       reset to 11.3 V at 5.5 V, Q28 follower, Q8 shaper
BRAMP v_c33 0 I = {I_C2:.6e} * pow(2, V(cvout) - 2.0)
C33 v_c33 0 0.01u
VRST v_vrst 0 11.3
BCTL v_ctl 0 V = 11.3 - V(v_c33)
SRST v_vrst v_c33 v_ctl 0 SWR
VG v_c33 v_g28 0
J28 vcc v_g28 v_src JN
R105 v_src 0 10k
R35 v_src v_nb8 100k
C10 v_src v_n10 0.01u
R34 v_n10 v_nb8 10k
Q8 v_col v_nb8 v_q8e QP8
R45 vcc v_q8e 22k
C11 v_q8e 0 1u
R36 v_col 0 10k
* ===== VCF (P2, R110 = 22k); S1 -> R62
R62 {s1} f_n62 220k
C17 f_n62 f_inL 1u
R70 f_inL f_nb 2.2k
R98 f_inR f_nb 2.2k
R68 vref f_nb 2.2k
C16 f_nb 0 10u
Q12L f_aL f_inL f_tail QN
Q12R f_aR f_inR f_tail QN
R95 vcc f_vtop 100
C28 f_vtop 0 47u
R115 f_vtop f_b22 10k
C30 f_b22 0 10u
Q22L f_vtop f_b22 f_dL QN
Q22R f_vtop f_b22 f_dR QN
Q16 f_dL f_dL f_cL QN
Q23 f_dR f_dR f_cR QN
Q14 f_cL f_cL f_bL QN
Q17 f_cR f_cR f_bR QN
Q13 f_bL f_bL f_aL QN
Q15 f_bR f_bR f_aR QN
C26 f_dL f_dR 0.033u
C24 f_cL f_cR 0.033u
C19 f_bL f_bR 0.033u
C18 f_aL f_aR 0.018u
C27 f_dR f_b21L 0.1u
C25 f_dL f_b21R 0.1u
R113 f_b21L f_nb21 100k
R114 f_b21R f_nb21 100k
R116 vref f_nb21 10k
C29 f_nb21 0 1u
Q21L f_c21L f_b21L f_t21 QN21
Q21R f_c21R f_b21R f_t21 QN21
R111 f_vtop f_c21L 2.2k
R110 f_vtop f_c21R 22k
Q20 f_t21 f_b20 f_e20 QN
R112 f_nb21 f_b20 10k
R109 f_b20 0 10k
R108 f_e20 0 2.2k
Q19 f_vtop f_c21R f_out19 QN
R96 f_out19 0 10k
C14 f_out19 tp6 1u
{vr4a(res)}C15 f_wip f_n15 1u
R67 f_n15 f_b18 2.2k
R99 f_b18 f_nb18 100k
R66 vref f_nb18 100k
C22F f_nb18 0 1u
Q18 vcc f_b18 f_q18e QN
R94 f_q18e 0 10k
C23 f_q18e f_n23 1u
R97 f_n23 f_inR 10k
* ===== control network (P4)
D37 c_str c_n37 DS
R152 c_n37 c_c62 100
C62 c_c62 0 1u
R138 c_c62 c_nx 68k
RVR6 c_nx c_ny {max(1e6 * taper_a(dec), 1.0):.3f}
SDEC c_nx c_ny acc 0 S4066
DC28 c_ny c_nj DS
DC27 c_nj c_nk DS
DC26 c_nk 0 DS
R117 vref c_nj 22k
Q41 vcc c_c62 c_e41 QNC
Q40 vcc c_e41 c_n53 QNC
J39 c_n53 0 0 JY
{pot("VR3", "c_cutn", "c_w3", "vcc", 50e3, taper_a(cut))}R47 c_cutn c_n561 10k
R61 c_v5p1 c_n561 10k
{pot("VR5", "c_v5p1", "c_w5", "c_n53", 50e3, taper_a(env))}RTM3 c_w3 c_ntm {TM3:.1f}
R73 c_ntm c_nb11 100k
Q10 c_n561 c_n134 c_nb11 QP
Q11 c_nc11 c_nb11 c_n561 QNC
Q9 0 c_nb9 c_n561 QP
R65 vref c_nb9 10k
R64 c_nb9 0 10k
R71 c_n134 c_n561 2.2k
R63 c_w5 c_n134 220k
SACC c_n53 c_v7p3 acc 0 S4066
{pot("VR7", "0", "c_w7", "c_v7p3", 50e3, accent)}D24 c_w7 c_n24 DS
R46 c_n24 c_v4p1 47k
{pot("VR4B", "c_v4p1", "c_w4", "c_c13", 50e3, min(res, 1.0))}C13 c_c13 0 1u
R72 c_w4 c_n134 100k
VTL c_nc11 c_nc11b 0
R69 c_nc11b f_tail 2.2k
* ===== VCA (P3)
D36 trig a_n36 DS
R134 a_n36 a_b32 22k
C41 a_b32 0 0.1u
Q32 a_c32 a_b32 a_e32 QN
R130 a_e32 0 100
R132 a_c32 a_n42 100
C42 a_n42 0 1u
R123 vcc a_n42 1.5Meg
D34 gate a_n34 DS
R137 a_n34 a_n137 1k
D35 a_n137 a_n35 DS
R133 a_n35 a_n133 2.2k
R131 a_n133 a_e31 220k
Q31 a_abc a_n42 a_e31 QPV
VABC a_abc a_abc2 0
QABC a_abc2 a_abc2 0 QN
R119 c_w7 a_nc36 47k
C36 a_nc36 0 0.033u
R120 a_nc36 a_n120 22k
D27 a_n120 a_abc DS
C21 tp6 a_n121 0.01u
R121 a_n121 a_p3 220k
C22 f_wip a_n122 0.01u
R122 a_n122 a_p3 100k
R124 a_p3 a_nb 2.2k
R125 a_p2 a_nb 2.2k
R126 vref a_nb 2.2k
C37 a_nb 0 10u
BOTA 0 a_p6 I = i(VABC) * tanh((V(a_p3) - V(a_p2)) / 0.0517)
R127 a_p6 a_n127 47k
R129 a_n127 a_n40 22k
R128 a_p6 a_n40 220k
R162 vref a_n40 2.2k
C40 a_n40 0 10u
EB8 a_p8 0 a_p6 0 1
C38 a_p8 out 1u
RVOL out 0 50k
.options method=gear reltol=2e-4 abstol=1e-11 vntol=2e-6 itl4=40
* operating point with the VCO capacitor held at its reset level (a
* relaxation oscillator has no DC state of its own); everything else settles
.ic v(v_c33)=11.3
.tran {1 / FS_OUT:.6e} {t_stop:.4f} 0 4u
.control
run
linearize v(cvout) v(v_src) v(c_n53) v(c_c13) i(VTL) i(VABC) v(tp6) v(out) v(gate) v(acc) v(slide) v(trig)
wrdata {{OUTFILE}} v(cvout) v(v_src) v(c_n53) v(c_c13) i(VTL) i(VABC) v(tp6) v(out) v(gate) v(acc) v(slide) v(trig)
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
    cir, dat = os.path.join(RAW, tag + ".cir"), os.path.join(RAW, tag + ".txt")
    with open(cir, "w") as fh:
        fh.write(text)
    r = subprocess.run(["ngspice", "-b", tag + ".cir"], cwd=RAW, capture_output=True, text=True)
    os.remove(cir)
    if r.returncode != 0 or not os.path.exists(dat):
        sys.exit(f"ngspice failed for {tag}:\n{r.stdout[-3000:]}\n{r.stderr[-1500:]}")
    raw = np.loadtxt(dat)
    os.remove(dat)
    names = ["cv", "saw", "meg", "c13", "itail", "iabc", "tp6", "out", "gate", "acc", "slide", "trig"]
    t = raw[:, 0]
    return t, {n: raw[:, 1 + 2 * j] for j, n in enumerate(names)}


def f_res_law():
    r = json.load(open(os.path.join(HERE, "vcf_results.json")))["model"]["ac"]
    pts = sorted((m["itail"], m["boost_hz"]) for m in r if m["k_res"] == 1.0 and m["boost_hz"] > 50)
    li, lf = np.log([p[0] for p in pts]), np.log([p[1] for p in pts])

    def f(i):
        x = np.log(np.maximum(i, 1e-15))
        s0 = (lf[1] - lf[0]) / (li[1] - li[0])
        s1 = (lf[-1] - lf[-2]) / (li[-1] - li[-2])
        y = np.interp(x, li, lf)
        y = np.where(x < li[0], lf[0] + s0 * (x - li[0]), y)
        y = np.where(x > li[-1], lf[-1] + s1 * (x - li[-1]), y)
        return np.exp(y)
    return f


def pitch_hz(t, saw, a, b):
    """Mean frequency from the saw's reset edges inside [a, b]."""
    m = (t >= a) & (t <= b)
    tt, vv = t[m], saw[m]
    d = np.diff(vv)
    edges = tt[1:][d > 2.0]
    if len(edges) < 2:
        return float("nan")
    keep = [edges[0]]
    for e in edges[1:]:
        if e - keep[-1] > 1e-3:
            keep.append(e)
    if len(keep) < 2:
        return float("nan")
    return float((len(keep) - 1) / (keep[-1] - keep[0]))


def analyse(t, d, steps, gate_intervals):
    f_law = f_res_law()
    itail = np.abs(d["itail"])
    fres = f_law(itail)
    iabc = np.abs(d["iabc"])
    out = d["out"]
    ref_rms = None
    rows = []
    for st in steps:
        k = st["step"] - 1
        a, b = T0 + k * STEP, T0 + (k + 1) * STEP
        m = (t >= a) & (t < b)
        row = dict(st)
        row["out_peak_V"] = float(np.max(np.abs(out[m])))
        rms = float(np.sqrt(np.mean(out[m] ** 2)))
        row["out_rms_V"] = rms
        row["cutoff_rest_before_hz"] = float(fres[(t >= a - 2e-3) & (t < a)].mean())
        row["cutoff_peak_hz"] = float(fres[m].max())
        row["cutoff_peak_ms"] = float(1e3 * (t[m][np.argmax(fres[m])] - a))
        row["cutoff_end_hz"] = float(fres[(t >= b - 2e-3) & (t < b)].mean())
        row["ladder_peak_uA"] = float(1e6 * itail[m].max())
        row["c13_end_V"] = float(d["c13"][(t >= b - 1e-3) & (t < b)].mean())
        row["meg_peak_V"] = float(d["meg"][m].max())
        row["vca_peak_uA"] = float(1e6 * iabc[m].max())
        row["vca_peak_ms"] = float(1e3 * (t[m][np.argmax(iabc[m])] - a))
        if not st["rest"]:
            row["pitch_end_hz"] = pitch_hz(t, d["saw"], b - 0.05, b)
            row["pitch_err_cents"] = float(1200 * np.log2(row["pitch_end_hz"] / st["target_hz"]))
            if st["legato"]:
                # glide: CV at 1 V/oct; time to reach 50 % / 90 % / within 10 cents
                cv = d["cv"][m]
                tt = t[m] - a
                c0 = d["cv"][(t >= a - 1e-3) & (t < a)].mean()
                c1 = 2.0 + st["semi"] / 12.0
                frac = (cv - c0) / (c1 - c0)
                row["glide_50_ms"] = float(1e3 * tt[np.argmax(frac >= 0.5)])
                row["glide_90_ms"] = float(1e3 * tt[np.argmax(frac >= 0.9)])
                row["glide_10cents_ms"] = float(1e3 * tt[np.argmax(np.abs(cv - c1) * 1200 < 10)])
            if ref_rms is None:
                ref_rms = rms
        rows.append(row)
    for r in rows:
        r["out_rms_db_re_step1"] = float(20 * np.log10(max(r["out_rms_V"], 1e-9) / ref_rms))
    return rows, fres, iabc


def write_wav(path, t, out):
    m = t >= T0 - 0.05
    x = out[m] - np.mean(out[m])
    x = x[: len(x) // 2 * 2].reshape(-1, 2).mean(axis=1)      # 88.2 kHz -> 44.1 kHz
    x = x / (np.max(np.abs(x)) + 1e-12) * 0.8
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(44100)
        w.writeframes((x * 32767).astype("<i2").tobytes())


def plot(tag, knobs, t, d, rows, fres, iabc, gate_intervals):
    import lxr_plotlib as L
    plt = L.setup_matplotlib()
    fig, ax = plt.subplots(6, 1, figsize=(13, 16), sharex=True)
    m = t >= T0 - 0.02
    tm = 1e3 * (t[m] - T0)
    c = L.PALETTE[1:]
    for k in range(len(PATTERN) + 1):
        for a in ax:
            a.axvline(1e3 * k * STEP, color="#c3cad2", lw=0.7)

    a = ax[0]
    a.plot(tm, 65.406 * 2 ** (d["cv"][m] - 2.0), color=c[0], label="CV → pitch (Hz)")
    for r in rows:
        if not r["rest"]:
            a.text(1e3 * (r["step"] - 1) * STEP + 3, r["target_hz"] * 1.06,
                   f"{r['note']}{' A' if r['accent'] else ''}{' S' if r['slide'] else ''}", fontsize=8)
    a.set_ylabel("Hz"); a.set_title("Pitch CV after the slide network (1 V/oct)")

    a = ax[1]
    a.plot(tm, d["gate"][m] / 12, color=c[0], label="gate")
    a.plot(tm, d["trig"][m] / 5.333 * 0.8 + 1.2, color=c[1], label="trigger")
    a.plot(tm, d["acc"][m] / 5 * 0.8 + 2.4, color=c[2], label="accent")
    a.plot(tm, d["slide"][m] / 5 * 0.8 + 3.6, color=c[3], label="slide switch")
    a.set_yticks([]); a.legend(loc="upper right", fontsize=7, ncol=4)
    a.set_title("Sequencer lanes (gate 3.5/6; slide held into the next step)")

    a = ax[2]
    a.plot(tm, d["meg"][m], color=c[0], label="MEG (N53)")
    a.plot(tm, d["c13"][m], color=c[3], label="accent sweep C13")
    a.set_ylabel("V"); a.legend(loc="upper right", fontsize=7)
    a.set_title("Filter envelope and accent sweep")

    a = ax[3]
    a.semilogy(tm, fres[m], color=c[1])
    a.set_ylabel("Hz"); a.set_title("Cutoff (resonance frequency from the ladder current, P2 law)")

    a = ax[4]
    a.plot(tm, 1e6 * iabc[m], color=c[2])
    a.set_ylabel("µA"); a.set_title("VCA control current (IC15 pin 1)")

    a = ax[5]
    a.plot(tm, d["out"][m], color=c[0], lw=0.5)
    a.set_ylabel("V"); a.set_xlabel("ms from step 1")
    a.set_title("Output (after C38, Volume full)")

    fig.suptitle(f"TB-303 end-to-end run '{tag}' — ngspice (S092 P4): " +
                 ", ".join(f"{k} {v}" for k, v in knobs.items()), fontsize=12, fontweight="bold")
    fig.tight_layout(rect=(0, 0, 1, 0.97))
    out = os.path.join(ROOT, "tools", "plots", f"303_e2e_run_{tag}.png")
    fig.savefig(out, dpi=95)
    return out


def main():
    runs = {
        "acid": dict(wave="saw", cut=0.35, res=0.85, env=0.6, dec=0.45, acc=0.8),
        "lowres": dict(wave="saw", cut=0.35, res=0.2, env=0.6, dec=0.45, acc=0.8),
        # S093 P6: the x1.5 Res top (the acid knobs at k_eff 1.5) for C1
        "acid_k15": dict(wave="saw", cut=0.35, res=1.5, env=0.6, dec=0.45, acc=0.8),
    }
    sel = sys.argv[1:] or list(runs)
    ev, steps, gates = build_events()
    t_stop = T0 + len(PATTERN) * STEP + 0.25
    results = {}
    path = os.path.join(HERE, "e2e_results.json")
    if os.path.exists(path):
        results = json.load(open(path))
    for tag in sel:
        knobs = runs[tag]
        t, d = run(f"e2e_{tag}", netlist(ev, knobs, t_stop, tag))
        rows, fres, iabc = analyse(t, d, steps, gates)
        # traces for the S092 P6 DSP comparison (temp folder, not the repo)
        m = t >= T0 - 0.05
        np.savez_compressed(os.path.join(RAW, f"e2e_{tag}_traces.npz"), t=t[m] - T0,
                            **{k: v[m].astype(np.float32) for k, v in d.items()})
        png = plot(tag, knobs, t, d, rows, fres, iabc, gates)
        wav = os.path.join(HERE, f"e2e_{tag}.wav")
        write_wav(wav, t, d["out"])
        results[tag] = dict(knobs=knobs, bpm=BPM, gate_fraction=3.5 / 6, trigger_ms=TRIG * 1e3,
                            gate_intervals_s=[[a - T0, b - T0] for a, b in gates], steps=rows)
        print(f"[{tag}] wrote {png} and {wav}")
        for r in rows:
            print("  ", {k: (round(v, 3) if isinstance(v, float) else v) for k, v in r.items()})
        with open(path, "w") as fh:
            json.dump(results, fh, indent=1)


if __name__ == "__main__":
    main()
