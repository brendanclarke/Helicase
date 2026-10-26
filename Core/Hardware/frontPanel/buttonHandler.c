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
#include "copyClearSession.h"
#include "PatternData.h"
#include "BankData.h"
#include "SceneData.h"
#include "menuEffects.h"
#if ENABLE_EUKLID_PAGE
#include "EuklidGenerator.h"
#endif
#include "sequencer.h"
#include "LiveRecord.h"
#include "presetManager.h"
#include "MidiVoiceControl.h"
#include "MidiPitch.h"
#include "MidiOut.h"
#include "MidiLearn.h"
#include "MidiNotes.h"
#include <string.h>
#include <stdint.h>
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
} bh_state;
/*
 * Removed: bh_state.seqRecording (S081 §19.7, B-2e).
 *
 * What: the button handler's private copy of the record state. Readers now query
 * LiveRecord. Affiliates: liveRec_mode(), liveRec_armed(). seqErasing stays
 * until Phase G (G-5) removes the old erase path.
 */

/* S081 G-5: SHIFT/CLEAR hold state for the live-erase chord. */
static uint8_t bh_shiftHeld;
static uint8_t bh_clearHeld;
/* Press tick of a Bank-page COPY (+2 B SRAM1, S082 B2.13). */
static uint16_t bh_bankCopyPressTick;

/*
 * Buttons holding a live-erase gesture (S081 §10.2, P1C items 10/16).
 *
 * What: one bit per SEQ button (bh_eraseSeqMask: an active-track SEQ hold
 * in VOICE/STEP; bh_eraseFxSeqMask: an FX-sequence hold in FX) and per
 * TRACK button (bh_eraseVoiceMask) whose press started an erase gesture.
 * A release ends a gesture only if its bit is set, whatever the mode or
 * erase state is by then. Cleared at a MODE press during erase (the
 * gestures were released by liveRec_eraseReleaseHeld()) and when erase
 * starts from off. Why: releases were routed by the mode at release time,
 * so after a mode change an old button's release ended a gesture started
 * by a new button, and a release after erase end was lost. RAM: 5 B.
 * Affiliates: processPress()/processRelease() erase routers,
 * handleModeButtons(), buttonHandler_liveEraseBegin().
 */
static uint16_t bh_eraseSeqMask;
static uint16_t bh_eraseFxSeqMask;
static uint8_t  bh_eraseVoiceMask;

static uint8_t lastActiveSubPage = 0;
uint8_t buttonHandler_selectedStep = 0;
static uint8_t selectedStepLed = LED_STEP1;

static uint16_t buttonHandler_buttonTimer = 0;
#define TIMER_ACTION_OCCURED -2
static int8_t buttonHandler_buttonTimerStepNr = NO_STEP_SELECTED;

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

/*
 * TRACK buttons holding the Effect-page voice mix overlay (S075 F2-F; +1 B
 * SRAM1, approved F2-Q5).
 *
 * Bit n is set when SHIFT+TRACK n opens the overlay, and by every later TRACK
 * press while any bit is set (with or without SHIFT): the overlay shows the
 * track pressed last. The release edge clears the bit even if SHIFT was
 * released first; the last clear restores the Effect page. While any bit is
 * set no TRACK press mutes (handleVoiceButton()). Overflow reconciliation
 * clears this mask and ends the overlay.
 */
static uint8_t buttonHandler_fxVoiceMixTrackMask = 0u;

/*
 * PERF mode held-VOICE bitmask for per-track Scene assignment (S077 P2 §3.1).
 *
 * What: one bit per VOICE button (bits 0..6) set while that VOICE button is
 * physically held in PERF mode. While any bit is set, SEQ presses are
 * intercepted for per-track assignment instead of scene-level switching. Why:
 * the hold-VOICE+press-SEQ gesture enables per-track Scene playback (plan
 * §4.1). Multiple VOICE buttons may be held simultaneously for multi-track
 * assignment (Q11). RAM: +1 byte SRAM1. Owner: buttonHandler.c. Affiliates:
 * perf_voiceActionOccurred, buttonHandler_perfVoiceHeldSeqPressed(),
 * buttonHandler_perfVoiceReleased().
 */
static uint8_t perf_heldVoiceMask = 0u;

/*
 * PERF VOICE action-occurred flag for mute-toggle cancellation (S077 P2 §3.1).
 *
 * What: set to 1 when a held VOICE button's hold produced a per-track
 * assignment (a SEQ press during the hold) or when a non-VOICE, non-SEQ button
 * was pressed during the hold. When set, VOICE releases do not toggle mute.
 * Why: the VOICE release is a single-track mute toggle only when no per-track
 * assignment or other unrelated action occurred (plan §4.1, Q8). RAM: +1 byte
 * SRAM1. Owner: buttonHandler.c. Affiliates: perf_heldVoiceMask.
 */
static uint8_t perf_voiceActionOccurred = 0u;

/*
 * General-purpose double-click detector (S077 P2 §3.2, Q7).
 *
 * What: detects a second press of the same button within DOUBLE_CLICK_TIMEOUT
 * of the first. Only one button can be in a double-click window at a time. The
 * first click fires its single-click action immediately (no latency); the second
 * click within the timeout returns DBLCLICK_DOUBLE so the caller can apply the
 * additive double-click action. Why: PERF mode needs double-click on SEQ
 * buttons for realignment; the detector is general-purpose for future reuse.
 * Protocol: dblclick_onPress(buttonNr) is called on every press and returns
 * DBLCLICK_FIRST or DBLCLICK_DOUBLE; dblclick_cancel() clears the window when
 * any non-SEQ action intervenes. A router that runs after onPress can also
 * recognise a double by (!dblclick_armed && dblclick_button == buttonNr).
 * RAM: +3 bytes SRAM1 (button id, deadline, armed flag). Affiliates:
 * DOUBLE_CLICK_TIMEOUT, time_sysTick.
 */
static uint8_t dblclick_button = 0u;
static uint16_t dblclick_deadline = 0u;
static uint8_t dblclick_armed = 0u;

/*
 * Looper foreground state (S080 C5, P2 Defects 7/8).
 *
 * What: bh_loopSelectHeldMask - SELECT buttons (1..8) held as looper
 * divisions. bh_loopLastSelectButton - the most recently pressed SELECT (owns
 * the active division, last-pressed-wins). bh_loopTrackMask - the track set
 * the sequencer actually loops (returned by seq_loopEngage()/UpdateMask()).
 * bh_loopBarModifier - momentary BAR modifier (0 none, 1 dot, 2 triplet).
 * bh_loopBarHeld - BAR1 (bit 0) / BAR2 (bit 1) physically held in PERF,
 * tracked in event order. Why: the loop stays engaged while any SELECT, any
 * VOICE or any BAR is held (S081 P3 Q1; S080 counted only VOICEs of looped
 * tracks), until all are released or the mode leaves PERF. Event-order BAR
 * state replaces btn_held[], which the ISR updates ahead of the queued edges.
 * Inputs: SELECT/VOICE/BAR press/release in PERF. Outputs: seq_loop*() calls,
 * SELECT/BAR LEDs. RAM: +5 bytes SRAM1 (S080 P2 adds bh_loopBarHeld).
 * Affiliates: buttonHandler_loopCheckDisengage(), buttonHandler_loopForceRelease(),
 * buttonHandler_loopSelectLed().
 */
static uint8_t bh_loopSelectHeldMask   = 0u;
static uint8_t bh_loopLastSelectButton = 0u;
static uint8_t bh_loopTrackMask        = 0u;
static uint8_t bh_loopBarModifier      = 0u;
static uint8_t bh_loopBarHeld          = 0u;

/*
 * SHIFT+REC settings menu state (S080 E7, P2 Defect 3).
 *
 * What: bh_shiftRecMenuActive is set while the SHIFT+REC overlay (repurposed
 * RECORDING_PAGE) is latched open. bh_shiftRecSavedPage/SubPg remember where
 * to return on exit. bh_shiftReleaseConsume swallows the SHIFT release that
 * pairs with a SHIFT press used to close a latched menu (SHIFT+REC or the
 * chain editor). Why: latched menus open on SHIFT+REC / SHIFT+PLAY+SEQ, stay
 * open after every button is released, and close on the next SHIFT press (or
 * REC for SHIFT+REC). The closing press's release must not run the
 * mode-specific SHIFT release path (which, in PERF, would re-enter the page
 * and clear looper LEDs). Inputs: SHIFT/REC/PLAY/SEQ events. Outputs:
 * page/sub-page switch and LED state. RAM: +4 bytes SRAM1 (S080 P2 adds
 * bh_shiftReleaseConsume). Affiliates: processPress()/processRelease()
 * BUT_SHIFT, BUT_REC, menu_chainMenuClose().
 */
static uint8_t bh_shiftRecMenuActive  = 0u;
static uint8_t bh_shiftRecSavedPage   = 0u;
static uint8_t bh_shiftRecSavedSubPg  = 0u;
static uint8_t bh_shiftReleaseConsume = 0u;

/*
 * SHIFT+PLAY chord arm (S080 G8).
 *
 * What: 1 between a PLAY press made with SHIFT held in PERF and the PLAY
 * release. Why: SHIFT+PLAY+SEQ opens the chain editor; PLAY in the chord never
 * toggles the transport (plan §4.1). Inputs: PLAY press/release. Outputs:
 * the SHIFT+SEQ PERF branch opens the editor instead of queuing. RAM: +1 byte
 * SRAM1. Affiliates: menu_chainMenuOpen().
 */
static uint8_t bh_chainPlayArmed = 0u;

/*
 * BAR overlay state (S080 E8).
 *
 * What: 0 = no overlay, 1 = LEFT overlay (`<`), 2 = RIGHT overlay (`>`). While
 * an overlay is active and SHIFT is not held, PERF VOICE buttons fire triggers
 * instead of toggling mutes. Why: the overlay converts the VOICE row into a
 * performance trigger row; it is runtime-only (not saved) and is suppressed by
 * the looper. Inputs: BAR1/BAR2 presses in PERF mode, and CC2 on the global
 * MIDI channel G in any mode (S083 follow-up: 0 off, 1 `<`, 2 `>`, through
 * buttonHandler_setOverlay()). It is the ONE overlay state: received MIDI
 * notes use it too, in any mode (MidiNotes). Outputs: VOICE trigger dispatch
 * (E12), MIDI-note overlays and BAR LED state. RAM: +1 byte SRAM1.
 */
static uint8_t bh_overlayActive = 0u;

/*
 * Per-track overlay gesture state (S083 Q5; S084 P2 master-tick timing).
 *
 * What: one entry per track. heldMidiNote: the MIDI note whose note-off
 * ends this track's roll, stored as note + 1 (0 = panel or none, so the
 * zero-initialised table needs no init code). note: internal note, bit 7
 * set when it came from `def` (records no note special, S081 Q12). count:
 * 0 idle, 1 = flam main pending, 0xFF = held roll. rate: the held roll's
 * step-roll index 1..9 (reported with each roll hit so LiveRecord can start
 * a span at any hit, S084 P4). The hit timing, rhythm position and
 * repeats live in the Sequencer's live engine (seq_live*, TIM3); this
 * table only owns the gesture. Why: S080-S083 timed repeats here with a
 * 1 ms timer from PAR_BPM, off the master clock (S084 P0 F4). MIDI notes on
 * several tracks (and several held VOICE buttons) roll at once (S083 Q5).
 * RAM: 7 x 4 B = 28 B SRAM1 static (S084: -28 B; pos, interval and next
 * removed, rate added). Writers: overlay start, drain, end and
 * re-configure below (all foreground). Affiliates: buttonHandler_tick(),
 * MidiNotes, seq_liveRollStart(), seq_liveTakeHits().
 */
typedef struct {
	uint8_t heldMidiNote;
	uint8_t note;
	uint8_t count;
	uint8_t rate;
} bh_overlay_track_t;

#define BH_OVERLAY_NOTE_DEFAULT  0x80u
#define BH_OVERLAY_VEL           100u /* flam main and roll base velocity */
#define BH_OVERLAY_NO_MIDI_NOTE  0u   /* heldMidiNote stores note + 1 */
#define BH_FLAM_TICKS            12u  /* flam main: 1/32 at 96 PPQ */
#define BH_OVERLAY_TYPE_FLAM_FIRST  4u   /* flm, fls */
#define BH_OVERLAY_TYPE_ROLL_FIRST  6u   /* 1/8 .. 128 = step index 1..9 */
#define BH_OVERLAY_TYPE_MAX         14u

static bh_overlay_track_t bh_overlay[NUM_TRACKS];

/* Voice mask of the panel-held overlay triggers (S083 keeps this). */
static uint8_t bh_overlayVoiceMask = 0u;

/*
 * Held overlay trigger type (S081 §5.7, R19, F-B/F-O).
 *
 * What: the trigger type (0..14, S084 P3) of the overlay whose VOICE is currently held,
 * or 0xFF when none. Why: pressing the other BAR while a VOICE is held switches
 * the overlay; the held trigger must adopt the new type/rate, and a roll needs
 * to know whether the previous type was a roll. RAM: +1 byte SRAM1. Affiliates:
 * buttonHandler_overlayTrigger(), buttonHandler_overlaySwitchHeld().
 */
static uint8_t bh_overlayHeldType = 0xFFu;

#define DBLCLICK_FIRST  0u
#define DBLCLICK_DOUBLE 1u

/* -----------------------------------------------------------------------
** Helpers
** ----------------------------------------------------------------------- */
uint8_t buttonHandler_getMode(void)  { return bh_state.selectButtonMode; }
uint8_t buttonHandler_getShift(void) { return (uint8_t)(btn_held[BUT_SHIFT]); }
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

/*
 * Clear every VOICE mute mirror (S080 P2 Defect 11).
 *
 * What: zeroes buttonHandler_mutedVoices and, when the VOICE row is currently
 * showing mute state, repaints it. Why: a queued scene change with a mute
 * reset (qac mut/u+r/mmr/rld/r+r) unmutes the Sequencer through
 * seq_setMute(7, 0), but the VOICE LEDs read this mirror; without the clear
 * they keep showing the old mutes, and the next VOICE release would toggle
 * from the stale state. The repaint is gated on menu_muteModeActive so a
 * queued change firing while another page owns the VOICE row does not
 * overwrite that page's LEDs. Inputs: none. Outputs: mirror cleared, VOICE
 * row repainted when it shows mutes. Accessors: menu_applyQueuedSceneAction().
 * Affiliates: seq_setMute(), buttonHandler_showMuteLEDs(),
 * buttonHandler_muteVoice().
 */
void buttonHandler_clearAllMutes(void)
{
    buttonHandler_mutedVoices = 0u;
    if (menu_muteModeActive)
        buttonHandler_showMuteLEDs();
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

/*
 * Register one button press with the double-click detector (S077 P2 §3.2).
 *
 * What: returns DBLCLICK_DOUBLE when the same button was armed within
 * DOUBLE_CLICK_TIMEOUT; otherwise arms this button and returns DBLCLICK_FIRST.
 * A press of a different button restarts the window for that button. Why: the
 * first click must always fire its single-click action immediately, so the
 * detector never delays a press. Inputs: buttonNr. Output: DBLCLICK_FIRST or
 * DBLCLICK_DOUBLE. A caller running after this can recognise a double by
 * checking (!dblclick_armed && dblclick_button == buttonNr). Uses time_sysTick
 * with the unsigned 16-bit wrap-safe subtraction used elsewhere in this file.
 * Affiliates: dblclick_cancel(), DOUBLE_CLICK_TIMEOUT.
 */
static uint8_t dblclick_onPress(uint8_t buttonNr)
{
    uint8_t result = DBLCLICK_FIRST;

    if (dblclick_armed && dblclick_button == buttonNr &&
        (uint16_t)(dblclick_deadline - time_sysTick) < 32768u) {
        /* Second press of the same button inside the window. */
        result = DBLCLICK_DOUBLE;
        dblclick_armed = 0u;
    } else {
        dblclick_button = buttonNr;
        dblclick_deadline = (uint16_t)(time_sysTick + DOUBLE_CLICK_TIMEOUT);
        dblclick_armed = 1u;
    }
    return result;
}

/*
 * Cancel any pending double-click window (S077 P2 §3.2).
 *
 * What: clears the armed flag so the next press of the previously armed button
 * starts a fresh single-click window. Why: any action other than the same
 * button press cancels the window. Inputs: none. Output: dblclick_armed = 0.
 */
static void dblclick_cancel(void)
{
    dblclick_armed = 0u;
}

/*
 * Copy/Clear row-decoding bridge.
 *
 * Inputs: physical button number. Outputs: the existing private mapping
 * result, exposed read-only to copyClearSession.c so normal routing and held
 * gestures share one hardware map.
 */
int8_t buttonHandler_seqIndex(uint8_t buttonNr)  { return btn_to_seq(buttonNr); }
int8_t buttonHandler_selectIndex(uint8_t buttonNr) { return btn_to_select(buttonNr); }
int8_t buttonHandler_voiceIndex(uint8_t buttonNr) { return btn_to_voice(buttonNr); }


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

#if ENABLE_EUKLID_PAGE
/* Euklid UI compiled out (Session 072 step 7; plan §13.1). */
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
#endif

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
    if (bh_state.selectButtonMode == SELECT_MODE_FX) {
        /*
         * Effect page: the common VOICE hold threshold opens FX lane-lock
         * editing. The timer sentinel consumes the matching release.
         */
        menuEffects_seqHoldExpired();
        return;
    }

    buttonHandler_armedAutomationStep = stepNr;
    led_setBlinkLed((uint8_t)(LED_STEP1 + ((uint8_t)stepNr % NUM_STEPS_PER_BAR)), 1);

}

static void buttonHandler_disarmTimerActionStep(void)
{
    /*
     * Stop the long-press STEP blink (S083: reset-lock remnant removed).
     *
     * What: clears the armed step and stops its STEP LED blink. Why: the
     * reset-lock restore that followed (S028 parser bridge) could never run
     * (buttonHandler_resetLock was never set) and routed through the retired
     * legacy MIDI CC map (plan R12). Inputs: buttonHandler_armedAutomationStep.
     * Outputs: blink stopped, step disarmed. Callers: step release, timer
     * actions. Affiliates: buttonHandler_armTimerActionStep().
     */
    if (buttonHandler_armedAutomationStep != NO_STEP_SELECTED) {
        led_setBlinkLed((uint8_t)(LED_STEP1 + ((uint8_t)buttonHandler_armedAutomationStep % NUM_STEPS_PER_BAR)), 0);
        buttonHandler_armedAutomationStep = NO_STEP_SELECTED;
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

/*
 * PERF-button fill-hold state (S079 §3.3).
 *
 * What: bh_fillArmed is set while the PERF (MODE2) button is held and the
 * fill hold has not yet elapsed; bh_fillPressTick records the press time in
 * the shared millisecond clock. Why: fill mode must activate only after the
 * double-click timeout of continuous hold, so a normal mode-switch tap never
 * triggers fill. Fill activates when the elapsed hold reaches
 * DOUBLE_CLICK_TIMEOUT and deactivates on release. Using time_sysTick (rather
 * than counting tick calls) keeps the hold independent of the main-loop
 * cadence. Inputs: buttonHandler_tick(), handleModeButtons(), processRelease().
 * Outputs: seq_fillActive (read by the TIM3 ISR). RAM: 3 B SRAM1 static.
 */
static uint16_t bh_fillPressTick;
static uint8_t bh_fillArmed;

/*
 * REC hold timer state (S081 §6, B-2b).
 *
 * What: bh_recPressTick records time_sysTick at the REC press; bh_recArmed is 1
 * while waiting for the hold threshold. The elapsed time is computed as
 * (uint16_t)(time_sysTick - bh_recPressTick), exactly like the fill hold. Why:
 * time_sysTick is a volatile uint16_t, so a 16-bit store and a wrap-safe 16-bit
 * subtraction are required. Inputs: set on REC press, cleared on release or
 * threshold. Outputs: read by buttonHandler_tick. RAM: +3 B. Affiliates:
 * bh_fillArmed/bh_fillPressTick.
 */
static uint16_t bh_recPressTick;
static uint8_t  bh_recArmed;

/*
 * BAR overlay trigger helpers (S080 E12; master-tick timing S084 P2).
 *
 * The overlay turns the VOICE row (and MIDI notes, S083) into a trigger row
 * while BAR `<` or `>` is active. Types 0..3 fire one hit at a fixed
 * velocity; 4..5 fire a soft grace hit now and a main hit BH_FLAM_TICKS
 * master ticks later; 6..14 are held rolls at the step-roll rates (index =
 * type - 5, S084 P3). First hits fire here in the foreground; flam mains
 * and roll repeats fire from the Sequencer's live engine on the 96-PPQ
 * master tick (seq_live*, TIM3) and are reported to LiveRecord by
 * buttonHandler_overlayDrain(). The ms timer from PAR_BPM is gone: it ran
 * off the master clock and ignored external sync (S084 P0 F4).
 * Affiliates: handleVoiceButton() (E12), processRelease(),
 * buttonHandler_tick(), MidiNotes, LiveRecord.
 */

/*
 * Velocity rhythm of overlay `which` (S083 follow-up; S084 helper).
 *
 * What: the packed roll-rhythm byte of `pv<` (which 1) or `pv>` (which 2;
 * also returned for 0, where no roll runs). Why: the same lookup was
 * repeated in the tick, start and re-configure paths; S084 also records it
 * (P4). Inputs: which. Output: rhythm byte (0 = off). Accessors:
 * buttonHandler_overlayStart(), _Reconfigure(), _Drain(), _Tick().
 * Affiliates: PAR_PERF_VEL_LT/RT, seq_liveSetRhythm().
 */
static uint8_t buttonHandler_overlayRhythm(uint8_t which)
{
    return parameter_values[(which == 1u) ? PAR_PERF_VEL_LT
                                          : PAR_PERF_VEL_RT];
}

/*
 * Report one overlay hit to LiveRecord (S081 §19.7, E-2a).
 *
 * What: builds a liveRec_trigger_t for the hit (track, note, velocity, kind,
 * roll, rhythm, source, age) and calls liveRec_triggerIn(). Why: LiveRecord
 * captures each hit when it sounds. Affiliates: liveRec_triggerIn().
 * S084: roll is the step-roll index on ROLL_START and on every ROLL_HIT
 * (the held rate), rhythm the active overlay's `pv` byte on roll kinds (0
 * otherwise), so LiveRecord can start or re-rate a span at any hit.
 */
static void buttonHandler_overlayReport(uint8_t voiceNr, uint8_t note,
                                        uint8_t noteIsDefault,
                                        uint8_t velocity, uint8_t kind,
                                        uint8_t roll, uint8_t rhythm,
                                        uint8_t source, uint16_t ageUs)
{
    liveRec_trigger_t t;

    t.track = voiceNr;
    t.note = note;
    t.velocity = velocity;
    t.kind = kind;
    t.roll = roll;
    t.rhythm = rhythm;
    /* S083: LIVEREC_SRC_OVERLAY for panel overlays, LIVEREC_SRC_MIDI for
    ** MIDI-started ones; ageUs is the first MIDI hit's arrival age (0 for
    ** repeats and panel hits). */
    t.source = source;
    t.note_is_default = noteIsDefault;
    t.age_us = ageUs;
    liveRec_triggerIn(&t);
}

/*
 * Report a track's live hits to LiveRecord (S084 P2).
 *
 * What: takes the hits the Sequencer's live engine fired since the last
 * take. A fired flam main is reported as FLAM_MAIN and ends the entry (a
 * MIDI-held flam's note-off already let it finish, B-12). One or more roll
 * hits are reported as ONE ROLL_HIT carrying the held rate and rhythm:
 * LiveRecord only counts roll hits and starts a span at the first hit it
 * sees for a track with no open span. The age is the time since the latest
 * hit (TIM2 µs, capped at 65535), so its placement uses the hit time, not
 * the 1 ms drain time (the S083 Q6 arrival-age path). Why: LiveRecord never
 * runs in TIM3; TIM3 publishes, this foreground step reports. Inputs:
 * track, bh_overlay[track], the live mailbox. Outputs:
 * buttonHandler_overlayReport() calls; the entry ends after a flam main.
 * Context: foreground. Accessors: buttonHandler_overlayTick() (every
 * pass), _EndTrack() and _Start() (final take before a gesture ends),
 * _Reconfigure() (before a rate or type change). Affiliates:
 * seq_liveTakeHits(), liveRec_triggerIn().
 */
static void buttonHandler_overlayDrain(uint8_t track)
{
    bh_overlay_track_t *e = &bh_overlay[track];
    uint32_t stampUs;
    uint32_t age;
    uint8_t flamMain;
    uint8_t hits;
    uint8_t pitch = (uint8_t)(e->note & 0x7Fu);
    uint8_t isDef = (uint8_t)((e->note & BH_OVERLAY_NOTE_DEFAULT) != 0u);
    uint8_t source = (e->heldMidiNote != BH_OVERLAY_NO_MIDI_NOTE)
                     ? LIVEREC_SRC_MIDI : LIVEREC_SRC_OVERLAY;

    if (e->count == 0u)
        return;
    hits = seq_liveTakeHits(track, &stampUs, &flamMain);
    if (hits == 0u && !flamMain)
        return;
    age = timebase_tim2Delta(timebase_tim2Now(), stampUs);
    if (age > 0xFFFFu)
        age = 0xFFFFu;
    if (flamMain) {
        buttonHandler_overlayReport(track, pitch, isDef, BH_OVERLAY_VEL,
                                    LIVEREC_HIT_FLAM_MAIN, 0u, 0u, source,
                                    (uint16_t)age);
        e->count = 0u;
        e->heldMidiNote = BH_OVERLAY_NO_MIDI_NOTE;
        return;
    }
    if (e->count == 0xFFu)
        buttonHandler_overlayReport(track, pitch, isDef, BH_OVERLAY_VEL,
                                    LIVEREC_HIT_ROLL_HIT, e->rate,
                                    buttonHandler_overlayRhythm(
                                        bh_overlayActive),
                                    source, (uint16_t)age);
}

/*
 * Report every track's live hits under the current overlay (S084 P0 A3).
 *
 * What: buttonHandler_overlayDrain() for tracks 0..6. Called right before
 * bh_overlayActive changes (BAR press, CC2, event-ring overflow). Why: the
 * drain reports a roll hit with the active overlay's `pv` rhythm; draining
 * after the change gave hits played under the old overlay the new overlay's
 * rhythm, which a span opened by such a hit would then store. Inputs: the
 * entries, bh_overlayActive (still the old value). Outputs: LiveRecord
 * reports. Context: foreground. Accessors: buttonHandler_setOverlay(), the
 * BAR1/BAR2 presses in processPress(), buttonHandler_processEvents()
 * overflow recovery. Affiliates: buttonHandler_overlayReconfigure() (which
 * drains again, now with nothing left from the old overlay).
 */
static void buttonHandler_overlayDrainAll(void)
{
    uint8_t track;

    for (track = 0u; track < NUM_TRACKS; track++)
        buttonHandler_overlayDrain(track);
}

/*
 * Start an overlay gesture on one track (S080 E12; S083 Q5 per-track;
 * S084 P2 master-tick timing, P3 15 types).
 *
 * What: a new gesture first ends the track's old one: its live entry is
 * stopped, its unreported hits are drained to LiveRecord, and a held roll's
 * recording is ended at this point. Then it reads the trigger type of
 * overlay `which` (1 = BAR <, 2 = BAR >) and fires the first hit:
 *  - 0..3 single: one hit at 127/100/64/32;
 *  - 4..5 flam: grace hit at 64 now, and the main hit (BH_OVERLAY_VEL,
 *    reset) armed BH_FLAM_TICKS master ticks later (seq_liveFlamArm());
 *  - 6..14 roll: hit 0 at BH_OVERLAY_VEL now, then repeats at step-roll
 *    index type - 5 on the master tick, shaped by the overlay's `pv`
 *    rhythm from position 1 (seq_liveRollStart()), until release.
 * The internal note is stored for every type, so a later switch of a held
 * single to a roll plays the right pitch (the S083 entry kept a stale note
 * after a single). Why: S084 P0 F4 (timing) and F1 (types).
 * Inputs: track, which, internal note (bit 7 = `def`), heldMidiNote (MIDI
 * note + 1 that ends a roll, or BH_OVERLAY_NO_MIDI_NOTE), source and age
 * for LiveRecord. Outputs: seq_triggerVoice(), the LiveRecord report, the
 * live entry, bh_overlay[track]. Context: foreground. Callers:
 * buttonHandler_overlayTrigger() (panel), buttonHandler_overlayMidiNoteOn()
 * (MidiNotes). Affiliates: buttonHandler_overlayDrain(), _EndTrack(),
 * seq_liveRollStart(), seq_liveFlamArm(), liveRec_triggerIn().
 */
static void buttonHandler_overlayStart(uint8_t track, uint8_t which,
                                       uint8_t note, uint8_t heldMidiNote,
                                       uint8_t source, uint16_t ageUs)
{
    bh_overlay_track_t *e;
    uint8_t type = (which == 1u) ? parameter_values[PAR_PERF_BTN_LT]
                                 : parameter_values[PAR_PERF_BTN_RT];
    uint8_t pitch = (uint8_t)(note & 0x7Fu);
    uint8_t isDef = (uint8_t)((note & BH_OVERLAY_NOTE_DEFAULT) != 0u);

    if (track >= NUM_TRACKS)
        return;
    e = &bh_overlay[track];
    if (type > BH_OVERLAY_TYPE_MAX)
        type = 0u;
    if (e->count != 0u) {                 /* a new gesture replaces the old */
        uint8_t wasRoll = (uint8_t)(e->count == 0xFFu);

        seq_liveStop(track);
        buttonHandler_overlayDrain(track);
        e->count = 0u;
        if (wasRoll)
            liveRec_rollRelease(track);
    }
    e->note = note;
    if (type < BH_OVERLAY_TYPE_FLAM_FIRST) {
        static const uint8_t singleVel[4] = {127u, 100u, 64u, 32u};

        seq_triggerVoice(track, singleVel[type], pitch, 1);
        buttonHandler_overlayReport(track, pitch, isDef, singleVel[type],
                                    LIVEREC_HIT_SINGLE, 0u, 0u, source, ageUs);
        e->heldMidiNote = BH_OVERLAY_NO_MIDI_NOTE;
        return;
    }
    if (type < BH_OVERLAY_TYPE_ROLL_FIRST) {
        seq_triggerVoice(track, 64u, pitch, 1);
        buttonHandler_overlayReport(track, pitch, isDef, 64u,
                                    LIVEREC_HIT_FLAM_GRACE, 0u, 0u,
                                    source, ageUs);
        seq_liveFlamArm(track, pitch, BH_OVERLAY_VEL, BH_FLAM_TICKS);
        e->count = 1u;
    } else {
        uint8_t idx = (uint8_t)(type - (BH_OVERLAY_TYPE_ROLL_FIRST - 1u));
        uint8_t rhythm = buttonHandler_overlayRhythm(which);

        seq_triggerVoice(track, BH_OVERLAY_VEL, pitch, 1);
        buttonHandler_overlayReport(track, pitch, isDef, BH_OVERLAY_VEL,
                                    LIVEREC_HIT_ROLL_START, idx, rhythm,
                                    source, ageUs);
        seq_liveSetRhythm(rhythm);
        seq_liveRollStart(track, idx, pitch, BH_OVERLAY_VEL, 1u);
        e->rate = idx;
        e->count = 0xFFu;
    }
    e->heldMidiNote = heldMidiNote;
}

/*
 * Panel VOICE overlay press (S080 E12; S083 C-1 `def` note).
 *
 * What: the held VOICE fires the active BAR overlay. A `def` note
 * (perf_note_lt/rt = 0) plays the default pitch (internal 63); before
 * S083 it played the track's MIDI note as a pitch, which made `nte` mean
 * two things (C-1). Inputs: voiceNr, bh_overlayActive, overlay params.
 * Outputs: bh_overlayVoiceMask, bh_overlayHeldType, the track's entry.
 */
static void buttonHandler_overlayTrigger(uint8_t voiceNr)
{
    uint8_t note = (bh_overlayActive == 1u)
                   ? parameter_values[PAR_PERF_NOTE_LT]
                   : parameter_values[PAR_PERF_NOTE_RT];
    uint8_t type = (bh_overlayActive == 1u)
                   ? parameter_values[PAR_PERF_BTN_LT]
                   : parameter_values[PAR_PERF_BTN_RT];

    if (note == 0u)
        note = (uint8_t)(MIDI_DEFAULT_TRIGGER_NOTE | BH_OVERLAY_NOTE_DEFAULT);
    bh_overlayVoiceMask |= (uint8_t)(1u << voiceNr);
    bh_overlayHeldType = (type > BH_OVERLAY_TYPE_MAX) ? 0u : type;
    buttonHandler_overlayStart(voiceNr, bh_overlayActive, note,
                               BH_OVERLAY_NO_MIDI_NOTE,
                               LIVEREC_SRC_OVERLAY, 0u);
}

/*
 * End one track's overlay gesture (S080 E12; S083 per-track).
 *
 * What: stops the track's live entry (no further repeat or flam main can
 * fire), reports the hits that already sounded (so LiveRecord's "released
 * before the second hit" test sees them), sends the voice and MIDI
 * note-off, and ends the track's recorded roll (S084: per track). Panel:
 * only for a VOICE that fired an overlay (bh_overlayVoiceMask). Inputs:
 * voiceNr. Outputs: entry idle, masks. Callers: processRelease() (VOICE
 * up), the looper engage, MIDI note-off (through
 * buttonHandler_overlayEndTrack()). Affiliates: seq_liveStop(),
 * buttonHandler_overlayDrain(), liveRec_rollRelease(),
 * midiOut_trackNoteOff().
 */
static void buttonHandler_overlayEndTrack(uint8_t track)
{
    seq_liveStop(track);
    buttonHandler_overlayDrain(track);
    bh_overlay[track].count = 0u;
    bh_overlay[track].heldMidiNote = BH_OVERLAY_NO_MIDI_NOTE;
    voiceControl_noteOff(track);
    midiOut_trackNoteOff(track);
    liveRec_rollRelease(track);
}

static void buttonHandler_overlayRelease(uint8_t voiceNr)
{
    uint8_t bit = (uint8_t)(1u << voiceNr);

    if ((bh_overlayVoiceMask & bit) == 0u)
        return;
    bh_overlayVoiceMask = (uint8_t)(bh_overlayVoiceMask & ~bit);
    if (bh_overlayVoiceMask == 0u)
        bh_overlayHeldType = 0xFFu;
    buttonHandler_overlayEndTrack(voiceNr);
}

/*
 * Start live erase from the key-press paths (S081 10.1, P1C item 16).
 *
 * What: enables erase (LiveRecord ends an overdub and keeps the prior
 * LATCH/OFF) and lights SHIFT and CLEAR. Entering from off also clears the
 * gesture masks, so a button still held from an earlier session cannot end
 * a gesture of this one. A repeat call while erase is already on (SHIFT
 * pressed again with CLEAR held) keeps the masks. Why: one entry point for
 * both key orders, mirroring buttonHandler_liveEraseEnd(). Affiliates:
 * liveRec_eraseBegin(), BC-3 masks.
 */
static void buttonHandler_liveEraseBegin(void)
{
    if (liveRec_mode() != LIVEREC_MODE_ERASE) {
        bh_eraseSeqMask = 0u;
        bh_eraseFxSeqMask = 0u;
        bh_eraseVoiceMask = 0u;
    }
    liveRec_eraseBegin();
    led_setValue(1u, LED_SHIFT);
    led_setValue(1u, LED_COPY);
}

/*
 * Re-configure active overlay entries after an overlay change (S081 5.7
 * R19/F-B/F-O; S083 per-track; S084 P2 master tick, P3 15 types).
 *
 * What: for every held entry (panel-held or MIDI-held; after a BAR press,
 * a CC2 change or an event-ring overflow), first reports its unreported
 * hits, then applies the S081 rules per track, judging "was a roll" by the
 * entry's own count (0xFF). The drain runs before the held test (S084 P0
 * B1): a MIDI flam whose main fired in TIM3 since the last drain is ended
 * by the drain (heldMidiNote cleared, B-12), so it is judged not held and no
 * roll is started that no note-off could end. Draining an unheld entry only
 * reports what buttonHandler_overlayTick() would report 1 ms later.
 *  - overlay off: the live entry stops, no extra hit, the recorded roll
 *    ends;
 *  - roll -> roll: seq_liveRollSetRate() keeps the time since the last hit
 *    (new interval from the last hit time);
 *  - single/flam -> roll: seq_liveRollStart() starts repeats one interval
 *    from now at rhythm position 1 (the hit already played was hit 0); a
 *    pending flam main is dropped, as before;
 *  - roll -> single/flam: the live entry stops, the recorded roll ends.
 * The new overlay's `pv` rhythm is published to the live engine at once.
 * Why: S084 P0 F4: rates on the master clock. Inputs: which (0 off, 1 <,
 * 2 >), the entries. Outputs: live engine calls, entries, LiveRecord rate
 * changes (rate and rhythm, S084 P4). Context: foreground. Callers:
 * buttonHandler_overlaySwitchHeld() (BAR press, CC2), the event-ring
 * overflow recovery in buttonHandler_processEvents(). Affiliates:
 * buttonHandler_overlayDrain(), seq_liveRollSetRate(), seq_liveRollStart(),
 * seq_liveStop(), liveRec_rollRateChange(), liveRec_rollRelease().
 */
static void buttonHandler_overlayReconfigure(uint8_t which)
{
    uint8_t track;

    for (track = 0u; track < NUM_TRACKS; track++) {
        bh_overlay_track_t *e = &bh_overlay[track];
        uint8_t midiHeld;
        uint8_t panelHeld;
        uint8_t oldRoll;
        uint8_t type;
        uint8_t idx;
        uint8_t rhythm;

        buttonHandler_overlayDrain(track);   /* may end a fired flam (B-12) */
        midiHeld = (uint8_t)(e->heldMidiNote != BH_OVERLAY_NO_MIDI_NOTE);
        panelHeld = (uint8_t)((bh_overlayVoiceMask >> track) & 1u);
        oldRoll = (uint8_t)(e->count == 0xFFu);
        if (!(midiHeld || panelHeld))
            continue;
        if (which == 0u) {
            seq_liveStop(track);
            e->count = 0u;
            liveRec_rollRelease(track);
            continue;
        }
        type = (which == 1u) ? parameter_values[PAR_PERF_BTN_LT]
                             : parameter_values[PAR_PERF_BTN_RT];
        if (type > BH_OVERLAY_TYPE_MAX)
            type = 0u;
        if (type < BH_OVERLAY_TYPE_ROLL_FIRST) {
            seq_liveStop(track);
            e->count = 0u;
            liveRec_rollRateChange(track, 0u, 0u);
            continue;
        }
        idx = (uint8_t)(type - (BH_OVERLAY_TYPE_ROLL_FIRST - 1u));
        rhythm = buttonHandler_overlayRhythm(which);
        seq_liveSetRhythm(rhythm);
        if (oldRoll) {
            seq_liveRollSetRate(track, idx);
        } else {
            /* single/flam -> roll: the hit already played was hit 0. */
            seq_liveRollStart(track, idx, (uint8_t)(e->note & 0x7Fu),
                              BH_OVERLAY_VEL, 1u);
            e->count = 0xFFu;
        }
        e->rate = idx;
        liveRec_rollRateChange(track, idx, rhythm);
    }
}

/*
 * Overlay changed: re-configure the active entries (S081 5.7; S083 Q5).
 *
 * What: the panel-held type follows the new overlay, and every held entry
 * (panel and MIDI) takes it; overlay off stops them. Since the S083
 * follow-up BAR and CC2 set the same state (bh_overlayActive), so there is
 * no BAR-versus-CC2 priority any more (C-9 and the F-3 split retired): the
 * last BAR press or CC2 wins. Inputs: bh_overlayActive,
 * bh_overlayVoiceMask. Outputs: entries via
 * buttonHandler_overlayReconfigure(). Callers: the BAR press in
 * processPress(), buttonHandler_setOverlay() (CC2).
 */
static void buttonHandler_overlaySwitchHeld(void)
{
    /* Panel BAR change: the held type follows the new overlay (S081). */
    if (bh_overlayVoiceMask != 0u) {
        uint8_t type = (bh_overlayActive == 1u)
                       ? parameter_values[PAR_PERF_BTN_LT]
                       : parameter_values[PAR_PERF_BTN_RT];
        bh_overlayHeldType = (bh_overlayActive == 0u) ? 0xFFu
                             : ((type > BH_OVERLAY_TYPE_MAX) ? 0u : type);
    }
    buttonHandler_overlayReconfigure(bh_overlayActive);
}

/*
 * End live erase from the key-release paths (S081 §10.1, P1B items 12/27).
 *
 * What: ends erase (LiveRecord restores the prior LATCH/OFF state), turns
 * SHIFT and CLEAR off, restores the REC LED to the record state, and asks
 * Menu to restart the automation search. Why: one exit point for both key
 * orders. Knob erase removes automation progressively and only from rtk,
 * so the underline must be recomputed by the search at the end rather than
 * dropped at the first turn (MN-5). Affiliates: liveRec_eraseEnd(),
 * menu_patternContentChanged().
 */
static void buttonHandler_liveEraseEnd(void)
{
    liveRec_eraseEnd();
    led_setValue(0u, LED_SHIFT);
    led_setValue(0u, LED_COPY);
    led_setValue((uint8_t)(liveRec_mode() == LIVEREC_MODE_LATCH), LED_REC);
    menu_patternContentChanged();
}

/*
 * Foreground overlay service, every buttonHandler_tick() (S080 E12; S083
 * per-track; S084 P2: timing moved to the master tick).
 *
 * What: publishes the active overlay's velocity rhythm (`pv<` = PAR_PERF_
 * VEL_LT, `pv>` = PAR_PERF_VEL_RT) to the live engine, so a `pv` edit or
 * a BAR/CC2 overlay change shapes every held roll from its next hit, and
 * reports each track's new live hits to LiveRecord
 * (buttonHandler_overlayDrain()). It no longer fires hits: flam mains and
 * roll repeats fire in TIM3 (seq_processLiveTicks()). Why: S084 P0 F4.
 * Inputs: bh_overlayActive, the PERF velocity parameters, the entries.
 * Outputs: seq_liveSetRhythm(), LiveRecord reports. Context: foreground,
 * 1 ms. Accessor: buttonHandler_tick(). Affiliates: seq_liveTakeHits(),
 * seq_rollRhythmVelocity() (applied in TIM3).
 */
static void buttonHandler_overlayTick(void)
{
    uint8_t track;

    seq_liveSetRhythm(buttonHandler_overlayRhythm(bh_overlayActive));
    for (track = 0u; track < NUM_TRACKS; track++)
        buttonHandler_overlayDrain(track);
}

/* Contracts in buttonHandler.h. */
uint8_t buttonHandler_overlayActive(void)
{
    return bh_overlayActive;
}

void buttonHandler_setOverlay(uint8_t which)
{
    /*
     * CC2 on G sets the BAR overlay (S083 follow-up; MIDI_SPEC §5.3).
     *
     * What: the same state change as a BAR `<`/`>` press: bh_overlayActive,
     * the BAR LEDs, and the held entries re-configured. LEDs: only in PERF
     * and while the looper does not own the BAR LEDs (mode entry and loop
     * disengage restore them from bh_overlayActive). A repeated value does
     * nothing. Inputs: which 0..2 (MidiNotes already rejected others).
     * Caller: midiNotes_setNoteAction(). Affiliates: the BAR press in
     * processPress(), buttonHandler_overlaySwitchHeld().
     */
    if (which > 2u || which == bh_overlayActive)
        return;
    buttonHandler_overlayDrainAll();     /* S084 P0 A3: old rhythm */
    bh_overlayActive = which;
    if (bh_state.selectButtonMode == SELECT_MODE_PERF && !seq_loopActive) {
        led_setValue((uint8_t)(which == 1u), LED_BAR1);
        led_setValue((uint8_t)(which == 2u), LED_BAR2);
    }
    buttonHandler_overlaySwitchHeld();
}

void buttonHandler_overlayMidiNoteOn(uint8_t track, uint8_t which,
                                     uint8_t internal, uint8_t midiNote,
                                     uint16_t ageUs)
{
    if (track >= NUM_TRACKS || which == 0u || which > 2u)
        return;
    buttonHandler_overlayStart(track, which,
                               (uint8_t)(internal & 0x7Fu),
                               (uint8_t)((midiNote & 0x7Fu) + 1u),
                               LIVEREC_SRC_MIDI, ageUs);
}

void buttonHandler_overlayMidiNoteOff(uint8_t track, uint8_t midiNote)
{
    uint8_t key = (uint8_t)((midiNote & 0x7Fu) + 1u);
    uint8_t t;

    /*
     * The resolved track first; if it does not hold this note (the map
     * changed between note-on and note-off), the first track that does.
     * A roll ends; a flam keeps its pending main hit (B-12).
     */
    if (track < NUM_TRACKS && bh_overlay[track].heldMidiNote == key) {
        t = track;
    } else {
        for (t = 0u; t < NUM_TRACKS; t++) {
            if (bh_overlay[t].heldMidiNote == key)
                break;
        }
        if (t >= NUM_TRACKS)
            return;
    }
    if (bh_overlay[t].count == 0xFFu)
        buttonHandler_overlayEndTrack(t);
    else
        bh_overlay[t].heldMidiNote = BH_OVERLAY_NO_MIDI_NOTE;
}

void buttonHandler_tick(void)
{
    /* S080 E12 / S084 P2: publish the overlay rhythm and report live hits. */
    buttonHandler_overlayTick();
    /* S080 F9: run a deferred/queued scene change when its boundary has fired. */
    menu_serviceDeferredSceneChange();
    /*
     * PERF fill-hold promotion (S079 §3.3).
     *
     * Inputs: bh_fillArmed/bh_fillPressTick set at PERF press. Output:
     * seq_fillActive is set once the hold reaches DOUBLE_CLICK_TIMEOUT. The
     * unsigned elapsed comparison stays correct across the 16-bit clock wrap.
     */
    if (bh_fillArmed &&
        (uint16_t)(time_sysTick - bh_fillPressTick) >= DOUBLE_CLICK_TIMEOUT &&
        !seq_fillActive)
        seq_fillActive = 1u;

    /*
     * REC hold past DOUBLE_CLICK_TIMEOUT enters overdub (S081 §6, B-2c).
     *
     * What: if bh_recArmed and the hold reaches the threshold, enter overdub and
     * pulse the REC LED; clear bh_recArmed. Why: overdub is a held gesture, not a
     * toggle. Inputs: bh_recPressTick, current tick. Outputs: overdub mode,
     * REC LED pulse. Affiliates: liveRec_overdubBegin, led_pulseLed.
     */
    if (bh_recArmed &&
        (uint16_t)(time_sysTick - bh_recPressTick) >= DOUBLE_CLICK_TIMEOUT) {
        bh_recArmed = 0u;
        liveRec_overdubBegin();
        led_pulseLed(LED_REC);
    }

    /*
     * COPY hold promotion for Bank pages (S082 P0, B2.13d).
     *
     * What: promotes a held Bank-page COPY to the other view at the common
     * hold threshold. Why: S1 tap/hold split. Inputs: press tick, Menu armed
     * state. Outputs: the Menu peek. Affiliates: BUTTON_TIMEOUT,
     * menu_bankCopyHoldReached().
     */
    if (menu_bankCopyArmed() &&
        (uint16_t)(time_sysTick - bh_bankCopyPressTick) >= BUTTON_TIMEOUT)
        menu_bankCopyHoldReached();

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
    /* S081 H-2b (Q13): silently refuse STEP toggles while T != A.
     * S082 P3 D45: also while the viewed Scene is still being loaded. */
    if (liveRec_activePatternLocked() || menu_viewedSceneLoadPending())
        return;
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

/* Forward declaration: defined below with the other PERF gesture handlers. */
static void buttonHandler_perfRefreshHeldSceneLeds(void);

/*
 * Per-track Scene assignment: VOICE held + SEQ pressed (S077 P2 §4.1).
 *
 * What: for every VOICE bit set in perf_heldVoiceMask, calls
 * seq_setTrackPlayedScene(track, scene) and seq_realignTrackToMasterClock(track),
 * then arms preset_startSingleVoiceApply() for the affected slot. Why: the user
 * holds one or more VOICE buttons and presses a SEQ button to assign those
 * tracks to play from the pressed Scene. Inputs: seqButtonPressed 0..15 (the
 * Scene index) and perf_heldVoiceMask. Output: per-track playback state updated,
 * instrument apply armed, perf_voiceActionOccurred set so the subsequent VOICE
 * release does not toggle mute, and the double-click window cancelled. HiHat
 * link: tracks 5 and 6 always switch together inside seq_setTrackPlayedScene()
 * and share slot 5. Validation: the Scene must be available (resident, and
 * not pending in a background Bank load, S082 P3).
 * Affiliates: seq_setTrackPlayedScene(), seq_realignTrackToMasterClock(),
 * preset_startSingleVoiceApply().
 */
static void buttonHandler_perfVoiceHeldSeqPressed(uint8_t seqButtonPressed)
{
    /* S082 P3 D52: per-track moves only to available Scenes (manual change). */
    if (seqButtonPressed >= SCENE_COUNT || seqButtonPressed >= 16u ||
        !bank_sceneAvailable(seqButtonPressed))
        return;
    /* S083 P3: the shared assignment must realign each track (R20). */
    seq_assignTracksToScene(perf_heldVoiceMask, seqButtonPressed);
    /* S083 plan 3.9 P: PC on the moved tracks' channels (panel only). */
    midiOut_trackProgramChange(perf_heldVoiceMask, seqButtonPressed);
    perf_voiceActionOccurred = 1u;
    dblclick_cancel();
    /* Re-blink each held track's (now updated) played Scene (S077 P2 §5.4). */
    buttonHandler_perfRefreshHeldSceneLeds();
}

/*
 * PERF Scene press with double-click detection (S077 P2 §4.2, §4.3).
 *
 * What: handles a SEQ press in PERF mode when no VOICE is held.
 *       PAR_FOLLOW on, single click: scene-level switch via
 *         menu_perfModeSceneButtonPressed() (coalesces all tracks + realigns).
 *       PAR_FOLLOW on, double click: same plus an explicit realign.
 *       PAR_FOLLOW off, single click: view change only via menu_setShownPattern()
 *         + menu_refreshPerfSceneLeds(); playback and per-track overrides stay.
 *       PAR_FOLLOW off, double click: full Scene change + coalesce + realign.
 * Why: PAR_FOLLOW off lets the user navigate the viewed Scene without changing
 * playback; a deliberate double-click then commits the change. Inputs:
 * seqButtonPressed 0..15, parameter_values[PAR_FOLLOW], and the double-click
 * detector state (set by processPress, which calls dblclick_onPress() for this
 * press). Output: Scene/view state updated per the rules above.
 * Affiliates: menu_perfModeSceneButtonPressed(), menu_setShownPattern(),
 * menu_refreshPerfSceneLeds(), seq_realignActivePatternToMasterClock().
 */
static void buttonHandler_perfScenePressed(uint8_t seqButtonPressed)
{
    /* A double is an onPress result of DOUBLE: the window was just consumed. */
    uint8_t isDouble = (uint8_t)(!dblclick_armed &&
                                 dblclick_button == seqButtonPressed);

    /* S082 P3 D52: Scene and view changes only to available Scenes. */
    if (seqButtonPressed >= SCENE_COUNT || seqButtonPressed >= 16u ||
        !bank_sceneAvailable(seqButtonPressed))
        return;

    if (parameter_values[PAR_FOLLOW]) {
        /* A full Scene-level switch coalesces all tracks and always realigns. */
        menu_perfModeSceneButtonPressed(seqButtonPressed);
        if (isDouble)
            seq_realignActivePatternToMasterClock();
    } else {
        /* Single click changes only the viewed Scene, not playback. */
        menu_setShownPattern(seqButtonPressed);
        menu_refreshPerfSceneLeds();
        if (isDouble)
            menu_perfModeSceneButtonPressed(seqButtonPressed);
    }
}

/*
 * Repaint the transient per-track Scene indication for a PERF VOICE hold
 * (S077 P2 §5.4).
 *
 * What: clears every SEQ LED's blink layer and blinks the SEQ LED of each Scene
 * that a currently held VOICE track is playing from. When the last VOICE is
 * released it restores the ordinary PERF Scene row via
 * menu_refreshPerfSceneLeds(). Why: while holding VOICE for per-track
 * assignment the user should see which Scene each held track plays from before
 * changing it; the indication is transient and vanishes on release. Inputs:
 * perf_heldVoiceMask, seq_getTrackPlayedScene(). Output: SEQ LED blink state.
 * Affiliates: menu_refreshPerfSceneLeds().
 */
static void buttonHandler_perfRefreshHeldSceneLeds(void)
{
    uint8_t i;

    for (i = 0u; i < 16u; i++)
        led_setBlinkLed((uint8_t)(LED_SEQ1 + i), 0u);
    for (i = 0u; i < NUM_TRACKS; i++) {
        uint8_t played;

        if ((perf_heldVoiceMask & (uint8_t)(1u << i)) == 0u)
            continue;
        played = seq_getTrackPlayedScene(i);
        if (played < 16u)
            led_setBlinkLed((uint8_t)(LED_SEQ1 + played), 1u);
    }
    if (perf_heldVoiceMask == 0u)
        menu_refreshPerfSceneLeds();
}

/*
 * Report the SELECT LED the PERF looper owns (S080 P2 Defect 4).
 *
 * What: returns the zero-based SELECT index of the active loop division while
 * any looper SELECT is held or the loop is engaged (sustained by VOICE/BAR),
 * otherwise 0xFF. Why: in PERF the SELECT row is the looper's division
 * display and must be dark when the looper is idle; Menu needs this to repaint
 * the row on page changes without owning looper state. Inputs:
 * bh_loopSelectHeldMask, seq_loopActive, bh_loopLastSelectButton. Output:
 * 0..7 or 0xFF. Accessors: menu_syncPerfSelectLed(). Affiliates:
 * led_setActiveSelectButton(), led_clearSelectLeds().
 */
uint8_t buttonHandler_loopSelectLed(void)
{
    if (bh_loopSelectHeldMask != 0u || seq_loopActive)
        return bh_loopLastSelectButton;
    return 0xFFu;
}

/*
 * End the loop when nothing holds it any more (S080 P2 Defect 8; S081 P3 Q1).
 *
 * What: returns while any looper SELECT, any VOICE or any BAR is held.
 * Otherwise disengages the loop (every looped track resumes where it would
 * have been), clears the modifier and track mirror, and restores the SELECT
 * row (dark) and the BAR overlay LEDs. Why: user rule Q1: the loop runs from
 * the first SELECT press until every SELECT, TRACK and BAR button is
 * released (or the mode leaves PERF), with or without record. A held VOICE
 * of a track that is not looped (lop off, or dropped) also holds it.
 * Inputs: bh_loopSelectHeldMask, perf_heldVoiceMask, bh_loopBarHeld.
 * Outputs: seq_loopDisengage(), LEDs. Accessors: SELECT, VOICE and BAR
 * release paths. Affiliates: menu_syncPerfSelectLed(), bh_overlayActive,
 * buttonHandler_perfVoiceReleased().
 */
static void buttonHandler_loopCheckDisengage(void)
{
    if (bh_loopSelectHeldMask != 0u)
        return;
    if (perf_heldVoiceMask != 0u)
        return;
    if (bh_loopBarHeld != 0u)
        return;
    if (seq_loopActive)
        seq_loopDisengage();
    bh_loopTrackMask = 0u;
    bh_loopBarModifier = 0u;
    if (bh_state.selectButtonMode == SELECT_MODE_PERF) {
        menu_syncPerfSelectLed();
        led_setValue((uint8_t)(bh_overlayActive == 1u), LED_BAR1);
        led_setValue((uint8_t)(bh_overlayActive == 2u), LED_BAR2);
    }
}

/*
 * Force the looper off and forget every hold (S080 P2).
 *
 * What: clears the SELECT and BAR hold state, disengages the loop (tracks
 * resume where they would have been) and clears the mirrors. Why: leaving
 * PERF and an event-ring overflow both lose the release edges that would end
 * the loop normally. S081 P3: also clears the PERF VOICE holds
 * (`perf_heldVoiceMask`), which now hold the loop. Accessors:
 * handleModeButtons(), buttonHandler_processEvents() overflow path.
 */
static void buttonHandler_loopForceRelease(void)
{
    bh_loopSelectHeldMask = 0u;
    bh_loopBarHeld = 0u;
    if (seq_loopActive)
        seq_loopDisengage();
    bh_loopTrackMask = 0u;
    bh_loopBarModifier = 0u;
    /*
     * S081 P3 (Q1): the PERF VOICE holds end with PERF. Their releases arrive
     * in the new mode, where the PERF release path does not run, so a bit
     * left set would hold the next loop for ever.
     */
    perf_heldVoiceMask = 0u;
    perf_voiceActionOccurred = 0u;
}

/*
 * PERF VOICE release: single-track mute toggle (S077 P2 §4.1, Q8, Q12).
 *
 * What: clears this VOICE bit from perf_heldVoiceMask. If perf_voiceActionOccurred
 * is 0 (no per-track assignment or unrelated button press during the hold),
 * toggles mute for this single track. Each VOICE button's mute fires on its own
 * release independently (Q12). Why: replaces the old cumulative unmute; the
 * mute toggle is cancelled when the hold produced a per-track assignment or an
 * unrelated button was pressed. Inputs: voiceNr 0..6. Output: mute toggled or
 * not; the mask bit cleared; perf_voiceActionOccurred reset when the last VOICE
 * is released. Affiliates: seq_setMute(), buttonHandler_muteVoice().
 */
static void buttonHandler_perfVoiceReleased(uint8_t voiceNr)
{
    uint8_t bit = (uint8_t)(1u << voiceNr);

    if ((perf_heldVoiceMask & bit) == 0u)
        return;
    perf_heldVoiceMask = (uint8_t)(perf_heldVoiceMask & ~bit);
    /*
     * VOICE release while looping (S080 P2 Defect 8 D1; S081 P3 Q1/Q4).
     *
     * What: a looped track's release drops it from the loop at once (it
     * resumes where it would have been; the others keep their loop starts).
     * Every release while looping is a looper gesture: it never toggles a
     * mute, and the loop ends if nothing holds it any more (any SELECT,
     * VOICE or BAR). Why: Q4, TRACK buttons keep their looper functions
     * while the loop is engaged; Q1, any held TRACK holds the loop, so the
     * release of a non-looped VOICE can be the last hold. Inputs: bit,
     * bh_loopTrackMask, seq_loopActive. Outputs: seq_loopUpdateMask(),
     * possibly seq_loopDisengage(). Affiliates:
     * buttonHandler_loopCheckDisengage().
     */
    if (seq_loopActive) {
        if ((bh_loopTrackMask & bit) != 0u)
            bh_loopTrackMask = seq_loopUpdateMask(
                (uint8_t)(bh_loopTrackMask & (uint8_t)~bit));
        perf_voiceActionOccurred = 1u;
        buttonHandler_loopCheckDisengage();
    }
    if (perf_voiceActionOccurred == 0u) {
        if (buttonHandler_mutedVoices & bit) {
            buttonHandler_muteVoice(voiceNr, 0);
            seq_setMute(voiceNr, 0);
        } else {
            buttonHandler_muteVoice(voiceNr, 1);
            seq_setMute(voiceNr, 1);
        }
        /* Repaint the VOICE mute row deterministically in PERF mode. */
        buttonHandler_showMuteLEDs();
    }
    if (perf_heldVoiceMask == 0u)
        perf_voiceActionOccurred = 0u;
    buttonHandler_perfRefreshHeldSceneLeds();
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
        case SELECT_MODE_PERF:
            /*
             * S080 F9 / Phase G / P2 Defect 10: SHIFT+SEQ in PERF.
             *
             * What: with PLAY held in the SHIFT+PLAY chord, opens the chain
             * editor for this Scene (Phase G). Otherwise queues a change to
             * this Scene at the qcd division. The queued Scene's SEQ LED
             * flashes at double rate until the change fires; that LED is
             * owned by menu_syncQueuedSceneLed() on the next tick, so no blink
             * is started here. Why: queuing lets the user pre-program the
             * next Scene during performance; the chord shares the SHIFT+SEQ
             * gesture space. Inputs: seqButtonPressed, bh_chainPlayArmed,
             * PAR_SCENE_QUEUE_DIV. Outputs: menu_chainMenuOpen() or
             * seq_queueSceneChange(). Affiliates: seq_queueSceneChange()
             * (Step 6), menu_serviceDeferredSceneChange(), menu_chainMenuOpen()
             * (Step 11).
             */
            if (bh_chainPlayArmed) {
                (void)menu_chainMenuOpen(seqButtonPressed);
                break;
            }
            /* S082 P3 D52: queue only an available Scene; availability only
             * grows during a load, so the target is still valid when the
             * change fires. */
            if (seqButtonPressed < SCENE_COUNT &&
                bank_sceneAvailable(seqButtonPressed)) {
                seq_queueSceneChange(seqButtonPressed,
                                     parameter_values[PAR_SCENE_QUEUE_DIV]);
            }
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
            /*
             * Re-press of the selected step toggles the two step pages
             * (S079 P2 fix 1).
             *
             * What: a press on the step that is already selected while the
             * STEP edit page is showing alternates SEQ subpage 1 and 2; any
             * other press selects the step and opens subpage 1 as before. Why:
             * the roll settings must be reachable from the step row without
             * the scroll gesture. Inputs: the visible step, the selected step,
             * and the current subpage. Output: menu_toggleStepEditSubpage() or
             * buttonHandler_selectActiveStep(). Affiliates:
             * menu_toggleStepEditSubpage(), menu_showStepEditPage().
             */
            if (buttonHandler_visibleStep(seqButtonPressed) ==
                    buttonHandler_selectedStep &&
                menu_activePage == SEQ_PAGE &&
                (menu_getSubPage() == 1u || menu_getSubPage() == 2u)) {
                menu_toggleStepEditSubpage();
            } else {
                led_clearAllBlinkLeds();
                buttonHandler_selectActiveStep(ledNr, seqButtonPressed);
            }
            break;
        case SELECT_MODE_PERF:
            /*
             * PERF SEQ press routing (S077 P2 §4.1, §4.2; S080 P2 Defect 9,
             * Phase G).
             *
             * What: while the chain editor is open, a SEQ press selects which
             * Scene's chain is viewed (no scene change). Otherwise a held
             * VOICE routes to per-track assignment, unless the looper is
             * engaged: then SEQ is always a full scene change. Without a held
             * VOICE the press is a scene-level switch with double-click
             * detection. Why: during a loop the VOICE buttons choose looped
             * tracks, so per-track assignment would hijack every scene change
             * (Defect 9). Inputs: seqButtonPressed, perf_heldVoiceMask,
             * seq_loopActive, menu_chainMenuIsOpen(). Outputs: chain view
             * switch, per-track assignment or scene change. Affiliates:
             * menu_chainMenuView() (Step 11), buttonHandler_perfVoiceHeldSeqPressed(),
             * buttonHandler_perfScenePressed().
             */
            if (menu_chainMenuIsOpen()) {
                menu_chainMenuView(seqButtonPressed);
                break;
            }
            if (perf_heldVoiceMask != 0u && !seq_loopActive)
                buttonHandler_perfVoiceHeldSeqPressed(seqButtonPressed);
            else
                buttonHandler_perfScenePressed(seqButtonPressed);
            break;
        case SELECT_MODE_FX:
            /*
             * FX SEQ press: `sel` jumps immediately; all modes arm the
             * common hold timer unless an existing lock hold owns the row.
             */
            menuEffects_seqButtonPressed(seqButtonPressed);
            if (menuEffects_seqHoldActive())
                buttonHandler_buttonTimerStepNr = TIMER_ACTION_OCCURED;
            else
                buttonHandler_setTimeraction(seqButtonPressed);
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

    case SELECT_MODE_FX:
        /* Hold-owned releases are consumed; a short tap only clears its timer. */
        if (menuEffects_seqHoldActive()) {
            buttonHandler_buttonTimerStepNr = TIMER_ACTION_OCCURED;
            return;
        }
        (void)buttonHandler_TimerActionOccured();
        break;

    default:
        break;
    }
}

/* Forward declarations for the latched SHIFT+REC menu helpers (S080 E7/P2). */
static void buttonHandler_shiftRecMenuEnter(void);
static void buttonHandler_shiftRecMenuExit(void);

/*
 * Close every latched front-panel surface before a mode change (S080 P2,
 * S081, S082 P1 D7/D32).
 *
 * What: closes the SHIFT+REC overlay (restoring its saved page only when
 * restore_page is set), closes the chain editor, ends held live-erase
 * gestures, and releases the looper when the mode leaves PERF.
 * Why: one implementation for the MODE press and the MIDI-origin Bank load,
 * which must leave the panel exactly as a MODE press would (D7); the MIDI
 * path does not restore the SHIFT+REC page (R8) because the Bank entry
 * follows at once.
 * Inputs: restore_page, leaving_perf. Outputs: latches closed, looper and
 * erase holds released.
 * Accessors: handleModeButtons(), buttonHandler_enterLoadSaveForBackgroundLoad().
 * Affiliates: buttonHandler_shiftRecMenuExit(), menu_chainMenuClose(),
 * liveRec_eraseReleaseHeld(), buttonHandler_loopForceRelease().
 */
static void buttonHandler_closeLatchedSurfaces(uint8_t restore_page,
                                               uint8_t leaving_perf)
{
    if (bh_shiftRecMenuActive) {
        if (restore_page) {
            buttonHandler_shiftRecMenuExit();
        } else {
            bh_shiftRecMenuActive = 0u;
            led_setBlinkLed(LED_REC, 0u);
            led_setValue((uint8_t)(liveRec_mode() == LIVEREC_MODE_LATCH),
                         LED_REC);
            led_setValue(buttonHandler_getShift(), LED_SHIFT);
        }
    }
    if (menu_chainMenuIsOpen())
        menu_chainMenuClose();
    if (liveRec_mode() == LIVEREC_MODE_ERASE) {
        liveRec_eraseReleaseHeld();
        bh_eraseSeqMask = 0u;
        bh_eraseFxSeqMask = 0u;
        bh_eraseVoiceMask = 0u;
    }
    if (leaving_perf)
        buttonHandler_loopForceRelease();
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
    /*
     * A locked background Bank load owns the panel (S082 P1 R7, D7;
     * S082 P3 D38, D40).
     *
     * What: every MODE press is dropped while the load is locked (phases 1-4
     * and 6, or a load requested stopped). Once it is unlocked MODE works
     * normally; Menu shows the frozen Load:[Bank] screen for Load/Save.
     * Why: the UI stays on Load:[Bank] until the load ends (D7); a queued exit
     * would fire right after the load, and an applied one would leave the page
     * under a running load.
     * Inputs: preset_backgroundBankLoadLocked(). Outputs: none.
     * Accessors: MODE button presses.
     * Affiliates: menu_switchPage() backstop, preset_tickBackgroundBankLoad().
     */
    if (preset_backgroundBankLoadLocked())
        return;
    {
        uint8_t next;

        if (buttonHandler_getShift() && mode == SELECT_MODE_VOICE)
            next = SELECT_MODE_VOICE;
        else if (buttonHandler_getShift())
            next = (uint8_t)((mode + 4u) & 0x07u);
        else
            next = (uint8_t)(mode & 0x07u);
        buttonHandler_closeLatchedSurfaces(
            1u,
            (uint8_t)(bh_state.selectButtonMode == SELECT_MODE_PERF &&
                      next != SELECT_MODE_PERF));
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
        /* Arm the fill hold timer (S079 §3.3); fill activates after the hold. */
        bh_fillArmed = 1u;
        bh_fillPressTick = time_sysTick;
        led_clearSequencerLeds();
        led_clearSelectLeds();
        lastActiveSubPage = menu_getSubPage();
        menu_switchPage(PERFORMANCE_PAGE);
        menu_switchSubPage(0);
        /*
         * S080 A2: reset the within-subpage cursor on PERF entry.
         *
         * What:      calls menu_resetActiveParameter() after the subpage
         *            switch so the display opens on cell 0 (mrp) of
         *            PERFORMANCE_PAGE subpage 0.
         * Why:       the old single-subpage layout used menu_switchSubPage(0)'s
         *            same-subpage toggle to jump between the first and second
         *            four-cell halves, so a fresh PERF entry could land on cell
         *            4 (4VM) instead of mrp. The S080 A1 split removes that
         *            second half, and this explicit reset guarantees the cursor
         *            is zero regardless of the prior page's menuIndex.
         * Inputs:    none; reads/writes menuIndex internally.
         * Outputs:   menuIndex parameter bits set to cell 0 of the active
         *            subpage.
         * Accessors: handleModeButtons() SELECT_MODE_PERF case (here);
         *            menu_resetActiveParameter() (menu.c).
         * Affiliates: PERFORMANCE_PAGE table (menuPages.h A1).
         */
        menu_resetActiveParameter();
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

    case SELECT_MODE_FX:
        /* SHIFT+PERF enters the Effect page and clears stale Pattern LEDs. */
        led_clearSequencerLeds();
        led_clearSelectLeds();
        menu_switchPage(EFFECT_PAGE);
        break;

    case SELECT_MODE_SOM_GEN:
        menu_switchPage(SOM_PAGE);
        break;

    default:
        break;
    }
    /*
     * BAR overlay LEDs shown only in PERF mode (S081 §5.7, R19).
     *
     * What: when the new mode is not PERF, both BAR LEDs are forced off; when it
     * is PERF they are restored from bh_overlayActive, the same expressions as
     * buttonHandler_loopCheckDisengage(). bh_overlayActive itself is not changed,
     * so the overlay is still active on PERF re-entry. Why: the toggled BAR LED
     * used to stay lit in modes where the BAR LEDs are only press feedback.
     */
    if (bh_state.selectButtonMode == SELECT_MODE_PERF) {
        led_setValue((uint8_t)(bh_overlayActive == 1u), LED_BAR1);
        led_setValue((uint8_t)(bh_overlayActive == 2u), LED_BAR2);
    } else {
        led_setValue(0u, LED_BAR1);
        led_setValue(0u, LED_BAR2);
    }
}

static void handleSelectButton(uint8_t selectNr)
{
    if (buttonHandler_getShift()) {
        switch (bh_state.selectButtonMode) {
        case SELECT_MODE_STEP:
            /*
             * SHIFT+SELECT in STEP mode: set the active track's pattern
             * length to the end of the bar represented by this SELECT button.
             *
             * What:       the active track's track_length is set to
             *             (selectNr + 1) * NUM_STEPS_PER_BAR, so SELECT 1
             *             gives 16 steps (one bar), SELECT 8 gives 128 steps
             *             (eight bars). The viewed bar clamps downward if it
             *             would exceed the new length, and the STEP/SELECT LEDs
             *             and the menu parameter display repaint.
             * Why:        provides a fast physical gesture for setting the
             *             per-track loop length from the front panel, matching
             *             the natural SELECT=bar mapping in STEP mode.
             * Inputs:     selectNr 0..7 from handleSelectButton(); the active
             *             Scene is menu_getViewedPattern(); the active track is
             *             menu_getActiveVoice().
             * Outputs:    pat_setTrackLength() commits the value and marks the
             *             Scene dirty (AutoSave). PAR_TRACK_LENGTH is refreshed
             *             by pat_applyTrackSettingsToMenu(). If the viewed bar
             *             was beyond the new length, buttonHandler_selectBar()
             *             clamps it, updating menu_currentBar, selectedStep, and
             *             repainting STEP/SELECT LEDs. menu_repaintAll()
             *             redraws the current LCD page.
             * Accessors:  menu_getViewedPattern(), menu_getActiveVoice(),
             *             menu_currentBar, menu_repaintAll().
             * Affiliates: pat_setTrackLength() (PatternData.c:1106),
             *             pat_applyTrackSettingsToMenu() (PatternData.c:1093),
             *             buttonHandler_selectBar() (buttonHandler.c:398),
             *             seq_advanceTrackStep() reads track_length at
             *             playback (sequencer.c:908).
             */
        {
            uint8_t newLen = (uint8_t)((selectNr + 1u) * NUM_STEPS_PER_BAR);
            uint8_t scene  = menu_getViewedPattern();
            uint8_t track  = menu_getActiveVoice();

            pat_setTrackLength(scene, track, newLen);

            /* Snap the viewed bar back into range when the new length is
             * shorter than the bar the user was viewing. */
            if (menu_currentBar >= (uint8_t)(selectNr + 1u))
                buttonHandler_selectBar(selectNr);

            pat_applyTrackSettingsToMenu(scene, track);
            menu_repaintAll();
        }
            break;

        case SELECT_MODE_VOICE:
            buttonHandler_selectBar(selectNr);
            break;

        case SELECT_MODE_FX:
            /*
             * SHIFT+SELECT is reserved for optional type UI hooks. S074:
             * CrumpBit resets that data line to normal and asks for its home
             * screen. Under the voice-mix overlay the hook still owns the press,
             * but default Effect screen navigation is suppressed until TRACK
             * release.
             */
            if (menuEffects_hookSelect(selectNr, 1u, 1u) ==
                EFFECT_UI_SHOW_HOME)
                menu_effectShowHome();
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

    case SELECT_MODE_FX: {
        /*
         * Let the active Effect type consume SELECT before default
         * navigation. S074: EFFECT_UI_SHOW_HOME means the type handled the
         * press and the page must show its home screen (CrumpBit toggles a
         * data line, then shows the overlay). menu_effectShowHome() repaints
         * and re-renders the LEDs. Default navigation routes through the type
         * owner check, which is the active SELECT LED for other types.
         */
        const uint8_t fx_action =
            menuEffects_hookSelect(selectNr, 0u, 1u);

        if (fx_action != 0u) {
            if (fx_action == EFFECT_UI_SHOW_HOME)
                menu_effectShowHome();
            break;
        }
        if (menu_fxVoiceMixOverlayActive())
            break;
        menu_switchSubPage(selectNr);
        menuEffects_renderSelectLeds(menu_getSubPage());
        menu_repaintAll();
        break; }

    case SELECT_MODE_PERF:
    {
        /*
         * S080 C4 / P2: SELECT buttons are the PERF performance looper.
         *
         * What:       SELECT1..8 select the loop division (0 = 1 bar ..
         *             7 = 1/128). The first press engages the loop: on the
         *             held VOICE tracks (VOICE then SELECT) or on all seven
         *             tracks, filtered by the loopable flags. Later presses
         *             only change the division (last pressed wins) - loop
         *             starts never move (Defect 7). While the transport is
         *             stopped the press only lights its LED (D17).
         * Why:        SELECT is otherwise unused in PERF.
         * Inputs:     selectNr 0..7, perf_heldVoiceMask, seq_loopActive.
         * Outputs:    seq_loopEngage()/seq_loopUpdateDivision(); the pressed
         *             SELECT LED lit; BAR LEDs off; the record mode is kept
         *             (S081 P3, L-2).
         * Accessors:  handleSelectButton() PERF case (here); the release half
         *             is buttonHandler_partButtonReleased().
         * Affiliates: seq_loopEngage(), buttonHandler_loopCheckDisengage().
         */
        uint8_t division = selectNr;

        bh_loopSelectHeldMask |= (uint8_t)(1u << selectNr);
        bh_loopLastSelectButton = selectNr;
        if (!seq_loopActive) {
            uint8_t held = (uint8_t)(perf_heldVoiceMask | bh_overlayVoiceMask);
            uint8_t trackMask = (held != 0u) ? held : 0x7Fu;

            bh_loopBarModifier = 0u;
            bh_loopTrackMask = seq_loopEngage(trackMask, division,
                                              bh_loopBarModifier);
            /*
             * Held VOICEs at a successful engage (S081 P3 FQ2, Q4).
             *
             * What: only if the loop engaged (transport running):
             *  1. every VOICE still firing an overlay trigger ends that
             *     gesture as its release would (buttonHandler_overlayRelease():
             *     repeats stop, note-off, liveRec_rollRelease() writes the
             *     recorded roll's end; a flam still waiting for its main hit
             *     loses it, as on an early release);
             *  2. those VOICEs become ordinary looper TRACK holds
             *     (perf_heldVoiceMask): their tracks are already in the
             *     engage mask above, they hold the loop, and their release
             *     drops the track with no mute toggle and no second overlay
             *     release;
             *  3. every held VOICE is marked as a looper gesture
             *     (perf_voiceActionOccurred), so no release toggles a mute
             *     while looping (Q4).
             * The record mode is left as it is (L-2; the P1 interim looper
             * force-off call is removed). With the transport stopped the
             * engage does nothing and the overlay gesture continues. Why: user
             * answer FQ2 ("roll ends when loop starts, track hold acts as
             * normal for loop operation"). Inputs: bh_overlayVoiceMask,
             * perf_heldVoiceMask, seq_loopActive. Outputs: overlay ended,
             * hold masks. Affiliates: buttonHandler_overlayRelease(),
             * liveRec_rollRelease(), buttonHandler_perfVoiceReleased().
             */
            if (seq_loopActive) {
                uint8_t overlay = bh_overlayVoiceMask;
                uint8_t v;

                for (v = 0u; v < NUM_TRACKS; v++) {
                    if ((overlay & (uint8_t)(1u << v)) != 0u)
                        buttonHandler_overlayRelease(v);
                }
                perf_heldVoiceMask |= overlay;
                if (perf_heldVoiceMask != 0u)
                    perf_voiceActionOccurred = 1u;
            }
        } else {
            seq_loopUpdateDivision(division, bh_loopBarModifier);
        }
        led_setActiveSelectButton(selectNr);
        /* S080 §3.3: both BAR LEDs are forced off while the looper runs. */
        led_setValue(0u, LED_BAR1);
        led_setValue(0u, LED_BAR2);
        break;
    }

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
    handleSelectButton(partNr);
}

static void buttonHandler_partButtonReleased(uint8_t partNr)
{
    /*
     * S080 C6 / P2 Defect 8: PERF looper SELECT release.
     *
     * What:      clears this SELECT's hold bit. If other SELECTs remain, the
     *            division moves to the highest remaining one when the
     *            released button owned it. When the last SELECT is released
     *            the loop ends only if no VOICE and no BAR is held (S081 P3
     *            Q1);
     *            otherwise it keeps running and the SELECT row keeps showing
     *            its division.
     * Why:       the loop is held by SELECT, VOICE or BAR holds (Defect 8;
     *            S081 P3 Q1: any VOICE). SHIFT+SELECT never sets a hold bit,
     *            so its release skips this block.
     * Inputs:    partNr 0..7, bh_loopSelectHeldMask.
     * Outputs:   seq_loopUpdateDivision(), possibly seq_loopDisengage(),
     *            SELECT LED.
     * Accessors: processRelease() SELECT routing.
     * Affiliates: buttonHandler_loopCheckDisengage(), menu_syncPerfSelectLed().
     */
    if (bh_state.selectButtonMode == SELECT_MODE_PERF &&
        (bh_loopSelectHeldMask & (uint8_t)(1u << partNr)) != 0u) {
        uint8_t bit = (uint8_t)(1u << partNr);

        bh_loopSelectHeldMask = (uint8_t)(bh_loopSelectHeldMask & ~bit);
        if (bh_loopSelectHeldMask != 0u) {
            if (partNr == bh_loopLastSelectButton) {
                uint8_t i;

                for (i = 8u; i > 0u; i--) {
                    if ((bh_loopSelectHeldMask &
                         (uint8_t)(1u << (i - 1u))) != 0u) {
                        bh_loopLastSelectButton = (uint8_t)(i - 1u);
                        break;
                    }
                }
            }
            if (seq_loopActive)
                seq_loopUpdateDivision(bh_loopLastSelectButton,
                                       bh_loopBarModifier);
            led_setActiveSelectButton(bh_loopLastSelectButton);
        } else {
            buttonHandler_loopCheckDisengage();
            if (seq_loopActive)
                led_setActiveSelectButton(bh_loopLastSelectButton);
        }
    }

    if (buttonHandler_TimerActionOccured())
        return;

    buttonHandler_buttonTimerStepNr = NO_STEP_SELECTED;
}

/*
 * SHIFT+REC settings menu entry/exit (S080 E7).
 *
 * What: buttonHandler_shiftRecMenuEnter() saves the current page and sub-page,
 * switches to the repurposed RECORDING_PAGE on sub-page 0 with the cursor at
 * cell 0, arms bh_shiftRecMenuActive, lights both SHIFT and REC LEDs, and
 * repaints. buttonHandler_shiftRecMenuExit() clears the flag, restores the
 * saved page/sub-page, returns the REC LED to the record state (and SHIFT to
 * its held state), and repaints. Why: the SHIFT+REC overlay must not disturb
 * normal SHIFT (Morph view / pattern settings) or REC (record toggle)
 * behaviour, so state is saved and restored. Inputs: current Menu page/sub-page
 * and the record/shift state. Outputs: page switch + LED state.
 * Accessors: processPress() BUT_REC (entry/exit) and BUT_SHIFT (exit; S080 P2
 * Defect 3: the menu latches open and SHIFT release never exits),
 * handleModeButtons() (exit before a mode switch). Affiliates:
 * menu_switchPage(), menu_switchSubPage(), menuPages.h RECORDING_PAGE (E3).
 */
static void buttonHandler_shiftRecMenuEnter(void)
{
    /* SHIFT+REC is dropped while a locked background Bank load owns the
     * panel (S082 P1 R7); an unlocked load allows it (S082 P3 W3). */
    if (preset_backgroundBankLoadLocked())
        return;
    bh_shiftRecSavedPage = menu_getActivePage();
    bh_shiftRecSavedSubPg = menu_getSubPage();
    menu_switchPage(RECORDING_PAGE);
    menu_switchSubPage(0u);
    menu_resetActiveParameter();
    bh_shiftRecMenuActive = 1u;
    led_setValue(1u, LED_SHIFT);
    /*
     * REC LED flashes while the settings menu is open and armed (S081 §6,
     * B-2f). With the menu latched, REC closes the menu instead of toggling
     * record, so the flashing LED shows that record is still armed.
     */
    if (liveRec_armed())
        led_setBlinkLed(LED_REC, 1u);
    else
        led_setValue(1u, LED_REC);
    menu_repaintAll();
}

static void buttonHandler_shiftRecMenuExit(void)
{
    bh_shiftRecMenuActive = 0u;
    menu_switchPage(bh_shiftRecSavedPage);
    menu_switchSubPage(bh_shiftRecSavedSubPg);
    menu_resetActiveParameter();
    led_setBlinkLed(LED_REC, 0u);
    led_setValue((uint8_t)(liveRec_mode() == LIVEREC_MODE_LATCH), LED_REC);
    led_setValue(buttonHandler_getShift(), LED_SHIFT);
    menu_repaintAll();
}

/*
 * Put the front panel into Load/Save for a MIDI-origin Bank load
 * (S082 P1 D7, D32).
 *
 * What: closes every latched surface without restoring the SHIFT+REC page
 * (R8), ends an open COPY/CLEAR session (nothing is applied), cancels the
 * double-click window, leaves Morph voice mode, and sets the Load/Save mode
 * with its mode LEDs; BAR LEDs go dark (non-PERF rule). The Menu page change
 * is Menu's (menu_backgroundBankLoadUiBegin()).
 * Why: a MIDI Bank load takes the UI exactly as if the user had pressed MODE
 * to Load/Save and OK (D7), abandoning any Menu state (D32).
 * Inputs: bh_state, latch flags, copy/clear session.
 * Outputs: bh_state.selectButtonMode = SELECT_MODE_LOAD_SAVE; LEDs.
 * Accessors: menu_backgroundBankLoadUiBegin().
 * Affiliates: buttonHandler_closeLatchedSurfaces(), copyClear_copyReleased(),
 * dblclick_cancel(), buttonHandler_setMorphVoiceMode().
 */
void buttonHandler_enterLoadSaveForBackgroundLoad(void)
{
    buttonHandler_closeLatchedSurfaces(
        0u, (uint8_t)(bh_state.selectButtonMode == SELECT_MODE_PERF));
    copyClear_copyReleased();
    dblclick_cancel();
    buttonHandler_setMorphVoiceMode(0u);
    bh_state.selectButtonMode = SELECT_MODE_LOAD_SAVE;
    led_clearAllBlinkLeds();
    led_setMode2(SELECT_MODE_LOAD_SAVE);
    led_setValue(0u, LED_BAR1);
    led_setValue(0u, LED_BAR2);
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

    /*
     * PERF VOICE button press: per-track hold arm (S077 P2 §4.1, Q8).
     *
     * What: sets this VOICE's bit in perf_heldVoiceMask and clears any pending
     * double-click window. While any VOICE bit is set, SEQ presses become
     * per-track assignments instead of Scene switches, and the mute toggle is
     * deferred to release. Why: this replaces the old cumulative unmute
     * (tracks 0..N, SHIFT+VOICE) and the immediate press-time single-track mute
     * so one feature owns the PERF VOICE gesture: a press arms the hold, a SEQ
     * press during the hold assigns playback, and a plain release toggles this
     * track's mute. Inputs: voiceNr 0..6. Output: perf_heldVoiceMask updated.
     * Affiliates: buttonHandler_perfVoiceReleased(),
     * buttonHandler_perfVoiceHeldSeqPressed().
     */
    if (bh_state.selectButtonMode == SELECT_MODE_PERF) {
        /*
         * VOICE press while the loop is engaged (S080 D1/P2 Defect 7;
         * S081 P3 Q4/FQ1).
         *
         * What: the press is a looper gesture, before the overlay trigger,
         * the mute arm and (via BH-7) the erase router:
         *  - a SELECT held: the loop moves to the held VOICE set (swap,
         *    S080 D1): leaving tracks resume, staying ones keep their loop
         *    starts, new ones get a fresh start;
         *  - no SELECT held: this track joins the loop with a fresh loop
         *    start (FQ1); tracks already looping keep theirs. A track the
         *    looper may not capture (lop off) does not join.
         * Either way the VOICE holds the loop (Q1), its release drops the
         * track (S080 D1), and it never fires an overlay trigger, toggles a
         * mute or starts an erase gesture (Q2/Q4). SHIFT+VOICE behaves the
         * same. Inputs: voiceNr, seq_loopActive, bh_loopSelectHeldMask,
         * perf_heldVoiceMask, bh_loopTrackMask. Outputs:
         * seq_loopUpdateMask(), perf_heldVoiceMask, perf_voiceActionOccurred.
         * Affiliates: buttonHandler_perfVoiceReleased(),
         * seq_loopableTrackMask().
         */
        if (seq_loopActive) {
            uint8_t bit = (uint8_t)(1u << voiceNr);

            perf_heldVoiceMask |= bit;
            perf_voiceActionOccurred = 1u;
            dblclick_cancel();
            if (bh_loopSelectHeldMask != 0u)
                bh_loopTrackMask = seq_loopUpdateMask(perf_heldVoiceMask);
            else
                bh_loopTrackMask = seq_loopUpdateMask(
                    (uint8_t)(bh_loopTrackMask | bit));
            return;
        }
        /*
         * S080 E12: BAR overlay trigger dispatch.
         *
         * What:      while a BAR overlay is active and SHIFT is not held, a
         *            VOICE down-press fires the overlay's configured trigger
         *            for that track instead of arming a mute toggle. Why: the
         *            overlay converts the VOICE row into a performance trigger
         *            row. SHIFT+VOICE falls through to the normal PERF
         *            behaviour (mute toggle on release). Inputs:
         *            bh_overlayActive, buttonHandler_getShift(), voiceNr.
         *            Outputs: buttonHandler_overlayTrigger(); the release half
         *            is handled in processRelease(). Affiliates:
         *            bh_overlayActive (E8), the overlay params (E1).
         *            Not reached while the loop is engaged (S081 P3 Q2/Q4:
         *            the loop pre-empts the overlay).
         */
        if (bh_overlayActive != 0u && !buttonHandler_getShift()) {
            buttonHandler_overlayTrigger(voiceNr);
            return;
        }
        perf_heldVoiceMask |= (uint8_t)(1u << voiceNr);
        dblclick_cancel();
        /*
         * Transient per-track Scene indication during the hold (S077 P2 §5.4):
         * blink the SEQ LED of the Scene each held track is playing from.
         */
        buttonHandler_perfRefreshHeldSceneLeds();
        return;
    }

    {
        uint8_t muteModeActive = buttonHandler_getShift();
        if (bh_state.selectButtonMode == SELECT_MODE_PERF ||
            bh_state.selectButtonMode == SELECT_MODE_FX)
            muteModeActive = (uint8_t)(1u - muteModeActive);

        if (bh_state.selectButtonMode == SELECT_MODE_FX &&
            menuEffects_hookTrack(voiceNr, buttonHandler_getShift(), 1u))
            return;

        if (buttonHandler_fxVoiceMixTrackMask != 0u) {
            /*
             * TRACK press while the Effect-page voice mix overlay is held
             * (S075 F2-F follow-up, user).
             *
             * What: once SHIFT+TRACK has opened the overlay, every TRACK
             * press (with or without SHIFT) joins the held set, and on the
             * Effect page the Scene-settings screen switches to the track
             * pressed last; the active track follows it, as SHIFT+TRACK does.
             * No TRACK press mutes until every held TRACK is released and the
             * overlay is gone, so this branch runs before the mute path and
             * consumes the press in every mode (a mode switch while TRACKs
             * are held ends the overlay, but the held TRACKs still block mute
             * until released). The Effect type's TRACK hook above keeps
             * priority. Inputs: voiceNr, the held mask, SHIFT. Outputs: mask
             * bit set (its release is consumed in processRelease()), overlay
             * moved, active voice and LEDs. Affiliates:
             * menu_fxVoiceMixOverlayBegin(), processRelease().
             */
            buttonHandler_fxVoiceMixTrackMask = (uint8_t)(
                buttonHandler_fxVoiceMixTrackMask | (uint8_t)(1u << voiceNr));
            if (bh_state.selectButtonMode == SELECT_MODE_FX) {
                menu_setActiveVoice(voiceNr);
                buttonHandler_showMuteLEDs();
                led_flashLed((uint8_t)(LED_VOICE1 + voiceNr));
                (void)menu_fxVoiceMixOverlayBegin(voiceNr);
                if (shouldPreviewVoice)
                    seq_previewVoice(voiceNr);
            }
            return;
        }

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

        if (bh_state.selectButtonMode == SELECT_MODE_FX) {
            /*
             * SHIFT+TRACK selects the active track without leaving FX mode and
             * shows its VOICE mix screen while TRACK is held (S075 F2-F).
             * The Effect type hook above already has priority.
             */
            menu_setActiveVoice(voiceNr);
            buttonHandler_showMuteLEDs();
            led_flashLed((uint8_t)(LED_VOICE1 + voiceNr));
            if (buttonHandler_getShift() && menu_fxVoiceMixOverlayBegin(voiceNr))
                buttonHandler_fxVoiceMixTrackMask = (uint8_t)(
                    buttonHandler_fxVoiceMixTrackMask |
                    (uint8_t)(1u << voiceNr));
            if (shouldPreviewVoice)
                seq_previewVoice(voiceNr);
            return;
        }

        menu_setActiveVoice(voiceNr);
        led_setActiveVoice(voiceNr);
        if (bh_state.selectButtonMode == SELECT_MODE_VOICE) {
            menu_switchPage(voiceNr);
            led_setActiveSelectButton(menu_getSubPage());
            /*
             * View-follows-track when PAR_FOLLOW is on (S077 P2 §6.1).
             *
             * What: if PAR_FOLLOW is on and the selected track plays from a
             * Scene other than the viewed one, switch the viewed Scene to that
             * track's played Scene. Why: VOICE/STEP views should present the
             * settings of the track that is actually sounding so the user edits
             * the right instrument. Inputs: parameter_values[PAR_FOLLOW],
             * seq_perTrackActive, seq_getTrackPlayedScene(voiceNr). Output:
             * menu_setShownPattern() moves the viewed Scene. Guard: only when a
             * per-track override exists, so there is no overhead otherwise.
             * Note: the retained Scene write target still follows the active
             * Scene (bank edit mask); this phase moves the view only.
             * Affiliates: menu_setShownPattern(), menu_getViewedPattern().
             */
            if (parameter_values[PAR_FOLLOW] && seq_perTrackActive) {
                uint8_t played = seq_getTrackPlayedScene(voiceNr);

                if (played != menu_getViewedPattern())
                    menu_setShownPattern(played);
            }
        }

        /*
         * Active voice is UI/menu context, so Menu owns the selected value.
         * Euklid params are then pulled directly from the Pattern generator
         * module so the generator page is correct if the user switches there.
         */
#if ENABLE_EUKLID_PAGE
        buttonHandler_applyEuklidParamsToMenu(voiceNr);
#endif

        if (bh_state.selectButtonMode == SELECT_MODE_STEP ||
            menu_activePage == SEQ_PAGE) {
            /*
             * Held steps + TRACK = STEP track automation overlay (S078 P2 §B).
             *
             * What: while one or more SEQ steps are physically held, a TRACK
             * (VOICE) press opens the held-step track automation overlay for
             * the pressed track instead of the ordinary track-settings page
             * switch. Why: the overlay edits automatable track timing
             * parameters (length, scale, shuffle) as per-step automation on
             * the held steps, so the TRACK press must not also reset the
             * front-page half or repaint the plain settings view. Inputs:
             * physical held-step mask. Output: Menu overlay entry; this press
             * returns before the ordinary half-toggle/page-switch path.
             * Affiliates: menu_enterStepTrackAutomationOverlay().
             */
            if (buttonHandler_seqHeldMask() != 0u) {
                menu_enterStepTrackAutomationOverlay(voiceNr);
                return;
            }
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
        }
#if ENABLE_EUKLID_PAGE
        if (menu_activePage == EUKLID_PAGE) {
            menu_repaintAll();
        }
#endif

        if (shouldPreviewVoice)
            seq_previewVoice(voiceNr);
    }
}

/* Process one press event */
static void processPress(uint8_t buttonNr)
{
    /*
     * Double-click detection and PERF VOICE gesture bookkeeping (S077 P2 §3.8,
     * §3.9).
     *
     * Every press is offered to the general-purpose double-click detector; a
     * non-SEQ press cancels any pending SEQ window. While a PERF VOICE hold is
     * active, a button that is neither a VOICE nor a SEQ button cancels the mute
     * gesture by marking the hold as action-occurred.
     */
    (void)dblclick_onPress(buttonNr);
    /*
     * Learn cancel (S083 C5): while Learn is active, a press of SHIFT, PLAY
     * or REC cancels it and is consumed (no transport, record or menu
     * action). SHIFT keeps its hold flag and its release is consumed, as
     * for a latched-menu close. Affiliates: midiLearn_cancel(),
     * bh_shiftReleaseConsume.
     */
    if (midiLearn_active() &&
        (buttonNr == BUT_SHIFT || buttonNr == BUT_START_STOP ||
         buttonNr == BUT_REC)) {
        midiLearn_cancel();
        if (buttonNr == BUT_SHIFT) {
            bh_shiftHeld = 1u;
            bh_shiftReleaseConsume = 1u;
        }
        return;
    }
    if (btn_to_seq(buttonNr) < 0)
        dblclick_cancel();
    if (perf_heldVoiceMask != 0u &&
        btn_to_voice(buttonNr) < 0 && btn_to_seq(buttonNr) < 0)
        perf_voiceActionOccurred = 1u;

    /*
     * Held-COPY owns its complete button gesture before ordinary row routing.
     * Inputs: one foreground event. Output: nonzero consumption prevents a
     * source or destination edge from leaking into normal Menu/Sequencer UI.
     */
    if (copyClear_buttonPressed(buttonNr))
        return;

    /*
     * Live erase button router, press edge (S081 §10.2, P1B items 8/9).
     *
     * What: while live erase is enabled, implements the per-mode table:
     *  - SEQ in VOICE/STEP erases the ACTIVE track's triggers/specials while
     *    held; SEQ in FX erases the FX sequence; SEQ in PERF (Scene buttons),
     *    Load/Save, global settings and SOM_GEN is consumed with no action;
     *  - VOICE in VOICE/STEP/PERF/FX erases that whole track while held, and is
     *    consumed elsewhere; except VOICE in PERF while the loop is engaged: it
     *    keeps its looper function (S081 P3 Q4) and falls through to
     *    handleVoiceButton();
     *  - PLAY is a plain transport toggle (never the SHIFT+PLAY chain chord).
     * Why: P1 erased in every mode, so PERF Scene buttons removed triggers, and
     * it used rtk instead of the active track. Inputs: button, mode, active
     * voice. Outputs: LiveRecord erase gestures or nothing. Accessors:
     * processPress(). Affiliates: liveRec_eraseSeqHeld(),
     * liveRec_eraseFxSeqHeld(), liveRec_eraseTrackHeld(), BH-2 (release edge).
     *
     * Each press that starts a gesture sets its button's bit in
     * bh_eraseSeqMask / bh_eraseFxSeqMask / bh_eraseVoiceMask; the release
     * edge ends exactly that gesture (P1C item 10).
     */
    if (liveRec_mode() == LIVEREC_MODE_ERASE) {
        int8_t eraseSeq = btn_to_seq(buttonNr);
        int8_t eraseVoice = btn_to_voice(buttonNr);
        uint8_t eraseMode = bh_state.selectButtonMode;

        if (eraseSeq >= 0) {
            uint16_t bit = (uint16_t)(1u << eraseSeq);

            if (eraseMode == SELECT_MODE_VOICE || eraseMode == SELECT_MODE_STEP) {
                bh_eraseSeqMask |= bit;
                liveRec_eraseSeqHeld(menu_getActiveVoice(), 1u);
            } else if (eraseMode == SELECT_MODE_FX) {
                bh_eraseFxSeqMask |= bit;
                liveRec_eraseFxSeqHeld(1u);
            }
            /* PERF, LOAD_SAVE, MENU, SOM_GEN: consumed */
            return;
        }
        if (eraseVoice >= 0 &&
            !(eraseMode == SELECT_MODE_PERF && seq_loopActive)) {
            if (eraseMode == SELECT_MODE_VOICE || eraseMode == SELECT_MODE_STEP ||
                eraseMode == SELECT_MODE_PERF  || eraseMode == SELECT_MODE_FX) {
                bh_eraseVoiceMask |= (uint8_t)(1u << eraseVoice);
                liveRec_eraseTrackHeld((uint8_t)eraseVoice, 1u);
            }
            /* other modes: consumed */
            return;
        }
        if (buttonNr == BUT_START_STOP) {
            bh_state.seqRunning = (uint8_t)(1u - bh_state.seqRunning);
            buttonHandler_setRunStopState(bh_state.seqRunning);
            seq_setRunning(bh_state.seqRunning);
            return;
        }
    }

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
        /*
         * S080 G8 / S083 F8: SHIFT+PLAY arms the SHIFT+PLAY chord in every
         * mode and never toggles the transport (MIDI_SPEC 6.3): +SEQ in
         * PERF opens the chain editor, +REC arms MIDI Learn.
         */
        if (buttonHandler_getShift()) {
            bh_chainPlayArmed = 1u;
            break;
        }
        buttonHandler_setRunStopState((uint8_t)(1u - bh_state.seqRunning));
        seq_setRunning((uint8_t)bh_state.seqRunning);
        break;

    case BUT_REC:
        /*
         * SHIFT+PLAY+REC arms MIDI Learn (S083 C5, F8).
         *
         * What: with SHIFT and PLAY held (the chord armed by F-6a), REC
         * performs none of its own functions (no record toggle, no SHIFT+REC
         * menu) and arms Learn; SHIFT, PLAY and REC flash until a valid CC
         * arrives or Learn is cancelled. Affiliates: midiLearn_arm(),
         * buttonHandler_setLearnLeds().
         */
        if (buttonHandler_getShift() && bh_chainPlayArmed) {
            midiLearn_arm();
            break;
        }
        if (bh_shiftRecMenuActive) {
            /* S080 E7: REC exits the SHIFT+REC settings menu. */
            buttonHandler_shiftRecMenuExit();
            break;
        }
        /*
         * REC while live erase is enabled (S081 §10.3, P1C item 5).
         *
         * What: REC does nothing while erase is on, including with SHIFT held
         * (which it almost always is during erase). An already-open SHIFT+REC
         * menu still closes. Why: SHIFT+REC was tested first, so REC during erase
         * opened the settings menu; the menu then swallowed the SHIFT release and
         * erase stayed on with both keys up. Affiliates: processRelease() BUT_SHIFT
         * (BC-2), buttonHandler_shiftRecMenuEnter().
         */
        if (liveRec_mode() == LIVEREC_MODE_ERASE)
            break;
        if (buttonHandler_getShift()) {
            /* S080 E7: SHIFT+REC opens the settings menu (overlay + quant). */
            buttonHandler_shiftRecMenuEnter();
        } else {
            /*
             * REC press: start the hold timer for overdub detection
             * (S081 §6, §19.7, B-2a).
             *
             * What: start the hold timer (bh_recPressTick, bh_recArmed). The
             * press no longer toggles recording. Why: the toggle moves to
             * release, and a hold past DOUBLE_CLICK_TIMEOUT enters overdub; this
             * matches the fill hold. Inputs: REC press event. Outputs: hold timer
             * started. Affiliates: buttonHandler_tick (overdub threshold),
             * liveRec_overdubBegin, liveRec_setLatch.
             */
            bh_recPressTick = time_sysTick;
            bh_recArmed = 1u;
        }
        break;

    case BUT_COPY:
        if (bh_state.selectButtonMode == SELECT_MODE_LOAD_SAVE &&
            menu_bankCopyPressed()) {
            bh_bankCopyPressTick = time_sysTick;
            break;
        }
        /*
         * SHIFT + copy/clear while recording and running keeps its erase
         * meaning. Every other press arms a copy operation, or a clear
         * operation when SHIFT is held (S075, spec §3); refusals are silent.
         * The release ends menu interaction; queued pastes/clears keep
         * running in the background. Affiliates: copyClear_copyPressed()/
         * copyClear_copyReleased().
         */
        bh_clearHeld = 1u;
        if (buttonHandler_getShift()) {
            if (liveRec_armed()) {
                /*
                 * SHIFT+COPY while armed enters live erase (S081 §10, G-5).
                 * The chord ends when both SHIFT and CLEAR are released.
                 */
                buttonHandler_liveEraseBegin();
            } else {
                (void)copyClear_copyPressed(1u);
            }
        } else {
            (void)copyClear_copyPressed(0u);
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
        if (bh_state.selectButtonMode == SELECT_MODE_FX) {
            /* FX BAR1 is inert unless an active type hook handles it. */
            (void)menuEffects_hookBar(0u, buttonHandler_getShift(), 1u);
            break;
        }
        if (bh_state.selectButtonMode == SELECT_MODE_PERF) {
            /*
             * S080 E9 / P2 Defect 8: BAR1 hold is tracked in event order (it
             * can sustain the loop) and, while the loop is engaged, is the
             * momentary triplet modifier (x2/3). Loop starts never move.
             */
            bh_loopBarHeld |= 0x01u;
            if (seq_loopActive) {
                bh_loopBarModifier = 2u;            /* triplet */
                seq_loopUpdateDivision(bh_loopLastSelectButton,
                                       bh_loopBarModifier);
                break;
            }
            buttonHandler_overlayDrainAll();  /* S084 P0 A3: old rhythm */
            bh_overlayActive = (bh_overlayActive == 1u) ? 0u : 1u;
            led_setValue((uint8_t)(bh_overlayActive == 1u), LED_BAR1);
            led_setValue((uint8_t)(bh_overlayActive == 2u), LED_BAR2);
            /* Switch/toggle the held overlay trigger while a VOICE is held. */
            buttonHandler_overlaySwitchHeld();
            break;
        }
        if (menu_currentBar > 0u)
            buttonHandler_selectBar((uint8_t)(menu_currentBar - 1u));
        else
            led_flashGroup(LED_FLASH_GROUP_SELECT, 0x0001u);
        break;

    case BUT_BAR2:
        led_setValue(1, LED_BAR2);
        /*
         * See BAR1 above. BAR2 is cursor-forward (keeps characters) in
         * Load/Save character entry and ordinary next-bar selection everywhere
         * else.
         */
        if (menu_loadSaveBarButtonPressed(1u))
            break;
        if (bh_state.selectButtonMode == SELECT_MODE_FX) {
            /* FX BAR2 is inert unless an active type hook handles it. */
            (void)menuEffects_hookBar(1u, buttonHandler_getShift(), 1u);
            break;
        }
        if (bh_state.selectButtonMode == SELECT_MODE_PERF) {
            /* S080 E9 / P2 Defect 8: BAR2 is tracked in event order and is the
             * dot modifier during a loop, else the right trigger-overlay
             * toggle (see BAR1 above). */
            bh_loopBarHeld |= 0x02u;
            if (seq_loopActive) {
                bh_loopBarModifier = 1u;            /* dot */
                seq_loopUpdateDivision(bh_loopLastSelectButton,
                                       bh_loopBarModifier);
                break;
            }
            buttonHandler_overlayDrainAll();  /* S084 P0 A3: old rhythm */
            bh_overlayActive = (bh_overlayActive == 2u) ? 0u : 2u;
            led_setValue((uint8_t)(bh_overlayActive == 1u), LED_BAR1);
            led_setValue((uint8_t)(bh_overlayActive == 2u), LED_BAR2);
            /* Switch/toggle the held overlay trigger while a VOICE is held. */
            buttonHandler_overlaySwitchHeld();
            break;
        }
        if (menu_currentBar < (NUM_BARS - 1u))
            buttonHandler_selectBar((uint8_t)(menu_currentBar + 1u));
        else
            led_flashGroup(LED_FLASH_GROUP_SELECT,
                           (uint16_t)(1u << (NUM_BARS - 1u)));
        break;

    case BUT_SHIFT:
        /*
         * SHIFT hold flag tracks the physical key (S081 §10.1, P1B item 12).
         * Set on every SHIFT press, including the press that closes a latched
         * menu (whose release is consumed). Why: the live-erase chord ends only
         * when BOTH SHIFT and CLEAR are released; a flag skipped on an early
         * return left erase unable to end, or able to end with SHIFT still held.
         */
        bh_shiftHeld = 1u;
        /*
         * S080 P2 Defect 3 / Phase G: SHIFT closes a latched menu.
         *
         * What: if the SHIFT+REC settings menu or the chain editor is open,
         * this press closes it (restoring the previous page) and is
         * consumed; its paired release is swallowed through
         * bh_shiftReleaseConsume. Why: both menus latch open on entry and
         * close on the next SHIFT press (plan §2.6, §4.1). Inputs:
         * bh_shiftRecMenuActive, menu_chainMenuIsOpen(). Outputs: menu closed,
         * release-consume armed. Affiliates: buttonHandler_shiftRecMenuExit(),
         * menu_chainMenuClose() (Step 11), processRelease() BUT_SHIFT.
         */
        if (bh_shiftRecMenuActive || menu_chainMenuIsOpen()) {
            if (bh_shiftRecMenuActive)
                buttonHandler_shiftRecMenuExit();
            else
                menu_chainMenuClose();
            bh_shiftReleaseConsume = 1u;
            return;
        }
        /* _SEQUENCER_ADD_SPIKE_: restore SHIFT-press mode behavior parity with AVR. */
        /* S081 G-5: SHIFT press completes a live-erase chord with held CLEAR. */
        if (bh_clearHeld && liveRec_armed())
            buttonHandler_liveEraseBegin();
        led_setValue(1, LED_SHIFT);
        switch (bh_state.selectButtonMode) {
        case SELECT_MODE_VOICE:
            /*
             * Hold SHIFT in VOICE mode to show and edit Morph endpoints
             * (S075 F2-G). The existing VOICE page and parameter buffer remain
             * active; only Menu's endpoint resolver changes. Release restores
             * the SHIFT+MODE VOICE latch state in processRelease(), while
             * SHIFT combinations continue to reach their normal handlers.
             *
             * Output: Menu's voiceModeShowMorph = 1; the next repaint/edit
             * resolves instrument and FX-send endpoint cells against Morph.
             */
            menu_setVoiceModeShowMorph(1u);
            return;

        case SELECT_MODE_FX:
            /*
             * Holding SHIFT displays/edits Morph endpoints on the FX page;
             * under the voice-mix overlay it selects the VOICE Morph view
             * while preserving Effect Morph state for return (S075 F2-Q6 c).
             */
            menu_setEffectShowMorph(1u);
            if (menu_fxVoiceMixOverlayActive())
                menu_setVoiceModeShowMorph(1u);
            break;

        case SELECT_MODE_PERF:
#if ENABLE_EUKLID_PAGE
        {
            uint8_t trackNr;
            uint8_t patternNr;

            /* Legacy pattern-settings layer retained only for diagnostics. */
            menu_switchPage(PATTERN_SETTINGS_PAGE);
            led_clearSelectLeds();
            led_clearAllBlinkLeds();
            led_setBlinkLed((uint8_t)(LED_STEP1 +
                                       parameter_values[PAR_TRACK_ROTATION]), 1);
            if (parameter_values[PAR_FOLLOW]) {
                menu_setShownPattern(menu_shownPattern);
                led_clearSequencerLeds();
                trackNr = menu_getActiveVoice();
                patternNr = menu_getViewedPattern();
                led_updatePatternTrack(trackNr, patternNr,
                                       buttonHandler_selectedStep);
            }
            led_setBlinkLed((uint8_t)(LED_PART_SELECT1 +
                                      menu_getViewedPattern()), 1);
            break;
        }
#endif
            /* PERF remains visible when the retired SHIFT layer is disabled. */
            break;

        case SELECT_MODE_STEP:
            /*
             * S078 §5.6: hold SHIFT on the STEP page to show and edit the
             * per-track length/scale/shuffle Morph endpoints, mirroring the
             * VOICE SHIFT Morph view. Release returns to Normal values.
             */
            menu_setPatternTrackMorphEndpoint(1u);
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

/*
 * Learn LED flash (S083 C5).
 * What: on = SHIFT, PLAY and REC flash (continuous; the code calls this a
 * blink). off = the three LEDs return to their own state: SHIFT to its
 * hold, PLAY to the transport, REC to the live-record LATCH state.
 * Inputs: on/off. Accessor: MidiLearn.c (arm, end). Affiliates:
 * led_setBlinkLed(), liveRec_mode().
 */
void buttonHandler_setLearnLeds(uint8_t on)
{
    led_setBlinkLed(LED_SHIFT, on);
    led_setBlinkLed(LED_START_STOP, on);
    led_setBlinkLed(LED_REC, on);
    if (!on) {
        led_setValue(bh_shiftHeld, LED_SHIFT);
        led_setValue((uint8_t)bh_state.seqRunning, LED_START_STOP);
        led_setValue((uint8_t)(liveRec_mode() == LIVEREC_MODE_LATCH), LED_REC);
    }
}

static void processRelease(uint8_t buttonNr)
{
    if (buttonNr == BUT_COPY) {
        if (bh_bankCopyPressTick) {
            menu_bankCopyReleased();
            bh_bankCopyPressTick = 0u;
            return;
        }
        /*
         * COPY release (S081 G-5): ends the live-erase chord when SHIFT is no
         * longer held, otherwise the ordinary copy/clear release runs.
         */
        bh_clearHeld = 0u;
        if (liveRec_mode() == LIVEREC_MODE_ERASE) {
            if (!bh_shiftHeld)
                buttonHandler_liveEraseEnd();
            else
                led_setValue(1u, LED_COPY);   /* keep lit while SHIFT held */
            return;
        }
        copyClear_copyReleased();
        return;
    }
    if (buttonNr == BUT_REC) {
        /*
         * REC release: toggle latch or end overdub (S081 §6, B-2d).
         *
         * What: if an overdub is in progress (mode OVERDUB, the hold passed the
         * threshold), end it and pulse the REC LED. Otherwise, if the hold timer
         * was still armed (a short press), toggle liveRec_setLatch() and set the
         * REC LED to the new state. While erase is enabled, release just clears
         * the hold timer (the prior LATCH/OFF returns through liveRec_eraseEnd).
         * Why: the toggle moves to release so the hold can be detected first.
         * Inputs: REC release event. Outputs: mode/LED changed. Affiliates:
         * liveRec_setLatch, liveRec_overdubEnd, led_pulseLed.
         */
        if (liveRec_mode() != LIVEREC_MODE_ERASE) {
            if (liveRec_mode() == LIVEREC_MODE_OVERDUB) {
                liveRec_overdubEnd();
                led_pulseLed(LED_REC);
            } else if (bh_recArmed) {
                uint8_t on = (uint8_t)(liveRec_mode() != LIVEREC_MODE_LATCH);

                liveRec_setLatch(on);
                led_setValue(on, LED_REC);
            }
        }
        bh_recArmed = 0u;
        return;
    }
    /*
     * Live erase button router, release edge (S081 §10.2, P1B items 8/9,
     * P1C items 10/16).
     *
     * What: a SEQ or TRACK release ends the gesture its own press started
     * (BC-3 masks), whatever the mode is now, and is consumed, even after
     * erase has ended. Any other SEQ/TRACK release while erase is on is
     * consumed. Why: P1B routed by the mode at release time, so after a mode
     * change an old button's release could end a new button's gesture, and a
     * release after erase end was lost (leaving LiveRecord's hold count set,
     * item 16). Affiliates: BC-4 (press edge), liveRec_eraseSeqHeld(),
     * liveRec_eraseFxSeqHeld(), liveRec_eraseTrackHeld().
     * S081 P3: a VOICE release whose press was a PERF hold (perf_heldVoiceMask,
     * looper or per-track gesture) is not consumed: it continues to the PERF
     * release path, so a looper TRACK hold ends normally during erase (Q4) and
     * the hold bit cannot be stranded.
     */
    {
        int8_t eraseSeq = btn_to_seq(buttonNr);
        int8_t eraseVoice = btn_to_voice(buttonNr);

        if (eraseSeq >= 0) {
            uint16_t bit = (uint16_t)(1u << eraseSeq);

            if ((bh_eraseSeqMask & bit) != 0u) {
                bh_eraseSeqMask &= (uint16_t)~bit;
                liveRec_eraseSeqHeld(menu_getActiveVoice(), 0u);
                return;
            }
            if ((bh_eraseFxSeqMask & bit) != 0u) {
                bh_eraseFxSeqMask &= (uint16_t)~bit;
                liveRec_eraseFxSeqHeld(0u);
                return;
            }
        }
        if (eraseVoice >= 0) {
            uint8_t bit = (uint8_t)(1u << eraseVoice);

            if ((bh_eraseVoiceMask & bit) != 0u) {
                bh_eraseVoiceMask &= (uint8_t)~bit;
                liveRec_eraseTrackHeld((uint8_t)eraseVoice, 0u);
                return;
            }
        }
        if (liveRec_mode() == LIVEREC_MODE_ERASE &&
            (eraseSeq >= 0 ||
             (eraseVoice >= 0 &&
              (perf_heldVoiceMask & (uint8_t)(1u << eraseVoice)) == 0u)))
            return;                                   /* consumed */
    }
    {
        /*
         * Consume release of an overlay TRACK before copy/clear routing
         * (S075 F2-F). The overlay remains while any paired TRACK is held;
         * releasing the last one restores the saved Effect page.
         */
        int8_t voice = btn_to_voice(buttonNr);
        if (voice >= 0) {
            const uint8_t bit = (uint8_t)(1u << (uint8_t)voice);
            if ((buttonHandler_fxVoiceMixTrackMask & bit) != 0u) {
                buttonHandler_fxVoiceMixTrackMask = (uint8_t)(
                    buttonHandler_fxVoiceMixTrackMask & (uint8_t)~bit);
                if (buttonHandler_fxVoiceMixTrackMask == 0u)
                    menu_fxVoiceMixOverlayEnd();
                return;
            }
            /*
             * PERF VOICE release: single-track mute toggle (S077 P2 §4.1, Q8,
             * Q12). The toggle fires only when the hold had no per-track
             * assignment and no unrelated button press; each VOICE's mute
             * fires on its own release independently.
             */
            /* S080 E12: an overlay-trigger VOICE release stops the repeat and
             * sends note-off instead of toggling mute. */
            if (bh_state.selectButtonMode == SELECT_MODE_PERF &&
                (bh_overlayVoiceMask & bit) != 0u) {
                buttonHandler_overlayRelease((uint8_t)voice);
                return;
            }
            if (bh_state.selectButtonMode == SELECT_MODE_PERF &&
                (perf_heldVoiceMask & bit) != 0u) {
                buttonHandler_perfVoiceReleased((uint8_t)voice);
                return;
            }
        }
    }
    if (copyClear_buttonReleased(buttonNr))
        return;

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
    case BUT_START_STOP:
        /* S080 G8: releasing PLAY ends the SHIFT+PLAY chord. */
        bh_chainPlayArmed = 0u;
        break;

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

    case BUT_MODE2:
        /*
         * PERF release ends fill mode (S079 §3.3).
         *
         * Output: the fill hold is disarmed and seq_fillActive cleared, so
         * fil/!fl conditions revert immediately. The PERF mode/page stays as
         * the press-side handler already switched it.
         */
        bh_fillArmed = 0u;
        bh_fillPressTick = 0u;
        seq_fillActive = 0u;
        break;

    case BUT_BAR1:
        if (bh_state.selectButtonMode == SELECT_MODE_PERF) {
            /*
             * S080 E8/E9, P2 Defect 8: BAR1 release in PERF.
             *
             * What: clears the BAR1 hold. While looping, the modifier falls
             * back to the other BAR (dot) or none, the BAR1 LED goes dark,
             * and the loop ends if nothing sustains it any more. Otherwise
             * the BAR LED shows the overlay toggle. Why: BAR holds sustain
             * the loop (Defect 8); P1 left the BAR LED lit after a modifier
             * release. Inputs: bh_loopBarHeld, seq_loopActive. Outputs:
             * seq_loopUpdateDivision(), LED, possibly seq_loopDisengage().
             * Affiliates: buttonHandler_loopCheckDisengage().
             */
            bh_loopBarHeld &= (uint8_t)~0x01u;
            if (seq_loopActive) {
                bh_loopBarModifier = (bh_loopBarHeld & 0x02u) ? 1u : 0u;
                seq_loopUpdateDivision(bh_loopLastSelectButton,
                                       bh_loopBarModifier);
                led_setValue(0u, LED_BAR1);
                buttonHandler_loopCheckDisengage();
                break;
            }
            led_setValue((uint8_t)(bh_overlayActive == 1u), LED_BAR1);
            break;
        }
        led_setValue(0, LED_BAR1);
        break;

    case BUT_BAR2:
        if (bh_state.selectButtonMode == SELECT_MODE_PERF) {
            /* S080 P2 Defect 8: BAR2 release, mirror of BAR1 above. */
            bh_loopBarHeld &= (uint8_t)~0x02u;
            if (seq_loopActive) {
                bh_loopBarModifier = (bh_loopBarHeld & 0x01u) ? 2u : 0u;
                seq_loopUpdateDivision(bh_loopLastSelectButton,
                                       bh_loopBarModifier);
                led_setValue(0u, LED_BAR2);
                buttonHandler_loopCheckDisengage();
                break;
            }
            led_setValue((uint8_t)(bh_overlayActive == 2u), LED_BAR2);
            break;
        }
        led_setValue(0, LED_BAR2);
        break;

    case BUT_SHIFT: {
        uint8_t eraseEnded = 0u;

        /*
         * S080 P2 Defect 3 / Phase G: latched-menu SHIFT release.
         *
         * What: (1) the release paired with a closing SHIFT press is consumed
         * and only turns the SHIFT LED off; (2) while SHIFT+REC or the chain
         * editor is open, every SHIFT release is ignored, so the menu stays
         * open and the SHIFT LED stays lit. Why: the old code exited SHIFT+REC
         * on the entry gesture's own release (Defect 3), and the PERF branch
         * below re-enters PERFORMANCE_PAGE, which snapped the chain editor
         * back to PERF. Inputs: bh_shiftReleaseConsume, bh_shiftRecMenuActive,
         * menu_chainMenuIsOpen(). Outputs: SHIFT LED; no page change.
         * Affiliates: processPress() BUT_SHIFT (5.2).
         */
        /*
         * SHIFT release and live erase (S081 §10.1, P1B item 12, P1C item 5).
         *
         * What: the hold flag is cleared first; then, if erase is on and CLEAR is
         * also free, erase ends here, before the latched-menu exits. The menu exits
         * keep their LED rules (consume: off; open menu: stays lit). If erase is on
         * and CLEAR is still held, the SHIFT LED stays lit. Why: the consume and
         * open-menu breaks ran before the erase test, so with the SHIFT+REC menu
         * open (BC-1) or a CLEAR press made inside it, releasing both keys left
         * erase on. Affiliates: BC-1, buttonHandler_liveEraseEnd(),
         * processRelease() BUT_COPY.
         */
        bh_shiftHeld = 0u;
        if (liveRec_mode() == LIVEREC_MODE_ERASE && !bh_clearHeld) {
            buttonHandler_liveEraseEnd();             /* SHIFT/CLEAR LEDs off */
            eraseEnded = 1u;
        }
        if (bh_shiftReleaseConsume) {
            bh_shiftReleaseConsume = 0u;
            led_setValue(0u, LED_SHIFT);
            break;
        }
        if (bh_shiftRecMenuActive || menu_chainMenuIsOpen()) {
            if (eraseEnded)
                led_setValue(1u, LED_SHIFT);          /* latched menu keeps it lit */
            break;
        }
        if (liveRec_mode() == LIVEREC_MODE_ERASE)
            led_setValue(1u, LED_SHIFT);              /* CLEAR still held */
        else
            led_setValue(0, LED_SHIFT);

        switch (bh_state.selectButtonMode) {
        case SELECT_MODE_VOICE:
            /*
             * End the momentary VOICE Morph view (S075 F2-G).
             *
             * Output: return to the SHIFT+MODE VOICE latch state and restore
             * selected voice/latch LED feedback. Do not enter STEP mode.
             */
            menu_setVoiceModeShowMorph(buttonHandler_morphVoiceModeActive);
            led_setActiveVoice(menu_getActiveVoice());
            if (buttonHandler_morphVoiceModeActive)
                led_setBlinkLed(LED_MODE1, 1u);
            break;

        case SELECT_MODE_PERF:
            /*
             * Return to the PERF page after a SHIFT gesture.
             *
             * What: clears stale blinks and re-enters PERFORMANCE_PAGE; the
             * page switch repaints the SEQ Scene row and the SELECT row from
             * looper state. Why: the old led_clearSelectLeds() +
             * led_initPerformanceLeds() pair wiped the looper's division LED
             * whenever SHIFT was released during a loop (S080 P2 Defect 4).
             * Affiliates: menu_switchPage() PERF LED block,
             * menu_syncPerfSelectLed().
             */
            led_clearAllBlinkLeds();
            menu_switchPage(PERFORMANCE_PAGE);
            /*
             * S080 A2 (SHIFT release): keep the PERF cursor on cell 0.
             *
             * What:      after returning to PERFORMANCE_PAGE on SHIFT
             *            release, re-zero the within-subpage cursor.
             * Why:       the SHIFT Morph view leaves menuIndex wherever the
             *            user was; the S080 layout expects entry at cell 0
             *            (mrp) on subpage 0. menu_switchPage() runs its own
             *            reset, but the subpage bits can still be stale from
             *            the previous context, so this repeats the guarded
             *            reset for the PERF case.
             * Inputs:    none.
             * Outputs:   menuIndex parameter bits zeroed for subpage 0.
             * Accessors: processRelease() BUT_SHIFT / SELECT_MODE_PERF (here).
             * Affiliates: menu_resetActiveParameter() (menu.c).
             */
            menu_resetActiveParameter();
            return;

        case SELECT_MODE_FX:
            /*
             * End the momentary Morph view while keeping FX mute LEDs. Under
             * the overlay only the VOICE endpoint view changes; TRACK keeps
             * the overlay up (S075 F2-Q6 c).
             */
            menu_setEffectShowMorph(0u);
            if (menu_fxVoiceMixOverlayActive())
                menu_setVoiceModeShowMorph(0u);
            buttonHandler_showMuteLEDs();
            return;

        case SELECT_MODE_STEP:
            /* S078 §5.6: leaving the STEP SHIFT view returns to Normal cells. */
            menu_setPatternTrackMorphEndpoint(0u);
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
    }

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
        /*
         * Reconcile a dropped edge in the held-COPY owner before ordinary
         * pairing masks are reset. Inputs: event-ring overflow. Output: the
         * Copy/Clear source/row ledger cannot remain half armed.
         */
        (void)copyClear_eventOverflow();
        /* a lost COPY release must not leave the Bank page held */
        menu_bankCopyForceRelease();
        bh_bankCopyPressTick = 0u;
        buttonHandler_voiceSceneSeqPressedMask = 0u;
        buttonHandler_loadSceneSeqPressedMask = 0u;
        /* S075 F2-F: a lost TRACK release must not strand the overlay. */
        buttonHandler_fxVoiceMixTrackMask = 0u;
        menu_fxVoiceMixOverlayEnd();
        /*
         * S077 P2: a dropped VOICE release must not strand the PERF per-track
         * hold or its action flag, and a lost SEQ edge must not leave a stale
         * double-click window armed.
         */
        perf_heldVoiceMask = 0u;
        perf_voiceActionOccurred = 0u;
        dblclick_cancel();
        buttonHandler_buttonTimerStepNr = NO_STEP_SELECTED;
        /*
         * S080 C8 / P2: a lost release must not strand the looper or a latch.
         *
         * What: releases the looper (tracks resume where they would have
         * been), clears every looper hold bit, drops the BAR overlay, and
         * closes the SHIFT+REC menu and the chain editor, disarming the
         * SHIFT+PLAY chord and the SHIFT release-consume. Why: a ring overflow
         * can drop the release edge, which would otherwise leave the looper
         * engaged with no held button or a menu latched with no exit edge.
         * Inputs: event overflow detected. Outputs: looper/overlay/latch
         * state cleared. Affiliates: buttonHandler_loopForceRelease(),
         * menu_chainMenuClose(), buttonHandler_shiftRecMenuExit().
         */
        buttonHandler_loopForceRelease();
        buttonHandler_overlayDrainAll();     /* S084 P0 A3: old rhythm */
        bh_overlayActive = 0u;
        /* S083: per-track entries stop with the overlay. */
        buttonHandler_overlayReconfigure(0u);
        if (bh_shiftRecMenuActive)
            buttonHandler_shiftRecMenuExit();
        if (menu_chainMenuIsOpen())
            menu_chainMenuClose();
        bh_shiftReleaseConsume = 0u;
        bh_chainPlayArmed = 0u;
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
        copyClear_postEvent();
    }
}
