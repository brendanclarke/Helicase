#!/usr/bin/env python3
"""Cross-check the Alkaline C engine against SPICE and the Python model (S093 P0).

What:       C1 (traces): the C ladder (DiodeLadder_renderB/A) driven by the
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
Outputs:    tools/alkaline/check_c_results.json; alk_c_{acid,lowres}_{B,A}.wav.
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
sys.argv = sys.argv[:1]
sys.path.insert(0, HERE)
import alk_model as M   # noqa: E402
import validate as V    # noqa: E402

BIN = os.path.join(ROOT, "tools", "dsp_test", "build", "alkaline")
TMP = tempfile.mkdtemp(prefix="alk_c_")
BLK = 32
OUT_SCALE = 32767.0 / 3.0


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


def c1():
    res = {}
    knobs = V.jload("e2e_results.json")
    for tag in ("acid", "lowres"):
        tn, d = V.load_traces(tag)
        k = knobs[tag]["knobs"]["res"]
        sel = tn > -0.05
        tt = tn[sel]
        s = d["saw"][sel] - np.mean(d["saw"][tn < 0])
        il, ia = np.abs(d["itail"][sel]), np.abs(d["iabc"][sel])
        n = (len(tt) // BLK) * BLK
        blob = np.concatenate([np.array([n], np.int32).view(np.float32), [np.float32(k)],
                               np.stack([s[:n], il[:n], ia[:n]], 1).astype(np.float32).ravel()])
        fin, fout = os.path.join(TMP, f"{tag}.in"), os.path.join(TMP, f"{tag}.out")
        blob.astype(np.float32).tofile(fin)
        subprocess.run([BIN, "--traces", fin, fout], check=True)
        o = np.fromfile(fout, np.float32).astype(float)
        yB, yA = o[:n], o[n:2 * n]
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
        for name, y in (("C_B", yB), ("C_A", yA), ("py_B_rt", yP)):
            steps = []
            for st in range(8):
                mm = m & (tt[:n] >= st * 0.125) & (tt[:n] < (st + 1) * 0.125)
                steps.append(round(float(V.level_db(y[mm], sp[mm])), 3)
                             if np.sqrt(np.mean(sp[mm] ** 2)) > 1e-3 else None)
            r[name] = dict(level_db=float(V.level_db(y[m], sp[m])),
                           lsd_db=float(V.spec_dist(y[m], sp[m])),
                           lsd60_db=lsd60(y[m], sp[m]), steps=steps)
        r["C_B_vs_py"] = dict(level_db=float(V.level_db(yB[m], yP[m])),
                              lsd_db=float(V.spec_dist(yB[m], yP[m])),
                              lsd60_db=lsd60(yB[m], yP[m]))
        res[tag] = r
        print(f"C1 {tag}: " + json.dumps(r))
        for name, y in (("B", yB), ("A", yA)):
            V.write_wav(os.path.join(HERE, f"alk_c_traces_{tag}_{name}.wav"), y[m])
    return res


def run_pattern(knobs, opt):
    fout = os.path.join(TMP, "pat.bin")
    subprocess.run([BIN, "--pattern", "0" if knobs["wave"] == "saw" else "1",
                    str(knobs["cut"]), str(knobs["res"]), str(knobs["env"]),
                    str(knobs["dec"]), str(knobs["acc"]), fout, str(opt)], check=True)
    raw = np.fromfile(fout, np.uint8)
    rec = 8 + 2 * BLK
    nb = len(raw) // rec
    raw = raw[:nb * rec].reshape(nb, rec)
    ctl = raw[:, :8].copy().view(np.float32).reshape(nb, 2).astype(float)
    out = raw[:, 8:].copy().view(np.int16).reshape(-1).astype(float) / OUT_SCALE
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
        for opt, name in ((0, "C_B"), (1, "C_A")):
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
    out = dict(c1=c1(), c2=c2())
    with open(os.path.join(HERE, "check_c_results.json"), "w") as fh:
        json.dump(out, fh, indent=1)


if __name__ == "__main__":
    main()
