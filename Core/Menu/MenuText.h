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
#define MENU_ROLL_RATES     4
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
static const char menuText_mix[]   = "mix";
static const char menuText_fm[]    = "fm ";
static const char menuText_dash[]  = "---";
static const char menuText_blank[] = "   ";
static const char menuText_any[]   = "any";

/* Each table: [0]=count, [1..n]=entries, 3+1 chars each */
static const char ppqNames[][4] = {
    {5}, {"1"}, {"4"}, {"8"}, {"16"}, {"32"},
};
static const char midiModes[][4] = {
    {2}, {"trg"}, {"nte"},
};
static const char quantisationNames[][4] = {
    {5}, {"off"}, {"8"}, {"16"}, {"32"}, {"64"},
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
static const char retriggerNames[][4] = {
    {7}, {"off"},{"v1"},{"v2"},{"v3"},{"v4"},{"v5"},{"v6"},
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
static const char rollRateNames[][4] = {
    {14},
    {"One"},{"1/1"},{"1/2"},{"1/3"},{"1/4"},{"1/6"},{"1/8"},
    {"12"},{"16"},{"24"},{"32"},{"48"},{"64"},{"128"},
};
static const char syncRateNames[][4] = {
    {12},
    {"off"},{"4/1"},{"2/1"},{"1/1"},{"1/2"},{"1/3"},
    {"1/4"},{"1/6"},{"1/8"},{"12"},{"16"},{"32"},
};
static const char extSyncNames[][4] = {
    {5}, {"off"}, {"usb"}, {"din"}, {"pls"}, {"aut"},
};
static const char waveformNames[][4] = {
    {6}, {"Sin"},{"Tri"},{"Saw"},{"Rec"},{"Noi"},{"Cym"},
};
static const char outputNames[][7] = {
    {6}, {"St1"},{"St2"},{"L1"},{"R1"},{"L2"},{"R2"},
};
static const char filterTypes[][8] = {
    {8}, {"LP"},{"HP"},{"BP"},{"UBP"},{"Nch"},{"Pek"},{"LP2"},{"off"},
};
static const char midiRoutingNames[][6] = {
    {6}, {"off"},{"U2M"},{"M2M"},{"A2M"},{"M2U"},{"M2A"},
};
static const char midiFilterNames[][16] = {
    {16},
    {"off"},{"N"},{"R"},{"RN"},{"C"},{"CN"},{"CR"},{"CRN"},
    {"P"},{"PN"},{"PR"},{"PRN"},{"PC"},{"PCN"},{"PCR"},{"all"},
};
static const char trackScaleNames[][4] = {
    {21},
    {"/8"},{"/7"},{"/6"},{"/5"},{"/4"},{"/3"},{"/25"},{"/2"},{"/.6"},{"/.3"},
    {"off"},{"x.3"},{"x.6"},{"x2"},{"x25"},{"x3"},{"x4"},{"x5"},{"x6"},{"x7"},{"x8"},
};

static const char shortNames[][4] = {
    {""}, {"coa"},{"fin"},{"atk"},{"dec"},{"eg2"},{"mod"},{"amt"},
    {"frq"},{"drv"},{"vol"},{"pan"},{"noi"},{"rpt"},{"mix"},
    {"res"},{"typ"},{"f1"},{"f2"},{"g1"},{"g2"},{"wav"},{"dst"},
    {"snc"},{"rtg"},{"ofs"},{"voi"},{"slp"},{"d1"},{"d2"},
    {"eqg"},{"eqf"},
    {"rol"},{"mrp"},{"nte"},{"prb"},{"stp"},{"len"},{"rot"},
    {"bpm"},{"ch"},{"out"},{"srt"},{"nxt"},{"mod"},{"vel"},
    {"fch"},{"flw"},{"qnt"},{"trk"},{"val"},{"shu"},{"ssv"},
    {"x"},{"y"},{"flx"},{"mid"},{"mrt"},{"txf"},{"rxf"},
    {"cki"},{"co1"},{"co2"},{"pcr"},{"cpu"},{"oit"},{"sca"},
    /* PERF per-voice Morph compact labels: voice number + "vm". */
    {"1vm"},{"2vm"},{"3vm"},{"4vm"},{"5vm"},{"6vm"},
};

static const char catNames[][16] = {
    {""}, {"Oscilltr"},{"Veloc EG"},{"Mod EG"},{"PitchMod"},
    {"FM"},{"Voice"},{"Noise"},{"Nois/Osc"},{"Filter"},
    {"Mod Osc"},{"LFO"},{"Transnt"},{"EQ"},
    {"Pattern"},{"Sound"},{"Step"},{"Euklid"},
    {"Global"},{"Velocity"},{"Parametr"},{"Sequencr"},
    {"Generatr"},{"MIDI"},{"Trigger"},
};

static const char longNames[][16] = {
    {""}, {"Coarse"},{"Fine"},{"Attack"},{"Decay"},{"Amount"},
    {"Frequncy"},{"Overdriv"},{"Volume"},{"Panning"},{"Mix"},
    {"RepeatCt"},{"Resnance"},{"Type"},{"Gain"},{"Waveform"},
    {"DstParam"},{"ClockSnc"},{"Retriggr"},{"Offset"},
    {"DstVoice"},{"Slope"},{"Dcy Clsd"},{"Dcy Open"},
    {"RollRate"},{"Morph"},{"Note"},{"Prbablty"},{"Number"},
    {"Length"},{"Steps"},{"Rotation"},{"Tempo"},{"SyncInpt"},
    {"AudioOut"},{"Channel"},{"SampleRt"},{"NextPatt"},{"Phase"},{"Mode"},
    {"Vol mod"},{"Fetch"},{"Follow"},{"Quantize"},{"AutTrack"},
    {"Aut Dest"},{"AutValue"},{"Shuffle"},{"Screensv"},
    {"X Positn"},{"Y Positn"},{"Flux"},{"Velocity"},
    {"Freqcy 1"},{"Freqcy 2"},{"Gain 1"},{"Gain 2"},
    {"Routing"},{"TxFilter"},{"RxFilter"},
    {"In PPQ"},{"Out1 PPQ"},{"Out2 PPQ"},{"Gate Mode"},{"PCReset"},
    {"CPU use time"},{"OscIntrp"},{"Scale"},
    /* Single-parameter long names for PERF per-voice Morph controls. */
    {"1 Morph"},{"2 Morph"},{"3 Morph"},{"4 Morph"},{"5 Morph"},{"6 Morph"},
};

#endif /* MENUTEXT_H_ */
