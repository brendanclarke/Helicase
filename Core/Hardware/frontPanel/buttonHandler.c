/*
 * buttonHandler.c — LXR-02 button handler.
 * Ported from original LXR AVR buttonHandler.c by Julian Schmidt.
 *
 * ISR SAFETY:
 *   buttonHandler_buttonPressed / buttonReleased are called from the TIM6 ISR
 *   (din_dout_exchange). They must not call any LCD functions or enter any
 *   spin-wait. They only write to the event ring and the held[] array.
 *
 *   buttonHandler_processEvents() is called from the main loop. It drains the
 *   ring and calls menu/LED actions, which are safe there.
 */

#include "buttonHandler.h"
#include "menu.h"
#include "screensaver.h"
#include "ledHandler.h"
#include "timebase.h"
#include "copyClearTools.h"
#include "PatternData.h"
#include "EuklidGenerator.h"
#include "sequencer.h"
#include "presetManager.h"
#include <string.h>
#include <stdint.h>
#include "MidiParser.h"

/* -----------------------------------------------------------------------
** Held-state array (written from ISR, read from both ISR and main loop)
** ----------------------------------------------------------------------- */
static volatile uint8_t btn_held[BUT_COUNT];

/* -----------------------------------------------------------------------
** Event ring — ISR writes, main loop reads
** ----------------------------------------------------------------------- */
#define EVT_RING_SIZE 16   /* power of two */
#define EVT_PRESSED   0x80

static volatile uint8_t evt_ring[EVT_RING_SIZE];
static volatile uint8_t evt_head = 0; /* written by ISR */
static volatile uint8_t evt_tail = 0; /* read  by main  */

static inline void evt_push(uint8_t buttonNr, uint8_t pressed)
{
    uint8_t next = (uint8_t)((evt_head + 1) & (EVT_RING_SIZE - 1));
    if (next == evt_tail) return; /* ring full — drop event */
    evt_ring[evt_head] = (uint8_t)(buttonNr | (pressed ? EVT_PRESSED : 0));
    evt_head = next;
}

/* -----------------------------------------------------------------------
** ISR-safe pressed / released — only record, never block
** ----------------------------------------------------------------------- */
void buttonHandler_buttonPressed(uint8_t buttonNr)
{
    screensaver_touch();
    if (buttonNr >= BUT_COUNT) return;
    btn_held[buttonNr] = 1;
    evt_push(buttonNr, 1);
}

void buttonHandler_buttonReleased(uint8_t buttonNr)
{
    if (buttonNr >= BUT_COUNT) return;
    btn_held[buttonNr] = 0;
    evt_push(buttonNr, 0);
}

/* -----------------------------------------------------------------------
** Mode / sequencer interaction state
** ----------------------------------------------------------------------- */
static volatile struct {
    unsigned selectButtonMode :3;
    unsigned seqRunning       :1;
    unsigned seqRecording     :1;
    unsigned seqErasing       :1; /* _SEQUENCER_ADD_SPIKE_: keep erase state while COPY is held */
} bh_state;

static uint8_t lastActiveSubPage = 0;
uint8_t buttonHandler_selectedStep = 0;
static uint8_t selectedStepLed = LED_STEP1;

static uint16_t buttonHandler_buttonTimer = 0;
#define TIMER_ACTION_OCCURED -2
static int8_t buttonHandler_buttonTimerStepNr = NO_STEP_SELECTED;

uint16_t buttonHandler_originalParameter = 0;
uint8_t buttonHandler_originalValue = 0;
uint8_t buttonHandler_resetLock = 0;

static uint8_t buttonHandler_mutedVoices = 0;
static int8_t buttonHandler_armedAutomationStep = NO_STEP_SELECTED;
static uint8_t buttonHandler_morphVoiceModeActive = 0;
/*
 * SEQ presses consumed by the Load menu, retained until their release edge.
 *
 * Menu may change page/submode between press and release while asynchronous
 * storage completes. Remembering the consumed physical button prevents the
 * release dispatcher from falling through into normal step erase/roll logic.
 * ButtonHandler owns this short-lived gesture pairing; Menu owns the policy
 * decision exposed by menu_loadSceneButtonPressed().
 */
static uint16_t buttonHandler_loadSceneSeqPressedMask = 0u;

/* -----------------------------------------------------------------------
** Helpers
** ----------------------------------------------------------------------- */
uint8_t buttonHandler_getMode(void)  { return bh_state.selectButtonMode; }
uint8_t buttonHandler_getShift(void) { return (uint8_t)(btn_held[BUT_SHIFT]); }
int8_t buttonHandler_getArmedAutomationStep(void) { return buttonHandler_armedAutomationStep; }

static void buttonHandler_setMorphVoiceMode(uint8_t onOff)
{
    /*
     * Enter or leave the VOICE-mode morph endpoint overlay.
     *
     * Why: SHIFT+MODE_VOICE should behave like ordinary VOICE mode while Menu
     * displays/edits the morph endpoint buffer. buttonHandler owns the mode
     * gesture and MODE LED feedback; Menu owns the parameter buffer flag.
     *
     * Input onOff is boolean. Outputs: local morph overlay state, Menu's
     * voiceModeShowMorph flag, and the MODE1 blink state are updated together.
     * Confederates: menu_setVoiceModeShowMorph() changes value resolution, and
     * led_setBlinkLed() provides persistent feedback without adding a new LED
     * mode. Risk: this is not a distinct selectButtonMode; SELECT_MODE_VOICE
     * branches must continue to handle subpages and voice selection normally.
     */
    buttonHandler_morphVoiceModeActive = (uint8_t)(onOff != 0u);
    menu_setVoiceModeShowMorph(buttonHandler_morphVoiceModeActive);
    led_setBlinkLed(LED_MODE1, buttonHandler_morphVoiceModeActive);
    if (buttonHandler_morphVoiceModeActive)
        led_setValue(1u, LED_MODE1);
}

void buttonHandler_setRunStopState(uint8_t running)
{
    bh_state.seqRunning = (unsigned)(running & 0x01u);
    led_setValue((uint8_t)bh_state.seqRunning, LED_START_STOP);
}

void buttonHandler_muteVoice(uint8_t voice, uint8_t isMuted)
{
    if (isMuted)
        buttonHandler_mutedVoices |= (uint8_t)(1u << voice);
    else
        buttonHandler_mutedVoices &= (uint8_t)~(1u << voice);

    if (menu_muteModeActive)
        led_setActiveVoiceLeds((uint8_t)(~buttonHandler_mutedVoices));
}

void buttonHandler_showMuteLEDs(void)
{
    led_setActiveVoiceLeds((uint8_t)(~buttonHandler_mutedVoices));
    menu_muteModeActive = 1;
}

/* Map button number to 0-based SEQ index, or -1. */
static int8_t btn_to_seq(uint8_t buttonNr)
{
    switch (buttonNr) {
    case BUT_SEQ1:  return 0;
    case BUT_SEQ2:  return 1;
    case BUT_SEQ3:  return 2;
    case BUT_SEQ4:  return 3;
    case BUT_SEQ5:  return 4;
    case BUT_SEQ6:  return 5;
    case BUT_SEQ7:  return 6;
    case BUT_SEQ8:  return 7;
    case BUT_SEQ9:  return 8;
    case BUT_SEQ10: return 9;
    case BUT_SEQ11: return 10;
    case BUT_SEQ12: return 11;
    case BUT_SEQ13: return 12;
    case BUT_SEQ14: return 13;
    case BUT_SEQ15: return 14;
    case BUT_SEQ16: return 15;
    default:        return -1;
    }
}

/* Map button number to 0-based select index, or -1. */
static int8_t btn_to_select(uint8_t buttonNr)
{
    switch (buttonNr) {
    case BUT_SELECT1: return 0; case BUT_SELECT2: return 1;
    case BUT_SELECT3: return 2; case BUT_SELECT4: return 3;
    case BUT_SELECT5: return 4; case BUT_SELECT6: return 5;
    case BUT_SELECT7: return 6; case BUT_SELECT8: return 7;
    default: return -1;
    }
}

/* Map button number to 0-based voice index, or -1. */
static int8_t btn_to_voice(uint8_t buttonNr)
{
    switch (buttonNr) {
    case BUT_VOICE_1: return 0; case BUT_VOICE_2: return 1;
    case BUT_VOICE_3: return 2; case BUT_VOICE_4: return 3;
    case BUT_VOICE_5: return 4; case BUT_VOICE_6: return 5;
    case BUT_VOICE_7: return 6;
    default: return -1;
    }
}


static uint8_t buttonHandler_barStartStep(void)
{
    /* Current bridge view helper.
     *
     * Menu owns menu_currentBar as the visible 16-step bar. buttonHandler uses
     * this to turn a STEP-row button index into the real PatternData step
     * 0..127 without repeating bar math at each call site. */
    return (uint8_t)(menu_currentBar * NUM_STEPS_PER_BAR);
}

static uint8_t buttonHandler_visibleStep(uint8_t seqButtonPressed)
{
    return (uint8_t)(buttonHandler_barStartStep() + seqButtonPressed);
}

static void buttonHandler_selectBar(uint8_t bar)
{
    /* Change or re-acknowledge the visible 16-step bar.
     *
     * Inputs: bar is SELECT/BAR-derived 0..7. Output: menu_currentBar updates,
     * STEP/SELECT LEDs repaint from PatternData, and the selected-bar SELECT LED
     * runs the current ledHandler flash timing for bar navigation feedback. */
    uint8_t selectRowShowsBar;
    if (bar >= NUM_BARS)
        return;
    menu_currentBar = bar;
    buttonHandler_selectedStep = buttonHandler_barStartStep();
    parameter_values[PAR_ACTIVE_STEP] = buttonHandler_selectedStep;
    selectRowShowsBar = (uint8_t)(bh_state.selectButtonMode != SELECT_MODE_VOICE);
    led_updatePatternTrackView(menu_getActiveVoice(), menu_getViewedPattern(),
                               buttonHandler_selectedStep, selectRowShowsBar);
    pat_applyTrackSettingsToMenu(menu_getViewedPattern(), menu_getActiveVoice());
    if (!selectRowShowsBar)
        led_setActiveSelectButton(menu_getSubPage());
    led_flashGroup(LED_FLASH_GROUP_SELECT, (uint16_t)(1u << bar));
}
static void buttonHandler_updateSubSteps(void)
{
    /*
     * Replaces the old LED_QUERY_SEQ_TRACK parser round-trip.
     *
     * Caller context: foreground button/menu mode changes only. This function
     * is never called from the TIM6 button ISR, so it can touch Menu, Pattern,
     * and LED state directly.
     *
     * Why it lives here: buttonHandler owns the selected-step cursor and knows
     * when the visible track/pattern has changed. ledHandler owns the actual
     * select/step LED writes, and PatternData owns pattern/track values. This
     * helper is the UI glue that refreshes both views after a button action.
     *
     * Inputs: current active voice and viewed pattern are read from Menu, and
     * the selected step is read from buttonHandler_selectedStep.
     *
     * Outputs: select LEDs are repainted from pattern data, and track-scoped
     * menu parameters such as length/rotation/shuffle are loaded for display.
     *
     * Risk: this intentionally preserves the old hidden side effect where the
     * LED query also refreshed menu parameter_values. If that side effect is
     * removed later, every caller that expects fresh track params must be
     * checked.
     */
    led_clearSelectLeds();
    {
        uint8_t trackNr = menu_getActiveVoice();
        uint8_t patternNr = menu_getViewedPattern();
        led_updatePatternTrack(trackNr, patternNr, buttonHandler_selectedStep);
        pat_applyTrackSettingsToMenu(patternNr, trackNr);
    }
}

static void buttonHandler_applyEuklidParamsToMenu(uint8_t track)
{
    /*
     * Why: entering PATGEN needs the current generator values in menu params,
     * but there is no parser request path anymore. Input: track index. Output:
     * PAR_EUKLID_* values updated for repaint. Risk: invalid tracks are ignored.
     */
    if (!pat_trackValid(track))
        return;
    parameter_values[PAR_EUKLID_LENGTH] = euklid_getLength(track);
    parameter_values[PAR_EUKLID_STEPS] = euklid_getSteps(track);
    parameter_values[PAR_EUKLID_ROTATION] = euklid_getRotation(track);
}

static void buttonHandler_enterSeqModeStepMode(void)
{
    menu_showStepTrackSettingsFirstHalf();
    menu_switchPage(SEQ_PAGE);
    buttonHandler_updateSubSteps();
    led_setBlinkLed(selectedStepLed, 1);
}

static void buttonHandler_leaveSeqModeStepMode(void)
{
    led_setBlinkLed(selectedStepLed, 0);
    led_setValue(0, selectedStepLed);
}

static void buttonHandler_armTimerActionStep(int8_t stepNr)
{
    /*
     * Arms the long-press automation editor for one concrete sequencer step.
     *
     * Caller context: buttonHandler_tick() promotes a held step button into an
     * armed automation step after BUTTON_TIMEOUT. The ISR only records button
     * events; this foreground path is where PatternData can be called safely.
     *
     * Why it lives here: the long-press gesture and blink choice are UI state,
     * but the armed automation destination must live in PatternData because it
     * controls later pattern/track mutation performed by menu parameter edits.
     *
     * Inputs: stepNr is a 0..127 absolute bridge step index. The visible bar is
     * already folded into that value, so the blink target is STEP1..16 at
     * stepNr % NUM_STEPS_PER_BAR.
     *
     * Outputs: buttonHandler_armedAutomationStep tracks the UI gesture,
     * pat_armAutomationStep(step, activeVoice, 1) records the edit target and
     * enables recording automation values for that track.
     *
     * Risk: recordAutomation is deliberately hard-coded to 1 to match the old
     * ARM_AUTOMATION_STEP opcode behavior. If automation arming becomes
     * per-pattern/per-track later, PatternData should absorb that policy.
     */
    buttonHandler_armedAutomationStep = stepNr;
    led_setBlinkLed((uint8_t)(LED_STEP1 + ((uint8_t)stepNr % NUM_STEPS_PER_BAR)), 1);

    pat_armAutomationStep((uint8_t)stepNr, menu_getActiveVoice(), 1);
}

static void buttonHandler_disarmTimerActionStep(void)
{
    /*
     * Clears any long-press automation editor state and restores reset-lock UI.
     *
     * Caller context: step button release, a completed timer action, or any
     * path that must cancel the currently armed automation step.
     *
     * Why it lives here: buttonHandler owns the blink LEDs and the temporary
     * reset-lock snapshot. PatternData owns the actual armed automation state,
     * so disarming must update both places explicitly now that the parser has
     * been removed.
     *
     * Inputs: buttonHandler_armedAutomationStep chooses which visible STEP LED
     * to stop blinking. buttonHandler_originalParameter/originalValue describe
     * the value that must be restored when reset-lock was active.
     *
     * Outputs: no return value. The selected blink LED is stopped,
     * pat_armAutomationStep(0, 0, 0) disables PatternData automation arming,
     * and reset-lock restoration is applied either through Preset APIs or
     * menu_parseGlobalParam depending on the parameter range.
     *
     * Risk: this keeps the historical parameter-range split. Sound parameters
     * must go through Preset so DSP state changes with parameter_values; global
     * menu parameters must go through menu_parseGlobalParam so their side
     * effects remain intact.
     */
    if (buttonHandler_armedAutomationStep != NO_STEP_SELECTED) {
        led_setBlinkLed((uint8_t)(LED_STEP1 + ((uint8_t)buttonHandler_armedAutomationStep % NUM_STEPS_PER_BAR)), 0);

        if (buttonHandler_resetLock == 1) {
            parameter_values[buttonHandler_originalParameter] = buttonHandler_originalValue;
        }

        buttonHandler_armedAutomationStep = NO_STEP_SELECTED;
        pat_armAutomationStep(0, 0, 0);

        if (buttonHandler_resetLock == 1) {
            buttonHandler_resetLock = 0;
            if (buttonHandler_originalParameter < 128) {
                preset_applySoundParameter(buttonHandler_originalParameter,
                                           buttonHandler_originalValue, 1);
            } else if (buttonHandler_originalParameter < END_OF_SOUND_PARAMETERS) {
                preset_applySoundParameter(buttonHandler_originalParameter,
                                           buttonHandler_originalValue, 1);
            } else {
                menu_parseGlobalParam(buttonHandler_originalParameter,
                                      parameter_values[buttonHandler_originalParameter]);
            }
            menu_repaintAll();
        }
        return;
    }

    buttonHandler_armedAutomationStep = NO_STEP_SELECTED;
    pat_armAutomationStep(0, 0, 0);
}

static uint8_t buttonHandler_TimerActionOccured(void)
{
    buttonHandler_disarmTimerActionStep();
    if (buttonHandler_buttonTimerStepNr == TIMER_ACTION_OCCURED)
        return 1;

    buttonHandler_buttonTimerStepNr = NO_STEP_SELECTED;
    return 0;
}

static void buttonHandler_setTimeraction(uint8_t buttonNr)
{
    buttonHandler_buttonTimer = (uint16_t)(time_sysTick + BUTTON_TIMEOUT);
    buttonHandler_buttonTimerStepNr = (int8_t)buttonNr;
}

void buttonHandler_tick(void)
{
    /* _SEQUENCER_ADD_SPIKE_: restored AVR long-press timer/arm behavior. */
    if (time_sysTick > buttonHandler_buttonTimer) {
        if (buttonHandler_buttonTimerStepNr >= 0) {
            buttonHandler_armTimerActionStep(buttonHandler_buttonTimerStepNr);
            buttonHandler_buttonTimerStepNr = TIMER_ACTION_OCCURED;
        }
    }
}

static void buttonHandler_selectActiveStep(uint8_t ledNr, uint8_t seqButtonPressed)
{
    /*
     * Selects one visible bridge step as the UI cursor without toggling pattern
     * data.
     *
     * Caller context: STEP/VOICE mode button gestures that should inspect or
     * edit a step. The old parser path fetched step values indirectly; this now
     * calls PatternData directly.
     *
     * Inputs: ledNr is the STEP LED to blink, and seqButtonPressed is the
     * 0..15 step-button index inside menu_currentBar. The selected absolute
     * step is menu_currentBar * 16 + seqButtonPressed.
     *
     * Outputs: selectedStepLed and PAR_ACTIVE_STEP are updated, the active
     * STEP LED blinks, PatternData loads note/velocity/probability/automation
     * values into menu parameter_values, and the visible bar LEDs are refreshed.
     *
     * Risk: this is UI selection only. Any caller that wants to actually toggle
     * a step must call pat_toggleStep() separately.
     */
    led_setBlinkLed(selectedStepLed, 0);
    led_setValue(0, selectedStepLed);

    buttonHandler_selectedStep = buttonHandler_visibleStep(seqButtonPressed);
    selectedStepLed = ledNr;
    parameter_values[PAR_ACTIVE_STEP] = buttonHandler_selectedStep;

    led_setBlinkLed(ledNr, 1);

    pat_applyStepToMenu(menu_getViewedPattern(), menu_getActiveVoice(),
                        buttonHandler_visibleStep(seqButtonPressed));
    buttonHandler_updateSubSteps();
}

static void buttonHandler_showStepParameterPage(void)
{
    /*
     * STEP mode starts on a track front page and moves to the per-step editor
     * only after the user selects a concrete STEP1..16 button.
     *
     * Inputs are implicit current Menu state. Output: SEQ_PAGE subpage 1 is
     * shown when STEP mode owns the SELECT/STEP UI. This keeps the front page
     * visible on mode entry or track change, then leaves it hidden until STEP
     * mode is re-entered or another voice changes the active track.
     */
    if (bh_state.selectButtonMode == SELECT_MODE_STEP &&
        menu_activePage == SEQ_PAGE) {
        if (menu_getSubPage() != 1u)
            menu_switchSubPage(1u);
        menu_repaintAll();
    }
}

static void buttonHandler_setRemoveStep(uint8_t ledNr, uint8_t seqButtonPressed)
{
    /*
     * Toggles one bridge sequencer step for the active voice/viewed pattern.
     *
     * Caller context: non-shift VOICE-mode release, or shift STEP-mode press.
     * In the parser version this went through step opcodes; the button layer
     * now asks PatternData to mutate the pattern directly.
     *
     * Inputs: ledNr is the visible STEP LED, seqButtonPressed is 0..15 and is
     * expanded to the absolute step index for menu_currentBar.
     *
     * Outputs: active-step UI state is updated, PatternData loads that step's
     * editable fields into the menu, PatternData toggles the step active bit,
     * and the STEP LED is rewritten from pat_isStepActive().
     *
     * Risk: the function name is historical. It toggles rather than only
     * removes. Keeping the name avoids unrelated call-site churn during the
     * FrontPanelParser removal.
     */
    uint8_t trackNr;
    uint8_t patternNr;

    led_setValue(0, ledNr);
    seqButtonPressed = buttonHandler_visibleStep(seqButtonPressed);

    buttonHandler_selectedStep = seqButtonPressed;
    parameter_values[PAR_ACTIVE_STEP] = buttonHandler_selectedStep;
    selectedStepLed = ledNr;

    pat_applyStepToMenu(menu_getViewedPattern(), menu_getActiveVoice(), seqButtonPressed);

    trackNr = menu_getActiveVoice();
    patternNr = menu_getViewedPattern();
    pat_toggleStep(trackNr, seqButtonPressed, patternNr);
    led_setValue(pat_isStepActive(trackNr, seqButtonPressed, patternNr),
                 ledNr);
}

static void buttonHandler_setTrackRotation(uint8_t seqButtonPressed)
{
    /*
     * Sets the visible track-rotation edit value from performance mode.
     *
     * Caller context: shift + STEP button while in SELECT_MODE_PERF.
     *
     * Why this calls PatternData: rotation mutates per-pattern/per-track data,
     * so it belongs behind the pat_ API. The LED blink is only feedback for the
     * front-panel performance gesture and therefore stays in buttonHandler/
     * ledHandler instead of PatternData.
     *
     * Inputs: seqButtonPressed is the desired rotation index from the STEP
     * button row. Current pattern and active voice are read from Menu.
     *
     * Outputs: PAR_TRACK_ROTATION is updated for the menu display, PatternData
     * applies the same mutation timing that seq_setTrackRotation() used before
     * this removal pass, and the selected rotation LED blinks.
     *
     * Risk: the long-term scoping target says this policy will change when
     * Pattern owns more sequencer state. This function intentionally preserves
     * current behavior for now.
     */
    parameter_values[PAR_TRACK_ROTATION] = seqButtonPressed;
    pat_setTrackRotation(menu_getViewedPattern(), menu_getActiveVoice(), seqButtonPressed);
    led_clearAllBlinkLeds();
    led_setBlinkLed((uint8_t)(LED_STEP1 + seqButtonPressed), 1);
}

static void buttonHandler_seqButtonPressed(uint8_t seqButtonPressed)
{
    uint8_t ledNr = (uint8_t)(seqButtonPressed + LED_STEP1);

    if (buttonHandler_getShift()) {
        switch (bh_state.selectButtonMode) {
        case SELECT_MODE_VOICE:
            buttonHandler_selectActiveStep(ledNr, seqButtonPressed);
            break;
        case SELECT_MODE_STEP:
            buttonHandler_setRemoveStep(ledNr, seqButtonPressed);
            buttonHandler_showStepParameterPage();
            break;
        case SELECT_MODE_PERF:
            buttonHandler_setTrackRotation(seqButtonPressed);
            break;
        default:
            break;
        }
    } else {
        switch (bh_state.selectButtonMode) {
        case SELECT_MODE_VOICE:
            buttonHandler_setTimeraction(buttonHandler_visibleStep(seqButtonPressed));
            break;
        case SELECT_MODE_STEP:
            led_clearAllBlinkLeds();
            buttonHandler_selectActiveStep(ledNr, seqButtonPressed);
            buttonHandler_showStepParameterPage();
            break;
        case SELECT_MODE_PERF:
            if (seqButtonPressed < 8u) {
                seq_setRoll(seqButtonPressed, 1);
                led_setValue(1, ledNr);
            }
            break;
        default:
            break;
        }
    }
}

static void buttonHandler_seqButtonReleased(uint8_t seqButtonPressed)
{
    uint8_t ledNr = (uint8_t)(seqButtonPressed + LED_STEP1);

    if (buttonHandler_getShift())
        return;

    switch (bh_state.selectButtonMode) {
    case SELECT_MODE_STEP:
        if (buttonHandler_TimerActionOccured())
            return;
        break;

    case SELECT_MODE_VOICE:
        if (buttonHandler_TimerActionOccured())
            return;
        buttonHandler_setRemoveStep(ledNr, seqButtonPressed);
        break;

    case SELECT_MODE_PERF:
        if (seqButtonPressed < 8u) {
            seq_setRoll(seqButtonPressed, 0);
            led_setValue(0, ledNr);
        }
        break;

    default:
        break;
    }
}

static void handleModeButtons(uint8_t mode)
{
    if (menu_loadInstrumentTransactionBusy()) {
        /*
         * Freeze mode ownership through the complete Instrument transaction.
         *
         * Input is any mode press after a staged Instrument request was
         * accepted. Output is no mode, page, blink, or nested-load mutation
         * until Preset finishes commit/rebuild/rebind and Menu releases busy.
         * This gate precedes the special Load/Save exit branch so even that
         * second press cannot detach UI context from the in-flight operation.
         */
        return;
    }
    if (!buttonHandler_getShift() &&
        mode == SELECT_MODE_LOAD_SAVE &&
        menu_loadInstrumentIsActive()) {
        /*
         * Exit nested Instrument Load mode.
         *
         * Inputs: Load/Save mode button while Menu is already browsing
         * instruments. Output: the normal Load page returns and voice blink
         * feedback is cleared. This must run before the generic mode switch so
         * a second Load/Save press does not simply re-enter the same submode.
         */
        led_clearAllBlinkLeds();
        menu_loadInstrumentExit();
        return;
    }

    if (buttonHandler_getShift() && mode == SELECT_MODE_VOICE) {
        /*
         * SHIFT+VOICE is now persistent morph voice mode.
         *
         * Why: the shifted VOICE mode gesture is reserved for viewing/editing
         * morph endpoint parameters on the normal voice pages. It must bypass
         * the old shifted-mode arithmetic, otherwise MODE1+SHIFT lands on a
         * generator/alternate mode instead of staying in VOICE semantics.
         *
         * Inputs: physical MODE1 press while SHIFT is held. Outputs:
         * SELECT_MODE_VOICE remains active, Menu shows the active voice page
         * from parameters2[], and MODE1 blinks until morph mode is left.
         */
        bh_state.selectButtonMode = SELECT_MODE_VOICE;
        led_clearAllBlinkLeds();
        led_setMode2(SELECT_MODE_VOICE);
        buttonHandler_setMorphVoiceMode(1u);
        menu_switchPage(menu_getActiveVoice());
        led_setActiveSelectButton(menu_getSubPage());
        menu_resetActiveParameter();
        menu_repaintAll();
        return;
    }

    buttonHandler_setMorphVoiceMode(0u);

    if (buttonHandler_getShift())
        bh_state.selectButtonMode = (uint8_t)((mode + 4u) & 0x07u);
    else
        bh_state.selectButtonMode = (uint8_t)(mode & 0x07u);

    led_clearAllBlinkLeds();
    led_setMode2(bh_state.selectButtonMode);

    switch (bh_state.selectButtonMode) {
    case SELECT_MODE_PERF:
        led_clearSequencerLeds();
        led_clearSelectLeds();
        led_initPerformanceLeds();
        lastActiveSubPage = menu_getSubPage();
        menu_switchPage(PERFORMANCE_PAGE);
        menu_switchSubPage(0);
        menu_repaintAll();
        break;

    case SELECT_MODE_STEP:
        led_setActiveSelectButton(menu_getSubPage());
        buttonHandler_enterSeqModeStepMode();
        break;

    case SELECT_MODE_VOICE:
        menu_switchPage(menu_getActiveVoice());
        led_setActiveSelectButton(menu_getSubPage());
        menu_resetActiveParameter();
        break;

    case SELECT_MODE_LOAD_SAVE:
        menu_switchPage(LOAD_PAGE);
        break;

    case SELECT_MODE_MENU:
        menu_switchPage(MENU_MIDI_PAGE);
        break;

    case SELECT_MODE_PAT_GEN:
        /*
         * Entering the Euclidean generator page needs the active track's
         * generator state visible in the menu immediately.
         *
         * Old behavior: the menu requested this through frontPanelParser.
         * New behavior: buttonHandler reads EuklidGenerator directly because
         * Euklid data now lives with Pattern under Core/Scene/Pattern.
         */
        buttonHandler_applyEuklidParamsToMenu(menu_getActiveVoice());
        menu_switchPage(EUKLID_PAGE);
        break;

    case SELECT_MODE_SOM_GEN:
        menu_switchPage(SOM_PAGE);
        break;

    default:
        break;
    }
}

static void handleSelectButton(uint8_t selectNr)
{
    if (buttonHandler_getShift()) {
        switch (bh_state.selectButtonMode) {
        case SELECT_MODE_STEP:
        case SELECT_MODE_VOICE:
            buttonHandler_selectBar(selectNr);
            break;

        case SELECT_MODE_PAT_GEN:
            buttonHandler_selectBar(selectNr);
            break;

        case SELECT_MODE_PERF:
            break;

        default:
            break;
        }
        return;
    }

    switch (bh_state.selectButtonMode) {
    case SELECT_MODE_STEP:
        buttonHandler_selectBar(selectNr);
        break;

    case SELECT_MODE_VOICE:
        menu_switchSubPage(selectNr);
        menu_resetActiveParameter();
        led_setActiveSelectButton(selectNr);
        menu_repaintAll();
        break;

    case SELECT_MODE_PAT_GEN:
        buttonHandler_selectBar(selectNr);
        break;

    case SELECT_MODE_PERF:
        /*
         * Single-pattern bridge re-align gesture.
         *
         * PERF SELECT1 is the only active pattern button while NUM_PATTERN is
         * one. Pressing it again does not queue a pattern; it asks Sequencer to
         * re-derive every track counter from the master step clock, track
         * length, rotation, and scale. Other SELECT buttons stay inactive until
         * the later Scene/pattern-selection trigger design replaces this.
         */
        if (selectNr == 0u &&
            seq_activePattern == 0u &&
            menu_getViewedPattern() == 0u) {
            seq_realignActivePatternToMasterClock();
            led_flashGroup(LED_FLASH_GROUP_SELECT, 0x0001u);
        }
        break;

    case SELECT_MODE_LOAD_SAVE:
        /* _SEQUENCER_ADD_SPIKE_: AVR parity for load/save page select LED. */
        led_setActivePage(selectNr);
        break;

    default:
        break;
    }
}

static void buttonHandler_partButtonPressed(uint8_t partNr)
{
    if (copyClear_Mode >= MODE_COPY_PATTERN) {
        if (copyClear_srcSet()) {
            uint8_t trackNr;
            uint8_t patternNr;

            copyClear_setDst((int8_t)partNr, MODE_COPY_PATTERN);
            copyClear_copyBar();
            led_clearAllBlinkLeds();

            trackNr = menu_getActiveVoice();
            patternNr = menu_getViewedPattern();
            led_updatePatternTrack(trackNr, patternNr, buttonHandler_selectedStep);
            pat_applyTrackSettingsToMenu(patternNr, trackNr);
        } else {
            copyClear_setSrc((int8_t)partNr, MODE_COPY_PATTERN);
            led_setBlinkLed((uint8_t)(LED_PART_SELECT1 + partNr), 1);
        }
    } else {
        handleSelectButton(partNr);
    }
}

static void buttonHandler_partButtonReleased(uint8_t partNr)
{
    (void)partNr;

    if (copyClear_Mode >= MODE_COPY_PATTERN) {
        return;
    }

    if (buttonHandler_TimerActionOccured())
        return;

    buttonHandler_buttonTimerStepNr = NO_STEP_SELECTED;
}

static void handleVoiceButton(uint8_t voiceNr)
{
    uint8_t wasSelectedVoice;
    uint8_t shouldPreviewVoice;

    if (menu_loadInstrumentTransactionBusy()) {
        /*
         * Consume voice selection and preview during Instrument commit.
         *
         * Input is any VOICE press while the captured destination is busy.
         * Output is no trigger and no destination/active-voice change. Preview
         * must be blocked as well as selection because the incoming runtime is
         * reset and rebuilt over bounded foreground ticks before it is valid to
         * audition.
         */
        return;
    }

    if (copyClear_Mode >= MODE_COPY_PATTERN) {
        if (copyClear_srcSet()) {
            /*
             * Finish track copy.
             *
             * PatternData copies between tracks inside the viewed pattern. The
             * button layer then repaints the front-panel LEDs and reloads
             * track-scoped menu parameters for the active voice.
             *
             * Risk: copy source/destination are stored as button indices and
             * masked in copyClearTools. Validation still belongs in PatternData
             * for the actual mutation.
             */
            uint8_t trackNr;
            uint8_t patternNr;

            copyClear_setDst((int8_t)voiceNr, MODE_COPY_TRACK);
            copyClear_copyTrack();
            led_clearAllBlinkLeds();

            trackNr = menu_getActiveVoice();
            patternNr = menu_getViewedPattern();
            led_updatePatternTrack(trackNr, patternNr, buttonHandler_selectedStep);
            pat_applyTrackSettingsToMenu(patternNr, trackNr);
        } else {
            copyClear_setSrc((int8_t)voiceNr, MODE_COPY_TRACK);
            led_setBlinkLed((uint8_t)(LED_VOICE1 + voiceNr), 1);
        }
        return;
    }

    /*
     * Preserve the stopped-transport audition gesture inside Instrument Load.
     *
     * Input: a repeated press of Menu's current Instrument Load destination.
     * Output: triggers that voice without reopening/resetting the nested load
     * cursor. This remains in ButtonHandler because seq_previewVoice() is the
     * existing front-panel audition endpoint; Menu only owns load selection.
     * Track 7 already reaches the ordinary preview path because it is not an
     * Instrument Load destination slot.
     */
    if (menu_loadInstrumentIsActive() && voiceNr == menu_getActiveVoice() &&
        !seq_isRunning()) {
        seq_previewVoice(voiceNr);
        return;
    }

    if (menu_loadInstrumentVoicePressed(voiceNr)) {
        uint8_t blink_voice;

        /*
         * LOAD_PAGE voice buttons select Instrument Load destination slots.
         *
         * Inputs: pressed voice button while Menu is on LOAD_PAGE. Output:
         * Menu enters/updates Instrument Load mode, active voice LED follows
         * the selected destination, and that voice blinks until the user exits
         * Instrument Load. Only VOICE blink state is cleared here: clearing all
         * blink LEDs would erase Menu's active-Scene SEQ feedback immediately
         * after it was painted. Normal voice selection, mute, and page
         * switching are skipped for this press.
         */
        led_setActiveVoice(voiceNr);
        for (blink_voice = 0u; blink_voice < INSTRUMENT_SLOT_COUNT;
             blink_voice++) {
            led_setBlinkLed((uint8_t)(LED_VOICE1 + blink_voice), 0u);
        }
        led_setBlinkLed((uint8_t)(LED_VOICE1 + voiceNr), 1u);
        return;
    }

    /*
     * Stopped-transport voice preview gate.
     *
     * Why: selecting a different VOICE changes UI context, but re-pressing the
     * already selected VOICE is an audition gesture when playback is stopped.
     * Inputs are the pressed voice, Menu's current active voice, and Sequencer
     * running state. Output is a boolean consumed after non-copy/non-mute voice
     * actions finish. Confederates: seq_previewVoice() owns the actual synth
     * and MIDI trigger path; copy/mute branches return before using this flag.
     */
    wasSelectedVoice = (uint8_t)(voiceNr == menu_getActiveVoice());
    shouldPreviewVoice = (uint8_t)(wasSelectedVoice && !seq_isRunning());

    {
        uint8_t muteModeActive = buttonHandler_getShift();
        if (bh_state.selectButtonMode == SELECT_MODE_PERF)
            muteModeActive = (uint8_t)(1u - muteModeActive);

        if (muteModeActive) {
            /*
             * Per-track mute is Sequencer playback state, so buttonHandler now
             * calls seq_setMute() directly after updating the local LED-facing
             * mute bitset. The parser opcode carried no useful abstraction
             * once the split front-panel processor architecture was removed.
             */
            if (buttonHandler_mutedVoices & (1u << voiceNr)) {
                buttonHandler_muteVoice(voiceNr, 0);
                seq_setMute(voiceNr, 0);
            } else {
                buttonHandler_muteVoice(voiceNr, 1);
                seq_setMute(voiceNr, 1);
            }
            return;
        }

        if (bh_state.selectButtonMode == SELECT_MODE_PERF) {
            /*
             * PERF voice buttons clear mutes up to the selected voice and then
             * repaint mute LEDs. The actual audible mute state belongs to
             * Sequencer, while buttonHandler_mutedVoices is the front-panel
             * shadow used to draw the current mute view.
             */
            uint8_t i;
            for (i = 0; i <= voiceNr; i++) {
                if (buttonHandler_mutedVoices & (1u << i)) {
                    seq_setMute(i, 0);
                    buttonHandler_mutedVoices &= (uint8_t)~(1u << i);
                }
            }
            buttonHandler_showMuteLEDs();
            if (shouldPreviewVoice)
                seq_previewVoice(voiceNr);
            return;
        }

        menu_setActiveVoice(voiceNr);
        led_setActiveVoice(voiceNr);
        if (bh_state.selectButtonMode == SELECT_MODE_VOICE) {
            menu_switchPage(voiceNr);
            led_setActiveSelectButton(menu_getSubPage());
        }

        /*
         * Active voice is UI/menu context, so Menu owns the selected value.
         * Euklid params are then pulled directly from the Pattern generator
         * module so the generator page is correct if the user switches there.
         */
        buttonHandler_applyEuklidParamsToMenu(voiceNr);

        if (bh_state.selectButtonMode == SELECT_MODE_STEP ||
            menu_activePage == SEQ_PAGE) {
            led_clearAllBlinkLeds();
            /*
             * STEP-mode VOICE presses own the track-settings front-page half.
             *
             * Why: selecting a new track should show the primary settings
             * half, while re-pressing the already selected track toggles to
             * the second half where per-track shuffle lives. Menu owns
             * menuIndex, so buttonHandler uses Menu helpers instead of editing
             * index bits directly. Output: the active track settings are
             * refreshed and the selected step LED resumes blinking.
             */
            if (wasSelectedVoice)
                menu_toggleStepTrackSettingsHalf();
            else
                menu_showStepTrackSettingsFirstHalf();
            menu_switchPage(SEQ_PAGE);
            buttonHandler_updateSubSteps();
            led_setBlinkLed(selectedStepLed, 1);
        } else if (menu_activePage == EUKLID_PAGE) {
            menu_repaintAll();
        }

        if (shouldPreviewVoice)
            seq_previewVoice(voiceNr);
    }
}

/* Process one press event */
static void processPress(uint8_t buttonNr)
{
    int8_t seq = btn_to_seq(buttonNr);
    if (seq >= 0) {
        if (menu_loadSceneButtonPressed((uint8_t)seq)) {
            buttonHandler_loadSceneSeqPressedMask = (uint16_t)(
                buttonHandler_loadSceneSeqPressedMask |
                (uint16_t)(1u << (uint8_t)seq));
            return;
        }
        buttonHandler_seqButtonPressed((uint8_t)seq);
        return;
    }

    {
        int8_t sel = btn_to_select(buttonNr);
        if (sel >= 0) {
            buttonHandler_partButtonPressed((uint8_t)sel);
            return;
        }
    }

    {
        int8_t voice = btn_to_voice(buttonNr);
        if (voice >= 0) {
            handleVoiceButton((uint8_t)voice);
            return;
        }
    }

    switch (buttonNr) {
    case BUT_MODE1:
    case BUT_MODE2:
    case BUT_MODE3:
    case BUT_MODE4:
        /* BUT_MODE1=31, BUT_MODE4=28: mode = 31 - buttonNr */
        handleModeButtons((uint8_t)(BUT_MODE1 - buttonNr));
        break;

    case BUT_START_STOP:
        /*
         * START/STOP is transport state. buttonHandler owns the physical LED
         * and toggled UI bit; Sequencer owns whether playback actually runs.
         * The old parser command is gone because this is now a direct same-CPU
         * call with no serialization boundary.
         */
        buttonHandler_setRunStopState((uint8_t)(1u - bh_state.seqRunning));
        seq_setRunning((uint8_t)bh_state.seqRunning);
        break;

    case BUT_REC:
        if (buttonHandler_getShift()) {
            menu_switchPage(RECORDING_PAGE);
        } else {
            /*
             * Recording mode is Sequencer playback/edit state. The REC LED is
             * local UI feedback, while seq_setRecordingMode() is the source of
             * truth for how incoming notes and button gestures are recorded.
             */
            bh_state.seqRecording = (uint8_t)((1u - bh_state.seqRecording) & 0x01u);
            led_setValue((uint8_t)bh_state.seqRecording, LED_REC);
            seq_setRecordingMode((uint8_t)bh_state.seqRecording);
        }
        break;

    case BUT_COPY:
        if (buttonHandler_getShift()) {
            if (bh_state.seqRecording && bh_state.seqRunning) {
                /*
                 * SHIFT+COPY while recording/running enters erase mode. This
                 * is direct Sequencer state because erase affects playback-time
                 * recording behavior, not copy/clear PatternData utilities.
                 */
                bh_state.seqErasing = 1;
                seq_setErasingMode((uint8_t)bh_state.seqErasing);
            } else {
                if (copyClear_Mode == MODE_CLEAR) {
                    copyClear_executeClear();
                } else {
                    copyClear_Mode = MODE_CLEAR;
                    copyClear_armClearMenu(1);
                }
            }
        } else {
            copyClear_Mode = MODE_COPY_TRACK;
            led_setBlinkLed(LED_COPY, 1);
            led_clearSelectLeds();
            led_clearVoiceLeds();
        }
        break;

    case BUT_BAR1:
        led_setValue(1, LED_BAR1);
        if (menu_currentBar > 0u)
            buttonHandler_selectBar((uint8_t)(menu_currentBar - 1u));
        else
            led_flashGroup(LED_FLASH_GROUP_SELECT, 0x0001u);
        break;

    case BUT_BAR2:
        led_setValue(1, LED_BAR2);
        if (menu_currentBar < (NUM_BARS - 1u))
            buttonHandler_selectBar((uint8_t)(menu_currentBar + 1u));
        else
            led_flashGroup(LED_FLASH_GROUP_SELECT,
                           (uint16_t)(1u << (NUM_BARS - 1u)));
        break;

    case BUT_SHIFT:
        /* _SEQUENCER_ADD_SPIKE_: restore SHIFT-press mode behavior parity with AVR. */
        led_setValue(1, LED_SHIFT);
        switch (bh_state.selectButtonMode) {
        case SELECT_MODE_VOICE:
            /*
             * Holding SHIFT in VOICE mode no longer enters a temporary STEP
             * overlay.
             *
             * Why: SHIFT+MODE_VOICE is now the persistent morph voice mode
             * gesture. A plain SHIFT press must not steal the UI away from
             * voice pages, because morph endpoint editing uses the same pages,
             * SELECT subpages, encoder, and endless pots as normal voice mode.
             * Output: only the physical SHIFT LED changes for this gesture.
             */
            return;

        case SELECT_MODE_PERF:
        case SELECT_MODE_PAT_GEN:
        {
            uint8_t trackNr;
            uint8_t patternNr;

            menu_switchPage(PATTERN_SETTINGS_PAGE);
            led_clearSelectLeds();
            led_clearAllBlinkLeds();

            if (bh_state.selectButtonMode == SELECT_MODE_PAT_GEN) {
                led_setBlinkLed(LED_MODE2, 1);
            } else {
                led_setBlinkLed((uint8_t)(LED_STEP1 + parameter_values[PAR_TRACK_ROTATION]), 1);
            }

            if (bh_state.selectButtonMode == SELECT_MODE_PAT_GEN && parameter_values[PAR_FOLLOW]) {
                /*
                 * Follow mode means the viewed pattern should snap back to the
                 * sequencer-followed pattern when entering the shift layer.
                 *
                 * After changing the shown pattern, the UI must explicitly
                 * reload LEDs plus PatternData-backed pattern/track params.
                 * This used to be hidden behind parser query opcodes.
                 */
                menu_setShownPattern(menu_shownPattern);
                led_clearSequencerLeds();
                trackNr = menu_getActiveVoice();
                patternNr = menu_getViewedPattern();
                led_updatePatternTrack(trackNr, patternNr, buttonHandler_selectedStep);
                pat_applyPatternSettingsToMenu(patternNr);
                pat_applyTrackSettingsToMenu(patternNr, trackNr);
            }

            led_setBlinkLed((uint8_t)(LED_PART_SELECT1 + menu_getViewedPattern()), 1);
            break;
        }

        case SELECT_MODE_STEP:
            buttonHandler_leaveSeqModeStepMode();
            break;

        default:
            break;
        }

        buttonHandler_showMuteLEDs();
        break;

    default:
        break;
    }
}

static void processRelease(uint8_t buttonNr)
{
    int8_t seq = btn_to_seq(buttonNr);
    if (seq >= 0) {
        uint16_t bit = (uint16_t)(1u << (uint8_t)seq);
        if ((buttonHandler_loadSceneSeqPressedMask & bit) != 0u) {
            buttonHandler_loadSceneSeqPressedMask = (uint16_t)(
                buttonHandler_loadSceneSeqPressedMask & (uint16_t)(~bit));
            return;
        }
        buttonHandler_seqButtonReleased((uint8_t)seq);
        return;
    }

    {
        int8_t sel = btn_to_select(buttonNr);
        if (sel >= 0) {
            buttonHandler_partButtonReleased((uint8_t)sel);
            return;
        }
    }

    switch (buttonNr) {
    case BUT_BAR1:
        led_setValue(0, LED_BAR1);
        break;

    case BUT_BAR2:
        led_setValue(0, LED_BAR2);
        break;

    case BUT_COPY:
        /* _SEQUENCER_ADD_SPIKE_: restore erase exit + copy-mode reset on release. */
        if (bh_state.seqErasing) {
            bh_state.seqErasing = 0;
            seq_setErasingMode((uint8_t)bh_state.seqErasing);
        } else if (!buttonHandler_getShift()) {
            copyClear_reset();
        }
        break;

    case BUT_SHIFT:
        /* _SEQUENCER_ADD_SPIKE_: restore shift-release unwind flow from AVR. */
        if (bh_state.seqErasing) {
            bh_state.seqErasing = 0;
            seq_setErasingMode((uint8_t)bh_state.seqErasing);
        }

        if (copyClear_Mode == MODE_CLEAR && !btn_held[BUT_COPY]) {
            copyClear_armClearMenu(0);
            copyClear_Mode = MODE_NONE;
        }

        led_setValue(0, LED_SHIFT);

        switch (bh_state.selectButtonMode) {
        case SELECT_MODE_VOICE:
            /*
             * VOICE-mode SHIFT release pairs with the no-op SHIFT press above.
             *
             * Output: restore the selected voice LEDs only. Do not call
             * buttonHandler_leaveSeqMode(), because SHIFT no longer entered
             * the STEP overlay in VOICE mode.
             */
            led_setActiveVoice(menu_getActiveVoice());
            if (buttonHandler_morphVoiceModeActive)
                led_setBlinkLed(LED_MODE1, 1u);
            break;

        case SELECT_MODE_PERF:
            led_clearAllBlinkLeds();
            led_clearSelectLeds();
            menu_switchPage(PERFORMANCE_PAGE);
            led_initPerformanceLeds();
            return;

        case SELECT_MODE_PAT_GEN:
            led_clearSelectLeds();
            led_setValue(1, (uint8_t)(menu_getViewedPattern() + LED_PART_SELECT1));
            menu_switchPage(EUKLID_PAGE);
            break;

        case SELECT_MODE_STEP:
            buttonHandler_enterSeqModeStepMode();
            break;

        default:
            break;
        }

        if (bh_state.selectButtonMode != SELECT_MODE_PERF)
            led_setActiveVoice(menu_getActiveVoice());
        else
            buttonHandler_showMuteLEDs();

        break;

    default:
        break;
    }
}

/* -----------------------------------------------------------------------
** buttonHandler_processEvents — call from main loop, safe to call LCD
**
** Drains ONE event per call. Original AVR processed one button per main-loop
** iteration. With our 1kHz TIM6 SPI exchange all 40 button states arrive
** atomically and can fire many events at once. `if` keeps the original AVR
** cadence of one button per main-loop pass.
** ----------------------------------------------------------------------- */
void buttonHandler_processEvents(void)
{
    if (evt_tail != evt_head) {
        uint8_t ev  = evt_ring[evt_tail];
        evt_tail = (uint8_t)((evt_tail + 1) & (EVT_RING_SIZE - 1));

        uint8_t pressed  = (uint8_t)((ev & EVT_PRESSED) != 0);
        uint8_t buttonNr = (uint8_t)(ev & (uint8_t)~EVT_PRESSED);

        if (pressed)
            processPress(buttonNr);
        else
            processRelease(buttonNr);
    }
}
