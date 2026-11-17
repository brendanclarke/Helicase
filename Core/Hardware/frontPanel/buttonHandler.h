/*
 * buttonHandler.h — LXR-02 button handler.
 * Ported from original LXR AVR buttonHandler.h by Julian Schmidt.
 *
 * CONCURRENCY: buttonHandler_buttonPressed / buttonReleased are called by the
 * foreground scan (din_dout_exchange via timebase_serviceFrontPanel at about
 * 500 Hz). They only update held state and enqueue an event; they do not call
 * LCD/menu actions. buttonHandler_processEvents() in the main loop dispatches
 * one event per call, with two bounded calls separated by audio rendering.
 */
#ifndef BUTTONHANDLER_H_
#define BUTTONHANDLER_H_
#include <stdint.h>
#include "config.h"

/* Select-button modes */
#define SELECT_MODE_VOICE       0x00
#define SELECT_MODE_PERF        0x01
#define SELECT_MODE_STEP        0x02
#define SELECT_MODE_LOAD_SAVE   0x03
/* SHIFT+PERF selects the Effect page; the retired Euklid mode is compiled out. */
#define SELECT_MODE_FX          0x05
#define SELECT_MODE_SOM_GEN     0x06
#define SELECT_MODE_MENU        0x07

/*
 * Common short hold-vs-tap threshold, sourced from config.h.
 *
 * What: the legacy timer code still names this value BUTTON_TIMEOUT, but the
 * actual policy is the shared BUTTON_HOLD_DELAY_MS used by all UI gestures.
 * Why: S066 needs the VOICE overlay and existing long-press paths to agree on
 * one wrap-safe threshold rather than drifting apart.
 * Affiliates: config.h, buttonHandler_tick(), and buttonHandler_setTimeraction().
 */
#define BUTTON_TIMEOUT          BUTTON_HOLD_DELAY_MS
#define NO_STEP_SELECTED        -1

/* Button index constants — LXR-02 shift-register chain order
**
** Shift register chain: 40 buttons across 5 bytes, indices 0..39.
** Index 40 is SW43 (PB7 GPIO) — a hardware-separate button, registered
** here so the same button-event API covers it. SW43 is BAR1 on LXR-02.
**
** BAR1 / BAR2: new buttons on LXR-02 not present on the original LXR.
** No port functionality assigned to them yet — reserved for future
** firmware. They are read and the LEDs can be lit; nothing acts on the
** events. */
enum ButtonNumbers {
    BUT_VOICE_7    = 0,  BUT_VOICE_6  = 1,  BUT_VOICE_5  = 2,  BUT_VOICE_4  = 3,
    BUT_VOICE_3    = 4,  BUT_VOICE_2  = 5,  BUT_VOICE_1  = 6,  BUT_BAR2     = 7,
    BUT_SEQ13      = 8,  BUT_SEQ14    = 9,  BUT_SEQ15    = 10, BUT_SEQ16    = 11,
    BUT_SELECT8    = 12, BUT_SELECT7  = 13, BUT_SELECT6  = 14, BUT_SELECT5  = 15,
    BUT_SEQ9       = 16, BUT_SEQ10    = 17, BUT_SEQ11    = 18, BUT_SEQ12    = 19,
    BUT_SELECT4    = 20, BUT_SELECT3  = 21, BUT_SELECT2  = 22, BUT_SELECT1  = 23,
    BUT_SEQ5       = 24, BUT_SEQ6     = 25, BUT_SEQ7     = 26, BUT_SEQ8     = 27,
    BUT_MODE4      = 28, BUT_MODE3    = 29, BUT_MODE2    = 30, BUT_MODE1    = 31,
    BUT_SEQ1       = 32, BUT_SEQ2     = 33, BUT_SEQ3     = 34, BUT_SEQ4     = 35,
    BUT_COPY       = 36, BUT_REC      = 37, BUT_START_STOP=38, BUT_SHIFT    = 39,
    BUT_BAR1       = 40,
    BUT_COUNT      = 41,
};

/*
 * Called from the foreground front-panel scan — only records events and held
 * state, with no LCD calls. The event producer is the scan; the consumer is
 * buttonHandler_processEvents() in the main loop.
 */
void buttonHandler_buttonPressed(uint8_t buttonNr);
void buttonHandler_buttonReleased(uint8_t buttonNr);

/*
 * Called from main loop — drains the event queue and executes menu/LED actions.
 */
void buttonHandler_processEvents(void);

/* Legacy tick — kept for long-press timer (can be called from main loop) */
void buttonHandler_tick(void);

/*
 * Return the raw physical held-state mask for SEQ1..SEQ16.
 *
 * What: bit N corresponds to BUT_SEQ(N+1), independent of Menu mode or timer
 * state. Why: Menu needs the physical selection while resolving held-step
 * automation values. Inputs: the foreground-scan-maintained held state.
 * Output: a 16-bit foreground-readable mask. Affiliate:
 * buttonHandler_visibleStep().
 */
uint16_t buttonHandler_seqHeldMask(void);

/*
 * Convert a visible SEQ button index to its absolute Pattern step.
 *
 * What: folds menu_currentBar into the 0..127 Pattern grid. Why: the VOICE
 * overlay stores button order as compact 0..15 indices but PatternData reads
 * absolute steps. Inputs: zero-based SEQ button index. Output: absolute step.
 */
uint8_t buttonHandler_visibleStep(uint8_t seqButtonPressed);

/*
 * Expose physical-row decoding to the held-COPY session.
 *
 * Inputs: raw button number. Outputs: zero-based row index or -1 when the
 * button is outside that family. The mappings remain owned by ButtonHandler
 * so Copy/Clear cannot drift from normal front-panel routing.
 */
int8_t buttonHandler_seqIndex(uint8_t buttonNr);
int8_t buttonHandler_selectIndex(uint8_t buttonNr);
int8_t buttonHandler_voiceIndex(uint8_t buttonNr);

uint8_t buttonHandler_getMode(void);
uint8_t buttonHandler_getShift(void);
/*
 * Put the front panel into Load/Save for a MIDI-origin Bank load
 * (S082 P1 D7, D32).
 * Inputs: none. Outputs: latches closed, copy/clear session ended,
 * Load/Save mode and LEDs. Menu performs the page change.
 * Accessors: menu_backgroundBankLoadUiBegin().
 */
void buttonHandler_enterLoadSaveForBackgroundLoad(void);
void    buttonHandler_setRunStopState(uint8_t running);
void    buttonHandler_showMuteLEDs(void);
void    buttonHandler_muteVoice(uint8_t voice, uint8_t isMuted);
/* S080 P2 Defect 11: clear the VOICE mute mirror after a bulk unmute. */
void    buttonHandler_clearAllMutes(void);
/*
 * Overlay executor entry points for MIDI notes (S083 Q5, N6, C8).
 *
 * buttonHandler_overlayActive(): the one overlay state, 0 off, 1 BAR `<`,
 * 2 BAR `>`, in any mode. Set by the BAR presses in PERF and by CC2 on G
 * (S083 follow-up; the earlier separate CC2 state and the BAR-over-CC2
 * priority C-9/F-3 are retired). MidiNotes uses it for every note-on.
 * buttonHandler_setOverlay(): CC2 on G: set the overlay exactly like a BAR
 * press (state, BAR LEDs in PERF unless the looper owns them, held entries
 * re-configured).
 * buttonHandler_overlayMidiNoteOn(): start overlay `which` (1/2) on `track`
 * at `internal` pitch; `midiNote` is remembered so its note-off ends a
 * roll; ageUs is the arrival age for the first hit's record placement;
 * sub is the note's place within a wide (Tetrakt) track's group of four,
 * which selects the layer every hit plays (S096 P3).
 * buttonHandler_overlayMidiNoteOff(): end the roll held by `midiNote`
 * (the resolved track first, then any track holding that note); a flam's
 * main hit still plays.
 * Context: foreground. Accessor: MidiNotes.c. Affiliates: the per-track
 * overlay table, LiveRecord.
 */
uint8_t buttonHandler_overlayActive(void);
void    buttonHandler_setOverlay(uint8_t which);
void    buttonHandler_overlayMidiNoteOn(uint8_t track, uint8_t which,
                                        uint8_t internal, uint8_t midiNote,
                                        uint16_t ageUs, uint8_t sub);
void    buttonHandler_overlayMidiNoteOff(uint8_t track, uint8_t midiNote);
/* S083 C5: flash SHIFT/PLAY/REC while Learn is active; restore on off. */
void    buttonHandler_setLearnLeds(uint8_t on);
/*
 * S080 P2 Defect 4: SELECT index (0..7) of the PERF looper division while the
 * looper is held or engaged, else 0xFF. Client: menu_syncPerfSelectLed().
 */
uint8_t buttonHandler_loopSelectLed(void);

extern uint8_t  buttonHandler_selectedStep;

#endif
