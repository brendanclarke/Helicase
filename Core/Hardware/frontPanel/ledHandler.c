/*
 * Core/Hardware/frontPanel/ledHandler.c
 *
 *  Created on: 17.05.2026
 * ------------------------------------------------------------------------------------------------------------------------
 *  Copyright 2026 Brendan Clarke
 *  brendanpaulclarke@gmail.com
 *  https://www.brendanclarke.com
 * ------------------------------------------------------------------------------------------------------------------------
 *  This file is part of the LXR02 Open-Source software.
 * ------------------------------------------------------------------------------------------------------------------------
 *  Redistribution and use of the LXR02 Open-Source, hardware driver code, or any derivative works are permitted
 *  provided that the following conditions are met:
 *
 *       - The code may not be sold, nor may it be used in a commercial product or activity.
 *
 *       - Redistributions that are modified from the original source must include the complete
 *         source code, including the source code for all components used by a binary built
 *         from the modified sources. However, as a special exception, the source code distributed
 *         need not include anything that is normally distributed (in either source or binary form)
 *         with the major components (compiler, kernel, and so on) of the operating system on which
 *         the executable runs, unless that component itself accompanies the executable.
 *
 *       - Redistributions must reproduce the above copyright notice, this list of conditions and the
 *         following disclaimer in the documentation and/or other materials provided with the distribution.
 * ------------------------------------------------------------------------------------------------------------------------
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 *   INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *   DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 *   SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 *   WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 *   USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * ------------------------------------------------------------------------------------------------------------------------
 */

/*
 * ledHandler.c — LXR-02 LED handler.
 * Ports original LXR ledHandler.c by Julian Schmidt.
 */
#include "ledHandler.h"
#include "timebase.h"
#include "dout.h"
#include "menu.h"
#include "buttonHandler.h"
#include "PatternData.h"
#include <string.h>

#define NUM_OF_PULSABLE_LEDS   8
#define LED_PULSE_TIME_MS      50
#define NUM_OF_BLINKABLE_LEDS  8
#define LED_BLINK_TIME_MS      250

static volatile uint16_t led_pulseEndTime[NUM_OF_PULSABLE_LEDS];
static volatile uint8_t  led_pulseLedNumber[NUM_OF_PULSABLE_LEDS];
static volatile uint8_t  led_pulsingLeds;
static volatile uint8_t  led_currentStepLed = 0xFFu;
static uint8_t           led_sw43State;
static uint8_t           led_sw43OriginalState;

static uint16_t led_nextBlinkTime;
static volatile uint8_t led_blinkLedNumber[NUM_OF_BLINKABLE_LEDS];
static volatile uint8_t led_blinkingLeds;

uint8_t led_originalLedState[NUM_OUTS / 8];  /* extern for ledHandler.c internal use */
SeqLedState seq_ledState = {0};

/* SR mapping: physical button index → SR chain bit */
static const uint8_t btn_to_sr[NUM_OUTS] = {
    39,38,37,36,35,34,33,32,
    31,30,29,28,27,26,25,24,
    23,22,21,20,19,18,17,16,
    15,14,13,12,11,10, 9, 8,
     7, 6, 5, 4, 3, 2, 1, 0,
};

/*
 * Canonical public IDs are logical AVR IDs; map them to current physical
 * chain button indices (0..39). LED_BAR1 is the dedicated SW43 GPIO LED.
 */
static const uint8_t led_logicalToPhysical[NUM_OUTS] = {
    /* mode/transport row */
    31, 30, 29, 28, 39, 37, 38, 7,
    /* voice row */
    6, 5, 4, 3, 2, 1, 0, 36,
    /* part-select row */
    23, 22, 21, 20, 15, 14, 13, 12,
    /* step row */
    32, 33, 34, 35, 24, 25, 26, 27, 16, 17, 18, 19, 8, 9, 10, 11
};

static uint8_t led_toPhysicalNumber(uint8_t ledNr)
{
    if (ledNr < NUM_OUTS) return led_logicalToPhysical[ledNr];
    if (ledNr == LED_BAR1) return LED_BAR1;
    return 0xFFu;
}

static uint8_t led_arrayPos(uint8_t ledNr) { return (uint8_t)(4 - btn_to_sr[ledNr]/8); }
static uint8_t led_bitPos(uint8_t ledNr)   { return btn_to_sr[ledNr] % 8; }

static void led_resetToOriginal(uint8_t ledNr)
{
    uint8_t ap = led_arrayPos(ledNr), bp = led_bitPos(ledNr);
    if (led_originalLedState[ap] & (1<<bp))
        dout_outputData[ap] |=  (uint8_t)(1<<bp);
    else
        dout_outputData[ap] &= (uint8_t)~(1<<bp);
}

void led_init(void)
{
    led_pulsingLeds = 0; led_blinkingLeds = 0;
    led_currentStepLed = 0xFFu;
    led_sw43State = 0;
    led_sw43OriginalState = 0;
    memset(led_originalLedState, 0, sizeof(led_originalLedState));
}

void led_setValue(uint8_t val, uint8_t ledNr)
{
    uint8_t physLed = led_toPhysicalNumber(ledNr);
    if (physLed == LED_BAR1) {
        led_sw43State = (uint8_t)(val ? 1u : 0u);
        led_sw43OriginalState = led_sw43State;
        dout_setSw43Led(led_sw43State);
        return;
    }
    if (physLed >= NUM_OUTS) return;

    uint8_t ap = led_arrayPos(physLed), bp = led_bitPos(physLed);
    if (val) { dout_outputData[ap] |=  (uint8_t)(1<<bp); led_originalLedState[ap] |=  (uint8_t)(1<<bp); }
    else      { dout_outputData[ap] &= (uint8_t)~(1<<bp); led_originalLedState[ap] &= (uint8_t)~(1<<bp); }
}

void led_setValueTemp(uint8_t val, uint8_t ledNr)
{
    uint8_t physLed = led_toPhysicalNumber(ledNr);
    if (physLed == LED_BAR1) {
        led_sw43State = (uint8_t)(val ? 1u : 0u);
        dout_setSw43Led(led_sw43State);
        return;
    }
    if (physLed >= NUM_OUTS) return;

    uint8_t ap = led_arrayPos(physLed), bp = led_bitPos(physLed);
    if (val) dout_outputData[ap] |=  (uint8_t)(1<<bp);
    else     dout_outputData[ap] &= (uint8_t)~(1<<bp);
    /* does not update led_originalLedState */
}

void led_reset(uint8_t ledNr)
{
    uint8_t physLed = led_toPhysicalNumber(ledNr);
    if (physLed == LED_BAR1) {
        led_sw43State = led_sw43OriginalState;
        dout_setSw43Led(led_sw43State);
        return;
    }
    if (physLed < NUM_OUTS) led_resetToOriginal(physLed);
}

void led_toggle(uint8_t ledNr)
{
    uint8_t physLed = led_toPhysicalNumber(ledNr);
    if (physLed == LED_BAR1) {
        led_sw43State ^= 1u;
        led_sw43OriginalState = led_sw43State;
        dout_setSw43Led(led_sw43State);
        return;
    }
    if (physLed >= NUM_OUTS) return;

    uint8_t ap = led_arrayPos(physLed), bp = led_bitPos(physLed);
    dout_outputData[ap]      ^= (uint8_t)(1<<bp);
    led_originalLedState[ap] ^= (uint8_t)(1<<bp);
}

void led_toggleTemp(uint8_t ledNr)
{
    uint8_t physLed = led_toPhysicalNumber(ledNr);
    if (physLed == LED_BAR1) {
        led_sw43State ^= 1u;
        dout_setSw43Led(led_sw43State);
        return;
    }
    if (physLed >= NUM_OUTS) return;

    uint8_t ap = led_arrayPos(physLed), bp = led_bitPos(physLed);
    dout_outputData[ap] ^= (uint8_t)(1<<bp);
}

void led_clearAll(void)
{
    int i;
    for (i=0;i<NUM_OUTS/8;i++) { dout_outputData[i]=0; led_originalLedState[i]=0; }
    led_sw43State = 0;
    led_sw43OriginalState = 0;
    dout_setSw43Led(0);
}

void led_pulseLed(uint8_t ledNr)
{
    uint8_t physLed = led_toPhysicalNumber(ledNr);
    if ((physLed >= NUM_OUTS) && (physLed != LED_BAR1)) return;

    int i;
    for (i=0;i<NUM_OF_PULSABLE_LEDS;i++) {
        if (!(led_pulsingLeds & (1<<i))) {
            led_pulseLedNumber[i] = ledNr;
            led_pulsingLeds |= (uint8_t)(1<<i);
            led_pulseEndTime[i] = (uint16_t)(time_sysTick + LED_PULSE_TIME_MS);
            led_toggleTemp(ledNr);
            break;
        }
    }
}

void led_setBlinkLed(uint8_t ledNr, uint8_t onOff)
{
    uint8_t physLed = led_toPhysicalNumber(ledNr);
    if ((physLed >= NUM_OUTS) && (physLed != LED_BAR1)) return;

    int i;
    if (onOff) {
        for (i=0;i<NUM_OF_BLINKABLE_LEDS;i++) {
            if (!(led_blinkingLeds & (1<<i))) {
                led_blinkLedNumber[i] = ledNr; led_blinkingLeds |= (uint8_t)(1<<i);
                led_toggleTemp(ledNr);
                break;
            }
        }
    } else {
        for (i=0;i<NUM_OF_BLINKABLE_LEDS;i++) {
            if ((led_blinkingLeds & (1<<i)) && led_blinkLedNumber[i]==ledNr) {
                led_blinkingLeds &= (uint8_t)~(1<<i);
                led_reset(ledNr);
            }
        }
    }
}

void led_clearAllBlinkLeds(void)
{
    int i;
    for (i=0;i<NUM_OF_BLINKABLE_LEDS;i++) {
        if (led_blinkingLeds & (1<<i)) {
            led_reset(led_blinkLedNumber[i]);
            led_blinkingLeds &= (uint8_t)~(1<<i);
        }
    }
}

void led_tickHandler(void)
{
    int i;
    for (i=0;i<NUM_OF_PULSABLE_LEDS;i++) {
        if ((led_pulsingLeds & (1<<i)) && (time_sysTick > led_pulseEndTime[i])) {
            led_pulsingLeds &= (uint8_t)~(1<<i);
            led_reset(led_pulseLedNumber[i]);
        }
    }
    if ((time_sysTick > led_nextBlinkTime) || ((led_nextBlinkTime - time_sysTick) > LED_BLINK_TIME_MS*2)) {
        led_nextBlinkTime = (uint16_t)(time_sysTick + LED_BLINK_TIME_MS);
        for (i=0;i<NUM_OF_BLINKABLE_LEDS;i++) {
            if (led_blinkingLeds & (1<<i)) {
                led_toggleTemp(led_blinkLedNumber[i]);
            }
        }
    }
}

/* -----------------------------------------------------------------------
** High-level API — mirrors original
** ----------------------------------------------------------------------- */

void led_setActivePage(uint8_t pageNr)
{
    if (pageNr < 8u) {
        led_setActiveSelectButton(pageNr);
    }
}

/* Light exactly one voice LED (LED_VOICE1..LED_VOICE7), clear others.
** voiceNr is 0-based. */
void led_setActiveVoice(uint8_t voiceNr)
{
    int i;
    for (i=0;i<7;i++) led_setValue(0, (uint8_t)(LED_VOICE1 + i));
    if (voiceNr < 7u) {
        led_setValue(1, (uint8_t)(LED_VOICE1 + voiceNr));
    }
    menu_muteModeActive = 0;
}

void led_setActiveVoiceLeds(uint8_t pattern)
{
    /* pattern bit 0 = voice 1, bit 6 = voice 7 */
    int i;
    for (i=0;i<7;i++)
        led_setValue((uint8_t)((pattern >> i) & 1u), (uint8_t)(LED_VOICE1 + i));
}

/* Light one SELECT LED (0-7), clear others */
void led_setActiveSelectButton(uint8_t butNr)
{
    int i;
    for (i=0;i<8;i++) led_setValue(0, (uint8_t)(LED_PART_SELECT1 + i));
    if (butNr < 8u) led_setValue(1, (uint8_t)(LED_PART_SELECT1 + butNr));
}

void led_setMode2Leds(uint8_t value)
{
    uint8_t i;
    for (i = 0; i < 8; i++) {
        led_setValue((uint8_t)(((~value) >> i) & 1u), (uint8_t)(LED_PART_SELECT1 + i));
    }
}

/* Set MODE LEDs according to selectButtonMode (0-7) — exact port from original */
void led_setMode2(uint8_t status)
{
    led_setValue(0, LED_MODE1); led_setValue(0, LED_MODE2);
    led_setValue(0, LED_MODE3); led_setValue(0, LED_MODE4);
    led_setBlinkLed(LED_MODE1, 0); led_setBlinkLed(LED_MODE2, 0);
    led_setBlinkLed(LED_MODE3, 0); led_setBlinkLed(LED_MODE4, 0);

    switch (status) {
    case 0: led_setValue(1, LED_MODE1); break;
    case 1: led_setValue(1, LED_MODE2); break;
    case 2: led_setValue(1, LED_MODE3); break;
    case 3: led_setValue(1, LED_MODE4); break;
    case 4: led_setValue(1, LED_MODE4); break;
    case 5: led_setBlinkLed(LED_MODE2, 1); break;
    case 6: break;
    case 7: led_setBlinkLed(LED_MODE4, 1); break;  /* global/MENU page blinks MODE4 */
    default: break;
    }
}

void led_clearSequencerLeds(void)
{
    int i;
    for (i=0;i<16;i++) led_setValue(0, (uint8_t)(LED_STEP1 + i));
}

void led_clearSequencerLeds1_8(void)
{
    int i;
    for (i=0;i<8;i++) led_setValue(0, (uint8_t)(LED_STEP1 + i));
}

void led_clearSequencerLeds9_16(void)
{
    int i;
    for (i=0;i<8;i++) led_setValue(0, (uint8_t)(LED_STEP9 + i));
}

void led_clearSelectLeds(void)
{
    int i;
    for (i=0;i<8;i++) led_setValue(0, (uint8_t)(LED_PART_SELECT1 + i));
}

void led_clearVoiceLeds(void)
{
    int i;
    for (i=0;i<7;i++) led_setValue(0, (uint8_t)(LED_VOICE1 + i));
}

void led_setActive_step(uint8_t stepNr)
{
    uint8_t ledNr = (uint8_t)(LED_STEP1 + (stepNr / 8u));
    if (led_currentStepLed != ledNr) {
        if (led_currentStepLed != 0xFFu) {
            led_reset(led_currentStepLed);
        }
        led_currentStepLed = ledNr;
        led_toggleTemp(ledNr);
    }
}

void led_clearActive_step(void)
{
    if (led_currentStepLed != 0xFFu) {
        led_reset(led_currentStepLed);
        led_currentStepLed = 0xFFu;
    }
}

void led_initPerformanceLeds(void)
{
    uint8_t playedPattern = menu_playedPattern;
    uint8_t viewedPattern = menu_getViewedPattern();

    led_clearSelectLeds();

    if (playedPattern < 8u) {
        led_setValue(1, (uint8_t)(playedPattern + LED_PART_SELECT1));
    }
    if ((playedPattern != viewedPattern) && (viewedPattern < 8u)) {
        led_setBlinkLed((uint8_t)(LED_PART_SELECT1 + viewedPattern), 1);
    }
}

void led_updateCurrentStep(uint8_t step)
{
    /* Called by led_processSeqLedState() when sequencer.c marks
     * SEQ_LED_DIRTY_CHASE. The sequencer only knows the playback sub-step; this
     * LED owner decides whether that chase light should be visible on the
     * current page and pattern. */
    uint8_t shownPattern;
    uint8_t playedPattern;

    /* Defensive bound check because seq_ledState is cross-module state. A bad
     * value should fail silently instead of indexing LED state incorrectly. */
    if (!pat_stepValid(step))
        return;

    /* shownPattern is the pattern the UI is editing/viewing; playedPattern is
     * the pattern the sequencer is actually playing. The chase LED is only
     * meaningful when those match. */
    shownPattern = menu_getViewedPattern();
    playedPattern = menu_playedPattern;

    if (shownPattern == playedPattern) {
        /* Show chase on voice, performance, sequencer, and Euklid pages. Do not
         * show it on global/load/save-style pages where STEP LEDs mean other
         * UI choices. */
        if ((menu_activePage < MENU_MIDI_PAGE) ||
            (menu_activePage == PERFORMANCE_PAGE) ||
            (menu_activePage == SEQ_PAGE) ||
            (menu_activePage == EUKLID_PAGE)) {
            led_setActive_step(step);
        }
    } else {
        /* If follow mode is off and the user is viewing another pattern, clear
         * the temporary chase LED so it does not imply the viewed pattern is
         * currently playing this step. */
        led_clearActive_step();
    }
}

void led_updateRecordedMainStep(uint8_t activeTrack,
                                uint8_t shownPattern,
                                uint8_t subStep)
{
    /* Called after live recording or MIDI recording when the parent main step
     * may have changed. This belongs in ledHandler because it is strictly a
     * physical STEP1..16 presentation update; PatternData owns the pattern bit
     * that we query below. */
    uint8_t mainStep = (uint8_t)((subStep & 0x7fu) / 8u);
    uint8_t on = 0;

    /* Performance page STEP LEDs select patterns/rotation rather than edit
     * track main-step state, so recording feedback must not overwrite them. */
    if (menu_activePage == PERFORMANCE_PAGE)
        return;
    /* subStep is expected to be 0..127; the divide should therefore land in
     * 0..15. Keep the guard because seq_ledState is shared state. */
    if (mainStep >= 16u)
        return;

    /* Query the PatternData owner for the actual main-step bit. The LED helper
     * does not cache or mutate pattern data. */
    if (pat_trackValid(activeTrack) && pat_patternValid(shownPattern))
        on = pat_isMainStepActive(activeTrack, mainStep, shownPattern);

    /* STEP1 + mainStep is the physical 16-button row. */
    led_setValue(on, (uint8_t)(LED_STEP1 + mainStep));
}

void led_updateRecordedSubStep(uint8_t activeTrack,
                               uint8_t shownPattern,
                               uint8_t step,
                               uint8_t selectedStepBase,
                               uint8_t shiftHeld,
                               uint8_t selectMode)
{
    /* Called after live recording when the current SELECT1..8 sub-step bank may
     * need a single LED refreshed. The caller supplies UI state explicitly so
     * this helper can stay presentation-only and not invent pattern/menu state. */
    uint8_t ledOffset;
    uint8_t on = 0;

    /* SELECT LEDs show sub-steps only while SHIFT is held or while the UI is in
     * step-select mode. Otherwise those LEDs belong to sub-page/pattern UI. */
    if (!shiftHeld && (selectMode != SELECT_MODE_STEP))
        return;
    /* Only update if the recorded sub-step is inside the currently displayed
     * bank of eight sub-steps. selectedStepBase is 0,8,16...120. */
    if ((step < selectedStepBase) || (step >= (uint8_t)(selectedStepBase + 8u)))
        return;
    if (!pat_stepValid(step))
        return;

    /* Ask PatternData whether the recorded sub-step is active. This preserves
     * the old behavior where recording may turn the LED on, but erase/other
     * pattern changes can turn it off. */
    if (pat_trackValid(activeTrack) && pat_patternValid(shownPattern))
        on = pat_isStepActive(activeTrack, step, shownPattern);

    /* SELECT1..8 correspond to the visible sub-step window. */
    ledOffset = (uint8_t)(step - selectedStepBase);
    led_setValue(on, (uint8_t)(LED_PART_SELECT1 + ledOffset));
}

void led_updatePatternTrack(uint8_t track, uint8_t pattern,
                            uint8_t selectedStepBase)
{
    /* Repaints all sequencer LEDs for one track/pattern.
     *
     * Called from menu/button code after view changes, copy/clear, pattern
     * follow updates, and Euklid generation. This replaced LED_QUERY_SEQ_TRACK:
     * the LED part lives here; the hidden menu parameter refresh is now an
     * adjacent pat_applyTrackSettingsToMenu() call at each caller. */
    uint8_t i;
    uint8_t start;

    /* Ignore invalid UI requests rather than lighting LEDs from bogus storage. */
    if (!pat_trackValid(track) || !pat_patternValid(pattern))
        return;

    /* STEP1..16 show the main-step mask for the requested track/pattern. */
    for (i = 0; i < 16u; i++) {
        uint8_t on = pat_isMainStepActive(track, i, pattern);
        led_setValue(on, (uint8_t)(LED_STEP1 + i));
    }

    /* SELECT1..8 show whichever eight sub-steps are currently selected in the
     * step editor. buttonHandler_selectedStep is already a sub-step base. */
    start = (uint8_t)((selectedStepBase / 8u) * 8u);
    for (i = 0; i < 8u; i++) {
        uint8_t stepNr = (uint8_t)(start + i);
        uint8_t on = pat_isStepActive(track, stepNr, pattern);
        led_setValue(on, (uint8_t)(LED_PART_SELECT1 + i));
    }
}

void led_setBeatPulse(uint8_t on)
{
    /* The old reverse LED pulse opcode toggled the START/STOP LED on quarter
     * beat boundaries. The sequencer now sets seq_ledState.beatPulse and this
     * LED-owned helper applies it in the foreground. */
    led_setValue((uint8_t)(on != 0u), LED_START_STOP);
}

void led_notifyPatternChanged(uint8_t playedPattern)
{
    /*
     * Why: replaces reverse SEQ_CHANGE_PAT feedback without reintroducing a
     * bridge. Input: newly playing pattern. Outputs: menu played-pattern state,
     * optional follow-mode viewed pattern update, and visible LEDs. Risk:
     * foreground LED/menu functions must not be called from a high-priority ISR.
     */
    uint8_t patNr = (uint8_t)(playedPattern & 0x07u);
    menu_playedPattern = patNr;

    if (parameter_values[PAR_FOLLOW]) {
        menu_setShownPattern(patNr);
        led_clearSequencerLeds();
        led_updatePatternTrack(menu_getActiveVoice(), patNr, buttonHandler_selectedStep);
        pat_applyTrackSettingsToMenu(patNr, menu_getActiveVoice());
    }

    if (buttonHandler_getMode() == SELECT_MODE_PERF) {
        led_clearAllBlinkLeds();
        led_initPerformanceLeds();
    }
}

void led_notifyTrackRotationReset(uint8_t rotation)
{
    /* Called by seq_setRunning() when stop resets all transient track rotation
     * values to zero through pat_setTrackRotation().
     *
     * Why this tiny function exists:
     * - The visible menu value PAR_TRACK_ROTATION is UI presentation state.
     * - The actual stored rotation mutation belongs in PatternData.
     * - Sequencer.c should not reach directly into menu parameter arrays for a
     *   UI refresh side effect after the parser removal.
     *
     * Input: rotation value to show, normally 0 after transport stop.
     * Output: menu edit parameter reflects the reset rotation.
     * Risk: this does not mutate pattern storage; callers must reset storage
     * through pat_setTrackRotation() before notifying the UI value here. */
    parameter_values[PAR_TRACK_ROTATION] = rotation;
}

void led_processSeqLedState(void)
{
    /* Foreground drain for deferred sequencer LED events.
     *
     * Who writes seq_ledState:
     * - sequencer.c writes beatPulse/chaseStep/recordSubStep/recordMainStep and
     *   ORs SEQ_LED_DIRTY_* bits while advancing playback or recording.
     *
     * Why this lives in ledHandler and not Sequencer:
     * - Sequencer owns timing and playback indices.
     * - ledHandler owns physical LED interpretation, page-specific LED meaning,
     *   and calls into button/menu state needed to decide whether an LED should
     *   be touched.
     * - Keeping the drain here prevents a new generic bridge from replacing the
     *   removed parser.
     *
     * Call timing:
     * - main.c calls this in the foreground loop.
     * - It should stay out of TIM3 because it reads menu/button state and writes
     *   shift-register LED state.
     */
    uint8_t d = seq_ledState.dirty;
    /* activeTrack is the UI-selected voice/track. Recording feedback should
     * only repaint the track the user is currently editing. */
    uint8_t activeTrack;
    /* shownPattern is the UI-viewed pattern. It may differ from the currently
     * playing pattern when follow is off. */
    uint8_t shownPattern;

    /* Nothing marked dirty means sequencer.c has not queued any LED work since
     * the last foreground drain. */
    if (!d)
        return;
    /* Clear the dirty byte before performing work. If sequencer.c queues more
     * events while we are rendering, those bits can be set again for the next
     * foreground pass rather than being lost at the end of this function. */
    seq_ledState.dirty = 0;

    /* Snapshot UI context once so all record-related LED updates in this drain
     * use a consistent track/pattern. */
    activeTrack = menu_getActiveVoice();
    shownPattern = menu_getViewedPattern();

    /* Beat pulse: set/clear START_STOP according to seq_ledState.beatPulse. */
    if (d & SEQ_LED_DIRTY_BEAT)
        led_setBeatPulse(seq_ledState.beatPulse);

    /* Chase light: move the temporary current-step LED if the viewed pattern
     * should show playback chase. */
    if (d & SEQ_LED_DIRTY_CHASE)
        led_updateCurrentStep(seq_ledState.chaseStep);

    /* Recorded sub-step: update SELECT1..8 only if the recorded sub-step falls
     * in the visible sub-step window and the UI mode makes SELECT LEDs mean
     * sub-steps. */
    if (d & SEQ_LED_DIRTY_REC_SUB)
        led_updateRecordedSubStep(activeTrack, shownPattern,
                                  seq_ledState.recordSubStep,
                                  buttonHandler_selectedStep,
                                  buttonHandler_getShift(),
                                  buttonHandler_getMode());

    /* Recorded main-step: update STEP1..16 for the parent main step of the
     * recorded sub-step, unless performance mode owns those LEDs. */
    if (d & SEQ_LED_DIRTY_REC_MAIN)
        led_updateRecordedMainStep(activeTrack, shownPattern,
                                   seq_ledState.recordMainStep);
}
