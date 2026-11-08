#!/usr/bin/env python3
"""Verify S073 special-writer tags in the instrument tables.

What:       classifies each descriptor key with the pre-S073 string rules and
            compares the result with its ROW_SPECIAL/ROW_MENU_SPECIAL tag; and
            (S093 P4) checks the instrument registry against the invariants
            the storage code relies on: rows in enum order and contiguous
            from 0 (the .hcnames header walks the type values until
            storage_instrumentTypeToText() returns NULL), 3-character type
            tokens (HCNAMES rows and AutoSave copy exactly 3 bytes), and one
            TABLES entry per registered type.
Why:        host proof that the flash tags preserve every old writer mapping.
Inputs:     the parameter tables in the repository root (five at S093 P4).
Outputs:    a row count and mismatch diagnostics; failure exits 1.
Accessors:  make -C tools/dsp_test special_tags.
Affiliates: InstrumentManager.c's diagnostic classifier.
"""
import re
import sys

TABLES = {
    'DRM': 'Core/DSP/Instruments/Drum/DrumParameters.c',
    'SNR': 'Core/DSP/Instruments/Snare/SnareParameters.c',
    'CYM': 'Core/DSP/Instruments/Cymbal/CymbalParameters.c',
    'HAT': 'Core/DSP/Instruments/HiHat/HiHatParameters.c',
    # S093 P4: the coarse row classifies as PITCH_COARSE | OSC1 (the osc1_
    # branch is type-independent here and in the firmware classifier,
    # InstrumentManager.c l. 3876); every alk_* key and the platform rows
    # classify as NONE or as their Drum twins.
    'ALK': 'Core/DSP/Instruments/Alkaline/AlkalineParameters.c',
}
ENUM = 'Core/DSP/Instruments/InstrumentManager.h'
REGISTRY = 'Core/DSP/Instruments/InstrumentManager.c'
ROW = re.compile(r'^\s*(ROW[A-Z_]*)\("([^"]+)"(.*)\),\s*$')


def expected(t, key):
    osc = None
    if key.startswith('osc1_'):
        osc = 'IM_SPECIAL_OSC1'
    elif key.startswith('osc2_') and t in ('DRM', 'CYM', 'HAT'):
        osc = 'IM_SPECIAL_OSC2'
    elif key.startswith('osc3_') and t in ('CYM', 'HAT'):
        osc = 'IM_SPECIAL_OSC3'
    elif key.startswith('noise_') and t == 'SNR':
        osc = 'IM_SPECIAL_OSC_NOISE'
    if osc:
        if key == 'noise_freq':
            return {'IM_SPECIAL_NOISE_FREQ', osc}
        # S087 B: osc1_type has its own writer (osc_setType()).
        if key == 'osc1_type':
            return {'IM_SPECIAL_OSC_TYPE', osc}
        # S088 P0: osc1_run stores the clamped run mode (rst/fre/itp).
        if key == 'osc1_run':
            return {'IM_SPECIAL_OSC_RUN', osc}
        if 'pitch_coarse' in key:
            return {'IM_SPECIAL_PITCH_COARSE', osc}
        if 'pitch_fine' in key:
            return {'IM_SPECIAL_PITCH_FINE', osc}
    simple = {
        'filter_freq': 'FILTER_FREQ', 'filter_reso': 'FILTER_RESO',
        'filter_drive': 'FILTER_DRIVE', 'filter_type': 'FILTER_TYPE',
        'amp_envelope_attack': 'AMP_ATTACK',
        'amp_envelope_decay': 'AMP_DECAY', 'amp_envelope_slope': 'AMP_SLOPE',
        'transient_wave': 'TRANSIENT_WAVE',
        'transient_freq': 'TRANSIENT_FREQ',
        'instrument_drive': 'INSTRUMENT_DRIVE', 'lfo_rate': 'LFO_RATE',
        # S089 P2: the voice chain selector has its own clamping writer.
        'voice_chain': 'VOICE_CHAIN',
        # S076 P2: lfo_offset now scales its byte through IM_SPECIAL_LFO_OFFSET.
        'lfo_offset': 'LFO_OFFSET'}
    if t == 'HAT' and key == 'amp_envelope_decay_choke':
        return {'IM_SPECIAL_HAT_DECAY_CHOKE'}
    if t in ('DRM', 'SNR'):
        simple.update({'pitch_envelope_decay': 'PITCH_EG_DECAY',
                       'pitch_envelope_slope': 'PITCH_EG_SLOPE',
                       'pitch_envelope_amount': 'PITCH_EG_AMOUNT'})
    if key in simple:
        return {'IM_SPECIAL_' + simple[key]}
    return set()


def actual(macro, rest):
    if not macro.endswith('_SPECIAL'):
        return set()
    tag = rest.rsplit(',', 1)[1].rstrip(' )')
    return {part.strip() for part in tag.split('|')}


def registry_check():
    """S093 P4 (F2): the registry invariants a further type must keep.

    What:   reads instrument_type_t (InstrumentManager.h) and the registry
            rows ([INSTRUMENT_TYPE_X] = { INSTRUMENT_TYPE_X, "tok", ...) of
            InstrumentManager.c. Fails when the rows are not in enum order,
            a designator disagrees with its row's type, a token is not 3
            lowercase characters, the row count differs from the real types
            (UNKNOWN excluded), or a registered type has no TABLES entry.
    Returns the number of problems (each printed).
    """
    enum = re.search(r'typedef enum \{(.*?)\} instrument_type_t;',
                     open(ENUM).read(), re.S).group(1)
    types = [t for t in re.findall(r'INSTRUMENT_TYPE_(\w+)', enum) if t != 'UNKNOWN']
    rows = re.findall(r'\[INSTRUMENT_TYPE_(\w+)\]\s*=\s*\{\s*INSTRUMENT_TYPE_(\w+),\s*"([^"]*)"',
                      open(REGISTRY).read())
    bad = 0
    if [r[0] for r in rows] != types:
        print(f'registry: rows {[r[0] for r in rows]} differ from enum {types}')
        bad += 1
    for desig, typ, tok in rows:
        if desig != typ:
            print(f'registry: [{desig}] holds INSTRUMENT_TYPE_{typ}')
            bad += 1
        if not re.fullmatch(r'[a-z0-9]{3}', tok):
            print(f'registry: {typ} token "{tok}" is not 3 lowercase characters')
            bad += 1
        if typ not in TABLES:
            print(f'registry: {typ} has no TABLES entry in check_special_tags.py')
            bad += 1
    return bad


def main():
    bad = registry_check()
    rows = 0
    for t, path in TABLES.items():
        with open(path) as source:
            for n, line in enumerate(source, 1):
                m = ROW.match(line)
                if not m:
                    continue
                rows += 1
                want = expected(t, m.group(2))
                got = actual(m.group(1), m.group(3))
                if want != got:
                    bad += 1
                    print(f'{path}:{n}: {m.group(2)} expected {sorted(want)} got {sorted(got)}')
    print(f'special tags {"OK" if not bad else "FAILED"} ({rows} rows, {bad} mismatches)')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
