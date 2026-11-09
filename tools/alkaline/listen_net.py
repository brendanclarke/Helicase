#!/usr/bin/env python3
"""Slow-network experiments and their listening test (S093 P3).

What:       the B2R voice with three slow-network forms, rendered and measured
            the same way as listen_bc.py:
              B2R   the shipped form: 8 states, one update per 64 samples
                    (T2-2), split over two blocks;
              q128  one update per 128 samples, split over four blocks
                    (gen_alk_tables.py --block 128);
              r6    6 states on one balanced basis common to every Res point
                    (gen_alk_tables.py --states 6, -DALK_NET_STATES=6u).
            Each experiment's tables are generated into build/s093p3/<name>
            (never the source tree). Per variant: the five listening sets
            against B2R (written at B2R's gain), the [SN] p7 calibration ring
            (the bench's AL-R line), LSD60 against the golden model on the
            SPICE traces at x1/x4/x8 (check_c.py C0's inputs), and the
            per-block count (control_cost.py).
Why:        the user asked for audio checks of the slow-network changes.
Outputs:    tools/alkaline/listen/alk_net_<set>_<variant>.wav,
            listen/listen_net_results.json.
Accessors:  python3 tools/alkaline/listen_net.py
"""
import json
import os
import re
import subprocess
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
DT = os.path.join(ROOT, "tools", "dsp_test")
sys.argv = sys.argv[:1]
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "variants"))
import listen_bc as LB    # noqa: E402
import run_variants as RV  # noqa: E402
import validate as V      # noqa: E402
import alk_model as M     # noqa: E402

VARIANTS = {
    "B2R": dict(gen=[], defs=[], rows=4),
    "q128": dict(gen=["--block", "128"], defs=[], rows=2),
    "r6": dict(gen=["--states", "6"], defs=["-DALK_NET_STATES=6u"], rows=3),
}


def tables(name, gen):
    d = os.path.join(ROOT, "build", "s093p3", name)
    host = os.path.join(d, "host")
    os.makedirs(host, exist_ok=True)
    subprocess.run([sys.executable, os.path.join(HERE, "gen_alk_tables.py"), "--out", d] + gen,
                   check=True, capture_output=True)
    for f in ("AlkalineTables.h", "AlkalineTables.c"):
        subprocess.run([sys.executable, os.path.join(DT, "extract.py"), "--src", os.path.join(d, f),
                        "--body", "--out", os.path.join(host, f.replace(".", ".host."))],
                       check=True, cwd=DT, capture_output=True)
    return d, host


def build(name, host, defs):
    exe = os.path.join(ROOT, "build", "s093p3", name, "alk")
    cmd = (["cc", "-O2", "-std=gnu11", "-w", "-fno-vectorize", "-fno-slp-vectorize",
            "-include", "prelude.h", "-I" + host, "-Ibuild/gen", "-I.", "-I../../Core/SampleRom",
            "-include", "cat_host.h", "-I../../Core/MIDI"] + defs +
           ["modules/alkaline.c", "../../Core/SampleRom/SampleMemory.c",
            "../../Core/SampleRom/SampleInstall.c", "catalogue_host.c", "-o", exe, "-lm"])
    subprocess.run(cmd, check=True, cwd=DT)
    return exe


def ring(exe):
    out = subprocess.run([exe], capture_output=True, text=True).stdout
    m = re.search(r"AL-R B2R: ring period ([0-9.]+) ms, decay per cycle ([0-9.-]+)", out)
    return (float(m.group(1)), float(m.group(2))) if m else None


def golden(exe):
    res = {}
    for tag in ("acid", "lowres"):
        for drv in (1.0, 4.0, 8.0):
            tt, k, s, il, ia = RV.case_inputs(tag)
            n = len(s)
            fin = os.path.join(ROOT, "build", "s093p3", "g.in")
            fout = os.path.join(ROOT, "build", "s093p3", "g.out")
            np.concatenate([np.array([n], np.int32).view(np.float32),
                            [np.float32(k / M.RES_KMAX)],
                            np.stack([s, il, ia], 1).astype(np.float32).ravel()]
                           ).astype(np.float32).tofile(fin)
            subprocess.run([exe, "--traces", fin, fout, str(drv)], check=True)
            y = np.fromfile(fout, np.float32).astype(float)[:n]
            ref = RV.reference(tag, drv)
            m = tt >= 0
            res[f"{tag}_x{drv:g}"] = round(LB.lsd60(y[m], ref[m]), 3)
    return res


def cost(d, defs, rows):
    out = subprocess.run([sys.executable, os.path.join(HERE, "control_cost.py"), "--tables", d,
                          "--rows", str(rows)] + sum((["--define", x[2:]] for x in defs), []),
                         capture_output=True, text=True).stdout
    m = re.search(r"per block: .* = (\d+) instructions, (\d+) VDIV", out)
    return (int(m.group(1)), int(m.group(2))) if m else out[-400:]


def main():
    os.makedirs(LB.OUT, exist_ok=True)
    exes, meta = {}, {}
    for name, v in VARIANTS.items():
        d, host = tables(name, v["gen"])
        exes[name] = build(name, host, v["defs"])
        meta[name] = dict(ring_ms_decay=ring(exes[name]), lsd60_golden=golden(exes[name]),
                          per_block=cost(d, v["defs"], v["rows"]))
        print(name, json.dumps(meta[name]))
    knobs = V.jload("e2e_results.json")
    sets = {
        "acid": (lambda e: LB.pattern(e, knobs["acid"]["knobs"], 0.0), int(0.125 * LB.FS)),
        "lowres": (lambda e: LB.pattern(e, knobs["lowres"]["knobs"], 0.0), int(0.125 * LB.FS)),
        "acid_x8": (lambda e: LB.pattern(e, knobs["acid"]["knobs"], 0.75), int(0.125 * LB.FS)),
        "sweep": (lambda e: LB.sweep(e, 0.0), LB.FS),
        "sweep_x8": (lambda e: LB.sweep(e, 0.75), LB.FS),
    }
    res = dict(meta=meta)
    for name, (render, seg) in sets.items():
        ys = {v: render(exes[v]) for v in VARIANTS}
        gain = 0.8 / (np.max(np.abs(ys["B2R"] - np.mean(ys["B2R"]))) + 1e-12)
        res[name] = {}
        for v, y in ys.items():
            LB.wav(os.path.join(LB.OUT, f"alk_net_{name}_{v}.wav"), y, gain)
            if v != "B2R":
                res[name][v] = LB.compare(ys["B2R"], y, seg)
        print(name + ": " + "  ".join(f"{v} level {r['level_db']:+.2f} dB null {r['null_db']:.1f} dB "
                                      f"LSD60 {r['lsd60_db']:.2f}" for v, r in res[name].items()))
    with open(os.path.join(LB.OUT, "listen_net_results.json"), "w") as fh:
        json.dump(res, fh, indent=1)


if __name__ == "__main__":
    main()
