#!/usr/bin/env python3
"""Cost and sound of the S093 P1 ladder kernel variants.

What:       for every variant in VARIANTS (compile-time switches of
            dlv_kernel.c):
              cost   M7 per-sample loop instructions, VDIV and loads/stores
                     (arm-none-eabi-gcc -Ofast, firmware flags; loops found
                     from the backward branches of dlv_render and vcaPass);
              sound  the host build driven by the SPICE end-to-end run's saw
                     x drive and its currents (block-ramped, as the firmware
                     applies them), against the golden RK4 model (LadderRef)
                     on the same inputs: log-spectral distance within 60 dB
                     of the peak (LSD60) and level.
Why:        S093_P1_ALKALINE_OVERHEAD_REDUCTIONS.md.
Inputs:     tools/dsp_test/build/gen (make -C tools/dsp_test alkaline); the
            e2e traces (tools/spice/tb303/e2e.py).
Outputs:    variants_results.json; the reference cache in the tempdir.
Accessors:  python3 tools/alkaline/variants/run_variants.py [name ...]
"""
import json
import os
import re
import subprocess
import sys
import tempfile

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ALK = os.path.dirname(HERE)
ROOT = os.path.dirname(os.path.dirname(ALK))
GEN = os.path.join(ROOT, "tools", "dsp_test", "build", "gen")
PRELUDE = os.path.join(ROOT, "tools", "dsp_test", "prelude.h")
TMP = os.path.join(tempfile.gettempdir(), "alk_variants")
os.makedirs(TMP, exist_ok=True)
argv = sys.argv[1:]
sys.argv = sys.argv[:1]
sys.path.insert(0, ALK)
import alk_model as M   # noqa: E402
import validate as V    # noqa: E402

BASE = dict(MODEL=1, PADE_RUNG=54, PADE_IO=54, RUNG_CLAMP=1, XIN_TAIL=0, GAU1=1,
            SPLIT=0, UNROLL=1, RUNG_HALF=0, NET1=0, Q21_LIN=0, RRAMP=1, OTA_P32=0)
VARIANTS = {}


def var(name, **kw):
    d = dict(BASE)
    d.update(kw)
    VARIANTS[name] = d


# --- baselines (the S093 P0 kernels)
var("B0")
var("A0", MODEL=0)
# --- single reductions on B
var("B_xtail", XIN_TAIL=1)
var("B_gau1", GAU1=0)
var("B_p32rung", PADE_RUNG=32)
var("B_noclamp", RUNG_CLAMP=0)
var("B_p32io", PADE_IO=32)
var("B_split", SPLIT=1)
var("B_unroll", UNROLL=2)
var("B_half", UNROLL=2, RUNG_HALF=1)
# --- combined
var("B_r1", XIN_TAIL=1, GAU1=0, PADE_RUNG=32, RUNG_CLAMP=0)
var("B_r2", XIN_TAIL=1, GAU1=0, PADE_RUNG=32, RUNG_CLAMP=0, SPLIT=1)
var("B_r3", XIN_TAIL=1, GAU1=0, PADE_RUNG=32, RUNG_CLAMP=0, SPLIT=1, UNROLL=2)
var("B_r4", XIN_TAIL=1, GAU1=0, PADE_RUNG=32, RUNG_CLAMP=0, SPLIT=1, UNROLL=2, RUNG_HALF=1)
# --- A and the A1 hybrid (rung 0 nonlinear)
var("A_xtail", MODEL=0, XIN_TAIL=1)
var("A_r", MODEL=0, XIN_TAIL=1, GAU1=0, SPLIT=1)
var("A_ru", MODEL=0, XIN_TAIL=1, GAU1=0, SPLIT=1, UNROLL=2)
var("A1", MODEL=2, XIN_TAIL=1)
var("A1_r", MODEL=2, XIN_TAIL=1, GAU1=0, PADE_RUNG=32, SPLIT=1)
var("A1_ru", MODEL=2, XIN_TAIL=1, GAU1=0, PADE_RUNG=32, SPLIT=1, UNROLL=2)
# --- network form N1 (no per-sample fast modes) and a linear Q21
var("B_n1", XIN_TAIL=1, NET1=1)
var("B_q21", XIN_TAIL=1, Q21_LIN=1)
var("B_n1r", XIN_TAIL=1, NET1=1, GAU1=0, PADE_RUNG=32, RUNG_CLAMP=0)
var("B_n1rq", XIN_TAIL=1, NET1=1, GAU1=0, PADE_RUNG=32, RUNG_CLAMP=0, Q21_LIN=1)
R5 = dict(XIN_TAIL=1, NET1=1, GAU1=0, PADE_RUNG=32, RUNG_CLAMP=0, Q21_LIN=1)
var("B_n1rq_hold", RRAMP=0, **R5)
# S093 P2 weighing: B-R (= B_n1rq_ota32) and its accuracy-cost cuts
var("BR_half", OTA_P32=1, UNROLL=2, RUNG_HALF=1, **R5)
var("B2R", MODEL=3, OTA_P32=1, **R5)
var("B2R_half", MODEL=3, OTA_P32=1, UNROLL=2, RUNG_HALF=1, **R5)
var("A1R_ota32", MODEL=2, XIN_TAIL=1, NET1=1, GAU1=0, PADE_RUNG=32, Q21_LIN=1, OTA_P32=1)
var("B_n1rq_ota32", OTA_P32=1, **R5)
var("B_n1rq_unroll", UNROLL=2, **R5)
var("B_n1rq_half", UNROLL=2, RUNG_HALF=1, **R5)
var("A_n1", MODEL=0, XIN_TAIL=1, NET1=1, GAU1=0)
var("A_n1q", MODEL=0, XIN_TAIL=1, NET1=1, GAU1=0, Q21_LIN=1)
var("A1_n1", MODEL=2, XIN_TAIL=1, NET1=1, GAU1=0, PADE_RUNG=32)
var("A1_n1q", MODEL=2, XIN_TAIL=1, NET1=1, GAU1=0, PADE_RUNG=32, Q21_LIN=1)

CASES = [("acid", 1.0), ("acid", 4.0), ("acid", 8.0), ("lowres", 1.0), ("lowres", 4.0)]
WINDOW = (-0.05, 0.6)


def dflags(v):
    return ["-D%s=%d" % kv for kv in v.items()]


# ------------------------------------------------------------------ cost
ARM = ["arm-none-eabi-gcc", "-mcpu=cortex-m7", "-mthumb", "-mfpu=fpv5-d16",
       "-mfloat-abi=hard", "-Ofast", "-std=gnu11", "-w", "-I" + HERE]
mk = open(os.path.join(ROOT, "Makefile")).read()
ARM += ["-I" + os.path.join(ROOT, i.rstrip("\\")) for i in
        re.findall(r"-I(\S+)", mk.split("ASFLAGS")[0])]


def loops(dis, fn):
    rows = []
    on = False
    for l in dis.splitlines():
        if re.match(r"^[0-9a-f]+ <%s>:" % re.escape(fn), l):
            on = True
            continue
        if on and re.match(r"^[0-9a-f]+ <", l):
            break
        m = re.match(r"\s+([0-9a-f]+):\s+(\S+)\s*(.*)", l)
        if on and m:
            rows.append((int(m.group(1), 16), m.group(2), m.group(3)))
    out = []
    for idx, (addr, op, args) in enumerate(rows):
        mb = re.match(r"([0-9a-f]+) <", args)
        # conditional backward branches only (an unconditional b/b.w back to
        # the start is the unresolved tail call into dlv_slowUpdate)
        if (op.startswith("b") and not op.startswith("bl") and mb and
                op.split(".")[0] not in ("b", "bx")):
            tgt = int(mb.group(1), 16)
            if tgt < addr:
                body = [r for r in rows if tgt <= r[0] <= addr]
                out.append(body)
    return out


def cost(v):
    obj = os.path.join(TMP, "k.o")
    subprocess.run(ARM + dflags(v) + ["-c", os.path.join(HERE, "dlv_kernel.c"), "-o", obj],
                   check=True)
    dis = subprocess.run(["arm-none-eabi-objdump", "-d", "--no-show-raw-insn", obj],
                         check=True, capture_output=True, text=True).stdout
    tot = dict(insn=0.0, vdiv=0.0, mem=0.0, mov=0.0)
    for fn, per in (("dlv_render", v["UNROLL"]), ("vcaPass", 1)):
        ls = loops(dis, fn)
        if not ls:
            continue
        body = max(ls, key=len)          # the per-sample loop
        tot["insn"] += len(body) / per
        tot["vdiv"] += sum(1 for r in body if r[1].startswith("vdiv")) / per
        tot["mem"] += sum(1 for r in body if r[1].startswith(("vldr", "vstr"))) / per
        tot["mov"] += sum(1 for r in body if r[1].startswith("vmov")) / per
    return {k: round(x, 1) for k, x in tot.items()}


# ------------------------------------------------------------------ sound
def ramp(x):
    e = x[31::32]
    prev = np.concatenate([[0.0], e[:-1]])
    r = (np.arange(32) + 1.0) / 32
    return (prev[:, None] + (e - prev)[:, None] * r[None, :]).ravel()


def case_inputs(tag):
    tn, d = V.load_traces(tag)
    k = V.jload("e2e_results.json")[tag]["knobs"]["res"]
    sel = (tn > WINDOW[0]) & (tn < WINDOW[1])
    tt = tn[sel]
    n = (len(tt) // 32) * 32
    s = (d["saw"][sel] - np.mean(d["saw"][tn < 0]))[:n]
    return tt[:n], k, s, np.abs(d["itail"][sel])[:n], np.abs(d["iabc"][sel])[:n]


def reference(tag, drv):
    path = os.path.join(TMP, f"ref_{tag}_{drv:g}.npy")
    if os.path.exists(path):
        return np.load(path)
    tt, k, s, il, ia = case_inputs(tag)
    lad = M.LadderRef()
    M.settle(lad, 0.0, k)
    lad.process(drv * s, ramp(il), k)
    y = M.vca_out(lad.vca_in, ramp(ia))
    np.save(path, y)
    return y


def build_host(name, v):
    exe = os.path.join(TMP, "h_" + name)
    subprocess.run(["cc", "-O2", "-std=gnu11", "-w", "-include", PRELUDE, "-I" + GEN, "-I" + HERE,
                    "-I" + os.path.join(ROOT, "Core", "DSPAudio")] + dflags(v) +
                   [os.path.join(HERE, f) for f in ("dlv_host.c", "dlv_common.c", "dlv_kernel.c",
                                                     "AlkalineTablesN1.c")] +
                   ["-o", exe, "-lm"], check=True)
    return exe


def run_host(exe, tag, drv):
    tt, k, s, il, ia = case_inputs(tag)
    n = len(s)
    fin, fout = os.path.join(TMP, "in.bin"), os.path.join(TMP, "out.bin")
    np.concatenate([np.array([n], np.int32).view(np.float32), [np.float32(k)], [np.float32(drv)],
                    np.stack([s, il, ia], 1).astype(np.float32).ravel()]).astype(np.float32).tofile(fin)
    subprocess.run([exe, fin, fout], check=True)
    return tt, np.fromfile(fout, np.float32).astype(float)


def lsd60(a, b, nfft=4096):
    def psd(x):
        win, acc = np.hanning(nfft), None
        for i in range(0, len(x) - nfft, nfft // 2):
            X = np.abs(np.fft.rfft(win * x[i:i + nfft])) ** 2
            acc = X if acc is None else acc + X
        return acc
    f = np.fft.rfftfreq(nfft, 1 / M.FS)
    pa, pb = psd(a), psd(b)
    m = (f >= 30) & (f <= 8000) & (pb > pb.max() * 1e-6)
    return float(np.sqrt(np.mean((10 * np.log10(pa[m] / pb[m])) ** 2)))


def main():
    names = argv or list(VARIANTS)
    res = {}
    path = os.path.join(HERE, "variants_results.json")
    if os.path.exists(path):
        res = json.load(open(path))
    base_out = {}
    for name in names:
        v = VARIANTS[name]
        c = cost(v)
        exe = build_host(name, v)
        snd = {}
        for tag, drv in CASES:
            tt, y = run_host(exe, tag, drv)
            ref = reference(tag, drv)
            m = tt >= 0
            snd[f"{tag}_x{drv:g}"] = dict(lsd60=round(lsd60(y[m], ref[m]), 3),
                                          level_db=round(float(V.level_db(y[m], ref[m])), 3))
        res[name] = dict(flags=v, cost=c, sound=snd)
        print(f"{name:10s} {c['insn']:6.1f} insn {c['vdiv']:.1f} vdiv {c['mem']:5.1f} ld/st | " +
              "  ".join(f"{k} {d['lsd60']:.2f}/{d['level_db']:+.2f}" for k, d in snd.items()))
        with open(path, "w") as fh:
            json.dump(res, fh, indent=1)


if __name__ == "__main__":
    main()
