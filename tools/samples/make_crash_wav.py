#!/usr/bin/env python3
"""Write the crash wave as a card sample (S094 P1 S2, decision C-D2).

What:       reads crashSample[32769] from Core/DSPAudio/Samples.c (unsigned
            8-bit: 32,768 points and a guard point equal to point 0) and
            writes SD_CARD/samples/02_crash.wav:
              - PCM, 16-bit, mono, 44,100 Hz in the header (the user-sample
                pitch law is f x 2^22 / REAL_FS: 1,024 points per nominal
                period, the same as the ROM wave's <<17 law, so the header
                rate is not used for pitch);
              - 32,769 frames, point k = (u_k - 127) x 256 exactly
                (u in 15..254 gives -28,416..+32,512: no clamp);
              - a smpl chunk with one forward loop, dwStart 0, dwEnd 32,767
                (inclusive; the installer stores loop_end = dwEnd + 1,
                filesystem.c filesystem_parseWav()), so the 32,769th frame is
                the guard the reader interpolates into at the seam.
Why:        the crash wave leaves the firmware (C-D1: -3 per sample on Drum
            and Snare, -6 on Cymbal/HiHat, 32,769 B of flash, ~540 B ITCM).
            The same points come back as a user sample, playable as slp (a
            carrier typ) or as an l entry (a modulator).
            Provenance (Samples.c header): freesound, X-Act heavy metal kit,
            Sabian AAX Studio Crash 16\", CC0 1.0 (public domain); cut, EQ'd and
            compressed by Julian Schmidt; originally 8-bit, 32 kHz.
Checks:     K1 every frame equals (u - 127) x 256 of the array (read back);
            K2 frame 32,768 equals frame 0; K3 the smpl loop reads back
            0..32,767. Exit 1 on any failure.
Inputs:     Core/DSPAudio/Samples.c (must still exist: run before S2.3).
Outputs:    SD_CARD/samples/02_crash.wav.
Accessors:  run once by hand in S2; the file is kept in the tree afterwards.
Affiliates: SampleInstall.c sampleInstall_finishSample() (the loop rule);
            S094_P0_SAMPLE_VOICE_REDUCTIONS.md §2.3.
"""
import re
import struct
import sys

SRC = "Core/DSPAudio/Samples.c"
OUT = "SD_CARD/samples/02_crash.wav"
POINTS = 32768


def read_rom():
    """The 32,769 unsigned bytes of crashSample[], in order."""
    text = open(SRC).read()
    body = text[text.index("crashSample[32769]"):]
    body = body[body.index("{") + 1:body.index("};")]
    body = re.sub(r"/\*.*?\*/", "", body, flags=re.S)
    vals = [int(x, 16) for x in re.findall(r"0x[0-9A-Fa-f]{2}", body)]
    if len(vals) != POINTS + 1:
        sys.exit(f"make_crash_wav: {len(vals)} points, expected {POINTS + 1}")
    return vals


def wav_bytes(frames):
    """RIFF/WAVE with fmt, data and smpl (one forward loop 0..POINTS-1)."""
    data = struct.pack("<%dh" % len(frames), *frames)
    fmt = struct.pack("<HHIIHH", 1, 1, 44100, 44100 * 2, 2, 16)
    # smpl: manufacturer, product, period (ns at 44.1 kHz), unity note 60,
    # pitch fraction, SMPTE format/offset, one loop, no sampler data; then
    # the loop: id 0, type 0 (forward), start, end (inclusive), fraction, count
    smpl = struct.pack("<IIIIIIIII", 0, 0, 22676, 60, 0, 0, 0, 1, 0)
    smpl += struct.pack("<IIIIII", 0, 0, 0, POINTS - 1, 0, 0)
    chunks = (b"fmt " + struct.pack("<I", len(fmt)) + fmt +
              b"data" + struct.pack("<I", len(data)) + data +
              b"smpl" + struct.pack("<I", len(smpl)) + smpl)
    return b"RIFF" + struct.pack("<I", 4 + len(chunks)) + b"WAVE" + chunks


def main():
    rom = read_rom()
    frames = [(u - 127) * 256 for u in rom]
    blob = wav_bytes(frames)
    with open(OUT, "wb") as fh:
        fh.write(blob)
    # read back: K1..K3
    back = open(OUT, "rb").read()
    d = back.index(b"data") + 8
    got = list(struct.unpack("<%dh" % len(frames), back[d:d + 2 * len(frames)]))
    s = back.index(b"smpl") + 8 + 36
    start, end = struct.unpack("<II", back[s + 8:s + 16])
    ok = got == frames and got[POINTS] == got[0] and (start, end) == (0, POINTS - 1)
    print("crash wav", "OK" if ok else "FAILED", OUT)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
