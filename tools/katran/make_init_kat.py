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
            Values (INIT): Katran_initVoice()'s bytes: Osc 1 typ pwm, Rec,
            coa 36 (32'), fin 63, pos 46, dtn 0, dmp 40; Osc 2 (S105 P1 A8)
            typ pwm, wave Rec; run 127 (on), kik 40; the chaos rows (S105 P1
            A3) chs 40 cyc 37 skw 63 imd 20; the patch LFO 74/57/51, vca 63;
            Osc 2 48/64/60, ring depth 127, ring mode rng, sdb 0; exc 50,
            nse 0, sth 60; drv 56, wet 57, tim 74, fbk 61, drf 0, dtk 0; the
            band (A4) cut 26 / wid 127 / ofs 63; out drive 0; ton 63; vol
            100, pan 63, spr 63, wid 63 (S103 P5: bipolar, 0 = full spread
            with no collapse); rot 34 (the stereo rotation, index 37 since
            S105 P1); the LFO and velocity rows off (the writer's own-slot
            token, the target selectors 255).
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
    "kat_b_type": "2", "kat_damp": "40",
    "kat_run": "127", "kat_kick": "40",
    "kat_chaos_cycle": "37", "kat_chaos_skew": "63", "kat_chaos_imod": "20",
    "kat_ring_mode": "0", "kat_ring_sideband": "0", "kat_out_drive": "0",
    "kat_chaos": "40",
    "kat_plfo_rate": "74", "kat_plfo_mod": "57", "kat_plfo_level": "51",
    "kat_vca": "63",
    "kat_b_coarse": "48", "kat_b_fine": "64", "kat_b_pos": "60", "kat_ring_depth": "127",
    "kat_level_a": "50", "kat_noise": "0", "kat_sync": "60",
    "kat_drive": "56", "kat_wet": "57", "kat_time": "74", "kat_feedback": "61",
    "kat_drift": "0", "kat_track": "0",
    "kat_flt_cut": "26", "kat_flt_width": "127", "kat_flt_offset": "63",
    "kat_rotate": "34", "kat_out_cut": "63",
    "instrument_vol": "100", "instrument_pan": "63",
    "kat_spread": "63", "kat_width": "63",
    "lfo_rate": "0", "lfo_amount": "0", "lfo_amount_2": "0", "lfo_wave": "0",
    "lfo_retrigger_voice": "0", "lfo_polarity": "0", "lfo_sync": "0", "lfo_offset": "0",
    "velo_mod_amount": "0", "velo_mod_dest": "255",
    "lfo_target_voice": "self", "lfo_target_param": "255",
    "lfo_target_voice_2": "self", "lfo_target_param_2": "255",
    # S105 P1 A8: Osc 2 as today (pwm Rec)
    "kat_b_wave": "2",
}


# --- S105 P1 KT-LBL -------------------------------------------------------
MARKER = "@advanced-rows"
MENU_H = os.path.join(ROOT, "Core", "Menu", "menu.h")
LABELS_C = os.path.join(ROOT, "Core", "Menu", "MenuLabels.c")
ENUM_ROW = re.compile(r'^\s*ROW[A-Z_]*\("([^"]+)",\s*(CAT_[A-Z0-9_]+),\s*'
                      r'(LONG_[A-Z0-9_]+),\s*(SHORT_[A-Z0-9_]+)')


def _enum_names(text, name):
    m = re.search(r'enum\s+' + name + r'\s*\{(.*?)\}', text, re.S)
    body = re.sub(r'/\*.*?\*/', '', m.group(1), flags=re.S)
    body = body.split('CAT_LABEL_COUNT')[0]
    return [t.strip() for t in body.split(',') if t.strip()]


def _label_strings(text, var):
    m = re.search(r'const char ' + var + r'\[\]\[\d+\]\s*=\s*\{(.*?)\n\};',
                  text, re.S)
    body = re.sub(r'/\*.*?\*/', '', m.group(1), flags=re.S)
    return re.findall(r'"([^"]*)"', body)


def _rows_with_labels():
    out = []
    with open(TABLE) as fh:
        for line in fh:
            m = ENUM_ROW.match(line)
            if m:
                out.append((m.group(1), m.group(2), m.group(3), m.group(4)))
    return out


def _page_rows():
    text = open(TABLE).read()
    m = re.search(r'katran_menu_pages\[\]\s*=\s*\{(.*?)\n\};', text, re.S)
    pages = re.findall(r'\{\{(.*?)\}\}', m.group(1), re.S)
    enum = _enum_names(text, 'enum') if False else []
    em = re.search(r'typedef enum \{(.*?)\} kat_param_index_t;', text, re.S)
    names = [t.strip().split('=')[0].strip()
             for t in re.sub(r'/\*.*?\*/', '', em.group(1), flags=re.S).split(',')
             if t.strip()]
    names = [n for n in names if n != 'KAT_PARAM_COUNT']
    index = {n: i for i, n in enumerate(names)}
    return [[index.get(t) for t in re.findall(r'KAT_PARAM_[A-Z0-9_]+', p)] for p in pages]


def check_labels():
    """S105 P1 (KT-LBL): the Katran's labels obey §A1.1.

    What:   resolves every row's label ids to their strings (menu.h enums,
            MenuLabels.c tables, index-aligned) and the page layout
            (katran_menu_pages[]); fails on a width over the LCD fields
            (3 / 8 / 9), a short name repeated on one page, or two rows with
            the same category and long name (the long view could not tell
            them apart).
    Why:    the reorganisation's rule must not erode with later rows.
    """
    mh = open(MENU_H).read()
    lc = open(LABELS_C).read()
    cats = _label_strings(lc, 'catNames')
    longs = _label_strings(lc, 'longNames')
    shorts = _label_strings(lc, 'shortNames')
    cat_id = {n: i for i, n in enumerate(_enum_names(mh, 'catNamesEnum'))}
    long_id = {n: i for i, n in enumerate(_enum_names(mh, 'longNamesEnum'))}
    short_id = {n: i for i, n in enumerate(_enum_names(mh, 'shortNamesEnum'))}
    rows = _rows_with_labels()
    bad = []
    seen = {}
    for i, (key, cat, lng, sht) in enumerate(rows):
        c = cats[cat_id[cat]] if cat in cat_id else '?'
        l = longs[long_id[lng]] if lng in long_id else '?'
        s = shorts[short_id[sht]] if sht in short_id else '?'
        if len(s) > 3 or len(c) > 8 or len(l) > 9:
            bad.append(f'{key}: width {c!r} {l!r} {s!r}')
        if (c, l) in seen:
            bad.append(f'{key}: {c} / {l} repeats {seen[(c, l)]}')
        seen[(c, l)] = key
    for pi, page in enumerate(_page_rows()):
        used = {}
        for idx in page:
            if idx is None or idx >= len(rows):
                continue
            key, cat, lng, sht = rows[idx]
            s = shorts[short_id[sht]] if sht in short_id else '?'
            if s in used:
                bad.append(f'page {pi}: {s!r} on both {used[s]} and {key}')
            used[s] = key
    if bad:
        sys.exit('make_init_kat: KT-LBL failures:\n  ' + '\n  '.join(bad))



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
    check_labels()
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
