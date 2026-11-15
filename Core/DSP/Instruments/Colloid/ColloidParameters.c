#include "ColloidParameters.h"
#include "ColloidVoice.h"
#include "menu.h"
#include "MenuText.h"
#include <stddef.h>

/*
 * Colloid parameter source of truth (S095 P2; S095 P3 rows and pages).
 *
 * What:       what each generic Kit storage cell means when the slot holds a
 *             Colloid: file key, menu text, dtype, Morph/modulation flags,
 *             modulation domain and the runtime binding into ColloidVoice.
 *             The pattern is AlkalineParameters.c; every row is on one line
 *             (check_special_tags.py and make_init_col.py parse them).
 * Why:        S095 P2 draft §8 with C-D20 (the platform's typ / wav / pos
 *             shape), as reworked by S095 P3 (the user's notes, 2026-11-12):
 *             the oscillator (coa fin pos psp | wav typ), the amp ADSR, the
 *             sustain switch and the pitch slew on the MOD page, three
 *             literal intervals with detune, spread and motion (FM page), the
 *             filter envelope with the filter drive (click page), one filter
 *             with right-channel offsets and its route (FILTER page), the
 *             post-filter overdrive and tone (MIX page), and the platform
 *             rows every engine shares (vol, pan, the LFO pair, velocity), so
 *             Morph, LFO, automation, MIDI and storage work unchanged.
 *             The enum is in page order (S095 P3 renumbered it: no Colloid
 *             content is stored but init.col, and the no-migration rule).
 * Not offered: srt (no SRR, the brief), run (free-running only), chn,
 *             a gate-length row (C-D8), a pan law of its own (C-D11), glide
 *             time / glide mode / tie (S095 P3 F3: the pitch slew replaces
 *             them), interval sets (F4: three literal intervals).
 * Accessors:  InstrumentManager registry; Menu (pages); Preset/Morph/LFO/
 *             automation through the registry; storage (keys, [morph] for
 *             the MORPHABLE rows only).
 * Affiliates: ColloidVoice.h; MenuText.h (colFiltTypeNames, colRouteNames,
 *             colWaveNames, the ONOFF_TAG_COL_* tags; alkTypeNames for typ);
 *             tools/colloid/make_init_col.py; SD_CARD/Instrument/Colloid/
 *             init.col.
 */

/* Load-menu label and type flags, imported by the registry. "Colloid" is 7
 * cells; the Morph row shows "ColloMrp" (menu.c, 5 characters + Mrp). */
const char colloid_instrument_display_label[] = "Colloid";
const uint8_t colloid_instrument_type_flags = COLLOID_INSTRUMENT_TYPE_FLAGS;

/* A normal sound cell: in both images, Morph-interpolated, a modulation
 * destination and step-automatable; written under [params] and [morph]. */
#define FLAGS_IMAGE \
    (INSTRUMENT_PARAM_FLAG_MORPHABLE | INSTRUMENT_PARAM_FLAG_MODULATABLE | \
     INSTRUMENT_PARAM_FLAG_AUTOMATABLE)

/* Runtime binding into the slot's ColloidVoice, with an optional
 * special-writer tag (S073 Step 2). */
#define BIND_SPECIAL(member_, type_, special_) \
    { INSTRUMENT_BIND_INSTANCE_OFFSET, (uint16_t)offsetof(ColloidVoice, member_), type_, special_ }
#define BIND(member_, type_) BIND_SPECIAL(member_, type_, IM_SPECIAL_NONE)

/* Modulation domains in descriptor units (AlkalineParameters.c). */
#define MOD_NONE  { 0u, 0u, INSTRUMENT_MOD_DOMAIN_NONE }
#define MOD_0_127 { 0u, 127u, INSTRUMENT_MOD_DOMAIN_CONTINUOUS }
#define MOD_PM63  { 0u, 127u, INSTRUMENT_MOD_DOMAIN_CONTINUOUS }
/* a wave row's domain: integers, the maximum from the slot's type
 * (instrumentManager_waveDomainMax(): Saw..Sin under wav, Sin..Rec under
 * pwm (S095 P3 F2), the installed tables under wtb) */
#define MOD_WAVE  { 0u, 0u, (uint8_t)(INSTRUMENT_MOD_DOMAIN_INTEGER | INSTRUMENT_MOD_DOMAIN_DYNAMIC_MAX) }

/* Row shapes (AlkalineParameters.c). */
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
/*
 * A DTYPE_ON_OFF selector with the generic store (S095 P2).
 *
 * What:       DTYPE_ON_OFF | (tag << 4) with AUTOMATABLE | SELECTOR and no
 *             special writer: the byte is stored as it is (TYPE_UINT8).
 *             Tag 0 is a plain on/off cell (sus); tags 5 and 6 name the
 *             Colloid's lists (MenuText.h ONOFF_TAG_COL_ROUTE, _FILT_TYPE).
 * Why:        the selectors are step-automatable (a `sus` off lock ends a
 *             note, C-D8) and never morphed (their in-between values are
 *             other choices). The menu clamps every
 *             edit, MIDI write and recorded automation to the tag's count
 *             (menu_clampCellValue()); the voice clamps on read too, so no
 *             store path can select past a list.
 */
#define ROW_SELECT_ONOFF(key_, cat_, long_, short_, tag_, member_, type_) \
    { key_, short_, long_, cat_, (uint8_t)(DTYPE_ON_OFF | ((tag_) << 4)), \
      (INSTRUMENT_PARAM_FLAG_AUTOMATABLE | INSTRUMENT_PARAM_FLAG_SELECTOR), \
      MOD_NONE, BIND(member_, type_) }
/* Cells with no ColloidVoice member (target selectors), flags 0: [params]
 * only. */
#define ROW_NOBIND(key_, cat_, long_, short_, dtype_, bind_kind_) \
    { key_, short_, long_, cat_, dtype_, 0u, MOD_NONE, { bind_kind_, 0u, 0u, IM_SPECIAL_NONE } }
/* Image cells applied through a supplemental binding (velocity amount). */
#define ROW_NOBIND_IMAGE(key_, cat_, long_, short_, dtype_, mod_, bind_kind_) \
    { key_, short_, long_, cat_, dtype_, FLAGS_IMAGE, mod_, { bind_kind_, 0u, 0u, IM_SPECIAL_NONE } }

/*
 * Colloid-local descriptor indices.
 *
 * What:       the order of colloid_param_descriptors[]; Scene/Kit storage,
 *             automation IDs, LFO/velocity tokens and NRPN numbers (slot x 64
 *             + index) use these numbers, and the file writer emits keys in
 *             this order.
 * Why:        the menu layout names cells by enum, not by number. S095 P3
 *             renumbered the table in page order (no Colloid content is
 *             stored but init.col, which is regenerated; the no-migration
 *             rule); from here a later row is appended before
 *             COL_PARAM_COUNT so every stored index stays (the standing
 *             append-only rule).
 */
typedef enum {
    /* OSC (page 0) */
    COL_PARAM_OSC1_PITCH_COARSE = 0u,
    COL_PARAM_OSC1_PITCH_FINE,
    COL_PARAM_OSC1_POSITION,
    COL_PARAM_POS_SPREAD,
    COL_PARAM_WAVE,
    COL_PARAM_OSC1_TYPE,
    /* AEG (page 1) */
    COL_PARAM_ATTACK,
    COL_PARAM_DECAY,
    COL_PARAM_SUSTAIN_LEVEL,
    COL_PARAM_RELEASE,
    /* MOD (page 2): velocity (below), the sustain switch, the slew */
    COL_PARAM_SUSTAIN,
    COL_PARAM_SLEW,
    /* FM (page 3) */
    COL_PARAM_INTERVAL_1,
    COL_PARAM_INTERVAL_2,
    COL_PARAM_INTERVAL_3,
    COL_PARAM_DETUNE,
    COL_PARAM_SPREAD,
    COL_PARAM_MOTION,
    /* click (page 4): the filter envelope and the filter drive */
    COL_PARAM_FENV_ATTACK,
    COL_PARAM_FENV_DECAY,
    COL_PARAM_FENV_SUSTAIN_LEVEL,
    COL_PARAM_FENV_RELEASE,
    COL_PARAM_FENV_AMOUNT,
    COL_PARAM_DRIVE,
    /* FILTER (page 5) */
    COL_PARAM_CUTOFF,
    COL_PARAM_RESO,
    COL_PARAM_FILT_TYPE,
    COL_PARAM_ROUTE,
    COL_PARAM_CUT_OFS,
    COL_PARAM_RES_OFS,
    COL_PARAM_ENV_OFS,
    COL_PARAM_DRIVE_OFS,
    /* MIX (page 7) */
    COL_PARAM_INSTRUMENT_VOL,
    COL_PARAM_INSTRUMENT_PAN,
    COL_PARAM_TONE,
    COL_PARAM_INSTRUMENT_DRIVE,
    /* LFO (page 6) */
    COL_PARAM_LFO_RATE,
    COL_PARAM_LFO_AMOUNT,
    COL_PARAM_LFO_AMOUNT_2,
    COL_PARAM_LFO_WAVE,
    COL_PARAM_LFO_RETRIGGER_VOICE,
    COL_PARAM_LFO_POLARITY,
    COL_PARAM_LFO_SYNC,
    COL_PARAM_LFO_OFFSET,
    /* velocity (MOD page) */
    COL_PARAM_VELO_MOD_AMOUNT,
    COL_PARAM_VELO_MOD_DEST,
    /* the LFO destinations (page 6) */
    COL_PARAM_LFO_TARGET_VOICE,
    COL_PARAM_LFO_TARGET_PARAM,
    COL_PARAM_LFO_TARGET_VOICE_2,
    COL_PARAM_LFO_TARGET_PARAM_2,
    COL_PARAM_COUNT
} colloid_param_index_t;

_Static_assert(COLLOID_PARAM_DESCRIPTOR_COUNT == COL_PARAM_COUNT,
               "COLLOID_PARAM_DESCRIPTOR_COUNT must match colloid_param_index_t");
_Static_assert(COL_PARAM_COUNT <= INSTRUMENT_PARAM_COUNT,
               "Colloid rows must fit the 64-cell Kit image");

/*
 * Colloid parameter descriptors.
 *
 * TYPE_FLT rows store byte / 127 into a 0..1 float
 * (instrumentManager_writeParameter()); TYPE_UINT8 rows store the byte (the
 * envelope times and glide index colEgK[] directly, so the async needs no
 * float-to-index conversion; an LFO or velocity overlay writes them in the
 * same 0..127 domain). ColloidVoice converts at control rate.
 */
const ParamDescriptor colloid_param_descriptors[] = {
    /* --- OSC --- */
    /* coa / fin: the existing pitch writers (midiFreq's high and low byte,
     * osc_recalcFreq()); the async recomputes the pitch from midiFreq and the
     * note every block, and every oscillator slews to it (slw). Their
     * LFO/velocity target is osc.modNodeValue, the base frequency's
     * multiplier (after the slew). coa 48 = C3 at the default note 63. */
    ROW_SPECIAL("osc1_pitch_coarse", "Oscilltr", "Coarse", "coa", DTYPE_0B127, MOD_0_127, osc.modNodeValue, TYPE_SPECIAL_F, IM_SPECIAL_PITCH_COARSE | IM_SPECIAL_OSC1),
    ROW_SPECIAL("osc1_pitch_fine", "Oscilltr", "Fine", "fin", DTYPE_PM63, MOD_PM63, osc.modNodeValue, TYPE_SPECIAL_F, IM_SPECIAL_PITCH_FINE | IM_SPECIAL_OSC1),
    /* pos: the frame centre under wtb (latched at the trigger, C-D4), the
     * width under pwm (every block, slewed); "---" under wav */
    ROW("osc1_position", "Oscilltr", "Position", "pos", DTYPE_0B127, MOD_0_127, osc.position, TYPE_UINT8),
    /* psp: the frame spread (wtb) or the width spread (pwm) around pos, on
     * the S095 P3 F7 curve (P2's maximum at 40, x5 at 127) */
    ROW("col_pos_spread", "Oscilltr", "PosSprd", "psp", DTYPE_0B127, MOD_0_127, posSpread, TYPE_FLT),
    /* wav: under typ wav Saw Rec Tri Sin, under pwm Sin Tri Saw Rec (S095 P3
     * F2, the platform's list), under wtb the table (wNN); the shared wave
     * helpers key on "osc1_wave" and know the Colloid case
     * (instrumentManager_waveDomainMax(), menu_formatWaveCell()) */
    ROW_MENU("osc1_wave", "Oscilltr", "Waveform", "wav", MENU_WAVEFORM, MOD_WAVE, osc.waveform, TYPE_UINT8),
    /* typ: wav / wtb / pwm, the Alkaline's names (C-D20); the writer is
     * Colloid_setOscType() (IM_SPECIAL_OSC_TYPE's Colloid branch) */
    ROW_SELECT_ONOFF_SPECIAL("osc1_type", "Oscilltr", "Type", "typ", ONOFF_TAG_ALK_TYPE, osc.oscType, TYPE_UINT8, IM_SPECIAL_OSC_TYPE | IM_SPECIAL_OSC1),
    /* --- AEG: the amp ADSR (draft §4.1): times are colEgK rows, 1 ms .. 10 s --- */
    ROW("col_attack", "Amp EG", "Attack", "atk", DTYPE_0B127, MOD_0_127, attack, TYPE_UINT8),
    ROW("col_decay", "Amp EG", "Decay", "dec", DTYPE_0B127, MOD_0_127, decay, TYPE_UINT8),
    ROW("col_sustain_level", "Amp EG", "SustLvl", "slv", DTYPE_0B127, MOD_0_127, sustainLevel, TYPE_FLT),
    ROW("col_release", "Amp EG", "Release", "rel", DTYPE_0B127, MOD_0_127, release, TYPE_UINT8),
    /* --- MOD (after the velocity dst / amt cells) --- */
    /* sus (C-D8): hold at the sustain level while on and the transport runs
     * (C-D19); a step lock or Pattern automation turns it off to end a note.
     * S095 P3 F8: on the MOD page. */
    ROW_SELECT_ONOFF("col_sustain", "Amp EG", "Sustain", "sus", 0u, sustain, TYPE_UINT8),
    /* slw (S095 P3 F3, Q1): the pitch slew rate. 0 instant; 1..127 a constant
     * rate from 1 octave per ms to 1 octave per 10 s. Every oscillator
     * travels to its own target (note, coa, fin, intervals, detune) at it. */
    ROW("col_slew", "Pitch", "Slew", "slw", DTYPE_0B127, MOD_0_127, slew, TYPE_UINT8),
    /* --- FM: the pitch groups and the stack's spread --- */
    /* in1..in3 (S095 P3 F4): literal intervals in semitones, -63..+63 (63 =
     * 0), for the oscillators of groups 1..3 (o mod 4); group 0 is the root */
    ROW("col_interval_1", "Pitch", "Intrvl1", "in1", DTYPE_PM63, MOD_PM63, interval[0], TYPE_UINT8),
    ROW("col_interval_2", "Pitch", "Intrvl2", "in2", DTYPE_PM63, MOD_PM63, interval[1], TYPE_UINT8),
    ROW("col_interval_3", "Pitch", "Intrvl3", "in3", DTYPE_PM63, MOD_PM63, interval[2], TYPE_UINT8),
    /* dtn (F5: up to +-250 cents), spr (F6: x5), mot (F6: depth x5) */
    ROW("col_detune", "Unison", "Detune", "dtn", DTYPE_0B127, MOD_0_127, detune, TYPE_FLT),
    ROW("col_spread", "Unison", "Spread", "spr", DTYPE_0B127, MOD_0_127, spread, TYPE_FLT),
    ROW("col_motion", "Unison", "Motion", "mot", DTYPE_0B127, MOD_0_127, motion, TYPE_FLT),
    /* --- click: the filter ADSR (draft §4.2), its bipolar amount, and the
     * filter drive (S095 P3 F10: moved here from the filter page) --- */
    ROW("col_fenv_attack", "Filt EG", "Attack", "fat", DTYPE_0B127, MOD_0_127, fAttack, TYPE_UINT8),
    ROW("col_fenv_decay", "Filt EG", "Decay", "fde", DTYPE_0B127, MOD_0_127, fDecay, TYPE_UINT8),
    ROW("col_fenv_sustain_level", "Filt EG", "SustLvl", "fsl", DTYPE_0B127, MOD_0_127, fSustain, TYPE_FLT),
    ROW("col_fenv_release", "Filt EG", "Release", "fre", DTYPE_0B127, MOD_0_127, fRelease, TYPE_UINT8),
    ROW("col_fenv_amount", "Filt EG", "Amount", "fam", DTYPE_PM63, MOD_PM63, fAmount, TYPE_FLT),
    ROW("col_drive", "Filter", "Drive", "drv", DTYPE_0B127, MOD_0_127, drive, TYPE_FLT),
    /* --- FILTER (S095 P3 F9): one filter, its route, the right offsets --- */
    ROW("col_cutoff", "Filter", "Cutoff", "frq", DTYPE_0B127, MOD_0_127, cut, TYPE_FLT),
    ROW("col_reso", "Filter", "Reso", "res", DTYPE_0B127, MOD_0_127, res, TYPE_FLT),
    /* typ: the left (str) or first (mono) filter's type */
    ROW_SELECT_ONOFF("col_filt_type", "Filter", "FiltType", "typ", ONOFF_TAG_COL_FILT_TYPE, filtType, TYPE_UINT8),
    /* rte: str (stereo: the right filter the left's type plus offsets), or a
     * type: mono, that filter in series after the first */
    ROW_SELECT_ONOFF("col_route", "Filter", "Route", "rte", ONOFF_TAG_COL_ROUTE, route, TYPE_UINT8),
    /* the right / second filter's offsets in their cell's own units (63 = 0) */
    ROW("col_cut_ofs", "Filter", "CutOfs", "fro", DTYPE_PM63, MOD_PM63, cutOfs, TYPE_FLT),
    ROW("col_res_ofs", "Filter", "ResOfs", "reo", DTYPE_PM63, MOD_PM63, resOfs, TYPE_FLT),
    ROW("col_env_ofs", "Filter", "EnvOfs", "eno", DTYPE_PM63, MOD_PM63, envOfs, TYPE_FLT),
    ROW("col_drive_ofs", "Filter", "DrvOfs", "dro", DTYPE_PM63, MOD_PM63, driveOfs, TYPE_FLT),
    /* --- MIX: the platform rows (the mixer reads vol and pan, C-D11), then
     * the post-filter tone and overdrive (S095 P3 F10, the Alkaline's) --- */
    ROW("instrument_vol", "Voice", "Volume", "vol", DTYPE_0B127, MOD_0_127, vol, TYPE_FLT),
    ROW("instrument_pan", "Voice", "Panning", "pan", DTYPE_PM63, MOD_PM63, pan, TYPE_UINT8),
    /* ton: alkToneC[byte]: 127 = open (exact), lower rolls off toward ~800 Hz */
    ROW("col_tone", "Voice", "Tone", "ton", DTYPE_0B127, MOD_0_127, tone, TYPE_UINT8),
    /* drv: the shared instrument_drive key and labels (the NRPN voice-drive
     * group, the Learn pattern); the byte goes to vcaDrive through
     * IM_SPECIAL_INSTRUMENT_DRIVE's Colloid branch (InstrumentManager.c) */
    ROW_SPECIAL("instrument_drive", "Voice", "Overdriv", "drv", DTYPE_0B127, MOD_0_127, vcaDrive, TYPE_UINT8, IM_SPECIAL_INSTRUMENT_DRIVE),
    /* --- the LFO pair: one oscillator, two destinations (AlkalineParameters.c) --- */
    ROW_SPECIAL("lfo_rate", "LFO", "Frequncy", "frq", DTYPE_0B127, MOD_0_127, lfo.modNodeValue, TYPE_SPECIAL_F, IM_SPECIAL_LFO_RATE),
    ROW("lfo_amount", "LFO", "Amount", "am1", DTYPE_0B127, MOD_0_127, lfo.modTarget.amount, TYPE_FLT),
    ROW("lfo_amount_2", "LFO", "Amount 2", "am2", DTYPE_0B127, MOD_0_127, lfo.modTarget2.amount, TYPE_FLT),
    ROW_MENU("lfo_wave", "LFO", "Waveform", "wav", MENU_LFO_WAVES, MOD_NONE, lfo.waveform, TYPE_UINT8),
    ROW_MENU("lfo_retrigger_voice", "LFO", "Retriggr", "rtg", MENU_RETRIGGER, MOD_NONE, lfo.retrigger, TYPE_UINT8),
    ROW("lfo_polarity", "LFO", "Polarity", "pol", DTYPE_LFO_POLARITY, MOD_NONE, lfo.polarity, TYPE_UINT8),
    ROW_MENU_SPECIAL("lfo_sync", "LFO", "ClockSnc", "snc", MENU_SYNC_RATES, MOD_NONE, lfo.sync, TYPE_UINT8, IM_SPECIAL_LFO_SYNC),
    ROW_SPECIAL("lfo_offset", "LFO", "Offset", "ofs", DTYPE_0B127, MOD_NONE, lfo.phaseOffset, TYPE_UINT32, IM_SPECIAL_LFO_OFFSET),
    /* velocity: it reaches the envelopes only through its dst targets (the
     * vol row, fam), C-D18 */
    ROW_NOBIND_IMAGE("velo_mod_amount", "Velocity", "Amount", "amt", DTYPE_0B127, MOD_NONE, INSTRUMENT_BIND_VELOCITY_AMOUNT),
    ROW_NOBIND("velo_mod_dest", "Velocity", "DstParam", "dst", DTYPE_TARGET_SELECTION_VELO, INSTRUMENT_BIND_VELOCITY_TARGET),
    /* the LFO destinations */
    ROW_NOBIND("lfo_target_voice", "LFO", "DstVoice", "vo1", DTYPE_VOICE_LFO, INSTRUMENT_BIND_LFO_TARGET_VOICE),
    ROW_NOBIND("lfo_target_param", "LFO", "DstParam", "ds1", DTYPE_TARGET_SELECTION_LFO, INSTRUMENT_BIND_LFO_TARGET_PARAM),
    ROW_NOBIND("lfo_target_voice_2", "LFO", "DstVoice2", "vo2", DTYPE_VOICE_LFO, INSTRUMENT_BIND_LFO_TARGET_VOICE_2),
    ROW_NOBIND("lfo_target_param_2", "LFO", "DstParam2", "ds2", DTYPE_TARGET_SELECTION_LFO, INSTRUMENT_BIND_LFO_TARGET_PARAM_2),
};

/* Runtime registry count used by InstrumentManager bounds checks. */
const uint8_t colloid_param_descriptor_count =
    (uint8_t)(sizeof(colloid_param_descriptors) / sizeof(colloid_param_descriptors[0]));

_Static_assert(sizeof(colloid_param_descriptors) / sizeof(colloid_param_descriptors[0]) ==
                   COL_PARAM_COUNT,
               "colloid_param_descriptors must have one row per index");

/* One empty cell of a 16-cell page. */
#define COL_E INSTRUMENT_MENU_EMPTY

/*
 * Colloid voice menu layout (S095 P2; S095 P3 F11).
 *
 * What:       eight sub-pages at the index of the front-panel SELECT button,
 *             each 16 cells shown as four-cell screens:
 *               0 OSC     coa fin pos psp | wav typ
 *               1 AEG     atk dec slv rel
 *               2 MOD     dst amt sus slw
 *               3 FM      in1 in2 in3 dtn | spr mot
 *               4 click   fat fde fsl fre | fam drv (the filter drive)
 *               5 FILTER  frq res typ rte | fro reo eno dro
 *               6 LFO     the Drum layout
 *               7 MIX     vol pan ton drv (the post-filter overdrive)
 * Why:        the user's layout (S095 P3 notes): the same SELECT positions as
 *             the other types (oscillator, amp envelope, modulation, filter,
 *             LFO, mix keep their buttons); the FM and click buttons carry
 *             the Colloid's pitch groups and filter envelope.
 */
const instrument_menu_page_t colloid_menu_pages[] = {
    /* 0 OSC */
    {{ COL_PARAM_OSC1_PITCH_COARSE, COL_PARAM_OSC1_PITCH_FINE, COL_PARAM_OSC1_POSITION, COL_PARAM_POS_SPREAD, COL_PARAM_WAVE, COL_PARAM_OSC1_TYPE, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E }},
    /* 1 AEG */
    {{ COL_PARAM_ATTACK, COL_PARAM_DECAY, COL_PARAM_SUSTAIN_LEVEL, COL_PARAM_RELEASE, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E }},
    /* 2 MOD: velocity, the sustain switch, the slew */
    {{ COL_PARAM_VELO_MOD_DEST, COL_PARAM_VELO_MOD_AMOUNT, COL_PARAM_SUSTAIN, COL_PARAM_SLEW, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E }},
    /* 3 FM: the intervals and detune, then spread and motion */
    {{ COL_PARAM_INTERVAL_1, COL_PARAM_INTERVAL_2, COL_PARAM_INTERVAL_3, COL_PARAM_DETUNE, COL_PARAM_SPREAD, COL_PARAM_MOTION, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E }},
    /* 4 click: the filter envelope, its amount, the filter drive */
    {{ COL_PARAM_FENV_ATTACK, COL_PARAM_FENV_DECAY, COL_PARAM_FENV_SUSTAIN_LEVEL, COL_PARAM_FENV_RELEASE, COL_PARAM_FENV_AMOUNT, COL_PARAM_DRIVE, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E }},
    /* 5 FILTER: the filter and its route, then the right / second offsets */
    {{ COL_PARAM_CUTOFF, COL_PARAM_RESO, COL_PARAM_FILT_TYPE, COL_PARAM_ROUTE, COL_PARAM_CUT_OFS, COL_PARAM_RES_OFS, COL_PARAM_ENV_OFS, COL_PARAM_DRIVE_OFS, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E }},
    /* 6 LFO: the Drum layout */
    {{ COL_PARAM_LFO_RATE, COL_PARAM_LFO_SYNC, COL_PARAM_LFO_WAVE, COL_PARAM_LFO_OFFSET, COL_PARAM_LFO_RETRIGGER_VOICE, COL_PARAM_LFO_POLARITY, COL_PARAM_LFO_AMOUNT, COL_PARAM_LFO_AMOUNT_2, COL_PARAM_LFO_TARGET_VOICE, COL_PARAM_LFO_TARGET_PARAM, COL_PARAM_LFO_TARGET_VOICE_2, COL_PARAM_LFO_TARGET_PARAM_2, COL_E, COL_E, COL_E, COL_E }},
    /* 7 MIX: vol pan, the tone, the overdrive (the Alkaline's cells) */
    {{ COL_PARAM_INSTRUMENT_VOL, COL_PARAM_INSTRUMENT_PAN, COL_PARAM_TONE, COL_PARAM_INSTRUMENT_DRIVE, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E }},
};

const uint8_t colloid_menu_page_count =
    (uint8_t)(sizeof(colloid_menu_pages) / sizeof(colloid_menu_pages[0]));

_Static_assert(COLLOID_MENU_PAGE_COUNT ==
                   (sizeof(colloid_menu_pages) / sizeof(colloid_menu_pages[0])),
               "COLLOID_MENU_PAGE_COUNT must match colloid_menu_pages");

uint8_t colloidParameters_cellInactive(const ColloidVoice *voice,
                                       const ParamDescriptor *descriptor)
{
    /* the row's index, or no answer for a pointer outside this table */
    const ParamDescriptor *const base = colloid_param_descriptors;

    if (!voice || !descriptor || descriptor < base ||
        descriptor >= base + COL_PARAM_COUNT)
        return 0u;
    switch ((colloid_param_index_t)(descriptor - base)) {
    case COL_PARAM_OSC1_POSITION:
    case COL_PARAM_POS_SPREAD:
        /* free-running wav oscillators: no start phase, frame or width */
        return (uint8_t)(voice->osc.oscType == OSC_TYPE_WAV);
    case COL_PARAM_SPREAD:
    case COL_PARAM_MOTION:
        /* a mono route (rte a filter type): positions have no effect (C-D16,
         * S095 P3 F9) */
        return (uint8_t)(voice->route != COL_RTE_STR && voice->route < COL_RTE_COUNT);
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
#undef COL_E
#undef MOD_WAVE
#undef MOD_PM63
#undef MOD_0_127
#undef MOD_NONE
