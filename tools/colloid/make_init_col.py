#!/usr/bin/env python3
"""Write and check the first Colloid instrument, init.col (S095 P2; S095 P3).

What:       reads the descriptor rows of Core/DSP/Instruments/Colloid/
            ColloidParameters.c (one row per line, as check_special_tags.py)
            and writes SD_CARD/Instrument/Colloid/init.col in the firmware
            writer's exact form (storageTypes.c storage_formatInstrumentLineView):
              format=helicase.instrument / version=1 / type=col / blank /
              [params] every key in table order / blank /
              [morph] the MORPHABLE rows only (macros ROW, ROW_SPECIAL,
              ROW_MENU, ROW_MENU_SPECIAL, ROW_NOBIND_IMAGE carry FLAGS_IMAGE;
              ROW_SELECT_ONOFF, ROW_SELECT_ONOFF_SPECIAL and ROW_NOBIND do
              not) / blank; LF endings.
            Values (INIT): Colloid_initVoice()'s bytes: typ wav, Saw, coa 48
            (C3), fin 63, the amp envelope 40/70/90/70 with sus on, slw 0
            (instant), in1..in3 63 (0 semitones: every group on the root),
            dtn 20 (+-6 cents at the outer pair), spr 20 (0.79 of the
            field), mot 10 (depth 0.84, 0.07 Hz): the x5 ranges' gentle
            start (S095 P3), the filter envelope 0/70/64/70 with fam
            63 (none), drv 0, the filter LP at frq 90 / res 20 routed str
            with every offset 63 (0), vol 100, pan 63, ton 127 (open), the
            overdrive 0; LFO and velocity off: the LFO voices `self` (the
            writer's own-slot token), the target selectors 255
            (INSTRUMENT_TARGET_TOKEN_OFF). S095 P3: the rows in page order.
Why:        Instrument Load only lists files, and a slot's type only changes
            by loading one (S093 P4 F1), so the card image carries a first
            Colloid. Every key is written: a missing key loads 0
            (instrumentManager_resetSlot()), which for coa and vol would be
            8 Hz and silence.
Inputs:     --check: the committed file must equal what this script writes,
            and every table key must have an INIT value (and no extra).
Outputs:    the file (default mode); with --check, a message and exit 1 on
            a difference.
Accessors:  python3 tools/colloid/make_init_col.py [--check];
            make -C tools/dsp_test special_tags (runs --check).
Affiliates: ColloidVoice.c Colloid_initVoice() (the same bytes);
            storage_instrumentParseLine() (the reader);
            tools/alkaline/make_init_alk.py (the same writer).
"""
import argparse
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
TABLE = os.path.join(ROOT, "Core", "DSP", "Instruments", "Colloid", "ColloidParameters.c")
OUT = os.path.join(ROOT, "SD_CARD", "Instrument", "Colloid", "init.col")
# one descriptor row: macro name, then the quoted file key
ROW = re.compile(r'^\s*(ROW[A-Z_]*)\("([^"]+)"')
# rows whose macro sets INSTRUMENT_PARAM_FLAG_MORPHABLE (FLAGS_IMAGE)
MORPHABLE = {"ROW", "ROW_SPECIAL", "ROW_MENU", "ROW_MENU_SPECIAL", "ROW_NOBIND_IMAGE"}

# init bytes per key; every key of the table must be here (and only those)
INIT = {
    "osc1_pitch_coarse": "48", "osc1_pitch_fine": "63", "osc1_position": "0",
    "col_pos_spread": "0", "osc1_wave": "0", "osc1_type": "0",
    "col_attack": "40", "col_decay": "70", "col_sustain_level": "90",
    "col_release": "70", "col_sustain": "1", "col_slew": "0",
    "col_interval_1": "63", "col_interval_2": "63", "col_interval_3": "63",
    "col_detune": "20", "col_spread": "20", "col_motion": "10",
    "col_fenv_attack": "0", "col_fenv_decay": "70", "col_fenv_sustain_level": "64",
    "col_fenv_release": "70", "col_fenv_amount": "63", "col_drive": "0",
    "col_cutoff": "90", "col_reso": "20", "col_filt_type": "0", "col_route": "0",
    "col_cut_ofs": "63", "col_res_ofs": "63", "col_env_ofs": "63", "col_drive_ofs": "63",
    "instrument_vol": "100", "instrument_pan": "63", "col_tone": "127",
    "instrument_drive": "0",
    "lfo_rate": "0", "lfo_amount": "0", "lfo_amount_2": "0", "lfo_wave": "0",
    "lfo_retrigger_voice": "0", "lfo_polarity": "0", "lfo_sync": "0", "lfo_offset": "0",
    "velo_mod_amount": "0", "velo_mod_dest": "255",
    "lfo_target_voice": "self", "lfo_target_param": "255",
    "lfo_target_voice_2": "self", "lfo_target_param_2": "255",
}

def rows():
    """(macro, key) for every descriptor row, in table order."""
    with open(TABLE) as fh:
        return [(m.group(1), m.group(2)) for m in map(ROW.match, fh) if m]


def text(r):
    """The file text, line for line as the firmware writer emits it."""
    lines = ["format=helicase.instrument", "version=1", "type=col", "", "[params]"]
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
        sys.exit(f"make_init_col: {len(r)} rows; no INIT value for {missing}; "
                 f"INIT keys not in the table {extra}")
    want = text(r)
    if a.check:
        have = open(OUT, newline="").read() if os.path.exists(OUT) else None
        if have != want:
            sys.exit(f"make_init_col: {OUT} differs from the table (rerun without --check)")
        print(f"make_init_col: init.col matches the table ({len(r)} rows)")
        return
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", newline="\n") as fh:
        fh.write(want)
    print("wrote", OUT)


if __name__ == "__main__":
    main()
