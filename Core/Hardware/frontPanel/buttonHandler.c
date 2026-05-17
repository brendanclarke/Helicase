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
#include "frontPanelParser.h"
#include "copyClearTools.h"
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

static uint8_t lastActivePage = 0;
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

#define ARM_AUTOMATION     0x40
#define DISARM_AUTOMATION  0x00

/* -----------------------------------------------------------------------
** Helpers
** ----------------------------------------------------------------------- */
uint8_t buttonHandler_getMode(void)  { return bh_state.selectButtonMode; }
uint8_t buttonHandler_getShift(void) { return (uint8_t)(btn_held[BUT_SHIFT]); }
int8_t buttonHandler_getArmedAutomationStep(void) { return buttonHandler_armedAutomationStep; }

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

static void buttonHandler_updateSubSteps(void)
{
    /* _SEQUENCER_ADD_SPIKE_: The AVR queried sequencer-owned step LEDs over UART.
    ** On STM we keep the same protocol call and route it through frontPanel_sendData()
    ** so the sequencer endpoint can be wired centrally in frontPanelParser.c. */
    led_clearSelectLeds();
    {
        uint8_t trackNr = menu_getActiveVoice();
        uint8_t patternNr = menu_getViewedPattern();
        uint8_t value = (uint8_t)((trackNr << 4) | (patternNr & 0x7u));
        frontPanel_sendData(LED_CC, LED_QUERY_SEQ_TRACK, value);
    }
}

static void buttonHandler_enterSeqModeStepMode(void)
{
    menu_switchSubPage(0);
    menu_switchPage(SEQ_PAGE);
    buttonHandler_updateSubSteps();
    led_setBlinkLed(selectedStepLed, 1);
}

static void buttonHandler_leaveSeqModeStepMode(void)
{
    led_setBlinkLed(selectedStepLed, 0);
    led_setValue(0, selectedStepLed);
}

static void buttonHandler_enterSeqMode(void)
{
    lastActivePage = menu_activePage;
    lastActiveSubPage = menu_getSubPage();
    menu_switchSubPage(0);
    menu_switchPage(SEQ_PAGE);
    buttonHandler_updateSubSteps();
    led_setBlinkLed(selectedStepLed, 1);
}

static void buttonHandler_leaveSeqMode(void)
{
    led_setBlinkLed(selectedStepLed, 0);
    led_setValue(0, selectedStepLed);
    led_clearSelectLeds();
    menu_switchPage(lastActivePage);
    menu_switchSubPage(lastActiveSubPage);
}

static void buttonHandler_armTimerActionStep(int8_t stepNr)
{
    uint8_t isMainStep = (uint8_t)((stepNr % 8) == 0);
    buttonHandler_armedAutomationStep = stepNr;

    if (isMainStep) {
        uint8_t mainStepNr = (uint8_t)(stepNr / 8);
        led_setBlinkLed((uint8_t)(LED_STEP1 + mainStepNr), 1);
    } else {
        uint8_t selectButtonNr = (uint8_t)(stepNr % 8);
        led_setBlinkLed((uint8_t)(LED_PART_SELECT1 + selectButtonNr), 1);
    }

    /* _SEQUENCER_ADD_SPIKE_: preserve AVR long-press automation arming message. */
    frontPanel_sendData(ARM_AUTOMATION_STEP, (uint8_t)stepNr,
                        (uint8_t)(menu_getActiveVoice() | ARM_AUTOMATION));
}

static void buttonHandler_disarmTimerActionStep(void)
{
    if (buttonHandler_armedAutomationStep != NO_STEP_SELECTED) {
        uint8_t isMainStep = (uint8_t)((buttonHandler_armedAutomationStep % 8) == 0);

        if (isMainStep) {
            uint8_t mainStepNr = (uint8_t)(buttonHandler_armedAutomationStep / 8);
            led_setBlinkLed((uint8_t)(LED_STEP1 + mainStepNr), 0);
        } else {
            uint8_t selectButtonNr = (uint8_t)(buttonHandler_armedAutomationStep % 8);
            led_setBlinkLed((uint8_t)(LED_PART_SELECT1 + selectButtonNr), 0);
        }

        if (buttonHandler_resetLock == 1) {
            parameter_values[buttonHandler_originalParameter] = buttonHandler_originalValue;
        }

        buttonHandler_armedAutomationStep = NO_STEP_SELECTED;
        /* _SEQUENCER_ADD_SPIKE_: keep AVR automation-disarm signal flow. */
        frontPanel_sendData(ARM_AUTOMATION_STEP, 0, DISARM_AUTOMATION);

        if (buttonHandler_resetLock == 1) {
            buttonHandler_resetLock = 0;
            if (buttonHandler_originalParameter < 128) {
                frontPanel_sendData(MIDI_CC, (uint8_t)buttonHandler_originalParameter,
                                    buttonHandler_originalValue);
            } else if (buttonHandler_originalParameter < END_OF_SOUND_PARAMETERS) {
                frontPanel_sendData(CC_2,
                                    (uint8_t)(buttonHandler_originalParameter - 128),
                                    buttonHandler_originalValue);
            } else {
                menu_parseGlobalParam(buttonHandler_originalParameter,
                                      parameter_values[buttonHandler_originalParameter]);
            }
            menu_repaintAll();
        }
        return;
    }

    buttonHandler_armedAutomationStep = NO_STEP_SELECTED;
    frontPanel_sendData(ARM_AUTOMATION_STEP, 0, DISARM_AUTOMATION);
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
    led_setBlinkLed(selectedStepLed, 0);
    led_setValue(0, selectedStepLed);

    buttonHandler_selectedStep = (uint8_t)(seqButtonPressed * 8u);
    selectedStepLed = ledNr;
    parameter_values[PAR_ACTIVE_STEP] = buttonHandler_selectedStep;

    led_setBlinkLed(ledNr, 1);

    /* _SEQUENCER_ADD_SPIKE_: request active-step params from sequencer side. */
    frontPanel_sendData(SEQ_CC, SEQ_REQUEST_STEP_PARAMS,
                        (uint8_t)(seqButtonPressed * 8u));
    buttonHandler_updateSubSteps();
}

static void buttonHandler_toggleStepParameterPage(void)
{
    if (bh_state.selectButtonMode == SELECT_MODE_STEP &&
        menu_activePage == SEQ_PAGE) {
        menu_switchSubPage(menu_getSubPage());
        menu_repaintAll();
    }
}

static void buttonHandler_setRemoveStep(uint8_t ledNr, uint8_t seqButtonPressed)
{
    uint8_t trackNr;
    uint8_t patternNr;
    uint8_t value;

    led_setValue(0, ledNr);
    seqButtonPressed = (uint8_t)(seqButtonPressed * 8u);

    buttonHandler_selectedStep = seqButtonPressed;
    parameter_values[PAR_ACTIVE_STEP] = buttonHandler_selectedStep;
    selectedStepLed = ledNr;

    /* _SEQUENCER_ADD_SPIKE_: AVR parity for main-step toggle + step-parameter refresh. */
    frontPanel_sendData(SEQ_CC, SEQ_REQUEST_STEP_PARAMS, seqButtonPressed);

    trackNr = menu_getActiveVoice();
    patternNr = menu_getViewedPattern();
    value = (uint8_t)((trackNr << 4) | (patternNr & 0x7u));
    frontPanel_sendData(MAIN_STEP_CC, value, (uint8_t)(seqButtonPressed / 8u));
}

static void buttonHandler_setTrackRotation(uint8_t seqButtonPressed)
{
    parameter_values[PAR_TRACK_ROTATION] = seqButtonPressed;
    /* _SEQUENCER_ADD_SPIKE_: track-rotation write forwarded to protocol endpoint. */
    frontPanel_sendData(SEQ_CC, SEQ_TRACK_ROTATION, seqButtonPressed);
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
            buttonHandler_toggleStepParameterPage();
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
            buttonHandler_setTimeraction((uint8_t)(seqButtonPressed * 8u));
            break;
        case SELECT_MODE_STEP:
            led_clearAllBlinkLeds();
            buttonHandler_selectActiveStep(ledNr, seqButtonPressed);
            led_setBlinkLed(LED_PART_SELECT1, 1);
            buttonHandler_toggleStepParameterPage();
            break;
        case SELECT_MODE_PERF:
            if (seqButtonPressed < 8u) {
                /* _SEQUENCER_ADD_SPIKE_: manual roll on while held in perf mode. */
                frontPanel_sendData(SEQ_CC, SEQ_ROLL_ON_OFF,
                                    (uint8_t)((seqButtonPressed & 0x0fu) + 0x10u));
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
            /* _SEQUENCER_ADD_SPIKE_: manual roll off on release. */
            frontPanel_sendData(SEQ_CC, SEQ_ROLL_ON_OFF,
                                (uint8_t)(seqButtonPressed & 0x0fu));
            led_setValue(0, ledNr);
        }
        break;

    default:
        break;
    }
}

static void handleModeButtons(uint8_t mode)
{
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
        /* _SEQUENCER_ADD_SPIKE_: request per-track euclid state before entering page. */
        frontPanel_sendData(SEQ_CC, SEQ_REQUEST_EUKLID_PARAMS, menu_getActiveVoice());
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
        {
            uint8_t stepNr = (uint8_t)(buttonHandler_selectedStep + selectNr);
            uint8_t ledNr = (uint8_t)(LED_PART_SELECT1 + selectNr);
            uint8_t trackNr;
            uint8_t patternNr;
            uint8_t value;

            /* _SEQUENCER_ADD_SPIKE_: restore shift+select sub-step toggle flow. */
            led_toggle(ledNr);
            trackNr = menu_getActiveVoice();
            patternNr = menu_getViewedPattern();
            value = (uint8_t)((trackNr << 4) | (patternNr & 0x7u));
            frontPanel_sendData(STEP_CC, value, stepNr);
            frontPanel_sendData(SEQ_CC, SEQ_REQUEST_STEP_PARAMS, stepNr);
            parameter_values[PAR_ACTIVE_STEP] = stepNr;
            buttonHandler_toggleStepParameterPage();
            break;
        }

        case SELECT_MODE_PAT_GEN:
        case SELECT_MODE_PERF:
        {
            uint8_t trackNr;
            uint8_t patternNr;
            uint8_t value;

            /* _SEQUENCER_ADD_SPIKE_: restore shift+select pattern-view select + query. */
            menu_setShownPattern(selectNr);
            led_clearSelectLeds();
            led_clearAllBlinkLeds();
            led_setBlinkLed((uint8_t)(LED_PART_SELECT1 + selectNr), 1);

            trackNr = menu_getActiveVoice();
            patternNr = menu_getViewedPattern();
            value = (uint8_t)((trackNr << 4) | (patternNr & 0x7u));
            frontPanel_sendData(LED_CC, LED_QUERY_SEQ_TRACK, value);
            frontPanel_sendData(SEQ_CC, SEQ_REQUEST_PATTERN_PARAMS, patternNr);
            frontPanel_sendData(SEQ_CC, SEQ_REQUEST_EUKLID_PARAMS, menu_getActiveVoice());
            break;
        }

        default:
            break;
        }
        return;
    }

    switch (bh_state.selectButtonMode) {
    case SELECT_MODE_STEP:
    {
        uint8_t stepNr = (uint8_t)(buttonHandler_selectedStep + selectNr);
        uint8_t selectButtonNr;

        /* _SEQUENCER_ADD_SPIKE_: restore non-shift sub-step selection requests. */
        frontPanel_sendData(SEQ_CC, SEQ_REQUEST_STEP_PARAMS, stepNr);
        parameter_values[PAR_ACTIVE_STEP] = stepNr;

        led_clearAllBlinkLeds();
        led_setBlinkLed((uint8_t)(LED_STEP1 + (stepNr / 8u)), 1);
        selectButtonNr = (uint8_t)(stepNr % 8u);
        led_setBlinkLed((uint8_t)(LED_PART_SELECT1 + selectButtonNr), 1);
        buttonHandler_toggleStepParameterPage();
        break;
    }

    case SELECT_MODE_VOICE:
        menu_switchSubPage(selectNr);
        menu_resetActiveParameter();
        led_setActiveSelectButton(selectNr);
        menu_repaintAll();
        break;

    case SELECT_MODE_PERF:
        if (menu_getActivePage() == PERFORMANCE_PAGE) {
            /* _SEQUENCER_ADD_SPIKE_: restore unshifted perf pattern-change trigger. */
            frontPanel_sendData(SEQ_CC, SEQ_CHANGE_PAT, selectNr);
            led_clearAllBlinkLeds();
            led_setBlinkLed((uint8_t)(LED_PART_SELECT1 + selectNr), 1);
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
            uint8_t value;

            copyClear_setDst((int8_t)partNr, MODE_COPY_PATTERN);
            copyClear_copyPattern();
            led_clearAllBlinkLeds();

            trackNr = menu_getActiveVoice();
            patternNr = menu_getViewedPattern();
            value = (uint8_t)((trackNr << 4) | (patternNr & 0x7u));
            /* _SEQUENCER_ADD_SPIKE_: keep sequence LED refresh request after copy. */
            frontPanel_sendData(LED_CC, LED_QUERY_SEQ_TRACK, value);
        } else {
            copyClear_setSrc((int8_t)partNr, MODE_COPY_PATTERN);
            led_setBlinkLed((uint8_t)(LED_PART_SELECT1 + partNr), 1);
        }
    } else {
        if (bh_state.selectButtonMode == SELECT_MODE_VOICE && buttonHandler_getShift()) {
            buttonHandler_setTimeraction((uint8_t)(buttonHandler_selectedStep + partNr));
        } else {
            handleSelectButton(partNr);
        }
    }
}

static void buttonHandler_partButtonReleased(uint8_t partNr)
{
    if (copyClear_Mode >= MODE_COPY_PATTERN) {
        return;
    }

    if (buttonHandler_TimerActionOccured())
        return;

    buttonHandler_buttonTimerStepNr = NO_STEP_SELECTED;
    if (buttonHandler_getShift() && bh_state.selectButtonMode == SELECT_MODE_VOICE)
        handleSelectButton(partNr);
}

static void handleVoiceButton(uint8_t voiceNr)
{
    if (copyClear_Mode >= MODE_COPY_PATTERN) {
        if (copyClear_srcSet()) {
            uint8_t trackNr;
            uint8_t patternNr;
            uint8_t value;

            copyClear_setDst((int8_t)voiceNr, MODE_COPY_TRACK);
            copyClear_copyTrack();
            led_clearAllBlinkLeds();

            trackNr = menu_getActiveVoice();
            patternNr = menu_getViewedPattern();
            value = (uint8_t)((trackNr << 4) | (patternNr & 0x7u));
            /* _SEQUENCER_ADD_SPIKE_: keep LED-query behavior after track copy. */
            frontPanel_sendData(LED_CC, LED_QUERY_SEQ_TRACK, value);
        } else {
            copyClear_setSrc((int8_t)voiceNr, MODE_COPY_TRACK);
            led_setBlinkLed((uint8_t)(LED_VOICE1 + voiceNr), 1);
        }
        return;
    }

    {
        uint8_t muteModeActive = buttonHandler_getShift();
        if (bh_state.selectButtonMode == SELECT_MODE_PERF)
            muteModeActive = (uint8_t)(1u - muteModeActive);

        if (muteModeActive) {
            /* _SEQUENCER_ADD_SPIKE_: restore per-track mute/unmute dispatch. */
            if (buttonHandler_mutedVoices & (1u << voiceNr)) {
                buttonHandler_muteVoice(voiceNr, 0);
                frontPanel_sendData(SEQ_CC, SEQ_UNMUTE_TRACK, voiceNr);
            } else {
                buttonHandler_muteVoice(voiceNr, 1);
                frontPanel_sendData(SEQ_CC, SEQ_MUTE_TRACK, voiceNr);
            }
            return;
        }

        if (bh_state.selectButtonMode == SELECT_MODE_PERF) {
            uint8_t i;
            for (i = 0; i <= voiceNr; i++) {
                if (buttonHandler_mutedVoices & (1u << i)) {
                    frontPanel_sendData(SEQ_CC, SEQ_UNMUTE_TRACK, i);
                    buttonHandler_mutedVoices &= (uint8_t)~(1u << i);
                }
            }
            buttonHandler_showMuteLEDs();
            return;
        }

        led_setActiveVoice(voiceNr);
        if (bh_state.selectButtonMode == SELECT_MODE_VOICE) {
            menu_switchPage(voiceNr);
            led_setActiveSelectButton(menu_getSubPage());
        }

        /* _SEQUENCER_ADD_SPIKE_: sequencer-facing active-track/euclid requests. */
        frontPanel_sendData(SEQ_CC, SEQ_SET_ACTIVE_TRACK, voiceNr);
        menu_setActiveVoice(voiceNr);
        frontPanel_sendData(SEQ_CC, SEQ_REQUEST_EUKLID_PARAMS, voiceNr);

        if (bh_state.selectButtonMode == SELECT_MODE_STEP) {
            led_clearAllBlinkLeds();
            buttonHandler_enterSeqModeStepMode();
        }
    }
}

/* Process one press event */
static void processPress(uint8_t buttonNr)
{
    int8_t seq = btn_to_seq(buttonNr);
    if (seq >= 0) {
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
        buttonHandler_setRunStopState((uint8_t)(1u - bh_state.seqRunning));
        /* _SEQUENCER_ADD_SPIKE_: restore sequencer run/stop command. */
        frontPanel_sendData(SEQ_CC, SEQ_RUN_STOP, (uint8_t)bh_state.seqRunning);
        break;

    case BUT_REC:
        if (buttonHandler_getShift()) {
            menu_switchPage(RECORDING_PAGE);
        } else {
            /* _SEQUENCER_ADD_SPIKE_: restore recording toggle + sequencer update. */
            bh_state.seqRecording = (uint8_t)((1u - bh_state.seqRecording) & 0x01u);
            led_setValue((uint8_t)bh_state.seqRecording, LED_REC);
            frontPanel_sendData(SEQ_CC, SEQ_REC_ON_OFF, (uint8_t)bh_state.seqRecording);
        }
        break;

    case BUT_COPY:
        if (buttonHandler_getShift()) {
            /* _SEQUENCER_ADD_SPIKE_: restore realtime erase / clear-mode flow from AVR. */
            if (bh_state.seqRecording && bh_state.seqRunning) {
                bh_state.seqErasing = 1;
                frontPanel_sendData(SEQ_CC, SEQ_ERASE_ON_OFF, (uint8_t)bh_state.seqErasing);
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
    {
        uint8_t selectedVoice = menu_getActiveVoice();
        midiParser_playVoiceMidiNote(selectedVoice, 127);
        break;
    }

    case BUT_BAR2:
    {
        uint8_t selectedVoice = menu_getActiveVoice();
        midiParser_playVoiceMidiNote(selectedVoice, 64);
        break;
    }

    case BUT_SHIFT:
        /* _SEQUENCER_ADD_SPIKE_: restore SHIFT-press mode behavior parity with AVR. */
        led_setValue(1, LED_SHIFT);
        switch (bh_state.selectButtonMode) {
        case SELECT_MODE_VOICE:
            buttonHandler_enterSeqMode();
            break;

        case SELECT_MODE_PERF:
        case SELECT_MODE_PAT_GEN:
        {
            uint8_t trackNr;
            uint8_t patternNr;
            uint8_t value;

            menu_switchPage(PATTERN_SETTINGS_PAGE);
            led_clearSelectLeds();
            led_clearAllBlinkLeds();

            if (bh_state.selectButtonMode == SELECT_MODE_PAT_GEN) {
                led_setBlinkLed(LED_MODE2, 1);
            } else {
                led_setBlinkLed((uint8_t)(LED_STEP1 + parameter_values[PAR_TRACK_ROTATION]), 1);
            }

            if (bh_state.selectButtonMode == SELECT_MODE_PAT_GEN && parameter_values[PAR_FOLLOW]) {
                menu_setShownPattern(menu_shownPattern);
                led_clearSequencerLeds();
                trackNr = menu_getActiveVoice();
                patternNr = menu_getViewedPattern();
                value = (uint8_t)((trackNr << 4) | (patternNr & 0x7u));
                frontPanel_sendData(LED_CC, LED_QUERY_SEQ_TRACK, value);
                frontPanel_sendData(SEQ_CC, SEQ_REQUEST_PATTERN_PARAMS, frontParser_midiMsg.data2);
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
    case BUT_COPY:
        /* _SEQUENCER_ADD_SPIKE_: restore erase exit + copy-mode reset on release. */
        if (bh_state.seqErasing) {
            bh_state.seqErasing = 0;
            frontPanel_sendData(SEQ_CC, SEQ_ERASE_ON_OFF, (uint8_t)bh_state.seqErasing);
        } else if (!buttonHandler_getShift()) {
            copyClear_reset();
        }
        break;

    case BUT_SHIFT:
        /* _SEQUENCER_ADD_SPIKE_: restore shift-release unwind flow from AVR. */
        if (bh_state.seqErasing) {
            bh_state.seqErasing = 0;
            frontPanel_sendData(SEQ_CC, SEQ_ERASE_ON_OFF, (uint8_t)bh_state.seqErasing);
        }

        if (copyClear_Mode == MODE_CLEAR) {
            copyClear_armClearMenu(0);
            copyClear_Mode = MODE_NONE;
        }

        led_setValue(0, LED_SHIFT);

        switch (bh_state.selectButtonMode) {
        case SELECT_MODE_VOICE:
            buttonHandler_leaveSeqMode();
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
