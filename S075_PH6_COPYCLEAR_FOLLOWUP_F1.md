# S075 — Phase 6 Copy and Clear — Follow-up F1 (hardware feedback, 2026-10-02)

Plan for the changes requested after the first hardware test of copy/clear.
**Plan only; no code is changed by this document.** Line numbers refer to the
current working tree of `dev-ph6-copyclear` (after the S075 implementation
pass) and are given with a function anchor.

Related: spec `S075_PH6_COPY_CLEAR_FULL_SPEC.md`, implementation schedule and
log `S075_PH6_COPYCLEAR_IMPLEMENTATION.md` (§14), trace plan
`S075_PH6_COPYCLEAR_TRACE_DEBUG_IMPLEMENTATION.md` (not yet implemented).

## Contents

1. Feedback summary
2. Root causes
3. Change F1-A — remove source blinking (undo Stage 1)
4. Change F1-B — copy and clear button LEDs
5. Change F1-C — row gestures: menu on first press, range rule, bar fix
6. Change F1-D — source indicator at column 9
7. Change F1-E — step copy labels
8. Change F1-F — destination blink covers every visible pasted/cleared LED
9. Change F1-G — clear selection kept between objects
10. Change F1-H — clear order: blink, trigger bits, then the pool work
11. Change F1-I — start latency: no wait for the name buffer, trickle rate
12. Other copy/clear operations: same concerns and conflicts
13. Spec, schedule and trace-plan updates
14. Resources
15. Verification
16. Follow-ups: open questions and concerns

---

## 1. Feedback summary

| # | User feedback | Type | Change |
|---|---|---|---|
| 1 | Source steps/bars/track/Scene must not flash; that was never asked for; undo it for all copy operations. The screen says what is copied. | correction | F1-A |
| 2 | Copy: only the copy/clear LED flashes, continuously, whenever the copy menu is open. Clear: only copy/clear and SHIFT flash. | correction | F1-B |
| 3 | A paste blinks its destination once when accepted, but every pasted-onto LED that is visible, not just the pressed step. Same for clears, including ranges. | correction | F1-F |
| 4 | The source indicator always starts at the 9th character of the top row. | cosmetic | F1-D |
| 5 | SEQ gestures: the menu appears on the first step press (not on release); a second press shows the range; later presses re-pair with the most recent held step; releases change nothing on screen; release of all sets the source; the next press pastes. | correction | F1-C |
| 6 | Bug: a step range cannot be set as a source in bar 2..8 (after a paste in another Scene's bar 2 the re-copy does nothing; bar 1 works). | bug | F1-C (§2.1) |
| 7 | Step copy labels: `step -> repl`, `step -> merge`, `auto -> repl`, `auto -> merge`. | wording | F1-E |
| 8 | Clear step uses the same gestures; bug in bar 2..8: first step 017 then last step gives `s001-032`; after releasing all, later presses pair with released steps (`s016-025`). | bug | F1-C (§2.1) |
| 9 | Clear: with no step held, a press and release selects just that step. If the menu shows `cancel`, only the screen updates; otherwise that step is cleared. Only a press while another step is held makes a range. | rule | F1-C, F1-G |
| 10 | Clear: the menu must not jump back to `cancel` on a new step; only on copy/clear release or a different type of clear. | correction | F1-G |
| 11 | Clear order: 1 blink the steps; 2 clear the active (trigger) bits; 3 run the stack search and clear (may take seconds) — so it never looks hung or rejected. | rule | F1-H |
| 12 | Pot clear sometimes runs at once, sometimes after 3–4 s. Proposed: start at once with a small allocation (≤ 0.1 % CPU), raise it once AutoSave and maintenance are paused. Feasible? | question | F1-I |

---

## 2. Root causes

### 2.1 Bars 2..8: the press stack stores 4-bit values

`copyClearSession.c` keeps the held row in `cc_rowStack[8]`, sixteen 4-bit
slots (`cc_rowGet()`/`cc_rowSet()`, lines 76–88). SEQ presses push the
**absolute** step (`cc_rowPush((uint8_t)buttonHandler_visibleStep(index))`,
`cc_copySeq()` line ~226 and `cc_clearPress()` line ~386), which is
16·bar + index. In bar 1 that fits in 4 bits; from bar 2 it is truncated:

| Event (bar 2) | Stored | Effect |
|---|---|---|
| press SEQ1 (step 17, abs 16) | 16 & 15 = 0 | `start` set from the argument (16): shows `017` — correct |
| press SEQ16 (abs 31) | 15 | `start = cc_rowTop()` = **0** → `s001-032` (user report) |
| release SEQ1 | `cc_rowRemove(16)` looks for 16, finds nothing | stack never empties |
| copy: release all | never empty | **source never commits** → "nothing happens" (user report) |
| clear: release all, press SEQ9 (abs 24) | top slot = 15 | `s016-025` (user report) |

The bar-1 case works because 0..15 fits. Fix in F1-C: store the row index
(0..15) and convert to an absolute step only when start/end are formed.

### 2.2 Pot-clear delay: waiting for the name buffer

`ccSvc_tick()` starts a register pass (and every queued job) only after
`ccSvc_ensureScratch()` has borrowed the 9 kB name buffer
(`copyClearService.c` lines 985–998). The borrow needs an idle filesystem
facade. When an AutoSave drain, Pattern AutoSave drain, trace append or
settings write is already running at the start of the operation, it finishes
first (user rule: in-flight writers complete cleanly) — that is the 3–4 s.
The register drain, all Pattern clears, whole-Pattern copy/reset and every
Scene-level commit **do not use the buffer at all**; only the step/bar paste
snapshot, the FX range snapshot and the name remap do. So most of the wait is
avoidable (F1-I).

---

## 3. Change F1-A — remove source blinking (undo Stage 1)

**Rule (user):** no LED shows the source in any copy operation; the menu's
source indicator does.

### F1-A-01 `Core/Hardware/frontPanel/ledHandler.c` / `.h` — REVERT to `HEAD`

Every S075 change in these two files is the Stage 1 group blink, which exists
only for source indication: `led_blinkGroupMask[]`, `led_blinkPhase`,
`led_blinkGroupIndex()`, `led_blinkGroupMember()`, `led_baseValue()`,
`led_blinkSlotMember()`, `led_setBlinkGroup()`, the group branch in
`led_renderFromStack()`, the guard in `led_clearAllBlinkLeds()`, the
`led_init()`/`led_clearAll()` resets and the `led_tickHandler()` phase toggle
(`git diff --stat`: +189/−1 in `.c`, +11 in `.h`). Restore both files to
`HEAD` (`00bd078`) content. The S070 layer stack (base < blink < flash <
pulse) is untouched.

### F1-A-02 `copyClearSession.c` — REMOVE group-blink use

- `copyClear_postEvent()` (line 605): delete the source-mask computation and
  the three `led_setBlinkGroup()` calls; keep only the copy/clear and SHIFT
  LED re-assertion (F1-B).
- `copyClear_copyReleased()` (line 467) and `copyClear_serviceFinished()`
  (line 866): delete the three `led_setBlinkGroup(..., 0u)` calls.
- Contract blocks: `copyClearSession.h` `copyClear_postEvent()` block — drop
  "recomputes the source group blink"; file header unchanged.

```c
/*
 * After every processed button event (consumed or not).
 * Output: re-asserts the copy/clear and SHIFT LED blinks, because a mode
 * change clears blink slots (F1-B). No LED shows the source: the menu's
 * source indicator does (user, F1). Cheap and idempotent.
 * Caller: buttonHandler_processEvents().
 */
```

---

## 4. Change F1-B — copy and clear button LEDs

| State | copy/clear LED | SHIFT LED |
|---|---|---|
| copy/clear held, no copy menu yet | steady on | normal |
| copy menu open (from the first source press, provisional or set) | blinking | normal |
| clear operation (SHIFT held at the press) | blinking from the press | blinking from the press |
| released | off | physical SHIFT state |

### F1-B-01 `copyClearSession.c` — one LED helper used by every state change

ADD after `cc_start()` (line 151):

```c
/*
 * Drive the copy/clear and SHIFT LEDs from the operation state (F1-B).
 *
 * What: copy with no menu -> copy/clear steady; copy menu open (provisional
 * or set source) -> copy/clear blinking; clear -> copy/clear and SHIFT
 * blinking. No other LED is touched. Why: user rule; one function so press,
 * menu open, mode change and release cannot disagree. Callers:
 * copyClear_copyPressed(), cc_openMenu(), copyClear_postEvent().
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
```

- `cc_commitSource()` (line 188): remove `led_setBlinkLed(LED_COPY, 1u)` (the
  menu now opens earlier, F1-C; `cc_openMenu()` calls the helper).
- `copyClear_copyPressed()` (line 443) and `copyClear_postEvent()`: call the
  helper instead of their own LED code.

---

## 5. Change F1-C — row gestures: menu on first press, range rule, bar fix

Applies to SEQ (steps, VOICE/STEP), SELECT (bars, STEP) and SEQ (FX steps,
EFFECTS), in both copy and clear. TRACK and PERF SEQ objects are single
presses and keep their current behaviour (source/object on press).

### 5.1 The literal action sequence (user)

| # | User action | Copy | Clear |
|---|---|---|---|
| 1 | press copy/clear | LED on (F1-B) | LEDs blink |
| 2 | press a step | menu opens, indicator = that step (provisional source); copy LED blinks | menu shows that step; selection kept (F1-G) |
| 3 | press a second step (first held) | indicator = range first→second | same |
| 4 | press a third step | range = second→third (most recent held → new) | same |
| 5 | release second and third | nothing changes | same |
| 6 | press a new step (first still held) | range = first→new | same |
| 7 | release all | nothing on screen; source is set | clear queued unless `cancel` (F1-G, F1-H) |
| 8 | press another step | paste | new single object (§9) |

### 5.2 Implementation steps (`copyClearSession.c`)

1. **Stack holds row indices.** `cc_rowPush(index)` stores `index` (0..15 for
   SEQ, 0..7 for SELECT, 0..15 for FX) and forms start/end through a new
   mapper; `cc_rowRemove(index)` and the release path use the raw index.

```c
/*
 * Absolute coordinate of one held row button (F1-C).
 *
 * What: a SEQ step index becomes 16 * visible bar + index for step objects;
 * SELECT (bars) and EFFECTS SEQ (FX steps) are already absolute. Why: the
 * press stack holds 4-bit row indices; storing absolute steps truncated
 * steps 16..127 and broke ranges from bar 2 on (F1 §2.1). The visible bar
 * cannot change while a row is held (BAR is consumed then). Caller:
 * cc_rowPush().
 */
static uint8_t cc_rowAbsolute(uint8_t index)
{
    return (cc_source.kind == CC_KIND_STEP)
               ? (uint8_t)buttonHandler_visibleStep(index)
               : index;
}
```

   `cc_rowPush()` then sets `cc_source.start = cc_rowAbsolute(top or
   index)`, `cc_source.end = cc_rowAbsolute(index)`. In
   `copyClear_buttonReleased()` (line 554) the `stored` computation (lines
   ~582–585) is removed: `cc_rowRemove(index)` takes the raw index.
2. **Menu on the first press (copy).** In `cc_copySeq()` / `cc_copySelect()`,
   when the row stack was empty: set kind/scene/track, push, set
   `cc_state.menu = ccCopy_menuForSource(&cc_source)` with the default
   selection, `cc_start()`, `cc_applyButtonLeds()`,
   `menu_copyClearMenuChanged()`. Phase stays `CC_OP_ARMED_COPY`
   (provisional): a further press of the same row is a range end, never a
   paste. Each further push calls `menu_copyClearMenuChanged()` so the
   indicator follows.
3. **Commit on release of all** (`copyClear_buttonReleased()`): when the stack
   empties in `CC_OP_ARMED_COPY`, `cc_commitSource()` sets
   `CC_OP_COPY` without reopening the menu (selection stays as the user left
   it) and without a repaint (nothing changes on screen, step 7).
4. **`copyClear_menuVisible()`** (line 683): visible in `CC_OP_ARMED_COPY`
   when `cc_state.menu != CC_MENU_NONE` too.
5. **Copy/clear released while a row is held** (`copyClear_copyReleased()`):
   unchanged — the provisional source is dropped, nothing applies.
6. **Clear rows**: `cc_clearPress()` already opens the menu on the first press;
   with the stack fix the range rule works in every bar. The "new single
   object after release" rule (feedback 9) is the existing path (the object
   flag is clear after release), with the selection kept (F1-G).

Contract block update (`copyClearSession.h`, `copyClear_buttonPressed()`):

```c
 * SEQ/SELECT/FX rows (F1-C): the first press opens the menu with that button
 * as the (provisional) source or clear object; a press while another button
 * of the row is held makes a range from the most recently pressed held
 * button to the new one; releases change nothing until the last button is
 * released, which sets the copy source or queues the clear (unless
 * `cancel`). Row buttons are stored as 0..15 indices; absolute steps are
 * formed per press, so ranges work in every bar.
```

---

## 6. Change F1-D — source indicator at column 9

`copyClear_formatMenu()` (line 796): the header is written at column 0
(`"COPY"` / `"CLR"`), then the indicator starts at column 8 (the 9th
character) regardless of header length:

```c
    cc_put(row0, &pos, (cc_state.phase == CC_OP_CLEAR) ? "CLR" : "COPY");
    /* F1-D: the source indicator always starts at the 9th character. */
    pos = 8u;
    cc_formatIndicator(row0, &pos);
```

The indicator is at most 8 characters (spec §8.1), so it always fits columns
8..15. Spec §8.1 example becomes `COPY    03T2s005`.

---

## 7. Change F1-E — step copy labels

`copyOps.c` `ccCopy_stepLabels[]` (line ~29):

```c
/* F1-E (user): step copy selections, default first. */
static const char *const ccCopy_stepLabels[] = {
    "step -> repl", "step -> merge", "auto -> repl", "auto -> merge"
};
```

Mapping is unchanged: `step -> repl` = copy all, `step -> merge` = merge all,
`auto -> repl` = copy automation, `auto -> merge` = merge automation. Bar
labels: see §16, Q1 (proposed `bar -> repl`, `bar -> merge`,
`auto -> repl`, `auto -> merge`). Labels are ≤ 14 characters.

---

## 8. Change F1-F — destination blink covers every visible pasted/cleared LED

One flash (`led_flashGroup()`, existing pre-S075 API) at acceptance, i.e. when
the paste or clear is queued. Only LEDs currently showing the destination
flash.

| Operation | SEQ flash mask | SELECT flash mask | VOICE flash |
|---|---|---|---|
| step / step-range paste (VOICE, STEP) | every destination step `(dst + i) % 128` whose bar is the visible bar; destination is always the viewed Scene and active track | — | — |
| bar / bar-range paste (STEP) | all 16 if the visible bar is one of the destination bars | destination bars `(dst + i) % 8` | — |
| `copy track` | all 16 if the destination track is the active track | — | destination track |
| `copy instrument` | — | — | destination track |
| Scene paste (PERF) | pressed Scene | — | — |
| FX step / range paste (EFFECTS) | destination FX steps `(dst + i) % 16` | — | — |
| clear step / range | object steps in the visible bar | — | — |
| clear bar / range | all 16 if the visible bar is in the range | object bars | — |
| clear track | all 16 if the track is the active track | — | the track |
| PERF Scene clears | the Scene | — | — |
| EFFECTS SEQ clear | the FX step (as now) | — | — |

### F1-F-01 `copyClearSession.c` — ADD `cc_flashObject()`; replace `cc_flashDestination()` calls

```c
/*
 * Flash every visible LED of a paste destination or clear object once
 * (F1-F).
 *
 * What: builds the SEQ/SELECT/VOICE masks of §8 from the object (kind,
 * Scene, track, start, length) and the current view (mode, viewed Scene,
 * active track, visible bar) and starts one led_flashGroup() per row. Why:
 * user rule — a single flashed step does not show what a range paste or
 * clear covers. Inputs: kind, Scene, track, first element, element count
 * (wrapped modulo the row size). Output: LED flashes only. Callers: the
 * paste and clear request paths in this file.
 */
static void cc_flashObject(cc_kind_t kind, uint8_t scene, uint8_t track,
                           uint8_t first, uint8_t count);
```

- Paste count: from the source (`|end − start| + 1` steps or bars; 128 for a
  track; 1 for Scene; FX range length).
- Calls: `cc_copySeq()`, `cc_copySelect()`, `cc_copyTrack()` after a
  successful `ccCopy_requestPaste()`; `cc_clearReleaseObject()` after a
  successful `ccClear_requestClear()` (replacing the current single-LED
  switch, lines ~413–427).
- `cc_flashDestination()` is removed.

---

## 9. Change F1-G — clear selection kept between objects

**Rule (user):** the clear menu returns to `cancel` only when copy/clear is
released or a different type of clear is chosen. A different type = a
different clear menu (step, bar, track, track-in-EFFECTS, Scene). Within one
type, every further object is a real clear with the selection shown.

### F1-G-01 `copyClearSession.c` `cc_clearObject()` (line 341)

Replace `cc_openMenu(ccClear_menuForObject(...))` with:

```c
    /*
     * F1-G (user): keep the selection while the clear type stays the same;
     * only a different clear menu (or releasing copy/clear) starts at
     * `cancel` again.
     */
    {
        cc_menu_t menu = ccClear_menuForObject(buttonHandler_getMode(), kind);

        if (menu != (cc_menu_t)cc_state.menu) {
            cc_state.menu = (uint8_t)menu;
            cc_state.selection = 0u;
        }
        cc_applyButtonLeds();
        menu_copyClearMenuChanged();
    }
```

`cc_openMenu()` (always resets) stays for copy menus. `copyClear_copyReleased()`
already resets menu and selection.

Contract block (`copyClearSession.h`, file-level types block for `cc_menu_t`)
gains: "Clear menus open at `cancel` once per type; the selection is kept for
further objects of the same type until copy/clear is released (F1-G)."

---

## 10. Change F1-H — clear order: blink, trigger bits, then the pool work

**Rule (user):** on acceptance of a clear: (1) blink what is cleared (F1-F);
(2) clear the active bits (the trigger bit 15 of each address entry, the
"static" part of the Pattern) when the selection turns triggers off;
(3) run the stack (pool) work in the background. The step LEDs therefore go
dark at once even when the pool work takes seconds.

### F1-H-01 `clearOps.c` — ADD `ccClear_triggersOffNow()`; call it from `ccClear_requestClear()` (line 81) after a successful enqueue

```c
/*
 * Turn the clear's trigger bits off at once (F1-H).
 *
 * What: for selections that end with triggers off — `clear step|bar|track`
 * (all, notes), `clear scene`, `clear pattern`, `clear notes` — writes bit 15
 * of every object step to 0 through pat_setStepActive() (one aligned
 * halfword RMW each; bits 14..0 untouched), then repaints the visible step
 * LEDs. `… automation`, `clear send`, `clear scene settings` and the FX
 * clears leave triggers alone. Why: user rule — the clear must look accepted
 * immediately; the queued pool work (block release/rewrite) follows and
 * publishes the same trigger-off state, so the result is unchanged. Safety:
 * trigger-bit writes are already foreground edits (step toggles); the pool
 * engine re-reads the live trigger inside its PRIMASK publish, and a paste
 * queued earlier that sets triggers again is overridden when this clear's
 * job runs after it (queue order). Cost: at most 896 RMWs (PERF Scene),
 * about 20 µs. Caller: ccClear_requestClear().
 */
static void ccClear_triggersOffNow(const cc_source_t *object,
                                   uint8_t selection);
```

- LED repaint: `led_updatePatternTrack(menu_getActiveVoice(),
  menu_getViewedPattern(), buttonHandler_selectedStep)` in VOICE/STEP when the
  object Scene is viewed; `menu_refreshPerfSceneLeds()` in PERF.
- Order in `cc_clearReleaseObject()`: `ccClear_requestClear()` (enqueue +
  trigger bits) then `cc_flashObject()`; the flash layer renders above the
  repainted base, so the user sees the blink first and dark steps after it.
- AutoSave: `pat_setStepActive()` marks the Pattern dirty (written after the
  suspension, as everything else).

---

## 11. Change F1-I — start latency: no wait for the name buffer, trickle rate

**Answer to feedback 12: yes, feasible.** Two parts: remove the wait where it
is not needed (the main cause), and add the requested trickle rate for work
that runs while an earlier writer is still finishing.

### 11.1 Borrow the name buffer only when it is used

| Work | Uses the buffer today | After F1-I |
|---|---|---|
| register pass (pot clear) | no | starts at once |
| Pattern clears (step/bar/track/PERF) | no | starts at once |
| whole-Pattern copy / reset | no | starts at once |
| Scene-level commits (Instrument, Kit, Effect, settings, Scene, clears) | only for the name remap | data commit at once; the job's last phase waits for the buffer to record the name remap |
| `ccSvc_nameContentChanged()` | marks 0xFE in the remap | clears the refreshed flag and arms the name write only; no buffer (the content markers — `autosave_markKitDirty/EffectDirty/SceneWith*Dirty` — already mark the source bytes) |
| step/bar/track paste, source and destination do **not** overlap | snapshot | no snapshot: source blocks read live and retargeted per step in the check and place phases |
| step/bar paste that overlaps its own source (same Scene and track, ranges intersect) | snapshot | snapshot (waits for the buffer) |
| FX step paste | snapshot (≤ 288 B) | 288 B stack snapshot; no buffer |
| end-of-operation name write | yes | yes (unchanged) |

Steps:

1. `ccSvc_tick()` (lines 985–998): start a job or register pass without
   `ccSvc_ensureScratch()`.
2. `ccSvc_runPatternPaste()` (line 304): ADD an overlap test in a new phase 0
   (`ccSvc_pasteOverlaps()`: same Scene and track and the destination step set
   intersects the source step set, using two 128-bit stack masks). Overlap →
   `ccSvc_ensureScratch()` (WAIT until granted) and the existing snapshot path;
   no overlap → new live path: `ccSvc_pasteBuild()` obtains the source block
   through `ccSvc_sourceBlock()` (live `pat_rawReadBlock()` + retarget into a
   stack buffer) instead of the table.
3. `ccSvc_nameCopy()` (line 807): returns 0 when the buffer is not borrowed;
   Scene-level executors (`copyOps.c` `ccCopy_runInstrument/Kit/Effect/
   Scene/PatternOnly`) move their name calls to a final phase that WAITs on
   `ccSvc_ensureScratch()`.
4. `ccSvc_nameContentChanged()` (line 824): drop the buffer use and the 0xFE
   value; `ccSvc_namesWritten()` marks sources only for remapped rows.
5. `ccCopy_runFxSteps()` (line 753): `effect_seq_step_t snap[16]` on the stack.

Contract blocks to update: `copyClearService.h` (scratch layout block: "borrowed
only by overlapping step/bar pastes and the name remap/write"),
`ccSvc_nameCopy`/`ccSvc_nameContentChanged` block, `copyClearService.c`
paste-engine block.

### 11.2 Trickle rate while an earlier writer is finishing

While the filesystem facade is still busy with a writer that started before
the operation (no new one can start: suspension), copy/clear work runs on a
time credit worth **0.1 % CPU** — 2 µs per 2 ms tick. When the facade is idle
(the pause is in full effect) the engines return to their full per-tick
counts (8 steps, 16 snapshot reads, 32 region entries).

```c
/*
 * Copy/clear trickle governor (F1-I; 2 B SRAM1).
 *
 * What: signed microsecond credit for engine work while an older writer
 * still owns the filesystem facade. Each tick adds CC_TRICKLE_US_PER_TICK
 * (2 µs per 2 ms = 0.1 % CPU, capped at CC_TRICKLE_CAP_US); each work unit
 * (one step, one compaction slide, one region entry, one Scene-level
 * commit) runs only while the credit is positive and is charged its measured
 * TIM2 time. Once the facade is idle the governor is bypassed and the
 * engines use their full per-tick counts. Why: user request — start a clear
 * at once with a small CPU share instead of waiting for AutoSave to finish,
 * then speed up when the pause is in effect. Inputs: filesystem_status(),
 * timebase_tim2Now(). Accessors: ccSvc_unitAllowed(), ccSvc_unitCharge().
 */
#define CC_TRICKLE_US_PER_TICK 2
#define CC_TRICKLE_CAP_US      40
static int16_t ccSvc_trickleCredit;
```

- Engines: each loop iteration checks `ccSvc_unitAllowed()` (full mode: the
  existing count limit; trickle mode: credit > 0) and wraps the unit with
  `start = timebase_tim2Now(); … ccSvc_unitCharge(start);`.
- A large unit (for example a Kit commit or `preset_startInstrumentCopy()`)
  can drive the credit negative; later units wait until it is repaid, so the
  average stays at 0.1 %.
- Expected trickle throughput (to be measured with the trace plan's job
  ticks): a step rewrite of a few µs → roughly one step per 2–4 ms; a pot
  clear's 896-step scan mostly reads empty steps (≈ 1 µs each).
- Immediate feedback does not depend on the governor: the blink and the
  trigger-bit clear (F1-F, F1-H) and the pot underline removal happen at the
  press/turn.

### 11.3 What still waits

- A step/bar paste that overlaps its own source waits for the buffer (the
  in-flight writer); its destination blink is immediate.
- The end-of-operation name write waits for the facade (unchanged).

---

## 12. Other copy/clear operations: same concerns and conflicts

| Operation | Affected by | Concern / conflict |
|---|---|---|
| Bar source/range (STEP SELECT) | F1-A, F1-C, F1-F | Same gesture change (menu on first press). Bars fit 4 bits, so no truncation bug. Labels: Q1. |
| FX step source/range (EFFECTS) | F1-A, F1-C, F1-F, F1-I | Same gesture change; FX steps 0..15 fit, no truncation bug. Paste blink now covers the whole destination range. |
| Track source (TRACK) | F1-A, F1-F | Source is a single press, menu on press (unchanged). The source track LED no longer blinks. |
| Scene source (PERF SEQ) | F1-A | The source Scene LED no longer blinks; PERF Scene LEDs show the normal Pattern/active state only. |
| `clear track` | F1-F, F1-G, F1-H | With F1-G, after one track clear every further TRACK press clears that track at release. **Q2**. |
| PERF `clear scene` / `clear pattern` / `clear automation` / `clear notes` / `clear fx` | F1-G, F1-H | With F1-G, every further Scene press repeats a destructive clear on that Scene. `clear scene` on another Scene also removes it from the Bank. **Q2**. |
| Pot clears in a clear operation | F1-G | Spec §3.2: pots are ignored while a clear menu is shown. With the selection kept, the menu stays up for the rest of the hold after the first object press, so pot clears are blocked until copy/clear is released. **Q3**. |
| Copy: encoder during a provisional source | F1-C | The menu is visible while steps are still held, so the selection can be changed before the source is set — intended (it is the selection used by the later paste). |
| Clears queued behind pastes | F1-H | A paste queued earlier that writes the same steps can briefly re-light a step that F1-H turned off; the clear's own job runs after it and leaves it off. Visible only if the queue is long. |
| Overlapping pastes | F1-I | Still wait for an in-flight writer (seconds at worst). **Q4**. |
| Trace plan (not implemented) | F1-A, F1-C, F1-I | `SOURCE_SET` stays at commit; add a provisional event or not (Q5); NAMES "changed rows" (0xFE) disappears; add trickle-mode ticks to `JOB_STATS` (§13). |
| Spec text | all | Conflicts listed in §13. |

---

## 13. Spec, schedule and trace-plan updates

| Document | Section | Change |
|---|---|---|
| `S075_PH6_COPY_CLEAR_FULL_SPEC.md` | §3.1 step 1–2 | LED: steady when pressed; blinks while the copy menu is open (from the first source press). |
| | §3.2 step 5 | "Each menu opening starts at `cancel`" → "a clear menu starts at `cancel` when its type is first opened; the selection is kept for further objects of the same type until copy/clear is released". |
| | §4.1 last paragraph | Step and bar sources: the menu opens on the first press (provisional); the source is set when the last held button of the row is released. |
| | §4.2 | Add the literal sequence of §5.1. |
| | §5 | Clear order (blink, trigger bits, pool work); single-object rule after release. |
| | §6 | Pot clears start without waiting for an in-flight writer (trickle rate). |
| | §8.1 | Indicator at column 9; step labels (F1-E). |
| | §8.2 | Remove "the source flashes when visible"; destination flash covers every visible pasted/cleared LED; remove the LED-consolidation bullet for the group blink. |
| | §9.3 | Name-buffer use: overlapping pastes and names only. |
| | §9.5 | Live-source path for non-overlapping pastes; trickle governor. |
| `S075_PH6_COPYCLEAR_IMPLEMENTATION.md` | §14 | New "14.7 F1 follow-up" entry when implemented (changes, RAM, build). |
| `S075_PH6_COPYCLEAR_TRACE_DEBUG_IMPLEMENTATION.md` | §2.2, T2-06, T2-13, T2-16, T7 | `SCRATCH` borrow becomes rare (overlap/names only); NAMES "changed rows" field always 0 → remove; `JOB_STATS` gains trickle-mode ticks (replace claim-wait nibble or add an event); `SOURCE_SET` unchanged. Line anchors move. |
| `knowledge_files/specification_reference/STORAGE_SRAM_MANIFEST.md` | §5, §8.2 | LED group blink bytes released; governor 2 B. |
| `MODULE_INTERCHANGE_SPEC.md` | ledHandler, CopyClear | `led_setBlinkGroup()` removed. |
| `SCOPING_TARGETS.md` | Phase 6 note | Remove "§4.11 LED priority stack implemented for copy/clear LEDs (group blink)". |

---

## 14. Resources

| Item | Change |
|---|---|
| SRAM1 | −7 B (`led_blinkGroupMask[3]`, `led_blinkPhase` removed) +2 B (trickle credit) = **−5 B** (net S075 becomes about +87 B of the approved +100 B) |
| Stack | +288 B peak in `ccCopy_runFxSteps()` (FX snapshot), +~400 B in the live-source paste path (two block buffers + decode list), foreground only |
| Flash | small net decrease (group blink removed) plus the live-source path and governor (estimate +0.5–1 KB) |
| CPU | unchanged in full mode; ≤ 0.1 % average in trickle mode |

---

## 15. Verification (hardware)

| Case | Expected |
|---|---|
| Copy step source in bars 1, 2 and 8; ranges both directions | menu on the first press; range updates per the §5.1 sequence; release sets the source; next press pastes |
| Re-copy after pasting into another Scene's bar 2 (user's bug) | source sets normally |
| Clear range in bar 2: SEQ1 then SEQ16 | `s017-032` |
| Clear: release all, press SEQ9 alone | `s025` (single), no stale pairing |
| Clear selection kept: choose `step`, press another step | that step clears; menu stays on `step` |
| Clear type change: step clear then TRACK press | menu opens at `cancel` |
| Indicator | always starts at column 9 for every source kind |
| LEDs | no source LED blinks; copy LED steady → blinking at the first press; clear: copy + SHIFT blink |
| Destination blink | all visible destination steps/bars/FX steps blink once per accepted paste/clear |
| Clear feedback | steps blink and go dark at once, even while an AutoSave drain is running |
| Pot clear during a running AutoSave drain | removal starts at once (trickle), speeds up when the drain ends |
| Overlapping paste during a running drain | waits for the drain; destination blinks at the press |

---

## 16. Follow-ups: open questions and concerns

- **Q1 — Bar copy labels.** Use the same pattern as steps: `bar -> repl`,
  `bar -> merge`, `auto -> repl`, `auto -> merge`? (FX `step` and the track
  and Scene labels unchanged?)
- **Q2 — Kept selection for destructive objects.** F1-G makes every further
  object of the same type a real clear. For steps and bars that is what you
  described. Should it also apply to TRACK clears and to PERF Scene clears
  (`clear scene` on another Scene empties it and removes it from the Bank)?
  Options: (a) same rule everywhere; (b) TRACK and PERF Scene clears return to
  `cancel` after each applied clear; (c) only `clear scene` returns to
  `cancel`.
- **Q3 — Pot clears while a clear menu is up.** With the menu now staying up
  for the rest of the hold, pot clears are blocked after the first object
  press until copy/clear is released (spec §3.2 step 5). Keep that, or let pot
  turns act while no object button is held (the menu stays as it is)?
- **Q4 — Overlapping pastes.** A paste that overlaps its own source (same
  Scene, track, intersecting steps) still needs the name buffer and so waits
  for an in-flight writer. Acceptable, or should the engine order the copy
  (forward/backward, like `memmove`) and drop the snapshot for this case too?
  Reversed ranges and wrap make the ordering more involved; I recommend
  keeping the snapshot unless the wait shows up in testing.
- **Q5 — Provisional source in the trace.** Should the trace plan add a
  record for the provisional source (each range update while held), or keep
  only the final `SOURCE_SET`? Recommendation: final only.
- **Q6 — "Different type of clear".** I read this as a different clear menu
  (step, bar, track, track in EFFECTS, Scene). Is a mode change between VOICE
  and STEP with the same object kind (steps) the same type? (Proposed: yes —
  the menu is the same.)
- **Q7 — Track paste blink.** For `copy track`, flash the 16 visible steps
  when the destination is the active track, in addition to its TRACK LED?
  (Proposed: yes.)
- **Concern — trickle throughput.** At 0.1 % CPU the pool work of a large
  clear can still take a second or two while a drain finishes; the immediate
  blink and dark steps (F1-F, F1-H) are what tells the user it was accepted.
  The trace plan's job ticks will show the real numbers.
