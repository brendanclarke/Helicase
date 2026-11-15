#include "AlkalineParameters.h"
#include "AlkalineVoice.h"
#include "menu.h"
#include "MenuText.h"
#include <stddef.h>

/*
 * Alkaline parameter source of truth (S093 P4).
 *
 * What:       what each generic Kit storage cell means when the slot holds an
 *             Alkaline: file key, menu text, dtype, Morph/modulation flags,
 *             modulation domain and the runtime binding into AlkalineVoice.
 *             The pattern is DrumParameters.c; every row is on one line
 *             (check_special_tags.py and make_init_alk.py parse them).
 * Why:        S092 P0 §5.2 / S093 P2 §6: the 303 controls (waveform, coarse,
 *             tuning, slide, cutoff, resonance, env mod, decay, accent), the
 *             drive extra (E2), and the platform rows every engine shares
 *             (vol, pan, the LFO pair, velocity), so Morph, LFO, automation,
 *             MIDI and storage work unchanged.
 * Not offered (S092 P0 §5.5): srt (no SRR, D4), run, chn, fine pitch,
 *             velo_vol_on_off (the 303's velocity is the accent, D1).
 *             typ is offered since S093 P8 with two types, wav and wtb
 *             (samples are not: S093_P8_IMPLEMENTATION.md §0).
 * Accessors:  InstrumentManager registry; Menu (pages); Preset/Morph/LFO/
 *             automation through the registry; storage (keys, [morph] for
 *             the MORPHABLE rows only, storageTypes.c l. 981-990).
 * Affiliates: AlkalineVoice.h; MenuText.h (alkWaveNames, alkTypeNames,
 *             ONOFF_TAG_ALK_TYPE);
 *             tools/alkaline/make_init_alk.py; SD_CARD/Instrument/Alkaline/
 *             init.alk.
 */

/* Load-menu label and type flags, imported by the registry. "Alkaline" is 8
 * cells; the Morph row shows "AlkalMrp" (§4.7). */
const char alkaline_instrument_display_label[] = "Alkaline";
const uint8_t alkaline_instrument_type_flags = ALKALINE_INSTRUMENT_TYPE_FLAGS;

/* A normal sound cell: in both images, Morph-interpolated, a modulation
 * destination and step-automatable; written under [params] and [morph]. */
#define FLAGS_IMAGE \
    (INSTRUMENT_PARAM_FLAG_MORPHABLE | INSTRUMENT_PARAM_FLAG_MODULATABLE | \
     INSTRUMENT_PARAM_FLAG_AUTOMATABLE)

/* Runtime binding into the slot's AlkalineVoice, with an optional
 * special-writer tag (S073 Step 2). */
#define BIND_SPECIAL(member_, type_, special_) \
    { INSTRUMENT_BIND_INSTANCE_OFFSET, (uint16_t)offsetof(AlkalineVoice, member_), type_, special_ }
#define BIND(member_, type_) BIND_SPECIAL(member_, type_, IM_SPECIAL_NONE)

/* Modulation domains in descriptor units (DrumParameters.c l. 51-56). */
#define MOD_NONE  { 0u, 0u, INSTRUMENT_MOD_DOMAIN_NONE }
#define MOD_0_127 { 0u, 127u, INSTRUMENT_MOD_DOMAIN_CONTINUOUS }
#define MOD_PM63  { 0u, 127u, INSTRUMENT_MOD_DOMAIN_CONTINUOUS }
/* S093 P8: a wave row's domain (DrumParameters.c l. 57-59): integers, the
 * maximum from the slot's type (instrumentManager_waveDomainMax(): Saw/Sqr
 * under wav, the items under smp/wtb) */
#define MOD_WAVE  { 0u, 0u, (uint8_t)(INSTRUMENT_MOD_DOMAIN_INTEGER | INSTRUMENT_MOD_DOMAIN_DYNAMIC_MAX) }

/* Row shapes (DrumParameters.c l. 70-98). */
#define ROW(key_, cat_, long_, short_, dtype_, mod_, member_, type_) \
    { key_, short_, long_, cat_, dtype_, FLAGS_IMAGE, mod_, BIND(member_, type_) }
#define ROW_SPECIAL(key_, cat_, long_, short_, dtype_, mod_, member_, type_, special_) \
    { key_, short_, long_, cat_, dtype_, FLAGS_IMAGE, mod_, BIND_SPECIAL(member_, type_, special_) }
#define ROW_MENU(key_, cat_, long_, short_, menu_, mod_, member_, type_) \
    { key_, short_, long_, cat_, (uint8_t)(DTYPE_MENU | (menu_ << 4)), FLAGS_IMAGE, mod_, BIND(member_, type_) }
/* ROW_MENU_SPECIAL: ROW_MENU with a constant DSP writer tag (S094 P1; the
 * Drum table's macro, DrumParameters.c l. 85-87, the same expansion) */
#define ROW_MENU_SPECIAL(key_, cat_, long_, short_, menu_, mod_, member_, type_, special_) \
    { key_, short_, long_, cat_, (uint8_t)(DTYPE_MENU | (menu_ << 4)), FLAGS_IMAGE, mod_, BIND_SPECIAL(member_, type_, special_) }
/*
 * A tagged DTYPE_ON_OFF selector with a special writer (S093 P8; Drum's
 * ROW_SELECT_ONOFF_SPECIAL, DrumParameters.c l. 108-130).
 *
 * What:       DTYPE_ON_OFF | (tag << 4) with AUTOMATABLE | SELECTOR: never
 *             morphed (so not under [morph]), not an LFO/velocity target; the
 *             tag picks the name table in menu_onoffTagNames(); the runtime
 *             write goes to the IM_SPECIAL_* writer.
 * Why:        the Alkaline typ list (wav wtb) is its own two names, and
 *             every DTYPE_MENU id is taken (MenuText.h, S078), so the list is a
 *             tagged on/off sub-type as osc1_run's is.
 */
#define ROW_SELECT_ONOFF_SPECIAL(key_, cat_, long_, short_, tag_, member_, type_, special_) \
    { key_, short_, long_, cat_, (uint8_t)(DTYPE_ON_OFF | ((tag_) << 4)), \
      (INSTRUMENT_PARAM_FLAG_AUTOMATABLE | INSTRUMENT_PARAM_FLAG_SELECTOR), \
      MOD_NONE, BIND_SPECIAL(member_, type_, special_) }
/* Cells with no AlkalineVoice member (target selectors), flags 0: [params]
 * only. */
#define ROW_NOBIND(key_, cat_, long_, short_, dtype_, bind_kind_) \
    { key_, short_, long_, cat_, dtype_, 0u, MOD_NONE, { bind_kind_, 0u, 0u, IM_SPECIAL_NONE } }
/* Image cells applied through a supplemental binding (velocity amount). */
#define ROW_NOBIND_IMAGE(key_, cat_, long_, short_, dtype_, mod_, bind_kind_) \
    { key_, short_, long_, cat_, dtype_, FLAGS_IMAGE, mod_, { bind_kind_, 0u, 0u, IM_SPECIAL_NONE } }

/*
 * Alkaline-local descriptor indices.
 *
 * What:       the order of alkaline_param_descriptors[]; Scene/Kit storage,
 *             automation IDs, LFO/velocity tokens and NRPN numbers (slot x 64
 *             + index) use these numbers, and the file writer emits keys in
 *             this order.
 * Why:        the menu layout names cells by enum, not by number; a new row
 *             is appended before ALK_PARAM_COUNT so every stored index stays.
 */
typedef enum {
    ALK_PARAM_WAVE = 0u,
    ALK_PARAM_OSC1_PITCH_COARSE,
    ALK_PARAM_TUNING,
    ALK_PARAM_SLIDE,
    ALK_PARAM_CUTOFF,
    ALK_PARAM_RESO,
    ALK_PARAM_ENVMOD,
    ALK_PARAM_DECAY,
    ALK_PARAM_ACCENT,
    ALK_PARAM_DRIVE,
    ALK_PARAM_INSTRUMENT_VOL,
    ALK_PARAM_INSTRUMENT_PAN,
    ALK_PARAM_LFO_RATE,
    ALK_PARAM_LFO_AMOUNT,
    ALK_PARAM_LFO_AMOUNT_2,
    ALK_PARAM_LFO_WAVE,
    ALK_PARAM_LFO_RETRIGGER_VOICE,
    ALK_PARAM_LFO_POLARITY,
    ALK_PARAM_LFO_SYNC,
    ALK_PARAM_LFO_OFFSET,
    ALK_PARAM_VELO_MOD_AMOUNT,
    ALK_PARAM_VELO_MOD_DEST,
    ALK_PARAM_LFO_TARGET_VOICE,
    ALK_PARAM_LFO_TARGET_PARAM,
    ALK_PARAM_LFO_TARGET_VOICE_2,
    ALK_PARAM_LFO_TARGET_PARAM_2,
    /*
     * S093 P6: appended rows. Indices 26 and 27 are new, so every stored cell,
     * automation ID and NRPN number of rows 0..25 keeps its meaning.
     */
    ALK_PARAM_ATTACK,            /* 26: VEG attack as the CPU trigger width */
    ALK_PARAM_OSC1_POSITION,     /* 27: note-start phase (0 = free-running) */
    ALK_PARAM_OSC1_TYPE,         /* 28 (S093 P8): wav / wtb */
    /*
     * S094 P1 S4: appended rows (29, 30); every stored cell, automation ID
     * and NRPN number of rows 0..28 keeps its meaning.
     */
    ALK_PARAM_VCA_DRIVE,         /* 29: instrument_drive, the VCA overdrive */
    ALK_PARAM_TONE,              /* 30: alk_tone, the post-filter tone */
    ALK_PARAM_COUNT
} alkaline_param_index_t;

_Static_assert(ALKALINE_PARAM_DESCRIPTOR_COUNT == ALK_PARAM_COUNT,
               "ALKALINE_PARAM_DESCRIPTOR_COUNT must match alkaline_param_index_t");
_Static_assert(ALK_PARAM_COUNT <= INSTRUMENT_PARAM_COUNT,
               "Alkaline rows must fit the 64-cell Kit image");

/*
 * Alkaline parameter descriptors.
 *
 * TYPE_FLT rows store byte / 127 into a 0..1 float (instrumentManager_
 * writeParameter(), InstrumentManager.c l. 2177); AlkalineVoice converts at
 * control rate. Categories use the Drum vocabulary (Oscilltr, Filter, Voice,
 * LFO, Velocity).
 */
const ParamDescriptor alkaline_param_descriptors[] = {
    /*
     * wav (S093 P8: Drum's osc1_wave row shape, DrumParameters.c l. 233).
     *
     * What:       under typ wav: 0 Saw (the stock sawTable, the 303 ramp
     *             within 0.5 %), 1 Sqr (the D8 per-octave Q8 square,
     *             AlkalineSquareTable.c); under wtb: the wavetable, named
     *             wNN. DTYPE_MENU | MENU_WAVEFORM, MOD_WAVE, an image cell
     *             bound to osc.waveform.
     * Why:        one cell names Saw/Sqr or the item by typ (P8 §8); the
     *             shared wave helpers key on "osc1_wave" and know the
     *             Alkaline case (instrumentManager_waveDomainMax(),
     *             menu_formatWaveCell()). As on Drum, Morph and the LFO step
     *             through the entries (under wav: Saw/Sqr switch).
     * Accessors:  alk_renderWav() (square = wav 1); alk_wtbBind() (the
     *             table); the menu wave helpers.
     * Affiliates: ALK_WAVE_SAW / ALK_WAVE_SQUARE; alkWaveNames.
     */
    ROW_MENU("osc1_wave", "Oscilltr", "Waveform", "wav", MENU_WAVEFORM, MOD_WAVE, osc.waveform, TYPE_UINT8),
    /* coa: the existing coarse writer (midiFreq high byte, osc_recalcFreq(),
     * which only sets osc.freq; the Alkaline async recomputes the pitch from
     * midiFreq and the note every block); its LFO/velocity target is
     * osc.modNodeValue, which osc_setTableFreq() multiplies in (T1-7);
     * 36 = C2 at the default note 63 ([SN] p7) */
    ROW_SPECIAL("osc1_pitch_coarse", "Oscilltr", "Coarse", "coa", DTYPE_0B127, MOD_0_127, osc.modNodeValue, TYPE_SPECIAL_F, IM_SPECIAL_PITCH_COARSE | IM_SPECIAL_OSC1),
    /* tun: +-700 cents ([SN] p1, D6) by (tuning x 127 - 63) / 63 x 7/12 octave
     * at control rate; 63 = centre */
    ROW("alk_tuning", "Oscilltr", "Tuning", "tun", DTYPE_PM63, MOD_PM63, tuning, TYPE_FLT),
    /* sld: latched at each trigger, decides the next note (A10); an image
     * ON/OFF cell like Drum's velo_vol_on_off */
    ROW("alk_slide", "Oscilltr", "Slide", "sld", DTYPE_ON_OFF, MOD_NONE, slide, TYPE_UINT8),
    /* the five 303 knobs: VR3 cutoff, VR4 resonance (both gangs), VR5 env
     * mod, VR6 decay, VR7 accent, as tapers in AlkalineControl.c */
    ROW("alk_cutoff", "Filter", "Cutoff", "cut", DTYPE_0B127, MOD_0_127, cutoff, TYPE_FLT),
    ROW("alk_reso", "Filter", "Resnance", "res", DTYPE_0B127, MOD_0_127, reso, TYPE_FLT),
    ROW("alk_envmod", "Filter", "Env Mod", "env", DTYPE_0B127, MOD_0_127, envMod, TYPE_FLT),
    ROW("alk_decay", "Filter", "Decay", "dec", DTYPE_0B127, MOD_0_127, decay, TYPE_FLT),
    ROW("alk_accent", "Voice", "Accent", "acc", DTYPE_0B127, MOD_0_127, accent, TYPE_FLT),
    /* drv: the ladder input x 8^(v/127) (D16) with the output make-up
     * 2^(ALK_MAKEUP_OCT (1 - v/127)) (B-2); 0 = the 303's own drive */
    ROW("alk_drive", "Filter", "Overdriv", "drv", DTYPE_0B127, MOD_0_127, drive, TYPE_FLT),
    /* platform rows (DrumParameters.c l. 257-258): the mixer reads them
     * through instrumentManager_runtimeVolume()/_runtimePan() */
    ROW("instrument_vol", "Voice", "Volume", "vol", DTYPE_0B127, MOD_0_127, vol, TYPE_FLT),
    ROW("instrument_pan", "Voice", "Panning", "pan", DTYPE_PM63, MOD_PM63, pan, TYPE_UINT8),
    /* the LFO pair: one oscillator, two destinations (DrumParameters.c
     * l. 270-281) */
    ROW_SPECIAL("lfo_rate", "LFO", "Frequncy", "frq", DTYPE_0B127, MOD_0_127, lfo.modNodeValue, TYPE_SPECIAL_F, IM_SPECIAL_LFO_RATE),
    ROW("lfo_amount", "LFO", "Amount", "am1", DTYPE_0B127, MOD_0_127, lfo.modTarget.amount, TYPE_FLT),
    ROW("lfo_amount_2", "LFO", "Amount 2", "am2", DTYPE_0B127, MOD_0_127, lfo.modTarget2.amount, TYPE_FLT),
    ROW_MENU("lfo_wave", "LFO", "Waveform", "wav", MENU_LFO_WAVES, MOD_NONE, lfo.waveform, TYPE_UINT8),
    ROW_MENU("lfo_retrigger_voice", "LFO", "Retriggr", "rtg", MENU_RETRIGGER, MOD_NONE, lfo.retrigger, TYPE_UINT8),
    ROW("lfo_polarity", "LFO", "Polarity", "pol", DTYPE_LFO_POLARITY, MOD_NONE, lfo.polarity, TYPE_UINT8),
    /* lfo_sync (S094 P1 S1): the special writer recomputes the LFO's phase
     * step with the mode (IM_SPECIAL_LFO_SYNC -> lfo_setSync()); a plain
     * byte store left the rate at the previous mode's until frq or the tempo
     * moved */
    ROW_MENU_SPECIAL("lfo_sync", "LFO", "ClockSnc", "snc", MENU_SYNC_RATES, MOD_NONE, lfo.sync, TYPE_UINT8, IM_SPECIAL_LFO_SYNC),
    ROW_SPECIAL("lfo_offset", "LFO", "Offset", "ofs", DTYPE_0B127, MOD_NONE, lfo.phaseOffset, TYPE_UINT32, IM_SPECIAL_LFO_OFFSET),
    /* velocity (DrumParameters.c l. 283-284) */
    ROW_NOBIND_IMAGE("velo_mod_amount", "Velocity", "Amount", "amt", DTYPE_0B127, MOD_NONE, INSTRUMENT_BIND_VELOCITY_AMOUNT),
    ROW_NOBIND("velo_mod_dest", "Velocity", "DstParam", "dst", DTYPE_TARGET_SELECTION_VELO, INSTRUMENT_BIND_VELOCITY_TARGET),
    /* the LFO destinations (DrumParameters.c l. 285-288) */
    ROW_NOBIND("lfo_target_voice", "LFO", "DstVoice", "vo1", DTYPE_VOICE_LFO, INSTRUMENT_BIND_LFO_TARGET_VOICE),
    ROW_NOBIND("lfo_target_param", "LFO", "DstParam", "ds1", DTYPE_TARGET_SELECTION_LFO, INSTRUMENT_BIND_LFO_TARGET_PARAM),
    ROW_NOBIND("lfo_target_voice_2", "LFO", "DstVoice2", "vo2", DTYPE_VOICE_LFO, INSTRUMENT_BIND_LFO_TARGET_VOICE_2),
    ROW_NOBIND("lfo_target_param_2", "LFO", "DstParam2", "ds2", DTYPE_TARGET_SELECTION_LFO, INSTRUMENT_BIND_LFO_TARGET_PARAM_2),
    /*
     * S093 P6: atk, the VEG attack as the 303's CPU trigger-pulse width.
     * Byte 0 = a 0.70 ms trigger, today's attack bit for bit; 127 = 0.55 ms
     * (SPICE 10->90 % 2.11 -> 5.15 ms). The exponential C42 discharge stays;
     * the trigger interpolates its delay, length and per-block factor from
     * alkVegAttack[] (generated). Heard after rests and long notes; on running
     * 16ths C42 has not recharged (the circuit's behaviour).
     */
    ROW("alk_attack", "Veloc EG", "Attack", "atk", DTYPE_0B127, MOD_0_127, attack, TYPE_FLT),
    /*
     * S093 P6: pos on Saw/Sqr, the Drum row shape (DrumParameters.c l. 314)
     * bound to the oscillator's position byte. 0 = free-running (the 303's
     * VCO, today's sound); 1..127 = every non-legato note starts at pos/128
     * of a cycle; while it moves, the per-block bend (AlkalineVoice.c).
     */
    ROW("osc1_position", "Oscilltr", "Position", "pos", DTYPE_0B127, MOD_0_127, osc.position, TYPE_UINT8),
    /*
     * S093 P8: typ, the oscillator type.
     *
     * What:       0 wav (Saw/Sqr through the fused table loop), 1 wtb (a
     *             wavetable frame's 1,024-point row through the same table
     *             loop); names alkTypeNames (tag ONOFF_TAG_ALK_TYPE).
     *             The writer (IM_SPECIAL_OSC_TYPE, an Alkaline slot) is
     *             Alkaline_setOscType(), which maps the byte to osc.oscType.
     * Why:        S093 P8: the wavetable oscillator under the Basic ceiling;
     *             samples are not offered (S093_P8_IMPLEMENTATION.md §0).
     * Accessors:  Alkaline_triggerVoice(), Alkaline_calcVoiceAsync(),
     *             Alkaline_calcVoiceSyncBlock() (osc.oscType).
     */
    ROW_SELECT_ONOFF_SPECIAL("osc1_type", "Oscilltr", "Type", "typ", ONOFF_TAG_ALK_TYPE, osc.oscType, TYPE_UINT8, IM_SPECIAL_OSC_TYPE | IM_SPECIAL_OSC1),
    /*
     * S094 P1 S4: the VCA overdrive (K1, user P9-D9).
     *
     * What:       the shared instrument_drive key and labels of the other four
     *             types ("Voice" / "Overdriv" / drv) on the mix page's fourth
     *             cell; the byte 0..127 -> vcaDrive through
     *             IM_SPECIAL_INSTRUMENT_DRIVE's Alkaline branch
     *             (InstrumentManager.c); the voice turns it into the OTA input
     *             gain x1..x32 (S094 P2) and its make-up (alkOvdGain[],
     *             alkOvdMakeup[]).
     * Why:        the same name in the LFO/velocity target lists, the NRPN
     *             chart's voice-drive group and the Learn pattern as the other
     *             types; the filter page's alk_drive keeps its labels (user:
     *             category "Filter" makes it clear).
     */
    ROW_SPECIAL("instrument_drive", "Voice", "Overdriv", "drv", DTYPE_0B127, MOD_0_127, vcaDrive, TYPE_UINT8, IM_SPECIAL_INSTRUMENT_DRIVE),
    /*
     * S094 P1 S4: the tone (post-FX draft §3.4.1).
     *
     * What:       the mix page's third cell (the other types' srt cell; the
     *             Alkaline uses no SRR, so the cell is free, not the row);
     *             the byte 0..127 -> tone (generic TYPE_UINT8 store), the one-pole's
     *             c from alkToneC[]: 127 = open (exact), lower values roll off
     *             toward about 800 Hz. Default 127.
     */
    ROW("alk_tone", "Voice", "Tone", "ton", DTYPE_0B127, MOD_0_127, tone, TYPE_UINT8),
};

/* Runtime registry count used by InstrumentManager bounds checks. */
const uint8_t alkaline_param_descriptor_count =
    (uint8_t)(sizeof(alkaline_param_descriptors) / sizeof(alkaline_param_descriptors[0]));

_Static_assert(sizeof(alkaline_param_descriptors) / sizeof(alkaline_param_descriptors[0]) ==
                   ALK_PARAM_COUNT,
               "alkaline_param_descriptors must have one row per index");

/* An empty 16-cell page (page 3 only after S093 P6: FM; Snare ships an
 * all-empty page 3 the same way). */
#define ALK_PAGE_EMPTY \
    {{ INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, \
       INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, \
       INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, \
       INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY }}

/*
 * Alkaline voice menu layout (S093 P4 F3; S093 P6 rearranged).
 *
 * What:       eight sub-pages at the index of the front-panel SELECT button
 *             (menu.c ≈ l. 4608): 0 oscillator, 1 amp envelope, 2 mod
 *             (pitch/velocity), 3 FM, 4 click (transient), 5 filter, 6 LFO,
 *             7 mix. Each page is 16 cells shown as four-cell screens.
 * Why:        S093 P6 (user): sld on mod; atk and dec on AEG (dec is the MEG
 *             decay and keeps its label: it moves so FILTER is one screen,
 *             cut res env drv); acc on click; pos with the oscillator.
 */
const instrument_menu_page_t alkaline_menu_pages[] = {
    /* 0 OSC: coa tun wav pos | typ (S093 P8: Drum's second screen) */
    {{ ALK_PARAM_OSC1_PITCH_COARSE, ALK_PARAM_TUNING, ALK_PARAM_WAVE, ALK_PARAM_OSC1_POSITION, ALK_PARAM_OSC1_TYPE, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY }},
    /* 1 AEG: atk dec */
    {{ ALK_PARAM_ATTACK, ALK_PARAM_DECAY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY }},
    /* 2 mod: dst amt sld */
    {{ ALK_PARAM_VELO_MOD_DEST, ALK_PARAM_VELO_MOD_AMOUNT, ALK_PARAM_SLIDE, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY }},
    /* 3 FM: none */
    ALK_PAGE_EMPTY,
    /* 4 click: acc */
    {{ ALK_PARAM_ACCENT, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY }},
    /* 5 FILTER: cut res env drv (one screen) */
    {{ ALK_PARAM_CUTOFF, ALK_PARAM_RESO, ALK_PARAM_ENVMOD, ALK_PARAM_DRIVE, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY }},
    /* 6 LFO: the Drum layout (DrumParameters.c l. 411) */
    {{ ALK_PARAM_LFO_RATE, ALK_PARAM_LFO_SYNC, ALK_PARAM_LFO_WAVE, ALK_PARAM_LFO_OFFSET, ALK_PARAM_LFO_RETRIGGER_VOICE, ALK_PARAM_LFO_POLARITY, ALK_PARAM_LFO_AMOUNT, ALK_PARAM_LFO_AMOUNT_2, ALK_PARAM_LFO_TARGET_VOICE, ALK_PARAM_LFO_TARGET_PARAM, ALK_PARAM_LFO_TARGET_VOICE_2, ALK_PARAM_LFO_TARGET_PARAM_2, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY }},
    /* 7 MIX: vol pan ton drv (S094 P1 S4: the cells line up with the other
     * four types' vol pan srt drv; ton takes the srt cell) */
    {{ ALK_PARAM_INSTRUMENT_VOL, ALK_PARAM_INSTRUMENT_PAN, ALK_PARAM_TONE, ALK_PARAM_VCA_DRIVE, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY }},
};

const uint8_t alkaline_menu_page_count =
    (uint8_t)(sizeof(alkaline_menu_pages) / sizeof(alkaline_menu_pages[0]));

_Static_assert(ALKALINE_MENU_PAGE_COUNT ==
                   (sizeof(alkaline_menu_pages) / sizeof(alkaline_menu_pages[0])),
               "ALKALINE_MENU_PAGE_COUNT must match alkaline_menu_pages");

#undef FLAGS_IMAGE
#undef BIND_SPECIAL
#undef BIND
#undef ROW
#undef ROW_SPECIAL
#undef ROW_MENU
#undef ROW_MENU_SPECIAL
#undef ROW_SELECT_ONOFF_SPECIAL
#undef ROW_NOBIND
#undef ROW_NOBIND_IMAGE
#undef ALK_PAGE_EMPTY
#undef MOD_WAVE
#undef MOD_PM63
#undef MOD_0_127
#undef MOD_NONE
