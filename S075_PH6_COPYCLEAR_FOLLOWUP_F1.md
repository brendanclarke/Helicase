# S075 — Phase 6 Copy and Clear — Follow-up F1 (hardware feedback, 2026-10-02)

Plan for the changes requested after the first hardware test of copy/clear.
Implemented together with the trace schedule on 2026-10-02; the decision
sections remain the behavioral authority. Line numbers refer to the
current working tree of `dev-ph6-copyclear` (after the S075 implementation
pass) and are given with a function anchor.

Related: spec `S075_PH6_COPY_CLEAR_FULL_SPEC.md`, implementation schedule and
log `S075_PH6_COPYCLEAR_IMPLEMENTATION.md` (§14), trace plan
`S075_PH6_COPYCLEAR_TRACE_DEBUG_IMPLEMENTATION.md`.

**Revision 2 (2026-10-02):** the user's answers to the first round of
follow-ups (Q1–Q7) are folded in; §16.1 records them.
**Revision 3 (2026-10-02):** the answers to A1–A5 are folded in (§16.1):
`clear track` blinks the TRACK LED only and darkens the current track's steps
at once; early-trigger storage O1 (+64 B) and trigger restore on a dropped
paste (+64 B) approved; +4 B DEV trace RAM approved; `clear … notes` clears
every special except probability (new change F1-J); a clear dropped in low
storage may leave its triggers off (accepted).
**Revision 4 (2026-10-02):** B1 (restore skips steps the user toggled since)
confirmed; B2 decided: `auto -> repl` also replaces the probability special
with the source's, `auto -> merge` leaves probability alone (new change F1-K,
§10). The combined implementation schedule is
`S075_PH6_COPYCLEAR_F1_AND_TRACE_IMPLEMENTATION.md`. The next request
implements this document **and** the trace plan together; §13.1 lists every
trace-plan hook that this document moves or changes, so the two can be
applied as one pass (F1 first, then the trace hooks by function anchor).

## Contents

1. Feedback summary
2. Root causes
3. Change F1-A — remove source blinking (undo Stage 1)
4. Change F1-B — copy and clear button LEDs
5. Change F1-C — row gestures: menu on first press, range rule, bar fix
6. Change F1-D — source indicator at column 9
7. Change F1-E — step and bar copy labels
8. Change F1-F — destination blink covers every visible pasted/cleared LED
9. Change F1-G — clear selection kept per button group
10. Change F1-H — clear order: blink, trigger bits, then the pool work;
    F1-J — `clear … notes` keeps probability;
    F1-K — `auto -> repl` carries probability
11. Change F1-I — start latency: no wait for the name buffer, trickle rate,
    early trigger bits for pastes
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

## 7. Change F1-E — step and bar copy labels

`copyOps.c` `ccCopy_stepLabels[]` (line ~29):

```c
/* F1-E (user): step copy selections, default first. */
static const char *const ccCopy_stepLabels[] = {
    "step -> repl", "step -> merge", "auto -> repl", "auto -> merge"
};
```

`copyOps.c` `ccCopy_barLabels[]` (line ~32), approved (Q1):

```c
/* F1-E (user, Q1): bar copy selections, default first. */
static const char *const ccCopy_barLabels[] = {
    "bar -> repl", "bar -> merge", "auto -> repl", "auto -> merge"
};
```

Mapping is unchanged: `… -> repl` (step/bar) = copy all, `… -> merge` =
merge all, `auto -> repl` = copy automation, `auto -> merge` = merge
automation. Track (`track`, `instrument`), Scene and FX (`step`) labels are
unchanged. Labels are ≤ 14 characters.

---

## 8. Change F1-F — destination blink covers every visible pasted/cleared LED

One flash (`led_flashGroup()`, existing pre-S075 API) at acceptance, i.e. when
the paste or clear is queued. Only LEDs currently showing the destination
flash.

| Operation | SEQ flash mask | SELECT flash mask | VOICE flash |
|---|---|---|---|
| step / step-range paste (VOICE, STEP) | every destination step `(dst + i) % 128` whose bar is the visible bar; destination is always the viewed Scene and active track | — | — |
| bar / bar-range paste (STEP) | all 16 if the visible bar is one of the destination bars | destination bars `(dst + i) % 8` | — |
| `copy track` | — | — | destination track only (Q7) |
| `copy instrument` | — | — | destination track |
| Scene paste (PERF) | pressed Scene | — | — |
| FX step / range paste (EFFECTS) | destination FX steps `(dst + i) % 16` | — | — |
| clear step / range | object steps in the visible bar | — | — |
| clear bar / range | all 16 if the visible bar is in the range | object bars | — |
| clear track | — (the current track's steps go dark at once through F1-H; A1) | — | the track |
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

## 9. Change F1-G — clear selection kept per button group

**Rule (user, Q2/Q6):** the clear menu returns to `cancel` only when
copy/clear is released or the user presses an object in a **different button
group**. The groups are: SEQ steps (VOICE/STEP), SELECT bars (STEP), TRACK
buttons (any mode), and SEQ Scenes (PERF). Within one group every further
object is a real clear with the selection shown — for every group, including
TRACK and PERF Scene clears (`clear scene` included). EFFECTS SEQ clears have
no menu and do not change the group. A mode change that keeps the same
group (for example steps in VOICE and in STEP) keeps the selection.

Each object kind maps to exactly one group (`CC_KIND_STEP` = SEQ steps,
`CC_KIND_BAR` = SELECT, `CC_KIND_TRACK` = TRACK, `CC_KIND_SCENE` = PERF SEQ),
so the previous object's kind (still in `cc_source.kind` after its release)
identifies the previous group; no new state is needed.

### F1-G-01 `copyClearSession.c` `cc_clearObject()` (line 341)

Read the previous kind before `cc_source` is overwritten, then replace
`cc_openMenu(ccClear_menuForObject(...))` with:

```c
    /*
     * F1-G (user, Q2/Q6): the selection is kept while the user stays in one
     * button group (SEQ steps, SELECT bars, TRACK, PERF SEQ Scenes); a press
     * in another group, or releasing copy/clear, starts at `cancel` again.
     * The TRACK menu has an extra `send` entry in EFFECTS mode: a kept
     * selection that does not exist in the new menu falls back to `cancel`.
     */
    {
        cc_menu_t menu = ccClear_menuForObject(buttonHandler_getMode(), kind);

        if (kind != previous_kind ||
            cc_state.selection >= ccClear_selectionCount(menu))
            cc_state.selection = 0u;
        cc_state.menu = (uint8_t)menu;
        cc_applyButtonLeds();
        menu_copyClearMenuChanged();
    }
```

`previous_kind` is `CC_KIND_NONE` after `copyClear_copyPressed()` (which
clears `cc_source`), so the first object of an operation always opens at
`cancel`. `cc_openMenu()` (always resets) stays for copy menus.

Contract block (`copyClearSession.h`, the `cc_menu_t` block) gains:

```c
 * Clear menus (F1-G): a menu opens at `cancel` for the first object of a
 * button group (SEQ steps, SELECT bars, TRACK, PERF SEQ Scenes); further
 * objects of the same group keep the selection, so each is a real clear,
 * until copy/clear is released or another group is pressed.
```

### F1-G-02 Pot clears (Q3, confirmed rule — no code change)

- A clear operation opened with SHIFT + copy/clear shows **no menu** until an
  object button is pressed; pot turns in that state are pot clears.
- Pot clears never open a menu. A turn removes the underline (`_` marker) at
  once (`menu_automationTargetCleared()`) and the automation is removed in
  the background (register, F1-I).
- Once an object button has opened a clear menu, the menu stays up until
  copy/clear is released, and pot turns do nothing (`copyClear_potTurned()`
  already returns 0 while a menu is visible). To clear by pot again, release
  copy/clear and hold SHIFT + copy/clear again.

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
  object Scene is viewed; `menu_refreshPerfSceneLeds()` in PERF. For `clear
  track` on the current (active) track this is what makes its step LEDs go
  dark at once (A1); the blink itself is the TRACK LED only.
- **A clear dropped after its early write (user, A5 clarification):** a clear
  job can be dropped only by `EVACUATE_FAILED` (a block from a pre-S075
  Pattern in the swap block cannot move in a nearly full pool), and only for
  selections that rewrite blocks (`… notes`, `… automation`). The triggers
  written off at the press then stay off; that is accepted. No restore
  storage for clears.
- Order in `cc_clearReleaseObject()`: `ccClear_requestClear()` (enqueue +
  trigger bits) then `cc_flashObject()`; the flash layer renders above the
  repainted base, so the user sees the blink first and dark steps after it.
- AutoSave: `pat_setStepActive()` marks the Pattern dirty (written after the
  suspension, as everything else).

### F1-J — `clear … notes` clears everything except automation and probability (A5)

**Rule (user, A5):** the purpose of `… notes` is to clear everything except
automation. It turns the trigger off and removes the note and velocity
specials, but keeps the probability special when it is set, because
probability gates the step's automation. Automation is kept. Applies to
`clear step notes`, `clear bar notes`, `clear track notes` and PERF
`clear notes`.

`copyClearService.c` `ccSvc_clearStep()` (line 530), notes branch — replace
`pat_rawEncode(block, 0u, 0u, 0u, 0u, autos, count)` with:

```c
    /*
     * notes (F1-J, user A5): everything except automation goes, but the
     * probability special stays when set, because it gates this step's
     * automation. Trigger off; the block shrinks or empties (swap path).
     */
    bytes = pat_rawEncode(block,
                          (uint8_t)(sp.flags & PAT_SPECIAL_PROB_BIT),
                          0u, 0u, sp.probability, autos, count);
```

The `ccSvc_clearStep()` contract block (`notes` sentence) and the
`clearOps.h`/`clearOps.c` file blocks are updated to "trigger off, note and
velocity specials removed; probability and automation kept". A step with no
block keeps the existing path (trigger off when on).

Spec §5 table: `… notes` → "trigger off, specials removed except
probability; automation kept".

### F1-K — `auto -> repl` carries the probability special (B2)

**Rule (user, B2):** probability belongs with the automation (F1-J). A
**replace** automation paste (`auto -> repl`) therefore replaces the
destination's probability special with the source's: when the source step
has probability set, the destination gets that value; when it has none, the
destination's probability special is removed. Note and velocity specials and
the trigger of the destination are kept. A **merge** automation paste
(`auto -> merge`) does not touch probability (destination specials kept, as
today). `step -> repl` / `step -> merge` (and the bar forms) are unchanged:
they already carry or merge all specials.

`copyOps.c` `ccCopy_buildStep()` (line 426), `CC_COPY_AUTO` branch — the
encode becomes:

```c
        /*
         * auto -> repl (F1-K, user B2): automation and the probability special
         * come from the source; note/velocity specials and the trigger stay.
         */
        flags = (uint8_t)((ds.flags & (uint8_t)~PAT_SPECIAL_PROB_BIT) |
                          (ss.flags & PAT_SPECIAL_PROB_BIT));
        return pat_rawEncode(out, flags, ds.note, ds.velocity,
                             ss.probability, sa, sc);
```

The skip test (`sc == 0u && dc == 0u`) also requires both probability flags
to be equal, otherwise a probability-only difference must still be written.
Spec §4.4 table row `copy … automation`: specials "unchanged except
probability := source".

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
  in-flight writer; accepted, Q4). Its destination blink is immediate and all
  its trigger bits are written at once (§11.4, O1).
- The end-of-operation name write waits for the facade (unchanged).

### 11.4 Early trigger bits for pastes (Q4, A2 = O1, A3 = restore)

**Rule:** like clears (F1-H), a paste writes its destination trigger bits at
acceptance (the press), repaints the visible step LEDs, and leaves the block
(specials and automation) to the background job. It applies to step, step
range, bar, bar range and `copy track` pastes with `… -> repl` (trigger :=
source) and `… -> merge` (trigger := destination OR source). `auto -> repl`
and `auto -> merge` keep the destination triggers, so they write nothing
early. Scene-level and FX pastes are unaffected.

**Overlap (O1, approved).** A paste that overlaps its own source (same Scene
and track, destination steps intersect source steps) would overwrite source
triggers that its job has not read yet. At the press, the source trigger bits
of the whole paste (≤ 128 steps, range order) are kept in a 128-bit mask in
the job's queue slot; the job's snapshot takes its trigger bits from that
mask. Every overlapping paste, step or bar range, therefore lights all of its
destination steps at the press.

**Restore on drop (A3, approved).** A paste can still be dropped by its job
(`NO_ROOM`, `EVACUATE_FAILED`). At the press, the previous destination trigger
bits (≤ 128 steps, destination order) are kept in a second 128-bit mask in
the slot. When the job is dropped, each destination step whose live trigger
still equals the value the early write gave it is restored from the mask; a
step the user has toggled since (after releasing copy/clear) is left as the
user set it (B1). The paste is then fully undone (its blocks were never
written), which keeps "a paste completes or is dropped whole".

**SRAM:** 2 × 16 B per slot × 4 slots + 1 B flags = **129 B** (64 B source
masks, O1; 64 B restore masks and the flags byte, A3). Approved.

**Implementation steps:**

1. `copyClearService.c` — ADD after `ccSvc_queue[]` (line ~47):

```c
/*
 * Early-trigger masks per queue slot (F1-I §11.4; 129 B SRAM1, approved
 * 2026-10-02: O1 +64 B, A3 restore storage).
 *
 * What: for each queued step/bar/track paste that wrote its destination
 * trigger bits at the press, src[i] bit n is the source trigger of the
 * paste's n-th step (range order) as read before the early write (used only
 * when the paste overlaps its own source), and prev[i] bit n is the
 * destination trigger of the n-th destination step before the early write.
 * flags bit i = slot i has early triggers; bit 4+i = slot i uses src[i].
 * Why: pasted steps light at once (user, Q4); the job must still copy the
 * pre-paste source triggers when the paste overlaps its source, and a
 * dropped paste must be undone whole (user, A3). Lifetime: written by
 * ccSvc_setEarlyTriggers() at the press, read by the snapshot phase and the
 * drop path, dead when the slot's job ends. Accessors:
 * ccSvc_setEarlyTriggers(), ccSvc_runPatternPaste(), ccSvc_tick().
 */
static uint8_t ccSvc_earlySrc[CC_QUEUE_SIZE][16];
static uint8_t ccSvc_earlyPrev[CC_QUEUE_SIZE][16];
static uint8_t ccSvc_earlyFlags;
```

2. `ccSvc_enqueue()` returns the slot index + 1 (0 = refused) so the request
   can attach the masks; callers that only test success are unchanged.
3. `copyOps.c` `ccCopy_requestPaste()` (line 88) — after a successful enqueue,
   `ccCopy_triggersNow(src, &job, slot)`:

```c
/*
 * Write a paste's destination trigger bits at acceptance (F1-I §11.4).
 *
 * What: for `… -> repl` and `… -> merge` step/bar/track pastes: (1) reads
 * the source trigger of every pasted step (live) into a 128-bit stack mask,
 * (2) reads the previous destination triggers into the slot's restore mask,
 * (3) if the paste overlaps its own source, stores the source mask in the
 * slot, (4) writes each destination trigger (repl := source; merge := OR)
 * with pat_setStepActive(), (5) repaints the visible step LEDs. Why: user
 * rule (Q4) — SEQ LEDs and triggers update at once even when the job waits
 * for the name buffer or runs at the trickle rate; the job later publishes
 * the same trigger bits with the blocks. Cost: ≤ 3 × 128 halfword accesses.
 * Caller: ccCopy_requestPaste(). Affiliates: ccSvc_setEarlyTriggers().
 */
```

4. `copyClearService.c` snapshot phase (`ccSvc_runPatternPaste()`, case 1):
   when the slot uses its source mask, the table entry's trigger bit comes
   from the mask instead of the live entry.
5. Drop path (`ccSvc_tick()`, job end with `CC_RUN_DROP`): when the slot has
   early triggers, call `ccSvc_restoreEarlyTriggers(job, slot)`, repaint the
   step LEDs if visible, then release the claim and pop the slot.

```c
/*
 * Undo a dropped paste's early trigger writes (F1-I §11.4, user A3).
 *
 * What: for each destination step of the dropped job, if its live trigger
 * still equals the value written at the press, restores the previous value
 * from the slot's restore mask; a step changed since (user toggle) is left
 * alone. The written value is recomputed from the slot's source mask (an
 * overlapping paste) or from the live source (unchanged since the press for
 * a non-overlapping paste), with the restore mask for `merge`. Why: a paste
 * completes or is dropped whole. Caller: ccSvc_tick() on CC_RUN_DROP.
 * Affiliates: ccCopy_triggersNow().
 */
static void ccSvc_restoreEarlyTriggers(const cc_job_t *job, uint8_t slot);
```

---

## 12. Other copy/clear operations: same concerns and conflicts

| Operation | Affected by | Concern / conflict (after the Q1–Q7 decisions) |
|---|---|---|
| Bar source/range (STEP SELECT) | F1-A, F1-C, F1-E, F1-F, §11.4 | Same gesture change (menu on first press). Bars fit 4 bits, so no truncation bug. Labels decided (Q1). Early triggers for all bar pastes, overlapping ones included (O1). |
| FX step source/range (EFFECTS) | F1-A, F1-C, F1-F, F1-I | Same gesture change; FX steps 0..15 fit. Paste blink covers the destination range. No trigger bits (FX locks). |
| Track source (TRACK) | F1-A, F1-F, §11.4 | Source on press (unchanged), source LED no longer blinks; paste blinks only the destination TRACK LED (Q7); `copy track` triggers written early (never overlaps: same Scene and track is an identical paste). |
| Scene source (PERF SEQ) | F1-A | Source Scene LED no longer blinks. |
| `clear track` | F1-F, F1-G, F1-H, F1-J | Kept selection applies (Q2): after one track clear, each further TRACK press clears that track at release. Blink: TRACK LED; the current track's steps go dark at once (A1). `track notes` keeps probability (F1-J). |
| PERF Scene clears | F1-G, F1-H | Kept selection applies (Q2), including `clear scene` (another Scene: emptied and no longer Bank-present). Trigger bits off at once for `clear scene`, `clear pattern`, `clear notes`. |
| Pot clears | F1-G-02, F1-I | Confirmed rule (Q3); now start at once (no buffer wait, trickle rate). |
| TRACK clear menu in EFFECTS vs other modes | F1-G | Same group, different menu length (`send`): a kept `send` falls back to `cancel` outside EFFECTS. |
| Copy: encoder during a provisional source | F1-C | The menu is visible while steps are held; a changed selection is the one the later paste uses. Intended. |
| `auto -> repl` pastes | F1-K | The destination's probability special follows the source (set, changed or removed). |
| Pastes and clears queued behind each other | F1-H, §11.4 | Early trigger writes happen in press order while jobs run in queue order; a step can briefly show an intermediate state (for example a clear's dark steps re-lit by an earlier queued paste's job, then dark again when the clear's job runs). Final state is correct. |
| Early triggers and drops | §11.4, F1-H | A dropped paste restores its previous triggers (A3), except steps the user toggled since (B1). A dropped clear leaves its triggers off (accepted). |
| `… notes` clears | F1-J | Probability special kept with the automation; the block shrinks rather than empties when probability is set. |
| Trace plan | all | See §13.1. |
| Spec text | all | See §13. |

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
| `S075_PH6_COPYCLEAR_TRACE_DEBUG_IMPLEMENTATION.md` | see §13.1 | Amendments applied in the same implementation pass. |
| `knowledge_files/specification_reference/STORAGE_SRAM_MANIFEST.md` | §5, §8.2 | LED group blink bytes released; governor 2 B. |
| `MODULE_INTERCHANGE_SPEC.md` | ledHandler, CopyClear | `led_setBlinkGroup()` removed. |
| `SCOPING_TARGETS.md` | Phase 6 note | Remove "§4.11 LED priority stack implemented for copy/clear LEDs (group blink)". |
| spec | §3.2, §6 | Pot clears: no menu ever; once an object opened a menu it stays until release and pots do nothing (F1-G-02). Kept selection per button group (F1-G). |
| spec | §4.4, §5 | Early trigger bits for pastes (§11.4) and clears (F1-H). |

### 13.1 Trace plan compatibility (for the combined implementation)

The next request implements this document and
`S075_PH6_COPYCLEAR_TRACE_DEBUG_IMPLEMENTATION.md` together. Order: apply F1
first, then the trace hooks, using the trace plan's **function anchors**
(its line numbers will have moved). Hooks affected by F1:

| Trace plan item | Effect of F1 | Amendment |
|---|---|---|
| §2.2 `SOURCE_SET`, T3-02 (`cc_commitSource()`) | The menu opens at the first press; `cc_commitSource()` still runs once, when the row is released (or on press for TRACK/Scene). | Keep: one record per final source (Q5). No record for provisional range updates. |
| T3-01 `OP_START` | LED code moves into `cc_applyButtonLeds()` (F1-B). | Record placement unchanged (after arming). |
| T2-06 `SCRATCH` borrow (`ccSvc_ensureScratch()`) | Borrowing becomes lazy: overlapping pastes and names only (F1-I). | Hook unchanged; it now fires rarely. `scratch_wait` counts only when a job actually waits. |
| T2-16 job start (`ccSvc_tick()`) | Jobs start without `ccSvc_ensureScratch()`. | Keep the `JOB_START` hook at the job start; remove nothing else. |
| T2-10 snapshot retarget count | A non-overlapping paste retargets live in **both** the check and the place phases. | Count retarget drops only in the phase that publishes (place, or snapshot for the overlap path) so each entry is counted once. |
| T2-10 `CHECK_FAIL` | The check phase also runs on the live path. | Same record, both paths. |
| T2-10 `GROW_UNPLACEABLE`, T2-07/T2-08 swap anomalies | Unchanged code paths. | Unchanged. |
| T2-13/T2-16 `NAMES` | `ccSvc_nameContentChanged()` no longer writes 0xFE (F1-I). | `NAMES` bits 16..23 ("rows changed only") become reserved 0; the request loop counts only remapped rows. Decoder prints the field only when nonzero. |
| §2.2 `JOB_STATS` | Trickle mode added (F1-I). | ADD event `0x17 JOB_TRICKLE`: bits 0..15 ticks spent in trickle mode, bits 16..31 units executed in trickle mode (each sat 65,535); emitted at job end and at register-pass end only when nonzero. Needs `ccSvc_traceState.trickle_ticks` and `trickle_units` (+4 B DEV-only; A4). |
| New: early triggers (F1-H, §11.4) | Trigger bits written at acceptance. | ADD event `0x24 EARLY_TRIG`: bits 0..7 steps written (sat 255), bit 8 paste (0 clear), bit 9 source mask used (overlap), bits 16..18 kind, bits 19..22 Scene, bits 23..25 track, bits 26..27 queue slot. One record per accepted paste/clear that wrote triggers. |
| New: drop after early triggers | §11.4 / A3 | ADD event `0x19 EARLY_RESTORED`: bits 0..7 steps restored, bits 8..15 steps left alone (changed by the user since), bits 16..17 queue slot; emitted right before a dropped paste's `JOB_END`. A dropped clear needs no record beyond its `JOB_END`. |
| New: F1-J | `notes` keeps probability | No new record. |
| T4-11 pot clears | Rule confirmed (F1-G-02), start latency changes only. | Unchanged. |
| T4-10..T4-16 Scene-level clears | Name calls move to a final phase that may WAIT on the buffer (F1-I step 3). | `FANOUT`/`MASK_SET` hooks stay with the data commit (first phase), not the name phase. |
| T5-01 | Unchanged. | Unchanged (D4). |
| T6-03/T6-04 | Unchanged. | Unchanged; `SUSPEND` begin edges now usually precede the first job by one tick instead of seconds. |
| T7 decoder | New events `0x17`, `0x19`, `0x24`; `NAMES` field change. | Add to `CC_EVENTS` and `cc_record_text()`. |
| §12 DEV RAM | +4 B (trickle counters). | 20 B → 24 B DEV-only (approved, A4). |
| Manifest / ledger | Production RAM of F1 | The 129 B early-trigger masks and the 2 B governor belong to the F1 manifest entry, not to the trace plan. |

---

## 14. Resources

| Item | Change |
|---|---|
| SRAM1 (production) | −7 B (`led_blinkGroupMask[3]`, `led_blinkPhase` removed) +2 B (trickle credit) +129 B (early-trigger masks: 64 B source O1, 64 B restore + 1 B flags A3) = **+124 B**. S075 net becomes about **+216 B**: +100 B approved originally, plus O1 (+64 B) and the A3 restore storage approved 2026-10-02. |
| SRAM1 (DEV only) | trace state 24 B (20 B D1 + 4 B A4, approved) and filesystem trace latches 2 B, as in the trace plan. 0 B in production. |
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
| Overlapping paste during a running drain | waits for the drain; destination blinks and all of its triggers/LEDs update at the press (O1) |
| Paste dropped for lack of pool space (nearly full Pattern) | triggers lit at the press return to their previous state; a step toggled by the user in between keeps the user's state |
| `clear track` on the current track | TRACK LED blinks; the track's step LEDs go dark at once |
| `clear step notes` on a step with note, velocity, probability and automation | trigger off; note and velocity gone; probability and automation kept |
| Paste `step -> repl` / `step -> merge` | destination triggers and SEQ LEDs update at the press |
| `auto -> repl` from a step with probability onto a step with a different probability | destination probability becomes the source's; note/velocity/trigger kept |
| `auto -> merge` | destination probability unchanged |
| Clear selection per group: `step` chosen, then another step | clears; menu stays on `step` |
| Group change: step clear, then TRACK | TRACK menu opens at `cancel` |
| Group kept across modes: step clear in VOICE, switch to STEP, press a step | selection kept |
| Pot clear | no menu; underline gone at the turn; automation removed in the background |
| Pot after an object menu | does nothing until copy/clear is released and re-held |
| Bar labels | `bar -> repl`, `bar -> merge`, `auto -> repl`, `auto -> merge` |
| `copy track` paste | only the destination TRACK LED blinks |

---

## 16. Follow-ups

### 16.1 Decisions (user, 2026-10-02)

| # | Question | Decision | Applied in |
|---|---|---|---|
| Q1 | Bar copy labels | `bar -> repl`, `bar -> merge`, `auto -> repl`, `auto -> merge` | F1-E |
| Q2 | When does the clear selection return to `cancel`? | Only on copy/clear release or when the user presses a different **button group**: TRACK, SEQ steps, SELECT bars, PERF SEQ Scenes. Applies to every group, including destructive Scene clears. | F1-G |
| Q3 | Pot clears while a clear menu is up | A menu opened by an object button stays until copy/clear is released; pots do nothing then. Pot clears happen in a fresh SHIFT + copy/clear hold, never open a menu, drop the `_` underline at once and remove the automation in the background. | F1-G-02 |
| Q4 | Overlapping pastes waiting for the buffer | Accepted, but SEQ LEDs/triggers must update quickly. SRAM options given. | §11.4, A2 |
| Q5 | Provisional source in the trace | Final source only. | §13.1 |
| Q6 | Same type across modes | Yes (same button group). | F1-G |
| Q7 | `copy track` blink | Destination TRACK LED only. | F1-F |
| A1 | `clear track` blink | TRACK LED only; the current track's step LEDs are cleared at once. | F1-F, F1-H |
| A2 | Early-trigger storage | O1, +64 B approved. | §11.4 |
| A3 | Paste dropped after early triggers | Restore; any extra RAM O1 needs is approved (+64 B restore masks, +1 B flags). | §11.4 |
| A4 | Trace DEV RAM +4 B | Approved. | §13.1 |
| A5 | `clear … notes` and dropped clears | A clear dropped in low storage may leave its triggers off (accepted). Independently, `… notes` clears every special except probability (when set), keeps automation, trigger off. | F1-H, F1-J |
| B1 | Restore when the user toggled a step meanwhile | Confirmed: such steps keep the user's state. | §11.4 |
| B2 | Automation pastes and probability | `auto -> repl` replaces probability with the source's; `auto -> merge` does not touch it. | F1-K |

### 16.2 Additional follow-ups

None open. Concern carried forward: at 0.1 % CPU the pool work of a large
clear can take a second or two while an earlier drain finishes; the
immediate blink and trigger bits are the user's acceptance signal, and the
trace plan's `JOB_TRICKLE` record will show the real numbers.
