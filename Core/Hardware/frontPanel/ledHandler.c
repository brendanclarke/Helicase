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

/*
 * Temporary LED effect capacities and periods.
 *
 * Pulsing is a one-shot temporary inversion: led_pulseLed() toggles the
 * physical output without changing the remembered/base LED state, then
 * led_tickHandler() restores that base state after LED_PULSE_TIME_MS.
 *
 * Blinking is persistent until cancelled: led_setBlinkLed(..., 1) puts an LED
 * into a slot, led_tickHandler() toggles it every LED_BLINK_TIME_MS, and
 * led_setBlinkLed(..., 0) or led_clearAllBlinkLeds() restores the base state.
 *
 * Clients/confederates: buttonHandler uses blink/pulse feedback for mode,
 * copy, step, and pattern-selection gestures; sequencer feedback uses direct
 * setters through led_processSeqLedState(). Inputs are logical LED IDs from
 * ledHandler.h, not raw shift-register bit numbers.
 */
#define NUM_OF_PULSABLE_LEDS   8
#define LED_PULSE_TIME_MS      50
#define NUM_OF_BLINKABLE_LEDS  8
#define LED_BLINK_TIME_MS      250

/*
 * One-shot pulse slots.
 *
 * led_pulseEndTime stores an absolute time_sysTick deadline for each active
 * pulse slot. led_pulseLedNumber stores the logical LED ID that must be
 * restored when the deadline passes. led_pulsingLeds is the active-slot bitset:
 * bit N set means slot N is in use. led_tickHandler() is the only normal
 * consumer. The arrays are volatile because the tick handler may run from the
 * timer-driven LED service path while foreground code starts effects.
 */
static volatile uint16_t led_pulseEndTime[NUM_OF_PULSABLE_LEDS];
static volatile uint8_t  led_pulseLedNumber[NUM_OF_PULSABLE_LEDS];
static volatile uint8_t  led_pulsingLeds;

/*
 * Current chase LED state for sequencer playback display.
 *
 * 0xFF means no temporary chase LED is active. led_setActive_step() stores the
 * logical STEP LED currently inverted for chase feedback; led_clearActive_step()
 * restores it. This is distinct from stored pattern LEDs, which live in
 * led_originalLedState and are written with led_setValue().
 */
static volatile uint8_t  led_currentStepLed = 0xFFu;

/*
 * Dedicated BAR1/SW43 GPIO LED state.
 *
 * Most LEDs are on the 74HC595 output chain and can be represented in
 * dout_outputData[] plus led_originalLedState[]. LED_BAR1 is separate GPIO, so
 * it needs its own current value and base/original value for temporary pulse,
 * blink, and reset behavior.
 */
static uint8_t           led_sw43State;
static uint8_t           led_sw43OriginalState;

/*
 * Persistent blink slots.
 *
 * led_nextBlinkTime is the next absolute time_sysTick when all active blink
 * slots should toggle. led_blinkLedNumber stores logical LED IDs and
 * led_blinkingLeds is the active-slot bitset. led_setBlinkLed() allocates or
 * frees slots; led_tickHandler() performs the periodic toggles.
 */
static uint16_t led_nextBlinkTime;
static volatile uint8_t led_blinkLedNumber[NUM_OF_BLINKABLE_LEDS];
static volatile uint8_t led_blinkingLeds;

/*
 * Remembered/base state for all shift-register LEDs.
 *
 * Why this exported array exists: temporary operations such as pulse, blink,
 * and chase need to alter dout_outputData[] without forgetting the real UI LED
 * state they must restore. led_setValue() and led_toggle() update this array;
 * led_setValueTemp(), led_toggleTemp(), and led_reset() read it but do not
 * replace it. Common clients are all high-level LED helpers in this file; raw
 * external writes should avoid touching it directly.
 *
 * Indexing: array byte/bit positions are physical shift-register positions
 * after logical LED IDs have been mapped through led_logicalToPhysical[] and
 * btn_to_sr[].
 */
uint8_t led_originalLedState[NUM_OUTS / 8];  /* extern for ledHandler.c internal use */

/*
 * Deferred sequencer LED payload.
 *
 * Sequencer timing code writes this small struct from playback/record paths and
 * sets dirty bits; foreground code drains it in led_processSeqLedState(). It
 * exists to keep TIM3 sequencer timing from consulting menu/button state or
 * mutating the shift-register LED chain directly.
 */
SeqLedState seq_ledState = {0};

/* SR mapping: physical button index → SR chain bit */
/*
 * Physical button-index to shift-register bit mapping.
 *
 * Input: physical front-panel output index after logical-to-physical mapping.
 * Output: the bit position in the five-byte 74HC595 chain. led_arrayPos() and
 * led_bitPos() consume this table to locate the byte/bit in dout_outputData[].
 */
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
/*
 * Logical LED ID to physical output-index mapping.
 *
 * The public API intentionally uses legacy AVR logical IDs (LED_MODE1,
 * LED_PART_SELECT1, LED_STEP1, etc.) because button/menu/sequencer code was
 * written around those names. The LXR-02 74HC595 chain is wired in a different
 * order, so this table maps a logical ID to the physical output index that
 * btn_to_sr[] can then convert to a shift-register bit. LED_BAR1 is not in
 * this table because it is a dedicated SW43 GPIO LED.
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

/*
 * Convert a public logical LED ID to a physical output index.
 *
 * Input: ledNr is a logical LED value from LedNumbers/LedAliasNumbers. Output:
 * a physical chain index 0..NUM_OUTS-1, LED_BAR1 for the dedicated GPIO LED,
 * or 0xFF for invalid IDs. All low-level LED writers call this first so invalid
 * logical IDs fail silently instead of corrupting shift-register state.
 */
static uint8_t led_toPhysicalNumber(uint8_t ledNr)
{
    if (ledNr < NUM_OUTS) return led_logicalToPhysical[ledNr];
    if (ledNr == LED_BAR1) return LED_BAR1;
    return 0xFFu;
}

/*
 * Locate a physical output index inside dout_outputData[].
 *
 * Inputs: ledNr is already a physical chain index, not a logical public LED ID.
 * Outputs: led_arrayPos() returns the output byte and led_bitPos() returns the
 * bit within that byte. The byte expression mirrors the existing 74HC595 chain
 * order where logical byte 0 is physically the last shifted byte.
 */
static uint8_t led_arrayPos(uint8_t ledNr) { return (uint8_t)(4 - btn_to_sr[ledNr]/8); }
static uint8_t led_bitPos(uint8_t ledNr)   { return btn_to_sr[ledNr] % 8; }

/*
 * Restore one shift-register LED to its remembered/base state.
 *
 * Input: ledNr is a physical chain index. Output: dout_outputData[] is updated
 * to match led_originalLedState[] for that LED; led_originalLedState[] itself
 * is not changed. This is the subsidiary restore primitive for led_reset(),
 * pulse expiry, blink cancellation, and chase cleanup.
 */
static void led_resetToOriginal(uint8_t ledNr)
{
    uint8_t ap = led_arrayPos(ledNr), bp = led_bitPos(ledNr);
    if (led_originalLedState[ap] & (1<<bp))
        dout_outputData[ap] |=  (uint8_t)(1<<bp);
    else
        dout_outputData[ap] &= (uint8_t)~(1<<bp);
}

/*
 * Initialize LED presentation state.
 *
 * Why: boot must begin from a known LED state before menu/button/sequencer code
 * starts issuing high-level updates. Inputs: none. Outputs: effect slot bitsets,
 * the temporary chase LED, the BAR1 GPIO shadow, and the remembered/base
 * shift-register state are reset. Hardware shift-register bytes are initialized
 * elsewhere; this function owns the ledHandler-side mirrors.
 *
 * Common caller: main/front-panel initialization before normal UI service.
 */
void led_init(void)
{
    led_pulsingLeds = 0; led_blinkingLeds = 0;
    led_currentStepLed = 0xFFu;
    led_sw43State = 0;
    led_sw43OriginalState = 0;
    memset(led_originalLedState, 0, sizeof(led_originalLedState));
}

/*
 * Set one LED's base state.
 *
 * Inputs: val is treated as boolean on/off; ledNr is a logical LED ID. Output:
 * the physical LED output is changed and the remembered/base state is updated
 * so future temporary effects restore to this value. For LED_BAR1 the GPIO
 * shadow state is updated and dout_setSw43Led() is called directly; for chain
 * LEDs this writes dout_outputData[] plus led_originalLedState[].
 *
 * Common callers: mode/page/voice selectors, buttonHandler transport LEDs,
 * sequencer LED foreground drain, and clear helpers. Invalid ledNr is ignored.
 */
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

/*
 * Set one LED temporarily without replacing its base state.
 *
 * Inputs: val is boolean on/off; ledNr is a logical LED ID. Output: the current
 * physical output changes, but led_originalLedState[] or led_sw43OriginalState
 * are left untouched. This is the primitive for pulse/blink/chase effects whose
 * whole point is to restore the previous UI state later.
 *
 * Common callers/confederates: led_toggleTemp(), led_tickHandler(), and any
 * temporary feedback effect that must not become the new steady-state LED.
 */
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

/*
 * Restore one LED to its remembered/base state.
 *
 * Input: ledNr is a logical LED ID. Output: the physical output is rewritten to
 * led_originalLedState[] or led_sw43OriginalState. No base state changes. This
 * is used when a temporary effect expires or is cancelled.
 *
 * Common callers: led_tickHandler() pulse expiry, blink cancellation,
 * led_clearActive_step(), and led_setActive_step() when moving the chase LED.
 */
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

/*
 * Toggle one LED's base state.
 *
 * Input: ledNr is a logical LED ID. Output: current physical state and
 * remembered/base state both invert. Use this for persistent UI toggles where
 * future resets should return to the toggled value.
 *
 * Common callers: step/sub-step edit feedback where the visible LED is treated
 * as optimistic persistent state. Temporary effects should use led_toggleTemp()
 * instead.
 */
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

/*
 * Toggle one LED temporarily without replacing its base state.
 *
 * Input: ledNr is a logical LED ID. Output: physical output inverts, but the
 * remembered/base state is unchanged. This lets led_reset() later restore the
 * pre-effect value. Invalid logical IDs are ignored.
 *
 * Common callers: led_pulseLed(), led_setBlinkLed() slot start, periodic blink
 * toggles in led_tickHandler(), and sequencer chase highlighting.
 */
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

/*
 * Clear every LED and every remembered/base LED state.
 *
 * Inputs: none. Outputs: all shift-register output bytes and their base-state
 * mirrors are zeroed; the dedicated BAR1 GPIO LED and its base shadow are also
 * cleared. This is a global visual reset, not a temporary-effect cancellation
 * only.
 *
 * Common callers: initialization and full-screen/UI reset paths.
 */
void led_clearAll(void)
{
    int i;
    for (i=0;i<NUM_OUTS/8;i++) { dout_outputData[i]=0; led_originalLedState[i]=0; }
    led_sw43State = 0;
    led_sw43OriginalState = 0;
    dout_setSw43Led(0);
}

/*
 * Start a one-shot temporary LED inversion.
 *
 * Input: ledNr is a logical LED ID. Output: if a pulse slot is available, the
 * LED is toggled temporarily and scheduled to reset after LED_PULSE_TIME_MS.
 * The base state is not changed. If all slots are busy, the request is dropped.
 *
 * Timing semantics: this is not a repeating pulse train. It is one inversion
 * lasting about 50 ms at the current LED_PULSE_TIME_MS value, after which
 * led_tickHandler() calls led_reset() for the original/base state.
 *
 * Common callers: short acknowledgements where "blip the current LED state" is
 * enough. Longer patterned flashes should use a dedicated flash path rather
 * than stretching this duration for every pulse client.
 */
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

/*
 * Add or remove one LED from the persistent blink set.
 *
 * Inputs: ledNr is a logical LED ID; onOff nonzero requests blinking, zero
 * cancels blinking. Output on start: a free blink slot stores ledNr and the LED
 * is temporarily toggled immediately. Output on stop: matching blink slots are
 * cleared and the LED is restored to its base state. Full slot allocation drops
 * a new start request silently.
 *
 * Common callers: buttonHandler mode/step/copy gestures and performance-view
 * pattern indications. This function does not own blink timing; led_tickHandler()
 * performs the periodic toggles.
 */
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

/*
 * Cancel all persistent blinking LEDs.
 *
 * Inputs: none. Outputs: every active blink slot is cleared and every affected
 * LED is restored to its remembered/base state. Pulse slots and the sequencer
 * chase state are not altered.
 *
 * Common callers: mode changes and UI gesture transitions where old blink
 * feedback must be cleared before drawing a new page/state.
 */
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

/*
 * Service time-based LED effects.
 *
 * Why: pulse and blink effects are represented as small slot tables so callers
 * can request visual feedback without blocking. Inputs are implicit global
 * effect state and time_sysTick. Outputs: expired pulse slots restore their
 * LEDs, and active blink slots toggle at LED_BLINK_TIME_MS intervals.
 *
 * Call timing: invoked from the front-panel/timebase service path. It must stay
 * cheap and non-blocking. It does not drain sequencer LED dirty state; that is
 * handled separately by led_processSeqLedState() in the foreground loop.
 */
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

/*
 * Show one active page on the SELECT LED row.
 *
 * Input: pageNr is a zero-based SELECT/page index 0..7. Output: the SELECT row
 * is rewritten through led_setActiveSelectButton(). This is a compatibility
 * helper for legacy menu/load-save code where page selection and SELECT LED
 * selection were the same concept.
 */
void led_setActivePage(uint8_t pageNr)
{
    if (pageNr < 8u) {
        led_setActiveSelectButton(pageNr);
    }
}

/* Light exactly one voice LED (LED_VOICE1..LED_VOICE7), clear others.
** voiceNr is 0-based. */
/*
 * Light exactly one voice LED and clear the rest.
 *
 * Input: voiceNr is zero-based, 0..6 for LED_VOICE1..LED_VOICE7. Output: all
 * voice LEDs are first cleared, the selected voice LED is set when valid, and
 * menu_muteModeActive is cleared because the row now represents active voice
 * selection rather than mute state.
 *
 * Common callers: buttonHandler voice selection and mode/page transitions.
 */
void led_setActiveVoice(uint8_t voiceNr)
{
    int i;
    for (i=0;i<7;i++) led_setValue(0, (uint8_t)(LED_VOICE1 + i));
    if (voiceNr < 7u) {
        led_setValue(1, (uint8_t)(LED_VOICE1 + voiceNr));
    }
    menu_muteModeActive = 0;
}

/*
 * Write an arbitrary bit pattern to the voice LED row.
 *
 * Input: pattern bit 0 controls LED_VOICE1, bit 6 controls LED_VOICE7. Output:
 * each voice LED is set/cleared from the corresponding bit. This is used when
 * the voice row represents a bitset, such as mute display, rather than one
 * selected voice.
 *
 * Common callers: buttonHandler mute-mode helpers.
 */
void led_setActiveVoiceLeds(uint8_t pattern)
{
    /* pattern bit 0 = voice 1, bit 6 = voice 7 */
    int i;
    for (i=0;i<7;i++)
        led_setValue((uint8_t)((pattern >> i) & 1u), (uint8_t)(LED_VOICE1 + i));
}

/* Light one SELECT LED (0-7), clear others */
/*
 * Light exactly one SELECT LED and clear the rest.
 *
 * Input: butNr is zero-based, 0..7 for LED_PART_SELECT1..8. Output: every
 * SELECT LED is cleared first, then the requested one is set if valid. This
 * writes base state, so temporary effects later restore to this selected row.
 *
 * Common callers: voice subpage selection, load/save page selection, and the
 * current bridge's bar indicator in STEP/PATTERN contexts.
 */
void led_setActiveSelectButton(uint8_t butNr)
{
    int i;
    for (i=0;i<8;i++) led_setValue(0, (uint8_t)(LED_PART_SELECT1 + i));
    if (butNr < 8u) led_setValue(1, (uint8_t)(LED_PART_SELECT1 + butNr));
}

/*
 * Write an inverted byte pattern to the SELECT LED row.
 *
 * Input: value bit 0 corresponds to LED_PART_SELECT1 before inversion. Output:
 * each SELECT LED receives the inverted bit value. This mirrors an original
 * LXR helper whose callers pass packed UI state where zero means LED on.
 *
 * Common callers: legacy mode/page code that wants a whole SELECT row pattern
 * rather than a single active select LED.
 */
void led_setMode2Leds(uint8_t value)
{
    uint8_t i;
    for (i = 0; i < 8; i++) {
        led_setValue((uint8_t)(((~value) >> i) & 1u), (uint8_t)(LED_PART_SELECT1 + i));
    }
}

/* Set MODE LEDs according to selectButtonMode (0-7) — exact port from original */
/*
 * Set the four MODE LEDs for the current select-button mode.
 *
 * Input: status is the buttonHandler selectButtonMode value. Outputs: all MODE
 * LEDs and their blink states are cleared, then the corresponding MODE LED is
 * lit or blinked according to the legacy mapping. Status 5 blinks MODE2 for
 * PAT_GEN-style mode; status 7 blinks MODE4 for global/menu mode.
 *
 * Common callers: buttonHandler mode changes. This function intentionally keeps
 * original LXR mode mapping rather than deriving it from enum names.
 */
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

/*
 * Clear the complete 16-LED sequencer/STEP row.
 *
 * Inputs: none. Output: LED_STEP1..LED_STEP16 base states are set off. Common
 * callers include mode changes, copy/clear operations, and pattern/page
 * repaints before a fresh sequencer row is drawn.
 */
void led_clearSequencerLeds(void)
{
    int i;
    for (i=0;i<16;i++) led_setValue(0, (uint8_t)(LED_STEP1 + i));
}

/*
 * Clear the first half of the sequencer/STEP row.
 *
 * Inputs: none. Output: LED_STEP1..LED_STEP8 base states are set off. This
 * exists for UI modes that partition the 16 step LEDs into two 8-button groups.
 */
void led_clearSequencerLeds1_8(void)
{
    int i;
    for (i=0;i<8;i++) led_setValue(0, (uint8_t)(LED_STEP1 + i));
}

/*
 * Clear the second half of the sequencer/STEP row.
 *
 * Inputs: none. Output: LED_STEP9..LED_STEP16 base states are set off. This
 * pairs with led_clearSequencerLeds1_8() for split-row UI modes.
 */
void led_clearSequencerLeds9_16(void)
{
    int i;
    for (i=0;i<8;i++) led_setValue(0, (uint8_t)(LED_STEP9 + i));
}

/*
 * Clear all SELECT LEDs.
 *
 * Inputs: none. Output: LED_PART_SELECT1..8 base states are set off. Common
 * callers are mode/page transitions, copy gestures, performance-page redraw,
 * and any UI state that will immediately redraw the SELECT row with a new
 * meaning.
 */
void led_clearSelectLeds(void)
{
    int i;
    for (i=0;i<8;i++) led_setValue(0, (uint8_t)(LED_PART_SELECT1 + i));
}

/*
 * Clear all voice LEDs.
 *
 * Inputs: none. Output: LED_VOICE1..7 base states are set off. Common callers
 * include copy setup and mode/page changes where the voice row will be redrawn
 * as active voice or mute state.
 */
void led_clearVoiceLeds(void)
{
    int i;
    for (i=0;i<7;i++) led_setValue(0, (uint8_t)(LED_VOICE1 + i));
}

/*
 * Move the temporary sequencer chase LED.
 *
 * Input: stepNr is the currently playing legacy sub-step index 0..127. Output:
 * the previous chase LED is restored, then the STEP LED for stepNr / 8 is
 * temporarily toggled. The base step LED state is not changed.
 *
 * Common caller: led_updateCurrentStep() during foreground drain of sequencer
 * chase dirty state. Phase 2 bridge work will reinterpret this mapping as
 * step-in-visible-bar rather than old step/8 grouping.
 */
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

/*
 * Remove any temporary sequencer chase LED.
 *
 * Inputs: none. Output: if a chase LED is active, it is restored to its base
 * state and led_currentStepLed returns to 0xFF. This is used when the UI is not
 * showing the played pattern or when a page uses STEP LEDs for something else.
 */
void led_clearActive_step(void)
{
    if (led_currentStepLed != 0xFFu) {
        led_reset(led_currentStepLed);
        led_currentStepLed = 0xFFu;
    }
}

/*
 * Draw SELECT-row feedback for performance mode.
 *
 * Inputs are implicit Menu state: menu_playedPattern and menu_getViewedPattern().
 * Output: SELECT LEDs are cleared, the currently played pattern LED is lit, and
 * the viewed-but-not-played pattern blinks. This preserves the old performance
 * pattern-follow display until the Scene bridge replaces pattern slots.
 *
 * Common callers: entering performance mode and pattern-change notification.
 */
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

/*
 * Render sequencer chase/current-step feedback for the active UI context.
 *
 * Input: step is the playback step/sub-step index supplied through
 * seq_ledState.chaseStep. Outputs: if the viewed pattern matches the played
 * pattern and the current page allows chase feedback, led_setActive_step()
 * moves the temporary STEP LED; otherwise any chase LED is cleared.
 *
 * Common callers/accessors: led_processSeqLedState() calls this in foreground
 * after sequencer.c marks SEQ_LED_DIRTY_CHASE. It reads menu_getViewedPattern(),
 * menu_playedPattern, and menu_activePage because LED meaning depends on UI
 * page. PatternData supplies pat_stepValid() for defensive bounds.
 */
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

/*
 * Refresh the STEP-row LED after a live-recorded note changes pattern data.
 *
 * Inputs: activeTrack is the currently edited Menu voice/track; shownPattern is
 * the pattern slot currently shown by the UI; subStep is the recorded
 * 0..127 legacy sub-step. Output: the parent main-step LED is set from
 * PatternData if the UI page permits recording feedback. No pattern storage is
 * mutated here.
 *
 * Common caller: led_processSeqLedState() after sequencer.c marks
 * SEQ_LED_DIRTY_REC_MAIN. Confederates: PatternData owns the main-step bit read
 * by pat_isMainStepActive(); Menu owns page context. Phase 2 bridge work will
 * reinterpret this as recorded-step-in-visible-bar feedback.
 */
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

/*
 * Refresh the SELECT-row LED after a live-recorded sub-step changes state.
 *
 * Inputs: activeTrack and shownPattern identify the UI-visible pattern track;
 * step is the recorded 0..127 step; selectedStepBase is the first step in the
 * currently visible old 8-step sub-step window; shiftHeld and selectMode tell
 * whether SELECT LEDs currently represent step data. Output: one SELECT LED is
 * set from pat_isStepActive() when the recorded step is visible and meaningful
 * for the current UI mode.
 *
 * Common caller: led_processSeqLedState() after SEQ_LED_DIRTY_REC_SUB. This
 * helper is presentation-only and intentionally receives Button/Menu state as
 * parameters instead of reaching into PatternData storage directly beyond the
 * final active-state read.
 */
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

/*
 * Repaint sequencer LEDs for one visible track/pattern.
 *
 * Inputs: track is a PatternData track 0..6, pattern is the viewed pattern
 * slot, and selectedStepBase is the old sub-step window base used for SELECT
 * LEDs. Outputs: STEP1..16 are redrawn from the pattern's main-step mask and
 * SELECT1..8 are redrawn from individual step active bits in the selected
 * window.
 *
 * Common callers: buttonHandler and menu call this after track, pattern, page,
 * copy/clear, and generator changes. PatternData owns all pattern reads.
 * Callers that also need menu parameter_values refreshed must call
 * pat_applyTrackSettingsToMenu() separately. Phase 2 bridge work will change
 * selectedStepBase interpretation to the current bar's first step.
 */
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

/*
 * Apply the transport beat-pulse LED state.
 *
 * Input: on is boolean and comes from seq_ledState.beatPulse. Output:
 * LED_START_STOP base state is set on/off. The old firmware pulsed this through
 * reverse front-panel messages; this direct helper keeps the physical LED write
 * owned by ledHandler while Sequencer owns beat timing.
 *
 * Common caller: led_processSeqLedState() when SEQ_LED_DIRTY_BEAT is set.
 */
void led_setBeatPulse(uint8_t on)
{
    /* The old reverse LED pulse opcode toggled the START/STOP LED on quarter
     * beat boundaries. The sequencer now sets seq_ledState.beatPulse and this
     * LED-owned helper applies it in the foreground. */
    led_setValue((uint8_t)(on != 0u), LED_START_STOP);
}

/*
 * Notify the front-panel layer that playback has switched patterns.
 *
 * Input: playedPattern is the new sequencer pattern slot; only the low three
 * bits are used by the legacy 0..7 pattern UI. Outputs: menu_playedPattern is
 * updated, follow mode may update menu_shownPattern, sequencer LEDs and track
 * settings may repaint, and performance SELECT LEDs are refreshed when the UI
 * is in performance mode.
 *
 * Common caller: sequencer.c after a pattern-boundary switch. Confederates:
 * Menu owns viewed/played pattern state and follow setting; PatternData owns
 * track settings; buttonHandler supplies current mode. This must run in
 * foreground context because it touches Menu and LED state.
 */
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

/*
 * Mirror a sequencer-driven track-rotation reset into the visible menu value.
 *
 * Input: rotation is the value that should be displayed for PAR_TRACK_ROTATION,
 * normally 0 after stop/reset behavior. Output: parameter_values is updated;
 * no LEDs or PatternData storage are changed here.
 *
 * Common caller: sequencer.c after it has already reset stored track rotation
 * through PatternData. This exists because Sequencer owns transport reset
 * timing, PatternData owns stored rotation, and Menu owns the visible parameter.
 */
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

/*
 * Drain deferred sequencer LED events in foreground context.
 *
 * Inputs are the volatile seq_ledState fields written by sequencer.c:
 * dirty selects which payloads are valid, chaseStep is the current playback
 * step, beatPulse is the START/STOP beat state, and recordSubStep/recordMainStep
 * identify recorded steps needing presentation refresh. Outputs are physical
 * LED updates and small Menu-side mirrors via helper functions in this file.
 *
 * Why this exists: Sequencer timing can discover LED-worthy events inside the
 * TIM3 playback path, but LED rendering needs Menu/Button state and shift-
 * register writes. This foreground drain is the ownership boundary between
 * timing and physical UI. Common caller: main loop after front-panel services.
 */
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
