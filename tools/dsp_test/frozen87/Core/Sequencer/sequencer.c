/*
 * sequencer.c
 *
 *  Created on: 11.04.2012
 *  Modified on 17.05.2026 by Brendan Clarke
 * ------------------------------------------------------------------------------------------------------------------------
 *  Copyright 2013 Julian Schmidt
 *  Julian@sonic-potions.com
 * ------------------------------------------------------------------------------------------------------------------------
 *  This file is part of the Sonic Potions LXR drumsynth firmware.
 * ------------------------------------------------------------------------------------------------------------------------
 *  Redistribution and use of the LXR code or any derivative works are permitted
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


#include "stm32f4xx.h"
#include "globals.h"
#include "DrumVoice.h"
#include "Snare.h"
#include "HiHat.h"
#include "MidiDefs.h"
#include "MidiIn.h"
#include "MidiOut.h"
#include "MidiTransport.h"
#include "MidiVoiceControl.h"
#include "CymbalVoice.h"
#include "sequencer.h"
#include <string.h>
#include "usb_manager.h"
#include "clockSync.h"
#include "MidiPitch.h"
#include "SomGenerator.h"
#include "triggerJacks.h"
#include "timebase.h"
#include "ledHandler.h"
#include "menu.h"
#include "SceneData.h"
#include "BankData.h" /* S080 G7: bank_scenePresent() for chain targets. */
#include "config.h"
#include "PatternTrace.h"
#include "PatternStackService.h"
#include "SceneModTargets.h"
#include "InstrumentManager.h"
#include "presetManager.h"
#include "presetMorphEngine.h"
#include "StepScale.h"
#include "EffectsManager.h"

/*
 * Pattern probability uses the existing hardware RNG without new state.
 * Inputs/outputs: compile-time declaration only; seq_advanceTrackStep() owns
 * the probability gate and calls GetRngValue() at trigger time.
 */
#include "random.h"


#define SEQ_INTERNAL_PPQ	96u
#define SEQ_MIDI_PPQ        24u
#define SEQ_DEFAULT_STEPS_PER_BEAT 4u
#define SEQ_INTERNAL_TICKS_PER_DEFAULT_STEP (SEQ_INTERNAL_PPQ / SEQ_DEFAULT_STEPS_PER_BEAT)
#define SEQ_INTERNAL_TICKS_PER_MIDI_CLOCK   (SEQ_INTERNAL_PPQ / SEQ_MIDI_PPQ)
#define SEQ_AUTO_SYNC_HOLD_US	500000UL

uint8_t seq_masterStepCnt=0;				/** compatibility mirror of the low 8 bits of seq_masterStepClock */
static uint16_t seq_masterStepClock = 0;    /**< fixed-grid sixteenth-note clock */
static uint32_t seq_elapsedPpqTicks = 0;    /**< 96 PPQ ticks elapsed since the current pattern/start reset */
/*
 * Resume position for MIDI Continue and Song Position Pointer (S083 T1).
 *
 * What: the master tick (96 PPQ) that a Continue plays first. Written at
 * every stop (the tick after the last one played, so nothing is played
 * twice; 0 when no tick was played yet), by an SPP while stopped
 * (SPP x 24, the next position), and cleared by Start. Why: a stop resets seq_elapsedPpqTicks
 * to 0, so without this Continue could only restart. RAM: 4 B SRAM1
 * (R-C). Writers: seq_setRunningEx() (stop/start), seq_setResumeTick()
 * (SPP), seq_continueRunning() (consumes). Context: TIM3 and foreground
 * (single aligned 32-bit word; Cortex-M7 reads/writes it atomically).
 */
static uint32_t seq_resumeTick = 0u;
static uint8_t seq_initialSchedulerTick = 1;/**< nonzero until the immediate step at PPQ tick 0 has been processed */
static uint8_t seq_internalMidiClockPhase = 0;

/*
 * FX-sequencer timing latch (Session 072 step 8; plan §11.1).
 *
 * TIM3 writes one RESET/STEP byte and foreground EffectsManager consumes it.
 * The short PRIMASK transaction prevents a foreground read/clear from
 * tearing a simultaneous scheduler publication; a newer step replaces an
 * older pending step by design.
 */
static volatile uint8_t seq_fxEvent = 0u;

/*
 * FX sequencer Q8.8 DDA accumulator (S078 §2.4.4 Stage 3).
 *
 * What: one uint16_t holding the FX sequencer's fractional tick remainder,
 * replacing the integer modulo in seq_fxClockTick(). Why: the FX sequencer
 * shares the same 128-position curve as Pattern tracks and must support
 * fractional tick intervals for non-musical CC positions. Inputs: incremented
 * 256 per PPQ tick; seeded/reset by seq_setStepIndexToStart(). Outputs: gates
 * seq_fxPublishStep() calls. RAM: 2 bytes ISR-static SRAM1. Affiliates:
 * stepScale_ticksQ8(), scene_effectConst()->seq_step_scale.
 */
static uint16_t seq_fxAccumulator = 0u;

/*
 * FX sequencer step counter (S078 Stage 3).
 *
 * What: monotonically incremented each time the FX DDA accumulator fires.
 * Replaces the former stateless n = seq_elapsedPpqTicks / ticks for computing
 * fwd/rev/pip/rnd step indices. Why: with fractional tick intervals, integer
 * division of the master clock no longer produces the correct step count.
 * Input: incremented in seq_fxClockTick(). Reset: seq_setStepIndexToStart().
 * Output: used to compute the FX step index. RAM: 4 bytes ISR-static SRAM1.
 */
static uint32_t seq_fxStepCounter = 0u;

/*
 * Effect automation reset latch (S081 §5.3, R13).
 *
 * The former owner-track mask and its seq_setEffectAutomationTracks() publisher
 * were removed with the FX step-marker machinery; only the reset latch remains.
 */
static volatile uint8_t seq_effectAutomationReset = 0u;

/* Publish one FX step while preserving a pending reset marker. */
static void seq_fxPublishStep(uint8_t index)
{
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    seq_fxEvent = (uint8_t)((seq_fxEvent & SEQ_FX_EVENT_RESET) |
                            SEQ_FX_EVENT_STEP |
                            (index & SEQ_FX_EVENT_INDEX));
    __set_PRIMASK(primask);
}

/* Publish a foreground reset for the next FX boundary and Effect overlays. */
static void seq_fxPublishReset(void)
{
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    seq_fxEvent = SEQ_FX_EVENT_RESET;
    /* The same boundary ends every Effect Pattern overlay and `fxm`. */
    seq_effectAutomationReset = 1u;
    __set_PRIMASK(primask);
}

/*
 * Clear Effect track overlays without touching the FX position (S081 R13).
 *
 * What: sets only seq_effectAutomationReset under PRIMASK. The next drain calls
 * effects_automationReset() (overlays, fxm override, menuHold). seq_fxEvent is
 * not touched. Why: R13 says a re-align clears track overlays, but the FX
 * sequence position must survive (seq_fxRealignToMasterClock() realigns it).
 * Affiliates: seq_drainPendingAutomation(), effects_automationReset().
 */
static void seq_requestEffectOverlayReset(void)
{
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    seq_effectAutomationReset = 1u;
    __set_PRIMASK(primask);
}

uint8_t seq_fxTakeEvent(void)
{
    uint8_t event;
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    event = seq_fxEvent;
    seq_fxEvent = 0u;
    __set_PRIMASK(primask);
    return event;
}

/*
 * Removed: legacy roll state (S081 §3.2, A3-5a).
 *
 * What: seq_rollRate and seq_rollState were the PAR_ROLL-driven performance roll
 * mechanism from the pre-S079 era. They have no remaining callers. Removing them
 * releases 2 B SRAM1. Affiliates: seq_setRoll (removed), seq_setRollRate
 * (removed), the legacy roll block in seq_advanceTrackStep (removed).
 */

static int16_t seq_stepIndex[NUM_TRACKS]; /**< fixed 0..15 track cursors; -1 before the next trigger */

/*
 * Per-track Q8.8 DDA tick accumulator (S078 §2.4.1).
 *
 * What: one uint16_t per track holding the fractional tick remainder. Every
 * PPQ tick adds 256 (1.0 in Q8.8); when the accumulator meets or exceeds the
 * track's Q8.8 interval, a step advance fires and the interval is subtracted.
 * Why: replaces the global SEQ_INTERNAL_TICKS_PER_DEFAULT_STEP modulo test,
 * allowing each track to advance at its own rate from the 128-position log
 * curve. Inputs: incremented by seq_processSchedulerTick(). Reset/seeded by
 * seq_setStepIndexToStart(), seq_realignActivePatternToMasterClock(),
 * seq_realignTrackToMasterClock(). Outputs: gates calls to
 * seq_advanceTrackStep(). RAM: 14 bytes ISR-static SRAM1 (7 × uint16_t).
 * Affiliates: stepScale_ticksQ8(), pat_scene_region_t::track_scale.
 */
static uint16_t seq_trackAccumulator[NUM_TRACKS];

/*
 * Per-track shuffle delay counters (S078 §3.2).
 *
 * What: seq_trackShuffleDelay[track] counts down the remaining PPQ ticks
 * before a shuffle-deferred step fires. seq_trackShufflePending[track] is
 * nonzero when a step trigger has been deferred and is waiting for its delay
 * to expire. seq_trackShuffleVel/Note hold the captured trigger payload so
 * step resolution happens at the original step, not at fire time. Why: shuffle
 * delays odd-indexed steps (0-indexed 1, 3, 5, ...) by a fraction of the
 * 1/16th note interval. The delay is always based on 24 PPQ ticks (the 1/16th
 * grid at 96 PPQ), not the track's step scale. Inputs: set when
 * seq_advanceTrackStep() reaches an odd step with nonzero shuffle.
 * Decremented each PPQ tick. Outputs: the deferred trigger fires when the
 * counter reaches zero. RAM: 28 bytes ISR-static SRAM1 (4 × 7 × uint8_t).
 * Affiliates: pat_scene_region_t::track_shuffle, seq_triggerVoice().
 */
static uint8_t seq_trackShuffleDelay[NUM_TRACKS];
static uint8_t seq_trackShufflePending[NUM_TRACKS];
static uint8_t seq_trackShuffleVel[NUM_TRACKS];
static uint8_t seq_trackShuffleNote[NUM_TRACKS];

/*
 * Deferred-trigger step index and roll flag per track (S081 §5.8).
 *
 * What: when a trigger is deferred by shuffle or micro-timing,
 * seq_trackShuffleStep[track] records the step that produced it and
 * seq_trackShuffleRoll[track] records whether it was a roll step.
 * Why: at the deferred fire time, the step's automation entries must be
 * queued (seq_queueStepAutomations) and a roll step must start via
 * seq_rollBegin rather than a plain trigger. The existing deferral
 * state (vel, note, delay, pending) carries only the trigger data, not
 * the step context. Inputs: written by seq_advanceTrackStep at the
 * deferral point. Outputs: read by seq_fireDeferred at fire time.
 * RAM: +14 B SRAM1 (7 x u8 + 7 x u8). Affiliates:
 * seq_trackShuffleDelay/Pending/Vel/Note, seq_queueStepAutomations,
 * seq_rollBegin.
 */
static volatile uint8_t seq_trackShuffleStep[NUM_TRACKS];
static volatile uint8_t seq_trackShuffleRoll[NUM_TRACKS];

/*
 * Deferred-trigger mute mask (S081 §8.6, P1B item 7).
 *
 * What: bit N set means track N's pending deferred trigger fires WITHOUT
 * sounding: seq_fireDeferred() still queues that step's automation (and the
 * step reset is skipped, because the live hit already reset the voice), but
 * no voice trigger and no roll start. Why: since A1-3, a deferred step's
 * automation is queued only when its trigger fires. Dropping the deferral
 * (the P1 cancel) lost the step's automation for the pass. Inputs: set by
 * seq_cancelPendingTrigger() under PRIMASK; cleared by seq_fireDeferred()
 * and whenever a new deferral is stored. Outputs: read by seq_fireDeferred
 * (TIM3). RAM: 1 B. Affiliates: seq_trackShufflePending[],
 * seq_trackShuffleStep[].
 */
static volatile uint8_t seq_trackShuffleMuteMask;

/*
 * Per-track repeat count for X:Y conditions (S079 §3.2).
 *
 * What: increments when a track's step index wraps (forward: len-1->0,
 * reverse: 0->len-1, pip: at each boundary reversal, random: once per len
 * steps drawn). Why: X:Y conditions need a modulo counter. Reset to 0 on
 * transport/pattern reset (seq_setStepIndexToStart) and Scene change
 * (seq_selectActivePattern). Once modes never wrap, so the counter stays at
 * 0. Inputs: play-mode wrap logic in seq_advanceTrackStep(). Outputs:
 * seq_evaluateStepCondition() reads this. RAM: 7 B SRAM1 static. Affiliates:
 * seq_setStepIndexToStart(), seq_selectActivePattern().
 */
static uint8_t seq_trackRepeatCount[NUM_TRACKS];

/*
 * Per-track free step counter for the random play mode (S079 §3.2).
 *
 * What: counts steps drawn in random play mode; when it reaches the effective
 * length it resets and seq_trackRepeatCount increments. Why: random mode never
 * "wraps" a cursor, so a free counter is the only way to advance the X:Y
 * repeat count once per traversal. RAM: 7 B SRAM1 static.
 */
static uint8_t seq_trackRndStepCount[NUM_TRACKS];

/*
 * Per-track last-condition-fired flag for lst/!ls chaining (S079 §3.4).
 *
 * What: set to 1 when the last evaluated conditional step (condition 1..36)
 * fired, 0 when it was suppressed. Default 0 (not fired). Updated by
 * seq_evaluateStepCondition() for any step with condition != 0. Why: lst/!ls
 * conditions chain off the most recent conditional step on this track.
 * Inputs: seq_evaluateStepCondition(). Outputs: condition indices 23/24 read
 * this. RAM: 7 B SRAM1 static. Affiliates: seq_setStepIndexToStart().
 */
static uint8_t seq_trackLastConditionFired[NUM_TRACKS];

/*
 * Fill-mode active flag (S079 §3.3).
 *
 * What: nonzero while the PERF button has been held past the double-click
 * timeout. Why: fil/!fl conditions read this flag. Written by
 * buttonHandler_tick() foreground PERF hold logic; read by the TIM3 ISR. A
 * single-byte store/load is atomic on Cortex-M7. RAM: 1 B SRAM1 static.
 * Affiliates: buttonHandler.c PERF press/release.
 */
uint8_t seq_fillActive;

/*
 * Looper state (S080 B3/C2, rewritten S080 P2 Defects 7/8/14).
 *
 * What: seq_loopActive is the ISR gate (see sequencer.h). seq_loopTrackMask
 * selects the looping tracks; seq_loopDivision/seq_loopModifier set the loop
 * length. For every looped track, seq_loopAnchor[] is the master-clock tick
 * (seq_elapsedPpqTicks units) at which its loop window starts, and
 * seq_loopStartStep/Accum/PlayState[] are the track's step cursor, Q8.8 DDA
 * accumulator and play-state bits just before that tick's DDA advance (the
 * snapshot). The ISR restores the snapshot whenever (elapsed - anchor) is a
 * positive multiple of the loop length, before the DDA advance.
 * Why anchors, not a shared tick counter: wraps always land on the master-
 * clock grid of the anchor, so a division/modifier change cannot shift a
 * track off the grid (P1 wrapped at once when the counter already exceeded a
 * shorter length), a track added mid-loop wraps on its own window instead of
 * another track's, and a scene change can keep the anchor while recomputing
 * the snapshot for the incoming scene (Defect 14). Resuming projects the
 * snapshot forward from the anchor, so a released track lands where it would
 * have been even when its position was never the canonical master-clock
 * position (Morph/automation scale changes, boundary commits, once modes).
 * Inputs: seq_loopEngage()/UpdateMask()/Disengage()/UpdateDivision(), scene
 * change refresh, transport reset rebase. Outputs: seq_loopProcessTick() wraps.
 * Concurrency: every foreground writer holds PRIMASK while it publishes; the
 * ISR reads only while seq_loopActive is set. RAM: 4 + 7x2 + 7x2 + 7 + 7x4 =
 * 67 B SRAM1 static, runtime-only (P1: 34 B). Affiliates:
 * seq_evaluateStepCondition() (lop/!lp), seq_processSchedulerTick().
 */
uint8_t seq_loopActive = 0u;
static uint8_t  seq_loopTrackMask = 0u;
static uint8_t  seq_loopDivision  = 0u;
static uint8_t  seq_loopModifier  = 0u;
static int16_t  seq_loopStartStep[NUM_TRACKS];
static uint16_t seq_loopStartAccum[NUM_TRACKS];
static uint8_t  seq_loopStartPlayState[NUM_TRACKS];
static uint32_t seq_loopAnchor[NUM_TRACKS];

/*
 * Transport gate (defined with the runtime state below). Declared here so the
 * S080 P2 scene-change helpers can read it before its definition.
 */
extern uint8_t seq_running;

/*
 * S080 F7: scene change deferral / queue state.
 *
 * What: seq_pendingSceneChange arms a direct (SEQ press) scene change that must
 * wait for a musical boundary; seq_pendingSceneQueue arms a queued (SHIFT+SEQ)
 * change. seq_pendingSceneIndex / seq_pendingQueueScene are the targets and
 * seq_pendingSceneDiv / seq_pendingQueueDiv the selected divisions (0 = instant;
 * scd 1..9 and qcd 1..17 index their own option lists, S080 P2: converted by
 * seq_sceneChangeDivUnits() and seq_queueChangeDivUnits() respectively).
 * seq_sceneFireFlag is the ISR -> foreground
 * signal that the boundary was reached. Why: non-instant scd/qcd changes are
 * detected in the TIM3 ISR (sample-accurate boundary) but executed in the
 * foreground (DSP apply, LED refresh, optional reload). RAM: 7 B SRAM1 static.
 * Affiliates: seq_handleMasterBoundary() (F8), the foreground executor
 * (menu_serviceDeferredSceneChange()).
 */
static uint8_t seq_pendingSceneChange = 0u;
static uint8_t seq_pendingSceneIndex  = 0u;
static uint8_t seq_pendingSceneDiv    = 0u;
static uint8_t seq_pendingSceneQueue  = 0u;
static uint8_t seq_pendingQueueScene  = 0u;
static uint8_t seq_pendingQueueDiv    = 0u;
static uint8_t seq_sceneFireFlag      = 0u;

/*
 * Scene chain runtime state (S080 G7, P2).
 *
 * What: seq_chainBarCount / seq_chainRepCount count master-clock bar and
 * repetition boundaries since the active Scene was selected (saturating at
 * 255). seq_previousScene is the Scene active before the last real change
 * (0xFF = none; the >NN conditions). seq_chainStopped freezes every track
 * after a `stp` chain fire. seq_pendingChainFire marks the pending direct
 * change as chain-raised. Why: the TIM3 evaluator needs per-activation
 * counts; the foreground executor needs to tell a chain repeat from a real
 * change. Inputs: seq_chainServiceTick(), seq_selectActivePattern(),
 * seq_setRunning(), seq_chainRepeatSelf(). RAM: 5 B SRAM1, runtime-only.
 * Affiliates: seq_chainEvaluate(), menu_serviceDeferredSceneChange().
 */
static uint8_t seq_chainBarCount    = 0u;
static uint8_t seq_chainRepCount    = 0u;
static uint8_t seq_previousScene    = 0xFFu;
static uint8_t seq_chainStopped     = 0u;
static uint8_t seq_pendingChainFire = 0u;

/*
 * Origin of the next whole-Scene change (S083 Q9; RAM approved Q13).
 *
 * What: 1 while the pending or executing whole-Scene change came from a
 * received MIDI PC; seq_sendProgChg() then sends nothing and clears it.
 * Why: MIDI-originated changes are never echoed (plan 3.9 P), also when a
 * deferred change fires at the next boundary. Writers:
 * seq_setSceneChangeOrigin() (menu_perfModeSceneChangeFrom(), panel or
 * MIDI), seq_chainEvaluate() (a chain fire is LXR-generated: cleared),
 * seq_sendProgChg() (consumed). RAM: 1 B SRAM1.
 */
static uint8_t seq_sceneChangeFromMidi = 0u;

void seq_setSceneChangeOrigin(uint8_t origin)
{
	seq_sceneChangeFromMidi = (uint8_t)(origin == MIDI_ORIGIN_MIDI);
}

/*
 * Convert a scene change division to master 1/16-note units (S080 F8).
 * 0 = instant (returns 0), 1..7 = 1/8, 1/4, 1/2, 1 bar, 2, 3, 4 bars,
 * 8/9 = end of shortest/longest track (approximated by one bar until track-
 * length-aware boundaries are added).
 */
static uint16_t seq_sceneChangeDivUnits(uint8_t div)
{
    switch (div) {
    case 1u: return 2u;
    case 2u: return 4u;
    case 3u: return 8u;
    case 4u: return 16u;
    case 5u: return 32u;
    case 6u: return 48u;
    case 7u: return 64u;
    case 8u: /* end of shortest track (approx) */
    case 9u: /* end of longest track (approx)  */
        return 16u;
    default:
        return 0u;
    }
}

/*
 * Convert a queued scene change division (qcd) to master 1/16-note units
 * (S080 P2).
 *
 * What: 0 = instant (returns 0); 1..3 = 1/8, 1/4, 1/2 note; 4..12 = 1..9 bars;
 * 13 = 12 bars; 14 = 15 bars; 15 = 16 bars; 16/17 = end of shortest/longest
 * track (approximated by one bar, like scd). Why: the queued division has its
 * own, longer option list (5b..9b, 12b, 15b, 16b, user request) so a queued
 * change can land on long phrase boundaries; the direct scd list is unchanged.
 * The order must match sceneQueueDivNames[] (MenuText.h). Inputs: qcd value.
 * Output: units of the 16-bit master step clock (largest 256). Context: TIM3
 * ISR and foreground (pure). Accessors: seq_handleMasterBoundary(),
 * seq_queueSceneChange(), seq_cancelPendingDirectChange().
 */
static uint16_t seq_queueChangeDivUnits(uint8_t div)
{
    static const uint16_t units[18] = {
        0u,                         /* ins */
        2u, 4u, 8u,                 /* 1/8, 1/4, 1/2 */
        16u, 32u, 48u, 64u, 80u,    /* 1b .. 5b */
        96u, 112u, 128u, 144u,      /* 6b .. 9b */
        192u, 240u, 256u,           /* 12b, 15b, 16b */
        16u, 16u                    /* ens, enl (approx) */
    };

    return (div < 18u) ? units[div] : 0u;
}

/*
 * Arm a direct (SEQ) scene change at a musical division (S080 F7, P2).
 *
 * What: records the target and scd division and arms the change; the TIM3
 * ISR raises the fire flag at the boundary. A user request replaces any chain
 * fire that has not run yet, so the pending change is no longer marked as
 * chain-raised. Why: a manual switch cancels conditional changes (plan §4.5);
 * a stale chain marker would make the executor treat this user change as a
 * chain repeat. Inputs: scene_index, division (scd 1..9; 0 is executed
 * directly by the caller). Outputs: seq_pendingScene*, seq_pendingChainFire
 * cleared. Accessors: menu_perfModeSceneButtonPressed().
 */
void seq_requestSceneChange(uint8_t scene_index, uint8_t division)
{
    seq_pendingSceneIndex = scene_index;
    seq_pendingSceneDiv = division;
    seq_pendingChainFire = 0u;
    seq_pendingSceneChange = 1u;
}

/*
 * Arm a queued (SHIFT+SEQ) scene change (S080 F7, P2 Defect 1).
 *
 * What: records the target and qcd division and arms the queue. A queued
 * change at the instant division (qcd = ins, the power-on default) or while
 * the transport is stopped raises the fire flag at once instead of waiting for
 * an ISR boundary. Why: the instant division converts to 0 units
 * (seq_queueChangeDivUnits()), and the ISR boundary test requires
 * units != 0, so an instant queue never fired (Defect 1); a
 * stopped transport has no boundaries at all. Queuing again replaces the
 * pending target; queuing the playing Scene still fires the queue action.
 * Inputs: scene_index, division (qcd value). Outputs: seq_pendingQueue*,
 * seq_pendingSceneQueue, possibly seq_sceneFireFlag. Context: foreground.
 * Accessors: buttonHandler SHIFT+SEQ (PERF). Affiliates:
 * seq_handleMasterBoundary() (deferred fire), menu_serviceDeferredSceneChange().
 */
void seq_queueSceneChange(uint8_t scene_index, uint8_t division)
{
    seq_pendingQueueScene = scene_index;
    seq_pendingQueueDiv = division;
    seq_pendingSceneQueue = 1u;
    if (seq_queueChangeDivUnits(division) == 0u || !seq_running)
        seq_sceneFireFlag = 1u;
}

uint8_t seq_sceneChangeFirePending(void) { return seq_sceneFireFlag; }

uint8_t seq_pendingSceneIsQueue(void)
{
    return seq_pendingSceneQueue;
}

uint8_t seq_pendingSceneTarget(void)
{
    return seq_pendingSceneQueue ? seq_pendingQueueScene : seq_pendingSceneIndex;
}

/*
 * Queued (SHIFT+SEQ) target, independent of any direct change (S080 P2
 * Defect 10).
 *
 * Output: the queued Scene 0..15 while a queued change is armed, else 0xFF.
 * Why: the fast-blink owner must show only the queued Scene. Single-byte
 * reads of ISR-shared state. Accessor: menu_syncQueuedSceneLed().
 */
uint8_t seq_queuedSceneTarget(void)
{
    return seq_pendingSceneQueue ? seq_pendingQueueScene : 0xFFu;
}

/*
 * Clear every pending scene change (S080 F7, P2).
 *
 * What: drops the direct, queued and chain-fired pending changes and the fire
 * flag. Why: called by the foreground executor once it has consumed a fired
 * change, and by the transport-stop path. Inputs: none. Outputs: all pending
 * scene-change state zero. Accessors: menu_serviceDeferredSceneChange().
 * Affiliates: seq_cancelPendingDirectChange().
 */
void seq_clearPendingSceneChange(void)
{
    seq_pendingSceneChange = 0u;
    seq_pendingSceneQueue = 0u;
    seq_pendingChainFire = 0u;
    seq_sceneFireFlag = 0u;
}

/*
 * Cancel a pending direct or chain-fired change, keeping a queued one
 * (S080 P2).
 *
 * What: clears the deferred direct (scd) change and any chain fire. A
 * user-queued change survives; if it was armed, the fire flag is cleared and
 * the ISR re-raises it at the next qcd boundary (or at once for an instant
 * queue). Why: a manual scene switch cancels conditional changes (plan §4.5)
 * but only a transport stop cancels a queued change (plan §5.3). Inputs:
 * none. Outputs: seq_pendingSceneChange/seq_pendingChainFire/seq_sceneFireFlag
 * cleared; an instant queue re-arms its fire flag. Accessors:
 * menu_perfSceneChangeExecute(). Affiliates: seq_queueSceneChange().
 */
void seq_cancelPendingDirectChange(void)
{
    seq_pendingSceneChange = 0u;
    seq_pendingChainFire = 0u;
    seq_sceneFireFlag = 0u;
    if (seq_pendingSceneQueue &&
        (seq_queueChangeDivUnits(seq_pendingQueueDiv) == 0u || !seq_running))
        seq_sceneFireFlag = 1u;
}

/*
 * Report whether the pending change was raised by a scene chain (S080 G7).
 *
 * Output: 1 when the fired direct change came from seq_chainFire(), else 0.
 * Accessor: menu_serviceDeferredSceneChange() (repeat vs. full change).
 */
uint8_t seq_pendingSceneIsChain(void)
{
    return (uint8_t)(seq_pendingChainFire && !seq_pendingSceneQueue);
}

/*
 * Per-track step-roll playback state (S079 §4; time window S084 P1).
 *
 * What: seq_rollActive holds two flags. SEQ_ROLL_RUN: the roll's window is
 * open and retriggers fire. SEQ_ROLL_CARRY: the roll's step had fill 100,
 * so the track's next step may carry it (user Q4, 2026-10-24: a carry needs
 * the same rate and rhythm, and is refused when the previous roll step's
 * fill was below 100). CARRY set with RUN clear is "parked": the window has
 * closed and no hit fires, but seq_rollTickAccum keeps counting down to 0,
 * so a carry resumes on phase. Parking lasts until the track's next step
 * event (the carry-eligibility check in seq_advanceTrackStep()).
 * seq_rollTickAccum counts the 96-PPQ ticks to the next retrigger.
 * seq_rollWindow counts the master ticks left in which retriggers may fire:
 * the step interval x fill / 100 from the step's (possibly deferred) fire,
 * replaced by each carrying step and extended over a carrying step's
 * deferral (seq_advanceTrackStep()). seq_rollVelPos is the position in the
 * rhythm velocity cycle. seq_rollLastRate/Rhythm/Fill are the running
 * roll's parameters (carry test, window-end duck). seq_rollStepVel/Note are
 * the payload each retrigger fires with.
 * Why: S079-S083 counted hits per step (seq_rollRemaining). A carry added
 * slots - 1 hits and resumed from an accumulator frozen at ppq - 1, so every
 * carried step lost its boundary hit and started almost one roll period
 * late. Recorded rolls (stored as carried steps) fell to about one hit per
 * step, and the loss depended on the track's step scale (S084 P0 F2). A
 * time window keeps the rate on the master clock at any step scale and needs
 * no per-step count; a rate that does not divide the step (16t in a 1/16
 * step) is exact.
 * Inputs: seq_rollBegin() at a step's fire or deferred fire; the
 * carry-eligibility check and the deferred-carry extension in
 * seq_advanceTrackStep(); the F-R cut; seq_rollClearTrack().
 * Outputs: retriggers and the window-end duck from seq_processRollTicks().
 * Context: written and read in TIM3 only. RAM: 8 x 7 u8 + 7 u16 = 70 B
 * SRAM1 static (S084: +14 B window, -7 B remaining, -21 B duck arrays).
 * Affiliates: seq_rollClearTrack(), seq_rollCarries(), seq_rollBegin(),
 * seq_processRollTicks(), seq_rollComputeVelocity(),
 * instrumentManager_requestGateDuck().
 */
#define SEQ_ROLL_RUN    0x01u   /* window open: retriggers fire */
#define SEQ_ROLL_CARRY  0x02u   /* fill 100: the next step may carry it */
static uint8_t seq_rollTickAccum[NUM_TRACKS];
static uint8_t seq_rollVelPos[NUM_TRACKS];
static uint16_t seq_rollWindow[NUM_TRACKS];
static uint8_t seq_rollActive[NUM_TRACKS];
static uint8_t seq_rollLastRate[NUM_TRACKS];
static uint8_t seq_rollLastRhythm[NUM_TRACKS];
static uint8_t seq_rollLastFill[NUM_TRACKS];
static uint8_t seq_rollStepVel[NUM_TRACKS];
static uint8_t seq_rollStepNote[NUM_TRACKS];

/*
 * End one track's step roll and clear its runtime state (S084 P1).
 *
 * What: zeroes the track's roll flags, accumulator, rhythm position,
 * window and last-roll parameters, so no retrigger, duck or carry follows.
 * seq_rollStepVel/Note are left as they are (only read while a roll runs).
 * Why: the same ten-line clear was repeated at four sites (boot, per-track
 * Scene change and its choke partner, whole-Scene change, transport
 * reset), and each site had to change when the state changed. Inputs:
 * track 0..6 (callers guarantee the range). Outputs: the per-track roll
 * state. Context: TIM3, or the foreground with TIM3 masked (the existing
 * call sites). Accessors: seq_init(), seq_setTrackPlayedScene(),
 * seq_selectActivePattern(), seq_setStepIndexToStart(). Affiliates: the
 * step roll state block above. Live overlay rolls (seq_live*) are a
 * separate state and are never cleared here.
 */
static void seq_rollClearTrack(uint8_t track)
{
    seq_rollActive[track] = 0u;
    seq_rollTickAccum[track] = 0u;
    seq_rollVelPos[track] = 0u;
    seq_rollWindow[track] = 0u;
    seq_rollLastRate[track] = 0u;
    seq_rollLastRhythm[track] = 0u;
    seq_rollLastFill[track] = 0u;
}

/*
 * Live (PERF BAR overlay / MIDI note) roll and flam engine (S084 P2).
 *
 * What: one entry per track for a held overlay gesture, driven by the
 * 96-PPQ master tick. seq_liveMode: SEQ_LIVE_IDLE, SEQ_LIVE_ROLL (repeats
 * until stopped) or SEQ_LIVE_FLAM (one pending main hit). seq_liveRate: the
 * step-roll index 1..9 (seq_rollPpq()). seq_liveAccum: ticks to the next
 * hit. seq_livePos: the roll's velocity-rhythm position. seq_liveNote/Vel:
 * the internal pitch and base velocity of the hits. seq_liveRhythm: the
 * packed rhythm byte of the active overlay (`pv<`/`pv>`), one for all
 * tracks, because every held entry follows the one overlay state.
 * Mailbox to the foreground: seq_liveHitCount counts roll hits fired since
 * the last take (saturating at 255), seq_liveFlamMask has bit N set when
 * track N's flam main fired, and seq_liveHitUs is the TIM2 µs stamp of the
 * latest hit.
 * Why: S080-S083 timed overlay repeats with a 1 ms foreground timer from
 * the menu tempo PAR_BPM, with integer division. Rolls ran 2-7 % fast,
 * ignored external sync, and jittered with render load, while recorded rolls
 * play from this master tick, so live and recorded rolls never matched
 * (S084 P0 F4). Running both on one clock makes them identical. LiveRecord
 * never runs in TIM3, so hits are published here and reported by the
 * foreground (buttonHandler_overlayDrain()) with their arrival age.
 * Inputs: the seq_live* API below (foreground, PRIMASK sections) and
 * seq_processLiveTicks() (TIM3). Outputs: seq_triggerVoice() hits and the
 * mailbox. Not cleared by Scene changes or transport resets: a held gesture
 * belongs to the panel or MIDI, not to the Pattern. RAM: 6 x 7 u8 + 1 +
 * 7 u8 + 1 + 7 u32 = 79 B SRAM1 static. Affiliates: buttonHandler overlay
 * (start, re-configure, drain, end), seq_rollPpq(),
 * seq_rollRhythmVelocity(), LiveRecord (through buttonHandler).
 */
#define SEQ_LIVE_IDLE  0u
#define SEQ_LIVE_ROLL  1u
#define SEQ_LIVE_FLAM  2u
static volatile uint8_t  seq_liveMode[NUM_TRACKS];
static uint8_t           seq_liveRate[NUM_TRACKS];
static uint8_t           seq_liveAccum[NUM_TRACKS];
static uint8_t           seq_livePos[NUM_TRACKS];
static uint8_t           seq_liveNote[NUM_TRACKS];
static uint8_t           seq_liveVel[NUM_TRACKS];
static volatile uint8_t  seq_liveRhythm;
static volatile uint8_t  seq_liveHitCount[NUM_TRACKS];
static volatile uint8_t  seq_liveFlamMask;
static volatile uint32_t seq_liveHitUs[NUM_TRACKS];

/*
 * Per-track play state (S078 §4.4).
 *
 * What: one byte per track packing runtime play mode state. Bit 0 is the
 * "stopped" flag for once modes. Bit 1 is the pip direction (0 = forward,
 * 1 = reverse); the earlier pip position in the two-length cycle is derived
 * from seq_stepIndex[] and this direction bit, so no cycle counter is needed.
 * Why: the DDA owns WHEN a step fires; this state owns WHERE the step index
 * goes. Inputs: set by seq_advanceTrackStep() direction logic, cleared by
 * retrigger events. Outputs: gates step advance for stopped once-mode tracks;
 * controls pip direction toggling. RAM: 7 bytes ISR-static SRAM1. Affiliates:
 * seq_setStepIndexToStart(), seq_setRunning(), seq_selectActivePattern(),
 * seq_setTrackPlayedScene().
 */
static uint8_t seq_trackPlayState[NUM_TRACKS];
#define SEQ_PLAY_STATE_STOPPED  (1u << 0)
#define SEQ_PLAY_STATE_PIP_REV  (1u << 1)

/*
 * Effective (morphed) per-track timing values (S078 §5.4).
 *
 * What: the values the sequencer actually plays for each track's loop length,
 * step-scale CC, and shuffle amount: the Pattern Normal value interpolated
 * against the Scene Morph endpoint at the associated voice's retained Morph
 * amount, or a step-automation overlay. Why: the Morph system must change
 * playback without writing the retained pat_scene_region_t Normal values or
 * marking the Pattern dirty, so the sequencer reads this cache instead of the
 * region. Inputs: refreshed by seq_refreshTrackEffectiveParams() from the
 * foreground Morph worker, the foreground Scene/Pattern change paths, and the
 * transport/Pattern reset path. Outputs: read by seq_advanceTrackStep(),
 * seq_processSchedulerTick(), and the realign helpers. RAM: 21 bytes ISR-static
 * SRAM1 (3 x uint8_t[7]). Affiliates: presetMorph_getTrackEffective*(),
 * presetMorph_trackTick().
 */
static uint8_t seq_effectiveTrackLength[NUM_TRACKS];
static uint8_t seq_effectiveTrackScale[NUM_TRACKS];
static uint8_t seq_effectiveTrackShuffle[NUM_TRACKS];

static uint16_t seq_tempo = 120;			/**< seq speed in bpm*/

static uint32_t	seq_lastTick = 0;			/**< stores the time the last step change occured*/
static float	seq_deltaT;					/**< time in [ms] until the next step
 	 	 	 	 	 	 	 	 	 	 	 1000ms = 1 sec
 	 	 	 	 	 	 	 	 	 	 	 1 min = 60 sec*/
/*
 * External-clock ticks still to interpolate (S084 P0 A1).
 *
 * What: the 96-PPQ scheduler ticks of the current external clock period
 * (MIDI clock: 4 per clock; pulse input: stepSize x 3 per pulse) that have
 * not been processed yet. A clock or pulse processes its first tick at once
 * (seq_extClockTicks()); seq_tick()'s tempo timer, whose period follows the
 * measured external tempo (seq_tempo), processes the rest one by one; the
 * next clock first catches up any tick still owed, so no clock period ever
 * runs more or fewer ticks than before. Why: the scheduler used to run all
 * ticks of a clock back to back, so every hit inside one clock period fired
 * at the same instant: live rolls of 64/128 bunched or lost hits under MIDI
 * clock, and under pulse sync (24 ticks per pulse at 4 PPQ) every roll,
 * shuffle and micro-timing finer than the pulse collapsed. Inputs: written
 * in TIM3 (clock/pulse handlers, seq_tick()); cleared by transport
 * start/stop/continue and the external reset (foreground or TIM3, a single
 * byte store). Output: interpolated seq_processSchedulerTick() calls. RAM:
 * 1 B SRAM1 static. Affiliates: seq_resetDeltaAndTick(),
 * seq_triggerNextMasterStep(), seq_tick(), seq_calcDeltaT().
 */
static volatile uint8_t seq_extTicksOwed = 0u;

uint8_t seq_delayedSyncStepFlag = 0;		//normally sync steps will only be advanced by external midi clocks in ext. sync mode
											//if the shuffle needs a delayed sync step, it is indicated here.

uint8_t seq_isSyncExternal = SEQ_EXT_SYNC_OFF;
static uint8_t seq_autoSyncActiveSource = SEQ_EXT_SYNC_OFF;
static uint32_t seq_autoSyncLastUs = 0;
uint8_t seq_lastMasterStep[NUM_TRACKS];		//keeps track of the last triggered master sync step of each track


static uint8_t seq_SomModeActive = 0;

static uint8_t seq_mutedTracks=0;			/**< indicate which tracks are muted */
uint8_t seq_running = 0;					/**< 1 if running, 0 if stopped*/

uint8_t seq_activePattern = 0;				/**< the currently playing pattern*/
uint8_t seq_pendingPattern = 0;				/**< next pattern to play*/

/*
 * Per-track played-Scene map (S077 P2 §3.1).
 *
 * What: one resident Scene byte per track, the Scene playback reads for that
 * track's sequence, instrument, and per-voice settings. Why: per-track Scene
 * playback lets individual tracks read a Scene other than the global active
 * one. Inputs: seq_init(), seq_selectActivePattern(),
 * seq_alignActivePatternToScene(), seq_handleMasterBoundary(), and the PERF
 * hold-VOICE+press-SEQ gesture through seq_setTrackPlayedScene(). Output:
 * playback-facing state; every entry equals seq_activePattern when no override
 * is set. RAM: +7 bytes SRAM1 (allocated in S067).
 */
uint8_t seq_perTrackPattern[NUM_TRACKS];

/*
 * Per-track playback override flag (S077 P2 §3.1).
 *
 * What: nonzero when any entry in seq_perTrackPattern[] differs from
 * seq_activePattern. Why: gates the fast path - when zero, all existing code
 * paths that reference seq_activePattern are correct as-is and no per-track
 * branching is needed. Recomputed by seq_recomputePerTrackActive() whenever
 * seq_perTrackPattern[] changes. Inputs: seq_perTrackPattern[],
 * seq_activePattern. Output: 0 or 1. RAM: +1 byte SRAM1. Owner: Sequencer.
 * Lifetime: static. Affiliates: seq_setTrackPlayedScene(),
 * seq_clearPerTrackOverrides(), seq_selectActivePattern(),
 * seq_alignActivePatternToScene(), seq_handleMasterBoundary().
 */
uint8_t seq_perTrackActive = 0;

/*
 * Recompute the seq_perTrackActive convenience flag (S077 P2 §1.2).
 *
 * What: walks seq_perTrackPattern[] and sets seq_perTrackActive to 1 if any
 * entry differs from seq_activePattern, 0 otherwise. Why: centralises the
 * derivation so every mutation site calls one function rather than duplicating
 * the scan. Inputs: seq_perTrackPattern[NUM_TRACKS], seq_activePattern.
 * Output: seq_perTrackActive (0 or 1). Caller: seq_setTrackPlayedScene(),
 * seq_clearPerTrackOverrides().
 */
static void seq_recomputePerTrackActive(void)
{
    uint8_t track;

    for (track = 0u; track < NUM_TRACKS; track++) {
        if (seq_perTrackPattern[track] != seq_activePattern) {
            seq_perTrackActive = 1u;
            return;
        }
    }
    seq_perTrackActive = 0u;
}

uint8_t seq_recordActive = 0;				/**< set to 1 to activate the reording mode*/

/* seq_eraseActive removed in S081 G-5 (replaced by the per-track erase masks). */

uint8_t seq_quantisation = QUANT_16;

uint8_t seq_barCounter;						/**< counts the absolute position in bars since the seq was started */

static uint8_t seq_loadPendigFlag = 0;

// --AS Allow it to be configured whether it keeps track of bar position in the song for
// the purpose of pattern changes
uint8_t seq_resetBarOnPatternChange=0;

uint8_t seq_newPatternAvailable = 0; //indicate that a new pattern has loaded in the background and we should switch

/*
 * TIM3-to-foreground automation handoff (+514 B SRAM1).
 *
 * What: 128 four-byte identity/payload records and two publication bytes. Why:
 * seq_advanceTrackStep() must only decode the bounded Pattern block and queue
 * raw values; descriptor validation/runtime writes belong to foreground code.
 * Lifetime: static until seq_drainPendingAutomation() consumes the queue.
 * Owner: Sequencer. Affiliate: PatternTrace is optional diagnostics only and
 * owns a separate 32-record, 256-byte DEV ring.
 */
typedef struct {
    uint16_t identity;
    uint16_t payload;
} seq_pending_automation_t;

_Static_assert(sizeof(seq_pending_automation_t) == 4u,
               "pending automation record must remain four bytes");
#define SEQ_PENDING_TYPE_AUTOMATION_BIT (1u << 10u)
#define SEQ_PENDING_STEP_ID_MASK        0x03FFu
static volatile seq_pending_automation_t
    seq_pending_automation[SEQ_PENDING_BUF_COUNT];
static volatile uint8_t seq_pending_automation_count = 0u;
static volatile uint8_t seq_pending_automation_drain = 0u;

/*
 * Per-voice automation restore bitmap (+48 B static SRAM1).
 *
 * What: six 64-bit bitmaps, one bit per descriptor-local parameter image.
 * Why: foreground automation writes are transient runtime overlays; the next
 * trigger must restore only the descriptor images changed since that trigger.
 * Lifetime: static until the matching trigger restores the set bits or a
 * transport/pattern reset clears them. Owner: Sequencer. Affiliate:
 * seq_drainPendingAutomation() and seq_restoreAutomatedParameters().
 * S075 F3: the bitmap is also the "automation holds this value" record that
 * enforces "automation always wins": the Morph sweep, the per-parameter menu
 * apply and synchronous voice applies consult it through
 * seq_automationHoldsParameter() and never overwrite a held runtime value
 * before the trigger.
 */
static uint64_t seq_automation_dirty[INSTRUMENT_SLOT_COUNT];

/*
 * Per-slot Instrument menu-override bitmap (S081 §5.1, R12).
 *
 * What: 6 x uint64_t, one per Instrument slot (0..5). Bit N is set
 * when the user has edited Instrument parameter N from the menu and the
 * edit should suppress that parameter's step automation until the next
 * trigger of that voice (or a transport/Scene change). Why: R12 says
 * "a menu edit of an automated Instrument parameter sounds at once and
 * suppresses that parameter's automation until the voice's next
 * trigger." This replaces the S075 F3 rule where automation always beat
 * the menu. Inputs: set by seq_menuOverrideSet() (called from
 * menu_cellCommitValue). Cleared by seq_restoreAutomatedParameters()
 * (on trigger), and by seq_setStepIndexToStart / seq_selectActivePattern
 * / seq_alignActivePatternToScene / seq_setTrackPlayedScene (on
 * transport/Scene events). Outputs: read by seq_drainPendingAutomation
 * to skip entries. RAM: +48 B SRAM1.
 * Affiliates: seq_automation_dirty[], presetMorph_writeRuntimeBase().
 */
static uint64_t seq_menuOverride[INSTRUMENT_SLOT_COUNT];

/*
 * Automation ramp state (S081 P2, rmp; +669 B SRAM1, +2 B DEV-only).
 *
 * What: seq_ramps[64] are the running linear ramps. Each slot holds the
 * track and Instrument target, the from/to 7-bit values, the last value the
 * service wrote (so a pass writes only when the rounded value changes; user
 * decision, +128 B), and the start/end times in 1/16 master ticks (low 16
 * bits, wrap-safe differences; a ramp spans at most one step plus its
 * deferral, far below 2048 ticks).
 * seq_rampCtx[7] is TIM3's per-track hand-off:
 *  - a: the step whose automation applies (A) and the "stepped" flag; the
 *    foreground ends the track's ramps when it consumes the flag;
 *  - b: the predicted next played step (B) + 1, 0 when no ramp may start;
 *  - endQ4: B's predicted apply time.
 * seq_rampBlockDiv paces the writes to every third DSP block.
 * Why: plan §5. Free slots are id 0, so zero-initialised storage starts
 * empty; a zero context has b = 0 (no B). Lifetime: static. Writers: TIM3
 * (seq_rampStepEvent, seq_fireDeferred, break hooks) for the contexts;
 * foreground (drain, ramp service, restore) for the slots. The foreground
 * reads a context under PRIMASK. RAM: 640 + 28 + 1 B every build; 2 B
 * DEV_MODE_LOGGING (slot exhaustion count). Affiliates:
 * seq_rampStepEvent(), seq_rampStartIfPaired(), seq_rampService(),
 * seq_restoreAutomatedParameters().
 */
typedef struct {
    uint16_t id;        /* 0 = free; 1 + ((track << 9) | target) */
    uint8_t  from;
    uint8_t  to;
    uint8_t  last;      /* last value written (start: from, written by drain) */
    uint16_t startQ4;   /* 1/16 master ticks, low 16 bits */
    uint16_t endQ4;
} seq_ramp_t;

/* 9 bytes of fields + 1 byte halfword padding (S081 P2, approved +128 B). */
_Static_assert(sizeof(seq_ramp_t) == 10u, "ramp slot must stay 10 bytes");

typedef struct {
    uint8_t  a;         /* bits 0..6 A step, bit 7 = stepped (unconsumed) */
    uint8_t  b;         /* B step + 1; 0 = no ramp target */
    uint16_t endQ4;     /* B's predicted apply time */
} seq_rampCtx_t;

_Static_assert(sizeof(seq_rampCtx_t) == 4u, "ramp context must stay 4 bytes");

#define SEQ_RAMP_SLOTS          64u
#define SEQ_RAMP_STEPPED        0x80u
#define SEQ_RAMP_BLOCK_DIV      3u     /* 3 x 0.73 ms DSP blocks = ~2.2 ms */

static seq_ramp_t seq_ramps[SEQ_RAMP_SLOTS];
static volatile seq_rampCtx_t seq_rampCtx[NUM_TRACKS];
static uint8_t seq_rampBlockDiv;
#if DEV_MODE_LOGGING
static uint16_t seq_rampNoSlotDrops;
#endif
/*
 * S081 P1 live-record service state (sequencer side, §19.1).
 *
 * These fields are the Sequencer half of the LiveRecord interface:
 *
 * seq_recRing/Head/Tail  step-boundary ring: a 16-entry uint16_t ring packing
 *   step (7 bits), track (3), fired (1). TIM3 writes; LiveRecord drains via
 *   seq_takeRecBoundary(). One producer, one consumer, byte cursors: no PRIMASK.
 *
 * seq_recTrack/seq_recWriting  the published recording target. Written by
 *   LiveRecord through seq_setRecordTarget(); read by TIM3. Single-byte stores.
 *
 * seq_echoStep[track]  step (0..127) of a recorded trigger ahead of the
 *   playhead that must be skipped once, or -1. TIM3 clears it on consumption;
 *   seq_setStepIndexToStart()/epoch sites/length-run-mode changes expire it.
 *
 * seq_liveHoldMask  bit N set while track N has an overlay trigger held with
 *   recording active; TIM3 skips that track's stored triggers (F-P).
 *
 * seq_eraseSpecialsMask / seq_eraseFullMask  per-track live-erase masks:
 *   specials clears bit 15 + specials (keep automation); full clears everything.
 *
 * seq_ppqStampUs/seq_ppqPeriodUs  timebase_tim2Now() at the head of the last
 *   scheduler tick and the interval since the previous one (sub-tick placement).
 *
 * seq_playbackEpoch  incremented by every Scene change, realign, transport
 *   start/stop and pattern load; LiveRecord polls it for take boundaries.
 *
 * seq_filterLatch/seq_filterErase/seq_filterEraseTrack  the registered
 *   LiveRecord latch/erase bitmaps the drain reads (NULL when recording is off).
 *
 * RAM: about 62 B SRAM1. TIM3-visible bytes are single-byte or PRIMASK-published.
 */
/*
 * TIM3 <-> foreground live-record state (S081 C-1..C-7, P1B item 24).
 *
 * What: every byte or word written by one context and read by the other is
 * volatile. The boundary ring is single-producer (TIM3 writes the entry,
 * then the head) and single-consumer (foreground reads the head once, then
 * the entry, then advances the tail). Why: with LTO the foreground drain can
 * be inlined into a loop; without volatile the compiler may keep
 * seq_recRingHead in a register across iterations, or load the entry before
 * the head check. Cortex-M7 byte and halfword accesses are single-copy
 * atomic, and volatile ordering is sufficient on this single core (the
 * seq_pending_automation precedent). Affiliates: seq_takeRecBoundary(), the
 * ring push in seq_advanceTrackStep(), liveRec_service().
 */
static volatile uint16_t seq_recRing[16];
static volatile uint8_t  seq_recRingHead;
static volatile uint8_t  seq_recRingTail;
/*
 * Step-boundary ring overflow count (S081 H-3, P1C item 13). DEV only.
 *
 * What: the number of ring entries dropped because the 16-entry ring was
 * full; every drop also records a PatternTrace 'W' event with cause 5
 * (ring full) and (track << 8) | step. Why: a dropped entry merges two
 * steps for LiveRecord's step-end table and loses a roll-span write; the
 * P1B log claimed this was counted, but the push dropped silently.
 * RAM: 2 B DEV-only (logging-allocation policy). Context: written in TIM3.
 * Affiliates: the ring push in the step-advance path, liveRec_drop()
 * causes 1-4, decode_devlogs.py.
 */
#if DEV_MODE_LOGGING
static volatile uint16_t seq_recRingDrops;
#endif

static volatile uint8_t  seq_recTrack;
static volatile uint8_t  seq_recWriting;
static volatile int8_t   seq_echoStep[NUM_TRACKS];
static volatile uint8_t  seq_eraseSpecialsMask;
static volatile uint8_t  seq_eraseFullMask;
static volatile uint8_t  seq_liveHoldMask;
static volatile uint32_t seq_ppqStampUs;
static volatile uint32_t seq_ppqPeriodUs;
static volatile uint8_t  seq_playbackEpoch;
static const uint8_t *seq_filterLatch;
static const uint8_t *seq_filterErase;
static uint8_t  seq_filterEraseTrack;

/*
 * Echo-mark deadline per track (S081 §8.6, P1B items 6/25).
 *
 * What: low 16 bits of the master tick after which a pending echo mark
 * (seq_echoStep[track]) is void. Set with the mark by
 * seq_suppressStepOnce(); TIM3 clears an expired mark before testing it.
 * Why: a mark must only silence the one replay it was set for. If the
 * playhead never reaches the marked step in that pass (run-mode jump,
 * once-mode park, play-mode or length change, rnd), a stale mark would
 * otherwise silence a later, legitimately recorded hit. A deadline covers
 * every such case without wiring each event (it replaces the unwired C-10
 * play-mode expiry). Inputs: written by seq_suppressStepOnce (foreground,
 * PRIMASK). Output: read in seq_advanceTrackStep (TIM3). RAM: 14 B SRAM1.
 * Affiliates: seq_echoStep[], seq_elapsedPpqTicks.
 */
static volatile uint16_t seq_echoDeadline[NUM_TRACKS];

/*
 * Scene epoch (S081 §9.2 / F11, P1B item 10).
 *
 * What: incremented only when the Pattern a track plays from changes: a
 * direct/queued/chain Scene change, a per-track Scene reassignment, a
 * Scene/Bank load alignment. seq_playbackEpoch is still incremented at
 * every playhead discontinuity (those plus transport start/stop, PC reset
 * and realign). Why: LiveRecord starts a new take (latches reset, target
 * re-evaluated) only on a Scene change (F11). A transport start or stop is
 * only a position break: it must not wipe latches set while stopped, nor
 * end a held live erase ("playback starts with live erase enabled"). Two
 * counters let LiveRecord tell the cases apart. Inputs: the three Scene
 * sites. Output: seq_getSceneEpoch(). RAM: 1 B.
 * Accessors: liveRec_service(). Affiliates: seq_playbackEpoch.
 */
static volatile uint8_t seq_sceneEpoch;

/*
 * Ring entry packing (S081 §19.1 item 1; S081 P3 adds the jump bit).
 *
 * step 7 bits, track 3, fired 1, jump 1 (bits 0..11 of 16). fired: the step
 * holds a trigger and its condition passed, known at its start. jump: the
 * entry was pushed by a looper position jump (wrap mid-step, resume), not by
 * a step advance; the step it begins is a partial replay whose trigger did
 * not fire on this pass (plan §3.9). Affiliates: seq_recRingPush(),
 * seq_takeRecBoundary(), liveRec_drainRing().
 */
#define SEQ_REC_RING_STEP_MASK   0x007Fu
#define SEQ_REC_RING_TRACK_MASK  0x0380u
#define SEQ_REC_RING_TRACK_SHIFT 7u
#define SEQ_REC_RING_FIRED_BIT   0x0400u
#define SEQ_REC_RING_JUMP_BIT    0x0800u

/*
 * Pop one step-boundary entry from the ring (S081 §19.1, C-1b).
 *
 * What: if the ring is non-empty, copies the tail entry into the output struct
 * and advances the tail. Returns 1 on success, 0 if empty. Why: single-producer
 * (TIM3) single-consumer (foreground) with byte head/tail needs no PRIMASK.
 * S081 P3: also reports whether a looper position jump (not a step advance)
 * pushed the entry.
 */
uint8_t seq_takeRecBoundary(seq_rec_boundary_t *out)
{
    uint16_t e;
    uint8_t head;

    if (!out)
        return 0u;
    /* P1B item 24: read the head once so the check and the tail advance agree. */
    head = seq_recRingHead;
    if (seq_recRingTail == head)
        return 0u;
    e = seq_recRing[seq_recRingTail];
    seq_recRingTail = (uint8_t)((seq_recRingTail + 1u) & 0x0Fu);
    out->step = (uint8_t)(e & SEQ_REC_RING_STEP_MASK);
    out->track = (uint8_t)((e & SEQ_REC_RING_TRACK_MASK) >>
                           SEQ_REC_RING_TRACK_SHIFT);
    out->fired = (uint8_t)((e & SEQ_REC_RING_FIRED_BIT) ? 1u : 0u);
    /* S081 P3: a looper position jump began this step (plan §3.9). */
    out->jump = (uint8_t)((e & SEQ_REC_RING_JUMP_BIT) ? 1u : 0u);
    return 1u;
}

/*
 * Push one step-boundary entry for the recording track (S081 C-9a; S081 P3).
 *
 * What: when recording is writing and the track is rtk or live-held, writes
 * {step, track, fired, jump} at the ring head and publishes the head; a full
 * ring drops the entry (DEV: counted, trace cause 5). A negative step (not
 * started) pushes nothing. Why: the step advance (C-9a) and, from P3, the
 * looper's position jumps (wrap mid-step, resume) must report the step that
 * begins in one format; one producer function keeps the head/tail protocol
 * in one place. Context: TIM3 (step advance, wrap) or foreground with
 * PRIMASK held (resume). The ring stays single-producer because the
 * foreground pushes only inside the looper's PRIMASK sections, and
 * single-consumer (liveRec_drainRing()). Inputs: track, step, fired, jump.
 * Output: one ring entry. Accessors: seq_advanceTrackStep(),
 * seq_loopProcessTick(), seq_loopResumeTrack(). Affiliates:
 * seq_takeRecBoundary(), seq_recRingDrops, liveRec_drainRing().
 */
static void seq_recRingPush(uint8_t track, int16_t step, uint8_t fired,
                            uint8_t jump)
{
    uint8_t next;

    if (!seq_recWriting || step < 0 ||
        (track != seq_recTrack &&
         (seq_liveHoldMask & (uint8_t)(1u << track)) == 0u))
        return;
    next = (uint8_t)((seq_recRingHead + 1u) & 0x0Fu);
    if (next != seq_recRingTail) {
        seq_recRing[seq_recRingHead] =
            (uint16_t)(((uint16_t)(step & 0x7F)) |
                       ((uint16_t)(track & 0x7u) << SEQ_REC_RING_TRACK_SHIFT) |
                       (fired ? SEQ_REC_RING_FIRED_BIT : 0u) |
                       (jump ? SEQ_REC_RING_JUMP_BIT : 0u));
        seq_recRingHead = next;
    } else {
#if DEV_MODE_LOGGING
        seq_recRingDrops++;
        patternTrace_record(PAT_TRACE_STAGE_LIVEREC_DROP, 5u,
                            (uint32_t)(((uint32_t)track << 8u) |
                                       (uint8_t)(step & 0x7F)));
#endif
    }
}

/*
 * Publish the recording target and writing flag (S081 §19.1, C-2).
 *
 * What: stores seq_recTrack and seq_recWriting and mirrors writing into
 * seq_recordActive (the published "Pattern writing" flag). Why: TIM3 must know
 * which track is recorded and whether recording is active without calling
 * LiveRecord. Single-byte stores, atomic on Cortex-M7. Caller: LiveRecord.
 */
void seq_setRecordTarget(uint8_t track, uint8_t writing)
{
    seq_recTrack = (uint8_t)(track & 0x07u);
    seq_recWriting = (uint8_t)(writing ? 1u : 0u);
    seq_recordActive = seq_recWriting;
}

/*
 * Mark one stored trigger to be skipped on its next replay (S081 §8.6).
 *
 * What: records the step and a deadline (capture tick + ticks until the
 * step + one step interval + 1) under PRIMASK, so TIM3 never sees a step
 * without its deadline. Why: a hit written onto a step the playhead has
 * not reached in this pass would otherwise sound twice: live, then stored.
 * Only steps AHEAD of the playhead are marked (LR-6); a step behind it is
 * not replayed in this pass, and marking it silenced the new hit on its
 * next pass. Inputs: track, step, aheadTicks from seq_placeFromSnapshot().
 * Outputs: seq_echoStep, seq_echoDeadline. Accessors: liveRec_commitHit().
 * Affiliates: the echo block in seq_advanceTrackStep().
 */
void seq_suppressStepOnce(uint8_t track, uint8_t step, uint16_t aheadTicks)
{
    uint16_t interval;
    uint32_t primask;

    if (track >= NUM_TRACKS)
        return;
    interval = (uint16_t)(stepScale_ticksQ8(seq_effectiveTrackScale[track])
                          >> 8u);
    primask = __get_PRIMASK();
    __disable_irq();
    seq_echoStep[track] = (int8_t)step;
    seq_echoDeadline[track] =
        (uint16_t)(seq_elapsedPpqTicks + aheadTicks + interval + 1u);
    __set_PRIMASK(primask);
}

/*
 * Set/clear a track's live-hold bit (S081 §8.8, F-P, C-3c).
 *
 * What: sets or clears bit N in seq_liveHoldMask. While set, TIM3 skips the
 * track's stored triggers (automation still plays). Caller: LiveRecord only.
 */
void seq_setLiveHold(uint8_t track, uint8_t on)
{
    if (track >= NUM_TRACKS)
        return;
    if (on)
        seq_liveHoldMask |= (uint8_t)(1u << track);
    else
        seq_liveHoldMask &= (uint8_t)~(1u << track);
}

/*
 * Silence a pending deferred trigger that a live hit replaced
 * (S081 §8.6, P1B item 7).
 *
 * What: if the track's pending deferral belongs to `step`, marks it muted.
 * The deferral still fires on time, so the step's automation is queued in
 * order, but it does not trigger the voice. A deferral that belongs to a
 * different (earlier) step is left alone. Why: the P1 version cleared the
 * pending flag. That dropped the replaced step's automation (now queued at
 * fire time, A1-3), and it also cancelled an earlier step's still-pending
 * hit. Inputs: track, the step the live hit was written to. Outputs:
 * seq_trackShuffleMuteMask. Context: foreground (PRIMASK).
 * Accessors: liveRec_commitHit() (rel == 0). Affiliates: seq_fireDeferred(),
 * seq_trackShuffleStep[].
 */
void seq_cancelPendingTrigger(uint8_t track, uint8_t step)
{
    uint32_t primask;

    if (track >= NUM_TRACKS)
        return;
    primask = __get_PRIMASK();
    __disable_irq();
    if (seq_trackShufflePending[track] && seq_trackShuffleStep[track] == step)
        seq_trackShuffleMuteMask |= (uint8_t)(1u << track);
    __set_PRIMASK(primask);
}

/*
 * Set the per-track live-erase masks (S081 §10, C-4; P1C item 11).
 *
 * What: stores the specials and full erase masks as one update. TIM3 reads
 * them to clear bit 15 + specials (specials) or everything (full). Why: the
 * two stores were separate, so TIM3 could act on one new mask and one old
 * mask for one tick (for example, a TRACK hold turning into a SEQ hold).
 * Context: foreground; a two-store PRIMASK section. Caller:
 * liveRec_pushEraseMasks().
 */
void seq_setEraseMasks(uint8_t specials, uint8_t full)
{
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    seq_eraseSpecialsMask = specials;
    seq_eraseFullMask = full;
    __set_PRIMASK(primask);
}

/* Current playback epoch (S081 §19.1 item 6, C-6b). */
uint8_t seq_getPlaybackEpoch(void)
{
    return seq_playbackEpoch;
}

/* Current Scene epoch (S081 §9.2, P1B item 10): bumps only on Scene change. */
uint8_t seq_getSceneEpoch(void)
{
    return seq_sceneEpoch;
}

/* Current quantisation option (read-only; S081 P1B live-record placement). */
uint8_t seq_getQuantisation(void)
{
    return seq_quantisation;
}

/*
 * Register the LiveRecord latch/erase bitmaps for the drain filter
 * (S081 §19.1 item 2, C-7b).
 *
 * What: stores the bitmap pointers and the erase track. The drain reads them to
 * skip latched/erased automation entries. NULL pointers mean unfiltered. Caller:
 * LiveRecord.
 */
void seq_setAutomationFilter(const uint8_t *latch, const uint8_t *erase,
                             uint8_t eraseTrack)
{
    seq_filterLatch = latch;
    seq_filterErase = erase;
    seq_filterEraseTrack = eraseTrack;
}

/*
 * Expire echo marks and empty the boundary ring (S081 §8.6, C-10).
 *
 * What: sets every seq_echoStep[] to -1 and resets the ring cursors. Why: a
 * stale echo mark would silence a later legitimate trigger; a stale ring entry
 * would look like a step boundary after a transport/Scene change. Callers:
 * seq_init(), seq_setStepIndexToStart(), the Scene/realign epoch sites.
 */
static void seq_liveRecResetMarks(void)
{
    uint8_t t;

    for (t = 0u; t < NUM_TRACKS; t++)
        seq_echoStep[t] = -1;
    seq_recRingHead = 0u;
    seq_recRingTail = 0u;
}


/*
 * Scene-target step-automation restore bitmap (+4 B normal SRAM1).
 *
 * What: one bit per current Scene target-table entry. Why: Scene-target step
 * values are runtime overlays, so transport restore must know which retained
 * values need to be re-applied without serializing or dirtying them. Lifetime:
 * static until the next restore/clear. Owner: Sequencer. Affiliates:
 * seq_applySceneAutomation(), seq_restoreAllSceneAutomation(), and the
 * kind-specific Preset/InstrumentManager runtime overlays.
 */
static uint32_t seq_scene_automation_dirty;

/*
 * Clear all pending transient automation restores.
 *
 * Inputs: none. Output: every voice-slot dirty bitmap is zero. This is used
 * at boot, fixed-grid restart, and transport stop so a stale overlay cannot
 * leak into a later transport or Scene/Pattern context.
 */
static void seq_clearAutomationDirty(void)
{
    uint8_t slot;

    for (slot = 0u; slot < INSTRUMENT_SLOT_COUNT; slot++)
        seq_automation_dirty[slot] = 0u;
    seq_scene_automation_dirty = 0u;
}

/*
 * Clear the Scene-target automation dirty bitmap (S076 Rule B).
 *
 * What: zeroes seq_scene_automation_dirty. Why: the dirty bits reference
 * the previous Scene's mod-target table indices. After a Scene switch the
 * new Scene's retained values are the correct transport-restore baseline,
 * and old dirty bits that indexed into the previous Scene's target table
 * could restore wrong entries or alias into the new table. Clearing here
 * means a subsequent transport reset restores retained values for only
 * those targets that the new Scene's automation actually wrote.
 * Inputs: none. Output: seq_scene_automation_dirty = 0.
 * Caller: seq_selectActivePattern() and seq_alignActivePatternToScene()
 * on Scene change. Affiliates: seq_applySceneAutomation() (the setter),
 * seq_restoreAllSceneAutomation() (the transport-boundary consumer),
 * seq_clearAutomationDirty() (the boot/transport clear that zeroes both
 * voice and Scene bitmaps).
 */
void seq_clearSceneAutomationDirty(void)
{
    seq_scene_automation_dirty = 0u;
}

/*
 * Clear one bit in the Scene-automation dirty mask (S081 §5.2).
 *
 * What: clears the bit at position `index` in seq_scene_automation_dirty. Why:
 * when the user edits a Scene-target parameter from the menu, the per-target
 * clear (§5.2) must also clear that target's held bit so the restore path does
 * not revert the edit. The existing bulk clear (seq_clearSceneAutomationDirty)
 * clears all bits and is used at Scene changes. This per-bit version serves the
 * R12 menu-edit path. Inputs: index, the bit position from
 * sceneModTarget_indexFromId(). Outputs: one bit cleared. Affiliates:
 * seq_scene_automation_dirty, seq_restoreAllSceneAutomation, the per-target
 * clear functions in preset/presetMorphEngine.
 */
void seq_clearSceneAutomationBit(uint8_t index)
{
    if (index >= 32u)
        return;
    seq_scene_automation_dirty &= (uint32_t)~(1u << index);
}

/*
 * Restore every dirty automation overlay to its morph-interpolated base value.
 *
 * What: walks all instrument slots and, for each set bit in
 * seq_automation_dirty[slot], writes the matching morph_interpolation[] value
 * back into the voice runtime through instrumentManager_writeRuntime(). This
 * is the all-slot counterpart of seq_restoreAutomatedParameters().
 *
 * Why: seq_clearAutomationDirty() only drops the tracking bits. Clearing those
 * bits first leaves any last automation value in the runtime image, so the
 * next step-0 trigger has no evidence that it must restore that value. The
 * restore-before-clear sequence returns every transient overlay to the current
 * Scene morph base before a transport, Pattern, or external reset re-enters
 * the fixed grid.
 *
 * Inputs: implicit seq_automation_dirty[] state and the active Scene's
 * instrument images. Outputs: all marked descriptor-local runtime values are
 * restored; the dirty bitmaps remain unchanged for the caller to clear.
 * Common caller: seq_setStepIndexToStart(), used by transport start/stop,
 * Pattern-boundary changes, and external reset. Boot continues to call
 * seq_clearAutomationDirty() directly because no runtime overlays exist yet.
 * Affiliates: seq_drainPendingAutomation() publishes the dirty bits,
 * seq_restoreAutomatedParameters() implements the one-trigger variant,
 * scene_instrumentSlotConst() resolves the active slot image, and
 * instrumentManager_descriptor()/instrumentManager_writeRuntime() apply the
 * descriptor-domain restore.
 */
static void seq_restoreAllAutomation(void)
{
    uint8_t slot;

    for (slot = 0u; slot < INSTRUMENT_SLOT_COUNT; slot++) {
        uint64_t mask = seq_automation_dirty[slot];
        const kit_instrument_slot_t *instrument;

        if (!mask)
            continue;
        /*
         * All-slot automation restore uses per-track played Scene (S077 P2).
         *
         * What: the morph_interpolation[] restore base comes from the Scene
         * whose instrument is actually loaded in each slot. Inputs:
         * seq_perTrackPattern[slot] for slot 0..5. When no per-track override is
         * set this equals seq_activePattern and behaviour is unchanged.
         */
        instrument = scene_instrumentSlotConst(seq_perTrackPattern[slot], slot);
        if (!instrument)
            continue;
        while (mask) {
            uint8_t local = (uint8_t)__builtin_ctzll(mask);
            const ParamDescriptor *descriptor =
                instrumentManager_descriptor(instrument->type, local);
            if (descriptor)
                (void)instrumentManager_writeRuntime(
                    slot, descriptor,
                    instrument->parameter_images.morph_interpolation[local]);
            mask &= (mask - 1ULL);
        }
    }
}

/*
 * Restore every dirty Scene-target step overlay to retained values.
 *
 * Inputs: seq_scene_automation_dirty and the active Scene's retained
 * SceneData/Kit values. Output: Morph, decimation, audio routing, generated
 * slot-6 decay, and the readable FX-send overlay return to retained values;
 * the next mixer block therefore ramps back to the retained FX send. The
 * bitmap remains set for seq_clearAutomationDirty(), matching the voice-
 * overlay restore contract.
 * Common caller: seq_setStepIndexToStart() on transport, Pattern, or
 * external-clock reset.
 */
static void seq_restoreAllSceneAutomation(void)
{
    uint32_t mask = seq_scene_automation_dirty;
    uint8_t scene_index = scene_getActiveIndex();
    const scene_t *scene = scene_getConst(scene_index);

    if (!mask)
        return;

    /*
     * Clear unconditional runtime owners first. Each helper is a no-op when
     * its overlay was not used, while clearing Morph queues a retained-base
     * rebuild for any slot that was overridden.
     */
    presetMorph_clearAllStepAutomationOverrides(scene_index);
    instrumentManager_clearSlot6Track7StepDecayOverride();
    /*
     * Clear the discrete Scene-setting overlays before retained values are
     * reapplied below. Audio routing is restored through its DSP owner; the FX
     * send overlay is consumed by the mixer and ramps back after this clear.
     */
    preset_clearAllAudioOutStepOverrides(scene_index);
    preset_clearAllFxSendStepOverrides();
    /*
     * S078 §5.5: drop every per-track timing overlay. Unlike the Scene-setting
     * overlays above, the effective track getters read these directly, so the
     * clear must happen before the retained-value pass below.
     */
    presetMorph_clearAllTrackParamStepOverrides();

    if (!scene)
        return;

    while (mask) {
        uint8_t index = (uint8_t)__builtin_ctz(mask);
        uint16_t id = sceneModTarget_idFromIndex(index);
        const scene_mod_target_descriptor_t *descriptor =
            sceneModTarget_descriptor(id);

        if (descriptor) {
            switch (descriptor->kind) {
            case SCENE_MOD_TARGET_KIND_VOICE_MORPH:
                parameter_values[PAR_VOICE1_MORPH + descriptor->voice_slot] =
                    scene_getVoiceMorphAmount(scene_index,
                                              descriptor->voice_slot);
                /*
                 * Commit the retained Morph image before a rapid restart can
                 * trigger this voice. The queued rebuild from
                 * presetMorph_clearAllStepAutomationOverrides() remains the
                 * bounded fallback for any other slots that were pending.
                 */
                presetMorph_applyVoiceNow(scene_index,
                                          descriptor->voice_slot);
                break;
            case SCENE_MOD_TARGET_KIND_AUDIO_OUT:
                (void)preset_applyKitAudioRouting(scene_index,
                                                  descriptor->voice_slot);
                break;
            case SCENE_MOD_TARGET_KIND_FX_SEND:
            case SCENE_MOD_TARGET_KIND_SLOT6_TRACK7_AMP_DECAY:
            /* S078 §5.5: track timing overlays are cleared above. */
            case SCENE_MOD_TARGET_KIND_TRACK_LENGTH:
            case SCENE_MOD_TARGET_KIND_TRACK_SCALE:
            case SCENE_MOD_TARGET_KIND_TRACK_SHUFFLE:
            /* `fxm` is cleared by the foreground FX reset latch, not in TIM3. */
            case SCENE_MOD_TARGET_KIND_EFFECT_MORPH:
            default:
                break;
            }
        }
        mask &= (mask - 1u);
    }
}

static void seq_sendRealtime(const uint8_t status);
static void seq_sendProgChg(const uint8_t ptn);
static void seq_processSchedulerTick(void);
static void seq_setStepIndexToStart();
static void seq_queueStepAutomations(uint8_t track, uint8_t step);
/* S081 §5.8 deferred-trigger fire with its automation (defined near
** seq_processDeferredTriggers()); forward-declared for seq_advanceTrackStep. */
static void seq_fireDeferred(uint8_t track);
/* S081 P2 automation ramps (defined after seq_restoreAutomatedParameters()). */
static void seq_rampStepEvent(uint8_t track, uint8_t applied, uint8_t deferred);
static void seq_rampBreakTrack(uint8_t track);
static void seq_rampBreakAll(void);
static void seq_rampConsumeTrack(uint8_t track);
static void seq_rampConsumeSteps(void);
static const ParamDescriptor *seq_writeInstrumentAutomation(
    uint8_t owner_scene, uint16_t target, uint8_t value);
static void seq_rampStartIfPaired(uint8_t track, uint8_t scene, uint8_t step,
                                  uint16_t target, uint8_t from,
                                  const ParamDescriptor *descriptor);
/* S080 P2 looper engine (defined after seq_realignTrackToMasterClock()). */
static void seq_loopRefreshSceneSnapshots(void);
static void seq_loopRebaseToStart(void);
static void seq_loopProcessTick(void);

/*
 * Publish one FX-sequencer position from the pure master-clock timeline.
 *
 * Inputs: current elapsed 96-PPQ ticks and the active Scene's retained FX
 * run/length/scale settings. Output: one newest-position latch event at each
 * scale boundary for fwd, rev, pip, or rnd. `sel` intentionally publishes no
 * clock event because its selected step is foreground-owned. No Scene, DSP,
 * or LED access occurs in this TIM3 path.
 */
static void seq_fxClockTick(void)
{
    const effect_record_t *record = scene_effectConst(scene_getActiveIndex());
    uint16_t interval;
    uint8_t len;
    uint8_t index;
    uint32_t n;

    /*
     * FX sequencer DDA clock (S078 §2.4.4 Stage 3).
     *
     * What: replaces the integer modulo (seq_elapsedPpqTicks % ticks) with the
     * same Q8.8 DDA accumulator model used by Pattern tracks. Why: the FX
     * sequencer shares the 128-position curve and must handle fractional tick
     * intervals. The step counter replaces the stateless n = ticks/elapsed
     * computation because fractional intervals make that division ambiguous.
     * Inputs: seq_fxAccumulator, interval from stepScale_ticksQ8(). Outputs:
     * seq_fxPublishStep() on each boundary. Reset: seq_setStepIndexToStart().
     * Affiliates: effects_service() foreground consumer.
     */
    if (!record || record->seq_run_mode >= EFFECT_SEQ_RUN_MODE_COUNT ||
        record->seq_run_mode == EFFECT_SEQ_RUN_SEL)
        return;
    interval = stepScale_ticksQ8(record->seq_step_scale);
    seq_fxAccumulator += 256u;
    if (seq_fxAccumulator < interval)
        return;
    seq_fxAccumulator -= interval;
    len = record->seq_length;
    if (len < EFFECT_SEQ_LENGTH_MIN || len > EFFECT_SEQ_LENGTH_MAX)
        len = EFFECT_SEQ_LENGTH_DEFAULT;
    n = seq_fxStepCounter;
    seq_fxStepCounter++;
    switch (record->seq_run_mode) {
    case EFFECT_SEQ_RUN_REV:
        index = (uint8_t)(len - 1u - (n % len));
        break;
    case EFFECT_SEQ_RUN_PIP: {
        uint32_t p = n % ((uint32_t)len * 2u);

        index = (uint8_t)(p < len ? p : ((uint32_t)len * 2u - 1u - p));
        break; }
    case EFFECT_SEQ_RUN_RND:
        index = (uint8_t)(((uint16_t)GetRngValue() & 0x7FFFu) % len);
        break;
    case EFFECT_SEQ_RUN_FWD:
    default:
        index = (uint8_t)(n % len);
        break;
    }
    seq_fxPublishStep(index);
}

/*
 * Realign the FX sequence to the master clock (S081 §3.3-4, Appendix A-5).
 *
 * What: under PRIMASK, recomputes the FX sequencer position from
 * seq_elapsedPpqTicks with the active Effect's current step scale, using the
 * same mapping as the track realign: master_q8 = elapsed << 8,
 * position = master_q8 / interval, seq_fxAccumulator = master_q8 % interval,
 * seq_fxStepCounter = position + 1. It then publishes the step index for
 * `position` through seq_fxPublishStep() (fwd/rev/pip by the same formula as
 * seq_fxClockTick(); rnd takes a fresh draw). `sel` has no clock and is left
 * alone. Why: seq_fxStepCounter counts boundaries since the transport reset, so
 * after the user changes the FX step scale, the FX sequence drifts out of
 * alignment with the Pattern. Realign must bring it back onto the master grid,
 * not skip it. Publishing a STEP (not a RESET) keeps the FX step valid;
 * EffectsManager treats it as a lock-step start (A2-2f). Inputs:
 * seq_elapsedPpqTicks, the active Effect record. Outputs: FX accumulator,
 * counter and published step. Affiliates: seq_fxClockTick(),
 * seq_fxPublishStep(), effects_seqConsume().
 */
static void seq_fxRealignToMasterClock(void)
{
    const effect_record_t *record = scene_effectConst(scene_getActiveIndex());
    uint16_t interval;
    uint8_t len;
    uint8_t index;
    uint32_t master_q8;
    uint32_t position;
    uint32_t primask;

    if (!record || record->seq_run_mode >= EFFECT_SEQ_RUN_MODE_COUNT ||
        record->seq_run_mode == EFFECT_SEQ_RUN_SEL)
        return;
    interval = stepScale_ticksQ8(record->seq_step_scale);
    if (interval == 0u)
        interval = 1u;
    len = record->seq_length;
    if (len < EFFECT_SEQ_LENGTH_MIN || len > EFFECT_SEQ_LENGTH_MAX)
        len = EFFECT_SEQ_LENGTH_DEFAULT;
    master_q8 = (uint32_t)seq_elapsedPpqTicks << 8u;
    position = master_q8 / interval;
    primask = __get_PRIMASK();
    __disable_irq();
    seq_fxAccumulator = (uint16_t)(master_q8 % interval);
    seq_fxStepCounter = position + 1u;
    __set_PRIMASK(primask);
    switch (record->seq_run_mode) {
    case EFFECT_SEQ_RUN_REV:
        index = (uint8_t)(len - 1u - (position % len));
        break;
    case EFFECT_SEQ_RUN_PIP: {
        uint32_t p = position % ((uint32_t)len * 2u);

        index = (uint8_t)(p < len ? p : ((uint32_t)len * 2u - 1u - p));
        break; }
    case EFFECT_SEQ_RUN_RND:
        index = (uint8_t)(((uint16_t)GetRngValue() & 0x7FFFu) % len);
        break;
    case EFFECT_SEQ_RUN_FWD:
    default:
        index = (uint8_t)(position % len);
        break;
    }
    seq_fxPublishStep(index);
}

/*
 * Recompute the effective per-track timing cache (S078 §5.4).
 *
 * What: for every track, reads the Pattern Normal values from the track's
 * played Scene region and interpolates them against that Scene's Morph
 * endpoints at the associated voice's retained Morph amount, then stores the
 * results in seq_effectiveTrackLength/Scale/Shuffle. Why: the sequencer reads
 * the cache instead of the region so a Morph sweep changes playback without
 * touching retained Pattern data. Inputs: seq_perTrackPattern[],
 * presetMorph_getTrackEffective*(). Outputs: the three effective arrays.
 * Callers: presetMorph_tick() (foreground Morph worker, every tick),
 * seq_selectActivePattern(), seq_alignActivePatternToScene(),
 * seq_setTrackPlayedScene(), and seq_setStepIndexToStart(). Foreground- or
 * ISR-safe: pure SRAM reads with no allocation or I/O.
 */
void seq_refreshTrackEffectiveParams(void)
{
	uint8_t track;

	for (track = 0u; track < NUM_TRACKS; track++) {
		uint8_t scene_index = seq_perTrackPattern[track];
		uint8_t new_len =
		    presetMorph_getTrackEffectiveLength(scene_index, track);
		uint8_t new_scale =
		    presetMorph_getTrackEffectiveScale(scene_index, track);

		/*
		 * S081 C-10: an effective length or scale change expires this track's echo
		 * mark, so a stale suppression cannot silence a later legitimate trigger.
		 */
		if (new_len != seq_effectiveTrackLength[track] ||
		    new_scale != seq_effectiveTrackScale[track])
			seq_echoStep[track] = -1;
		seq_effectiveTrackLength[track] = new_len;
		seq_effectiveTrackScale[track] = new_scale;
		seq_effectiveTrackShuffle[track] =
		    presetMorph_getTrackEffectiveShuffle(scene_index, track);
	}
}

//------------------------------------------------------------------------------
void seq_init()
{
	uint8_t i;

	memset(seq_stepIndex,0,sizeof(seq_stepIndex));
	memset(seq_lastMasterStep,0,NUM_TRACKS);
	/* S078: clear every per-track DDA, shuffle, and play-mode byte at boot. */
	memset(seq_trackAccumulator, 0, sizeof(seq_trackAccumulator));
	memset(seq_trackShuffleDelay, 0, sizeof(seq_trackShuffleDelay));
	memset(seq_trackShufflePending, 0, sizeof(seq_trackShufflePending));
	memset(seq_trackShuffleVel, 0, sizeof(seq_trackShuffleVel));
	memset(seq_trackShuffleNote, 0, sizeof(seq_trackShuffleNote));
	memset(seq_trackPlayState, 0, sizeof(seq_trackPlayState));
	/* S079: clear per-track condition and roll runtime state at boot. */
	memset(seq_trackRepeatCount, 0, sizeof(seq_trackRepeatCount));
	memset(seq_trackRndStepCount, 0, sizeof(seq_trackRndStepCount));
	memset(seq_trackLastConditionFired, 0,
	       sizeof(seq_trackLastConditionFired));
	/* S079/S084 P1: no step roll runs at boot. */
	for (i = 0u; i < NUM_TRACKS; i++)
		seq_rollClearTrack(i);
	/* S084 P2: no live overlay roll or flam is armed at boot. */
	memset((void *)seq_liveMode, 0, sizeof(seq_liveMode));
	memset((void *)seq_liveHitCount, 0, sizeof(seq_liveHitCount));
	seq_liveFlamMask = 0u;
	seq_liveRhythm = 0u;
	seq_fillActive = 0u;
	seq_fxAccumulator = 0u;
	seq_fxStepCounter = 0u;
	/* S081: expire echo marks, empty the boundary ring and start at epoch 0. */
	seq_liveRecResetMarks();
	seq_playbackEpoch = 0u;
	seq_recWriting = 0u;
	seq_recordActive = 0u;
	seq_liveHoldMask = 0u;
	seq_eraseSpecialsMask = 0u;
	seq_eraseFullMask = 0u;
	/*
	 * S078 §5.4: seed the effective track cache from the resident Normal
	 * values. scene_initAll() has already run pat_initScene(), so the regions
	 * carry the Normal defaults here.
	 */
	seq_refreshTrackEffectiveParams();
	/* Keep the future per-track map aligned with the one active Scene. */
	for (uint8_t track = 0u; track < NUM_TRACKS; track++)
		seq_perTrackPattern[track] = seq_activePattern;
	/* S077 P2: identical entries mean the per-track fast path is inactive. */
	seq_perTrackActive = 0u;
	seq_pending_automation_count = 0u;
	seq_pending_automation_drain = 0u;
	seq_clearAutomationDirty();
}
//------------------------------------------------------------------------------
static void seq_calcDeltaT(uint16_t bpm)
{
	//--- calc deltaT ----
	// The hardware timer services one 96 PPQ transport tick at a time.
	// Track/default-step scheduling is derived from those ticks separately.
	seq_deltaT 	= (1000*60)/bpm; 	//bei 12 = 500ms = time for one beat
	seq_deltaT /= (float)SEQ_INTERNAL_PPQ;
	seq_deltaT *= SYSTICK_TICKS_PER_MS; //systick_ticks is the canonical 0.25ms LXR tick

	/* The transport tick stays uniform: fixed-grid patterns have no shuffle. */
}
//------------------------------------------------------------------------------
void seq_setBpm(uint16_t bpm)
{
	if (bpm == 0)
		bpm = 1;
	seq_tempo 	= bpm;
	//seq_calcDeltaT(bpm);
	lfo_recalcSync();
}
//------------------------------------------------------------------------------
uint16_t seq_getBpm()
{
	return seq_tempo;
}
//------------------------------------------------------------------------------
void seq_sync()
{
	sync_tick();
}
//------------------------------------------------------------------------------
void seq_armActivePatternReload(void)
{
	seq_loadPendigFlag = 1;
}
//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
/*
 * Assign one track to play from a specific Scene (S077 P2 §1.3).
 *
 * What: sets seq_perTrackPattern[track] to scene_index and recalculates
 * seq_perTrackActive. Tracks 5 and 6 (HiHat choke pair, indices 5 and 6 in
 * the seven-track array) always switch together. Why: the PERF
 * hold-VOICE+press-SEQ gesture calls this to override one track's playback
 * Scene without changing the global active Scene. Inputs: track 0..6, a Scene
 * index validated by pat_patternValid(). Output: seq_perTrackPattern[track]
 * updated. ISR safety: foreground-only write to a single byte read by TIM3;
 * ARM Cortex-M7 byte stores are atomic. Affiliates:
 * seq_clearPerTrackOverrides(), seq_recomputePerTrackActive(),
 * preset_startSingleVoiceApply() (called after this by the button handler).
 */
void seq_setTrackPlayedScene(uint8_t track, uint8_t scene_index)
{
    if (track >= NUM_TRACKS || !pat_patternValid(scene_index))
        return;

    seq_perTrackPattern[track] = scene_index;
    /* The HiHat choke pair shares one played Scene (tracks 5 and 6). */
    if (track >= 5u) {
        seq_perTrackPattern[5] = scene_index;
        seq_perTrackPattern[6] = scene_index;
    }
    /*
     * Once-mode retrigger on per-track Scene assignment (S078 §4.3).
     *
     * What: clear the reassigned track's play-state byte so a stopped once /
     * once-free track restarts its one-pass playback. Voice 6 assigns both
     * tracks 5 and 6 together, so both are retriggered when either is
     * reassigned. Why: per-track Scene reassignment is a defined retrigger
     * event; the double-click realign gesture is not. Affiliates:
     * seq_realignTrackToMasterClock() (the caller repositions the track).
     */
    seq_trackPlayState[track] = 0u;
    if (track >= 5u) {
        seq_trackPlayState[5] = 0u;
        seq_trackPlayState[6] = 0u;
    }
    /*
     * S079 §3.2/§3.4: a per-track Scene change resets that track's X:Y repeat
     * counter, random step counter, and last-condition chain flag.
     */
    seq_trackRepeatCount[track] = 0u;
    seq_trackRndStepCount[track] = 0u;
    seq_trackLastConditionFired[track] = 0u;
    if (track >= 5u) {
        seq_trackRepeatCount[5] = 0u;
        seq_trackRndStepCount[5] = 0u;
        seq_trackLastConditionFired[5] = 0u;
        seq_trackRepeatCount[6] = 0u;
        seq_trackRndStepCount[6] = 0u;
        seq_trackLastConditionFired[6] = 0u;
    }
    /* S079 P2 fix 4 / S084 P1: a per-track Scene change ends the track's
     * step roll, and its choke partner's (tracks 5 and 6). */
    seq_rollClearTrack(track);
    if (track >= 5u) {
        seq_rollClearTrack((uint8_t)((track == 5u) ? 6u : 5u));
    }
	/*
	 * Clear the affected slot's R12 menu override at the per-track Scene change
	 * (S081 §5.1). Tracks 5 and 6 both map to slot 5, matching the choke pair.
	 */
	seq_menuOverride[(track >= INSTRUMENT_SLOT_COUNT)
	                 ? (INSTRUMENT_SLOT_COUNT - 1u) : track] = 0u;
	/* S081 C-6/C-10: bump the epoch and expire this track's echo mark. */
	seq_playbackEpoch++;
	/* S081 P2: a playhead break ends automation ramps (no B will follow);
	 * the choke pair changes Scene together, so both tracks break. */
	seq_rampBreakTrack(track);
	if (track >= 5u) {
		seq_rampBreakTrack(5u);
		seq_rampBreakTrack(6u);
	}
	seq_echoStep[track] = -1;
	if (track >= 5u)
		seq_echoStep[5] = seq_echoStep[6] = -1;
	/* S078 §5.4: the reassigned track's Scene changed, rebuild its cache. */
	seq_refreshTrackEffectiveParams();
	seq_recomputePerTrackActive();
}

/*
 * Reset all per-track played-Scene entries to the active Scene (S077 P2 §1.5).
 *
 * What: sets every seq_perTrackPattern[] entry to seq_activePattern and clears
 * seq_perTrackActive to 0. Why: any scene-level playback change (PERF SEQ
 * press without a VOICE hold) coalesces all tracks back to one Scene
 * (plan §4.3, Q2). Inputs: seq_activePattern. Output: all entries equal
 * activePattern. Affiliates: the button handler coalesce gesture;
 * seq_selectActivePattern()/seq_alignActivePatternToScene() already perform the
 * same loop inline for their own Scene changes.
 */
void seq_clearPerTrackOverrides(void)
{
    uint8_t track;

    for (track = 0u; track < NUM_TRACKS; track++)
        seq_perTrackPattern[track] = seq_activePattern;
    seq_perTrackActive = 0u;
    /* S078 §5.4: all played Scenes were coalesced, rebuild the cache. */
    seq_refreshTrackEffectiveParams();
}

/*
 * Read one track's played Scene (S077 P2 §3.5).
 *
 * What: returns seq_perTrackPattern[track], or seq_activePattern for an
 * out-of-range track. Why: Preset's mixer consumer resolves per-slot FX-send,
 * fader-mode, and Morph-amount lookups through the track's played Scene rather
 * than the global active Scene. Inputs: track/slot 0..6. Output: resident Scene
 * index. Context: foreground only. Affiliates: preset_getSlotPlayedScene().
 */
uint8_t seq_getTrackPlayedScene(uint8_t track)
{
    if (track >= NUM_TRACKS)
        return seq_activePattern;
    return seq_perTrackPattern[track];
}

/*
 * Move tracks to play another Scene (S077 P2 4.1; S083 P3).
 *
 * What: for each track in trackMask: seq_setTrackPlayedScene(),
 * seq_realignTrackToMasterClock() (which also advances the Scene epoch, so
 * LiveRecord starts a new take, S081 14.3 / R20), ends the track's MIDI
 * note (the new Scene may map it to another channel or note), and arms the
 * slot's instrument apply once per slot (tracks 6 and 7 share slot 5).
 * The HiHat pair (track indices 5 and 6) always moves together: if either
 * bit is set both are processed (assessment F-7), because
 * seq_setTrackPlayedScene() moves the partner anyway and the partner must
 * also be realigned and have its MIDI note ended.
 * Why: one implementation for the panel gesture (hold VOICE + SEQ) and a
 * MIDI PC on a voice channel; the MIDI path must not skip the realign
 * (plan R20). Inputs: trackMask (bit per track), Scene 0..15, already
 * validated available by the caller. Context: foreground.
 * Accessors: buttonHandler_perfVoiceHeldSeqPressed(),
 * midiProgram_programChange(). Affiliates: preset_startSingleVoiceApply(),
 * midiOut_trackNoteOff().
 */
void seq_assignTracksToScene(uint8_t trackMask, uint8_t scene_index)
{
	uint8_t applied = 0u;
	uint8_t track;

	/* F-7: the HiHat choke pair moves as one (see the contract above). */
	if (trackMask & 0x60u)
		trackMask |= 0x60u;

	for (track = 0u; track < NUM_TRACKS; track++) {
		uint8_t slot;

		if ((trackMask & (uint8_t)(1u << track)) == 0u)
			continue;
		seq_setTrackPlayedScene(track, scene_index);
		seq_realignTrackToMasterClock(track);
		midiOut_trackNoteOff(track);
		slot = (track >= INSTRUMENT_SLOT_COUNT)
		       ? (uint8_t)(INSTRUMENT_SLOT_COUNT - 1u) : track;
		if ((applied & (uint8_t)(1u << slot)) == 0u) {
			applied |= (uint8_t)(1u << slot);
			preset_startSingleVoiceApply(slot, scene_index);
		}
	}
}

//------------------------------------------------------------------------------
/*
 * Trigger a voice with optional automation reset (S081 §5.8).
 *
 * What: fires noteOff then noteOn (or noteOnNoReset) for the voice,
 * sends MIDI, triggers hardware trigger output. The reset argument
 * selects whether the pending-reset mask is set: 1 for first triggers
 * (step start, deferred fire, roll first hit, overlay, preview), 0 for
 * roll retriggers 2+. Why: only a step's first trigger may restore the
 * Morph base and clear the menu override; later roll hits must not
 * (F-L). Inputs: voiceNr 0..6, vol, note, reset 0/1. Outputs: voice
 * queued in the trigger ring, pending-reset bit set if reset=1.
 * Affiliates: voiceControl_noteOn/noteOnNoReset, seq_advanceTrackStep
 * callers, seq_processRollTicks, seq_processLiveTicks (S084 P2: overlay
 * roll repeats reset = 0, flam main reset = 1, from TIM3), the
 * buttonHandler overlay start (first hits, foreground), seq_previewVoice.
 */
void seq_triggerVoice(uint8_t voiceNr, uint8_t vol, uint8_t note, uint8_t reset)
{
	if(voiceNr > 6) return;

	/*
	 * Trigger one fixed-grid event.
	 *
	 * Inputs: valid voice plus caller-selected velocity/note. Outputs: synth,
	 * trigger jack, and MIDI note-on are driven. seq_advanceTrackStep() resolves
	 * PatternData defaults/specials before calling this owner; this function
	 * sends the internal note through the track's inverse note map (S083 C-2).
	 */

	//turn the trigger off before sending the next one
	if(voiceNr>=5)
	{
		//hihat channels choke each other
		trigger_triggerVoice(5, TRIGGER_OFF);
		trigger_triggerVoice(6, TRIGGER_OFF);
	} else {
		trigger_triggerVoice(voiceNr, TRIGGER_OFF);
	}

	//--AS if a note is on for that channel send note-off first
	voiceControl_noteOff(voiceNr);

	//Trigger internal synth voice
	if (reset)
		voiceControl_noteOn(voiceNr, note, vol);
	else
		voiceControl_noteOnNoReset(voiceNr, note, vol);

	/*
	 * Track MIDI output (S077 P2 6.5; S083 C-2, C-6, Q4).
	 *
	 * What: the internal note goes out as the note that plays it back on
	 * this track (inverse note map of the Scene the track plays); a pitch
	 * the track's range cannot express sends nothing. Inputs:
	 * seq_perTrackPattern[voiceNr], note, vol. Affiliates:
	 * midiOut_trackTrigger(), MidiNoteMap.
	 */
	midiOut_trackTrigger(voiceNr, seq_perTrackPattern[voiceNr], note, vol);
}
void seq_previewVoice(uint8_t voiceNr)
{
	uint8_t note;

	/*
	 * Trigger one voice without advancing or reading sequencer step state.
	 *
	 * Why: buttonHandler uses this for stopped-transport VOICE re-press
	 * preview. seq_triggerVoice() intentionally reads the current step for
	 * playback automation and MIDI velocity; preview must skip that so audition
	 * cannot depend on seq_stepIndex[] or write automation side effects.
	 *
	 * Input voiceNr is the 0-based UI track/voice. Outputs: trigger jack, synth
	 * voice play the default pitch and MIDI note-on uses the same inverse map as
	 * normal playback (S083 C-1). Confederates: MidiVoiceControl owns
	 * note-on/off, and triggerJacks owns trigger pulse state. Invalid voices and
	 * running transport are ignored.
	 */
	if (voiceNr > 6u || seq_running)
		return;

	/*
	 * Preview plays the default pitch (S083 C-1): a track's MIDI note is the
	 * root of its receive range, not a pitch. MIDI output goes through the
	 * inverse map like any trigger.
	 */
	note = MIDI_DEFAULT_TRIGGER_NOTE;

	if (voiceNr >= 5u) {
		trigger_triggerVoice(5, TRIGGER_OFF);
		trigger_triggerVoice(6, TRIGGER_OFF);
	} else {
		trigger_triggerVoice(voiceNr, TRIGGER_OFF);
	}

	voiceControl_noteOff(voiceNr);
	voiceControl_noteOn(voiceNr, note, ROLL_VOLUME);

	midiOut_trackTrigger(voiceNr, seq_perTrackPattern[voiceNr], note,
	                     ROLL_VOLUME);
}
//------------------------------------------------------------------------------
static void seq_resetStepScheduler(void)
{
	/*
	 * Reset the fixed 16-step scheduler clock.
	 *
	 * Inputs: transport start/stop/reset or Scene selection. Outputs: PPQ and
	 * master step clocks return to zero and the next scheduler pass advances all
	 * tracks together. There are no per-track scale/shuffle event counters.
	 */
	seq_elapsedPpqTicks = 0u;
	seq_masterStepClock = 0u;
	seq_masterStepCnt = 0u;
	seq_initialSchedulerTick = 1u;
	seq_internalMidiClockPhase = 0u;
}

void seq_selectActivePattern(uint8_t pattern)
{
	/*
	 * Immediately align playback to a newly selected Scene/Pattern.
	 *
	 * Inputs: front-panel PERF supplies the Scene index after Menu has validated
	 * resident Scene presence. PatternData still gets the final bounds check here
	 * because Sequencer owns the mutable playback globals. Outputs:
	 * seq_activePattern and seq_pendingPattern point at the same Scene, queued
	 * boundary-load flags are cancelled, and all fixed-grid cursors realign to
	 * the existing master clock. Scene switching does not allocate or schedule
	 * any per-track timing metadata.
	 */
	if (!pat_patternValid(pattern))
		return;

	/*
	 * S080 G7/G12: a Scene selection starts a fresh chain.
	 *
	 * What: remembers the outgoing Scene for the >NN conditions (only on a
	 * real change), restarts the bar/repetition counts and releases a `stp`
	 * freeze. Why: counts are per Scene activation; any executed change -
	 * manual, queued or chain - cancels pending conditional state (plan
	 * §4.5). Affiliates: seq_chainServiceTick(), seq_chainEvaluate().
	 */
	if (pattern != seq_activePattern)
		seq_previousScene = seq_activePattern;
	seq_chainBarCount = 0u;
	seq_chainRepCount = 0u;
	seq_chainStopped = 0u;
	seq_activePattern = pattern;
	seq_pendingPattern = pattern;
	/* S067 has one playback Scene, so every track follows the new target. */
	for (uint8_t track = 0u; track < NUM_TRACKS; track++)
		seq_perTrackPattern[track] = pattern;
	/*
	 * Scene-level switch always coalesces (S077 P2 §4.3, Q2).
	 *
	 * What: the loop above already set every seq_perTrackPattern[] entry to the
	 * new pattern, so the per-track override flag is now necessarily 0.
	 */
	seq_perTrackActive = 0u;
	seq_loadPendigFlag = 0u;
	seq_newPatternAvailable = 0u;
	/*
	 * Once-mode retrigger on Scene change (S078 §4.3).
	 *
	 * What: clear every per-track play-state byte before realignment so a
	 * stopped once/once-free track restarts its one-pass playback. The
	 * realignment below then positions onc tracks on the master clock and
	 * 1fr tracks at step zero.
	 */
	memset(seq_trackPlayState, 0, sizeof(seq_trackPlayState));
	/*
	 * S079 §3.2/§3.4: a Scene change resets the per-track X:Y repeat counters
	 * and last-condition chain flags; the new Scene's conditions start fresh.
	 */
	memset(seq_trackRepeatCount, 0, sizeof(seq_trackRepeatCount));
	memset(seq_trackRndStepCount, 0, sizeof(seq_trackRndStepCount));
	memset(seq_trackLastConditionFired, 0,
	       sizeof(seq_trackLastConditionFired));
	/* S079 P2 fix 4 / S084 P1: a Scene change ends every running step roll
	 * (its window, duck and carry with it). Live overlay rolls continue. */
	for (uint8_t track = 0u; track < NUM_TRACKS; track++)
		seq_rollClearTrack(track);
	/*
	 * Clear the menu override at transport/Scene change (S081 §5.1).
	 *
	 * What: zeroes the entire seq_menuOverride array (or the affected slot for
	 * per-track changes). Why: R12 override ends at transport start/stop/reset
	 * and at Scene changes. After clearing, step automation resumes on the next
	 * drain pass. Affiliates: seq_restoreAutomatedParameters (per-trigger
	 * clear), seq_menuOverrideSet (set site).
	 */
	memset(seq_menuOverride, 0, sizeof(seq_menuOverride));
	/* S081 C-6/C-10: bump the playback epoch and expire echo marks. */
	seq_playbackEpoch++;
	/* S081 P2: a playhead break ends automation ramps (no B will follow). */
	seq_rampBreakAll();
	/* S081 P1B item 10: a Scene change also advances the Scene epoch. */
	seq_sceneEpoch++;
	seq_liveRecResetMarks();
	/* S078 §5.4: the played Scenes changed, so rebuild the effective cache. */
	seq_refreshTrackEffectiveParams();
	{
		/*
		 * S080 P2 Defect 14: realign and re-snapshot the looper atomically.
		 *
		 * What: every track moves to its master-clock position in the new
		 * Scene, and every looped track keeps its loop-start tick while its
		 * snapshot is recomputed for the new Scene. Why: the TIM3 ISR must
		 * never wrap a track to the outgoing Scene's snapshot after the
		 * realign. Affiliates: seq_loopRefreshSceneSnapshots().
		 */
		uint32_t primask = __get_PRIMASK();

		__disable_irq();
		seq_realignActivePatternToMasterClock();
		seq_loopRefreshSceneSnapshots();
		__set_PRIMASK(primask);
	}
	/*
	 * Release Scene 17 on the first whole-Scene change (S082 C8, D9a, D10).
	 *
	 * What: once every track points at a resident Scene, Scene 17's storage
	 * becomes invalid and can never be selected again; AutoSave and overlap
	 * paste return because their Scene 17 gates fall away.
	 * Why: Scene 17 owns the sounding Effect and Scene-level state until a
	 * whole-Scene change replaces them; per-track moves never release it.
	 * Inputs: new Scene p < 16 and an existing Scene 17.
	 * Outputs: scene_backgroundRelease().
	 * Accessors: seq_selectActivePattern() (SEQ press, MIDI PC, queued/chain).
	 * Affiliates: scene_backgroundRelease(), pat_regionFor(), the C9 gates.
	 */
	if (pattern < SCENE_COUNT && scene_backgroundExists())
		scene_backgroundRelease();
	led_notifyPatternChanged(seq_activePattern);
	seq_sendProgChg(seq_activePattern);
	voiceControl_noteOff(0xFF);
	/* S083: end every track's MIDI note; the new Scene may map tracks to
	** other channels and notes. Affiliate: midiOut_trackNoteOff(). */
	midiOut_trackNoteOff(0xFFu);
	/*
	 * S076 Rule B: clear the Scene-target automation dirty bitmap.
	 *
	 * What: the dirty bits reference mod-target table indices from the
	 * previous Scene. The new Scene's retained values are the correct
	 * transport-restore baseline, and stale bits could restore wrong
	 * entries or alias into the new Scene's table. Clearing here means a
	 * subsequent transport reset only restores targets that the new
	 * Scene's own automation wrote.
	 * Affiliate: seq_restoreAllSceneAutomation() (transport-boundary
	 * restore, uses this bitmap).
	 */
	seq_clearSceneAutomationDirty();
}

void seq_alignActivePatternToScene(uint8_t scene_index)
{
	/*
	 * Realign playback to a Scene selection an external owner already committed.
	 *
	 * See sequencer.h for the full contract and the defect history; the short
	 * version is that seq_activePattern is the index playback reads at
	 * seq_advanceTrackStep()'s pat_isStepActive() call, and it is NOT derived
	 * from scene_getActiveIndex(). Any owner that changes the active Scene
	 * without a front-panel PERF press must therefore realign it here, or
	 * playback keeps reading whichever Scene was last selected (Scene 0 from
	 * BSS at boot) while loads write the newly committed one.
	 *
	 * Inputs: resident Scene index; rejected unless pat_patternValid() accepts
	 * it, so an out-of-range Bank manifest value leaves playback untouched
	 * rather than pointing at a nonexistent Scene. Outputs: the active and
	 * pending indices agree, no boundary swap is left queued, and every track
	 * cursor is rederived from the running master clock so an alignment during
	 * playback lands on the correct step rather than restarting the bar.
	 *
	 * This intentionally shares seq_selectActivePattern()'s state assignments
	 * but none of its performance side effects (LED notify, MIDI program
	 * change, all-notes-off). Boot-time Bank Load calls this before audio
	 * starts, where those would be wrong or actively harmful.
	 */
	if (!pat_patternValid(scene_index))
		return;

	/* S080 G7: a Bank Load alignment starts a fresh chain with no history. */
	seq_previousScene = 0xFFu;
	seq_chainBarCount = 0u;
	seq_chainRepCount = 0u;
	seq_chainStopped = 0u;
	seq_activePattern = scene_index;
	seq_pendingPattern = scene_index;
	/* Keep the future per-track assignment stub aligned during boot restore. */
	for (uint8_t track = 0u; track < NUM_TRACKS; track++)
		seq_perTrackPattern[track] = scene_index;
	/*
	 * Bank Load alignment also clears per-track overrides (S077 P2 §6.4).
	 *
	 * What: Bank Load coalesces all tracks to the loaded active Scene. Former
	 * per-track overrides are stale because Scene indices now point at newly
	 * loaded content.
	 */
	seq_perTrackActive = 0u;
	seq_loadPendigFlag = 0u;
	seq_newPatternAvailable = 0u;
	/* S078 §4.3: a committed Scene change retriggers once-mode tracks. */
	memset(seq_trackPlayState, 0, sizeof(seq_trackPlayState));
	/*
	 * Clear the menu override at transport/Scene change (S081 §5.1).
	 *
	 * What: zeroes the entire seq_menuOverride array (or the affected slot for
	 * per-track changes). Why: R12 override ends at transport start/stop/reset
	 * and at Scene changes. After clearing, step automation resumes on the next
	 * drain pass. Affiliates: seq_restoreAutomatedParameters (per-trigger
	 * clear), seq_menuOverrideSet (set site).
	 */
	memset(seq_menuOverride, 0, sizeof(seq_menuOverride));
	/* S081 C-6/C-10: bump the playback epoch and expire echo marks. */
	seq_playbackEpoch++;
	/* S081 P2: a playhead break ends automation ramps (no B will follow). */
	seq_rampBreakAll();
	/* S081 P1B item 10: a Scene change also advances the Scene epoch. */
	seq_sceneEpoch++;
	seq_liveRecResetMarks();
	/* S078 §5.4: the committed Scene change rebuilds the effective cache. */
	seq_refreshTrackEffectiveParams();
	{
		/*
		 * S080 P2 Defect 14: realign and re-snapshot the looper atomically.
		 *
		 * What: every track moves to its master-clock position in the new
		 * Scene, and every looped track keeps its loop-start tick while its
		 * snapshot is recomputed for the new Scene (Bank Load alignment). Why:
		 * the TIM3 ISR must never wrap a track to the outgoing Scene's snapshot
		 * after the realign. Affiliates: seq_loopRefreshSceneSnapshots().
		 */
		uint32_t primask = __get_PRIMASK();

		__disable_irq();
		seq_realignActivePatternToMasterClock();
		seq_loopRefreshSceneSnapshots();
		__set_PRIMASK(primask);
	}
	/*
	 * S076 Rule B: clear the Scene-target automation dirty bitmap.
	 * Same rationale as seq_selectActivePattern() — see Change 15.
	 */
	seq_clearSceneAutomationDirty();
}

/*
 * Move playback to Scene 17 without a sound change (D5, D16).
 *
 * What: points the active, pending and every per-track Scene at index 16 in
 * one PRIMASK section and re-snapshots the looper for the new Scene, keeping
 * step positions, DDA accumulators, rolls, conditions, epochs and looper
 * anchors; cancels pending direct/queued Scene changes. No program change,
 * note-off or LED notify is sent (Scene 17 has no number).
 * Why: Scene 17 is an exact copy of what played, so the switch must change the
 * read address only; seq_selectActivePattern() would reset rolls/conditions,
 * realign and send PC (audible), and seq_alignActivePatternToScene() also
 * realigns.
 * Inputs: Scene 17 built (scene_backgroundExists()).
 * Outputs: seq_activePattern = seq_perTrackPattern[] = 16.
 * Context: foreground.
 * Accessors: preset_requestBackgroundBankLoad() tick.
 * Affiliates: seq_loopRefreshSceneSnapshots() (S080 looper invariants: under
 * PRIMASK, anchors kept), seq_clearPendingSceneChange(),
 * seq_refreshTrackEffectiveParams(), LiveRecord (follows the played Scene).
 */
void seq_switchToBackgroundScene(void)
{
	uint32_t primask;
	uint8_t track;

	if (!scene_backgroundExists() || !pat_patternValid(SCENE_BACKGROUND_INDEX))
		return;
	primask = __get_PRIMASK();
	__disable_irq();
	seq_previousScene = seq_activePattern;
	seq_activePattern = SCENE_BACKGROUND_INDEX;
	seq_pendingPattern = SCENE_BACKGROUND_INDEX;
	for (track = 0u; track < NUM_TRACKS; track++)
		seq_perTrackPattern[track] = SCENE_BACKGROUND_INDEX;
	seq_perTrackActive = 0u;
	seq_loadPendigFlag = 0u;
	seq_newPatternAvailable = 0u;
	seq_loopRefreshSceneSnapshots();
	__set_PRIMASK(primask);
	/*
	 * A background load owns the transport now: drop every pending Scene
	 * change (direct, queued and chain-fired) so nothing fires mid-load.
	 */
	seq_clearPendingSceneChange();
	seq_chainBarCount = 0u;
	seq_chainRepCount = 0u;
	seq_chainStopped = 0u;
	seq_refreshTrackEffectiveParams();
}

/*
 * Move selected tracks onto Scene 17 without a sound change (S082 P1 D31).
 *
 * What: under PRIMASK, points seq_perTrackPattern[t] at index 16 for each
 * track in track_mask, recomputes seq_perTrackActive and re-snapshots the
 * looper; then cancels pending Scene changes and refreshes the effective
 * track cache. Active Scene, step positions, DDA accumulators, rolls,
 * conditions, epochs, the live-record take (D35) and looper anchors are kept.
 * Why: a rebuild in place copies only the tracks whose resident Scene the
 * load replaces; only those may move, because tracks on unaffected Scenes
 * stay where they are (D31). Each moved track's data and slot were just
 * copied from the Scene it was playing, so the move changes the read address
 * only (D4).
 * Inputs: track_mask (bit t = track t), Scene 17 exists.
 * Outputs: seq_perTrackPattern[], seq_perTrackActive.
 * Context: foreground.
 * Accessors: preset_tickBackgroundBankLoad() phase 3 (rebuild).
 * Affiliates: seq_switchToBackgroundScene() (fresh build),
 * seq_recomputePerTrackActive(), seq_loopRefreshSceneSnapshots(),
 * seq_clearPendingSceneChange(), seq_refreshTrackEffectiveParams().
 */
void seq_moveTracksToBackgroundScene(uint8_t track_mask)
{
	uint32_t primask;
	uint8_t track;

	if (!scene_backgroundExists() || !pat_patternValid(SCENE_BACKGROUND_INDEX))
		return;
	primask = __get_PRIMASK();
	__disable_irq();
	for (track = 0u; track < NUM_TRACKS; track++)
		if (track_mask & (uint8_t)(1u << track))
			seq_perTrackPattern[track] = SCENE_BACKGROUND_INDEX;
	seq_recomputePerTrackActive();
	seq_loopRefreshSceneSnapshots();
	__set_PRIMASK(primask);
	seq_clearPendingSceneChange();
	seq_refreshTrackEffectiveParams();
}

/*
 * Queue raw automation entries for one scheduled fixed-grid step.
 *
 * Inputs: track and current 16-step scheduler position. Output: each valid
 * automation word is copied into the bounded pending queue with a step
 * identity/type bit; no Scene target application or descriptor/runtime write
 * occurs in TIM3 context. Full-queue events are retained as PatternTrace
 * overflow witnesses when DEV_MODE_LOGGING is enabled. Affiliates:
 * PatternData's dynamic block format and seq_drainPendingAutomation().
 */
static void seq_queueStepAutomations(uint8_t track, uint8_t step)
{
    const pat_scene_region_t *region;
    uint16_t address;
    uint16_t offset;
    const uint8_t *block;
    uint16_t header;
    uint16_t step_id;
    uint8_t flags;
    uint8_t value_count = 0u;
    uint8_t auto_count;
    uint8_t i;
    uint16_t base;

    /*
     * Per-track automation region (S077 P2 §3.2).
     *
     * What: reads the automation block from the track's own played Scene.
     * Why: automation entries are authored per-Scene; a track playing from
     * Scene B must queue Scene B's automation, not Scene A's. Inputs:
     * seq_perTrackPattern[track]. When no per-track override is set this
     * equals seq_activePattern and behaviour is unchanged.
     */
    region = pat_sceneRegion(seq_perTrackPattern[track]);
    if (!region || !pat_trackValid(track) || !pat_stepValid(step))
        return;
    address = region->address[track][step];
    if ((address & PAT_ADDR_SPECIALS_BIT) == 0u)
        return;
    offset = (uint16_t)(address & PAT_ADDR_OFFSET_MASK);
    if (offset == PAT_ADDR_SENTINEL || (offset & 3u) != 0u ||
        offset >= (PAT_STACK_SIZE * 32u))
        return;
    block = &region->pool[offset];
    header = (uint16_t)(((uint16_t)block[0] << 8u) | block[1]);
    step_id = (uint16_t)(track * NUM_STEPS + step);
    if (((header & PAT_BLOCK_STEP_ID_MASK) >> PAT_BLOCK_STEP_ID_SHIFT) !=
        step_id)
        return;
    flags = (uint8_t)(block[2] & PAT_SPECIAL_FLAGS_MASK);
    if (flags & PAT_SPECIAL_NOTE_BIT)
        value_count++;
    if (flags & PAT_SPECIAL_VEL_BIT)
        value_count++;
    if (flags & PAT_SPECIAL_COND_BIT)
        value_count++;
    if (flags & PAT_SPECIAL_ROLL_BIT)
        value_count++;
    if (flags & PAT_SPECIAL_TIMING_BIT)
        value_count++;
    if (flags & PAT_SPECIAL_RHYTHM_BIT)
        value_count++;
    if (flags & PAT_SPECIAL_FILL_BIT)
        value_count++;
    auto_count = (uint8_t)(block[1] & PAT_BLOCK_AUTO_COUNT_MASK);
    if ((uint32_t)offset +
            ((uint32_t)((PAT_BLOCK_HEADER_BYTES + 1u + value_count +
                         ((uint16_t)auto_count * 2u) + 3u) >> 2u) * 4u) >
        (PAT_STACK_SIZE * 32u))
        return;
    base = (uint16_t)(PAT_BLOCK_HEADER_BYTES + 1u + value_count);
    for (i = 0u; i < auto_count; i++) {
        uint16_t packed = (uint16_t)(block[base + (i * 2u)] |
                                     ((uint16_t)block[base + (i * 2u) + 1u]
                                      << 8u));
        /* D17's Pattern-only off entry is UI state, not playback work. */
        if ((packed & 0x01FFu) == PAT_AUTOMATION_TARGET_OFF)
            continue;
        if (seq_pending_automation_count < SEQ_PENDING_BUF_COUNT) {
            uint8_t pending_index = seq_pending_automation_count;

            seq_pending_automation[pending_index].identity =
                (uint16_t)(step_id | SEQ_PENDING_TYPE_AUTOMATION_BIT);
            seq_pending_automation[pending_index].payload = packed;
            seq_pending_automation_count = (uint8_t)(pending_index + 1u);
            seq_pending_automation_drain = 1u;
        } else {
            patternTrace_recordOverflow(
                (uint16_t)(step_id | SEQ_PENDING_TYPE_AUTOMATION_BIT), packed);
        }
    }
}

/*
 * Queue one Effect-overlay step boundary for a track (Session 072 step 9).
 *
 * The marker uses the same gate as step automation and precedes that step's
 * entries. The foreground drain turns it into StepBegin, so parameters not
 * rewritten by the owning track end their overlay. A full queue records the
 * same PatternTrace overflow witness as automation entries.
 */
/*
 * Effect-type match test for per-track FX automation (S077 P2 §3.6, Q9).
 *
 * What: compares the active Scene's retained effect type against one track's
 * played Scene's retained effect type. Why: FX automation entries are authored
 * for a specific effect type; applying them to a different type would write
 * parameters into undefined slots. Inputs: seq_activePattern,
 * seq_perTrackPattern[track]. Output: 1 when the types match (or either record
 * is unavailable is treated as no-match), 0 otherwise. Context: safe in TIM3
 * ISR - scene_effectConst() returns a const SRAM1 pointer with no allocation
 * or I/O. Affiliate: seq_drainPendingAutomation().
 */
static uint8_t seq_trackEffectTypeMatchesActive(uint8_t track)
{
    const effect_record_t *active;
    const effect_record_t *played;

    if (track >= NUM_TRACKS)
        return 0u;
    active = scene_effectConst(seq_activePattern);
    played = scene_effectConst(seq_perTrackPattern[track]);
    if (!active || !played)
        return 0u;
    return (uint8_t)(active->type == played->type);
}

/*
 * Removed: Effect step markers (S081 §5.3, R13).
 *
 * What: seq_queueEffectStepMarker() queued a marker entry with
 * SEQ_PENDING_TYPE_FX_STEP_BIT for each track step. The drain called
 * effects_automationStepBegin() on these. Under R13, Effect overlays are
 * last-writer-wins with no per-step ownership tracking, so markers are
 * unnecessary. Affiliates: seq_advanceTrackStep caller (removed),
 * SEQ_PENDING_TYPE_FX_STEP_BIT (removed), seq_drainPendingAutomation marker
 * branch (removed).
 */

/*
 * Repeat-count condition test (S079 §3; factored for S081 P2).
 *
 * What: conditions 11..19 ("X:Y") pass when repeatCount % Y == X - 1;
 * 20 ("1:-") passes only on the first play (repeatCount 0). Why: playback
 * and the ramp predictor must apply the identical rule; the predictor passes
 * the repeat count B will see. Inputs: condition 11..20, repeat count.
 * Output: pass flag. Context: TIM3. Accessors: seq_conditionEval(),
 * seq_rampConditionPredict().
 */
static uint8_t seq_conditionRepeatPasses(uint8_t cond, uint8_t repeatCount)
{
    static const uint8_t xy_table[9][2] = {
        {1u,2u},{2u,2u},{1u,3u},{2u,3u},{3u,3u},
        {1u,4u},{2u,4u},{3u,4u},{4u,4u}
    };

    if (cond == 20u)
        return (uint8_t)(repeatCount == 0u);
    return (uint8_t)((repeatCount % xy_table[cond - 11u][1]) ==
                     (uint8_t)(xy_table[cond - 11u][0] - 1u));
}

/*
 * Evaluate the per-step trigger condition (S079 §3).
 *
 * What: the single gate before a step fires. Returns 1 (fire) or 0
 * (suppress). Updates seq_trackLastConditionFired for conditional steps.
 * Why: all condition logic lives at one entry point. Condition 0 and
 * undefined values (37..255) always pass.
 *
 * Probability conditions 1..10 map to 10%..95% and are compared against the
 * hardware RNG. Repeat-count conditions 11..20 fire when
 * (seq_trackRepeatCount % Y) == (X - 1); value 20 ("1:-") fires only on the
 * first play of the track. Fill conditions 21/22 read seq_fillActive.
 * S080 B4 inserts loop conditions 23 ("lop") and 24 ("!lp") between fill and
 * last; last conditions move to 25/26 and read seq_trackLastConditionFired.
 * Mute conditions 27..40 test one voice's mute bit in seq_mutedTracks (27..33
 * muted, 34..40 not muted). Total table size is 41 (indices 0..40).
 *
 * Inputs: specials struct (condition index), track index (for mute/repeat/
 * last state). Outputs: gate result, side effect on
 * seq_trackLastConditionFired. Context: TIM3 ISR. Affiliates:
 * seq_advanceTrackStep(), GetRngValue(), seq_mutedTracks,
 * seq_trackRepeatCount, seq_fillActive, seq_trackLastConditionFired.
 */
/*
 * Shared condition dispatch with an optional side effect (S081 F-D, §9.1a).
 *
 * writeState != 0 updates seq_trackLastConditionFired (playback). 0 leaves it
 * untouched (the muted-rtk "would-have-fired" probe). A probability condition
 * still takes a fresh RNG draw either way.
 */
static uint8_t seq_conditionEval(const pat_step_specials_t *sp,
                                 uint8_t track, uint8_t writeState)
{
    uint8_t cond = sp->condition;
    uint8_t result = 1u;

    if (cond == 0u || cond > 40u)
        return 1u;

    if (cond <= 10u) {
        /* Probability: 10%..95%, values 1..10. */
        static const uint8_t prob_thresholds[10] = {
            13u, 25u, 38u, 51u, 64u, 76u, 89u, 102u, 114u, 121u
        };
        uint8_t threshold = prob_thresholds[cond - 1u];
        uint8_t rnd = (uint8_t)(((uint16_t)(GetRngValue() & 0x7FFFu) *
                                 127u) / 32767u);

        result = (uint8_t)(rnd < threshold);
    } else if (cond <= 20u) {
        /* Repeat count: X:Y, values 11..20 (shared with the S081 P2 ramp
         * predictor through seq_conditionRepeatPasses()). */
        result = seq_conditionRepeatPasses(cond, seq_trackRepeatCount[track]);
    } else if (cond <= 22u) {
        /* Fill / not-fill, values 21..22. */
        if (cond == 21u)
            result = seq_fillActive;
        else
            result = (uint8_t)(!seq_fillActive);
    } else if (cond <= 24u) {
        /*
         * Loop active / inactive, values 23..24 (S080 B4).
         *
         * What: index 23 ("lop") fires only while the PERF looper is engaged;
         * 24 ("!lp") fires only while it is not. Why: lets pattern steps react
         * to the looper. Input: seq_loopActive (single-byte, foreground set,
         * ISR read). No boundary timing - evaluated on every step. Affiliates:
         * conditionNames[] (MenuText.h B1), seq_loopEngage()/Disengage().
         */
        if (cond == 23u)
            result = seq_loopActive;
        else
            result = (uint8_t)(!seq_loopActive);
    } else if (cond <= 26u) {
        /* Last / not-last conditional step, values 25..26 (S080 B4 shift). */
        if (cond == 25u)
            result = seq_trackLastConditionFired[track];
        else
            result = (uint8_t)(!seq_trackLastConditionFired[track]);
    } else {
        /*
         * Mute / not-mute, values 27..40 (S080 B4 shift). 27..33 = "is voice N
         * muted" for voices 1..7, 34..40 = "is voice N not muted". The
         * zero-based voice index is (cond - 27) mod 7 over both halves.
         */
        uint8_t voice_idx = (uint8_t)((cond - 27u) % 7u);
        uint8_t is_muted = (uint8_t)((seq_mutedTracks >> voice_idx) & 1u);

        if (cond <= 33u)
            result = is_muted;
        else
            result = (uint8_t)(!is_muted);
    }

    if (writeState)
        seq_trackLastConditionFired[track] = result;
    return result;
}

static uint8_t seq_evaluateStepCondition(const pat_step_specials_t *sp,
                                         uint8_t track)
{
    return seq_conditionEval(sp, track, 1u);
}

/* Side-effect-free condition probe for a muted recording track (S081 C-9b). */
static uint8_t seq_conditionPassesPure(const pat_step_specials_t *sp,
                                       uint8_t track)
{
    return seq_conditionEval(sp, track, 0u);
}

/*
 * Step-roll tick table corrected to 96 PPQ (S081 §5.4, R17, Q11).
 *
 * What: the table values are in 96-PPQ ticks (the TIM3 tick rate). Previously
 * the values were 24-PPQ, making every roll 4x too fast on hardware. New table:
 * {0, 48, 32, 24, 16, 12, 8, 6, 4, 3} for indices 0..9 (off, 1/8, 8T, 16, 16T,
 * 32, 32T, 64, 64T, 128). 48 (was index 7) is dropped (same rate as 32T, F-A).
 * 64T and 128 are added. Guard changed from rate > 8 to rate > 9. Why: the S079
 * values were 24-PPQ quantities applied to the 96-PPQ tick, so every roll played
 * 4x its label (confirmed on hardware). Every existing stored roll slows to its
 * labelled rate. Stored indices shift with no migration (F-H). Inputs: rate
 * index 0..9. Outputs: ticks between roll hits.
 * Affiliates: seq_processRollTicks, seq_rollBegin, seq_processLiveTicks
 * and seq_liveRollStart/SetRate (S084 P2: PERF/MIDI overlay rolls use the
 * same table), stepRollRateNames and perfBtnTypeNames (MenuText.h; the
 * overlay roll types are step index + 5, S084 P3), menu_getRollRateName,
 * STEP roll clamp. LiveRecord's copy (liveRec_rollPpqTicks) is removed
 * (S084 P4).
 */
static uint8_t seq_rollPpq(uint8_t rate)
{
    static const uint8_t table[10] = {
        0u, 48u, 32u, 24u, 16u, 12u, 8u, 6u, 4u, 3u
    };

    if (rate > 9u)
        return 0u;
    return table[rate];
}

/*
 * Compute the velocity for the current roll retrigger position (S079 §5).
 *
 * What: decode the rhythm byte (bits 7..4 cycle length, bit 3 direction, bits
 * 2..0 full count) and return the velocity for the current position in the
 * cycle. Within the full-count window the step velocity applies; beyond it the
 * velocity is halved (direction 0) or doubled (direction 1). Halved velocity
 * floors at 1; doubled clamps at 127. Byte 0x00 and any full >= cycle decode as
 * off (no velocity change: every retrigger at step velocity). Inputs:
 * seq_rollVelPos, seq_rollStepVel, seq_rollLastRhythm. Output: velocity
 * 1..127. S083 follow-up: the decode moved unchanged into the pure
 * seq_rollRhythmVelocity() (contract in sequencer.h) so the PERF BAR
 * overlay rolls (`pv<`/`pv>`, buttonHandler) use the identical rule; this
 * wrapper only supplies the step roll's per-track state.
 */
static uint8_t seq_rollComputeVelocity(uint8_t track)
{
    return seq_rollRhythmVelocity(seq_rollLastRhythm[track],
                                  seq_rollStepVel[track],
                                  seq_rollVelPos[track]);
}

/* Contract in sequencer.h (the former body of seq_rollComputeVelocity()). */
uint8_t seq_rollRhythmVelocity(uint8_t rhythm, uint8_t vel, uint8_t pos)
{
    uint8_t r = rhythm;
    uint8_t cycle = (uint8_t)((r >> 4u) & 0x0Fu);
    uint8_t dir = (uint8_t)((r >> 3u) & 1u);
    uint8_t full = (uint8_t)(r & 0x07u);

    if (cycle == 0u)
        cycle = 1u;
    if (full == 0u)
        full = 1u;
    if (full > cycle) {         /* off-equivalent / invalid -> step velocity */
        full = 1u;
        cycle = 1u;
    }
    pos = (uint8_t)(pos % cycle);
    if (pos < full)
        return vel;
    if (dir == 0u) {
        vel = (uint8_t)(vel / 2u);
        if (vel == 0u)
            vel = 1u;
        return vel;
    }
    vel = (uint8_t)(vel * 2u);
    if (vel > 127u)
        vel = 127u;
    return vel;
}

/*
 * Does an incoming roll step continue the running roll? (S079 §4.2;
 * carry rule S084 P1, user Q4.)
 *
 * What: true when the running roll may be carried (SEQ_ROLL_CARRY: its
 * step had fill 100 and the track has not had a non-carrying step since)
 * and the incoming step has a roll with the same rate and rhythm. The
 * incoming step's fill does NOT have to match; it only sets that step's
 * window. Why: a recorded roll ends on a step whose fill is the release
 * point (< 100). S079 refused that carry, so the last step restarted the
 * roll at the step boundary, which jumped the phase and reset the velocity
 * rhythm (S084 P0 F3). User decision Q4 (2026-10-24): carry unless the
 * previous step's fill was below 100. Inputs: track, the incoming step's
 * resolved specials. Output: nonzero for a carry. Context: TIM3. Accessors:
 * seq_rollBegin(), the carry-eligibility check and the deferred-carry
 * extension in seq_advanceTrackStep(). Affiliates: SEQ_ROLL_CARRY (set by
 * seq_rollBegin(), cleared at the next non-carrying step event).
 */
static uint8_t seq_rollCarries(uint8_t track, const pat_step_specials_t *sp)
{
    return (uint8_t)((seq_rollActive[track] & SEQ_ROLL_CARRY) != 0u &&
                     sp->roll != 0u &&
                     seq_rollLastRate[track] == sp->roll &&
                     seq_rollLastRhythm[track] == sp->rhythm);
}

/*
 * Begin or carry a per-step roll at the step's fire time (S079 §4, §6;
 * time window S084 P1).
 *
 * What: computes the step's roll window, the step interval x fill / 100 in
 * whole 96-PPQ ticks, from the track's effective step scale. Without a
 * carry it fires hit #1 (reset = 1) and starts the retrigger accumulator at
 * the rate's tick interval. With a carry it fires nothing, requests the
 * reset-only renewal and leaves the accumulator and the rhythm position
 * running, so the hits continue on the same phase across the step
 * boundary. Either way the window is replaced by this step's window,
 * measured from now: the new step owns the roll from here (F-R). The step's
 * fill sets the carry flag for the next step (fill 100 only, user Q4).
 * Fill 0 gives a zero window: a single hit, ducked on the same tick.
 * Why: the S079 engine counted hits per step, and its carry lost the
 * boundary hit of every carried step (S084 P0 F2). The rate is the master
 * clock (seq_rollPpq()); the step scale only sets how long the roll lasts.
 * Inputs: track, the step's resolved specials (roll, rhythm, fill,
 * velocity, note), seq_effectiveTrackScale[track]. Outputs: hit #1 or a
 * reset-only renewal; seq_rollActive/TickAccum/VelPos/Window and the last
 * roll parameters. Context: TIM3 (seq_advanceTrackStep() on time,
 * seq_fireDeferred() when deferred). Affiliates: seq_rollCarries(),
 * seq_processRollTicks(), voiceControl_resetOnly(), stepScale_ticksQ8().
 */
static void seq_rollBegin(uint8_t track, const pat_step_specials_t *sp)
{
    uint8_t ppq = seq_rollPpq(sp->roll);
    uint8_t carry = seq_rollCarries(track, sp);
    uint16_t window;

    if (ppq == 0u) {
        seq_rollActive[track] = 0u;
        seq_rollWindow[track] = 0u;
        return;
    }
    /* Step interval x fill / 100 in whole ticks, rounded from Q8.8. */
    window = (uint16_t)(((uint32_t)stepScale_ticksQ8(
                 seq_effectiveTrackScale[track]) * sp->fill + 12800u)
                 / 25600u);
    if (!carry) {
        seq_rollTickAccum[track] = ppq;
        seq_rollVelPos[track] = 0u;
        seq_triggerVoice(track, sp->velocity, sp->note, 1);
        seq_rollVelPos[track] = 1u;
    } else {
        /*
         * Roll renewal requests a reset-only (no new trigger) (S081 §5.8).
         *
         * What: when a roll carries across a step boundary, the carry path
         * fires no new first hit. It calls voiceControl_resetOnly(track) so
         * that the pending-reset mask is set, causing the next
         * voiceControl_processPending pass to restore automation and clear the
         * menu override without an audible trigger. Why: the new step may have
         * different automation entries; the restore ensures the old step's
         * entries do not persist. Inputs: track. Outputs: pending-reset bit
         * set. Affiliates: voiceControl_resetOnly,
         * seq_restoreAutomatedParameters, the ring push in seq_advanceTrackStep.
         */
        voiceControl_resetOnly(track);   /* S081 §5.8 renewal, no hit */
    }
    seq_rollWindow[track] = window;
    seq_rollActive[track] = (uint8_t)(SEQ_ROLL_RUN |
        ((sp->fill >= 100u) ? SEQ_ROLL_CARRY : 0u));
    seq_rollLastRate[track] = sp->roll;
    seq_rollLastRhythm[track] = sp->rhythm;
    seq_rollLastFill[track] = sp->fill;
    seq_rollStepVel[track] = sp->velocity;
    seq_rollStepNote[track] = sp->note;
}

/*
 * Advance and service one fixed-grid step for one track.
 *
 * Input: track index at a sixteenth-note scheduler boundary. Output: its
 * cursor advances modulo the track's per-track length from PatternData,
 * active steps trigger with PatternData specials, and every allowed dynamic
 * block can queue raw automation even when the step has no trigger bit.
 * seq_evaluateStepCondition() runs before the trigger-active check and gates
 * the complete step: trigger and automation together. A failed condition
 * therefore leaves previously held automation values unchanged. Erase remains
 * an edit operation independent of the conditional gate.
 *
 * The wrap boundary is region->track_length[track] from the active Scene's
 * pat_scene_region_t, not the compile-time NUM_STEPS_PER_BAR constant.
 * This allows each track to loop independently at lengths 1–128. A zero or
 * inaccessible length falls back to NUM_STEPS_PER_BAR (16) so a corrupt or
 * uninitialized region never causes a stuck or runaway cursor.
 *
 * Affiliates: PatternData (region ownership, step trigger/automation data),
 * seq_drainPendingAutomation() (foreground consumer of queued automation and
 * Effect overlay markers).
 */
static void seq_advanceTrackStep(uint8_t track)
{
	/*
	 * Read the per-track loop length from the active Scene's resident region.
	 * pat_sceneRegion() returns a pointer to SRAM1 with no allocation or SD
	 * I/O, so this is safe in TIM3 ISR context (priority 2). A NULL region or
	 * a zero-length field falls back to the historic 16-step bar.
	 */
	/*
	 * Per-track played-Scene read (S077 P2 §3.2).
	 *
	 * What: reads the track's own played Scene instead of the global active
	 * Scene for pattern region, step activity, step specials, and automation.
	 * Why: when a track plays from a Scene other than the active one, its
	 * sequence data (loop length, step triggers, automation) must come from that
	 * track's assigned Scene. Inputs: seq_perTrackPattern[track] - a single-byte
	 * SRAM1 read, atomic on Cortex-M7. Output: region, step checks, and
	 * automation all resolve against the track's played Scene. When no per-track
	 * override is set this equals seq_activePattern and behaviour is identical.
	 */
	const pat_scene_region_t *region = pat_sceneRegion(seq_perTrackPattern[track]);
	/*
	 * Effective (morphed) loop length (S078 §5.4).
	 *
	 * What: the DDA owns when a step fires; the loop boundary is the cached
	 * effective length produced by seq_refreshTrackEffectiveParams() (Normal
	 * interpolated against the Scene Morph endpoint, or a step-automation
	 * overlay). Why: reading the cache lets a Morph sweep shorten or lengthen
	 * the phrase without writing the retained Pattern region. Inputs:
	 * seq_effectiveTrackLength[track]. Affiliates:
	 * presetMorph_getTrackEffectiveLength() (the cache producer).
	 */
	uint8_t len = seq_effectiveTrackLength[track];
	if (len < 1u)
		len = NUM_STEPS_PER_BAR;
	uint8_t play_mode = (region) ? region->track_play_mode[track] : 0u;

	/*
	 * Wrap immediately when a Morph sweep shortens the loop (S078 §5.1).
	 *
	 * What: an index left beyond the new effective length is parked at the last
	 * step before the mode advance below, so forward mode wraps to step 0 on
	 * this boundary and reverse/pip modes cannot crawl backwards to re-enter
	 * range over many steps. Why: a Morph that shrinks the phrase must take
	 * effect at once. Inputs: seq_stepIndex[track], effective len. Output:
	 * index clamped into 0..len-1.
	 */
	if (seq_stepIndex[track] >= (int16_t)len)
		seq_stepIndex[track] = (int16_t)(len - 1u);

	/*
	 * Play mode direction logic (S078 §4.4).
	 *
	 * What: replaces the unconditional increment-and-wrap with a per-track
	 * direction switch. FWD: existing behaviour. REV: decrement, wrap at 0 to
	 * length-1. PIP: cycle of 2×length, boundaries play twice; the direction
	 * bit in seq_trackPlayState[] disambiguates the two occurrences of each
	 * boundary index. RND: uniform random within length. ONC/1FR: forward
	 * one-pass, set the stopped flag at the end. Why: allows each track to have
	 * an independent playback direction and mode. Values >= 6 are treated as
	 * fwd for forward compatibility. Inputs: region->track_play_mode[track],
	 * seq_trackPlayState[track]. Outputs: seq_stepIndex[track] updated,
	 * stopped flag set for once modes. Affiliates: seq_trackPlayState[]
	 * retrigger logic, seq_setTrackPlayedScene().
	 * Mirrored side-effect-free by seq_rampNextStep() (S081 P2); change both
	 * together.
	 */
	if (play_mode >= 6u)
		play_mode = 0u;
	if ((play_mode == 4u || play_mode == 5u) &&
	    (seq_trackPlayState[track] & SEQ_PLAY_STATE_STOPPED))
		return;
	switch (play_mode) {
	case 1u: /* rev */
		seq_stepIndex[track]--;
		if (seq_stepIndex[track] < 0) {
			seq_stepIndex[track] = (int16_t)(len - 1u);
			/* Wrap 0 -> len-1 counts as one traversal (S079 §3.2). */
			seq_trackRepeatCount[track]++;
		}
		break;
	case 2u: /* pip */
		if (seq_trackPlayState[track] & SEQ_PLAY_STATE_PIP_REV) {
			if (seq_stepIndex[track] <= 0) {
				seq_trackPlayState[track] &=
				    (uint8_t)~SEQ_PLAY_STATE_PIP_REV;
				seq_trackRepeatCount[track]++;
			} else {
				seq_stepIndex[track]--;
			}
		} else {
			if (seq_stepIndex[track] >= (int16_t)(len - 1u)) {
				seq_trackPlayState[track] |= SEQ_PLAY_STATE_PIP_REV;
				seq_trackRepeatCount[track]++;
			} else {
				seq_stepIndex[track]++;
			}
		}
		break;
	case 3u: /* rnd */
		seq_stepIndex[track] =
		    (int16_t)(((uint16_t)GetRngValue() & 0x7FFFu) % len);
		if (++seq_trackRndStepCount[track] >= len) {
			seq_trackRndStepCount[track] = 0u;
			seq_trackRepeatCount[track]++;
		}
		break;
	case 4u: /* onc — once, aligned to the master clock at entry */
	case 5u: /* 1fr — once, always from step zero */
		seq_stepIndex[track]++;
		if (seq_stepIndex[track] >= (int16_t)len) {
			seq_stepIndex[track] = (int16_t)(len - 1u);
			seq_trackPlayState[track] |= SEQ_PLAY_STATE_STOPPED;
			return;
		}
		break;
	case 0u: /* fwd */
	default:
		seq_stepIndex[track]++;
		if (seq_stepIndex[track] >= (int16_t)len) {
			seq_stepIndex[track] = 0;
			seq_trackRepeatCount[track]++;
		}
		break;
	}

	if (seq_SomModeActive) {
		if (track == 0u)
			som_tick(seq_stepIndex[0], seq_mutedTracks);
		return;
	}

	{
		uint8_t track_muted = (uint8_t)((seq_mutedTracks >> track) & 1u);
		uint8_t process_step = (uint8_t)(!track_muted);
		/* Fired bit reported to the boundary ring (S081 §9.1a, C-9a). */
		uint8_t rec_fired = 0u;
		/* S081 P2: what this step did with its automation (ramp hand-off). */
		uint8_t ramp_applied = 0u;
		uint8_t ramp_deferred = 0u;

		/*
		 * Mute gate with self-mute bypass (S079 §3.5).
		 *
		 * What: muted tracks normally skip all step processing. However, if a
		 * step on a muted track has a mute condition that tests its own track
		 * (e.g. "3mu" on track 3), the mute is bypassed and the condition is
		 * evaluated. Why: a self-mute condition is meaningless without bypass -
		 * the step would never be reached. Self-mute conditions test the
		 * zero-based voice index equal to this track. Inputs: seq_mutedTracks
		 * bitmask, the step's address entry, and its pool condition byte.
		 * Output: process_step is set when the step should run despite the
		 * mute. Affiliates: pat_readStepSpecials(), seq_evaluateStepCondition().
		 */
		if (track_muted) {
			const pat_scene_region_t *mute_region =
			    pat_sceneRegion(seq_perTrackPattern[track]);
			uint16_t mute_addr = mute_region
			    ? mute_region->address[track][(uint8_t)seq_stepIndex[track]]
			    : PAT_ADDR_SENTINEL;

			if ((mute_addr & PAT_ADDR_SPECIALS_BIT) != 0u) {
				pat_step_specials_t msp = pat_readStepSpecials(
				    seq_perTrackPattern[track], track,
				    (uint8_t)seq_stepIndex[track]);
				uint8_t cond = msp.condition;
				uint8_t self_mute = 0u;

				/* S080 B4: mute condition ranges shifted +2 by lop/!lp. */
				if (cond >= 27u && cond <= 33u &&
				    (uint8_t)(cond - 27u) == track)
					self_mute = 1u;
				else if (cond >= 34u && cond <= 40u &&
				    (uint8_t)(cond - 34u) == track)
					self_mute = 1u;
				if (self_mute &&
				    seq_evaluateStepCondition(&msp, track))
					process_step = 1u;
			}
		}

		/*
		 * Would-have-fired for a muted recording track (S081 F-D, C-9b).
		 *
		 * What: when this track is the recording track and is muted, evaluate its
		 * step condition without side effects (seq_conditionPassesPure) so the ring's
		 * fired bit still counts steps that would have fired. Why: F-D counts fired
		 * steps on a muted rtk; nothing plays. Affiliates: seq_evaluateStepCondition.
		 */
		/*
		 * Would-have-fired for a muted recording track (S081 F-D, P1B item 4).
		 *
		 * What: on a muted rtk, the ring's fired bit is set only when the step holds
		 * a trigger (bit 15) AND its condition would pass, evaluated without side
		 * effects (seq_conditionPassesPure). Why: F-D counts a muted step as fired
		 * only if it would have fired. Without the bit-15 test every empty step of a
		 * muted rtk counted as fired, and LATCH re-stamped own-Instrument values onto
		 * every step of the track. Inputs: the step's address entry and specials,
		 * seq_recWriting, seq_recTrack. Output: rec_fired for the ring push (C-9a).
		 * Context: TIM3. Affiliates: seq_conditionEval(), the ring push below,
		 * liveRec_stepEnd().
		 */
		if (!process_step && seq_recWriting && track == seq_recTrack) {
			pat_step_specials_t rsp = pat_readStepSpecials(
			    seq_perTrackPattern[track], track, (uint8_t)seq_stepIndex[track]);
			rec_fired = (uint8_t)(pat_isStepActive(track,
			                          (uint8_t)seq_stepIndex[track],
			                          seq_perTrackPattern[track]) &&
			                      seq_conditionPassesPure(&rsp, track));
		}

		if (process_step) {
		/*
		 * Resolve specials and evaluate the step gate before checking bit 15.
		 * Bit 14 owns both conditional values and automation, so a non-trigger
		 * step is still a complete playback step for automation purposes.
		 */
		pat_step_specials_t sp = pat_readStepSpecials(
		    seq_perTrackPattern[track], track, (uint8_t)seq_stepIndex[track]);
		uint8_t step_allowed = seq_evaluateStepCondition(&sp, track);
		/* Set when this step's own trigger is deferred (A1-3b); the step-allowed
		 * automation block below is then skipped so the entries queue at the
		 * deferred fire instead. */
		uint8_t deferred_this_step = 0u;
		/* Set when this step's stored trigger is suppressed (C-9c). */
		uint8_t trigger_suppressed = 0u;

		/*
		 * Fired bit: bit 15 set and the condition passed, known at the step start
		 * whether or not the trigger is deferred (S081 §9.1a, C-9a).
		 */
		rec_fired = (uint8_t)(step_allowed &&
		    pat_isStepActive(track, (uint8_t)seq_stepIndex[track],
		                     seq_perTrackPattern[track]));

		/*
		 * A running roll ends only when the next step has a trigger that fires
		 * (S081 F-R, §3.3-16).
		 *
		 * What: the previous code ended a roll at any step with sp.roll == 0,
		 * including empty and automation-only steps. The new code ends a roll
		 * only when the step has a trigger (bit 15 set) and its condition passes.
		 * An empty step does not cut the roll; the roll engine ends it naturally
		 * after its fill window (seq_rollWindow reaches 0). Why: a roll delayed
		 * into the next step's time was immediately cut by that boundary, losing
		 * the last hits. With this change, a roll runs its own length from its
		 * delayed first hit, as designed. Inputs: sp.roll, step trigger state,
		 * condition result. Outputs: seq_rollActive may be cleared. Affiliates:
		 * seq_rollBegin carry path, seq_rollWindow, seq_processRollTicks.
		 * S084 P1: the cut clears the window and both flags, so neither a
		 * retrigger nor a duck nor a carry follows the plain hit.
		 */
		if (seq_rollActive[track] && sp.roll == 0u &&
		    pat_isStepActive(track, (uint8_t)seq_stepIndex[track],
		                     seq_perTrackPattern[track]) &&
		    step_allowed) {
			seq_rollActive[track] = 0u;
			seq_rollWindow[track] = 0u;
		}

		/*
		 * Consume an echo mark and detect a live-hold suppression for this step
		 * (S081 §8.6, F-P, C-9c). Automation still queues (A1-3c).
		 */
		{
			uint8_t echo_hit = 0u;

			/*
			 * Expire a stale echo mark before testing it (S081 §8.6, P1B items 6/25).
			 *
			 * What: a mark whose deadline has passed is dropped without effect. Then,
			 * as before, a mark equal to this step is consumed and the stored trigger
			 * skipped (fired still 1). Why: covers every path where the playhead never
			 * reaches the mark (run-mode jumps, length/play-mode changes, once park).
			 * Context: TIM3. Affiliates: seq_suppressStepOnce(), seq_echoDeadline[].
			 */
			if (seq_echoStep[track] >= 0 &&
			    (int16_t)((uint16_t)seq_elapsedPpqTicks -
			              seq_echoDeadline[track]) > 0)
				seq_echoStep[track] = -1;
			if (step_allowed && seq_echoStep[track] >= 0 &&
			    (uint8_t)seq_echoStep[track] == (uint8_t)seq_stepIndex[track]) {
				echo_hit = 1u;
				seq_echoStep[track] = -1;
			}
			trigger_suppressed = (uint8_t)(step_allowed &&
			    (echo_hit ||
			     (seq_liveHoldMask & (uint8_t)(1u << track)) != 0u));
		}

		/*
		 * Carry eligibility lasts exactly one step (S084 P1).
		 *
		 * What: at every step event of the track, a roll that could be
		 * carried keeps that right only if THIS step will carry it: its
		 * trigger fires (bit 15 and the condition, rec_fired), it is not
		 * suppressed by an echo mark or a live hold, it is not being erased,
		 * and its roll has the same rate and rhythm (seq_rollCarries()). If
		 * not, SEQ_ROLL_CARRY is cleared. A roll whose window is still open
		 * keeps running to its window end (F-R: an empty step does not cut
		 * it); a parked roll (window already closed) ends here. Why: a
		 * fill-100 roll parks at its window end so that the ±1 tick jitter of
		 * a fractional step interval, or a shuffled or micro-timed carrying
		 * step, cannot break the carry. Without this check a parked roll would
		 * stay carryable across empty steps and resume on a stale phase.
		 * Inputs: rec_fired, trigger_suppressed, seq_eraseFullMask,
		 * seq_eraseSpecialsMask, sp. Output: SEQ_ROLL_CARRY cleared or kept.
		 * Context: TIM3. Affiliates: seq_rollCarries(), seq_rollBegin(),
		 * seq_processRollTicks() (parking), the deferred-carry extension
		 * below.
		 */
		if ((seq_rollActive[track] & SEQ_ROLL_CARRY) != 0u &&
		    !(rec_fired && !trigger_suppressed &&
		      ((seq_eraseFullMask | seq_eraseSpecialsMask) &
		       (uint8_t)(1u << track)) == 0u &&
		      seq_rollCarries(track, &sp)))
			seq_rollActive[track] &= (uint8_t)~SEQ_ROLL_CARRY;

		if (pat_isStepActive(track, (uint8_t)seq_stepIndex[track], seq_perTrackPattern[track])) {
			if ((seq_eraseFullMask & (uint8_t)(1u << track)) != 0u) {
				/*
				 * Live erase (full) clears the trigger and queues reclamation.
				 *
				 * S081 G-5: seq_eraseFullMask replaces the old active-voice guard.
				 * Pool
				 * reclamation is a foreground PatternStackService event so this
				 * ISR never mutates pool bytes or bitmap state.
				 *
				 * S077 P2 §3.2: erase targets the pattern data the track is
				 * actually playing, not necessarily the global active Scene.
				 * Erasing from the active Scene while the track plays from a
				 * different Scene would clear steps that are not audible.
				 */
				pat_setStepActive(seq_perTrackPattern[track], track,
				                  (uint8_t)seq_stepIndex[track], 0u);
				patSvc_enqueueErase(seq_perTrackPattern[track], track,
				                    (uint8_t)seq_stepIndex[track]);
			} else if ((seq_eraseSpecialsMask & (uint8_t)(1u << track)) != 0u) {
				/* S081 G-5: specials erase keeps automation; TIM3 clears bit 15. */
				pat_setStepActive(seq_perTrackPattern[track], track,
				                  (uint8_t)seq_stepIndex[track], 0u);
				patSvc_enqueueEraseSpecials(seq_perTrackPattern[track], track,
				                            (uint8_t)seq_stepIndex[track]);
			} else if (step_allowed && !trigger_suppressed) {
				/*
				 * Trigger emission: per-step roll or shuffle/micro-timing
				 * deferral (S078 §3.2, S079 §4, §7).
				 *
				 * What: a step with a roll rate hands its complete trigger
				 * stream to seq_rollBegin() (which fires retrigger #1 and
				 * arms the tick engine). Otherwise odd-indexed steps with
				 * nonzero shuffle are deferred by (shuffle x 24) / 256 PPQ
				 * ticks and micro-timing adds (step_interval x timing) / 100
				 * ticks on top. Why: shuffle is a 1/16-grid groove; micro-timing
				 * is relative to the track's own step interval. Inputs:
				 * seq_effectiveTrackShuffle[track], sp.roll, sp.timing,
				 * seq_stepIndex[track] parity. Outputs: roll start, immediate
				 * trigger, or combined deferred trigger setup. A pending
				 * deferral is flushed first so a fast track cannot lose an
				 * un-fired odd step. Affiliates: seq_processDeferredTriggers(),
				 * seq_rollBegin().
				 */
				/*
				 * Effective (morphed) shuffle (S078 §5.1/§3.2): the
				 * Normal amount interpolated against the Scene Morph
				 * endpoint, or a step-automation overlay.
				 */
				uint8_t shuffle_val = seq_effectiveTrackShuffle[track];
				uint8_t is_odd_step =
				    (uint8_t)(seq_stepIndex[track] & 1);

				/*
				 * Roll steps honour shuffle and micro-timing (S081 §3.3-15, F-S).
				 *
				 * What: both roll and non-roll triggers are deferred by the same
				 * shuffle + micro-timing delay. The deferral stores the step index
				 * and a roll flag so the deferred fire can start a roll
				 * (seq_rollBegin) or a plain hit. When a trigger is deferred, the
				 * local `deferred_this_step` is set so the step-allowed block below
				 * does not queue this step's automation now: seq_fireDeferred()
				 * queues it when the trigger fires. Why: pre-existing defect, roll
				 * steps ignored shuffle and micro-timing; and queueing automation at
				 * the step start let the delayed trigger's restore undo it (§3.3-1).
				 * Inputs: sp.roll, shuffle_val, sp.timing, step index. Outputs:
				 * seq_trackShuffle* deferral state, deferred_this_step. Affiliates:
				 * seq_fireDeferred(), seq_rollBegin(), the step-allowed automation
				 * block (A1-3c).
				 */
				if (seq_trackShufflePending[track]) {
					/* Fire the earlier step's deferral and its automation first, so
					 * entries stay in step order and a deferred roll keeps its roll
					 * (replaces the old inline flush). */
					seq_fireDeferred(track);
				}
				{
					uint8_t total_delay = 0u;

					if (shuffle_val > 0u && is_odd_step &&
					    len > 0u)
						total_delay = (uint8_t)(
						    ((uint16_t)shuffle_val * 24u) / 256u);
					if (sp.timing > 0u) {
						uint16_t mt_interval = (uint16_t)(
						    stepScale_ticksQ8(
						        seq_effectiveTrackScale[track])
						    >> 8u);
						total_delay = (uint8_t)(total_delay +
						    (uint8_t)(((uint32_t)mt_interval *
						               sp.timing) / 100u));
					}
					if (total_delay > 0u) {
						seq_trackShuffleDelay[track] =
						    (uint8_t)(total_delay - 1u);
						seq_trackShufflePending[track] = 1u;
						seq_trackShuffleVel[track] = sp.velocity;
						seq_trackShuffleNote[track] = sp.note;
						seq_trackShuffleStep[track] =
						    (uint8_t)seq_stepIndex[track];
						seq_trackShuffleRoll[track] =
						    (uint8_t)(sp.roll > 0u);
						/*
						 * Keep a carried roll sounding through its deferral
						 * (S084 P1).
						 *
						 * What: when a step that will carry the running roll
						 * is deferred by shuffle and/or micro-timing, the
						 * roll's window is stretched (never shortened) to
						 * reach the deferred fire, and a parked roll is
						 * reopened. At the fire, seq_rollBegin() carries it
						 * and sets the step's own window. Why: the previous
						 * step's window ends at this step's boundary; without
						 * the stretch the hits that fall between the boundary
						 * and the deferred fire would be lost, which is the
						 * F2 gap again on every shuffled step. Inputs:
						 * total_delay (ticks to the fire), sp. Outputs:
						 * seq_rollWindow, SEQ_ROLL_RUN. A muted fire
						 * (seq_cancelPendingTrigger()) carries nothing, so the
						 * roll parks at the stretched window end and the next
						 * step event ends it. Context: TIM3. Affiliates:
						 * seq_fireDeferred(), seq_rollBegin(),
						 * seq_rollCarries().
						 */
						if (sp.roll > 0u &&
						    seq_rollCarries(track, &sp)) {
							if (seq_rollWindow[track] < total_delay)
								seq_rollWindow[track] = total_delay;
							seq_rollActive[track] |= SEQ_ROLL_RUN;
						}
						/* A fresh deferral is never muted (P1B item 7). */
						seq_trackShuffleMuteMask &= (uint8_t)~(1u << track);
						deferred_this_step = 1u;
					} else if (sp.roll > 0u) {
						seq_rollBegin(track, &sp);
					} else {
						seq_triggerVoice(track, sp.velocity, sp.note, 1);
					}
				}
			}
		}

		/*
		 * Automation follows the conditional gate, not trigger state. Preserve
		 * the existing live-erase guard so the active edit track does not apply
		 * a queued automation value while its trigger is being removed.
		 */
        /*
         * Automation follows the conditional gate, not trigger state
         * (PATTERN_DYNAMIC_STACK.md §6.1; S081 §5.8).
         *
         * What: the existing call stays in the step-allowed block, so every
         * step whose condition passes queues its automation at the step start,
         * including steps with no trigger. The one exception: when this step's
         * own trigger was deferred (deferred_this_step), the call is skipped and
         * seq_fireDeferred() queues the entries at the deferred fire, together
         * with the resetting trigger. Before queueing, a still-pending deferral
         * from an earlier step on this track is fired first (seq_fireDeferred),
         * so entries stay in step order even when the next step has automation
         * but no trigger. Why: automation-only steps are a product feature and
         * must never depend on trigger state; only a step whose reset is delayed
         * needs its automation delayed with it. The live-erase guard on this call
         * is unchanged until Phase G (G-5).
         */
        if (step_allowed &&
            ((seq_eraseFullMask & (uint8_t)(1u << track)) == 0u)) {
            /*
             * Keep entry order without cutting delays short (S081 A1-3c, P1B
             * item 23).
             *
             * What: a deferral still pending from an earlier step is fired early
             * only when this step is about to queue automation entries. Otherwise
             * it keeps its own timing and fires at expiry. Why: the early fire
             * exists to keep the earlier step's entries ahead of this step's
             * entries in the pending queue. Firing it at every step start short-
             * ened long delays (shuffle + up to 99 % micro-timing) to the step
             * boundary even when the next step was empty.
             * pat_stepAutomationCount() only reads the address entry and block
             * header, so it is TIM3-safe like seq_queueStepAutomations().
             */
            if (seq_trackShufflePending[track] && !deferred_this_step &&
                pat_stepAutomationCount(seq_perTrackPattern[track], track,
                                        (uint8_t)seq_stepIndex[track]) != 0u)
                seq_fireDeferred(track);
            if (!deferred_this_step) {
                /* S081 §5.3: the Effect step marker is removed (R13); the
                 * step's own automation entries are queued directly. */
                seq_queueStepAutomations(track, (uint8_t)seq_stepIndex[track]);
                ramp_applied = 1u;
            } else {
                ramp_deferred = 1u;
            }
        }
		}

		/* S081 P2: hand the step to the automation ramp engine. */
		seq_rampStepEvent(track, ramp_applied, ramp_deferred);

		/*
		 * Report the recording track's step boundaries (S081 §9.1a, §19.1 item 1,
		 * C-9a).
		 *
		 * What: when seq_recWriting is set and (track == seq_recTrack or the
		 * track's live-hold bit is set), push {step, track, fired} into seq_recRing.
		 * The entry marks the START of this step. LiveRecord keeps the last entry
		 * per track and treats that track's next entry as "the previous step ended".
		 * fired = bit 15 set && the condition passed, known at the step start; it is
		 * set for an echo-suppressed step (the live hit was its sound). A full ring
		 * drops the entry (DEV: counted, trace cause 5, P1C item 13). Why:
		 * LiveRecord's step-end table needs every step of rtk and whether its first
		 * trigger fired. Affiliates: seq_takeRecBoundary(), liveRec_service().
		 * The push itself is seq_recRingPush() (S081 P3), shared with the
		 * looper's jump entries.
		 */
		seq_recRingPush(track, seq_stepIndex[track], rec_fired, 0u);
	}

	/*
	 * Removed: legacy PAR_ROLL performance roll block (S081 §3.2, A3-5b).
	 * Superseded by the overlay trigger system (S080) and step-roll recording.
	 */
}

/*
 * Apply one Scene-target automation value as a runtime-only overlay.
 *
 * Inputs: canonical Scene target ID and its seven-bit Pattern value. Output:
 * the owning DSP runtime receives a clamped value, while retained Scene/Kit
 * data, AutoSave dirty state, and Bank-clean state remain untouched. Voice
 * Morph expands stored 0..126 to 0..252 and stored 127 to 255 so its endpoint
 * remains reachable. A successful runtime overlay sets the corresponding bit
 * for transport-boundary restoration. FX_SEND updates the transient send
 * overlay consumed by the mixer on the next block. Common caller:
 * seq_drainPendingAutomation(). Affiliates: SceneModTargets, Preset, and
 * InstrumentManager runtime overlay APIs.
 */
static uint8_t seq_applySceneAutomation(uint16_t target, uint8_t value)
{
	const scene_mod_target_descriptor_t *descriptor =
		sceneModTarget_descriptor(target);
	uint8_t index;

	if (!descriptor)
		return 0u;
	if (value > descriptor->max_value)
		value = (uint8_t)descriptor->max_value;
	if (!sceneModTarget_indexFromId(target, &index))
		return 0u;

	switch (descriptor->kind) {
	case SCENE_MOD_TARGET_KIND_VOICE_MORPH: {
		uint8_t morph = (value < 127u) ? (uint8_t)(value * 2u) : 255u;

		/*
		 * Morph is a transient base replacement, not a retained Scene edit.
		 * parameter_values[] is only the live PERF mirror and is restored from
		 * SceneData at the same transport boundary as the Morph worker.
		 */
		parameter_values[PAR_VOICE1_MORPH + descriptor->voice_slot] = morph;
		presetMorph_setStepAutomationOverride(
			scene_getActiveIndex(), descriptor->voice_slot, morph);
		break;
	}
	case SCENE_MOD_TARGET_KIND_SLOT6_TRACK7_AMP_DECAY:
		instrumentManager_setSlot6Track7StepDecayOverride(value);
		break;
	case SCENE_MOD_TARGET_KIND_AUDIO_OUT:
		/* Keep the live superpage value aligned with the DSP route. */
		preset_applyVoiceAudioOutRuntime(descriptor->voice_slot, value);
		preset_setAudioOutStepOverride(descriptor->voice_slot, value);
		break;
	case SCENE_MOD_TARGET_KIND_FX_SEND:
		/* Store the runtime overlay consumed by the mixer on the next block. */
		preset_setFxSendStepOverride(descriptor->voice_slot, value);
		break;
	case SCENE_MOD_TARGET_KIND_EFFECT_MORPH:
		/* `fxm` expands like voice Morph and stays runtime-only until reset. */
		effects_setMorphAutomation(effect_expand7Linear(value));
		break;
	case SCENE_MOD_TARGET_KIND_TRACK_LENGTH:
	case SCENE_MOD_TARGET_KIND_TRACK_SCALE:
	case SCENE_MOD_TARGET_KIND_TRACK_SHUFFLE: {
		/*
		 * Per-track timing step automation (S078 §5.5).
		 *
		 * What: routes the 7-bit Pattern value to a runtime-only overlay
		 * for the target track's length, scale, or shuffle. Why: the
		 * overlay is consumed by the effective track getters, so step
		 * automation changes playback without overwriting the retained
		 * Pattern Normal values or raising a Pattern dirty bit.
		 * Inputs: descriptor->voice_slot carries the track index.
		 * Outputs: presetMorph overlay set. Affiliates:
		 * presetMorph_setTrackParamStepOverride(),
		 * seq_restoreAllSceneAutomation() (the transport clear).
		 */
		presetMorph_trackParam_t param;

		if (descriptor->voice_slot >= NUM_TRACKS)
			return 0u;
		switch (descriptor->kind) {
		case SCENE_MOD_TARGET_KIND_TRACK_LENGTH:
			param = PRESETMORPH_TRACK_LENGTH;
			break;
		case SCENE_MOD_TARGET_KIND_TRACK_SCALE:
			param = PRESETMORPH_TRACK_SCALE;
			break;
		default:
			param = PRESETMORPH_TRACK_SHUFFLE;
			break;
		}
		presetMorph_setTrackParamStepOverride(descriptor->voice_slot,
		                                      param, value);
		/*
		 * S078 §5.10: step automation writes the effective cache directly so
		 * playback changes on the next DDA tick; the overlay keeps the value
		 * stable until the transport/Pattern restore recomputes it.
		 */
		if (param == PRESETMORPH_TRACK_LENGTH)
			seq_effectiveTrackLength[descriptor->voice_slot] = value;
		else if (param == PRESETMORPH_TRACK_SCALE)
			seq_effectiveTrackScale[descriptor->voice_slot] = value;
		else
			seq_effectiveTrackShuffle[descriptor->voice_slot] = value;
		break; }
	default:
		return 0u;
	}

	seq_scene_automation_dirty |= (1u << index);
	return 1u;
}

/*
 * Apply queued step automation after front-panel service.
 *
 * Inputs: the volatile four-byte queue published by TIM3. Output: valid voice
 * descriptor targets update their owning runtime image through
 * InstrumentManager; Scene targets dispatch through seq_applySceneAutomation()
 * to their Preset/Scene owners and never enter the voice retrigger-restore
 * bitmap. FX reset is taken before this pass; FX step markers bracket Effect
 * overlay end candidates, and the final marker group is flushed after the
 * queue handoff. The foreground follows the live producer count and atomically
 * resets only after no append raced the drain. Affiliate: main.c's pre-audio
 * foreground sequence.
 * S081 P2: before applying entries it ends the ramps of tracks whose next
 * step has applied; each written Instrument entry of an rmp track may start a
 * ramp toward the same target on the predicted next step
 * (seq_rampStartIfPaired()).
 */
void seq_drainPendingAutomation(void)
{
    uint8_t i = 0u;

    /* Reset Effect overlays before any records from the new pass apply. */
    if (seq_effectAutomationReset) {
        uint32_t primask = __get_PRIMASK();

        __disable_irq();
        seq_effectAutomationReset = 0u;
        __set_PRIMASK(primask);
        effects_automationReset();
    }

    /* S081 P2: ramps of tracks whose next step applied end first. */
    seq_rampConsumeSteps();

    if (!seq_pending_automation_drain)
        return;

    /*
     * Consume the monotonically growing producer range. The interrupt-safe
     * handoff below closes the only race: an append between the last count
     * read and queue reset is detected and left for this same drain pass.
     */
    for (;;) {
        while (i < seq_pending_automation_count) {
            uint16_t identity = seq_pending_automation[i].identity;
            uint16_t packed = seq_pending_automation[i].payload;
            uint16_t target = (uint16_t)(packed & 0x01FFu);
            /* Automation storage is already in descriptor parameter space;
             * do not apply MIDI-CC-style 7-bit-to-8-bit expansion here. */
            uint8_t value = (uint8_t)((packed >> 9u) & 0x7Fu);
            /*
             * Per-track ownership for this entry (S077 P2 §1.16).
             *
             * What: the writing track is embedded in the packed step
             * identity as (step_id / NUM_STEPS); its played Scene selects the
             * instrument image used for validation and the effect type used by
             * the FX gate. Output: owner_track 0..6, owner_scene resident
             * index. When no per-track override is set owner_scene equals
             * seq_activePattern and behaviour is unchanged.
             */
            uint8_t owner_track = (uint8_t)((identity &
                                             SEQ_PENDING_STEP_ID_MASK) /
                                            NUM_STEPS);
            uint8_t owner_scene = seq_getTrackPlayedScene(owner_track);

            /*
             * Filter entries against registered latch and erase bitmaps
             * (S081 §19.1, C-7c).
             *
             * What: skip an entry whose target is latched (any owner track: the
             * menu value is what sounds), or whose target is being erased on the
             * owner track. Why: latched/erased parameters must not play back
             * their stored automation. Instrument overrides are handled in the
             * voice branch below (A1-4d). Inputs: seq_filterLatch,
             * seq_filterErase, target, owner_track.
             */
            if (seq_filterLatch != 0 &&
                (seq_filterLatch[target >> 3u] &
                 (uint8_t)(1u << (target & 7u)))) {
                i++;
                continue;
            }
            if (seq_filterErase != 0 && owner_track == seq_filterEraseTrack &&
                (seq_filterErase[target >> 3u] &
                 (uint8_t)(1u << (target & 7u)))) {
                i++;
                continue;
            }

            /*
             * Marker branch removed (S081 §5.3, R13): entries are now only
             * automation records; Effect overlays are last-writer-wins.
             */
            if ((identity & SEQ_PENDING_TYPE_AUTOMATION_BIT) != 0u) {
                if (instrumentParam_isVoiceParameter(target) &&
                    instrumentManager_targetValid(owner_scene, target,
                                                  INSTRUMENT_TARGET_AUTOMATION)) {
                    uint8_t slot = instrumentParam_slot(target);

                    /*
                     * Skip overridden parameters in the drain (S081 §5.1, R12).
                     *
                     * What: for each Instrument-parameter entry, check
                     * seq_menuOverride[slot] bit for the parameter's local
                     * index. If set, skip that entry - the menu edit takes
                     * priority. Why: R12 says the menu value suppresses
                     * automation until the next trigger. The override bit is
                     * cleared by seq_restoreAutomatedParameters at the next
                     * trigger, so automation resumes. Affiliates:
                     * seq_menuOverrideSet, seq_restoreAutomatedParameters.
                     */
                    if (slot < INSTRUMENT_SLOT_COUNT &&
                        (seq_menuOverride[slot] &
                         (1ULL << instrumentParam_local(target)))) {
                        i++;
                        continue;
                    }
                    /*
                     * Write the value and mark it held for the next trigger's
                     * restore (seq_writeInstrumentAutomation(), shared with
                     * the S081 P2 ramp service). A successful write of an
                     * rmp track's entry may start a ramp toward the same
                     * target on the predicted next step.
                     */
                    {
                        const ParamDescriptor *descriptor =
                            seq_writeInstrumentAutomation(owner_scene, target,
                                                          value);

                        if (descriptor)
                            seq_rampStartIfPaired(owner_track, owner_scene,
                                (uint8_t)((identity & SEQ_PENDING_STEP_ID_MASK) %
                                          NUM_STEPS),
                                target, value, descriptor);
                    }
                } else if (sceneModTarget_isSceneTarget(target)) {
                    (void)seq_applySceneAutomation(target, value);
                } else if (effectTarget_isEffectId(target)) {
                    /*
                     * Effect automation type gate (S077 P2 §1.17, Q9).
                     *
                     * What: before applying a block-7 (Effect) entry, compare
                     * the active Scene's effect type against the owning
                     * track's played Scene's effect type. If the types differ,
                     * drop the entry. Why: effect parameters are type-specific;
                     * writing values authored for one effect type into another
                     * would touch undefined slots. Fast path: when no per-track
                     * override is set the types are trivially equal, so the
                     * comparison is skipped. Block-7 entries are owned by the
                     * writing track via effects_applyAutomation().
                     */
                    if (!seq_perTrackActive ||
                        seq_trackEffectTypeMatchesActive(owner_track))
                        (void)effects_applyAutomation(owner_track,
                            effectTarget_local(target), value);
                }
            }
            i++;
        }

        {
            uint32_t primask;

            __asm volatile("mrs %0, primask\n\tcpsid i"
                           : "=r"(primask) :: "memory");
            if (i == seq_pending_automation_count) {
                seq_pending_automation_count = 0u;
                seq_pending_automation_drain = 0u;
                __asm volatile("msr primask, %0" :: "r"(primask)
                               : "memory");
                return;
            }
            __asm volatile("msr primask, %0" :: "r"(primask)
                           : "memory");
        }
    }
}

/*
 * Restore automated descriptor parameters immediately before a trigger.
 *
 * Inputs: visible trigger track 0..6, where track 6 shares slot 5's
 * descriptor image. Output: every dirty descriptor-local runtime value for
 * that slot is restored from morph_interpolation[] (the value the morph
 * crossfader would currently apply), then the slot bitmap is cleared.
 * Invalid/stale bits are discarded safely; Scene-level targets remain
 * outside this voice-descriptor restore path until their runtime boundary is
 * defined. Affiliate: MidiVoiceControl.c's single trigger funnel.
 * S081 P2 (D3): the trigger track's own ramps end first
 * (seq_rampConsumeTrack); parameters still held by a running ramp of this
 * slot (a live press, MIDI note, flam main or roll renewal mid-step) are
 * neither restored nor un-marked, so the ramp continues without a dip to the
 * menu value. Ramps of the other track sharing slot 5 (tracks 5/6) keep
 * running.
 */
void seq_restoreAutomatedParameters(uint8_t trigger_track)
{
    uint8_t slot;
    uint64_t mask;
    uint64_t rampHeld = 0u;
    const kit_instrument_slot_t *instrument;

    if (trigger_track >= NUM_TRACKS)
        return;
    slot = (trigger_track >= INSTRUMENT_SLOT_COUNT)
        ? (INSTRUMENT_SLOT_COUNT - 1u) : trigger_track;
    /* S081 P2: this track's ramps end at its own next step first. */
    seq_rampConsumeTrack(trigger_track);
    /*
     * R12 override clear (S081 §5.8, Appendix A-2): the menu override must be
     * cleared before the early return so it ends at every trigger of the voice,
     * not only when a dirty bit is still set. seq_menuOverrideSet() clears the
     * matching dirty bit on every menu edit, so a clear placed after the dirty
     * clear would be skipped exactly when an override exists.
     */
    seq_menuOverride[slot] = 0u;
    mask = seq_automation_dirty[slot];
    if (!mask)
        return;
    /*
     * S081 P2 (D3): collect the parameters of this slot that a running ramp
     * still holds; they are skipped below and keep their held bit.
     */
    {
        uint8_t i;

        for (i = 0u; i < SEQ_RAMP_SLOTS; i++) {
            uint16_t key;

            if (seq_ramps[i].id == 0u)
                continue;
            key = (uint16_t)((seq_ramps[i].id - 1u) & 0x01FFu);
            if (instrumentParam_slot(key) == slot)
                rampHeld |= (1ULL << instrumentParam_local(key));
        }
        mask &= ~rampHeld;
    }

    /*
     * Retrigger restore reads the track's played Scene (S077 P2 §3.2).
     *
     * What: the morph_interpolation[] values used to restore automation
     * overlays must come from the Scene whose instrument is actually loaded in
     * this slot - the track's played Scene, not the global active Scene. Why:
     * if a track plays from Scene B, restoring from Scene A's image would apply
     * wrong values. Inputs: seq_perTrackPattern[trigger_track].
     */
    instrument = scene_instrumentSlotConst(seq_perTrackPattern[trigger_track], slot);
    if (instrument) {
        while (mask) {
            uint8_t local = (uint8_t)__builtin_ctzll(mask);
            const ParamDescriptor *descriptor = instrumentManager_descriptor(
                instrument->type, local);

            if (descriptor)
                (void)instrumentManager_writeRuntime(
                    slot, descriptor,
                    instrument->parameter_images.morph_interpolation[local]);
            mask &= (mask - 1ULL);
        }
    }
    /* S081 P2: ramp-held parameters stay marked (automation keeps priority). */
    seq_automation_dirty[slot] = rampHeld & seq_automation_dirty[slot];
}

/* ===== S081 P2 automation ramps ===== */

/*
 * Predict the next played step for an automation ramp (S081 P2 §5.2).
 *
 * What: mirrors seq_advanceTrackStep()'s play-mode switch without side
 * effects: fwd/rev wrap at the effective length, pip repeats its boundary
 * step and turns, onc/1fr have no step after the last, rnd is never
 * predicted (D7). *repeatInc is 1 when the advance to B increments the
 * track's repeat count (wrap or pip turn), so B's repeat conditions are
 * evaluated with the count B will see. Why: a ramp needs B before B is
 * reached. The looper is ignored on purpose (D8): ramps follow the unlooped
 * segment; at a loop wrap the restored step's apply ends the ramps
 * (SQ-11). Inputs: track, its played Scene region, live index/play state,
 * effective length. Output: B index or -1. Context: TIM3. Keep in step with
 * the switch in seq_advanceTrackStep(). Affiliates: seq_rampPredict().
 */
static int16_t seq_rampNextStep(uint8_t track,
                                const pat_scene_region_t *region,
                                uint8_t *repeatInc)
{
    uint8_t len = seq_effectiveTrackLength[track];
    uint8_t mode = region->track_play_mode[track];
    int16_t idx = seq_stepIndex[track];

    if (len < 1u)
        len = NUM_STEPS_PER_BAR;
    if (mode >= 6u)
        mode = 0u;
    if (idx >= (int16_t)len)
        idx = (int16_t)(len - 1u);
    *repeatInc = 0u;
    switch (mode) {
    case 1u: /* rev */
        if (--idx < 0) {
            idx = (int16_t)(len - 1u);
            *repeatInc = 1u;
        }
        return idx;
    case 2u: /* pip: the boundary plays twice */
        if (seq_trackPlayState[track] & SEQ_PLAY_STATE_PIP_REV) {
            if (idx <= 0) {
                *repeatInc = 1u;
                return idx;
            }
            return (int16_t)(idx - 1);
        }
        if (idx >= (int16_t)(len - 1u)) {
            *repeatInc = 1u;
            return idx;
        }
        return (int16_t)(idx + 1);
    case 3u: /* rnd: never predicted (D7) */
        return -1;
    case 4u: /* onc */
    case 5u: /* 1fr */
        return (idx + 1 >= (int16_t)len) ? -1 : (int16_t)(idx + 1);
    default: /* fwd */
        if (++idx >= (int16_t)len) {
            idx = 0;
            *repeatInc = 1u;
        }
        return idx;
    }
}

/*
 * Will B play, as far as can be known now? (S081 P2 D1)
 *
 * What: 1 when B's condition is predictable at A's apply time AND passes:
 * no condition, the repeat counters (with the count B will see), or
 * lst/!ls (seq_trackLastConditionFired already holds A's result; no other
 * condition on this track is evaluated before B). Probability, fill, loop
 * and mute conditions return 0, so B is never ramped to and jumps as today.
 * Why: the ramp rule requires both steps to play; a step that might not
 * play must not be ramped toward. Condition evaluation itself is unchanged.
 * Context: TIM3. Affiliates: seq_conditionEval(), seq_rampPredict().
 */
static uint8_t seq_rampConditionPredict(const pat_step_specials_t *sp,
                                        uint8_t track, uint8_t repeatInc)
{
    uint8_t cond = sp->condition;

    if (cond == 0u || cond > 40u)
        return 1u;
    if (cond >= 11u && cond <= 20u)
        return seq_conditionRepeatPasses(
            cond, (uint8_t)(seq_trackRepeatCount[track] + repeatInc));
    if (cond == 25u)
        return seq_trackLastConditionFired[track];
    if (cond == 26u)
        return (uint8_t)(!seq_trackLastConditionFired[track]);
    return 0u;              /* rNN, fil/!fl, lop/!lp, mute: not predictable */
}

/*
 * Predict B and its apply time for step A (S081 P2 §5.2).
 *
 * What: at A's step start on an rmp track, fills the context's B (+1) and
 * B's apply time, or leaves b = 0 when no ramp may start because:
 *  - A has no automation, or the track is muted / full-erased;
 *  - there is no predictable B (rnd, once end);
 *  - B has no automation, or B's condition is not predictably passing.
 * B's start is the next DDA advance (ceil((interval - remainder) / 256)
 * ticks after this tick). B's automation is deferred like B's trigger when
 * B holds a trigger: shuffle on odd steps plus micro-timing, the same
 * arithmetic as the deferral in seq_advanceTrackStep(). Why: a ramp must end
 * where B's value lands today. Inputs: live index, accumulator remainder,
 * effective scale/shuffle, B's specials, erase/mute masks. Output: *ctx b
 * and endQ4 (a is set by the caller). Context: TIM3, called from
 * seq_rampStepEvent() at A's step start (also for a deferred A, whose
 * prediction is taken before the playhead moves on). Affiliates:
 * seq_rampNextStep(), seq_rampConditionPredict(), pat_stepAutomationCount().
 */
static void seq_rampPredict(uint8_t track, const pat_scene_region_t *region,
                            seq_rampCtx_t *ctx)
{
    uint8_t scene = seq_perTrackPattern[track];
    uint8_t bit = (uint8_t)(1u << track);
    uint8_t repeatInc;
    int16_t b;
    pat_step_specials_t spb;
    uint16_t interval;
    uint16_t r;
    uint16_t ticksToB;
    uint16_t dB = 0u;

    ctx->b = 0u;
    ctx->endQ4 = 0u;
    if (pat_stepAutomationCount(scene, track,
                                (uint8_t)seq_stepIndex[track]) == 0u)
        return;
    if ((seq_mutedTracks & bit) || (seq_eraseFullMask & bit))
        return;
    b = seq_rampNextStep(track, region, &repeatInc);
    if (b < 0 || pat_stepAutomationCount(scene, track, (uint8_t)b) == 0u)
        return;
    spb = pat_readStepSpecials(scene, track, (uint8_t)b);
    if (!seq_rampConditionPredict(&spb, track, repeatInc))
        return;
    interval = stepScale_ticksQ8(seq_effectiveTrackScale[track]);
    r = seq_trackAccumulator[track];
    ticksToB = (interval > r) ? (uint16_t)((interval - r + 255u) >> 8u) : 1u;
    /* B's automation is deferred with B's trigger (shuffle + micro-timing). */
    if (pat_isStepActive(track, (uint8_t)b, scene) &&
        (seq_eraseSpecialsMask & bit) == 0u) {
        uint8_t shuffle = seq_effectiveTrackShuffle[track];

        if (shuffle > 0u && (b & 1))
            dB = (uint16_t)(((uint16_t)shuffle * 24u) / 256u);
        if (spb.timing > 0u)
            dB = (uint16_t)(dB + (uint16_t)(((uint32_t)(interval >> 8u) *
                                             spb.timing) / 100u));
    }
    ctx->endQ4 = (uint16_t)((seq_elapsedPpqTicks + ticksToB + dB) << 4u);
    ctx->b = (uint8_t)(b + 1);
}

/*
 * TIM3 ramp hand-off at a step (S081 P2 §5.1/§5.5).
 *
 * What: on an rmp track only, records what happened at this step for the
 * foreground:
 *  - applied (automation queued now): stepped flag + prediction of B;
 *  - deferred (automation queues at the deferred fire): prediction only;
 *    seq_fireDeferred() sets the stepped flag when the entries queue, so
 *    the ramps into this step run until its value actually lands;
 *  - neither (condition failed, muted, full-erased, no automation block):
 *    stepped flag with no B, so ramps toward this step end at its start.
 * Tracks with rmp off write nothing; the ramp service cancels any ramp left
 * on them. Why: ramps end at the next played step's apply time, and that
 * step's own value then takes over. Context: TIM3. Accessors:
 * seq_advanceTrackStep(). Affiliates: seq_rampPredict(), seq_fireDeferred(),
 * seq_rampConsumeTrack().
 */
static void seq_rampStepEvent(uint8_t track, uint8_t applied, uint8_t deferred)
{
    const pat_scene_region_t *region =
        pat_sceneRegion(seq_perTrackPattern[track]);
    seq_rampCtx_t ctx;

    if (!region || ((region->track_auto_smooth >> track) & 1u) == 0u)
        return;
    ctx.a = (uint8_t)(seq_stepIndex[track] & 0x7F);
    if (!applied && !deferred) {
        ctx.a |= SEQ_RAMP_STEPPED;
        ctx.b = 0u;
        ctx.endQ4 = 0u;
    } else {
        if (applied)
            ctx.a |= SEQ_RAMP_STEPPED;
        seq_rampPredict(track, region, &ctx);
    }
    seq_rampCtx[track] = ctx;
}

/*
 * Position break for automation ramps (S081 P2 §5.5).
 *
 * What: marks the track's context "stepped, no B", so the foreground ends
 * its ramps at the next consume and no ramp starts until the next step's
 * prediction. seq_rampBreakAll() does it for every track. Why: after a
 * transport change, realign, Scene change, PC reset, loop wrap/resume or a
 * chain stop, the predicted B will not follow. A ramp left running would
 * drift toward a value that never plays. Context: TIM3 or foreground
 * (PRIMASK section). Accessors: every seq_playbackEpoch bump site,
 * seq_loopProcessTick() wrap, seq_loopResumeTrack(), the chain stop.
 */
static void seq_rampBreakTrack(uint8_t track)
{
    uint32_t primask = __get_PRIMASK();

    if (track >= NUM_TRACKS)
        return;
    __disable_irq();
    seq_rampCtx[track].a = (uint8_t)(SEQ_RAMP_STEPPED | 0x7Fu);
    seq_rampCtx[track].b = 0u;
    __set_PRIMASK(primask);
}

static void seq_rampBreakAll(void)
{
    uint8_t track;

    for (track = 0u; track < NUM_TRACKS; track++)
        seq_rampBreakTrack(track);
}

/*
 * Ramp time base (S081 P2 §5.5).
 *
 * What: the current master position in 1/16 ticks (low 16 bits): the 96-PPQ
 * tick count plus the sub-tick fraction from the tick's microsecond stamp.
 * Why: ramps measured in master ticks stretch with a tempo change; the
 * sub-tick fraction keeps the ~2 ms updates smooth between 5 ms ticks. The
 * fraction is clamped to 15/16, so a stalled clock (external sync stopped)
 * holds the ramp just before the next tick instead of running past it.
 * Context: foreground (PRIMASK copy). Affiliates: seq_ppqStampUs,
 * seq_captureTrackTime() (the same fields), seq_rampPredict() (same units).
 */
static uint16_t seq_rampNowQ4(void)
{
    uint32_t primask = __get_PRIMASK();
    uint32_t ticks;
    uint32_t stamp;
    uint32_t period;
    uint32_t now;
    uint32_t frac = 0u;

    __disable_irq();
    ticks = seq_elapsedPpqTicks;
    stamp = seq_ppqStampUs;
    period = seq_ppqPeriodUs;
    now = timebase_tim2Now();
    __set_PRIMASK(primask);
    if (period != 0u) {
        uint32_t since = now - stamp;

        frac = (since >= period)
            ? 15u : (uint32_t)(((uint64_t)since * 16u) / period);
        if (frac > 15u)
            frac = 15u;
    }
    return (uint16_t)((ticks << 4u) + frac);
}

/*
 * End a track's ramps when its next step applies (S081 P2 §5.5).
 *
 * What: consumes the TIM3 stepped flag of one track (or all) and frees that
 * track's ramp slots. The values stay where they are; the new step's own
 * entries (drain) or today's restore rules take over. Why: a ramp lives
 * exactly from A's apply to the next played step's apply. Ending at B's
 * arrival (not at the computed time) keeps a late ramp sample from
 * overwriting B's value. At a loop wrap the restored step's apply ends the
 * ramps, so nothing ramps across the wrap (D8). Order: the trigger restore
 * consumes its own track first, the drain consumes all tracks before
 * applying entries, the ramp service consumes all tracks each block, and
 * seq_rampStartIfPaired() consumes A's own flag when TIM3 set it after the
 * drain's consume. Context: foreground. Affiliates: seq_rampStepEvent(),
 * seq_fireDeferred(), seq_restoreAutomatedParameters(),
 * seq_drainPendingAutomation().
 */
static void seq_rampEndTrack(uint8_t track)
{
    uint8_t i;

    for (i = 0u; i < SEQ_RAMP_SLOTS; i++) {
        if (seq_ramps[i].id != 0u &&
            (uint8_t)((seq_ramps[i].id - 1u) >> 9u) == track)
            seq_ramps[i].id = 0u;
    }
}

static void seq_rampConsumeTrack(uint8_t track)
{
    uint32_t primask;
    uint8_t stepped;

    if (track >= NUM_TRACKS)
        return;
    primask = __get_PRIMASK();
    __disable_irq();
    stepped = (uint8_t)(seq_rampCtx[track].a & SEQ_RAMP_STEPPED);
    if (stepped)
        seq_rampCtx[track].a =
            (uint8_t)(seq_rampCtx[track].a & (uint8_t)~SEQ_RAMP_STEPPED);
    __set_PRIMASK(primask);
    if (stepped)
        seq_rampEndTrack(track);
}

static void seq_rampConsumeSteps(void)
{
    uint32_t primask;
    uint8_t steppedMask = 0u;
    uint8_t track;

    /* One PRIMASK section for all seven flags (runs every DSP block). */
    primask = __get_PRIMASK();
    __disable_irq();
    for (track = 0u; track < NUM_TRACKS; track++) {
        if (seq_rampCtx[track].a & SEQ_RAMP_STEPPED) {
            seq_rampCtx[track].a = (uint8_t)(seq_rampCtx[track].a &
                                             (uint8_t)~SEQ_RAMP_STEPPED);
            steppedMask |= (uint8_t)(1u << track);
        }
    }
    __set_PRIMASK(primask);
    for (track = 0u; steppedMask != 0u; track++, steppedMask >>= 1u) {
        if (steppedMask & 1u)
            seq_rampEndTrack(track);
    }
}

/*
 * Which Instrument parameters ramp (S081 P2 D2).
 *
 * What: only automatable descriptors with a numeric-range dtype (0B127,
 * PM63, and the numeric 0B255/0B15/1B16/1B128 should a descriptor use
 * them). Menu tables (waveforms, filter types), FM mode, on/off, LFO
 * polarity and target/voice selections never ramp: their in-between values
 * are other choices, not intermediate settings. Why: user decision D2.
 * Inputs: the descriptor of the target's slot in the played Scene. Output:
 * flag. Affiliates: seq_rampStartIfPaired().
 */
static uint8_t seq_rampDtypeOk(const ParamDescriptor *descriptor)
{
    if (!descriptor ||
        (descriptor->flags & INSTRUMENT_PARAM_FLAG_AUTOMATABLE) == 0u)
        return 0u;
    switch (descriptor->dtype & 0x0Fu) {
    case DTYPE_0B127:
    case DTYPE_PM63:
    case DTYPE_0B255:
    case DTYPE_0B15:
    case DTYPE_1B16:
    case DTYPE_1B128:
        return 1u;
    default:
        return 0u;
    }
}

/*
 * Is the track's rmp mode on in its played Scene? (S081 P2)
 *
 * What: reads pat_scene_region_t.track_auto_smooth of
 * seq_perTrackPattern[track]. Why: ramps start and keep running only while
 * the flag is on; turning it off (or a played-Scene change to a Pattern
 * with it off) cancels them at the next service pass. Context: foreground.
 * Affiliates: seq_rampStartIfPaired(), seq_rampService().
 */
static uint8_t seq_rampTrackOn(uint8_t track)
{
    const pat_scene_region_t *region;

    if (track >= NUM_TRACKS)
        return 0u;
    region = pat_sceneRegion(seq_perTrackPattern[track]);
    return (uint8_t)(region && ((region->track_auto_smooth >> track) & 1u));
}

/*
 * Write one Instrument automation value (drain and ramps, S081 P2).
 *
 * What: resolves the descriptor in the owning track's played Scene, writes
 * the runtime value, and marks the parameter held for the next trigger's
 * restore. Returns the descriptor on success. Why: ramp samples are
 * automation values under the binding priority rule (MEMORY.md): the same
 * write path keeps the Morph sweep and menu apply from overwriting them.
 * The caller has already validated the target and the R12 override.
 * Context: foreground. Accessors: seq_drainPendingAutomation(),
 * seq_rampService(). Affiliates: instrumentManager_writeRuntime(),
 * seq_automation_dirty[].
 */
static const ParamDescriptor *seq_writeInstrumentAutomation(
    uint8_t owner_scene, uint16_t target, uint8_t value)
{
    uint8_t slot = instrumentParam_slot(target);
    uint8_t local = instrumentParam_local(target);
    const kit_instrument_slot_t *instrument =
        scene_instrumentSlotConst(owner_scene, slot);
    const ParamDescriptor *descriptor = instrument
        ? instrumentManager_descriptor(instrument->type, local) : 0;

    if (!descriptor || !instrumentManager_writeRuntime(slot, descriptor, value))
        return 0;
    seq_automation_dirty[slot] |= (1ULL << local);
    return descriptor;
}

/*
 * Start a ramp for a just-written entry of step A (S081 P2 §5.4).
 *
 * What: when A's entry was written on an rmp track for a rampable target,
 * and TIM3's context for this track still describes this A with a
 * predicted B that holds the same target, takes a slot that ramps from A's
 * value (just written) to B's value, from now to B's predicted apply time.
 * An existing slot for the same track/target is reused. With all 64 slots
 * busy the pair jumps as today (D10; DEV count). If A's stepped flag is
 * still set (TIM3 queued A's entries and set the flag after this drain
 * pass's consume), the flag is consumed here and the track's older ramps end
 * first; otherwise the next consume would end the ramp started here. Why:
 * plan §5.4; the context check drops the pair (jump) if TIM3 has already
 * moved past A, which can only happen when the foreground lags a whole
 * step. Inputs: owning track/Scene, A's step (from the pending identity),
 * target, written value, descriptor. Output: one slot. Context: foreground
 * (drain). Affiliates: pat_findStepAutomation(), seq_rampDtypeOk(),
 * seq_rampNowQ4(), seq_rampEndTrack().
 */
static void seq_rampStartIfPaired(uint8_t track, uint8_t scene, uint8_t step,
                                  uint16_t target, uint8_t from,
                                  const ParamDescriptor *descriptor)
{
    seq_rampCtx_t ctx;
    uint32_t primask;
    uint16_t now;
    uint16_t id;
    uint8_t to;
    uint8_t i;
    uint8_t freeSlot = SEQ_RAMP_SLOTS;
    uint8_t stepped = 0u;

    if (!seq_rampTrackOn(track) || !seq_rampDtypeOk(descriptor))
        return;
    primask = __get_PRIMASK();
    __disable_irq();
    ctx = seq_rampCtx[track];
    if ((uint8_t)(ctx.a & 0x7Fu) == step && (ctx.a & SEQ_RAMP_STEPPED)) {
        seq_rampCtx[track].a = (uint8_t)(ctx.a & (uint8_t)~SEQ_RAMP_STEPPED);
        stepped = 1u;
    }
    __set_PRIMASK(primask);
    if (stepped)
        seq_rampEndTrack(track);
    if ((uint8_t)(ctx.a & 0x7Fu) != step || ctx.b == 0u)
        return;
    if (!pat_findStepAutomation(scene, track, (uint8_t)(ctx.b - 1u), target,
                                &to))
        return;
    now = seq_rampNowQ4();
    if ((int16_t)(ctx.endQ4 - now) <= 0)
        return;
    id = (uint16_t)(1u + (((uint16_t)track << 9u) | target));
    for (i = 0u; i < SEQ_RAMP_SLOTS; i++) {
        if (seq_ramps[i].id == id) {
            freeSlot = i;
            break;
        }
        if (seq_ramps[i].id == 0u && freeSlot == SEQ_RAMP_SLOTS)
            freeSlot = i;
    }
    if (freeSlot == SEQ_RAMP_SLOTS) {
#if DEV_MODE_LOGGING
        seq_rampNoSlotDrops++;
#endif
        return;                         /* cap reached: jump as today (D10) */
    }
    seq_ramps[freeSlot].id = id;
    seq_ramps[freeSlot].from = from;
    seq_ramps[freeSlot].to = to;
    seq_ramps[freeSlot].last = from;    /* the drain has just written it */
    seq_ramps[freeSlot].startQ4 = now;
    seq_ramps[freeSlot].endQ4 = ctx.endQ4;
}

/*
 * Automation ramp service (S081 P2 §5.5, D6/D9).
 *
 * What: every DSP block it ends the ramps of tracks whose next step has
 * applied. Every third block (~2.2 ms) it writes each running ramp's value
 * from + (to - from) x elapsed / duration, rounded to the 7-bit descriptor
 * value, holding `to` after the predicted end until the next step applies
 * (from is set to `to` then, so the held value stays exact however long the
 * hold lasts). A value is written only when it differs from the slot's last
 * written value (user decision; the held bit keeps every other writer off the
 * parameter, so an unchanged value is still in the runtime). A ramp is
 * cancelled (its value stays) when:
 *  - the track's rmp turned off;
 *  - live record latches or erases the target (the drain's filters);
 *  - an R12 menu override is set;
 *  - the target is no longer valid in the track's played Scene.
 * Why: plan §5.5. Writes go through seq_writeInstrumentAutomation(), so ramp
 * values are held automation values (binding priority rule). Cost: at most
 * 64 runtime writes per pass (every ramp changing value each pass), the worst
 * case measured on the stress Scene (DSP CPU policy; plan §7). Context: foreground, audio render loop, right
 * after seq_drainPendingAutomation(). Accessors: main.c
 * audio_check_and_render(). Affiliates: seq_rampConsumeSteps(),
 * seq_rampStartIfPaired().
 */
void seq_rampService(void)
{
    uint16_t now;
    uint8_t i;

    seq_rampConsumeSteps();
    if (++seq_rampBlockDiv < SEQ_RAMP_BLOCK_DIV)
        return;
    seq_rampBlockDiv = 0u;
    now = seq_rampNowQ4();
    for (i = 0u; i < SEQ_RAMP_SLOTS; i++) {
        seq_ramp_t *r = &seq_ramps[i];
        uint16_t key;
        uint8_t track;
        uint16_t target;
        uint8_t scene;
        uint8_t slot;
        int16_t num;
        int16_t den;
        uint8_t value;

        if (r->id == 0u)
            continue;
        key = (uint16_t)(r->id - 1u);
        track = (uint8_t)(key >> 9u);
        target = (uint16_t)(key & 0x01FFu);
        scene = seq_perTrackPattern[track];
        slot = instrumentParam_slot(target);
        if (!seq_rampTrackOn(track) ||
            (seq_filterLatch != 0 &&
             (seq_filterLatch[target >> 3u] & (uint8_t)(1u << (target & 7u)))) ||
            (seq_filterErase != 0 && track == seq_filterEraseTrack &&
             (seq_filterErase[target >> 3u] & (uint8_t)(1u << (target & 7u)))) ||
            (slot < INSTRUMENT_SLOT_COUNT &&
             (seq_menuOverride[slot] & (1ULL << instrumentParam_local(target)))) ||
            !instrumentManager_targetValid(scene, target,
                                           INSTRUMENT_TARGET_AUTOMATION)) {
            r->id = 0u;                 /* cancel: value stays */
            continue;
        }
        num = (int16_t)(now - r->startQ4);
        den = (int16_t)(r->endQ4 - r->startQ4);
        if (den <= 0 || num >= den) {
            value = r->to;
            r->from = r->to;            /* hold `to` exactly from now on */
        } else if (num <= 0) {
            value = r->from;
        } else {
            int32_t delta = ((int32_t)r->to - (int32_t)r->from) * num;

            value = (uint8_t)((int32_t)r->from +
                ((delta >= 0) ? (delta + den / 2) / den
                              : -((-delta + den / 2) / den)));
        }
        if (value == r->last)
            continue;                   /* unchanged: runtime already holds it */
        r->last = value;
        (void)seq_writeInstrumentAutomation(scene, target, value);
    }
}

/*
 * Set the menu-override bit and clear the held bit for one parameter
 * (S081 §5.1).
 *
 * What: sets bit `local` in seq_menuOverride[slot] and clears the same
 * bit in seq_automation_dirty[slot]. Why: the held bit gates whether
 * presetMorph_writeRuntimeBase() applies the Morph sweep. Clearing it
 * lets the menu edit's endpoint write stand. The override bit then
 * tells the drain to skip that parameter's automation entries.
 * Foreground only - the drain and restore are foreground too, so no
 * PRIMASK. Inputs: slot 0..5, local parameter index. Outputs: override
 * and dirty bitmaps updated. Affiliates: menu_cellCommitValue
 * INSTRUMENT branch, seq_drainPendingAutomation filter,
 * seq_restoreAutomatedParameters clear.
 */
void seq_menuOverrideSet(uint8_t slot, uint8_t local)
{
    if (slot >= INSTRUMENT_SLOT_COUNT || local >= INSTRUMENT_PARAM_COUNT)
        return;
    seq_menuOverride[slot] |= (1ULL << local);
    seq_automation_dirty[slot] &= ~(1ULL << local);
}

/*
 * Contract in sequencer.h. Read-only view of the overlay bitmap;
 * INSTRUMENT_PARAM_COUNT is 64, one bit per descriptor-local index.
 */
uint8_t seq_automationHoldsParameter(uint8_t slot, uint8_t local)
{
    if (slot >= INSTRUMENT_SLOT_COUNT || local >= INSTRUMENT_PARAM_COUNT)
        return 0u;
    return (uint8_t)((seq_automation_dirty[slot] >> local) & 1u);
}

static uint8_t seq_handleMasterBoundary(void)
{
	uint8_t masterStepPos = (uint8_t)(seq_masterStepClock % NUM_STEPS_PER_BAR);

	/*
	 * Master boundaries are based on the corrected default grid, not on any one
	 * scaled track. They own pattern-boundary commit, trigger clock output, beat
	 * LED pulse, and 128-step drift correction checkpoints.
	 */
	if (masterStepPos == 0u && seq_masterStepClock != 0u) {
		seq_barCounter++;
		if ((seq_activePattern != seq_pendingPattern) || seq_loadPendigFlag) {
			if (seq_resetBarOnPatternChange)
				seq_barCounter = 0u;
			seq_loadPendigFlag = 0u;
			seq_newPatternAvailable = 0u;
			seq_activePattern = seq_pendingPattern;
			/* The S067 stub follows the instant master-boundary switch. */
			for (uint8_t track = 0u; track < NUM_TRACKS; track++)
				seq_perTrackPattern[track] = seq_activePattern;
			/*
			 * Master-boundary commit clears per-track overrides (S077 P2).
			 *
			 * What: the loop above already set every entry to the committed
			 * active pattern, so the override flag is necessarily 0.
			 */
			seq_perTrackActive = 0u;
			seq_setStepIndexToStart();
			seq_resetStepScheduler();
			led_notifyPatternChanged(seq_activePattern);
			seq_sendProgChg(seq_activePattern);
			voiceControl_noteOff(0xFF);
			/* S083: end every track's MIDI note on this boundary too. */
			midiOut_trackNoteOff(0xFFu);
			return 1u;
		}
	}

	if ((seq_masterStepClock % SEQ_DEFAULT_STEPS_PER_BEAT) == 0u) {
		seq_ledState.beatPulse = 1u;
		seq_ledState.dirty |= SEQ_LED_DIRTY_BEAT;
	} else if ((seq_masterStepClock % SEQ_DEFAULT_STEPS_PER_BEAT) == 1u) {
		seq_ledState.beatPulse = 0u;
		seq_ledState.dirty |= SEQ_LED_DIRTY_BEAT;
	}

	/*
	 * S080 F8: deferred / queued scene-change boundary detection.
	 *
	 * What: while a direct (scd) or queued (qcd) scene change is pending, watch
	 * the master 1/16 clock for the selected division boundary (1/8 note .. 4
	 * bars) and raise seq_sceneFireFlag for the foreground executor. Why: the
	 * TIM3 ISR owns sample-accurate boundary detection; the foreground owns the
	 * actual DSP/LED change. Inputs: seq_pendingSceneChange/Queue, the pending
	 * division, seq_masterStepClock. Outputs: seq_sceneFireFlag. A queued
	 * change converts qcd through its own longer list (S080 P2: up to 16
	 * bars). Affiliates: seq_sceneChangeDivUnits(), seq_queueChangeDivUnits(),
	 * menu_serviceDeferredSceneChange().
	 */
	if (seq_pendingSceneChange || seq_pendingSceneQueue) {
		uint16_t units = seq_pendingSceneQueue
		    ? seq_queueChangeDivUnits(seq_pendingQueueDiv)
		    : seq_sceneChangeDivUnits(seq_pendingSceneDiv);

		if (units != 0u && seq_masterStepClock != 0u &&
		    (seq_masterStepClock % units) == 0u)
			seq_sceneFireFlag = 1u;
	}

	trigger_clockTick((uint8_t)((seq_masterStepClock % NUM_STEPS) + 1u));
	return 0u;
}

/*
 * Map one position on the master cycle to a track step index for a play mode.
 *
 * What: pure position-to-index mapping shared by the realign paths. FWD/ONC
 * use position % length; REV mirrors it; PIP folds a 2×length cycle; 1FR always
 * starts at zero; RND draws a fresh random index. Why: realignment must land a
 * non-forward track on the step its play mode would have reached, not the
 * forward index. Inputs: play mode 0..5, integer position in steps, length.
 * Output: a step index 0..length-1. Affiliates:
 * seq_realignActivePatternToMasterClock(),
 * seq_realignTrackToMasterClock().
 */
static int16_t seq_stepIndexForMode(uint8_t play_mode, uint32_t position,
                                    uint8_t len)
{
	switch (play_mode) {
	case 1u: /* rev */
		return (int16_t)((uint32_t)(len - 1u) - (position % len));
	case 2u: { /* pip */
		uint32_t p = position % ((uint32_t)len * 2u);

		return (int16_t)(p < len ? p : ((uint32_t)len * 2u - 1u - p));
	}
	case 3u: /* rnd */
		return (int16_t)(((uint16_t)GetRngValue() & 0x7FFFu) % len);
	case 5u: /* 1fr */
		return 0;
	case 4u: /* onc */
	default: /* fwd */
		return (int16_t)(position % len);
	}
}

void seq_realignActivePatternToMasterClock(void)
{
	uint8_t track;

	/*
	 * Recalculate runtime track positions from the master clock.
	 *
	 * What: derives each track's step cursor from the running master clock so a
	 * mid-playback Scene/Bank realignment lands on the correct beat rather than
	 * restarting from step 0. This is a performance action, not a PatternData
	 * edit.
	 *
	 * Why per-track: each track wraps at its own track_length from the active
	 * Scene's pat_scene_region_t. The master clock is the absolute sixteenth-
	 * note count since start; the modulo of each track's length gives its
	 * current position within its own independent loop. A zero or inaccessible
	 * length falls back to NUM_STEPS_PER_BAR (16).
	 *
	 * Inputs: seq_masterStepClock (global), seq_activePattern (selects the
	 * Scene region). Outputs: seq_stepIndex[] and seq_lastMasterStep[] for
	 * every track, plus a chase LED dirty event for the UI-selected voice.
	 *
	 * Affiliates: pat_sceneRegion() (SRAM1 pointer, no SD I/O — safe in any
	 * context), seq_advanceTrackStep() (uses the same per-track length for its
	 * wrap boundary), led_processSeqLedState() (drains the chase dirty bit).
	 */
	/*
	 * Per-track realignment uses each track's played Scene (S077 P2 §3.2).
	 *
	 * What: each track derives its step position from the master clock using its
	 * own played Scene's track_length, not the global active Scene's. Why: tracks
	 * playing from different Scenes may have different track lengths; a global
	 * region read would position tracks at step offsets for the wrong loop
	 * length. Inputs: seq_perTrackPattern[track], seq_masterStepClock. Output:
	 * seq_stepIndex[track] = masterStepClock % that track's length. Note:
	 * pat_sceneRegion() returns an SRAM1 pointer with no allocation, so moving
	 * the call inside the loop is safe in any context.
	 */
	for (track = 0u; track < NUM_TRACKS; track++) {
		const pat_scene_region_t *region =
		    pat_sceneRegion(seq_perTrackPattern[track]);
		/* Effective (morphed) length and scale from the S078 §5.4 cache. */
		uint8_t len = seq_effectiveTrackLength[track];
		if (len < 1u)
			len = NUM_STEPS_PER_BAR;
		uint8_t play_mode = (region) ? region->track_play_mode[track] : 0u;
		uint16_t interval;
		uint32_t master_q8;
		uint32_t position;

		if (play_mode >= 6u)
			play_mode = 0u;
		if ((play_mode == 4u || play_mode == 5u) &&
		    (seq_trackPlayState[track] & SEQ_PLAY_STATE_STOPPED)) {
			/*
			 * A stopped once-mode track is not restarted by realignment
			 * (S078 §4.3): leave its parked cursor and DDA alone.
			 */
			continue;
		}
		/*
		 * DDA accumulator phase realignment (S078 §2.4.5): the accumulator
		 * takes the fractional remainder of the master Q8.8 timeline so a
		 * realigned track picks up exactly where it would have been if it had
		 * been running from the start. The step index comes from the same
		 * Q8.8 timeline through the play mode, so a non-forward track lands on
		 * the step its mode would have reached.
		 */
		interval = stepScale_ticksQ8(seq_effectiveTrackScale[track]);
		master_q8 = (uint32_t)seq_elapsedPpqTicks << 8u;
		position = master_q8 / interval;
		seq_stepIndex[track] = seq_stepIndexForMode(play_mode, position, len);
		seq_trackAccumulator[track] = (uint16_t)(master_q8 % interval);
		if (play_mode == 2u) {
			if ((position % ((uint32_t)len * 2u)) >= len)
				seq_trackPlayState[track] |= SEQ_PLAY_STATE_PIP_REV;
			else
				seq_trackPlayState[track] &=
				    (uint8_t)~SEQ_PLAY_STATE_PIP_REV;
		}
		seq_lastMasterStep[track] = (uint8_t)seq_stepIndex[track];
	}
	seq_ledState.chaseStep = seq_stepIndex[menu_getActiveVoice()];
	seq_ledState.dirty |= SEQ_LED_DIRTY_CHASE;
	/* R12: realign clears the whole Instrument menu override (S081 §5.1). */
	memset(seq_menuOverride, 0, sizeof(seq_menuOverride));
	/* S081 C-6/C-10: bump the playback epoch and expire echo marks. */
	seq_playbackEpoch++;
	/* S081 P2: a playhead break ends automation ramps (no B will follow). */
	seq_rampBreakAll();
	seq_liveRecResetMarks();
	/*
	 * Realign keeps the FX sequence on the master grid and clears Effect track
	 * overlays (S081 §3.3-4, Appendix A-5). A STEP (not a RESET) keeps the FX
	 * position valid; the overlay reset latch clears the overlays on the next
	 * drain. The per-track realign below does the same (idempotent).
	 */
	seq_fxRealignToMasterClock();
	seq_requestEffectOverlayReset();
}

/*
 * Realign a single track's step position against the master clock
 * (S077 P2 §1.4).
 *
 * What: derives one track's seq_stepIndex from seq_masterStepClock using that
 * track's own played Scene's track_length from PatternData's
 * pat_sceneRegion(seq_perTrackPattern[track]). Why: when a per-track Scene
 * assignment changes a track's loop length, the track must be repositioned
 * against the master clock to avoid phase discontinuities; also used by the
 * double-click single-track realign gesture. Inputs: track 0..6,
 * seq_masterStepClock (global). Output: seq_stepIndex[track] and
 * seq_lastMasterStep[track] updated; the chase LED dirty bit is set when the
 * track is the UI-selected voice. Affiliates: pat_sceneRegion(),
 * seq_realignActivePatternToMasterClock() (the all-track variant).
 */
void seq_realignTrackToMasterClock(uint8_t track)
{
	const pat_scene_region_t *region;
	uint8_t len;
	uint8_t play_mode;
	uint16_t interval;
	uint32_t master_q8;
	uint32_t position;

	if (track >= NUM_TRACKS)
		return;
	region = pat_sceneRegion(seq_perTrackPattern[track]);
	/* Effective (morphed) length and scale from the S078 §5.4 cache. */
	len = seq_effectiveTrackLength[track];
	if (len < 1u)
		len = NUM_STEPS_PER_BAR;
	play_mode = (region) ? region->track_play_mode[track] : 0u;
	if (play_mode >= 6u)
		play_mode = 0u;
	if ((play_mode == 4u || play_mode == 5u) &&
	    (seq_trackPlayState[track] & SEQ_PLAY_STATE_STOPPED)) {
		/* Stopped once-mode tracks are not restarted by realignment. */
		return;
	}
	/*
	 * Single-track DDA phase realignment (S078 §2.4.5). Same Q8.8 phase and
	 * play-mode mapping as the all-track variant above. Used by per-track Scene
	 * assignment and the double-click realign gesture.
	 */
	interval = stepScale_ticksQ8(seq_effectiveTrackScale[track]);
	master_q8 = (uint32_t)seq_elapsedPpqTicks << 8u;
	position = master_q8 / interval;
	seq_stepIndex[track] = seq_stepIndexForMode(play_mode, position, len);
	seq_trackAccumulator[track] = (uint16_t)(master_q8 % interval);
	if (play_mode == 2u) {
		if ((position % ((uint32_t)len * 2u)) >= len)
			seq_trackPlayState[track] |= SEQ_PLAY_STATE_PIP_REV;
		else
			seq_trackPlayState[track] &= (uint8_t)~SEQ_PLAY_STATE_PIP_REV;
	}
	seq_lastMasterStep[track] = (uint8_t)seq_stepIndex[track];
	if (track == menu_getActiveVoice()) {
		seq_ledState.chaseStep = seq_stepIndex[track];
		seq_ledState.dirty |= SEQ_LED_DIRTY_CHASE;
	}
	/* R12: per-track realign clears only the affected slot's menu override. */
	seq_menuOverride[(track >= INSTRUMENT_SLOT_COUNT)
	                 ? (INSTRUMENT_SLOT_COUNT - 1u) : track] = 0u;
	/* S081 C-6/C-10: bump the epoch and expire this track's echo mark. */
	seq_playbackEpoch++;
	/* S081 P2: a playhead break ends automation ramps (no B will follow). */
	seq_rampBreakTrack(track);
	/*
	 * A per-track Scene change advances the Scene epoch (S081 P1B item 10,
	 * P1C item 12).
	 *
	 * What: LiveRecord starts a new take when this epoch moves. Why here and
	 * not in seq_setTrackPlayedScene(): today the only per-track Scene change
	 * is the buttonHandler Scene path, which sets the played Scene and then
	 * realigns, so both sites see the same events. If a second caller of
	 * seq_setTrackPlayedScene() appears without a realign, move this bump into
	 * seq_setTrackPlayedScene(). Affiliates: seq_getSceneEpoch(),
	 * liveRec_service() step 1.
	 */
	seq_sceneEpoch++;
	seq_echoStep[track] = -1;
	/* Realign the FX sequence and clear Effect track overlays (S081 §3.3-4). */
	seq_fxRealignToMasterClock();
	seq_requestEffectOverlayReset();
}

/* =====================================================================
 * S080 P2 looper engine (Defects 7, 8, 14)
 * ===================================================================== */

/*
 * Convert a looper division + modifier to a master-clock tick count (S080 C2).
 *
 * What: division 0..7 maps to 1 bar (16 x 24 = 384 PPQ ticks at 96 PPQ) down
 * to 1/128 (3 ticks); modifier 1 (dot) scales x3/2 and modifier 2 (triplet)
 * scales x2/3. Why: the loop period is measured on the master step clock, the
 * same grid used by bar-based conditions. Inputs: division 0..7 (clamped),
 * modifier 0..2. Output: tick count >= 1. Affiliates: seq_loopProcessTick().
 */
static uint16_t seq_loopLengthTicks(uint8_t division, uint8_t modifier)
{
	static const uint16_t base[8] = {
		384u, 192u, 96u, 48u, 24u, 12u, 6u, 3u
	};
	uint16_t ticks;

	if (division > 7u)
		division = 0u;
	ticks = base[division];
	if (modifier == 1u)             /* dot: +50% */
		ticks = (uint16_t)((ticks * 3u) / 2u);
	else if (modifier == 2u)        /* triplet: x2/3 */
		ticks = (uint16_t)((ticks * 2u) / 3u);
	return (ticks != 0u) ? ticks : 1u;
}

/*
 * One track's position image (S080 P2).
 *
 * What: step cursor, Q8.8 DDA accumulator and play-state bits (once-stopped,
 * pip direction) - everything seq_advanceTrackStep() needs to continue from a
 * point in time. Used for the loop snapshot and the rewind/projection maths.
 */
typedef struct {
	int16_t  step;
	uint16_t accum;
	uint8_t  play_state;
} seq_loop_pos_t;

/*
 * Move a track position image by a signed Q8.8 tick delta (S080 P2, S081 P1B).
 *
 * What: adds deltaQ8 (1/256 tick units; negative = rewind) to the image's
 * DDA accumulator and walks the step cursor by the resulting number of step
 * advances under the track's play mode (fwd/rev wrap, pip 2 x len cycle with
 * direction bit, rnd fresh draw, onc/1fr clamp with the stopped bit). Returns
 * the signed number of step advances applied (0 = same step). Why: the
 * looper needs whole-tick shifts (seq_loopShiftPosition wraps this with
 * ticks * 256, numerically unchanged). Live-record placement needs sub-tick
 * shifts and to know whether a target step is behind, current or ahead of
 * the playhead. Uses the current effective scale and length for the whole
 * span (accepted S080 approximation). Inputs: track 0..6, position image,
 * signed Q8 delta. Outputs: image updated; return = step advances.
 * Context: foreground only (64-bit maths). Accessors: seq_loopShiftPosition()
 * (looper), seq_placeFromSnapshot() (live record). Affiliates:
 * seq_advanceTrackStep() (the per-step semantics mirrored here),
 * stepScale_ticksQ8().
 */
static int32_t seq_shiftPositionQ8(uint8_t track, seq_loop_pos_t *pos,
                                   int32_t deltaQ8)
{
	const pat_scene_region_t *region =
	    pat_sceneRegion(seq_perTrackPattern[track]);
	uint8_t len = seq_effectiveTrackLength[track];
	uint8_t mode = region ? region->track_play_mode[track] : 0u;
	int64_t interval =
	    (int64_t)stepScale_ticksQ8(seq_effectiveTrackScale[track]);
	int64_t total;
	int64_t steps;
	int64_t rem;

	if (len < 1u)
		len = NUM_STEPS_PER_BAR;
	if (mode >= 6u)
		mode = 0u;
	total = (int64_t)pos->accum + (int64_t)deltaQ8;
	steps = total / interval;
	rem = total % interval;
	if (rem < 0) {
		rem += interval;
		steps--;
	}
	pos->accum = (uint16_t)rem;
	if (steps == 0)
		return 0;

	switch (mode) {
	case 1u: { /* rev */
		int64_t base = (pos->step < 0) ? 0 : pos->step;
		int64_t idx = (base - steps) % (int64_t)len;

		if (idx < 0)
			idx += len;
		pos->step = (int16_t)idx;
		break;
	}
	case 2u: { /* pip: positions 0..len-1 forward, len..2len-1 reverse */
		int64_t cycle = (int64_t)len * 2;
		int64_t p;

		if (pos->step < 0)
			p = -1;
		else if (pos->play_state & SEQ_PLAY_STATE_PIP_REV)
			p = cycle - 1 - pos->step;
		else
			p = pos->step;
		p = (p + steps) % cycle;
		if (p < 0)
			p += cycle;
		if (p < (int64_t)len) {
			pos->step = (int16_t)p;
			pos->play_state &= (uint8_t)~SEQ_PLAY_STATE_PIP_REV;
		} else {
			pos->step = (int16_t)(cycle - 1 - p);
			pos->play_state |= SEQ_PLAY_STATE_PIP_REV;
		}
		break;
	}
	case 3u: /* rnd */
		pos->step = (int16_t)(((uint16_t)GetRngValue() & 0x7FFFu) % len);
		break;
	case 4u: /* onc */
	case 5u: { /* 1fr: virtual position len = stopped after the last step */
		int64_t v = (pos->play_state & SEQ_PLAY_STATE_STOPPED)
		    ? (int64_t)len : (int64_t)pos->step;

		v += steps;
		if (v >= (int64_t)len) {
			pos->step = (int16_t)(len - 1u);
			pos->play_state |= SEQ_PLAY_STATE_STOPPED;
		} else {
			if (v < -1)
				v = -1;
			pos->step = (int16_t)v;
			pos->play_state &= (uint8_t)~SEQ_PLAY_STATE_STOPPED;
		}
		break;
	}
	case 0u: /* fwd */
	default: {
		int64_t idx = ((int64_t)pos->step + steps) % (int64_t)len;

		if (idx < 0)
			idx += len;
		pos->step = (int16_t)idx;
		break;
	}
	}
	return (int32_t)steps;
}

/* Looper wrapper: whole-tick shift, numerically identical to P1 (S080). */
static void seq_loopShiftPosition(uint8_t track, seq_loop_pos_t *pos,
                                  int32_t ticks)
{
	(void)seq_shiftPositionQ8(track, pos, ticks * 256);
}

/*
 * Canonical position just before a master tick's advance (S080 P2 Defect 14).
 *
 * What: the master-clock position mapping of seq_realignTrackToMasterClock()
 * (position = (tick << 8) / interval, accumulator = remainder, play mode
 * applied to the index, pip direction from the 2 x len cycle) evaluated at
 * `anchor - 1`, i.e. the state the DDA would hold just before tick `anchor`
 * advances. Anchor 0 is the transport-start seed (cursor -1, accumulator
 * interval - 256). Why: after a scene change the looped track's loop start
 * stays at the same master-clock tick; this computes the incoming Scene's
 * content at that tick with the incoming Scene's own scale and length (plan
 * §3.6 corrected). Inputs: track, anchor tick. Output: position image.
 * Accessors: seq_loopRefreshSceneSnapshots(). Affiliates:
 * seq_stepIndexForMode(), seq_realignTrackToMasterClock().
 */
static void seq_loopCanonicalPreAdvance(uint8_t track, uint32_t anchor,
                                        seq_loop_pos_t *pos)
{
	const pat_scene_region_t *region =
	    pat_sceneRegion(seq_perTrackPattern[track]);
	uint8_t len = seq_effectiveTrackLength[track];
	uint8_t mode = region ? region->track_play_mode[track] : 0u;
	uint16_t interval = stepScale_ticksQ8(seq_effectiveTrackScale[track]);
	uint64_t master_q8;
	uint64_t position;

	if (len < 1u)
		len = NUM_STEPS_PER_BAR;
	if (mode >= 6u)
		mode = 0u;
	pos->play_state = 0u;
	if (anchor == 0u) {
		pos->step = -1;
		pos->accum = (uint16_t)(interval - 256u);
		return;
	}
	master_q8 = (uint64_t)(anchor - 1u) << 8u;
	position = master_q8 / interval;
	pos->step = seq_stepIndexForMode(mode, (uint32_t)position, len);
	pos->accum = (uint16_t)(master_q8 % interval);
	if (mode == 2u && (position % ((uint64_t)len * 2u)) >= len)
		pos->play_state = SEQ_PLAY_STATE_PIP_REV;
}

/*
 * PRIMASK snapshot of a track's timing state (S081 §19.1 item 11, C-8a).
 *
 * What: captures seq_elapsedPpqTicks, the PPQ us stamp/period, seq_stepIndex,
 * the DDA accumulator, the effective interval/length/shuffle, play mode/state
 * and the played Scene under PRIMASK. Why: LiveRecord runs in the foreground and
 * needs a consistent snapshot of TIM3-mutated state for placement math.
 * S081 P3: also copies the track's loop anchor, snapshot and loop length when
 * it is looped.
 */
void seq_captureTrackTime(uint8_t track, seq_track_time_t *out)
{
    const pat_scene_region_t *region;
    uint32_t primask;

    if (!out || track >= NUM_TRACKS)
        return;
    primask = __get_PRIMASK();
    __disable_irq();
    out->elapsedTick = seq_elapsedPpqTicks;
    out->stampUs = seq_ppqStampUs;
    out->periodUs = seq_ppqPeriodUs;
    /* S081 P1B item 17: the µs time of this capture, same section as the tick. */
    out->nowUs = timebase_tim2Now();
    out->stepIndex = seq_stepIndex[track];
    out->accumulator = seq_trackAccumulator[track];
    out->interval = (uint16_t)stepScale_ticksQ8(seq_effectiveTrackScale[track]);
    out->length = seq_effectiveTrackLength[track];
    out->shuffle = seq_effectiveTrackShuffle[track];
    region = pat_sceneRegion(seq_perTrackPattern[track]);
    out->playMode = region ? region->track_play_mode[track] : 0u;
    out->playState = seq_trackPlayState[track];
    out->playedScene = seq_perTrackPattern[track];
    /*
     * S081 P3: the track's loop state under the same PRIMASK, so placement
     * sees the anchor and snapshot that the next wraps will use (plan §3.3).
     * Not looped (or before the first scheduler tick): looped = 0.
     */
    out->looped = (uint8_t)(seq_loopActive && !seq_initialSchedulerTick &&
                            (seq_loopTrackMask & (uint8_t)(1u << track)) != 0u);
    out->loopAnchor = seq_loopAnchor[track];
    out->loopStartStep = seq_loopStartStep[track];
    out->loopStartAccum = seq_loopStartAccum[track];
    out->loopStartPlayState = seq_loopStartPlayState[track];
    out->loopTicks = seq_loopLengthTicks(seq_loopDivision, seq_loopModifier);
    __set_PRIMASK(primask);
}

/*
 * Map a placement shift that crosses a loop wrap (S081 P3, plan §3.3, Q6).
 *
 * What: for a looped track, finds which loop pass the target time falls in.
 * Times are Q8 ticks relative to the snapshot (the state after tick
 * anchor - 1): the capture is nowRel = (elapsed - anchor + 1) * 256; pass k
 * covers relative times 256 + k*L*256 .. 255 + (k+1)*L*256 (the wrap at tick
 * anchor + k*L restores the snapshot and the same tick's DDA adds 256).
 *  - Same pass as the capture, or from the first pass back into the
 *    unlooped history: returns 0, the caller walks the live cursor (exact).
 *  - A later or earlier pass: the image is the snapshot shifted by the
 *    target's phase in its pass (x - k*L*256 + 256), i.e. exactly what that
 *    pass plays. A forward snap across the wrap lands on the replayed step
 *    after it (Q6).
 *  - Before the loop's first pass (kTgt < 0) from a later pass: the
 *    unlooped history, the snapshot shifted by the full relative offset.
 * Why: the live-cursor walk (P1B) continues past the window end as if
 * unlooped. Verified against a TIM3 timeline simulation: 0 mismatches in
 * 86,593,860 cases (S081_P3_IMPLEMENTATION.md §0). Inputs: track, capture
 * snapshot with loop fields, signed Q8 delta. Output: *pos and 1 when the
 * snapshot branch applies, else 0 (pos untouched). Context: foreground
 * (64-bit maths). Accessors: seq_placeFromSnapshot(). Affiliates:
 * seq_shiftPositionQ8(), seq_loopProcessTick(), seq_captureTrackTime().
 */
static uint8_t seq_placeLoopImage(uint8_t track, const seq_track_time_t *snap,
                                  int32_t deltaQ8, seq_loop_pos_t *pos)
{
    int64_t span;
    int64_t nowRel;
    int64_t tgtRel;
    int64_t kNow;
    int64_t kTgt;
    int64_t x;

    if (!snap->looped || snap->loopTicks == 0u ||
        snap->elapsedTick < snap->loopAnchor)
        return 0u;
    span = (int64_t)snap->loopTicks * 256;
    nowRel = ((int64_t)snap->elapsedTick - (int64_t)snap->loopAnchor + 1) * 256;
    tgtRel = nowRel + (int64_t)deltaQ8;
    kNow = (nowRel - 256) / span;
    x = tgtRel - 256;
    kTgt = (x >= 0) ? (x / span) : -((-x + span - 1) / span);
    if (kTgt == kNow || (kNow == 0 && kTgt < 0))
        return 0u;                      /* same pass: live cursor is exact */
    pos->step = snap->loopStartStep;
    pos->accum = snap->loopStartAccum;
    pos->play_state = snap->loopStartPlayState;
    (void)seq_shiftPositionQ8(track, pos,
        (int32_t)((kTgt < 0) ? tgtRel : (x - kTgt * span + 256)));
    return 1u;
}

/*
 * Place a captured hit on a track's live step grid (S081 §8.3, P1B item 5).
 *
 * What: starting from the PRIMASK snapshot of the track's live cursor,
 * accumulator and play state taken when the hit sounded, shifts the image by
 * deltaQ8 (the quantised or sub-tick offset from the capture tick) and
 * reports the step the hit lands on, the micro-timing delay (0..99) from
 * that step's nominal start, and whether the step is behind, current or
 * ahead of the playhead. snapToStep rounds to the nearest step start of the
 * track's own grid (qnt trk). Shuffle is not used: it applies only at
 * playback, so a recorded step gets the track's shuffle like any other
 * step. Why: the P1 code mapped ticks through the canonical master-clock
 * position (tick*256/current interval), which differs from the playing step
 * as soon as the track's scale or length changed during playback (menu
 * edit, Morph, scale/length automation). Hits then landed on the wrong
 * step. Placement must follow the track that is actually playing. P1B
 * measured the delay from the shuffled start of odd steps, so a hit before
 * that start (including a quantised hit exactly on the grid) was written to
 * the previous step at delay 99. Inputs: track, snapshot, signed Q8 delta,
 * snap flag. Outputs: *out (step, delay, rel, ahead, snapQ8); returns 0
 * when the track has not started (cursor -1). Context: foreground.
 * Accessors: liveRec_triggerIn(), liveRec_placeFlam(), liveRec_endRoll().
 * Affiliates: seq_shiftPositionQ8(), seq_captureTrackTime().
 * S081 P3: for a looped track, a shift that crosses a loop wrap is mapped
 * onto the pass the target falls in (seq_placeLoopImage()): a forward snap
 * across the wrap lands on the replayed step after it, a backward one at the
 * end of the previous pass (or in the unlooped history before the loop's
 * first pass). rel is then decided by time; ahead is the time to the landed
 * step, so the echo mark covers the play after the wrap.
 */
uint8_t seq_placeFromSnapshot(uint8_t track, const seq_track_time_t *snap,
                              int32_t deltaQ8, uint8_t snapToStep,
                              seq_place_t *out)
{
    seq_loop_pos_t pos;
    int32_t steps;
    uint16_t interval;
    uint16_t off;
    uint8_t mode;
    uint8_t delay = 0u;
    int32_t snapQ8 = 0;
    uint8_t crossed;

    if (!snap || !out || track >= NUM_TRACKS)
        return 0u;
    if (snap->stepIndex < 0)
        return 0u;
    pos.step = snap->stepIndex;
    pos.accum = snap->accumulator;
    pos.play_state = snap->playState;
    mode = snap->playMode;
    if (mode >= 6u)
        mode = 0u;
    /* rnd: backward only (plan §8.3). */
    if (mode == 3u && deltaQ8 > 0)
        deltaQ8 = 0;
    /* S081 P3: a shift across a loop wrap maps onto the replayed pass. */
    crossed = seq_placeLoopImage(track, snap, deltaQ8, &pos);
    steps = crossed ? 0 : seq_shiftPositionQ8(track, &pos, deltaQ8);
    if (pos.step < 0)
        return 0u;
    off = pos.accum;
    interval = snap->interval;
    if (interval == 0u)
        interval = 1u;

    if (snapToStep) {
        if ((uint32_t)off * 2u >= (uint32_t)interval) {
            snapQ8 = (int32_t)interval - (int32_t)off;
            steps += seq_shiftPositionQ8(track, &pos, snapQ8);
            off = pos.accum;
        } else {
            snapQ8 = -(int32_t)off;
            off = 0u;
        }
    }

    delay = (uint8_t)(((uint32_t)off * 100u + interval / 2u) / interval);
    if (delay >= 100u) {
        steps += seq_shiftPositionQ8(track, &pos,
                                     (int32_t)interval - (int32_t)off);
        off = pos.accum;
        delay = 0u;
    }

    out->step = (uint8_t)pos.step;
    out->delay = delay;
    /*
     * S081 P3: behind / current / ahead by TIME when the shift crossed a
     * wrap (the landed step is in another pass; its index may be lower even
     * though it plays later). Otherwise by step advances, as before.
     */
    if (crossed)
        out->rel = (int8_t)((deltaQ8 > 0) ? 1 : -1);
    else
        out->rel = (int8_t)((steps > 0) ? 1 : ((steps < 0) ? -1 : 0));
    out->snapQ8 = (int16_t)snapQ8;
    out->ahead = 0u;
    if (out->rel > 0) {
        /* The trk snap's shift counts toward the distance (P1C review C-2). */
        int32_t aheadQ8 = deltaQ8 + snapQ8 - (int32_t)off;

        out->ahead = (aheadQ8 > 0) ? (uint16_t)(aheadQ8 >> 8) : 0u;
    }
    return 1u;
}

/*
 * Quantise a capture to the master-clock grid (S081 §8.3, P1B item 5).
 *
 * What: for qnt 1/8, 1/16, 1/32, 1/64 (48/24/12/6 ticks) returns the
 * nearest grid tick to (tick + frac8/256); for off and trk returns tick
 * unchanged. Why: master-grid quantise is the same for every track; the
 * track-grid snap (trk) must follow the track's live step starts, so it
 * lives in seq_placeFromSnapshot(). The sub-tick fraction makes the
 * half-way decision exact. Inputs: capture tick, sub-tick fraction 0..255.
 * Output: grid tick. Context: any. Accessors: liveRec_triggerIn().
 * Affiliates: seq_quantisation, seq_loopEventAnchor() (same grid).
 */
uint32_t seq_quantiseTick(uint32_t tick, uint8_t frac8)
{
    uint32_t q;
    uint32_t rem;

    switch (seq_quantisation) {
    case QUANT_8:  q = 48u; break;
    case QUANT_16: q = 24u; break;
    case QUANT_32: q = 12u; break;
    case QUANT_64: q = 6u;  break;
    default:       return tick;   /* off / trk */
    }
    rem = tick % q;
    return ((rem * 256u + frac8) >= (q * 128u)) ? (tick + (q - rem))
                                                : (tick - rem);
}

/*
 * Loop start tick for an engage / add event (S080 plan §3.4, P2 D4).
 *
 * What: with recording quantisation on (qnt 8/16/32/64), the last quantise
 * boundary on the master clock (it may lie more than one loop length back:
 * the track then jumps back at the next wrap, plan §3.4 note). With qnt off,
 * the tick at which the fastest track of `mask` last advanced, i.e.
 * now - floor(accumulator / 256). Before the first scheduler tick the anchor
 * is 0. Why: the loop must start at the beginning of a step, not wherever the
 * button landed, so the first repeat plays the step that was sounding.
 * Inputs: mask (non-zero), seq_elapsedPpqTicks, seq_quantisation, the DDA
 * accumulators. Output: anchor tick <= now. Context: foreground under
 * PRIMASK. Accessors: seq_loopSnapshotTracks().
 */
static uint32_t seq_loopEventAnchor(uint8_t mask)
{
	uint32_t now = seq_elapsedPpqTicks;
	uint16_t q;
	uint16_t best = 0xFFFFu;
	uint8_t fastest = 0u;
	uint8_t track;
	uint32_t back;

	if (seq_initialSchedulerTick)
		return 0u;
	switch (seq_quantisation) {
	case QUANT_8:  q = 48u; break;
	case QUANT_16: q = 24u; break;
	case QUANT_32: q = 12u; break;
	case QUANT_64: q = 6u;  break;
	default:       q = 0u;  break;
	}
	if (q != 0u)
		return now - (now % q);
	for (track = 0u; track < NUM_TRACKS; track++) {
		uint16_t interval;

		if ((mask & (uint8_t)(1u << track)) == 0u)
			continue;
		interval = stepScale_ticksQ8(seq_effectiveTrackScale[track]);
		if (interval < best) {
			best = interval;
			fastest = track;
		}
	}
	back = (uint32_t)(seq_trackAccumulator[fastest] >> 8u);
	return (back > now) ? 0u : (now - back);
}

/*
 * Snapshot the tracks of one engage / add event (S080 P2 Defect 7, D4).
 *
 * What: computes one anchor for the event and, for every track in `mask`,
 * rewinds its live state to just before that anchor's advance and stores it as
 * the loop snapshot. Why: tracks added in the same gesture share one loop
 * start; tracks already looping are untouched. Inputs: mask, live DDA state.
 * Outputs: seq_loopAnchor/StartStep/StartAccum/StartPlayState for `mask`.
 * Context: foreground under PRIMASK. Accessors: seq_loopEngage(),
 * seq_loopUpdateMask().
 */
static void seq_loopSnapshotTracks(uint8_t mask)
{
	uint32_t anchor;
	int32_t back;
	uint8_t track;

	if (mask == 0u)
		return;
	anchor = seq_loopEventAnchor(mask);
	back = seq_initialSchedulerTick
	    ? 0 : -(int32_t)(seq_elapsedPpqTicks - anchor + 1u);
	for (track = 0u; track < NUM_TRACKS; track++) {
		seq_loop_pos_t pos;
		uint32_t track_anchor = anchor;
		int32_t track_back = back;

		if ((mask & (uint8_t)(1u << track)) == 0u)
			continue;
		/*
		 * Per-track loop anchor for QUANT_TRACK (S081 §5.6, L1, Appendix A-15).
		 *
		 * seq_loopEventAnchor() is unchanged. `off` keeps the fastest-track
		 * anchor; 1/8..1/64 keep the last grid boundary; `trk` falls into its
		 * default q = 0 path, but its single return value is not used here.
		 * Instead, when seq_quantisation == QUANT_TRACK, each track in the mask
		 * gets its own anchor: its own last step start (now - accumulator>>8),
		 * the same formula `off` applies to the fastest track only. `trk`
		 * catches every track's own sounding step. Anchors choose only which
		 * window [anchor, anchor + L) a track replays; every track wraps on the
		 * same length L, so step timing under the loop is unchanged. Edge
		 * (existing, accepted in S080): a track whose step is longer than the
		 * loop is anchored more than one length back and jumps back at the next
		 * wrap.
		 */
		if (seq_initialSchedulerTick) {
			track_anchor = 0u;
			track_back = 0;
		} else if (seq_quantisation == QUANT_TRACK) {
			uint32_t now = seq_elapsedPpqTicks;
			uint32_t b = (uint32_t)(seq_trackAccumulator[track] >> 8u);

			track_anchor = (b > now) ? 0u : (now - b);
			track_back = -(int32_t)(now - track_anchor + 1u);
		}
		pos.step = seq_stepIndex[track];
		pos.accum = seq_trackAccumulator[track];
		pos.play_state = seq_trackPlayState[track];
		seq_loopShiftPosition(track, &pos, track_back);
		seq_loopAnchor[track] = track_anchor;
		seq_loopStartStep[track] = pos.step;
		seq_loopStartAccum[track] = pos.accum;
		seq_loopStartPlayState[track] = pos.play_state;
	}
}

/*
 * Resume one track where it would have been without the loop (S080 P2 D3).
 *
 * What: projects the track's loop snapshot (state just before its anchor
 * tick) forward to the current tick and writes it to the live cursor,
 * accumulator and play state, then refreshes the chase LED for the selected
 * voice. Why: dropped or released tracks must stay in time with the tracks
 * that never looped; projecting from the snapshot is exact even when the
 * track was not on its canonical master-clock position (the P2 realign was
 * not). After a scene change the snapshot is the incoming Scene's canonical
 * position, so the projection equals the incoming Scene's master-clock
 * position. Inputs: track, snapshot, anchor, seq_elapsedPpqTicks. Outputs:
 * live track state. Context: foreground under PRIMASK. Accessors:
 * seq_loopUpdateMask(), seq_loopDisengage().
 * S081 P3: also drops the track's echo mark and pushes a jump entry when the
 * step changes.
 */
static void seq_loopResumeTrack(uint8_t track)
{
	seq_loop_pos_t pos;
	uint32_t ahead;
	int16_t before = seq_stepIndex[track];

	pos.step = seq_loopStartStep[track];
	pos.accum = seq_loopStartAccum[track];
	pos.play_state = seq_loopStartPlayState[track];
	ahead = seq_initialSchedulerTick
	    ? 0u : (seq_elapsedPpqTicks - seq_loopAnchor[track] + 1u);
	if (ahead > 0x7FFFFFFFu)
		ahead = 0x7FFFFFFFu;
	seq_loopShiftPosition(track, &pos, (int32_t)ahead);
	seq_stepIndex[track] = pos.step;
	seq_trackAccumulator[track] = pos.accum;
	seq_trackPlayState[track] = pos.play_state;
	seq_lastMasterStep[track] = (uint8_t)((pos.step < 0) ? 0 : pos.step);
	/* S081 P2: a playhead break ends automation ramps (no B will follow). */
	seq_rampBreakTrack(track);
	/*
	 * Live record across a resume (S081 P3, plan §3.4/§3.9).
	 *
	 * What: (1) the track's echo mark is dropped: the cursor jumps, so a
	 * marked step index no longer means the play it was set for. (2) If the
	 * resumed cursor is on another step, a jump entry reports that step as
	 * beginning now (fired 0); the next DDA advance reports the following
	 * step as usual. Why: release, a track leaving in a swap and disengage
	 * move the playhead without a step advance, so LiveRecord would keep the
	 * pre-resume step. Same step index: no entry (the playhead continues the
	 * step it was in). Context: foreground; every caller
	 * (seq_loopUpdateMask(), seq_loopDisengage()) holds PRIMASK, so the push
	 * cannot interleave with the TIM3 producer. Affiliates:
	 * seq_recRingPush(), seq_suppressStepOnce(), liveRec_drainRing().
	 */
	seq_echoStep[track] = -1;
	if (pos.step >= 0 && pos.step != before)
		seq_recRingPush(track, pos.step, 0u, 1u);
	if (track == menu_getActiveVoice()) {
		seq_ledState.chaseStep = seq_stepIndex[track];
		seq_ledState.dirty |= SEQ_LED_DIRTY_CHASE;
	}
}

uint8_t seq_loopEngage(uint8_t trackMask, uint8_t division, uint8_t modifier)
{
	uint32_t primask;
	uint8_t mask;

	/*
	 * Start a loop (S080 C2, P2 D3/D4/D17).
	 *
	 * What: filters trackMask by the loopable flags, snapshots the tracks at
	 * one anchor, publishes division/modifier/mask and sets seq_loopActive
	 * last, all under PRIMASK so the TIM3 ISR never sees a partial loop.
	 * The record-off now happens in the caller (S081 §3.3-9, B-4b): the
	 * Sequencer must not call back into LiveRecord. Engages only while the
	 * transport runs; returns
	 * the mask now looping (0 when stopped). Inputs: 7-bit mask, division
	 * 0..7, modifier 0..2. Outputs: looper state. Accessors: buttonHandler
	 * SELECT press (PERF). Affiliates: seq_loopableTrackMask(),
	 * seq_loopSnapshotTracks().
	 */
	if (!seq_running)
		return 0u;
	if (division > 7u)
		division = 0u;
	if (modifier > 2u)
		modifier = 0u;
	mask = (uint8_t)(trackMask & seq_loopableTrackMask() & 0x7Fu);
	primask = __get_PRIMASK();
	__disable_irq();
	seq_loopActive = 0u;
	seq_loopDivision = division;
	seq_loopModifier = modifier;
	seq_loopSnapshotTracks(mask);
	seq_loopTrackMask = mask;
	seq_loopActive = 1u;            /* final gate */
	__set_PRIMASK(primask);
	return mask;
}

uint8_t seq_loopUpdateMask(uint8_t newMask)
{
	uint32_t primask;
	uint8_t old;
	uint8_t track;

	/*
	 * Change the looped track set while engaged (S080 P2 Defect 7, D1/D2).
	 *
	 * What: tracks leaving the set resume where they would have been (inside
	 * this call, before the mask is committed - the P2 ordering bug); tracks
	 * staying keep their anchors and snapshots; tracks joining get one fresh
	 * anchor for this event. Division and modifier are untouched. Inputs:
	 * desired 7-bit mask (filtered by the loopable flags). Outputs: looper
	 * state; returns the committed mask (0 when not engaged). Context:
	 * foreground; PRIMASK held for the whole update. Accessors: buttonHandler
	 * VOICE press/release (PERF). Affiliates: seq_loopResumeTrack(),
	 * seq_loopSnapshotTracks().
	 */
	newMask = (uint8_t)(newMask & seq_loopableTrackMask() & 0x7Fu);
	primask = __get_PRIMASK();
	__disable_irq();
	if (!seq_loopActive) {
		__set_PRIMASK(primask);
		return 0u;
	}
	old = seq_loopTrackMask;
	for (track = 0u; track < NUM_TRACKS; track++) {
		uint8_t bit = (uint8_t)(1u << track);

		if ((old & bit) != 0u && (newMask & bit) == 0u)
			seq_loopResumeTrack(track);
	}
	seq_loopSnapshotTracks((uint8_t)(newMask & (uint8_t)~old));
	seq_loopTrackMask = newMask;
	__set_PRIMASK(primask);
	return newMask;
}

void seq_loopDisengage(void)
{
	uint32_t primask;
	uint8_t mask;
	uint8_t track;

	/*
	 * End the loop (S080 C2, P2 D3).
	 *
	 * What: clears the gate and the mask, then resumes every looped track
	 * where it would have been, under PRIMASK. With the transport stopped
	 * there is nothing to resume (the stop already reset every cursor).
	 * Accessors: buttonHandler_loopCheckDisengage()/loopForceRelease().
	 */
	primask = __get_PRIMASK();
	__disable_irq();
	mask = seq_loopTrackMask;
	seq_loopActive = 0u;
	seq_loopTrackMask = 0u;
	if (seq_running) {
		for (track = 0u; track < NUM_TRACKS; track++) {
			if ((mask & (uint8_t)(1u << track)) != 0u)
				seq_loopResumeTrack(track);
		}
	}
	__set_PRIMASK(primask);
}

void seq_loopUpdateDivision(uint8_t division, uint8_t modifier)
{
	/*
	 * Change the loop length (S080 C2; P2 Defect 7: never moves a loop
	 * start). Single-byte stores; the ISR picks up the new length on its next
	 * tick and keeps wrapping on each track's anchor grid. Accessors:
	 * buttonHandler SELECT press/release and BAR press/release (PERF).
	 */
	if (division > 7u)
		division = 0u;
	if (modifier > 2u)
		modifier = 0u;
	seq_loopDivision = division;
	seq_loopModifier = modifier;
}

/*
 * Recompute loop snapshots for a newly selected Scene (S080 P2 Defect 14).
 *
 * What: keeps every looped track's anchor tick and replaces its snapshot with
 * the incoming Scene's canonical position just before that tick (its own
 * scale, length and play mode). Example: outgoing 1/16 loop start at step 5
 * (tick 96), incoming 1/8 -> step 3. Why: the loop start stays at the same
 * master-clock position across scene changes (plan §3.6 corrected). The caller
 * has just refreshed the effective-timing cache and realigned every track.
 * Inputs: seq_loopAnchor[], effective cache. Outputs: snapshots. Context:
 * foreground; caller holds PRIMASK. Accessors: seq_selectActivePattern(),
 * seq_alignActivePatternToScene(), seq_chainRepeatSelf().
 */
static void seq_loopRefreshSceneSnapshots(void)
{
	uint8_t track;

	if (!seq_loopActive)
		return;
	for (track = 0u; track < NUM_TRACKS; track++) {
		seq_loop_pos_t pos;

		if ((seq_loopTrackMask & (uint8_t)(1u << track)) == 0u)
			continue;
		seq_loopCanonicalPreAdvance(track, seq_loopAnchor[track], &pos);
		seq_loopStartStep[track] = pos.step;
		seq_loopStartAccum[track] = pos.accum;
		seq_loopStartPlayState[track] = pos.play_state;
	}
}

/*
 * Rebase an engaged loop to the start (S080 P2 Defect 14, decided).
 *
 * What: every looped track's anchor becomes tick 0 and its snapshot the
 * freshly seeded start state (cursor -1, accumulator interval - 256). Why: a
 * resetting scene change (pcr on for direct/chain changes, a qac reset for
 * queued ones) or an external/boundary reset restarts the master clock, so
 * the old anchor ticks no longer exist; the loop start becomes the beginning
 * of the incoming Scene. Inputs: seeded live state. Outputs: anchors and
 * snapshots. Context: called at the end of seq_setStepIndexToStart() (TIM3 or
 * foreground). The transport-stop path clears seq_loopActive first, so this is
 * a no-op there.
 */
static void seq_loopRebaseToStart(void)
{
	uint8_t track;

	if (!seq_loopActive)
		return;
	for (track = 0u; track < NUM_TRACKS; track++) {
		if ((seq_loopTrackMask & (uint8_t)(1u << track)) == 0u)
			continue;
		seq_loopAnchor[track] = 0u;
		seq_loopStartStep[track] = seq_stepIndex[track];
		seq_loopStartAccum[track] = seq_trackAccumulator[track];
		seq_loopStartPlayState[track] = seq_trackPlayState[track];
	}
}

/*
 * Looper wrap (S080 C3, rewritten S080 P2 D3).
 *
 * What: for every looped track whose (elapsed - anchor) is a positive
 * multiple of the loop length, restores the snapshot. Called once per master
 * tick before the DDA advance, so the step that fires at the anchor tick is
 * part of the loop and replays on every wrap. Why: in the TIM3 ISR so tight
 * divisions (1/64, 1/128) loop sample-accurately; per-track anchors keep
 * every wrap on the anchor's master-clock grid. Inputs: seq_loopActive,
 * division/modifier, mask, anchors, snapshots, seq_elapsedPpqTicks. Outputs:
 * seq_stepIndex[]/seq_trackAccumulator[]/seq_trackPlayState[] of wrapped
 * tracks. Context: TIM3 ISR. Affiliates: seq_processSchedulerTick().
 * S081 P3: a wrap that lands mid-step pushes a jump entry for the recording
 * track (no step advance follows on this tick); echo marks are not cleared
 * here (plan §3.4: the P1B deadline expires them).
 */
static void seq_loopProcessTick(void)
{
	uint16_t loop_ticks;
	uint8_t mask;
	uint8_t track;

	if (!seq_loopActive)
		return;
	mask = seq_loopTrackMask;
	if (mask == 0u)
		return;
	loop_ticks = seq_loopLengthTicks(seq_loopDivision, seq_loopModifier);
	for (track = 0u; track < NUM_TRACKS; track++) {
		uint32_t since;

		if ((mask & (uint8_t)(1u << track)) == 0u)
			continue;
		if (seq_loopAnchor[track] >= seq_elapsedPpqTicks)
			continue;
		since = seq_elapsedPpqTicks - seq_loopAnchor[track];
		if ((since % loop_ticks) != 0u)
			continue;
		seq_stepIndex[track] = seq_loopStartStep[track];
		seq_trackAccumulator[track] = seq_loopStartAccum[track];
		seq_trackPlayState[track] = seq_loopStartPlayState[track];
		/* S081 P2 (D8): no ramp across the loop wrap; the restored step's
		 * apply starts fresh. */
		seq_rampBreakTrack(track);
		/*
		 * Report a mid-step wrap to live record (S081 P3 FQ3, plan §3.9).
		 *
		 * What: when this tick's DDA will not advance the restored track
		 * (accumulator + 256 < interval), no step-advance entry follows, so
		 * a jump entry reports the restored step as beginning now (fired 0).
		 * When the DDA does advance, its own entry reports the replayed step
		 * and nothing is pushed here. Why: with qnt off or a 1/8..1/64 grid,
		 * a track whose step does not start at the anchor wraps mid-step;
		 * without an entry LiveRecord keeps treating the pre-wrap step as
		 * playing (wrong-step writes; the first-change dirty bit blocks the
		 * next pass). Every wrap must be a new pass for live record, also
		 * for loops shorter than a step. Inputs: restored cursor and
		 * accumulator, effective scale. Output: at most one ring entry.
		 * Context: TIM3, before the DDA. Affiliates: seq_recRingPush(),
		 * liveRec_drainRing(), liveRec_stepEnd() (jump rule).
		 */
		if (!seq_SomModeActive &&
		    (uint32_t)seq_trackAccumulator[track] + 256u <
		        (uint32_t)stepScale_ticksQ8(seq_effectiveTrackScale[track]))
			seq_recRingPush(track, seq_stepIndex[track], 0u, 1u);
	}
}

/*
 * Tracks the looper may capture (S080 P2 Defect 8a).
 *
 * What: bit N set when track N's played Scene does not exclude it from
 * looping (pat_scene_region_t.track_loop_exclude). Why: excluded tracks are
 * silently left out of every engage/add, with no LED or error. Read at
 * gesture time; a later scene change does not drop a track already looping.
 * Inputs: seq_perTrackPattern[], resident regions. Output: 7-bit mask.
 * Accessors: seq_loopEngage(), seq_loopUpdateMask().
 */
uint8_t seq_loopableTrackMask(void)
{
	uint8_t mask = 0u;
	uint8_t track;

	for (track = 0u; track < NUM_TRACKS; track++) {
		const pat_scene_region_t *region =
		    pat_sceneRegion(seq_perTrackPattern[track]);

		if (!region ||
		    (region->track_loop_exclude & (uint8_t)(1u << track)) == 0u)
			mask |= (uint8_t)(1u << track);
	}
	return mask;
}

/* =====================================================================
 * S080 Phase G scene chain engine
 * ===================================================================== */

#define SEQ_CHAIN_BAR_TICKS 384u    /* 16 x 1/16 at 96 PPQ */

/*
 * True on the tick at which the longest track completes a repetition
 * (S080 G7, P2 D10).
 *
 * What: the repetition length is the largest effective length x Q8.8 step
 * interval over the seven tracks; the boundary is the first master tick
 * at or after each multiple of it. Why: "end of repetition = end of the
 * longest track by scale x length" (plan §4.5), on the master clock so
 * realigned and reset Scenes agree. Inputs: effective cache,
 * seq_elapsedPpqTicks. Output: 0/1. Context: TIM3 ISR.
 */
static uint8_t seq_chainAtRepetitionEnd(void)
{
	uint32_t longest = 0u;
	uint8_t track;

	for (track = 0u; track < NUM_TRACKS; track++) {
		uint8_t len = seq_effectiveTrackLength[track];
		uint32_t cycle;

		if (len < 1u)
			len = NUM_STEPS_PER_BAR;
		cycle = (uint32_t)len *
		        stepScale_ticksQ8(seq_effectiveTrackScale[track]);
		if (cycle > longest)
			longest = cycle;
	}
	if (longest == 0u)
		return 0u;
	return (uint8_t)((((uint64_t)seq_elapsedPpqTicks << 8u) % longest) < 256u);
}

/*
 * Weight of one chain condition at this boundary (S080 G7).
 *
 * What: 0 = does not fire here, 100 = fires, 10..90 = probability (r1x..r9b).
 * Index order matches chainConditionNames[] (MenuText.h): 0 nul, 1 xal, 2 bal,
 * 3..13 xNN, 14..24 bNN, 25..33 rNx, 34..42 rNb, 43..46 X:4, 47..54 X:8,
 * 55..70 >NN, 71 fix, 72 fib, 73 !fx, 74 !fb, 75 lox, 76 lob, 77 !lx, 78 !lb.
 * Repetition-kind conditions only count at a repetition end, bar-kind only at
 * a bar end. Counts fire when equal (D11). X:4 / X:8 use the master bar just
 * completed. Inputs: condition, boundary flags, chain counters, fill/loop
 * flags, seq_previousScene. Output: weight. Context: TIM3 ISR.
 */
static uint8_t seq_chainConditionWeight(uint8_t cond, uint8_t at_bar,
                                        uint8_t at_rep)
{
	static const uint8_t counts[11] = {
		1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 16u, 32u, 64u
	};
	uint32_t bar_done = seq_elapsedPpqTicks / SEQ_CHAIN_BAR_TICKS;

	if (cond == 0u)
		return 0u;
	if (cond == 1u)
		return at_rep ? 100u : 0u;
	if (cond == 2u)
		return at_bar ? 100u : 0u;
	if (cond <= 13u)
		return (at_rep && seq_chainRepCount == counts[cond - 3u]) ? 100u : 0u;
	if (cond <= 24u)
		return (at_bar && seq_chainBarCount == counts[cond - 14u]) ? 100u : 0u;
	if (cond <= 33u)
		return at_rep ? (uint8_t)((cond - 24u) * 10u) : 0u;
	if (cond <= 42u)
		return at_bar ? (uint8_t)((cond - 33u) * 10u) : 0u;
	if (cond <= 46u)
		return (at_bar && bar_done != 0u &&
		        ((bar_done - 1u) % 4u) == (uint32_t)(cond - 43u)) ? 100u : 0u;
	if (cond <= 54u)
		return (at_bar && bar_done != 0u &&
		        ((bar_done - 1u) % 8u) == (uint32_t)(cond - 47u)) ? 100u : 0u;
	if (cond <= 70u)
		return (at_rep && seq_previousScene == (uint8_t)(cond - 55u))
		    ? 100u : 0u;
	switch (cond) {
	case 71u: return (at_rep && seq_fillActive) ? 100u : 0u;
	case 72u: return (at_bar && seq_fillActive) ? 100u : 0u;
	case 73u: return (at_rep && !seq_fillActive) ? 100u : 0u;
	case 74u: return (at_bar && !seq_fillActive) ? 100u : 0u;
	case 75u: return (at_rep && seq_loopActive) ? 100u : 0u;
	case 76u: return (at_bar && seq_loopActive) ? 100u : 0u;
	case 77u: return (at_rep && !seq_loopActive) ? 100u : 0u;
	case 78u: return (at_bar && !seq_loopActive) ? 100u : 0u;
	default:  return 0u;
	}
}

/*
 * Resolve a next-scene value to a Scene index (S080 G7, D12).
 *
 * What: 0 slf, 1 nxt (wraps 16->1), 2 prv (wraps 1->16), 3..18 s01..s16,
 * 19..34 rNN (random among Scenes 1..NN), 35..38 r+N (random among the next N,
 * excluding the current), 39..42 r-N (previous N). Returns 0xFF for stp or an
 * absent target (no change). Order matches chainNextNames[]. Inputs: next
 * value, current Scene. Output: Scene 0..15 or 0xFF. Context: TIM3 ISR
 * (GetRngValue() and bank_scenePresent() are ISR-safe reads).
 */
static uint8_t seq_chainResolveNext(uint8_t next, uint8_t cur)
{
	uint8_t target;
	uint16_t rnd = (uint16_t)GetRngValue() & 0x7FFFu;

	/*
	 * S082 C9: Scene 17 has no chain number. Its chain fields are cleared when
	 * it is built, so this is a defensive guard against a stale condition
	 * evaluating while a background Bank load runs; never wrap (cur + 1) to 0.
	 */
	if (cur >= SCENE_COUNT)
		return 0xFFu;
	if (next == 0u)
		target = cur;
	else if (next == 1u)
		target = (uint8_t)((cur + 1u) & 0x0Fu);
	else if (next == 2u)
		target = (uint8_t)((cur + 15u) & 0x0Fu);
	else if (next <= 18u)
		target = (uint8_t)(next - 3u);
	else if (next <= 34u)
		target = (uint8_t)(rnd % (uint16_t)(next - 18u));
	else if (next <= 38u)
		target = (uint8_t)((cur + 1u + (rnd % (uint16_t)(next - 34u))) & 0x0Fu);
	else if (next <= 42u)
		target = (uint8_t)((cur + 15u - (rnd % (uint16_t)(next - 38u))) & 0x0Fu);
	else
		return 0xFFu;               /* stp is handled by the caller */
	return bank_scenePresent(target) ? target : 0xFFu;
}

/*
 * Evaluate the active Scene's chain at a boundary and fire (S080 G7).
 *
 * What: weights both conditions; condition 1 has priority. w1 = 100 fires 1.
 * Two probabilities summing over 100 are weighted proportionally (one always
 * fires). Otherwise one draw r in 0..99: r < w1 fires 1, r < w1 + w2 fires 2,
 * else nothing (the Scene repeats). A `stp` result freezes every track; any
 * other result raises a chain-marked direct change that the foreground runs at
 * once (not re-quantised by scd, D9). Why: plan §4.5 evaluation rules. Inputs:
 * scene_getChainCondition()/Next() of seq_activePattern, boundary flags.
 * Outputs: seq_chainStopped or the pending-change fields + fire flag (set
 * last). Context: TIM3 ISR. Affiliates: menu_serviceDeferredSceneChange().
 */
static void seq_chainEvaluate(uint8_t at_bar, uint8_t at_rep)
{
	uint8_t scene = seq_activePattern;
	uint8_t w1 = seq_chainConditionWeight(
	    scene_getChainCondition(scene, 0u), at_bar, at_rep);
	uint8_t w2 = seq_chainConditionWeight(
	    scene_getChainCondition(scene, 1u), at_bar, at_rep);
	uint8_t which = 0u;
	uint8_t next;
	uint8_t target;

	if (w1 == 0u && w2 == 0u)
		return;
	if (w1 >= 100u) {
		which = 1u;
	} else if (w1 != 0u && w2 != 0u && w2 < 100u &&
	           (uint16_t)w1 + w2 > 100u) {
		uint16_t r = (uint16_t)(((uint16_t)GetRngValue() & 0x7FFFu) %
		                        (uint16_t)(w1 + w2));
		which = (r < w1) ? 1u : 2u;
	} else {
		uint16_t r = (uint16_t)(((uint16_t)GetRngValue() & 0x7FFFu) % 100u);
		uint16_t sum = (uint16_t)w1 + w2;

		if (sum > 100u)
			sum = 100u;
		if (r < w1)
			which = 1u;
		else if (r < sum)
			which = 2u;
	}
	if (which == 0u)
		return;
	next = scene_getChainNext(scene, (uint8_t)(which - 1u));
	if (next == SCENE_CHAIN_NEXT_STOP) {
		seq_chainStopped = 1u;
		/* S081 P2: frozen tracks reach no B; end their ramps. */
		seq_rampBreakAll();
		return;
	}
	target = seq_chainResolveNext(next, scene);
	if (target == 0xFFu)
		return;
	seq_pendingSceneIndex = target;
	seq_pendingSceneDiv = 0u;
	seq_pendingSceneChange = 1u;
	seq_pendingChainFire = 1u;
	/* S083: a chain fire is LXR-generated; its PC is sent. */
	seq_sceneChangeFromMidi = 0u;
	seq_sceneFireFlag = 1u;         /* published last */
}

/*
 * Per-tick chain boundary service (S080 G7; S082 P3 D50, D52).
 *
 * What: on each master tick after the first, detects bar and repetition
 * boundaries and advances the counters; evaluates the chain unless a direct,
 * queued or chain change is already pending (a manual or queued switch
 * cancels conditional changes, plan §4.5), or a background Bank load runs.
 * Why: counters keep running during a load (D50); evaluation is suspended
 * because a chain target (`sNN`, `nxt`, `prv`, random range) could resolve
 * differently depending on which children have loaded (D52). After the load
 * the next boundary evaluates with the counts as they stand.
 * Inputs: seq_elapsedPpqTicks, pending flags,
 * preset_backgroundBankLoadRunning() (one-byte read, ISR-safe).
 * Outputs: counters, seq_chainEvaluate(). Context: TIM3 ISR via
 * seq_processSchedulerTick().
 */
static void seq_chainServiceTick(void)
{
	uint8_t at_bar;
	uint8_t at_rep;

	if (seq_elapsedPpqTicks == 0u)
		return;
	at_bar = (uint8_t)((seq_elapsedPpqTicks % SEQ_CHAIN_BAR_TICKS) == 0u);
	at_rep = seq_chainAtRepetitionEnd();
	if (!at_bar && !at_rep)
		return;
	if (at_bar && seq_chainBarCount < 255u)
		seq_chainBarCount++;
	if (at_rep && seq_chainRepCount < 255u)
		seq_chainRepCount++;
	if (seq_pendingSceneChange || seq_pendingSceneQueue || seq_sceneFireFlag)
		return;
	if (preset_backgroundBankLoadRunning())
		return;
	seq_chainEvaluate(at_bar, at_rep);
}

/*
 * Chain repeat of the playing Scene (S080 G7, P2 D13).
 *
 * What: resets the chain counters and `stp` freeze, retriggers once-mode
 * tracks and the per-track condition counters, and realigns every track to the
 * master clock (keeping loop anchors), or restarts tracks and master clock
 * when `restart` (pcr) is set. Why: a chain fire to the playing Scene (slf, or
 * a resolved target equal to it) is a repeat - plan §4.5 "3 bars, stop 1 bar,
 * then repeat" - and must not re-run the bounded DSP Scene apply. Inputs:
 * restart. Outputs: sequencer state. Context: foreground. Accessors:
 * menu_serviceDeferredSceneChange(). Affiliates: seq_resetToPatternStart(),
 * seq_loopRefreshSceneSnapshots().
 */
void seq_chainRepeatSelf(uint8_t restart)
{
	uint32_t primask;

	seq_chainBarCount = 0u;
	seq_chainRepCount = 0u;
	seq_chainStopped = 0u;
	if (restart) {
		seq_resetToPatternStart();
		return;
	}
	primask = __get_PRIMASK();
	__disable_irq();
	memset(seq_trackPlayState, 0, sizeof(seq_trackPlayState));
	memset(seq_trackRepeatCount, 0, sizeof(seq_trackRepeatCount));
	memset(seq_trackRndStepCount, 0, sizeof(seq_trackRndStepCount));
	memset(seq_trackLastConditionFired, 0,
	       sizeof(seq_trackLastConditionFired));
	seq_realignActivePatternToMasterClock();
	seq_loopRefreshSceneSnapshots();
	__set_PRIMASK(primask);
}

/*
 * Fire one track's deferred trigger with its automation (S081 §5.8).
 *
 * What: queues the deferred step's automation entries
 * (seq_queueStepAutomations(track, seq_trackShuffleStep[track])), then
 * fires the trigger: for a roll step it re-reads the step's specials and
 * calls seq_rollBegin() (first hit with reset = 1); otherwise
 * seq_triggerVoice(track, vel, note, 1). Clears the pending state. Why:
 * one routine serves both the expiry path and every early flush, so a
 * deferred step always keeps its automation and its roll. Queueing in TIM3
 * right beside the trigger keeps the foreground order
 * voiceControl_processPending() (reset + trigger) ->
 * seq_drainPendingAutomation() (entries), so the step's own automation
 * lands after its reset. Context: TIM3 only. Inputs:
 * seq_trackShuffleStep/Roll/Vel/Note. Outputs: entries queued, trigger
 * enqueued, pending cleared. Affiliates: seq_processDeferredTriggers()
 * (expiry), seq_advanceTrackStep() (early flush), seq_rollBegin().
 * S081 P2: sets the ramp context's stepped flag, so ramps into this step end
 * when its value lands.
 */
static void seq_fireDeferred(uint8_t track)
{
	uint8_t step = seq_trackShuffleStep[track];
	uint8_t roll = seq_trackShuffleRoll[track];
	uint8_t muted;

	muted = (uint8_t)((seq_trackShuffleMuteMask >> track) & 1u);
	seq_trackShuffleMuteMask &= (uint8_t)~(1u << track);
	seq_trackShufflePending[track] = 0u;
	seq_queueStepAutomations(track, step);
	/* S081 P2: the deferred step's automation lands now; end the ramps
	 * into it (the context still describes this step unless the next step
	 * has already started, in which case that step's event ended them). */
	if ((uint8_t)(seq_rampCtx[track].a & 0x7Fu) == step)
		seq_rampCtx[track].a |= SEQ_RAMP_STEPPED;
	if (muted)
		return;                 /* P1B item 7: automation kept, no trigger */
	if (roll) {
		pat_step_specials_t sp = pat_readStepSpecials(
		    seq_perTrackPattern[track], track, step);
		seq_rollBegin(track, &sp);
	} else {
		seq_triggerVoice(track, seq_trackShuffleVel[track],
		                 seq_trackShuffleNote[track], 1);
	}
}

/*
 * Deferred trigger tick-down (S078 §3.2, S079 §7.4).
 *
 * What: each PPQ tick decrements the delay counter for any track with a
 * pending deferred trigger. When the counter reaches zero, the deferred
 * trigger fires with the velocity/note captured at the original step. Why:
 * the delay carries the combined shuffle and micro-timing offset for a step,
 * which must operate at PPQ tick resolution for smooth timing. The shuffle
 * part is (shuffle_value * 24) / 256 (0..11 ticks at 96 PPQ); micro-timing
 * adds (step_interval x timing) / 100 on top. A pending deferral fires even if
 * its track stopped in the meantime (S078 §10.2): the step was already
 * reached; the delay only postpones execution. Inputs:
 * seq_trackShufflePending[], seq_trackShuffleDelay[]. Outputs:
 * seq_triggerVoice() when a delay expires. Affiliates: seq_advanceTrackStep()
 * sets up the deferred trigger.
 */
static void seq_processDeferredTriggers(void)
{
	uint8_t track;

	for (track = 0u; track < NUM_TRACKS; track++) {
		if (!seq_trackShufflePending[track])
			continue;
		if (seq_trackShuffleDelay[track] == 0u) {
			seq_fireDeferred(track);
		} else {
			seq_trackShuffleDelay[track]--;
		}
	}
}

/*
 * Tick-driven per-step roll retrigger engine (S079 §4; time window
 * S084 P1).
 *
 * What: once per 96-PPQ master tick, for each track with a step roll:
 *  - window end: when an open window (SEQ_ROLL_RUN) has no ticks left it
 *    closes. A fill < 100 roll requests the voice duck here (the fill
 *    boundary, S079 P2 fix 4) and ends; a fill-100 roll parks
 *    (SEQ_ROLL_CARRY only) until the track's next step decides on a carry;
 *  - parked: the accumulator keeps counting down to 0 and holds there, and
 *    no hit fires, so a carry resumes exactly on phase (a hit that fell due
 *    while parked fires on the carry's tick);
 *  - running: one window tick is used, and when the accumulator reaches 0
 *    a retrigger fires (reset = 0) at the rhythm-shaped velocity, the
 *    accumulator restarts at the rate's interval and the rhythm position
 *    advances.
 * Hits therefore sit at first-hit + k x interval for as long as windows
 * stay open, independent of the track's step scale.
 * Why: the S079 engine counted hits per step and froze its accumulator
 * between steps, so carried steps lost their boundary hit (S084 P0 F2).
 * Inputs: seq_rollActive, seq_rollWindow, seq_rollTickAccum,
 * seq_rollVelPos, seq_rollLastRate/Fill, seq_rollStepNote. Outputs:
 * seq_triggerVoice(), instrumentManager_requestGateDuck(). Context: TIM3,
 * from seq_processSchedulerTick() after the DDA, so a step that carries the
 * roll renews its window before the window-end test on the same tick.
 * Affiliates: seq_rollBegin(), seq_rollCarries(), seq_rollComputeVelocity(),
 * seq_rollPpq().
 */
static void seq_processRollTicks(void)
{
	uint8_t track;

	for (track = 0u; track < NUM_TRACKS; track++) {
		uint8_t st = seq_rollActive[track];
		uint8_t ppq;
		uint8_t vel;

		if (st == 0u)
			continue;
		if ((st & SEQ_ROLL_RUN) != 0u && seq_rollWindow[track] == 0u) {
			st = (uint8_t)(st & ~SEQ_ROLL_RUN);
			seq_rollActive[track] = st;
			if (seq_rollLastFill[track] < 100u)
				instrumentManager_requestGateDuck(track);
			if (st == 0u)
				continue;
		}
		if ((st & SEQ_ROLL_RUN) == 0u) {
			if (seq_rollTickAccum[track] > 0u)
				seq_rollTickAccum[track]--;       /* parked: phase only */
			continue;
		}
		seq_rollWindow[track]--;
		if (seq_rollTickAccum[track] > 0u) {
			seq_rollTickAccum[track]--;
			continue;
		}
		ppq = seq_rollPpq(seq_rollLastRate[track]);
		if (ppq == 0u) {
			seq_rollActive[track] = 0u;
			continue;
		}
		vel = seq_rollComputeVelocity(track);
		seq_triggerVoice(track, vel, seq_rollStepNote[track], 0);
		seq_rollTickAccum[track] = (uint8_t)(ppq - 1u);
		seq_rollVelPos[track]++;
	}
}

/*
 * Live overlay roll/flam tick (S084 P2).
 *
 * What: once per 96-PPQ master tick, for every armed live entry: count the
 * accumulator down; at 0, a flam fires its main hit (reset = 1, it is the
 * gesture's main trigger) and goes idle, and a roll fires a retrigger
 * (reset = 0) at the overlay rhythm's velocity for its position, then
 * restarts the accumulator at the rate's interval. Each hit updates the
 * mailbox (count or flam bit, µs stamp). Why: overlay hits on the master
 * clock, exactly the step-roll rate table and spacing, so a live roll and
 * its recording sound the same at any tempo and under external sync (S084
 * P0 F4). Inputs: seq_live* state, seq_liveRhythm. Outputs:
 * seq_triggerVoice(), the mailbox. Context: TIM3 only. Called from
 * seq_processSchedulerTick() before the running gate (internal sync, or
 * external sync while running) and from seq_tick() under external sync
 * while stopped, never both on one tick. Affiliates: seq_liveRollStart(),
 * seq_liveTakeHits(), buttonHandler_overlayDrain().
 */
static void seq_processLiveTicks(void)
{
	uint8_t track;

	for (track = 0u; track < NUM_TRACKS; track++) {
		uint8_t mode = seq_liveMode[track];
		uint8_t ppq;
		uint8_t vel;

		if (mode == SEQ_LIVE_IDLE)
			continue;
		if (seq_liveAccum[track] > 0u) {
			seq_liveAccum[track]--;
			continue;
		}
		if (mode == SEQ_LIVE_FLAM) {
			seq_triggerVoice(track, seq_liveVel[track],
			                 seq_liveNote[track], 1);
			seq_liveMode[track] = SEQ_LIVE_IDLE;
			seq_liveFlamMask |= (uint8_t)(1u << track);
		} else {
			ppq = seq_rollPpq(seq_liveRate[track]);
			if (ppq == 0u) {
				seq_liveMode[track] = SEQ_LIVE_IDLE;
				continue;
			}
			vel = seq_rollRhythmVelocity(seq_liveRhythm,
			                             seq_liveVel[track],
			                             seq_livePos[track]);
			if (seq_livePos[track] < 0xFFu)
				seq_livePos[track]++;
			else
				seq_livePos[track] = 16u;   /* 256 mod 60: same phase */
			seq_triggerVoice(track, vel, seq_liveNote[track], 0);
			seq_liveAccum[track] = (uint8_t)(ppq - 1u);
			if (seq_liveHitCount[track] < 0xFFu)
				seq_liveHitCount[track]++;
		}
		seq_liveHitUs[track] = timebase_tim2Now();
	}
}

/*
 * Live overlay engine API (S084 P2). Contracts in sequencer.h.
 *
 * What: the foreground arms, re-rates, stops and drains the per-track live
 * entries. Each call that writes more than one byte masks TIM3 (PRIMASK),
 * so seq_processLiveTicks() never sees a half-written entry. Start values:
 * a roll's first repeat is ppq - 1 ticks after the next tick boundary, so
 * the first gap from the press is between ppq - 1 and ppq ticks and every
 * later gap is exactly ppq (Q3: hits are spaced from the press). A rate
 * change keeps the time since the last hit (the S081 F-B rule "new interval
 * from the last hit time"). Why: buttonHandler owns the gesture, the
 * Sequencer owns the clock. Inputs/outputs: see each contract. Context:
 * foreground. Accessors: buttonHandler_overlayStart(),
 * buttonHandler_overlayReconfigure(), buttonHandler_overlayEndTrack(),
 * buttonHandler_overlayDrain(), buttonHandler_overlayTick(). Affiliates:
 * seq_processLiveTicks(), the live engine state block.
 */
void seq_liveRollStart(uint8_t track, uint8_t rate, uint8_t note,
                       uint8_t vel, uint8_t pos)
{
	uint8_t ppq;
	uint32_t primask;

	if (track >= NUM_TRACKS)
		return;
	ppq = seq_rollPpq(rate);
	primask = __get_PRIMASK();
	__disable_irq();
	seq_liveHitCount[track] = 0u;
	seq_liveFlamMask &= (uint8_t)~(1u << track);
	if (ppq == 0u) {
		seq_liveMode[track] = SEQ_LIVE_IDLE;
	} else {
		seq_liveMode[track] = SEQ_LIVE_ROLL;
		seq_liveRate[track] = rate;
		seq_liveAccum[track] = (uint8_t)(ppq - 1u);
		seq_livePos[track] = pos;
		seq_liveNote[track] = note;
		seq_liveVel[track] = vel;
	}
	__set_PRIMASK(primask);
}

void seq_liveRollSetRate(uint8_t track, uint8_t rate)
{
	uint8_t newPpq;
	uint8_t oldPpq;
	int16_t n;
	uint32_t primask;

	if (track >= NUM_TRACKS)
		return;
	newPpq = seq_rollPpq(rate);
	if (newPpq == 0u)
		return;
	primask = __get_PRIMASK();
	__disable_irq();
	if (seq_liveMode[track] == SEQ_LIVE_ROLL) {
		oldPpq = seq_rollPpq(seq_liveRate[track]);
		n = (int16_t)((int16_t)seq_liveAccum[track] +
		              (int16_t)newPpq - (int16_t)oldPpq);
		if (n < 0)
			n = 0;
		if (n > (int16_t)(newPpq - 1u))
			n = (int16_t)(newPpq - 1u);
		seq_liveAccum[track] = (uint8_t)n;
		seq_liveRate[track] = rate;
	}
	__set_PRIMASK(primask);
}

void seq_liveFlamArm(uint8_t track, uint8_t note, uint8_t vel,
                     uint8_t ticks)
{
	uint32_t primask;

	if (track >= NUM_TRACKS)
		return;
	primask = __get_PRIMASK();
	__disable_irq();
	seq_liveMode[track] = SEQ_LIVE_FLAM;
	seq_liveAccum[track] = (uint8_t)((ticks != 0u) ? (ticks - 1u) : 0u);
	seq_liveNote[track] = note;
	seq_liveVel[track] = vel;
	seq_liveHitCount[track] = 0u;
	seq_liveFlamMask &= (uint8_t)~(1u << track);
	__set_PRIMASK(primask);
}

void seq_liveStop(uint8_t track)
{
	if (track < NUM_TRACKS)
		seq_liveMode[track] = SEQ_LIVE_IDLE;
}

void seq_liveSetRhythm(uint8_t rhythm)
{
	seq_liveRhythm = rhythm;
}

uint8_t seq_liveTakeHits(uint8_t track, uint32_t *stampUs,
                         uint8_t *flamMain)
{
	uint8_t n;
	uint32_t primask;

	if (flamMain)
		*flamMain = 0u;
	if (track >= NUM_TRACKS)
		return 0u;
	primask = __get_PRIMASK();
	__disable_irq();
	n = seq_liveHitCount[track];
	seq_liveHitCount[track] = 0u;
	if (flamMain) {
		*flamMain = (uint8_t)((seq_liveFlamMask >> track) & 1u);
		seq_liveFlamMask &= (uint8_t)~(1u << track);
	}
	if (stampUs)
		*stampUs = seq_liveHitUs[track];
	__set_PRIMASK(primask);
	return n;
}

static void seq_processSchedulerTick(void)
{
	uint8_t track;
	uint8_t anyAdvanced = 0u;
	/*
	 * PPQ microsecond stamp (S081 §19.1 item 11, C-5b).
	 *
	 * What: records timebase_tim2Now() and the interval since the previous tick.
	 * Why: LiveRecord needs sub-tick timing for placement. Inputs: none. Outputs:
	 * seq_ppqStampUs/seq_ppqPeriodUs. Affiliates: timebase_tim2Now, liveRec
	 * placement.
	 */
	{
		uint32_t now = timebase_tim2Now();

		seq_ppqPeriodUs = now - seq_ppqStampUs;
		seq_ppqStampUs = now;
	}

	/*
	 * Live overlay rolls and flams first, running or stopped (S084 P2).
	 *
	 * What: services the PERF/MIDI overlay entries on this master tick,
	 * before the running gate, so a held roll repeats at the tempo while the
	 * transport is stopped (internal sync) and follows every external clock
	 * tick while running. Why: the overlay must be playable stopped, as
	 * before S084, but on the master clock. Order: it touches no Pattern,
	 * DDA or step-roll state; a stored step on the same track this tick
	 * triggers after it. Context: TIM3. Affiliates: seq_processLiveTicks(),
	 * seq_tick() (external sync, stopped).
	 */
	seq_processLiveTicks();

	if (!seq_running)
		return;

	if (seq_initialSchedulerTick) {
		seq_initialSchedulerTick = 0u;
		seq_masterStepClock = 0u;
		seq_masterStepCnt = 0u;
		if (seq_handleMasterBoundary()) {
			/* Pattern switching must not suppress the independent FX boundary. */
			seq_fxClockTick();
			midiTransport_checkMtc();
			return;
		}
	} else {
		seq_elapsedPpqTicks++;
		if ((seq_elapsedPpqTicks % SEQ_INTERNAL_TICKS_PER_DEFAULT_STEP) == 0u) {
			seq_masterStepClock =
				(uint16_t)(seq_elapsedPpqTicks / SEQ_INTERNAL_TICKS_PER_DEFAULT_STEP);
			seq_masterStepCnt = (uint8_t)seq_masterStepClock;
			if (seq_handleMasterBoundary()) {
				/* Pattern switching must not suppress the independent FX boundary. */
				seq_fxClockTick();
				midiTransport_checkMtc();
				return;
			}
		}
	}

	/*
	 * S080 G7: scene chain boundaries (bar / repetition) are evaluated on the
	 * master clock before any track moves (Step 11).
	 */
	seq_chainServiceTick();

	/* Shuffle deferral tick-down runs before this tick's DDA advance. */
	seq_processDeferredTriggers();

	/*
	 * S080 G11: a chain `stp` freezes every track in place (transport keeps
	 * running, deferred triggers and rolls finish). Neither the loop wrap nor
	 * the DDA advance runs while frozen.
	 */
	if (!seq_chainStopped) {
		/*
		 * S080 P2 D3: looper wrap BEFORE the DDA advance, so a wrap at tick T
		 * restores the pre-advance snapshot and this tick's advance replays
		 * the anchor tick's step (P1 wrapped after the advance and replayed
		 * the step after the one that was sounding).
		 */
		seq_loopProcessTick();

		/*
		 * Per-track DDA step advance (S078 §2.4.1, replacing the global divisor).
		 *
		 * What: each PPQ tick adds 1.0 (256 in Q8.8) to every track's accumulator.
		 * When accumulator >= interval, a step advance fires and the interval is
		 * subtracted. The fractional remainder carries forward for drift-free
		 * fractional timing. Why: allows each track to run at its own rate from the
		 * 128-position log curve. The old global modulo test
		 * (seq_elapsedPpqTicks % SEQ_INTERNAL_TICKS_PER_DEFAULT_STEP) is removed
		 * from track advance; seq_masterStepClock still uses it for bar display,
		 * boundaries, trigger clock, and MIDI beat. Inputs: seq_trackAccumulator
		 * [track], track_scale from the track's played Scene region,
		 * stepScale_ticksQ8(). Outputs: seq_advanceTrackStep() when the threshold
		 * is met; anyAdvanced set for LED chase update. The while loop handles the
		 * minimum-interval case (CC 0 = 1536 > 256, so at most one iteration per
		 * tick). Affiliates: seq_advanceTrackStep(), seq_processShuffleDelays().
		 */
		if (seq_initialSchedulerTick == 0u) {
			for (track = 0u; track < NUM_TRACKS; track++) {
				/*
				 * Effective (morphed) step-scale CC (S078 §5.4): the cached
				 * value follows a Morph sweep or step-automation overlay without
				 * touching the retained Pattern region.
				 */
				uint8_t cc = seq_effectiveTrackScale[track];
				uint16_t interval = stepScale_ticksQ8(cc);
	
				seq_trackAccumulator[track] += 256u;
				while (seq_trackAccumulator[track] >= interval) {
					seq_trackAccumulator[track] -= interval;
					seq_advanceTrackStep(track);
					anyAdvanced = 1u;
				}
			}
		}
	}

    if (anyAdvanced) {
        seq_ledState.chaseStep = seq_stepIndex[menu_getActiveVoice()];
        seq_ledState.dirty |= SEQ_LED_DIRTY_CHASE;
    }

    /*
     * S079 §4 / S084 P1: step roll retriggers run after the DDA advance, so
     * a step that carries a roll renews its window before the window-end
     * test on the same tick.
     */
    seq_processRollTicks();

    /* FX sequencer follows the same master PPQ clock, independent of Pattern. */
    seq_fxClockTick();
    midiTransport_checkMtc();
}
//------------------------------------------------------------------------------
uint8_t seq_getExtSync()
{
	if (seq_isSyncExternal == SEQ_EXT_SYNC_AUTO) {
		if (seq_autoSyncActiveSource == SEQ_EXT_SYNC_OFF)
			return 0;
		if (timebase_tim2Delta(timebase_tim2Now(), seq_autoSyncLastUs) >
				SEQ_AUTO_SYNC_HOLD_US) {
			/* AUTO falls back to the internal clock when the selected external
			** source disappears. Kick the scheduler out of any long external-sync
			** wait so free-run resumes promptly. */
			seq_autoSyncActiveSource = SEQ_EXT_SYNC_OFF;
			seq_deltaT = 0;
			seq_lastTick = systick_ticks;
			return 0;
		}
		return 1;
	}
	return (uint8_t)(seq_isSyncExternal != SEQ_EXT_SYNC_OFF);
}
//------------------------------------------------------------------------------
void seq_setExtSync(uint8_t isExt)
{
	seq_setExtSyncSource(isExt ? SEQ_EXT_SYNC_DIN : SEQ_EXT_SYNC_OFF);
}
//------------------------------------------------------------------------------
void seq_setExtSyncSource(uint8_t source)
{
	if (source > SEQ_EXT_SYNC_AUTO)
		source = SEQ_EXT_SYNC_OFF;
	seq_isSyncExternal = source;
	seq_autoSyncActiveSource = SEQ_EXT_SYNC_OFF;
	seq_deltaT = 0;
}
//------------------------------------------------------------------------------
uint8_t seq_getExtSyncSource(void)
{
	return seq_isSyncExternal;
}
//------------------------------------------------------------------------------
void seq_noteExtSyncActivity(uint8_t source, uint32_t timestampUs)
{
	if (source == SEQ_EXT_SYNC_OFF || source > SEQ_EXT_SYNC_PULSE)
		return;

	if (seq_isSyncExternal == source) {
		seq_autoSyncActiveSource = source;
		seq_autoSyncLastUs = timestampUs;
		return;
	}

	if (seq_isSyncExternal == SEQ_EXT_SYNC_AUTO) {
		/* AUTO priority, low to high: internal, USB, DIN, jack pulse. A lower
		** priority source cannot steal the clock while a higher source is fresh. */
		if (seq_autoSyncActiveSource != SEQ_EXT_SYNC_OFF &&
				seq_autoSyncActiveSource > source &&
				timebase_tim2Delta(timestampUs, seq_autoSyncLastUs) <=
				SEQ_AUTO_SYNC_HOLD_US)
			return;
		seq_autoSyncActiveSource = source;
		seq_autoSyncLastUs = timestampUs;
	}
}
//------------------------------------------------------------------------------
void seq_setDeltaT(float delta)
{
	seq_deltaT = delta;
}
//------------------------------------------------------------------------------

/*
 * Run one external clock period's scheduler ticks, interpolated (S084 P0 A1).
 *
 * What: first processes every tick still owed from the previous period
 * (catch-up when the clock ran faster than the measured tempo), then the
 * tick that belongs on this clock edge, and leaves the remaining ticks - 1
 * owed for seq_tick() to process one per tempo-timer period. The timer is
 * re-anchored on this edge (seq_lastTick, seq_calcDeltaT()), so the
 * interpolated ticks are spaced by the measured external tempo from here.
 * Each period therefore runs exactly `ticks` scheduler ticks, as before;
 * only their timing within the period changes. Why: back-to-back ticks put
 * every hit, roll retrigger, shuffle and micro-timing inside one clock
 * period on the same instant (S084 P0 assessment A1). Inputs: ticks (4 per
 * MIDI clock; stepSize x 3 per pulse, at most 96). Outputs:
 * seq_processSchedulerTick() calls, seq_extTicksOwed, the tempo timer
 * anchor. Context: TIM3 (clockSync via midiTransport_processRealtime(),
 * triggerJacks_tick()); runs only while the transport runs. Accessors:
 * seq_resetDeltaAndTick(), seq_triggerNextMasterStep(). Affiliates:
 * seq_tick() (interpolation), seq_setRunningEx()/seq_continueRunning()/
 * seq_resetToPatternStart() (clear the owed ticks).
 */
static void seq_extClockTicks(uint16_t ticks)
{
	while (seq_extTicksOwed != 0u) {
		seq_extTicksOwed--;
		seq_processSchedulerTick();
	}
	if (ticks != 0u) {
		seq_processSchedulerTick();
		seq_extTicksOwed = (uint8_t)((ticks > 256u) ? 255u : (ticks - 1u));
	}
	seq_lastTick = systick_ticks;
	seq_calcDeltaT(seq_tempo);
}

void seq_triggerNextMasterStep(uint8_t stepSize)
{
	/*
	 * Advance scheduler time from a trigger-jack pulse.
	 *
	 * Trigger input still reports spacing in the legacy native 32 PPQ units
	 * (`PRE_4_PPQ == 8`, etc.). The corrected sequencer scheduler runs at 96
	 * PPQ, so one native unit equals three internal ticks. Processing those
	 * ticks through the same scheduler path keeps track-scale timing, every-step
	 * visitation, and master-clock loop correction identical between internal,
	 * MIDI, and pulse sync. S084 P0 A1: the ticks are spread over the pulse
	 * period (seq_extClockTicks()), not run back to back.
	 */
	seq_extClockTicks((uint16_t)((uint16_t)stepSize * 3u));
}
//------------------------------------------------------------------------------
void seq_resetDeltaAndTick()
{
	/*
	 * MIDI clock is 24 PPQ while the internal scheduler is 96 PPQ, so every
	 * external MIDI clock pulse advances four internal scheduler ticks. This
	 * replaces the old "4 steps every 3 clocks" bridge, which belonged to the
	 * previous 32-steps-per-beat grid. S084 P0 A1: the first tick runs on the
	 * clock, the other three are interpolated by seq_tick().
	 */
	seq_extClockTicks(SEQ_INTERNAL_TICKS_PER_MIDI_CLOCK);
}
//------------------------------------------------------------------------------
void seq_resetToPatternStart(void)
{
	/* External reset should reposition the sequence without toggling transport
	** state or sending MIDI stop/start. The next clock pulse will play the
	** pattern start according to each track's rotation. */
	seq_barCounter = 0;
	seq_delayedSyncStepFlag = 0;
	seq_extTicksOwed = 0u;          /* S084 P0 A1: no tick of the old period */
	seq_setStepIndexToStart();
	seq_resetStepScheduler();
	seq_deltaT = 0;
	seq_lastTick = systick_ticks;
}
//------------------------------------------------------------------------------
/** call periodically to check if the next step has to be processed */
void seq_tick()
{
	if(systick_ticks-seq_lastTick >= seq_deltaT)
	{

		float rest = systick_ticks-seq_lastTick - seq_deltaT;
		seq_lastTick = systick_ticks;
		seq_calcDeltaT(seq_tempo);
		seq_deltaT = seq_deltaT - rest;

		/*
		 * External sync, transport stopped (S084 P2, Q9): no clock reaches
		 * the scheduler (clockSync and the pulse input tick it only while
		 * running), so live overlay rolls run from this tempo timer.
		 * seq_tempo follows the incoming clock (sync_calcBpm(),
		 * trigger_applyPulseTempo()), so the rate matches the external
		 * tempo. With no clock at all they keep the last measured tempo.
		 *
		 * External sync, transport running (S084 P0 A1): the clock edge ran
		 * the period's first tick (seq_extClockTicks()); each tempo-timer
		 * period here runs one of the ticks still owed, so the period's
		 * ticks are spread evenly instead of bunched on the edge. Internal
		 * sync never owes ticks; a switch to internal drops any left over.
		 */
		if (!seq_getExtSync()) {
			seq_extTicksOwed = 0u;
			seq_processSchedulerTick();
		} else if (!seq_running) {
			seq_processLiveTicks();
		} else if (seq_extTicksOwed != 0u) {
			seq_extTicksOwed--;
			seq_processSchedulerTick();
		}

		if(!seq_getExtSync()) //only send internal MIDI clock to output when external sync is off
		{
			if (seq_internalMidiClockPhase == 0u)
			{
				seq_sendRealtime(MIDI_CLOCK);
			}
			seq_internalMidiClockPhase++;
			if (seq_internalMidiClockPhase >= SEQ_INTERNAL_TICKS_PER_MIDI_CLOCK)
				seq_internalMidiClockPhase = 0u;
		}
	}


}
//------------------------------------------------------------------------------
void seq_setQuantisation(uint8_t value)
{
	/*
	 * Clamp quantisation to the valid range 0..QUANT_TRACK (S081 §5.6).
	 *
	 * What: rejects values > 5. Why: the enum now has 6 entries (0..5). An
	 * out-of-range value from a corrupt file or MIDI could index past the
	 * quantisationNames array. Inputs: value 0..5. Outputs: seq_quantisation set.
	 * Affiliates: filesystem.c quantisation parse.
	 */
	if (value > QUANT_TRACK)
		value = 0u;
	seq_quantisation = value;
}
uint8_t seq_isRunning() {
	return seq_running;
}

/*
 * Start or stop the sequencer transport.
 *
 * What: the single entry point for transport state transitions. Both paths
 * reset the fixed-grid scheduler and converge on seq_setStepIndexToStart(),
 * which restores transient automation before clearing its dirty tracking and
 * rewinds every track cursor to one position before step zero.
 *
 * Why the assignment of seq_running is branch-specific: TIM3 can preempt this
 * code at any instruction boundary. The stop path publishes seq_running = 0
 * before its teardown, so the scheduler returns immediately. The start path
 * leaves seq_running at 0 until scheduler state, cursors, and automation
 * restore are complete; the first enabled tick therefore cannot process a
 * premature step zero and then be reset by the foreground path.
 *
 * Inputs: isRunning — nonzero starts transport, zero stops it. sendRealtime
 * (S083) = 0 when the change came from received MIDI transport (C-11: MRT
 * forwarding already carries the received message; the LXR must not send a
 * second copy); 1 for the panel, MTC and menu paths.
 * Outputs: stop halts transport, sends MIDI_STOP when sendRealtime is set,
 * silences notes/triggers, and restores automation; start resets scheduler
 * state, sends MIDI_START when sendRealtime is set, and enables transport
 * only after the common grid reset is complete. A stop also records
 * seq_resumeTick (the next unplayed tick) for a later Continue (R-C); a
 * start clears it.
 * Common callers: front-panel transport, MIDI realtime, clockSync, and the
 * Menu audio-suspend path. Affiliates: seq_processSchedulerTick() consumes
 * seq_running; seq_resetStepScheduler() prepares the first tick;
 * seq_setStepIndexToStart() performs the restore-before-clear operation;
 * voiceControl_noteOff(), trigger_reset()/trigger_allOff(), and
 * midiTransport_checkMtc() finish stop-side hardware/MTC cleanup.
 *
 * The stop-branch seq_clearAutomationDirty() call intentionally lives only in
 * seq_setStepIndexToStart(). A separate early clear would erase the dirty bits
 * before the restore and recreate the missed-automation defect.
 */
void seq_setRunningEx(uint8_t isRunning, uint8_t sendRealtime)
{
	/* S084 P0 A1: a transport change drops external ticks still owed, so a
	 * restart never replays the old clock period's remainder. */
	seq_extTicksOwed = 0u;
	/* S081 §19.1 item 6: a transport change is a take boundary. */
	seq_playbackEpoch++;
	/* S081 P2: a playhead break ends automation ramps (no B will follow). */
	seq_rampBreakAll();
	if (!isRunning)
	{
		/*
		 * S083 R-C: remember where playback continues (assessment F-1).
		 * seq_elapsedPpqTicks is the tick already processed, so the resume
		 * position is the next one; storing the processed tick made Continue
		 * replay it (a step on that tick fired twice). Before the first
		 * scheduler tick nothing was played: resume at 0.
		 */
		if (seq_running)
			seq_resumeTick = seq_initialSchedulerTick
			                 ? 0u : (seq_elapsedPpqTicks + 1u);
		seq_running = 0u;

		seq_barCounter = 0;
		seq_resetStepScheduler();
		seq_deltaT = 0;
		/*
		 * S080 B5 / P2: the looper is runtime-only; a transport stop ends it
		 * (before seq_setStepIndexToStart(), so no rebase happens). The
		 * buttonHandler hold state is reconciled on the next looper button
		 * edge (seq_loopActive reads 0).
		 */
		seq_loopActive = 0u;
		seq_loopTrackMask = 0u;
		/*
		 * S080 G12 / P2: a transport stop cancels pending, queued and
		 * chain-fired scene changes and clears all chain state (the only
		 * thing that stops a queued change, plan §5.3).
		 */
		seq_pendingSceneChange = 0u;
		seq_pendingSceneQueue = 0u;
		seq_pendingChainFire = 0u;
		seq_sceneFireFlag = 0u;
		seq_chainBarCount = 0u;
		seq_chainRepCount = 0u;
		seq_chainStopped = 0u;
		seq_previousScene = 0xFFu;
		if (sendRealtime)
			seq_sendRealtime(MIDI_STOP);

		voiceControl_noteOff(0xFF);
		/* S083: Stop ends every transmitted track note as well. */
		midiOut_trackNoteOff(0xFFu);

		trigger_reset(0);
		trigger_allOff();

		midiTransport_checkMtc();

		/*
		 * Queue the foreground chase drain after publishing seq_running = 0.
		 * The last playback position may still own a LED_LAYER_CHASE; the
		 * drain-side transport guard removes that inversion on its next pass.
		 */
		seq_ledState.dirty |= SEQ_LED_DIRTY_CHASE;
	} else {
		seq_resetStepScheduler();
		/* S083 T1: Start restarts from the Pattern start. */
		seq_resumeTick = 0u;
		if (sendRealtime)
			seq_sendRealtime(MIDI_START);
		trigger_reset(1);
		/* S080 G7: counts start at zero with the transport. */
		seq_chainBarCount = 0u;
		seq_chainRepCount = 0u;
		seq_chainStopped = 0u;
	}

	seq_setStepIndexToStart();

	if (isRunning)
		seq_running = 1u;
}

/* Front panel, MTC, menu: transport change with LXR transport output. */
void seq_setRunning(uint8_t isRunning)
{
	seq_setRunningEx(isRunning, 1u);
}

/*
 * Resume playback at seq_resumeTick (S083 T1, MIDI Continue).
 *
 * What: the start sequence of seq_setRunningEx() (scheduler reset, trigger
 * reset, chain counts, automation restore through
 * seq_setStepIndexToStart()), then, for a non-zero resume tick, places the
 * master clock one tick before it and realigns every track and the FX
 * sequence to it through the master-clock anchors (the same mapping as the
 * per-track realign, PATTERN_DYNAMIC_STACK.md 6.5), so the next scheduler
 * tick plays position `resume` (the first tick not yet played: a stop
 * stores processed + 1, an SPP the located position), so no tick is
 * processed twice. A zero resume tick is a plain start.
 * Why: Continue must resume, not restart (plan 3.8). Inputs:
 * sendRealtime (0 when the Continue was received, C-11), seq_resumeTick.
 * Outputs: transport running at the resume position; seq_resumeTick
 * cleared. Context: TIM3 (sync_midiContinue()); the realign helpers are
 * context-safe (pat_sceneRegion() SRAM reads, FX realign under PRIMASK).
 * Affiliates: seq_realignActivePatternToMasterClock(), clockSync.c.
 */
void seq_continueRunning(uint8_t sendRealtime)
{
	uint32_t resume = seq_resumeTick;

	if (seq_running)
		return;
	seq_extTicksOwed = 0u;          /* S084 P0 A1: see seq_setRunningEx() */
	seq_playbackEpoch++;
	seq_rampBreakAll();
	seq_resetStepScheduler();
	if (sendRealtime)
		seq_sendRealtime(MIDI_CONTINUE);
	trigger_reset(1);
	seq_chainBarCount = 0u;
	seq_chainRepCount = 0u;
	seq_chainStopped = 0u;
	seq_setStepIndexToStart();
	if (resume != 0u) {
		seq_elapsedPpqTicks = resume - 1u;
		seq_masterStepClock = (uint16_t)(seq_elapsedPpqTicks /
		                                 SEQ_INTERNAL_TICKS_PER_DEFAULT_STEP);
		seq_masterStepCnt = (uint8_t)seq_masterStepClock;
		seq_initialSchedulerTick = 0u;
		seq_realignActivePatternToMasterClock();
	}
	seq_resumeTick = 0u;
	seq_running = 1u;
}

/*
 * Song Position Pointer while stopped (S083 T1).
 * What: stores `tick` (SPP x 24) as the resume position; ignored while the
 * transport runs. Accessor: midiTransport_handleSystemCommon().
 */
void seq_setResumeTick(uint32_t tick)
{
	if (!seq_running)
		seq_resumeTick = tick;
}
//------------------------------------------------------------------------------
void seq_setMute(uint8_t trackNr, uint8_t isMuted)
{
	if(trackNr==7)
	{
		//unmute all
		seq_mutedTracks = 0;
	} else {
		//mute/unmute tracks
		if(isMuted) {
			//mute track
			seq_mutedTracks |= (1<<trackNr);
			/*
			 * End the muted track's sound and MIDI note (S083 fix).
			 *
			 * What: stops the track's voice and sends its MIDI note-off.
			 * Why: the old call passed midi_MidiChannels[trackNr] (a zero-
			 * based channel) as a voice index, so it stopped the wrong voice
			 * or none. Affiliates: voiceControl_noteOff(),
			 * midiOut_trackNoteOff().
			 */
			voiceControl_noteOff(trackNr);
			midiOut_trackNoteOff(trackNr);
		} else {
			//unmute track
			seq_mutedTracks &= ~(1<<trackNr);
		}
	}
};
//------------------------------------------------------------------------------
uint8_t seq_isTrackMuted(uint8_t trackNr)
{
	if(seq_mutedTracks & (1<<trackNr) )
	{
		return 1;
	}
	return 0;
}
/*
 * Removed: seq_setRoll / seq_setRollRate (S081 §3.2, A3-5c/d).
 *
 * What: the PAR_ROLL-driven performance roll setters, superseded by the overlay
 * trigger system (S080) and step-roll recording (S081).
 */
//------------------------------------------------------------------------
/*
 * Removed: seq_quantize and seq_recordTrigger (S081 §3.2, B-7a/b).
 *
 * What: seq_quantize() used the retired 128-sub-step bar and seq_recordTrigger()
 * folded every recording into bar 1 as a trigger-only bit. Both are replaced by
 * LiveRecord (liveRec_triggerIn) and the master-clock quantiser
 * seq_quantiseTick() (Phase C). Affiliates: liveRec_triggerIn, seq_quantiseTick.
 */


/*
 * Publish a recorded step for STEP-row LED feedback (S081 §19.8, B-8c).
 *
 * What: sets seq_ledState.recordMainStep to the absolute step (0..127) and marks
 * SEQ_LED_DIRTY_REC_MAIN when the recorded track is the active voice and the
 * recorded Scene is the viewed Pattern. led_updateRecordedMainStep() then draws
 * STEP (step % 16) only when step / 16 == menu_currentBar. Why: the Sequencer owns
 * seq_ledState; the old writer passed step % 16, so a bar-3 recording lit a bar-1
 * LED. Inputs: track, scene, absolute step. Outputs: LED dirty state. Clients:
 * LiveRecord after a successful trigger write.
 */
void seq_markRecordedStep(uint8_t track, uint8_t scene, uint8_t step)
{
	if (track != menu_getActiveVoice() || scene != menu_getViewedPattern())
		return;
	seq_ledState.recordMainStep = step;
	seq_ledState.dirty |= SEQ_LED_DIRTY_REC_MAIN;
}

/* LXR-generated realtime (Clock, Start, Stop; S083: TXF in MidiOut). */
static void seq_sendRealtime(const uint8_t status)
{
	midiOut_realtime(status);
}

/*
 * Program change for a whole-Scene change (S080; S083 Phase A).
 *
 * What: sends the Scene number as a PC on G under TXF P. Scene 17 has no
 * PC number (S082 C9/D8). G off sends nothing. Phase D adds the MIDI-origin
 * suppression (no echo, D-8). Inputs: Scene index. Affiliates:
 * midiOut_programChange(), midiIn_globalChannel().
 */
static void seq_sendProgChg(const uint8_t ptn)
{
	/* S083 Q9: a MIDI-originated change is not echoed. */
	if (seq_sceneChangeFromMidi) {
		seq_sceneChangeFromMidi = 0u;
		return;
	}
	if (ptn >= SCENE_COUNT)
		return;
	midiOut_programChange(midiIn_globalChannel(), ptn);
}

static void seq_setStepIndexToStart()
{
	/*
	 * Reset every runtime cursor to the fixed-grid start position.
	 *
	 * Input: implicit active Scene/transport state. Output: each track is one
	 * position before step zero, so the immediate scheduler boundary plays step
	 * zero. Before the dirty bitmap is cleared, all transient automation overlays
	 * are restored to each slot's morph_interpolation[] base. There is no
	 * PatternData rotation, length, or event-count affiliate.
	 *
	 * This is the common reset path for transport start/stop, Pattern-boundary
	 * changes, and external MIDI/sync reset. The restore must precede
	 * seq_clearAutomationDirty(); otherwise step zero can inherit a runtime value
	 * from the previous pass after its tracking bit has been discarded.
	 * Affiliates: seq_restoreAllSceneAutomation() restores Scene-target
	 * overlays, seq_restoreAllAutomation() restores the six slot bitmaps, and
	 * seq_clearAutomationDirty() then drops both tracking sets; seq_init()
	 * deliberately calls the clear helper directly at boot because no runtime
	 * overlay exists.
	 */
	uint8_t i;

	/* Reset FX position/held Morph before the next foreground boundary. */
	seq_fxPublishReset();
	seq_restoreAllSceneAutomation();
	seq_restoreAllAutomation();
	seq_clearAutomationDirty();
	/* R12: transport start/stop/reset clears the Instrument menu override
	 * (S081 §5.1). See seq_menuOverrideSet() for the set site. */
	memset(seq_menuOverride, 0, sizeof(seq_menuOverride));
	/* S081 C-6/C-10: bump the playback epoch and expire echo marks. */
	seq_playbackEpoch++;
	/* S081 P2: a playhead break ends automation ramps (no B will follow). */
	seq_rampBreakAll();
	seq_liveRecResetMarks();
	/*
	 * S078 §5.5: recompute the effective track cache after the restore cleared
	 * every step-automation overlay, so the DDA seed below starts from the
	 * Normal/Morph base rather than a stale automated value.
	 */
	seq_refreshTrackEffectiveParams();
	for(i=0;i<NUM_TRACKS;i++) {
		/* Seed from the effective (morphed) scale cache, S078 §5.4. */
		uint8_t cc = seq_effectiveTrackScale[i];
		uint16_t interval = stepScale_ticksQ8(cc);

		seq_lastMasterStep[i] = 0u;
		seq_stepIndex[i] = -1;
		/*
		 * Seed the DDA so the first scheduler tick fires step zero (S078).
		 *
		 * What: the accumulator starts one Q8.8 tick below the track's
		 * interval, so the very first "+= 256" crosses the threshold exactly
		 * and the step-0 trigger plays on the initial scheduler tick with a
		 * zero remainder. Why: a zero seed would delay the first step by one
		 * full step interval. Affiliates: seq_processSchedulerTick() DDA loop.
		 */
		seq_trackAccumulator[i] = (uint16_t)(interval - 256u);
		seq_trackShuffleDelay[i] = 0u;
		seq_trackShufflePending[i] = 0u;
		seq_trackPlayState[i] = 0u;
		/* S079: reset per-track condition and roll runtime state. */
		seq_trackRepeatCount[i] = 0u;
		seq_trackRndStepCount[i] = 0u;
		seq_trackLastConditionFired[i] = 0u;
		seq_rollClearTrack(i);
	}
	/*
	 * Seed the FX DDA the same way so its step zero plays on the first tick,
	 * and restart its step counter.
	 */
	{
		const effect_record_t *fx = scene_effectConst(scene_getActiveIndex());

		seq_fxAccumulator =
		    (uint16_t)(stepScale_ticksQ8(fx ? fx->seq_step_scale
		                                    : STEP_SCALE_DEFAULT) - 256u);
		seq_fxStepCounter = 0u;
	}

	/*
	 * S080 P2 Defect 14: an engaged loop survives a cursor reset.
	 *
	 * What: rebase every looped track to the seeded start (anchor 0). Why: a
	 * resetting scene change, an external reset or a boundary pattern commit
	 * restarts the master clock; the loop start becomes the beginning of the
	 * Scene instead of the loop dying with stale buttonHandler state (P1
	 * cleared seq_loopActive here). Transport stop clears seq_loopActive before
	 * calling this, so a stop still ends the loop. Affiliates:
	 * seq_loopRebaseToStart(), seq_setRunning().
	 */
	seq_loopRebaseToStart();

}
