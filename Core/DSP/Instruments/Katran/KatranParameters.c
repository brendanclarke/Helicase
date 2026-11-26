#include "KatranParameters.h"
#include "KatranVoice.h"
#include "menu.h"
#include "MenuText.h"
#include <stddef.h>
#include <string.h>

/*
 * Katran parameter source of truth (S103 P1; plan revision 3 §8).
 *
 * What:       what each Kit storage cell means when the slot holds a Katran:
 *             key, labels, dtype, flags, modulation domain, runtime binding.
 *             Every row on one line (check_special_tags.py and
 *             make_init_kat.py parse them). The enum is in page order.
 * Why:        the musically relevant levers only (the Q1 answer), plus the
 *             platform's 21 rows; the LFO page is the platform's, unchanged.
 *             58 rows; 58..59 free; 60..63 reserved for the insert effect
 *             (P§9).
 * S105 P1:    the reorganisation of S105_P1_KATRAN_FURTHER_TWEAKS.md §A1:
 *             every row's category / long / short agree (the short name
 *             abbreviates the long name); seven chaos rows became four
 *             (A3); F1 + F2 became the band (A4: cut wid ofs); src became
 *             a type per oscillator (A8: kat_b_type, kat_b_wave); the ring
 *             rows (dpt r/f sdb) and the output drive were added. Freed
 *             indices were reused in place and kat_rotate moved 57 -> 37,
 *             so the table stays one row per index with no hole. No
 *             migration (the standing rule): an old Scene's byte at a reused
 *             index loads into the new row.
 * Not offered: srt (no SRR), osc1_run (free-running), chn, a gate, an amp
 *             ADSR (the chaos envelopes, P§3.3), the sub (P§6.2), every
 *             fixed value of P§8.3.
 * Affiliates: KatranVoice.h; MenuText.h (katOscTypeNames, katRingModeNames);
 *             tools/katran/make_init_kat.py;
 *             SD_CARD/Instrument/Katran/init.kat.
 */

/* Load-menu label and type flags, imported by the registry. "Katran" is 6
 * cells; the Morph row shows "KatraMrp" (menu.c, 5 characters + Mrp). */
const char katran_instrument_display_label[] = "Katran";
const uint8_t katran_instrument_type_flags = KATRAN_INSTRUMENT_TYPE_FLAGS;

/* A normal sound cell: in both images, Morph-interpolated, a modulation
 * destination and step-automatable; written under [params] and [morph]. */
#define FLAGS_IMAGE \
    (INSTRUMENT_PARAM_FLAG_MORPHABLE | INSTRUMENT_PARAM_FLAG_MODULATABLE | \
     INSTRUMENT_PARAM_FLAG_AUTOMATABLE)

/* Runtime binding into the slot's KatranVoice, with an optional special-writer
 * tag (S073 Step 2). */
#define BIND_SPECIAL(member_, type_, special_) \
    { INSTRUMENT_BIND_INSTANCE_OFFSET, (uint16_t)offsetof(KatranVoice, member_), type_, special_ }
#define BIND(member_, type_) BIND_SPECIAL(member_, type_, IM_SPECIAL_NONE)

/* Modulation domains in descriptor units (AlkalineParameters.c). */
#define MOD_NONE  { 0u, 0u, INSTRUMENT_MOD_DOMAIN_NONE }
#define MOD_0_127 { 0u, 127u, INSTRUMENT_MOD_DOMAIN_CONTINUOUS }
#define MOD_PM63  { 0u, 127u, INSTRUMENT_MOD_DOMAIN_CONTINUOUS }
/* A wave row's domain: integers, the maximum from the slot's type
 * (instrumentManager_waveDomainMax(): Saw..Sin under wav, Tri..Rec under
 * pwm, the installed tables under wtb). */
#define MOD_WAVE  { 0u, 0u, (uint8_t)(INSTRUMENT_MOD_DOMAIN_INTEGER | INSTRUMENT_MOD_DOMAIN_DYNAMIC_MAX) }

/* Row shapes (AlkalineParameters.c / ColloidParameters.c). */
#define ROW(key_, cat_, long_, short_, dtype_, mod_, member_, type_) \
    { key_, short_, long_, cat_, dtype_, FLAGS_IMAGE, mod_, BIND(member_, type_) }
#define ROW_SPECIAL(key_, cat_, long_, short_, dtype_, mod_, member_, type_, special_) \
    { key_, short_, long_, cat_, dtype_, FLAGS_IMAGE, mod_, BIND_SPECIAL(member_, type_, special_) }
#define ROW_MENU(key_, cat_, long_, short_, menu_, mod_, member_, type_) \
    { key_, short_, long_, cat_, (uint8_t)(DTYPE_MENU | (menu_ << 4)), FLAGS_IMAGE, mod_, BIND(member_, type_) }
#define ROW_MENU_SPECIAL(key_, cat_, long_, short_, menu_, mod_, member_, type_, special_) \
    { key_, short_, long_, cat_, (uint8_t)(DTYPE_MENU | (menu_ << 4)), FLAGS_IMAGE, mod_, BIND_SPECIAL(member_, type_, special_) }
/* A tagged DTYPE_ON_OFF selector with a special writer (typ: the Alkaline's
 * ROW_SELECT_ONOFF_SPECIAL): AUTOMATABLE | SELECTOR, never morphed, not an
 * LFO/velocity target. */
#define ROW_SELECT_ONOFF_SPECIAL(key_, cat_, long_, short_, tag_, member_, type_, special_) \
    { key_, short_, long_, cat_, (uint8_t)(DTYPE_ON_OFF | ((tag_) << 4)), \
      (INSTRUMENT_PARAM_FLAG_AUTOMATABLE | INSTRUMENT_PARAM_FLAG_SELECTOR), \
      MOD_NONE, BIND_SPECIAL(member_, type_, special_) }
/* A DTYPE_ON_OFF selector with the generic store. */
#define ROW_SELECT_ONOFF(key_, cat_, long_, short_, tag_, member_, type_) \
    { key_, short_, long_, cat_, (uint8_t)(DTYPE_ON_OFF | ((tag_) << 4)), \
      (INSTRUMENT_PARAM_FLAG_AUTOMATABLE | INSTRUMENT_PARAM_FLAG_SELECTOR), \
      MOD_NONE, BIND(member_, type_) }
/* Cells with no KatranVoice member (target selectors), flags 0: [params]
 * only. */
#define ROW_NOBIND(key_, cat_, long_, short_, dtype_, bind_kind_) \
    { key_, short_, long_, cat_, dtype_, 0u, MOD_NONE, { bind_kind_, 0u, 0u, IM_SPECIAL_NONE } }
/* Image cells applied through a supplemental binding (velocity amount). */
#define ROW_NOBIND_IMAGE(key_, cat_, long_, short_, dtype_, mod_, bind_kind_) \
    { key_, short_, long_, cat_, dtype_, FLAGS_IMAGE, mod_, { bind_kind_, 0u, 0u, IM_SPECIAL_NONE } }

/*
 * Katran-local descriptor indices (S103 P1; S105 P1 reorganised).
 *
 * What:       the order of katran_param_descriptors[]: Scene/Kit storage,
 *             automation IDs, LFO/velocity tokens and NRPN numbers (slot x 64
 *             + index) use these numbers, and the file writer emits keys in
 *             this order.
 * Why:        S105 P1 (§A1.4): the rows A3, A4 and A8 removed left indices
 *             6, 10..15, 34..37; the new rows took them in place, kat_rotate
 *             moved from 57 to 37 and kat_b_wave took 57, so the table stays
 *             contiguous at 58 rows. 58..59 are free (Part B's two rows);
 *             60..63 stay reserved for the insert effect.
 */
typedef enum {
    KAT_PARAM_OSC1_PITCH_COARSE = 0u,
    KAT_PARAM_OSC1_PITCH_FINE,
    KAT_PARAM_OSC1_POSITION,
    KAT_PARAM_SPREAD_A,
    KAT_PARAM_WAVE,
    KAT_PARAM_OSC1_TYPE,
    KAT_PARAM_B_TYPE,
    KAT_PARAM_DAMP,
    KAT_PARAM_RUN,
    KAT_PARAM_KICK,
    KAT_PARAM_CHAOS_CYCLE,
    KAT_PARAM_CHAOS_SKEW,
    KAT_PARAM_CHAOS_IMOD,
    KAT_PARAM_RING_MODE,
    KAT_PARAM_RING_SIDEBAND,
    KAT_PARAM_OUT_DRIVE,
    KAT_PARAM_CHAOS,
    KAT_PARAM_PLFO_RATE,
    KAT_PARAM_PLFO_MOD,
    KAT_PARAM_PLFO_LEVEL,
    KAT_PARAM_VCA,
    KAT_PARAM_B_COARSE,
    KAT_PARAM_B_FINE,
    KAT_PARAM_B_POS,
    KAT_PARAM_RING_DEPTH,
    KAT_PARAM_LEVEL_A,
    KAT_PARAM_NOISE,
    KAT_PARAM_SYNC,
    KAT_PARAM_DRIVE,
    KAT_PARAM_WET,
    KAT_PARAM_TIME,
    KAT_PARAM_FEEDBACK,
    KAT_PARAM_DRIFT,
    KAT_PARAM_TRACK,
    KAT_PARAM_FLT_CUT,
    KAT_PARAM_FLT_WIDTH,
    KAT_PARAM_FLT_OFFSET,
    KAT_PARAM_ROTATE,
    KAT_PARAM_OUT_CUT,
    KAT_PARAM_INSTRUMENT_VOL,
    KAT_PARAM_INSTRUMENT_PAN,
    KAT_PARAM_SPREAD,
    KAT_PARAM_WIDTH,
    KAT_PARAM_LFO_RATE,
    KAT_PARAM_LFO_AMOUNT,
    KAT_PARAM_LFO_AMOUNT_2,
    KAT_PARAM_LFO_WAVE,
    KAT_PARAM_LFO_RETRIGGER_VOICE,
    KAT_PARAM_LFO_POLARITY,
    KAT_PARAM_LFO_SYNC,
    KAT_PARAM_LFO_OFFSET,
    KAT_PARAM_VELO_MOD_AMOUNT,
    KAT_PARAM_VELO_MOD_DEST,
    KAT_PARAM_LFO_TARGET_VOICE,
    KAT_PARAM_LFO_TARGET_PARAM,
    KAT_PARAM_LFO_TARGET_VOICE_2,
    KAT_PARAM_LFO_TARGET_PARAM_2,
    KAT_PARAM_B_WAVE,
    KAT_PARAM_COUNT
} kat_param_index_t;

_Static_assert(KATRAN_PARAM_DESCRIPTOR_COUNT == KAT_PARAM_COUNT,
               "KATRAN_PARAM_DESCRIPTOR_COUNT must match kat_param_index_t");
/* P§9: the four effect rows must fit after the last Basic row. */
_Static_assert(KAT_PARAM_COUNT <= KATRAN_PARAM_FX_FIRST,
               "Katran Basic rows past 59");
_Static_assert(KATRAN_PARAM_FX_FIRST + 4u <= INSTRUMENT_PARAM_COUNT,
               "the reserved Katran effect rows must fit the 64-cell image");
/* no Basic key may start with fx_ (the merge's keys) is checked by
 * make_init_kat.py --check, not here */

/*
 * Katran parameter descriptors.
 *
 * TYPE_UINT8 for every own row: the async converts the byte, so an LFO or
 * velocity overlay writes the same 0..127 domain. instrument_vol is TYPE_FLT
 * (the mixer reads vol as a float), as every engine.
 */
const ParamDescriptor katran_param_descriptors[] = {
    /* --- Osc 1: pitch, position, detune, its type and wave --- */
    ROW_SPECIAL("osc1_pitch_coarse", CAT_OSC1, LONG_COARSE, SHORT_COARSE, DTYPE_0B127, MOD_0_127, osc.modNodeValue, TYPE_SPECIAL_F, IM_SPECIAL_PITCH_COARSE | IM_SPECIAL_OSC1),
    ROW_SPECIAL("osc1_pitch_fine", CAT_OSC1, LONG_FINE, SHORT_FINE, DTYPE_PM63, MOD_PM63, osc.modNodeValue, TYPE_SPECIAL_F, IM_SPECIAL_PITCH_FINE | IM_SPECIAL_OSC1),
    ROW("osc1_position", CAT_OSC1, LONG_POSITION, SHORT_POS, DTYPE_0B127, MOD_0_127, osc.position, TYPE_UINT8),
    ROW("kat_spread_a", CAT_OSC1, LONG_DETUNE, SHORT_DTN, DTYPE_0B127, MOD_0_127, spreadA, TYPE_UINT8),
    ROW_MENU("osc1_wave", CAT_OSC1, LONG_WAVE, SHORT_WAVE, MENU_WAVEFORM, MOD_WAVE, osc.waveform, TYPE_UINT8),
    ROW_SELECT_ONOFF_SPECIAL("osc1_type", CAT_OSC1, LONG_TYPE, SHORT_FIL_TYPE, ONOFF_TAG_KAT_OSC_TYPE, osc.oscType, TYPE_UINT8, IM_SPECIAL_OSC_TYPE | IM_SPECIAL_OSC1),
    ROW_SELECT_ONOFF_SPECIAL("kat_b_type", CAT_OSC2, LONG_TYPE, SHORT_FIL_TYPE, ONOFF_TAG_KAT_OSC_TYPE, bType, TYPE_UINT8, IM_SPECIAL_OSC_TYPE | IM_SPECIAL_OSC2),
    ROW("kat_damp", CAT_BUFFER, LONG_DAMPING, SHORT_DMP, DTYPE_0B127, MOD_0_127, damp_, TYPE_UINT8),
    /* --- aeg: run, kick, the exciter and the four chaos rows --- */
    ROW("kat_run", CAT_VOICE, LONG_RUN, SHORT_PLAY_MODE, DTYPE_0B127, MOD_0_127, run, TYPE_UINT8),
    ROW("kat_kick", CAT_TRIGGER, LONG_KICK, SHORT_KIK, DTYPE_0B127, MOD_0_127, kick, TYPE_UINT8),
    ROW("kat_chaos_cycle", CAT_CHAOS, LONG_CYCLE, SHORT_CYC, DTYPE_0B127, MOD_0_127, chaosCycle, TYPE_UINT8),
    ROW("kat_chaos_skew", CAT_CHAOS, LONG_SKEW, SHORT_SKW, DTYPE_PM63, MOD_PM63, chaosSkew, TYPE_UINT8),
    ROW("kat_chaos_imod", CAT_CHAOS, LONG_INTERMOD, SHORT_IMD, DTYPE_0B127, MOD_0_127, chaosImod, TYPE_UINT8),
    /* --- Ring/FM: Osc 2's action on the cores, then the output drive --- */
    ROW_SELECT_ONOFF("kat_ring_mode", CAT_RING_FM, LONG_MODE, SHORT_RFM, ONOFF_TAG_KAT_RING_MODE, ringMode, TYPE_UINT8),
    ROW("kat_ring_sideband", CAT_RING_FM, LONG_SIDEBAND, SHORT_SDB, DTYPE_0B127, MOD_0_127, ringSideband, TYPE_UINT8),
    ROW("kat_out_drive", CAT_VOICE, LONG_DRIVE_WORD, SHORT_DRIVE, DTYPE_0B127, MOD_0_127, outDrive, TYPE_UINT8),
    ROW("kat_chaos", CAT_CHAOS, LONG_DEPTH, SHORT_CHS, DTYPE_0B127, MOD_0_127, chaos, TYPE_UINT8),
    /* --- mod: the patch LFO, the VCA balance, the stereo pair --- */
    /* S105 P1 A2: the step-synced rate (DTYPE_0B127_KAT_RATE names the stops) */
    ROW("kat_plfo_rate", CAT_MOD_OSC, LONG_RATE, SHORT_RTE, DTYPE_0B127_KAT_RATE, MOD_0_127, plfoRate, TYPE_UINT8),
    ROW("kat_plfo_mod", CAT_MOD_OSC, LONG_SELFMOD, SHORT_SLF, DTYPE_0B127, MOD_0_127, plfoMod, TYPE_UINT8),
    ROW("kat_plfo_level", CAT_MOD_OSC, LONG_DEPTH, SHORT_FXD, DTYPE_0B127, MOD_0_127, plfoLevel, TYPE_UINT8),
    ROW("kat_vca", CAT_VCA, LONG_BALANCE, SHORT_BAL, DTYPE_PM63, MOD_PM63, vca, TYPE_UINT8),
    /* --- Osc 2: pitch, position, the ring depth --- */
    ROW("kat_b_coarse", CAT_OSC2, LONG_COARSE, SHORT_COARSE, DTYPE_0B127, MOD_0_127, bCoarse, TYPE_UINT8),
    ROW("kat_b_fine", CAT_OSC2, LONG_FINE, SHORT_FINE, DTYPE_PM63, MOD_PM63, bFine, TYPE_UINT8),
    ROW("kat_b_pos", CAT_OSC2, LONG_POSITION, SHORT_POS, DTYPE_0B127, MOD_0_127, bPos, TYPE_UINT8),
    ROW("kat_ring_depth", CAT_RING_FM, LONG_DEPTH, SHORT_FXD, DTYPE_0B127, MOD_0_127, ringDepth, TYPE_UINT8),
    ROW("kat_level_a", CAT_EXCITER, LONG_LEVEL, SHORT_EXC, DTYPE_0B127, MOD_0_127, levelA, TYPE_UINT8),
    ROW("kat_noise", CAT_EXCITER, LONG_NOISEMIX, SHORT_NOISE, DTYPE_0B127, MOD_0_127, noise, TYPE_UINT8),
    ROW("kat_sync", CAT_OSC1, LONG_SYNCTHR, SHORT_SYNC, DTYPE_0B127, MOD_0_127, sync, TYPE_UINT8),
    /* --- click: the loop (Torcido, the delay) and its spread --- */
    ROW("kat_drive", CAT_TORCIDO, LONG_DRIVE_WORD, SHORT_DRIVE, DTYPE_0B127, MOD_0_127, drive, TYPE_UINT8),
    ROW("kat_wet", CAT_TORCIDO, LONG_BLEND, SHORT_BLN, DTYPE_0B127, MOD_0_127, wet, TYPE_UINT8),
    ROW("kat_time", CAT_DELAY, LONG_TIME, SHORT_MICROTIMING, DTYPE_0B127, MOD_0_127, time, TYPE_UINT8),
    ROW("kat_feedback", CAT_DELAY, LONG_FEEDBACK, SHORT_FBK, DTYPE_0B127, MOD_0_127, feedback, TYPE_UINT8),
    ROW("kat_drift", CAT_DELAY, LONG_WOWFLUTR, SHORT_WOW, DTYPE_0B127, MOD_0_127, drift_, TYPE_UINT8),
    ROW("kat_track", CAT_DELAY, LONG_TRACK, SHORT_DTK, DTYPE_0B127, MOD_0_127, track, TYPE_UINT8),
    /* --- filter: the band (S105 P1 A4) --- */
    ROW("kat_flt_cut", CAT_FILTER, LONG_CUTOFF, SHORT_CUT, DTYPE_PM63, MOD_PM63, fltCut, TYPE_UINT8),
    ROW("kat_flt_width", CAT_FILTER, LONG_WIDTH, SHORT_WID, DTYPE_0B127, MOD_0_127, fltWidth, TYPE_UINT8),
    ROW("kat_flt_offset", CAT_FILTER, LONG_OFFSET, SHORT_OFFSET, DTYPE_PM63, MOD_PM63, fltOffset, TYPE_UINT8),
    /* the stereo rotation (S103 P4); index 37 since S105 P1 (it was 57) */
    ROW("kat_rotate", CAT_STEREO, LONG_ROTATION, SHORT_ROTATION, DTYPE_0B127, MOD_0_127, rotate, TYPE_UINT8),
    /* the output tone (F3, after the matrix since S105 P1 A5) */
    ROW("kat_out_cut", CAT_VOICE, LONG_TONE, SHORT_TON, DTYPE_PM63, MOD_PM63, outCut, TYPE_UINT8),
    /* --- mix: vol, pan, the spread and the width --- */
    ROW("instrument_vol", CAT_VOICE, LONG_VOL, SHORT_VOL, DTYPE_0B127, MOD_0_127, vol, TYPE_FLT),
    ROW("instrument_pan", CAT_VOICE, LONG_PAN, SHORT_PAN, DTYPE_PM63, MOD_PM63, pan, TYPE_UINT8),
    ROW("kat_spread", CAT_DELAY, LONG_SPREAD, SHORT_SPR, DTYPE_PM63, MOD_PM63, spread, TYPE_UINT8),
    /* S103 P5: wid bipolar (stored 63 = 0, the one-pan rule's display):
     * 0 full spread, no collapse; + wider; 0..-12 blends to the collapsing
     * law; -12..-63 narrows to mono (KatranVoice.c kat_stereoTargets()) */
    ROW("kat_width", CAT_STEREO, LONG_WIDTH, SHORT_WID, DTYPE_PM63, MOD_PM63, width, TYPE_UINT8),
    /* --- the LFO pair: the platform's rows (AlkalineParameters.c) --- */
    ROW_SPECIAL("lfo_rate", CAT_LFO, LONG_FREQ, SHORT_FREQ, DTYPE_0B127, MOD_0_127, lfo.modNodeValue, TYPE_SPECIAL_F, IM_SPECIAL_LFO_RATE),
    ROW("lfo_amount", CAT_LFO, LONG_AMOUNT, SHORT_AM1, DTYPE_0B127, MOD_0_127, lfo.modTarget.amount, TYPE_FLT),
    ROW("lfo_amount_2", CAT_LFO, LONG_AMOUNT_2, SHORT_AM2, DTYPE_0B127, MOD_0_127, lfo.modTarget2.amount, TYPE_FLT),
    ROW_MENU("lfo_wave", CAT_LFO, LONG_WAVE, SHORT_WAVE, MENU_LFO_WAVES, MOD_NONE, lfo.waveform, TYPE_UINT8),
    ROW_MENU("lfo_retrigger_voice", CAT_LFO, LONG_RETRIGGER, SHORT_RETRIGGER, MENU_RETRIGGER, MOD_NONE, lfo.retrigger, TYPE_UINT8),
    ROW("lfo_polarity", CAT_LFO, LONG_POLARITY, SHORT_POL, DTYPE_LFO_POLARITY, MOD_NONE, lfo.polarity, TYPE_UINT8),
    ROW_MENU_SPECIAL("lfo_sync", CAT_LFO, LONG_CLOCKSYNC, SHORT_SYNC, MENU_SYNC_RATES, MOD_NONE, lfo.sync, TYPE_UINT8, IM_SPECIAL_LFO_SYNC),
    ROW_SPECIAL("lfo_offset", CAT_LFO, LONG_OFFSET, SHORT_OFFSET, DTYPE_0B127, MOD_NONE, lfo.phaseOffset, TYPE_UINT32, IM_SPECIAL_LFO_OFFSET),
    /* --- velocity and the LFO destinations --- */
    ROW_NOBIND_IMAGE("velo_mod_amount", CAT_VELOCITY, LONG_AMOUNT, SHORT_FM_AMNT, DTYPE_0B127, MOD_NONE, INSTRUMENT_BIND_VELOCITY_AMOUNT),
    ROW_NOBIND("velo_mod_dest", CAT_VELOCITY, LONG_DEST_PARAM, SHORT_DEST, DTYPE_TARGET_SELECTION_VELO, INSTRUMENT_BIND_VELOCITY_TARGET),
    ROW_NOBIND("lfo_target_voice", CAT_LFO, LONG_DEST_VOICE, SHORT_VO1, DTYPE_VOICE_LFO, INSTRUMENT_BIND_LFO_TARGET_VOICE),
    ROW_NOBIND("lfo_target_param", CAT_LFO, LONG_DEST_PARAM, SHORT_DS1, DTYPE_TARGET_SELECTION_LFO, INSTRUMENT_BIND_LFO_TARGET_PARAM),
    ROW_NOBIND("lfo_target_voice_2", CAT_LFO, LONG_DSTVOICE2, SHORT_VO2, DTYPE_VOICE_LFO, INSTRUMENT_BIND_LFO_TARGET_VOICE_2),
    ROW_NOBIND("lfo_target_param_2", CAT_LFO, LONG_DSTPARAM2, SHORT_DS2, DTYPE_TARGET_SELECTION_LFO, INSTRUMENT_BIND_LFO_TARGET_PARAM_2),
    /* Osc 2's wave (S105 P1 A8; index 57) */
    ROW_MENU("kat_b_wave", CAT_OSC2, LONG_WAVE, SHORT_WAVE, MENU_WAVEFORM, MOD_WAVE, bWave, TYPE_UINT8),
};

const uint8_t katran_param_descriptor_count =
    (uint8_t)(sizeof(katran_param_descriptors) / sizeof(katran_param_descriptors[0]));

_Static_assert(sizeof(katran_param_descriptors) / sizeof(katran_param_descriptors[0]) ==
                   KAT_PARAM_COUNT,
               "katran_param_descriptors must have one row per index");

/* One empty cell of a 16-cell page. */
#define KAT_E INSTRUMENT_MENU_EMPTY

/*
 * Katran voice menu layout (S103 P1; plan revision 3 §8.2).
 *
 * What:       eight SELECT pages of 16 cells (four-cell screens):
 *               0 osc     coa fin pos dtn | typ wav snc dmp     (Osc 1; S105 P1)
 *               1 aeg     run kik exc noi | chs cyc skw imd     (S105 P1 A3)
 *               2 mod     dst amt rot wid | rte slf dpt bal     (S105 P1)
 *               3 fm      coa fin pos typ | wav dpt r/f sdb     (Osc 2; S105 P1)
 *               4 click   drv bln tim fbk | wow dtk spr --- | (fx dpt rte mix)
 *               5 filter  cut wid ofs ---                       (the band, A4)
 *               6 LFO     the platform layout, unchanged
 *               7 mix     vol pan ton drv                       (S105 P1 A5)
 * Why:        S105 P1 (§A1.2): each page is one block of the diagram. osc
 *             is Osc 1 alone (its type, wave and the sync threshold that
 *             resets it); fm is Osc 2 and what it does to the cores; aeg
 *             holds what feeds and moves the loop (run, kick, the exciter
 *             level and noise the kick boosts, the four chaos rows); mod the
 *             patch LFO, the VCA balance and the stereo pair; click the loop
 *             (Torcido, the delay, spr); filter the band; mix vol pan ton
 *             drv, as Alkaline and Colloid. Click's screen 3 stays empty for
 *             the insert effect.
 */
const instrument_menu_page_t katran_menu_pages[] = {
    /* 0 OSC: Osc 1 */
    {{ KAT_PARAM_OSC1_PITCH_COARSE, KAT_PARAM_OSC1_PITCH_FINE, KAT_PARAM_OSC1_POSITION,
       KAT_PARAM_SPREAD_A, KAT_PARAM_OSC1_TYPE, KAT_PARAM_WAVE, KAT_PARAM_SYNC,
       KAT_PARAM_DAMP, KAT_E, KAT_E, KAT_E, KAT_E, KAT_E, KAT_E, KAT_E, KAT_E }},
    /* 1 AEG: run, kick, the exciter, the four chaos rows */
    {{ KAT_PARAM_RUN, KAT_PARAM_KICK, KAT_PARAM_LEVEL_A, KAT_PARAM_NOISE,
       KAT_PARAM_CHAOS, KAT_PARAM_CHAOS_CYCLE, KAT_PARAM_CHAOS_SKEW, KAT_PARAM_CHAOS_IMOD,
       KAT_E, KAT_E, KAT_E, KAT_E, KAT_E, KAT_E, KAT_E, KAT_E }},
    /* 2 MOD: velocity, the stereo pair; then the patch LFO and the balance */
    {{ KAT_PARAM_VELO_MOD_DEST, KAT_PARAM_VELO_MOD_AMOUNT, KAT_PARAM_ROTATE, KAT_PARAM_WIDTH,
       KAT_PARAM_PLFO_RATE, KAT_PARAM_PLFO_MOD, KAT_PARAM_PLFO_LEVEL, KAT_PARAM_VCA,
       KAT_E, KAT_E, KAT_E, KAT_E, KAT_E, KAT_E, KAT_E, KAT_E }},
    /* 3 FM: Osc 2 and its action on the cores */
    {{ KAT_PARAM_B_COARSE, KAT_PARAM_B_FINE, KAT_PARAM_B_POS, KAT_PARAM_B_TYPE,
       KAT_PARAM_B_WAVE, KAT_PARAM_RING_DEPTH, KAT_PARAM_RING_MODE, KAT_PARAM_RING_SIDEBAND,
       KAT_E, KAT_E, KAT_E, KAT_E, KAT_E, KAT_E, KAT_E, KAT_E }},
    /* 4 click: Torcido, the delay and its spread; cells 8..11 are the reserved
     * effect */
    {{ KAT_PARAM_DRIVE, KAT_PARAM_WET, KAT_PARAM_TIME, KAT_PARAM_FEEDBACK,
       KAT_PARAM_DRIFT, KAT_PARAM_TRACK, KAT_PARAM_SPREAD, KAT_E,
       KAT_E, KAT_E, KAT_E, KAT_E, KAT_E, KAT_E, KAT_E, KAT_E }},
    /* 5 FILTER: the band */
    {{ KAT_PARAM_FLT_CUT, KAT_PARAM_FLT_WIDTH, KAT_PARAM_FLT_OFFSET, KAT_E,
       KAT_E, KAT_E, KAT_E, KAT_E,
       KAT_E, KAT_E, KAT_E, KAT_E, KAT_E, KAT_E, KAT_E, KAT_E }},
    /* 6 LFO: the platform's layout (the Colloid's), unchanged */
    {{ KAT_PARAM_LFO_RATE, KAT_PARAM_LFO_SYNC, KAT_PARAM_LFO_WAVE, KAT_PARAM_LFO_OFFSET,
       KAT_PARAM_LFO_RETRIGGER_VOICE, KAT_PARAM_LFO_POLARITY, KAT_PARAM_LFO_AMOUNT,
       KAT_PARAM_LFO_AMOUNT_2, KAT_PARAM_LFO_TARGET_VOICE, KAT_PARAM_LFO_TARGET_PARAM,
       KAT_PARAM_LFO_TARGET_VOICE_2, KAT_PARAM_LFO_TARGET_PARAM_2,
       KAT_E, KAT_E, KAT_E, KAT_E }},
    /* 7 MIX: vol, pan, tone, drive */
    {{ KAT_PARAM_INSTRUMENT_VOL, KAT_PARAM_INSTRUMENT_PAN, KAT_PARAM_OUT_CUT,
       KAT_PARAM_OUT_DRIVE,
       KAT_E, KAT_E, KAT_E, KAT_E, KAT_E, KAT_E, KAT_E, KAT_E, KAT_E, KAT_E, KAT_E, KAT_E }},
};

const uint8_t katran_menu_page_count =
    (uint8_t)(sizeof(katran_menu_pages) / sizeof(katran_menu_pages[0]));

_Static_assert(KATRAN_MENU_PAGE_COUNT ==
                   (sizeof(katran_menu_pages) / sizeof(katran_menu_pages[0])),
               "KATRAN_MENU_PAGE_COUNT must match katran_menu_pages");

/*
 * The rte stop names (S105 P1 A2).
 *
 * What:       for a value that is one of katPlfoStopRow[], copies the
 *             stop's 3-character name (/64 32t /32 16t d32 /16 8Tr d16 /8
 *             4Tr d/8 /4 d/4 /2 d/2 1br 2br 4br 8br 16b 32b; the first 16
 *             are CrumpBit's crumpBit_grainStopNames, CrumpBitParameters.c
 *             :456, copied here because that table is static) and
 *             returns 1; else 0 (the caller prints the number).
 * Accessors:  menu.c menu_formatDtype3().
 */
uint8_t katranParameters_rateName3(uint8_t value, char out[3])
{
    static const char names[KAT_PLFO_STOPS][4] = {
        "/64", "32t", "/32", "16t", "d32", "/16", "8Tr", "d16",
        "/8 ", "4Tr", "d/8", "/4 ", "d/4", "/2 ", "d/2", "1br",
        "2br", "4br", "8br", "16b", "32b"
    };
    uint32_t i;

    for (i = 0u; i < KAT_PLFO_STOPS; i++) {
        if (value == katPlfoStopRow[i]) {
            memcpy(out, names[i], 3u);
            return 1u;
        }
    }
    return 0u;
}

uint8_t katranParameters_cellInactive(const KatranVoice *voice,
                                      const ParamDescriptor *descriptor)
{
    const ParamDescriptor *const base = katran_param_descriptors;

    if (!voice || !descriptor || descriptor < base ||
        descriptor >= base + KAT_PARAM_COUNT)
        return 0u;
    switch ((kat_param_index_t)(descriptor - base)) {
    case KAT_PARAM_OSC1_POSITION:
        /* S105 P1 A8: free-running wav, and not the arena read */
        return (uint8_t)(voice->osc.oscType == OSC_TYPE_WAV && !voice->aBuf);
    case KAT_PARAM_B_POS:
        return (uint8_t)(voice->bOscType == OSC_TYPE_WAV &&
                         (voice->bType & 127u) != KAT_TYPE_BUF);
    case KAT_PARAM_WAVE:
        /* buf reads no table */
        return voice->aBuf;
    case KAT_PARAM_B_WAVE:
        return (uint8_t)((voice->bType & 127u) == KAT_TYPE_BUF);
    case KAT_PARAM_DAMP:
        /* nothing reads the arena */
        return (uint8_t)(!voice->aBuf && (voice->bType & 127u) != KAT_TYPE_BUF);
    default:
        return 0u;
    }
}

#undef FLAGS_IMAGE
#undef BIND_SPECIAL
#undef BIND
#undef ROW
#undef ROW_SPECIAL
#undef ROW_MENU
#undef ROW_MENU_SPECIAL
#undef ROW_SELECT_ONOFF_SPECIAL
#undef ROW_SELECT_ONOFF
#undef ROW_NOBIND
#undef ROW_NOBIND_IMAGE
#undef KAT_E
#undef MOD_WAVE
#undef MOD_PM63
#undef MOD_0_127
#undef MOD_NONE
