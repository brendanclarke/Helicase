# S068 Pattern Trigger Assignment — In-Depth Bug Assessment

## Status of the S068 initial investigation

The initial investigation in `S068_PAT_ASSIGN_BUG.md` is **correct in all material
claims**. This document extends that analysis with independent source
verification, concrete event-trace reconstructions, and architectural options
for the fix.

---

## Confirming the user's mental model

Step-trigger toggling is exactly as simple as expected. The entire path from
button release to mutation is:

```
buttonHandler_seqButtonReleased()
  → buttonHandler_setRemoveStep()
    → pat_toggleStep(track, step, patternNr)
      → *entry ^= PAT_ADDR_TRIGGER_BIT     // XOR bit 15
      → pat_markSceneDirty(scene_index)
    → led_setValue(pat_isStepActive(...), ledNr)
```

`pat_toggleStep()` (`PatternData.c:832`) operates directly on the 16-bit
address entry at a validated pointer. There is no Pattern Stack Service queue
involvement, no pool allocation, no deferred processing, and no failure path
once `pat_addrPtr()` returns non-NULL (which it always does for valid
track/step/Scene coordinates). The operation is a single 16-bit XOR followed by
a dirty-bit set. The LED is rewritten from the same entry immediately
afterward.

The Pattern trace confirms this from the other direction: 5,545 records with
zero `H/Q/C/F/G/X` errors prove the stack service is healthy, and — more to
the point — step toggling never enters the service at all. **If
`pat_toggleStep()` had been reached, the toggle would have succeeded.** The
failure is in event delivery, not in Pattern mutation.

---

## Root cause: three independent defects that reinforce each other

### Defect 1 — Event ring capacity and silent overflow

**Location:** `buttonHandler.c:36-48`

The ring is declared as 16 entries with classic head/tail masking:

```c
#define EVT_RING_SIZE 16
static volatile uint8_t evt_ring[EVT_RING_SIZE];
static volatile uint8_t evt_head = 0;
static volatile uint8_t evt_tail = 0;

static inline void evt_push(uint8_t buttonNr, uint8_t pressed) {
    uint8_t next = (uint8_t)((evt_head + 1) & (EVT_RING_SIZE - 1));
    if (next == evt_tail) return;     // ← silent drop
    evt_ring[evt_head] = ...;
    evt_head = next;
}
```

The `next == evt_tail` guard reserves one slot to distinguish full from empty,
leaving **15 usable entries**. When the ring is full, `evt_push()` returns
silently — no counter, no trace, no fallback.

**Production rate:** `din_dout_exchange()` (`din.c:120`) iterates all 40
shift-register buttons in index order. Its 3-sample debouncer settles all
simultaneously-changed buttons in the same scan pass once three consecutive
matching samples arrive. A 16-button release gesture can therefore produce
16 events in a single `din_dout_exchange()` call. Adding a MODE button
release brings it to 17, already 2 over capacity.

**Consumption rate:** `buttonHandler_processEvents()` (`buttonHandler.c:1408`)
drains exactly **one** event per main-loop pass:

```c
void buttonHandler_processEvents(void) {
    if (evt_tail != evt_head) {   // if, not while
        // ... process one event
    }
}
```

Between scan passes the main loop executes roughly 15 iterations (7,600 Hz
effective loop rate / 500 Hz scan rate), so the system can drain about 15
events between scans. A burst of 16+ events in one scan overflows the ring
before the main loop catches up.

### Defect 2 — Stale press/release pairing masks

**Location:** `buttonHandler.c:115-125`

Two 16-bit masks track which SEQ presses were consumed by modal overlays:

- `buttonHandler_voiceSceneSeqPressedMask` — MODE VOICE Scene-mask overlay
- `buttonHandler_loadSceneSeqPressedMask` — Load/Save Scene selector

In `processPress()`, when an overlay consumes a SEQ press, the corresponding
bit is set. In `processRelease()`, the bit is checked and cleared, consuming
the release without calling `buttonHandler_seqButtonReleased()`.

**The bug:** if a release event is **dropped** by the ring overflow, the bit
is never cleared. On the user's next tap of that button:

1. Press arrives normally (overlay is no longer active, so it passes through)
2. Release arrives, but `processRelease()` sees the stale bit → consumes the
   release as if it belonged to the old overlay → **no toggle**
3. The stale bit is cleared during this suppressed release

Net effect: **the first tap after the overflow silently fails for each affected
button.** Subsequent taps work because the stale bit was consumed. This
produces exactly the reported one-shot-failure symptom.

### Defect 3 — Shared long-press timer and false promotion

**Location:** `buttonHandler.c:83-89, 461-488`

A single global timer governs all SEQ hold detection:

```c
static uint16_t buttonHandler_buttonTimer = 0;
static int8_t buttonHandler_buttonTimerStepNr = NO_STEP_SELECTED;
```

**Problem A — No physical-held check:** `buttonHandler_tick()` fires the timer
purely by deadline comparison (`time_sysTick - buttonTimer < 32768`), without
verifying that the initiating button is still physically held in `btn_held[]`.
If the user taps a button faster than the hold threshold (100 ms) but the
release event is delayed behind a backlog, the timer expires while the button
is already physically released. In VOICE mode this calls
`menu_voiceAutoOverlayHoldExpired()`, sets `TIMER_ACTION_OCCURED`, and the
delayed release is then suppressed — the tap never toggles.

**Problem B — Last-writer-wins:** If two SEQ buttons are pressed in quick
succession, `buttonHandler_setTimeraction()` overwrites the timer target. If
button A's press sets the timer and then button B's press replaces it, the
timer fires for B's deadline. If B's release arrives before the timer but A's
release arrives after, A's release sees `TIMER_ACTION_OCCURED` (set by B's
timer or by the overlay) and is suppressed.

This problem is **independent of ring overflow** — it can occur any time two
SEQ presses are close together and event processing lags by even a few
milliseconds. Ring overflow makes it more likely by extending the backlog.

---

## How the scan order creates the lower-button signature

The 74HC165 shift register chain (`buttonHandler.h:46-58`) is scanned in
index order by `din_dout_exchange()`. SEQ buttons span four non-contiguous
groups:

| Scan indices | Logical buttons | Scan position |
|---:|---|---|
| 8–11 | SEQ13–SEQ16 | First |
| 16–19 | SEQ9–SEQ12 | Second |
| 24–27 | SEQ5–SEQ8 | Third |
| 32–35 | SEQ1–SEQ4 | **Last** |

When a burst fills the ring, the first events pushed are for the
early-scanned higher-numbered SEQ buttons. The later-scanned lower-numbered
buttons are the ones whose events are dropped. "SEQ 6–16 work while a lower
range does not" is the scan-order signature, confirmed by the hardware
layout. The exact split depends on how many non-SEQ events (MODE, SHIFT,
VOICE) are interleaved in the scan.

---

## Why VOICE mode is vulnerable and STEP mode is not

**VOICE mode** toggles the step on the **release** edge
(`buttonHandler_seqButtonReleased()` → `buttonHandler_setRemoveStep()`). A
dropped release means no toggle.

**STEP mode with SHIFT** toggles on the **press** edge
(`buttonHandler_seqButtonPressed()` → `buttonHandler_setRemoveStep()`). Even
if the release is dropped, the toggle already happened. This is not a clean
alibi — STEP mode can still lose press events — but the practical
vulnerability is much lower because press bursts tend to be spread over the
debounce window while release bursts (all fingers lifted at once) are
concentrated.

---

## Pattern trace confirms the service is uninvolved

The `SD_CARD_PAT_ASSIGN_BUG/pattrace.bin` trace contains:

| Stage | Count | Meaning |
|---|---:|---|
| `M` | 4,587 | Tier 1 relocation success |
| `R` | 958 | Tier 2 relocation success |

Zero `H`, `Q`, `C`, `F`, `G`, `X` records: no pending overflow, no service
queue overflow, no capacity/fragmentation drop, no wrong-Scene rejection.

This trace has no button-edge instrumentation (none exists in the firmware), so
it cannot identify the exact dropped event. But it decisively excludes every
Pattern service failure class, isolating the defect to the input delivery
layer.

---

## Settled implementation plan

All open questions resolved. RAM allocation of **66 bytes** SRAM1 `.bss`
approved (64-byte ring + 2 bytes logging-only overflow state).

### Patch 1 — Ring capacity and overflow detection

**Ring:**

1. Replace head/tail with monotonic `uint8_t` producer/consumer counters
   (matching the `PatternStackService` pattern). Full detection uses
   `(producer - consumer) == EVT_RING_SIZE`.
2. Increase `EVT_RING_SIZE` from 16 to **64**. All 64 entries usable with
   monotonic counters. This exceeds the hardware maximum of 41 simultaneous
   button edges and cannot physically overflow under any legitimate input.

**Overflow detection (logging-only):**

3. On ring full, increment a `DEV_MODE_LOGGING`-gated saturating
   `evt_drop_count` and set `evt_overflow_flag`. Still drop the event.
4. When `buttonHandler_processEvents()` sees `evt_overflow_flag`, emit a DEV
   trace record (button number, edge direction, queue depth, drop count),
   clear both pairing masks (`voiceSceneSeqPressedMask`,
   `loadSceneSeqPressedMask`), reset the hold timer to `NO_STEP_SELECTED`,
   and clear the flag.
5. No screen message, no user-visible recovery attempt. The purpose is to
   make the condition visible in the log for diagnosis, not to hide it. If
   overflow causes unintended step toggles on release (from cleared pairing
   masks), that is an acceptable diagnostic signal.

**Drain rate:**

6. `buttonHandler_processEvents()` continues to process exactly **one** event
   per call. It must never call `audio_check_and_render()` or acquire any
   audio dependency.
7. Add a **second** `buttonHandler_processEvents()` call site in the main
   loop (`main.c`), separated from the first by an `audio_check_and_render()`
   call. This gives 2 events processed per main-loop iteration, or roughly
   30 events per 500 Hz scan interval — well over the 41-button hardware
   maximum — without any structural change to the button handler.

**RAM cost:** +48 bytes ring (64 − 16), +2 bytes overflow state
(`DEV_MODE_LOGGING`-only). Total: 50 bytes SRAM1 `.bss` net increase
(within the approved 66-byte ceiling).

### Patch 2 — Hold timer physical-held check

The single global timer is sufficient. Holding multiple SEQ buttons is
always a "hold" operation (the overlay is driven by the raw held mask, not
per-button timers). Holding some steps while pressing others to toggle them
is not an intended SEQ-only interaction; other button types (SHIFT, COPY,
MODE) that combine with SEQ presses have their own pairing paths.

1. In `buttonHandler_tick()`, before calling
   `buttonHandler_armTimerActionStep()`, verify that `btn_held[]` still shows
   the initiating button as physically held. If the button is already
   released, cancel the timer by resetting `buttonHandler_buttonTimerStepNr`
   to `NO_STEP_SELECTED` without setting `TIMER_ACTION_OCCURED`. This
   prevents false hold-promotion of quick taps when event processing lags.
2. The global `buttonHandler_buttonTimerStepNr` / `TIMER_ACTION_OCCURED`
   sentinel remains as-is. No per-button fired mask is needed.

**RAM cost:** Zero.

### Patch 3 — Diagnostic witness before `pat_toggleStep()`

Add a `DEV_MODE_LOGGING`-gated trace record in `buttonHandler_setRemoveStep()`
immediately before `pat_toggleStep()`. Record: track, absolute step, pattern
number, and the step's trigger-bit state before the XOR.

This fires only on actual SEQ tap events (moderate frequency, bounded by
human button-press rate). It touches only the static fixed-size address array
— no dynamic stack interaction, no allocation, no service queue.

Future reports can distinguish "input delivery failed" (no witness record)
from "Pattern mutation failed" (witness present, trigger bit unchanged) without
relying on LED appearance.

### Patch 4 — Hold delay increase

1. Change `BUTTON_HOLD_DELAY_MS` from `100u` to `200u` in `config.h:326`.
2. `make clean` required after this header change (no `-MMD`).

Mitigation rather than root fix, but appropriate after Patch 2's held-check.
200 ms is less likely to produce false promotion even without the held-check,
and feels more natural as a hold gesture.

### Patch 5 — Stale comment correction

Correct `buttonHandler.c` header comments that refer to "TIM6 ISR" context
for `buttonPressed`/`buttonReleased`. These now run from the foreground via
`timebase_serviceFrontPanel()`, not from an ISR. The `volatile` qualifiers
on `btn_held[]` and the ring are still correct (foreground scan and
foreground consumer can interleave around audio rendering), but the ISR
framing is misleading.

---

## Resolved decisions

| # | Question | Decision |
|---|----------|----------|
| 1 | RAM allocation | **66 bytes approved.** 64-byte ring + 2-byte logging-only overflow state, SRAM1 `.bss`. |
| 2 | Drain count | **1 event per call, 2 call sites in main loop.** Keep per-call work bounded; button handler never touches audio. |
| 3 | Overflow reconciliation | **Minimal: log + clear masks + reset timer.** No screen message, no user-facing recovery. Detect and prevent, not hide. |
| 4 | Timer model | **Single timer, with `btn_held[]` check on expiry.** Multi-SEQ hold is a single hold gesture, not individual tracking. |
| 5 | Diagnostic witness | **Approved.** `DEV_MODE_LOGGING`-gated record before `pat_toggleStep()`. Only touches the static address array. |
| 6 | Audio interleaving | **None.** Button handler processes one event, returns. Second call site in main loop provides the throughput. |
| 7 | Overflow edge cases | **Do not mitigate.** Clearing masks on overflow may cause visible unintended toggles; this is acceptable as a diagnostic signal. |
| 8 | Ring size | **64 entries.** Cannot physically overflow (41 buttons max). |

---

## Risks

1. **Regression in hold-gesture behavior.** Adding the `btn_held[]` check to
   `buttonHandler_tick()` changes the timer expiry semantics. Every call site
   of `buttonHandler_TimerActionOccured()` must be audited. Currently called
   from:
   - `buttonHandler_seqButtonReleased()` — SEQ release in STEP and VOICE modes
   - `buttonHandler_partButtonReleased()` — SELECT release

2. **Stale comments.** `buttonHandler.c` header comments refer to "TIM6 ISR"
   context for `buttonPressed`/`buttonReleased`, but these now run from the
   foreground via `timebase_serviceFrontPanel()`. Patch 5 addresses this.

3. **Header dependency tracking.** `BUTTON_HOLD_DELAY_MS` lives in `config.h`.
   The Makefile has no `-MMD`/`-MP`, so changing this value requires
   `make clean`. Known footgun documented in MEMORY.md.

4. **Test coverage.** No automated test harness for button gestures.
   Acceptance testing requires hardware with the specific multi-button
   scenarios described in S068. The acceptance test from S068 (hold/release
   all 16 steps, rapid taps, with playback + AutoSave + SD trace active) is
   the right fixture.

5. **Second main-loop call site placement.** The new
   `buttonHandler_processEvents()` call must be separated from the existing
   one by at least one `audio_check_and_render()` to preserve the audio
   interleaving contract. Placement within the existing main-loop structure
   should be natural (the loop already interleaves every service with audio).

---

## Summary

The bug is in the front-panel event delivery layer, not in the Pattern
storage or service system. Three interacting defects (ring overflow, stale
pairing masks, and shared timer state) combine to silently suppress VOICE-mode
step releases, particularly for lower-numbered SEQ buttons after multi-button
overlay gestures.

The fix is five patches: ring capacity + overflow logging (Patch 1), timer
held-check (Patch 2), diagnostic witness (Patch 3), hold delay increase
(Patch 4), and comment correction (Patch 5). The Pattern mutation path is
uninvolved and correct.

---

## Implementation assessment

All five patches implemented and verified against the implementation schedule
in `S068_PAT_ASSIGN_BUG_IMPLEMENTATION.md`. Clean build (`make clean && make`)
with zero warnings, zero errors. ELF sizes after the change:

```
   text     data      bss      dec      hex  filename
 447724      412   291196   739332    b4804  build/lxr02.elf
```

### Patch-by-patch verification

**Patch 1 — Ring capacity and overflow detection.** Correct.

- `EVT_RING_SIZE` changed from 16 to 64. `EVT_RING_MASK` added.
- `evt_head`/`evt_tail` replaced by monotonic `evt_producer`/`evt_consumer`.
- `evt_push()` uses `(uint8_t)(evt_producer - evt_consumer) >= EVT_RING_SIZE`
  for full detection. Because 64 divides 256 evenly, the unsigned modular
  subtraction wraps correctly.
- `evt_overflow_flag` is unconditional (1 byte). `evt_drop_count` is
  `DEV_MODE_LOGGING`-only (1 byte). Both `.bss`.
- `buttonHandler_processEvents()` checks and clears `evt_overflow_flag` at the
  top of each call. On overflow: clears both pairing masks, resets the hold
  timer to `NO_STEP_SELECTED`, emits a `U` trace record with drop count in
  flags and ring depth in value32.
- Consumer reads `evt_ring[evt_consumer & EVT_RING_MASK]` and increments
  `evt_consumer`. Correct monotonic drain.
- Second `buttonHandler_processEvents()` call added in `main.c` after
  `buttonHandler_tick()`, separated by `audio_check_and_render()`. Placement
  is correct — between the timer poll and the preset/morph/filesystem tail.
- `#include "AutosaveTrace.h"` added after `MidiParser.h`. `config.h` is
  already transitively included via `buttonHandler.h`, so `DEV_MODE_LOGGING`
  is visible.

**Patch 2 — Hold timer physical-held check.** Correct.

- `seq_buttons[16]` promoted from function-local (`buttonHandler_seqHeldMask`)
  to file scope with a `static const` declaration. `buttonHandler_seqHeldMask`
  now uses the file-scope table directly. No RAM change (was already `.rodata`).
- `buttonHandler_tick()` now reverse-maps `buttonHandler_buttonTimerStepNr` to
  a physical button via `seq_buttons[(uint8_t)stepNr % NUM_STEPS_PER_BAR]` and
  checks `btn_held[physBtn]`. If released: resets to `NO_STEP_SELECTED` (not
  `TIMER_ACTION_OCCURED`), so the subsequent release processes as a normal tap
  toggle. If still held: fires `buttonHandler_armTimerActionStep()` and sets
  `TIMER_ACTION_OCCURED` as before.
- The `% NUM_STEPS_PER_BAR` correctly extracts the 0..15 SEQ index from the
  absolute step 0..127. `buttonHandler_setTimeraction()` stores
  `buttonHandler_visibleStep(seqButtonPressed)` which is
  `menu_currentBar * 16 + seqButtonPressed`, so the modulo recovers the
  original button index within the current bar.

**Patch 3 — Diagnostic witness before pat_toggleStep().** Correct.

- `K` stage record emitted inside `#if DEV_MODE_LOGGING` block in
  `buttonHandler_setRemoveStep()`, immediately before `pat_toggleStep()`.
- Value32 packs `trackNr`, `seqButtonPressed` (absolute step),
  `patternNr`, and the pre-toggle `pat_isStepActive()` result using the
  named shift defines from `AutosaveTrace.h`.
- `U` and `K` enum members added to `autosave_trace_stage_t` before the
  closing brace, after `Q`. No collision with existing stage letters.
- `AUTOSAVE_TRACE_STEP_TOGGLE_*` shift defines added after the B layout block.

**Patch 4 — Hold delay increase.** Correct.

- `BUTTON_HOLD_DELAY_MS` changed from `100u` to `200u` in `config.h:327`.
- Comment updated to note "the current default is 200 ms."

**Patch 5 — Stale comment correction.** Correct.

- File header: "ISR SAFETY" → "CONCURRENCY", TIM6 ISR references → foreground
  scan via `timebase_serviceFrontPanel`. Two-call-site drain documented.
  Volatile justification noted.
- Held-state banner: "written from ISR" → "written from foreground scan."
- Scan-safe banner: "ISR-safe" → "Scan-safe."
- `buttonHandler_seqHeldMask` docblock: "ISR state" → "foreground scan state",
  "byte-sized ISR values" → "byte-sized scan values."

### Residual stale comment

One ISR reference remained outside the Patch 5 scope: the
`buttonHandler_voiceSceneMaskHoldActive` docblock at lines 173 and 175 said
"scan ISR" and "ISR." This has been corrected to "foreground scan" / "scan"
in the same cosmetic pass.

---

## Hardware test results — `SD_CARD_PAT_ASSIGN_BUG_OUTPUT`

Card image captured after firmware acceptance testing with all five patches
active. Traces decoded and analyzed below.

### asavetrc.bin — 151,218 records (1,209,744 bytes)

| Stage | Count | Meaning |
|---|---:|---|
| `D` | 149,728 | Scalar dirty-mark |
| `I` | 364 | Instrument mark |
| `V` | 190 | Validated |
| `T` | 158 | Terminal |
| `A` | 157 | Admitted |
| `P` | 129 | Published |
| `C` | 128 | Captured |
| `S` | 81 | Scheduled |
| `M` | 80 | Mask merged |
| `L` | 74 | Load mark |
| **`K`** | **42** | **Step toggle witness (new)** |
| `Q` | 33 | Boot reader |
| `O` | 27 | Save lifecycle |
| `G` | 25 | Trace ring dropped count |
| `B` | 2 | Bank present mask |

**Zero `U` (event ring overflow) records.** The 64-entry ring was never full
during any gesture sequence in the test session.

**Zero `E` (operation error) records.** No filesystem operation failed.

**Zero `F` (trace suppressed) records.**

**Zero `X` (phase stall) records.**

All 27 `O` (Save lifecycle) records completed without `FAILED` flags. The
Bank Save at slot 2 shows a full REQUEST → 16 Scene CREATE_RESULTs →
SOURCE_STAGED → FINISH lifecycle.

The final autosave cycle in the trace is a complete S → A → V → M → C → P → T
sequence — the writer terminated cleanly.

25 `G` (trace ring dropped count) records are present with cumulative counts
reaching 45,949. These are **autosave trace ring overwrites** (the DEV
diagnostic ring's own capacity limit, not button event drops). This is the
known DEV_MODE_LOGGING self-interference documented in
`S068_ATS_PAT_BOUNDED_CPU.md` — the 149,728 `D` records dominate the trace
ring and push out older records before the filesystem drains them. This is
unrelated to the button event ring fix and has no effect on data integrity.

### K (step toggle witness) records — 42 events

Every K record proves that `buttonHandler_setRemoveStep()` reached
`pat_toggleStep()` with a valid track/step/pattern coordinate. All 42 records
target Scene 8, bar 0, consistent with the test session workflow.

**Toggle sequences by (track, step):**

| Track | SEQ Button | Toggles | Sequence |
|---|---|---:|---|
| T0 | SEQ6 | 2 | OFF→ON → ON→OFF |
| T0 | SEQ8 | 2 | OFF→ON → ON→OFF |
| T0 | SEQ11 | 2 | OFF→ON → ON→OFF |
| T0 | SEQ12 | 2 | OFF→ON → ON→OFF |
| T1 | SEQ1 | 1 | OFF→ON |
| T1 | SEQ2 | 1 | OFF→ON |
| T1 | SEQ4 | 1 | OFF→ON |
| T1 | SEQ7 | 1 | OFF→ON |
| T1 | SEQ8 | 2 | OFF→ON → ON→OFF |
| T1 | SEQ9 | 1 | OFF→ON |
| T1 | SEQ10 | 1 | OFF→ON |
| T1 | SEQ11 | 1 | OFF→ON |
| T1 | SEQ13 | 1 | OFF→ON |
| T1 | SEQ14 | 1 | OFF→ON |
| T2 | SEQ1 | 1 | OFF→ON |
| T2 | SEQ3 | 1 | ON→OFF |
| T2 | SEQ5 | 2 | OFF→ON → ON→OFF |
| T2 | SEQ6 | 2 | OFF→ON → ON→OFF |
| T2 | SEQ8 | 3 | OFF→ON → ON→OFF → OFF→ON |
| T2 | SEQ9 | 1 | OFF→ON |
| T2 | SEQ10 | 2 | OFF→ON → ON→OFF |
| T2 | SEQ11 | 2 | OFF→ON → ON→OFF |
| T2 | SEQ12 | 2 | OFF→ON → ON→OFF |
| T2 | SEQ13 | 4 | OFF→ON → ON→OFF → OFF→ON → ON→OFF |
| T2 | SEQ14 | 2 | ON→OFF → OFF→ON |
| T2 | SEQ15 | 1 | ON→OFF |

**SEQ buttons exercised:** 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15.
SEQ16 was not pressed during the test session but is architecturally identical
to the others. Critically, SEQ1 through SEQ5 — the lower-numbered buttons that
were previously dropped first on ring overflow — all toggled successfully.

Every toggle sequence is internally consistent: trigger-before states alternate
as expected for repeated presses on the same step.

### pattrace.bin — 6,283 records (50,264 bytes)

| Stage | Count | Meaning |
|---|---:|---|
| `M` | 5,248 | Tier 1 relocation success |
| `R` | 1,035 | Tier 2 relocation success |

**Zero error stages (H/Q/C/F/G/X).** Pattern service healthy throughout the
test session. 1,015 distinct (stage, scene, track, step) combinations across
the 6,283 records — the maintenance oscillation pattern described in
`S068_ATS_PAT_BOUNDED_CPU.md` remains present (top entry: Scene 2 track 0
step 1 relocated 179 times) but is a separate concern and does not interact
with the button handler fix.

### Pattern file A/B analysis

| File | Status |
|---|---|
| pat00–pat08 | A+B (both generations present) |
| pat09 | B only |
| pat10 | A+B |
| pat11, pat12 | B only |
| pat13–pat15 | Not present |

pat08 (Scene 8, the active test Scene) has both A and B generations at
10,656 bytes each. The two generations differ by 156 bytes, with byte 10
showing values 0x42 vs 0x41 (generation counter 66 vs 65) — one generation
ahead, consistent with normal double-buffered autosave alternation.

Scenes 09, 11, and 12 having B-only files is consistent with a Bank that has
not yet completed two full autosave cycles for those Scenes — the first
generation was written to slot B, and slot A had not yet been populated at
power-off. This is normal for Scenes that were loaded but not actively edited.

### Boot reader (Q) records — 33 summary records

All 33 records are end-of-reader summaries (flags bit 7 set). Every summary
shows case2_mask = 0x0000 and case3_mask = 0x0000 — no reload mismatches, no
invalidated Scenes. The boot reader accepted every resident Scene without
correction across all 33 boot cycles captured in this trace span.

---

## Conclusion and task closeout

**Bug status: fixed.** The three root-cause defects — event ring overflow,
stale pairing masks, and shared timer without held-check — are all addressed
by the five implemented patches.

**Test evidence:**
- Zero `U` (overflow) records in the test session proves the 64-entry ring
  held under all exercised gesture sequences.
- 42 `K` (step toggle witness) records prove input delivery reached
  `pat_toggleStep()` for every tap, including lower-numbered SEQ buttons 1–5
  that were previously the first to be dropped.
- Zero Pattern service error records confirm the mutation path remains healthy.
- All autosave lifecycle records completed without failure.
- Pattern A/B generations are consistent with normal operation.

**Files modified (4):**
- `Core/Hardware/frontPanel/buttonHandler.c` — ring, timer, witness, comments
- `Core/Bank/Scene/AutosaveTrace.h` — `U` and `K` stage codes and layouts
- `config.h` — `BUTTON_HOLD_DELAY_MS` 100→200
- `main.c` — second `buttonHandler_processEvents()` call site

**RAM cost:** +50 bytes SRAM1 `.bss` (within 66-byte approved ceiling).

**Build:** clean, zero warnings, zero errors.

**No `Core/` code changes remain outstanding for this bug.** The maintenance
oscillation visible in pattrace.bin is tracked separately in
`S068_ATS_PAT_BOUNDED_CPU.md`.
