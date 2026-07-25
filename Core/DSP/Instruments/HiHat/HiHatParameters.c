#include "HiHatParameters.h"
#include "HiHat.h"
#include "menu.h"
#include "MenuText.h"
#include <stddef.h>

/*
 * Hi-hat parameter source of truth.
 *
 * ParameterArray/SceneData provide generic per-slot storage. This file defines
 * what each storage cell means when the slot contains a hihat: SD-card key,
 * menu labels, dtype, morph/mod/automation capability flags, and runtime
 * binding into HiHatVoice or a supplemental binding kind.
 */
/*
 * Load-menu type metadata for HiHat instruments.
 *
 * Inputs/outputs: InstrumentManager imports this immutable label and flag
 * byte into the central registry. Clients read it through registry accessors
 * instead of hardcoding "HiHat", Advanced policy, or Choke capability in
 * Menu/filesystem code. Affiliates are the Instrument Load browser,
 * assignment-limit checks, and generic VOICE7 choke substitution.
 */
const char hihat_instrument_display_label[] = "HiHat";
const uint8_t hihat_instrument_type_flags = HIHAT_INSTRUMENT_TYPE_FLAGS;

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
 * BIND stores the HiHatVoice byte offset and scalar type used by the generic
 * runtime writer. Some parameters still need extra DSP setters; those are
 * recognized later by InstrumentManager using descriptor->file_key.
 */
#define BIND(member_, type_) \
    { INSTRUMENT_BIND_INSTANCE_OFFSET, (uint16_t)offsetof(HiHatVoice, member_), type_ }

#define MOD_NONE \
    { 0u, 0u, INSTRUMENT_MOD_DOMAIN_NONE }
#define MOD_0_127 \
    { 0u, 127u, INSTRUMENT_MOD_DOMAIN_CONTINUOUS }
#define MOD_PM63 \
    { 0u, 127u, INSTRUMENT_MOD_DOMAIN_CONTINUOUS }
#define MOD_WAVE \
    { 0u, 0u, (uint8_t)(INSTRUMENT_MOD_DOMAIN_INTEGER | \
                        INSTRUMENT_MOD_DOMAIN_DYNAMIC_MAX) }

/*
 * ROW creates a normal descriptor-backed sound parameter. It uses FLAGS_IMAGE,
 * so the cell participates in morphing, modulation target selection, and step
 * automation.
 *
 * The explicit modulation domain is the LFO/velocity overlay contract in
 * descriptor-value units. It intentionally lives next to the row instead of in
 * InstrumentManager so selectors such as filter type and retrigger can remain
 * image-backed without being treated as continuous LFO destinations.
 */
#define ROW(key_, cat_, long_, short_, dtype_, mod_, member_, type_) \
    { key_, short_, long_, cat_, dtype_, FLAGS_IMAGE, mod_, BIND(member_, type_) }

/*
 * ROW_MENU is a ROW variant for parameters whose dtype encodes a named menu
 * table, such as waveform, filter type, or LFO sync rate.
 */
#define ROW_MENU(key_, cat_, long_, short_, menu_, mod_, member_, type_) \
    { key_, short_, long_, cat_, (uint8_t)(DTYPE_MENU | (menu_ << 4)), FLAGS_IMAGE, mod_, BIND(member_, type_) }

/*
 * ROW_NOBIND is for descriptor-owned cells that do not write a HiHatVoice
 * member directly. These are single-endpoint supplemental values such as
 * modulation target selectors, so they intentionally start with flags=0 rather
 * than FLAGS_IMAGE.
 */
#define ROW_NOBIND(key_, cat_, long_, short_, dtype_, bind_kind_) \
    { key_, short_, long_, cat_, dtype_, 0u, MOD_NONE, { bind_kind_, 0u, 0u } }

/*
 * ROW_NOBIND_IMAGE is for image parameters whose runtime destination is not a
 * HiHatVoice member. The value still morphs, can be a modulation/automation
 * target, and is applied through the supplied binding kind.
 */
#define ROW_NOBIND_IMAGE(key_, cat_, long_, short_, dtype_, mod_, bind_kind_) \
    { key_, short_, long_, cat_, dtype_, FLAGS_IMAGE, mod_, { bind_kind_, 0u, 0u } }

/*
 * Slot decimation is intentionally an image parameter, not a Scene target.
 *
 * Inputs: the normal descriptor text fields. Output: a descriptor whose flags
 * mark per-instrument decimation as morphable, modulatable, and automatable
 * even though its runtime binding is supplemental. InstrumentManager already
 * applies this binding through mixer_decimation_rate[slot]; the modulation and
 * automation target resolvers need follow-up adapter work so this descriptor
 * can be selected like other voice-local sound parameters without pretending
 * it is a direct HiHatVoice member.
 *
 * This small wrapper exists so instrument_decimation rows advertise their
 * intended contract directly at the row site. ROW_NOBIND_IMAGE remains the
 * generic supplemental-image helper for any future non-instance parameter.
 */
#define ROW_SLOT_DECIMATION(key_, cat_, long_, short_, dtype_) \
    ROW_NOBIND_IMAGE(key_, cat_, long_, short_, dtype_, MOD_0_127, INSTRUMENT_BIND_SLOT_DECIMATION)

/*
 * Hi-hat-local descriptor indices.
 *
 * These enum values must stay in the same order as hihat_param_descriptors[].
 * Menu layouts use the names instead of raw numbers, while Scene/Storage still
 * stores the numeric descriptor index. The hihat uses the generic `_choke`
 * descriptor suffix for its open decay variant, so one menu layout can expose
 * the base decay while Menu substitutes the choke descriptor on VOICE7.
 */
typedef enum {
    HIHAT_PARAM_OSC1_WAVE = 0u,
    HIHAT_PARAM_OSC1_PITCH_COARSE,
    HIHAT_PARAM_OSC1_PITCH_FINE,
    HIHAT_PARAM_OSC2_WAVE,
    HIHAT_PARAM_OSC2_PITCH_COARSE,
    HIHAT_PARAM_OSC2_MOD_AMOUNT,
    HIHAT_PARAM_OSC3_WAVE,
    HIHAT_PARAM_OSC3_PITCH_COARSE,
    HIHAT_PARAM_OSC3_MOD_AMOUNT,
    HIHAT_PARAM_FILTER_FREQ,
    HIHAT_PARAM_FILTER_RESO,
    HIHAT_PARAM_FILTER_DRIVE,
    HIHAT_PARAM_FILTER_TYPE,
    HIHAT_PARAM_AMP_ENVELOPE_ATTACK,
    HIHAT_PARAM_AMP_ENVELOPE_DECAY,
    HIHAT_PARAM_AMP_ENVELOPE_DECAY_CHOKE,
    HIHAT_PARAM_AMP_ENVELOPE_SLOPE,
    HIHAT_PARAM_INSTRUMENT_VOL,
    HIHAT_PARAM_INSTRUMENT_PAN,
    HIHAT_PARAM_INSTRUMENT_DRIVE,
    HIHAT_PARAM_INSTRUMENT_DECIMATION,
    HIHAT_PARAM_LFO_RATE,
    HIHAT_PARAM_LFO_AMOUNT,
    HIHAT_PARAM_LFO_AMOUNT_2,
    HIHAT_PARAM_LFO_WAVE,
    HIHAT_PARAM_LFO_RETRIGGER_VOICE,
    HIHAT_PARAM_LFO_POLARITY,
    HIHAT_PARAM_LFO_SYNC,
    HIHAT_PARAM_LFO_OFFSET,
    HIHAT_PARAM_VELO_VOL_ON_OFF,
    HIHAT_PARAM_VELO_MOD_AMOUNT,
    HIHAT_PARAM_VELO_MOD_DEST,
    HIHAT_PARAM_LFO_TARGET_VOICE,
    HIHAT_PARAM_LFO_TARGET_PARAM,
    HIHAT_PARAM_LFO_TARGET_VOICE_2,
    HIHAT_PARAM_LFO_TARGET_PARAM_2,
    HIHAT_PARAM_TRANSIENT_WAVE,
    HIHAT_PARAM_TRANSIENT_VOL,
    HIHAT_PARAM_TRANSIENT_FREQ,
    HIHAT_PARAM_COUNT
} hihat_param_index_t;

_Static_assert(HIHAT_PARAM_DESCRIPTOR_COUNT == HIHAT_PARAM_COUNT,
               "HIHAT_PARAM_DESCRIPTOR_COUNT must match hihat_param_index_t");

/*
 * Hi-hat parameter descriptors.
 *
 * Each row defines the SD-card key, category label, long edit label,
 * three-character menu label, display dtype, capability flags, modulation
 * domain, and runtime binding for one descriptor-indexed storage cell.
 */
const ParamDescriptor hihat_param_descriptors[] = {
    ROW_MENU("osc1_wave", "Oscilltr", "Waveform", "wav", MENU_WAVEFORM, MOD_WAVE, osc.waveform, TYPE_UINT8),
    ROW("osc1_pitch_coarse", "Oscilltr", "Coarse", "coa", DTYPE_0B127, MOD_0_127, osc.modNodeValue, TYPE_SPECIAL_F),
    ROW("osc1_pitch_fine", "Oscilltr", "Fine", "fin", DTYPE_PM63, MOD_PM63, osc.modNodeValue, TYPE_SPECIAL_F),
    ROW_MENU("osc2_wave", "Mod Osc", "Waveform", "wav", MENU_WAVEFORM, MOD_WAVE, modOsc.waveform, TYPE_UINT8),
    ROW("osc2_pitch_coarse", "Mod Osc", "Freqcy 1", "f1", DTYPE_0B127, MOD_0_127, modOsc.modNodeValue, TYPE_SPECIAL_F),
    ROW("osc2_mod_amount", "Mod Osc", "Gain 1", "g1", DTYPE_0B127, MOD_0_127, fmModAmount1, TYPE_FLT),
    ROW_MENU("osc3_wave", "Mod Osc", "Waveform", "wav", MENU_WAVEFORM, MOD_WAVE, modOsc2.waveform, TYPE_UINT8),
    ROW("osc3_pitch_coarse", "Mod Osc", "Freqcy 2", "f2", DTYPE_0B127, MOD_0_127, modOsc2.modNodeValue, TYPE_SPECIAL_F),
    ROW("osc3_mod_amount", "Mod Osc", "Gain 2", "g2", DTYPE_0B127, MOD_0_127, fmModAmount2, TYPE_FLT),
    ROW("filter_freq", "Filter", "Frequncy", "frq", DTYPE_0B127, MOD_0_127, filter.f, TYPE_FLT),
    ROW("filter_reso", "Filter", "Resnance", "res", DTYPE_0B127, MOD_0_127, filter.q, TYPE_FLT),
    ROW("filter_drive", "Filter", "Overdriv", "drv", DTYPE_0B127, MOD_0_127, filter.drive, TYPE_FLT),
    ROW_MENU("filter_type", "Filter", "Type", "typ", MENU_FILTER, MOD_NONE, filterType, TYPE_UINT8),
    ROW("amp_envelope_attack", "Veloc EG", "Attack", "atk", DTYPE_0B127, MOD_0_127, oscVolEg.attack, TYPE_FLT),
    ROW("amp_envelope_decay", "Veloc EG", "Dcy Clsd", "d1", DTYPE_0B127, MOD_0_127, decayClosed, TYPE_FLT),
    ROW("amp_envelope_decay_choke", "Veloc EG", "Dcy Open", "d2", DTYPE_0B127, MOD_0_127, decayOpen, TYPE_FLT),
    ROW("amp_envelope_slope", "Veloc EG", "Slope", "slp", DTYPE_0B127, MOD_0_127, oscVolEg.slope, TYPE_FLT),
    ROW("instrument_vol", "Voice", "Volume", "vol", DTYPE_0B127, MOD_0_127, vol, TYPE_FLT),
    ROW("instrument_pan", "Voice", "Panning", "pan", DTYPE_PM63, MOD_PM63, pan, TYPE_UINT8),
    ROW("instrument_drive", "Voice", "Overdriv", "drv", DTYPE_0B127, MOD_0_127, distortion.shape, TYPE_FLT),
    ROW_SLOT_DECIMATION("instrument_decimation", "Voice", "SampleRt", "srt", DTYPE_0B127),
    /*
     * LFO runtime rows.
     *
     * The LFO remains one oscillator per voice. modTarget and modTarget2 are
     * separate destinations for that oscillator, so amount and target cells are
     * duplicated while rate/wave/sync/offset/retrigger/polarity are shared.
     * Pair 1 keeps the original file keys; pair 2 adds new save/load-ready
     * keys without changing the Kit folder hierarchy.
     */
    ROW("lfo_rate", "LFO", "Frequncy", "frq", DTYPE_0B127, MOD_0_127, lfo.modNodeValue, TYPE_SPECIAL_F),
    ROW("lfo_amount", "LFO", "Amount", "am1", DTYPE_0B127, MOD_0_127, lfo.modTarget.amount, TYPE_FLT),
    ROW("lfo_amount_2", "LFO", "Amount 2", "am2", DTYPE_0B127, MOD_0_127, lfo.modTarget2.amount, TYPE_FLT),
    ROW_MENU("lfo_wave", "LFO", "Waveform", "wav", MENU_LFO_WAVES, MOD_NONE, lfo.waveform, TYPE_UINT8),
    ROW_MENU("lfo_retrigger_voice", "LFO", "Retriggr", "rtg", MENU_RETRIGGER, MOD_NONE, lfo.retrigger, TYPE_UINT8),
    ROW("lfo_polarity", "LFO", "Polarity", "pol", DTYPE_LFO_POLARITY, MOD_NONE, lfo.polarity, TYPE_UINT8),
    ROW_MENU("lfo_sync", "LFO", "ClockSnc", "snc", MENU_SYNC_RATES, MOD_NONE, lfo.sync, TYPE_UINT8),
    ROW("lfo_offset", "LFO", "Offset", "ofs", DTYPE_0B127, MOD_NONE, lfo.phaseOffset, TYPE_UINT32),
    ROW("velo_vol_on_off", "Velocity", "Vol mod", "vel", DTYPE_ON_OFF, MOD_NONE, volumeMod, TYPE_UINT8),
    ROW_NOBIND_IMAGE("velo_mod_amount", "Velocity", "Amount", "amt", DTYPE_0B127, MOD_NONE, INSTRUMENT_BIND_VELOCITY_AMOUNT),
    ROW_NOBIND("velo_mod_dest", "Velocity", "DstParam", "dst", DTYPE_TARGET_SELECTION_VELO, INSTRUMENT_BIND_VELOCITY_TARGET),
    ROW_NOBIND("lfo_target_voice", "LFO", "DstVoice", "vo1", DTYPE_VOICE_LFO, INSTRUMENT_BIND_LFO_TARGET_VOICE),
    ROW_NOBIND("lfo_target_param", "LFO", "DstParam", "ds1", DTYPE_TARGET_SELECTION_LFO, INSTRUMENT_BIND_LFO_TARGET_PARAM),
    ROW_NOBIND("lfo_target_voice_2", "LFO", "DstVoice2", "vo2", DTYPE_VOICE_LFO, INSTRUMENT_BIND_LFO_TARGET_VOICE_2),
    ROW_NOBIND("lfo_target_param_2", "LFO", "DstParam2", "ds2", DTYPE_TARGET_SELECTION_LFO, INSTRUMENT_BIND_LFO_TARGET_PARAM_2),
    ROW_MENU("transient_wave", "Transnt", "Waveform", "wav", MENU_TRANS, MOD_NONE, transGen.waveform, TYPE_UINT8),
    ROW("transient_vol", "Transnt", "Volume", "vol", DTYPE_0B127, MOD_0_127, transGen.volume, TYPE_FLT),
    ROW("transient_freq", "Transnt", "Frequncy", "frq", DTYPE_0B127, MOD_0_127, transGen.pitch, TYPE_FLT),
};

/* Runtime registry count used by InstrumentManager bounds checks. */
const uint8_t hihat_param_descriptor_count =
    (uint8_t)(sizeof(hihat_param_descriptors) / sizeof(hihat_param_descriptors[0]));

/*
 * Hi-hat voice menu layout.
 *
 * VOICE6 and VOICE7 share one runtime hihat slot. The layout exposes the base
 * amp_envelope_decay descriptor; Menu's generic VOICE7 choke resolver replaces
 * that base descriptor with amp_envelope_decay_choke only when this Choke type
 * is assigned to slot 6. Keeping one table prevents HiHat from carrying a
 * private open-hat menu fork now that `_choke` is the registry convention.
 */
const instrument_menu_page_t hihat_menu_pages[] = {
    {{ HIHAT_PARAM_OSC1_PITCH_COARSE, HIHAT_PARAM_OSC1_PITCH_FINE, HIHAT_PARAM_OSC1_WAVE, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY }},
    {{ HIHAT_PARAM_AMP_ENVELOPE_ATTACK, HIHAT_PARAM_AMP_ENVELOPE_DECAY, HIHAT_PARAM_AMP_ENVELOPE_SLOPE, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY }},
    {{ HIHAT_PARAM_VELO_MOD_DEST, HIHAT_PARAM_VELO_MOD_AMOUNT, HIHAT_PARAM_VELO_VOL_ON_OFF, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY }},
    {{ HIHAT_PARAM_OSC2_PITCH_COARSE, HIHAT_PARAM_OSC3_PITCH_COARSE, HIHAT_PARAM_OSC2_MOD_AMOUNT, HIHAT_PARAM_OSC3_MOD_AMOUNT, HIHAT_PARAM_OSC2_WAVE, HIHAT_PARAM_OSC3_WAVE, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY }},
    {{ HIHAT_PARAM_TRANSIENT_WAVE, HIHAT_PARAM_TRANSIENT_VOL, HIHAT_PARAM_TRANSIENT_FREQ, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY }},
    {{ HIHAT_PARAM_FILTER_FREQ, HIHAT_PARAM_FILTER_RESO, HIHAT_PARAM_FILTER_TYPE, HIHAT_PARAM_FILTER_DRIVE, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY }},
    /*
     * One wide LFO sub-page: oscillator controls, application shape/amounts,
     * then target selector pairs. Menu displays this 16-cell row as four-cell
     * screens behind the same SELECT button, so the old Mix/Voice sub-page
     * below remains accessible.
     */
    {{ HIHAT_PARAM_LFO_RATE, HIHAT_PARAM_LFO_SYNC, HIHAT_PARAM_LFO_WAVE, HIHAT_PARAM_LFO_OFFSET, HIHAT_PARAM_LFO_RETRIGGER_VOICE, HIHAT_PARAM_LFO_POLARITY, HIHAT_PARAM_LFO_AMOUNT, HIHAT_PARAM_LFO_AMOUNT_2, HIHAT_PARAM_LFO_TARGET_VOICE, HIHAT_PARAM_LFO_TARGET_PARAM, HIHAT_PARAM_LFO_TARGET_VOICE_2, HIHAT_PARAM_LFO_TARGET_PARAM_2, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY }},
    {{ HIHAT_PARAM_INSTRUMENT_VOL, HIHAT_PARAM_INSTRUMENT_PAN, HIHAT_PARAM_INSTRUMENT_DECIMATION, HIHAT_PARAM_INSTRUMENT_DRIVE, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY }},
};

/* Number of instrument-owned subpages exposed for each HiHat voice page. */
const uint8_t hihat_menu_page_count =
    (uint8_t)(sizeof(hihat_menu_pages) / sizeof(hihat_menu_pages[0]));

_Static_assert(HIHAT_MENU_PAGE_COUNT ==
                   (sizeof(hihat_menu_pages) / sizeof(hihat_menu_pages[0])),
               "HIHAT_MENU_PAGE_COUNT must match hihat_menu_pages");

#undef FLAGS_IMAGE
#undef BIND
#undef ROW
#undef ROW_MENU
#undef ROW_SLOT_DECIMATION
#undef ROW_NOBIND
#undef ROW_NOBIND_IMAGE
#undef MOD_WAVE
#undef MOD_PM63
#undef MOD_0_127
#undef MOD_NONE
