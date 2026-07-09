/*
 * menu.h — LXR-02 menu system.
 * Ported from original LXR AVR menu.h by Julian Schmidt.
 * PROGMEM / pgmspace stripped. Direct array access throughout.
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

#ifndef __LCD_MENU_MANAGER__
#define __LCD_MENU_MANAGER__

#include <stdint.h>
// #include "Parameters.h"
#include "ParameterArray.h"

#define NUM_SUB_PAGES   8
#define MASK_PARAMETER  0x07
#define MASK_PAGE       0xf8
#define PAGE_SHIFT      3

extern uint8_t menu_activePage;
extern uint8_t menu_activeVoice;
extern uint8_t menu_playedPattern;
extern uint8_t menu_shownPattern;
extern uint8_t menu_currentBar;
extern uint8_t menu_muteModeActive;
/*
 * Voice-page morph endpoint display/edit flag.
 *
 * Why: SHIFT+VOICE reuses the normal voice pages while pointing their values at
 * the morph endpoint buffer instead of the active kit buffer. Menu owns this
 * flag because Menu owns display value selection and edit destinations; Preset
 * owns applying active sound parameters to DSP.
 *
 * Inputs/accessors: buttonHandler sets this through menu_setVoiceModeShowMorph().
 * Outputs/effects: menu repaint and edit helpers choose parameters2[] for
 * voice-page sound parameters while nonzero. Confederates: parameters2[] is the
 * morph-kit endpoint buffer, parameter_values[] is the active-kit buffer, and
 * Preset's morph interpolation reads both. Risk: global, pattern, STEP, load,
 * save, and performance parameters must never be redirected to parameters2[].
 */
extern uint8_t voiceModeShowMorph;

#define NUM_PRESET_LOCATIONS 5
extern uint8_t menu_currentPresetNr[NUM_PRESET_LOCATIONS];

enum PageNames {
    VOICE1_PAGE, VOICE2_PAGE, VOICE3_PAGE,
    VOICE4_PAGE, VOICE5_PAGE, VOICE6_PAGE, VOICE7_PAGE,
    MENU_MIDI_PAGE,
    LOAD_PAGE,
    SAVE_PAGE,
    PERFORMANCE_PAGE,
    SEQ_PAGE,
    EUKLID_PAGE,
    PATTERN_SETTINGS_PAGE,
    RECORDING_PAGE,
    SOM_PAGE,
    NUM_PAGES
};

enum NamesEnum {
    TEXT_EMPTY = 0, TEXT_COARSE, TEXT_FINE, TEXT_ATTACK, TEXT_DECAY,
    TEXT_PITCH_DECAY, TEXT_MOD_AMOUNT, TEXT_FM_AMOUNT, TEXT_FM_FREQ,
    TEXT_DRIVE, TEXT_VOLUME,                    /* 10 */
    TEXT_PAN, TEXT_NOISE, TEXT_MIX, TEXT_REPEAT,
    TEXT_FILTER_F, TEXT_FILTER_RESO, TEXT_FILTER_TYPE,
    TEXT_MOD_OSC1_FREQ, TEXT_MOD_OSC2_FREQ,
    TEXT_MOD_OSC1_GAIN,                         /* 20 */
    TEXT_MOD_OSC2_GAIN,
    TEXT_FREQ_LFO, TEXT_MOD_LFO, TEXT_WAVE_LFO, TEXT_TARGET_LFO,
    TEXT_SYNC_LFO, TEXT_RETRIGGER_LFO, TEXT_OFFSET_LFO,
    TEXT_TARGET_VOICE_LFO,
    TEXT_EG_SLOPE,                              /* 30 */
    TEXT_DECAY_CLOSED, TEXT_DECAY_OPEN, TEXT_WAVEFORM,
    TEXT_TRANSIENT_WAVE, TEXT_TRANSIENT_VOLUME, TEXT_TRANSIENT_FREQ,
    TEXT_EQ_GAIN, TEXT_EQ_FREQ,
    TEXT_ROLL_SPEED, TEXT_X_FADE,               /* 40 */
    TEXT_STEP_VELOCITY, TEXT_NOTE, TEXT_PROBABILITY, TEXT_ACTIVE_STEP,
    TEXT_PAT_LENGTH, TEXT_NUM_STEPS, TEXT_ROTATION,
    TEXT_BPM, TEXT_EXT_SYNC,
    TEXT_MIDI_CHANNEL, TEXT_AUDIO_OUT,          /* 50 */
    TEXT_SAMPLE_RATE, TEXT_PATTERN_BEAT, TEXT_PATTERN_NEXT,
    TEXT_MODE, TEXT_OSC_VOLUME, TEXT_FILTER_DRIVE,
    TEXT_VEL_DEST, TEXT_VEL_AMT, TEXT_VEL_MOD_VOL,
    TEXT_FETCH,                                 /* 60 */
    TEXT_FOLLOW, TEXT_QUANTISATION, TEXT_AUTOMATION_TRACK,
    TEXT_PARAM_DEST, TEXT_PARAM_VAL, TEXT_SHUFFLE,
    TEXT_SCREENSAVER_ON_OFF,
    TEXT_SKIP,
    TEXT_POS_X, TEXT_POS_Y,                     /* 70 */
    TEXT_FLUX, TEXT_SOM_FREQ, TEXT_MIDI_MODE,
    TEXT_MIDI_ROUTING, TEXT_MIDI_FILT_TX, TEXT_MIDI_FILT_RX,
    TEXT_TRIGGER_IN_PPQ, TEXT_TRIGGER_OUT1_PPQ, TEXT_TRIGGER_OUT2_PPQ,
    TEXT_TRIGGER_GATE_MODE, TEXT_BAR_RESET_MODE, TEXT_MIDI_CHAN_GLOBAL,
    TEXT_CPU_USE,
    TEXT_OSC_INTERP,
    TEXT_TRACK_SCALE,
    NUM_NAMES
};

enum catNamesEnum {
    CAT_EMPTY, CAT_OSC, CAT_VELO_EG, CAT_PITCH_EG, CAT_PITCH_MOD,
    CAT_FM, CAT_VOICE, CAT_NOISE, CAT_NOISE_OSC, CAT_FILTER,
    CAT_MOD_OSC, CAT_LFO, CAT_TRANS, CAT_EQ,
    CAT_PATTERN, CAT_SOUND, CAT_STEP, CAT_EUKLID,
    CAT_GLOBAL, CAT_VELOCITY, CAT_PARAMETER, CAT_SEQUENCER,
    CAT_GENERATOR, CAT_MIDI, CAT_TRIGGER
};

enum longNamesEnum {
    LONG_EMPTY, LONG_COARSE, LONG_FINE, LONG_ATTACK, LONG_DECAY,
    LONG_AMOUNT, LONG_FREQ, LONG_DRIVE, LONG_VOL, LONG_PAN, LONG_MIX,
    LONG_REPEAT_CNT, LONG_RESONANCE, LONG_TYPE, LONG_GAIN, LONG_WAVE,
    LONG_DEST_PARAM, LONG_CLOCKSYNC, LONG_RETRIGGER, LONG_OFFSET,
    LONG_DEST_VOICE, LONG_SLOPE, LONG_DECAY_CLOSED, LONG_DECAY_OPEN,
    LONG_ROLLRATE, LONG_MORPH, LONG_NOTE, LONG_PROBABILITY,
    LONG_NUMBER, LONG_LENGTH, LONG_STEPS, LONG_ROTATION,
    LONG_TEMPO, LONG_EXTERNAL_SYNC, LONG_AUDIO_OUT, LONG_MIDI_CHANNEL, LONG_SAMPLE_RATE,
    LONG_NEXT_PAT, LONG_PHASE, LONG_MODE, LONG_VOLUME_MOD,
    LONG_FETCH, LONG_FOLLOW, LONG_QUANTISATION,
    LONG_AUTOMATION_TRACK, LONG_AUTOMATION_DEST, LONG_AUTOMATION_VAL,
    LONG_SHUFFLE, LONG_SCREENSAVER,
    LONG_X, LONG_Y, LONG_FLUX, LONG_VELOCITY,
    LONG_FREQ1, LONG_FREQ2, LONG_GAIN1, LONG_GAIN2,
    LONG_MIDI_ROUTING, LONG_MIDI_FILT_TX, LONG_MIDI_FILT_RX,
    LONG_TRIGGER_IN, LONG_TRIGGER_OUT1, LONG_TRIGGER_OUT2,
    LONG_TRIGGER_GATE_MODE, LONG_BAR_RESET_MODE,
    LONG_CPU_USE_TIME,
    LONG_OSC_INTERP,
    LONG_SCALE,
};

enum shortNamesEnum {
    SHORT_EMPTY, SHORT_COARSE, SHORT_FINE, SHORT_ATTACK, SHORT_DECAY,
    SHORT_EG2, SHORT_MOD, SHORT_FM_AMNT, SHORT_FREQ, SHORT_DRIVE,
    SHORT_VOL, SHORT_PAN, SHORT_NOISE, SHORT_REPEAT, SHORT_MIX,
    SHORT_FIL_RESO, SHORT_FIL_TYPE,
    SHORT_MOD_OSC1_FREQ, SHORT_MOD_OSC2_FREQ,
    SHORT_MOD_OSC1_GAIN, SHORT_MOD_OSC2_GAIN,
    SHORT_WAVE, SHORT_DEST, SHORT_SYNC, SHORT_RETRIGGER, SHORT_OFFSET,
    SHORT_VOICE, SHORT_SLOPE, SHORT_DECAY1, SHORT_DECAY2,
    SHORT_EQ_GAIN, SHORT_EQ_FREQ,
    SHORT_ROLL, SHORT_MORPH, SHORT_NOTE, SHORT_PROBABILITY,
    SHORT_STEP, SHORT_LENGTH, SHORT_ROTATION,
    SHORT_BPM, SHORT_CHANNEL, SHORT_OUT, SHORT_SR, SHORT_NXT,
    SHORT_MODE, SHORT_VELOCITY, SHORT_FETCH, SHORT_FOLLOW, SHORT_QUANT,
    SHORT_TRACK, SHORT_VALUE, SHORT_SHUFFLE, SHORT_SCREEN_SAVER,
    SHORT_X, SHORT_Y, SHORT_FLUX, SHORT_MIDI, SHORT_MIDI_ROUTING,
    SHORT_MIDI_FILT_TX, SHORT_MIDI_FILT_RX,
    SHORT_TRIGGER_IN, SHORT_TRIGGER_OUT1, SHORT_TRIGGER_OUT2,
    SHORT_BAR_RESET_MODE, SHORT_CPU_USE, SHORT_OSC_INTERP, SHORT_SCALE
};

#define PAR_RUNTIME_CPU_USE 0xFFFEu

#define ARROW_SIGN '>'

enum saveStateEnum {
    SAVE_STATE_EDIT_TYPE = 0,
    SAVE_STATE_EDIT_PRESET_NR,
    SAVE_STATE_EDIT_NAME1, SAVE_STATE_EDIT_NAME2,
    SAVE_STATE_EDIT_NAME3, SAVE_STATE_EDIT_NAME4,
    SAVE_STATE_EDIT_NAME5, SAVE_STATE_EDIT_NAME6,
    SAVE_STATE_EDIT_NAME7, SAVE_STATE_EDIT_NAME8,
    SAVE_STATE_OK
};

enum loadSaveEnum {
    SAVE_TYPE_KIT = 0,
    SAVE_TYPE_PATTERN,
    SAVE_TYPE_MORPH,
    SAVE_TYPE_PERFORMANCE,
    SAVE_TYPE_ALL,
    SAVE_TYPE_GLO,
    SAVE_TYPE_SAMPLES,
    NUM_SAVE_TYPES
};

enum Datatypes {
    DTYPE_0B255 = 0,
    DTYPE_0B127,
    DTYPE_PM100,
    DTYPE_MENU,
    DTYPE_PM63,
    DTYPE_1B16,
    DTYPE_ON_OFF,
    DTYPE_MIX_FM,
    DTYPE_TARGET_SELECTION_LFO,
    DTYPE_TARGET_SELECTION_VELO,
    DTYPE_VOICE_LFO,
    DTYPE_AUTOM_TARGET,
    DTYPE_0b1,
    DTYPE_NOTE_NAME,
    DTYPE_0B15,
    DTYPE_1B128,
};

typedef struct PageStruct {
    uint8_t  top1, top2, top3, top4, top5, top6, top7, top8;
    uint16_t bot1, bot2, bot3, bot4, bot5, bot6, bot7, bot8;
} Page;

typedef struct NameStruct {
    const uint8_t shortName;
    const uint8_t category;
    const uint8_t longName;
} Name;

typedef struct ModTargStruct {
    const uint8_t  nameIdx;
    const uint16_t param;
} ModTarg;

typedef struct ModTargetVoiceOffsetStruct {
    uint8_t start;
    uint8_t end;
} ModTargetVoiceOffset;

extern const enum Datatypes parameter_dtypes[NUM_PARAMS];
extern uint8_t parameter_values[NUM_PARAMS];
extern uint8_t parameters2[END_OF_SOUND_PARAMETERS];
extern char currentDisplayBuffer[2][16];
extern char editDisplayBuffer[2][17];

extern const Page menuPages[NUM_PAGES][NUM_SUB_PAGES];

extern const ModTarg modTargets[];
extern const ModTargetVoiceOffset modTargetVoiceOffsets[6];
extern uint8_t paramToModTarget[END_OF_SOUND_PARAMETERS];

void menu_repaintAll(void);
void menu_repaint(void);
void menu_setNumSamples(uint8_t num);
void menu_resetSaveParameters(void);
void menu_init(void);
void menu_start(void);
void menu_parseEncoder(int8_t inc, uint8_t button);
void menu_switchPage(uint8_t pageNr);
void menu_switchSubPage(uint8_t subPageNr);
/*
 * Morph voice view setter.
 *
 * Why: buttonHandler owns the SHIFT+VOICE gesture, but Menu owns whether voice
 * pages display/edit the active kit or morph endpoint buffer. Input onOff is a
 * boolean. Output: voiceModeShowMorph is updated and subsequent Menu repaint,
 * encoder, and endless-pot code uses the matching parameter buffer.
 */
void menu_setVoiceModeShowMorph(uint8_t onOff);
/*
 * STEP front-page half navigation.
 *
 * Why: repeated VOICE presses in STEP mode should toggle between track settings
 * halves without buttonHandler editing menuIndex directly. Output: the visible
 * SEQ_PAGE subpage-0 half is selected and endless-pot mappings are refreshed.
 */
void menu_toggleStepTrackSettingsHalf(void);
void menu_showStepTrackSettingsFirstHalf(void);
void menu_resetActiveParameter(void);
uint8_t menu_getSubPage(void);
/*
 * Parameter buffer resolution for voice morph mode.
 *
 * Inputs are canonical ParameterArray ids from menuPages. Outputs either the
 * currently visible value or a mutable edit pointer for that id. Clients:
 * repaint, encoder, and endless-pot edit code. Risk: callers must still route
 * commits through Menu/Preset helpers so active-kit edits and morph-endpoint
 * edits get the correct side effects.
 */
uint8_t menu_paramUsesMorphView(uint16_t paramNr);
uint8_t menu_getParameterDisplayValue(uint16_t paramNr);
uint8_t *menu_getParameterEditPtr(uint16_t paramNr);
void menu_parseKnobDelta(uint8_t knobNr, int8_t delta);
void menu_notifyExternalParamChanged(uint16_t paramNr);
void menu_serviceKnobRepaint(void);  /* call from main loop after RV1-4 read loop */
void menu_pollPresetStatus(void);   /* call from main loop — handles async SD completion */
void menu_parseGlobalParam(uint16_t paramNr, uint8_t value);
void menu_sendAllParameters(void);
void menu_serviceRuntimeWidgets(void);
uint8_t menu_getActivePage(void);
uint8_t menu_areMuteLedsShown(void);
uint8_t menu_getActiveVoice(void);
void menu_setActiveVoice(uint8_t voiceNr);
void menu_sendAllGlobals(void);

void numtostrpu(char *buf, uint8_t num, char pad);
void numtostrps(char *buf, int8_t num);
void numtostru(char *buf, uint8_t num);

/* Direct page name access for load page kit browser */
void menu_setShownPattern(uint8_t patternNr);
uint8_t menu_getViewedPattern(void);
void sendDisplayBuffer(void);

#endif /* __LCD_MENU_MANAGER__ */
