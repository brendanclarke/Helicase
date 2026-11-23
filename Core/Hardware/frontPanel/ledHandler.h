/*
 * Core/Hardware/frontPanel/ledHandler.h
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
 * ledHandler.h — LXR-02 LED handler.
 * Mirrors original LXR ledHandler.h API by Julian Schmidt.
 */
#ifndef LEDHANDLER_H_
#define LEDHANDLER_H_

#include <stdint.h>
#include "dout.h"
#include "InstrumentManager.h"

/*
 * Canonical public LED numbering model: legacy AVR logical IDs.
 * 0..39 are logical LEDs on the 5x595 chain. LED_BAR1 is SW43 on GPIO.
 */
enum LedNumbers {
    LED_MODE1 = 0, LED_MODE2, LED_MODE3, LED_MODE4,
    LED_SHIFT, LED_REC, LED_START_STOP, LED_UNUSED,

    LED_VOICE1, LED_VOICE2, LED_VOICE3, LED_VOICE4,
    LED_VOICE5, LED_VOICE6, LED_VOICE7, LED_COPY,

    LED_PART_SELECT1, LED_PART_SELECT2, LED_PART_SELECT3, LED_PART_SELECT4,
    LED_PART_SELECT5, LED_PART_SELECT6, LED_PART_SELECT7, LED_PART_SELECT8,

    LED_STEP1, LED_STEP2, LED_STEP3, LED_STEP4,
    LED_STEP5, LED_STEP6, LED_STEP7, LED_STEP8,
    LED_STEP9, LED_STEP10, LED_STEP11, LED_STEP12,
    LED_STEP13, LED_STEP14, LED_STEP15, LED_STEP16,

    LED_BAR1 = 40
};

/*
 * Compatibility aliases for newer symbol names used by STM-side code.
 */
enum LedAliasNumbers {
    LED_SHIFT_CH = LED_SHIFT,
    LED_BAR2 = LED_UNUSED,

    /* Voice aliases matching logical voice order. */
    LED_VOICE_1 = LED_VOICE1, LED_VOICE_2 = LED_VOICE2, LED_VOICE_3 = LED_VOICE3,
    LED_VOICE_4 = LED_VOICE4, LED_VOICE_5 = LED_VOICE5, LED_VOICE_6 = LED_VOICE6,
    LED_VOICE_7 = LED_VOICE7,

    /* Legacy STM convenience aliases. */
    LED_SELECT1 = LED_PART_SELECT1, LED_SELECT2 = LED_PART_SELECT2,
    LED_SELECT3 = LED_PART_SELECT3, LED_SELECT4 = LED_PART_SELECT4,
    LED_SELECT5 = LED_PART_SELECT5, LED_SELECT6 = LED_PART_SELECT6,
    LED_SELECT7 = LED_PART_SELECT7, LED_SELECT8 = LED_PART_SELECT8,

    LED_SEQ1 = LED_STEP1, LED_SEQ2 = LED_STEP2, LED_SEQ3 = LED_STEP3, LED_SEQ4 = LED_STEP4,
    LED_SEQ5 = LED_STEP5, LED_SEQ6 = LED_STEP6, LED_SEQ7 = LED_STEP7, LED_SEQ8 = LED_STEP8,
    LED_SEQ9 = LED_STEP9, LED_SEQ10 = LED_STEP10, LED_SEQ11 = LED_STEP11, LED_SEQ12 = LED_STEP12,
    LED_SEQ13 = LED_STEP13, LED_SEQ14 = LED_STEP14, LED_SEQ15 = LED_STEP15, LED_SEQ16 = LED_STEP16
};

/*
 * Low-level LED API.
 *
 * These functions accept logical LED IDs from LedNumbers/LedAliasNumbers, not
 * raw 74HC595 positions. `val` inputs are boolean. The normal setters update
 * remembered/base LED state; the `Temp` variants alter only the current
 * physical output so later reset/pulse/blink cleanup can restore the previous
 * base state. Common clients: buttonHandler, menu page transitions, sequencer
 * foreground LED drain, and copy/clear UI.
 */
void led_init(void);
void led_setValue(uint8_t val, uint8_t ledNr);
void led_setValueTemp(uint8_t val, uint8_t ledNr);
/*
 * Restore the explicit base state for one logical LED. Temporary effect
 * cancellation is owned by ledHandler.c's internal layer renderer, so public
 * callers should use the effect API when they need blink/flash/pulse/chase
 * priority preserved.
 */
void led_reset(uint8_t ledNr);
void led_toggle(uint8_t ledNr);
void led_toggleTemp(uint8_t ledNr);
void led_clearAll(void);
/*
 * Time-based LED effect API.
 *
 * led_tickHandler() services pulse and blink slot tables and must be called
 * regularly from the front-panel service path. led_pulseLed() is a short
 * one-shot inversion that returns to original state automatically.
 * led_setBlinkLed() starts/stops persistent blinking until explicitly cleared.
 * led_clearAllBlinkLeds() cancels every blink slot and restores base state.
 * S081 P4: pulse and flash deadlines are compared by signed 16-bit distance,
 * so effects started just before the time_sysTick wrap last their full time.
 */
void led_tickHandler(void);
void led_pulseLed(uint8_t ledNr);
typedef enum {
    LED_FLASH_GROUP_SELECT = 0,
    LED_FLASH_GROUP_SEQ,
    LED_FLASH_GROUP_MODE,
    LED_FLASH_GROUP_VOICE,
    LED_FLASH_GROUP_BAR,
    LED_FLASH_GROUP_FUNCTION,
    LED_FLASH_GROUP_COUNT
} LedFlashGroup;
void led_flashGroup(LedFlashGroup group, uint16_t mask);
void led_flashLed(uint8_t ledNr);
void led_setBlinkLed(uint8_t ledNr, uint8_t onOff);
void led_clearAllBlinkLeds(void);
/*
 * Fast blink slots (S080 P2 Defect 10; S082 P3 W1): double-rate LEDs on their
 * own layer, sharing one phase. Slot QUEUED_SCENE = queued Scene's SEQ LED;
 * slot BANK_LOAD = Load/Save mode LED during a background Bank load.
 * ledNr 0xFF frees a slot. Blink and base writes never disturb them.
 * Accessors: menu_syncQueuedSceneLed().
 */
#define LED_FAST_SLOT_QUEUED_SCENE 0u
#define LED_FAST_SLOT_BANK_LOAD    1u
#define LED_FAST_SLOT_COUNT        2u
void    led_setFastBlinkSlot(uint8_t slot, uint8_t ledNr);
uint8_t led_getFastBlinkSlot(uint8_t slot);
/* S080 P2: 1 when ledNr holds a persistent blink slot. */
uint8_t led_isBlinkLed(uint8_t ledNr);

/*
 * Owned SEQ/BAR display (S102 P1, the chromatic keyboard overlay).
 *
 * What: led_ownedShow() makes STEP1..16 show seqBits (bit n = SEQ n+1),
 * BAR1 (`<`) show bar1 and BAR2 (`>`) show bar2, over everything this file
 * paints (base, chase, blink, flash, pulse, fast blink). Every other writer
 * keeps working on the state underneath; led_ownedRelease() reveals it as
 * it is now. Why: the overlay applies in every mode and must leave each
 * mode's SEQ/BAR display intact for its exit. Inputs: the 16-bit key
 * picture and two booleans. Outputs: dout_ownedMask/Data and the SW43 owner
 * (dout.h). Context: main loop. Accessor: buttonHandler.c
 * buttonHandler_chromaPaint() / buttonHandler_chromaExit().
 */
void led_ownedShow(uint16_t seqBits, uint8_t bar1, uint8_t bar2);
void led_ownedRelease(void);

/* High-level voice/mode LED control — matches original API */
/*
 * High-level voice/mode/page LED API.
 *
 * These helpers encode row-level UI meaning on top of the low-level LED
 * primitives. Inputs are zero-based row indices or bit patterns as described
 * beside each function. They write remembered/base LED state unless otherwise
 * noted by the implementation.
 */
void led_setActivePage(uint8_t pageNr);            /* light one page/select LED */
void led_setActiveVoice(uint8_t voiceNr);           /* light one voice LED */
void led_setActiveVoiceLeds(uint8_t pattern);       /* light pattern of voice LEDs */
void led_setActiveSelectButton(uint8_t butNr);      /* light one SELECT LED */
void led_setMode2Leds(uint8_t value);               /* byte-write select LED helper */
void led_setMode2(uint8_t status);                  /* set MODE button LEDs per selectButtonMode */
void led_clearSequencerLeds(void);
void led_clearSequencerLeds1_8(void);
void led_clearSequencerLeds9_16(void);
void led_clearSelectLeds(void);
void led_clearVoiceLeds(void);
void led_setActive_step(uint8_t stepNr);
void led_clearActive_step(void);

/*
 * Paint the visible SEQ row from one parameter's automation presence.
 *
 * What: lights a step when its current Pattern block contains the exact
 * canonical target, and always keeps physically held steps lit. Why: the
 * single-parameter VOICE held-step overlay uses this row to show which steps
 * can supply the displayed automation value. Inputs: Pattern track/Scene,
 * target ID, and the 16-bit visible held-step mask. Output: STEP1..STEP16
 * remembered LED state. Affiliates: pat_readStepAutomations() and
 * led_updatePatternTrackView() for restoration after overlay exit.
 */
void led_updateAutomationStepView(uint8_t track, uint8_t pattern,
                                  instrument_param_id_t target,
                                  uint16_t heldMask);

/* Deferred sequencer LED dirty bits.
 *
 * Why these flags exist:
 * - seq_tick() is owned by the TIM3 timing path and can discover LED-worthy
 *   events while advancing playback.
 * - Directly repainting the 74HC595 LED chain and consulting menu/button state
 *   from that timing path is too entangled and was formerly hidden behind the
 *   removed front-panel parser.
 * - The sequencer now writes a tiny data payload plus one of these bits; the
 *   foreground main loop calls led_processSeqLedState() to perform the actual
 *   LED/menu-aware rendering here in ledHandler.c.
 *
 * Meaning of each bit:
 * - CHASE: chase/current-step LED should move to seq_ledState.chaseStep;
 *   the drain also checks the active track's bar for the VOICE-mode bar
 *   pulse (S081 P4), reading the track live, not from chaseStep.
 * - BEAT: START/STOP beat pulse LED should reflect seq_ledState.beatPulse.
 * - REC_SUB: compatibility dirty bit; SELECT1..8 now identify bars, so the
 *   exported handler is a documented no-op during the bridge.
 * - REC_MAIN: a live-recorded bridge step should update STEP1..16 visibility
 *   when that step is inside the viewed bar.
 *
 * Risk:
 * - These flags are shared between sequencer timing code and foreground LED
 *   code. The payloads are byte-sized and existing code used the same coarse
 *   dirty-byte exchange pattern, but future widening should audit races.
 */
#define SEQ_LED_DIRTY_CHASE    0x01
#define SEQ_LED_DIRTY_BEAT     0x02
/* 0x04 retired in S081 B-8 (the recordSubStep endpoint is removed). */
#define SEQ_LED_DIRTY_REC_MAIN 0x08

typedef struct {
    /* Bitmask of SEQ_LED_DIRTY_* values. Written by sequencer.c, drained by
     * led_processSeqLedState() in foreground. */
    volatile uint8_t dirty;
    /* Sub-step index 0..127 for the moving chase light. Carries no track id;
     * the S081 P4 bar pulse reads the active track live through
     * seq_captureTrackTime() instead of this field. */
    volatile uint8_t chaseStep;
    /* 0/1 value for the temporary beat pulse on LED_START_STOP. */
    volatile uint8_t beatPulse;
    /* Absolute step index 0..127 whose STEP-row LED may need refresh (S081
     * B-8c: the field now carries the absolute step, not step % 16). */
    volatile uint8_t recordMainStep;
} SeqLedState;

/*
 * Exported deferred sequencer LED state.
 *
 * Writers: sequencer.c updates the volatile payload fields and ORs dirty bits
 * while advancing playback or recording. Reader/owner: led_processSeqLedState()
 * drains dirty bits in foreground and performs the physical LED rendering.
 * External clients should treat this as a narrow timing-to-UI handoff, not a
 * general LED API.
 */
extern SeqLedState seq_ledState;

/*
 * Sequencer LED helpers.
 * Why: these replace the old protocol LED path with LED-owned rendering.
 * Inputs are pattern/track/step display coordinates. Outputs are physical LED
 * state updates. Risk: led_processSeqLedState() is foreground-only because it
 * touches menu/button state and shift-register LED state.
 */
/* Move/clear chase feedback for playback step `step` after page/pattern checks. */
void led_updateCurrentStep(uint8_t step);
/* Refresh STEP-row record feedback for one recorded absolute step (S081 B-8c). */
void led_updateRecordedMainStep(uint8_t activeTrack,
                                uint8_t shownPattern,
                                uint8_t step);
/* Repaint STEP and SELECT rows from PatternData for one track/pattern view. */
void led_updatePatternTrack(uint8_t track, uint8_t pattern,
                            uint8_t selectedStepBase);
void led_updatePatternTrackView(uint8_t track, uint8_t pattern,
                                uint8_t selectedStepBase,
                                uint8_t updateSelectRow);
/* Apply START/STOP beat pulse state supplied by Sequencer. */
void led_setBeatPulse(uint8_t on);
/* Update Menu/LED pattern-follow state after Sequencer changes pattern. */
void led_notifyPatternChanged(uint8_t playedPattern);
/* Mirror a Sequencer rotation reset into the visible menu parameter. */
/*
 * Foreground drain for seq_ledState dirty payloads.
 *
 * The CHASE payload is transport-gated at this ownership boundary: while
 * seq_isRunning() is zero, the drain clears any retained LED_LAYER_CHASE
 * rather than installing a playback-position inversion on the STEP row.
 * This covers boot, transport stop, and stopped Scene/pattern realignment.
 * S081 P4: in VOICE mode a CHASE drain that finds the active track in a new
 * bar pulses that bar's SELECT LED once (single-bar and rnd tracks never; no
 * pulse on the first step after a start). The bar is read live through
 * seq_captureTrackTime(), not from chaseStep, which has no track id.
 */
void led_processSeqLedState(void);

#endif
