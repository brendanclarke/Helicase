#include "DrumParameters.h"
#include "DrumVoice.h"
#include "menu.h"
#include "MenuText.h"
#include <stddef.h>

/*
 * Drum parameter source of truth.
 *
 * ParameterArray/SceneData decide that every kit slot has generic storage.
 * This file defines what each storage cell means when the slot contains a drum:
 * file key, menu text, dtype, morph/mod flags, and runtime offset from the
 * slot's DrumVoice instance.
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
 * BIND stores the DrumVoice byte offset and scalar type used by the generic
 * runtime writer. Some parameters still need extra DSP setters; those are
 * recognized later by InstrumentManager using descriptor->file_key.
 */
#define BIND(member_, type_) \
    { INSTRUMENT_BIND_INSTANCE_OFFSET, (uint16_t)offsetof(DrumVoice, member_), type_ }

/*
 * ROW creates a normal descriptor-backed sound parameter. It uses FLAGS_IMAGE,
 * so the cell participates in morphing, modulation target selection, and step
 * automation.
 */
#define ROW(key_, cat_, long_, short_, dtype_, member_, type_) \
    { key_, short_, long_, cat_, dtype_, FLAGS_IMAGE, BIND(member_, type_) }

/*
 * ROW_MENU is a ROW variant for parameters whose dtype encodes a named menu
 * table, such as waveform, filter type, or LFO sync rate.
 */
#define ROW_MENU(key_, cat_, long_, short_, menu_, member_, type_) \
    ROW(key_, cat_, long_, short_, (uint8_t)(DTYPE_MENU | (menu_ << 4)), member_, type_)

/*
 * ROW_NOBIND is for descriptor-owned cells that do not write a DrumVoice
 * member directly. These are single-endpoint supplemental values such as
 * modulation target selectors, so they intentionally start with flags=0 rather
 * than FLAGS_IMAGE.
 */
#define ROW_NOBIND(key_, cat_, long_, short_, dtype_, bind_kind_) \
    { key_, short_, long_, cat_, dtype_, 0u, { bind_kind_, 0u, 0u } }

/*
 * ROW_NOBIND_IMAGE is for image parameters whose runtime destination is not a
 * DrumVoice member. The value still morphs, can be a modulation/automation
 * target, and is applied through the supplied binding kind.
 */
#define ROW_NOBIND_IMAGE(key_, cat_, long_, short_, dtype_, bind_kind_) \
    { key_, short_, long_, cat_, dtype_, FLAGS_IMAGE, { bind_kind_, 0u, 0u } }

/*
 * Drum-local descriptor indices.
 *
 * These enum values must stay in the same order as drum_param_descriptors[].
 * Menu layouts use the names instead of raw numbers, while Scene/Storage still
 * stores the numeric descriptor index. The slot identity is supplied elsewhere,
 * so these names describe "a drum parameter" rather than "drum voice 1/2/3".
 */
typedef enum {
    DRUM_PARAM_OSC1_WAVE = 0u,
    DRUM_PARAM_OSC1_PITCH_COARSE,
    DRUM_PARAM_OSC1_PITCH_FINE,
    DRUM_PARAM_OSC2_WAVE,
    DRUM_PARAM_OSC2_PITCH_COARSE,
    DRUM_PARAM_OSC2_MOD_AMOUNT,
    DRUM_PARAM_OSC2_MOD_TYPE,
    DRUM_PARAM_FILTER_FREQ,
    DRUM_PARAM_FILTER_RESO,
    DRUM_PARAM_FILTER_DRIVE,
    DRUM_PARAM_FILTER_TYPE,
    DRUM_PARAM_AMP_ENVELOPE_ATTACK,
    DRUM_PARAM_AMP_ENVELOPE_DECAY,
    DRUM_PARAM_AMP_ENVELOPE_SLOPE,
    DRUM_PARAM_PITCH_ENVELOPE_DECAY,
    DRUM_PARAM_PITCH_ENVELOPE_AMOUNT,
    DRUM_PARAM_PITCH_ENVELOPE_SLOPE,
    DRUM_PARAM_INSTRUMENT_VOL,
    DRUM_PARAM_INSTRUMENT_PAN,
    DRUM_PARAM_INSTRUMENT_DRIVE,
    DRUM_PARAM_INSTRUMENT_DECIMATION,
    DRUM_PARAM_LFO_RATE,
    DRUM_PARAM_LFO_AMOUNT,
    DRUM_PARAM_LFO_AMOUNT_2,
    DRUM_PARAM_LFO_WAVE,
    DRUM_PARAM_LFO_RETRIGGER_VOICE,
    DRUM_PARAM_LFO_POLARITY,
    DRUM_PARAM_LFO_SYNC,
    DRUM_PARAM_LFO_OFFSET,
    DRUM_PARAM_VELO_VOL_ON_OFF,
    DRUM_PARAM_VELO_MOD_AMOUNT,
    DRUM_PARAM_VELO_MOD_DEST,
    DRUM_PARAM_LFO_TARGET_VOICE,
    DRUM_PARAM_LFO_TARGET_PARAM,
    DRUM_PARAM_LFO_TARGET_VOICE_2,
    DRUM_PARAM_LFO_TARGET_PARAM_2,
    DRUM_PARAM_TRANSIENT_WAVE,
    DRUM_PARAM_TRANSIENT_VOL,
    DRUM_PARAM_TRANSIENT_FREQ,
    DRUM_PARAM_COUNT
} drum_param_index_t;

_Static_assert(DRUM_PARAM_DESCRIPTOR_COUNT == DRUM_PARAM_COUNT,
               "DRUM_PARAM_DESCRIPTOR_COUNT must match drum_param_index_t");

/*
 * Drum parameter descriptors.
 *
 * Each row defines the SD-card key, category label, long edit label,
 * three-character menu label, display dtype, capability flags, and runtime binding for one
 * descriptor-indexed storage cell.
 */
const ParamDescriptor drum_param_descriptors[] = {
    ROW_MENU("osc1_wave", "Oscilltr", "Waveform", "wav", MENU_WAVEFORM, osc.waveform, TYPE_UINT8),
    ROW("osc1_pitch_coarse", "Oscilltr", "Coarse", "coa", DTYPE_0B127, osc.modNodeValue, TYPE_SPECIAL_F),
    ROW("osc1_pitch_fine", "Oscilltr", "Fine", "fin", DTYPE_PM63, osc.modNodeValue, TYPE_SPECIAL_F),
    ROW_MENU("osc2_wave", "FM", "Waveform", "wav", MENU_WAVEFORM, modOsc.waveform, TYPE_UINT8),
    ROW("osc2_pitch_coarse", "FM", "Frequncy", "frq", DTYPE_0B127, modOsc.modNodeValue, TYPE_SPECIAL_F),
    ROW("osc2_mod_amount", "FM", "Amount", "amt", DTYPE_0B127, fmModAmount, TYPE_FLT),
    ROW("osc2_mod_type", "FM", "Mode", "mod", DTYPE_MIX_FM, mixOscs, TYPE_UINT8),
    ROW("filter_freq", "Filter", "Frequncy", "frq", DTYPE_0B127, filter.f, TYPE_FLT),
    ROW("filter_reso", "Filter", "Resnance", "res", DTYPE_0B127, filter.q, TYPE_FLT),
    ROW("filter_drive", "Filter", "Overdriv", "drv", DTYPE_0B127, filter.drive, TYPE_FLT),
    ROW_MENU("filter_type", "Filter", "Type", "typ", MENU_FILTER, filterType, TYPE_UINT8),
    ROW("amp_envelope_attack", "Veloc EG", "Attack", "atk", DTYPE_0B127, oscVolEg.attack, TYPE_FLT),
    ROW("amp_envelope_decay", "Veloc EG", "Decay", "dec", DTYPE_0B127, oscVolEg.decay, TYPE_FLT),
    ROW("amp_envelope_slope", "Veloc EG", "Slope", "slp", DTYPE_0B127, oscVolEg.slope, TYPE_FLT),
    ROW("pitch_envelope_decay", "PitchMod", "Decay", "dec", DTYPE_0B127, oscPitchEg.decay, TYPE_FLT),
    ROW("pitch_envelope_amount", "PitchMod", "Amount", "amt", DTYPE_0B127, egPitchModAmount, TYPE_FLT),
    ROW("pitch_envelope_slope", "PitchMod", "Slope", "slp", DTYPE_0B127, oscPitchEg.slope, TYPE_FLT),
    ROW("instrument_vol", "Voice", "Volume", "vol", DTYPE_0B127, vol, TYPE_FLT),
    ROW("instrument_pan", "Voice", "Panning", "pan", DTYPE_PM63, pan, TYPE_UINT8),
    ROW("instrument_drive", "Voice", "Overdriv", "drv", DTYPE_0B127, distortion.shape, TYPE_FLT),
    ROW_NOBIND_IMAGE("instrument_decimation", "Voice", "SampleRt", "srt", DTYPE_0B127, INSTRUMENT_BIND_SLOT_DECIMATION),
    /*
     * LFO runtime rows.
     *
     * The LFO remains one oscillator per voice. modTarget and modTarget2 are
     * separate destinations for that oscillator, so amount and target cells are
     * duplicated while rate/wave/sync/offset/retrigger/polarity are shared.
     * Pair 1 keeps the original file keys; pair 2 adds new save/load-ready
     * keys without changing the Kit folder hierarchy.
     */
    ROW("lfo_rate", "LFO", "Frequncy", "frq", DTYPE_0B127, lfo.modNodeValue, TYPE_SPECIAL_F),
    ROW("lfo_amount", "LFO", "Amount", "am1", DTYPE_0B127, lfo.modTarget.amount, TYPE_FLT),
    ROW("lfo_amount_2", "LFO", "Amount 2", "am2", DTYPE_0B127, lfo.modTarget2.amount, TYPE_FLT),
    ROW_MENU("lfo_wave", "LFO", "Waveform", "wav", MENU_LFO_WAVES, lfo.waveform, TYPE_UINT8),
    ROW_MENU("lfo_retrigger_voice", "LFO", "Retriggr", "rtg", MENU_RETRIGGER, lfo.retrigger, TYPE_UINT8),
    ROW("lfo_polarity", "LFO", "Polarity", "pol", DTYPE_LFO_POLARITY, lfo.polarity, TYPE_UINT8),
    ROW_MENU("lfo_sync", "LFO", "ClockSnc", "snc", MENU_SYNC_RATES, lfo.sync, TYPE_UINT8),
    ROW("lfo_offset", "LFO", "Offset", "ofs", DTYPE_0B127, lfo.phaseOffset, TYPE_UINT32),
    ROW("velo_vol_on_off", "Velocity", "Vol mod", "vel", DTYPE_ON_OFF, volumeMod, TYPE_UINT8),
    ROW_NOBIND_IMAGE("velo_mod_amount", "Velocity", "Amount", "amt", DTYPE_0B127, INSTRUMENT_BIND_VELOCITY_AMOUNT),
    ROW_NOBIND("velo_mod_dest", "Velocity", "DstParam", "dst", DTYPE_TARGET_SELECTION_VELO, INSTRUMENT_BIND_VELOCITY_TARGET),
    ROW_NOBIND("lfo_target_voice", "LFO", "DstVoice", "vo1", DTYPE_VOICE_LFO, INSTRUMENT_BIND_LFO_TARGET_VOICE),
    ROW_NOBIND("lfo_target_param", "LFO", "DstParam", "ds1", DTYPE_TARGET_SELECTION_LFO, INSTRUMENT_BIND_LFO_TARGET_PARAM),
    ROW_NOBIND("lfo_target_voice_2", "LFO", "DstVoice2", "vo2", DTYPE_VOICE_LFO, INSTRUMENT_BIND_LFO_TARGET_VOICE_2),
    ROW_NOBIND("lfo_target_param_2", "LFO", "DstParam2", "ds2", DTYPE_TARGET_SELECTION_LFO, INSTRUMENT_BIND_LFO_TARGET_PARAM_2),
    ROW_MENU("transient_wave", "Transnt", "Waveform", "wav", MENU_TRANS, transGen.waveform, TYPE_UINT8),
    ROW("transient_vol", "Transnt", "Volume", "vol", DTYPE_0B127, transGen.volume, TYPE_FLT),
    ROW("transient_freq", "Transnt", "Frequncy", "frq", DTYPE_0B127, transGen.pitch, TYPE_FLT),
};

/* Runtime registry count used by InstrumentManager bounds checks. */
const uint8_t drum_param_descriptor_count =
    (uint8_t)(sizeof(drum_param_descriptors) / sizeof(drum_param_descriptors[0]));

/*
 * Drum voice menu layout.
 *
 * Why this is here instead of Core/Menu/menuPages.h: the old page table stored
 * flat PAR_* ids, but drum storage is now descriptor-indexed. These cells are
 * a position-for-position translation of menuPages.old VOICE1-3 into local
 * descriptor enum names. Empty cells preserve the old navigation shape; cells
 * formerly owned by Pattern/Scene/global state are deliberately empty here and
 * can be resolved by Menu as non-instrument cells.
 */
const instrument_menu_page_t drum_menu_pages[] = {
    {{ DRUM_PARAM_OSC1_PITCH_COARSE, DRUM_PARAM_OSC1_PITCH_FINE, DRUM_PARAM_OSC1_WAVE, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY }},
    {{ DRUM_PARAM_AMP_ENVELOPE_ATTACK, DRUM_PARAM_AMP_ENVELOPE_DECAY, DRUM_PARAM_AMP_ENVELOPE_SLOPE, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY }},
    {{ DRUM_PARAM_PITCH_ENVELOPE_DECAY, DRUM_PARAM_PITCH_ENVELOPE_SLOPE, DRUM_PARAM_PITCH_ENVELOPE_AMOUNT, INSTRUMENT_MENU_SKIP, DRUM_PARAM_VELO_MOD_DEST, DRUM_PARAM_VELO_MOD_AMOUNT, DRUM_PARAM_VELO_VOL_ON_OFF, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY }},
    {{ DRUM_PARAM_OSC2_MOD_AMOUNT, DRUM_PARAM_OSC2_PITCH_COARSE, DRUM_PARAM_OSC2_WAVE, DRUM_PARAM_OSC2_MOD_TYPE, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY }},
    {{ DRUM_PARAM_TRANSIENT_WAVE, DRUM_PARAM_TRANSIENT_VOL, DRUM_PARAM_TRANSIENT_FREQ, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY }},
    {{ DRUM_PARAM_FILTER_FREQ, DRUM_PARAM_FILTER_RESO, DRUM_PARAM_FILTER_TYPE, DRUM_PARAM_FILTER_DRIVE, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY }},
    /*
     * One wide LFO sub-page: oscillator controls, application shape/amounts,
     * then target selector pairs. Menu displays this 16-cell row as four-cell
     * screens behind the same SELECT button, so the old Mix/Voice sub-page
     * below remains accessible.
     */
    {{ DRUM_PARAM_LFO_RATE, DRUM_PARAM_LFO_SYNC, DRUM_PARAM_LFO_WAVE, DRUM_PARAM_LFO_OFFSET, DRUM_PARAM_LFO_RETRIGGER_VOICE, DRUM_PARAM_LFO_POLARITY, DRUM_PARAM_LFO_AMOUNT, DRUM_PARAM_LFO_AMOUNT_2, DRUM_PARAM_LFO_TARGET_VOICE, DRUM_PARAM_LFO_TARGET_PARAM, DRUM_PARAM_LFO_TARGET_VOICE_2, DRUM_PARAM_LFO_TARGET_PARAM_2, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY }},
    {{ DRUM_PARAM_INSTRUMENT_VOL, DRUM_PARAM_INSTRUMENT_PAN, DRUM_PARAM_INSTRUMENT_DECIMATION, DRUM_PARAM_INSTRUMENT_DRIVE, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY }},
};

/* Number of instrument-owned subpages exposed for Drum voice pages. */
const uint8_t drum_menu_page_count =
    (uint8_t)(sizeof(drum_menu_pages) / sizeof(drum_menu_pages[0]));

_Static_assert(DRUM_MENU_PAGE_COUNT ==
                   (sizeof(drum_menu_pages) / sizeof(drum_menu_pages[0])),
               "DRUM_MENU_PAGE_COUNT must match drum_menu_pages");

#undef FLAGS_IMAGE
#undef BIND
#undef ROW
#undef ROW_MENU
#undef ROW_NOBIND
#undef ROW_NOBIND_IMAGE
