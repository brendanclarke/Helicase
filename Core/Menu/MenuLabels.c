/*
 * MenuLabels.c - the short, category and long name pools (S097 P1 item 2).
 *
 * What: shortNames[], catNames[] and longNames[], formerly static tables in
 * MenuText.h, defined once here. Static menu cells reach them through
 * valueNames (MenuText.h); instrument and effect descriptor rows hold 1-byte
 * indexes (ParamDescriptor.short_id / cat_id / long_id). The enums in menu.h
 * (shortNamesEnum, catNamesEnum, longNamesEnum) must stay index-aligned with
 * these tables; the asserts at the end check the counts.
 * Accessors: menuLabel_short(), menuLabel_cat(), menuLabel_long()
 * (MenuText.h), and menu.c directly.
 */
#include "menu.h"
#include "MenuText.h"

const char shortNames[][4] = {
    {""}, {"coa"},{"fin"},{"atk"},{"dec"},{"mod"},{"amt"},
    {"frq"},{"drv"},{"vol"},{"pan"},{"noi"},{"rpt"},{"mix"},
    {"res"},{"typ"},{"f1"},{"f2"},{"g1"},{"g2"},{"wav"},{"dst"},
    {"snc"},{"rtg"},{"ofs"},{"voi"},{"slp"},{"d1"},{"d2"},
    {"eqg"},{"eqf"},
    {"mrp"},{"nte"},{"prb"},{"stp"},{"len"},{"rot"},
    {"bpm"},{"ch"},{"out"},{"fxm"},{"nxt"},{"vel"},
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
    {"cmp"},{"cam"},{"ctm"},{"csc"},{"cgr"},
    /*
     * S078 per-track run mode compact label (S078 P4: renamed from `mod`).
     * `run` matches the FX sequencer's run-mode cell (menuEffects.c), so the
     * same concept reads the same on the Effect page and the STEP track
     * settings. Accessor: valueNames[TEXT_TRACK_PLAY_MODE] (menu.c).
     */
    {"run"},
    /* S079 step-special compact labels: cdn/tim/rol/rtm/rfl (fix 2, P2);
     * S099 P3: rfl is fil ("Step" "Fill"). */
    {"cdn"},{"tim"},{"rol"},{"rtm"},{"fil"},
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
    /*
     * S097 P1 item 2: instrument and effect descriptor labels (the
     * descriptor rows hold the index, ParamDescriptor.short_id). Labels that already
     * existed above are reused by their existing id.
     */
    {"srt"},{"am1"},{"am2"},{"pol"},{"vo1"},{"ds1"},{"vo2"},{"ds2"},{"pos"},
    {"chn"},{"tun"},{"sld"},{"cut"},{"env"},{"acc"},{"ton"},{"psp"},{"slv"},
    {"rel"},{"sus"},{"slw"},{"in1"},{"in2"},{"in3"},{"dtn"},{"spr"},{"mot"},
    {"rte"},{"fro"},{"reo"},{"eno"},
    {"dro"},{"pat"},{"pde"},{"psl"},{"pre"},{"pam"},{"nlv"},{"nto"},{"nwd"},
    {"npn"},{"smA"},{"smB"},{"smC"},{"smD"},{"tuA"},{"tuB"},{"tuC"},{"tuD"},
    {"lvA"},{"lvB"},{"lvC"},{"lvD"},{"dyA"},{"dyB"},{"dyC"},{"dyD"},{"pnA"},
    {"pnB"},{"pnC"},{"pnD"},{"lon"},{"drA"},{"drB"},{"drC"},{"drD"},{"srA"},
    {"srB"},{"srC"},{"srD"},{"spA"},{"spB"},{"spC"},{"spD"},
    {"tnA"},{"tnB"},{"tnC"},{"tnD"},{"teA"},{"teB"},{"teC"},
    {"teD"},{"wid"},{"bof"},{"biv"},{"fbk"},{"sub"},{"spn"},{"aux"},{"p2"},
    {"p3"},{"den"},{"bal"},{"dmp"},{"xfb"},{"deg"},{"wow"},{"hld"},
    /* S100 P2: "str" (SHORT_STR) replaces "hed" in place (menu.h). */
    {"rev"},{"str"},{"dck"},{"siz"},{"dif"},{"lsh"},{"lsf"},{"hsh"},
    {"hsf"},{"frz"},{"shm"},{"int"},{"e>r"},{"bln"},
    /* S099 P1: the chorus rows; S099 P2: fx rte dpt mix (the effect's; csr,
     * cbw, cdv and cwd removed) */
    /* S103 P3 Tier A: "rte" and "mix" of the effect rows removed here (they
     * use the identical SHORT_RTE / SHORT_MIX entries above), as were "eg2"
     * (unused), the first "rol" and the second "mod"; Tiers C/D removed
     * "dcy" "lng" "stm" "sfb" "sdt" "sto" and "fat" "fde" "fsl" "fre" "fam"
     * (their rows use dec len tim fbk dtn ton / atk dec slv rel amt). Each
     * string left the table at the position its id left menu.h's enum. */
    {"fx "},{"dpt"},
    /* S103 P1 / S105 P1: the Katran's own short labels (menu.h SHORT_KIK ..
     * SHORT_SDB); its other rows reuse existing labels */
    {"kik"},{"cyc"},{"skw"},{"imd"},{"chs"},{"dtk"},{"exc"},{"slf"},{"r/f"},
    {"sdb"},
    /* S104 P1: the Global MIDI Omni channel (menu.h SHORT_OMN). */
    {"omn"},
};

/*
 * Category names are at most 8 characters (S097 P1 item 4): the LCD shows at
 * most 8 (menu_displayModTargetFull(), menu_repaintGeneric() and the
 * descriptor label readers). The row width is 8 + NUL. longNames is 9 + NUL:
 * two descriptor long names have 9 characters ("DstVoice2", "DstParam2"; the
 * step-automation target view can show up to 14 characters of category +
 * long name), kept whole since S097 P1 item 2.
 */
const char catNames[][9] = {
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
    /*
     * S097 P1 item 2: instrument and effect descriptor labels (the
     * descriptor rows hold the index, ParamDescriptor.cat_id). Labels that already
     * existed above are reused by their existing id.
     */
    {"Amp EG"},{"Pitch"},{"Unison"},{"Filt EG"},{"Pitch EG"},{"Sample"},
    {"Envelope"},{"Space"},{"Tone"},{"Effect"},{"Bits"},{"CrumpBit"},
    {"Grain"},{"Reverb"},{"Delay"},{"Shimmer"},{"Echo"},{"Plate"},
    /* S105 P1 (A1): Katran's block categories (menu.h CAT_OSC1 .. CAT_TORCIDO) */
    {"Osc 1"},{"Osc 2"},{"Buffer"},{"Exciter"},{"Chaos"},{"Stereo"},{"VCA"},
    {"Ring/FM"},{"Torcido"},
};

const char longNames[][10] = {
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
    {"In PPQ"},{"Out1 PPQ"},{"Out2 PPQ"},{"GateMode"},{"PCReset"},
    {"CPU use"},{"OscIntrp"},{"Scale"},
    /* Single-parameter long names for PERF per-voice Morph controls. */
    {"1 Morph"},{"2 Morph"},{"3 Morph"},{"4 Morph"},{"5 Morph"},{"6 Morph"},
    /* Read-only Pattern pool-use widget title (8 characters shown). */
    {"PtrnStor"},
    /* Requested long label; valueNames assigns it to category Global. */
    {"AutoSave"},
    /* S074 bus compressor long names. */
    {"BusComp"},{"CompAmt"},{"CompTime"},{"CompSC"},{"CompGR"},
    /* S078 per-track run mode long name (S078 P4: renamed from PlayMode to
     * match the FX sequencer's RunMode). */
    {"RunMode"},
    /* S079 step-special long names (fix 2: Conditin, % Delay; P2: Rhythm). */
    {"Conditin"},{"% Delay"},{"RollRate"},{"Rhythm"},{"Fill"},
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
    /*
     * S097 P1 item 2: instrument and effect descriptor labels (the
     * descriptor rows hold the index, ParamDescriptor.long_id). Labels that already
     * existed above are reused by their existing id.
     */
    {"SampleRt"},{"Amount 2"},{"Polarity"},{"DstVoice2"},{"DstParam2"},
    {"Position"},{"Run Mode"},{"Chain"},{"Tuning"},{"Slide"},{"Cutoff"},
    {"Env Mod"},{"Accent"},{"Tone"},{"PosSprd"},{"SustLvl"},{"Release"},
    {"Sustain"},{"Slew"},{"Intrvl1"},{"Intrvl2"},{"Intrvl3"},{"Detune"},
    {"Spread"},{"Motion"},{"Drive"},{"Reso"},{"FiltType"},{"Route"},
    {"CutOfs"},{"ResOfs"},{"EnvOfs"},{"DrvOfs"},{"Level"},{"Width"},{"Pan"},
    {"Sample"},{"Tune"},{"Trigger"},{"Space"},{"Time"},{"Feedback"},
    {"Envelope"},{"BitOff"},{"BitInv"},{"Rate"},{"SubType"},{"Switch"},
    {"SubPan"},{"Aux"},{"Pitch2"},{"Pitch3"},{"Density"},{"Balance"},
    {"FinePtch"},{"Damping"},{"XFeed"},{"Degrade"},{"WowFlutr"},{"Hold"},
    /* S100 P2: "Stereo" (LONG_STEREO) replaces "Heads" in place (menu.h). */
    {"Reverse"},{"Stereo"},{"Ducking"},{"PreDelay"},{"Size"},{"Diffusn"},
    {"LowShelf"},{"LowFreq"},{"HighDamp"},{"HighFreq"},{"Modulatn"},
    {"Freeze"},{"Interval"},{"GateBlom"},{"EchoRvb"},{"Blend"},
    /* S099 P1: the chorus rows */
    {"Depth"},{"Source"},
    /* S103 P1 / S105 P1: the Katran rows (menu.h LONG_RUN .. LONG_SIDEBAND) */
    {"Run"},{"Kick"},{"Cycle"},{"Skew"},{"SelfMod"},{"NoiseMix"},{"SyncThr"},
    {"Tracking"},{"Intermod"},{"Sideband"},
    /* S104 P1: the Global MIDI Omni channel (menu.h LONG_OMNI_CH). */
    {"Omni Ch"},
};

_Static_assert(sizeof(shortNames) / sizeof(shortNames[0]) == SHORT_LABEL_COUNT,
               "shortNames[] must match enum shortNamesEnum");
_Static_assert(sizeof(catNames) / sizeof(catNames[0]) == CAT_LABEL_COUNT,
               "catNames[] must match enum catNamesEnum");
_Static_assert(sizeof(longNames) / sizeof(longNames[0]) == LONG_LABEL_COUNT,
               "longNames[] must match enum longNamesEnum");
_Static_assert(SHORT_LABEL_COUNT <= 256 && CAT_LABEL_COUNT <= 256 &&
               LONG_LABEL_COUNT <= 256,
               "descriptor label ids are uint8_t");
