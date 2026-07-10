#include "SnareParameters.h"
#include "Snare.h"
#include "menu.h"
#include "MenuText.h"
#include <stddef.h>

/*
 * Snare parameter source of truth.
 *
 * ParameterArray/SceneData provide generic per-slot storage. This file defines
 * what each storage cell means when the slot contains a snare: SD-card key,
 * menu labels, dtype, morph/mod/automation capability flags, and runtime
 * binding into SnareVoice or a supplemental binding kind.
 */

/*
 * FLAGS_IMAGE marks a normal sound parameter image cell: the value is stored
 * in both main/morph instrument images, can be interpolated by the morph
 * worker, can be a modulation destination, and can be written by step
 * automation. ROW and ROW_MENU use these flags.
 */
#define FLAGS_IMAGE \
    (INSTRUMENT_PARAM_FLAG_MORPHABLE | INSTRUMENT_PARAM_FLAG_MODULATABLE | \
     INSTRUMENT_PARAM_FLAG_AUTOMATABLE)

/*
 * BIND stores the SnareVoice byte offset and scalar type used by the generic
 * runtime writer. Some parameters still need extra DSP setters; those are
 * recognized later by InstrumentManager using descriptor->file_key.
 */
#define BIND(member_, type_) \
    { INSTRUMENT_BIND_INSTANCE_OFFSET, (uint16_t)offsetof(SnareVoice, member_), type_ }

/*
 * ROW creates a normal descriptor-backed sound parameter. It uses FLAGS_IMAGE,
 * so the cell participates in morphing, modulation target selection, and step
 * automation.
 */
#define ROW(key_, short_, long_, cat_, dtype_, member_, type_) \
    { key_, short_, long_, cat_, dtype_, FLAGS_IMAGE, BIND(member_, type_) }

/*
 * ROW_MENU is a ROW variant for parameters whose dtype encodes a named menu
 * table, such as waveform, filter type, or LFO sync rate.
 */
#define ROW_MENU(key_, short_, long_, cat_, menu_, member_, type_) \
    ROW(key_, short_, long_, cat_, (uint8_t)(DTYPE_MENU | (menu_ << 4)), member_, type_)

/*
 * ROW_NOBIND is for descriptor-owned cells that do not write a SnareVoice
 * member directly. These are single-endpoint supplemental values such as
 * modulation target selectors, so they intentionally start with flags=0 rather
 * than FLAGS_IMAGE.
 */
#define ROW_NOBIND(key_, short_, long_, cat_, dtype_, bind_kind_) \
    { key_, short_, long_, cat_, dtype_, 0u, { bind_kind_, 0u, 0u } }

/*
 * ROW_NOBIND_IMAGE is for image parameters whose runtime destination is not a
 * SnareVoice member. The value still morphs, can be a modulation/automation
 * target, and is applied through the supplied binding kind.
 */
#define ROW_NOBIND_IMAGE(key_, short_, long_, cat_, dtype_, bind_kind_) \
    { key_, short_, long_, cat_, dtype_, FLAGS_IMAGE, { bind_kind_, 0u, 0u } }

/*
 * Snare-local descriptor indices.
 *
 * These enum values must stay in the same order as snare_param_descriptors[].
 * Menu layouts use the names instead of raw numbers, while Scene/Storage still
 * stores the numeric descriptor index. The slot identity is supplied elsewhere.
 */
typedef enum {
    SNARE_PARAM_OSC1_WAVE = 0u,
    SNARE_PARAM_OSC1_PITCH_COARSE,
    SNARE_PARAM_OSC1_PITCH_FINE,
    SNARE_PARAM_NOISE_FREQ,
    SNARE_PARAM_OSC1_NOISE_MIX,
    SNARE_PARAM_FILTER_FREQ,
    SNARE_PARAM_FILTER_RESO,
    SNARE_PARAM_FILTER_DRIVE,
    SNARE_PARAM_FILTER_TYPE,
    SNARE_PARAM_AMP_ENVELOPE_ATTACK,
    SNARE_PARAM_AMP_ENVELOPE_DECAY,
    SNARE_PARAM_AMP_ENVELOPE_SLOPE,
    SNARE_PARAM_AMP_ATTACK_REPEAT,
    SNARE_PARAM_PITCH_ENVELOPE_DECAY,
    SNARE_PARAM_PITCH_ENVELOPE_AMOUNT,
    SNARE_PARAM_PITCH_ENVELOPE_SLOPE,
    SNARE_PARAM_INSTRUMENT_VOL,
    SNARE_PARAM_INSTRUMENT_PAN,
    SNARE_PARAM_INSTRUMENT_DRIVE,
    SNARE_PARAM_INSTRUMENT_DECIMATION,
    SNARE_PARAM_LFO_RATE,
    SNARE_PARAM_LFO_AMOUNT,
    SNARE_PARAM_LFO_WAVE,
    SNARE_PARAM_LFO_RETRIGGER_VOICE,
    SNARE_PARAM_LFO_SYNC,
    SNARE_PARAM_LFO_OFFSET,
    SNARE_PARAM_VELO_VOL_ON_OFF,
    SNARE_PARAM_VELO_MOD_AMOUNT,
    SNARE_PARAM_VELO_MOD_DEST,
    SNARE_PARAM_LFO_TARGET_VOICE,
    SNARE_PARAM_LFO_TARGET_PARAM,
    SNARE_PARAM_TRANSIENT_WAVE,
    SNARE_PARAM_TRANSIENT_VOL,
    SNARE_PARAM_TRANSIENT_FREQ,
    SNARE_PARAM_COUNT
} snare_param_index_t;

/*
 * Snare parameter descriptors.
 *
 * Each row defines the SD-card key, three-character menu label, long edit label,
 * category label, display dtype, capability flags, and runtime binding for one
 * descriptor-indexed storage cell.
 */
const ParamDescriptor snare_param_descriptors[] = {
    ROW_MENU("osc1_wave", "wav", "Waveform", "Oscilltr", MENU_WAVEFORM, osc.waveform, TYPE_UINT8),
    ROW("osc1_pitch_coarse", "coa", "Coarse", "Oscilltr", DTYPE_0B127, osc.modNodeValue, TYPE_SPECIAL_F),
    ROW("osc1_pitch_fine", "fin", "Fine", "Oscilltr", DTYPE_PM63, osc.modNodeValue, TYPE_SPECIAL_F),
    ROW("noise_freq", "noi", "Frequncy", "Noise", DTYPE_0B127, noiseOsc.modNodeValue, TYPE_SPECIAL_F),
    ROW("osc1_noise_mix", "mix", "Mix", "Nois/Osc", DTYPE_0B127, mix, TYPE_FLT),
    ROW("filter_freq", "frq", "Frequncy", "Filter", DTYPE_0B127, filter.f, TYPE_FLT),
    ROW("filter_reso", "res", "Resnance", "Filter", DTYPE_0B127, filter.q, TYPE_FLT),
    ROW("filter_drive", "drv", "Overdriv", "Filter", DTYPE_0B127, filter.drive, TYPE_FLT),
    ROW_MENU("filter_type", "typ", "Type", "Filter", MENU_FILTER, filterType, TYPE_UINT8),
    ROW("amp_envelope_attack", "atk", "Attack", "Veloc EG", DTYPE_0B127, oscVolEg.attack, TYPE_FLT),
    ROW("amp_envelope_decay", "dec", "Decay", "Veloc EG", DTYPE_0B127, oscVolEg.decay, TYPE_FLT),
    ROW("amp_envelope_slope", "slp", "Slope", "Veloc EG", DTYPE_0B127, oscVolEg.slope, TYPE_FLT),
    ROW("amp_attack_repeat", "rpt", "RepeatCt", "Veloc EG", DTYPE_0B127, oscVolEg.repeat, TYPE_UINT8),
    ROW("pitch_envelope_decay", "dec", "Decay", "PitchMod", DTYPE_0B127, oscPitchEg.decay, TYPE_FLT),
    ROW("pitch_envelope_amount", "amt", "Amount", "PitchMod", DTYPE_0B127, egPitchModAmount, TYPE_FLT),
    ROW("pitch_envelope_slope", "slp", "Slope", "PitchMod", DTYPE_0B127, oscPitchEg.slope, TYPE_FLT),
    ROW("instrument_vol", "vol", "Volume", "Voice", DTYPE_0B127, vol, TYPE_FLT),
    ROW("instrument_pan", "pan", "Panning", "Voice", DTYPE_PM63, pan, TYPE_UINT8),
    ROW("instrument_drive", "drv", "Overdriv", "Voice", DTYPE_0B127, distortion.shape, TYPE_FLT),
    ROW_NOBIND_IMAGE("instrument_decimation", "srt", "SampleRt", "Voice", DTYPE_0B127, INSTRUMENT_BIND_SLOT_DECIMATION),
    ROW("lfo_rate", "frq", "Frequncy", "LFO", DTYPE_0B127, lfo.modNodeValue, TYPE_SPECIAL_F),
    ROW("lfo_amount", "amt", "Amount", "LFO", DTYPE_0B127, lfo.modTarget.amount, TYPE_FLT),
    ROW_MENU("lfo_wave", "wav", "Waveform", "LFO", MENU_LFO_WAVES, lfo.waveform, TYPE_UINT8),
    ROW_MENU("lfo_retrigger_voice", "rtg", "Retriggr", "LFO", MENU_RETRIGGER, lfo.retrigger, TYPE_UINT8),
    ROW_MENU("lfo_sync", "snc", "ClockSnc", "LFO", MENU_SYNC_RATES, lfo.sync, TYPE_UINT8),
    ROW("lfo_offset", "ofs", "Offset", "LFO", DTYPE_0B127, lfo.phaseOffset, TYPE_UINT32),
    ROW("velo_vol_on_off", "vel", "Vol mod", "Velocity", DTYPE_ON_OFF, volumeMod, TYPE_UINT8),
    ROW_NOBIND_IMAGE("velo_mod_amount", "amt", "Amount", "Velocity", DTYPE_0B127, INSTRUMENT_BIND_VELOCITY_AMOUNT),
    ROW_NOBIND("velo_mod_dest", "dst", "DstParam", "Velocity", DTYPE_TARGET_SELECTION_VELO, INSTRUMENT_BIND_VELOCITY_TARGET),
    ROW_NOBIND("lfo_target_voice", "voi", "DstVoice", "LFO", DTYPE_VOICE_LFO, INSTRUMENT_BIND_LFO_TARGET_VOICE),
    ROW_NOBIND("lfo_target_param", "dst", "DstParam", "LFO", DTYPE_TARGET_SELECTION_LFO, INSTRUMENT_BIND_LFO_TARGET_PARAM),
    ROW_MENU("transient_wave", "wav", "Waveform", "Transnt", MENU_TRANS, transGen.waveform, TYPE_UINT8),
    ROW("transient_vol", "vol", "Volume", "Transnt", DTYPE_0B127, transGen.volume, TYPE_FLT),
    ROW("transient_freq", "frq", "Frequncy", "Transnt", DTYPE_0B127, transGen.pitch, TYPE_FLT),
};

/* Runtime registry count used by InstrumentManager bounds checks. */
const uint8_t snare_param_descriptor_count =
    (uint8_t)(sizeof(snare_param_descriptors) / sizeof(snare_param_descriptors[0]));

/*
 * Snare voice menu layout.
 *
 * This preserves menuPages.old VOICE4 positions using snare descriptor enum
 * names.
 * Page cells that used to point at deleted/non-instrument PAR_* ids stay empty
 * in this instrument-owned table so Menu can keep those owners separate.
 */
const instrument_menu_page_t snare_menu_pages[] = {
    {{ SNARE_PARAM_OSC1_PITCH_COARSE, SNARE_PARAM_OSC1_PITCH_FINE, SNARE_PARAM_NOISE_FREQ, SNARE_PARAM_OSC1_NOISE_MIX, SNARE_PARAM_OSC1_WAVE, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY }},
    {{ SNARE_PARAM_AMP_ENVELOPE_ATTACK, SNARE_PARAM_AMP_ENVELOPE_DECAY, SNARE_PARAM_AMP_ATTACK_REPEAT, SNARE_PARAM_AMP_ENVELOPE_SLOPE, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY }},
    {{ SNARE_PARAM_PITCH_ENVELOPE_DECAY, SNARE_PARAM_PITCH_ENVELOPE_SLOPE, SNARE_PARAM_PITCH_ENVELOPE_AMOUNT, INSTRUMENT_MENU_SKIP, SNARE_PARAM_VELO_MOD_DEST, SNARE_PARAM_VELO_MOD_AMOUNT, SNARE_PARAM_VELO_VOL_ON_OFF, INSTRUMENT_MENU_EMPTY }},
    {{ INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY }},
    {{ SNARE_PARAM_TRANSIENT_WAVE, SNARE_PARAM_TRANSIENT_VOL, SNARE_PARAM_TRANSIENT_FREQ, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY }},
    {{ SNARE_PARAM_FILTER_FREQ, SNARE_PARAM_FILTER_RESO, SNARE_PARAM_FILTER_TYPE, SNARE_PARAM_FILTER_DRIVE, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY }},
    {{ SNARE_PARAM_LFO_RATE, SNARE_PARAM_LFO_SYNC, SNARE_PARAM_LFO_AMOUNT, SNARE_PARAM_LFO_WAVE, SNARE_PARAM_LFO_RETRIGGER_VOICE, SNARE_PARAM_LFO_OFFSET, SNARE_PARAM_LFO_TARGET_VOICE, SNARE_PARAM_LFO_TARGET_PARAM }},
    {{ SNARE_PARAM_INSTRUMENT_VOL, SNARE_PARAM_INSTRUMENT_PAN, SNARE_PARAM_INSTRUMENT_DECIMATION, SNARE_PARAM_INSTRUMENT_DRIVE, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY }},
};

/* Number of instrument-owned subpages exposed for the Snare voice page. */
const uint8_t snare_menu_page_count =
    (uint8_t)(sizeof(snare_menu_pages) / sizeof(snare_menu_pages[0]));

#undef FLAGS_IMAGE
#undef BIND
#undef ROW
#undef ROW_MENU
#undef ROW_NOBIND
#undef ROW_NOBIND_IMAGE
