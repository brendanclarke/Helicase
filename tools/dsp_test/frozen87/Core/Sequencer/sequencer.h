/*
 * sequencer.h
 *
 * Timing/playback scheduler API.
 */

#ifndef SEQUENCER_H_
#define SEQUENCER_H_

#include "stm32f4xx.h"
#include "globals.h"
#include "PatternData.h"

#define ROLL_VOLUME 100

enum Seq_QuantisationEnum
{
	NO_QUANTISATION,
	QUANT_8,
	QUANT_16,
	QUANT_32,
	QUANT_64,
	/*
	 * QUANT_TRACK: track-grid quantisation (S081 §5.6, R16).
	 *
	 * What: adds QUANT_TRACK = 5 after QUANT_64. Recording snaps to the nearest
	 * step start of the recorded track's own grid. The looper uses it for per-track
	 * loop starts (each looped track's loop start is its own current step start).
	 * Why: the existing quantisation options are all master-clock divisions; trk
	 * provides a track-relative snap for tracks running at non-standard scales.
	 * Affiliates: seq_quantisation, seq_setQuantisation, seq_loopEventAnchor,
	 * quantisationNames.
	 */
	QUANT_TRACK,
};

extern uint8_t seq_activePattern;
extern uint8_t seq_newPatternAvailable;
extern uint8_t seq_resetBarOnPatternChange;

/*
 * Live recording and erasing activity flags.
 *
 * What: nonzero while the front-panel RECORD or ERASE mode is active. Why:
 * the Pattern AutoSave drain must not copy the live Pattern region while
 * seq_tick() can still modify it. Inputs/outputs: written by the existing
 * record/erase mode handlers and read by filesystem.c's drain scheduler.
 * Affiliates: PatternData mutation paths and S064 Pattern AutoSave.
 */
extern uint8_t seq_recordActive;

/*
 * Fill-mode flag, written by buttonHandler PERF hold logic (S079 §3.3).
 *
 * What: nonzero while fill mode is active, i.e. while the PERF button has been
 * held past DOUBLE_CLICK_TIMEOUT. Why: the condition evaluator reads this for
 * the fil/!fl conditions. Written by the foreground button handler, read by
 * the TIM3 ISR; a single-byte store/load is atomic on Cortex-M7. Affiliates:
 * seq_evaluateStepCondition(), buttonHandler.c PERF hold.
 */
extern uint8_t seq_fillActive;

/*
 * Loop-active flag (S080 B2/C1, P2).
 *
 * What: single-byte ISR gate, 1 while the PERF looper is engaged: a looper
 * SELECT, the VOICE of a looped track, or a BAR button is held (S080 P2
 * Defect 8). It may be 1 with no looped tracks (all looped VOICEs released
 * while SELECT is held). Written by the sequencer looper API under PRIMASK;
 * read by the TIM3 ISR in seq_evaluateStepCondition() (lop/!lp), the scene
 * chain conditions (lox/lob/!lx/!lb) and the loop wrap. Single-byte reads are
 * atomic on Cortex-M7, like seq_fillActive. Inputs: seq_loopEngage()/
 * seq_loopDisengage()/transport stop. Affiliates: buttonHandler looper,
 * seq_fillActive.
 */
extern uint8_t seq_loopActive;

/*
 * Looper control API (S080 C1, rewritten S080 P2 Defects 7/8/14).
 *
 * What: seq_loopEngage() starts a loop on trackMask (filtered by the loopable
 * flags), anchoring each track's loop start on the master clock, and returns
 * the mask actually looping (0 and no engage while the transport is stopped).
 * seq_loopUpdateMask() changes the looped set while engaged: added tracks get
 * a fresh anchor, kept tracks keep theirs, dropped tracks resume where they
 * would have been; it returns the new mask. seq_loopDisengage() ends the loop
 * and resumes every looped track. seq_loopUpdateDivision() changes the loop
 * length without moving any loop start. seq_loopableTrackMask() reports the
 * tracks whose played Scene allows looping (Defect 8a). Why: the foreground
 * owns gestures and the TIM3 ISR owns step timing; all shared state is
 * published under PRIMASK. Inputs: 7-bit masks, division 0..7 (1 bar..1/128),
 * modifier 0 none / 1 dot / 2 triplet. Callers: buttonHandler.c PERF
 * SELECT/VOICE/BAR handlers.
 */
uint8_t seq_loopEngage(uint8_t trackMask, uint8_t division, uint8_t modifier);
uint8_t seq_loopUpdateMask(uint8_t newMask);
void    seq_loopDisengage(void);
void    seq_loopUpdateDivision(uint8_t division, uint8_t modifier);
uint8_t seq_loopableTrackMask(void);

/*
 * Deferred / queued scene change API (S080 F7/F8/F9).
 *
 * What: seq_requestSceneChange() arms a direct Scene change at a musical
 * division (division 0 = instant, applied by the caller instead);
 * seq_queueSceneChange() arms a queued (SHIFT+SEQ) change. The TIM3 ISR raises
 * the fire flag at the boundary; the foreground polls seq_sceneChangeFirePending()
 * and then reads seq_pendingSceneTarget()/seq_pendingSceneIsQueue() and calls
 * seq_clearPendingSceneChange(). Why: scene changes can be quantised to the
 * master clock without doing DSP/LED work in the ISR. Inputs: scene index and
 * division (scd/qcd option value). Outputs: the pending state and fire flag.
 * Affiliates: menu_serviceDeferredSceneChange(), buttonHandler SHIFT+SEQ.
 */
void seq_requestSceneChange(uint8_t scene_index, uint8_t division);
/* S083 Q9: mark the next whole-Scene change as panel or MIDI (no echo). */
void seq_setSceneChangeOrigin(uint8_t origin);
void seq_queueSceneChange(uint8_t scene_index, uint8_t division);
uint8_t seq_sceneChangeFirePending(void);
uint8_t seq_pendingSceneIsQueue(void);
uint8_t seq_pendingSceneTarget(void);
void seq_clearPendingSceneChange(void);
/* S080 P2: cancel a pending direct/chain change but keep a queued one. */
void seq_cancelPendingDirectChange(void);
/* S080 G7: 1 when the fired pending change came from a scene chain. */
uint8_t seq_pendingSceneIsChain(void);
/* S080 P2 Defect 10: queued target Scene (0..15) or 0xFF when none. */
uint8_t seq_queuedSceneTarget(void);
/*
 * S080 G7: chain repeat of the playing Scene (counters reset, stp cleared,
 * realign; restart when `restart` = pcr). Client: menu_serviceDeferredSceneChange().
 */
void seq_chainRepeatSelf(uint8_t restart);

/*
 * Per-track Pattern assignment stub for the unified stack service.
 *
 * What: seven resident Scene indices, one per sequencer track. Why: S067
 * keeps every track assigned to seq_activePattern, but the array establishes
 * the future per-track playback data path without adding a second mutation
 * target today. Inputs: seq_init(), seq_selectActivePattern(), and the
 * master-boundary switch. Output: all entries mirror the active Scene.
 * RAM: +7 bytes SRAM1. Affiliate: future per-track Pattern assignment.
 */
extern uint8_t seq_perTrackPattern[NUM_TRACKS];

/*
 * Nonzero when any track plays from a Scene other than seq_activePattern
 * (S077 P2 §1.1).
 *
 * Inputs: derived from seq_perTrackPattern[] vs seq_activePattern. Output:
 * foreground code reads this to gate per-track UI/record behaviour: the fast
 * path when 0 leaves all seq_activePattern reads correct as-is. Affiliates:
 * seq_setTrackPlayedScene(), seq_clearPerTrackOverrides().
 */
extern uint8_t seq_perTrackActive;

/*
 * Set one track's played Scene for per-track playback (S077 P2 §1.3).
 *
 * Inputs: track 0..6, valid Scene index. Output: seq_perTrackPattern[track]
 * updated, seq_perTrackActive recomputed. Tracks 5+6 (HiHat pair) switch
 * together. Client: the buttonHandler PERF hold-VOICE+press-SEQ gesture.
 */
void seq_setTrackPlayedScene(uint8_t track, uint8_t scene_index);
/* S083 P3: shared panel/MIDI per-track Scene move (realign + note-off);
** the HiHat pair (bits 5/6) always moves together (F-7). */
void seq_assignTracksToScene(uint8_t trackMask, uint8_t scene_index);

/*
 * Coalesce all tracks back to the active Scene (S077 P2 §1.5).
 *
 * Output: all seq_perTrackPattern[] == seq_activePattern; seq_perTrackActive
 * = 0. Client: the scene-level SEQ press coalesce (PAR_FOLLOW on single-click,
 * PAR_FOLLOW off double-click).
 */
void seq_clearPerTrackOverrides(void);

/*
 * Read one track's played Scene (S077 P2 §3.5).
 *
 * Inputs: track/slot 0..6. Output: the resident Scene playback reads for that
 * track (seq_activePattern for an out-of-range track). Client:
 * preset_getSlotPlayedScene() resolves per-voice FX-send, fader, and Morph
 * lookups through this.
 */
uint8_t seq_getTrackPlayedScene(uint8_t track);

/*
 * Realign one track's step cursor from the master clock using its own played
 * Scene's track length (S077 P2 §1.4).
 *
 * Input: track 0..6. Output: seq_stepIndex[track] repositioned. Client:
 * per-track assignment gesture, double-click single-track realign.
 */
void seq_realignTrackToMasterClock(uint8_t track);
/*
 * Recompute the effective (morphed) per-track timing cache (S078 §5.4).
 *
 * What: refreshes the sequencer's ISR-static effective length/scale/shuffle
 * arrays from each track's played Scene Normal values, Scene Morph endpoints,
 * and associated voice Morph amount. Why: the sequencer reads these arrays
 * instead of the Pattern region, so a Morph sweep changes playback without
 * writing retained Normal data. Inputs: none. Output: seq_effectiveTrack*
 * updated. Callers: presetMorph_tick() (every foreground worker tick),
 * seq_selectActivePattern(), seq_alignActivePatternToScene(),
 * seq_setTrackPlayedScene(), seq_clearPerTrackOverrides(), and
 * seq_setStepIndexToStart().
 */
void seq_refreshTrackEffectiveParams(void);

/*
 * Trigger a voice with optional automation reset (S081 §5.8).
 *
 * reset = 1 for a step's first trigger (on-time, deferred, roll first hit,
 * overlay, preview): the pending-reset mask is set so the voice's next audio-
 * boundary trigger restores Instrument automation to the Morph base and clears
 * the R12 menu override. reset = 0 for roll retriggers 2+ (F-L). See the
 * definition in sequencer.c for the full description.
 */
void seq_triggerVoice(uint8_t voiceNr, uint8_t vol, uint8_t note, uint8_t reset);
/*
 * Roll velocity rhythm rule (S079 §5), shared (S083 follow-up).
 *
 * What: the velocity of roll hit `pos` (0 = the first hit) under the packed
 * rhythm byte (bits 7..4 cycle, bit 3 0 halve / 1 double, bits 2..0 full
 * count): inside the full-count window `vel`, beyond it vel/2 (floor 1) or
 * vel*2 (clamp 127). 0x00 and any full > cycle are off (always `vel`).
 * Why: step rolls (step special `rtm`) and the PERF BAR overlay rolls
 * (`pv<`/`pv>`) must use one rule. Pure, no state. Callers:
 * seq_rollComputeVelocity() (step rolls) and seq_processLiveTicks()
 * (overlay rolls, S084 P2), both TIM3.
 */
uint8_t seq_rollRhythmVelocity(uint8_t rhythm, uint8_t vel, uint8_t pos);
/*
 * Live overlay roll/flam engine (S084 P2). Contracts; the definitions in
 * sequencer.c carry the full description.
 *
 * seq_liveRollStart(track, rate, note, vel, pos): arm a held roll on the
 * master tick. The caller has already fired hit 0. rate is the step-roll
 * index 1..9 (0 or invalid: stops). The first repeat follows one rate
 * interval after the press (ppq - 1 whole ticks from the next tick); pos is
 * the rhythm position of that repeat (1 after a fired hit 0). Clears the
 * track's mailbox.
 * seq_liveRollSetRate(track, rate): change a running roll's rate and keep
 * the time since its last hit (S081 F-B). No effect unless a roll runs.
 * seq_liveFlamArm(track, note, vel, ticks): arm a flam main hit `ticks`
 * master ticks after the press (12 = 1/32), fired with reset = 1. Clears
 * the mailbox.
 * seq_liveStop(track): disarm the track; the mailbox keeps unreported hits
 * for a final seq_liveTakeHits().
 * seq_liveSetRhythm(rhythm): the packed rhythm byte applied to every live
 * roll from its next hit (the active overlay's `pv<`/`pv>`).
 * seq_liveTakeHits(track, &stampUs, &flamMain): returns the roll hits
 * fired since the last take (0..255), sets flamMain when the flam main
 * fired, and stampUs to the TIM2 µs time of the latest hit; clears them.
 * Context: foreground; TIM3-safe (PRIMASK). Accessor: buttonHandler
 * overlay. Affiliates: seq_processLiveTicks().
 */
void    seq_liveRollStart(uint8_t track, uint8_t rate, uint8_t note,
                          uint8_t vel, uint8_t pos);
void    seq_liveRollSetRate(uint8_t track, uint8_t rate);
void    seq_liveFlamArm(uint8_t track, uint8_t note, uint8_t vel,
                        uint8_t ticks);
void    seq_liveStop(uint8_t track);
void    seq_liveSetRhythm(uint8_t rhythm);
uint8_t seq_liveTakeHits(uint8_t track, uint32_t *stampUs,
                         uint8_t *flamMain);
/*
 * Stopped-transport voice preview.
 *
 * Why: front-panel VOICE re-press audition should use Sequencer's existing
 * voice/MIDI/trigger ownership without advancing pattern state. Input voiceNr
 * is the UI track/voice index. Output is one synth/MIDI preview trigger when
 * valid. Clients: buttonHandler voice button path. Risk: this must not mutate
 * seq_stepIndex[], recording state, automation lanes, or PatternData steps.
 */
void seq_previewVoice(uint8_t voiceNr);
void seq_init(void);
void seq_tick(void);
void seq_resetDeltaAndTick(void);
void seq_resetToPatternStart(void);
void seq_realignActivePatternToMasterClock(void);
/* S083 T1 transport: contract in sequencer.c. sendRealtime = 0 when the
** change came from received MIDI transport (C-11: MRT forwards it). */
void seq_setRunningEx(uint8_t isRunning, uint8_t sendRealtime);
void seq_continueRunning(uint8_t sendRealtime);
void seq_setResumeTick(uint32_t tick);
void seq_setDeltaT(float delta);
void seq_triggerNextMasterStep(uint8_t stepSize);
void seq_setBpm(uint16_t bpm);
uint16_t seq_getBpm(void);
void seq_sync(void);
uint8_t seq_getExtSync(void);
void seq_setQuantisation(uint8_t value);
void seq_setExtSync(uint8_t isExt);

enum SeqExternalSyncSource {
	SEQ_EXT_SYNC_OFF = 0,
	SEQ_EXT_SYNC_USB,
	SEQ_EXT_SYNC_DIN,
	SEQ_EXT_SYNC_PULSE,
	SEQ_EXT_SYNC_AUTO,
};

void seq_setExtSyncSource(uint8_t source);
uint8_t seq_getExtSyncSource(void);
void seq_noteExtSyncActivity(uint8_t source, uint32_t timestampUs);
/*
 * Immediately select the active Scene/Pattern for front-panel PERF switching.
 *
 * Inputs: pattern is a resident Scene/Pattern index validated against
 * PatternData. Outputs: seq_activePattern and seq_pendingPattern are aligned,
 * deferred pattern-load flags are cleared, track counters are recalculated from
 * the existing master tick/step position, UI follow/chase state is notified, and
 * any sounding notes are released. Clients: menu_perfModeSceneButtonPressed().
 * Pattern-only queued switching is retired; callers that need a musical switch
 * must use this Scene-aligned path or a future Scene-level scheduler.
 */
void seq_selectActivePattern(uint8_t pattern);
/*
 * Realign playback to a Scene selection that another owner has already made.
 *
 * Why this exists separately from seq_selectActivePattern(): Pattern is the one
 * Scene-owned payload that playback and the STEP UI address through Sequencer/
 * Menu state (seq_activePattern / menu_shownPattern) rather than through
 * scene_getActiveIndex(). Both are BSS-zero at boot and, before this API
 * existed, were assigned only by menu_perfModeSceneButtonPressed(). Bank Load
 * commits a new active Scene without any front-panel press, so the two indices
 * silently disagreed with SceneData: a subsequent Scene Load wrote its Pattern
 * into the committed active Scene while the sequencer kept reading Scene 0.
 * That produced the "Scene Load never loads the pattern" defect, with every
 * other Scene payload appearing correct because they all resolve through
 * scene_getActiveIndex(). See SCENE_LOAD_PAT_RESTORE.md for the full analysis.
 *
 * Inputs: a resident Scene/Pattern index, validated against PatternData exactly
 * as seq_selectActivePattern() validates its own. Outputs: seq_activePattern
 * and seq_pendingPattern are aligned to that Scene, deferred pattern-load flags
 * are cleared, and fixed-grid track cursors are recalculated from the existing
 * master clock.
 *
 * Deliberately NOT done here, and this is the whole reason the narrower entry
 * point exists: no led_notifyPatternChanged(), no seq_sendProgChg() — which
 * would emit a MIDI program change on the wire — and no voiceControl_noteOff().
 * This is a state restore for a selection somebody else owns, not a performance
 * action, and it must be safe to call during pre-audio boot Bank Load where
 * emitting MIDI or forcing note-offs would be an unwanted side effect.
 *
 * Clients: filesystem.c's Bank Load active-Scene metadata commit. Front-panel
 * PERF switching must keep using seq_selectActivePattern() so that its LED,
 * program-change, and note-off behaviour is unchanged.
 */
void seq_alignActivePatternToScene(uint8_t scene_index);
/*
 * Move playback to Scene 17 without a sound change (S082 P1 D5, D16).
 *
 * What: points the active, pending and every per-track Scene at index 16
 * under PRIMASK, re-snapshots the looper and cancels pending Scene changes.
 * It deliberately does not realign positions, reset rolls/conditions/epochs,
 * send a program change or force note-offs.
 * Inputs: a built Scene 17. Outputs: playback reads Scene 17.
 * Accessors: preset_requestBackgroundBankLoad() tick.
 * Affiliates: seq_selectActivePattern(), seq_loopRefreshSceneSnapshots().
 */
void seq_switchToBackgroundScene(void);
/*
 * Move selected tracks onto Scene 17 without a sound change (S082 P1 D31).
 * Inputs: track_mask (bit t = track t). Outputs: those tracks play index 16;
 * seq_perTrackActive recomputed; pending Scene changes cancelled.
 * Accessors: preset_tickBackgroundBankLoad() phase 3 (rebuild in place).
 */
void seq_moveTracksToBackgroundScene(uint8_t track_mask);
/*
 * Drain TIM3-published voice automation in foreground context.
 *
 * Inputs: the bounded pending queue written by seq_advanceTrackStep(), whose
 * packed automation values are already in the voice descriptor parameter
 * domain. Output: validated voice descriptor runtime images are updated after
 * front-panel service without MIDI-CC-style 7-bit expansion; Scene targets
 * remain queued only for the later Scene-target session. Affiliate: main.c
 * calls this before audio rendering.
 * S081 P2: first ends the ramps of tracks whose next step has applied; each
 * written Instrument entry of an rmp track may start a linear ramp toward the
 * same target on the predicted next step.
 */
void seq_drainPendingAutomation(void);
/*
 * Automation ramp service (S081 P2, rmp).
 *
 * What: every call (one per DSP block) ends the ramps of tracks whose next
 * step has applied; every third call (~2.2 ms) writes each running ramp's
 * interpolated 7-bit value, when it differs from the last value written,
 * through the drain's Instrument write path, so
 * ramp samples are held automation values (binding priority rule). Ramps are
 * cancelled, value kept, on rmp off, live-record latch/erase, an R12 menu
 * override or an invalid target. Inputs: the ramp table filled by the drain,
 * TIM3's per-track ramp contexts, the master tick clock. Outputs: runtime
 * Instrument writes, seq_automation_dirty bits. Context: foreground only.
 * Client: main.c audio_check_and_render(), right after
 * seq_drainPendingAutomation().
 */
void seq_rampService(void);
/*
 * Restore transient automation overlays for one visible trigger track.
 *
 * Inputs: trigger track 0..6, with track 6 mapped to descriptor slot 5.
 * Output: dirty voice-descriptor runtime values are restored from the active
 * Scene endpoint image immediately before the trigger and their bitmap is
 * cleared. Scene-level targets are intentionally not handled by this API.
 * Client: MidiVoiceControl.c's common trigger path.
 * S081 P2 (D3): first ends the trigger track's own ramps; parameters still
 * held by a running ramp of the slot (a mid-step live press, MIDI note, flam
 * or roll renewal) are neither restored nor un-marked.
 */
void seq_restoreAutomatedParameters(uint8_t trigger_track);
/*
 * Report whether step automation currently holds one voice parameter
 * (S075 F3).
 *
 * What: nonzero when seq_automation_dirty[slot] has bit `local` set, that is,
 * seq_drainPendingAutomation() wrote a step value into this descriptor's
 * runtime and the voice has not been triggered since.
 * Why: automation always wins (user rule). The overlay lasts until the next
 * trigger, so writers of the Morph base update morph_interpolation[] but
 * leave a held runtime value alone; seq_restoreAutomatedParameters() then
 * applies the new base at the trigger.
 * Inputs: instrument slot 0..5 (track 7 uses slot 5), descriptor-local index
 * 0..INSTRUMENT_PARAM_COUNT-1. Output: 0/1; 0 for out-of-range input.
 * Context: foreground only. The drain, the trigger funnel
 * (voiceControl_processPending()), the transport restores and the Morph sweep
 * all run in the main loop, so the 64-bit bitmap is never read half-written.
 * Client: presetMorph_writeRuntimeBase() (presetMorphEngine.c).
 * Affiliates: seq_drainPendingAutomation(), seq_restoreAutomatedParameters(),
 * seq_restoreAllAutomation().
 */
uint8_t seq_automationHoldsParameter(uint8_t slot, uint8_t local);
/*
 * Set the Instrument menu override for one parameter and clear its held bit
 * (S081 §5.1, R12).
 *
 * What: sets bit `local` in seq_menuOverride[slot] and clears the same bit in
 * seq_automation_dirty[slot], so the menu edit's endpoint write stands and the
 * drain skips that parameter's automation until the voice's next trigger.
 * Inputs: slot 0..5, descriptor-local index. Context: foreground only.
 * Client: menu_cellCommitValue() INSTRUMENT branch. Affiliates:
 * seq_restoreAutomatedParameters() (per-trigger clear),
 * seq_drainPendingAutomation() (skip filter).
 */
void seq_menuOverrideSet(uint8_t slot, uint8_t local);
/*
 * Clear the Scene-target automation dirty bitmap (S076 Rule B).
 *
 * What: zeroes the bitmap that tracks which Scene mod targets were written
 * by step automation during the current transport pass. After a Scene
 * switch the previous Scene's dirty bits are stale and could alias into
 * the new Scene's target table on transport restore.
 * Input: none. Output: seq_scene_automation_dirty = 0.
 * Callers: seq_selectActivePattern(), seq_alignActivePatternToScene().
 * Affiliate: seq_applySceneAutomation().
 */
void seq_clearSceneAutomationDirty(void);
/*
 * Clear one Scene-target automation dirty bit (S081 §5.2).
 *
 * What: clears the bit at `index` in the Scene-target dirty mask, so a menu
 * edit's per-target clear also clears its held bit and the transport restore
 * cannot revert the edit. Input: bit index (0..31). Client:
 * menu_cellCommitValue() R12 Scene-setting clears. Affiliates:
 * seq_clearSceneAutomationDirty() (bulk clear), seq_restoreAllSceneAutomation().
 */
void seq_clearSceneAutomationBit(uint8_t index);
/*
 * Start or stop the sequencer transport.
 *
 * Inputs: nonzero starts, zero stops. Output: both paths reset the fixed-grid
 * cursor state through seq_setStepIndexToStart(), which restores any dirty
 * voice automation to its Scene morph-interpolation base before clearing the
 * dirty bitmap. The start path publishes seq_running only after that reset so
 * the TIM3 scheduler cannot process an initial tick against partially reset
 * state; the stop path publishes the stopped state before teardown.
 * The stop path also queues SEQ_LED_DIRTY_CHASE after publishing that state so
 * the foreground LED drain clears any chase layer left on the last step.
 *
 * Clients: front-panel transport, MIDI realtime/clockSync, and Menu audio
 * suspend. This call does not alter PatternData or the persisted Scene.
 */
void seq_setRunning(uint8_t isRunning);
uint8_t seq_isRunning(void);
void seq_armActivePatternReload(void);
void seq_setMute(uint8_t trackNr, uint8_t isMuted);
uint8_t seq_isTrackMuted(uint8_t trackNr);
/* seq_setRoll()/seq_setRollRate() retired in S081 A3-5 (PAR_ROLL). */

/*
 * TIM3-to-foreground FX-sequencer latch (Session 072 step 8; plan §11.1).
 *
 * TIM3 publishes only RESET/STEP plus a 0..15 position. EffectsManager takes
 * the newest byte from foreground `effects_service()` and performs all Scene,
 * Morph, DSP, and LED-facing work there. A RESET and first STEP may share one
 * byte. The latch is one byte of SRAM1 and is not a general event queue.
 */
#define SEQ_FX_EVENT_STEP  0x80u
#define SEQ_FX_EVENT_RESET 0x40u
#define SEQ_FX_EVENT_INDEX 0x0Fu
uint8_t seq_fxTakeEvent(void);
/* seq_recordTrigger()/seq_setRecordingMode() removed in S081 B-7 (LiveRecord). */
/* Publish a recorded absolute step for STEP-row LED feedback (S081 B-8c). */
void seq_markRecordedStep(uint8_t track, uint8_t scene, uint8_t step);
/* -------------------------------------------------------------------------
** S081 P1 live-record service API (sequencer side).
** ------------------------------------------------------------------------- */

/*
 * PRIMASK snapshot of a track's timing state (S081 §19.1 item 11, C-8a;
 * S081 P3 loop fields).
 *
 * Inputs: track 0..6. Outputs: a consistent snapshot of TIM3-mutated state
 * for LiveRecord's placement math. S081 P3: when the track is looped,
 * looped = 1 and the loopAnchor, loopStartStep/Accum/PlayState and loopTicks
 * fields copy its loop anchor tick, snapshot (cursor, accumulator, play
 * state before the anchor tick's advance) and loop length, so
 * seq_placeFromSnapshot() can map a shift that crosses a wrap (plan §3.3).
 * Stack only (+12 B per local).
 */
typedef struct {
    uint32_t elapsedTick;
    uint32_t stampUs;
    uint32_t periodUs;
    uint32_t nowUs;      /* P1B item 17: capture µs time (same section) */
    int16_t  stepIndex;
    uint16_t accumulator;
    uint16_t interval;
    uint8_t  length;
    uint8_t  shuffle;
    uint8_t  playMode;
    uint8_t  playState;
    uint8_t  playedScene;
    /* S081 P3: loop state at the capture (looped = 0: the rest is unused). */
    uint8_t  looped;
    uint8_t  loopStartPlayState;
    int16_t  loopStartStep;
    uint16_t loopStartAccum;
    uint16_t loopTicks;
    uint32_t loopAnchor;
} seq_track_time_t;

/*
 * One step-boundary ring entry (S081 §19.1 item 1; S081 P3).
 *
 * step: the step that begins. track: 0..6. fired: that step holds a trigger
 * and its condition passed (known at its start). jump: the entry comes from a
 * looper position jump (a wrap that lands mid-step, or a resume), not from a
 * step advance; the step is a partial replay and its trigger did not fire on
 * this pass. Producer: seq_recRingPush(). Consumer: liveRec_drainRing(),
 * which applies a step's own fired and jump flags when that step ends.
 */
typedef struct {
    uint8_t step;
    uint8_t track;
    uint8_t fired;
    uint8_t jump;
} seq_rec_boundary_t;

/*
 * One placed live hit (S081 §8.3, P1B item 5, P1C item 6).
 *
 * step  landing step index. delay  micro-timing 0..99 from the nominal start.
 * rel  -1 behind the current step, 0 current, +1 ahead. ahead  whole ticks
 * from capture to the landed step start when rel > 0. snapQ8  the signed Q8
 * shift the trk snap added to deltaQ8 (0 without snap): deltaQ8 + snapQ8 is
 * the snapped hit's offset from the capture. Why snapQ8: a quantised flam
 * places its grace 1/32 before the SNAPPED main; without it the grace was
 * placed from the raw hit time under trk. Affiliates: liveRec_placeFlam().
 */
typedef struct {
    uint8_t step;
    uint8_t delay;
    int8_t  rel;
    uint16_t ahead;
    int16_t  snapQ8;
} seq_place_t;

void    seq_setRecordTarget(uint8_t track, uint8_t writing);
void    seq_setAutomationFilter(const uint8_t *latch, const uint8_t *erase,
                                uint8_t eraseTrack);
void    seq_setEraseMasks(uint8_t specials, uint8_t full);
void    seq_suppressStepOnce(uint8_t track, uint8_t step, uint16_t aheadTicks);
void    seq_setLiveHold(uint8_t track, uint8_t on);
void    seq_cancelPendingTrigger(uint8_t track, uint8_t step);
uint8_t seq_takeRecBoundary(seq_rec_boundary_t *out);
void    seq_captureTrackTime(uint8_t track, seq_track_time_t *out);
/*
 * Place a captured hit on a track's step grid (S081 §8.3, P1B item 5;
 * S081 P3 loop-aware): shifts the live cursor by deltaQ8, or, for a looped
 * track when the shift crosses a wrap, the loop snapshot by the target's
 * phase. Returns 0 when the track has not started.
 */
uint8_t seq_placeFromSnapshot(uint8_t track, const seq_track_time_t *snap,
                              int32_t deltaQ8, uint8_t snapToStep,
                              seq_place_t *out);
uint32_t seq_quantiseTick(uint32_t tick, uint8_t frac8);
uint8_t seq_getPlaybackEpoch(void);
uint8_t seq_getSceneEpoch(void);
uint8_t seq_getQuantisation(void);

#endif /* SEQUENCER_H_ */
