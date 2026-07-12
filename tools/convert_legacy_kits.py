#!/usr/bin/env python3
"""Convert legacy flat Pxxx.SND kits into Phase 2 Kit/ directories.

The legacy file is an eight-byte display name followed by bytes indexed by the
current ParameterArray ``PAR_*`` enum values. That direct indexing was verified
against the original-hardware "Slak" kit: ``PAR_COARSE1`` is payload[8],
``PAR_FILTER_DRIVE_1`` is payload[128], and ``PAR_AUDIO_OUT1`` is payload[215].

The generated text schema mirrors Core/Hardware/SD/storageTypes.c: kitset.kcg
owns the six instrument files and audio routing, while each instrument file
owns only the parameters for its voice. Morph sections are initialized from the
same legacy bytes as params so old kits sound the same before a dedicated morph
save/edit pass exists.
"""

from __future__ import annotations

import re
import shutil
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SD_ROOT = ROOT / "SD_CARD"
PARAMETER_ARRAY_H = ROOT / "Core" / "Scene" / "Preset" / "ParameterArray.h"
PARAM_RENAME_TXT = ROOT / "param_rename.txt"

# Voice slots and extensions are fixed by storageTypes.c. The short filename
# prefix is six characters so prefix+d1/s1/c1/h1 still fits FAT 8.3 names.
INSTRUMENT_FILES = [
    ("drm", "d1", 1),
    ("drm", "d2", 2),
    ("drm", "d3", 3),
    ("snr", "s1", 4),
    ("cym", "c1", 5),
    ("hat", "h1", 6),
]

INSTRUMENT_RENAME_SECTIONS = {
    1: "DRUM",
    2: "DRUM",
    3: "DRUM",
    4: "SNARE",
    5: "CYMBAL",
    6: "HIHAT",
}

INSTRUMENT_PARAM_COUNT = 64
INSTRUMENT_PARAM_INVALID = 0xFFFF
CANONICAL_TARGET_KEYS = {"velo_mod_dest", "lfo_target_param"}
LFO_TARGET_VOICE_KEYS = {"lfo_target_voice", "lfo_target_voice_2"}

# Descriptor order is the generic storage index for each instrument type.
# ParameterArray only allocates slot storage; the firmware's instrument files
# define what each index means. This Python copy exists only so legacy
# modTargets[] bytes can be converted into slot*64+descriptor_index references.
DESCRIPTOR_KEYS = {
    "DRUM": [
        "osc1_wave", "osc1_pitch_coarse", "osc1_pitch_fine",
        "osc2_wave", "osc2_pitch_coarse", "osc2_mod_amount",
        "osc2_mod_type", "filter_freq", "filter_reso", "filter_drive",
        "filter_type", "amp_envelope_attack", "amp_envelope_decay",
        "amp_envelope_slope", "pitch_envelope_decay",
        "pitch_envelope_amount", "pitch_envelope_slope", "instrument_vol",
        "instrument_pan", "instrument_drive", "instrument_decimation",
        "lfo_rate", "lfo_amount", "lfo_wave", "lfo_retrigger_voice",
        "lfo_sync", "lfo_offset", "velo_vol_on_off", "velo_mod_amount",
        "velo_mod_dest", "lfo_target_voice", "lfo_target_param",
        "transient_wave", "transient_vol", "transient_freq",
    ],
    "SNARE": [
        "osc1_wave", "osc1_pitch_coarse", "osc1_pitch_fine",
        "noise_freq", "osc1_noise_mix", "filter_freq", "filter_reso",
        "filter_drive", "filter_type", "amp_envelope_attack",
        "amp_envelope_decay", "amp_envelope_slope", "amp_attack_repeat",
        "pitch_envelope_decay", "pitch_envelope_amount",
        "pitch_envelope_slope", "instrument_vol", "instrument_pan",
        "instrument_drive", "instrument_decimation", "lfo_rate",
        "lfo_amount", "lfo_wave", "lfo_retrigger_voice", "lfo_sync",
        "lfo_offset", "velo_vol_on_off", "velo_mod_amount",
        "velo_mod_dest", "lfo_target_voice", "lfo_target_param",
        "transient_wave", "transient_vol", "transient_freq",
    ],
    "CYMBAL": [
        "osc1_wave", "osc1_pitch_coarse", "osc1_pitch_fine",
        "osc2_wave", "osc2_pitch_coarse", "osc2_mod_amount",
        "osc3_wave", "osc3_pitch_coarse", "osc3_mod_amount",
        "filter_freq", "filter_reso", "filter_drive", "filter_type",
        "amp_envelope_attack", "amp_envelope_decay",
        "amp_envelope_slope", "amp_attack_repeat", "instrument_vol",
        "instrument_pan", "instrument_drive", "instrument_decimation",
        "lfo_rate", "lfo_amount", "lfo_wave", "lfo_retrigger_voice",
        "lfo_sync", "lfo_offset", "velo_vol_on_off", "velo_mod_amount",
        "velo_mod_dest", "lfo_target_voice", "lfo_target_param",
        "transient_wave", "transient_vol", "transient_freq",
    ],
    "HIHAT": [
        "osc1_wave", "osc1_pitch_coarse", "osc1_pitch_fine",
        "osc2_wave", "osc2_pitch_coarse", "osc2_mod_amount",
        "osc3_wave", "osc3_pitch_coarse", "osc3_mod_amount",
        "filter_freq", "filter_reso", "filter_drive", "filter_type",
        "amp_envelope_attack", "amp_envelope_decay",
        "amp_envelope_decay_choke", "amp_envelope_slope", "instrument_vol",
        "instrument_pan", "instrument_drive", "instrument_decimation",
        "lfo_rate", "lfo_amount", "lfo_wave", "lfo_retrigger_voice",
        "lfo_sync", "lfo_offset", "velo_vol_on_off", "velo_mod_amount",
        "velo_mod_dest", "lfo_target_voice", "lfo_target_param",
        "transient_wave", "transient_vol", "transient_freq",
    ],
}

DEFAULT_PARAM_RENAMES = {
    "DRUM": {
        "osc_wave": "osc1_wave",
        "coarse": "osc1_pitch_coarse",
        "fine": "osc1_pitch_fine",
        "mod_wave": "osc2_wave",
        "filter_freq": "filter_freq",
        "reso": "filter_reso",
        "velo_attack": "amp_envelope_attack",
        "velo_decay": "amp_envelope_decay",
        "vol_slope": "amp_envelope_slope",
        "pitch_decay": "pitch_envelope_decay",
        "mod_amount": "pitch_envelope_amount",
        "pitch_slope": "pitch_envelope_slope",
        "fm_amount": "osc2_mod_amount",
        "fm_freq": "osc2_pitch_coarse",
        "volume": "instrument_vol",
        "pan": "instrument_pan",
        "drive": "instrument_drive",
        "voice_decimation": "instrument_decimation",
        "freq_lfo": "lfo_rate",
        "amount_lfo": "lfo_amount",
        "filter_drive": "filter_drive",
        "mix_mod": "osc2_mod_type",
        "volume_mod_on_off": "velo_vol_on_off",
        "velo_mod_amt": "velo_mod_amount",
        "vel_dest": "velo_mod_dest",
        "wave_lfo": "lfo_wave",
        "voice_lfo": "lfo_target_voice",
        "target_lfo": "lfo_target_param",
        "retrigger_lfo": "lfo_retrigger_voice",
        "sync_lfo": "lfo_sync",
        "offset_lfo": "lfo_offset",
        "filter_type": "filter_type",
        "transient_vol": "transient_vol",
        "transient_wave": "transient_wave",
        "transient_freq": "transient_freq",
    },
    "SNARE": {
        "osc_wave": "osc1_wave",
        "coarse": "osc1_pitch_coarse",
        "fine": "osc1_pitch_fine",
        "noise_freq": "noise_freq",
        "mix": "osc1_noise_mix",
        "filter_freq": "filter_freq",
        "reso": "filter_reso",
        "velo_attack": "amp_envelope_attack",
        "velo_decay": "amp_envelope_decay",
        "vol_slope": "amp_envelope_slope",
        "repeat": "amp_attack_repeat",
        "pitch_decay": "pitch_envelope_decay",
        "mod_amount": "pitch_envelope_amount",
        "pitch_slope": "pitch_envelope_slope",
        "volume": "instrument_vol",
        "pan": "instrument_pan",
        "drive": "instrument_drive",
        "voice_decimation": "instrument_decimation",
        "freq_lfo": "lfo_rate",
        "amount_lfo": "lfo_amount",
        "filter_drive": "filter_drive",
        "volume_mod_on_off": "velo_vol_on_off",
        "velo_mod_amt": "velo_mod_amount",
        "vel_dest": "velo_mod_dest",
        "wave_lfo": "lfo_wave",
        "voice_lfo": "lfo_target_voice",
        "target_lfo": "lfo_target_param",
        "retrigger_lfo": "lfo_retrigger_voice",
        "sync_lfo": "lfo_sync",
        "offset_lfo": "lfo_offset",
        "filter_type": "filter_type",
        "transient_vol": "transient_vol",
        "transient_wave": "transient_wave",
        "transient_freq": "transient_freq",
    },
    "CYMBAL": {
        "wave1": "osc1_wave",
        "coarse": "osc1_pitch_coarse",
        "fine": "osc1_pitch_fine",
        "mod_osc1_freq": "osc2_pitch_coarse",
        "mod_osc2_freq": "osc3_pitch_coarse",
        "mod_osc1_gain": "osc2_mod_amount",
        "mod_osc2_gain": "osc3_mod_amount",
        "wave2": "osc2_wave",
        "wave3": "osc3_wave",
        "filter_freq": "filter_freq",
        "reso": "filter_reso",
        "velo_attack": "amp_envelope_attack",
        "velo_decay": "amp_envelope_decay",
        "vol_slope": "amp_envelope_slope",
        "repeat": "amp_attack_repeat",
        "volume": "instrument_vol",
        "pan": "instrument_pan",
        "drive": "instrument_drive",
        "voice_decimation": "instrument_decimation",
        "freq_lfo": "lfo_rate",
        "amount_lfo": "lfo_amount",
        "filter_drive": "filter_drive",
        "volume_mod_on_off": "velo_vol_on_off",
        "velo_mod_amt": "velo_mod_amount",
        "vel_dest": "velo_mod_dest",
        "wave_lfo": "lfo_wave",
        "voice_lfo": "lfo_target_voice",
        "target_lfo": "lfo_target_param",
        "retrigger_lfo": "lfo_retrigger_voice",
        "sync_lfo": "lfo_sync",
        "offset_lfo": "lfo_offset",
        "filter_type": "filter_type",
        "transient_vol": "transient_vol",
        "transient_wave": "transient_wave",
        "transient_freq": "transient_freq",
    },
    "HIHAT": {
        "wave1": "osc1_wave",
        "coarse": "osc1_pitch_coarse",
        "fine": "osc1_pitch_fine",
        "mod_osc1_freq": "osc2_pitch_coarse",
        "mod_osc2_freq": "osc3_pitch_coarse",
        "mod_osc1_gain": "osc2_mod_amount",
        "mod_osc2_gain": "osc3_mod_amount",
        "wave2": "osc2_wave",
        "wave3": "osc3_wave",
        "filter_freq": "filter_freq",
        "reso": "filter_reso",
        "velo_attack": "amp_envelope_attack",
        "decay_closed": "amp_envelope_decay",
        "decay_open": "amp_envelope_decay_choke",
        "vol_slope": "amp_envelope_slope",
        "volume": "instrument_vol",
        "pan": "instrument_pan",
        "drive": "instrument_drive",
        "voice_decimation": "instrument_decimation",
        "freq_lfo": "lfo_rate",
        "amount_lfo": "lfo_amount",
        "filter_drive": "filter_drive",
        "volume_mod_on_off": "velo_vol_on_off",
        "velo_mod_amt": "velo_mod_amount",
        "vel_dest": "velo_mod_dest",
        "wave_lfo": "lfo_wave",
        "voice_lfo": "lfo_target_voice",
        "target_lfo": "lfo_target_param",
        "retrigger_lfo": "lfo_retrigger_voice",
        "sync_lfo": "lfo_sync",
        "offset_lfo": "lfo_offset",
        "filter_type": "filter_type",
        "transient_vol": "transient_vol",
        "transient_wave": "transient_wave",
        "transient_freq": "transient_freq",
    },
}

# Legacy modTargets[] order copied from Core/Menu/Cc2Text.c.
#
# Do not parse Cc2Text.c at conversion time: the converter output must be
# deterministic and reviewable even while firmware UI tables are being deleted.
# This list is an explicit compatibility fixture for translating old target
# indices into canonical instrument IDs.
LEGACY_MOD_TARGET_PARAMS = [
    "PAR_NONE",
    "PAR_VOICE_DECIMATION_ALL",
    "PAR_COARSE1",
    "PAR_FINE1",
    "PAR_OSC_WAVE_DRUM1",
    "PAR_VELOA1",
    "PAR_VELOD1",
    "PAR_VOL_SLOPE1",
    "PAR_MOD_EG1",
    "PAR_PITCH_SLOPE1",
    "PAR_MODAMNT1",
    "PAR_VEL_DEST_1",
    "PAR_VELO_MOD_AMT_1",
    "PAR_VOLUME_MOD_ON_OFF1",
    "PAR_FMAMNT1",
    "PAR_FM_FREQ1",
    "PAR_MOD_WAVE_DRUM1",
    "PAR_MIX_MOD_1",
    "PAR_TRANS1_WAVE",
    "PAR_TRANS1_VOL",
    "PAR_TRANS1_FREQ",
    "PAR_FILTER_FREQ_1",
    "PAR_RESO_1",
    "PAR_FILTER_TYPE_1",
    "PAR_FILTER_DRIVE_1",
    "PAR_FREQ_LFO1",
    "PAR_SYNC_LFO1",
    "PAR_AMOUNT_LFO1",
    "PAR_WAVE_LFO1",
    "PAR_RETRIGGER_LFO1",
    "PAR_OFFSET_LFO1",
    "PAR_VOL1",
    "PAR_PAN1",
    "PAR_VOICE_DECIMATION1",
    "PAR_DRIVE1",
    "PAR_MIDI_NOTE1",
    "PAR_COARSE2",
    "PAR_FINE2",
    "PAR_OSC_WAVE_DRUM2",
    "PAR_VELOA2",
    "PAR_VELOD2",
    "PAR_VOL_SLOPE2",
    "PAR_MOD_EG2",
    "PAR_PITCH_SLOPE2",
    "PAR_MODAMNT2",
    "PAR_VEL_DEST_2",
    "PAR_VELO_MOD_AMT_2",
    "PAR_VOLUME_MOD_ON_OFF2",
    "PAR_FMAMNT2",
    "PAR_FM_FREQ2",
    "PAR_MOD_WAVE_DRUM2",
    "PAR_MIX_MOD_2",
    "PAR_TRANS2_WAVE",
    "PAR_TRANS2_VOL",
    "PAR_TRANS2_FREQ",
    "PAR_FILTER_FREQ_2",
    "PAR_RESO_2",
    "PAR_FILTER_TYPE_2",
    "PAR_FILTER_DRIVE_2",
    "PAR_FREQ_LFO2",
    "PAR_SYNC_LFO2",
    "PAR_AMOUNT_LFO2",
    "PAR_WAVE_LFO2",
    "PAR_RETRIGGER_LFO2",
    "PAR_OFFSET_LFO2",
    "PAR_VOL2",
    "PAR_PAN2",
    "PAR_VOICE_DECIMATION2",
    "PAR_DRIVE2",
    "PAR_MIDI_NOTE2",
    "PAR_COARSE3",
    "PAR_FINE3",
    "PAR_OSC_WAVE_DRUM3",
    "PAR_VELOA3",
    "PAR_VELOD3",
    "PAR_VOL_SLOPE3",
    "PAR_MOD_EG3",
    "PAR_PITCH_SLOPE3",
    "PAR_MODAMNT3",
    "PAR_VEL_DEST_3",
    "PAR_VELO_MOD_AMT_3",
    "PAR_VOLUME_MOD_ON_OFF3",
    "PAR_FMAMNT3",
    "PAR_FM_FREQ3",
    "PAR_MOD_WAVE_DRUM3",
    "PAR_MIX_MOD_3",
    "PAR_TRANS3_WAVE",
    "PAR_TRANS3_VOL",
    "PAR_TRANS3_FREQ",
    "PAR_FILTER_FREQ_3",
    "PAR_RESO_3",
    "PAR_FILTER_TYPE_3",
    "PAR_FILTER_DRIVE_3",
    "PAR_FREQ_LFO3",
    "PAR_SYNC_LFO3",
    "PAR_AMOUNT_LFO3",
    "PAR_WAVE_LFO3",
    "PAR_RETRIGGER_LFO3",
    "PAR_OFFSET_LFO3",
    "PAR_VOL3",
    "PAR_PAN3",
    "PAR_VOICE_DECIMATION3",
    "PAR_DRIVE3",
    "PAR_MIDI_NOTE3",
    "PAR_COARSE4",
    "PAR_FINE4",
    "PAR_NOISE_FREQ1",
    "PAR_MIX1",
    "PAR_OSC_WAVE_SNARE",
    "PAR_VELOA4",
    "PAR_VELOD4",
    "PAR_REPEAT4",
    "PAR_VOL_SLOPE4",
    "PAR_MOD_EG4",
    "PAR_PITCH_SLOPE4",
    "PAR_MODAMNT4",
    "PAR_VEL_DEST_4",
    "PAR_VELO_MOD_AMT_4",
    "PAR_VOLUME_MOD_ON_OFF4",
    "PAR_TRANS4_WAVE",
    "PAR_TRANS4_VOL",
    "PAR_TRANS4_FREQ",
    "PAR_FILTER_FREQ_4",
    "PAR_RESO_4",
    "PAR_FILTER_TYPE_4",
    "PAR_FILTER_DRIVE_4",
    "PAR_FREQ_LFO4",
    "PAR_SYNC_LFO4",
    "PAR_AMOUNT_LFO4",
    "PAR_WAVE_LFO4",
    "PAR_RETRIGGER_LFO4",
    "PAR_OFFSET_LFO4",
    "PAR_VOL4",
    "PAR_PAN4",
    "PAR_VOICE_DECIMATION4",
    "PAR_SNARE_DISTORTION",
    "PAR_MIDI_NOTE4",
    "PAR_COARSE5",
    "PAR_FINE5",
    "PAR_WAVE1_CYM",
    "PAR_VELOA5",
    "PAR_VELOD5",
    "PAR_REPEAT5",
    "PAR_VOL_SLOPE5",
    "PAR_VEL_DEST_5",
    "PAR_VELO_MOD_AMT_5",
    "PAR_VOLUME_MOD_ON_OFF5",
    "PAR_MOD_OSC_F1_CYM",
    "PAR_MOD_OSC_F2_CYM",
    "PAR_MOD_OSC_GAIN1_CYM",
    "PAR_MOD_OSC_GAIN2_CYM",
    "PAR_WAVE2_CYM",
    "PAR_WAVE3_CYM",
    "PAR_TRANS5_WAVE",
    "PAR_TRANS5_VOL",
    "PAR_TRANS5_FREQ",
    "PAR_FILTER_FREQ_5",
    "PAR_RESO_5",
    "PAR_FILTER_TYPE_5",
    "PAR_FILTER_DRIVE_5",
    "PAR_FREQ_LFO5",
    "PAR_SYNC_LFO5",
    "PAR_AMOUNT_LFO5",
    "PAR_WAVE_LFO5",
    "PAR_RETRIGGER_LFO5",
    "PAR_OFFSET_LFO5",
    "PAR_VOL5",
    "PAR_PAN5",
    "PAR_VOICE_DECIMATION5",
    "PAR_CYMBAL_DISTORTION",
    "PAR_MIDI_NOTE5",
    "PAR_COARSE6",
    "PAR_FINE6",
    "PAR_WAVE1_HH",
    "PAR_VELOA6",
    "PAR_VELOD6_CLOSED",
    "PAR_VELOD6_OPEN",
    "PAR_VOL_SLOPE6",
    "PAR_VEL_DEST_6",
    "PAR_VELO_MOD_AMT_6",
    "PAR_VOLUME_MOD_ON_OFF6",
    "PAR_MOD_OSC_F1",
    "PAR_MOD_OSC_F2",
    "PAR_MOD_OSC_GAIN1",
    "PAR_MOD_OSC_GAIN2",
    "PAR_WAVE2_HH",
    "PAR_WAVE3_HH",
    "PAR_TRANS6_WAVE",
    "PAR_TRANS6_VOL",
    "PAR_TRANS6_FREQ",
    "PAR_FILTER_FREQ_6",
    "PAR_RESO_6",
    "PAR_FILTER_TYPE_6",
    "PAR_FILTER_DRIVE_6",
    "PAR_FREQ_LFO6",
    "PAR_SYNC_LFO6",
    "PAR_AMOUNT_LFO6",
    "PAR_WAVE_LFO6",
    "PAR_RETRIGGER_LFO6",
    "PAR_OFFSET_LFO6",
    "PAR_VOL6",
    "PAR_PAN6",
    "PAR_VOICE_DECIMATION6",
    "PAR_HAT_DISTORTION",
    "PAR_MIDI_NOTE6",
]

# This is the Python-side copy of storageTypes.c's key-to-ParameterArray maps.
# Keeping the PAR_* names instead of numeric offsets lets the converter check
# the live enum values before reading bytes from legacy .SND files.
INSTRUMENT_PARAMS = {
    1: [
        ("osc_wave", "PAR_OSC_WAVE_DRUM1"),
        ("coarse", "PAR_COARSE1"),
        ("fine", "PAR_FINE1"),
        ("mod_wave", "PAR_MOD_WAVE_DRUM1"),
        ("filter_freq", "PAR_FILTER_FREQ_1"),
        ("reso", "PAR_RESO_1"),
        ("velo_attack", "PAR_VELOA1"),
        ("velo_decay", "PAR_VELOD1"),
        ("vol_slope", "PAR_VOL_SLOPE1"),
        ("pitch_decay", "PAR_MOD_EG1"),
        ("mod_amount", "PAR_MODAMNT1"),
        ("pitch_slope", "PAR_PITCH_SLOPE1"),
        ("fm_amount", "PAR_FMAMNT1"),
        ("fm_freq", "PAR_FM_FREQ1"),
        ("volume", "PAR_VOL1"),
        ("pan", "PAR_PAN1"),
        ("drive", "PAR_DRIVE1"),
        ("voice_decimation", "PAR_VOICE_DECIMATION1"),
        ("freq_lfo", "PAR_FREQ_LFO1"),
        ("amount_lfo", "PAR_AMOUNT_LFO1"),
        ("filter_drive", "PAR_FILTER_DRIVE_1"),
        ("mix_mod", "PAR_MIX_MOD_1"),
        ("volume_mod_on_off", "PAR_VOLUME_MOD_ON_OFF1"),
        ("velo_mod_amt", "PAR_VELO_MOD_AMT_1"),
        ("vel_dest", "PAR_VEL_DEST_1"),
        ("wave_lfo", "PAR_WAVE_LFO1"),
        ("voice_lfo", "PAR_VOICE_LFO1"),
        ("target_lfo", "PAR_TARGET_LFO1"),
        ("retrigger_lfo", "PAR_RETRIGGER_LFO1"),
        ("sync_lfo", "PAR_SYNC_LFO1"),
        ("offset_lfo", "PAR_OFFSET_LFO1"),
        ("filter_type", "PAR_FILTER_TYPE_1"),
        ("transient_vol", "PAR_TRANS1_VOL"),
        ("transient_wave", "PAR_TRANS1_WAVE"),
        ("transient_freq", "PAR_TRANS1_FREQ"),
    ],
    2: [
        ("osc_wave", "PAR_OSC_WAVE_DRUM2"),
        ("coarse", "PAR_COARSE2"),
        ("fine", "PAR_FINE2"),
        ("mod_wave", "PAR_MOD_WAVE_DRUM2"),
        ("filter_freq", "PAR_FILTER_FREQ_2"),
        ("reso", "PAR_RESO_2"),
        ("velo_attack", "PAR_VELOA2"),
        ("velo_decay", "PAR_VELOD2"),
        ("vol_slope", "PAR_VOL_SLOPE2"),
        ("pitch_decay", "PAR_MOD_EG2"),
        ("mod_amount", "PAR_MODAMNT2"),
        ("pitch_slope", "PAR_PITCH_SLOPE2"),
        ("fm_amount", "PAR_FMAMNT2"),
        ("fm_freq", "PAR_FM_FREQ2"),
        ("volume", "PAR_VOL2"),
        ("pan", "PAR_PAN2"),
        ("drive", "PAR_DRIVE2"),
        ("voice_decimation", "PAR_VOICE_DECIMATION2"),
        ("freq_lfo", "PAR_FREQ_LFO2"),
        ("amount_lfo", "PAR_AMOUNT_LFO2"),
        ("filter_drive", "PAR_FILTER_DRIVE_2"),
        ("mix_mod", "PAR_MIX_MOD_2"),
        ("volume_mod_on_off", "PAR_VOLUME_MOD_ON_OFF2"),
        ("velo_mod_amt", "PAR_VELO_MOD_AMT_2"),
        ("vel_dest", "PAR_VEL_DEST_2"),
        ("wave_lfo", "PAR_WAVE_LFO2"),
        ("voice_lfo", "PAR_VOICE_LFO2"),
        ("target_lfo", "PAR_TARGET_LFO2"),
        ("retrigger_lfo", "PAR_RETRIGGER_LFO2"),
        ("sync_lfo", "PAR_SYNC_LFO2"),
        ("offset_lfo", "PAR_OFFSET_LFO2"),
        ("filter_type", "PAR_FILTER_TYPE_2"),
        ("transient_vol", "PAR_TRANS2_VOL"),
        ("transient_wave", "PAR_TRANS2_WAVE"),
        ("transient_freq", "PAR_TRANS2_FREQ"),
    ],
    3: [
        ("osc_wave", "PAR_OSC_WAVE_DRUM3"),
        ("coarse", "PAR_COARSE3"),
        ("fine", "PAR_FINE3"),
        ("mod_wave", "PAR_MOD_WAVE_DRUM3"),
        ("filter_freq", "PAR_FILTER_FREQ_3"),
        ("reso", "PAR_RESO_3"),
        ("velo_attack", "PAR_VELOA3"),
        ("velo_decay", "PAR_VELOD3"),
        ("vol_slope", "PAR_VOL_SLOPE3"),
        ("pitch_decay", "PAR_MOD_EG3"),
        ("mod_amount", "PAR_MODAMNT3"),
        ("pitch_slope", "PAR_PITCH_SLOPE3"),
        ("fm_amount", "PAR_FMAMNT3"),
        ("fm_freq", "PAR_FM_FREQ3"),
        ("volume", "PAR_VOL3"),
        ("pan", "PAR_PAN3"),
        ("drive", "PAR_DRIVE3"),
        ("voice_decimation", "PAR_VOICE_DECIMATION3"),
        ("freq_lfo", "PAR_FREQ_LFO3"),
        ("amount_lfo", "PAR_AMOUNT_LFO3"),
        ("filter_drive", "PAR_FILTER_DRIVE_3"),
        ("mix_mod", "PAR_MIX_MOD_3"),
        ("volume_mod_on_off", "PAR_VOLUME_MOD_ON_OFF3"),
        ("velo_mod_amt", "PAR_VELO_MOD_AMT_3"),
        ("vel_dest", "PAR_VEL_DEST_3"),
        ("wave_lfo", "PAR_WAVE_LFO3"),
        ("voice_lfo", "PAR_VOICE_LFO3"),
        ("target_lfo", "PAR_TARGET_LFO3"),
        ("retrigger_lfo", "PAR_RETRIGGER_LFO3"),
        ("sync_lfo", "PAR_SYNC_LFO3"),
        ("offset_lfo", "PAR_OFFSET_LFO3"),
        ("filter_type", "PAR_FILTER_TYPE_3"),
        ("transient_vol", "PAR_TRANS3_VOL"),
        ("transient_wave", "PAR_TRANS3_WAVE"),
        ("transient_freq", "PAR_TRANS3_FREQ"),
    ],
    4: [
        ("osc_wave", "PAR_OSC_WAVE_SNARE"),
        ("coarse", "PAR_COARSE4"),
        ("fine", "PAR_FINE4"),
        ("noise_freq", "PAR_NOISE_FREQ1"),
        ("mix", "PAR_MIX1"),
        ("filter_freq", "PAR_FILTER_FREQ_4"),
        ("reso", "PAR_RESO_4"),
        ("velo_attack", "PAR_VELOA4"),
        ("velo_decay", "PAR_VELOD4"),
        ("vol_slope", "PAR_VOL_SLOPE4"),
        ("repeat", "PAR_REPEAT4"),
        ("pitch_decay", "PAR_MOD_EG4"),
        ("mod_amount", "PAR_MODAMNT4"),
        ("pitch_slope", "PAR_PITCH_SLOPE4"),
        ("volume", "PAR_VOL4"),
        ("pan", "PAR_PAN4"),
        ("drive", "PAR_SNARE_DISTORTION"),
        ("voice_decimation", "PAR_VOICE_DECIMATION4"),
        ("freq_lfo", "PAR_FREQ_LFO4"),
        ("amount_lfo", "PAR_AMOUNT_LFO4"),
        ("filter_drive", "PAR_FILTER_DRIVE_4"),
        ("volume_mod_on_off", "PAR_VOLUME_MOD_ON_OFF4"),
        ("velo_mod_amt", "PAR_VELO_MOD_AMT_4"),
        ("vel_dest", "PAR_VEL_DEST_4"),
        ("wave_lfo", "PAR_WAVE_LFO4"),
        ("voice_lfo", "PAR_VOICE_LFO4"),
        ("target_lfo", "PAR_TARGET_LFO4"),
        ("retrigger_lfo", "PAR_RETRIGGER_LFO4"),
        ("sync_lfo", "PAR_SYNC_LFO4"),
        ("offset_lfo", "PAR_OFFSET_LFO4"),
        ("filter_type", "PAR_FILTER_TYPE_4"),
        ("transient_vol", "PAR_TRANS4_VOL"),
        ("transient_wave", "PAR_TRANS4_WAVE"),
        ("transient_freq", "PAR_TRANS4_FREQ"),
    ],
    5: [
        ("wave1", "PAR_WAVE1_CYM"),
        ("coarse", "PAR_COARSE5"),
        ("fine", "PAR_FINE5"),
        ("mod_osc1_freq", "PAR_MOD_OSC_F1_CYM"),
        ("mod_osc2_freq", "PAR_MOD_OSC_F2_CYM"),
        ("mod_osc1_gain", "PAR_MOD_OSC_GAIN1_CYM"),
        ("mod_osc2_gain", "PAR_MOD_OSC_GAIN2_CYM"),
        ("wave2", "PAR_WAVE2_CYM"),
        ("wave3", "PAR_WAVE3_CYM"),
        ("filter_freq", "PAR_FILTER_FREQ_5"),
        ("reso", "PAR_RESO_5"),
        ("velo_attack", "PAR_VELOA5"),
        ("velo_decay", "PAR_VELOD5"),
        ("vol_slope", "PAR_VOL_SLOPE5"),
        ("repeat", "PAR_REPEAT5"),
        ("volume", "PAR_VOL5"),
        ("pan", "PAR_PAN5"),
        ("drive", "PAR_CYMBAL_DISTORTION"),
        ("voice_decimation", "PAR_VOICE_DECIMATION5"),
        ("freq_lfo", "PAR_FREQ_LFO5"),
        ("amount_lfo", "PAR_AMOUNT_LFO5"),
        ("filter_drive", "PAR_FILTER_DRIVE_5"),
        ("volume_mod_on_off", "PAR_VOLUME_MOD_ON_OFF5"),
        ("velo_mod_amt", "PAR_VELO_MOD_AMT_5"),
        ("vel_dest", "PAR_VEL_DEST_5"),
        ("wave_lfo", "PAR_WAVE_LFO5"),
        ("voice_lfo", "PAR_VOICE_LFO5"),
        ("target_lfo", "PAR_TARGET_LFO5"),
        ("retrigger_lfo", "PAR_RETRIGGER_LFO5"),
        ("sync_lfo", "PAR_SYNC_LFO5"),
        ("offset_lfo", "PAR_OFFSET_LFO5"),
        ("filter_type", "PAR_FILTER_TYPE_5"),
        ("transient_vol", "PAR_TRANS5_VOL"),
        ("transient_wave", "PAR_TRANS5_WAVE"),
        ("transient_freq", "PAR_TRANS5_FREQ"),
    ],
    6: [
        ("wave1", "PAR_WAVE1_HH"),
        ("coarse", "PAR_COARSE6"),
        ("fine", "PAR_FINE6"),
        ("mod_osc1_freq", "PAR_MOD_OSC_F1"),
        ("mod_osc2_freq", "PAR_MOD_OSC_F2"),
        ("mod_osc1_gain", "PAR_MOD_OSC_GAIN1"),
        ("mod_osc2_gain", "PAR_MOD_OSC_GAIN2"),
        ("wave2", "PAR_WAVE2_HH"),
        ("wave3", "PAR_WAVE3_HH"),
        ("filter_freq", "PAR_FILTER_FREQ_6"),
        ("reso", "PAR_RESO_6"),
        ("velo_attack", "PAR_VELOA6"),
        ("decay_closed", "PAR_VELOD6_CLOSED"),
        ("decay_open", "PAR_VELOD6_OPEN"),
        ("vol_slope", "PAR_VOL_SLOPE6"),
        ("volume", "PAR_VOL6"),
        ("pan", "PAR_PAN6"),
        ("drive", "PAR_HAT_DISTORTION"),
        ("voice_decimation", "PAR_VOICE_DECIMATION6"),
        ("freq_lfo", "PAR_FREQ_LFO6"),
        ("amount_lfo", "PAR_AMOUNT_LFO6"),
        ("filter_drive", "PAR_FILTER_DRIVE_6"),
        ("volume_mod_on_off", "PAR_VOLUME_MOD_ON_OFF6"),
        ("velo_mod_amt", "PAR_VELO_MOD_AMT_6"),
        ("vel_dest", "PAR_VEL_DEST_6"),
        ("wave_lfo", "PAR_WAVE_LFO6"),
        ("voice_lfo", "PAR_VOICE_LFO6"),
        ("target_lfo", "PAR_TARGET_LFO6"),
        ("retrigger_lfo", "PAR_RETRIGGER_LFO6"),
        ("sync_lfo", "PAR_SYNC_LFO6"),
        ("offset_lfo", "PAR_OFFSET_LFO6"),
        ("filter_type", "PAR_FILTER_TYPE_6"),
        ("transient_vol", "PAR_TRANS6_VOL"),
        ("transient_wave", "PAR_TRANS6_WAVE"),
        ("transient_freq", "PAR_TRANS6_FREQ"),
    ],
}


def strip_c_comments(text: str) -> str:
    """Remove comments so stale enum notes cannot affect the conversion."""
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//.*", "", text)


def parse_param_enum() -> dict[str, int]:
    """Return ParameterArray enum values keyed by PAR_* symbol."""
    text = strip_c_comments(PARAMETER_ARRAY_H.read_text(encoding="ascii"))
    match = re.search(r"enum\s+ParamEnums\s*\{(.*?)\}\s*;", text, flags=re.S)
    if not match:
        raise RuntimeError(f"could not find ParamEnums in {PARAMETER_ARRAY_H}")

    values: dict[str, int] = {}
    current = -1
    for raw_entry in match.group(1).split(","):
        entry = raw_entry.strip()
        if not entry:
            continue
        if "=" in entry:
            name, expr = [part.strip() for part in entry.split("=", 1)]
            if expr in values:
                current = values[expr]
            else:
                current = int(expr, 0)
        else:
            name = entry
            current += 1
        values[name] = current

    return values


def parse_param_renames() -> dict[str, dict[str, str]]:
    """Return per-instrument legacy-key to file-key renames."""
    if not PARAM_RENAME_TXT.exists():
        return {section: dict(rows) for section, rows in DEFAULT_PARAM_RENAMES.items()}

    sections: dict[str, dict[str, str]] = {}
    current: str | None = None

    for line_nr, raw_line in enumerate(PARAM_RENAME_TXT.read_text(encoding="ascii").splitlines(), start=1):
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        if "=" not in line:
            current = line
            if current in sections:
                raise RuntimeError(f"duplicate rename section {current!r} at {PARAM_RENAME_TXT}:{line_nr}")
            sections[current] = {}
            continue
        if current is None:
            raise RuntimeError(f"rename outside a section at {PARAM_RENAME_TXT}:{line_nr}")
        if line.count("=") != 1:
            raise RuntimeError(f"bad rename line at {PARAM_RENAME_TXT}:{line_nr}: {line!r}")
        old_key, new_key = [part.strip() for part in line.split("=", 1)]
        if not old_key or not new_key:
            raise RuntimeError(f"empty rename key at {PARAM_RENAME_TXT}:{line_nr}")
        if old_key in sections[current]:
            raise RuntimeError(f"duplicate rename key {old_key!r} in {current} at {PARAM_RENAME_TXT}:{line_nr}")
        sections[current][old_key] = new_key

    return sections


def validate_param_renames(param_renames: dict[str, dict[str, str]]) -> None:
    """Prove every instrument key has one unambiguous output name."""
    expected_sections = set(INSTRUMENT_RENAME_SECTIONS.values())
    unexpected_sections = sorted(set(param_renames) - expected_sections)
    if unexpected_sections:
        raise RuntimeError(
            f"unexpected rename sections in {PARAM_RENAME_TXT}: "
            f"{', '.join(unexpected_sections)}"
        )

    for section in sorted(expected_sections):
        renames = param_renames.get(section)
        if renames is None:
            raise RuntimeError(f"missing {section} section in {PARAM_RENAME_TXT}")

        slots = [
            slot
            for slot, rename_section in INSTRUMENT_RENAME_SECTIONS.items()
            if rename_section == section
        ]
        expected_keys = {
            key
            for slot in slots
            for key, _ in INSTRUMENT_PARAMS[slot]
        }
        missing = sorted(expected_keys - set(renames))
        unexpected = sorted(set(renames) - expected_keys)
        if missing:
            raise RuntimeError(
                f"{section} rename section is missing keys: {', '.join(missing)}"
            )
        if unexpected:
            raise RuntimeError(
                f"{section} rename section has unknown keys: "
                f"{', '.join(unexpected)}"
            )

        destination_keys = list(renames.values())
        duplicates = sorted({
            key for key in destination_keys if destination_keys.count(key) > 1
        })
        if duplicates:
            raise RuntimeError(
                f"{section} rename section has duplicate output keys: "
                f"{', '.join(duplicates)}"
            )

def build_canonical_param_ids(
    param_renames: dict[str, dict[str, str]],
) -> dict[str, int]:
    """Map legacy PAR_* names to canonical slot*64+descriptor-index IDs.

    The input side is the legacy ParameterArray symbol used by Pxxx.SND files;
    the output side is the firmware's current instrument_param_id_t value.
    Clients: legacy_target_to_canonical(), which converts old modTargets[]
    index bytes for target fields only.
    """
    canonical_by_param: dict[str, int] = {}

    for slot, param_rows in INSTRUMENT_PARAMS.items():
        section = INSTRUMENT_RENAME_SECTIONS[slot]
        renames = param_renames[section]
        descriptor_index = {
            key: index for index, key in enumerate(DESCRIPTOR_KEYS[section])
        }
        for old_key, param_name in param_rows:
            file_key = renames[old_key]
            index = descriptor_index.get(file_key)
            if index is None:
                continue
            canonical_by_param[param_name] = (
                (slot - 1) * INSTRUMENT_PARAM_COUNT + index
            )

    return canonical_by_param


def legacy_target_to_canonical(
    target_index: int,
    canonical_by_param: dict[str, int],
) -> int:
    """Translate one legacy modTargets[] index to a canonical instrument ID.

    Invalid, global, MIDI-note, and otherwise non-instrument targets become the
    firmware's INSTRUMENT_PARAM_INVALID sentinel. storageTypes.c accepts this
    uint16_t value and Preset maps it back to "no target" for the current
    compatibility UI/DSP path.
    """
    if target_index < 0 or target_index >= len(LEGACY_MOD_TARGET_PARAMS):
        return INSTRUMENT_PARAM_INVALID
    return canonical_by_param.get(
        LEGACY_MOD_TARGET_PARAMS[target_index],
        INSTRUMENT_PARAM_INVALID,
    )


def sanitize_display_name(raw: bytes, fallback: str) -> str:
    chars = []
    for byte in raw:
        if 0x20 <= byte <= 0x7E:
            chars.append(chr(byte))
        else:
            chars.append(" ")
    name = "".join(chars).strip()
    return name or fallback


def path_safe_name(name: str, fallback: str) -> str:
    cleaned = re.sub(r"[^A-Za-z0-9 _-]+", "", name).strip()
    cleaned = re.sub(r"\s+", " ", cleaned)
    return cleaned or fallback


def filename_prefix(name: str, fallback: str) -> str:
    cleaned = re.sub(r"[^A-Za-z0-9]+", "", name).lower()
    return (cleaned[:6] or fallback.lower())[:6]


def payload_value(payload: bytes, param_values: dict[str, int], param_name: str) -> int:
    """Read the legacy byte for one PAR_* value, failing on missing storage."""
    index = param_values[param_name]
    if index < 0 or index >= len(payload):
        raise ValueError(
            f"{param_name} maps to payload[{index}], but legacy file has "
            f"only {len(payload)} payload bytes"
        )
    return payload[index]


def legacy_param_symbols_available(param_values: dict[str, int]) -> bool:
    """Return true when current headers still expose legacy sound PAR_* names."""
    required = {
        param_name
        for rows in INSTRUMENT_PARAMS.values()
        for _, param_name in rows
    }
    required.update(f"PAR_AUDIO_OUT{slot}" for slot in range(1, 7))
    required.add("PAR_VELOD6_OPEN")
    return required.issubset(param_values)


def first_param_value(path: Path, keys: set[str], default: int = 0) -> int:
    for raw_line in path.read_text(encoding="ascii").splitlines():
        if "=" not in raw_line:
            continue
        key, value = [part.strip() for part in raw_line.split("=", 1)]
        if key in keys:
            try:
                return int(value, 0)
            except ValueError:
                return default
    return default


def rewrite_lfo_self_targets(path: Path, source_slot: int) -> bool:
    """Rewrite absolute self-target LFO voices in one instrument file.

    The self decision uses only the source Kit slot and the LFO voice selector.
    The paired target parameter intentionally stays numeric: firmware load
    normalization combines that local descriptor identity with the loaded
    numeric destination voice. This keeps "self" as a storage alias instead of
    inventing a new descriptor, Scene, or runtime value.
    """
    changed = False
    next_lines: list[str] = []

    for raw_line in path.read_text(encoding="ascii").splitlines():
        if "=" not in raw_line:
            next_lines.append(raw_line)
            continue

        key, value = [part.strip() for part in raw_line.split("=", 1)]
        if key in LFO_TARGET_VOICE_KEYS and value == str(source_slot):
            next_lines.append(f"{key}=self")
            changed = True
        else:
            next_lines.append(raw_line)

    if changed:
        path.write_text("\n".join(next_lines) + "\n", encoding="ascii")
    return changed


def upgrade_kitset_lfo_self_targets(kitset_path: Path) -> int:
    """Upgrade LFO self selectors for all files referenced by one kitset."""
    touched = 0
    current_slot = 0

    for raw_line in kitset_path.read_text(encoding="ascii").splitlines():
        line = raw_line.strip()
        if (line.startswith("[slot") and line.endswith("]") and
                len(line) == len("[slot1]") and line[5].isdigit()):
            current_slot = int(line[5])
            continue
        if current_slot < 1 or current_slot > len(INSTRUMENT_FILES):
            continue
        if "=" not in line:
            continue
        key, value = [part.strip() for part in line.split("=", 1)]
        if key != "file":
            continue
        if rewrite_lfo_self_targets(kitset_path.parent / value, current_slot):
            touched += 1

    return touched


def upgrade_existing_kit_tree(kit_root: Path) -> int:
    """Upgrade already-generated Kit/ files when legacy enum symbols are gone."""
    if not kit_root.exists():
        raise RuntimeError(
            f"{kit_root} is missing and current ParameterArray.h no longer "
            "contains legacy sound PAR_* symbols needed for fresh conversion"
        )

    upgraded = 0
    for hat_path in sorted(kit_root.glob("*/*.hat")):
        text = hat_path.read_text(encoding="ascii")
        next_text = (
            text.replace("amp_envelope_decay_closed", "amp_envelope_decay")
                .replace("amp_envelope_decay_open", "amp_envelope_decay_choke")
        )
        if next_text != text:
            hat_path.write_text(next_text, encoding="ascii")
            upgraded += 1

    for kitset_path in sorted(kit_root.glob("*/kitset.kcg")):
        text = kitset_path.read_text(encoding="ascii")
        if "slot6_track7_amp_envelope_decay=" in text:
            continue
        hat_files = sorted(kitset_path.parent.glob("*.hat"))
        decay = first_param_value(
            hat_files[0],
            {"amp_envelope_decay_choke", "amp_envelope_decay_open"},
            0,
        ) if hat_files else 0
        lines = text.splitlines()
        insert_at = 2 if len(lines) >= 2 and lines[1].startswith("version=") else len(lines)
        lines[insert_at:insert_at] = [
            f"slot6_track7_amp_envelope_decay={decay}",
            f"slot6_track7_morph_amp_envelope_decay={decay}",
        ]
        kitset_path.write_text("\n".join(lines) + "\n", encoding="ascii")
        upgraded += 1

    for kitset_path in sorted(kit_root.glob("*/kitset.kcg")):
        upgraded += upgrade_kitset_lfo_self_targets(kitset_path)

    return upgraded


def instrument_values(
    payload: bytes,
    param_values: dict[str, int],
    param_renames: dict[str, dict[str, str]],
    canonical_by_param: dict[str, int],
    slot: int,
) -> list[tuple[str, int | str]]:
    values = []
    section = INSTRUMENT_RENAME_SECTIONS[slot]
    renames = param_renames.get(section)
    if renames is None:
        raise RuntimeError(f"missing {section} section in {PARAM_RENAME_TXT}")

    expected_keys = {key for key, _ in INSTRUMENT_PARAMS[slot]}
    missing = sorted(expected_keys - set(renames))
    if missing:
        raise RuntimeError(f"{section} rename section is missing keys: {', '.join(missing)}")

    for key, param_name in INSTRUMENT_PARAMS[slot]:
        file_key = renames[key]
        value = payload_value(payload, param_values, param_name)
        if file_key in LFO_TARGET_VOICE_KEYS and value == slot:
            # "self" is chosen from the source slot and LFO voice selector
            # only. The paired lfo_target_param remains the converter's
            # numeric canonical target so firmware normalization can combine
            # its local descriptor with the loaded destination voice.
            value = "self"
        elif file_key in CANONICAL_TARGET_KEYS:
            value = legacy_target_to_canonical(value, canonical_by_param)
        values.append((file_key, value))
    return values


def write_instrument(
    path: Path,
    instrument_type: str,
    values: list[tuple[str, int | str]],
) -> None:
    lines = [
        "format=helicase.instrument",
        "version=1",
        f"type={instrument_type}",
        "",
        "[params]",
    ]
    lines.extend(f"{key}={value}" for key, value in values)
    lines.extend(["", "[morph]"])
    lines.extend(f"{key}={value}" for key, value in values)
    lines.append("")
    path.write_text("\n".join(lines), encoding="ascii")


def write_kitset(
    path: Path,
    files: list[tuple[str, str, int]],
    payload: bytes,
    param_values: dict[str, int],
) -> None:
    slot6_track7_decay = payload_value(payload, param_values, "PAR_VELOD6_OPEN")
    lines = [
        "format=helicase.kitset",
        "version=1",
        f"slot6_track7_amp_envelope_decay={slot6_track7_decay}",
        f"slot6_track7_morph_amp_envelope_decay={slot6_track7_decay}",
        "",
    ]

    for slot, (instrument_type, filename, _) in enumerate(files, start=1):
        audio_param = f"PAR_AUDIO_OUT{slot}"
        audio_out = payload_value(payload, param_values, audio_param)
        lines.extend([
            f"[slot{slot}]",
            f"type={instrument_type}",
            f"file={filename}",
            f"audio_out={audio_out}",
            "",
        ])

    path.write_text("\n".join(lines), encoding="ascii")


def populate_instrument_root(kit_root: Path, instrument_root: Path) -> int:
    """Copy converted kit instruments into the flat Instrument/ browser pool.

    Root Instrument filenames intentionally stay equal to their Kit member
    filenames. The firmware's Instrument browser stores an eight-character stem
    beside the staged payload, and changing the root pool to add kit-number
    prefixes would be an unrelated user-visible browser rename. Duplicate
    stems are rejected instead of silently overwriting a member.
    """
    if instrument_root.exists():
        shutil.rmtree(instrument_root)
    instrument_root.mkdir()

    copied = 0
    for source in sorted(
        path
        for path in kit_root.glob("*/*")
        if path.suffix.lower() in {".drm", ".snr", ".cym", ".hat"}
    ):
        destination = instrument_root / source.name
        if destination.exists():
            raise RuntimeError(
                f"duplicate root Instrument filename would be overwritten: "
                f"{source.name}"
            )
        shutil.copy2(source, destination)
        copied += 1
    return copied


def convert_one(
    snd_path: Path,
    kit_root: Path,
    param_values: dict[str, int],
    param_renames: dict[str, dict[str, str]],
    canonical_by_param: dict[str, int],
) -> None:
    legacy_slot = int(snd_path.stem[1:])
    data = snd_path.read_bytes()
    if len(data) < 8:
        raise ValueError(f"{snd_path} is too short for an eight-byte kit name")

    payload = data[8:]
    kit_display_name = sanitize_display_name(data[:8], f"Kit{legacy_slot:03d}")
    dir_name = f"{legacy_slot + 1:03d} {path_safe_name(kit_display_name, f'Kit{legacy_slot:03d}')}"
    kit_dir = kit_root / dir_name
    kit_dir.mkdir(parents=True, exist_ok=True)

    prefix = filename_prefix(kit_display_name, f"kit{legacy_slot:03d}")
    files = [
        (instrument_type, f"{prefix}{suffix}.{instrument_type}", slot)
        for instrument_type, suffix, slot in INSTRUMENT_FILES
    ]

    for instrument_type, filename, slot in files:
        write_instrument(
            kit_dir / filename,
            instrument_type,
            instrument_values(
                payload,
                param_values,
                param_renames,
                canonical_by_param,
                slot,
            ),
        )

    write_kitset(kit_dir / "kitset.kcg", files, payload, param_values)


def main() -> None:
    param_values = parse_param_enum()
    param_renames = parse_param_renames()
    validate_param_renames(param_renames)
    canonical_by_param = build_canonical_param_ids(param_renames)
    kit_root = SD_ROOT / "Kit"

    if not legacy_param_symbols_available(param_values):
        upgraded = upgrade_existing_kit_tree(kit_root)
        instrument_count = populate_instrument_root(kit_root, SD_ROOT / "Instrument")
        print(
            f"upgraded existing {kit_root}; touched {upgraded} files; "
            f"copied {instrument_count} instruments into {SD_ROOT / 'Instrument'}"
        )
        return

    # The generated directory is a mirror of the legacy Pxxx.SND files, so stale
    # kit folders or macOS metadata must be removed before writing fresh output.
    if kit_root.exists():
        shutil.rmtree(kit_root)
    kit_root.mkdir()

    snd_files = sorted(SD_ROOT.glob("P[0-9][0-9][0-9].SND"))
    if not snd_files:
        raise RuntimeError(f"no legacy Pxxx.SND files found in {SD_ROOT}")

    for snd_path in snd_files:
        convert_one(
            snd_path,
            kit_root,
            param_values,
            param_renames,
            canonical_by_param,
        )

    instrument_count = populate_instrument_root(kit_root, SD_ROOT / "Instrument")

    print(
        f"converted {len(snd_files)} legacy kits into {kit_root}; "
        f"copied {instrument_count} instruments into {SD_ROOT / 'Instrument'}"
    )


if __name__ == "__main__":
    main()
