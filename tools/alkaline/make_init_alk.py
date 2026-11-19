#!/usr/bin/env python3
"""Write and check the first Alkaline instrument, init.alk (S093 P4, F1).

What:       reads the descriptor rows of Core/DSP/Instruments/Alkaline/
            AlkalineParameters.c (one row per line, as check_special_tags.py)
            and writes SD_CARD/Instrument/Alkaline/init.alk in the firmware
            writer's exact form (storageTypes.c storage_formatInstrumentLineView):
              format=helicase.instrument / version=1 / type=alk / blank /
              [params] every key in table order / blank /
              [morph] the MORPHABLE rows only (macros ROW, ROW_SPECIAL,
              ROW_MENU, ROW_NOBIND_IMAGE carry FLAGS_IMAGE; ROW_SELECT_ONOFF
              and ROW_NOBIND do not) / blank; LF endings.
            Values (INIT): the five 303 knobs 64, Tuning 63, coarse 36 (C2),
            drive 0, Saw, slide off, vol 100, pan 63; LFO and velocity off:
            the LFO voices `self` (the writer's own-slot token), the target
            selectors 255 (INSTRUMENT_TARGET_TOKEN_OFF).
Why:        S093 P4 F1 (user): Instrument Load only lists files and a slot's
            type only changes by loading one, so the card image carries a
            first Alkaline. Every key is written: a missing key loads 0
            (instrumentManager_resetSlot()), which for coarse, tuning and vol
            would be 8 Hz, -700 cents and silence.
Inputs:     --check: the committed file must equal what this script writes,
            and every table key must have an INIT value (and no extra).
Outputs:    the file (default mode); with --check, a message and exit 1 on
            a difference.
Accessors:  python3 tools/alkaline/make_init_alk.py [--check];
            make -C tools/dsp_test special_tags (runs --check).
Affiliates: AlkalineVoice.c Alkaline_initVoice() (the same bytes);
            storage_instrumentParseLine() (the reader).
"""
import argparse
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
TABLE = os.path.join(ROOT, "Core", "DSP", "Instruments", "Alkaline", "AlkalineParameters.c")
OUT = os.path.join(ROOT, "SD_CARD", "Instrument", "Alkaline", "init.alk")
OUT_ADV = os.path.join(ROOT, "SD_CARD", "Instrument", "AlkAdv", "init.alx")
# one descriptor row: macro name, then the quoted file key
ROW = re.compile(r'^\s*(ROW[A-Z_]*)\("([^"]+)"')
# rows whose macro sets INSTRUMENT_PARAM_FLAG_MORPHABLE (FLAGS_IMAGE).
# S094 P1: ROW_MENU_SPECIAL (lfo_sync) is FLAGS_IMAGE too; without it
# the [morph] section would lose lfo_sync and init.alk would change.
MORPHABLE = {"ROW", "ROW_SPECIAL", "ROW_MENU", "ROW_MENU_SPECIAL", "ROW_NOBIND_IMAGE"}

# init bytes per key; every key of the table must be here (and only those)
INIT = {
    "osc1_wave": "0", "osc1_pitch_coarse": "36", "alk_tuning": "63", "alk_slide": "0",
    "alk_cutoff": "64", "alk_reso": "64", "alk_envmod": "64", "alk_decay": "64",
    "alk_accent": "64", "alk_drive": "0", "instrument_vol": "100", "instrument_pan": "63",
    "lfo_rate": "0", "lfo_amount": "0", "lfo_amount_2": "0", "lfo_wave": "0",
    "lfo_retrigger_voice": "0", "lfo_polarity": "0", "lfo_sync": "0", "lfo_offset": "0",
    "velo_mod_amount": "0", "velo_mod_dest": "255",
    "lfo_target_voice": "self", "lfo_target_param": "255",
    "lfo_target_voice_2": "self", "lfo_target_param_2": "255",
    # S093 P6 (appended rows): attack at a 0.70 ms trigger (today's), pos 0
    # (free-running)
    "alk_attack": "0", "osc1_position": "0",
    # S093 P8 (appended row): typ wav (Saw/Sqr); osc1_wave (row 0, was
    # alk_wave) is an image row since P8, so it is under [morph] too
    "osc1_type": "0",
    # S094 P1 S4 (appended rows): no VCA overdrive, the tone open: the
    # neutral settings (bit-identical to the S093 voice)
    "instrument_drive": "0", "alk_tone": "127",
}

# S099 P1 (P-E): the alx rows (after the @advanced-rows marker); S099 P2: fx
# chr, dpt 0 (I), rte 40 (for dpt 3..127), mix 64 (dry and wet at -3 dB); the
# cbw cdv cwd rows are gone (tone fixed, drive and width derived)
INIT_ADV = {
    "fx_type": "1", "fx_rate": "40", "fx_depth": "0",
    "fx_mix": "64",
}
MARKER = "@advanced-rows"


def rows():
    """((macro, key) of the Basic rows, (macro, key) of the alx rows) in table
    order; the alx rows follow the @advanced-rows marker (S099 P1)."""
    basic, adv, seen = [], [], False
    with open(TABLE) as fh:
        for line in fh:
            if MARKER in line:
                seen = True
                continue
            m = ROW.match(line)
            if m:
                (adv if seen else basic).append((m.group(1), m.group(2)))
    return basic, adv


def text(r, typ, init):
    """The file text, line for line as the firmware writer emits it."""
    lines = ["format=helicase.instrument", "version=1", f"type={typ}", "", "[params]"]
    lines += [f"{k}={init[k]}" for _, k in r]
    lines += ["", "[morph]"]
    lines += [f"{k}={init[k]}" for m, k in r if m in MORPHABLE]
    lines += [""]
    return "\n".join(lines) + "\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()
    basic, adv = rows()
    jobs = [(OUT, "alk", basic, INIT), (OUT_ADV, "alx", basic + adv, {**INIT, **INIT_ADV})]
    for path, typ, r, init in jobs:
        keys = [k for _, k in r]
        missing = [k for k in keys if k not in init]
        extra = [k for k in init if k not in keys]
        if missing or extra:
            sys.exit(f"make_init_alk: {len(r)} {typ} rows; no INIT value for {missing}; "
                     f"INIT keys not in the table {extra}")
        want = text(r, typ, init)
        if a.check:
            have = open(path, newline="").read() if os.path.exists(path) else None
            if have != want:
                sys.exit(f"make_init_alk: {path} differs from the table (rerun without --check)")
            print(f"make_init_alk: {os.path.basename(path)} matches the table ({len(r)} rows)")
            continue
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", newline="\n") as fh:
            fh.write(want)
        print("wrote", path)


if __name__ == "__main__":
    main()
