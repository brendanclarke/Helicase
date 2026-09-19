# S068 Pattern trigger assignment failure

## Finding

The failed VOICE-mode SEQ taps are not being rejected by the dynamic Pattern
stack service. They are being lost or consumed in the front-panel gesture path
before `pat_toggleStep()` is reached.

The primary defect is the button event queue:

- `buttonHandler.c` declares a 16-entry ring, but its head/tail empty-slot
  representation can retain only 15 events.
- `evt_push()` silently drops an edge whenever the ring is full. It records no
  counter, trace, or recovery state.
- `buttonHandler_processEvents()` consumes only one edge per main-loop pass.
- The 500 Hz DIN scan can publish many debounced edges in one pass. A held-step
  automation gesture is specifically capable of producing a burst of up to 16
  SEQ press edges and another 16 release edges, in addition to any mode, shift,
  encoder-button, or transport edge.

VOICE mode changes a trigger only on the SEQ **release**. A dropped release
therefore produces exactly the reported result: `pat_toggleStep()` is never
called, the LED is not changed, and playback continues to see the old trigger
bit. STEP mode with SHIFT changes the trigger on the **press**, so it does not
share the same release-only failure path. That difference explains why the
STEP observation is not a clean alibi but why VOICE is much easier to provoke.

The reported numerical split also follows the hardware scan order. DIN scans
physical indices upward. The SEQ groups occur in this order:

| Scan indices | Logical buttons |
|---:|---|
| 8-11 | SEQ13-16 |
| 16-19 | SEQ9-12 |
| 24-27 | SEQ5-8 |
| 32-35 | SEQ1-4 |

When an edge burst fills the queue, the later-scanned, lower-numbered SEQ
buttons are the first group to be discarded. “SEQ6-16 work while a lower
range does not” is therefore a signature of this queue layout, not a Pattern
pool capacity boundary.

## The hold timer makes the loss easier to turn into a blocked tap

VOICE uses one global `buttonHandler_buttonTimerStepNr`, not per-button gesture
state. `buttonHandler_tick()` promotes the timer after the deadline without
first checking whether that particular button is still physically held. It
then writes the global `TIMER_ACTION_OCCURED` sentinel even if
`menu_voiceAutoOverlayHoldExpired()` declined to open the overlay because the
raw held mask was already zero.

Consequently, an event backlog can produce this sequence:

1. The press event is consumed and arms the timer.
2. The physical button is released, but its release event is delayed behind
   other edges (or is dropped).
3. The timer expires; the raw-mask check declines the overlay, but ButtonHandler
   still records “timer action occurred.”
4. If the delayed release eventually arrives, it is consumed as a long-press
   release instead of toggling the step.

The sentinel also remains set after `buttonHandler_TimerActionOccured()`
returns true; a later successfully processed SEQ press normally overwrites it,
but a dropped press cannot do so. The silent queue loss and the shared timer
therefore reinforce one another.

There is a second persistent form of the same queue defect in the two
press/release pairing masks. A SEQ press consumed by the MODE-VOICE Scene-mask
overlay or the Load/Save Scene selector sets a per-button bit; only its matching
release clears that bit. If a burst drops the later-scanned lower-button
releases, those lower bits remain set. The next ordinary VOICE release for
each affected button is then swallowed to clear the stale bit, while buttons
whose original releases survived work normally. If the reported “assignment”
included either of those overlays, this is the exact one-tap-afterward failure
mechanism and produces the same upper-working/lower-blocked split.

Changing `BUTTON_HOLD_DELAY_MS` from 100 ms to 200 ms is appropriate and will
reduce false promotion under short stalls, but it is mitigation rather than
the root fix. A release edge that is silently dropped will still never toggle
the step at either threshold.

## What the supplied Pattern trace proves

`SD_CARD_PAT_ASSIGN_BUG/pattrace.bin` contains 5,545 complete eight-byte
records:

| Stage | Count | Meaning |
|---|---:|---|
| `M` | 4,587 | successful Tier 1 relocation |
| `R` | 958 | successful Tier 2 relocation |

It contains **zero** `H`, `Q`, `C`, `F`, `G`, or `X` records: no pending-buffer
overflow, service-queue overflow, capacity drop, fragmentation drop, gap
fallback, or wrong-Scene/service-closed rejection was recorded. Scene 8 owns
5,315 of the 5,545 successful relocation records.

More importantly, an ordinary trigger toggle does not use the stack service at
all. `buttonHandler_setRemoveStep()` directly calls `pat_toggleStep()`, which
XORs bit 15 of a validated address entry and has no allocation or service
admission failure path. If it had been called, the immediate LED readback would
have seen the same modified halfword. The joint absence of both LED and sound
therefore places the failure before that call.

The Pattern trace has no button-edge or event-ring records, so it cannot name
the exact dropped edge in this historical run. It does, however, decisively
exclude the Pattern service failure classes it was designed to record. The
source defect plus the lower-button scan-order symptom identify the
front-panel ring as the targeted cause.

## Recommended correction

Implement this as one input-integrity change, then make the requested timing
change:

1. Replace the head/tail empty-slot ring with monotonic producer/consumer
   counters, as already used by `PatternStackService`, so all 16 existing slots
   are usable and full detection is unambiguous. Prefer 32 entries if the RAM
   allocation is approved; 16 simultaneous SEQ edges leave no room for the
   modifier edge that caused them.
2. Never silently discard an edge. Retain a saturating drop counter and a DEV
   trace containing button number, edge direction, queue depth, and raw held
   mask. If RAM cannot be added, recover by reconciling foreground gesture
   state from the authoritative `btn_held[]` mask after overflow.
3. Drain a small bounded batch per foreground pass (for example 4-8 events),
   stopping between batches for audio rendering. Preserving “one AVR event per
   loop” is not a useful invariant when the STM32 scan publishes up to 40
   edges atomically.
4. Make tap/hold ownership per initiating SEQ button or per held-mask gesture.
   A timer expiry must verify that its initiating button is still physically
   held before setting the release-suppression state. Suppress only releases
   belonging to that completed hold gesture.
5. Increase `BUTTON_HOLD_DELAY_MS` to 200 ms as requested, after the ownership
   fix rather than as its substitute.

## Acceptance test

- In VOICE mode, hold and release all 16 steps together, make a multi-step
  automation edit, then tap SEQ1 through SEQ16 rapidly. Every tap must toggle
  exactly once.
- Repeat while playback, scalar AutoSave, Pattern AutoSave, Pattern
  maintenance, LCD marker repaint, and SD trace flushing are active.
- Repeat in STEP mode with SHIFT and across the 16-bit `time_sysTick` wrap.
- Assert zero event drops. With fault injection forcing a full queue, assert
  that raw-state reconciliation yields no stuck overlay, stale suppression
  bit, or lost final button state.
- Add a diagnostic witness immediately before `pat_toggleStep()` so a future
  report can distinguish input delivery from Pattern mutation without relying
  on LED appearance.

No `Core/` code was changed in this assessment.
