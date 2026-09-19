# S068 Pattern Trigger Assignment — Implementation Schedule

Parent: `S068_PAT_ASSIGN_BUG_IN_DEPTH.md`

Every code change site is listed by file, line, and action (add/modify/remove).
Descriptions are written to serve as adjacent comment-block text documenting
each change in both `.c` and `.h` files.

Post-implementation: `make clean && make && make img` (header changes require
clean build; no `-MMD` in this Makefile).

---

## Patch 1 — Ring capacity and overflow detection

### Site 1.1 — Ring size and counter declarations

**File:** `Core/Hardware/frontPanel/buttonHandler.c`
**Lines:** 33–41
**Action:** Modify

Replace the head/tail ring section:

```c
/* -----------------------------------------------------------------------
** Event ring — ISR writes, main loop reads
** ----------------------------------------------------------------------- */
#define EVT_RING_SIZE 16   /* power of two */
#define EVT_PRESSED   0x80

static volatile uint8_t evt_ring[EVT_RING_SIZE];
static volatile uint8_t evt_head = 0; /* written by ISR */
static volatile uint8_t evt_tail = 0; /* read  by main  */
```

with:

```c
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
** architectural maximum), 41 events cannot overflow 64 slots. The ring is
** therefore immune to overflow under any legitimate input scenario.
**
** Inputs: evt_push() is called from buttonHandler_buttonPressed/Released,
** which run in the foreground din_dout_exchange() scan at 500 Hz.
** Outputs: buttonHandler_processEvents() drains one event per call from the
** main loop. Two call sites in the main loop give ~30 events drained per
** scan interval.
**
** RAM: 64 bytes ring + 2 bytes overflow state = 66 bytes SRAM1 .bss
** (approved in S068_PAT_ASSIGN_BUG_IN_DEPTH.md).
** ----------------------------------------------------------------------- */
#define EVT_RING_SIZE 64u  /* power of two, must exceed BUT_COUNT (41) */
#define EVT_RING_MASK (EVT_RING_SIZE - 1u)
#define EVT_PRESSED   0x80u

static volatile uint8_t evt_ring[EVT_RING_SIZE];
static volatile uint8_t evt_producer = 0; /* monotonic, written by scan */
static volatile uint8_t evt_consumer = 0; /* monotonic, written by main */

/*
 * Overflow detection state (unconditional 1-byte flag, DEV_MODE_LOGGING-only
 * 1-byte counter).
 *
 * What: evt_overflow_flag is set by evt_push() when the ring is full.
 * buttonHandler_processEvents() checks and clears it on the next drain pass,
 * using it to reconcile gesture state that may have been corrupted by the
 * dropped event. evt_drop_count is a saturating counter incremented on each
 * drop, included in the diagnostic trace record, and reset after emission.
 *
 * Why unconditional flag: even in production builds, a dropped event must
 * trigger pairing-mask and timer reconciliation to prevent the stale-bit
 * failure mode described in S068_PAT_ASSIGN_BUG_IN_DEPTH.md Defect 2.
 * The counter is logging-only because its value is useful only in trace
 * analysis.
 *
 * Lifetime: file-scope static, .bss, never freed.
 */
static volatile uint8_t evt_overflow_flag = 0;
#if DEV_MODE_LOGGING
static volatile uint8_t evt_drop_count = 0;
#endif
```

### Site 1.2 — evt_push() rewrite

**File:** `Core/Hardware/frontPanel/buttonHandler.c`
**Lines:** 43–49
**Action:** Modify

Replace:

```c
static inline void evt_push(uint8_t buttonNr, uint8_t pressed)
{
    uint8_t next = (uint8_t)((evt_head + 1) & (EVT_RING_SIZE - 1));
    if (next == evt_tail) return; /* ring full — drop event */
    evt_ring[evt_head] = (uint8_t)(buttonNr | (pressed ? EVT_PRESSED : 0));
    evt_head = next;
}
```

with:

```c
/*
 * Push one button edge event into the ring.
 *
 * What: encodes buttonNr (0..BUT_COUNT-1) and direction (press/release) into
 * one byte, stores it at the producer's slot, and advances the monotonic
 * producer counter. If the ring is full, the event is dropped and the
 * overflow flag is set.
 *
 * Why inline: this runs at the scan rate (500 Hz * up to 41 buttons per
 * scan). The function body is a few compares and a byte store; call overhead
 * would dominate.
 *
 * Full detection: `(uint8_t)(evt_producer - evt_consumer) >= EVT_RING_SIZE`
 * uses unsigned modular subtraction on the 8-bit counters. Because
 * EVT_RING_SIZE (64) divides evenly into the uint8_t range (256), the
 * monotonic counters wrap cleanly and the comparison remains correct across
 * the 0xFF→0x00 boundary.
 *
 * Inputs: buttonNr is a BUT_* enum value; pressed is nonzero for a press
 * edge, zero for release.
 * Outputs: one ring entry written, or overflow flag set on drop.
 * Affiliates: evt_consumer is read but never written here. evt_overflow_flag
 * and evt_drop_count are written only on the full path.
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
```

### Site 1.3 — processEvents() rewrite

**File:** `Core/Hardware/frontPanel/buttonHandler.c`
**Lines:** 1400–1422
**Action:** Modify

Replace the entire function and its preceding comment block:

```c
/* -----------------------------------------------------------------------
** buttonHandler_processEvents — call from main loop, safe to call LCD
**
** Drains ONE event per call. Original AVR processed one button per main-loop
** iteration. With our 1kHz TIM6 SPI exchange all 40 button states arrive
** atomically and can fire many events at once. `if` keeps the original AVR
** cadence of one button per main-loop pass.
** ----------------------------------------------------------------------- */
void buttonHandler_processEvents(void)
{
    if (evt_tail != evt_head) {
        uint8_t ev  = evt_ring[evt_tail];
        evt_tail = (uint8_t)((evt_tail + 1) & (EVT_RING_SIZE - 1));

        uint8_t pressed  = (uint8_t)((ev & EVT_PRESSED) != 0);
        uint8_t buttonNr = (uint8_t)(ev & (uint8_t)~EVT_PRESSED);

        if (pressed)
            processPress(buttonNr);
        else
            processRelease(buttonNr);
    }
}
```

with:

```c
/* -----------------------------------------------------------------------
** buttonHandler_processEvents — call from main loop, safe to call LCD
**
** What: drains ONE event per call from the monotonic-counter ring, then
** returns. Two call sites in the main loop (separated by
** audio_check_and_render) give ~30 events per 500 Hz scan interval, well
** over the 41-button hardware maximum.
**
** Why one per call: the original AVR cadence of one button per main-loop
** iteration keeps per-call CPU bounded and preserves the audio interleaving
** contract. The button handler must never call audio_check_and_render()
** itself or acquire any audio dependency.
**
** Overflow reconciliation: when evt_overflow_flag is set by evt_push(),
** this function clears both pairing masks (voiceSceneSeqPressedMask,
** loadSceneSeqPressedMask) and resets the hold timer to NO_STEP_SELECTED.
** This prevents Defect 2 (stale pairing bits from dropped releases
** suppress the next tap) and Defect 3 (orphaned timer fires for a
** button whose release was lost). The reconciliation may cause a visible
** unintended toggle on release for buttons whose presses were overlay-
** consumed — this is accepted as a diagnostic signal, not hidden.
**
** Inputs: evt_ring[], evt_producer (read), evt_consumer (read/write),
** evt_overflow_flag (read/clear).
** Outputs: dispatches to processPress() or processRelease(); on overflow,
** emits a DEV trace record and reconciles gesture state.
** Affiliates: evt_push() is the sole producer.
** ----------------------------------------------------------------------- */
void buttonHandler_processEvents(void)
{
    if (evt_overflow_flag) {
        evt_overflow_flag = 0;
        buttonHandler_voiceSceneSeqPressedMask = 0u;
        buttonHandler_loadSceneSeqPressedMask = 0u;
        buttonHandler_buttonTimerStepNr = NO_STEP_SELECTED;
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

        uint8_t pressed  = (uint8_t)((ev & EVT_PRESSED) != 0u);
        uint8_t buttonNr = (uint8_t)(ev & (uint8_t)~EVT_PRESSED);

        if (pressed)
            processPress(buttonNr);
        else
            processRelease(buttonNr);
    }
}
```

### Site 1.4 — Include AutosaveTrace.h

**File:** `Core/Hardware/frontPanel/buttonHandler.c`
**Lines:** 26–27 (after `#include "MidiParser.h"`)
**Action:** Add

Add after line 26:

```c
#include "AutosaveTrace.h"
```

**Why:** `autosaveTrace_record()` is called in the overflow reconciliation path
(Site 1.3) and the diagnostic witness (Site 3.1). The include is unconditional;
when `DEV_MODE_LOGGING` is 0, `AutosaveTrace.h` provides no-op stubs so the
call sites compile to nothing.

### Site 1.5 — Second processEvents() call in main loop

**File:** `main.c`
**Lines:** 1288–1289
**Action:** Add

Current code at lines 1285–1289:

```c
        buttonHandler_processEvents();
        audio_check_and_render();
        // tick the update buttons async
        buttonHandler_tick();
        audio_check_and_render();
```

Insert after line 1289 (after the `audio_check_and_render()` that follows
`buttonHandler_tick()`):

```c
        // second event drain: tick may have cancelled a timer; drain its
        // successor so the release is processed in the same loop pass
        buttonHandler_processEvents();
        audio_check_and_render();
```

**Why:** a single drain per loop iteration can process ~15 events between
500 Hz scan passes. With the 64-entry ring this is already sufficient to
prevent overflow, but a second drain placed after `buttonHandler_tick()`
has a specific behavioral benefit: if `tick()` just cancelled a timer (via
the new held-check in Patch 2), the next event to drain is likely the
release edge for that same button. Processing it immediately in the same
loop pass means the tap toggle happens without a one-iteration delay.

**Inputs:** none (same function, second call site).
**Outputs:** drains one more event; combined with the first call, gives
2 events per main-loop pass (~30 per scan interval).
**Affiliates:** the first `buttonHandler_processEvents()` at line 1285,
`buttonHandler_tick()` at line 1288, and the audio interleave contract.

---

## Patch 2 — Hold timer physical-held check

### Site 2.1 — Promote seq_buttons[] to file scope

**File:** `Core/Hardware/frontPanel/buttonHandler.c`
**Lines:** 145–150 (inside `buttonHandler_seqHeldMask()`)
**Action:** Modify

Move the `static const uint8_t seq_buttons[16]` array from function-local
scope inside `buttonHandler_seqHeldMask()` to file scope, immediately above
the function definition.

Current (lines 143–150):

```c
uint16_t buttonHandler_seqHeldMask(void)
{
    static const uint8_t seq_buttons[16] = {
        BUT_SEQ1, BUT_SEQ2, BUT_SEQ3, BUT_SEQ4,
        BUT_SEQ5, BUT_SEQ6, BUT_SEQ7, BUT_SEQ8,
        BUT_SEQ9, BUT_SEQ10, BUT_SEQ11, BUT_SEQ12,
        BUT_SEQ13, BUT_SEQ14, BUT_SEQ15, BUT_SEQ16
    };
```

Becomes (insert before `buttonHandler_seqHeldMask` definition):

```c
/*
 * Physical button numbers for the sixteen SEQ buttons, indexed 0..15.
 *
 * What: maps the logical step-button index used by Menu, PatternData, and
 * the hold timer back to the physical BUT_SEQ* number for btn_held[] lookup.
 *
 * Why file scope: buttonHandler_tick() needs this table to reverse-map
 * buttonHandler_buttonTimerStepNr (an absolute step 0..127) back to a
 * physical button number for the held-check. Previously local to
 * buttonHandler_seqHeldMask(), which also uses it for the same translation
 * in the opposite direction (physical → logical mask bit).
 *
 * Inputs: index is a 0..15 SEQ button index (== absolute_step %
 * NUM_STEPS_PER_BAR). Output: BUT_SEQ* enum value suitable for indexing
 * btn_held[].
 *
 * RAM: zero additional — `static const` was already in .rodata; moving it
 * to file scope changes only visibility, not storage.
 */
static const uint8_t seq_buttons[16] = {
    BUT_SEQ1, BUT_SEQ2, BUT_SEQ3, BUT_SEQ4,
    BUT_SEQ5, BUT_SEQ6, BUT_SEQ7, BUT_SEQ8,
    BUT_SEQ9, BUT_SEQ10, BUT_SEQ11, BUT_SEQ12,
    BUT_SEQ13, BUT_SEQ14, BUT_SEQ15, BUT_SEQ16
};
```

Remove the `static const` declaration from inside `buttonHandler_seqHeldMask()`
and reference the file-scope array directly.

### Site 2.2 — Add held-check to buttonHandler_tick()

**File:** `Core/Hardware/frontPanel/buttonHandler.c`
**Lines:** 477–489
**Action:** Modify

Replace:

```c
void buttonHandler_tick(void)
{
    /*
     * Foreground long-press poll using the wrapping 16-bit millisecond clock.
     * A deadline is in the past when unsigned elapsed time is below half the
     * counter range. This remains correct across the time_sysTick wrap.
     */
    if (buttonHandler_buttonTimerStepNr >= 0 &&
        (uint16_t)(time_sysTick - buttonHandler_buttonTimer) < 32768u) {
        buttonHandler_armTimerActionStep(buttonHandler_buttonTimerStepNr);
        buttonHandler_buttonTimerStepNr = TIMER_ACTION_OCCURED;
    }
}
```

with:

```c
void buttonHandler_tick(void)
{
    /*
     * Foreground long-press poll using the wrapping 16-bit millisecond clock.
     *
     * What: checks whether the hold timer deadline has elapsed, and if so,
     * verifies that the initiating button is still physically held before
     * promoting the gesture to a long-press action.
     *
     * Why the held-check: without it, a quick tap whose release event is
     * delayed behind other events in the ring can have its timer expire
     * while the button is already physically released. The timer then calls
     * buttonHandler_armTimerActionStep() and sets TIMER_ACTION_OCCURED,
     * causing the delayed release to be consumed as a completed hold gesture
     * instead of toggling the step. This is Defect 3 from
     * S068_PAT_ASSIGN_BUG_IN_DEPTH.md.
     *
     * How the reverse mapping works: buttonHandler_buttonTimerStepNr stores
     * the absolute step index (0..127) set by buttonHandler_setTimeraction()
     * via buttonHandler_visibleStep(). The physical button is at
     * seq_buttons[stepNr % NUM_STEPS_PER_BAR], where seq_buttons[] maps
     * logical index 0..15 to the BUT_SEQ* enum value.
     *
     * Deadline comparison: `(uint16_t)(time_sysTick - buttonTimer) < 32768u`
     * treats the unsigned elapsed time as past when it is less than half the
     * counter range. This remains correct across the time_sysTick wrap.
     *
     * Inputs: buttonHandler_buttonTimerStepNr (absolute step or sentinel),
     * buttonHandler_buttonTimer (deadline tick), btn_held[] (physical state),
     * seq_buttons[] (reverse mapping table).
     * Outputs: either fires armTimerActionStep and sets TIMER_ACTION_OCCURED,
     * or cancels the timer by resetting to NO_STEP_SELECTED (no sentinel,
     * so the subsequent release processes normally as a tap toggle).
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
```

---

## Patch 3 — Diagnostic witness before pat_toggleStep()

### Site 3.1 — Trace record in buttonHandler_setRemoveStep()

**File:** `Core/Hardware/frontPanel/buttonHandler.c`
**Lines:** 555–559 (inside `buttonHandler_setRemoveStep()`)
**Action:** Add

Insert between the `patternNr = menu_getViewedPattern();` line and the
`pat_toggleStep()` call. Current lines 555–559:

```c
    trackNr = menu_getActiveVoice();
    patternNr = menu_getViewedPattern();
    pat_toggleStep(trackNr, seqButtonPressed, patternNr);
    led_setValue(pat_isStepActive(trackNr, seqButtonPressed, patternNr),
                 ledNr);
```

Insert after line 556:

```c
#if DEV_MODE_LOGGING
    /*
     * Diagnostic witness: proves that the input delivery path reached the
     * Pattern mutation call.
     *
     * What: emits a trace record immediately before pat_toggleStep(),
     * capturing the track, absolute step, pattern number, and the step's
     * current trigger-bit state (before the XOR).
     *
     * Why: future reports of "step didn't toggle" can distinguish input
     * delivery failure (no witness record in the trace) from Pattern
     * mutation failure (witness present but trigger bit unchanged) without
     * relying on LED appearance. The cost is one 8-byte trace record per
     * actual SEQ tap, bounded by human button-press rate (~10 Hz sustained
     * maximum).
     *
     * This touches only the static fixed-size address array via
     * pat_isStepActive() — no dynamic stack interaction, no allocation,
     * no service queue.
     *
     * Value32 layout:
     *   bits 0..7:   trackNr (0..6)
     *   bits 8..15:  seqButtonPressed (absolute step, 0..127)
     *   bits 16..23: patternNr (viewed pattern/Scene index, 0..15)
     *   bit  24:     current trigger state before toggle (0 or 1)
     *
     * Flags: 0 (reserved).
     */
    autosaveTrace_record(
        AUTOSAVE_TRACE_STAGE_STEP_TOGGLE,
        0u,
        (uint32_t)trackNr
        | ((uint32_t)seqButtonPressed << 8u)
        | ((uint32_t)patternNr << 16u)
        | ((uint32_t)pat_isStepActive(trackNr, seqButtonPressed, patternNr)
           << 24u));
#endif
```

### Site 3.2 — New trace stage codes in AutosaveTrace.h

**File:** `Core/Bank/Scene/AutosaveTrace.h`
**Lines:** 155–156 (after `AUTOSAVE_TRACE_STAGE_BOOT_READER = 'Q'`, before
the closing brace of the enum)
**Action:** Add

Insert two new enum members before the closing `}`:

```c
    /*
     * U: button event ring overflow witness. Emitted by
     * buttonHandler_processEvents() when evt_overflow_flag is set, proving
     * that at least one button edge was dropped. flags: the saturating drop
     * count since the last emission (0..255). value32: ring depth at the
     * moment of detection (evt_producer - evt_consumer).
     *
     * Why: the ring overflow is the root cause of S068's silent step-toggle
     * failures. This record makes the condition visible in the trace file
     * without any user-facing message. Under the 64-entry ring this stage
     * should never appear in normal operation; its presence in a trace
     * proves a scenario that exceeds the architectural button-count maximum.
     */
    AUTOSAVE_TRACE_STAGE_EVT_OVERFLOW = 'U',
    /*
     * K: step toggle witness. Emitted by buttonHandler_setRemoveStep()
     * immediately before pat_toggleStep(). Proves that the input delivery
     * path reached the Pattern mutation call for a specific track, step,
     * and pattern.
     *
     * flags: 0 (reserved).
     * value32 layout:
     *   bits 0..7:   trackNr (0..6)
     *   bits 8..15:  absolute step (0..127)
     *   bits 16..23: pattern/Scene index (0..15)
     *   bit  24:     trigger state before toggle (0 = was off, 1 = was on)
     *
     * Why: distinguishes input delivery failure (no K record) from Pattern
     * mutation failure (K present, trigger unchanged) in future diagnosis.
     * Bounded by human button-press rate; no dynamic stack interaction.
     */
    AUTOSAVE_TRACE_STAGE_STEP_TOGGLE = 'K',
```

### Site 3.3 — Trace layout defines for new stages

**File:** `Core/Bank/Scene/AutosaveTrace.h`
**Lines:** After the `AUTOSAVE_TRACE_BANK_PRESENT_MASK_SHIFT` define block
(around line 374), before the function prototypes.
**Action:** Add

```c
/*
 * U (EVT_OVERFLOW) layout. flags = saturating drop count. value32 = ring
 * depth at detection time. No sub-field shifting needed; both fit in their
 * natural widths (flags is uint8_t, depth is uint8_t stored in low byte of
 * value32).
 */

/*
 * K (STEP_TOGGLE) value32 layout. Track, absolute step, pattern, and
 * pre-toggle trigger state are packed into one 32-bit value so the existing
 * eight-byte record format carries the full mutation coordinate without
 * requiring a second record.
 */
#define AUTOSAVE_TRACE_STEP_TOGGLE_TRACK_SHIFT    0u
#define AUTOSAVE_TRACE_STEP_TOGGLE_STEP_SHIFT     8u
#define AUTOSAVE_TRACE_STEP_TOGGLE_PATTERN_SHIFT  16u
#define AUTOSAVE_TRACE_STEP_TOGGLE_TRIGGER_SHIFT  24u
```

---

## Patch 4 — Hold delay increase

### Site 4.1 — BUTTON_HOLD_DELAY_MS

**File:** `config.h`
**Line:** 326
**Action:** Modify

Change:

```c
#define BUTTON_HOLD_DELAY_MS                 100u
```

to:

```c
#define BUTTON_HOLD_DELAY_MS                 200u
```

**Why:** 200 ms is less likely to produce false hold-promotion even without
the held-check, and feels more natural as a hold gesture. This is mitigation
rather than root fix; the held-check (Patch 2) is the structural correction.
`BUTTON_TIMEOUT` in `buttonHandler.h:33` aliases this value and does not need
a separate change.

**Build note:** `config.h` is included by `buttonHandler.h` and transitively
by many translation units. `make clean` is required before rebuilding.

---

## Patch 5 — Stale comment correction

### Site 5.1 — File header comment

**File:** `Core/Hardware/frontPanel/buttonHandler.c`
**Lines:** 1–12
**Action:** Modify

Replace:

```c
/*
 * buttonHandler.c — LXR-02 button handler.
 * Ported from original LXR AVR buttonHandler.c by Julian Schmidt.
 *
 * ISR SAFETY:
 *   buttonHandler_buttonPressed / buttonReleased are called from the TIM6 ISR
 *   (din_dout_exchange). They must not call any LCD functions or enter any
 *   spin-wait. They only write to the event ring and the held[] array.
 *
 *   buttonHandler_processEvents() is called from the main loop. It drains the
 *   ring and calls menu/LED actions, which are safe there.
 */
```

with:

```c
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
 *   dispatches to menu/LED actions. buttonHandler_tick() polls the hold
 *   timer between the two drain calls.
 *
 *   The volatile qualifiers on btn_held[] and the ring remain correct:
 *   the scan and consumer can interleave around audio rendering within
 *   the same foreground context.
 */
```

### Site 5.2 — Held-state comment

**File:** `Core/Hardware/frontPanel/buttonHandler.c`
**Lines:** 28–31
**Action:** Modify

Replace:

```c
/* -----------------------------------------------------------------------
** Held-state array (written from ISR, read from both ISR and main loop)
** ----------------------------------------------------------------------- */
static volatile uint8_t btn_held[BUT_COUNT];
```

with:

```c
/* -----------------------------------------------------------------------
** Held-state array (written from foreground scan, read from main loop)
** ----------------------------------------------------------------------- */
static volatile uint8_t btn_held[BUT_COUNT];
```

### Site 5.3 — Scan-safe comment

**File:** `Core/Hardware/frontPanel/buttonHandler.c`
**Lines:** 51–53
**Action:** Modify

Replace:

```c
/* -----------------------------------------------------------------------
** ISR-safe pressed / released — only record, never block
** ----------------------------------------------------------------------- */
```

with:

```c
/* -----------------------------------------------------------------------
** Scan-safe pressed / released — only record, never block
** ----------------------------------------------------------------------- */
```

### Site 5.4 — Public header concurrency comments

**File:** `Core/Hardware/frontPanel/buttonHandler.h`
**Lines:** 5–8, 61–64, and 80–83
**Action:** Modify

Update the public boundary comments from the retired TIM6-ISR model to the
actual foreground `din_dout_exchange()` scan producer and main-loop consumer.
The comments also document that `buttonHandler_processEvents()` is called
twice per main-loop pass with audio rendering between calls, and that the
held-state mask is maintained by the foreground scan.

**Why:** the implementation and its public declaration must describe the same
concurrency boundary. Leaving the old ISR claim in the header would invite a
future caller to apply the wrong latency/safety assumptions to the event ring.

---

## Summary of changes

| Patch | File | Lines | Action | RAM | Description |
|---|---|---|---|---|---|
| 1.1 | buttonHandler.c | 33–41 | Modify | +50 bytes | Ring 16→64, monotonic counters, overflow flag/counter |
| 1.2 | buttonHandler.c | 43–49 | Modify | — | evt_push() with monotonic full detection |
| 1.3 | buttonHandler.c | 1400–1422 | Modify | — | processEvents() monotonic drain + overflow reconciliation |
| 1.4 | buttonHandler.c | 26–27 | Add | — | `#include "AutosaveTrace.h"` |
| 1.5 | main.c | 1289 | Add | — | Second processEvents() call after tick() |
| 2.1 | buttonHandler.c | 143–150 | Modify | 0 | Promote seq_buttons[] to file scope |
| 2.2 | buttonHandler.c | 477–489 | Modify | 0 | btn_held[] check in buttonHandler_tick() |
| 3.1 | buttonHandler.c | 556 | Add | — | DEV_MODE_LOGGING trace before pat_toggleStep() |
| 3.2 | AutosaveTrace.h | 155 | Add | — | Stage codes 'U' (overflow) and 'K' (step toggle) |
| 3.3 | AutosaveTrace.h | ~374 | Add | — | Value layout defines for U and K stages |
| 4.1 | config.h | 326 | Modify | 0 | BUTTON_HOLD_DELAY_MS 100→200 |
| 5.1 | buttonHandler.c | 1–12 | Modify | 0 | Header comment: ISR→foreground correction |
| 5.2 | buttonHandler.c | 28–31 | Modify | 0 | Held-state comment: ISR→foreground |
| 5.3 | buttonHandler.c | 51–53 | Modify | 0 | Scan-safe comment: ISR→foreground |
| 5.4 | buttonHandler.h | 5–8, 61–64, 80–83 | Modify | 0 | Public concurrency comments: ISR→foreground |

**Total RAM cost:** +50 bytes SRAM1 `.bss` (48 ring expansion + 1 overflow flag
+ 1 drop counter). Within the 66-byte approved ceiling.

**Files modified:** 5 (`buttonHandler.c`, `buttonHandler.h`, `main.c`,
`AutosaveTrace.h`, `config.h`)

**Build:** `make clean && make && make img`

**Acceptance test:** per S068_PAT_ASSIGN_BUG.md — hold/release all 16 steps,
rapid taps in VOICE and STEP modes, with playback + AutoSave + SD trace active.
Assert zero event drops in the trace. Verify lower-numbered SEQ buttons toggle
reliably after overlay gestures.

---

## Implementation notes

### 2026-09-19 — Code implementation pass

The source audit confirmed the scheduled boundaries: Pattern trigger toggling
is a direct `pat_toggleStep()` operation, while the failure mechanism is in
front-panel event delivery and shared hold-gesture state. The Pattern Stack
Service was not changed.

Implemented:

- Replaced the 15-usable-entry masked ring with a 64-entry monotonic-counter
  SPSC ring. All 64 slots are usable; unsigned counter subtraction remains
  valid across the 8-bit wrap because the capacity is 64.
- Added unconditional overflow reconciliation: a dropped event sets a flag;
  the next foreground drain clears both overlay press/release masks and
  cancels the shared hold timer. Logging builds emit stage `U` with the
  saturating drop count and queue depth.
- Added stage `K`, emitted immediately before `pat_toggleStep()`, packing the
  track, absolute step, viewed Pattern/Scene, and pre-toggle trigger state.
- Added the second one-event drain after `buttonHandler_tick()` in `main.c`,
  retaining an `audio_check_and_render()` interleave on both sides.
- Moved the SEQ physical-button lookup table to file scope and made timer
  expiry require the initiating physical button to remain held. A released
  button cancels the timer without setting `TIMER_ACTION_OCCURED`.
- Increased `BUTTON_HOLD_DELAY_MS` from 100 ms to 200 ms.
- Corrected stale ISR descriptions in both `buttonHandler.c` and
  `buttonHandler.h` to document the foreground scan boundary and the two
  bounded main-loop drains.

The approved allocation is unchanged in ownership: the ring grows by 48
bytes in SRAM1 `.bss`, plus one unconditional overflow flag and one
`DEV_MODE_LOGGING`-only drop counter. No Pattern pool, snapshot, or service
allocation was added.

Verification completed for the source/build pass: `make clean && make` and
`make img` both passed, and `git diff --check` passed. The resulting firmware
reports `text=447,724`, `data=412`, `bss=291,196`; the linked symbols show the
64-byte ring plus the two overflow-state bytes. Hardware acceptance of the
VOICE/STEP rapid-tap and all-16-button release fixtures remains pending.
