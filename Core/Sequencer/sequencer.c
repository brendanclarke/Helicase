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
#include "Uart.h"
#include "MidiMessages.h"
#include "MidiVoiceControl.h"
#include "CymbalVoice.h"
#include "sequencer.h"
#include <string.h>
#include "usb_manager.h"
#include "clockSync.h"
#include "MidiParser.h"
#include "MidiNoteNumbers.h"
#include "SomGenerator.h"
#include "triggerJacks.h"
#include "timebase.h"
#include "ledHandler.h"
#include "menu.h"
#include "SceneData.h"
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

/* Effect automation handshake: owner mask and reset latch are SRAM1 bytes. */
static volatile uint8_t seq_effectAutomationTracks = 0u;
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

void seq_setEffectAutomationTracks(uint8_t mask)
{
    /* Single-byte store; TIM3 reads it before queueing an FX marker. */
    seq_effectAutomationTracks = mask;
}
uint8_t seq_rollRate = 0x08;				//start with roll rate = 1/16
uint8_t seq_rollState = 0;					/**< each bit represents a voice. if bit is set, roll is active*/

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

uint8_t seq_eraseActive=0;					/**RECORD will be 1 if live erasing the active voice  */

uint8_t seq_quantisation = QUANT_16;

uint8_t seq_barCounter;						/**< counts the absolute position in bars since the seq was started */

static uint8_t seq_loadPendigFlag = 0;

// --AS Allow it to be configured whether it keeps track of bar position in the song for
// the purpose of pattern changes
uint8_t seq_resetBarOnPatternChange=0;

// --AS keep track of which midi notes are playing
static uint8_t midi_chan_notes[16];		    /**< what note is playing on each channel */
static uint16_t midi_notes_on=0;		    /**< which channels have a note currently playing */

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
/* Bit 11 marks a payload-free Effect step boundary; bits 0..9 keep step id. */
#define SEQ_PENDING_TYPE_FX_STEP_BIT    (1u << 11u)
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

static void seq_sendMidi(MidiMsg msg);
static void seq_sendRealtime(const uint8_t status);
static void seq_sendProgChg(const uint8_t ptn);
static void seq_processSchedulerTick(void);
static void seq_setStepIndexToStart();
static void seq_queueStepAutomations(uint8_t track, uint8_t step);

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

		seq_effectiveTrackLength[track] =
		    presetMorph_getTrackEffectiveLength(scene_index, track);
		seq_effectiveTrackScale[track] =
		    presetMorph_getTrackEffectiveScale(scene_index, track);
		seq_effectiveTrackShuffle[track] =
		    presetMorph_getTrackEffectiveShuffle(scene_index, track);
	}
}

//------------------------------------------------------------------------------
void seq_init()
{
	memset(seq_stepIndex,0,sizeof(seq_stepIndex));
	memset(seq_lastMasterStep,0,NUM_TRACKS);
	/* S078: clear every per-track DDA, shuffle, and play-mode byte at boot. */
	memset(seq_trackAccumulator, 0, sizeof(seq_trackAccumulator));
	memset(seq_trackShuffleDelay, 0, sizeof(seq_trackShuffleDelay));
	memset(seq_trackShufflePending, 0, sizeof(seq_trackShufflePending));
	memset(seq_trackShuffleVel, 0, sizeof(seq_trackShuffleVel));
	memset(seq_trackShuffleNote, 0, sizeof(seq_trackShuffleNote));
	memset(seq_trackPlayState, 0, sizeof(seq_trackPlayState));
	seq_fxAccumulator = 0u;
	seq_fxStepCounter = 0u;
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
void seq_setNextPattern(const uint8_t patNr)
{
	/*
	 * Deprecated pattern-chain entry point.
	 *
	 * Scene expansion removed standalone Pattern switching: a musical switch must
	 * select the Scene, its Pattern, and Scene-owned parameters together through
	 * menu_perfModeSceneButtonPressed()/seq_selectActivePattern(). Inputs from
	 * legacy MIDI program-change paths are therefore ignored instead of queuing a
	 * Pattern-only change that would desynchronize playback from the active Scene.
	 */
	(void)patNr;
}
//------------------------------------------------------------------------------
void seq_armActivePatternReload(void)
{
	seq_loadPendigFlag = 1;
}
//------------------------------------------------------------------------------
static void seq_sendMidi(MidiMsg msg)
{
	//send to usb midi
	usb_sendMidi(msg);

	//send to hardware midi out jack
	uart_sendMidi(msg);

}


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

//------------------------------------------------------------------------------
void seq_triggerVoice(uint8_t voiceNr, uint8_t vol, uint8_t note)
{
	uint8_t midiChan; // which midi channel to send a note on
	uint8_t midiNote; // which midi note to send

	if(voiceNr > 6) return;

	/*
	 * Trigger one fixed-grid event.
	 *
	 * Inputs: valid voice plus caller-selected velocity/note. Outputs: synth,
	 * trigger jack, and MIDI note-on are driven. seq_advanceTrackStep() resolves
	 * PatternData defaults/specials before calling this owner; this function
	 * retains responsibility for the track MIDI-note override and voice trigger.
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
	voiceControl_noteOn(voiceNr, note, vol);

	/*
	 * Per-track MIDI output (S077 P2 §6.5).
	 *
	 * What: MIDI output uses the track's played Scene's channel, not the global
	 * active Scene's. Why: each track sends MIDI on the channel configured in
	 * the Scene it is playing from. Inputs: seq_perTrackPattern[voiceNr].
	 * Output: MIDI note-on uses the correct Scene's channel. When no per-track
	 * override is set this equals seq_activePattern and behaviour is unchanged.
	 */
	midiChan = (uint8_t)(scene_getTrackMidiChannel(seq_perTrackPattern[voiceNr], voiceNr) - 1u);

	/*
	 * MIDI output note/channel are PatternData-owned track settings now. A note
	 * value of 0 preserves the old "use the triggered note" behavior; nonzero
	 * values override the outgoing MIDI note for this pattern track.
	 */
	midiNote = scene_getTrackMidiNote(seq_perTrackPattern[voiceNr], voiceNr);
	if(midiNote == 0)
		midiNote = note;

	//send the new note to midi/usb out
	seq_sendMidiNoteOn(midiChan, midiNote, vol);
}
void seq_previewVoice(uint8_t voiceNr)
{
	uint8_t midiChan;
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
	 * voice, and MIDI note-on follow the same channel/note ownership as normal
	 * playback. Confederates: PatternData supplies the optional track MIDI note
	 * override, MidiVoiceControl owns note-on/off, and triggerJacks owns trigger
	 * pulse state. Invalid voices and running transport are ignored.
	 */
	if (voiceNr > 6u || seq_running)
		return;

	/*
	 * Preview uses the track's played Scene for MIDI identity (S077 P2 §6.5).
	 *
	 * What: stopped-transport voice preview auditions through the MIDI
	 * channel/note of whichever Scene the track is playing from. Inputs:
	 * seq_perTrackPattern[voiceNr].
	 */
	note = scene_getTrackMidiNote(seq_perTrackPattern[voiceNr], voiceNr);
	if (note == 0u)
		note = MIDI_DEFAULT_TRIGGER_NOTE;

	if (voiceNr >= 5u) {
		trigger_triggerVoice(5, TRIGGER_OFF);
		trigger_triggerVoice(6, TRIGGER_OFF);
	} else {
		trigger_triggerVoice(voiceNr, TRIGGER_OFF);
	}

	voiceControl_noteOff(voiceNr);
	voiceControl_noteOn(voiceNr, note, ROLL_VOLUME);

	midiChan = (uint8_t)(scene_getTrackMidiChannel(seq_perTrackPattern[voiceNr], voiceNr) - 1u);
	seq_sendMidiNoteOn(midiChan, note, ROLL_VOLUME);
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
	/* S078 §5.4: the played Scenes changed, so rebuild the effective cache. */
	seq_refreshTrackEffectiveParams();
	seq_realignActivePatternToMasterClock();
	led_notifyPatternChanged(seq_activePattern);
	seq_sendProgChg(seq_activePattern);
	voiceControl_noteOff(0xFF);
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
	/* S078 §5.4: the committed Scene change rebuilds the effective cache. */
	seq_refreshTrackEffectiveParams();
	seq_realignActivePatternToMasterClock();
	/*
	 * S076 Rule B: clear the Scene-target automation dirty bitmap.
	 * Same rationale as seq_selectActivePattern() — see Change 15.
	 */
	seq_clearSceneAutomationDirty();
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
    if (flags & PAT_SPECIAL_PROB_BIT)
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
 * or I/O. Affiliates: seq_queueEffectStepMarker() and seq_drainPendingAutomation().
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

static void seq_queueEffectStepMarker(uint8_t track, uint8_t step)
{
    uint16_t step_id = (uint16_t)(track * NUM_STEPS + step);

    if ((seq_effectAutomationTracks & (uint8_t)(1u << track)) == 0u)
        return;
    /*
     * Effect-type gate for per-track FX step markers (S077 P2 §3.6, Q9).
     *
     * What: before queueing an FX step boundary, compare the active Scene's
     * retained effect type against the track's played Scene's retained effect
     * type. If they differ, the marker is dropped so the foreground drain does
     * not begin an overlay against the wrong effect type. Why: FX automation
     * entries are authored for a specific effect type. Inputs:
     * seq_activePattern, seq_perTrackPattern[track]. Output: marker queued only
     * when types match. Fast path: when no per-track override is set the types
     * are trivially equal, so the comparison is skipped behind seq_perTrackActive.
     */
    if (seq_perTrackActive && !seq_trackEffectTypeMatchesActive(track))
        return;
    if (seq_pending_automation_count < SEQ_PENDING_BUF_COUNT) {
        uint8_t pending_index = seq_pending_automation_count;

        seq_pending_automation[pending_index].identity =
            (uint16_t)(step_id | SEQ_PENDING_TYPE_FX_STEP_BIT);
        seq_pending_automation[pending_index].payload = 0u;
        seq_pending_automation_count = (uint8_t)(pending_index + 1u);
        seq_pending_automation_drain = 1u;
    } else {
        patternTrace_recordOverflow(
            (uint16_t)(step_id | SEQ_PENDING_TYPE_FX_STEP_BIT), 0u);
    }
}

/*
 * Evaluate the conditional playback gate for one step.
 *
 * Input: resolved PatternData specials read from the dynamic block before the
 * trigger-active check. Output: 1 when the complete step may participate in
 * playback, or 0 when both its trigger and automation are suppressed.
 *
 * Probability is currently the only conditional special. The PatternData
 * default (127, including an absent probability special) always passes; lower
 * values are compared against the same hardware-RNG conversion used by the
 * former trigger-local implementation. This is the single entry point for
 * future conditional-trigger evaluations, so named conditions can be added
 * here without reintroducing trigger-state-dependent automation queueing.
 *
 * Common caller: seq_advanceTrackStep() in TIM3 ISR context. Affiliates:
 * pat_readStepSpecials() and GetRngValue().
 */
static uint8_t seq_evaluateStepCondition(const pat_step_specials_t *sp)
{
    /* Probability gate. */
    if (sp->probability < 127u) {
        uint8_t rnd = (uint8_t)(((uint16_t)(GetRngValue() & 0x7FFFu) *
                                 127u) / 32767u);
        if (rnd >= sp->probability)
            return 0u;
    }

    /* Future conditional-trigger evaluations belong at this single gate. */
    return 1u;
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
	 */
	if (play_mode >= 6u)
		play_mode = 0u;
	if ((play_mode == 4u || play_mode == 5u) &&
	    (seq_trackPlayState[track] & SEQ_PLAY_STATE_STOPPED))
		return;
	switch (play_mode) {
	case 1u: /* rev */
		seq_stepIndex[track]--;
		if (seq_stepIndex[track] < 0)
			seq_stepIndex[track] = (int16_t)(len - 1u);
		break;
	case 2u: /* pip */
		if (seq_trackPlayState[track] & SEQ_PLAY_STATE_PIP_REV) {
			if (seq_stepIndex[track] <= 0) {
				seq_trackPlayState[track] &=
				    (uint8_t)~SEQ_PLAY_STATE_PIP_REV;
			} else {
				seq_stepIndex[track]--;
			}
		} else {
			if (seq_stepIndex[track] >= (int16_t)(len - 1u)) {
				seq_trackPlayState[track] |= SEQ_PLAY_STATE_PIP_REV;
			} else {
				seq_stepIndex[track]++;
			}
		}
		break;
	case 3u: /* rnd */
		seq_stepIndex[track] =
		    (int16_t)(((uint16_t)GetRngValue() & 0x7FFFu) % len);
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
		if (seq_stepIndex[track] >= (int16_t)len)
			seq_stepIndex[track] = 0;
		break;
	}

	if (seq_SomModeActive) {
		if (track == 0u)
			som_tick(seq_stepIndex[0], seq_mutedTracks);
		return;
	}

	if (!(seq_mutedTracks & (1u << track))) {
		/*
		 * Resolve specials and evaluate the step gate before checking bit 15.
		 * Bit 14 owns both conditional values and automation, so a non-trigger
		 * step is still a complete playback step for automation purposes.
		 */
		pat_step_specials_t sp = pat_readStepSpecials(
		    seq_perTrackPattern[track], track, (uint8_t)seq_stepIndex[track]);
		uint8_t step_allowed = seq_evaluateStepCondition(&sp);

		if (pat_isStepActive(track, (uint8_t)seq_stepIndex[track], seq_perTrackPattern[track])) {
			if (seq_eraseActive && track == menu_getActiveVoice()) {
				/*
				 * Live erase clears only the static trigger in TIM3. Pool
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
			} else if (step_allowed) {
				/*
				 * Shuffle trigger deferral (S078 §3.2).
				 *
				 * What: odd-indexed steps (1, 3, 5, ...) with nonzero shuffle
				 * are deferred by (shuffle_value * 24) / 256 PPQ ticks. Even
				 * steps fire immediately. Why: shuffle is a groove/feel
				 * concept that delays every other beat. The delay is always
				 * based on the 1/16th grid (24 PPQ ticks), not the track's
				 * step scale, so it vanishes for large scales (musically
				 * correct). Inputs: region->track_shuffle[track],
				 * seq_stepIndex[track] parity. Outputs: immediate trigger or
				 * deferred trigger setup. A pending deferral is flushed first
				 * so a fast track cannot lose an un-fired odd step.
				 * Affiliates: seq_processSchedulerTick() shuffle tick-down.
				 */
				/*
				 * Effective (morphed) shuffle (S078 §5.1/§3.2): the
				 * Normal amount interpolated against the Scene Morph
				 * endpoint, or a step-automation overlay.
				 */
				uint8_t shuffle_val = seq_effectiveTrackShuffle[track];
				uint8_t is_odd_step =
				    (uint8_t)(seq_stepIndex[track] & 1);

				if (seq_trackShufflePending[track]) {
					seq_trackShufflePending[track] = 0u;
					seq_triggerVoice(track,
					                 seq_trackShuffleVel[track],
					                 seq_trackShuffleNote[track]);
				}
				if (shuffle_val > 0u && is_odd_step && len > 0u) {
					uint8_t delay =
					    (uint8_t)(((uint16_t)shuffle_val * 24u) / 256u);

					if (delay > 0u) {
						seq_trackShuffleDelay[track] =
						    (uint8_t)(delay - 1u);
						seq_trackShufflePending[track] = 1u;
						seq_trackShuffleVel[track] = sp.velocity;
						seq_trackShuffleNote[track] = sp.note;
					} else {
						seq_triggerVoice(track, sp.velocity, sp.note);
					}
				} else {
					seq_triggerVoice(track, sp.velocity, sp.note);
				}
			}
		}

		/*
		 * Automation follows the conditional gate, not trigger state. Preserve
		 * the existing live-erase guard so the active edit track does not apply
		 * a queued automation value while its trigger is being removed.
		 */
        if (step_allowed &&
            (!seq_eraseActive || track != menu_getActiveVoice())) {
            /* Marker first; a same-step entry then re-holds its parameter. */
            seq_queueEffectStepMarker(track,
                                      (uint8_t)seq_stepIndex[track]);
            seq_queueStepAutomations(track, (uint8_t)seq_stepIndex[track]);
        }
	}

	if (seq_rollRate != 0xffu && (seq_rollState & (1u << track))) {
		if ((seq_stepIndex[track] % seq_rollRate) == 0) {
			const uint8_t vol = ROLL_VOLUME;
			seq_triggerVoice(track, vol, MIDI_DEFAULT_TRIGGER_NOTE);
			seq_recordTrigger(track);
		}
	}
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

            if ((identity & SEQ_PENDING_TYPE_FX_STEP_BIT) != 0u) {
                /* One owning track's step boundary precedes its entries. */
                effects_automationStepBegin(owner_track);
            } else if ((identity & SEQ_PENDING_TYPE_AUTOMATION_BIT) != 0u) {
                if (instrumentParam_isVoiceParameter(target) &&
                    instrumentManager_targetValid(owner_scene, target,
                                                  INSTRUMENT_TARGET_AUTOMATION)) {
                    uint8_t slot = instrumentParam_slot(target);
                    const kit_instrument_slot_t *instrument =
                        scene_instrumentSlotConst(owner_scene, slot);
                    const ParamDescriptor *descriptor = instrument
                        ? instrumentManager_descriptor(
                              instrument->type, instrumentParam_local(target))
                        : 0;

                    /* Mark only successful voice overlays for retrigger restore. */
                    if (descriptor &&
                        instrumentManager_writeRuntime(slot, descriptor, value)) {
                        uint8_t local = instrumentParam_local(target);
                        seq_automation_dirty[slot] |= (1ULL << local);
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
                /* Close the last FX marker group after the producer lock ends. */
                effects_automationStepFlush();
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
 */
void seq_restoreAutomatedParameters(uint8_t trigger_track)
{
    uint8_t slot;
    uint64_t mask;
    const kit_instrument_slot_t *instrument;

    if (trigger_track >= NUM_TRACKS)
        return;
    slot = (trigger_track >= INSTRUMENT_SLOT_COUNT)
        ? (INSTRUMENT_SLOT_COUNT - 1u) : trigger_track;
    mask = seq_automation_dirty[slot];
    if (!mask)
        return;

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
    seq_automation_dirty[slot] = 0u;
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
}

/*
 * Shuffle delay tick-down (S078 §3.2).
 *
 * What: each PPQ tick decrements the shuffle delay counter for any track with
 * a pending deferred trigger. When the counter reaches zero, the deferred
 * trigger fires with the velocity/note captured at the original step. Why:
 * shuffle must operate at PPQ tick resolution for smooth swing feel. The delay
 * is computed from (shuffle_value * 24) / 256, which produces 0..11 ticks of
 * delay at 96 PPQ. A pending deferral fires even if its track stopped in the
 * meantime (S078 §10.2): the step was already reached; shuffle only delays
 * execution. Inputs: seq_trackShufflePending[], seq_trackShuffleDelay[].
 * Outputs: seq_triggerVoice() when a delay expires. Affiliates:
 * seq_advanceTrackStep() sets up the deferred trigger.
 */
static void seq_processShuffleDelays(void)
{
	uint8_t track;

	for (track = 0u; track < NUM_TRACKS; track++) {
		if (!seq_trackShufflePending[track])
			continue;
		if (seq_trackShuffleDelay[track] == 0u) {
			seq_trackShufflePending[track] = 0u;
			seq_triggerVoice(track, seq_trackShuffleVel[track],
			                 seq_trackShuffleNote[track]);
		} else {
			seq_trackShuffleDelay[track]--;
		}
	}
}

static void seq_processSchedulerTick(void)
{
	uint8_t track;
	uint8_t anyAdvanced = 0u;

	if (!seq_running)
		return;

	if (seq_initialSchedulerTick) {
		seq_initialSchedulerTick = 0u;
		seq_masterStepClock = 0u;
		seq_masterStepCnt = 0u;
		if (seq_handleMasterBoundary()) {
			/* Pattern switching must not suppress the independent FX boundary. */
			seq_fxClockTick();
			midiParser_checkMtc();
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
				midiParser_checkMtc();
				return;
			}
		}
	}

	/* Shuffle deferral tick-down runs before this tick's DDA advance. */
	seq_processShuffleDelays();

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

    if (anyAdvanced) {
        seq_ledState.chaseStep = seq_stepIndex[menu_getActiveVoice()];
        seq_ledState.dirty |= SEQ_LED_DIRTY_CHASE;
    }

    /* FX sequencer follows the same master PPQ clock, independent of Pattern. */
    seq_fxClockTick();
    midiParser_checkMtc();
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
	 * MIDI, and pulse sync.
	 */
	uint16_t ticks = (uint16_t)stepSize * 3u;
	while (ticks--) {
		seq_processSchedulerTick();
	}
	seq_lastTick = systick_ticks;
	seq_calcDeltaT(seq_tempo);
}
//------------------------------------------------------------------------------
void seq_resetDeltaAndTick()
{
	uint8_t i;

	/*
	 * MIDI clock is 24 PPQ while the internal scheduler is 96 PPQ, so every
	 * external MIDI clock pulse advances four internal scheduler ticks. This
	 * replaces the old "4 steps every 3 clocks" bridge, which belonged to the
	 * previous 32-steps-per-beat grid.
	 */
	for (i = 0u; i < SEQ_INTERNAL_TICKS_PER_MIDI_CLOCK; i++)
		seq_processSchedulerTick();
	seq_lastTick = systick_ticks;
	seq_calcDeltaT(seq_tempo);
}
//------------------------------------------------------------------------------
void seq_resetToPatternStart(void)
{
	/* External reset should reposition the sequence without toggling transport
	** state or sending MIDI stop/start. The next clock pulse will play the
	** pattern start according to each track's rotation. */
	seq_barCounter = 0;
	seq_delayedSyncStepFlag = 0;
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

		if (!seq_getExtSync())
			seq_processSchedulerTick();

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
 * Inputs: isRunning — nonzero starts transport, zero stops it.
 * Outputs: stop halts transport, sends MIDI_STOP, silences notes/triggers,
 * and restores automation; start resets scheduler state, sends MIDI_START,
 * and enables transport only after the common grid reset is complete.
 * Common callers: front-panel transport, MIDI realtime, clockSync, and the
 * Menu audio-suspend path. Affiliates: seq_processSchedulerTick() consumes
 * seq_running; seq_resetStepScheduler() prepares the first tick;
 * seq_setStepIndexToStart() performs the restore-before-clear operation;
 * voiceControl_noteOff(), trigger_reset()/trigger_allOff(), and
 * midiParser_checkMtc() finish stop-side hardware/MTC cleanup.
 *
 * The stop-branch seq_clearAutomationDirty() call intentionally lives only in
 * seq_setStepIndexToStart(). A separate early clear would erase the dirty bits
 * before the restore and recreate the missed-automation defect.
 */
void seq_setRunning(uint8_t isRunning)
{
	if (!isRunning)
	{
		seq_running = 0u;

		seq_barCounter = 0;
		seq_resetStepScheduler();
		seq_deltaT = 0;
		seq_sendRealtime(MIDI_STOP);

		voiceControl_noteOff(0xFF);

		trigger_reset(0);
		trigger_allOff();

		midiParser_checkMtc();

		/*
		 * Queue the foreground chase drain after publishing seq_running = 0.
		 * The last playback position may still own a LED_LAYER_CHASE; the
		 * drain-side transport guard removes that inversion on its next pass.
		 */
		seq_ledState.dirty |= SEQ_LED_DIRTY_CHASE;
	} else {
		seq_resetStepScheduler();
		seq_sendRealtime(MIDI_START);
		trigger_reset(1);
	}

	seq_setStepIndexToStart();

	if (isRunning)
		seq_running = 1u;
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
			// --AS turn off the midi note that may be playing on that track
			voiceControl_noteOff(midi_MidiChannels[trackNr]);
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
void seq_setRoll(uint8_t voice, uint8_t onOff)
{
	if(voice >= 7) return;

	if(onOff) {
		seq_rollState |= (1<<voice);
		if(seq_rollRate == 0xff) {
			//trigger one shot
			seq_triggerVoice(voice,ROLL_VOLUME,MIDI_DEFAULT_TRIGGER_NOTE);
			//record roll notes
			seq_recordTrigger(voice);
		}
	} else {
		seq_rollState &= ~(1<<voice);
	}
};
//--------------------------------------------------------------------------------
void seq_setRollRate(uint8_t rate)
{
	/*
	0 - one shot immediate trigger
	1 - 1/1
	2 - 1/2
	3 - 1/3
	4 - 1/4
	5 - 1/6
	6 - 1/8
	7 - 1/12
	8 - 1/16
	9 - 1/24
	10 - 1/32
	11 - 1/48
	12 - 1/64
	13 - 1/128
				*/

	switch(rate)
	{
	case 0:
		seq_rollRate = 0xfe;
		break;
	case 1: // 1/1
		seq_rollRate = 0x7f;
		break;

	case 2: // 1/2
		seq_rollRate = 0x3f;
		break;

	case 3:// 1/3
		seq_rollRate = 0x2a;
			break;

	case 4:// 1/4
		seq_rollRate = 0x1f;
			break;

	case 5:// 1/6
		seq_rollRate = 0x31;
			break;

	case 6:// 1/8
			seq_rollRate = 0x0f;
			break;

	case 7:// 1/12
		seq_rollRate = 0x0a;
			break;

	case 8:// 1/16
		seq_rollRate = 0x07;
			break;

	case 9: // 1/24
		seq_rollRate = 0x05;
		break;

	case 10:// 1/32
		seq_rollRate = 0x03;
		break;

	case 11:// 1/48
		seq_rollRate = 0x02;
		break;

	case 12://1/64
		seq_rollRate = 0x01;
		break;

	case 13://1/128
		seq_rollRate = 0x00;
		break;
	}
	seq_rollRate +=1; //is there a reason for this offset here? seems the value could be assigned directly!?!

}
//------------------------------------------------------------------------
/** quantize a step to the seq_quantisation value*/
#define QUANT(x) (NUM_STEPS/x)
static uint8_t seq_quantize(uint8_t step)
{
	uint8_t quantisationMultiplier=1;
	switch(seq_quantisation)
	{
	case QUANT_8:
		quantisationMultiplier = QUANT(8);
		break;

	case QUANT_16:
		quantisationMultiplier = QUANT(16);
		break;

	case QUANT_32:
		quantisationMultiplier = QUANT(32);
		break;

	case QUANT_64:
		quantisationMultiplier = QUANT(64);
		break;

	case NO_QUANTISATION:
	default:
		return step;
		break;
	}

	//now calc the quantisation
	float frac = step/(float)quantisationMultiplier;
	uint8_t itg = (uint8_t)frac;
	frac = frac - itg;

	if(frac>=0.5f)
	{
		return ((itg + 1)*quantisationMultiplier)&0x7f;
	}
	return itg*quantisationMultiplier;
}
//------------------------------------------------------------------------
//------------------------------------------------------------------------
void seq_recordTrigger(uint8_t trackNr)
{
	/*
	 * Record a live event as one trigger bit when recording is active.
	 *
     * Input: target track from MIDI or roll performance. Output: the quantized
     * fixed-grid address entry's bit 15 is set and visible STEP feedback is
     * dirtied. MIDI note and velocity are special values assigned by the step
     * editor; this trigger-only recording path still does not record them.
	 * Affiliates: MidiParser, roll handling, PatternData, and LED record state.
	 */
	//only record notes when seq is running and recording
	if(trackNr < NUM_TRACKS && seq_running && seq_recordActive)
	{
		const uint8_t quantizedStep = (uint8_t)(seq_quantize(
			(uint8_t)seq_stepIndex[trackNr]) % NUM_STEPS_PER_BAR);

		/*
		 * Record only into the active Scene/Pattern.
		 *
		 * Pattern-next/repeat is removed, so even late-bar quantized notes that
		 * land on step 0 stay in seq_activePattern. Future Scene-level switching
		 * can reintroduce cross-Scene recording explicitly; the sequencer must
		 * not infer it from unrelated legacy pattern-setting bytes.
		 */
		pat_setStepActive(seq_activePattern, trackNr, quantizedStep, 1u);

		if( (menu_getViewedPattern() == seq_activePattern) && ( menu_getActiveVoice() == trackNr) )
		{
			/*
			 * Recording LED updates are queued rather than drawn here.
			 *
			 * Sequencer knows the recorded step, but ledHandler owns the LED
			 * hardware and buttonHandler owns selected-step/shift/mode UI
			 * context. The dirty flags tell led_processSeqLedState() to repaint
			 * only when the recorded track/pattern is visible.
			 */
			seq_ledState.recordMainStep = quantizedStep;
			seq_ledState.dirty |= SEQ_LED_DIRTY_REC_MAIN;
		}
	}
}

//------------------------------------------------------------------------
void seq_setRecordingMode(uint8_t active)
{
	/*
	 * Record-arm gate on per-track overrides (S077 P2 §3.7).
	 *
	 * What: refuses to arm live recording when any track plays from a Scene
	 * other than the active Scene. Why: recording into a track whose played
	 * Scene differs from the viewed Scene would write steps into the wrong
	 * Scene's pattern data. Inputs: seq_perTrackActive. Output: seq_recordActive
	 * unchanged when per-track overrides exist; disarming (active == 0) is always
	 * honoured.
	 */
	if (active && seq_perTrackActive)
		return;
	seq_recordActive = active;
}

void seq_setErasingMode(uint8_t active)
{
	seq_eraseActive = active;
}

void seq_midiNoteOff(uint8_t chan)
{
	uint8_t i;
	MidiMsg msg;

	// we are not filtering according to tx filter because they might have turned that
	// setting on while a note was sustaining

	msg.bits.length=2;
	msg.data2=0;

	if(chan==0xff) { // all notes off
		for(i=0; i<16; i++)
			if((1<<i) & midi_notes_on) {
				msg.status=	NOTE_OFF | i;
				msg.data1=midi_chan_notes[i];
				seq_sendMidi(msg);
			}
		// reset all
		midi_notes_on=0;
		return;
	}
	/*
	 * Mirror the note-on channel guard for the note-off cache.
	 *
	 * Inputs: chan is either 0xff for all-notes-off or a zero-based MIDI
	 * channel. Output: invalid channels are ignored before bit shifts or
	 * midi_chan_notes[] indexing. This keeps live MIDI recording and sequencer
	 * playback from touching memory outside the 16-channel cache if stale
	 * settings or future Scene routing accidentally pass a sentinel value.
	 */
	if(chan >= 16u)
		return;
	// The proper way to do a note off is with 0x80. 0x90 with velocity 0 is also used, however I think there is still
	// synth gear out there that doesn't recognize that properly.
	if((1<<chan) & midi_notes_on) {
		msg.status=	NOTE_OFF | chan;
		msg.data1=midi_chan_notes[chan];
		seq_sendMidi(msg);
		// turn off our knowledge of that note playing
		midi_notes_on &= (~(1<<chan));
	}
}

static void seq_sendRealtime(const uint8_t status)
{
	MidiMsg msg = {0,0,0, {0,0,0}};
	// --AS FILT filter out realtime msgs if appropriate
	if((midiParser_txRxFilter & 0x20)==0)
		return;
	msg.status=status;
	seq_sendMidi(msg);
}

/* Send a note on message. This will filter out these messages if appropriate
 */
void seq_sendMidiNoteOn(const uint8_t channel, const uint8_t note, const uint8_t veloc)
{
	MidiMsg msg = {0,0,0, {0,0,2}};
	// --AS FILT filter out note msgs if appropriate
	if((midiParser_txRxFilter & 0x10)==0)
		return;
	/*
	 * Guard the note-on bookkeeping table before composing/sending MIDI.
	 *
	 * Inputs: channel is a zero-based MIDI channel derived from Scene track
	 * settings. Output: valid 0..15 channels send and update midi_chan_notes[];
	 * invalid values are ignored instead of indexing past the 16-channel note
	 * cache. Why this must exist: Scene expansion exposed more paths where a
	 * stale/corrupt pattern or sentinel value can reach playback during a live
	 * trigger; a bad channel here can hard-fault on the second note when the
	 * note-on cache is written.
	 */
	if(channel >= 16u)
		return;

	msg.status=NOTE_ON | channel;
	msg.data1=note;
	msg.data2=veloc;
	seq_sendMidi(msg);

	// keep track of which notes are on so we can turn them off later
	midi_chan_notes[channel]=note;
	midi_notes_on |= (1 << channel);

}

/* This will send a prog change on the global channel and will filter
 * out the message if appropriate
 */
static void seq_sendProgChg(const uint8_t ptn)
{
	MidiMsg msg = {0,0,0, {0,0,1}};

	// --AS FILT filter out PC msgs if appropriate
	if((midiParser_txRxFilter & 0x80)==0)
		return;

	msg.status = PROG_CHANGE | midi_MidiChannels[7];
	msg.data1=ptn;
	msg.bits.length=1;
	seq_sendMidi(msg);
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

}
