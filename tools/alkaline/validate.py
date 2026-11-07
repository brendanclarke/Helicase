#!/usr/bin/env python3
"""Validate the Alkaline DSP equations against the S092 SPICE models (S092 P6).

What:       V1 small-signal response of the DSP filter equations against the
            P2 ngspice AC results; V2 the [SN] p7 calibration ring with the
            three solvers; V3 the control path against P3/P4 (expo law,
            MEG decays, accent sweep run, VEG); V4 the audio path driven by
            the SPICE end-to-end traces (saw, ladder current, VCA current)
            against SPICE's TP6 and output; V5 the full DSP model on the
            P4 pattern against the SPICE per-step table (real-time forms);
            V6 the solver choice (P0 D2): every solver against the golden
            RK4 across filter drive, with renders (WAV) for listening.
Why:        record: S092_P6_DSP_EQUATIONS.md.
Inputs:     alk_constants.json; tools/spice/tb303/{vcf,control,vca}_results.json,
            e2e_results.json; the e2e traces in <tempdir>/lxr_spice_tb303.
Outputs:    tools/alkaline/validate_results.json, tools/plots/alk_dsp_*.png,
            tools/alkaline/alk_<run>_<solver>.wav
Accessors:  python3 tools/alkaline/validate.py [v1 v2 v3 v4 v5 v6]
"""
import json
import math
import os
import sys
import tempfile
import time
import wave

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
SP = os.path.join(ROOT, "tools", "spice", "tb303")
sys.path.insert(0, SP)
sys.path.insert(0, os.path.join(ROOT, "tools", "plots"))
import alk_model as M   # noqa: E402
import vcf as VCF       # noqa: E402

RAW = os.path.join(tempfile.gettempdir(), "lxr_spice_tb303")
FS = M.FS
RES = {}


def jload(name):
    return json.load(open(os.path.join(SP, name)))


# the solvers (S092 P6 §2-§4): golden RK4 x8; options A/B with the network per
# sample (8 states); A_rt/B_rt the real-time forms (SplitNet: fast modes per
# sample, slow network per block)
SOLVERS = {
    "ref": lambda: M.LadderRef(),
    "A": lambda: M.LadderTPT(stage_nl=False),
    "B": lambda: M.LadderTPT(stage_nl=True),
    "A_rt": lambda: M.LadderTPT(stage_nl=False, block_net=True),
    "B_rt": lambda: M.LadderTPT(stage_nl=True, block_net=True),
}


# ------------------------------------------------------------------ V1
def lin_response(f, i_lad, k, order=None, with_vca=False):
    """Small-signal S1 -> TP6 of the DSP equations (tanh -> 1), with the full
    feedback network (alk_model.FeedbackNet) and the output-coupling load."""
    w = np.array([i_lad / (4 * M.VT * c) for c in M.C_LADDER])
    net = M.make_net(k, order)
    ns = net.A.shape[0]
    out = np.empty(len(f), complex)
    top = np.empty(len(f), complex)
    vca = np.empty(len(f), complex)
    for n, fr in enumerate(f):
        sv = 2j * np.pi * fr
        J = np.zeros((4, 4), complex)
        J[0, 0], J[0, 1] = -w[0], w[0]
        for q in (1, 2, 3):
            J[q, q - 1] = w[q]
            J[q, q] = -2 * w[q]
            if q < 3:
                J[q, q + 1] = w[q]
        hpo = sv * M.TAU_OUT / (1 + sv * M.TAU_OUT)
        J[3, 3] -= M.W_LOAD * hpo                       # term L
        hx = np.linalg.solve(sv * np.eye(4) - J, np.array([-w[0], 0, 0, 0]))
        gt = -M.A_OUT / (2 * M.VT) * hpo * 2 * M.VT * hx[3]   # tp_pre per unit xin
        H = net.C @ np.linalg.solve(sv * np.eye(ns) - net.A, net.B) + net.D   # rows u, vca, tp6
        xin = H[0, 1] / (2 * M.VT - H[0, 0] * gt)       # per unit S1
        out[n] = H[2, 0] * gt * xin + H[2, 1]
        top[n] = 2 * M.VT * hx[3] * xin
        vca[n] = H[1, 0] * gt * xin + H[1, 1]
    return (out, top, vca) if with_vca else (out, top)


def v1():
    """Curve-by-curve against fresh SPICE AC runs (vcf.py netlist, R110 22k)."""
    rows = []
    for k in (0.0, 0.5, 0.8, 1.0):
        for i in (1e-6, 3e-6, 10e-6, 30e-6, 100e-6, 300e-6):
            raw = VCF.run("v1_ac", VCF.ac_netlist(k, i))
            f, sp_db = raw[:, 0], raw[:, 1]
            h, _ = lin_response(f, i, k)
            d_db = 20 * np.log10(np.abs(h))
            diff = d_db - sp_db
            hi = f > 150
            jsh = int(np.argmax(np.where(hi, sp_db, -999)))
            jdh = int(np.argmax(np.where(hi, d_db, -999)))
            lo = f < 30
            jsl = int(np.argmax(np.where(lo, sp_db, -999)))
            jdl = int(np.argmax(np.where(lo, d_db, -999)))
            off = sp_db > sp_db.max() - 60       # the band that matters
            if k > 0:
                for jj in (jsh, jsl):
                    off &= np.abs(np.log(f / f[jj])) > np.log(1.25)
            rows.append(dict(k=k, i_uA=i * 1e6,
                             rms_diff_db=float(np.sqrt(np.mean(diff[sp_db > sp_db.max() - 60] ** 2))),
                             rms_diff_off_peak_db=float(np.sqrt(np.mean(diff[off] ** 2))),
                             max_diff_off_peak_db=float(np.max(np.abs(diff[off]))),
                             main_peak=dict(spice_hz=float(f[jsh]), dsp_hz=float(f[jdh]),
                                            spice_db=float(sp_db[jsh]), dsp_db=float(d_db[jdh])),
                             low_peak=dict(spice_hz=float(f[jsl]), dsp_hz=float(f[jdl]),
                                           spice_db=float(sp_db[jsl]), dsp_db=float(d_db[jdl]))))
            r = rows[-1]
            print(f"V1 k {k:.1f} I {i * 1e6:5.0f}uA: off-peak rms {r['rms_diff_off_peak_db']:.2f} dB "
                  f"(max {r['max_diff_off_peak_db']:.2f}); main {r['main_peak']['dsp_hz']:.0f}/"
                  f"{r['main_peak']['spice_hz']:.0f} Hz {r['main_peak']['dsp_db']:+.1f}/{r['main_peak']['spice_db']:+.1f} dB; "
                  f"low {r['low_peak']['dsp_hz']:.1f}/{r['low_peak']['spice_hz']:.1f} Hz "
                  f"{r['low_peak']['dsp_db']:+.1f}/{r['low_peak']['spice_db']:+.1f} dB")
    RES["v1"] = rows


# ------------------------------------------------------------------ V2
def bl_saw(f0, n, hi=11.16, lo=5.81):
    """Band-limited falling saw (P1 levels) of n samples at FS."""
    t = np.arange(n) / FS
    H = int((FS / 2) / f0)
    x = np.zeros(n)
    for h in range(1, H + 1):
        x += np.sin(2 * np.pi * h * f0 * t) / h
    return (lo + hi) / 2 + (hi - lo) / 2 * (2 / np.pi) * x


def v2():
    """[SN] p7 calibration ring (Res full, saw at C2, 7.375 uA), measured on
    Q19's emitter (tp_pre; TP6 differs from it only by C14's sub-Hz voltage)."""
    f0 = 65.406
    n = int(0.35 * FS)
    s = bl_saw(f0, n)
    i = np.full(n, 7.375e-6)
    out = {}
    for name, mk in SOLVERS.items():
        lad = mk()
        t0 = time.time()
        M.settle(lad, float(np.mean(s)), 1.0)
        lad.process(s, i, 1.0)
        y = lad.tp_pre
        t = np.arange(n) / FS
        # resets of the input saw after 0.2 s
        e = [x for x in t[1:][np.diff(s) > 1.0] if x > 0.2]
        r = VCF.ring_metrics(t, y, e[0] + 0.05e-3, e[0] + 1 / f0 - 0.05e-3)
        out[name] = dict(ring=r, max_stage=lad.max_stage, max_out_arg=lad.max_out,
                         cycle_vpp=float(np.ptp(y[(t > e[0]) & (t < e[0] + 1 / f0)])),
                         secs=time.time() - t0)
        print(f"V2 {name}: {r}  p-p {out[name]['cycle_vpp']:.3f} V, stage max {lad.max_stage:.2f}, "
              f"Q21 arg max {lad.max_out:.2f}")
    out["spice"] = jload("vcf_results.json")["model"]["ring_by_q21_bf"]["300"]
    RES["v2"] = out


# ------------------------------------------------------------------ V3
def v3():
    out = {}
    # (a) expo law vs P4 dc_map (MEG at rest, no accent)
    p4 = jload("control_results.json")
    meg = M.Meg()
    n53 = meg.n53()
    v13 = M.constants()["accent"]["v13_rest"]
    errs = []
    for row in p4["dc_map"]:
        for d in row:
            i, _ = M.expo_current(d["cut"], d["env"], 0.0, n53, v13)
            errs.append(1200 * math.log2(i / (d["i_uA"] * 1e-6)))
    out["expo_err_cents"] = dict(max=float(np.max(np.abs(errs))), rms=float(np.sqrt(np.mean(np.square(errs)))))
    print("V3a expo law vs P4: max", out["expo_err_cents"])
    # (b) MEG decay to 10 %
    meg_t = {}
    for dec, acc in ((0.0, False), (0.5, False), (1.0, False), (1.0, True)):
        mg = M.Meg()
        rest = mg.n53()
        mg.trigger()
        dt = M.BLOCK / FS
        t, peak, t10 = 0.0, None, None
        acc_left = 0.125 if acc else 0.0
        while t < 4.0:
            v = mg.step(dt, dec, acc_left > 0)
            acc_left -= dt
            t += dt
            if peak is None or v > peak:
                peak, tp = v, t
            elif t10 is None and v < rest + 0.1 * (peak - rest):
                t10 = t - tp
        meg_t[f"{dec}{'A' if acc else ''}"] = t10
    sp = {f"{m['decay_knob']}{'A' if m['accent'] else ''}": m["t_to_10pct_s"] for m in p4["meg"]}
    out["meg_t10"] = {k: dict(dsp=meg_t[k], spice=sp[k]) for k in meg_t}
    print("V3b MEG t10:", out["meg_t10"])
    # (c) accent run vs P4 (4 accented 16ths + 1 normal, Accent 1, Env Mod 0.5, Cutoff 0.5, Decay 0.5)
    out["accent_run"] = {}
    for rk in (0.0, 0.5, 1.0):
        peaks = control_run(rk)
        spk = [r for r in p4["accent_run"] if r["res"] == rk][0]["peak_i_uA"]
        out["accent_run"][str(rk)] = dict(dsp_uA=peaks, spice_uA=spk)
        print(f"V3c accent run res {rk}: dsp {[round(x, 1) for x in peaks]} spice {[round(x, 1) for x in spk]}")
    # (d) VEG: decay from the peak, attack
    p3 = jload("vca_results.json")
    vg = M.Veg()
    dt = M.BLOCK / FS
    vg.trigger()
    t, cur = [], []
    for n in range(int(5.0 / dt)):
        cur.append(vg.step(dt, True))
        t.append((n + 1) * dt)
    cur = np.array(cur)
    t = np.array(t)
    pk = cur.max()
    tpk = t[np.argmax(cur)]
    def t_at(db):
        j = np.where((t > tpk) & (cur < pk * 10 ** (db / 20)))[0]
        return float(t[j[0]] - tpk) if len(j) else float("nan")
    i10 = np.where(cur > 0.1 * pk)[0][0]
    i90 = np.where(cur > 0.9 * pk)[0][0]
    out["veg"] = dict(peak_uA=pk * 1e6, t_m6=t_at(-6), t_m20=t_at(-20), t_m40=t_at(-40),
                      attack_10_90_ms=1e3 * (t[i90] - t[i10]),
                      spice=dict(peak_uA=p3["veg"]["long_vg12.0"]["peak_uA"],
                                 t_m6=p3["veg"]["long_vg12.0"]["t_m6db_s"],
                                 t_m20=p3["veg"]["long_vg12.0"]["t_m20db_s"],
                                 t_m40=p3["veg"]["long_vg12.0"]["t_m40db_s"],
                                 attack_10_90_ms="2.11-2.61 (trigger 0.65-0.70 ms)"))
    print("V3d VEG:", out["veg"])
    RES["v3"] = out


def control_run(res, n_steps=5, acc_steps=(0, 1, 2, 3)):
    """P4 §3.5 pattern through the DSP control path; peak ladder current per step."""
    dt = M.BLOCK / FS
    mg, sw = M.Meg(), M.AccentNet(vca_branch=False)
    steps = int(0.125 / dt + 0.5)
    v134 = M.V561
    peaks = []
    for k in range(n_steps):
        acc = k in acc_steps
        mg.trigger()
        pk = 0.0
        for b in range(steps):
            n53 = mg.step(dt, 0.5, acc)
            vw, _ = sw.step(dt, acc, 1.0, res, n53, v134)
            i, dv = M.expo_current(0.5, 0.5, res, n53, vw)
            v134 = M.V561 + dv
            pk = max(pk, i)
        peaks.append(pk * 1e6)
    return peaks


# ------------------------------------------------------------------ V4
def load_traces(tag):
    p = os.path.join(RAW, f"e2e_{tag}_traces.npz")
    if not os.path.exists(p):
        raise SystemExit(f"missing {p}: run tools/spice/tb303/e2e.py first")
    d = np.load(p)
    t = d["t"].astype(float)
    tn = np.arange(t[0], t[-1], 1 / FS)
    return tn, {k: np.interp(tn, t, d[k].astype(float)) for k in d.files if k != "t"}


def level_db(a, b):
    return 20 * np.log10(np.sqrt(np.mean(a ** 2)) / np.sqrt(np.mean(b ** 2)))


def spec_dist(a, b, nfft=4096):
    """Log-spectral distance (dB RMS) over 30 Hz-8 kHz, Welch-style."""
    def psd(x):
        hop = nfft // 2
        win = np.hanning(nfft)
        acc = None
        for s in range(0, len(x) - nfft, hop):
            X = np.abs(np.fft.rfft(win * x[s:s + nfft])) ** 2
            acc = X if acc is None else acc + X
        return acc
    fa = np.fft.rfftfreq(nfft, 1 / FS)
    m = (fa > 30) & (fa < 8000)
    pa, pb = psd(a), psd(b)
    return float(np.sqrt(np.mean((10 * np.log10(pa[m] + 1e-20) - 10 * np.log10(pb[m] + 1e-20)) ** 2)))


def v4():
    out = {}
    knobs = jload("e2e_results.json")
    for tag in ("acid", "lowres"):
        tn, d = load_traces(tag)
        k = knobs[tag]["knobs"]["res"]
        s, itail, iabc = d["saw"], np.abs(d["itail"]), np.abs(d["iabc"])
        m = tn >= 0.0
        out[tag] = {}
        for name, mk in SOLVERS.items():
            lad = mk()
            # start from the DC operating point (as SPICE), then run the traces
            M.settle(lad, float(np.mean(s[tn < 0])), k)
            tp6 = lad.process(s, itail, k)
            y = M.vca_out(lad.vca_in, iabc)
            sp_tp6, sp_out = d["tp6"] - np.mean(d["tp6"][m]), d["out"]
            r = dict(out_level_db=level_db(y[m], sp_out[m]), out_lsd_db=spec_dist(y[m], sp_out[m]),
                     max_stage=lad.max_stage, max_q21_arg=lad.max_out)
            if not np.isnan(tp6[0]):           # the real-time forms do not carry TP6
                r.update(tp6_level_db=level_db(tp6[m], sp_tp6[m]), tp6_lsd_db=spec_dist(tp6[m], sp_tp6[m]))
            # per-step output level against SPICE
            steps = []
            for st in range(8):
                a, b = st * 0.125, (st + 1) * 0.125
                mm = (tn >= a) & (tn < b)
                if np.sqrt(np.mean(sp_out[mm] ** 2)) > 1e-3:
                    steps.append(round(level_db(y[mm], sp_out[mm]), 2))
                else:
                    steps.append(None)
            r["out_step_level_err_db"] = steps
            out[tag][name] = r
            print(f"V4 {tag} {name}: {r}")
            if name in ("ref", "B_rt"):
                write_wav(os.path.join(HERE, f"alk_v4_{tag}_{name}.wav"), y[m])
        write_wav(os.path.join(HERE, f"alk_v4_{tag}_spice.wav"), d["out"][m])
    RES["v4"] = out


def write_wav(path, x):
    x = x - np.mean(x)
    x = x / (np.max(np.abs(x)) + 1e-12) * 0.8
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(int(FS))
        w.writeframes((x * 32767).astype("<i2").tobytes())


# ------------------------------------------------------------------ V5
class Osc:
    """Table oscillator: per-level saw and square cycles from P1 (fit_spice),
    band-limited to the level's harmonic count, linear interpolation."""

    def __init__(self, wave_):
        lv = M.constants()["square_levels"]
        self.edges = [16.3516 * 2 ** k for k in range(1, 11)]
        self.tabs = []
        for k in range(11):
            src = lv[min(k, len(lv) - 1)]["sq" if wave_ == "square" else "saw"]
            c = np.fft.rfft(np.array(src))
            top = 16.3516 * 2 ** (k + 1)
            H = max(1, int((FS / 2) / top))
            c[H + 1:] = 0
            self.tabs.append(np.fft.irfft(c, 1024))
        self.ph = 0.0

    def block(self, f, n):
        lvl = sum(1 for e in self.edges if f >= e)
        tab = self.tabs[lvl]
        inc = f / FS * 1024
        idx = self.ph + inc * np.arange(n)
        self.ph = (self.ph + inc * n) % 1024
        i0 = np.floor(idx).astype(int) % 1024
        fr = idx - np.floor(idx)
        return tab[i0] * (1 - fr) + tab[(i0 + 1) % 1024] * fr


def dsp_pattern(knobs, solver):
    """The P4 pattern through the whole DSP model, scheduled the way the
    firmware will run it: a step event lands in the first block that starts
    at or after the step boundary (latency under one block); the knobs, MEG, accent sweep, VEG and VCA accent run once
    per block; the ladder and VCA currents ramp per sample across the block
    (the gate-off ramp is the one-block 0.73 ms release, P0 D9)."""
    import e2e as E
    dt = M.BLOCK / FS
    T0 = 0.05                                  # pre-roll before step 1
    n_blocks = int((T0 + len(E.PATTERN) * E.STEP + 0.25) / dt)
    osc = Osc(knobs["wave"])
    lad = SOLVERS[solver]()
    mg, an, vg = M.Meg(), M.AccentNet(), M.Veg()
    k = knobs["res"]
    _, _, gates = E.build_events()
    gates = [(g0 - E.T0, g1 - E.T0) for g0, g1 in gates]
    cv = target = 2.0
    gliding = acc_on = prev_slide = False
    next_step = 0
    i_prev, a_prev = None, 0.0
    v134 = M.V561
    s_all, i_all, a_all, t_all, cvs, ctl = [], [], [], [], [], []
    for b in range(n_blocks):
        t = b * dt - T0                        # pattern time (step 1 at 0)
        while next_step < len(E.PATTERN) and next_step * E.STEP <= t + 1e-9:
            note = E.PATTERN[next_step]
            if note is None:
                acc_on = gliding = prev_slide = False
            else:
                semi, acc_on, slide = note
                target = 2.0 + semi / 12.0
                gliding = prev_slide           # slide is taken from the flagged note
                if not gliding:
                    cv = target
                    mg.trigger()
                    vg.trigger()
                prev_slide = slide
            next_step += 1
        if next_step >= len(E.PATTERN) and t >= len(E.PATTERN) * E.STEP:
            acc_on = False
        if gliding:
            cv = target + (cv - target) * math.exp(-dt / M.TAU_SLIDE)
        gate_on = any(g0 <= t < g1 for g0, g1 in gates)
        n53 = mg.step(dt, knobs["dec"], acc_on)
        vw, i_acc = an.step(dt, acc_on, knobs["acc"], k, n53, v134)
        i_lad, dv = M.expo_current(knobs["cut"], knobs["env"], k, n53, vw)
        v134 = M.V561 + dv
        # the C36 branch feeds pin 1 through D27 whether or not the gate is open
        i_vca = vg.step(dt, gate_on) + i_acc
        s_all.append(osc.block(M.F_C2 * 2 ** (cv - 2.0), M.BLOCK))
        ip = i_lad if i_prev is None else i_prev
        ramp = (np.arange(M.BLOCK) + 1.0) / M.BLOCK
        i_all.append(ip + (i_lad - ip) * ramp)
        a_all.append(a_prev + (i_vca - a_prev) * ramp)
        i_prev, a_prev = i_lad, i_vca
        t_all.append(t + np.arange(M.BLOCK) / FS)
        cvs.append(np.full(M.BLOCK, cv))
        ctl.append((t, mg.v, n53, an.v13, vw))
    s, i_l, i_v, tt, cvv = (np.concatenate(x) for x in (s_all, i_all, a_all, t_all, cvs))
    ctl = np.array(ctl)
    M.settle(lad, float(np.mean(s[:M.BLOCK * 50])), k)
    tp6 = lad.process(s, i_l, k)
    y = M.vca_out(lad.vca_in, i_v)
    return tt, dict(saw=s, itail=i_l, iabc=i_v, tp6=tp6, out=y, cv=cvv,
                    ctl_t=ctl[:, 0], c62=ctl[:, 1], n53=ctl[:, 2], c13=ctl[:, 3], w4=ctl[:, 4])


def v5():
    import e2e as E
    out = {}
    sp = jload("e2e_results.json")
    flaw = E.f_res_law()
    for tag in ("acid", "lowres"):
        knobs = sp[tag]["knobs"]
        out[tag] = {}
        for solver in ("B_rt", "A_rt"):
            tt, d = dsp_pattern(knobs, solver)
            rows = []
            ref1 = None
            for srow in sp[tag]["steps"]:
                k = srow["step"] - 1
                a, b = k * E.STEP, (k + 1) * E.STEP
                m = (tt >= a) & (tt < b)
                r = dict(step=srow["step"])
                fr = flaw(d["itail"][m])
                r["cutoff_peak_hz"] = (float(fr.max()), srow["cutoff_peak_hz"])
                r["cutoff_end_hz"] = (float(fr[-30:].mean()), srow["cutoff_end_hz"])
                r["vca_peak_uA"] = (float(1e6 * d["iabc"][m].max()), srow["vca_peak_uA"])
                rms = float(np.sqrt(np.mean(d["out"][m] ** 2)))
                if ref1 is None:
                    ref1 = rms
                r["out_rms_db"] = (float(20 * np.log10(max(rms, 1e-12) / ref1)), srow["out_rms_db_re_step1"])
                if not srow["rest"]:
                    f_end = M.F_C2 * 2 ** (d["cv"][m][-1] - 2.0)
                    r["pitch_hz"] = (float(f_end), srow["target_hz"])
                rows.append(r)
            out[tag][solver] = rows
            write_wav(os.path.join(HERE, f"alk_v5_{tag}_{solver}.wav"), d["out"][tt >= 0])
            print(f"V5 {tag} {solver}:")
            for r in rows:
                print("   ", r)
            if solver == "B_rt":
                plot_v5(tag, tt, d)
    RES["v5"] = out


def plot_v5(tag, tt, d):
    import lxr_plotlib as L
    tn, sp = load_traces(tag)
    plt = L.setup_matplotlib()
    fig = plt.figure(figsize=(13, 11))
    gs = fig.add_gridspec(4, 2, height_ratios=(1, 1, 1, 1.1))
    ax = [fig.add_subplot(gs[0, :])]
    ax += [fig.add_subplot(gs[1, :], sharex=ax[0]), fig.add_subplot(gs[2, :], sharex=ax[0])]
    ax[0].semilogy(1e3 * tn, np.abs(sp["itail"]) * 1e6, color="#8a96a3", lw=2.2, label="SPICE")
    ax[0].semilogy(1e3 * tt, d["itail"] * 1e6, color=L.PALETTE[1], lw=1, label="DSP")
    ax[0].set_ylabel("ladder µA"); ax[0].legend(fontsize=7)
    ax[0].set_title("Ladder current (cutoff): knobs, MEG and accent sweep at control rate")
    ax[1].plot(1e3 * tn, np.abs(sp["iabc"]) * 1e6, color="#8a96a3", lw=2.2, label="SPICE")
    ax[1].plot(1e3 * tt, d["iabc"] * 1e6, color=L.PALETTE[2], lw=1, label="DSP")
    ax[1].set_ylabel("VCA µA"); ax[1].legend(fontsize=7)
    ax[1].set_title("VCA current: VEG on the gate plus the C36 accent branch")
    ax[2].plot(1e3 * tn, sp["out"], color="#8a96a3", lw=1.6, label="SPICE")
    ax[2].plot(1e3 * tt, d["out"], color="#c0392b", lw=0.6, label="DSP (B, real-time form)")
    ax[2].set_ylabel("out V"); ax[2].set_xlabel("ms"); ax[2].legend(fontsize=7)
    ax[2].set_xlim(-20, 1250)
    ax[2].set_title("Output (VCA pin 6 into the output stage load)")
    for j, (a_, w_, nm) in enumerate(((0.125, 0.04, "step 2 onset (accent)"),
                                     (0.375, 0.04, "step 4 (slide into G2)"))):
        z = fig.add_subplot(gs[3, j])
        m1 = (tn >= a_ - 0.002) & (tn < a_ + w_)
        m2 = (tt >= a_ - 0.002) & (tt < a_ + w_)
        z.plot(1e3 * tn[m1], sp["out"][m1], color="#8a96a3", lw=2.2, label="SPICE")
        z.plot(1e3 * tt[m2], d["out"][m2], color="#c0392b", lw=0.9, label="DSP (B_rt)")
        z.set_title(f"Zoom: {nm}"); z.set_xlabel("ms"); z.legend(fontsize=7)
    fig.suptitle(f"Alkaline DSP model against SPICE, pattern '{tag}' (S092 P6)", fontweight="bold")
    fig.tight_layout(rect=(0, 0, 1, 0.97))
    fig.savefig(os.path.join(ROOT, "tools", "plots", f"alk_dsp_pattern_{tag}.png"), dpi=95)


# ------------------------------------------------------------------ V6
def v6():
    """Solver choice (P0 D2): the SPICE acid run's saw and currents with the
    filter input scaled by drv (E2), every solver against the golden RK4:
    output level and spectral distance, the largest stage argument. Renders
    at drv 1 and 4 for listening."""
    out = {}
    knobs = jload("e2e_results.json")
    tn, d = load_traces("acid")
    k = knobs["acid"]["knobs"]["res"]
    sel = tn > -0.05
    tt = tn[sel]
    m = (tt >= 0) & (tt < 1.0)
    s0, itail, iabc = d["saw"][sel], np.abs(d["itail"][sel]), np.abs(d["iabc"][sel])
    s_dc = float(np.mean(d["saw"][tn < 0]))
    for drv in (1.0, 2.0, 4.0, 8.0):
        ys = {}
        for name, mk in SOLVERS.items():
            lad = mk()
            M.settle(lad, drv * s_dc, k)
            lad.process(drv * s0, itail, k)
            ys[name] = (M.vca_out(lad.vca_in, iabc), lad.max_stage)
        ref = ys["ref"][0][m]
        row = {"ref_max_stage": ys["ref"][1]}
        for name, (y, st) in ys.items():
            if name != "ref":
                row[name] = dict(level_db=level_db(y[m], ref), lsd_db=spec_dist(y[m], ref), max_stage=st)
        out[f"drv{drv:g}"] = row
        print(f"V6 drive x{drv:g}: ref stage max {ys['ref'][1]:.2f} | " + " | ".join(
            f"{n_} {r_['level_db']:+.2f} dB lsd {r_['lsd_db']:.2f} st {r_['max_stage']:.2f}"
            for n_, r_ in row.items() if n_ != "ref_max_stage"))
        if drv in (1.0, 4.0):
            for name in ("ref", "A_rt", "B_rt"):
                write_wav(os.path.join(HERE, f"alk_d2_{name}_drv{int(drv)}.wav"), ys[name][0][m])
    RES["v6"] = out


def main():
    sel = sys.argv[1:] or ["v1", "v2", "v3", "v4", "v5", "v6"]
    path = os.path.join(HERE, "validate_results.json")
    if os.path.exists(path):
        RES.update(json.load(open(path)))
    for name in sel:
        globals()[name]()
        with open(path, "w") as fh:
            json.dump(RES, fh, indent=1, default=float)


if __name__ == "__main__":
    main()
