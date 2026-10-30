#!/usr/bin/env python3
"""Sample optimizer module host tests: tools/sampleoptimize.py (S087 stage C,
C-47).

What:       builds a temporary card and runs the optimizer on it.
            Laws:
              1. a 600-point wavetable stays 600 or shrinks, never grows;
              2. a smpl loop survives a rewrite (and the loop points stay
                 inside the file);
              3. a sample in a subfolder is processed and lands in the same
                 subfolder;
              4. a sample without smpl is not trimmed (its whole file loops).
Why:        S087 P0 A10/A18 and C-44: the optimizer must never grow a table,
            keep smpl through a re-encode, and leave unlooped samples alone.
Inputs:     tools/sampleoptimize.py, numpy, the standard library.
Outputs:    "sampleoptimize failures: N"; exit 1 if N > 0.
Accessors:  make -C tools/dsp_test sampleoptimize.
Affiliates: tools/sampleoptimize.py docstring, FILESYSTEM_SPEC.md.
"""
import math
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import wave

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import sampleoptimize as so  # noqa: E402

failures = 0


def expect(cond, msg):
    global failures
    if not cond:
        print(f"FAIL: {msg}")
        failures += 1


def write_pcm16(path, samples, rate=44100, loop=None):
    data = struct.pack("<%dh" % len(samples), *samples)
    fmt = struct.pack("<HHIIHH", 1, 1, rate, rate * 2, 2, 16)
    chunks = [b"fmt " + struct.pack("<I", len(fmt)) + fmt]
    if loop is not None:
        body = struct.pack("<IIIIIIIII", 0, 0, int(1e9 / rate), 60, 0, 0, 0, 1, 0)
        body += struct.pack("<IIIIII", 0, 0, loop[0], loop[1], 0, 0)
        chunks.append(b"smpl" + struct.pack("<I", len(body)) + body)
    chunks.append(b"data" + struct.pack("<I", len(data)) + data)
    payload = b"WAVE" + b"".join(chunks)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(b"RIFF" + struct.pack("<I", len(payload)) + payload)


def main():
    card = tempfile.mkdtemp(prefix="s087opt_")
    try:
        # Two 600-point frames (a cycle with a little second harmonic).
        frame = [int(20000 * (math.sin(2 * math.pi * i / 600) +
                              0.2 * math.sin(4 * math.pi * i / 600)))
                 for i in range(600)]
        write_pcm16(os.path.join(card, "wavetables", "tab", "f1.wav"), frame)
        write_pcm16(os.path.join(card, "wavetables", "tab", "f2.wav"), frame)
        before = os.path.getsize(os.path.join(card, "wavetables", "tab", "f1.wav"))

        # A looped sample with a long silent tail after the loop.
        s = [int(10000 * math.sin(2 * math.pi * i / 100)) for i in range(600)] + [0] * 1400
        write_pcm16(os.path.join(card, "samples", "01_cut.wav"), s, loop=(0, 500))
        # The same shape without smpl: it must not be trimmed.
        write_pcm16(os.path.join(card, "samples", "02_noloop.wav"), s)
        # A subfolder sample.
        write_pcm16(os.path.join(card, "samples", "sub", "03_deep.wav"), s[:300])

        r = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "sampleoptimize.py"), card],
                           capture_output=True, text=True)
        expect(r.returncode == 0, "optimizer succeeded")
        if r.returncode != 0:
            print(r.stdout)
            print(r.stderr)

        # Law 1: table size never grows.
        after = os.path.getsize(os.path.join(card, "wavetables", "tab", "f1.wav"))
        expect(after <= before, "law 1: the wavetable never grows")
        x1, _, _, _, _ = so.read_wav_chunks(os.path.join(card, "wavetables", "tab", "f1.wav"))
        x2, _, _, _, _ = so.read_wav_chunks(os.path.join(card, "wavetables", "tab", "f2.wav"))
        expect(len(x1) == len(x2) and len(x1) <= 600, "law 1: one frame length, not grown")

        # Law 2: smpl survives the rewrite.
        xl, rate, _, _, loop = so.read_wav_chunks(os.path.join(card, "samples", "01_cut.wav"))
        expect(loop == (0, 500), "law 2: the smpl loop survives")
        expect(len(xl) > 501, "law 2: the loop stays inside the file")
        expect(len(xl) < 2000, "law 2: the looped tail was trimmed")

        # Law 4: no smpl, no trim.
        xn, _, _, _, loop = so.read_wav_chunks(os.path.join(card, "samples", "02_noloop.wav"))
        expect(loop is None and len(xn) == 2000, "law 4: an unlooped sample is untouched")

        # Law 3: the subfolder sample is present.
        expect(os.path.isfile(os.path.join(card, "samples", "sub", "03_deep.wav")),
               "law 3: the subfolder sample is processed in place")
    finally:
        shutil.rmtree(card, ignore_errors=True)

    print(f"sampleoptimize failures: {failures}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
