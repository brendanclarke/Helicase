#!/usr/bin/env python3
"""Shared readers and writers for the LXR1 -> Helicase converters.

LXR1 side (Sonic Potions LXR, https://github.com/SonicPotions/LXR, front
firmware ``front/LxrAvr``):

* ``Parameters.h`` defines ``enum ParamEnums``. A kit file stores one byte per
  enum value from 0 up to ``END_OF_SOUND_PARAMETERS`` (228); the enum body is
  copied into ``LXR1_PARAM_ENUM_SOURCE`` below and evaluated with the C rules
  (aliases, implicit increments), so the byte offsets are exact. (Checked
  equal to upstream master with ``evaluate_c_enum``.)
* Kit generations seen in the LXR history:

  - V0.1x (early 2013): a different enum, 225 sound bytes. Refused.
  - V0.2x (LXR commit 74f5b23, July 2013 - the factory kits in
    SdCardImage.zip): today's offsets, 221 sound bytes, LFO/velocity
    targets as menu-page references (``decode_target`` "pages").
  - November 2013 onward: 228 sound bytes (seven MIDI notes appended),
    targets as ``modTargets[]`` indices (``Menu/Cc2Text.c``).

* ``Preset/presetManager.c`` writes the files:

  - ``pNNN.snd``  8-byte name + sound parameters (short files are zero-filled
    on load, which is how the 229-byte factory kits lack the seven MIDI-note
    bytes);
  - ``pNNN.pat``  8-byte name + the pattern-set block;
  - ``pNNN.all``  8-byte name, version byte, global parameters padded to 64
    bytes, the kit padded to 512 bytes, then the pattern-set block;
  - ``pNNN.prf``  as ``.all`` but the 64-byte block holds BPM and the bar
    reset mode (version 1 files have only the BPM byte and no padding).
    These settings blocks are skipped, never converted.

* The pattern-set block (``preset_writePatternData()``), as laid out by the
  mainboard's ``seq_sendStepInfoToFront()`` / ``seq_sendMainStepInfoToFront()``:

  - 7,168 steps x 7 bytes (volume|active, prob, note, p1 dest, p1 value,
    p2 dest, p2 value). Step ``i`` is track ``(i // 128) // 8``, pattern
    ``(i // 128) % 8``, sub-step ``i % 128``. Each of the 16 main steps owns
    eight sub-steps (1/128 notes at a 1/16 main step).
  - 56 main-step masks (uint16 LE, index ``pattern * 7 + track``, bit N =
    main step N).
  - 8 x (next pattern, change bar).
  - 1 shuffle byte (0..127, one value for all tracks).
  - optional 56 length/rotate bytes (low nibble length in main steps, 0 =
    16; high nibble rotation in main steps).

  Step automation destinations are ``ParamEnums`` numbers in the mainboard's
  numbering (values below 128 are stored +1; see ``Firmware.automation_id``),
  0 or 0xFF = none.

Helicase side (authority: knowledge_files/specification_reference/
FILESYSTEM_SPEC.md and PATTERN_DYNAMIC_STACK.md):

* instrument key order, the morphable rows and the valid modulation and
  automation targets are read from the firmware descriptor tables
  (``Core/DSP/Instruments/*/*Parameters.c``) at run time, so target tokens and
  automation IDs always match the firmware the files are made for;
* PAT4 is built byte for byte like ``filesystem_patternBuildHeader()`` and
  ``PatternData`` (header, address array, bitmap, pool, CRC32C) and every file
  is decoded again and checked before it is written.
"""

from __future__ import annotations

import math
import re
import shutil
import struct
import sys
from dataclasses import dataclass, field
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parents[1]

# ---------------------------------------------------------------------------
# LXR1 parameter enum (front/LxrAvr/Parameters.h, SonicPotions/LXR master).
# Comments are stripped before evaluation; the trailing comments here only
# mark the absolute values for review.
# ---------------------------------------------------------------------------
LXR1_PARAM_ENUM_SOURCE = """
PAR_NONE, PAR_MOD_WHEEL,
PAR_OSC_WAVE_DRUM1 = PAR_MOD_WHEEL, PAR_OSC_WAVE_DRUM2, PAR_OSC_WAVE_DRUM3,
PAR_OSC_WAVE_SNARE, NRPN_DATA_ENTRY_COARSE, PAR_WAVE1_CYM, PAR_WAVE1_HH,
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
PAR_PAN1, PAR_PAN2, PAR_PAN3, NRPN_FINE, NRPN_COARSE, PAR_PAN4, PAR_PAN5,
PAR_PAN6,
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
PAR_MIDI_NOTE1, PAR_MIDI_NOTE2, PAR_MIDI_NOTE3, PAR_MIDI_NOTE4,
PAR_MIDI_NOTE5, PAR_MIDI_NOTE6, PAR_MIDI_NOTE7,
END_OF_SOUND_PARAMETERS,                           /* 228 */
PAR_ROLL = END_OF_SOUND_PARAMETERS, PAR_MORPH,
PAR_ACTIVE_STEP, PAR_STEP_VOLUME, PAR_STEP_PROB, PAR_STEP_NOTE,
PAR_EUKLID_LENGTH, PAR_EUKLID_STEPS, PAR_EUKLID_ROTATION,
PAR_AUTOM_TRACK, PAR_P1_DEST, PAR_P2_DEST, PAR_P1_VAL, PAR_P2_VAL,
PAR_SHUFFLE, PAR_PATTERN_BEAT, PAR_PATTERN_NEXT, PAR_TRACK_LENGTH,
PAR_POS_X, PAR_POS_Y, PAR_FLUX, PAR_SOM_FREQ, PAR_TRACK_ROTATION,
PAR_BEGINNING_OF_GLOBALS,
PAR_BPM = PAR_BEGINNING_OF_GLOBALS,                /* 251 */
PAR_MIDI_CHAN_1, PAR_MIDI_CHAN_2, PAR_MIDI_CHAN_3, PAR_MIDI_CHAN_4,
PAR_MIDI_CHAN_5, PAR_MIDI_CHAN_6,
PAR_FETCH, PAR_FOLLOW, PAR_QUANTISATION, PAR_SCREENSAVER_ON_OFF,
PAR_MIDI_MODE, PAR_MIDI_CHAN_7, PAR_MIDI_ROUTING, PAR_MIDI_FILT_TX,
PAR_MIDI_FILT_RX, PAR_PRESCALER_CLOCK_IN, PAR_PRESCALER_CLOCK_OUT1,
PAR_PRESCALER_CLOCK_OUT2, PAR_TRIG_GATE_MODE,
PAR_BAR_RESET_MODE, PAR_MIDI_CHAN_GLOBAL,
NUM_PARAMS
"""


def evaluate_c_enum(body: str) -> dict[str, int]:
    """Evaluate a C enum body (comments allowed) into name -> value."""
    body = re.sub(r"/\*.*?\*/", "", body, flags=re.S)
    body = re.sub(r"//.*", "", body)
    values: dict[str, int] = {}
    current = -1
    for raw in body.split(","):
        entry = raw.strip()
        if not entry:
            continue
        if "=" in entry:
            name, expr = (part.strip() for part in entry.split("=", 1))
            # Initialisers are a literal, a prior name, or "NAME + n" (the
            # 2013 V0.2x headers use the last form).
            current = 0
            for term in expr.split("+"):
                term = term.strip()
                current += values[term] if term in values else int(term, 0)
        else:
            name = entry
            current += 1
        values[name] = current
    return values


LXR1_PAR = evaluate_c_enum(LXR1_PARAM_ENUM_SOURCE)
LXR1_SOUND_PARAM_COUNT = LXR1_PAR["END_OF_SOUND_PARAMETERS"]
LXR1_NAME_BY_VALUE: dict[int, str] = {}
for _name, _value in LXR1_PAR.items():
    # First name wins, so aliases (PAR_MOD_WHEEL, PAR_ROLL) never shadow the
    # sound parameter stored at the same offset.
    if _name.startswith("PAR_") and _value not in LXR1_NAME_BY_VALUE:
        LXR1_NAME_BY_VALUE[_value] = _name
LXR1_NAME_BY_VALUE[LXR1_PAR["PAR_OSC_WAVE_DRUM1"]] = "PAR_OSC_WAVE_DRUM1"
LXR1_NAME_BY_VALUE[LXR1_PAR["PAR_BPM"]] = "PAR_BPM"

# Self-checks against the absolute values in Parameters.h comments. Its
# /*250*/ on PAR_BPM and /*270*/ on PAR_BAR_RESET_MODE predate the insertion
# of PAR_TRACK_ROTATION (--AS PATROT), so the real globals start at 251.
assert LXR1_PAR["PAR_COARSE1"] == 8
assert LXR1_PAR["PAR_BANK_CHANGE"] == 127
assert LXR1_PAR["PAR_FILTER_DRIVE_1"] == 128
assert LXR1_PAR["PAR_AUDIO_OUT6"] == 220
assert LXR1_SOUND_PARAM_COUNT == 228
assert LXR1_PAR["PAR_P1_VAL"] == 240
assert LXR1_PAR["PAR_BPM"] == 251
assert LXR1_PAR["PAR_BAR_RESET_MODE"] == 271

# modTargets[] from front/LxrAvr/Menu/Cc2Text.c, in index order. Kit
# PAR_VEL_DEST_n and PAR_TARGET_LFOn bytes are indices into this list.
LXR1_MOD_TARGETS = [
    "PAR_NONE", "PAR_VOICE_DECIMATION_ALL",
] + [
    name
    for voice in (1, 2, 3)
    for name in (
        f"PAR_COARSE{voice}", f"PAR_FINE{voice}", f"PAR_OSC_WAVE_DRUM{voice}",
        f"PAR_VELOA{voice}", f"PAR_VELOD{voice}", f"PAR_VOL_SLOPE{voice}",
        f"PAR_MOD_EG{voice}", f"PAR_PITCH_SLOPE{voice}", f"PAR_MODAMNT{voice}",
        f"PAR_VEL_DEST_{voice}", f"PAR_VELO_MOD_AMT_{voice}",
        f"PAR_VOLUME_MOD_ON_OFF{voice}", f"PAR_FMAMNT{voice}",
        f"PAR_FM_FREQ{voice}", f"PAR_MOD_WAVE_DRUM{voice}",
        f"PAR_MIX_MOD_{voice}", f"PAR_TRANS{voice}_WAVE",
        f"PAR_TRANS{voice}_VOL", f"PAR_TRANS{voice}_FREQ",
        f"PAR_FILTER_FREQ_{voice}", f"PAR_RESO_{voice}",
        f"PAR_FILTER_TYPE_{voice}", f"PAR_FILTER_DRIVE_{voice}",
        f"PAR_FREQ_LFO{voice}", f"PAR_SYNC_LFO{voice}",
        f"PAR_AMOUNT_LFO{voice}", f"PAR_WAVE_LFO{voice}",
        f"PAR_RETRIGGER_LFO{voice}", f"PAR_OFFSET_LFO{voice}",
        f"PAR_VOL{voice}", f"PAR_PAN{voice}", f"PAR_VOICE_DECIMATION{voice}",
        f"PAR_DRIVE{voice}", f"PAR_MIDI_NOTE{voice}",
    )
] + [
    "PAR_COARSE4", "PAR_FINE4", "PAR_NOISE_FREQ1", "PAR_MIX1",
    "PAR_OSC_WAVE_SNARE", "PAR_VELOA4", "PAR_VELOD4", "PAR_REPEAT4",
    "PAR_VOL_SLOPE4", "PAR_MOD_EG4", "PAR_PITCH_SLOPE4", "PAR_MODAMNT4",
    "PAR_VEL_DEST_4", "PAR_VELO_MOD_AMT_4", "PAR_VOLUME_MOD_ON_OFF4",
    "PAR_TRANS4_WAVE", "PAR_TRANS4_VOL", "PAR_TRANS4_FREQ",
    "PAR_FILTER_FREQ_4", "PAR_RESO_4", "PAR_FILTER_TYPE_4",
    "PAR_FILTER_DRIVE_4", "PAR_FREQ_LFO4", "PAR_SYNC_LFO4",
    "PAR_AMOUNT_LFO4", "PAR_WAVE_LFO4", "PAR_RETRIGGER_LFO4",
    "PAR_OFFSET_LFO4", "PAR_VOL4", "PAR_PAN4", "PAR_VOICE_DECIMATION4",
    "PAR_SNARE_DISTORTION", "PAR_MIDI_NOTE4",
    "PAR_COARSE5", "PAR_FINE5", "PAR_WAVE1_CYM", "PAR_VELOA5", "PAR_VELOD5",
    "PAR_REPEAT5", "PAR_VOL_SLOPE5", "PAR_VEL_DEST_5", "PAR_VELO_MOD_AMT_5",
    "PAR_VOLUME_MOD_ON_OFF5", "PAR_MOD_OSC_F1_CYM", "PAR_MOD_OSC_F2_CYM",
    "PAR_MOD_OSC_GAIN1_CYM", "PAR_MOD_OSC_GAIN2_CYM", "PAR_WAVE2_CYM",
    "PAR_WAVE3_CYM", "PAR_TRANS5_WAVE", "PAR_TRANS5_VOL", "PAR_TRANS5_FREQ",
    "PAR_FILTER_FREQ_5", "PAR_RESO_5", "PAR_FILTER_TYPE_5",
    "PAR_FILTER_DRIVE_5", "PAR_FREQ_LFO5", "PAR_SYNC_LFO5",
    "PAR_AMOUNT_LFO5", "PAR_WAVE_LFO5", "PAR_RETRIGGER_LFO5",
    "PAR_OFFSET_LFO5", "PAR_VOL5", "PAR_PAN5", "PAR_VOICE_DECIMATION5",
    "PAR_CYMBAL_DISTORTION", "PAR_MIDI_NOTE5",
    "PAR_COARSE6", "PAR_FINE6", "PAR_WAVE1_HH", "PAR_VELOA6",
    "PAR_VELOD6_CLOSED", "PAR_VELOD6_OPEN", "PAR_VOL_SLOPE6",
    "PAR_VEL_DEST_6", "PAR_VELO_MOD_AMT_6", "PAR_VOLUME_MOD_ON_OFF6",
    "PAR_MOD_OSC_F1", "PAR_MOD_OSC_F2", "PAR_MOD_OSC_GAIN1",
    "PAR_MOD_OSC_GAIN2", "PAR_WAVE2_HH", "PAR_WAVE3_HH", "PAR_TRANS6_WAVE",
    "PAR_TRANS6_VOL", "PAR_TRANS6_FREQ", "PAR_FILTER_FREQ_6", "PAR_RESO_6",
    "PAR_FILTER_TYPE_6", "PAR_FILTER_DRIVE_6", "PAR_FREQ_LFO6",
    "PAR_SYNC_LFO6", "PAR_AMOUNT_LFO6", "PAR_WAVE_LFO6",
    "PAR_RETRIGGER_LFO6", "PAR_OFFSET_LFO6", "PAR_VOL6", "PAR_PAN6",
    "PAR_VOICE_DECIMATION6", "PAR_HAT_DISTORTION", "PAR_MIDI_NOTE6",
]
# Cc2Text.c index comments: voice 1 starts at 2, voice 6 ends at 204.
assert LXR1_MOD_TARGETS[2] == "PAR_COARSE1"
assert LXR1_MOD_TARGETS[36] == "PAR_COARSE2"
assert LXR1_MOD_TARGETS[104] == "PAR_COARSE4"
assert LXR1_MOD_TARGETS[137] == "PAR_COARSE5"
assert LXR1_MOD_TARGETS[171] == "PAR_COARSE6"
assert len(LXR1_MOD_TARGETS) == 205

# Older target encoding (LXR firmware V0.2x, July..November 2013, which saved
# the factory kits): a target byte is (sub-page << 3) | entry, an index into
# that voice's menuPages[voice - 1][sub-page] parameter row
# (front/AVR/Menu/menuPages.h and menu.h MASK_PAGE/PAGE_SHIFT at LXR commit
# 74f5b23). Velocity targets index the voice's own pages; LFO targets index
# the pages of PAR_VOICE_LFOn. Commit 37a338e (Nov 2013) replaced this with
# modTargets indices, in the same release that appended the seven MIDI-note
# bytes, so a 221-byte kit payload always uses this encoding.
_V02_PAGE_TEMPLATE = (
    ("PAR_COARSE{v}", "PAR_FINE{v}", "PAR_OSC_WAVE_DRUM{v}"),
    ("PAR_VELOA{v}", "PAR_VELOD{v}", "PAR_VOL_SLOPE{v}"),
    ("PAR_MOD_EG{v}", "PAR_PITCH_SLOPE{v}", "PAR_MODAMNT{v}", "PAR_NONE",
     "PAR_VEL_DEST_{v}", "PAR_VELO_MOD_AMT_{v}", "PAR_VOLUME_MOD_ON_OFF{v}"),
    ("PAR_FMAMNT{v}", "PAR_FM_FREQ{v}", "PAR_MOD_WAVE_DRUM{v}",
     "PAR_MIX_MOD_{v}"),
)
_V02_PAGE_TAIL = (
    ("PAR_TRANS{v}_WAVE", "PAR_TRANS{v}_VOL", "PAR_TRANS{v}_FREQ"),
    ("PAR_FILTER_FREQ_{v}", "PAR_RESO_{v}", "PAR_FILTER_TYPE_{v}",
     "PAR_FILTER_DRIVE_{v}"),
    ("PAR_FREQ_LFO{v}", "PAR_SYNC_LFO{v}", "PAR_AMOUNT_LFO{v}",
     "PAR_WAVE_LFO{v}", "PAR_RETRIGGER_LFO{v}", "PAR_OFFSET_LFO{v}",
     "PAR_VOICE_LFO{v}", "PAR_TARGET_LFO{v}"),
    ("PAR_VOL{v}", "PAR_PAN{v}", "PAR_VOICE_DECIMATION{v}", "{drive}",
     "PAR_AUDIO_OUT{v}", "PAR_TRACK_LENGTH", "PAR_MIDI_CHAN_{v}"),
)
_V02_HEAD_BY_VOICE = {
    4: (("PAR_COARSE4", "PAR_FINE4", "PAR_NOISE_FREQ1", "PAR_MIX1",
         "PAR_OSC_WAVE_SNARE"),
        ("PAR_VELOA4", "PAR_VELOD4", "PAR_REPEAT4", "PAR_VOL_SLOPE4"),
        ("PAR_MOD_EG4", "PAR_PITCH_SLOPE4", "PAR_MODAMNT4", "PAR_NONE",
         "PAR_VEL_DEST_4", "PAR_VELO_MOD_AMT_4", "PAR_VOLUME_MOD_ON_OFF4"),
        ()),
    5: (("PAR_COARSE5", "PAR_FINE5", "PAR_WAVE1_CYM"),
        ("PAR_VELOA5", "PAR_VELOD5", "PAR_REPEAT5", "PAR_VOL_SLOPE5"),
        ("PAR_VEL_DEST_5", "PAR_VELO_MOD_AMT_5", "PAR_VOLUME_MOD_ON_OFF5"),
        ("PAR_MOD_OSC_F1_CYM", "PAR_MOD_OSC_F2_CYM", "PAR_MOD_OSC_GAIN1_CYM",
         "PAR_MOD_OSC_GAIN2_CYM", "PAR_WAVE2_CYM", "PAR_WAVE3_CYM")),
    6: (("PAR_COARSE6", "PAR_FINE6", "PAR_WAVE1_HH"),
        ("PAR_VELOA6", "PAR_VELOD6_CLOSED", "PAR_VELOD6_OPEN",
         "PAR_VOL_SLOPE6"),
        ("PAR_VEL_DEST_6", "PAR_VELO_MOD_AMT_6", "PAR_VOLUME_MOD_ON_OFF6"),
        ("PAR_MOD_OSC_F1", "PAR_MOD_OSC_F2", "PAR_MOD_OSC_GAIN1",
         "PAR_MOD_OSC_GAIN2", "PAR_WAVE2_HH", "PAR_WAVE3_HH")),
}
_V02_DRIVE = {1: "PAR_DRIVE1", 2: "PAR_DRIVE2", 3: "PAR_DRIVE3",
              4: "PAR_SNARE_DISTORTION", 5: "PAR_CYMBAL_DISTORTION",
              6: "PAR_HAT_DISTORTION"}


def _v02_voice_pages(voice: int) -> list[tuple[str, ...]]:
    """menuPages[voice - 1][0..7] parameter rows, padded to 8 with PAR_NONE."""
    head = (_V02_HEAD_BY_VOICE[voice] if voice in _V02_HEAD_BY_VOICE else
            tuple(tuple(n.format(v=voice) for n in row)
                  for row in _V02_PAGE_TEMPLATE))
    tail = tuple(tuple(n.format(v=voice, drive=_V02_DRIVE[voice]) for n in row)
                 for row in _V02_PAGE_TAIL)
    return [row + ("PAR_NONE",) * (8 - len(row)) for row in head + tail]


LXR1_V02_VOICE_PAGES = {voice: _v02_voice_pages(voice) for voice in range(1, 7)}
assert LXR1_V02_VOICE_PAGES[1][5][0] == "PAR_FILTER_FREQ_1"
assert LXR1_V02_VOICE_PAGES[6][1][2] == "PAR_VELOD6_OPEN"
assert LXR1_V02_VOICE_PAGES[4][7][3] == "PAR_SNARE_DISTORTION"

TARGET_ENCODINGS = ("pages", "modtargets")


def decode_target(encoding: str, value: int, page_voice: int) -> str | None:
    """Return the PAR_* name an LXR1 kit target byte points at, or None.

    ``page_voice`` is the voice whose menu pages a ``pages`` byte indexes
    (the owning voice for velocity, PAR_VOICE_LFOn for LFO).
    """
    if encoding == "modtargets":
        if value == 0 or value >= len(LXR1_MOD_TARGETS):
            return None
        return LXR1_MOD_TARGETS[value]
    if value == 0 or not 1 <= page_voice <= 6:
        return None
    page, entry = value >> 3, value & 0x07
    if page >= 8:
        return None
    name = LXR1_V02_VOICE_PAGES[page_voice][page][entry]
    return None if name == "PAR_NONE" else name


# ---------------------------------------------------------------------------
# Kit slot layout and LXR1 -> Helicase instrument key map.
# ---------------------------------------------------------------------------
INSTRUMENT_SLOT_COUNT = 6
NUM_TRACKS = 7
INSTRUMENT_PARAM_COUNT = 64
TARGET_TOKEN_OFF = 0xFF

# (type extension, short filename suffix) per one-based slot. The six-char
# filename prefix plus this suffix keeps every stem within eight characters.
SLOT_TYPES = {1: ("drm", "d1"), 2: ("drm", "d2"), 3: ("drm", "d3"),
              4: ("snr", "s1"), 5: ("cym", "c1"), 6: ("hat", "h1")}

DESCRIPTOR_SOURCES = {
    "drm": ("Drum/DrumParameters.c", "drum_param_descriptors"),
    "snr": ("Snare/SnareParameters.c", "snare_param_descriptors"),
    "cym": ("Cymbal/CymbalParameters.c", "cymbal_param_descriptors"),
    "hat": ("HiHat/HiHatParameters.c", "hihat_param_descriptors"),
}

# Helicase keys that LXR1 has no byte for, with the value that reproduces the
# LXR1 sound. lfo_polarity 0 = `neg`, the original-LXR value-relative LFO law
# (FILESYSTEM_SPEC.md "Current LFO shape"); the second LFO target is unused.
HELICASE_ONLY_DEFAULTS = {
    "lfo_amount_2": 0,
    "lfo_polarity": 0,
    "lfo_target_voice_2": "self",
    "lfo_target_param_2": TARGET_TOKEN_OFF,
}

# Keys converted by target translation rather than copied.
TARGET_KEYS = ("velo_mod_dest", "lfo_target_voice", "lfo_target_param")


def _common_voice_keys(v: int) -> dict[str, str]:
    """Rows every LXR1 voice stores under the same per-voice PAR_* family."""
    return {
        "filter_freq": f"PAR_FILTER_FREQ_{v}",
        "filter_reso": f"PAR_RESO_{v}",
        "filter_drive": f"PAR_FILTER_DRIVE_{v}",
        "filter_type": f"PAR_FILTER_TYPE_{v}",
        "amp_envelope_attack": f"PAR_VELOA{v}",
        "amp_envelope_slope": f"PAR_VOL_SLOPE{v}",
        "instrument_vol": f"PAR_VOL{v}",
        "instrument_pan": f"PAR_PAN{v}",
        "instrument_decimation": f"PAR_VOICE_DECIMATION{v}",
        "lfo_rate": f"PAR_FREQ_LFO{v}",
        "lfo_amount": f"PAR_AMOUNT_LFO{v}",
        "lfo_wave": f"PAR_WAVE_LFO{v}",
        "lfo_retrigger_voice": f"PAR_RETRIGGER_LFO{v}",
        "lfo_sync": f"PAR_SYNC_LFO{v}",
        "lfo_offset": f"PAR_OFFSET_LFO{v}",
        "velo_vol_on_off": f"PAR_VOLUME_MOD_ON_OFF{v}",
        "velo_mod_amount": f"PAR_VELO_MOD_AMT_{v}",
        "transient_wave": f"PAR_TRANS{v}_WAVE",
        "transient_vol": f"PAR_TRANS{v}_VOL",
        "transient_freq": f"PAR_TRANS{v}_FREQ",
    }


def _slot_key_map(slot: int) -> dict[str, str]:
    """Return Helicase key -> LXR1 PAR_* name for one Kit slot (1..6)."""
    keys = _common_voice_keys(slot)
    if slot <= 3:
        keys.update({
            "osc1_wave": f"PAR_OSC_WAVE_DRUM{slot}",
            "osc1_pitch_coarse": f"PAR_COARSE{slot}",
            "osc1_pitch_fine": f"PAR_FINE{slot}",
            "osc2_wave": f"PAR_MOD_WAVE_DRUM{slot}",
            "osc2_pitch_coarse": f"PAR_FM_FREQ{slot}",
            "osc2_mod_amount": f"PAR_FMAMNT{slot}",
            "osc2_mod_type": f"PAR_MIX_MOD_{slot}",
            "amp_envelope_decay": f"PAR_VELOD{slot}",
            "pitch_envelope_decay": f"PAR_MOD_EG{slot}",
            "pitch_envelope_amount": f"PAR_MODAMNT{slot}",
            "pitch_envelope_slope": f"PAR_PITCH_SLOPE{slot}",
            "instrument_drive": f"PAR_DRIVE{slot}",
        })
    elif slot == 4:
        keys.update({
            "osc1_wave": "PAR_OSC_WAVE_SNARE",
            "osc1_pitch_coarse": "PAR_COARSE4",
            "osc1_pitch_fine": "PAR_FINE4",
            "noise_freq": "PAR_NOISE_FREQ1",
            "osc1_noise_mix": "PAR_MIX1",
            "amp_envelope_decay": "PAR_VELOD4",
            "amp_attack_repeat": "PAR_REPEAT4",
            "pitch_envelope_decay": "PAR_MOD_EG4",
            "pitch_envelope_amount": "PAR_MODAMNT4",
            "pitch_envelope_slope": "PAR_PITCH_SLOPE4",
            "instrument_drive": "PAR_SNARE_DISTORTION",
        })
    elif slot == 5:
        keys.update({
            "osc1_wave": "PAR_WAVE1_CYM",
            "osc1_pitch_coarse": "PAR_COARSE5",
            "osc1_pitch_fine": "PAR_FINE5",
            "osc2_wave": "PAR_WAVE2_CYM",
            "osc2_pitch_coarse": "PAR_MOD_OSC_F1_CYM",
            "osc2_mod_amount": "PAR_MOD_OSC_GAIN1_CYM",
            "osc3_wave": "PAR_WAVE3_CYM",
            "osc3_pitch_coarse": "PAR_MOD_OSC_F2_CYM",
            "osc3_mod_amount": "PAR_MOD_OSC_GAIN2_CYM",
            "amp_envelope_decay": "PAR_VELOD5",
            "amp_attack_repeat": "PAR_REPEAT5",
            "instrument_drive": "PAR_CYMBAL_DISTORTION",
        })
    else:
        keys.update({
            "osc1_wave": "PAR_WAVE1_HH",
            "osc1_pitch_coarse": "PAR_COARSE6",
            "osc1_pitch_fine": "PAR_FINE6",
            "osc2_wave": "PAR_WAVE2_HH",
            "osc2_pitch_coarse": "PAR_MOD_OSC_F1",
            "osc2_mod_amount": "PAR_MOD_OSC_GAIN1",
            "osc3_wave": "PAR_WAVE3_HH",
            "osc3_pitch_coarse": "PAR_MOD_OSC_F2",
            "osc3_mod_amount": "PAR_MOD_OSC_GAIN2",
            "amp_envelope_decay": "PAR_VELOD6_CLOSED",
            # HiHat is the Choke instrument: track 7 plays this row.
            "amp_envelope_decay_choke": "PAR_VELOD6_OPEN",
            "instrument_drive": "PAR_HAT_DISTORTION",
        })
    return keys


SLOT_KEY_MAP = {slot: _slot_key_map(slot) for slot in range(1, 7)}

# Reverse map used for targets and step automation: PAR_* -> (slot, key).
PAR_TO_SLOT_KEY: dict[str, tuple[int, str]] = {
    par: (slot, key)
    for slot, keys in SLOT_KEY_MAP.items()
    for key, par in keys.items()
}


# ---------------------------------------------------------------------------
# Firmware descriptor tables.
# ---------------------------------------------------------------------------
@dataclass(frozen=True)
class Descriptor:
    key: str
    macro: str

    @property
    def image(self) -> bool:
        """True for FLAGS_IMAGE rows: morphable, modulatable, automatable.

        Every descriptor macro except ROW_NOBIND carries FLAGS_IMAGE
        (DrumParameters.c macro block); ROW_NOBIND rows are the single-endpoint
        target selectors.
        """
        return self.macro != "ROW_NOBIND"


def load_descriptor_tables(firmware_root: Path) -> dict[str, list[Descriptor]]:
    """Parse the four instrument descriptor arrays in declaration order."""
    tables: dict[str, list[Descriptor]] = {}
    base = firmware_root / "Core" / "DSP" / "Instruments"
    for type_ext, (relative, array_name) in DESCRIPTOR_SOURCES.items():
        path = base / relative
        if not path.is_file():
            raise SystemExit(
                f"firmware descriptor table not found: {path}\n"
                "pass --firmware-root <Helicase repository root>")
        text = re.sub(r"/\*.*?\*/", "", path.read_text(encoding="utf-8"),
                      flags=re.S)
        match = re.search(
            r"const\s+ParamDescriptor\s+" + array_name +
            r"\s*\[\s*\]\s*=\s*\{(.*?)\n\};", text, flags=re.S)
        if not match:
            raise SystemExit(f"{array_name}[] not found in {path}")
        rows = [Descriptor(key, macro) for macro, key in re.findall(
            r'^\s*(ROW\w*)\(\s*"([a-z0-9_]+)"', match.group(1), flags=re.M)]
        if not rows or len(rows) > INSTRUMENT_PARAM_COUNT:
            raise SystemExit(f"unexpected descriptor count {len(rows)} in {path}")
        tables[type_ext] = rows
    return tables


class Firmware:
    """Descriptor lookups derived from one Helicase source tree."""

    def __init__(self, firmware_root: Path):
        self.root = firmware_root
        self.tables = load_descriptor_tables(firmware_root)
        self.index = {
            type_ext: {d.key: i for i, d in enumerate(rows)}
            for type_ext, rows in self.tables.items()
        }
        self._check_key_coverage()

    def _check_key_coverage(self) -> None:
        """Fail if the key map and the firmware descriptors disagree."""
        for slot, (type_ext, _) in SLOT_TYPES.items():
            firmware_keys = set(self.index[type_ext])
            converted = (set(SLOT_KEY_MAP[slot]) | set(HELICASE_ONLY_DEFAULTS) |
                         set(TARGET_KEYS))
            missing = sorted(firmware_keys - converted)
            unknown = sorted(converted - firmware_keys)
            if missing or unknown:
                raise SystemExit(
                    f"slot {slot} ({type_ext}) key map is out of date with the "
                    f"firmware descriptors: missing={missing} unknown={unknown}")

    def descriptor(self, slot: int, key: str) -> tuple[int, Descriptor]:
        type_ext = SLOT_TYPES[slot][0]
        i = self.index[type_ext][key]
        return i, self.tables[type_ext][i]

    def automation_id(self, dest: int) -> int | None:
        """Map an LXR1 step-automation destination to an instrument_param_id_t.

        The mainboard stores parameters below 128 one higher than the front's
        ParamEnums number (its CC numbering: frontPanelParser.c
        FRONT_SET_P1_DEST "val++"); 128 and above are stored unchanged. 128
        itself can only be PAR_FILTER_DRIVE_1, because PAR_BANK_CHANGE (127)
        is never an automation target.
        """
        par_value = dest - 1 if dest < 128 else dest
        name = LXR1_NAME_BY_VALUE.get(par_value)
        if name is None or name not in PAR_TO_SLOT_KEY:
            return None
        slot, key = PAR_TO_SLOT_KEY[name]
        index, desc = self.descriptor(slot, key)
        if not desc.image:
            return None
        return (slot - 1) * INSTRUMENT_PARAM_COUNT + index


# ---------------------------------------------------------------------------
# Diagnostics.
# ---------------------------------------------------------------------------
class Report:
    """Collects per-file notes so conversions explain every approximation."""

    def __init__(self) -> None:
        self.notes: list[str] = []

    def note(self, text: str) -> None:
        self.notes.append(text)

    def flush(self, prefix: str) -> None:
        for text in self.notes:
            print(f"  {prefix}: {text}")
        self.notes.clear()


# ---------------------------------------------------------------------------
# Names and library slots.
# ---------------------------------------------------------------------------
DISPLAY_NAME_MAX = 8
NUMBERED_RE = re.compile(r"^(\d{3})[ _]")


def lxr1_display_name(raw: bytes, fallback: str) -> str:
    """Printable LXR1 8-byte name with trailing padding removed."""
    text = "".join(chr(b) if 0x20 <= b <= 0x7E else " " for b in raw[:8])
    return text.strip() or fallback


def safe_name(name: str, fallback: str, limit: int = DISPLAY_NAME_MAX) -> str:
    """Display name usable as an eight-character FAT/LFN component.

    Characters outside letters, digits, space, '-' and '_' become spaces
    (``Moch.to.`` -> ``Moch to``), runs collapse, and the result is trimmed to
    ``limit`` characters, matching the firmware's canonical repair rule.
    """
    cleaned = re.sub(r"[^A-Za-z0-9 _-]", " ", name)
    cleaned = re.sub(r"\s+", " ", cleaned).strip()
    cleaned = cleaned[:limit].rstrip()
    return cleaned or fallback[:limit]


def numbered_name(base: str, number: int, limit: int = DISPLAY_NAME_MAX) -> str:
    """``base`` shortened so ``"<base> <number>"`` fits ``limit`` characters."""
    suffix = f" {number}"
    return base[:limit - len(suffix)].rstrip() + suffix


def instrument_prefix(name: str, fallback: str) -> str:
    cleaned = re.sub(r"[^A-Za-z0-9]", "", name).lower()
    return (cleaned or fallback.lower())[:6]


def lxr1_file_number(path: Path) -> int | None:
    match = re.match(r"^[pP](\d{3})$", path.stem)
    return int(match.group(1)) if match else None


def occupied_slots(dest: Path, suffix: str | None = None) -> dict[int, list[Path]]:
    """Map library slot -> existing entries (``NNN name`` dirs or files)."""
    slots: dict[int, list[Path]] = {}
    if not dest.is_dir():
        return slots
    for entry in dest.iterdir():
        if entry.name.startswith("."):
            continue
        if suffix is None and not entry.is_dir():
            continue
        if suffix is not None and entry.suffix.lower() != suffix:
            continue
        match = NUMBERED_RE.match(entry.name)
        if match:
            slots.setdefault(int(match.group(1)), []).append(entry)
    return slots


class SlotAllocator:
    """Hands out library slots 000..999 without touching existing entries.

    Default: the lowest free slot at or after ``start``. With ``fixed`` the
    caller names the slot (LXR1 file number); an occupied slot is refused
    unless ``overwrite`` is set, in which case the old entries are removed.
    """

    def __init__(self, dest: Path, start: int, overwrite: bool,
                 dry_run: bool, suffix: str | None = None):
        self.dest = dest
        self.next = start
        self.overwrite = overwrite
        self.dry_run = dry_run
        self.taken = occupied_slots(dest, suffix)
        self.used: set[int] = set()

    def allocate(self, fixed: int | None = None) -> int:
        if fixed is not None:
            if fixed in self.used:
                raise SystemExit(f"slot {fixed:03d} assigned twice in this run")
            if fixed in self.taken:
                if not self.overwrite:
                    names = ", ".join(p.name for p in self.taken[fixed])
                    raise SystemExit(
                        f"slot {fixed:03d} is occupied ({names}); "
                        "pass --overwrite to replace it")
                for old in self.taken.pop(fixed):
                    print(f"  replacing {old}")
                    if not self.dry_run:
                        if old.is_dir():
                            shutil.rmtree(old)
                        else:
                            old.unlink()
            self.used.add(fixed)
            return fixed
        while self.next in self.taken or self.next in self.used:
            self.next += 1
        if self.next > 999:
            raise SystemExit(f"no free library slot left in {self.dest}")
        self.used.add(self.next)
        return self.next


def convert_each(files: list[Path], convert, report: "Report") -> int:
    """Run ``convert(path)`` for every input; return the failure count.

    A malformed or unsupported file (ValueError) or an I/O error is reported
    with its name and skipped, so one bad file never stops a card-sized
    batch. Notes collected in ``report`` are printed under the file's name.
    """
    failures = 0
    for path in files:
        try:
            convert(path)
        except (ValueError, OSError) as error:
            print(f"ERROR {path.name}: {error}")
            failures += 1
        report.flush(path.name)
    return failures


def collect_inputs(paths: list[Path], extensions: tuple[str, ...]) -> list[Path]:
    """Expand files and directories into sorted LXR1 files (case-insensitive)."""
    found: list[Path] = []
    for path in paths:
        if path.is_dir():
            found.extend(p for p in sorted(path.iterdir())
                         if p.is_file() and p.suffix.lower() in extensions
                         and not p.name.startswith("._"))
        elif path.is_file():
            found.append(path)
        else:
            raise SystemExit(f"input not found: {path}")
    if not found:
        raise SystemExit("no LXR1 input files found")
    return found


# ---------------------------------------------------------------------------
# LXR1 readers.
# ---------------------------------------------------------------------------
@dataclass
class Lxr1Kit:
    name: str
    params: bytes                     # exactly LXR1_SOUND_PARAM_COUNT bytes
    target_encoding: str              # "pages" (2013) or "modtargets"

    def value(self, par_name: str) -> int:
        return self.params[LXR1_PAR[par_name]]


# Kit payload sizes written by the LXR firmware generations.
V01_SOUND_PARAM_COUNT = 225           # V0.1x: different layout, refused
V02_SOUND_PARAM_COUNT = 221           # V0.2x: final layout, page targets


def kit_from_bytes(name: str, raw: bytes, encoding: str) -> Lxr1Kit:
    """Apply preset_readDrumsetData(): zero-fill short data, drop extra."""
    params = bytes(raw[:LXR1_SOUND_PARAM_COUNT]).ljust(LXR1_SOUND_PARAM_COUNT, b"\0")
    return Lxr1Kit(name, params, encoding)


def snd_target_encoding(payload_len: int, override: str) -> str:
    """Pick the kit target encoding from the payload length (or the override).

    228+ bytes: modTargets (Nov 2013 onward). Shorter: the V0.2x page
    encoding. 225 bytes is the pre-release V0.1x layout, whose byte offsets
    differ from every later firmware, so it is refused rather than misread.
    """
    if payload_len == V01_SOUND_PARAM_COUNT and override == "auto":
        raise ValueError("225-byte kit payload is the LXR V0.1x layout "
                         "(different parameter offsets); not supported")
    if override != "auto":
        return override
    return "modtargets" if payload_len >= LXR1_SOUND_PARAM_COUNT else "pages"


def read_snd(path: Path, encoding: str = "auto") -> Lxr1Kit:
    data = path.read_bytes()
    if len(data) < 8:
        raise ValueError(f"{path} is shorter than the 8-byte kit name")
    payload = data[8:]
    return kit_from_bytes(lxr1_display_name(data[:8], path.stem), payload,
                          snd_target_encoding(len(payload), encoding))


STEP_BYTES = 7
STEPS_PER_TRACK = 128
NUM_LXR1_PATTERNS = 8
SUBSTEPS_PER_MAIN = 8
STEP_COUNT = STEPS_PER_TRACK * NUM_LXR1_PATTERNS * NUM_TRACKS
PATTERN_SET_MIN_BYTES = STEP_COUNT * STEP_BYTES + 56 * 2 + 8 * 2 + 1
PATTERN_SET_BYTES = PATTERN_SET_MIN_BYTES + 56
NO_AUTOMATION = 0xFF


@dataclass
class Lxr1Step:
    volume: int
    prob: int
    note: int
    p1_dest: int
    p1_val: int
    p2_dest: int
    p2_val: int

    @property
    def active(self) -> bool:
        return bool(self.volume & 0x80)


@dataclass
class Lxr1Pattern:
    steps: list[list[Lxr1Step]]       # [track][128 sub-steps]
    main_masks: list[int]             # [track]
    next_pattern: int
    change_bar: int
    length_rotate: list[int | None]   # [track]; None if the file lacks it


@dataclass
class Lxr1PatternSet:
    patterns: list[Lxr1Pattern]
    shuffle: int
    has_lengths: bool


def read_pattern_set(data: bytes, offset: int, origin: str) -> Lxr1PatternSet:
    """Decode one preset_writePatternData() block starting at ``offset``."""
    available = len(data) - offset
    if available < PATTERN_SET_MIN_BYTES:
        raise ValueError(
            f"{origin}: pattern data is {available} bytes, expected at least "
            f"{PATTERN_SET_MIN_BYTES}")
    steps = [[[None] * STEPS_PER_TRACK for _ in range(NUM_TRACKS)]
             for _ in range(NUM_LXR1_PATTERNS)]
    pos = offset
    for i in range(STEP_COUNT):
        abs_pat = i // STEPS_PER_TRACK
        track, pattern = divmod(abs_pat, NUM_LXR1_PATTERNS)
        steps[pattern][track][i % STEPS_PER_TRACK] = Lxr1Step(
            *data[pos:pos + STEP_BYTES])
        pos += STEP_BYTES
    masks = struct.unpack_from("<56H", data, pos)
    pos += 112
    info = data[pos:pos + 16]
    pos += 16
    shuffle = data[pos]
    pos += 1
    has_lengths = len(data) - pos >= 56
    lengths = list(data[pos:pos + 56]) if has_lengths else [None] * 56
    patterns = []
    for p in range(NUM_LXR1_PATTERNS):
        patterns.append(Lxr1Pattern(
            steps=steps[p],
            main_masks=[masks[p * NUM_TRACKS + t] for t in range(NUM_TRACKS)],
            next_pattern=info[2 * p],
            change_bar=info[2 * p + 1],
            length_rotate=[lengths[p * NUM_TRACKS + t] for t in range(NUM_TRACKS)],
        ))
    return Lxr1PatternSet(patterns, shuffle & 0x7F, has_lengths)


def read_pat(path: Path) -> tuple[str, Lxr1PatternSet]:
    data = path.read_bytes()
    name = lxr1_display_name(data[:8], path.stem)
    return name, read_pattern_set(data, 8, str(path))


@dataclass
class Lxr1Performance:
    """Kit, patterns and Scene-level settings of an ``.all`` or ``.prf``.

    Only values that are Scene settings in Helicase are kept: the seven track
    MIDI channels (``.all`` only; 0 = not stored). True globals (BPM, bar
    reset, MIDI routing/filters, clock, quantisation, ...) are skipped.
    """
    name: str
    kind: str                          # "all" or "prf"
    version: int
    kit: Lxr1Kit
    patterns: Lxr1PatternSet
    midi_channels: list[int]           # [track] 1..16, 0 = not stored


LXR1_FILE_VERSION_MAX = 2
SETTINGS_BLOCK = 64
KIT_BLOCK = 512
LXR1_MIDI_CHANNEL_PARS = [f"PAR_MIDI_CHAN_{t}" for t in range(1, NUM_TRACKS + 1)]


def read_performance(path: Path, encoding: str = "auto") -> Lxr1Performance:
    """Decode preset_saveAll() output for ``.all`` and ``.prf`` files.

    Both formats were added with the modTargets encoding (Nov 2013 and
    later), so ``auto`` means modTargets here.
    """
    data = path.read_bytes()
    kind = path.suffix.lower().lstrip(".")
    if len(data) < 9:
        raise ValueError(f"{path} is too short")
    name = lxr1_display_name(data[:8], path.stem)
    version = data[8]
    if version > LXR1_FILE_VERSION_MAX:
        raise ValueError(f"{path}: file version {version} is newer than "
                         f"{LXR1_FILE_VERSION_MAX}")
    channels = [0] * NUM_TRACKS
    if kind == "all":
        # preset_writeGlobalData(): one byte per ParamEnums value from
        # PAR_BEGINNING_OF_GLOBALS; unused bytes of the 64 are 0xFF padding.
        first = LXR1_PAR["PAR_BEGINNING_OF_GLOBALS"]
        for t, par in enumerate(LXR1_MIDI_CHANNEL_PARS):
            value = data[9 + LXR1_PAR[par] - first]
            channels[t] = value if 1 <= value <= 16 else 0
    # Every .all, and a version 2 .prf, pads its settings to 64 bytes; a
    # version 1 .prf holds only the BPM byte.
    pos = 9 + (SETTINGS_BLOCK if kind == "all" or version > 1 else 1)
    kit_raw = bytearray(data[pos:pos + LXR1_SOUND_PARAM_COUNT])
    # A file saved by firmware with fewer sound parameters pads the kit block
    # with 0xFF; the only such bytes are the trailing MIDI notes.
    for i in range(LXR1_PAR["PAR_MIDI_NOTE1"], len(kit_raw)):
        if kit_raw[i] == 0xFF:
            kit_raw[i] = 0
    kit = kit_from_bytes(name, bytes(kit_raw),
                         "modtargets" if encoding == "auto" else encoding)
    patterns = read_pattern_set(data, pos + KIT_BLOCK, str(path))
    return Lxr1Performance(name, kind, version, kit, patterns, channels)


def scene_settings_from_kit(kit: Lxr1Kit) -> tuple[list[int], list[int]]:
    """(audio_out[6], midi_note[7]) Scene values carried by an LXR1 kit.

    Audio out 0..5 = St1 St2 L1 R1 L2 R2 in both systems. An LXR1 track note
    1..127 (the fixed note the track sends) becomes the Helicase track note
    root; 0 (play the step note) becomes `on`, the Helicase default.
    """
    audio_out = []
    for slot in range(1, INSTRUMENT_SLOT_COUNT + 1):
        route = kit.value(f"PAR_AUDIO_OUT{slot}")
        audio_out.append(route if route <= 5 else 0)
    notes = []
    for track in range(1, NUM_TRACKS + 1):
        note = kit.value(f"PAR_MIDI_NOTE{track}")
        notes.append(note if 1 <= note <= 127 else 0)
    return audio_out, notes


# ---------------------------------------------------------------------------
# Kit / Instrument writers.
# ---------------------------------------------------------------------------
def convert_targets(kit: Lxr1Kit, slot: int, fw: Firmware,
                    report: Report) -> dict[str, int | str]:
    """Translate LXR1 kit target bytes into Helicase compact tokens.

    The byte is decoded with the kit's encoding (``decode_target``) into the
    PAR_* it addresses; that parameter's Kit slot becomes the destination
    voice and its descriptor index on that slot becomes the token. LFO
    targets may point at any voice. Velocity targets: Helicase's picker is
    self-scoped, so a target on another voice becomes off.
    """
    out: dict[str, int | str] = {}
    lfo_voice = kit.value(f"PAR_VOICE_LFO{slot}")

    def resolve(value: int, page_voice: int,
                what: str) -> tuple[int, int] | None:
        par = decode_target(kit.target_encoding, value, page_voice)
        if par is None:
            return None
        if par not in PAR_TO_SLOT_KEY:
            report.note(f"voice {slot} {what} target {par} has no Helicase "
                        "Instrument parameter; set to off")
            return None
        target_slot, key = PAR_TO_SLOT_KEY[par]
        local, desc = fw.descriptor(target_slot, key)
        if not desc.image:
            report.note(f"voice {slot} {what} target {key} is not a "
                        "modulation target in Helicase; set to off")
            return None
        return target_slot, local

    velo = resolve(kit.value(f"PAR_VEL_DEST_{slot}"), slot, "velocity")
    if velo is not None and velo[0] != slot:
        report.note(f"voice {slot} velocity target is on voice {velo[0]}; "
                    "Helicase velocity targets are voice-local, set to off")
        velo = None
    out["velo_mod_dest"] = velo[1] if velo else TARGET_TOKEN_OFF

    lfo = resolve(kit.value(f"PAR_TARGET_LFO{slot}"), lfo_voice, "LFO")
    if lfo is not None:
        target_voice = lfo[0]
        out["lfo_target_param"] = lfo[1]
    else:
        target_voice = lfo_voice
        if not 1 <= target_voice <= INSTRUMENT_SLOT_COUNT:
            target_voice = slot
        out["lfo_target_param"] = TARGET_TOKEN_OFF
    out["lfo_target_voice"] = "self" if target_voice == slot else target_voice
    return out


def instrument_values(kit: Lxr1Kit, slot: int, fw: Firmware,
                      report: Report) -> list[tuple[str, int | str, bool]]:
    """(key, value, morphable) rows for one slot in firmware descriptor order."""
    keys = SLOT_KEY_MAP[slot]
    targets = convert_targets(kit, slot, fw, report)
    rows = []
    for desc in fw.tables[SLOT_TYPES[slot][0]]:
        if desc.key in targets:
            value: int | str = targets[desc.key]
        elif desc.key in HELICASE_ONLY_DEFAULTS:
            value = HELICASE_ONLY_DEFAULTS[desc.key]
        else:
            value = kit.value(keys[desc.key])
        rows.append((desc.key, value, desc.image))
    return rows


def write_kit_dir(kit_dir: Path, kit: Lxr1Kit, fw: Firmware,
                  report: Report) -> None:
    """Write ``kitset.kcg`` plus six instrument files (FILESYSTEM_SPEC Kit).

    Both endpoints carry the LXR1 values ([morph] = [params]), so the Kit
    sounds identical at any Morph amount.
    """
    kit_dir.mkdir(parents=True, exist_ok=True)
    prefix = instrument_prefix(kit.name, "kit")
    open_decay = kit.value("PAR_VELOD6_OPEN")
    kitset = [
        "format=helicase.kitset",
        "version=1",
        f"slot6_track7_amp_envelope_decay={open_decay}",
        f"slot6_track7_morph_amp_envelope_decay={open_decay}",
        "",
    ]
    for slot in range(1, INSTRUMENT_SLOT_COUNT + 1):
        type_ext, suffix = SLOT_TYPES[slot]
        filename = f"{prefix}{suffix}.{type_ext}"
        rows = instrument_values(kit, slot, fw, report)
        lines = ["format=helicase.instrument", "version=1",
                 f"type={type_ext}", "", "[params]"]
        lines += [f"{key}={value}" for key, value, _ in rows]
        lines += ["", "[morph]"]
        lines += [f"{key}={value}" for key, value, morph in rows if morph]
        lines.append("")
        (kit_dir / filename).write_text("\n".join(lines), encoding="ascii")
        kitset += [f"[slot{slot}]", f"type={type_ext}", f"file={filename}", ""]
    (kit_dir / "kitset.kcg").write_text("\n".join(kitset), encoding="ascii")


# ---------------------------------------------------------------------------
# PAT4.
# ---------------------------------------------------------------------------
PAT4_VERSION = 2
PAT_STACK_SIZE = 256
PAT_HEADER_BYTES = 160
PAT_ADDRESS_BYTES = NUM_TRACKS * STEPS_PER_TRACK * 2
PAT_BITMAP_BYTES = 512
PAT_POOL_BYTES = PAT_STACK_SIZE * 32
PAT_FILE_BYTES = PAT_HEADER_BYTES + PAT_ADDRESS_BYTES + PAT_BITMAP_BYTES + PAT_POOL_BYTES
PAT_CRC_OFFSET = 14
PAT_ADDR_SENTINEL = 0x3FFF
PAT_ADDR_TRIGGER = 1 << 15
PAT_ADDR_SPECIALS = 1 << 14
PAT_POOL_ALLOC_BYTES = 8056          # pool below the 136-byte swap block
STEP_SCALE_1_16 = 76
assert PAT_FILE_BYTES == 10656

SPECIAL_ORDER = ("note", "velocity", "condition", "roll", "timing", "rhythm",
                 "fill")
SPECIAL_DEFAULTS = {"note": 63, "velocity": 100, "condition": 0, "roll": 0,
                    "timing": 0, "rhythm": 0, "fill": 100}


@dataclass
class HelicaseStep:
    trigger: bool = False
    specials: dict[str, int] = field(default_factory=dict)
    automation: list[tuple[int, int]] = field(default_factory=list)


@dataclass
class HelicasePattern:
    steps: list[list[HelicaseStep]]          # [track][128]
    length: list[int]
    scale: list[int]
    shuffle: list[int]

    @classmethod
    def empty(cls) -> "HelicasePattern":
        return cls([[HelicaseStep() for _ in range(STEPS_PER_TRACK)]
                    for _ in range(NUM_TRACKS)],
                   [16] * NUM_TRACKS, [STEP_SCALE_1_16] * NUM_TRACKS,
                   [0] * NUM_TRACKS)

    def has_triggers(self) -> bool:
        return any(step.trigger for track in self.steps for step in track)


def crc32c(data: bytes) -> int:
    crc = 0xFFFFFFFF
    for value in data:
        crc ^= value
        for _ in range(8):
            crc = (crc >> 1) ^ 0x82F63B78 if crc & 1 else crc >> 1
    return crc ^ 0xFFFFFFFF


def build_pat4(pattern: HelicasePattern) -> bytes:
    """Serialise one Pattern exactly like the firmware PAT4 writer."""
    header = bytearray(PAT_HEADER_BYTES)
    header[0:4] = b"PAT4"
    struct.pack_into("<HHH", header, 4, PAT4_VERSION, PAT_STACK_SIZE,
                     PAT_HEADER_BYTES)
    for t in range(NUM_TRACKS):
        base = 48 + 16 * t
        header[base] = pattern.length[t]
        header[base + 1] = pattern.scale[t]
        header[base + 2] = pattern.shuffle[t]
        # bytes 3..5: run mode fwd, loopable, `rmp` off; 6..15 reserved.

    address = [PAT_ADDR_SENTINEL] * (NUM_TRACKS * STEPS_PER_TRACK)
    pool = bytearray(PAT_POOL_BYTES)
    used = 0
    for t in range(NUM_TRACKS):
        for s, step in enumerate(pattern.steps[t]):
            specials = {k: v for k, v in step.specials.items()
                        if v != SPECIAL_DEFAULTS[k]}
            entry = PAT_ADDR_TRIGGER if step.trigger else 0
            if specials or step.automation:
                flags = 0
                values = []
                for bit, name in enumerate(SPECIAL_ORDER):
                    if name in specials:
                        flags |= 1 << bit
                        values.append(specials[name])
                body = bytearray()
                head = ((t * STEPS_PER_TRACK + s) << 6) | len(step.automation)
                body += bytes([head >> 8, head & 0xFF, flags]) + bytes(values)
                for target, value in step.automation:
                    body += struct.pack("<H", (value << 9) | target)
                size = (len(body) + 3) & ~3
                if used + size > PAT_POOL_ALLOC_BYTES:
                    raise ValueError("Pattern pool is full")
                pool[used:used + len(body)] = body
                entry |= PAT_ADDR_SPECIALS | used
                used += size
            else:
                entry |= PAT_ADDR_SENTINEL
            address[t * STEPS_PER_TRACK + s] = entry

    bitmap = bytearray(PAT_BITMAP_BYTES)
    bitmap[256:] = b"\xFF" * 256
    for chunk in range(used // 4):
        bitmap[chunk >> 3] |= 1 << (chunk & 7)

    data = bytearray(header + struct.pack(f"<{len(address)}H", *address) +
                     bitmap + pool)
    struct.pack_into("<I", data, PAT_CRC_OFFSET, crc32c(bytes(data)))
    verify_pat4(bytes(data), pattern)
    return bytes(data)


def verify_pat4(data: bytes, expected: HelicasePattern) -> None:
    """Decode a PAT4 image with the reader rules and compare it to the source."""
    if len(data) != PAT_FILE_BYTES or data[:4] != b"PAT4":
        raise AssertionError("bad PAT4 size or magic")
    zeroed = bytearray(data)
    stored = struct.unpack_from("<I", data, PAT_CRC_OFFSET)[0]
    zeroed[PAT_CRC_OFFSET:PAT_CRC_OFFSET + 4] = b"\0\0\0\0"
    if crc32c(bytes(zeroed)) != stored:
        raise AssertionError("PAT4 CRC mismatch")
    addresses = struct.unpack_from(f"<{NUM_TRACKS * STEPS_PER_TRACK}H", data,
                                   PAT_HEADER_BYTES)
    bitmap = data[PAT_HEADER_BYTES + PAT_ADDRESS_BYTES:
                  PAT_HEADER_BYTES + PAT_ADDRESS_BYTES + PAT_BITMAP_BYTES]
    pool = data[PAT_FILE_BYTES - PAT_POOL_BYTES:]
    owned = set()
    for i, entry in enumerate(addresses):
        t, s = divmod(i, STEPS_PER_TRACK)
        want = expected.steps[t][s]
        if bool(entry & PAT_ADDR_TRIGGER) != want.trigger:
            raise AssertionError(f"trigger mismatch t{t} s{s}")
        if not entry & PAT_ADDR_SPECIALS:
            if entry & PAT_ADDR_SENTINEL != PAT_ADDR_SENTINEL:
                raise AssertionError(f"t{t} s{s}: offset without specials bit")
            continue
        off = entry & PAT_ADDR_SENTINEL
        if off & 3 or off >= PAT_POOL_ALLOC_BYTES:
            raise AssertionError(f"t{t} s{s}: bad pool offset {off}")
        head = pool[off] << 8 | pool[off + 1]
        if head >> 6 != i:
            raise AssertionError(f"t{t} s{s}: back-reference mismatch")
        count = head & 0x3F
        flags = pool[off + 2]
        if flags & 0x80:
            raise AssertionError(f"t{t} s{s}: unknown special flag")
        nspec = bin(flags).count("1")
        decoded = {}
        for bit, name in enumerate(SPECIAL_ORDER):
            if flags & (1 << bit):
                decoded[name] = pool[off + 3 + len(decoded)]
        autos = [struct.unpack_from("<H", pool, off + 3 + nspec + 2 * k)[0]
                 for k in range(count)]
        autos = [(a & 0x1FF, a >> 9) for a in autos]
        want_specials = {k: v for k, v in want.specials.items()
                         if v != SPECIAL_DEFAULTS[k]}
        if decoded != want_specials or autos != want.automation:
            raise AssertionError(f"t{t} s{s}: block content mismatch")
        if len({target for target, _ in autos}) != len(autos):
            raise AssertionError(f"t{t} s{s}: duplicate automation target")
        chunks = (3 + nspec + 2 * count + 3) // 4
        for c in range(off // 4, off // 4 + chunks):
            if c in owned or not bitmap[c >> 3] & (1 << (c & 7)):
                raise AssertionError(f"t{t} s{s}: bitmap/overlap error")
            owned.add(c)
    for c in range(2048):
        if (c in owned) != bool(bitmap[c >> 3] & (1 << (c & 7))):
            raise AssertionError(f"bitmap chunk {c} does not match the blocks")
    if bitmap[256:] != b"\xFF" * 256:
        raise AssertionError("bitmap upper half must be 0xFF")


# ---------------------------------------------------------------------------
# LXR1 pattern -> Helicase Pattern.
# ---------------------------------------------------------------------------
ROLL_INDEX_BY_SPACING = {1: 9, 2: 7, 4: 5}   # sub-steps -> `128`, `64`, `32`


def probability_to_condition(prob: int) -> int:
    """LXR1 step probability 0..127 -> Helicase condition 0 or r10..r95.

    Same buckets as tools/convert_pattern_specials.py (127 = always).
    """
    if prob >= 127:
        return 0
    for threshold, condition in ((114, 10), (102, 9), (89, 8), (76, 7),
                                 (64, 6), (51, 5), (38, 4), (25, 3), (13, 2)):
        if prob >= threshold:
            return condition
    return 1


def timing_for_substep(k: int) -> int:
    """Micro-timing percent that delays a 1/16 step by exactly k/8.

    The sequencer delays by floor(24 * timing / 100) ticks of 96 PPQ, so
    ceil(12.5 * k) lands on the 3-tick sub-step grid.
    """
    return int(math.ceil(k * 100 / SUBSTEPS_PER_MAIN))


def convert_pattern(src: Lxr1Pattern, shuffle: int, fw: Firmware,
                    partial_rolls: str, report: Report,
                    label: str) -> HelicasePattern:
    """Convert one of the eight LXR1 patterns into a Helicase Pattern.

    One LXR1 main step becomes one Helicase 1/16 step. Its eight sub-steps
    become a plain trigger (one sub-step; micro-timing if it is not the
    first) or a roll (sub-steps evenly spaced 1, 2 or 4 apart from the first
    sub-step to the end of the step: rate 128/64/32, fill 100). Any other
    shape keeps the first hit only (``partial_rolls == "first"``), or, for
    even spacing, becomes a roll with a fill shorter than the step (``"fill"``;
    the firmware ducks the voice at the fill end). Track rotation is baked
    into the step order.
    """
    out = HelicasePattern.empty()
    dropped_autos = 0
    approximated = 0
    for t in range(NUM_TRACKS):
        raw = src.length_rotate[t]
        length = (raw & 0x0F) if raw is not None else 0
        rotate = (raw >> 4) if raw is not None else 0
        if length and rotate > length:
            rotate %= length
        length = length or 16
        out.length[t] = length
        out.shuffle[t] = shuffle
        for j in range(length):
            m = (j + rotate) % length
            if not src.main_masks[t] >> m & 1:
                continue
            subs = src.steps[t][m * 8:(m + 1) * 8]
            hits = [k for k, sd in enumerate(subs) if sd.active]
            if not hits:
                continue
            first = subs[hits[0]]
            step = out.steps[t][j]
            step.trigger = True
            step.specials = {
                "velocity": first.volume & 0x7F,
                "note": first.note & 0x7F,
                "condition": probability_to_condition(first.prob & 0x7F),
            }
            if hits[0]:
                step.specials["timing"] = timing_for_substep(hits[0])
            if len(hits) > 1:
                gaps = {b - a for a, b in zip(hits, hits[1:])}
                spacing = gaps.pop() if len(gaps) == 1 else None
                whole_step = (spacing is not None and hits[0] == 0 and
                              len(hits) * spacing == SUBSTEPS_PER_MAIN)
                if spacing in ROLL_INDEX_BY_SPACING and whole_step:
                    step.specials["roll"] = ROLL_INDEX_BY_SPACING[spacing]
                elif spacing in ROLL_INDEX_BY_SPACING and partial_rolls == "fill":
                    # fill = n*s/8 of the step: the firmware's slot count
                    # ceil(floor(24*fill/100) / (3*s)) is then exactly n.
                    step.specials["roll"] = ROLL_INDEX_BY_SPACING[spacing]
                    step.specials["fill"] = min(
                        100, len(hits) * spacing * 100 // SUBSTEPS_PER_MAIN)
                else:
                    approximated += 1
            seen = set()
            for dest, value in ((first.p1_dest, first.p1_val),
                                (first.p2_dest, first.p2_val)):
                if dest in (NO_AUTOMATION, 0):
                    continue
                target = fw.automation_id(dest)
                if target is None:
                    dropped_autos += 1
                    continue
                if target not in seen:
                    seen.add(target)
                    step.automation.append((target, value & 0x7F))
    if approximated:
        report.note(f"{label}: {approximated} step(s) with irregular sub-step "
                    "rhythms kept only their first hit")
    if dropped_autos:
        report.note(f"{label}: {dropped_autos} automation value(s) aimed at "
                    "parameters Helicase cannot automate were dropped")
    return out


def pattern_has_content(src: Lxr1Pattern) -> bool:
    return any(
        src.main_masks[t] >> m & 1 and
        any(sd.active for sd in src.steps[t][m * 8:(m + 1) * 8])
        for t in range(NUM_TRACKS) for m in range(16))


# ---------------------------------------------------------------------------
# Scene and Bank writers.
# ---------------------------------------------------------------------------
CHAIN_NEXT_ABSOLUTE = 3          # s01 = Scene slot 0
CHAIN_NEXT_RANDOM = 18           # r0N = 18 + N
CHAIN_BAR_CONDITIONS = {n: 13 + n for n in range(1, 9)}   # b01..b08 = 14..21
CHAIN_BAR_CONDITIONS.update({16: 22, 32: 23, 64: 24})


def chain_for(index: int, src: Lxr1Pattern, report: Report,
              label: str) -> tuple[int, int]:
    """Map LXR1 next-pattern / change-bar onto Scene chain cn1/nx1.

    LXR1 moves to ``next`` every ``change_bar + 1`` bars; ``next`` 8..14 is
    `r2`..`r8` (random among the first N patterns).
    """
    nxt = src.next_pattern
    if nxt == index or nxt > 14:
        return 0, 0
    bars = src.change_bar + 1
    if bars not in CHAIN_BAR_CONDITIONS:
        nearest = min(CHAIN_BAR_CONDITIONS, key=lambda n: (abs(n - bars), n))
        report.note(f"{label}: change after {bars} bars approximated as "
                    f"{nearest}")
        bars = nearest
    if nxt < NUM_LXR1_PATTERNS:
        return CHAIN_BAR_CONDITIONS[bars], CHAIN_NEXT_ABSOLUTE + nxt
    return CHAIN_BAR_CONDITIONS[bars], CHAIN_NEXT_RANDOM + (nxt - 8 + 2)


def csv(values) -> str:
    return ",".join(str(v) for v in values)


def write_sceneset(path: Path, pattern: HelicasePattern, audio_out: list[int],
                   midi_channel: list[int], midi_note: list[int],
                   chain: tuple[int, int]) -> None:
    """sceneset.scg in filesystem_nextScenesetLine() order (no Effect).

    midi_channel 0 = `glb`, 1..16; midi_note 0 = `on`, 1..127 = root.
    """
    lines = [
        "format=helicase.sceneset",
        "version=1",
        "morph_amount=0",
        "voice_morph_amount=" + csv([0] * 6),
        "midi_channel=" + csv(midi_channel),
        "midi_note=" + csv(midi_note),
        "audio_out=" + csv(audio_out),
        "fx_send_amount=" + csv([0] * 6),
        "fader_setting=" + csv([0] * 6),
        "effect_morph_amount=0",
        "bus_comp_mode=0",
        "bus_comp_amount=0",
        "bus_comp_time=0",
        "bus_comp_sidechain=0",
        "fx_send_morph=" + csv([0] * 6),
        # Morph endpoints equal the Normal values, so Morph changes nothing.
        "track_morph_length=" + csv(pattern.length),
        "track_morph_scale=" + csv(pattern.scale),
        "track_morph_shuffle=" + csv(pattern.shuffle),
        "chain_condition=" + csv([chain[0], 0]),
        "chain_next=" + csv([chain[1], 0]),
        "",
    ]
    path.write_text("\n".join(lines), encoding="ascii")


def write_bankset(path: Path, scene_count: int) -> None:
    """bankset.bcg v2 with every converted Scene in one VOICE edit mask."""
    tied = (1 << scene_count) - 1
    lines = ["format=helicase.bankset", "version=2", "active_scene=0"]
    for i in range(16):
        mask = tied if i < scene_count else 1 << i
        lines.append(f"scene_mask_voice_edit_{i}={mask:04x}")
    lines.append("")
    path.write_text("\n".join(lines), encoding="ascii")


def add_common_arguments(parser) -> None:
    parser.add_argument("--firmware-root", type=Path, default=REPO_ROOT,
                        help="Helicase repository root whose descriptor tables "
                             "define the output (default: this repository)")
    parser.add_argument("--start-slot", type=int, default=0,
                        help="first library slot to use (default 0); occupied "
                             "slots are skipped")
    parser.add_argument("--dry-run", action="store_true",
                        help="report what would be written without writing")


def add_target_encoding_argument(parser) -> None:
    parser.add_argument(
        "--target-encoding", choices=("auto",) + TARGET_ENCODINGS,
        default="auto",
        help="how kit LFO/velocity target bytes are read: auto (default; "
             ".snd by payload size, .all/.prf = modtargets), pages (LXR "
             "V0.2x, 2013 factory kits) or modtargets (Nov 2013 and later)")


def add_numbering_arguments(parser, what: str) -> None:
    parser.add_argument("--keep-numbers", action="store_true",
                        help=f"use the LXR1 file number as the {what} slot "
                             "(p007 -> 007)")
    parser.add_argument("--overwrite", action="store_true",
                        help="with --keep-numbers, replace an occupied slot")


def check_start_slot(value: int) -> None:
    if not 0 <= value <= 999:
        sys.exit("--start-slot must be 0..999")
