#!/usr/bin/env python3
"""Listening test for the two remaining cuts of S093 P3 §5 (S093 P3).

What:       builds the Alkaline bench (tools/dsp_test modules/alkaline.c) four
            ways and renders the same material through each:
              B2R  the current firmware sources (T1-1 accent, 13-point Res
                   blend over k_eff 0..1.5 since S093 P6);
              b    T2-3, the accent solved once with lagged diode states
                   (-DALK_ACC_SOLVES=1);
              c    T2-4, the nearest of 33 Res points, one product per row
                   (-DALK_RES_NEAREST=1, a 33-point table generated into
                   build/s093p3/t24, not the source tree);
              bc   both.
            Material: the S092 P4 pattern (acid and lowres knobs, the SPICE
            run's) at drive x1 and acid at x8 and x16 (16^drive since S093
            P6), and a Res sweep (bench --sweep: a 16th bass line, Res 0 -> 1
            over 6 s, then an LFO-like 1.5 Hz wobble; S093 P6: the sweep now
            reaches the x1.5 feedback top) at x1 and x8, the T2-4 risk.
            Every variant of a set is written with the B2R file's gain (peak
            0.8 of B2R), so level changes stay audible.
Why:        the user asked what (b) and (c) do to the sound before choosing.
Decided (user, S093 P3): (b) accepted, now the default (ALK_ACC_SOLVES 1);
            (c) rejected and its ALK_RES_NEAREST switch removed from the
            sources. A rerun therefore renders only B2R as it was (the exact
            T1-1 accent, -DALK_ACC_SOLVES=2) and b (the default); the c and bc
            files in listen/ are the record of the rejected test.
Inputs:     tools/dsp_test (make -C tools/dsp_test alkaline first);
            e2e_results.json (the pattern knobs).
Outputs:    tools/alkaline/listen/alk_listen_<set>_<variant>.wav and
            tools/alkaline/listen/listen_bc_results.json: per variant against
            B2R the level, the null residual (B2R minus the variant, dB re
            B2R), LSD60; per pattern step the level difference; for the
            sweep the residual per second.
Accessors:  python3 tools/alkaline/listen_bc.py
"""
import json
import os
import subprocess
import sys
import wave

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
DT = os.path.join(ROOT, "tools", "dsp_test")
OUT = os.path.join(HERE, "listen")
T24 = os.path.join(ROOT, "build", "s093p3", "t24")
sys.argv = sys.argv[:1]
sys.path.insert(0, HERE)
import validate as V   # noqa: E402
import alk_model as M  # noqa: E402

FS = 44108
BLK = 32
VARIANTS = {"B2R": ["-DALK_ACC_SOLVES=2"], "b": ["-DALK_ACC_SOLVES=1"]}


def tables33():
    host = os.path.join(T24, "host")
    os.makedirs(host, exist_ok=True)
    subprocess.run([sys.executable, os.path.join(HERE, "gen_alk_tables.py"), "--points", "33",
                    "--out", T24], check=True, capture_output=True)
    for f in ("AlkalineTables.h", "AlkalineTables.c"):
        subprocess.run([sys.executable, os.path.join(DT, "extract.py"), "--src", os.path.join(T24, f),
                        "--body", "--out", os.path.join(host, f.replace(".", ".host."))],
                       check=True, cwd=DT, capture_output=True)
    return host


def build(name, defs, host33):
    exe = os.path.join(T24, "alk_" + name)
    inc = (["-I" + host33] if host33 else []) + ["-Ibuild/gen"]
    cmd = (["cc", "-O2", "-std=gnu11", "-w", "-fno-vectorize", "-fno-slp-vectorize",
            "-include", "prelude.h"] + inc + ["-I.", "-I../../Core/SampleRom", "-include", "cat_host.h",
            "-I../../Core/MIDI"] + defs +
           ["modules/alkaline.c", "../../Core/SampleRom/SampleMemory.c",
            "../../Core/SampleRom/SampleInstall.c", "catalogue_host.c", "-o", exe, "-lm"])
    subprocess.run(cmd, check=True, cwd=DT)
    return exe


def pattern(exe, knobs, drv):
    f = os.path.join(T24, "pat.bin")
    subprocess.run([exe, "--pattern", "0" if knobs["wave"] == "saw" else "1", str(knobs["cut"]),
                    str(knobs["res"] / M.RES_KMAX), str(knobs["env"]),
                    str(M.decay_knob_303(knobs["dec"])), str(knobs["acc"]),
                    f, "0", str(drv)], check=True)
    raw = np.fromfile(f, np.uint8)
    rec = 8 + 2 * BLK
    nb = len(raw) // rec
    out = raw[:nb * rec].reshape(nb, rec)[:, 8:].copy().view(np.int16).reshape(-1).astype(float)
    t = np.arange(len(out)) / FS - 0.05
    return out[t >= 0]


def sweep(exe, drv):
    f = os.path.join(T24, "sweep.bin")
    subprocess.run([exe, "--sweep", "0", str(drv), f], check=True)
    return np.fromfile(f, np.int16).astype(float)


def wav(path, x, gain):
    y = np.clip((x - np.mean(x)) * gain, -1.0, 1.0)
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(FS)
        w.writeframes((y * 32767).astype("<i2").tobytes())


def db(x):
    return float(20 * np.log10(max(x, 1e-12)))


def compare(ref, y, seg):
    n = min(len(ref), len(y))
    ref, y = ref[:n], y[:n]
    rms = lambda v: float(np.sqrt(np.mean(v ** 2)))
    r = dict(level_db=round(db(rms(y)) - db(rms(ref)), 3),
             null_db=round(db(rms(ref - y)) - db(rms(ref)), 1),
             lsd60_db=round(V.lsd60(y, ref) if hasattr(V, "lsd60") else lsd60(y, ref), 3))
    segs = []
    for a in range(0, n - seg + 1, seg):
        sr, sy = ref[a:a + seg], y[a:a + seg]
        if rms(sr) < 1.0:
            segs.append(None)
            continue
        segs.append(dict(level_db=round(db(rms(sy)) - db(rms(sr)), 2),
                         null_db=round(db(rms(sr - sy)) - db(rms(sr)), 1)))
    r["segments"] = segs
    return r


def lsd60(a, b, nfft=4096):
    def psd(x):
        win, acc = np.hanning(nfft), None
        for s in range(0, len(x) - nfft, nfft // 2):
            X = np.abs(np.fft.rfft(win * x[s:s + nfft])) ** 2
            acc = X if acc is None else acc + X
        return acc
    f = np.fft.rfftfreq(nfft, 1 / FS)
    pa, pb = psd(a), psd(b)
    m = (f >= 30) & (f <= 8000) & (pb > pb.max() * 1e-6)
    return float(np.sqrt(np.mean((10 * np.log10(pa[m] / pb[m])) ** 2)))


def main():
    os.makedirs(OUT, exist_ok=True)
    exes = {n: build(n, d, None) for n, d in VARIANTS.items()}
    knobs = V.jload("e2e_results.json")
    sets = {
        "acid": (lambda e: pattern(e, knobs["acid"]["knobs"], 0.0), int(0.125 * FS)),
        "lowres": (lambda e: pattern(e, knobs["lowres"]["knobs"], 0.0), int(0.125 * FS)),
        # S093 P6: drive is 16^drv now: x8 is drv 0.75, x16 is drv 1.0
        "acid_x8": (lambda e: pattern(e, knobs["acid"]["knobs"], 0.75), int(0.125 * FS)),
        "acid_x16": (lambda e: pattern(e, knobs["acid"]["knobs"], 1.0), int(0.125 * FS)),
        "sweep": (lambda e: sweep(e, 0.0), FS),
        "sweep_x8": (lambda e: sweep(e, 0.75), FS),
    }
    res = {}
    for name, (render, seg) in sets.items():
        ys = {v: render(exes[v]) for v in VARIANTS}
        gain = 0.8 / (np.max(np.abs(ys["B2R"] - np.mean(ys["B2R"]))) + 1e-12)
        res[name] = {}
        for v, y in ys.items():
            wav(os.path.join(OUT, f"alk_listen_{name}_{v}.wav"), y, gain)
            if v != "B2R":
                res[name][v] = compare(ys["B2R"], y, seg)
        print(name + ": " + "  ".join(f"{v} level {r['level_db']:+.2f} dB null {r['null_db']:.1f} dB "
                                      f"LSD60 {r['lsd60_db']:.2f}" for v, r in res[name].items()))
    old = os.path.join(OUT, "listen_bc_results.json")
    if os.path.exists(old):                 # keep the recorded c / bc results
        prev = json.load(open(old))
        for k in res:
            for v in ("c", "bc"):
                if v in prev.get(k, {}):
                    res[k][v] = prev[k][v]
    with open(old, "w") as fh:
        json.dump(res, fh, indent=1)


if __name__ == "__main__":
    main()
