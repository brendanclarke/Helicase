#!/usr/bin/env python3
"""
sampleoptimize.py — shrink an LXR-02 card's wavetables and samples so they
take less sample flash but play the same.

Usage
-----
    python3 tools/sampleoptimize.py [CARD_ROOT] [--worst-db 20]
                                    [--typical-db 40] [--trim-db 80]
                                    [--dry-run]

CARD_ROOT defaults to SD_CARD/ at the repository root. The script works on
two optional folders under it:

    wavetables/   every folder (recursively) that holds .wav files is one
                  wavetable; each file is one single-cycle frame
    samples/      one-shot samples (Load:[Samples] installs these)

What it does
------------
1. Backup. `wavetables` is copied to `wavetables_old` and `samples` to
   `samples_old`. If an `_old` folder already exists it is left untouched
   and used as the source. A second run therefore re-optimises from your
   originals instead of backing up already-optimised files.
2. Wavetables. For each wavetable, it finds the smallest power-of-two frame
   length that **plays the same**: the candidate table and the original
   are both read the way the oscillator reads them (linear interpolation,
   on a phase grid 16x finer than the longer table). The difference must
   meet both thresholds:
   - no frame's playback error above --worst-db (default -20 dB);
   - the median frame's error at or below --typical-db (default -40 dB).
   Frames are resampled to that length by harmonic truncation (band-limited,
   DC kept), written as mono 16-bit at the source rate, and keep their file
   names, so the frame order is unchanged.
   - If the result would not be smaller than the source, or no shorter
     length passes, the files are copied unchanged.
   - Frames of mixed lengths are all resampled to one common length, because
     the firmware needs one length per wavetable. If no shorter length
     passes, that is the longest frame's length.
   - If band-limiting would push a peak past full scale, the whole table
     is scaled down by one common factor (reported).
3. Samples. Two changes only:
   - **Trailing trim:** the tail below --trim-db (default 80 dB) under the
     sample's peak is removed. A one-shot plays the same; the onset is
     never moved.
   - **Conversion:** files the firmware cannot install (stereo, 8/24/32-bit,
     sample rates other than 44,100 Hz) are converted to mono, 16-bit,
     44,100 Hz so they install at all. Stereo is mixed to mono, so those
     files change by design.
   A file that needs neither change is copied byte-for-byte.
4. The optimised files replace the contents of `wavetables` and `samples`.
   Non-WAV files are copied through. A summary prints the flash used before
   and after.

Run with --dry-run to see the report without writing anything.

Notes
-----
- "Plays the same" compares playback, so it covers both the harmonics that
  resampling removes and the interpolation error a shorter table adds.
  - On the S084 example set (104 x 600-sample AKWF frames) it gives
    256 points: 124,800 B -> 53,248 B. The worst frame is at -23 dB and the
    median at -45 dB, with a measured brightness change of 0.1 dB or less
    (tools/plots/plot_wavetable_formats.py).
  - Bright tables (e.g. raw saws) keep most of their length, because their
    upper harmonics are real content.
- Extra WAV chunks (smpl, acid, ...) are not written for re-encoded files;
  the firmware ignores them.
- Requires Python 3 and numpy (not part of the stdlib-only build
  requirements):
      python3 -m venv /tmp/lxrtools && /tmp/lxrtools/bin/pip install numpy
"""
import argparse
import os
import re
import shutil
import sys
import wave

try:
    import numpy as np
except ImportError:  # pragma: no cover
    sys.exit("sampleoptimize.py needs numpy: python3 -m pip install numpy")

FIRMWARE_RATE = 44100
SAMPLE_FLASH_BYTES = 1308320      # sample audio region today (STORAGE_SRAM_MANIFEST §4.1)
STOCK_WAVES_BYTES = 61120         # planned stock-wave area (S084 P1 §4.1.2)
MIN_FRAME_POINTS = 16


# ----------------------------------------------------------------- helpers
def natural_key(name):
    """Numeric-aware sort key: 'w2' before 'w10' (the planned card order)."""
    return [int(t) if t.isdigit() else t.lower() for t in re.split(r"(\d+)", name)]


def is_wav(name):
    return name.lower().endswith(".wav") and not name.startswith(".")


def read_wav(path):
    """Return (float mono samples in -1..1, sample rate, channels, bytes/sample)."""
    with wave.open(path, "rb") as w:
        ch, sw, rate, n = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
        raw = w.readframes(n)
    if sw == 1:
        x = (np.frombuffer(raw, dtype=np.uint8).astype(np.float64) - 128.0) / 128.0
    elif sw == 2:
        x = np.frombuffer(raw, dtype="<i2").astype(np.float64) / 32768.0
    elif sw == 3:
        b = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 3).astype(np.int32)
        v = b[:, 0] | (b[:, 1] << 8) | (b[:, 2] << 16)
        v = np.where(v & 0x800000, v - 0x1000000, v)
        x = v.astype(np.float64) / 8388608.0
    elif sw == 4:
        x = np.frombuffer(raw, dtype="<i4").astype(np.float64) / 2147483648.0
    else:
        raise ValueError(f"unsupported sample width {sw}")
    if ch > 1:
        x = x.reshape(-1, ch).mean(axis=1)
    return x, rate, ch, sw


def write_wav16(path, x, rate):
    y = np.clip(np.round(x * 32767.0), -32768, 32767).astype("<i2")
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(int(rate))
        w.writeframes(y.tobytes())


def resample_cycle(frame, n):
    """Band-limited resample of one cycle to n points (DC and harmonics < n/2 kept)."""
    c = np.fft.rfft(frame) / len(frame)
    h = min(len(c) - 1, n // 2 - 1)
    out = np.zeros(n // 2 + 1, dtype=complex)
    out[: h + 1] = c[: h + 1]
    return np.fft.irfft(out * n, n)


def fit_frame(frame, n):
    """Frame at n points. Shortening is band-limited (resample_cycle).
    Lengthening, needed only to unify mixed-length tables, reads the
    original with linear interpolation: exactly what the oscillator played,
    with no new overshoot."""
    if n >= len(frame):
        return interp_read(frame, np.arange(n) / n)
    return resample_cycle(frame, n)


def interp_read(table, ph):
    """Linear-interpolation read at phases ph (0..1), as the oscillator does."""
    n = len(table)
    pos = ph * n
    i0 = np.floor(pos).astype(int) % n
    fr = pos - np.floor(pos)
    return table[i0] + fr * (table[(i0 + 1) % n] - table[i0])


def playback_error_db(frame, n):
    """Difference between playing the frame resampled to n points and playing
    the original, in dB relative to the original's AC energy. Both are read
    with linear interpolation on a phase grid 16x finer than the longer
    table, so the result covers removed harmonics and interpolation error."""
    m = 16 * max(len(frame), n)
    ph = np.arange(m) / m
    ref = interp_read(frame, ph)
    cand = interp_read(fit_frame(frame, n), ph)
    ac = np.sum((ref - ref.mean()) ** 2)
    if ac <= 0:
        return -300.0
    err = np.sum((cand - ref) ** 2)
    return 10 * np.log10(err / ac) if err > 0 else -300.0


def resample_signal(x, src_rate, dst_rate):
    """Band-limited sample-rate conversion of a one-shot (zero-padded FFT)."""
    if src_rate == dst_rate or len(x) == 0:
        return x
    pad = 4096
    xp = np.concatenate([np.zeros(pad), x, np.zeros(pad)])
    n_out = int(round(len(xp) * dst_rate / src_rate))
    X = np.fft.rfft(xp)
    Y = np.zeros(n_out // 2 + 1, dtype=complex)
    m = min(len(X), len(Y))
    Y[:m] = X[:m]
    y = np.fft.irfft(Y, n_out) * (n_out / len(xp))
    p_out = int(round(pad * dst_rate / src_rate))
    return y[p_out: n_out - p_out]


def fmt_bytes(n):
    return f"{n:,} B"


# ----------------------------------------------------------------- wavetables
def optimise_wavetable(src_dir, files, dst_dir, worst_db, typical_db, dry):
    frames, rates, src_bytes = [], [], 0
    for f in files:
        x, rate, ch, sw = read_wav(os.path.join(src_dir, f))
        frames.append(x)
        rates.append(rate)
        src_bytes += len(x) * sw * ch
    lengths = sorted(set(len(x) for x in frames))
    max_len = lengths[-1]

    chosen = None
    n = MIN_FRAME_POINTS
    while n < max_len:
        errs = [playback_error_db(x, n) for x in frames]
        worst, typical = max(errs), float(np.median(errs))
        if worst <= -worst_db and typical <= -typical_db:
            chosen, chosen_worst, chosen_typ = n, worst, typical
            break
        n *= 2

    uniform = len(lengths) == 1
    out_bytes = None if chosen is None else chosen * 2 * len(frames)
    keep_original = uniform and (chosen is None or out_bytes >= src_bytes)

    if keep_original:
        action = "unchanged (no shorter length plays the same)" if chosen is None \
            else f"unchanged ({chosen} points would not be smaller)"
        if not dry:
            os.makedirs(dst_dir, exist_ok=True)
            for f in files:
                shutil.copy2(os.path.join(src_dir, f), os.path.join(dst_dir, f))
        return src_bytes, src_bytes, action

    if chosen is None:            # mixed lengths, nothing shorter passes:
        chosen = max_len          # unify at the longest frame's length
        errs = [playback_error_db(x, chosen) for x in frames]
        chosen_worst, chosen_typ = max(errs), float(np.median(errs))
    out = [fit_frame(x, chosen) for x in frames]
    peak = max(np.max(np.abs(y)) for y in out)
    scale = 1.0 if peak <= 0.9999 else 0.9999 / peak
    def show(db):
        return "exact" if db <= -200 else f"{db:.1f} dB"
    action = (f"{max_len if uniform else '/'.join(map(str, lengths))} -> {chosen} points, "
              f"playback error worst {show(chosen_worst)} / median {show(chosen_typ)}")
    if not uniform:
        action += " (lengths unified for install)"
    if scale < 1.0:
        action += f", table scaled {20 * np.log10(scale):.2f} dB to avoid clipping"
    if not dry:
        os.makedirs(dst_dir, exist_ok=True)
        for f, y, rate in zip(files, out, rates):
            write_wav16(os.path.join(dst_dir, f), y * scale, rate)
    return src_bytes, chosen * 2 * len(frames), action


def process_wavetables(src_root, dst_root, limits, dry):
    worst_db, typical_db = limits
    total_in = total_out = 0
    print(f"\nWavetables (plays the same: worst frame <= -{worst_db:g} dB, median <= -{typical_db:g} dB)")
    for dirpath, dirnames, filenames in os.walk(src_root):
        dirnames.sort(key=natural_key)
        rel = os.path.relpath(dirpath, src_root)
        dst_dir = os.path.join(dst_root, rel) if rel != "." else dst_root
        wavs = sorted([f for f in filenames if is_wav(f)], key=natural_key)
        others = [f for f in filenames if not is_wav(f) and not f.startswith(".")]
        if not dry:
            os.makedirs(dst_dir, exist_ok=True)
            for f in others:
                shutil.copy2(os.path.join(dirpath, f), os.path.join(dst_dir, f))
        if not wavs:
            continue
        try:
            b_in, b_out, action = optimise_wavetable(dirpath, wavs, dst_dir, worst_db, typical_db, dry)
        except (wave.Error, ValueError, EOFError) as e:
            print(f"  {rel}: skipped ({e})")
            continue
        total_in += b_in
        total_out += b_out
        name = rel if rel != "." else "(wavetables root)"
        print(f"  {name}: {len(wavs)} frames, {fmt_bytes(b_in)} -> {fmt_bytes(b_out)}  [{action}]")
    return total_in, total_out


# ----------------------------------------------------------------- samples
def process_samples(src_root, dst_root, trim_db, dry):
    total_in = total_out = 0
    print(f"\nSamples (trailing trim below -{trim_db:g} dB re peak)")
    if not dry:
        os.makedirs(dst_root, exist_ok=True)
    for f in sorted(os.listdir(src_root), key=natural_key):
        src = os.path.join(src_root, f)
        dst = os.path.join(dst_root, f)
        if os.path.isdir(src) or f.startswith("."):
            if os.path.isdir(src) and not f.startswith(".") and not dry:
                shutil.copytree(src, dst, dirs_exist_ok=True)
            continue
        if not is_wav(f):
            if not dry:
                shutil.copy2(src, dst)
            continue
        try:
            x, rate, ch, sw = read_wav(src)
        except (wave.Error, ValueError, EOFError) as e:
            print(f"  {f}: copied unchanged (unreadable: {e})")
            if not dry:
                shutil.copy2(src, dst)
            continue
        b_in = len(x) * 2                       # what the firmware would store
        notes = []
        conforming = (ch == 1 and sw == 2 and rate == FIRMWARE_RATE)
        if not conforming:
            b_in = 0                            # does not install today
            if ch != 1:
                notes.append(f"{ch}ch->mono")
            if sw != 2:
                notes.append(f"{8 * sw}-bit->16-bit")
            if rate != FIRMWARE_RATE:
                x = resample_signal(x, rate, FIRMWARE_RATE)
                notes.append(f"{rate}->{FIRMWARE_RATE} Hz")
        peak = float(np.max(np.abs(x))) if len(x) else 0.0
        end = len(x)
        if peak > 0:
            thr = max(peak * 10 ** (-trim_db / 20.0), 1.0 / 32768.0)
            above = np.nonzero(np.abs(x) > thr)[0]
            if len(above):
                end = min(len(x), int(above[-1]) + 32)
        trimmed = len(x) - end
        if trimmed > 0:
            notes.append(f"trimmed {trimmed} samples ({trimmed / FIRMWARE_RATE * 1000:.0f} ms) of tail")
        b_out = end * 2
        if conforming and trimmed == 0:
            if not dry:
                shutil.copy2(src, dst)
            notes.append("unchanged")
        elif not dry:
            if peak > 0.99997:
                x = x * (0.99997 / peak)
            write_wav16(dst, x[:end], FIRMWARE_RATE)
        total_in += b_in
        total_out += b_out
        label = "" if conforming else "  (did not install before)"
        print(f"  {f}: {fmt_bytes(b_in)} -> {fmt_bytes(b_out)}  [{', '.join(notes)}]{label}")
    return total_in, total_out


# ----------------------------------------------------------------- main
def prepare(card, name, dry):
    """Back up card/name to card/name_old (once) and return (source, target)."""
    live = os.path.join(card, name)
    old = os.path.join(card, name + "_old")
    if os.path.isdir(old):
        print(f"{name}_old exists: using it as the source (backup left untouched)")
        return old, live
    if not os.path.isdir(live):
        return None, None
    if not dry:
        shutil.copytree(live, old)
        print(f"backed up {name}/ -> {name}_old/")
    else:
        print(f"(dry run) would back up {name}/ -> {name}_old/")
    return (old if not dry else live), live


def main():
    repo = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("card", nargs="?", default=os.path.join(repo, "SD_CARD"),
                    help="card root holding wavetables/ and samples/ (default: SD_CARD/)")
    ap.add_argument("--worst-db", type=float, default=20.0,
                    help="no wavetable frame may play more than this many dB different from the original (default 20)")
    ap.add_argument("--typical-db", type=float, default=40.0,
                    help="the median frame must play at least this many dB close to the original (default 40)")
    ap.add_argument("--trim-db", type=float, default=80.0,
                    help="sample tail below this many dB under the peak is trimmed (default 80)")
    ap.add_argument("--dry-run", action="store_true", help="report only; write nothing")
    args = ap.parse_args()

    card = os.path.abspath(args.card)
    if not os.path.isdir(card):
        sys.exit(f"card root not found: {card}")
    print(f"card: {card}")

    tmp = os.path.join(card, ".sampleoptimize_tmp")
    if not args.dry_run:
        shutil.rmtree(tmp, ignore_errors=True)
        os.makedirs(tmp)

    totals = {}
    for name, fn, opt in (("wavetables", process_wavetables, (args.worst_db, args.typical_db)),
                          ("samples", process_samples, args.trim_db)):
        src, live = prepare(card, name, args.dry_run)
        if src is None:
            print(f"\n{name}/: not present, skipped")
            continue
        out = os.path.join(tmp, name)
        totals[name] = fn(src, out, opt, args.dry_run)
        if not args.dry_run:
            # The backup exists, so the live folder can be replaced safely.
            shutil.rmtree(live, ignore_errors=True)
            shutil.move(out, live)

    if not args.dry_run:
        shutil.rmtree(tmp, ignore_errors=True)

    print("\nSummary (bytes of audio the firmware stores)")
    grand_in = grand_out = 0
    for name, (b_in, b_out) in totals.items():
        print(f"  {name:10} {fmt_bytes(b_in):>14} -> {fmt_bytes(b_out):>14}")
        grand_in += b_in
        grand_out += b_out
    avail = SAMPLE_FLASH_BYTES - STOCK_WAVES_BYTES
    print(f"  {'total':10} {fmt_bytes(grand_in):>14} -> {fmt_bytes(grand_out):>14}"
          f"   (sample flash for user content: about {fmt_bytes(avail)})")
    if grand_out > avail:
        print("  WARNING: the optimised set still does not fit; Load:[Samples] will skip what does not fit.")
    if args.dry_run:
        print("\n(dry run: nothing was written)")


if __name__ == "__main__":
    main()
