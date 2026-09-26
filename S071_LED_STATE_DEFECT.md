# S071 LED State Defect Assessment

**Branch**: `dev-ph5-effects`  
**Base commit**: `7341d3b`  
**Date**: 2026-09-26  
**Trigger**: Hardware test 1 (per-Scene mask isolation) failed at first boot

---

## Mandate

**When playback is not running, no CHASE light shall be visible.** There is
no playback position to represent, so any `LED_LAYER_CHASE` installed on a
STEP/SEQ LED while `seq_running == 0` is a defect. The foreground drain
must enforce this invariant: a chase dirty event that arrives while
transport is stopped must clear — not install — the chase layer.

---

## Reported symptoms

All three symptoms affect LED\_STEP1 / LED\_SEQ1 (the first step/sequence
LED, logical position 24, physical chain index 32, shift-register byte 4
bit 7). Observed at first boot with S071 firmware applied over an existing
SD card containing old-format autosave and bankset files.

| # | Context | Expected | Observed |
|---|---------|----------|----------|
| 1 | Pattern view, track 0, step 0 set | LED lit | LED unlit; fixed by changing track and returning |
| 2 | VOICE held, Scene 0 not in active Scene's mask | LED off | LED illuminated (active Scene flashes correctly) |
| 3 | PERF mode, Scene 0 present with active steps | LED lit | LED unlit |

---

## Root cause — confirmed: all three symptoms

### Spurious CHASE inversion layer installed at boot

A single `LED_LAYER_CHASE` inversion on LED\_STEP1, installed during
the first foreground drain of sequencer LED state after boot, produces
all three opposite readings. Pattern data, Scene masks, and Scene present
masks are correct; the LED hardware is displaying their complement.

#### Mechanism

1. **Filesystem boot phase** (`filesystem.c:28604`):
   `seq_alignActivePatternToScene(bank_activeSceneSlot())` calls
   `seq_realignActivePatternToMasterClock()` (`sequencer.c:1013`).

2. **Unconditional chase event** (`sequencer.c:1048–1049`):
   `seq_realignActivePatternToMasterClock()` unconditionally writes
   `seq_ledState.chaseStep` and sets `SEQ_LED_DIRTY_CHASE`. It does not
   test `seq_running`. At boot `seq_masterStepClock` is zero (BSS), so
   the queued chase step is 0 even though transport is stopped.

3. **`menu_start()` paints correct base LEDs**: `menu_switchPage(VOICE1_PAGE)`
   calls `led_clearSequencerLeds()` then `led_updatePatternTrackView()`.
   Base states are correct at this point.

4. **First foreground drain** (`ledHandler.c:1492–1493`):
   `led_processSeqLedState()` finds `SEQ_LED_DIRTY_CHASE` set. It calls
   `led_updateCurrentStep(0)`. Because boot is on a VOICE page and the
   viewed and played Scene indices match (both set at `filesystem.c:28605–28606`),
   this calls `led_setActive_step(0)` (`ledHandler.c:1096`), which
   installs `LED_LAYER_CHASE` on LED\_STEP1 and inverts its physical
   output via `led_toggleTemp()`.

5. **Layer persists across view changes**: `led_clearSequencerLeds()`,
   `menu_refreshVoiceHeldSceneLeds()`, and `menu_refreshPerfSceneLeds()`
   rewrite base states and cancel blink layers, but none calls
   `led_clearActive_step()`. Each subsequent `led_setValue()` call invokes
   `led_renderFromStack()` (`ledHandler.c:327–332`), which reapplies the
   CHASE inversion over every new base value.

#### How one inversion produces three symptoms

The CHASE layer reconstructs the base state and toggles it. Whatever the
correct base is, the rendered output is its complement:

| View | Correct base for SEQ index 0 | CHASE-rendered result |
|------|----:|----:|
| Track 0, step 0 active | on | **off** (symptom 1) |
| VOICE-held, Scene 0 absent from edit mask | off | **on** (symptom 2) |
| PERF, Scene 0 present with active steps | on | **off** (symptom 3) |

#### Why "change track and back" clears symptom 1

Changing the active voice triggers a page transition through
`menu_switchPage()`, which repaints the step row. On return to the
original track, if the intervening view change caused a subsequent
`led_processSeqLedState()` drain (e.g. a track change that triggers
`led_updateCurrentStep()` under conditions where `led_clearActive_step()`
fires), the CHASE layer is removed and the base state renders correctly.
The spurious layer is a one-time boot artefact — once cleared, it does
not return unless transport starts and stops on the same step.

---

## Chase persistence investigation

### Can an LED hang or be stuck representing a chase when there is no playback?

**Yes.** Three confirmed paths produce or leave a CHASE layer while
`seq_running == 0`. In each case the layer inverts whichever STEP/SEQ LED
it occupies, and nothing in the current code clears it until playback
restarts.

#### Path 1 — Boot (reported defect)

`filesystem_autosaveBootReaderBlocking()` → `seq_alignActivePatternToScene()`
→ `seq_realignActivePatternToMasterClock()`. Transport has never run, but
`SEQ_LED_DIRTY_CHASE` is set for step 0. The foreground drain installs the
CHASE layer on LED\_STEP1.

#### Path 2 — Transport stop (latent defect)

When `seq_setRunning(0)` is called (`sequencer.c:1277`), `seq_running` is
set to 0 and the scheduler, cursors, and automation are reset. But **no
chase-clearing event is queued**, and `led_clearActive_step()` is never
called. The CHASE layer from the last-played step remains installed.
The inversion persists on that step LED until playback restarts and a new
chase event moves or replaces the layer.

#### Path 3 — PERF Scene change while stopped

`menu_perfModeSceneButtonPressed()` (`menu.c:6269`) calls
`seq_selectActivePattern()` → `seq_realignActivePatternToMasterClock()`.
When transport is stopped, `seq_masterStepClock` is 0 (reset by
`seq_setStepIndexToStart()` during the previous stop), so a chase event
for step 0 is queued. The foreground drain installs the CHASE layer on
LED\_STEP1 even though nothing is playing.

### Chase source and clearing audit

**Sources of `SEQ_LED_DIRTY_CHASE`:**

| Site | Location | `seq_running` guard? | Problem? |
|------|----------|---------------------|----------|
| `seq_realignActivePatternToMasterClock()` | `sequencer.c:1048–1049` | None | Yes — fires at boot, PERF changes while stopped |
| `seq_processSchedulerTick()` | `sequencer.c:1085–1086` | Yes (returns at line 1057 if `!seq_running`) | No |

**Clearing sites:**

| Site | Location | Called from transport stop? |
|------|----------|---------------------------|
| `led_clearActive_step()` | `ledHandler.c:1129` | **No** — only called from `led_updateCurrentStep()`, which only fires through `SEQ_LED_DIRTY_CHASE` drain |
| `led_clearAll()` | `ledHandler.c:506` | No — nuclear reset, never called from stop path |
| `led_init()` | `ledHandler.c:354` | No — boot only |

**There is no code path that clears the CHASE layer when transport stops.**

---

## Generalization

This failure is **not** specific to Scene 0 or to mask data. Cold boot
always selects SEQ index 0 because `seq_masterStepClock` is zero (BSS).
Transport stop leaves the chase on whichever step was last playing.
Any of the 16 STEP/SEQ LEDs can be inverted by a stuck chase. LEDs
outside the shared STEP/SEQ row cannot be affected.

---

## Secondary finding: legacy `bankset.bcg` mask migration

### (Separate from the LED state defect, still requires fix)

**File**: `Core/Hardware/SD/storageTypes.c` lines 1229–1232.

When the parser encounters the old single key `scene_mask_voice_edit=XXXX`
(no `_NN` suffix), the value is copied to **all 16** staging entries:

```c
for (scene_i = 0u; scene_i < BANK_SCENE_SLOT_COUNT; scene_i++)
    state->scene_mask_voice_edit[scene_i] = value16;
state->seen_scene_mask_voice_edit = 0xffffu;
```

The old firmware's default was `scene_mask_voice_edit=0001` (only Scene 0's
bit). After the legacy expansion every Scene starts with `0x0001`.
When any Scene *i* ≠ 0 becomes active, `bank_ensureActiveInVoiceEditMask()`
adds bit *i*, producing `mask[i] = 0x0001 | (1u << i)`. Scene 0 is then
in that Scene's edit mask even though the user never toggled it.

This bug does **not** cause the LED state defect observed at boot (the
CHASE inversion does), but it would cause incorrect VOICE-held LED states
after the CHASE layer is cleared — Scene 0 would appear lit in every
Scene's edit mask when an old `bankset.bcg` is loaded.

---

## Remediation plan

### Fix 1 — Drain-side chase guard (primary, required)

Enforce the mandate at the single point where chase events are
consumed. Two coordinated changes:

**A. `ledHandler.c`, `led_processSeqLedState()`** — when transport is
stopped, clear the chase instead of installing it:

```c
/* Current: */
if (d & SEQ_LED_DIRTY_CHASE)
    led_updateCurrentStep(seq_ledState.chaseStep);

/* Proposed: */
if (d & SEQ_LED_DIRTY_CHASE) {
    if (seq_running)
        led_updateCurrentStep(seq_ledState.chaseStep);
    else
        led_clearActive_step();
}
```

**B. `sequencer.c`, `seq_setRunning()`** — queue a chase drain on
transport stop so the foreground clears the layer:

```c
/* In the stop branch, after seq_running = 0u: */
seq_ledState.dirty |= SEQ_LED_DIRTY_CHASE;
```

**Why drain-side, not source-side**: guarding the write in
`seq_realignActivePatternToMasterClock()` with `if (seq_running)` would
fix paths 1 and 3, but NOT path 2 (transport stop leaves an already-
installed chase). The drain-side guard is a single policy — "if transport
is not running, clear the chase" — that covers all three paths:

| Path | How it reaches the drain | Drain action |
|------|-------------------------|--------------|
| Boot | `seq_realignActivePatternToMasterClock()` sets dirty | `seq_running == 0` → clear |
| Transport stop | `seq_setRunning(0)` sets dirty (new) | `seq_running == 0` → clear |
| PERF while stopped | `seq_selectActivePattern()` → `seq_realignActivePatternToMasterClock()` sets dirty | `seq_running == 0` → clear |

### Fix 2 — Legacy bankset.bcg mask migration (secondary, required)

**File**: `Core/Hardware/SD/storageTypes.c`, `storage_banksetParseLine()`.

Change the legacy single-key path to set each Scene to its self-only
default instead of copying the old value:

```c
/* Current (buggy): */
for (scene_i = 0u; scene_i < BANK_SCENE_SLOT_COUNT; scene_i++)
    state->scene_mask_voice_edit[scene_i] = value16;

/* Proposed (correct): */
for (scene_i = 0u; scene_i < BANK_SCENE_SLOT_COUNT; scene_i++)
    state->scene_mask_voice_edit[scene_i] = (uint16_t)(1u << scene_i);
```

### Fix 3 — Defensive autosave format guard (optional)

**File**: `Core/Bank/Scene/Autosave.c`, `autosave_applyBankPayload()`.

Add a width sentinel or version field to the Bank section so the reader
can distinguish old 2-byte mask records from new 32-byte records. When an
old record is detected, default all per-Scene masks to `(1u << i)` instead
of reading potentially-non-zero padding bytes. This is optional because
normal old files have zero padding, but becomes required if non-zero
padding is found in production files.

---

## SRAM impact

No additional SRAM is required. Fix 1 adds a branch guard and one dirty-
bit write. Fix 2 changes the value written to the existing staging array.

## Files affected by remediation

| File | Change |
|------|--------|
| `Core/Hardware/frontPanel/ledHandler.c` | Drain-side `seq_running` guard in `led_processSeqLedState()` |
| `Core/Sequencer/sequencer.c` | Queue `SEQ_LED_DIRTY_CHASE` in `seq_setRunning()` stop branch |
| `Core/Hardware/SD/storageTypes.c` | Legacy mask expansion → self-only defaults |
| `Core/Bank/Scene/Autosave.c` | (Optional) Old-format detection and default masks |

---

## Implementation assessment — 2026-09-26

All four scheduled changes have been applied and verified against the
implementation schedule (`S071_LED_DEFECT_IMPLEMENTATION.md`).

### Fix 1A — Drain-side chase guard

- **1A-1** (`ledHandler.c:54`): `#include "sequencer.h"` added after
  a comment block explaining the dependency. No circular include.
- **1A-2** (`ledHandler.c:1499–1507`): `led_processSeqLedState()` now
  branches on `seq_isRunning()`. Running transport forwards to
  `led_updateCurrentStep()` (unchanged behaviour). Stopped transport
  calls `led_clearActive_step()`, removing any retained CHASE inversion.
  Comment updated to describe the transport-aware contract.

### Fix 1B — Chase drain on transport stop

- **1B-1** (`sequencer.c:1295–1300`): `seq_ledState.dirty |= SEQ_LED_DIRTY_CHASE`
  added in the `!isRunning` branch after `midiParser_checkMtc()`. Appears
  after `seq_running = 0u` (line 1281), so the drain reads the stopped
  state. Comment block documents the purpose. Header comment for
  `seq_setRunning()` in `sequencer.h` (lines 165–166) updated to
  describe the new stop-side dirty write.

### Fix 2 — Legacy bankset.bcg mask migration

- **2-1** (`storageTypes.c:1236–1237`): Legacy single-key expansion now
  writes `(uint16_t)(1u << scene_i)` instead of `value16`. Comment block
  added explaining the migration rationale. The `seen_scene_mask_voice_edit = 0xffffu`
  assignment is unchanged — all 16 entries are written and marked seen.

### Defect paths covered

| Path | Fix | Status |
|------|-----|--------|
| Boot: spurious chase from `seq_realignActivePatternToMasterClock()` | 1A-2 | Covered — drain clears when `!seq_isRunning()` |
| Transport stop: last-played chase persists | 1B-1 + 1A-2 | Covered — stop queues dirty; drain clears |
| PERF Scene change while stopped | 1A-2 | Covered — drain clears when `!seq_isRunning()` |
| Legacy bankset.bcg: Scene 0 broadcast to all masks | 2-1 | Covered — self-only defaults |

### Outstanding

- **Fix 3** (defensive autosave format guard) not applied. Remains
  optional pending hardware test with known-clean autosave files.
- **Hardware verification** required: boot LED states, transport
  stop/start chase, PERF Scene change while stopped, legacy bankset.bcg
  mask isolation. See verification criteria in implementation schedule.
