#!/usr/bin/env python3
"""Write and check the first Tetrakt instrument, init.tkt (S096 P1).

What:       reads the descriptor rows of Core/DSP/Instruments/Tetrakt/
            TetraktParameters.c (one row per line, as check_special_tags.py)
            and writes SD_CARD/Instrument/Tetrakt/init.tkt in the firmware
            writer's exact form (storageTypes.c storage_formatInstrumentLineView):
              format=helicase.instrument / version=1 / type=tkt / blank /
              [params] every key in table order / blank /
              [morph] the MORPHABLE rows only (macros ROW, ROW_SPECIAL,
              ROW_MENU, ROW_MENU_SPECIAL, ROW_NOBIND_IMAGE carry FLAGS_IMAGE;
              ROW_MENU_SELECT, ROW_SELECT_ONOFF and ROW_NOBIND do not) /
              blank; LF endings.
            Values (INIT): Tetrakt_initVoice()'s bytes: samples 0..3, every
            tune 63 (0), level 100, decay 70, pan 63 (centre), lon A (1),
            velocity off (dest 255, amount 0) with vel on, drive 0, SRR 127
            (the full rate), space sends 0 with S1 at 40/40/30/100,
            tone/tone envelope 63 (flat), the LFO and its targets off (the
            voices 'self', the selectors 255 = INSTRUMENT_TARGET_TOKEN_OFF),
            vol 100, pan 63, width 0, space level 100.
Why:        Instrument Load only lists files, and a slot's type only changes
            by loading one (S093 P4 F1), so the card image carries a first
            Tetrakt. Every key is written: a missing key loads 0
            (instrumentManager_resetSlot()), which for tkt_srr_* would stop
            the layer and for tkt_sample_* would select nothing.
Inputs:     --check: the committed file must equal what this script writes,
            and every table key must have an INIT value (and no extra).
Outputs:    the file (default mode); with --check, a message and exit 1 on
            a difference.
Accessors:  python3 tools/tetrakt/make_init_tkt.py [--check];
            make -C tools/dsp_test special_tags (runs --check).
Affiliates: TetraktVoice.c Tetrakt_initVoice() (the same bytes);
            storage_instrumentParseLine() (the reader);
            tools/colloid/make_init_col.py (the same writer).
"""
import argparse
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
TABLE = os.path.join(ROOT, "Core", "DSP", "Instruments", "Tetrakt", "TetraktParameters.c")
OUT = os.path.join(ROOT, "SD_CARD", "Instrument", "Tetrakt", "init.tkt")
# one descriptor row: macro name, then the quoted file key
ROW = re.compile(r'^\s*(ROW[A-Z_]*)\("([^"]+)"')
# rows whose macro sets INSTRUMENT_PARAM_FLAG_MORPHABLE (FLAGS_IMAGE)
MORPHABLE = {"ROW", "ROW_SPECIAL", "ROW_MENU", "ROW_MENU_SPECIAL", "ROW_NOBIND_IMAGE"}

# init bytes per key; every key of the table must be here (and only those).
# The same bytes as Tetrakt_initVoice() (TetraktVoice.c).
INIT = {
    "tkt_sample_a": "0", "tkt_sample_b": "1", "tkt_sample_c": "2", "tkt_sample_d": "3",
    "tkt_tune_a": "63", "tkt_tune_b": "63", "tkt_tune_c": "63", "tkt_tune_d": "63",
    "tkt_level_a": "100", "tkt_level_b": "100", "tkt_level_c": "100", "tkt_level_d": "100",
    "tkt_decay_a": "70", "tkt_decay_b": "70", "tkt_decay_c": "70", "tkt_decay_d": "70",
    "tkt_pan_a": "63", "tkt_pan_b": "63", "tkt_pan_c": "63", "tkt_pan_d": "63",
    "tkt_layer_on": "1",
    "velo_mod_dest": "255", "velo_mod_amount": "0", "velo_vol_on_off": "1",
    "tkt_drive_a": "0", "tkt_drive_b": "0", "tkt_drive_c": "0", "tkt_drive_d": "0",
    "tkt_srr_a": "127", "tkt_srr_b": "127", "tkt_srr_c": "127", "tkt_srr_d": "127",
    "tkt_space_a": "0", "tkt_space_b": "0", "tkt_space_c": "0", "tkt_space_d": "0",
    "tkt_space_time": "40", "tkt_space_feedback": "40", "tkt_space_detune": "30",
    "tkt_space_tone": "100",
    "tkt_tone_a": "63", "tkt_tone_b": "63", "tkt_tone_c": "63", "tkt_tone_d": "63",
    "tkt_tone_env_a": "63", "tkt_tone_env_b": "63", "tkt_tone_env_c": "63", "tkt_tone_env_d": "63",
    "lfo_rate": "0", "lfo_amount": "0", "lfo_amount_2": "0", "lfo_wave": "0",
    "lfo_retrigger_voice": "0", "lfo_polarity": "0", "lfo_sync": "0", "lfo_offset": "0",
    "lfo_target_voice": "self", "lfo_target_param": "255",
    "lfo_target_voice_2": "self", "lfo_target_param_2": "255",
    "instrument_vol": "100", "instrument_pan": "63", "tkt_width": "0", "tkt_space_level": "100",
}

def rows():
    """(macro, key) for every descriptor row, in table order."""
    with open(TABLE) as fh:
        return [(m.group(1), m.group(2)) for m in map(ROW.match, fh) if m]


def text(r):
    """The file text, line for line as the firmware writer emits it."""
    lines = ["format=helicase.instrument", "version=1", "type=tkt", "", "[params]"]
    lines += [f"{k}={INIT[k]}" for _, k in r]
    lines += ["", "[morph]"]
    lines += [f"{k}={INIT[k]}" for m, k in r if m in MORPHABLE]
    lines += [""]
    return "\n".join(lines) + "\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()
    r = rows()
    keys = [k for _, k in r]
    missing = [k for k in keys if k not in INIT]
    extra = [k for k in INIT if k not in keys]
    if missing or extra:
        sys.exit(f"make_init_tkt: {len(r)} rows; no INIT value for {missing}; "
                 f"INIT keys not in the table {extra}")
    want = text(r)
    if a.check:
        have = open(OUT, newline="").read() if os.path.exists(OUT) else None
        if have != want:
            sys.exit(f"make_init_tkt: {OUT} differs from the table (rerun without --check)")
        print(f"make_init_tkt: init.tkt matches the table ({len(r)} rows)")
        return
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", newline="\n") as fh:
        fh.write(want)
    print("wrote", OUT)


if __name__ == "__main__":
    main()

