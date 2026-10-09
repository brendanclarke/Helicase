/*
 * copyClearSession.h — Phase 6 copy/clear operation state and button routing.
 *
 * What: owns the copy and clear operations while the copy/clear button is
 * held: which operation is running, the source, the menu and selection, the
 * range rule for SEQ/SELECT rows, button-edge ownership, LEDs, and the
 * predicate that suspends AutoSave and Pattern maintenance. Why: one owner
 * must see every front-panel event first while copy/clear is held, so no
 * press leaks into step toggles, holds, Scene switches or mutes (spec §7).
 * Inputs: buttonHandler events, encoder and endless-pot turns routed by Menu,
 * mode/bar/track/Scene context from Menu and buttonHandler. Outputs: queued
 * pastes and clears (copyClearService.h), LED state, the menu text drawn by
 * Menu. Affiliates: copyOps.h, clearOps.h, copyClearService.h, menu.c,
 * buttonHandler.c, ledHandler.c. Reference: knowledge_files/
 * specification_reference/COPYCLEAR_UTILITIES.md.
 */
#ifndef COPY_CLEAR_SESSION_H_
#define COPY_CLEAR_SESSION_H_

#include <stdint.h>

/*
 * Operation phase. CC_OP_ARMED_COPY: copy/clear held, no source yet.
 * CC_OP_COPY: a copy operation has started (source set). CC_OP_CLEAR: SHIFT
 * was held when copy/clear was pressed. CC_OP_NONE: button not held (queued
 * work may still be finishing; see copyClear_backgroundSuspended()).
 */
typedef enum {
    CC_OP_NONE = 0u,
    CC_OP_ARMED_COPY,
    CC_OP_COPY,
    CC_OP_CLEAR
} cc_op_t;

/* What a copy object button selected, the kind of a held clear object, or a
 * cross-kind paste (CC_KIND_BAR_TO_STEP: bar source, step destination). */
/*
 * What:       CC_KIND_BAR_TO_STEP (6) is a cross-kind paste: the source is a
 *             bar or bar range (CC_KIND_BAR coordinates: bar index 0..7) but
 *             the destination is an absolute step (0..127). The paste engine
 *             resolves this through ccSvc_pasteGeometry() which uses bar-scale
 *             source geometry (src->start * 16, count * 16) and step-scale
 *             destination geometry (job->start as absolute step).
 * Why:        allows the user to paste a copied bar starting at any step, not
 *             only at a bar boundary. The source kind remains CC_KIND_BAR (the
 *             source was selected with a SELECT button); only the job kind
 *             changes to CC_KIND_BAR_TO_STEP to signal the cross-kind
 *             interpretation. Value 6 fits in the 3-bit trace field
 *             (ccTrace_job: job->kind & 0x07u).
 * Inputs:     set by cc_copySeq() when a bar source accepts a SEQ destination.
 * Outputs:    consumed by ccSvc_pasteGeometry(), ccSvc_pasteTriggersNow(),
 *             ccCopy_runJob(), and the trace encoder.
 * Affiliates: CC_KIND_BAR, CC_KIND_STEP, ccSvc_pasteGeometry().
 */
typedef enum {
    CC_KIND_NONE = 0u,
    CC_KIND_STEP,
    CC_KIND_BAR,
    CC_KIND_TRACK,
    CC_KIND_SCENE,
    CC_KIND_FX_STEP,
    CC_KIND_BAR_TO_STEP
} cc_kind_t;

/*
 * One source or held clear object (6 B).
 *
 * kind: cc_kind_t. scene: the Scene it belongs to. track: 0..6 for step, bar
 * and track objects. start/end: absolute step 0..127, bar 0..7, FX step
 * 0..15 or Scene 0..15, in press order (start may exceed end: reversed).
 * A single object has start == end. reserved: spare, kept zero.
 */
typedef struct {
    uint8_t kind;
    uint8_t scene;
    uint8_t track;
    uint8_t start;
    uint8_t end;
    uint8_t reserved;
} cc_source_t;

/* Which selection list the menu shows (copyOps.h / clearOps.h own labels). */
typedef enum {
    CC_MENU_NONE = 0u,
    CC_MENU_COPY_STEP,
    CC_MENU_COPY_BAR,
    CC_MENU_COPY_TRACK,
    CC_MENU_COPY_SCENE,
    CC_MENU_COPY_FX,
    CC_MENU_CLEAR_STEP,
    CC_MENU_CLEAR_BAR,
    CC_MENU_CLEAR_TRACK,
    CC_MENU_CLEAR_TRACK_FX,
    CC_MENU_CLEAR_SCENE
} cc_menu_t;

/*
 * Automation targets under one endless pot (resolved by Menu's
 * menu_knobClearTarget()). pattern_target: Pattern automation ID or
 * INSTRUMENT_PARAM_INVALID (0xFFFF). fx_lane: FX sequence lane 0..15 or 0xFF
 * for none.
 */
typedef struct {
    uint16_t pattern_target;
    uint8_t fx_lane;
} cc_pot_target_t;

/*
 * Boot initialization. Inputs: none. Output: every operation, source, row
 * stack and edge mask is idle/empty and ccSvc_init() has run. Caller: main.c
 * after patSvc_init(). Affiliates: copyClearService.c.
 */
void copyClear_init(void);

/*
 * Copy/clear button press (called by buttonHandler only when the press is not
 * the recording+running erase gesture).
 * Inputs: shift_held = SHIFT state at the press. Output: nonzero when an
 * operation was armed (copy) or a clear operation began; zero when refused
 * silently (recording/erasing, Load/Save or Instrument Load busy, LOAD/SAVE,
 * MENU or SOM mode, or pastes/clears of the previous operation still queued:
 * they read that operation's source). LEDs: copy -> copy/clear LED steady
 * until its menu opens, then blinking; clear -> SHIFT and copy/clear blink,
 * latched until release (F1-B). Refusals are silent and traced in DEV builds.
 * Affiliates: buttonHandler.c processPress() case BUT_COPY.
 */
uint8_t copyClear_copyPressed(uint8_t shift_held);

/*
 * Copy/clear button release: ends menu interaction (spec §3).
 * Output: no further presses act; held clear objects apply nothing; the
 * menu closes; LEDs return to the mode's state; queued work keeps running and
 * the AutoSave/maintenance suspension lasts until ccSvc reports idle.
 * Affiliates: buttonHandler.c processRelease() case BUT_COPY,
 * ccSvc_interactionEnded().
 */
void copyClear_copyReleased(void);

/*
 * Route one press/release while an operation is armed or running.
 * Inputs: BUT_* number. Output: nonzero when copy/clear consumed the edge
 * (the caller must not process it further); zero when the normal handler
 * should run (navigation). Consumed SEQ/SELECT/TRACK presses record their
 * button in an edge mask so the matching release is consumed too, even after
 * the copy/clear button has been released. Spec §7 routing tables.
 * Affiliates: buttonHandler.c processPress()/processRelease().
 */
uint8_t copyClear_buttonPressed(uint8_t buttonNr);
uint8_t copyClear_buttonReleased(uint8_t buttonNr);

/*
 * After every processed button event (consumed or not).
 * Output: re-asserts the copy/clear and SHIFT LED blinks because a mode change
 * clears blink slots. No source-row LED is owned; the menu indicator carries
 * the source. Cheap and idempotent. Caller: buttonHandler_processEvents().
 */
void copyClear_postEvent(void);

/*
 * Button-event ring overflow: forget every edge mask and the row stack (a
 * release may have been lost). Returns 0 (kept for the caller's cast).
 */
uint8_t copyClear_eventOverflow(void);

/*
 * Menu drawing. copyClear_menuVisible() is nonzero while a menu is set in any
 * held phase: copy from the first provisional source press, and clear from
 * the first object press until release. copyClear_formatMenu() fills two
 * 16-character rows (NUL at index 16): row 0 `Copy`/`Clear` at column 0 and
 * the source indicator from column 8 (the 9th character), row 1 the bracketed
 * selection label.
 * Callers: menu_repaint(), va_queueMarkerTransaction().
 */
uint8_t copyClear_menuVisible(void);
void copyClear_formatMenu(char row0[17], char row1[17]);

/*
 * Encoder and pot ownership while the copy/clear button is held.
 * copyClear_ownsEncoder()/copyClear_ownsPots() are nonzero for the whole
 * hold, so no parameter value changes (spec §3.2). copyClear_encoderTurned()
 * moves the selection (clamped, no wrap) when a menu is shown and does
 * nothing otherwise; encoder clicks are ignored (user, B18) and never reach
 * this module. copyClear_potTurned() starts a pot clear in a clear operation
 * when no menu is shown (spec §6) and does nothing otherwise; once a clear
 * object menu is open, pots do nothing until copy/clear release (F1-G). It
 * returns nonzero when something was cleared or registered. Callers:
 * menu_parseEncoder(), menu_parseKnobDelta().
 */
uint8_t copyClear_ownsEncoder(void);
void copyClear_encoderTurned(int8_t inc);
uint8_t copyClear_ownsPots(void);
uint8_t copyClear_potTurned(const cc_pot_target_t *target);

/*
 * Report whether the held copy/clear gesture is a clear (not a copy).
 *
 * What: nonzero only while CC_OP_CLEAR is the active phase, i.e. SHIFT was
 * held when the copy/clear button went down. Why: copyClear_ownsPots() is
 * true for both copy and clear, but the STEP held-step track automation
 * overlay (S078 P2 §8) must intercept a pot turn only to remove automation
 * from the held steps; in copy mode the same pot turn must stay inert.
 * Inputs: cc_state.phase. Output: 1 for clear, 0 for armed-copy/copy/none.
 * Callers: menu_parseKnobDelta().
 */
uint8_t copyClear_isClearMode(void);

/*
 * AutoSave and Pattern maintenance suspension (spec §9.2).
 * Output: nonzero from the start of an operation (first copy object press or
 * pot clear) until the copy/clear button has been released and the service
 * has finished every queued paste/clear, the register, every apply worker it
 * started, and the name write. Callers: filesystem_tick() scheduler gates,
 * patSvc_tick() repair gate.
 */
uint8_t copyClear_backgroundSuspended(void);

/*
 * Called by ccSvc when all background work is done after interaction ended.
 * Output: the source is forgotten, the suspension ends, the copy/clear-owned
 * LEDs are removed. Caller: ccSvc_tick().
 */
void copyClear_serviceFinished(void);

/* Read-only source access for copyOps/clearOps/ccSvc (operation lifetime). */
const cc_source_t *copyClear_source(void);

#endif /* COPY_CLEAR_SESSION_H_ */
