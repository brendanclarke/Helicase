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
 * Press-order stack for the held row (9 B): 16 four-bit button indices,
 * oldest first, plus a count. Top = most recent button still held. Used only
 * for SEQ and SELECT rows. Accessors: cc_rowPush/cc_rowRemove/cc_rowTop.
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

/*
 * Range rule, press side: a press while another button of the same row is
 * held makes a pair (start = most recent still-held button, end = this one);
 * later pairs replace earlier ones. The first press of a hold is the single
 * object until a pair exists.
 */
static void cc_rowPush(uint8_t index)
{
    if (cc_rowCount == 0u) {
        cc_source.start = index;
        cc_source.end = index;
        cc_state.flags = (uint8_t)(cc_state.flags & (uint8_t)~CC_FLAG_PAIR);
    } else {
        cc_source.start = cc_rowTop();
        cc_source.end = index;
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

/* Open or refresh the menu for the current source/object at its default. */
static void cc_openMenu(cc_menu_t menu)
{
    cc_state.menu = (uint8_t)menu;
    cc_state.selection = 0u;
    menu_copyClearMenuChanged();
}

static uint8_t cc_selectionCount(void)
{
    if (cc_state.phase == CC_OP_CLEAR)
        return ccClear_selectionCount((cc_menu_t)cc_state.menu);
    return ccCopy_selectionCount((cc_menu_t)cc_state.menu);
}

/* Commit the copy source and start the operation (spec §3.1 step 2). */
static void cc_commitSource(void)
{
    cc_state.phase = CC_OP_COPY;
    cc_start();
    led_setBlinkLed(LED_COPY, 1u);
    cc_openMenu(ccCopy_menuForSource(&cc_source));
}

/* Flash the destination LED once after a paste or clear (spec §8.2). */
static void cc_flashDestination(LedFlashGroup group, uint8_t index)
{
    led_flashGroup(group, (uint16_t)(1u << index));
}

/* ---- copy routing (spec §7.1) ----------------------------------------- */

static uint8_t cc_copySeq(uint8_t index)
{
    uint8_t mode = buttonHandler_getMode();
    uint8_t has_source = (uint8_t)(cc_state.phase == CC_OP_COPY);

    if (mode == SELECT_MODE_VOICE || mode == SELECT_MODE_STEP) {
        if (!has_source) {
            if (cc_state.row != CC_ROW_NONE && cc_state.row != CC_ROW_SEQ)
                return 1u;
            if (cc_rowCount == 0u) {
                cc_source.kind = CC_KIND_STEP;
                cc_source.scene = cc_activeScene();
                cc_source.track = menu_getActiveVoice();
            }
            cc_state.row = CC_ROW_SEQ;
            cc_rowPush((uint8_t)buttonHandler_visibleStep(index));
            return 1u;
        }
        if (cc_source.kind == CC_KIND_STEP) {
            if (ccCopy_requestPaste(&cc_source, cc_state.selection,
                                    cc_activeScene(), menu_getActiveVoice(),
                                    buttonHandler_visibleStep(index)))
                cc_flashDestination(LED_FLASH_GROUP_SEQ, index);
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
                cc_flashDestination(LED_FLASH_GROUP_SEQ, index);
            return 1u;
        }
        /* Other sources: PERF SEQ selects the active Scene (navigation). */
        return 0u;
    }
    if (mode == SELECT_MODE_FX) {
        if (!has_source) {
            if (cc_state.row != CC_ROW_NONE && cc_state.row != CC_ROW_SEQ)
                return 1u;
            if (cc_rowCount == 0u) {
                cc_source.kind = CC_KIND_FX_STEP;
                cc_source.scene = cc_activeScene();
                cc_source.track = 0u;
            }
            cc_state.row = CC_ROW_SEQ;
            cc_rowPush(index);
            return 1u;
        }
        if (cc_source.kind == CC_KIND_FX_STEP) {
            if (ccCopy_requestPaste(&cc_source, cc_state.selection,
                                    cc_activeScene(), 0u, index))
                cc_flashDestination(LED_FLASH_GROUP_SEQ, index);
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
    if (!has_source) {
        if (cc_state.row != CC_ROW_NONE && cc_state.row != CC_ROW_SELECT)
            return 1u;
        if (cc_rowCount == 0u) {
            cc_source.kind = CC_KIND_BAR;
            cc_source.scene = cc_activeScene();
            cc_source.track = menu_getActiveVoice();
        }
        cc_state.row = CC_ROW_SELECT;
        cc_rowPush(index);
        return 1u;
    }
    switch (cc_source.kind) {
    case CC_KIND_STEP:
    case CC_KIND_TRACK:
        return 0u;          /* navigate (bar) / normal */
    case CC_KIND_BAR:
        if (ccCopy_requestPaste(&cc_source, cc_state.selection,
                                cc_activeScene(), menu_getActiveVoice(),
                                index))
            cc_flashDestination(LED_FLASH_GROUP_SELECT, index);
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
            cc_flashDestination(LED_FLASH_GROUP_VOICE, index);
        return 1u;
    default:
        return 1u;
    }
}

/* ---- clear routing (spec §7.2) ---------------------------------------- */

/* Capture a clear object and open its menu at `cancel`. */
static void cc_clearObject(cc_kind_t kind, uint8_t scene, uint8_t track,
                           uint8_t index)
{
    cc_source.kind = (uint8_t)kind;
    cc_source.scene = scene;
    cc_source.track = track;
    cc_source.start = index;
    cc_source.end = index;
    cc_state.flags = (uint8_t)(cc_state.flags | CC_FLAG_OBJECT);
    cc_start();
    cc_openMenu(ccClear_menuForObject(buttonHandler_getMode(), kind));
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
        cc_flashDestination(LED_FLASH_GROUP_SEQ, index);
        return 1u;
    }
    if ((cc_state.flags & CC_FLAG_OBJECT) != 0u) {
        /* One object at a time; a second press of the same row is a range. */
        if (cc_state.row == row && row != CC_ROW_NONE &&
            (cc_source.kind == CC_KIND_STEP || cc_source.kind == CC_KIND_BAR)) {
            cc_rowPush(row == CC_ROW_SEQ ?
                           (uint8_t)buttonHandler_visibleStep(index) : index);
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
        cc_rowPush((uint8_t)buttonHandler_visibleStep(index));
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
    if ((cc_state.flags & CC_FLAG_OBJECT) == 0u)
        return;
    cc_state.flags = (uint8_t)(cc_state.flags & (uint8_t)~CC_FLAG_OBJECT);
    if (cc_state.selection == 0u)
        return;
    if (!ccClear_requestClear(&cc_source, cc_state.selection))
        return;
    switch (cc_source.kind) {
    case CC_KIND_TRACK:
        cc_flashDestination(LED_FLASH_GROUP_VOICE, cc_source.track);
        break;
    case CC_KIND_SCENE:
        cc_flashDestination(LED_FLASH_GROUP_SEQ, cc_source.scene);
        break;
    case CC_KIND_BAR:
        cc_flashDestination(LED_FLASH_GROUP_SELECT, cc_source.end);
        break;
    default:
        break;
    }
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
    /* Refusals are silent (spec §3.3). */
    if (seq_recordActive || seq_eraseActive || menu_isStorageBusy() ||
        menu_loadInstrumentTransactionBusy() ||
        !cc_modeAllowed(buttonHandler_getMode()) || ccSvc_jobCount() != 0u)
        return 0u;
    cc_rowReset();
    cc_state.menu = CC_MENU_NONE;
    cc_state.selection = 0u;
    cc_state.flags = 0u;
    memset(&cc_source, 0, sizeof(cc_source));
    ccSvc_interactionStarted();
    if (shift_held) {
        cc_state.phase = CC_OP_CLEAR;
        led_setBlinkLed(LED_SHIFT, 1u);
        led_setBlinkLed(LED_COPY, 1u);
    } else {
        cc_state.phase = CC_OP_ARMED_COPY;
        led_setValue(1u, LED_COPY);
    }
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
    led_setBlinkGroup(LED_FLASH_GROUP_SEQ, 0u);
    led_setBlinkGroup(LED_FLASH_GROUP_SELECT, 0u);
    led_setBlinkGroup(LED_FLASH_GROUP_VOICE, 0u);
    if (menu_was_visible)
        menu_copyClearMenuClosed();
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
        uint8_t stored = (row == CC_ROW_SEQ &&
                          cc_source.kind != CC_KIND_FX_STEP)
                             ? (uint8_t)buttonHandler_visibleStep(index)
                             : index;

        if (!cc_rowRemove(stored))
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
    uint8_t mode;
    uint16_t seq_mask = 0u;
    uint8_t select_mask = 0u;
    uint8_t voice_mask = 0u;

    if (cc_state.phase == CC_OP_NONE)
        return;
    /* Re-assert the latched blinks (a mode change clears blink slots). */
    if (cc_state.phase == CC_OP_CLEAR) {
        led_setBlinkLed(LED_SHIFT, 1u);
        led_setBlinkLed(LED_COPY, 1u);
    } else if (cc_state.phase == CC_OP_COPY) {
        led_setBlinkLed(LED_COPY, 1u);
    } else {
        led_setValue(1u, LED_COPY);
    }

    /* Source indication on the visible LEDs (spec §8.2). */
    mode = buttonHandler_getMode();
    if (cc_state.phase == CC_OP_COPY) {
        uint8_t lo = (cc_source.start < cc_source.end) ? cc_source.start
                                                       : cc_source.end;
        uint8_t hi = (cc_source.start < cc_source.end) ? cc_source.end
                                                       : cc_source.start;
        uint8_t i;

        switch (cc_source.kind) {
        case CC_KIND_STEP:
            if ((mode == SELECT_MODE_VOICE || mode == SELECT_MODE_STEP) &&
                cc_source.scene == cc_activeScene() &&
                cc_source.track == menu_getActiveVoice()) {
                for (i = lo; i <= hi; i++) {
                    if ((uint8_t)(i / NUM_STEPS_PER_BAR) == menu_currentBar)
                        seq_mask = (uint16_t)(seq_mask |
                                              (uint16_t)(1u << (i % 16u)));
                }
            }
            break;
        case CC_KIND_BAR:
            if (mode == SELECT_MODE_STEP && cc_source.scene == cc_activeScene())
                for (i = lo; i <= hi && i < 8u; i++)
                    select_mask = (uint8_t)(select_mask | (uint8_t)(1u << i));
            break;
        case CC_KIND_TRACK:
            if (cc_source.scene == cc_activeScene())
                voice_mask = (uint8_t)(1u << cc_source.track);
            break;
        case CC_KIND_SCENE:
            if (mode == SELECT_MODE_PERF)
                seq_mask = (uint16_t)(1u << cc_source.scene);
            break;
        case CC_KIND_FX_STEP:
            if (mode == SELECT_MODE_FX && cc_source.scene == cc_activeScene())
                for (i = lo; i <= hi && i < 16u; i++)
                    seq_mask = (uint16_t)(seq_mask | (uint16_t)(1u << i));
            break;
        default:
            break;
        }
    }
    led_setBlinkGroup(LED_FLASH_GROUP_SEQ, seq_mask);
    led_setBlinkGroup(LED_FLASH_GROUP_SELECT, select_mask);
    led_setBlinkGroup(LED_FLASH_GROUP_VOICE, voice_mask);
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
    if (cc_state.phase == CC_OP_COPY)
        return (uint8_t)(cc_state.menu != CC_MENU_NONE);
    if (cc_state.phase == CC_OP_CLEAR)
        return (uint8_t)(cc_state.menu != CC_MENU_NONE);
    return 0u;
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
        cc_put(row, pos, "S");
        cc_putNum(row, pos, (uint16_t)(s->scene + 1u), 2u);
        cc_put(row, pos, (cc_state.phase == CC_OP_COPY &&
                          cc_state.selection == CC_COPY_INSTRUMENT) ? "i" : "T");
        cc_putNum(row, pos, (uint16_t)(s->track + 1u), 1u);
        break;
    case CC_KIND_SCENE:
        cc_put(row, pos, "S");
        cc_putNum(row, pos, (uint16_t)(s->scene + 1u), 2u);
        if (cc_state.phase == CC_OP_COPY) {
            static const char *const copy_suffix[] = { "", "c", "K", "f", "P" };
            if (cc_state.selection < 5u)
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
    cc_put(row0, &pos, (cc_state.phase == CC_OP_CLEAR) ? "CLR  " : "COPY ");
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
    /* Pots do nothing in a copy operation or while a clear menu is shown. */
    if (cc_state.phase != CC_OP_CLEAR || copyClear_menuVisible() || !target)
        return 0u;
    if (!ccClear_potTurned(target))
        return 0u;
    cc_start();
    return 1u;
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
    led_setBlinkGroup(LED_FLASH_GROUP_SEQ, 0u);
    led_setBlinkGroup(LED_FLASH_GROUP_SELECT, 0u);
    led_setBlinkGroup(LED_FLASH_GROUP_VOICE, 0u);
}

const cc_source_t *copyClear_source(void)
{
    return &cc_source;
}
