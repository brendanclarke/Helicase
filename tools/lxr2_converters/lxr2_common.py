#!/usr/bin/env python3
"""Shared readers and writers for the LXR-02 -> Helicase converters.

LXR-02 side (Erica Synths LXR-02 desktop firmware,
https://github.com/erica-synths/lxr-desktop-app, commit a43cd4f, 2026-10-23):

* ``Core/Inc/Parameters.h`` defines ``enum ParamEnums``. A kit stores one byte
  per enum value from 0 up to ``END_OF_SOUND_PARAMETERS`` (247). The enum body
  is copied into ``LXR2_PARAM_ENUM_SOURCE`` and evaluated with the C rules
  (``evaluate_c_enum``). Offsets 0..220 (every sound parameter Helicase has)
  are identical to the LXR1 enum: both firmwares descend from LXR 0.37. The
  LXR-02 then moved the seven track MIDI notes to the globals (221..227 are
  now unused) and appended six PWM amounts, the FX block and a morph-kit byte.
* ``Core/Src/Preset/presetManager.c`` reads and writes a *project*: a folder
  ``/projNN`` (NN = 00..63) holding

  - ``prj.nfo``       the 8-byte project name;
  - ``glo.cfg``       one byte per global parameter from ``PAR_BPM`` (272) to
                      ``PAR_PATTERN_MODE`` (301): 30 bytes;
  - ``NN-NAME.snd``   kit slot NN: 8-byte name + 247 sound bytes
                      (``preset_saveDrumsetSD``; short files are zero-filled
                      on load);
  - ``NN-NAME.pat``   pattern slot NN: 8-byte name, the tag ``PatV03``, then
                      one pattern (``preset_writePatternData``, see
                      ``read_pattern_payload``);
  - ``NN-NAME.sng``   song slot NN: 8-byte name + 64 pattern numbers
                      (64 = end, 65 = loop).

  ``NAME`` is the first five characters of the slot name; the loader finds a
  slot by ``NN-*`` and falls back to the older ``pNNN.ext`` names. The card
  folder ``/tmp`` holds unsaved edits of the current project in the same
  ``NN-*`` names, plus ``tmp.snd`` / ``tmp.pat`` (the edit buffers: one slot
  byte, then the kit or pattern data without a name).
* ``Core/Src/menu/Cc2Text.c`` ``modTargets[]`` (229 entries) decodes the kit
  velocity and LFO target bytes; ``Core/Src/Soundengine/Sequencer/
  sequencer.c`` defines how a pattern plays (64 steps per track, flam and
  shift per step, probability in percent, song advance at the end of the
  longest track).

Helicase side: the format-neutral Helicase writers (descriptor loader and
key-coverage check, Kit/Instrument writer, PAT4 builder and verifier,
``sceneset.scg`` writer, slot allocator, report and per-file error handling)
are imported from ``../lxr1_converters/lxr1_common.py``, so the LXR1 and
LXR-02 converters write byte-for-byte the same Helicase structures. An LXR-02
kit is converted by first translating it into the LXR1 kit layout
(``lxr1_kit_image``: same offsets, targets re-encoded as LXR1 ``modTargets``
indices) and then running the LXR1 kit pipeline on it.
"""

from __future__ import annotations

import re
import struct
import sys
from dataclasses import dataclass
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR.parent / "lxr1_converters"))

import lxr1_common as l1  # noqa: E402  (path set up above)
from lxr1_common import (  # noqa: E402,F401  (re-exported for the scripts)
    INSTRUMENT_PARAM_COUNT, INSTRUMENT_SLOT_COUNT, NUM_TRACKS, PAR_TO_SLOT_KEY,
    REPO_ROOT, Firmware, HelicasePattern, Report, SlotAllocator, build_pat4,
    check_start_slot, convert_each, evaluate_c_enum, safe_name, write_kit_dir,
    write_sceneset,
)

# ---------------------------------------------------------------------------
# LXR-02 parameter enum (Core/Inc/Parameters.h). Comments are stripped before
# evaluation; the trailing comments mark absolute values for review.
# ---------------------------------------------------------------------------
LXR2_PARAM_ENUM_SOURCE = """
PAR_NONE, PAR_MOD_WHEEL,
PAR_OSC_WAVE_DRUM1 = PAR_MOD_WHEEL, PAR_OSC_WAVE_DRUM2, PAR_OSC_WAVE_DRUM3,
PAR_OSC_WAVE_SNARE, PAR_NRPN_DATA_ENTRY_COARSE, PAR_WAVE1_CYM, PAR_WAVE1_HH,
PAR_COARSE1, PAR_FINE1, PAR_COARSE2, PAR_FINE2, PAR_COARSE3, PAR_FINE3,
PAR_COARSE4, PAR_FINE4, PAR_COARSE5, PAR_FINE5, PAR_COARSE6, PAR_FINE6,
PAR_MOD_WAVE_DRUM1, PAR_MOD_WAVE_DRUM2, PAR_MOD_WAVE_DRUM3, PAR_WAVE2_CYM,
PAR_WAVE3_CYM, PAR_WAVE2_HH, PAR_WAVE3_HH,
PAR_NOISE_FREQ1, PAR_MIX1,
PAR_MOD_OSC_F1_CYM, PAR_MOD_OSC_F2_CYM, PAR_MOD_OSC_GAIN1_CYM,
PAR_MOD_OSC_GAIN2_CYM, PAR_MOD_OSC_F1, PAR_MOD_OSC_F2, PAR_MOD_OSC_GAIN1,
PAR_MOD_OSC_GAIN2,
PAR_FILTER_FREQ_1, PAR_FILTER_FREQ_2, PAR_FILTER_FREQ_3, PAR_FILTER_FREQ_4,
PAR_FILTER_FREQ_5, PAR_FILTER_FREQ_6,
PAR_RESO_1, PAR_RESO_2, PAR_RESO_3, PAR_RESO_4, PAR_RESO_5, PAR_RESO_6,
PAR_VELOA1, PAR_VELOD1, PAR_VELOA2, PAR_VELOD2, PAR_VELOA3, PAR_VELOD3,
PAR_VELOA4, PAR_VELOD4, PAR_VELOA5, PAR_VELOD5, PAR_VELOA6,
PAR_VELOD6_CLOSED, PAR_VELOD6_OPEN,
PAR_VOL_SLOPE1, PAR_VOL_SLOPE2, PAR_VOL_SLOPE3, PAR_VOL_SLOPE4,
PAR_VOL_SLOPE5, PAR_VOL_SLOPE6,
PAR_REPEAT4, PAR_REPEAT5,
PAR_MOD_EG1, PAR_MOD_EG2, PAR_MOD_EG3, PAR_MOD_EG4,
PAR_MODAMNT1, PAR_MODAMNT2, PAR_MODAMNT3, PAR_MODAMNT4,
PAR_PITCH_SLOPE1, PAR_PITCH_SLOPE2, PAR_PITCH_SLOPE3, PAR_PITCH_SLOPE4,
PAR_FMAMNT1, PAR_FM_FREQ1, PAR_FMAMNT2, PAR_FM_FREQ2, PAR_FMAMNT3,
PAR_FM_FREQ3,
PAR_VOL1, PAR_VOL2, PAR_VOL3, PAR_VOL4, PAR_VOL5, PAR_VOL6,
PAR_PAN1, PAR_PAN2, PAR_PAN3, PAR_NRPN_FINE, PAR_NRPN_COARSE, PAR_PAN4,
PAR_PAN5, PAR_PAN6,
PAR_DRIVE1, PAR_DRIVE2, PAR_DRIVE3, PAR_SNARE_DISTORTION,
PAR_CYMBAL_DISTORTION, PAR_HAT_DISTORTION,
PAR_VOICE_DECIMATION1, PAR_VOICE_DECIMATION2, PAR_VOICE_DECIMATION3,
PAR_VOICE_DECIMATION4, PAR_VOICE_DECIMATION5, PAR_VOICE_DECIMATION6,
PAR_VOICE_DECIMATION_ALL,
PAR_FREQ_LFO1, PAR_FREQ_LFO2, PAR_FREQ_LFO3, PAR_FREQ_LFO4, PAR_FREQ_LFO5,
PAR_FREQ_LFO6,
PAR_AMOUNT_LFO1, PAR_AMOUNT_LFO2, PAR_AMOUNT_LFO3, PAR_AMOUNT_LFO4,
PAR_AMOUNT_LFO5, PAR_AMOUNT_LFO6,
PAR_BANK_CHANGE,                                   /* 127 */
PAR_FILTER_DRIVE_1, PAR_FILTER_DRIVE_2, PAR_FILTER_DRIVE_3,
PAR_FILTER_DRIVE_4, PAR_FILTER_DRIVE_5, PAR_FILTER_DRIVE_6,
PAR_MIX_MOD_1, PAR_MIX_MOD_2, PAR_MIX_MOD_3,
PAR_VOLUME_MOD_ON_OFF1, PAR_VOLUME_MOD_ON_OFF2, PAR_VOLUME_MOD_ON_OFF3,
PAR_VOLUME_MOD_ON_OFF4, PAR_VOLUME_MOD_ON_OFF5, PAR_VOLUME_MOD_ON_OFF6,
PAR_VELO_MOD_AMT_1, PAR_VELO_MOD_AMT_2, PAR_VELO_MOD_AMT_3,
PAR_VELO_MOD_AMT_4, PAR_VELO_MOD_AMT_5, PAR_VELO_MOD_AMT_6,
PAR_VEL_DEST_1, PAR_VEL_DEST_2, PAR_VEL_DEST_3, PAR_VEL_DEST_4,
PAR_VEL_DEST_5, PAR_VEL_DEST_6,
PAR_WAVE_LFO1, PAR_WAVE_LFO2, PAR_WAVE_LFO3, PAR_WAVE_LFO4, PAR_WAVE_LFO5,
PAR_WAVE_LFO6,
PAR_VOICE_LFO1, PAR_VOICE_LFO2, PAR_VOICE_LFO3, PAR_VOICE_LFO4,
PAR_VOICE_LFO5, PAR_VOICE_LFO6,
PAR_TARGET_LFO1, PAR_TARGET_LFO2, PAR_TARGET_LFO3, PAR_TARGET_LFO4,
PAR_TARGET_LFO5, PAR_TARGET_LFO6,
PAR_RETRIGGER_LFO1, PAR_RETRIGGER_LFO2, PAR_RETRIGGER_LFO3,
PAR_RETRIGGER_LFO4, PAR_RETRIGGER_LFO5, PAR_RETRIGGER_LFO6,
PAR_SYNC_LFO1, PAR_SYNC_LFO2, PAR_SYNC_LFO3, PAR_SYNC_LFO4, PAR_SYNC_LFO5,
PAR_SYNC_LFO6,
PAR_OFFSET_LFO1, PAR_OFFSET_LFO2, PAR_OFFSET_LFO3, PAR_OFFSET_LFO4,
PAR_OFFSET_LFO5, PAR_OFFSET_LFO6,
PAR_FILTER_TYPE_1, PAR_FILTER_TYPE_2, PAR_FILTER_TYPE_3, PAR_FILTER_TYPE_4,
PAR_FILTER_TYPE_5, PAR_FILTER_TYPE_6,
PAR_TRANS1_VOL, PAR_TRANS2_VOL, PAR_TRANS3_VOL, PAR_TRANS4_VOL,
PAR_TRANS5_VOL, PAR_TRANS6_VOL,
PAR_TRANS1_WAVE, PAR_TRANS2_WAVE, PAR_TRANS3_WAVE, PAR_TRANS4_WAVE,
PAR_TRANS5_WAVE, PAR_TRANS6_WAVE,
PAR_TRANS1_FREQ, PAR_TRANS2_FREQ, PAR_TRANS3_FREQ, PAR_TRANS4_FREQ,
PAR_TRANS5_FREQ, PAR_TRANS6_FREQ,
PAR_AUDIO_OUT1, PAR_AUDIO_OUT2, PAR_AUDIO_OUT3, PAR_AUDIO_OUT4,
PAR_AUDIO_OUT5, PAR_AUDIO_OUT6,                    /* 215..220 */
PAR_UNUSED_1, PAR_UNUSED_2, PAR_UNUSED_3, PAR_UNUSED_4, PAR_UNUSED_5,
PAR_UNUSED_6, PAR_UNUSED_7,                        /* old MIDI notes */
PAR_PWM_1, PAR_PWM_2, PAR_PWM_3, PAR_PWM_4, PAR_PWM_5, PAR_PWM_6,
PAR_FX_TYPE, PAR_FX_ROUTING, PAR_FX_DRY_WET,       /* 234..236 */
PAR_FX_DIST_TYPE, PAR_FX_P1, PAR_FX_P2, PAR_FX_P3, PAR_FX_P4, PAR_FX_RATIO,
PAR_FX_WAVE, PAR_FX_REVERB_TYPE, PAR_FX_DELAY_TYPE,
PAR_MORPH_KIT_DEST,                                /* 246 */
END_OF_SOUND_PARAMETERS,                           /* 247 */
PAR_ROLL = END_OF_SOUND_PARAMETERS, PAR_MORPH,
PAR_ACTIVE_STEP, PAR_STEP_VOLUME, PAR_STEP_PROB, PAR_STEP_NOTE,
PAR_EUKLID_LENGTH, PAR_EUKLID_STEPS, PAR_EUKLID_ROTATION,
PAR_AUTOM_TRACK, PAR_P1_DEST, PAR_P2_DEST, PAR_P1_VAL, PAR_P2_VAL,
PAR_SHUFFLE, PAR_UNUSED, PAR_PATTERN_NEXT, PAR_TRACK_LENGTH,
UNUSED_PAR_GLO_SAT, PAR_POS_Y, PAR_FLUX, PAR_SOM_FREQ, PAR_TRACK_ROTATION,
PAR_STEP_FLAM, PAR_STEP_SHIFT,
PAR_BEGINNING_OF_GLOBALS,
PAR_BPM = PAR_BEGINNING_OF_GLOBALS,                /* 272 */
PAR_MIDI_CHAN_1, PAR_MIDI_CHAN_2, PAR_MIDI_CHAN_3, PAR_MIDI_CHAN_4,
PAR_MIDI_CHAN_5, PAR_MIDI_CHAN_6,
PAR_FETCH, PAR_FOLLOW, PAR_QUANTISATION, PAR_SCREENSAVER_ON_OFF,
PAR_MIDI_MODE, PAR_MIDI_CHAN_7, PAR_MIDI_ROUTING, PAR_MIDI_FILT_TX,
PAR_MIDI_FILT_RX, PAR_PRESCALER_CLOCK_IN, PAR_PRESCALER_CLOCK_OUT1,
PAR_PRESCALER_CLOCK_OUT2, PAR_TRIG_GATE_MODE,
PAR_BAR_RESET_MODE, PAR_MIDI_CHAN_GLOBAL,
PAR_MIDI_NOTE1, PAR_MIDI_NOTE2, PAR_MIDI_NOTE3, PAR_MIDI_NOTE4,
PAR_MIDI_NOTE5, PAR_MIDI_NOTE6, PAR_MIDI_NOTE7,    /* 294..300 */
PAR_PATTERN_MODE,                                  /* 301 */
NUM_PARAMS
"""

LXR2_PAR = evaluate_c_enum(LXR2_PARAM_ENUM_SOURCE)
LXR2_SOUND_PARAM_COUNT = LXR2_PAR["END_OF_SOUND_PARAMETERS"]
LXR2_NAME_BY_VALUE: dict[int, str] = {}
for _name, _value in LXR2_PAR.items():
    # First name wins, so aliases (PAR_MOD_WHEEL, PAR_ROLL) never shadow the
    # sound parameter stored at the same offset.
    if _name.startswith("PAR_") and _value not in LXR2_NAME_BY_VALUE:
        LXR2_NAME_BY_VALUE[_value] = _name
LXR2_NAME_BY_VALUE[LXR2_PAR["PAR_OSC_WAVE_DRUM1"]] = "PAR_OSC_WAVE_DRUM1"

# Self-checks. Parameters.h comments are stale after PAR_AUDIO_OUT6 (its
# /*228*/ on PAR_ROLL and /*250*/ on PAR_BPM predate the PWM and FX bytes).
assert LXR2_PAR["PAR_COARSE1"] == 8
assert LXR2_PAR["PAR_FILTER_DRIVE_1"] == 128
assert LXR2_PAR["PAR_AUDIO_OUT6"] == 220
assert LXR2_PAR["PAR_PWM_1"] == 228
assert LXR2_PAR["PAR_FX_TYPE"] == 234
assert LXR2_SOUND_PARAM_COUNT == 247
assert LXR2_PAR["PAR_BPM"] == 272
assert LXR2_PAR["PAR_MIDI_NOTE1"] == 294
assert LXR2_PAR["NUM_PARAMS"] == 302

# Every LXR1 parameter the Helicase key map reads sits at the same offset in
# the LXR-02 kit, which is what lets lxr1_kit_image() copy bytes 0..220.
SHARED_SOUND_PARAM_COUNT = LXR2_PAR["PAR_AUDIO_OUT6"] + 1
for _par in PAR_TO_SLOT_KEY:
    assert LXR2_PAR[_par] == l1.LXR1_PAR[_par] < SHARED_SOUND_PARAM_COUNT, _par

# modTargets[] from Core/Src/menu/Cc2Text.c, in index order. Kit
# PAR_VEL_DEST_n / PAR_TARGET_LFOn bytes and the step automation menu are
# indices into this list. Unlike the LXR1 list it has a PWM entry per voice,
# the four shared FX parameters at the end of every voice block, and no MIDI
# note entries.
_FX_TARGETS = ("PAR_FX_P1", "PAR_FX_P2", "PAR_FX_P3", "PAR_FX_P4")
LXR2_MOD_TARGETS = ["PAR_NONE", "PAR_VOICE_DECIMATION_ALL"] + [
    name
    for v in (1, 2, 3)
    for name in (
        f"PAR_COARSE{v}", f"PAR_FINE{v}", f"PAR_OSC_WAVE_DRUM{v}", f"PAR_PWM_{v}",
        f"PAR_VELOA{v}", f"PAR_VELOD{v}", f"PAR_VOL_SLOPE{v}",
        f"PAR_MOD_EG{v}", f"PAR_PITCH_SLOPE{v}", f"PAR_MODAMNT{v}",
        f"PAR_VEL_DEST_{v}", f"PAR_VELO_MOD_AMT_{v}", f"PAR_VOLUME_MOD_ON_OFF{v}",
        f"PAR_FMAMNT{v}", f"PAR_FM_FREQ{v}", f"PAR_MOD_WAVE_DRUM{v}",
        f"PAR_MIX_MOD_{v}",
        f"PAR_TRANS{v}_WAVE", f"PAR_TRANS{v}_VOL", f"PAR_TRANS{v}_FREQ",
        f"PAR_FILTER_FREQ_{v}", f"PAR_RESO_{v}", f"PAR_FILTER_TYPE_{v}",
        f"PAR_FILTER_DRIVE_{v}",
        f"PAR_FREQ_LFO{v}", f"PAR_SYNC_LFO{v}", f"PAR_AMOUNT_LFO{v}",
        f"PAR_WAVE_LFO{v}", f"PAR_RETRIGGER_LFO{v}", f"PAR_OFFSET_LFO{v}",
        f"PAR_VOL{v}", f"PAR_PAN{v}", f"PAR_VOICE_DECIMATION{v}", f"PAR_DRIVE{v}",
    ) + _FX_TARGETS
] + [
    "PAR_COARSE4", "PAR_FINE4", "PAR_NOISE_FREQ1", "PAR_MIX1",
    "PAR_OSC_WAVE_SNARE", "PAR_PWM_4",
    "PAR_VELOA4", "PAR_VELOD4", "PAR_REPEAT4", "PAR_VOL_SLOPE4",
    "PAR_MOD_EG4", "PAR_PITCH_SLOPE4", "PAR_MODAMNT4",
    "PAR_VEL_DEST_4", "PAR_VELO_MOD_AMT_4", "PAR_VOLUME_MOD_ON_OFF4",
    "PAR_TRANS4_WAVE", "PAR_TRANS4_VOL", "PAR_TRANS4_FREQ",
    "PAR_FILTER_FREQ_4", "PAR_RESO_4", "PAR_FILTER_TYPE_4", "PAR_FILTER_DRIVE_4",
    "PAR_FREQ_LFO4", "PAR_SYNC_LFO4", "PAR_AMOUNT_LFO4", "PAR_WAVE_LFO4",
    "PAR_RETRIGGER_LFO4", "PAR_OFFSET_LFO4",
    "PAR_VOL4", "PAR_PAN4", "PAR_VOICE_DECIMATION4", "PAR_SNARE_DISTORTION",
    *_FX_TARGETS,
    "PAR_COARSE5", "PAR_FINE5", "PAR_WAVE1_CYM", "PAR_PWM_5",
    "PAR_VELOA5", "PAR_VELOD5", "PAR_REPEAT5", "PAR_VOL_SLOPE5",
    "PAR_VEL_DEST_5", "PAR_VELO_MOD_AMT_5", "PAR_VOLUME_MOD_ON_OFF5",
    "PAR_MOD_OSC_F1_CYM", "PAR_MOD_OSC_F2_CYM", "PAR_MOD_OSC_GAIN1_CYM",
    "PAR_MOD_OSC_GAIN2_CYM", "PAR_WAVE2_CYM", "PAR_WAVE3_CYM",
    "PAR_TRANS5_WAVE", "PAR_TRANS5_VOL", "PAR_TRANS5_FREQ",
    "PAR_FILTER_FREQ_5", "PAR_RESO_5", "PAR_FILTER_TYPE_5", "PAR_FILTER_DRIVE_5",
    "PAR_FREQ_LFO5", "PAR_SYNC_LFO5", "PAR_AMOUNT_LFO5", "PAR_WAVE_LFO5",
    "PAR_RETRIGGER_LFO5", "PAR_OFFSET_LFO5",
    "PAR_VOL5", "PAR_PAN5", "PAR_VOICE_DECIMATION5", "PAR_CYMBAL_DISTORTION",
    *_FX_TARGETS,
    "PAR_COARSE6", "PAR_FINE6", "PAR_WAVE1_HH", "PAR_PWM_6",
    "PAR_VELOA6", "PAR_VELOD6_CLOSED", "PAR_VELOD6_OPEN", "PAR_VOL_SLOPE6",
    "PAR_VEL_DEST_6", "PAR_VELO_MOD_AMT_6", "PAR_VOLUME_MOD_ON_OFF6",
    "PAR_MOD_OSC_F1", "PAR_MOD_OSC_F2", "PAR_MOD_OSC_GAIN1", "PAR_MOD_OSC_GAIN2",
    "PAR_WAVE2_HH", "PAR_WAVE3_HH",
    "PAR_TRANS6_WAVE", "PAR_TRANS6_VOL", "PAR_TRANS6_FREQ",
    "PAR_FILTER_FREQ_6", "PAR_RESO_6", "PAR_FILTER_TYPE_6", "PAR_FILTER_DRIVE_6",
    "PAR_FREQ_LFO6", "PAR_SYNC_LFO6", "PAR_AMOUNT_LFO6", "PAR_WAVE_LFO6",
    "PAR_RETRIGGER_LFO6", "PAR_OFFSET_LFO6",
    "PAR_VOL6", "PAR_PAN6", "PAR_VOICE_DECIMATION6", "PAR_HAT_DISTORTION",
    *_FX_TARGETS,
]
# Cc2Text.c index comments and modTargetVoiceOffsets[] (2/40/78/116/153/191,
# last 228).
assert LXR2_MOD_TARGETS[10] == "PAR_PITCH_SLOPE1"
assert LXR2_MOD_TARGETS[39] == "PAR_FX_P4"
assert LXR2_MOD_TARGETS[40] == "PAR_COARSE2"
assert LXR2_MOD_TARGETS[78] == "PAR_COARSE3"
assert LXR2_MOD_TARGETS[116] == "PAR_COARSE4"
assert LXR2_MOD_TARGETS[130] == "PAR_VELO_MOD_AMT_4"
assert LXR2_MOD_TARGETS[153] == "PAR_COARSE5"
assert LXR2_MOD_TARGETS[191] == "PAR_COARSE6"
assert LXR2_MOD_TARGETS[220] == "PAR_OFFSET_LFO6"
assert len(LXR2_MOD_TARGETS) == 229

# Re-encoding table: PAR name -> its LXR1 modTargets index (first match; the
# LXR1 list names each parameter once).
LXR1_MOD_TARGET_INDEX = {}
for _i, _name in enumerate(l1.LXR1_MOD_TARGETS):
    LXR1_MOD_TARGET_INDEX.setdefault(_name, _i)

# ---------------------------------------------------------------------------
# Value domains that differ from Helicase.
# ---------------------------------------------------------------------------
# Oscillator waveform menu (MenuText.h waveformNames): LXR-02 0..5 =
# Sin Tri Saw Rec Noi PWM; Helicase 0..5 = Sin Tri Saw Rec Noi Cym (6+ =
# samples). PWM becomes Rec, the pulse wave PWM is built from.
LXR2_WAVE_PWM = 5
HELICASE_WAVE_REC = 3
WAVEFORM_PARS_BY_SLOT = {
    1: ("PAR_OSC_WAVE_DRUM1", "PAR_MOD_WAVE_DRUM1"),
    2: ("PAR_OSC_WAVE_DRUM2", "PAR_MOD_WAVE_DRUM2"),
    3: ("PAR_OSC_WAVE_DRUM3", "PAR_MOD_WAVE_DRUM3"),
    4: ("PAR_OSC_WAVE_SNARE",),
    5: ("PAR_WAVE1_CYM", "PAR_WAVE2_CYM", "PAR_WAVE3_CYM"),
    6: ("PAR_WAVE1_HH", "PAR_WAVE2_HH", "PAR_WAVE3_HH"),
}
# Audio out menu (outputNames): 0..5 = St1 St2 L1 R1 L2 R2 in both systems;
# LXR-02 6 = the FX bus, whose output is PAR_FX_ROUTING (0..5).
LXR2_AUDIO_OUT_FX = 6
AUDIO_OUT_NAMES = ("St1", "St2", "L1", "R1", "L2", "R2")
FX_TYPE_NAMES = ("off", "drv", "rng", "cmp", "del")    # MenuText.h fxNames
SRT_NEUTRAL = 127                  # PAR_VOICE_DECIMATION_ALL full rate

# The LXR-02 plays a step note n at n - 60 semitones (SEQ_DEFAULT_NOTE,
# Oscillator.c osc_setBaseNote); Helicase plays n - 63
# (MIDI_DEFAULT_TRIGGER_NOTE), so notes move up by 3 to keep the pitch.
LXR2_DEFAULT_NOTE = 60
NOTE_OFFSET = 63 - LXR2_DEFAULT_NOTE

# Init kit (Core/Src/Preset/initKit.c): 8-byte name "Initkit" + 246 bytes.
# preset_init() copies initKit[8..246] into parameters 0..238 of every slot
# of a new project, so an untouched slot matches these 239 bytes and name.
LXR2_INIT_KIT = bytes.fromhex(
    "496e69746b69740000000000010004001f3f293f3a3f2d407f3f323f00000000"
    "0000007f3d00000000387f7f7f7f7f7f7f44680000000000000015000d000300"
    "0a006b0005112e0b040a000600001a5d070e2c0e7f51160a0e0f000000000000"
    "7f7f7f7f7f7f3f3f3f00003f3f3f3f3f3f3f3f3f7f7f7f7f7f7f7f0000000000"
    "0000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000101010101010000000000000000000000000000000000"
    "0000000000000000000000010100000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000")
INIT_KIT_COPIED_PARAMS = LXR2_SOUND_PARAM_COUNT - 8
assert len(LXR2_INIT_KIT) == 254 and LXR2_INIT_KIT[:7] == b"Initkit"

# ---------------------------------------------------------------------------
# Pattern, song and project constants (sequencer.h, presetManager.c).
# ---------------------------------------------------------------------------
NUM_SLOTS = 64                     # NUM_KIT_SLOTS = NUM_PATTERN = 64
LXR2_STEPS = 64                    # NUM_STEPS: 4 bars x 16 steps
LXR2_BARS = 4
STEP_BYTES = 7
NO_AUTOMATION = 0xFF               # MidiMessages.h
TIMING_DEFAULT = 0x70              # flam 0, shift 7 (= no shift)
PATTERN_VERSION_MAX = 3            # "PatV03"
SONG_LENGTH = 64                   # MAX_SONG_LENGTH
SONG_END = 64
SONG_LOOP = 65
GLOBALS_BYTES = LXR2_PAR["NUM_PARAMS"] - LXR2_PAR["PAR_BEGINNING_OF_GLOBALS"]
PATTERN_MODE_AUTO = 1              # patModeNames: off, Aut

# Flam (StepTiming.flam, stepFlamNames off x2 x3 x4 x6 x8) plays N evenly
# spaced hits over the 24-tick step (flamIdxToPattern()); each is exactly a
# Helicase roll rate (PATTERN_DYNAMIC_STACK.md roll table): x2 every 12
# ticks = `32` (5), x3 every 8 = `32t` (6), x4 every 6 = `64` (7), x6 every
# 4 = `64t` (8), x8 every 3 = `128` (9).
FLAM_ROLL_INDEX = {1: 5, 2: 6, 3: 7, 4: 8, 5: 9}
FLAM_SPACING_TICKS = {1: 12, 2: 8, 3: 6, 4: 4, 5: 3}
STEP_TICKS = 24
SHIFT_TICKS = 3                    # one shift unit = 1/8 step

# preset_loadDefaultGlobals() sets every track MIDI note to 48. A project
# whose seven notes are all still 48 never had them set, so the Scenes get
# Helicase's own default (`on`, no note root) instead of seven equal roots
# (Helicase lets one track own a root; tracks 2..7 would receive no notes).
LXR2_DEFAULT_TRACK_NOTE = 48

# Helicase Scene chain tokens (MenuText.h chainConditionNames/
# chainNextNames; sequencer.c seq_chainConditionWeight/ResolveNext).
CHAIN_REPEAT_CONDITIONS = {n: 2 + n for n in range(1, 9)}    # x01..x08 = 3..10
CHAIN_REPEAT_CONDITIONS.update({16: 11, 32: 12, 64: 13})
CHAIN_NEXT_ABSOLUTE = 3            # s01 = Scene slot 0
CHAIN_NEXT_STOP = 43               # stp
BANK_SCENES = 16


# ---------------------------------------------------------------------------
# Names, numbering and inputs.
# ---------------------------------------------------------------------------
SLOT_FILE_RE = re.compile(r"^(\d{2})-")
OLD_SLOT_FILE_RE = re.compile(r"^[pP](\d{3})$")
PROJECT_DIR_RE = re.compile(r"^proj(\d{2})$", re.IGNORECASE)


def lxr2_name(raw: bytes) -> str:
    """Printable 8-byte LXR-02 name; "" for blank or the firmware's "Empty".

    preset_isNameEmpty() treats "Empty" (zero padded) as no name, and the
    firmware shows unnamed slots as "Empty".
    """
    text = l1.lxr1_display_name(raw, "")
    return "" if text == "Empty" else text


def tagged_name(base: str, tag: str, limit: int = l1.DISPLAY_NAME_MAX) -> str:
    """``base`` shortened so ``"<base> <tag>"`` fits ``limit`` characters."""
    suffix = f" {tag}"
    return (base[:limit - len(suffix)].rstrip() + suffix).strip()


def is_tmp_buffer(path: Path) -> bool:
    """True for ``tmp.snd`` / ``tmp.pat``, the LXR-02 edit-buffer files."""
    return path.stem.lower() == "tmp"


def slot_number(path: Path) -> int | None:
    """Slot of ``NN-NAME.ext`` (0..63) or old ``pNNN.ext``; else None."""
    match = SLOT_FILE_RE.match(path.name) or OLD_SLOT_FILE_RE.match(path.stem)
    return int(match.group(1)) if match else None


def project_dirs(root: Path) -> list[Path]:
    """``projNN`` sub-folders of a card root, sorted by number."""
    if not root.is_dir():
        return []
    return sorted((p for p in root.iterdir()
                   if p.is_dir() and PROJECT_DIR_RE.match(p.name)),
                  key=lambda p: int(PROJECT_DIR_RE.match(p.name).group(1)))


def collect_inputs(paths: list[Path], extensions: tuple[str, ...]) -> list[Path]:
    """Expand files and directories into sorted LXR-02 files.

    A directory is scanned for the extensions (case-insensitive, macOS ``._``
    files ignored). A card root (a folder with ``projNN`` sub-folders) is
    scanned together with each project folder; ``/tmp`` is only read when
    named explicitly, because it repeats slots of the last-used project.
    """
    found: list[Path] = []
    for path in paths:
        if path.is_dir():
            for folder in [path] + project_dirs(path):
                found.extend(p for p in sorted(folder.iterdir())
                             if p.is_file() and p.suffix.lower() in extensions
                             and not p.name.startswith("._"))
        elif path.is_file():
            found.append(path)
        else:
            raise SystemExit(f"input not found: {path}")
    if not found:
        raise SystemExit("no LXR-02 input files found")
    return found


def collect_projects(paths: list[Path]) -> list[Path]:
    """Expand inputs into project folders (``projNN``, ``tmp`` or any folder
    holding ``NN-*`` slot files). A card root expands to its ``projNN``."""
    found: list[Path] = []
    for path in paths:
        if not path.is_dir():
            raise SystemExit(f"not a project folder: {path}")
        subs = project_dirs(path)
        found.extend(subs if subs else [path])
    return found


def project_name(folder: Path) -> str:
    """Name from ``prj.nfo``; the folder name when it is missing or blank."""
    nfo = find_file(folder, "prj.nfo")
    if nfo is not None:
        name = lxr2_name(nfo.read_bytes()[:8])
        if name:
            return name
    return folder.name


def find_file(folder: Path, name: str) -> Path | None:
    """Case-insensitive lookup of one file in ``folder`` (FAT is caseless)."""
    if not folder.is_dir():
        return None
    for entry in folder.iterdir():
        if entry.is_file() and entry.name.lower() == name.lower():
            return entry
    return None


def find_slot_file(folder: Path, slot: int, ext: str,
                   report: Report | None = None) -> Path | None:
    """The file the LXR-02 loads for ``slot``: ``NN-*.ext``, else ``pNNN.ext``.

    preset_makeFileNameLoad() takes the first ``NN-*`` directory match, so a
    second match is reported (the converter takes the first by name).
    """
    matches = sorted(p for p in folder.iterdir()
                     if p.is_file() and not p.name.startswith("._")
                     and p.suffix.lower() == ext
                     and p.name.startswith(f"{slot:02d}-"))
    if len(matches) > 1 and report is not None:
        report.note(f"slot {slot:02d}: several {ext} files "
                    f"({', '.join(p.name for p in matches)}); using {matches[0].name}")
    if matches:
        return matches[0]
    return find_file(folder, f"p{slot:03d}{ext}")


# ---------------------------------------------------------------------------
# Kits.
# ---------------------------------------------------------------------------
@dataclass
class Lxr2Kit:
    name: str                          # "" when unnamed
    params: bytes                      # exactly LXR2_SOUND_PARAM_COUNT bytes

    def value(self, par_name: str) -> int:
        return self.params[LXR2_PAR[par_name]]

    def is_untouched_init_kit(self) -> bool:
        """True for a slot a new project filled with the init kit."""
        return (self.name == "Initkit" and
                self.params[:INIT_KIT_COPIED_PARAMS] ==
                LXR2_INIT_KIT[8:8 + INIT_KIT_COPIED_PARAMS])


def kit_from_bytes(name: str, raw: bytes) -> Lxr2Kit:
    """Apply preset_readDrumsetData(): zero-fill short data, drop extra."""
    params = bytes(raw[:LXR2_SOUND_PARAM_COUNT]).ljust(LXR2_SOUND_PARAM_COUNT,
                                                        b"\0")
    return Lxr2Kit(name, params)


def read_snd(path: Path) -> tuple[Lxr2Kit, int | None]:
    """Read ``NN-NAME.snd`` / ``pNNN.snd`` / ``tmp.snd``; return (kit, slot).

    ``tmp.snd`` (preset_saveTmpKit) is one slot byte + the parameters, with
    no name; it is named ``Tmp NN`` after the slot it was edited from and
    has no slot of its own.
    """
    data = path.read_bytes()
    if is_tmp_buffer(path):
        if len(data) < 1:
            raise ValueError("tmp.snd is empty")
        # The edit buffer repeats a saved slot, so it never claims that
        # slot's number (slot None: always a free library slot).
        return kit_from_bytes(f"Tmp {data[0]:02d}", data[1:]), None
    if len(data) < 8:
        raise ValueError("shorter than the 8-byte kit name")
    return kit_from_bytes(lxr2_name(data[:8]), data[8:]), slot_number(path)


def init_kit() -> Lxr2Kit:
    """The kit a new LXR-02 project puts in every slot (for missing slots)."""
    return kit_from_bytes("Initkit", LXR2_INIT_KIT[8:])


def lxr1_kit_image(kit: Lxr2Kit, report: Report, label: str) -> l1.Lxr1Kit:
    """Translate an LXR-02 kit into the LXR1 kit layout used by lxr1_common.

    * Bytes 0..220 copy unchanged (identical offsets, checked at import).
    * Waveform 5 (PWM) becomes 3 (Rec): Helicase's 5 is the Cym crash wave.
    * Velocity/LFO target bytes are LXR-02 modTargets indices; each is
      decoded to its PAR name and re-encoded as that name's LXR1 modTargets
      index, so lxr1_common.convert_targets() resolves the same parameter.
      Indices the firmware would reject (>= 229) load as 0 there too.
      PWM and FX targets have no LXR1/Helicase equivalent and become off.
    * An invalid LFO voice becomes 1, as preset_sendDrumsetParameters() does.
    * Bytes 221..227 (LXR1 MIDI notes) stay 0: the LXR-02 keeps track MIDI
      notes in the globals; banks take them from glo.cfg.
    """
    lead = f"{label}: " if label else ""
    params = bytearray(l1.LXR1_SOUND_PARAM_COUNT)
    params[:SHARED_SOUND_PARAM_COUNT] = kit.params[:SHARED_SOUND_PARAM_COUNT]
    for slot, pars in WAVEFORM_PARS_BY_SLOT.items():
        pwm_used = False
        for par in pars:
            offset = LXR2_PAR[par]
            if params[offset] == LXR2_WAVE_PWM:
                params[offset] = HELICASE_WAVE_REC
                pwm_used = True
            elif params[offset] > LXR2_WAVE_PWM:
                report.note(f"{lead}voice {slot} waveform value "
                            f"{params[offset]} is out of range; set to Sin")
                params[offset] = 0
        if pwm_used:
            report.note(f"{lead}voice {slot} PWM waveform written as Rec "
                        f"(pulse width {kit.value(f'PAR_PWM_{slot}')} not "
                        "carried; Helicase has no PWM)")
    for slot in range(1, INSTRUMENT_SLOT_COUNT + 1):
        for par, what in ((f"PAR_VEL_DEST_{slot}", "velocity"),
                          (f"PAR_TARGET_LFO{slot}", "LFO")):
            offset = LXR2_PAR[par]
            index = params[offset]
            name = LXR2_MOD_TARGETS[index] if index < len(LXR2_MOD_TARGETS) else None
            if name is None or name == "PAR_NONE":
                params[offset] = 0
            elif name in LXR1_MOD_TARGET_INDEX:
                params[offset] = LXR1_MOD_TARGET_INDEX[name]
            else:
                kind = "FX" if name in _FX_TARGETS else "PWM"
                report.note(f"{lead}voice {slot} {what} target {name} is an "
                            f"LXR-02 {kind} parameter with no Helicase "
                            "Instrument parameter; set to off")
                params[offset] = 0
        voice_offset = LXR2_PAR[f"PAR_VOICE_LFO{slot}"]
        if not 1 <= params[voice_offset] <= INSTRUMENT_SLOT_COUNT:
            params[voice_offset] = 1
    srt = kit.value("PAR_VOICE_DECIMATION_ALL")
    if srt != SRT_NEUTRAL:
        report.note(f"{lead}all-voice sample rate (srt) {srt} not carried "
                    "(Helicase retired it; use each voice's sample rate)")
    fx_type = kit.value("PAR_FX_TYPE")
    if fx_type != 0:
        report.note(f"{lead}FX '{fx_type_name(fx_type)}' (dry/wet "
                    f"{kit.value('PAR_FX_DRY_WET')}) not converted; Helicase "
                    "has no matching Effect type")
    return l1.Lxr1Kit(kit.name, bytes(params), "modtargets")


def fx_type_name(value: int) -> str:
    return FX_TYPE_NAMES[value] if value < len(FX_TYPE_NAMES) else str(value)


def check_kit(kit: Lxr2Kit, fw: Firmware, report: Report, label: str) -> l1.Lxr1Kit:
    """Translate the kit and run every instrument conversion once.

    This pass collects all notes (also in a dry run) and fails before any
    slot is claimed; ``write_kit_dir`` later repeats the same pure
    conversion silently.
    """
    image = lxr1_kit_image(kit, report, label)
    for slot in range(1, INSTRUMENT_SLOT_COUNT + 1):
        l1.instrument_values(image, slot, fw, _Prefixed(report, label))
    return image


class _Prefixed(Report):
    """Adds a label (if any) to notes produced by lxr1_common helpers."""

    def __init__(self, target: Report, label: str):
        super().__init__()
        self.target = target
        self.label = label

    def note(self, text: str) -> None:
        self.target.note(f"{self.label}: {text}" if self.label else text)


def kit_audio_out(kit: Lxr2Kit, report: Report, label: str) -> list[int]:
    """Scene ``audio_out[6]`` from the kit's voice routes.

    0..5 copy. A voice routed to the FX bus (6) goes to the FX output
    (PAR_FX_ROUTING): with FX off the LXR-02 bus passes it there unchanged
    (FxBus_process FX_OFF), so that is exact; with an effect on, the effect
    is lost and a note is printed.
    """
    fx_route = kit.value("PAR_FX_ROUTING")
    fx_route = fx_route if fx_route < len(AUDIO_OUT_NAMES) else 0
    fx_type = kit.value("PAR_FX_TYPE")
    routes = []
    for slot in range(1, INSTRUMENT_SLOT_COUNT + 1):
        route = kit.value(f"PAR_AUDIO_OUT{slot}")
        if route == LXR2_AUDIO_OUT_FX:
            if fx_type != 0:
                report.note(f"{label}: voice {slot} played through FX "
                            f"'{fx_type_name(fx_type)}'; now dry on "
                            f"{AUDIO_OUT_NAMES[fx_route]}")
            route = fx_route
        elif route >= len(AUDIO_OUT_NAMES):
            route = 0
        routes.append(route)
    return routes


# ---------------------------------------------------------------------------
# Patterns.
# ---------------------------------------------------------------------------
@dataclass
class Lxr2Step:
    volume: int
    prob: int
    note: int
    p1_dest: int
    p1_val: int
    p2_dest: int
    p2_val: int

    def automation(self) -> list[tuple[int, int]]:
        return [(d, v) for d, v in ((self.p1_dest, self.p1_val),
                                    (self.p2_dest, self.p2_val))
                if d not in (NO_AUTOMATION, 0)]


@dataclass
class Lxr2Pattern:
    name: str                          # "" when unnamed
    version: int                       # 1, 2 or 3 (PatV tag; 1 = no tag)
    steps: list[list[Lxr2Step]]        # [track][64]
    timing: list[list[int]]            # [track][64] flam | shift << 4
    main_masks: list[int]              # [track] 64-bit; bit s = step s on
    shuffle: int
    lengths: list[int] | None          # [track] raw; None if not stored
    kit_nr: int | None                 # None before PatV02

    def has_content(self) -> bool:
        """True if any track has a step the LXR-02 plays (main bit set
        within the track length, probability above 0)."""
        for t in range(NUM_TRACKS):
            length = track_length(self.lengths, t)
            for s in range(length):
                if self.main_masks[t] >> s & 1 and self.steps[t][s].prob:
                    return True
        return False


def empty_pattern() -> Lxr2Pattern:
    """A cleared pattern (seq_clearPattern()), for missing pattern slots."""
    return Lxr2Pattern(
        "", PATTERN_VERSION_MAX,
        [[Lxr2Step(127, 100, LXR2_DEFAULT_NOTE, NO_AUTOMATION, 0,
                   NO_AUTOMATION, 0) for _ in range(LXR2_STEPS)]
         for _ in range(NUM_TRACKS)],
        [[TIMING_DEFAULT] * LXR2_STEPS for _ in range(NUM_TRACKS)],
        [0] * NUM_TRACKS, 0, [16] * NUM_TRACKS, 0)


def pattern_payload_bytes(version: int) -> int:
    """Bytes preset_writePatternData() writes for one pattern of a version
    (V1 counted with its optional length block)."""
    per_track = LXR2_STEPS * STEP_BYTES + (LXR2_STEPS if version > 2 else 0)
    return (NUM_TRACKS * per_track + NUM_TRACKS * LXR2_BARS * 2 + 1 +
            NUM_TRACKS + (1 if version > 1 else 0))


def read_pattern_payload(data: bytes, pos: int, version: int, name: str,
                         origin: str) -> Lxr2Pattern:
    """Decode one pattern as preset_readPatternData() reads it.

    Per track: 64 x 7 step bytes (volume|0x80, prob 0..100, note, p1 dest,
    p1 value, p2 dest, p2 value), then (PatV03) 64 timing bytes
    (StepTiming bitfield: flam low nibble, shift high nibble). Then for each
    track 4 uint16 LE main-step words (bar 0..3, bit n = step 16*bar + n),
    the shuffle byte, 7 track lengths and (PatV02+) the kit number. The
    firmware fails a V2/V3 file without its kit number and accepts a V1
    file without its lengths (all tracks then stay 16 steps).
    """
    def need(count: int, what: str) -> None:
        if pos + count > len(data):
            raise ValueError(f"{origin}: truncated in the {what} "
                             f"(PatV0{version} needs "
                             f"{pattern_payload_bytes(version)} data bytes)")

    steps, timing = [], []
    for t in range(NUM_TRACKS):
        need(LXR2_STEPS * STEP_BYTES, f"track {t + 1} steps")
        steps.append([Lxr2Step(*data[pos + i * STEP_BYTES:
                                      pos + (i + 1) * STEP_BYTES])
                      for i in range(LXR2_STEPS)])
        pos += LXR2_STEPS * STEP_BYTES
        if version > 2:
            need(LXR2_STEPS, f"track {t + 1} timing")
            timing.append(list(data[pos:pos + LXR2_STEPS]))
            pos += LXR2_STEPS
        else:
            timing.append([TIMING_DEFAULT] * LXR2_STEPS)
    masks = []
    for t in range(NUM_TRACKS):
        need(LXR2_BARS * 2, "main steps")
        words = struct.unpack_from(f"<{LXR2_BARS}H", data, pos)
        pos += LXR2_BARS * 2
        masks.append(sum(w << (16 * bar) for bar, w in enumerate(words)))
    need(1, "shuffle")
    shuffle = data[pos]
    pos += 1
    lengths = None
    if pos + NUM_TRACKS <= len(data):
        lengths = list(data[pos:pos + NUM_TRACKS])
        pos += NUM_TRACKS
    elif version > 1:
        need(NUM_TRACKS, "track lengths")
    kit_nr = None
    if version > 1:
        need(1, "kit number")
        kit_nr = data[pos]
    return Lxr2Pattern(name, version, steps, timing, masks, shuffle, lengths,
                       kit_nr)


def read_pat(path: Path) -> tuple[Lxr2Pattern, int | None]:
    """Read ``NN-NAME.pat`` / ``pNNN.pat`` / ``tmp.pat``; return (pattern, slot).

    ``preset_loadPatternSD``: 8-byte name, then ``PatVNN`` (V2+) or the data
    directly (V1, no tag). ``tmp.pat`` (preset_saveTmpPat): one slot byte,
    then PatV03 data without name or tag; like ``tmp.snd`` it is named
    ``Tmp NN`` and has no slot of its own.
    """
    data = path.read_bytes()
    if is_tmp_buffer(path):
        if len(data) < 1:
            raise ValueError("tmp.pat is empty")
        return (read_pattern_payload(data, 1, PATTERN_VERSION_MAX,
                                     f"Tmp {data[0]:02d}", path.name), None)
    if len(data) < 8:
        raise ValueError("shorter than the 8-byte pattern name")
    name = lxr2_name(data[:8])
    tag = data[8:14]
    if tag[:4] == b"PatV":
        if not tag[4:6].isdigit():
            raise ValueError(f"unreadable version tag {tag!r}")
        version, pos = int(tag[4:6]), 14
        if not 1 <= version <= PATTERN_VERSION_MAX:
            raise ValueError(f"pattern version {version} is not supported "
                             f"(PatV01..PatV0{PATTERN_VERSION_MAX})")
    else:
        version, pos = 1, 8
    return read_pattern_payload(data, pos, version, name, path.name), slot_number(path)


def track_length(lengths: list[int] | None, track: int) -> int:
    """Steps a track plays: 0 (or no length block) = 16; the step index
    wraps at 64 whatever the stored length (seq_nextStep())."""
    raw = lengths[track] if lengths is not None else 0
    return min(raw or 16, LXR2_STEPS)


def shift_units(timing_byte: int) -> int:
    """StepTiming.shift (high nibble) -> -7..+7 (getShiftValue(); 15 -> 0)."""
    raw = timing_byte >> 4
    return raw - 7 if raw <= 14 else 0


def flam_index(timing_byte: int) -> int:
    """StepTiming.flam (low nibble) 0..5; flamIdxToPattern() plays other
    values as off."""
    flam = timing_byte & 0x0F
    return flam if flam in FLAM_ROLL_INDEX else 0


def probability_to_condition(prob: int) -> int | None:
    """LXR-02 step probability (percent) -> Helicase condition.

    The LXR-02 plays a step when a random 1..100 is <= prob, so 100 (and
    above) always plays -> condition 0, and 0 never plays -> None (the step
    is left out). 1..99 take the nearest of r10..r90 (1..9) and r95 (10),
    ties rounding up; 96..99 stay r95 so a chance step never becomes certain.
    """
    if prob >= 100:
        return 0
    if prob <= 0:
        return None
    buckets = [(10 * n, n) for n in range(1, 10)] + [(95, 10)]
    return min(buckets, key=lambda b: (abs(b[0] - prob), -b[0]))[1]


def automation_id(dest: int, fw: Firmware) -> int | None:
    """Map an LXR-02 step-automation destination to an instrument_param_id_t.

    Stored like the LXR1 (CortexParser.c FRONT_SET_P1_DEST "val++"):
    parameters below 128 are stored one higher, 128 and above unchanged.
    """
    par_value = dest - 1 if dest < 128 else dest
    name = LXR2_NAME_BY_VALUE.get(par_value)
    if name is None or name not in PAR_TO_SLOT_KEY:
        return None
    slot, key = PAR_TO_SLOT_KEY[name]
    index, desc = fw.descriptor(slot, key)
    if not desc.image:
        return None
    return (slot - 1) * INSTRUMENT_PARAM_COUNT + index


@dataclass
class _Hit:
    """One LXR-02 step hit placed on a Helicase step."""
    source: int                        # LXR-02 step that owns the hit
    ticks: int                         # delay from the Helicase step start


def place_hits(src: Lxr2Pattern, t: int, length: int) -> tuple[dict[int, _Hit], int, int]:
    """Place one track's hits on Helicase steps; return (hits, never, moved_back).

    A shift of 0..+7 delays the hit inside its own step (3 ticks per unit).
    A shift of -1..-7 plays the hit in the *previous* step's time, at
    24 - 3k ticks (seq_getShiftFlamPattern() STEP_NEXT). Such a hit moves to
    the previous Helicase step when that step is free: a run of consecutive
    negatively shifted steps moves together when the step before the run is
    not played (or the run is the whole track); otherwise every hit of the
    run stays on its own step without the shift ("moved_back" counts them).
    Steps with probability 0 never play and are left out ("never").
    """
    played = [s for s in range(length)
              if src.main_masks[t] >> s & 1 and src.steps[t][s].prob > 0]
    never = sum(1 for s in range(length)
                if src.main_masks[t] >> s & 1 and src.steps[t][s].prob == 0)
    played_set = set(played)
    negative = {s for s in played if shift_units(src.timing[t][s]) < 0}
    hits: dict[int, _Hit] = {}
    kept = 0
    for s in played:
        shift = shift_units(src.timing[t][s])
        if shift >= 0:
            hits[s] = _Hit(s, shift * SHIFT_TICKS)
            continue
        start, walked = s, 0
        while (start - 1) % length in negative and walked < length:
            start = (start - 1) % length
            walked += 1
        movable = walked >= length or (start - 1) % length not in played_set
        if movable:
            hits[(s - 1) % length] = _Hit(s, STEP_TICKS + shift * SHIFT_TICKS)
        else:
            hits[s] = _Hit(s, 0)
            kept += 1
    return hits, never, kept


def convert_pattern(src: Lxr2Pattern, fw: Firmware, report: Report,
                    label: str) -> HelicasePattern:
    """Convert one LXR-02 pattern into a Helicase Pattern.

    One LXR-02 step becomes one Helicase 1/16 step; track length 1..64 is
    kept and the pattern shuffle goes to every track. Per hit: velocity =
    volume, note = note + 3 (same pitch), probability -> condition, flam ->
    roll (exact rates, fill 100), shift -> micro-timing or a move to the
    previous step (``place_hits``). A hit's automation is the automation
    stored on the LXR-02 step whose time it plays in, because
    seq_triggerVoice() applies the automation of the current step index;
    steps without a hit get none (the LXR-02 applies automation only when a
    step triggers).
    """
    lead = f"{label}: " if label else ""
    out = HelicasePattern.empty()
    dropped_autos = never_total = kept_total = clamped = cut_rolls = 0
    for t in range(NUM_TRACKS):
        raw_length = src.lengths[t] if src.lengths is not None else 0
        if raw_length > LXR2_STEPS:
            report.note(f"{lead}track {t + 1} length {raw_length} plays "
                        f"as {LXR2_STEPS}")
        length = track_length(src.lengths, t)
        out.length[t] = length
        out.shuffle[t] = src.shuffle & 0x7F
        hits, never, kept = place_hits(src, t, length)
        never_total += never
        kept_total += kept
        for pos, hit in hits.items():
            data = src.steps[t][hit.source]
            note = (data.note & 0x7F) + NOTE_OFFSET
            if note > 127:
                clamped += 1
                note = 127
            step = out.steps[t][pos]
            step.trigger = True
            step.specials = {
                "velocity": data.volume & 0x7F,
                "note": note,
                "condition": probability_to_condition(data.prob),
            }
            if hit.ticks:
                step.specials["timing"] = l1.timing_for_substep(
                    hit.ticks // SHIFT_TICKS)
            flam = flam_index(src.timing[t][hit.source])
            if flam:
                step.specials["roll"] = FLAM_ROLL_INDEX[flam]
                # Hits after the step end run on into the next step; the
                # LXR-02 interleaves them with that step's own hit, Helicase
                # ends the roll at it.
                last = hit.ticks + STEP_TICKS - FLAM_SPACING_TICKS[flam]
                if last >= STEP_TICKS and (pos + 1) % length in hits \
                        and (pos + 1) % length != pos:
                    cut_rolls += 1
            seen = set()
            for dest, value in src.steps[t][pos].automation():
                target = automation_id(dest, fw)
                if target is None:
                    dropped_autos += 1
                elif target not in seen:
                    seen.add(target)
                    step.automation.append((target, value & 0x7F))
    if never_total:
        report.note(f"{lead}{never_total} step(s) with probability 0 never "
                    "play on the LXR-02 and were left out")
    if kept_total:
        report.note(f"{lead}{kept_total} negatively shifted step(s) could "
                    "not move into the previous step (it has its own hit) and "
                    "play on the beat")
    if cut_rolls:
        report.note(f"{lead}{cut_rolls} flam(s) shifted into the next step "
                    "end at that step's hit (the LXR-02 interleaves them)")
    if clamped:
        report.note(f"{lead}{clamped} step note(s) above 124 clamped to "
                    "127 after the +3 pitch offset")
    if dropped_autos:
        report.note(f"{lead}{dropped_autos} automation value(s) aimed at "
                    "parameters Helicase cannot automate were dropped")
    return out


# ---------------------------------------------------------------------------
# Songs, globals and projects.
# ---------------------------------------------------------------------------
@dataclass
class Lxr2Song:
    name: str
    entries: list[int]                 # raw 64 bytes
    slot: int | None

    def sequence(self) -> tuple[list[int], str]:
        """(patterns, ending): the patterns played in order and how the song
        ends - "stop" (SONG_END: playback stops), "loop" (SONG_LOOP, or all
        64 entries used: back to the first entry). seq_nextStep() treats
        any other value >= 64 like a pattern number it cannot play; it is
        taken as the end here."""
        patterns = []
        for value in self.entries:
            if value == SONG_LOOP:
                return patterns, "loop"
            if value >= NUM_SLOTS:
                return patterns, "stop"
            patterns.append(value)
        return patterns, "loop"

    def is_default(self) -> bool:
        """True for the song a new project gets: pattern 00, then end."""
        return self.entries[0] == 0 and all(v == SONG_END
                                            for v in self.entries[1:])


def read_sng(path: Path) -> Lxr2Song:
    """``NN-NAME.sng``: 8-byte name + 64 entries (preset_loadSongSD)."""
    data = path.read_bytes()
    if len(data) < 8:
        raise ValueError("shorter than the 8-byte song name")
    entries = list(data[8:8 + SONG_LENGTH])
    entries += [SONG_END] * (SONG_LENGTH - len(entries))
    return Lxr2Song(lxr2_name(data[:8]), entries, slot_number(path))


@dataclass
class Lxr2Globals:
    """The glo.cfg values that are Scene settings in Helicase."""
    midi_channels: list[int]           # [track] 1..16 (0 = not stored)
    midi_notes: list[int]              # [track] 0 = step note, 1..127 fixed
    pattern_mode: int                  # 0 off, 1 auto (pattern loads its kit)
    stored: bool                       # False = LXR-02 defaults used


def default_globals() -> Lxr2Globals:
    """preset_loadDefaultGlobals(): channel 1, note 48, pattern mode auto."""
    return Lxr2Globals([1] * NUM_TRACKS, [LXR2_DEFAULT_TRACK_NOTE] * NUM_TRACKS,
                       PATTERN_MODE_AUTO, False)


def read_globals(folder: Path) -> Lxr2Globals:
    """Read ``glo.cfg``: one byte per parameter from PAR_BPM (272) on."""
    path = find_file(folder, "glo.cfg")
    if path is None:
        return default_globals()
    data = path.read_bytes()
    if len(data) < GLOBALS_BYTES:
        raise ValueError(f"{path.name}: {len(data)} bytes, expected "
                         f"{GLOBALS_BYTES}")
    first = LXR2_PAR["PAR_BEGINNING_OF_GLOBALS"]

    def value(par: str) -> int:
        return data[LXR2_PAR[par] - first]

    channels = [value(f"PAR_MIDI_CHAN_{t}") for t in range(1, NUM_TRACKS + 1)]
    notes = [value(f"PAR_MIDI_NOTE{t}") for t in range(1, NUM_TRACKS + 1)]
    return Lxr2Globals([c if 1 <= c <= 16 else 0 for c in channels],
                       [n if n <= 127 else 0 for n in notes],
                       value("PAR_PATTERN_MODE"), True)


@dataclass
class Lxr2Project:
    folder: Path
    name: str
    globals: Lxr2Globals
    kits: list[Lxr2Kit | None]         # [slot]; None = no file
    patterns: list[Lxr2Pattern | None]
    songs: list[Lxr2Song | None]

    def kit_for(self, pattern: Lxr2Pattern | None, report: Report,
                label: str) -> tuple[int, Lxr2Kit]:
        """The kit slot and kit a pattern plays with.

        Pattern mode ``Aut`` (default): the pattern's kit number
        (seq_setPendingPatternActive() loads it). Mode ``off``: kit 00, the
        kit preset_loadProject() loads. A missing kit file plays the init
        kit (a new project's content) and is reported.
        """
        slot = 0
        if self.globals.pattern_mode >= PATTERN_MODE_AUTO and pattern is not None:
            slot = pattern.kit_nr or 0
            if slot >= NUM_SLOTS:
                report.note(f"{label}: kit number {slot} is invalid; using kit 00")
                slot = 0
        kit = self.kits[slot]
        if kit is None:
            report.note(f"{label}: kit {slot:02d} has no file; using the "
                        "LXR-02 init kit")
            kit = init_kit()
        return slot, kit


def read_project(folder: Path, report: Report) -> Lxr2Project:
    """Read every slot of a project folder the way preset_loadProject() does
    (missing slot files are allowed; unreadable ones fail the project)."""
    if not folder.is_dir():
        raise ValueError(f"{folder} is not a folder")
    name = project_name(folder)
    globals_ = read_globals(folder)
    kits: list[Lxr2Kit | None] = []
    patterns: list[Lxr2Pattern | None] = []
    songs: list[Lxr2Song | None] = []
    for slot in range(NUM_SLOTS):
        path = find_slot_file(folder, slot, ".snd", report)
        kits.append(read_snd(path)[0] if path else None)
        path = find_slot_file(folder, slot, ".pat", report)
        patterns.append(read_pat(path)[0] if path else None)
        path = find_slot_file(folder, slot, ".sng", report)
        songs.append(read_sng(path) if path else None)
    if not any(kits) and not any(patterns):
        raise ValueError("no NN-*.snd or NN-*.pat files; not an "
                         "LXR-02 project folder")
    return Lxr2Project(folder, name, globals_, kits, patterns, songs)


def scene_midi_settings(project: Lxr2Project,
                        report: Report) -> tuple[list[int], list[int]]:
    """(midi_channel[7], midi_note[7]) for every Scene of a project.

    Channels 1..16 copy (0 = ``glb`` when glo.cfg is missing a value). A
    fixed track note (the LXR-02 sends it instead of the step note and only
    answers to it) becomes the Helicase track note root; 0 (send the step
    note) becomes ``on``. When all seven notes are still the LXR-02 default
    48 they were never set, and every track gets ``on`` (Helicase's own
    default). Tracks sharing a note on one channel are reported: in
    Helicase the lower track owns that root (MIDI_SPEC.md 4.1).
    """
    g = project.globals
    if not g.stored:
        report.note("no glo.cfg; MIDI channels use the LXR-02 default "
                    "(channel 1)")
    notes = list(g.midi_notes)
    if all(note == LXR2_DEFAULT_TRACK_NOTE for note in notes):
        if g.stored:
            report.note("track MIDI notes are the LXR-02 default (48 on every "
                        "track); written as `on` (no note root)")
        return list(g.midi_channels), [0] * NUM_TRACKS
    owners: dict[tuple[int, int], list[int]] = {}
    for t in range(NUM_TRACKS):
        if notes[t]:
            owners.setdefault((g.midi_channels[t], notes[t]), []).append(t + 1)
    for (channel, note), tracks in sorted(owners.items()):
        if len(tracks) > 1:
            where = f"channel {channel}" if channel else "the global channel"
            report.note(f"tracks {', '.join(map(str, tracks))} share MIDI note "
                        f"{note} on {where}; Helicase gives it to track "
                        f"{tracks[0]}")
    return list(g.midi_channels), notes


# ---------------------------------------------------------------------------
# Banks.
# ---------------------------------------------------------------------------
@dataclass
class SceneSpec:
    """One Bank-local Scene to write."""
    index: int                         # Bank slot 0..15
    name: str
    pattern: HelicasePattern
    pat4: bytes
    kit_slot: int
    kit: Lxr2Kit
    kit_image: l1.Lxr1Kit
    audio_out: list[int]
    chain: tuple[int, int] = (0, 0)


def tied_masks(scenes: list[SceneSpec]) -> list[int]:
    """VOICE edit masks: Scenes that play the same LXR-02 kit slot share one
    mask (editing that kit on the LXR-02 changes all of them); absent slots
    keep their self-only default. The groups never overlap, as Helicase
    requires (masks stay mutually exclusive)."""
    masks = [1 << i for i in range(BANK_SCENES)]
    groups: dict[int, int] = {}
    for scene in scenes:
        groups[scene.kit_slot] = groups.get(scene.kit_slot, 0) | 1 << scene.index
    for scene in scenes:
        masks[scene.index] = groups[scene.kit_slot]
    return masks


def write_bankset(path: Path, masks: list[int], active_scene: int) -> None:
    """bankset.bcg v2 with 16 indexed VOICE edit-mask lines."""
    lines = ["format=helicase.bankset", "version=2",
             f"active_scene={active_scene}"]
    lines += [f"scene_mask_voice_edit_{i}={mask:04x}"
              for i, mask in enumerate(masks)]
    lines.append("")
    path.write_text("\n".join(lines), encoding="ascii")


def write_bank(bank_dir: Path, scenes: list[SceneSpec], fw: Firmware,
               midi_channel: list[int], midi_note: list[int]) -> None:
    """Write bankset.bcg and one ``SS <name>/`` folder per Scene.

    Each Scene holds sceneset.scg, ``<name>.pat`` and ``Kit <kit name>/``;
    no Effect file (Scenes load with the Effect off).
    """
    for scene in scenes:
        scene_dir = bank_dir / f"{scene.index:02d} {scene.name}"
        scene_dir.mkdir(parents=True)
        kit_name = safe_name(scene.kit.name, f"Kit {scene.kit_slot:02d}")
        write_kit_dir(scene_dir / f"Kit {kit_name}", scene.kit_image, fw,
                      Report())
        (scene_dir / f"{scene.name}.pat").write_bytes(scene.pat4)
        write_sceneset(scene_dir / "sceneset.scg", scene.pattern,
                       scene.audio_out, midi_channel, midi_note, scene.chain)
    write_bankset(bank_dir / "bankset.bcg", tied_masks(scenes),
                  min(scene.index for scene in scenes))


def repeat_parts(count: int) -> list[int]:
    """Split a run of ``count`` plays into chainable repeat counts
    (x01..x08, x16, x32, x64), largest first."""
    parts = []
    for size in sorted(CHAIN_REPEAT_CONDITIONS, reverse=True):
        while count >= size:
            parts.append(size)
            count -= size
    return parts


def song_plan(song: Lxr2Song) -> tuple[list[tuple[int, int]], str]:
    """(pattern, repeats) per Scene in play order, and the song ending.

    Consecutive entries of the same pattern merge into one Scene that
    repeats (the LXR-02 plays every entry once, to the end of its longest
    track, which is exactly one Helicase repetition); a run longer than the
    chain counts allow is split over several Scenes.
    """
    patterns, ending = song.sequence()
    runs: list[list[int]] = []
    for value in patterns:
        if runs and runs[-1][0] == value:
            runs[-1][1] += 1
        else:
            runs.append([value, 1])
    plan = [(value, part) for value, count in runs
            for part in repeat_parts(count)]
    return plan, ending


def song_chains(plan: list[tuple[int, int]], ending: str) -> list[tuple[int, int]]:
    """Scene chain (cn1, nx1) per planned Scene.

    Every Scene moves on after its repeat count (``xNN``) to the next Scene
    (``sNN``). The last one stops playback (``stp``) or goes back to Scene 00
    (loop). A one-Scene loop needs no chain.
    """
    if len(plan) == 1 and ending == "loop":
        return [(0, 0)]
    chains = []
    for i, (_, repeats) in enumerate(plan):
        condition = CHAIN_REPEAT_CONDITIONS[repeats]
        if i + 1 < len(plan):
            chains.append((condition, CHAIN_NEXT_ABSOLUTE + i + 1))
        elif ending == "loop":
            chains.append((condition, CHAIN_NEXT_ABSOLUTE))
        else:
            chains.append((condition, CHAIN_NEXT_STOP))
    return chains


# ---------------------------------------------------------------------------
# Command-line helpers.
# ---------------------------------------------------------------------------
def add_common_arguments(parser) -> None:
    l1.add_common_arguments(parser)


def add_numbering_arguments(parser, what: str) -> None:
    parser.add_argument("--keep-numbers", action="store_true",
                        help=f"use the LXR-02 slot number as the {what} slot "
                             "(07-NAME -> 007); one project per run")
    parser.add_argument("--overwrite", action="store_true",
                        help="with --keep-numbers, replace an occupied slot")

