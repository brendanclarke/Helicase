# S070 Phase 4 — Automation Missed on Transport Restart: Implementation Schedule

Session: S070 · Branch: `dev-ph5-effects` · 2026-09-24.
Source analysis: `S070_PHASE4_AUTOMATION_MISSED.md`.

---

## Summary of verified code state

All line numbers verified against the current `dev-ph5-effects` branch at commit
`13e45f9`.

| Symbol                              | File                                   | Line(s) | Kind           |
|-------------------------------------|----------------------------------------|---------|----------------|
| `seq_automation_dirty[]`            | `Core/Sequencer/sequencer.c`           | 177     | static array   |
| `seq_clearAutomationDirty()`        | `Core/Sequencer/sequencer.c`           | 186–192 | static fn      |
| `seq_restoreAutomatedParameters()`  | `Core/Sequencer/sequencer.c`           | 772–801 | public fn      |
| `seq_setStepIndexToStart()`         | `Core/Sequencer/sequencer.c`           | 1444–1464 | static fn    |
| `seq_setRunning()`                  | `Core/Sequencer/sequencer.c`           | 1079–1117 | public fn    |
| `seq_init()`                        | `Core/Sequencer/sequencer.c`           | 201–211 | public fn      |
| `seq_handleMasterBoundary()`        | `Core/Sequencer/sequencer.c`           | 803–834 | static fn      |
| `seq_resetToPatternStart()`         | `Core/Sequencer/sequencer.c`           | 1028–1039 | public fn    |
| `seq_drainPendingAutomation()`      | `Core/Sequencer/sequencer.c`           | 697–759 | public fn      |
| `voiceControl_triggerNow()`         | `Core/MIDI/MidiVoiceControl.c`         | 151–172 | static fn      |
| `voiceControl_processPending()`     | `Core/MIDI/MidiVoiceControl.c`         | 247–253 | public fn      |
| Foreground drain loop               | `main.c`                               | 192–193 | audio render    |
| `scene_instrumentSlotConst()`       | `Core/Bank/Scene/SceneData.h`          | 297     | public fn decl  |
| `instrumentManager_writeRuntime()`  | `Core/DSP/Instruments/InstrumentManager.h` | 508 | public fn decl  |
| `instrumentManager_descriptor()`    | `Core/DSP/Instruments/InstrumentManager.h` | 228 | public fn decl  |
| `morph_interpolation[]`             | `Core/Bank/Scene/SceneData.h`          | 48      | struct member   |
| `INSTRUMENT_SLOT_COUNT`             | `Core/DSP/Instruments/InstrumentManager.h` | 18  | `#define` = 6   |

### Call sites for `seq_setStepIndexToStart()`

| Caller                           | File             | Line | Context                              |
|----------------------------------|------------------|------|--------------------------------------|
| `seq_setRunning()` (common tail) | `sequencer.c`    | 1115 | Transport start and stop             |
| `seq_handleMasterBoundary()`     | `sequencer.c`    | 823  | Bar-boundary pattern change          |
| `seq_resetToPatternStart()`      | `sequencer.c`    | 1035 | External MIDI/sync reset             |

All three callers benefit from fix 4A: pattern changes should not carry stale
automation from the outgoing pattern, and external resets should behave the same
as stop/start.

### Call sites for `seq_clearAutomationDirty()`

| Caller                           | File             | Line | Context                              |
|----------------------------------|------------------|------|--------------------------------------|
| `seq_init()`                     | `sequencer.c`    | 210  | Boot — no restore needed             |
| `seq_setRunning()` stop branch   | `sequencer.c`    | 1089 | Redundant: also cleared at 1458      |
| `seq_setStepIndexToStart()`      | `sequencer.c`    | 1458 | Authoritative grid-reset clear       |

---

## Change list

Three behavioral changes in `Core/Sequencer/sequencer.c`, plus the adjacent
public API contract comment in `Core/Sequencer/sequencer.h`. No new RAM. No
filesystem, SD, or PatternData involvement.

---

### Change 1 of 3 — Add `seq_restoreAllAutomation()` (Fix 4A)

**Operation:** ADD new static function.
**File:** `Core/Sequencer/sequencer.c`
**Location:** After `seq_clearAutomationDirty()` (after line 192), before the
existing forward declarations (line 194).

#### Code to add (insert after line 192)

```c
/*
 * Restore every dirty automation overlay to its morph-interpolated base value.
 *
 * What: walks all six instrument slots and, for each set bit in
 * seq_automation_dirty[slot], writes the corresponding morph_interpolation[]
 * value back into the voice's runtime image through
 * instrumentManager_writeRuntime(). This reverses any transient parameter
 * overlays that seq_drainPendingAutomation() applied during the previous
 * playback pass.
 *
 * Why: seq_clearAutomationDirty() zeros the tracking bitmaps but does not
 * touch the voice runtime. If the bitmaps are cleared without restoring first,
 * the next trigger's seq_restoreAutomatedParameters() finds a zero bitmap and
 * skips the restore, leaving the voice at whatever the last automation wrote.
 * This function closes that gap: it performs the same restore-from-base that
 * seq_restoreAutomatedParameters() does for one track, extended to all six
 * instrument slots in one pass.
 *
 * Inputs: implicit — reads seq_automation_dirty[] (the six per-slot 64-bit
 * dirty bitmaps) and the active Scene's instrument slot images. Each set bit
 * identifies a descriptor-local parameter index whose runtime value was
 * overwritten by automation and must be returned to its morph_interpolation[]
 * base before the bitmap is cleared.
 *
 * Outputs: every parameter marked dirty in any slot has its runtime image
 * restored to the morph_interpolation[] value. The dirty bitmaps themselves
 * are NOT cleared here — the caller (seq_setStepIndexToStart) calls
 * seq_clearAutomationDirty() immediately after, which zeroes the bitmaps.
 * Splitting restore from clear keeps each function single-purpose and allows
 * seq_init() to continue calling seq_clearAutomationDirty() alone at boot
 * when no instruments are loaded and nothing needs restoring.
 *
 * Common callers: seq_setStepIndexToStart() only. That function is itself
 * called from three sites:
 *   - seq_setRunning() line 1115 (transport start/stop)
 *   - seq_handleMasterBoundary() line 823 (bar-boundary pattern change)
 *   - seq_resetToPatternStart() line 1035 (external MIDI/sync reset)
 *
 * Affiliate code:
 *   - seq_restoreAutomatedParameters() (sequencer.c:772) — the per-track
 *     version of the same restore logic, called from MidiVoiceControl.c's
 *     trigger funnel. This function reuses its pattern: ctzll walk of mask
 *     bits, descriptor lookup, writeRuntime from morph_interpolation[].
 *   - seq_drainPendingAutomation() (sequencer.c:697) — the foreground
 *     consumer that writes automation values into the voice runtime and sets
 *     the dirty bits this function restores.
 *   - instrumentManager_writeRuntime() (InstrumentManager.c) — the runtime
 *     writer that applies a descriptor-domain value to the DSP owner.
 *   - instrumentManager_descriptor() (InstrumentManager.c) — resolves a
 *     descriptor-local index to its ParamDescriptor for the slot's type.
 *   - scene_instrumentSlotConst() (SceneData.c) — reads the active Scene's
 *     kit_instrument_slot_t for a given slot, providing both the instrument
 *     type (for descriptor lookup) and the parameter_images (for the
 *     morph_interpolation[] source values).
 *   - INSTRUMENT_SLOT_COUNT (InstrumentManager.h:18) — always 6.
 */
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
```

#### Verification

- Zero new static RAM: the function uses only stack locals (`slot`, `mask`,
  `local`, pointers).
- `seq_activePattern` is already used by `seq_restoreAutomatedParameters()` on
  line 786 in the same file; the new function reuses the same pattern.
- The `__builtin_ctzll` / `mask &= (mask - 1ULL)` bit-walk is identical to the
  accepted pattern at lines 789–797.
- NULL instrument or NULL descriptor guards match the existing guards at
  lines 787/793.
- If all bitmaps are zero (the common case when no automation was active), the
  function returns after six zero-checks with no writeRuntime calls.

---

### Change 2 of 3 — Call `seq_restoreAllAutomation()` from `seq_setStepIndexToStart()` (Fix 4A)

**Operation:** MODIFY existing function.
**File:** `Core/Sequencer/sequencer.c`
**Location:** `seq_setStepIndexToStart()`, line 1444–1464.

#### Current code (lines 1444–1464)

```c
static void seq_setStepIndexToStart()
{
	/*
	 * Reset every runtime cursor to the fixed-grid start position.
	 *
	 * Input: implicit active Scene/transport state. Output: each track is one
	 * position before step zero, so the immediate scheduler boundary plays step
	 * zero. There is no PatternData rotation, length, or event-count affiliate.
	 */
	uint8_t i;
	/*
	 * Fixed-grid restart also drops overlays from the prior step/context; this
	 * covers both transport restart and active Scene/Pattern realignment.
	 */
	seq_clearAutomationDirty();
	for(i=0;i<NUM_TRACKS;i++) {
		seq_lastMasterStep[i] = 0u;
		seq_stepIndex[i] = -1;
	}

}
```

#### New code (replaces lines 1444–1464)

```c
static void seq_setStepIndexToStart()
{
	/*
	 * Reset every runtime cursor to the fixed-grid start position.
	 *
	 * Input: implicit active Scene/transport state. Output: each track is one
	 * position before step zero, so the immediate scheduler boundary plays step
	 * zero. There is no PatternData rotation, length, or event-count affiliate.
	 *
	 * Why: transport stop/start, bar-boundary pattern changes, and external
	 * resets all converge here. The restore-then-clear sequence ensures that
	 * every voice parameter returns to its morph_interpolation[] base before
	 * the dirty bitmaps are zeroed, so the next trigger's
	 * seq_restoreAutomatedParameters() does not find a zero bitmap with stale
	 * runtime values still in the voice. Without the restore step, automation
	 * gestures at the beginning of a pattern are missed approximately 50% of
	 * the time on transport restart (depends on where the user stopped).
	 *
	 * Inputs: seq_automation_dirty[] bitmaps and the active Scene's instrument
	 * slot images (read by seq_restoreAllAutomation). Outputs: all dirty voice
	 * parameters restored to morph_interpolation[] base, bitmaps zeroed, step
	 * indexes set to -1 (one before step zero).
	 *
	 * Common callers:
	 *   - seq_setRunning() line 1115 (transport start/stop)
	 *   - seq_handleMasterBoundary() line 823 (bar-boundary pattern change)
	 *   - seq_resetToPatternStart() line 1035 (external MIDI/sync reset)
	 *
	 * Affiliate code:
	 *   - seq_restoreAllAutomation() — restores every dirty slot's runtime
	 *     parameters from morph_interpolation[]. Must be called before
	 *     seq_clearAutomationDirty() so the bitmap is still populated when the
	 *     restore reads it.
	 *   - seq_clearAutomationDirty() — zeros all six slot bitmaps. Called after
	 *     restore so the next playback pass starts with clean tracking state.
	 *   - seq_init() — calls seq_clearAutomationDirty() directly at boot without
	 *     the restore step, because no instruments are loaded and the voice
	 *     runtime is uninitialized at that point.
	 */
	uint8_t i;

	seq_restoreAllAutomation();
	seq_clearAutomationDirty();
	for(i=0;i<NUM_TRACKS;i++) {
		seq_lastMasterStep[i] = 0u;
		seq_stepIndex[i] = -1;
	}

}
```

#### What changed

| Line area     | Before                          | After                                    |
|---------------|---------------------------------|------------------------------------------|
| Comment block | Shorter, no restore mention     | Documents the restore-then-clear sequence, the bug it fixes, inputs/outputs, callers, and affiliates |
| Before clear  | (nothing)                       | `seq_restoreAllAutomation();` call added |
| Clear call    | `seq_clearAutomationDirty();`   | Unchanged — still present, now runs after restore |
| Step resets   | Unchanged                       | Unchanged                                |

#### Verification

- `seq_restoreAllAutomation()` reads `seq_automation_dirty[]` before
  `seq_clearAutomationDirty()` zeroes it. Order is critical.
- `seq_init()` at line 201 is unaffected: it calls `seq_clearAutomationDirty()`
  directly, not `seq_setStepIndexToStart()`, so it skips the restore. This is
  correct because at boot no instruments are loaded and
  `scene_instrumentSlotConst()` returns NULL for unpopulated slots.
- All three callers of `seq_setStepIndexToStart()` now get the restore for free:
  transport stop/start, pattern boundary, and external reset.

---

### Change 3 of 3 — Restructure `seq_setRunning()` (Fixes 4B and 4C)

**Operation:** MODIFY existing function.
**File:** `Core/Sequencer/sequencer.c`
**Location:** `seq_setRunning()`, lines 1079–1117.

#### Current code (lines 1079–1117)

```c
void seq_setRunning(uint8_t isRunning)
{
	seq_running = isRunning;
	//jump to 1st step if sequencer is stopped
	if(!seq_running)
	{
		/*
		 * Transport stop discards transient automation overlays before any later
		 * preview or restart can reuse the runtime voice objects.
		 */
		seq_clearAutomationDirty();

		//reset song position bar counter
		seq_barCounter = 0;
		seq_resetStepScheduler();
		//so the next seq_tick call will trigger the next step immediately
		seq_deltaT = 0;
		seq_sendRealtime(MIDI_STOP);

		//--AS send notes off on all channels that have notes playing and reset our bitmap to reflect that
		voiceControl_noteOff(0xFF);

		trigger_reset(0);
		trigger_allOff();


		// --AS if mtc was doing it's thing, tell it to stop it.
		midiParser_checkMtc();
	} else {
		seq_resetStepScheduler();
		seq_sendRealtime(MIDI_START);
		trigger_reset(1);
	}

	// set start points back to default (happens on start and stop. needs to happen on start
	// in case the user has entered a rotate value while stopped)
	seq_setStepIndexToStart();

}
```

#### New code (replaces lines 1079–1117)

```c
/*
 * Start or stop the sequencer transport.
 *
 * What: the single entry point for transport state transitions. Handles both
 * the start path (isRunning = 1) and the stop path (isRunning = 0), with
 * seq_setStepIndexToStart() shared by both as the common tail.
 *
 * Why the assignment of seq_running is branch-specific instead of a single
 * assignment at the top:
 *
 *   Race condition: TIM3 (priority 2) can preempt the foreground at any
 *   instruction boundary. seq_processSchedulerTick() (sequencer.c:883)
 *   checks seq_running at its entry gate (line 888). If seq_running is set
 *   to 1 before seq_resetStepScheduler() and seq_setStepIndexToStart()
 *   complete, TIM3 can fire the initial scheduler tick prematurely:
 *
 *   1. TIM3 sees seq_running = 1 and seq_initialSchedulerTick = 1 (left
 *      over from the prior stop path). It processes the initial tick: step 0
 *      advances, trigger and automation are queued.
 *   2. ISR returns. Foreground continues: seq_resetStepScheduler() sets
 *      seq_initialSchedulerTick = 1 again; seq_setStepIndexToStart() resets
 *      step indexes to -1 and clears dirty bitmaps.
 *   3. Next scheduler tick sees seq_initialSchedulerTick = 1 again, fires
 *      step 0 a second time. The second trigger's restore finds the first
 *      automation's dirty bits and restores them, undoing step 0's automation.
 *
 *   Probability: ~0.06% per restart at 120 BPM (3 us race window / 5.2 ms
 *   tick period), but increases with tempo and widens if MIDI/USB driver
 *   latency extends the window.
 *
 *   Fix: for the stop branch, seq_running = 0 is set FIRST so TIM3 sees the
 *   transport as stopped immediately. For the start branch, seq_running = 1
 *   is set LAST, after seq_setStepIndexToStart() has initialized cursors,
 *   restored and cleared dirty bitmaps, and prepared clean state. The first
 *   TIM3 tick after seq_running = 1 will find fully initialized state.
 *
 * Inputs: isRunning — 1 to start, 0 to stop.
 *
 * Outputs:
 *   Stop: transport halted, all notes off, MIDI stop sent, step indexes
 *     reset to -1, automation overlays restored and bitmaps cleared.
 *   Start: scheduler reset, MIDI start sent, step indexes reset to -1,
 *     automation overlays restored and bitmaps cleared, then seq_running = 1
 *     enables the TIM3 scheduler to begin processing ticks.
 *
 * Common callers: buttonHandler.c (play/stop button), midiParser.c (external
 * MIDI start/stop), som.c (song-of-machine mode transitions).
 *
 * Affiliate code:
 *   - seq_processSchedulerTick() (sequencer.c:883) — the TIM3 ISR consumer
 *     that gates on seq_running. The race this restructuring closes is between
 *     the assignment of seq_running and the completion of state initialization.
 *   - seq_setStepIndexToStart() (sequencer.c:1444) — shared by both branches;
 *     with fix 4A it now calls seq_restoreAllAutomation() before clearing
 *     dirty bitmaps.
 *   - seq_resetStepScheduler() (sequencer.c:361) — resets PPQ/master clocks
 *     and sets seq_initialSchedulerTick = 1.
 *   - voiceControl_noteOff() — sends note-off on all active channels.
 *   - seq_sendRealtime() — sends MIDI realtime start/stop bytes.
 *   - trigger_reset() / trigger_allOff() — resets hardware trigger outputs.
 *   - midiParser_checkMtc() — stops MTC if active.
 *
 * Fix 4C — redundant seq_clearAutomationDirty() removed: the stop branch
 * previously called seq_clearAutomationDirty() at its own line 1089 before
 * seq_setStepIndexToStart() called it again at line 1458. With fix 4A in
 * place (restore-before-clear inside seq_setStepIndexToStart), the early
 * call was not just redundant but harmful: it would zero the bitmaps before
 * seq_restoreAllAutomation() could read them, defeating the restore. Removing
 * it ensures the single authoritative clear in seq_setStepIndexToStart()
 * always runs after the restore.
 */
void seq_setRunning(uint8_t isRunning)
{
	if (!isRunning)
	{
		seq_running = 0u;

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
		seq_running = 1u;
}
```

#### What changed — line by line

| Area                          | Before                                   | After                                          | Why                                      |
|-------------------------------|------------------------------------------|-------------------------------------------------|------------------------------------------|
| Comment block                 | None (bare function)                     | Full block documenting the race, inputs/outputs, callers, affiliates, and fix 4C rationale | Implementation documentation requirement |
| Line 1081                     | `seq_running = isRunning;`               | REMOVED — replaced by branch-specific assignments | Fix 4B: close the TIM3 preemption race   |
| Stop branch entry             | `if(!seq_running)` (after assignment)    | `if (!isRunning)` (tests parameter directly)    | Parameter is the authority, not the just-assigned static |
| Stop: `seq_running`           | Set at top (line 1081) via `isRunning`   | `seq_running = 0u;` at top of stop branch       | Fix 4B: TIM3 sees stopped state immediately |
| Stop: `seq_clearAutomationDirty()` | Called at line 1089                 | REMOVED                                         | Fix 4C: redundant; seq_setStepIndexToStart handles it after restore |
| Start: `seq_running`          | Set at top (line 1081) via `isRunning`   | `seq_running = 1u;` AFTER `seq_setStepIndexToStart()` | Fix 4B: TIM3 cannot fire initial tick until state is ready |
| Common tail                   | `seq_setStepIndexToStart();`             | Unchanged                                       | Still the shared grid-reset entry point  |

#### Verification

- **Stop path:** `seq_running = 0u` is the first statement in the stop branch.
  TIM3 sees `seq_running == 0` immediately and returns from
  `seq_processSchedulerTick()` at line 888. All subsequent stop operations
  run without risk of a premature scheduler tick.

- **Start path:** `seq_resetStepScheduler()` sets `seq_initialSchedulerTick = 1`
  and resets PPQ clocks. `seq_setStepIndexToStart()` restores automation,
  clears bitmaps, and resets step indexes. Only then does `seq_running = 1u`
  allow TIM3 to enter the scheduler. The first tick finds
  `seq_initialSchedulerTick == 1` and a clean, fully initialized state.

- **Fix 4C safety:** The removed `seq_clearAutomationDirty()` at old line 1089
  was always followed by `seq_setStepIndexToStart()` at line 1115 which calls
  `seq_clearAutomationDirty()` internally (line 1458). The removed call was
  pure redundancy. With fix 4A, removing it is also necessary: if the early
  clear ran first, `seq_restoreAllAutomation()` inside `seq_setStepIndexToStart()`
  would find zeroed bitmaps and restore nothing.

- **MIDI realtime ordering:** `seq_sendRealtime(MIDI_STOP)` and
  `seq_sendRealtime(MIDI_START)` remain inside their respective branches
  and execute before `seq_setStepIndexToStart()`. MIDI stop fires while the
  transport is already flagged stopped. MIDI start fires before the transport
  is flagged running, which is correct: external devices receive the start
  byte while the sequencer is still initializing, and the first clock pulse
  follows after `seq_running = 1u`.

- **`trigger_reset()` / `trigger_allOff()` ordering:** Both remain inside
  the stop branch and execute after `seq_running = 0u`. No change in
  observable behavior.

---

## Files changed

| File | Changes |
|------|---------|
| `Core/Sequencer/sequencer.c` | 3 behavioral changes plus adjacent implementation blocks |
| `Core/Sequencer/sequencer.h` | Adjacent `seq_setRunning()` API contract block; no signature change |

`MEMORY.md` and this implementation record carry the session notes. No new
public API, RAM, filesystem, SD, or PatternData format changes.

---

## Detailed change map

```
Core/Sequencer/sequencer.c
│
├── Change 1: ADD seq_restoreAllAutomation()
│   Location: after line 192 (after seq_clearAutomationDirty)
│   Operation: insert new static function
│   Dependencies: seq_automation_dirty[], seq_activePattern,
│                 scene_instrumentSlotConst(), instrumentManager_descriptor(),
│                 instrumentManager_writeRuntime(), morph_interpolation[]
│   New RAM: 0 bytes (stack-only locals)
│
├── Change 2: MODIFY seq_setStepIndexToStart()
│   Location: lines 1444–1464
│   Operation: add seq_restoreAllAutomation() call before seq_clearAutomationDirty()
│              and expand the comment block
│   Dependencies: Change 1 must exist first
│   New RAM: 0 bytes
│
└── Change 3: MODIFY seq_setRunning()
    Location: lines 1079–1117
    Operation: restructure seq_running assignment to branch-specific positions,
               remove redundant seq_clearAutomationDirty() from stop branch,
               add documentation comment block
    Dependencies: Change 2 must exist first (fix 4C depends on fix 4A)
    New RAM: 0 bytes
```

---

## What is NOT changed

- **`sequencer.h`** — `seq_restoreAllAutomation()` is `static` and needs no
  declaration. `seq_restoreAutomatedParameters()` signature at line 155 is
  unchanged. The `seq_setRunning()` signature is also unchanged; its adjacent
  contract block now documents restore-before-clear and publication ordering.

- **`MidiVoiceControl.c`** — The trigger funnel at line 168 continues to call
  `seq_restoreAutomatedParameters()` for per-track restore on each trigger.
  That function's behavior is unchanged: it still reads the dirty bitmap for
  one slot, restores from `morph_interpolation[]`, and clears that slot.

- **`main.c`** — The foreground drain loop at lines 192–193 is unchanged.
  `voiceControl_processPending()` still processes the trigger ring, and
  `seq_drainPendingAutomation()` still writes automation values into voice
  runtime and sets dirty bits.

- **`InstrumentManager.c`** / **`InstrumentManager.h`** — No API or behavior
  changes. `instrumentManager_writeRuntime()` and `instrumentManager_descriptor()`
  are called from the new function exactly as they are from the existing
  `seq_restoreAutomatedParameters()`.

- **`SceneData.c`** / **`SceneData.h`** — No changes. `scene_instrumentSlotConst()`
  is called from the new function the same way it is from the existing code.

- **`seq_init()`** (line 201) — Unchanged. It calls `seq_clearAutomationDirty()`
  directly at line 210, not `seq_setStepIndexToStart()`, so it does not execute
  the restore. This is correct: at boot no instruments are loaded, no voice
  runtime is initialized, and `scene_instrumentSlotConst()` returns NULL for
  unpopulated slots.

- **No SD, filesystem, or PatternData format changes.**

- **No new static RAM allocations.**

---

## Implementation order

1. **Change 1** — Add `seq_restoreAllAutomation()`. Compile-check: the function
   is `static` and uncalled at this point, so the compiler may warn about an
   unused function. This is expected and resolved by Change 2.

2. **Change 2** — Modify `seq_setStepIndexToStart()` to call the new function.
   Compile-check: the unused-function warning from Change 1 disappears. The
   restore-before-clear sequence is now active for all three callers.

3. **Change 3** — Restructure `seq_setRunning()`. This is safe to apply after
   Change 2 because fix 4C (removing the redundant `seq_clearAutomationDirty()`)
   depends on fix 4A being in place inside `seq_setStepIndexToStart()`.

All three changes can be committed together as a single atomic commit since they
form one logical fix.

---

## Test plan (from S070_PHASE4_AUTOMATION_MISSED.md §6)

After applying all three changes:

1. Load Scene 6 (or any Scene with automation at step 0 and later steps).
2. Play for several bars. Stop playback during a bar where automation has been
   applied (after step 0's automation targets have been set and additional
   automation has fired on later steps).
3. Restart. Verify that step 0's automation gesture is audible and correct.
4. Repeat 10 times. Step 0 automation should fire all 10 times.
5. Additionally verify: pattern boundary change (bar end with pending pattern)
   and external MIDI reset both produce correct step 0 automation on re-entry.

---

## Implementation notes — 2026-09-24

### Source changes applied

- Added static `seq_restoreAllAutomation()` immediately after
  `seq_clearAutomationDirty()` in `Core/Sequencer/sequencer.c`. It walks each
  dirty descriptor bit for all six instrument slots and writes the active
  Scene's `morph_interpolation[]` value through the existing descriptor/runtime
  writer. The dirty bitmaps remain intact until the caller clears them.
- Updated `seq_setStepIndexToStart()` so restore runs before
  `seq_clearAutomationDirty()`. Its adjacent block now documents the shared
  transport, Pattern-boundary, and external-reset callers and the required
  restore-before-clear ordering.
- Restructured `seq_setRunning()` so stop publishes `seq_running = 0` before
  teardown, start publishes `seq_running = 1` only after the common grid reset,
  and the old redundant stop-branch clear is removed. The function now carries
  an adjacent block documenting the TIM3 preemption race and the fix rationale.
- Added the corresponding public transport contract block beside
  `seq_setRunning()` in `Core/Sequencer/sequencer.h`, keeping the declaration
  and implementation behavior documented together.

### Checks

- `git diff --check` is clean for the edited source/header and implementation
  note files.
- Source inspection confirms `seq_init()` still clears directly at boot, while
  all three `seq_setStepIndexToStart()` callers now restore before clearing.
- The documented firmware build could not be executed in this environment:
  PowerShell reports that both `make` and `arm-none-eabi-gcc` are unavailable.
  Hardware restart, Pattern-boundary, and external-reset validation therefore
  remain pending until the firmware toolchain/fixture is available.
