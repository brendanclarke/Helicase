/*
 * MenuText.h — LXR-02 menu label strings.
 * Ported from original LXR AVR MenuText.h by Julian Schmidt.
 * PROGMEM removed — plain const arrays, Cortex-M7 accesses flash directly.
 */

/*
 *  Modified on: 17.05.2026
 * ------------------------------------------------------------------------------------------------------------------------
 *  Modifications Copyright 2026 Brendan Clarke
 *  brendanpaulclarke@gmail.com
 *  https://www.brendanclarke.com
 * ------------------------------------------------------------------------------------------------------------------------
 *  The modifications to this file are part of the LXR02 Open-Source software.
 *  The same license and restrictions on use for the LXR software apply.
 * ------------------------------------------------------------------------------------------------------------------------
 */

#ifndef MENUTEXT_H_
#define MENUTEXT_H_

/* DTYPE_MENU stores its menu table id in the high nibble of one byte. Id 0 is
** valid and is reserved here for track scale so the new 21-entry scale menu can
** use the normal menu dtype path without widening the packed dtype table. */
#define MENU_TRACK_SCALE    0
#define MENU_FILTER         1
#define MENU_WAVEFORM       2
#define MENU_AUDIO_OUT      3
/* S087 B: osc1_type table. Id 4 was unused; DTYPE_MENU keeps typ out of
** S081 ramps (seq_rampDtypeOk() ramps numeric dtypes only) and avoids a new
** dtype, which would wrap the 4-bit dtype field (S087 P0 §12.5). */
#define MENU_OSC_TYPE       4
#define MENU_SYNC_RATES     5
#define MENU_LFO_WAVES      6
#define MENU_RETRIGGER      7
#define MENU_NEXT_PATTERN   8
#define MENU_SEQ_QUANT      9
#define MENU_TRANS          10
#define MENU_MIDI           11
#define MENU_MIDI_ROUTING   12
#define MENU_MIDI_FILTERING 13
#define MENU_PPQ            14
#define MENU_EXT_SYNC       15
static const char menuText_ok[]    = "ok ";
static const char menuText_off[]   = "off";
static const char menuText_on[]    = "on ";
static const char menuText_dash[]  = "---";
static const char menuText_blank[] = "   ";
static const char menuText_any[]   = "any";
static const char menuText_glb[]   = "glb";

/* Each table: [0]=count, [1..n]=entries, 3+1 chars each */
static const char ppqNames[][4] = {
    {5}, {"1"}, {"4"}, {"8"}, {"16"}, {"32"},
};
static const char midiModes[][4] = {
    {2}, {"trg"}, {"nte"},
};
/*
 * Quantisation display names with trk option (S081 §5.6, R16).
 *
 * What: 6 entries. Adds "trk" after "64". Why: R16 adds track-grid
 * quantisation for recording and looper per-track loop starts. Inputs:
 * quantisation index 0..5. Affiliates: seq_quantisation, QUANT_TRACK enum,
 * seq_setQuantisation clamp, seq_loopEventAnchor.
 */
static const char quantisationNames[][4] = {
    {6}, {"off"}, {"8"}, {"16"}, {"32"}, {"64"}, {"trk"},
};
static const char transientNames[][4] = {
    {14},
    {"Snp"},{"Ofs"},{"Clk"},{"Ck2"},{"Tik"},{"Kik"},{"Rim"},
    {"Drp"},{"Hat"},{"Clp"},{"Kk2"},{"Snr"},{"Tom"},{"Sp2"},
};
static const char nextPatternNames[][4] = {
    {23},
    /*
     * Retired Pattern-next labels.
     *
     * The Pattern Settings page no longer exposes next/repeat controls, and the
     * sequencer ignores Pattern-only switches. Keep the table only so any stale
     * DTYPE_MENU reference has bounded display strings until Phase 4 rebuilds
     * Pattern storage/menu ownership.
     */
    {"p1"},{"p2"},{"p3"},{"p4"},{"p5"},{"p6"},{"p7"},{"p8"},
    {"p9"},{"p10"},{"p11"},{"p12"},{"p13"},{"p14"},{"p15"},{"p16"},
    {"r2"},{"r3"},{"r4"},{"r5"},{"r6"},{"r7"},{"r8"},
};
/*
 * LFO retrigger selector names (S076 P2).
 *
 * The leading byte is the selectable-entry count. Index 7 is the new "scn"
 * option (LFO_RETRIGGER_SCENE): a Scene change resets this LFO's phase to its
 * scaled offset, while every voice value keeps the existing voice retrigger
 * behaviour. Values 0..6 are unchanged.
 */
static const char retriggerNames[][4] = {
    {8}, {"off"},{"v1"},{"v2"},{"v3"},{"v4"},{"v5"},{"v6"},{"scn"},
};
static const char lfoWaveNames[][4] = {
    {8}, {"sin"},{"tri"},{"sup"},{"sdn"},{"sqr"},{"rnd"},{"xup"},{"xdn"},
};
/* Three-character display names for the shared LFO polarity selector.
** Clients: descriptor ROW("lfo_polarity", ..., DTYPE_LFO_POLARITY), compact
** value formatting, and single-parameter edit formatting. Outputs: "neg",
** "pos", and "bi " while the stored numeric value remains aligned with
** mod_node_polarity_t: 0 negative, 1 positive, 2 bipolar. */
static const char lfoPolarityNames[][4] = {
    {3}, {"neg"},{"pos"},{"bi"},
};
/*
 * Drum osc2 mod mode names (S085 P1).
 *
 * What:       [0] = count (4), then "fm ", "mix", "snc", "rng" in stored
 *             order (DrumVoice.h DRUM_MOD_FM .. DRUM_MOD_RNG).
 * Why:        DTYPE_MIX_FM grew from two values to four. One table replaces
 *             the two literals menuText_fm / menuText_mix, so the names,
 *             clamp and automation maximum share one count.
 * Accessors:  menu.c menu_getMixFmName(), menu_clampCellValue(),
 *             menu_automationValueMax().
 * Affiliates: DrumParameters.c osc2_mod_type; DrumVoice.h DRUM_MOD_COUNT.
 */
static const char mixFmNames[][4] = {
    {4}, {"fm "},{"mix"},{"snc"},{"rng"},
};
/* rollRateNames / MENU_ROLL_RATES retired in place (S081 A3-5, Q14). */
static const char syncRateNames[][4] = {
    {12},
    {"off"},{"4/1"},{"2/1"},{"1/1"},{"1/2"},{"1/3"},
    {"1/4"},{"1/6"},{"1/8"},{"12"},{"16"},{"32"},
};
static const char extSyncNames[][4] = {
    {5}, {"off"}, {"usb"}, {"din"}, {"pls"}, {"aut"},
};
/*
 * Per-track play-mode names (S078 §4.2).
 *
 * The leading byte is the selectable-entry count; entries follow in the stored
 * play-mode order fwd/rev/pip/rnd/onc/1fr. Clients: the STEP track-settings
 * `run` cell and the menu.c run-mode formatter (menu_getPlayModeName).
 */
static const char trackPlayModeNames[][4] = {
    {6}, {"fwd"},{"rev"},{"pip"},{"rnd"},{"onc"},{"1fr"},
};
/* S086 P2: Re3/Sw3 = SQ3/SW3 (Oscillator.h); the count must equal
** OSC_SAMPLE_START (asserted in menu.c). User samples follow as sNN. */
static const char waveformNames[][4] = {
    {8}, {"Sin"},{"Tri"},{"Saw"},{"Rec"},{"Noi"},{"Cym"},{"Re3"},{"Sw3"},
};
/*
 * osc1_type names (S087 stage B).
 *
 * What:       [0] = count (OSC_TYPE_COUNT), then wav smp slp wtb.
 * Why:        the typ cell, its automation display and the single-parameter
 *             view share one table; the menu shows only the first
 *             OSC_TYPE_SELECTABLE entries.
 * Accessors:  getMaxEntriesForMenu(), getMenuItemNameForValue(),
 *             menu_automationValueMax() (menu.c).
 * Affiliates: OSC_TYPE_* (Oscillator.h); MENU_OSC_TYPE.
 */
static const char oscTypeNames[][4] = {
    {4}, {"wav"},{"smp"},{"slp"},{"wtb"},
};
/*
 * DTYPE_ON_OFF high-nibble sub-type tag (S088 P0 §2.1 D12).
 *
 * What:       ONOFF_TAG_OSC_RUN = 1u. The descriptor dtype byte stores
 *             DTYPE_ON_OFF | (ONOFF_TAG_OSC_RUN << 4) = 0x16 so that menu.c
 *             can tell a run-mode cell from an ordinary on/off cell. The low
 *             nibble (& 0x0f) is 6 = DTYPE_ON_OFF; the high nibble (>> 4)
 *             is 1.
 * Why:        all 16 DTYPE_MENU ids and all 16 dtype enum values are taken
 *             (S078 binding mandate). The high nibble of DTYPE_ON_OFF was
 *             unused: every existing >> 4 read of the dtype byte sits inside
 *             a DTYPE_MENU case (verified S088 P0 §2.1).
 * Inputs:     the four engine *Parameters.c osc1_run descriptor rows.
 * Outputs:    read by the six DTYPE_ON_OFF switch sites in menu.c.
 * Accessors:  ROW_SELECT_ONOFF_SPECIAL (each *Parameters.c).
 * Affiliates: oscRunNames below; S088_P0_OSC_RUN_MODES.md §2.1.
 */
#define ONOFF_TAG_OSC_RUN   1u
/*
 * Oscillator run-mode names (S088 P0 §2.1).
 *
 * What:       [0] = count (3), then rst, fre, itp. Same shape as mixFmNames:
 *             a leading count byte and 3+1 character entries.
 * Why:        the six tagged DTYPE_ON_OFF sites in menu.c use this table for
 *             display and clamping; the step-automation display and the
 *             single-parameter view read it too.
 * Accessors:  menu.c menu_formatCellValue3(), menu_repaintGeneric(),
 *             menu_clampCellValue(), menu_automationValueMax(),
 *             menu_formatAutomationValue3() and the step-automation edit
 *             display.
 * Affiliates: ONOFF_TAG_OSC_RUN above; the osc1_run descriptor rows;
 *             OSC_RUN_COUNT (Oscillator.h) is the same 3.
 */
static const char oscRunNames[][4] = {
    {3}, {"rst"},{"fre"},{"itp"},
};
/*
 * DTYPE_ON_OFF tag for the voice chain selector (S089 P2).
 *
 * What:       ONOFF_TAG_VOICE_CHAIN = 2u: the voice_chain rows' dtype byte is
 *             DTYPE_ON_OFF | (2 << 4) = 0x26. Tag 1 stays osc1_run.
 * Why:        S088 D12 reserved further tags as 2, 3, ... for tagged on/off
 *             selectors; menu.c resolves the name table per tag through
 *             menu_onoffTagNames().
 * Accessors:  ROW_SELECT_ONOFF_SPECIAL (the four voice_chain rows).
 * Affiliates: voiceChainNames below; ONOFF_TAG_OSC_RUN above.
 */
#define ONOFF_TAG_VOICE_CHAIN   2u
/*
 * Voice chain names (S089 P2).
 *
 * What:       [0] = count (2), then T+D, WFd. Same shape as oscRunNames.
 * Why:        display, clamp and automation max of the chn cell; the count
 *             must equal VOICE_CHAIN_COUNT (asserted in menu.c).
 * Accessors:  menu_onoffTagNames() (menu.c).
 * Affiliates: VOICE_CHAIN_* (voiceFold.h).
 */
static const char voiceChainNames[][4] = {
    {2}, {"T+D"},{"WFd"},
};
/*
 * Wavefolder type names: the wav cell under WFd (S089 P2).
 *
 * What:       foldTypeNames: [0] = count (8), then the 3-character codes;
 *             foldTypeLongNames: the same order, 8 characters for the
 *             single-parameter view (left field).
 * Why:        under WFd the transient_wave byte is a fold type
 *             (VOICE_FOLD_*); the counts must equal VOICE_FOLD_COUNT
 *             (asserted in menu.c).
 * Accessors:  menu_cellIsFoldWave() users in menu.c.
 * Affiliates: VOICE_FOLD_* (voiceFold.h).
 */
static const char foldTypeNames[][4] = {
    {8}, {"Tri"},{"TrD"},{"Sin"},{"SnD"},{"Asn"},{"Chb"},{"TrI"},{"Fd2"},
};
static const char foldTypeLongNames[][9] = {
    {8}, {"TriFold "},{"TriDecay"},{"SinFold "},{"SinDecay"},
         {"AsymFold"},{"Chebyshv"},{"IntFold "},{"Fold2x  "},
};
static const char outputNames[][7] = {
    {6}, {"St1"},{"St2"},{"L1"},{"R1"},{"L2"},{"R2"},
};
static const char filterTypes[][8] = {
    {8}, {"LP"},{"HP"},{"BP"},{"UBP"},{"Nch"},{"Pek"},{"LP2"},{"off"},
};
/*
 * MRT routing names (S083 F22, MIDI_SPEC 3.3).
 * Order matches midiDefs_routeMask(): off UtD DtD DtU bth DtA AtD all.
 * Stored values change meaning (no migration, standing rule).
 */
static const char midiRoutingNames[][6] = {
    {8}, {"off"},{"UtD"},{"DtD"},{"DtU"},{"bth"},{"DtA"},{"AtD"},{"all"},
};
static const char midiFilterNames[][16] = {
    {16},
    {"off"},{"N"},{"R"},{"RN"},{"C"},{"CN"},{"CR"},{"CRN"},
    {"P"},{"PN"},{"PR"},{"PRN"},{"PC"},{"PCN"},{"PCR"},{"all"},
};
static const char shortNames[][4] = {
    {""}, {"coa"},{"fin"},{"atk"},{"dec"},{"eg2"},{"mod"},{"amt"},
    {"frq"},{"drv"},{"vol"},{"pan"},{"noi"},{"rpt"},{"mix"},
    {"res"},{"typ"},{"f1"},{"f2"},{"g1"},{"g2"},{"wav"},{"dst"},
    {"snc"},{"rtg"},{"ofs"},{"voi"},{"slp"},{"d1"},{"d2"},
    {"eqg"},{"eqf"},
    {"rol"},{"mrp"},{"nte"},{"prb"},{"stp"},{"len"},{"rot"},
    {"bpm"},{"ch"},{"out"},{"fxm"},{"nxt"},{"mod"},{"vel"},
    /* TEXT_AUTOMATION_TRACK reused as the S081 rtk recording-track cell. */
    {"fch"},{"flw"},{"qnt"},{"rtk"},{"val"},{"shu"},{"ssv"},
    {"x"},{"y"},{"flx"},{"mid"},{"mrt"},{"txf"},{"rxf"},
    {"cki"},{"co1"},{"co2"},{"pcr"},{"cpu"},{"oit"},{"sca"},
    /* PERF per-voice Morph compact labels: voice number + "vm". */
    {"1vm"},{"2vm"},{"3vm"},{"4vm"},{"5vm"},{"6vm"},
    /* Read-only Pattern pool-use widget label. */
    {"pts"},
    /* Requested short label for the persistent Global AutoSave switch. */
    {"ats"},
    /* S074 bus compressor compact labels. */
    {"cmp"},{"cam"},{"ctm"},{"csc"},
    /*
     * S078 per-track run mode compact label (S078 P4: renamed from `mod`).
     * `run` matches the FX sequencer's run-mode cell (menuEffects.c), so the
     * same concept reads the same on the Effect page and the STEP track
     * settings. Accessor: valueNames[TEXT_TRACK_PLAY_MODE] (menu.c).
     */
    {"run"},
    /* S079 step-special compact labels: cdn/tim/rol/rtm/rfl (fix 2, P2). */
    {"cdn"},{"tim"},{"rol"},{"rtm"},{"rfl"},
    /* S080 E4/G10: BAR overlay, scene globals, and chain labels. The S080 F2
     * duplicate "pcr" was removed in S080 P2 (Defect 5); index 63 above is the
     * only pcr label. */
    {"pb<"},{"pn<"},{"pb>"},{"pn>"},
    {"scd"},{"qcd"},{"qac"},
    {"cn1"},{"nx1"},{"cn2"},{"nx2"},
    /* S080 P2 Defect 8a: per-track loopable. */
    {"lop"},
    /* S081 P2: per-track automation ramp; label `rmp` since the S083
     * follow-up (user 2026-10-22; was `asm`). */
    {"rmp"},
    /* S083 follow-up: overlay roll velocity rhythm. */
    {"pv<"},{"pv>"},
};

static const char catNames[][16] = {
    {""}, {"Oscilltr"},{"Veloc EG"},{"Mod EG"},{"PitchMod"},
    {"FM"},{"Voice"},{"Noise"},{"Nois/Osc"},{"Filter"},
    {"Mod Osc"},{"LFO"},{"Transnt"},{"EQ"},
    {"Pattern"},{"Sound"},{"Step"},{"Euklid"},
    {"Global"},{"Velocity"},{"Parametr"},{"Sequencr"},
    {"Generatr"},{"MIDI"},{"Trigger"},
    /* S074: CAT_SCENE, the full-view category of Scene-owned cells. */
    {"Scene"},
    /*
     * S078 P4: CAT_TRACK, the full-view category of per-track Pattern
     * settings. The clicked-in view of the STEP run-mode cell reads
     * "Track   RunMode", in parallel with the FX sequencer's
     * "FX Seq  RunMode". Accessor: valueNames[TEXT_TRACK_PLAY_MODE].
     */
    {"Track"},
    /* S080 E4: BAR overlay settings category. */
    {"Prfrmnc"},
    /* S080 G10: scene chain settings category. */
    {"Chain"},
    /* S083 follow-up (user 2026-10-22): CAT_AUTOMATION for the track
     * automation ramp cell `rmp` ("Automtn  Ramp"). */
    {"Automtn"},
};

static const char longNames[][16] = {
    {""}, {"Coarse"},{"Fine"},{"Attack"},{"Decay"},{"Amount"},
    {"Frequncy"},{"Overdriv"},{"Volume"},{"Panning"},{"Mix"},
    {"RepeatCt"},{"Resnance"},{"Type"},{"Gain"},{"Waveform"},
    {"DstParam"},{"ClockSnc"},{"Retriggr"},{"Offset"},
    {"DstVoice"},{"Slope"},{"Dcy Clsd"},{"Dcy Open"},
    {"RollRate"},{"Morph"},{"Note"},{"Prbablty"},{"Number"},
    {"Length"},{"Steps"},{"Rotation"},{"Tempo"},{"SyncInpt"},
    {"AudioOut"},{"Channel"},{"FX Morph"},{"NextPatt"},{"Phase"},{"Mode"},
    {"Vol mod"},{"Fetch"},{"Follow"},{"Quantize"},{"RecTrk"},
    {"Aut Dest"},{"AutValue"},{"Shuffle"},{"Screensv"},
    {"X Positn"},{"Y Positn"},{"Flux"},{"Velocity"},
    {"Freqcy 1"},{"Freqcy 2"},{"Gain 1"},{"Gain 2"},
    {"Routing"},{"TxFilter"},{"RxFilter"},
    {"In PPQ"},{"Out1 PPQ"},{"Out2 PPQ"},{"Gate Mode"},{"PCReset"},
    {"CPU use time"},{"OscIntrp"},{"Scale"},
    /* Single-parameter long names for PERF per-voice Morph controls. */
    {"1 Morph"},{"2 Morph"},{"3 Morph"},{"4 Morph"},{"5 Morph"},{"6 Morph"},
    /* Read-only Pattern pool-use widget title; fits the 15-visible-char field. */
    {"PtrnStoreUse"},
    /* Requested long label; valueNames assigns it to category Global. */
    {"AutoSave"},
    /* S074 bus compressor long names. */
    {"BusComp"},{"CompAmt"},{"CompTime"},{"CompSC"},
    /* S078 per-track run mode long name (S078 P4: renamed from PlayMode to
     * match the FX sequencer's RunMode). */
    {"RunMode"},
    /* S079 step-special long names (fix 2: Conditin, % Delay; P2: Rhythm). */
    {"Conditin"},{"% Delay"},{"RollRate"},{"Rhythm"},{"RollFill"},
    /* S080 E4/F4/G10: overlay, scene-global, and chain long names. */
    {"BtnStat<"},{"BtnNote<"},{"BtnStat>"},{"BtnNote>"},
    {"ChngDiv"},{"QueDiv"},{"QueActn"},
    {"Conditn1"},{"NxtScn1"},{"Conditn2"},{"NxtScn2"},
    /* S080 P2 Defect 8a. */
    {"Loopable"},
    /* S081 P2; "Ramp" since the S083 follow-up (was "Automtn", which is
     * now the cell's category). */
    {"Ramp"},
    /* S083 follow-up: overlay roll velocity rhythm (Prfrmnc Rhythm</>). */
    {"Rhythm<"},{"Rhythm>"},
};

/*
 * Trigger condition display names (S079 §3; fix 3 extends to track 7;
 * S080 B1 adds the loop-active pair).
 *
 * S080 B1: insert `lop` (loop active) and `!lp` (loop inactive) after `!fl`;
 * the count grows 39 -> 41 and every later condition shifts up by two.
 * The table is indexed directly by the stored condition value
 * (menu_getConditionName() reads conditionNames[value + 1]), so it must stay
 * in numeric order. The mute group is therefore emitted in the dispatch's
 * order - 1mu..7mu (indices 27..33) then !1m..!7m (34..40) - matching
 * seq_evaluateStepCondition()'s (cond - 27) % 7 / boundary cond <= 33 rule.
 * (S079 fix 3 had appended 7mu/!7m at the end of the old 6-track grouping,
 * leaving the displayed labels one group out of step; this corrects it.)
 *
 * [0]=count; [1..41] = condition indices 0..40.
 */
static const char conditionNames[][4] = {
    {41},
    {"  -"},{"r10"},{"r20"},{"r30"},{"r40"},{"r50"},{"r60"},{"r70"},
    {"r80"},{"r90"},{"r95"},
    {"1:2"},{"2:2"},{"1:3"},{"2:3"},{"3:3"},{"1:4"},{"2:4"},{"3:4"},
    {"4:4"},{"1:-"},
    {"fil"},{"!fl"},{"lop"},{"!lp"},{"lst"},{"!ls"},
    {"1mu"},{"2mu"},{"3mu"},{"4mu"},{"5mu"},{"6mu"},{"7mu"},
    {"!1m"},{"!2m"},{"!3m"},{"!4m"},{"!5m"},{"!6m"},{"!7m"},
};

/*
 * Step-roll rate display names, aligned with the §5.4 table (S081 R17).
 *
 * What: 10 entries plus the count byte. Drop " 48" (was between 32T and 64).
 * Add "64t" and "128". Why: the table indices and names must match the new
 * 10-entry seq_rollPpq table. Stored bytes shift with no migration. Inputs:
 * roll index 0..9. Affiliates: seq_rollPpq, menu_getRollRateName, STEP roll
 * clamp, perfBtnTypeNames (overlay roll types 6..14 = these indices 1..9,
 * S084 P3).
 */
static const char stepRollRateNames[][4] = {
    {10},
    {"off"},{"1/8"},{"8tr"},{" 16"},{"16t"},{" 32"},{"32t"},{" 64"},{"64t"},{"128"},
};

/*
 * PERF BAR overlay trigger-type names (S080 E4; S084 P3 one roll list).
 *
 * [0] = count 15; [1..15] = stored value 0..14. 0..3 single velocities
 * (vmx v10 v64 v32), 4..5 flams (flm fls), 6..14 held rolls with exactly
 * the STEP roll labels and rates: value v is step-roll index v - 5 (1/8,
 * 8tr, 16, 16t, 32, 32t, 64, 64t, 128). Why: PERF offered only six of the
 * nine step rates under different names (S084 P0 F1); one list makes every
 * step roll playable live and keeps one vocabulary (Q1). Stored values
 * 6..11 changed meaning; no migration (user Q2, 2026-10-24). Rendered by
 * the static-param special case in menu.c (all DTYPE_MENU ids are taken),
 * which reads the count from [0][0]. Affiliates: PAR_PERF_BTN_LT/RT,
 * stepRollRateNames, seq_rollPpq(), buttonHandler_overlayStart()
 * (BH_OVERLAY_TYPE_* layout), menu_clampCellValue().
 */
static const char perfBtnTypeNames[][4] = {
    {15},
    {"vmx"},{"v10"},{"v64"},{"v32"},{"flm"},{"fls"},
    {"1/8"},{"8tr"},{" 16"},{"16t"},{" 32"},{"32t"},{" 64"},{"64t"},{"128"},
};

/*
 * S080 F4: direct scene-change division names (scd).
 * [0]=count; [1..10] = stored value 0..9: instant, 1/8, 1/4, 1/2, 1-bar,
 * 2/3/4-bar, end-of-shortest-track, end-of-longest-track. Since S080 P2 the
 * queued division (qcd) has its own longer list, sceneQueueDivNames below.
 */
static const char sceneChangeDivNames[][4] = {
    {10},
    {"ins"},{"1/8"},{"1/4"},{"1/2"},{"1b"},
    {"2b"},{"3b"},{"4b"},{"ens"},{"enl"},
};

/*
 * S080 P2: queued scene-change division names (qcd, QueDiv).
 *
 * What: [0]=count; [1..18] = stored qcd value 0..17: instant, 1/8, 1/4, 1/2,
 * then 1..9, 12, 15 and 16 bars, then end-of-shortest-track and
 * end-of-longest-track. Why: a queued change is pre-programmed during a
 * performance and often lands at a phrase boundary, so qcd offers the longer
 * bar counts (5b..9b, 12b, 15b, 16b, user request) that a direct change does
 * not need. Rendered by the S080 static-param resolver because every
 * DTYPE_MENU id is taken. Affiliates: PAR_SCENE_QUEUE_DIV,
 * seq_queueChangeDivUnits() (sequencer.c, must keep the same order),
 * menu_clampCellValue() (clamp 0..17), filesystem_sanitizeLoadedGlobals().
 */
static const char sceneQueueDivNames[][4] = {
    {18},
    {"ins"},{"1/8"},{"1/4"},{"1/2"},{"1b"},
    {"2b"},{"3b"},{"4b"},{"5b"},{"6b"},
    {"7b"},{"8b"},{"9b"},{"12b"},{"15b"},
    {"16b"},{"ens"},{"enl"},
};

/*
 * S080 F4 / P2 Defect 15: queued scene-change action names.
 *
 * What: [0] = count; [1..9] = qac values 0..8: nrm (plain change), mut (all
 * mutes off), mrp (incoming Scene morphs to 0), res (queued PC reset: tracks
 * and master clock restart), u+r (mute + reset), o+r (morph + reset), mmr
 * (mute + morph + reset), rld (reload + mute + morph), r+r (reload + mute +
 * morph + reset). Why: a queued change has its own PC reset, separate from
 * pcr which governs direct and chain-fired changes. Rendered by the S080
 * static-param resolver (all DTYPE_MENU ids are taken). Affiliates:
 * PAR_SCENE_QUEUE_ACTION, menu_queueActionFlags() (menu.c),
 * menu_clampCellValue() (clamp 0..8).
 */
static const char sceneQueueActionNames[][4] = {
    {9},
    {"nrm"},{"mut"},{"mrp"},{"res"},{"u+r"},
    {"o+r"},{"mmr"},{"rld"},{"r+r"},
};

/*
 * S080 G9: scene chain condition names.
 * [0]=count; [1..79] = condition index 0..78 (nul, always-rep, always-bar,
 * repeat counts, bar counts, probabilities, bar positions, from-scene, fill
 * and loop conditions). Rendered by the chain menu static-param path (G8/G10).
 */
static const char chainConditionNames[][4] = {
    {79},
    {"nul"},{"xal"},{"bal"},
    {"x01"},{"x02"},{"x03"},{"x04"},{"x05"},{"x06"},{"x07"},{"x08"},
    {"x16"},{"x32"},{"x64"},
    {"b01"},{"b02"},{"b03"},{"b04"},{"b05"},{"b06"},{"b07"},{"b08"},
    {"b16"},{"b32"},{"b64"},
    {"r1x"},{"r2x"},{"r3x"},{"r4x"},{"r5x"},{"r6x"},{"r7x"},{"r8x"},{"r9x"},
    {"r1b"},{"r2b"},{"r3b"},{"r4b"},{"r5b"},{"r6b"},{"r7b"},{"r8b"},{"r9b"},
    {"1:4"},{"2:4"},{"3:4"},{"4:4"},
    {"1:8"},{"2:8"},{"3:8"},{"4:8"},{"5:8"},{"6:8"},{"7:8"},{"8:8"},
    {">01"},{">02"},{">03"},{">04"},{">05"},{">06"},{">07"},{">08"},
    {">09"},{">10"},{">11"},{">12"},{">13"},{">14"},{">15"},{">16"},
    {"fix"},{"fib"},{"!fx"},{"!fb"},
    {"lox"},{"lob"},{"!lx"},{"!lb"},
};

/*
 * S080 G9: scene chain next-scene names.
 * [0]=count; [1..44] = value 0..43 (self, next, previous, absolute s01..s16,
 * random up to r01..r16, relative random r+1..r+4 / r-1..r-4, stop playback).
 */
static const char chainNextNames[][4] = {
    {44},
    {"slf"},{"nxt"},{"prv"},
    {"s01"},{"s02"},{"s03"},{"s04"},{"s05"},{"s06"},{"s07"},{"s08"},
    {"s09"},{"s10"},{"s11"},{"s12"},{"s13"},{"s14"},{"s15"},{"s16"},
    {"r01"},{"r02"},{"r03"},{"r04"},{"r05"},{"r06"},{"r07"},{"r08"},
    {"r09"},{"r10"},{"r11"},{"r12"},{"r13"},{"r14"},{"r15"},{"r16"},
    {"r+1"},{"r+2"},{"r+3"},{"r+4"},
    {"r-1"},{"r-2"},{"r-3"},{"r-4"},
    {"stp"},
};

#endif /* MENUTEXT_H_ */
