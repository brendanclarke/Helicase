/*
 * buttonHandler.c — LXR-02 button handler.
 * Ported from original LXR AVR buttonHandler.c by Julian Schmidt.
 *
 * CONCURRENCY:
 *   buttonHandler_buttonPressed / buttonReleased are called from the
 *   foreground scan (din_dout_exchange via timebase_serviceFrontPanel at
 *   500 Hz), not from an ISR. They write to the event ring and btn_held[].
 *
 *   buttonHandler_processEvents() is called from the main loop (two call
 *   sites, separated by audio_check_and_render). It drains the ring and
 *   dispatches to menu/LED actions. buttonHandler_tick() polls the hold timer
 *   between the two drain calls.
 *
 *   The volatile qualifiers on btn_held[] and the ring remain correct: the
 *   scan and consumer can interleave around audio rendering within the same
 *   foreground context.
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
#include "AutosaveTrace.h"

/* -----------------------------------------------------------------------
** Held-state array (written from foreground scan, read from main loop)
** ----------------------------------------------------------------------- */
static volatile uint8_t btn_held[BUT_COUNT];

/* -----------------------------------------------------------------------
** Event ring — foreground scan writes, main loop reads
**
** What: a power-of-two SPSC ring storing one byte per button edge event.
** The high bit (EVT_PRESSED) encodes direction; the low seven bits encode
** the button number (0..BUT_COUNT-1).
**
** Why monotonic counters instead of masked head/tail: the classic
** `(head+1)&mask == tail` guard reserves one slot to distinguish full from
** empty, leaving only SIZE-1 usable entries. With 16 SEQ buttons, modifier
** keys, and transport buttons, a simultaneous release burst can exceed that
** capacity. Monotonic producer/consumer counters (matching the
** PatternStackService pattern) use `(producer - consumer) == SIZE` for full
** detection, making all SIZE entries usable and eliminating the off-by-one
** capacity loss.
**
** Why 64 entries: the hardware has 41 shift-register buttons. Even if every
** button changes state in a single scan pass (physically impossible but the
** architectural maximum), 41 events cannot overflow 64 slots.
**
** Inputs: evt_push() is called by the foreground din_dout_exchange() scan at
** 500 Hz. Outputs: buttonHandler_processEvents() drains one event per call
** from the main loop. Two call sites give roughly 30 events per scan.
**
** RAM: 64 bytes ring plus one unconditional overflow flag and one
** DEV_MODE_LOGGING-only drop counter (66 bytes at the approved maximum).
** ----------------------------------------------------------------------- */
#define EVT_RING_SIZE 64u  /* power of two, greater than BUT_COUNT (41) */
#define EVT_RING_MASK (EVT_RING_SIZE - 1u)
#define EVT_PRESSED   0x80u

static volatile uint8_t evt_ring[EVT_RING_SIZE];
static volatile uint8_t evt_producer = 0; /* monotonic, written by scan */
static volatile uint8_t evt_consumer = 0; /* monotonic, written by main */

/*
 * Overflow detection state.
 *
 * What: evt_overflow_flag is set by evt_push() when the ring is full.
 * buttonHandler_processEvents() checks and clears it on the next drain pass,
 * using it to reconcile gesture state that may have been corrupted by the
 * dropped event. evt_drop_count is a saturating logging-only count included
 * in the diagnostic trace record and reset after emission.
 *
 * Why the flag is unconditional: even a production build must reconcile
 * pairing masks and the hold timer after a dropped event. The count is
 * logging-only because its value is useful only in trace analysis.
 *
 * Lifetime: file-scope static .bss, never freed.
 */
static volatile uint8_t evt_overflow_flag = 0;
#if DEV_MODE_LOGGING
static volatile uint8_t evt_drop_count = 0;
#endif

/*
 * Push one button edge event into the ring.
 *
 * What: encodes buttonNr and direction into one byte, stores it at the
 * producer's slot, and advances the monotonic producer counter. If the ring
 * is full, the event is dropped and the overflow flag is set.
 *
 * Why inline: this runs during the 500 Hz scan, potentially once per hardware
 * button in a single pass. The body is a few compares and a byte store.
 *
 * Full detection: unsigned modular subtraction remains correct across the
 * 0xff-to-0x00 counter wrap because EVT_RING_SIZE divides the uint8_t range.
 *
 * Inputs: buttonNr is a BUT_* enum value; pressed is nonzero for a press edge.
 * Outputs: one ring entry written, or overflow state updated on a drop.
 * Affiliates: evt_consumer is read but never written here.
 */
static inline void evt_push(uint8_t buttonNr, uint8_t pressed)
{
    if ((uint8_t)(evt_producer - evt_consumer) >= EVT_RING_SIZE) {
        evt_overflow_flag = 1;
#if DEV_MODE_LOGGING
        if (evt_drop_count < 255u)
            evt_drop_count++;
#endif
        return;
    }
    evt_ring[evt_producer & EVT_RING_MASK] =
        (uint8_t)(buttonNr | (pressed ? EVT_PRESSED : 0u));
    evt_producer++;
}

/* -----------------------------------------------------------------------
** Scan-safe pressed / released — only record, never block
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
 * Foreground MODE VOICE hold overlay state.
 *
 * Why: btn_held[] is written immediately by the foreground scan, while
 * press/release events are consumed later from the ring. A SEQ press that
 * happened while MODE VOICE was physically held can therefore be processed
 * after the scan has already cleared btn_held[BUT_MODE1] for the release edge. This flag is set
 * and cleared in event order by processPress()/processRelease(), so MODE VOICE
 * Scene-mask SEQ buttons are always consumed as overlay toggles and cannot leak
 * into normal step editing.
 */
static uint8_t buttonHandler_voiceSceneMaskHoldActive = 0u;
/*
 * SEQ presses consumed by the MODE VOICE Scene-mask overlay.
 *
 * Why: consuming only the press edge is not enough in VOICE mode. The normal
 * VOICE SEQ release path toggles a Pattern step when no long-press timer action
 * occurred, so an overlay press must also suppress its matching release edge.
 * Inputs are physical SEQ press bits accepted by menu_voiceHeldSceneButtonPressed();
 * output is release-edge consumption in processRelease().
 */
static uint16_t buttonHandler_voiceSceneSeqPressedMask = 0u;
/*
 * SEQ presses consumed by the Load/Save menu, retained until their release edge.
 *
 * Menu may change page/submode between press and release while asynchronous
 * storage completes. Remembering the consumed physical button prevents the
 * release dispatcher from falling through into normal step erase/roll logic.
 * ButtonHandler owns this short-lived gesture pairing; Menu owns whether the
 * current Load/Save context treats SEQ buttons as Scene target toggles.
 */
static uint16_t buttonHandler_loadSceneSeqPressedMask = 0u;

/* -----------------------------------------------------------------------
** Helpers
** ----------------------------------------------------------------------- */
uint8_t buttonHandler_getMode(void)  { return bh_state.selectButtonMode; }
uint8_t buttonHandler_getShift(void) { return (uint8_t)(btn_held[BUT_SHIFT]); }
int8_t buttonHandler_getArmedAutomationStep(void) { return buttonHandler_armedAutomationStep; }

/*
 * Return the raw physical held-state mask for the sixteen SEQ buttons.
 *
 * What: translates the scattered shift-register button numbers into a compact
 * bitmask for Menu's held-step overlay. Why: the foreground scan state remains
 * private so callers cannot depend on hardware ordering. Inputs: volatile
 * btn_held[].
 * Output: bit N is set for the physically held SEQ(N+1) button. This is a
 * foreground read of byte-sized scan values and performs no UI work.
 */
/*
 * Physical button numbers for the sixteen SEQ buttons, indexed 0..15.
 *
 * What: maps the logical step-button index used by Menu, PatternData, and the
 * hold timer back to the physical BUT_SEQ* number for btn_held[] lookup.
 *
 * Why file scope: buttonHandler_tick() needs this table to reverse-map
 * buttonHandler_buttonTimerStepNr (an absolute step 0..127) back to a
 * physical button number for the held-check. buttonHandler_seqHeldMask()
 * uses the same table for the opposite physical-to-logical translation.
 *
 * Inputs: index is a 0..15 SEQ button index. Output: BUT_SEQ* enum value.
 * RAM: zero additional; the static const table was already in .rodata.
 */
static const uint8_t seq_buttons[16] = {
    BUT_SEQ1, BUT_SEQ2, BUT_SEQ3, BUT_SEQ4,
    BUT_SEQ5, BUT_SEQ6, BUT_SEQ7, BUT_SEQ8,
    BUT_SEQ9, BUT_SEQ10, BUT_SEQ11, BUT_SEQ12,
    BUT_SEQ13, BUT_SEQ14, BUT_SEQ15, BUT_SEQ16
};

uint16_t buttonHandler_seqHeldMask(void)
{
    uint16_t mask = 0u;
    uint8_t i;

    for (i = 0u; i < 16u; i++) {
        if (btn_held[seq_buttons[i]])
            mask |= (uint16_t)(1u << i);
    }
    return mask;
}

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

/*
 * Convert a visible SEQ button index into the absolute Pattern step.
 *
 * What: folds the current Menu bar into the fixed 128-step track. Why: Menu's
 * overlay retains compact button indices for press ordering but PatternData
 * APIs use absolute coordinates. Inputs: zero-based visible button index.
 * Output: menu_currentBar * NUM_STEPS_PER_BAR + index.
 */
uint8_t buttonHandler_visibleStep(uint8_t seqButtonPressed)
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
    if (!selectRowShowsBar)
        led_setActiveSelectButton(menu_getSubPage());
    led_flashGroup(LED_FLASH_GROUP_SELECT, (uint16_t)(1u << bar));
    /* S066: the held-step overlay uses the new bar for absolute Pattern
     * resolution and must repaint its automation-presence LED row. */
    menu_voiceAutoOverlayBarChanged();
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
     * Completes the common long-press threshold for one sequencer step.
     *
     * Caller context: buttonHandler_tick() promotes a held step button after
     * BUTTON_TIMEOUT. The ISR only records button events; this foreground path
     * is where the VOICE overlay or legacy STEP automation state is notified.
     *
     * Why it lives here: the shared gesture timing belongs to the button layer,
     * while Menu owns VOICE overlay state and PatternData owns the legacy STEP
     * automation destination. Keeping the branch here preserves one timer and
     * one release-suppression path for both modes.
     *
     * Inputs: stepNr is a 0..127 absolute bridge step index. The visible bar is
     * already folded into that value, so the blink target is STEP1..16 at
     * stepNr % NUM_STEPS_PER_BAR.
     *
     * Outputs: VOICE mode calls menu_voiceAutoOverlayHoldExpired() and leaves
     * release suppression to the existing timer sentinel. Legacy STEP mode
     * records buttonHandler_armedAutomationStep and enables its blink target.
     *
     * Risk: recordAutomation is deliberately hard-coded to 1 to match the old
     * ARM_AUTOMATION_STEP opcode behavior. If automation arming becomes
     * per-pattern/per-track later, PatternData should absorb that policy.
     */
    if (bh_state.selectButtonMode == SELECT_MODE_VOICE) {
        /*
         * VOICE hold threshold crossed: transfer gesture ownership to Menu.
         * Menu reads the raw held mask in its next foreground service pass;
         * this call only marks the overlay active and never touches LCD or
         * PatternData. The timer sentinel then suppresses the matching release.
         */
        menu_voiceAutoOverlayHoldExpired();
        return;
    }

    buttonHandler_armedAutomationStep = stepNr;
    led_setBlinkLed((uint8_t)(LED_STEP1 + ((uint8_t)stepNr % NUM_STEPS_PER_BAR)), 1);

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
    /*
     * Foreground long-press poll using the wrapping 16-bit millisecond clock.
     *
     * What: checks whether the hold timer deadline has elapsed, then verifies
     * that the initiating button is still physically held before promoting the
     * gesture to a long-press action.
     *
     * Why the held-check: without it, a quick tap whose release event is
     * delayed behind other events in the ring can have its timer expire while
     * the button is already physically released. The timer would then set
     * TIMER_ACTION_OCCURED, causing the delayed release to be consumed as a
     * completed hold instead of toggling the step.
     *
     * Reverse mapping: buttonHandler_buttonTimerStepNr stores the absolute
     * step index (0..127) set by buttonHandler_setTimeraction() via
     * buttonHandler_visibleStep(). The physical button is at
     * seq_buttons[stepNr % NUM_STEPS_PER_BAR].
     *
     * Deadline comparison: unsigned elapsed time below half the counter range
     * remains correct across the time_sysTick wrap.
     *
     * Inputs: timer step/deadline, btn_held[], and seq_buttons[]. Outputs:
     * fires buttonHandler_armTimerActionStep() and sets TIMER_ACTION_OCCURED,
     * or cancels the timer by resetting to NO_STEP_SELECTED so the later
     * release is handled as a normal tap.
     * Affiliates: buttonHandler_setTimeraction() arms the timer;
     * buttonHandler_seqButtonReleased() checks TIMER_ACTION_OCCURED.
     */
    if (buttonHandler_buttonTimerStepNr >= 0 &&
        (uint16_t)(time_sysTick - buttonHandler_buttonTimer) < 32768u) {
        uint8_t physBtn = seq_buttons[
            (uint8_t)buttonHandler_buttonTimerStepNr % NUM_STEPS_PER_BAR];
        if (!btn_held[physBtn]) {
            buttonHandler_buttonTimerStepNr = NO_STEP_SELECTED;
            return;
        }
        buttonHandler_armTimerActionStep(buttonHandler_buttonTimerStepNr);
        buttonHandler_buttonTimerStepNr = TIMER_ACTION_OCCURED;
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

    menu_showStepEditPage();
    buttonHandler_updateSubSteps();
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

    trackNr = menu_getActiveVoice();
    patternNr = menu_getViewedPattern();
#if DEV_MODE_LOGGING
    /*
     * Diagnostic witness: prove that input delivery reached the Pattern
     * mutation call.
     *
     * What: emits a trace record immediately before pat_toggleStep(),
     * capturing the track, absolute step, pattern number, and trigger-bit
     * state before the XOR.
     *
     * Why: future reports of "step did not toggle" can distinguish input
     * delivery failure (no K record) from Pattern mutation failure (K present
     * but the trigger state unchanged) without relying on LED appearance.
     * This reads only the fixed-size address array; it performs no dynamic
     * stack access, allocation, or service-queue operation.
     *
     * Value32 layout is defined by AUTOSAVE_TRACE_STEP_TOGGLE_* in
     * AutosaveTrace.h. Flags are reserved and remain zero.
     */
    autosaveTrace_record(
        AUTOSAVE_TRACE_STAGE_STEP_TOGGLE,
        0u,
        ((uint32_t)trackNr << AUTOSAVE_TRACE_STEP_TOGGLE_TRACK_SHIFT)
        | ((uint32_t)seqButtonPressed << AUTOSAVE_TRACE_STEP_TOGGLE_STEP_SHIFT)
        | ((uint32_t)patternNr << AUTOSAVE_TRACE_STEP_TOGGLE_PATTERN_SHIFT)
        | ((uint32_t)pat_isStepActive(trackNr, seqButtonPressed, patternNr)
           << AUTOSAVE_TRACE_STEP_TOGGLE_TRIGGER_SHIFT));
#endif
    pat_toggleStep(trackNr, seqButtonPressed, patternNr);
    led_setValue(pat_isStepActive(trackNr, seqButtonPressed, patternNr),
                 ledNr);
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
            break;
        default:
            break;
        }
    } else {
        switch (bh_state.selectButtonMode) {
        case SELECT_MODE_VOICE:
            if (menu_voiceAutoOverlayActive()) {
                /* An overlay-owned press is not a new tap/hold timer. Reuse
                 * the existing timer sentinel so its release is consumed. */
                buttonHandler_buttonTimerStepNr = TIMER_ACTION_OCCURED;
            } else {
                buttonHandler_setTimeraction(
                    buttonHandler_visibleStep(seqButtonPressed));
            }
            break;
        case SELECT_MODE_STEP:
            led_clearAllBlinkLeds();
            buttonHandler_selectActiveStep(ledNr, seqButtonPressed);
            break;
        case SELECT_MODE_PERF:
            menu_perfModeSceneButtonPressed(seqButtonPressed);
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
        if (menu_voiceAutoOverlayActive()) {
            /* Overlay-owned release: Menu's raw-mask service removes it. */
            buttonHandler_buttonTimerStepNr = TIMER_ACTION_OCCURED;
            return;
        }
        if (buttonHandler_TimerActionOccured())
            return;
        buttonHandler_setRemoveStep(ledNr, seqButtonPressed);
        break;

    case SELECT_MODE_PERF:
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
         * Exit nested Instrument Load/Save mode.
         *
         * Inputs: Load/Save mode button while Menu is already browsing or
         * exporting instruments. Output: the normal Load/Save page returns and
         * voice blink feedback is cleared. This must run before the generic
         * mode switch so a second Load/Save press does not simply re-enter the
         * same submode.
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
         * Euklid data now lives with Pattern under Core/Bank/Scene/Pattern.
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
         * Load/Save voice buttons select nested Instrument slots.
         *
         * Inputs: pressed voice button while Menu owns Load or Save. Output:
         * Load enters/updates Instrument Load destination mode; Save
         * enters/updates root Instrument Save source mode. In both cases the
         * active voice LED follows the selected slot and blinks until the user
         * exits the nested Instrument surface. Only VOICE blink state is
         * cleared here: clearing all blink LEDs would erase Menu's active-Scene
         * SEQ feedback immediately after it was painted. Normal voice
         * selection, mute, and page switching are skipped for this press.
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
        if (buttonHandler_voiceSceneMaskHoldActive &&
            menu_voiceHeldSceneButtonPressed((uint8_t)seq)) {
            buttonHandler_voiceSceneSeqPressedMask = (uint16_t)(
                buttonHandler_voiceSceneSeqPressedMask |
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
        if (buttonNr == BUT_MODE1 &&
            bh_state.selectButtonMode == SELECT_MODE_VOICE) {
            /*
             * MODE VOICE hold overlays the Scene edit-mask on the SEQ row.
             *
             * Inputs: the ISR held[] state has already marked MODE1 held, and
             * handleModeButtons() has ensured VOICE mode is current. Output:
             * Menu paints scene_mask_voice_edit immediately, before any SEQ
             * toggle, so the user can see which Scenes will receive voice/Scene
             * parameter fan-out while holding MODE VOICE.
             *
             * buttonHandler_voiceSceneMaskHoldActive is deliberately separate
             * from btn_held[BUT_MODE1]. It follows foreground event order, so a
             * queued SEQ press cannot become a Pattern step just because the ISR
             * has already seen the later MODE1 release.
             */
            buttonHandler_voiceSceneMaskHoldActive = 1u;
            menu_refreshVoiceHeldSceneLeds();
        }
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
        /*
         * Load/Save name editing borrows BAR1/BAR2 as text helpers.
         *
         * Menu owns that context because it knows whether a character cell is
         * selected and which buffer is active. If Menu consumes the press,
         * normal bar navigation is skipped; otherwise BAR buttons keep their
         * sequencer bar-selection behavior.
         */
        if (menu_loadSaveBarButtonPressed(0u))
            break;
        if (menu_currentBar > 0u)
            buttonHandler_selectBar((uint8_t)(menu_currentBar - 1u));
        else
            led_flashGroup(LED_FLASH_GROUP_SELECT, 0x0001u);
        break;

    case BUT_BAR2:
        led_setValue(1, LED_BAR2);
        /*
         * See BAR1 above. BAR2 is insert-space-forward in Load/Save character
         * entry and ordinary next-bar selection everywhere else.
         */
        if (menu_loadSaveBarButtonPressed(1u))
            break;
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
        if ((buttonHandler_voiceSceneSeqPressedMask & bit) != 0u) {
            /*
             * Suppress the release half of a MODE VOICE Scene-mask SEQ press.
             *
             * The press already toggled/consumed the Scene edit-mask overlay.
             * Returning here prevents buttonHandler_seqButtonReleased() from
             * interpreting the same physical button as a VOICE-mode step tap.
             */
            buttonHandler_voiceSceneSeqPressedMask = (uint16_t)(
                buttonHandler_voiceSceneSeqPressedMask & (uint16_t)(~bit));
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
    case BUT_MODE1:
        if (bh_state.selectButtonMode == SELECT_MODE_VOICE) {
            /*
             * Release the temporary MODE VOICE Scene edit-mask overlay.
             *
             * Output: the SEQ row returns to VOICE-mode neutral state and any
             * MODE1 morph blink is restored. This pairs with the press-side
             * menu_refreshVoiceHeldSceneLeds() call so the edit-mask LEDs do
             * not linger after the hold gesture ends. The pattern-track repaint
             * is needed because VOICE mode normally owns the SEQ row as a step
             * view; clearing the overlay without redrawing would leave the row
             * blank until some unrelated view happened to refresh it.
             */
            buttonHandler_voiceSceneMaskHoldActive = 0u;
            led_clearSequencerLeds();
            led_clearAllBlinkLeds();
            led_updatePatternTrackView(menu_getActiveVoice(),
                                       menu_getViewedPattern(),
                                       buttonHandler_selectedStep,
                                       0u);
            led_setActiveVoice(menu_getActiveVoice());
            led_setActiveSelectButton(menu_getSubPage());
            if (buttonHandler_morphVoiceModeActive)
                led_setBlinkLed(LED_MODE1, 1u);
        }
        break;

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
** What: drains ONE event per call from the monotonic-counter ring, then
** returns. Two call sites in the main loop (separated by
** audio_check_and_render) provide roughly 30 events per 500 Hz scan interval,
** above the 41-button hardware maximum.
**
** Why one per call: the original AVR cadence of one button per main-loop
** iteration keeps per-call CPU bounded and preserves the audio interleaving
** contract. The button handler must never call audio_check_and_render() or
** acquire an audio dependency itself.
**
** Overflow reconciliation: when evt_overflow_flag is set by evt_push(), this
** function clears both pairing masks and resets the hold timer to
** NO_STEP_SELECTED. This prevents stale overlay bits from suppressing the next
** tap and prevents an orphaned timer from promoting a button whose release
** was lost. The event that overflowed is still dropped; no user-facing error
** path is added.
** ----------------------------------------------------------------------- */
void buttonHandler_processEvents(void)
{
    if (evt_overflow_flag) {
        evt_overflow_flag = 0;
        buttonHandler_voiceSceneSeqPressedMask = 0u;
        buttonHandler_loadSceneSeqPressedMask = 0u;
        buttonHandler_buttonTimerStepNr = NO_STEP_SELECTED;
#if DEV_MODE_LOGGING
        {
            uint8_t depth = (uint8_t)(evt_producer - evt_consumer);
            uint8_t drops = evt_drop_count;
            evt_drop_count = 0;
            autosaveTrace_record(
                AUTOSAVE_TRACE_STAGE_EVT_OVERFLOW,
                drops,
                (uint32_t)depth);
        }
#endif
    }

    if (evt_consumer != evt_producer) {
        uint8_t ev = evt_ring[evt_consumer & EVT_RING_MASK];
        evt_consumer++;

        uint8_t pressed  = (uint8_t)((ev & EVT_PRESSED) != 0);
        uint8_t buttonNr = (uint8_t)(ev & (uint8_t)~EVT_PRESSED);

        if (pressed)
            processPress(buttonNr);
        else
            processRelease(buttonNr);
    }
}
