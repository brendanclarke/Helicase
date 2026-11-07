#!/usr/bin/env python3
"""What 'weaker drive' means: A-R, B-R and B2R against the golden model.

What:       for the S092 P4 acid run (Res 0.85) and lowres run (Res 0.2) at
            drive x1/x4/x8: output level (and its growth with drive, i.e.
            compression), level per frequency band against the golden model,
            and the resonance's share of the output (energy within +-1/3
            octave of the resonance frequency, per step). Writes listening
            renders alk_drive_<run>_x<d>_<form>.wav to tools/alkaline/.
Why:        S093_P2_ALKALINE_B-R_PRE_IMPLEMENTATION.md §3B.
Accessors:  python3 tools/alkaline/variants/drive_character.py
"""
import os
import sys
import json
import numpy as np
sys.argv = sys.argv[:1]
import run_variants as R
import alk_model as M
import validate as V

FORMS = {"A-R": "A_n1q", "B-R": "B_n1rq_ota32", "B2R": "B2R"}
BANDS = ((30, 200), (200, 800), (800, 3000), (3000, 8000))
CASES = [("acid", 1.0), ("acid", 4.0), ("acid", 8.0), ("lowres", 1.0), ("lowres", 4.0), ("lowres", 8.0)]


def psd(x, nfft=4096):
    win, acc = np.hanning(nfft), None
    for i in range(0, len(x) - nfft, nfft // 2):
        X = np.abs(np.fft.rfft(win * x[i:i + nfft])) ** 2
        acc = X if acc is None else acc + X
    return acc


def band_db(x, ref):
    f = np.fft.rfftfreq(4096, 1 / M.FS)
    px, pr = psd(x), psd(ref)
    return [float(10 * np.log10(px[(f >= a) & (f < b)].sum() / pr[(f >= a) & (f < b)].sum()))
            for a, b in BANDS]


def reso_share(tt, y, il, k):
    """Energy within +-1/3 octave of the resonance frequency (P2 law from the
    ladder current) over total energy, per 16th step, averaged over steps."""
    import e2e as E
    flaw = E.f_res_law()
    shares = []
    for st in range(5):
        m = (tt >= st * 0.125) & (tt < (st + 1) * 0.125)
        if m.sum() < 4096:
            continue
        seg = y[m][:4096]
        fr = float(np.median(flaw(il[m])))
        X = np.abs(np.fft.rfft(np.hanning(4096) * seg)) ** 2
        f = np.fft.rfftfreq(4096, 1 / M.FS)
        sel = (f > fr / 2 ** (1 / 3)) & (f < fr * 2 ** (1 / 3))
        shares.append(X[sel].sum() / X[(f > 30)].sum())
    return float(10 * np.log10(np.mean(shares)))


def main():
    sys.path.insert(0, os.path.join(R.ROOT, "tools", "spice", "tb303"))
    exes = {n: R.build_host(v, R.VARIANTS[v]) for n, v in FORMS.items()}
    out = {}
    for tag, drv in CASES:
        tt, k, s, il, ia = R.case_inputs(tag)
        ref = R.reference(tag, drv)
        m = tt >= 0
        row = {"golden": dict(level_db=float(20 * np.log10(np.sqrt(np.mean(ref[m] ** 2)))),
                              reso_db=reso_share(tt[m], ref[m], il[m], k))}
        V.write_wav(os.path.join(R.ALK, f"alk_drive_{tag}_x{drv:g}_golden.wav"), ref[m])
        for name, exe in exes.items():
            _, y = R.run_host(exe, tag, drv)
            row[name] = dict(level_db=float(20 * np.log10(np.sqrt(np.mean(y[m] ** 2)))),
                             vs_golden_db=float(V.level_db(y[m], ref[m])),
                             bands_db=band_db(y[m], ref[m]),
                             reso_db=reso_share(tt[m], y[m], il[m], k),
                             lsd60=R.lsd60(y[m], ref[m]))
            V.write_wav(os.path.join(R.ALK, f"alk_drive_{tag}_x{drv:g}_{name}.wav"), y[m])
        out[f"{tag}_x{drv:g}"] = row
        g = row["golden"]
        print(f"{tag} x{drv:g}: golden level {g['level_db']:+.2f} dBV, resonance share {g['reso_db']:+.2f} dB")
        for name in FORMS:
            r = row[name]
            print(f"   {name:4s} level {r['vs_golden_db']:+.2f} | bands 30-200/200-800/800-3k/3k-8k "
                  + " ".join(f"{b:+.2f}" for b in r["bands_db"])
                  + f" | resonance share {r['reso_db']:+.2f} ({r['reso_db'] - g['reso_db']:+.2f}) | LSD60 {r['lsd60']:.2f}")
    json.dump(out, open(os.path.join(R.HERE, "drive_character.json"), "w"), indent=1)


if __name__ == "__main__":
    main()
