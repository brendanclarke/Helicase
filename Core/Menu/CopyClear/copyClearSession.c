/*
 * copyClearSession.c — Phase 6 copy/clear operation state and button routing.
 *
 * Contract in copyClearSession.h. Implementation of the routing tables in
 * spec §7 and the range rule in spec §4.2: while the copy/clear button is
 * held this module sees every SEQ, SELECT, TRACK, BAR and MODE edge first and
 * either consumes it (sources, pastes, clear objects, refused modes) or lets
 * the normal handler run (navigation). Consumed SEQ/SELECT/TRACK presses are
 * paired with their releases through edge masks, like
 * buttonHandler_loadSceneSeqPressedMask. Pastes and clears are queued in the
 * background service; this file never mutates Pattern or Scene data itself
 * except through ccClear_fxStepNow()/ccClear_potTurned() (immediate clears).
 */

#include "copyClearSession.h"
#include "copyClearService.h"
#include "copyOps.h"
#include "clearOps.h"
#include "buttonHandler.h"
#include "ledHandler.h"
#include "menu.h"
#include "sequencer.h"
#include "SceneData.h"
#include "PatternData.h"
#include "EffectTypes.h"
#include <string.h>

/* Held-row families for the range rule (cc_state.row). */
#define CC_ROW_NONE   0u
#define CC_ROW_SEQ    1u
#define CC_ROW_SELECT 2u

/* cc_state.flags bits. */
#define CC_FLAG_PAIR      0x01u   /* a start/end pair exists for this hold */
#define CC_FLAG_OBJECT    0x02u   /* a clear object is held (menu open)    */

/*
 * Operation state (6 B): phase (cc_op_t), started flag (suspension begun),
 * menu kind (cc_menu_t), selection index, held row family (CC_ROW_*), flags
 * (CC_FLAG_*). Lifetime: firmware; reset at copy/clear release (phase, menu,
 * row, flags) and at copyClear_serviceFinished() (started).
 */
static struct {
    uint8_t phase;
    uint8_t started;
    uint8_t menu;
    uint8_t selection;
    uint8_t row;
    uint8_t flags;
} cc_state;

/* Source (copy) or held object (clear), spec §4.2 range rule. 6 B. */
static cc_source_t cc_source;

/*
 * Press-order stack for the held row (9 B): 16 four-bit row indices (SEQ,
 * SELECT and FX), oldest first, plus a count. Absolute steps are formed at
 * each press because four bits cannot represent bars 2..8. Accessors:
 * cc_rowPush/cc_rowRemove/cc_rowTop.
 */
static uint8_t cc_rowStack[8];
static uint8_t cc_rowCount;

/*
 * Consumed-edge masks (4 B): bit N set when copy/clear consumed the press of
 * SEQ N+1, SELECT N+1 or TRACK N+1, so the matching release is consumed too,
 * even after the copy/clear button has been released. Consumed MODE and BAR
 * presses are not paired: their release handlers only restore LEDs/overlays
 * and are harmless after a consumed press.
 */
static uint16_t cc_seqMask;
static uint8_t cc_selectMask;
static uint8_t cc_trackMask;

/* ---- row stack (spec §4.2) ------------------------------------------- */

static uint8_t cc_rowGet(uint8_t i)
{
    return (uint8_t)((cc_rowStack[i >> 1u] >> ((i & 1u) * 4u)) & 0x0Fu);
}

static void cc_rowSet(uint8_t i, uint8_t v)
{
    uint8_t shift = (uint8_t)((i & 1u) * 4u);

    cc_rowStack[i >> 1u] = (uint8_t)((cc_rowStack[i >> 1u] &
                                      (uint8_t)~(0x0Fu << shift)) |
                                     (uint8_t)((v & 0x0Fu) << shift));
}

static uint8_t cc_rowTop(void)
{
    return cc_rowCount ? cc_rowGet((uint8_t)(cc_rowCount - 1u)) : 0u;
}

/* Absolute coordinate for the current source kind and a raw row index. */
static uint8_t cc_rowAbsolute(uint8_t index)
{
    return (cc_source.kind == CC_KIND_STEP)
               ? (uint8_t)buttonHandler_visibleStep(index)
               : index;
}

/*
 * Range rule, press side: a press while another button of the same row is
 * held makes a pair (start = most recent still-held button, end = this one);
 * later pairs replace earlier ones. The stack stores raw row indices.
 */
static void cc_rowPush(uint8_t index)
{
    if (cc_rowCount == 0u) {
        cc_source.start = cc_rowAbsolute(index);
        cc_source.end = cc_source.start;
        cc_state.flags = (uint8_t)(cc_state.flags & (uint8_t)~CC_FLAG_PAIR);
    } else {
        cc_source.start = cc_rowAbsolute(cc_rowTop());
        cc_source.end = cc_rowAbsolute(index);
        cc_state.flags = (uint8_t)(cc_state.flags | CC_FLAG_PAIR);
    }
    if (cc_rowCount < 16u) {
        cc_rowSet(cc_rowCount, index);
        cc_rowCount++;
    }
}

/* Range rule, release side: drop the button; returns nonzero when empty. */
static uint8_t cc_rowRemove(uint8_t index)
{
    uint8_t i;
    uint8_t j;

    for (i = 0u; i < cc_rowCount; i++) {
        if (cc_rowGet(i) != index)
            continue;
        for (j = i; (uint8_t)(j + 1u) < cc_rowCount; j++)
            cc_rowSet(j, cc_rowGet((uint8_t)(j + 1u)));
        cc_rowCount--;
        break;
    }
    return (uint8_t)(cc_rowCount == 0u);
}

static void cc_rowReset(void)
{
    memset(cc_rowStack, 0, sizeof(cc_rowStack));
    cc_rowCount = 0u;
    cc_state.row = CC_ROW_NONE;
}

/* ---- small helpers ---------------------------------------------------- */

/* The active Scene (viewed and written; spec §1). */
static uint8_t cc_activeScene(void)
{
    return menu_getViewedPattern();
}

/* Suspension begins with the first object press or pot clear (spec §9.2). */
static void cc_start(void)
{
    cc_state.started = 1u;
}

/*
 * Drive only the copy/clear and SHIFT LEDs from operation state (F1-B).
 * Source indication is the menu's text; no source-row LED is owned here.
 */
static void cc_applyButtonLeds(void)
{
    if (cc_state.phase == CC_OP_CLEAR) {
        led_setBlinkLed(LED_SHIFT, 1u);
        led_setBlinkLed(LED_COPY, 1u);
    } else if (cc_state.menu != CC_MENU_NONE) {
        led_setBlinkLed(LED_COPY, 1u);
    } else {
        led_setBlinkLed(LED_COPY, 0u);
        led_setValue(1u, LED_COPY);
    }
}

/* Mode reached by a MODE button press, exactly as handleModeButtons(). */
static uint8_t cc_modeForButton(uint8_t buttonNr)
{
    uint8_t mode = (uint8_t)(BUT_MODE1 - buttonNr);

    if (buttonHandler_getShift())
        mode = (uint8_t)((mode + 4u) & 7u);
    return mode;
}

static uint8_t cc_modeAllowed(uint8_t mode)
{
    return (uint8_t)(mode == SELECT_MODE_VOICE || mode == SELECT_MODE_STEP ||
                     mode == SELECT_MODE_PERF || mode == SELECT_MODE_FX);
}

/* Open a copy/clear menu at its default selection. */
static void cc_openMenu(cc_menu_t menu)
{
    cc_state.menu = (uint8_t)menu;
    cc_state.selection = 0u;
    cc_applyButtonLeds();
    menu_copyClearMenuChanged();
}

static uint8_t cc_selectionCount(void)
{
    if (cc_state.phase == CC_OP_CLEAR)
        return ccClear_selectionCount((cc_menu_t)cc_state.menu);
    return ccCopy_selectionCount((cc_menu_t)cc_state.menu);
}

/*
 * Commit the copy source (F1-C); row menus stay open with their selection.
 */
static void cc_commitSource(void)
{
    cc_state.phase = CC_OP_COPY;
    cc_start();
    if (cc_state.menu == CC_MENU_NONE)
        cc_openMenu(ccCopy_menuForSource(&cc_source));
    ccTrace(AUTOSAVE_TRACE_CC_EVT_SOURCE_SET,
            (uint32_t)(cc_source.kind & 0x7u) |
            ((uint32_t)(cc_source.scene & 0xFu) << 3u) |
            ((uint32_t)(cc_source.track & 0x7u) << 7u) |
            ((uint32_t)(cc_source.start & 0x7Fu) << 10u) |
            ((uint32_t)(cc_source.end & 0x7Fu) << 17u) |
            ((uint32_t)(buttonHandler_getMode() & 0x7u) << 24u));
}

/*
 * Flash every visible LED of a paste destination or clear object once (F1-F).
 * A range flashes the full visible object, not only its final button.
 */
static void cc_flashObject(cc_kind_t kind, uint8_t scene, uint8_t track,
                           uint8_t first, uint8_t count)
{
    uint8_t mode = buttonHandler_getMode();
    uint8_t viewed = (uint8_t)(scene == cc_activeScene());
    uint16_t seq = 0u;
    uint8_t sel = 0u;
    uint8_t voice = 0u;
    uint8_t i;

    switch (kind) {
    case CC_KIND_STEP:
        if ((mode == SELECT_MODE_VOICE || mode == SELECT_MODE_STEP) &&
            viewed && track == menu_getActiveVoice())
            for (i = 0u; i < count; i++) {
                uint8_t s = (uint8_t)((first + i) & (NUM_STEPS - 1u));

                if ((uint8_t)(s / NUM_STEPS_PER_BAR) == menu_currentBar)
                    seq = (uint16_t)(seq | (uint16_t)(1u << (s % 16u)));
            }
        break;
    case CC_KIND_BAR:
        if (mode == SELECT_MODE_STEP && viewed)
            for (i = 0u; i < count; i++) {
                uint8_t b = (uint8_t)((first + i) & (NUM_BARS - 1u));

                sel = (uint8_t)(sel | (uint8_t)(1u << b));
                if (b == menu_currentBar && track == menu_getActiveVoice())
                    seq = 0xFFFFu;
            }
        break;
    case CC_KIND_TRACK:
        if (viewed)
            voice = (uint8_t)(1u << (track & 7u));
        break;
    case CC_KIND_SCENE:
        if (mode == SELECT_MODE_PERF)
            seq = (uint16_t)(1u << (scene & 15u));
        break;
    case CC_KIND_FX_STEP:
        if (mode == SELECT_MODE_FX && viewed)
            for (i = 0u; i < count; i++)
                seq = (uint16_t)(seq | (uint16_t)(1u << ((first + i) & 15u)));
        break;
    default:
        break;
    }
    if (seq)
        led_flashGroup(LED_FLASH_GROUP_SEQ, seq);
    if (sel)
        led_flashGroup(LED_FLASH_GROUP_SELECT, sel);
    if (voice)
        led_flashGroup(LED_FLASH_GROUP_VOICE, voice);
}

static uint8_t cc_sourceLength(void)
{
    uint8_t lo = (cc_source.start < cc_source.end) ? cc_source.start
                                                   : cc_source.end;
    uint8_t hi = (cc_source.start < cc_source.end) ? cc_source.end
                                                   : cc_source.start;

    return (uint8_t)(hi - lo + 1u);
}

/* ---- copy routing (spec §7.1) ----------------------------------------- */

/*
 * One copy-source row press: open the menu provisionally on the first press,
 * then keep the most recent held row as the range anchor. Other row families
 * are consumed while a family is held.
 */
static uint8_t cc_copyRowPress(uint8_t row, cc_kind_t kind, uint8_t track,
                               uint8_t index)
{
    if (cc_state.row != CC_ROW_NONE && cc_state.row != row)
        return 1u;
    if (cc_rowCount == 0u) {
        cc_source.kind = (uint8_t)kind;
        cc_source.scene = cc_activeScene();
        cc_source.track = track;
    }
    cc_state.row = row;
    cc_rowPush(index);
    if (cc_state.menu == CC_MENU_NONE) {
        cc_start();
        cc_openMenu(ccCopy_menuForSource(&cc_source));
    } else {
        menu_copyClearMenuChanged();
    }
    return 1u;
}

static uint8_t cc_copySeq(uint8_t index)
{
    uint8_t mode = buttonHandler_getMode();
    uint8_t has_source = (uint8_t)(cc_state.phase == CC_OP_COPY);

    if (mode == SELECT_MODE_VOICE || mode == SELECT_MODE_STEP) {
        if (!has_source)
            return cc_copyRowPress(CC_ROW_SEQ, CC_KIND_STEP,
                                   menu_getActiveVoice(), index);
        if (cc_source.kind == CC_KIND_STEP) {
            if (ccCopy_requestPaste(&cc_source, cc_state.selection,
                                    cc_activeScene(), menu_getActiveVoice(),
                                    buttonHandler_visibleStep(index)))
                cc_flashObject(CC_KIND_STEP, cc_activeScene(),
                               menu_getActiveVoice(),
                               buttonHandler_visibleStep(index),
                               cc_sourceLength());
        }
        /*
         * What:       bar-to-step paste. When the copy source is a bar or bar
         *             range, a SEQ press pastes the bar content starting at
         *             the pressed step. The job kind is set to
         *             CC_KIND_BAR_TO_STEP so the geometry function uses bar
         *             source coordinates and step destination coordinates.
         *             The flash uses CC_KIND_STEP because the destination is
         *             a step range (the user sees step LEDs flash, not bar
         *             LEDs). The step count is sourceLength * NUM_STEPS_PER_BAR
         *             (each bar expands to 16 steps).
         * Why:        a bar is a contiguous block of steps; pasting to a step
         *             destination gives sub-bar placement precision. The
         *             reverse (step source -> bar destination) is navigation,
         *             not a paste, by user rule.
         * Inputs:     cc_source (kind == CC_KIND_BAR), index (SEQ button).
         * Outputs:    queued CC_KIND_BAR_TO_STEP job via ccCopy_requestPaste.
         * Affiliates: cc_copySelect() (bar-to-bar paste, unchanged),
         *             CC_KIND_BAR_TO_STEP, ccSvc_pasteGeometry().
         */
        else if (cc_source.kind == CC_KIND_BAR) {
            if (ccCopy_requestBarToStep(&cc_source, cc_state.selection,
                                        cc_activeScene(),
                                        menu_getActiveVoice(),
                                        buttonHandler_visibleStep(index)))
                cc_flashObject(CC_KIND_STEP, cc_activeScene(),
                               menu_getActiveVoice(),
                               buttonHandler_visibleStep(index),
                               (uint8_t)(cc_sourceLength() *
                                         NUM_STEPS_PER_BAR));
        }
        return 1u;
    }
    if (mode == SELECT_MODE_PERF) {
        if (!has_source) {
            /* Scene sources are set on press (spec §4.1). */
            cc_source.kind = CC_KIND_SCENE;
            cc_source.scene = index;
            cc_source.track = 0u;
            cc_source.start = index;
            cc_source.end = index;
            cc_commitSource();
            return 1u;
        }
        if (cc_source.kind == CC_KIND_SCENE) {
            if (ccCopy_requestPaste(&cc_source, cc_state.selection, index, 0u,
                                    index))
                cc_flashObject(CC_KIND_SCENE, index, 0u, index, 1u);
            return 1u;
        }
        /* Other sources: PERF SEQ selects the active Scene (navigation). */
        return 0u;
    }
    if (mode == SELECT_MODE_FX) {
        if (!has_source)
            return cc_copyRowPress(CC_ROW_SEQ, CC_KIND_FX_STEP, 0u, index);
        if (cc_source.kind == CC_KIND_FX_STEP) {
            if (ccCopy_requestPaste(&cc_source, cc_state.selection,
                                    cc_activeScene(), 0u, index))
                cc_flashObject(CC_KIND_FX_STEP, cc_activeScene(), 0u, index,
                               cc_sourceLength());
        }
        return 1u;
    }
    return 1u;
}

static uint8_t cc_copySelect(uint8_t index)
{
    uint8_t has_source = (uint8_t)(cc_state.phase == CC_OP_COPY);

    if (buttonHandler_getMode() != SELECT_MODE_STEP)
        return 0u;
    if (!has_source)
        return cc_copyRowPress(CC_ROW_SELECT, CC_KIND_BAR,
                               menu_getActiveVoice(), index);
    switch (cc_source.kind) {
    case CC_KIND_STEP:
    case CC_KIND_TRACK:
        return 0u;          /* navigate (bar) / normal */
    case CC_KIND_BAR:
        if (ccCopy_requestPaste(&cc_source, cc_state.selection,
                                cc_activeScene(), menu_getActiveVoice(),
                                index))
            cc_flashObject(CC_KIND_BAR, cc_activeScene(),
                           menu_getActiveVoice(), index, cc_sourceLength());
        return 1u;
    default:
        return 1u;
    }
}

static uint8_t cc_copyTrack(uint8_t index)
{
    uint8_t mode = buttonHandler_getMode();

    if (cc_state.phase != CC_OP_COPY) {
        if (cc_state.row != CC_ROW_NONE)
            return 1u;
        /* Track sources are set on press (spec §4.1). */
        cc_source.kind = CC_KIND_TRACK;
        cc_source.scene = cc_activeScene();
        cc_source.track = index;
        cc_source.start = index;
        cc_source.end = index;
        cc_commitSource();
        return 1u;
    }
    switch (cc_source.kind) {
    case CC_KIND_STEP:
    case CC_KIND_BAR:
        /* Navigate in VOICE/STEP; PERF/EFFECTS use TRACK for mutes. */
        return (uint8_t)(mode == SELECT_MODE_VOICE ||
                         mode == SELECT_MODE_STEP) ? 0u : 1u;
    case CC_KIND_TRACK:
        if (ccCopy_requestPaste(&cc_source, cc_state.selection,
                                cc_activeScene(), index, index))
            cc_flashObject(CC_KIND_TRACK, cc_activeScene(), index, index, 1u);
        return 1u;
    default:
        return 1u;
    }
}

/* ---- clear routing (spec §7.2) ---------------------------------------- */

/*
 * Capture a clear object and show its menu. Keep the previous selection for
 * another object in the same button group; a new group starts at cancel.
 */
static void cc_clearObject(cc_kind_t kind, uint8_t scene, uint8_t track,
                           uint8_t index)
{
    cc_kind_t previous_kind = (cc_kind_t)cc_source.kind;
    cc_menu_t menu = ccClear_menuForObject(buttonHandler_getMode(), kind);

    cc_source.kind = (uint8_t)kind;
    cc_source.scene = scene;
    cc_source.track = track;
    cc_source.start = index;
    cc_source.end = index;
    cc_state.flags = (uint8_t)(cc_state.flags | CC_FLAG_OBJECT);
    cc_start();
    if (kind != previous_kind ||
        cc_state.selection >= ccClear_selectionCount(menu))
        cc_state.selection = 0u;
    cc_state.menu = (uint8_t)menu;
    cc_applyButtonLeds();
    menu_copyClearMenuChanged();
}

static uint8_t cc_clearPress(uint8_t row, uint8_t index)
{
    uint8_t mode = buttonHandler_getMode();

    if (row == CC_ROW_SEQ && mode == SELECT_MODE_FX) {
        /* EFFECTS SEQ clears at once, without a menu (spec §5). */
        if ((cc_state.flags & CC_FLAG_OBJECT) != 0u)
            return 1u;
        ccClear_fxStepNow(cc_activeScene(), index);
        cc_start();
        cc_flashObject(CC_KIND_FX_STEP, cc_activeScene(), 0u, index, 1u);
        return 1u;
    }
    if ((cc_state.flags & CC_FLAG_OBJECT) != 0u) {
        /* One object at a time; a second press of the same row is a range. */
        if (cc_state.row == row && row != CC_ROW_NONE &&
            (cc_source.kind == CC_KIND_STEP || cc_source.kind == CC_KIND_BAR)) {
            cc_rowPush(index);
            menu_copyClearMenuChanged();
        }
        return 1u;
    }
    if (row == CC_ROW_SEQ && (mode == SELECT_MODE_VOICE ||
                              mode == SELECT_MODE_STEP)) {
        cc_rowReset();
        cc_state.row = CC_ROW_SEQ;
        cc_clearObject(CC_KIND_STEP, cc_activeScene(), menu_getActiveVoice(),
                       0u);
        cc_rowPush(index);
        menu_copyClearMenuChanged();
        return 1u;
    }
    if (row == CC_ROW_SEQ && mode == SELECT_MODE_PERF) {
        cc_clearObject(CC_KIND_SCENE, index, 0u, index);
        return 1u;
    }
    if (row == CC_ROW_SELECT) {
        if (mode != SELECT_MODE_STEP)
            return 0u;
        cc_rowReset();
        cc_state.row = CC_ROW_SELECT;
        cc_clearObject(CC_KIND_BAR, cc_activeScene(), menu_getActiveVoice(),
                       0u);
        cc_rowPush(index);
        menu_copyClearMenuChanged();
        return 1u;
    }
    return 1u;
}

/* Queue the clear for the released object unless `cancel` is shown. */
static void cc_clearReleaseObject(void)
{
    uint8_t lo;

    if ((cc_state.flags & CC_FLAG_OBJECT) == 0u)
        return;
    cc_state.flags = (uint8_t)(cc_state.flags & (uint8_t)~CC_FLAG_OBJECT);
    if (cc_state.selection == 0u)
        return;
    if (!ccClear_requestClear(&cc_source, cc_state.selection))
        return;
    lo = (cc_source.start < cc_source.end) ? cc_source.start : cc_source.end;
    cc_flashObject((cc_kind_t)cc_source.kind, cc_source.scene,
                   cc_source.track,
                   (cc_source.kind == CC_KIND_TRACK) ? cc_source.track : lo,
                   cc_sourceLength());
}

/* ---- public API -------------------------------------------------------- */

void copyClear_init(void)
{
    memset(&cc_state, 0, sizeof(cc_state));
    memset(&cc_source, 0, sizeof(cc_source));
    cc_rowReset();
    cc_seqMask = 0u;
    cc_selectMask = 0u;
    cc_trackMask = 0u;
    ccSvc_init();
}

uint8_t copyClear_copyPressed(uint8_t shift_held)
{
    uint8_t mode = buttonHandler_getMode();
    uint32_t refused = 0u;

    /* Refusals remain silent; DEV trace records every applicable reason. */
    if (seq_recordActive)                      refused |= 1u << 0u;
    if (seq_eraseActive)                       refused |= 1u << 1u;
    if (menu_isStorageBusy())                  refused |= 1u << 2u;
    if (menu_loadInstrumentTransactionBusy())  refused |= 1u << 3u;
    if (!cc_modeAllowed(mode))                 refused |= 1u << 4u;
    if (ccSvc_jobCount() != 0u)                refused |= 1u << 5u;
    if (refused != 0u) {
        ccTrace(AUTOSAVE_TRACE_CC_EVT_OP_REFUSED,
                refused | ((uint32_t)(mode & 0x7u) << 8u) |
                ((uint32_t)ccSvc_jobCount() << 16u));
        return 0u;
    }
    cc_rowReset();
    cc_state.menu = CC_MENU_NONE;
    cc_state.selection = 0u;
    cc_state.flags = 0u;
    memset(&cc_source, 0, sizeof(cc_source));
    ccSvc_interactionStarted();
    cc_state.phase = shift_held ? CC_OP_CLEAR : CC_OP_ARMED_COPY;
    cc_applyButtonLeds();
    ccTrace(AUTOSAVE_TRACE_CC_EVT_OP_START,
            (uint32_t)(cc_state.phase & 0x3u) |
            ((uint32_t)(mode & 0x7u) << 2u) |
            ((uint32_t)ccSvc_traceOpSequence() << 8u) |
            ((uint32_t)ccSvc_jobCount() << 16u));
    return 1u;
}

void copyClear_copyReleased(void)
{
    uint8_t menu_was_visible;

    if (cc_state.phase == CC_OP_NONE)
        return;
    menu_was_visible = copyClear_menuVisible();
    /* A held object or an uncommitted row applies nothing (spec §3.2 step 7). */
    cc_rowReset();
    cc_state.flags = 0u;
    cc_state.menu = CC_MENU_NONE;
    cc_state.selection = 0u;
    cc_state.phase = CC_OP_NONE;
    led_setBlinkLed(LED_COPY, 0u);
    led_setBlinkLed(LED_SHIFT, 0u);
    led_setValue(0u, LED_COPY);
    led_setValue(buttonHandler_getShift(), LED_SHIFT);
    if (menu_was_visible)
        menu_copyClearMenuClosed();
    ccTrace(AUTOSAVE_TRACE_CC_EVT_OP_RELEASE,
            (uint32_t)(cc_state.started ? 1u : 0u) |
            ((uint32_t)(menu_was_visible ? 1u : 0u) << 1u) |
            ((uint32_t)ccSvc_jobCount() << 8u));
    ccSvc_interactionEnded();
    if (!cc_state.started)
        memset(&cc_source, 0, sizeof(cc_source));
}

uint8_t copyClear_buttonPressed(uint8_t buttonNr)
{
    int8_t seq;
    int8_t sel;
    int8_t trk;
    uint8_t consumed;

    if (cc_state.phase == CC_OP_NONE)
        return 0u;
    if (buttonNr == BUT_SHIFT)
        return 0u;     /* MODE modifier only; its LED layer stays (spec §7). */
    if (buttonNr == BUT_MODE1 || buttonNr == BUT_MODE2 ||
        buttonNr == BUT_MODE3 || buttonNr == BUT_MODE4) {
        /* LOAD/SAVE, MENU and SOM are not reachable during an operation. */
        return cc_modeAllowed(cc_modeForButton(buttonNr)) ? 0u : 1u;
    }
    if (buttonNr == BUT_BAR1 || buttonNr == BUT_BAR2) {
        /* Ranges never cross bars; FX sources ignore BAR (spec §4.2, §7.1). */
        return (uint8_t)(cc_rowCount != 0u ||
                         (cc_state.phase == CC_OP_COPY &&
                          cc_source.kind == CC_KIND_FX_STEP));
    }

    seq = buttonHandler_seqIndex(buttonNr);
    sel = buttonHandler_selectIndex(buttonNr);
    trk = buttonHandler_voiceIndex(buttonNr);
    if (seq >= 0) {
        consumed = (cc_state.phase == CC_OP_CLEAR)
                       ? cc_clearPress(CC_ROW_SEQ, (uint8_t)seq)
                       : cc_copySeq((uint8_t)seq);
        if (consumed)
            cc_seqMask = (uint16_t)(cc_seqMask | (uint16_t)(1u << seq));
        return consumed;
    }
    if (sel >= 0) {
        consumed = (cc_state.phase == CC_OP_CLEAR)
                       ? cc_clearPress(CC_ROW_SELECT, (uint8_t)sel)
                       : cc_copySelect((uint8_t)sel);
        if (consumed)
            cc_selectMask = (uint8_t)(cc_selectMask | (uint8_t)(1u << sel));
        return consumed;
    }
    if (trk >= 0) {
        if (cc_state.phase == CC_OP_CLEAR) {
            if ((cc_state.flags & CC_FLAG_OBJECT) == 0u) {
                cc_rowReset();
                cc_clearObject(CC_KIND_TRACK, cc_activeScene(), (uint8_t)trk,
                               (uint8_t)trk);
            }
            consumed = 1u;
        } else {
            consumed = cc_copyTrack((uint8_t)trk);
        }
        if (consumed)
            cc_trackMask = (uint8_t)(cc_trackMask | (uint8_t)(1u << trk));
        return consumed;
    }
    return 0u;
}

uint8_t copyClear_buttonReleased(uint8_t buttonNr)
{
    int8_t seq = buttonHandler_seqIndex(buttonNr);
    int8_t sel = buttonHandler_selectIndex(buttonNr);
    int8_t trk = buttonHandler_voiceIndex(buttonNr);
    uint8_t row = CC_ROW_NONE;
    uint8_t index = 0u;

    if (seq >= 0 && (cc_seqMask & (uint16_t)(1u << seq)) != 0u) {
        cc_seqMask = (uint16_t)(cc_seqMask & (uint16_t)~(1u << seq));
        row = CC_ROW_SEQ;
        index = (uint8_t)seq;
    } else if (sel >= 0 && (cc_selectMask & (uint8_t)(1u << sel)) != 0u) {
        cc_selectMask = (uint8_t)(cc_selectMask & (uint8_t)~(1u << sel));
        row = CC_ROW_SELECT;
        index = (uint8_t)sel;
    } else if (trk >= 0 && (cc_trackMask & (uint8_t)(1u << trk)) != 0u) {
        cc_trackMask = (uint8_t)(cc_trackMask & (uint8_t)~(1u << trk));
        if (cc_state.phase == CC_OP_CLEAR && cc_source.kind == CC_KIND_TRACK &&
            cc_source.track == (uint8_t)trk)
            cc_clearReleaseObject();
        return 1u;
    } else {
        return 0u;
    }

    if (cc_state.phase == CC_OP_NONE)
        return 1u;    /* interaction ended: the release only pairs its press */

    if (cc_state.row == row && row != CC_ROW_NONE) {
        /* The row stack stores raw row indices, not absolute steps. */
        if (!cc_rowRemove(index))
            return 1u;
        cc_state.row = CC_ROW_NONE;
        /* Last button of the row released: the source/object is final. */
        if (cc_state.phase == CC_OP_ARMED_COPY)
            cc_commitSource();
        else if (cc_state.phase == CC_OP_CLEAR)
            cc_clearReleaseObject();
        return 1u;
    }
    if (cc_state.phase == CC_OP_CLEAR && row == CC_ROW_SEQ &&
        cc_source.kind == CC_KIND_SCENE && cc_source.scene == index)
        cc_clearReleaseObject();
    return 1u;
}

void copyClear_postEvent(void)
{
    if (cc_state.phase == CC_OP_NONE)
        return;
    cc_applyButtonLeds();
}

uint8_t copyClear_eventOverflow(void)
{
    cc_seqMask = 0u;
    cc_selectMask = 0u;
    cc_trackMask = 0u;
    cc_rowReset();
    /* An object whose release was lost applies nothing. */
    cc_state.flags = (uint8_t)(cc_state.flags & (uint8_t)~CC_FLAG_OBJECT);
    return 0u;
}

uint8_t copyClear_menuVisible(void)
{
    return (uint8_t)(cc_state.phase != CC_OP_NONE &&
                     cc_state.menu != CC_MENU_NONE);
}

/* Append text to a fixed row at *pos, never past 16 characters. */
static void cc_put(char row[17], uint8_t *pos, const char *text)
{
    while (*text != '\0' && *pos < 16u)
        row[(*pos)++] = *text++;
}

/* Append a zero-padded one-based number of `digits` digits. */
static void cc_putNum(char row[17], uint8_t *pos, uint16_t value,
                      uint8_t digits)
{
    char buf[4];
    uint8_t i;

    if (digits > 3u)
        digits = 3u;
    for (i = 0u; i < digits; i++) {
        buf[digits - 1u - i] = (char)('0' + (value % 10u));
        value = (uint16_t)(value / 10u);
    }
    buf[digits] = '\0';
    cc_put(row, pos, buf);
}

/*
 * Source indicator (spec §8.1): at most 8 characters, one-based numbers.
 * Clear menus show the pressed object the same way; Scene objects take the
 * suffix of the selection shown.
 */
static void cc_formatIndicator(char row[17], uint8_t *pos)
{
    const cc_source_t *s = &cc_source;
    uint8_t single = (uint8_t)(s->start == s->end);

    switch (s->kind) {
    case CC_KIND_STEP:
        if (single) {
            cc_putNum(row, pos, (uint16_t)(s->scene + 1u), 2u);
            cc_put(row, pos, "T");
            cc_putNum(row, pos, (uint16_t)(s->track + 1u), 1u);
            cc_put(row, pos, "s");
            cc_putNum(row, pos, (uint16_t)(s->start + 1u), 3u);
        } else {
            cc_put(row, pos, "s");
            cc_putNum(row, pos, (uint16_t)(s->start + 1u), 3u);
            cc_put(row, pos, "-");
            cc_putNum(row, pos, (uint16_t)(s->end + 1u), 3u);
        }
        break;
    case CC_KIND_BAR:
        if (single) {
            cc_put(row, pos, "S");
            cc_putNum(row, pos, (uint16_t)(s->scene + 1u), 2u);
            cc_put(row, pos, "T");
            cc_putNum(row, pos, (uint16_t)(s->track + 1u), 1u);
            cc_put(row, pos, "b");
            cc_putNum(row, pos, (uint16_t)(s->start + 1u), 1u);
        } else {
            cc_put(row, pos, "b");
            cc_putNum(row, pos, (uint16_t)(s->start + 1u), 1u);
            cc_put(row, pos, "-");
            cc_putNum(row, pos, (uint16_t)(s->end + 1u), 1u);
        }
        break;
    case CC_KIND_TRACK:
        /*
         * What:       track source indicator. The middle letter identifies the
         *             copy selection: "T" for a whole track, "i" for
         *             instrument copy, "m" for inst -> morph (S076 P3,
         *             label S077 P4).
         * Why:        the menu label already distinguishes the selection, but
         *             the indicator keeps the source readable at a glance and
         *             matches the design's SNN{L}N form.
         * Affiliates: CC_COPY_TRACK, CC_COPY_INSTRUMENT, CC_COPY_MORPH.
         */
        cc_put(row, pos, "S");
        cc_putNum(row, pos, (uint16_t)(s->scene + 1u), 2u);
        if (cc_state.phase == CC_OP_COPY &&
            cc_state.selection == CC_COPY_INSTRUMENT)
            cc_put(row, pos, "i");
        else if (cc_state.phase == CC_OP_COPY &&
                 cc_state.selection == CC_COPY_MORPH)
            cc_put(row, pos, "m");
        else
            cc_put(row, pos, "T");
        cc_putNum(row, pos, (uint16_t)(s->track + 1u), 1u);
        break;
    case CC_KIND_SCENE:
        cc_put(row, pos, "S");
        cc_putNum(row, pos, (uint16_t)(s->scene + 1u), 2u);
        if (cc_state.phase == CC_OP_COPY) {
            /*
             * What:       Scene copy suffix by selection: "" scene, "c"
             *             settings, "K" kit, "f" effect, "P" pattern, "m"
             *             scene -> morph (S076 P3, label S077 P4).
             * Why:        CC_COPY_SCENE_MORPH = 5 needs its own indicator
             *             letter; the suffix "m" stays (one-character
             *             positional abbreviation, not the menu label).
             */
            static const char *const copy_suffix[] = {
                "", "c", "K", "f", "P", "m"
            };
            if (cc_state.selection < 6u)
                cc_put(row, pos, copy_suffix[cc_state.selection]);
        } else {
            static const char *const clear_suffix[] = {
                "", "", "c", "P", "P", "P", "f", "fs"
            };
            if (cc_state.selection < 8u)
                cc_put(row, pos, clear_suffix[cc_state.selection]);
        }
        break;
    case CC_KIND_FX_STEP:
        if (single) {
            cc_put(row, pos, "S");
            cc_putNum(row, pos, (uint16_t)(s->scene + 1u), 2u);
            cc_put(row, pos, "fs");
            cc_putNum(row, pos, (uint16_t)(s->start + 1u), 2u);
        } else {
            cc_put(row, pos, "fs");
            cc_putNum(row, pos, (uint16_t)(s->start + 1u), 2u);
            cc_put(row, pos, "-");
            cc_putNum(row, pos, (uint16_t)(s->end + 1u), 2u);
        }
        break;
    default:
        break;
    }
}

void copyClear_formatMenu(char row0[17], char row1[17])
{
    uint8_t pos = 0u;
    const char *label;

    if (!row0 || !row1)
        return;
    memset(row0, ' ', 16u);
    memset(row1, ' ', 16u);
    row0[16] = '\0';
    row1[16] = '\0';
    /*
     * Row 0: operation word, then the source indicator (S075 F2-C).
     *
     * What: `Copy` or `Clear` from column 0 (user: mixed case, full word),
     * then the source indicator from column 8, the 9th character (F1-D), so
     * the indicator never moves with the word length. Inputs: cc_state.phase.
     * Output: row0[0..15]. Affiliates: cc_formatIndicator(),
     * copyClear_menuVisible() contract in copyClearSession.h.
     */
    cc_put(row0, &pos, (cc_state.phase == CC_OP_CLEAR) ? "Clear" : "Copy");
    pos = 8u;
    cc_formatIndicator(row0, &pos);
    label = (cc_state.phase == CC_OP_CLEAR)
                ? ccClear_label((cc_menu_t)cc_state.menu, cc_state.selection)
                : ccCopy_label((cc_menu_t)cc_state.menu, cc_state.selection);
    row1[0] = '[';
    pos = 1u;
    cc_put(row1, &pos, label);
    row1[15] = ']';
}

uint8_t copyClear_ownsEncoder(void)
{
    return (uint8_t)(cc_state.phase != CC_OP_NONE);
}

void copyClear_encoderTurned(int8_t inc)
{
    int16_t next;
    uint8_t count;

    if (!copyClear_menuVisible() || inc == 0)
        return;
    count = cc_selectionCount();
    if (count == 0u)
        return;
    next = (int16_t)cc_state.selection + inc;
    if (next < 0)
        next = 0;
    if (next >= (int16_t)count)
        next = (int16_t)(count - 1u);
    if ((uint8_t)next == cc_state.selection)
        return;
    cc_state.selection = (uint8_t)next;
    menu_copyClearMenuChanged();
}

uint8_t copyClear_ownsPots(void)
{
    return (uint8_t)(cc_state.phase != CC_OP_NONE);
}

uint8_t copyClear_potTurned(const cc_pot_target_t *target)
{
    /* Pots never run while a clear object menu is visible. */
    if (cc_state.phase != CC_OP_CLEAR || copyClear_menuVisible() || !target)
        return 0u;
    if (!ccClear_potTurned(target))
        return 0u;
    cc_start();
    return 1u;
}

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
uint8_t copyClear_isClearMode(void)
{
    return (uint8_t)(cc_state.phase == CC_OP_CLEAR);
}

uint8_t copyClear_backgroundSuspended(void)
{
    return (uint8_t)((cc_state.phase != CC_OP_NONE && cc_state.started) ||
                     ccSvc_busy());
}

void copyClear_serviceFinished(void)
{
    if (cc_state.phase != CC_OP_NONE)
        return;
    memset(&cc_source, 0, sizeof(cc_source));
    cc_state.started = 0u;
}

const cc_source_t *copyClear_source(void)
{
    return &cc_source;
}
