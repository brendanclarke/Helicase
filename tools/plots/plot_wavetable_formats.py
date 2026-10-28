#!/usr/bin/env python3
"""Wavetable storage formats: sound quality against flash size (S084 P1).

Input:  a folder of single-cycle WAV frames (default: SD_CARD/loops, the
        user's example set of 104 x 600-sample AKWF frames).
Output: wavetable_formats.png and a printed summary table.

Each candidate format is simulated the way the oscillator would play it:
the stored table(s) are read with linear interpolation at the LXR-02 rate
(44,108 Hz) for a set of notes, and the rendered signal is analysed:

  alias+image  energy that is not at a harmonic of the note (aliasing from
               harmonics above Nyquist, plus linear-interpolation images),
               in dB relative to the harmonic energy. Lower is cleaner.
  brightness   harmonic energy rendered, relative to the ideal band-limited
               note (every source harmonic below Nyquist, none above), in
               dB. 0 dB = nothing lost; negative = duller than ideal.
  table SNR    16-bit source frame against the stored frame (quantisation
               and resampling error), in dB.

Formats (bytes are for all frames of the set):
  raw        the files as they are (600 points, 16-bit, no mips)
  r256       resampled to 256 points, 16-bit, no mips
  r128       resampled to 128 points, 16-bit, no mips
  r256-8     256 points, 8-bit, no mips
  m256       256 points + half-length mip levels (256,128,64,32,16), 16-bit
  m128       128 points + half-length mips, 16-bit
  m256-8     256 points + mips, 8-bit
Fewer frames (any format) divide storage proportionally; the `wav` control
interpolates between neighbouring frames, so a table can be thinned.
"""
import glob
import math
import os
import sys
import wave

import numpy as np

import lxr_plotlib as L

FS = L.FS
NOTES = [55.0, 110.0, 220.0, 440.0, 880.0, 1760.0, 3520.0]
N_RENDER = 16384


def load_frames(folder):
    frames = []
    for f in sorted(glob.glob(os.path.join(folder, "*.wav"))):
        with wave.open(f) as w:
            n, ch, sw = w.getnframes(), w.getnchannels(), w.getsampwidth()
            raw = w.readframes(n)
        if sw != 2:
            continue
        x = np.frombuffer(raw, dtype="<i2").astype(float)
        if ch == 2:
            x = x.reshape(-1, 2).mean(axis=1)
        frames.append(x / 32768.0)
    return frames


def resample_bl(frame, n, hmax=None):
    """Band-limited resample of one cycle to n points (harmonic truncation)."""
    c = np.fft.rfft(frame) / len(frame)
    h = min(len(c) - 1, n // 2 - 1 if hmax is None else hmax)
    out = np.zeros(n // 2 + 1, dtype=complex)
    out[1: h + 1] = c[1: h + 1]
    return np.fft.irfft(out * n, n)


def quantise(x, bits):
    if bits >= 16:
        return np.round(x * 32767) / 32767
    q = 2 ** (bits - 1) - 1
    return np.round(np.clip(x, -1, 1) * q) / q


def build(frame, fmt):
    """Return the list of stored levels (level 0 first) and bytes per frame."""
    kind, n, bits = fmt["kind"], fmt["n"], fmt["bits"]
    if kind == "raw":
        lv = [quantise(frame, bits)]
    elif kind == "flat":
        lv = [quantise(resample_bl(frame, n), bits)]
    else:  # mip pyramid, half length per level, critical band-limit
        lv, m = [], n
        while m >= 16:
            lv.append(quantise(resample_bl(frame, m), bits))
            m //= 2
    nbytes = sum(len(v) for v in lv) * (2 if bits >= 16 else 1)
    return lv, nbytes


def choose_level(levels, f):
    """Brightest stored level whose harmonics all stay below Nyquist
    (the last, smallest level if none does)."""
    for lv in levels:
        if (len(lv) // 2 - 1) * f <= FS / 2:
            return lv
    return levels[-1]


def render(table, f):
    n = len(table)
    ph = (np.arange(N_RENDER) * f / FS) % 1.0
    idx = ph * n
    i0 = np.floor(idx).astype(int) % n
    fr = idx - np.floor(idx)
    return table[i0] + fr * (table[(i0 + 1) % n] - table[i0])


def analyse(y, f, ideal_energy):
    w = np.blackman(len(y))
    spec = np.abs(np.fft.rfft(y * w)) ** 2
    freqs = np.fft.rfftfreq(len(y), 1 / FS)
    k = np.round(freqs / f)
    near = (np.abs(freqs - k * f) <= 3 * FS / len(y)) & (k >= 1)
    harm = spec[near].sum()
    other = spec[~near & (freqs > 15)].sum()
    norm = (w ** 2).sum() * len(y) / 2.0
    harm_e = harm / norm
    alias_db = 10 * math.log10(other / harm + 1e-15)
    bright_db = 10 * math.log10(harm_e / ideal_energy + 1e-15)
    return alias_db, bright_db


def ideal_energy(frame, f):
    c = np.fft.rfft(frame) / len(frame)
    kmax = int((FS / 2) / f)
    k = np.arange(1, min(len(c), kmax + 1))
    return 2 * np.sum(np.abs(c[k]) ** 2)


FORMATS = [
    dict(name="raw", kind="raw", n=None, bits=16),
    dict(name="r256", kind="flat", n=256, bits=16),
    dict(name="r128", kind="flat", n=128, bits=16),
    dict(name="r256-8", kind="flat", n=256, bits=8),
    dict(name="m256", kind="mip", n=256, bits=16),
    dict(name="m128", kind="mip", n=128, bits=16),
    dict(name="m256-8", kind="mip", n=256, bits=8),
]


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    folder = args[0] if args else os.path.join(L.HERE, "..", "..", "SD_CARD", "loops")
    frames = load_frames(folder)
    if not frames:
        sys.exit(f"no 16-bit WAV frames in {folder}")
    pick = [frames[i] for i in np.linspace(0, len(frames) - 1, 6).astype(int)]
    results = {}
    for fmt in FORMATS:
        bytes_total = 0
        for fr in frames:
            _, nb = build(fr, fmt)
            bytes_total += nb
        al = np.zeros(len(NOTES))
        br = np.zeros(len(NOTES))
        snr = []
        for fr in pick:
            levels, _ = build(fr, fmt)
            src_on_grid = resample_bl(fr, len(levels[0])) if fmt["kind"] != "raw" else fr
            err = levels[0] - src_on_grid
            snr.append(10 * math.log10(np.sum(src_on_grid ** 2) / (np.sum(err ** 2) + 1e-20)))
            for i, f in enumerate(NOTES):
                tab = choose_level(levels, f) if fmt["kind"] == "mip" else levels[0]
                y = render(tab, f)
                a, b = analyse(y, f, ideal_energy(fr, f))
                al[i] += a / len(pick)
                br[i] += b / len(pick)
        results[fmt["name"]] = dict(bytes=bytes_total, alias=al, bright=br, snr=float(np.mean(snr)))

    print(f"{len(frames)} frames from {folder}; source frame length {len(frames[0])}")
    print(f"{'format':8} {'bytes':>9}  " + "  ".join(f"{int(f):>5}Hz" for f in NOTES) + "   (alias+image dB | brightness dB)  table SNR")
    for name, r in results.items():
        al = " ".join(f"{v:6.0f}" for v in r["alias"])
        br = " ".join(f"{v:5.1f}" for v in r["bright"])
        print(f"{name:8} {r['bytes']:9,d}  alias {al}\n{'':19}bright {br}   SNR {r['snr']:.0f} dB")

    plt = L.setup_matplotlib()
    fig, ax = plt.subplots(1, 3, figsize=(16, 5.2), gridspec_kw={"width_ratios": [1.25, 1.25, 1]})
    cols = dict(zip(results, [L.PALETTE[6], L.PALETTE[1], L.PALETTE[2], L.PALETTE[0], L.PALETTE[4], L.PALETTE[3], L.PALETTE[5]]))
    for name, r in results.items():
        ls = "--" if name.startswith("m") else "-"
        ax[0].plot(NOTES, r["alias"], ls, marker="o", ms=3, color=cols[name], label=name)
        ax[1].plot(NOTES, r["bright"], ls, marker="o", ms=3, color=cols[name], label=name)
    for a in ax[:2]:
        a.set_xscale("log")
        a.set_xticks(NOTES)
        a.set_xticklabels([str(int(f)) for f in NOTES])
        a.set_xlabel("note (Hz)")
        a.legend(ncol=2)
    ax[0].set_title("Aliasing + interpolation images (lower is cleaner)")
    ax[0].set_ylabel("dB relative to the note")
    ax[1].set_title("Brightness vs the ideal band-limited note")
    ax[1].set_ylabel("dB (0 = nothing lost)")
    names = list(results)
    ax[2].barh(names, [results[n]["bytes"] / 1024 for n in names], color=[cols[n] for n in names])
    for i, n in enumerate(names):
        ax[2].text(results[n]["bytes"] / 1024 + 2, i, f"{results[n]['bytes'] / 1024:.0f} KB · SNR {results[n]['snr']:.0f} dB",
                   va="center", fontsize=8)
    ax[2].invert_yaxis()
    ax[2].set_xlabel("flash for all frames (KB)")
    ax[2].set_title(f"Storage ({len(frames)} frames)")
    ax[2].set_xlim(0, max(r["bytes"] for r in results.values()) / 1024 * 1.6)
    fig.suptitle(f"Wavetable storage formats on the example set ({len(frames)} × {len(frames[0])}-sample frames)",
                 fontsize=13, fontweight="bold")
    fig.tight_layout(rect=(0, 0, 1, 0.94))
    fig.savefig(L.out_path("wavetable_formats.png"), dpi=110)
    print("wrote wavetable_formats.png")


if __name__ == "__main__" and "--stock" not in sys.argv:
    main()


# --------------------------------------------------------------------------
# Stock-wave layouts (run with --stock): today's 11 octave tables x 1,024
# points against half-length pyramids, using the generated sq3/sw3/sqa/tra/swa
# recipes from plot_stock_waves.py.
# --------------------------------------------------------------------------
def stock_layouts():
    from plot_stock_waves import build as build_waves, N as NW
    waves = build_waves()
    edges = L.OCTAVE_EDGES_HZ
    layouts = {
        "oct11x1024": "11 octave levels x 1,024 points (today's stock format)",
        "pyr1024": "pyramid 1,024 -> 16 points, critical band-limit",
        "pyr2048": "pyramid 2,048 -> 16 points, critical band-limit",
        "oct11os8": "11 octave levels, length = pow2 >= 8 x harmonics (max 1,024)",
        "oct11os4": "11 octave levels, length = pow2 >= 4 x harmonics (max 1,024)",
    }
    print("\nStock-wave layouts (mean over sq3 sw3 sqa tra swa)")
    for lay, desc in layouts.items():
        al = np.zeros(len(NOTES))
        br = np.zeros(len(NOTES))
        for name, w in waves.items():
            src = L.from_harmonics(w["coeffs"], NW)
            if lay.startswith("oct11"):
                tabs, nbytes = [], 0
                for k in range(11):
                    top = edges[k] if k < 10 else FS / 2
                    h = max(1, min(511, int((FS / 2) / top)))
                    n = 1024
                    if lay != "oct11x1024":
                        os_ = 8 if lay == "oct11os8" else 4
                        n = 16
                        while n < os_ * h and n < 1024:
                            n *= 2
                    tabs.append((edges[k - 1] if k else 0.0, resample_bl(src, n, min(h, n // 2 - 1))))
                    nbytes += n * 2
            else:
                n0 = 1024 if lay == "pyr1024" else 2048
                levels, m = [], n0
                while m >= 16:
                    levels.append(resample_bl(src, m))
                    m //= 2
                nbytes = sum(len(v) for v in levels) * 2
            for i, f in enumerate(NOTES):
                if lay.startswith("oct11"):
                    tab = [t for lo, t in tabs if f >= lo][-1]
                else:
                    tab = choose_level(levels, f)
                y = render(tab, f)
                a, b = analyse(y, f, ideal_energy(src, f))
                al[i] += a / len(waves)
                br[i] += b / len(waves)
        print(f"{lay:11} {nbytes:6d} B/wave  alias " + " ".join(f"{v:5.0f}" for v in al)
              + "  bright " + " ".join(f"{v:5.1f}" for v in br) + f"   ({desc})")


if __name__ == "__main__" and "--stock" in sys.argv:
    stock_layouts()
