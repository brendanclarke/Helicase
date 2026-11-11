#!/usr/bin/env python3
"""List the card files whose stored wave bytes change meaning (S094 P1 S2).

What:       walks SD_CARD (or a path given as the argument) for Instrument
            files (*.drm, *.snr, *.cym, *.hat; in /Instrument and inside Kit
            folders) and reads their [params] section:
              - osc1_wave = 5 with osc1_type 0 or absent: a Cym carrier; after
                S2 it clamps to Noi (OSC_STOCK_LAST);
              - a modulator wave (Drum osc2_wave; Cymbal/HiHat osc2_wave and
                osc3_wave) = 5: the Cym modulator; after S2 it is the first
                user entry (s01) when a sample is installed;
              - a modulator wave >= 6: a user entry; after S2 every one plays
                the next entry in the list (the meaning of each index moves
                down one).
            Prints one line per affected file and key; a count at the end.
Why:        C-D3: no remap code (standing rule: no backward compatibility);
            the user re-points by hand from this list.
Inputs:     the card tree; argv[1] optional root (default SD_CARD).
Outputs:    the list on stdout; exit 0.
Accessors:  run by hand in S2, before S2.3.
Affiliates: Oscillator.h OSC_SAMPLE_START (6 -> 5), OSC_STOCK_LAST (CRASH ->
            NOISE).
"""
import os
import sys

MODULATORS = {"drm": ("osc2_wave",), "cym": ("osc2_wave", "osc3_wave"),
              "hat": ("osc2_wave", "osc3_wave"), "snr": ()}


def params(path):
    """The [params] key=value pairs of one instrument file."""
    out, on = {}, False
    for line in open(path, errors="replace"):
        line = line.strip()
        if line.startswith("["):
            on = line == "[params]"
        elif on and "=" in line:
            k, v = line.split("=", 1)
            out[k] = v
    return out


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else "SD_CARD"
    hits = 0
    for d, _, files in os.walk(root):
        for f in sorted(files):
            ext = f.rsplit(".", 1)[-1].lower()
            if ext not in MODULATORS:
                continue
            p = params(os.path.join(d, f))
            wav = p.get("osc1_wave", "")
            if wav == "5" and p.get("osc1_type", "0") == "0":
                print(f"{os.path.join(d, f)}: osc1_wave=5 (Cym carrier) -> Noi")
                hits += 1
            for k in MODULATORS[ext]:
                v = p.get(k, "")
                if v.isdigit() and int(v) >= 5:
                    what = "Cym modulator -> s01" if v == "5" else "user entry moves down one"
                    print(f"{os.path.join(d, f)}: {k}={v} ({what})")
                    hits += 1
    print(f"scan_wave_refs: {hits} reference(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
