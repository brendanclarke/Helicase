#!/usr/bin/env python3
"""TB-303 VCF (diode ladder + resonance loop): ngspice runs and analysis (S092 P2).

What:       transistor-level netlist of the 303 VCF from the Roland Service
            Notes p5: Q12 input pair, the diode-connected ladder (Q13/Q15,
            Q14/Q17, Q16/Q23) with C18/C19/C24/C26, the Q22 top pair, the
            Q21/Q20 differential output, Q19 and C14, and the resonance loop
            VR4 -> C15 -> R67 -> Q18 -> C23 -> R97/R98 -> Q12. The cutoff
            control (Q11's collector current) is an ideal tail current,
            swept. Runs .ac sweeps over tail current and resonance, the
            [SN] p7 calibration transient, and a drive (level) series.
Why:        S092_P1 §8 step 1; record: S092_P2_VCB_MODEL_VCF.md.
Inputs:     values from [SN] p5 (see the document); generic device models.
Outputs:    tools/spice/tb303/out/vcf_*.cir (netlists), vcf_results.json,
            tools/plots/303_vcf_spice.png. Raw data goes to a temp folder and
            is deleted after it is read.
Accessors:  python3 tools/spice/tb303/vcf.py   (ngspice, numpy, matplotlib)
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

VT = 0.02585                     # thermal voltage at 27 C
SAW_VPP = 5.35                   # P1: saw at the Q28 source (S1), 11.16 -> 5.81 V

MODELS = """
* Generic small-signal NPN for every transistor (2SC1583 pairs Q12/Q21,
* 2SC2291 pair Q22, ladder and buffer transistors). NOT vendor models.
.model QN NPN(IS=1e-14 BF=300 VAF=100 CJE=8p CJC=4p TF=0.4n RB=50)
"""
QN21 = ".model QN21 NPN(IS=1e-14 BF={bf} VAF=100 CJE=8p CJC=4p TF=0.4n RB=50)"


def vcf_core(k_res, itail, drive_lines, extra="", gfb=None, r110="22k", bf21=300):
    """k_res: VR4 wiper position 0..1 (1 = Res full CW = wiper at the output
    end). itail: Q11 collector current (A). gfb: None = the circuit as read
    (Q18 emitter follower); a number = Q18 replaced by an ideal gain block of
    that gain driving C23 (the superseded calibrated variant). r110: 22k
    (x0xb0x KiCad value; reproduces [SN] p7) or 2.2k (the [SN] p5 print).
    bf21: Q21's current gain (generic 300; the x0xb0x study uses 400)."""
    r_top = max(50e3 * (1.0 - k_res), 1.0)
    r_bot = max(50e3 * k_res, 1.0)
    return f"""{MODELS}
{QN21.format(bf=bf21)}
VCC vcc 0 12
VREF vref 0 5.333
* --- filter rail: R95 100 ohm, C28 47u
R95 vcc vtop 100
C28 vtop 0 47u
* --- Q22 top pair, bases through R115 10k (C30 decoupling, value unread: 10u)
R115 vtop b22 10k
C30 b22 0 10u
Q22L vtop b22 dL QN
Q22R vtop b22 dR QN
* --- diode-connected ladder (base = collector) and its capacitors
Q16 dL dL cL QN
Q23 dR dR cR QN
Q14 cL cL bL QN
Q17 cR cR bR QN
Q13 bL bL aL QN
Q15 bR bR aR QN
C26 dL dR 0.033u
C24 cL cR 0.033u
C19 bL bR 0.033u
C18 aL aR 0.018u
* --- Q12 input pair; tail = Q11 collector (ideal here)
Q12L aL inL tail QN
Q12R aR inR tail QN
ITAIL tail 0 {itail:.6e}
* --- input: S1 -> R62 220k -> C17 1u -> Q12L base; R70/R98 2.2k to the bias
*     node nb (R68 2.2k from the 5.333 V reference, C16 10u)
R62 s1 n62 220k
C17 n62 inL 1u
R70 inL nb 2.2k
R98 inR nb 2.2k
R68 vref nb 2.2k
C16 nb 0 10u
* --- output: C27/C25 0.1u across the top capacitor into the Q21 pair
* as drawn, the coupling capacitors cross: C27 takes the right top node to
* Q21's left base, C25 the left top node to its right base
C27 dR b21L 0.1u
C25 dL b21R 0.1u
R113 b21L nb21 100k
R114 b21R nb21 100k
R116 vref nb21 10k
C29 nb21 0 1u
Q21L c21L b21L t21 QN21
Q21R c21R b21R t21 QN21
R111 vtop c21L 2.2k
* R110: 22k per the x0xb0x KiCad schematic; the [SN] p5 print reads 2.2k (S092 P2 §2)
R110 vtop c21R {r110}
Q20 t21 b20 e20 QN
R112 nb21 b20 10k
R109 b20 0 10k
R108 e20 0 2.2k
Q19 vtop c21R out19 QN
R96 out19 0 10k
C14 out19 fout 1u
* --- resonance: VR4 50k (B) from the output to ground, wiper -> C15
RVRT fout wip {r_top:.3f}
RVRB wip 0 {r_bot:.3f}
C15 wip n15 1u
R67 n15 b18 2.2k
R99 b18 nb18 100k
R66 vref nb18 100k
C22 nb18 0 1u
{"Q18 vcc b18 q18e QN" if gfb is None else f"EFB q18e 0 n15 0 {gfb}"}
R94 q18e 0 10k
C23 q18e n23 1u
R97 n23 inR 10k
* --- VCA input load (IC15 path, R121 220k / C21; approximated)
RVCA fout 0 220k
{drive_lines}
{extra}
"""


def ac_netlist(k_res, itail, gfb=None, r110="22k", bf21=300):
    drive = "VIN s1 0 DC 8.5 AC 1"
    return f"""* TB-303 VCF ac, k={k_res} itail={itail} gfb={gfb} r110={r110} bf21={bf21}
{vcf_core(k_res, itail, drive, gfb=gfb, r110=r110, bf21=bf21)}
.options reltol=1e-4
.ac dec 60 5 22k
.control
run
wrdata {{OUTFILE}} vdb(fout) vp(fout) vdb(dL,dR)
quit
.endc
.end
"""


def tran_netlist(k_res, itail, f_hz, vpp, t_stop, t_step=2e-6, gfb=None, r110="22k", bf21=300):
    # falling saw 11.16 -> 11.16 - vpp, the P1 shape (reset 2 us)
    hi = 11.16
    lo = hi - vpp
    T = 1.0 / f_hz
    drive = (f"VIN s1 0 PULSE({lo:.4f} {hi:.4f} 0 2u {T - 4e-6:.6e} 0 {T:.6e})")
    return f"""* TB-303 VCF tran, k={k_res} itail={itail} f={f_hz} gfb={gfb} r110={r110} bf21={bf21}
{vcf_core(k_res, itail, drive, gfb=gfb, r110=r110, bf21=bf21)}
.options reltol=1e-4 method=gear
.tran {t_step:.3e} {t_stop:.4f} 0 {t_step:.3e}
.control
run
wrdata {{OUTFILE}} v(fout) v(s1) v(dL,dR) v(inL,inR)
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
    r = subprocess.run(["ngspice", "-b", tag + ".cir"], cwd=RAW,
                       capture_output=True, text=True)
    if r.returncode != 0 or not os.path.exists(dat):
        sys.exit(f"ngspice failed for {tag}:\n{r.stdout[-2500:]}\n{r.stderr[-1500:]}")
    raw = np.loadtxt(dat)
    os.remove(dat)
    os.remove(cir)
    return raw


# ------------------------------------------------------------------ helpers
def ac_run(k, i):
    """Returns f, the output (C14) in dB and the ladder (across C26) in dB.
    wrdata writes (f, re, im) per vector; the dB value is the re column."""
    raw = run(f"vcf_ac_k{int(k * 100)}_i{i * 1e6:.0f}u", ac_netlist(k, i))
    return raw[:, 0], raw[:, 1], raw[:, 5]


def slope_between(f, mag, f1, f2):
    if f2 > f[-1]:
        return float("nan")
    return float((np.interp(f2, f, mag) - np.interp(f1, f, mag)) / np.log2(f2 / f1))


def ladder_metrics(f, top):
    """Ladder alone (differential voltage across the top capacitor C26):
    -3 dB point against its 10 Hz level, slope just above and well above."""
    ref = np.interp(10.0, f, top)
    below = np.where(top < ref - 3.0)[0]
    fc = float(f[below[0]]) if len(below) else float("nan")
    return dict(fc_hz=fc, slope_fc_4fc=slope_between(f, top, fc, 4 * fc),
                slope_8fc_32fc=slope_between(f, top, 8 * fc, 32 * fc))


def output_metrics(f, out, out0):
    """Output (C14): its maximum, the low-side -3 dB point (the output
    high-pass), and the resonance boost against the Res-minimum curve out0
    at the same current."""
    imax = int(np.argmax(out))
    low = np.where((f < f[imax]) & (out < out[imax] - 3.0))[0]
    hp = float(f[low[-1]]) if len(low) else float("nan")
    boost = out - out0
    ib = int(np.argmax(boost))
    return dict(max_db=float(out[imax]), max_hz=float(f[imax]), hp_3db_hz=hp,
                boost_db=float(boost[ib]), boost_hz=float(f[ib]),
                peak_hz=float(f[imax]))


def ring_metrics(t, v, t0, t1):
    """Ring period and decay after the saw's reset edge in [t0, t1]."""
    m = (t >= t0) & (t <= t1)
    tt, vv = t[m], v[m] - np.mean(v[m])
    pk = [i for i in range(1, len(vv) - 1) if vv[i] > vv[i - 1] and vv[i] >= vv[i + 1]
          and vv[i] > 0.05 * np.max(np.abs(vv))]
    if len(pk) < 3:
        return dict(period_ms=float("nan"), decay_ratio=float("nan"), n_peaks=len(pk))
    tp = tt[pk]
    per = float(np.median(np.diff(tp[:6])) * 1e3)
    amps = vv[pk]
    ratio = float(amps[1] / amps[0]) if amps[0] != 0 else float("nan")
    return dict(period_ms=per, decay_ratio=ratio, n_peaks=len(pk),
                first_last=float(amps[min(len(amps) - 1, 5)] / amps[0]))


def ring_at(i, bf21=300, f0=65.406, vpp=SAW_VPP, k=1.0, tag="ring"):
    raw = run(tag, tran_netlist(k, i, f0, vpp, 0.30, bf21=bf21))
    t, vout, vin = raw[:, 0], raw[:, 1], raw[:, 3]
    e = [x for x in t[1:][np.diff(vin) > 1.0] if x > 0.2]
    T = 1.0 / f0
    m = (t > e[0]) & (t < e[0] + T)
    r = ring_metrics(t, vout, e[0] + 0.05e-3, e[0] + T - 0.05e-3)
    r["cycle_vpp"] = float(np.ptp(vout[m]))
    return r, (t, vout, vin, e[0])


def ac_pair(i, r110="22k", bf21=300):
    raw0 = run(f"vcf_ac_k0_i{i * 1e6:.0f}u_{r110}", ac_netlist(0.0, i, r110=r110, bf21=bf21))
    raw1 = run(f"vcf_ac_k100_i{i * 1e6:.0f}u_{r110}", ac_netlist(1.0, i, r110=r110, bf21=bf21))
    return raw0, raw1


def main():
    res = {"r110_2k2": {}, "model": {"r110": "22k", "ac": [], "drive": []}}
    curves = {}
    currents = [1e-6, 3e-6, 10e-6, 30e-6, 100e-6, 300e-6]

    # ---------------- A. R110 = 2.2k as printed in [SN] p5
    pts = []
    for i in (3e-6, 30e-6, 300e-6):
        raw0, raw1 = ac_pair(i, r110="2.2k")
        m = dict(itail=i, **ladder_metrics(raw0[:, 0], raw0[:, 5]),
                 **output_metrics(raw1[:, 0], raw1[:, 1], raw0[:, 1]))
        pts.append(m)
        print(f"[R110 2.2k] I={i * 1e6:5.0f}uA ladder fc {m['fc_hz']:7.1f} Hz  Res full boost "
              f"{m['boost_db']:+5.1f} dB @ {m['boost_hz']:6.0f} Hz")
    # stage gains and the ladder's own oscillation requirement (30 uA)
    for r110 in ("2.2k", "22k"):
        txt = ac_netlist(0.0, 30e-6, r110=r110).replace(
            "wrdata {OUTFILE} vdb(fout) vp(fout) vdb(dL,dR)",
            "wrdata {OUTFILE} v(dL,dR) v(inL,inR) v(b21L,b21R) v(c21R) v(fout)")
        raw = run(f"vcf_stages_{r110}", txt)
        f = raw[:, 0]
        cv = lambda j: raw[:, 1 + 3 * j] + 1j * raw[:, 2 + 3 * j]
        lad = cv(0) / cv(1)
        ph = np.unwrap(np.angle(lad)) * 180 / np.pi
        j180 = int(np.argmin(np.abs(ph - (ph[0] - 180.0))))
        j1k = int(np.argmin(np.abs(f - 1000.0)))
        q21 = abs(cv(3)[j1k] / cv(2)[j1k])
        key = "r110_2k2" if r110 == "2.2k" else "model"
        res[key].update(dict(
            ladder_dc_gain=float(abs(lad[0])), ladder_phase_turn_hz=float(f[j180]),
            ladder_gain_at_turn_db=float(20 * np.log10(abs(lad[j180]))),
            loop_gain_needed=float(1.0 / abs(lad[j180])), q21_stage_gain=float(q21),
            feedback_divider=2.2 / 12.2, loop_gain_available=float(q21 * 2.2 / 12.2)))
        print(f"[stages R110 {r110}] Q21 stage x{q21:.1f}, loop {q21 * 2.2 / 12.2:.2f}, "
              f"needed {1.0 / abs(lad[j180]):.1f}")
    res["r110_2k2"]["points"] = pts

    # ---------------- B. the model: R110 = 22k
    out0 = {}
    for k in (0.0, 0.5, 0.8, 1.0):
        for i in currents:
            raw = run(f"vcf_ac_k{int(k * 100)}_i{i * 1e6:.0f}u", ac_netlist(k, i))
            f, out, top = raw[:, 0], raw[:, 1], raw[:, 5]
            if k == 0.0:
                out0[i] = out
            m = dict(k_res=k, itail=i, **output_metrics(f, out, out0[i]))
            if k == 0.0:
                m.update(ladder_metrics(f, top))
            res["model"]["ac"].append(m)
            curves[(k, i)] = (f, out)
            print(f"[model] k={k:.1f} I={i * 1e6:6.1f}uA boost {m['boost_db']:+5.1f} dB @ "
                  f"{m['boost_hz']:7.1f} Hz  out max {m['max_db']:+6.1f} dB @ {m['max_hz']:7.1f} Hz"
                  + (f"  | ladder fc {m['fc_hz']:7.1f} Hz slope {m['slope_fc_4fc']:+5.1f} / "
                     f"{m['slope_8fc_32fc']:+5.1f} dB/oct" if k == 0.0 else ""))

    # calibration current: the Res-full resonance at 500 Hz (2.0 ms)
    pk = [(m["itail"], m["boost_hz"]) for m in res["model"]["ac"] if m["k_res"] == 1.0
          and m["boost_hz"] > 50]
    i_cal = float(np.exp(np.interp(np.log(500.0), np.log([p for _, p in pk]),
                                   np.log([i for i, _ in pk]))))
    cal = {}
    for bf in (300, 400):
        r, tr = ring_at(i_cal, bf21=bf, tag=f"vcf_cal_bf{bf}")
        cal[str(bf)] = r
        if bf == 300:
            t, vout, vin, e0 = tr
            m = (t >= e0 - 3e-3) & (t <= e0 + 1.25 / 65.406)
            curves["cal"] = (t[m] - e0, vout[m], vin[m])
    res["model"]["i_cal"] = i_cal
    res["model"]["ring_by_q21_bf"] = cal
    print("[model] I_cal", i_cal, cal)

    # stability: no input, Res full, after the coupling networks settle (1.2 s run)
    so = {}
    for i in (i_cal, 30e-6, 100e-6, 300e-6):
        raw = run("vcf_selfosc", tran_netlist(1.0, i, 65.406, 0.0, 1.2, t_step=10e-6))
        t, v = raw[:, 0], raw[:, 1]
        mm = t > 0.8
        so[f"{i:.3e}"] = float(np.ptp(v[mm]))
    res["model"]["no_input_vpp_after_0p8s"] = so
    print("[model] no-input p-p after 0.8 s:", so)

    # drive: C2 saw at x0.25 / x1 / x2 of the 303 level, Res min and full
    for k in (0.0, 1.0):
        for scale in (0.25, 1.0, 2.0):
            raw = run(f"vcf_drive_k{int(k * 100)}_x{int(scale * 100)}",
                      tran_netlist(k, i_cal * 4, 65.406, SAW_VPP * scale, 0.30, 4e-6))
            t, vout, vin = raw[:, 0], raw[:, 1], raw[:, 3]
            T = 1.0 / 65.406
            e = []
            for x in t[1:][np.diff(vin) > 0.05 * SAW_VPP * scale]:
                if x > 0.2 and (not e or x - e[-1] > 0.5 * T):
                    e.append(x)            # one entry per reset edge
            tt = np.linspace(e[0], e[1], 1024, endpoint=False)
            cyc = np.interp(tt, t, vout)
            X = np.abs(np.fft.rfft(cyc - cyc.mean()))[1:21]
            res["model"]["drive"].append(dict(
                k_res=k, scale=scale, itail=i_cal * 4, out_vpp=float(np.ptp(cyc)),
                harm_db=list(np.round(20 * np.log10(X / X[0] + 1e-12), 2))))
            curves[("drive", k, scale)] = cyc / scale
            print(f"[model] drive k={k} x{scale}: out {np.ptp(cyc):.3f} Vpp")

    with open(os.path.join(HERE, "vcf_results.json"), "w") as fh:
        json.dump(res, fh, indent=1)
    plot(res, curves, currents)


def plot(res, curves, currents):
    import lxr_plotlib as L
    plt = L.setup_matplotlib()
    fig, ax = plt.subplots(3, 2, figsize=(13, 12.5))
    colors = L.PALETTE[1:]

    for a, k in ((ax[0, 0], 0.0), (ax[0, 1], 1.0)):
        for c, i in zip(colors, currents):
            f, mag = curves[(k, i)]
            a.semilogx(f, mag, color=c, label=f"I = {i * 1e6:g} µA")
        a.set_ylim(-90, 10)
        a.set_xlim(5, 22000)
        a.set_title(f"Response S1 → C14, Res {'full' if k else 'minimum'} (dB), R110 22k")
        a.set_xlabel("Hz"); a.legend(fontsize=7, ncol=2)

    a = ax[1, 0]
    for k, c in zip((0.0, 0.5, 0.8, 1.0), colors):
        pts = [(m["itail"], m["fc_hz"] if k == 0 else m["boost_hz"]) for m in res["model"]["ac"] if m["k_res"] == k]
        a.loglog([p[0] * 1e6 for p in pts], [p[1] for p in pts], "o-", color=c,
                 label=("ladder −3 dB point" if k == 0 else f"resonance, Res {k:.1f}"))
    a.axhline(500, color="#8a96a3", ls=":", lw=1)
    a.text(1.1, 520, "[SN] p7 ring 2 ms", fontsize=7, color="#5b6672")
    a.set_title("Cutoff against Q11 current"); a.set_xlabel("µA"); a.set_ylabel("Hz")
    a.legend(fontsize=7)

    a = ax[1, 1]
    for k, c in zip((0.5, 0.8, 1.0), colors[1:]):
        pts = [(m["boost_hz"], m["boost_db"]) for m in res["model"]["ac"] if m["k_res"] == k]
        a.semilogx([p[0] for p in pts], [p[1] for p in pts], "o-", color=c, label=f"Res {k:.1f}")
    a.set_title("Resonance boost against frequency (dB over Res minimum)")
    a.set_xlabel("peak frequency (Hz)"); a.set_ylabel("dB"); a.legend(fontsize=7)

    a = ax[2, 0]
    t, vout, vin = curves["cal"]
    a.plot(1e3 * t, vout, color=L.PALETTE[1], label="filter output (C14)")
    a2 = a.twinx()
    a2.plot(1e3 * t, vin, color="#c3cad2", lw=1, label="saw at S1")
    rc = res["model"]["ring_by_q21_bf"]["300"]
    a.set_title(f"[SN] p7 calibration: C2 saw, Res full, I = {res['model']['i_cal'] * 1e6:.2f} µA; "
                f"ring {rc['period_ms']:.2f} ms, {rc['decay_ratio']:.2f}/cycle", fontsize=10)
    a.set_xlabel("ms from the saw reset"); a.set_ylabel("V"); a.legend(loc="upper right", fontsize=7)

    a = ax[2, 1]
    for k, ls in ((0.0, "-"), (1.0, "--")):
        for scale, c in zip((0.25, 1.0, 2.0), colors):
            cyc = curves[("drive", k, scale)]
            a.plot(np.arange(1024) / 1024, cyc, ls=ls, color=c, lw=1.2,
                   label=f"Res {'full' if k else 'min'}, level ×{scale:g} (÷{scale:g})")
    a.set_title("Drive: one C2 cycle, output divided by input level")
    a.set_xlabel("phase"); a.set_ylabel("V per unit level"); a.legend(fontsize=6.5, ncol=2)

    fig.suptitle("TB-303 VCF — ngspice, [SN] p5 values with R110 22k, ideal Q11 current (S092 P2)",
                 fontsize=13, fontweight="bold")
    fig.tight_layout(rect=(0, 0, 1, 0.97))
    out = os.path.join(ROOT, "tools", "plots", "303_vcf_spice.png")
    fig.savefig(out, dpi=105)
    print("wrote", out)


if __name__ == "__main__":
    main()
