#include "TetraktParameters.h"
#include "TetraktVoice.h"
#include "menu.h"
#include "MenuText.h"
#include <stddef.h>

/*
 * Tetrakt parameter source of truth (S096 P1).
 *
 * What:       what each generic Kit storage cell means when the slot holds a
 *             Tetrakt: file key, menu text (category / long / short, plan
 *             §8.2), dtype, Morph/modulation flags, modulation domain and
 *             the runtime binding into TetraktVoice. The pattern is
 *             ColloidParameters.c; every row is on one line.
 * Why:        S096_P1_TETRAKT_SAMPLE_PLAYER.md §4, §8 (revision 7).
 *             The enum is in page order. From the first release a later row
 *             can only be appended before TKT_PARAM_COUNT (the standing
 *             append-only rule), and the table is already 64 of 64.
 * Not offered: srt (the per-layer SRR replaces it), run, chn, a global tune
 *             (the trigger note transposes the four), W2's curve (fixed).
 * Accessors:  InstrumentManager registry; Menu (pages); Preset/Morph/LFO/
 *             automation through the registry; storage.
 * Affiliates: TetraktVoice.h; MenuText.h (tktLayerNames,
 *             ONOFF_TAG_TKT_LAYERS); tools/tetrakt/make_init_tkt.py;
 *             SD_CARD/Instrument/Tetrakt/init.tkt.
 */

/* Load-menu label and type flags, imported by the registry. "Tetrakt" is 7
 * cells; the Morph row shows "TetraMrp" (menu.c, 5 characters + Mrp). */
const char tetrakt_instrument_display_label[] = "Tetrakt";
const uint8_t tetrakt_instrument_type_flags = TETRAKT_INSTRUMENT_TYPE_FLAGS;

/* A normal sound cell: in both images, Morph-interpolated, a modulation
 * destination and step-automatable; written under [params] and [morph]. */
#define FLAGS_IMAGE \
    (INSTRUMENT_PARAM_FLAG_MORPHABLE | INSTRUMENT_PARAM_FLAG_MODULATABLE | \
     INSTRUMENT_PARAM_FLAG_AUTOMATABLE)

/* Runtime binding into the slot's TetraktVoice, with an optional
 * special-writer tag (S073 Step 2). */
#define BIND_SPECIAL(member_, type_, special_) \
    { INSTRUMENT_BIND_INSTANCE_OFFSET, (uint16_t)offsetof(TetraktVoice, member_), type_, special_ }
#define BIND(member_, type_) BIND_SPECIAL(member_, type_, IM_SPECIAL_NONE)

/* Modulation domains in descriptor units (AlkalineParameters.c). */
#define MOD_NONE  { 0u, 0u, INSTRUMENT_MOD_DOMAIN_NONE }
#define MOD_0_127 { 0u, 127u, INSTRUMENT_MOD_DOMAIN_CONTINUOUS }
#define MOD_PM63  { 0u, 127u, INSTRUMENT_MOD_DOMAIN_CONTINUOUS }

/* Row shapes (AlkalineParameters.c / ColloidParameters.c). */
#define ROW(key_, cat_, long_, short_, dtype_, mod_, member_, type_) \
    { key_, short_, long_, cat_, dtype_, FLAGS_IMAGE, mod_, BIND(member_, type_) }
#define ROW_SPECIAL(key_, cat_, long_, short_, dtype_, mod_, member_, type_, special_) \
    { key_, short_, long_, cat_, dtype_, FLAGS_IMAGE, mod_, BIND_SPECIAL(member_, type_, special_) }
#define ROW_MENU(key_, cat_, long_, short_, menu_, mod_, member_, type_) \
    { key_, short_, long_, cat_, (uint8_t)(DTYPE_MENU | (menu_ << 4)), FLAGS_IMAGE, mod_, BIND(member_, type_) }
#define ROW_MENU_SPECIAL(key_, cat_, long_, short_, menu_, mod_, member_, type_, special_) \
    { key_, short_, long_, cat_, (uint8_t)(DTYPE_MENU | (menu_ << 4)), FLAGS_IMAGE, mod_, BIND_SPECIAL(member_, type_, special_) }
/*
 * A sample-choice row (S096 P1, plan §4.1).
 *
 * What:       DTYPE_MENU | (MENU_WAVEFORM << 4) with AUTOMATABLE | SELECTOR
 *             and no modulation domain: stored as the catalogue index, never
 *             morphed and not an LFO / velocity target.
 * Why:        the wave helpers show the catalogue's sNN code and name, and
 *             clamp to the installed count, once they know the tkt_sample_
 *             key (InstrumentManager.c waveDomainMax, menu.c
 *             menu_formatWaveCell(): S7.7, S9.10). The sample latches at a
 *             restart (plan §3.3), so a Morph between two samples would only
 *             step at hits: it is a selector.
 */
#define ROW_MENU_SELECT(key_, cat_, long_, short_, menu_, member_, type_) \
    { key_, short_, long_, cat_, (uint8_t)(DTYPE_MENU | (menu_ << 4)), \
      (INSTRUMENT_PARAM_FLAG_AUTOMATABLE | INSTRUMENT_PARAM_FLAG_SELECTOR), \
      MOD_NONE, BIND(member_, type_) }
/*
 * A tagged DTYPE_ON_OFF selector (S096 P1, plan §4.3).
 *
 * What:       DTYPE_ON_OFF | (tag << 4) with AUTOMATABLE | SELECTOR and the
 *             generic store: the lon mask (tag ONOFF_TAG_TKT_LAYERS) and the
 *             vel switch (tag 0). Never morphed, not an LFO / velocity target.
 * Why:        step locks, Pattern automation and MIDI all reach one row;
 *             menu_clampCellValue() and menu_onoffTagNames() follow the tag.
 */
#define ROW_SELECT_ONOFF(key_, cat_, long_, short_, tag_, member_, type_) \
    { key_, short_, long_, cat_, (uint8_t)(DTYPE_ON_OFF | ((tag_) << 4)), \
      (INSTRUMENT_PARAM_FLAG_AUTOMATABLE | INSTRUMENT_PARAM_FLAG_SELECTOR), \
      MOD_NONE, BIND(member_, type_) }
/* Cells with no TetraktVoice member (target selectors), flags 0: [params]
 * only. */
#define ROW_NOBIND(key_, cat_, long_, short_, dtype_, bind_kind_) \
    { key_, short_, long_, cat_, dtype_, 0u, MOD_NONE, { bind_kind_, 0u, 0u, IM_SPECIAL_NONE } }
/* Image cells applied through a supplemental binding (velocity amount). */
#define ROW_NOBIND_IMAGE(key_, cat_, long_, short_, dtype_, mod_, bind_kind_) \
    { key_, short_, long_, cat_, dtype_, FLAGS_IMAGE, mod_, { bind_kind_, 0u, 0u, IM_SPECIAL_NONE } }

/*
 * Tetrakt-local descriptor indices (S096 P1).
 *
 * What:       the order of tetrakt_param_descriptors[]: Scene/Kit storage,
 *             automation IDs, LFO/velocity tokens and NRPN numbers (slot x
 *             64 + index) use these numbers. Groups of four are layers A..D
 *             (so a group's layer is index & 3 from its A row).
 * Why:        page order (plan §8.2); the velocity groups (SD3) are found
 *             by their A rows.
 */
typedef enum {
    TKT_PARAM_SAMPLE_A = 0u, TKT_PARAM_SAMPLE_B, TKT_PARAM_SAMPLE_C, TKT_PARAM_SAMPLE_D,
    TKT_PARAM_TUNE_A,        TKT_PARAM_TUNE_B,   TKT_PARAM_TUNE_C,   TKT_PARAM_TUNE_D,
    TKT_PARAM_LEVEL_A,       TKT_PARAM_LEVEL_B,  TKT_PARAM_LEVEL_C,  TKT_PARAM_LEVEL_D,
    TKT_PARAM_DECAY_A,       TKT_PARAM_DECAY_B,  TKT_PARAM_DECAY_C,  TKT_PARAM_DECAY_D,
    TKT_PARAM_PAN_A,         TKT_PARAM_PAN_B,    TKT_PARAM_PAN_C,    TKT_PARAM_PAN_D,
    TKT_PARAM_LAYER_ON,
    TKT_PARAM_VELO_MOD_DEST,
    TKT_PARAM_VELO_MOD_AMOUNT,
    TKT_PARAM_VELO_VOL,
    TKT_PARAM_DRIVE_A,       TKT_PARAM_DRIVE_B,  TKT_PARAM_DRIVE_C,  TKT_PARAM_DRIVE_D,
    TKT_PARAM_SRR_A,         TKT_PARAM_SRR_B,    TKT_PARAM_SRR_C,    TKT_PARAM_SRR_D,
    TKT_PARAM_SPACE_A,       TKT_PARAM_SPACE_B,  TKT_PARAM_SPACE_C,  TKT_PARAM_SPACE_D,
    TKT_PARAM_SPACE_TIME,
    TKT_PARAM_SPACE_FEEDBACK,
    TKT_PARAM_SPACE_DETUNE,
    TKT_PARAM_SPACE_TONE,
    TKT_PARAM_TONE_A,        TKT_PARAM_TONE_B,   TKT_PARAM_TONE_C,   TKT_PARAM_TONE_D,
    TKT_PARAM_TONE_ENV_A,    TKT_PARAM_TONE_ENV_B, TKT_PARAM_TONE_ENV_C, TKT_PARAM_TONE_ENV_D,
    TKT_PARAM_LFO_RATE,
    TKT_PARAM_LFO_AMOUNT,
    TKT_PARAM_LFO_AMOUNT_2,
    TKT_PARAM_LFO_WAVE,
    TKT_PARAM_LFO_RETRIGGER_VOICE,
    TKT_PARAM_LFO_POLARITY,
    TKT_PARAM_LFO_SYNC,
    TKT_PARAM_LFO_OFFSET,
    TKT_PARAM_LFO_TARGET_VOICE,
    TKT_PARAM_LFO_TARGET_PARAM,
    TKT_PARAM_LFO_TARGET_VOICE_2,
    TKT_PARAM_LFO_TARGET_PARAM_2,
    TKT_PARAM_INSTRUMENT_VOL,
    TKT_PARAM_INSTRUMENT_PAN,
    TKT_PARAM_WIDTH,
    TKT_PARAM_SPACE_LEVEL,
    TKT_PARAM_COUNT
} tetrakt_param_index_t;

_Static_assert(TETRAKT_PARAM_DESCRIPTOR_COUNT == TKT_PARAM_COUNT,
               "TETRAKT_PARAM_DESCRIPTOR_COUNT must match tetrakt_param_index_t");
_Static_assert(TKT_PARAM_COUNT <= INSTRUMENT_PARAM_COUNT,
               "Tetrakt rows must fit the 64-cell Kit image");
_Static_assert(TETRAKT_PARAM_LAYER_ON_INDEX == TKT_PARAM_LAYER_ON,
               "TETRAKT_PARAM_LAYER_ON_INDEX names the switch row");

/*
 * Tetrakt parameter descriptors (S096 P1, plan §4, §8.2).
 *
 * Layer rows bind TYPE_UINT8 bytes (SD4); vol is TYPE_FLT (byte / 127, read
 * by the mixer). Labels are the user's (revision 6): category, long name,
 * short name with the layer letter.
 */
const ParamDescriptor tetrakt_param_descriptors[] = {
    /* --- OSC: the samples, the tunes --- */
    ROW_MENU_SELECT("tkt_sample_a", CAT_SAMPLE, LONG_SAMPLE, SHORT_SMA, MENU_WAVEFORM, layer[0].sample, TYPE_UINT8),
    ROW_MENU_SELECT("tkt_sample_b", CAT_SAMPLE, LONG_SAMPLE, SHORT_SMB, MENU_WAVEFORM, layer[1].sample, TYPE_UINT8),
    ROW_MENU_SELECT("tkt_sample_c", CAT_SAMPLE, LONG_SAMPLE, SHORT_SMC, MENU_WAVEFORM, layer[2].sample, TYPE_UINT8),
    ROW_MENU_SELECT("tkt_sample_d", CAT_SAMPLE, LONG_SAMPLE, SHORT_SMD, MENU_WAVEFORM, layer[3].sample, TYPE_UINT8),
    ROW("tkt_tune_a", CAT_SAMPLE, LONG_TUNE, SHORT_TUA, DTYPE_PM63, MOD_PM63, layer[0].tune, TYPE_UINT8),
    ROW("tkt_tune_b", CAT_SAMPLE, LONG_TUNE, SHORT_TUB, DTYPE_PM63, MOD_PM63, layer[1].tune, TYPE_UINT8),
    ROW("tkt_tune_c", CAT_SAMPLE, LONG_TUNE, SHORT_TUC, DTYPE_PM63, MOD_PM63, layer[2].tune, TYPE_UINT8),
    ROW("tkt_tune_d", CAT_SAMPLE, LONG_TUNE, SHORT_TUD, DTYPE_PM63, MOD_PM63, layer[3].tune, TYPE_UINT8),
    /* --- AEG: level, decay, pan --- */
    ROW("tkt_level_a", CAT_SAMPLE, LONG_LEVEL, SHORT_LVA, DTYPE_0B127, MOD_0_127, layer[0].level, TYPE_UINT8),
    ROW("tkt_level_b", CAT_SAMPLE, LONG_LEVEL, SHORT_LVB, DTYPE_0B127, MOD_0_127, layer[1].level, TYPE_UINT8),
    ROW("tkt_level_c", CAT_SAMPLE, LONG_LEVEL, SHORT_LVC, DTYPE_0B127, MOD_0_127, layer[2].level, TYPE_UINT8),
    ROW("tkt_level_d", CAT_SAMPLE, LONG_LEVEL, SHORT_LVD, DTYPE_0B127, MOD_0_127, layer[3].level, TYPE_UINT8),
    ROW("tkt_decay_a", CAT_ENVELOPE, LONG_DECAY, SHORT_DYA, DTYPE_0B127, MOD_0_127, layer[0].decay, TYPE_UINT8),
    ROW("tkt_decay_b", CAT_ENVELOPE, LONG_DECAY, SHORT_DYB, DTYPE_0B127, MOD_0_127, layer[1].decay, TYPE_UINT8),
    ROW("tkt_decay_c", CAT_ENVELOPE, LONG_DECAY, SHORT_DYC, DTYPE_0B127, MOD_0_127, layer[2].decay, TYPE_UINT8),
    ROW("tkt_decay_d", CAT_ENVELOPE, LONG_DECAY, SHORT_DYD, DTYPE_0B127, MOD_0_127, layer[3].decay, TYPE_UINT8),
    ROW("tkt_pan_a", CAT_SAMPLE, LONG_PAN_WORD, SHORT_PNA, DTYPE_PM63, MOD_PM63, layer[0].pan, TYPE_UINT8),
    ROW("tkt_pan_b", CAT_SAMPLE, LONG_PAN_WORD, SHORT_PNB, DTYPE_PM63, MOD_PM63, layer[1].pan, TYPE_UINT8),
    ROW("tkt_pan_c", CAT_SAMPLE, LONG_PAN_WORD, SHORT_PNC, DTYPE_PM63, MOD_PM63, layer[2].pan, TYPE_UINT8),
    ROW("tkt_pan_d", CAT_SAMPLE, LONG_PAN_WORD, SHORT_PND, DTYPE_PM63, MOD_PM63, layer[3].pan, TYPE_UINT8),
    /* --- MOD: the layer switch (four bit cells on the page, plan §4.3), velocity (the Drum's three) --- */
    ROW_SELECT_ONOFF("tkt_layer_on", CAT_SAMPLE, LONG_TRIGGER, SHORT_LON, ONOFF_TAG_TKT_LAYERS, layerOn, TYPE_UINT8),
    ROW_NOBIND("velo_mod_dest", CAT_VELOCITY, LONG_DEST_PARAM, SHORT_DEST, DTYPE_TARGET_SELECTION_VELO, INSTRUMENT_BIND_VELOCITY_TARGET),
    ROW_NOBIND_IMAGE("velo_mod_amount", CAT_VELOCITY, LONG_AMOUNT, SHORT_FM_AMNT, DTYPE_0B127, MOD_NONE, INSTRUMENT_BIND_VELOCITY_AMOUNT),
    ROW_SELECT_ONOFF("velo_vol_on_off", CAT_VELOCITY, LONG_VOLUME_MOD, SHORT_VELOCITY, 0u, veloVol, TYPE_UINT8),
    /* --- FM: drive, SRR --- */
    ROW("tkt_drive_a", CAT_SAMPLE, LONG_DRIVE_WORD, SHORT_DRA, DTYPE_0B127, MOD_0_127, layer[0].drive, TYPE_UINT8),
    ROW("tkt_drive_b", CAT_SAMPLE, LONG_DRIVE_WORD, SHORT_DRB, DTYPE_0B127, MOD_0_127, layer[1].drive, TYPE_UINT8),
    ROW("tkt_drive_c", CAT_SAMPLE, LONG_DRIVE_WORD, SHORT_DRC, DTYPE_0B127, MOD_0_127, layer[2].drive, TYPE_UINT8),
    ROW("tkt_drive_d", CAT_SAMPLE, LONG_DRIVE_WORD, SHORT_DRD, DTYPE_0B127, MOD_0_127, layer[3].drive, TYPE_UINT8),
    ROW("tkt_srr_a", CAT_SAMPLE, LONG_SAMPLERT, SHORT_SRA, DTYPE_0B127, MOD_0_127, layer[0].srr, TYPE_UINT8),
    ROW("tkt_srr_b", CAT_SAMPLE, LONG_SAMPLERT, SHORT_SRB, DTYPE_0B127, MOD_0_127, layer[1].srr, TYPE_UINT8),
    ROW("tkt_srr_c", CAT_SAMPLE, LONG_SAMPLERT, SHORT_SRC, DTYPE_0B127, MOD_0_127, layer[2].srr, TYPE_UINT8),
    ROW("tkt_srr_d", CAT_SAMPLE, LONG_SAMPLERT, SHORT_SRD, DTYPE_0B127, MOD_0_127, layer[3].srr, TYPE_UINT8),
    /* --- click: the sends, the space stage --- */
    ROW("tkt_space_a", CAT_SAMPLE, LONG_SPACE, SHORT_SPA, DTYPE_0B127, MOD_0_127, layer[0].space, TYPE_UINT8),
    ROW("tkt_space_b", CAT_SAMPLE, LONG_SPACE, SHORT_SPB, DTYPE_0B127, MOD_0_127, layer[1].space, TYPE_UINT8),
    ROW("tkt_space_c", CAT_SAMPLE, LONG_SPACE, SHORT_SPC, DTYPE_0B127, MOD_0_127, layer[2].space, TYPE_UINT8),
    ROW("tkt_space_d", CAT_SAMPLE, LONG_SPACE, SHORT_SPD, DTYPE_0B127, MOD_0_127, layer[3].space, TYPE_UINT8),
    /* S103 P3 C3-C6: the space stage's four cells read `tim fbk dtn ton`
     * (the ids the Spc chain, the Space delays and the Colloid use; were
     * `stm sfb sdt sto`, removed). The category keeps them apart in the
     * long view ("Space Time" ...); the layer tones tnA..tnD are on another
     * page. */
    ROW("tkt_space_time", CAT_SPACE, LONG_TIME, SHORT_MICROTIMING, DTYPE_0B127, MOD_0_127, spaceTime, TYPE_UINT8),
    ROW("tkt_space_feedback", CAT_SPACE, LONG_FEEDBACK, SHORT_FBK, DTYPE_0B127, MOD_0_127, spaceFeedback, TYPE_UINT8),
    ROW("tkt_space_detune", CAT_SPACE, LONG_DETUNE, SHORT_DTN, DTYPE_0B127, MOD_0_127, spaceDetune, TYPE_UINT8),
    ROW("tkt_space_tone", CAT_SPACE, LONG_TONE, SHORT_TON, DTYPE_0B127, MOD_0_127, spaceTone, TYPE_UINT8),
    /* --- FILTER: tone, tone envelope --- */
    ROW("tkt_tone_a", CAT_SAMPLE, LONG_TONE, SHORT_TNA, DTYPE_PM63, MOD_PM63, layer[0].tone, TYPE_UINT8),
    ROW("tkt_tone_b", CAT_SAMPLE, LONG_TONE, SHORT_TNB, DTYPE_PM63, MOD_PM63, layer[1].tone, TYPE_UINT8),
    ROW("tkt_tone_c", CAT_SAMPLE, LONG_TONE, SHORT_TNC, DTYPE_PM63, MOD_PM63, layer[2].tone, TYPE_UINT8),
    ROW("tkt_tone_d", CAT_SAMPLE, LONG_TONE, SHORT_TND, DTYPE_PM63, MOD_PM63, layer[3].tone, TYPE_UINT8),
    ROW("tkt_tone_env_a", CAT_TONE, LONG_ENVELOPE, SHORT_TEA, DTYPE_PM63, MOD_PM63, layer[0].toneEnv, TYPE_UINT8),
    ROW("tkt_tone_env_b", CAT_TONE, LONG_ENVELOPE, SHORT_TEB, DTYPE_PM63, MOD_PM63, layer[1].toneEnv, TYPE_UINT8),
    ROW("tkt_tone_env_c", CAT_TONE, LONG_ENVELOPE, SHORT_TEC, DTYPE_PM63, MOD_PM63, layer[2].toneEnv, TYPE_UINT8),
    ROW("tkt_tone_env_d", CAT_TONE, LONG_ENVELOPE, SHORT_TED, DTYPE_PM63, MOD_PM63, layer[3].toneEnv, TYPE_UINT8),
    /* --- the LFO pair (the Colloid's rows, ColloidParameters.c:277-284, :290-293) --- */
    ROW_SPECIAL("lfo_rate", CAT_LFO, LONG_FREQ, SHORT_FREQ, DTYPE_0B127, MOD_0_127, lfo.modNodeValue, TYPE_SPECIAL_F, IM_SPECIAL_LFO_RATE),
    ROW("lfo_amount", CAT_LFO, LONG_AMOUNT, SHORT_AM1, DTYPE_0B127, MOD_0_127, lfo.modTarget.amount, TYPE_FLT),
    ROW("lfo_amount_2", CAT_LFO, LONG_AMOUNT_2, SHORT_AM2, DTYPE_0B127, MOD_0_127, lfo.modTarget2.amount, TYPE_FLT),
    ROW_MENU("lfo_wave", CAT_LFO, LONG_WAVE, SHORT_WAVE, MENU_LFO_WAVES, MOD_NONE, lfo.waveform, TYPE_UINT8),
    ROW_MENU("lfo_retrigger_voice", CAT_LFO, LONG_RETRIGGER, SHORT_RETRIGGER, MENU_RETRIGGER, MOD_NONE, lfo.retrigger, TYPE_UINT8),
    ROW("lfo_polarity", CAT_LFO, LONG_POLARITY, SHORT_POL, DTYPE_LFO_POLARITY, MOD_NONE, lfo.polarity, TYPE_UINT8),
    ROW_MENU_SPECIAL("lfo_sync", CAT_LFO, LONG_CLOCKSYNC, SHORT_SYNC, MENU_SYNC_RATES, MOD_NONE, lfo.sync, TYPE_UINT8, IM_SPECIAL_LFO_SYNC),
    ROW_SPECIAL("lfo_offset", CAT_LFO, LONG_OFFSET, SHORT_OFFSET, DTYPE_0B127, MOD_NONE, lfo.phaseOffset, TYPE_UINT32, IM_SPECIAL_LFO_OFFSET),
    ROW_NOBIND("lfo_target_voice", CAT_LFO, LONG_DEST_VOICE, SHORT_VO1, DTYPE_VOICE_LFO, INSTRUMENT_BIND_LFO_TARGET_VOICE),
    ROW_NOBIND("lfo_target_param", CAT_LFO, LONG_DEST_PARAM, SHORT_DS1, DTYPE_TARGET_SELECTION_LFO, INSTRUMENT_BIND_LFO_TARGET_PARAM),
    ROW_NOBIND("lfo_target_voice_2", CAT_LFO, LONG_DSTVOICE2, SHORT_VO2, DTYPE_VOICE_LFO, INSTRUMENT_BIND_LFO_TARGET_VOICE_2),
    ROW_NOBIND("lfo_target_param_2", CAT_LFO, LONG_DSTPARAM2, SHORT_DS2, DTYPE_TARGET_SELECTION_LFO, INSTRUMENT_BIND_LFO_TARGET_PARAM_2),
    /* --- MIX: the voice (vol, the L-R balance), the width, the space level --- */
    ROW("instrument_vol", CAT_VOICE, LONG_VOL, SHORT_VOL, DTYPE_0B127, MOD_0_127, vol, TYPE_FLT),
    ROW("instrument_pan", CAT_VOICE, LONG_PAN, SHORT_PAN, DTYPE_PM63, MOD_PM63, pan, TYPE_UINT8),
    ROW("tkt_width", CAT_SPACE, LONG_WIDTH, SHORT_WID, DTYPE_0B127, MOD_0_127, width, TYPE_UINT8),
    ROW("tkt_space_level", CAT_SPACE, LONG_LEVEL, SHORT_SLV, DTYPE_0B127, MOD_0_127, spaceLevel, TYPE_UINT8),
};

const uint8_t tetrakt_param_descriptor_count =
    (uint8_t)(sizeof(tetrakt_param_descriptors) / sizeof(tetrakt_param_descriptors[0]));

_Static_assert(sizeof(tetrakt_param_descriptors) / sizeof(tetrakt_param_descriptors[0]) ==
                   TKT_PARAM_COUNT,
               "tetrakt_param_descriptors must have one row per index");

/* One empty cell of a 16-cell page; the four bit cells of the switch (SD1). */
#define TKT_E INSTRUMENT_MENU_EMPTY
#define TKT_BIT(b) INSTRUMENT_MENU_BIT(b)

/*
 * Tetrakt voice menu layout (S096 P1, plan §8.2, the user's revision 6).
 *
 * What:       eight sub-pages at the index of the front-panel SELECT button,
 *             16 cells each, shown as four-cell screens; on every per-layer
 *             screen pot 1 is layer A .. pot 4 layer D:
 *               0 OSC     smA..smD | tuA..tuD
 *               1 AEG     lvA..lvD | dyA..dyD | pnA..pnD
 *               2 MOD     onA..onD (the lon bit cells) | dst amt vel
 *               3 FM      drA..drD | srA..srD
 *               4 click   spA..spD | stm sfb sdt sto
 *               5 FILTER  tnA..tnD | teA..teD
 *               6 LFO     the Drum layout (the Colloid's)
 *               7 MIX     vol pan wid slv (one screen: the next is the
 *                         platform's Scene and fader settings)
 */
const instrument_menu_page_t tetrakt_menu_pages[] = {
    /* 0 OSC */
    {{ TKT_PARAM_SAMPLE_A, TKT_PARAM_SAMPLE_B, TKT_PARAM_SAMPLE_C, TKT_PARAM_SAMPLE_D, TKT_PARAM_TUNE_A, TKT_PARAM_TUNE_B, TKT_PARAM_TUNE_C, TKT_PARAM_TUNE_D, TKT_E, TKT_E, TKT_E, TKT_E, TKT_E, TKT_E, TKT_E, TKT_E }},
    /* 1 AEG */
    {{ TKT_PARAM_LEVEL_A, TKT_PARAM_LEVEL_B, TKT_PARAM_LEVEL_C, TKT_PARAM_LEVEL_D, TKT_PARAM_DECAY_A, TKT_PARAM_DECAY_B, TKT_PARAM_DECAY_C, TKT_PARAM_DECAY_D, TKT_PARAM_PAN_A, TKT_PARAM_PAN_B, TKT_PARAM_PAN_C, TKT_PARAM_PAN_D, TKT_E, TKT_E, TKT_E, TKT_E }},
    /* 2 MOD: the four layer switches, then velocity (the Drum's order) */
    {{ TKT_BIT(0u), TKT_BIT(1u), TKT_BIT(2u), TKT_BIT(3u), TKT_PARAM_VELO_MOD_DEST, TKT_PARAM_VELO_MOD_AMOUNT, TKT_PARAM_VELO_VOL, TKT_E, TKT_E, TKT_E, TKT_E, TKT_E, TKT_E, TKT_E, TKT_E, TKT_E }},
    /* 3 FM */
    {{ TKT_PARAM_DRIVE_A, TKT_PARAM_DRIVE_B, TKT_PARAM_DRIVE_C, TKT_PARAM_DRIVE_D, TKT_PARAM_SRR_A, TKT_PARAM_SRR_B, TKT_PARAM_SRR_C, TKT_PARAM_SRR_D, TKT_E, TKT_E, TKT_E, TKT_E, TKT_E, TKT_E, TKT_E, TKT_E }},
    /* 4 click */
    {{ TKT_PARAM_SPACE_A, TKT_PARAM_SPACE_B, TKT_PARAM_SPACE_C, TKT_PARAM_SPACE_D, TKT_PARAM_SPACE_TIME, TKT_PARAM_SPACE_FEEDBACK, TKT_PARAM_SPACE_DETUNE, TKT_PARAM_SPACE_TONE, TKT_E, TKT_E, TKT_E, TKT_E, TKT_E, TKT_E, TKT_E, TKT_E }},
    /* 5 FILTER */
    {{ TKT_PARAM_TONE_A, TKT_PARAM_TONE_B, TKT_PARAM_TONE_C, TKT_PARAM_TONE_D, TKT_PARAM_TONE_ENV_A, TKT_PARAM_TONE_ENV_B, TKT_PARAM_TONE_ENV_C, TKT_PARAM_TONE_ENV_D, TKT_E, TKT_E, TKT_E, TKT_E, TKT_E, TKT_E, TKT_E, TKT_E }},
    /* 6 LFO: the Drum layout */
    {{ TKT_PARAM_LFO_RATE, TKT_PARAM_LFO_SYNC, TKT_PARAM_LFO_WAVE, TKT_PARAM_LFO_OFFSET, TKT_PARAM_LFO_RETRIGGER_VOICE, TKT_PARAM_LFO_POLARITY, TKT_PARAM_LFO_AMOUNT, TKT_PARAM_LFO_AMOUNT_2, TKT_PARAM_LFO_TARGET_VOICE, TKT_PARAM_LFO_TARGET_PARAM, TKT_PARAM_LFO_TARGET_VOICE_2, TKT_PARAM_LFO_TARGET_PARAM_2, TKT_E, TKT_E, TKT_E, TKT_E }},
    /* 7 MIX */
    {{ TKT_PARAM_INSTRUMENT_VOL, TKT_PARAM_INSTRUMENT_PAN, TKT_PARAM_WIDTH, TKT_PARAM_SPACE_LEVEL, TKT_E, TKT_E, TKT_E, TKT_E, TKT_E, TKT_E, TKT_E, TKT_E, TKT_E, TKT_E, TKT_E, TKT_E }},
};

const uint8_t tetrakt_menu_page_count =
    (uint8_t)(sizeof(tetrakt_menu_pages) / sizeof(tetrakt_menu_pages[0]));

_Static_assert(TETRAKT_MENU_PAGE_COUNT ==
                   (sizeof(tetrakt_menu_pages) / sizeof(tetrakt_menu_pages[0])),
               "TETRAKT_MENU_PAGE_COUNT must match tetrakt_menu_pages");

/*
 * The nine velocity groups' layer-A rows (S096 P1 SD3), in TKT_KIND_* order.
 */
static const uint8_t tkt_groupA[TKT_KIND_COUNT] = {
    TKT_PARAM_TUNE_A, TKT_PARAM_LEVEL_A, TKT_PARAM_DECAY_A, TKT_PARAM_PAN_A,
    TKT_PARAM_DRIVE_A, TKT_PARAM_SRR_A, TKT_PARAM_SPACE_A, TKT_PARAM_TONE_A,
    TKT_PARAM_TONE_ENV_A,
};

uint8_t tetraktParameters_velocityKind(const uint8_t token)
{
    uint8_t k;

    for (k = 0u; k < TKT_KIND_COUNT; k++)
        if (token == tkt_groupA[k])
            return k;
    return TKT_KIND_NONE;
}

uint8_t tetraktParameters_velocityTokenHidden(const uint8_t token)
{
    uint8_t k;

    for (k = 0u; k < TKT_KIND_COUNT; k++)
        if (token > tkt_groupA[k] && token < (uint8_t)(tkt_groupA[k] + TKT_LAYERS))
            return 1u;
    return 0u;
}

#undef FLAGS_IMAGE
#undef BIND_SPECIAL
#undef BIND
#undef ROW
#undef ROW_SPECIAL
#undef ROW_MENU
#undef ROW_MENU_SPECIAL
#undef ROW_MENU_SELECT
#undef ROW_SELECT_ONOFF
#undef ROW_NOBIND
#undef ROW_NOBIND_IMAGE
#undef TKT_E
#undef TKT_BIT
#undef MOD_PM63
#undef MOD_0_127
#undef MOD_NONE

