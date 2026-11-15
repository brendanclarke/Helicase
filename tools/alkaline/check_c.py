#!/usr/bin/env python3
"""Cross-check the Alkaline C engine against SPICE and the Python model (S093 P0, P3).

What:       C0 (S093 P3): the firmware kernels (B2R, B-R, A-R) against their
            reference, the S093 P1 variant kernels (variants/dlv_kernel.c:
            B2R, B_n1rq_ota32, A_n1q), on identical inputs: the SPICE e2e
            saw x drive and currents, acid x1/x4/x8/x16 and lowres
            x1/x4/x8/x16, the
            first 0.6 s (run_variants.py's cases and window); each also
            against the golden RK4 model (LSD60 and level).
            C1 (traces): the C ladder (B2R, B-R, A-R) driven by the
            SPICE end-to-end run's own saw, ladder current and VCA current,
            against SPICE's output and the Python real-time model (B_rt)
            on the same inputs (the S092 P6 V4 comparison).
            C2 (pattern): the whole C voice (AlkalineVoice: oscillator,
            control path, ladder) playing the P4 pattern, per step against
            the SPICE table (e2e_results.json) and against the Python
            pattern driver (the S092 P6 V5 comparison).
Why:        S093 P0 §5: the C must reproduce the validated model.
Inputs:     tools/dsp_test/build/alkaline (make -C tools/dsp_test alkaline);
            the e2e traces in <tempdir>/lxr_spice_tb303 (tools/spice/tb303/e2e.py).
Outputs:    tools/alkaline/check_c_results.json;
            alk_c_{acid,lowres}_{B2R,BR,AR}.wav (C2) and
            alk_c_traces_{acid,lowres}_{B2R,BR,AR}.wav (C1).
Accessors:  python3 tools/alkaline/check_c.py
"""
import json
import math
import os
import subprocess
import sys
import tempfile

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
PARTS = [a for a in sys.argv[1:] if a in ("c0", "c1", "c2")] or ["c0", "c1", "c2"]
sys.argv = sys.argv[:1]
sys.path.insert(0, HERE)
import alk_model as M   # noqa: E402
import validate as V    # noqa: E402

BIN = os.path.join(ROOT, "tools", "dsp_test", "build", "alkaline")
TMP = tempfile.mkdtemp(prefix="alk_c_")
BLK = 32
OUT_SCALE = 32767.0 / 7.0          # DL_OUT_SCALE (S093 P1 D17)
KERNELS = (("B2R", "B2R"), ("AR", "A_n1q"), ("BR", "B_n1rq_ota32"))   # --traces order


def makeup_at_x1():
    """The voice's output make-up at drive x1 (AlkalineVoice.h ALK_MAKEUP_OCT):
    C2 divides it out, so the pattern is compared at the circuit's level."""
    import re
    h = open(os.path.join(ROOT, "Core", "DSP", "Instruments", "Alkaline", "AlkalineVoice.h")).read()
    return 2.0 ** float(re.search(r"#define ALK_MAKEUP_OCT\s+([0-9.]+)f", h).group(1))


def lsd60(a, b, nfft=4096):
    """Log-spectral distance (dB RMS), 30 Hz-8 kHz, only over bins within 60 dB
    of the reference's peak: the int16 output's quantisation floor (about
    -100 dB re full scale) is not compared against SPICE's deep stopband."""
    def psd(x):
        win, acc = np.hanning(nfft), None
        for s in range(0, len(x) - nfft, nfft // 2):
            X = np.abs(np.fft.rfft(win * x[s:s + nfft])) ** 2
            acc = X if acc is None else acc + X
        return acc
    f = np.fft.rfftfreq(nfft, 1 / M.FS)
    pa, pb = psd(a), psd(b)
    m = (f >= 30) & (f <= 8000) & (pb > pb.max() * 1e-6)
    return float(np.sqrt(np.mean((10 * np.log10(pa[m] / pb[m])) ** 2)))


def c0():
    sys.path.insert(0, os.path.join(HERE, "variants"))
    import run_variants as RV
    res = {}
    exes = {vname: RV.build_host(vname, RV.VARIANTS[vname]) for _, vname in KERNELS}
    for tag in ("acid", "lowres"):
        for drv in (1.0, 4.0, 8.0, 16.0):
            tt, k, s, il, ia = RV.case_inputs(tag)
            n = len(s)
            fin, fout = os.path.join(TMP, "c0.in"), os.path.join(TMP, "c0.out")
            np.concatenate([np.array([n], np.int32).view(np.float32),
                            [np.float32(k / M.RES_KMAX)],
                            np.stack([s, il, ia], 1).astype(np.float32).ravel()]
                           ).astype(np.float32).tofile(fin)
            subprocess.run([BIN, "--traces", fin, fout, str(drv)], check=True)
            o = np.fromfile(fout, np.float32).astype(float)
            ref = RV.reference(tag, drv)
            m = tt >= 0
            row = {}
            for idx, (cname, vname) in enumerate(KERNELS):
                yc = o[idx * n:(idx + 1) * n]
                _, yv = RV.run_host(exes[vname], tag, drv)
                row[cname] = dict(
                    vs_variant_lsd60=round(lsd60(yc[m], yv[m]), 4),
                    vs_variant_level_db=round(float(V.level_db(yc[m], yv[m])), 4),
                    vs_variant_maxabs_v=float(np.max(np.abs(yc[m] - yv[m]))),
                    vs_golden_lsd60=round(lsd60(yc[m], ref[m]), 3),
                    variant_vs_golden_lsd60=round(lsd60(yv[m], ref[m]), 3),
                    vs_golden_level_db=round(float(V.level_db(yc[m], ref[m])), 3))
            res[f"{tag}_x{drv:g}"] = row
            print(f"C0 {tag} x{drv:g}: " + json.dumps(row))
    return res


def c1():
    res = {}
    knobs = V.jload("e2e_results.json")
    for tag in ("acid", "lowres", "acid_k15"):
        tn, d = V.load_traces(tag)
        k = knobs[tag]["knobs"]["res"]
        sel = tn > -0.05
        tt = tn[sel]
        s = d["saw"][sel] - np.mean(d["saw"][tn < 0])
        il, ia = np.abs(d["itail"][sel]), np.abs(d["iabc"][sel])
        n = (len(tt) // BLK) * BLK
        blob = np.concatenate([np.array([n], np.int32).view(np.float32),
                               [np.float32(k / M.RES_KMAX)],
                               np.stack([s[:n], il[:n], ia[:n]], 1).astype(np.float32).ravel()])
        fin, fout = os.path.join(TMP, f"{tag}.in"), os.path.join(TMP, f"{tag}.out")
        blob.astype(np.float32).tofile(fin)
        subprocess.run([BIN, "--traces", fin, fout], check=True)
        o = np.fromfile(fout, np.float32).astype(float)
        ys = {c: o[i * n:(i + 1) * n] for i, (c, _) in enumerate(KERNELS)}
        # Python B_rt on the same signals, settled the same way (DC 0), with
        # the currents block-ramped as the firmware applies them: SPICE's
        # I_abc trace carries audio-rate pin-1 feedthrough that no control-
        # rate current has (S093 P0 §5.3)
        def ramp(x):
            e = x[BLK - 1::BLK]
            prev = np.concatenate([[0.0], e[:-1]])
            r = (np.arange(BLK) + 1.0) / BLK
            return (prev[:, None] + (e - prev)[:, None] * r[None, :]).ravel()
        lad = M.LadderTPT(stage_nl=True, block_net=True)
        M.settle(lad, 0.0, k)
        lad.process(s[:n], ramp(il[:n]), k)
        yP = M.vca_out(lad.vca_in, ramp(ia[:n]))
        sp = d["out"][sel][:n]
        m = tt[:n] >= 0
        r = {}
        for name, y in [("C_" + c, ys[c]) for c, _ in KERNELS] + [("py_B_rt", yP)]:
            steps = []
            for st in range(8):
                mm = m & (tt[:n] >= st * 0.125) & (tt[:n] < (st + 1) * 0.125)
                steps.append(round(float(V.level_db(y[mm], sp[mm])), 3)
                             if np.sqrt(np.mean(sp[mm] ** 2)) > 1e-3 else None)
            r[name] = dict(level_db=float(V.level_db(y[m], sp[m])),
                           lsd_db=float(V.spec_dist(y[m], sp[m])),
                           lsd60_db=lsd60(y[m], sp[m]), steps=steps)
        yB = ys["B2R"]
        r["C_B2R_vs_py"] = dict(level_db=float(V.level_db(yB[m], yP[m])),
                                lsd_db=float(V.spec_dist(yB[m], yP[m])),
                                lsd60_db=lsd60(yB[m], yP[m]))
        res[tag] = r
        print(f"C1 {tag}: " + json.dumps(r))
        for c, _ in KERNELS:
            V.write_wav(os.path.join(HERE, f"alk_c_traces_{tag}_{c}.wav"), ys[c][m])
    return res


def run_pattern(knobs, opt):
    fout = os.path.join(TMP, "pat.bin")
    subprocess.run([BIN, "--pattern", "0" if knobs["wave"] == "saw" else "1",
                    str(knobs["cut"]), str(knobs["res"] / M.RES_KMAX), str(knobs["env"]),
                    str(M.decay_knob_303(knobs["dec"])), str(knobs["acc"]), fout, str(opt)],
                   check=True)
    raw = np.fromfile(fout, np.uint8)
    rec = 8 + 2 * BLK
    nb = len(raw) // rec
    raw = raw[:nb * rec].reshape(nb, rec)
    ctl = raw[:, :8].copy().view(np.float32).reshape(nb, 2).astype(float)
    out = raw[:, 8:].copy().view(np.int16).reshape(-1).astype(float) / OUT_SCALE / makeup_at_x1()
    dt = BLK / M.FS
    tb = np.arange(nb) * dt - 0.05
    return tb, ctl[:, 0], ctl[:, 1], np.arange(nb * BLK) / M.FS - 0.05, out


def c2():
    import e2e as E
    res = {}
    sp_all = V.jload("e2e_results.json")
    flaw = E.f_res_law()
    for tag in ("acid", "lowres"):
        knobs = sp_all[tag]["knobs"]
        res[tag] = {}
        tP, dP = V.dsp_pattern(knobs, "B_rt")
        for opt, name in ((0, "C_B2R"), (1, "C_AR"), (2, "C_BR")):
            tb, il, ia, ts, out = run_pattern(knobs, opt)
            rows, ref1 = [], None
            for srow in sp_all[tag]["steps"]:
                kk = srow["step"] - 1
                a, b = kk * E.STEP, (kk + 1) * E.STEP
                mb = (tb >= a) & (tb < b)
                ms = (ts >= a) & (ts < b)
                fr = flaw(il[mb])
                rms = float(np.sqrt(np.mean(out[ms] ** 2)))
                ref1 = rms if ref1 is None else ref1
                rows.append(dict(
                    step=srow["step"],
                    cutoff_peak_cents=round(1200 * math.log2(fr.max() / srow["cutoff_peak_hz"]), 1),
                    cutoff_end_cents=round(1200 * math.log2(fr[-3:].mean() / srow["cutoff_end_hz"]), 1),
                    vca_peak_uA=(round(1e6 * ia[mb].max(), 1), round(srow["vca_peak_uA"], 1)),
                    out_rms_db=(round(20 * math.log10(max(rms, 1e-12) / ref1), 2),
                                round(srow["out_rms_db_re_step1"], 2))))
            n = min(len(out), len(dP["out"]))
            m = ts[:n] >= 0
            vs_py = dict(level_db=float(V.level_db(out[:n][m], dP["out"][:n][m])),
                         lsd_db=float(V.spec_dist(out[:n][m], dP["out"][:n][m])),
                         lsd60_db=lsd60(out[:n][m], dP["out"][:n][m]))
            res[tag][name] = dict(steps=rows, vs_python=vs_py)
            print(f"C2 {tag} {name}: vs Python B_rt {vs_py}")
            for r in rows:
                print("    ", r)
            V.write_wav(os.path.join(HERE, f"alk_c_{tag}_{name[2:]}.wav"), out[ts >= 0])
    return res


def main():
    if not os.path.exists(BIN):
        raise SystemExit("build it first: make -C tools/dsp_test alkaline")
    sys.path.insert(0, os.path.join(ROOT, "tools", "spice", "tb303"))
    path = os.path.join(HERE, "check_c_results.json")
    out = json.load(open(path)) if os.path.exists(path) else {}
    for part in PARTS:
        out[part] = dict(c0=c0, c1=c1, c2=c2)[part]()
    with open(path, "w") as fh:
        json.dump(out, fh, indent=1)


if __name__ == "__main__":
    main()
