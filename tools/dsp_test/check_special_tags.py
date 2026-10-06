#!/usr/bin/env python3
"""Verify S073 special-writer tags in the four instrument tables.

What:       classifies each descriptor key with the pre-S073 string rules and
            compares the result with its ROW_SPECIAL/ROW_MENU_SPECIAL tag.
Why:        host proof that the flash tags preserve every old writer mapping.
Inputs:     the four parameter tables in the repository root.
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
}
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


def main():
    bad = rows = 0
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
