# S070 Phase 4 — Automation missed on transport restart

Session: S070 · Branch: `dev-ph5-effects` · 2026-09-24.

Observation: automation at the beginning of a pattern is missed approximately
50% of the time on transport stop/restart. Scene 6 was in use.

---

## 1. Root cause

`seq_clearAutomationDirty()` (`sequencer.c:186`) zeros every
`seq_automation_dirty[slot]` bitmap but does not restore the voice runtime
parameters to their morph_interpolation base values.

During playback, `seq_drainPendingAutomation()` writes automation values
directly into the voice's runtime image through
`instrumentManager_writeRuntime()` and marks the corresponding bit in
`seq_automation_dirty[slot]`. When the next trigger fires,
`seq_restoreAutomatedParameters()` reads that dirty bitmap, restores each
marked parameter from `morph_interpolation[]`, and clears the slot. This is
the retrigger-restore cycle that keeps automation transient: each trigger
starts from the base value, then the new step's automation overwrites
selected parameters.

At transport stop, `seq_setRunning(0)` calls `seq_setStepIndexToStart()`
(`sequencer.c:1115`), which calls `seq_clearAutomationDirty()`. This zeros
the tracking bitmap but leaves the voice runtime holding whatever the last
automation wrote. Parameters that were at, say, cutoff = 127 from step 12's
automation remain at 127 even though the bitmap says "nothing is dirty."

On transport restart, the first step 0 trigger calls
`seq_restoreAutomatedParameters()`. The bitmap is zero → no restore. The
voice starts with the stale automation values from the previous run, not its
morph_interpolation base values. Step 0's own automation then applies, but
only modifies the targets it owns. Any parameter that was automated earlier
in the bar but not at step 0 stays at its previous-run value.

### Why the effect is "missed"

The user hears the pattern's characteristic automation gesture — a sweep, a
morph change, a level jump — because it depends on the parameter going from
base to automated. On restart, the starting point is not the base but
whatever the previous run's last automation left behind. The gesture is
absent or different:

- Normal loop: step 15 automation → step 0 restore to base → step 0
  automation. Sweep from base to target.
- Stop-restart: step 15 automation → stop → dirty cleared without restore →
  start → step 0 trigger (no restore, stale values) → step 0 automation.
  No sweep because the starting point is already near or above the target.

### Why approximately 50%

It depends on where the user stops. If they stop during a step that has
automation overlays active (e.g. steps 8–15 in a pattern where step 8
automates filter cutoff), the stale values persist. If they stop during a
step where the parameters happen to be at their base values (e.g. steps 0–7
before any automation fires), the restart is clean. With automation spread
across roughly half the steps, the failure rate is roughly 50%.

---

## 2. Code path

### Transport stop

```
seq_setRunning(0)                           sequencer.c:1079
  seq_clearAutomationDirty()                sequencer.c:1089  ← bitmap zeroed
  seq_resetStepScheduler()                  sequencer.c:1093
  seq_deltaT = 0                            sequencer.c:1095
  voiceControl_noteOff(0xFF)                sequencer.c:1099
  seq_setStepIndexToStart()                 sequencer.c:1115
    seq_clearAutomationDirty()              sequencer.c:1458  ← zeroed again
    seq_stepIndex[i] = -1                   sequencer.c:1461
```

After this, `seq_automation_dirty[]` is all-zero but voice runtime
parameters still hold the values written by the last
`instrumentManager_writeRuntime()` during playback.

### Transport start

```
seq_setRunning(1)                           sequencer.c:1079
  seq_running = 1                           sequencer.c:1081
  seq_resetStepScheduler()                  sequencer.c:1108
    seq_initialSchedulerTick = 1            sequencer.c:373
    seq_elapsedPpqTicks = 0                 sequencer.c:370
  seq_sendRealtime(MIDI_START)              sequencer.c:1109
  seq_setStepIndexToStart()                 sequencer.c:1115
    seq_clearAutomationDirty()              sequencer.c:1458
    seq_stepIndex[i] = -1                   sequencer.c:1461
```

### First tick (TIM3 ISR → seq_processSchedulerTick)

```
seq_processSchedulerTick()                  sequencer.c:883
  seq_initialSchedulerTick → 0              sequencer.c:892
  seq_masterStepClock = 0                   sequencer.c:893
  seq_handleMasterBoundary()                sequencer.c:895   → returns 0
  seq_advanceTrackStep(track)               sequencer.c:911
    seq_stepIndex[track]++  (-1 → 0)        sequencer.c:585
    pat_readStepSpecials(…, 0)              sequencer.c:601
    seq_evaluateStepCondition()             sequencer.c:603
    seq_triggerVoice(track, …)              sequencer.c:617   → queues trigger
    seq_queueStepAutomations(track, 0)      sequencer.c:628   → queues automation
```

### Foreground drain (audio render loop, main.c:186)

```
voiceControl_processPending()               main.c:192
  voiceControl_triggerNow()                 MidiVoiceControl.c:252
    seq_restoreAutomatedParameters(voice)   MidiVoiceControl.c:168
      mask = seq_automation_dirty[slot]     sequencer.c:782
        → 0  → NO RESTORE                  ← THE BUG
    instrumentManager_triggerTrack()        MidiVoiceControl.c:169
      voice starts with stale parameters

seq_drainPendingAutomation()                main.c:193
  instrumentManager_writeRuntime(…, value)  sequencer.c:732
    step 0's automation targets are set
  seq_automation_dirty[slot] |= bit         sequencer.c:734
    only step 0's targets are now marked dirty
```

Parameters automated by earlier steps but not by step 0 remain at their
stale values from the previous run. They are neither restored (bitmap was
zero at trigger time) nor overwritten (step 0 does not target them).

---

## 3. Secondary issue: seq_setRunning race

`seq_running = 1` is assigned at `sequencer.c:1081` before
`seq_resetStepScheduler()` and `seq_setStepIndexToStart()` complete. TIM3
(priority 2) can preempt the foreground at any instruction boundary after
that assignment.

If TIM3 fires between `seq_running = 1` and `seq_setStepIndexToStart()`:

1. `seq_processSchedulerTick()` sees `seq_running = 1` and
   `seq_initialSchedulerTick = 1` (left over from the stop path's
   `seq_resetStepScheduler()`). It processes the initial tick: step 0 is
   advanced, trigger and automation are queued.
2. ISR returns. The foreground continues: `seq_resetStepScheduler()` sets
   `seq_initialSchedulerTick = 1` again; `seq_setStepIndexToStart()` resets
   `seq_stepIndex[]` to -1 and clears dirty.
3. The next scheduler tick sees `seq_initialSchedulerTick = 1`, processes
   the initial tick again: step 0 fires a second time.

The second trigger's `seq_restoreAutomatedParameters()` finds the dirty bits
that the first automation drain set and restores those parameters to their
base values. If no new automation entries are available in the same sub-frame
(the queue was already drained), the parameters stay at restored values and
the voice plays without step 0's automation.

The UART and USB MIDI sends are non-blocking (FIFO push,
`Uart.c:220`/`usb_manager.c:177`), so the race window is approximately 3 µs
between `seq_running = 1` and the end of `seq_setStepIndexToStart()`. With
the scheduler tick period of ~5.2 ms at 120 BPM, the probability per restart
is about 0.06%. This is too low to explain the observed 50% rate, but the
race is real and should be fixed.

---

## 4. Proposed fix

### 4A — Restore before clearing (primary)

Add a function that walks every instrument slot's dirty bitmap and restores
from `morph_interpolation[]`, then call it from `seq_setStepIndexToStart()`
before `seq_clearAutomationDirty()`. This is the same operation that
`seq_restoreAutomatedParameters()` performs for one track, extended to all
slots.

```c
static void seq_restoreAllAutomation(void)
{
    uint8_t slot;

    for (slot = 0u; slot < INSTRUMENT_SLOT_COUNT; slot++) {
        uint64_t mask = seq_automation_dirty[slot];
        const kit_instrument_slot_t *instrument;

        if (!mask)
            continue;
        instrument = scene_instrumentSlotConst(seq_activePattern, slot);
        if (!instrument)
            continue;
        while (mask) {
            uint8_t local = (uint8_t)__builtin_ctzll(mask);
            const ParamDescriptor *descriptor =
                instrumentManager_descriptor(instrument->type, local);
            if (descriptor)
                (void)instrumentManager_writeRuntime(
                    slot, descriptor,
                    instrument->parameter_images.morph_interpolation[local]);
            mask &= (mask - 1ULL);
        }
    }
}

static void seq_setStepIndexToStart()
{
    uint8_t i;
    seq_restoreAllAutomation();        /* ← restore BEFORE clearing */
    seq_clearAutomationDirty();
    for (i = 0u; i < NUM_TRACKS; i++) {
        seq_lastMasterStep[i] = 0u;
        seq_stepIndex[i] = -1;
    }
}
```

At boot (`seq_init()`), no instruments are loaded. `seq_clearAutomationDirty()`
is called directly there, bypassing the restore. That call remains unchanged
because there is nothing to restore: the voice runtime is uninitialized, and
`scene_instrumentSlotConst()` returns NULL for unpopulated slots.

`seq_setStepIndexToStart()` is also called from `seq_handleMasterBoundary()`
(bar-boundary pattern change, `sequencer.c:823`) and
`seq_resetToPatternStart()` (external reset, `sequencer.c:1035`). Both
benefit from the restore: a pattern change should not carry stale automation
values from the old pattern, and an external reset should behave the same as
a stop/start.

### 4B — Close the seq_setRunning race (secondary)

Move `seq_running = isRunning` after all state initialization so TIM3 cannot
fire the initial tick prematurely:

```c
void seq_setRunning(uint8_t isRunning)
{
    if (!isRunning)
    {
        seq_running = 0u;
        seq_clearAutomationDirty();
        seq_barCounter = 0;
        seq_resetStepScheduler();
        seq_deltaT = 0;
        seq_sendRealtime(MIDI_STOP);
        voiceControl_noteOff(0xFF);
        trigger_reset(0);
        trigger_allOff();
        midiParser_checkMtc();
    } else {
        seq_resetStepScheduler();
        seq_sendRealtime(MIDI_START);
        trigger_reset(1);
    }
    seq_setStepIndexToStart();
    if (isRunning)
        seq_running = 1u;              /* ← set AFTER state is ready */
}
```

For the stop branch, `seq_running = 0` is moved to the top of the branch
so TIM3 sees the transport as stopped immediately. For the start branch,
`seq_running = 1` is deferred until after `seq_setStepIndexToStart()` has
initialized cursors, cleared dirty bitmaps, and (with fix 4A) restored
parameters. The first TIM3 scheduler tick after `seq_running = 1` will find
clean state.

### 4C — Remove redundant clear (cleanup)

The stop branch at `sequencer.c:1089` calls `seq_clearAutomationDirty()`
before `seq_setStepIndexToStart()` calls it again at line 1115/1458. With
fix 4A in place, the first call is redundant. Remove it to avoid confusion
about which clear is authoritative.

---

## 5. Impact

Fix 4A resolves the primary symptom: after stop/restart, every voice
parameter starts from its morph_interpolation base, and step 0's automation
gesture plays correctly.

Fix 4B closes a low-probability race that can cause a double step-0 trigger
with the second trigger undoing the first's automation. The probability is
~0.06% at 120 BPM but increases with higher tempos and grows to ~1% if
MIDI routing or USB driver latency widens the race window.

Both fixes are sequencer-internal. No SD, no filesystem, no PatternData
format change.

---

## 6. Test

After applying fixes 4A and 4B:

1. Load Scene 6 (or any Scene with automation at step 0 and later steps).
2. Play for several bars. Stop playback during a bar where automation has
   been applied (i.e., after step 0's automation targets have been set and
   additional automation has fired on later steps).
3. Restart. Verify that step 0's automation gesture is audible and correct.
4. Repeat 10 times. Step 0 automation should fire all 10 times.

Evidence: the T1 test's trace can additionally verify that automation
entries appear in the pending queue on every first step after restart.

---

## 7. Implementation and result

Fixes 4A, 4B, and 4C applied in commit `2f5b3d2` on branch `dev-ph5-effects`.
Full change map, code listings, and verification notes in
`S070_PHASE4_AUTOMATION_MISSED_IMPLEMENTATION.md`.

| Fix | Change | File | Lines |
|-----|--------|------|-------|
| 4A  | Added `seq_restoreAllAutomation()` — walks all six slots' dirty bitmaps and restores from `morph_interpolation[]` before the clear | `sequencer.c` | 221–245 |
| 4A  | `seq_setStepIndexToStart()` calls `seq_restoreAllAutomation()` before `seq_clearAutomationDirty()` | `sequencer.c` | 1536–1537 |
| 4B  | Stop branch publishes `seq_running = 0` first; start branch defers `seq_running = 1` until after `seq_setStepIndexToStart()` completes | `sequencer.c` | 1161–1188 |
| 4C  | Removed redundant `seq_clearAutomationDirty()` from stop branch — the authoritative clear now lives only inside `seq_setStepIndexToStart()` | `sequencer.c` | (removed from old line 1089) |
| —   | Contract block for `seq_setRunning()` in header | `sequencer.h` | 156–168 |

**T3 result — PASS.** Step 0 automation fires correctly on every transport
restart. No audible double trigger. See `S070_PHASE4_TESTING_FINAL.md` T3 row.
