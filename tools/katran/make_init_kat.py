#!/usr/bin/env python3
"""Write and check the first Katran instrument, init.kat (S103 P1).

What:       reads the descriptor rows of Core/DSP/Instruments/Katran/
            KatranParameters.c (one row per line, as check_special_tags.py)
            and writes SD_CARD/Instrument/Katran/init.kat in the firmware
            writer's exact form (storageTypes.c storage_formatInstrumentLineView):
              format=helicase.instrument / version=1 / type=kat / blank /
              [params] every key in table order / blank /
              [morph] the MORPHABLE rows only (macros ROW, ROW_SPECIAL,
              ROW_MENU, ROW_MENU_SPECIAL, ROW_NOBIND_IMAGE carry FLAGS_IMAGE;
              ROW_SELECT_ONOFF and ROW_NOBIND do not) / blank; LF endings.
            Values (INIT): Katran_initVoice()'s bytes (plan §11): typ pwm,
            Rec, coa 36 (32'), fin 63, pos 46, dtn 0, src off, dmp 40; run
            127 (on), kik 40, CE1 90/63/0, CE2 100/63/63, chaos 20, the patch
            LFO 74/57/51, vca 63; OSC B 48/64/60/55, exc 50, nse 0, sth 60;
            drv 56, wet 57, tim 74, fbk 61, drf 0, dtk 0; F1 53/30/LP, lct 20,
            cut 63; vol 100, pan 63, spr 63, wid 64; the LFO and velocity rows
            off (the writer's own-slot token, the target selectors 255).
            The Basic table has no fx_ key (the merge's rows are reserved but
            not built, P§9); --check fails if one appears.
Why:        Instrument Load only lists files, and a slot's type only changes
            by loading one (S093 P4 F1), so the card image carries a first
            Katran. Every key is written: a missing key loads 0, which for coa
            and vol would be 8 Hz and silence.
Inputs:     --check: the committed file must equal what this script writes,
            and every table key must have an INIT value (and no extra).
Outputs:    the file (default mode); with --check, a message and exit 1 on
            a difference.
Accessors:  python3 tools/katran/make_init_kat.py [--check];
            make -C tools/dsp_test special_tags (runs --check).
Affiliates: KatranVoice.c Katran_initVoice() (the same bytes);
            storage_instrumentParseLine() (the reader);
            tools/colloid/make_init_col.py (the same writer).
"""
import argparse
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
TABLE = os.path.join(ROOT, "Core", "DSP", "Instruments", "Katran", "KatranParameters.c")
OUT = os.path.join(ROOT, "SD_CARD", "Instrument", "Katran", "init.kat")
# one descriptor row: macro name, then the quoted file key
ROW = re.compile(r'^\s*(ROW[A-Z_]*)\("([^"]+)"')
# rows whose macro sets INSTRUMENT_PARAM_FLAG_MORPHABLE (FLAGS_IMAGE)
MORPHABLE = {"ROW", "ROW_SPECIAL", "ROW_MENU", "ROW_MENU_SPECIAL", "ROW_NOBIND_IMAGE"}

# init bytes per key; every key of the table must be here (and only those)
INIT = {
    "osc1_pitch_coarse": "36", "osc1_pitch_fine": "63", "osc1_position": "46",
    "kat_spread_a": "0", "osc1_wave": "2", "osc1_type": "2",
    "kat_source": "0", "kat_damp": "40",
    "kat_run": "127", "kat_kick": "40",
    "kat_ce1_cycle": "90", "kat_ce1_skew": "63", "kat_ce1_depth": "0",
    "kat_ce2_cycle": "100", "kat_ce2_skew": "63", "kat_ce2_depth": "63",
    "kat_chaos": "20",
    "kat_plfo_rate": "74", "kat_plfo_mod": "57", "kat_plfo_level": "51",
    "kat_vca": "63",
    "kat_b_coarse": "48", "kat_b_fine": "64", "kat_b_pos": "60", "kat_b_level": "55",
    "kat_level_a": "50", "kat_noise": "0", "kat_sync": "60",
    "kat_drive": "56", "kat_wet": "57", "kat_time": "74", "kat_feedback": "61",
    "kat_drift": "0", "kat_track": "0",
    "kat_loop_cutoff": "53", "kat_loop_reso": "30", "kat_loop_type": "0",
    "kat_low_cut": "20", "kat_out_cut": "63",
    "instrument_vol": "100", "instrument_pan": "63",
    "kat_spread": "63", "kat_width": "64",
    "lfo_rate": "0", "lfo_amount": "0", "lfo_amount_2": "0", "lfo_wave": "0",
    "lfo_retrigger_voice": "0", "lfo_polarity": "0", "lfo_sync": "0", "lfo_offset": "0",
    "velo_mod_amount": "0", "velo_mod_dest": "255",
    "lfo_target_voice": "self", "lfo_target_param": "255",
    "lfo_target_voice_2": "self", "lfo_target_param_2": "255",
}
MARKER = "@advanced-rows"


def rows():
    """The Basic rows in table order; the advanced marker is not built (P§9)."""
    basic, seen = [], False
    with open(TABLE) as fh:
        for line in fh:
            if MARKER in line:
                seen = True
                continue
            m = ROW.match(line)
            if m:
                if seen:
                    sys.exit("make_init_kat: an @advanced-rows row exists "
                             "(the merge is not built, P§9)")
                basic.append((m.group(1), m.group(2)))
    return basic


def text(r, init):
    """The file text, line for line as the firmware writer emits it."""
    lines = ["format=helicase.instrument", "version=1", "type=kat", "", "[params]"]
    lines += [f"{k}={init[k]}" for _, k in r]
    lines += ["", "[morph]"]
    lines += [f"{k}={init[k]}" for m, k in r if m in MORPHABLE]
    lines += [""]
    return "\n".join(lines) + "\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()
    r = rows()
    keys = [k for _, k in r]
    bad_fx = [k for k in keys if k.startswith("fx_")]
    if bad_fx:
        sys.exit(f"make_init_kat: Basic keys must not start with fx_: {bad_fx}")
    missing = [k for k in keys if k not in INIT]
    extra = [k for k in INIT if k not in keys]
    if missing or extra:
        sys.exit(f"make_init_kat: {len(r)} rows; no INIT value for {missing}; "
                 f"INIT keys not in the table {extra}")
    want = text(r, INIT)
    if a.check:
        have = open(OUT, newline="").read() if os.path.exists(OUT) else None
        if have != want:
            sys.exit(f"make_init_kat: {OUT} differs from the table (rerun without --check)")
        print(f"make_init_kat: init.kat matches the table ({len(r)} rows)")
        return
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", newline="\n") as fh:
        fh.write(want)
    print("wrote", OUT)


if __name__ == "__main__":
    main()
