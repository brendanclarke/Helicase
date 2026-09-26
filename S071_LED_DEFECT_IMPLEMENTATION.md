# S071 LED State Defect — Implementation Schedule

**Source**: `S071_LED_STATE_DEFECT.md` remediation plan  
**Branch**: `dev-ph5-effects`  
**Date**: 2026-09-26

---

## Fix 1A — Drain-side chase guard in `led_processSeqLedState()`

### Change 1A-1: Add `#include "sequencer.h"`

**File**: `Core/Hardware/frontPanel/ledHandler.c`  
**Line**: 47 (after last existing `#include`)  
**Action**: ADD

`led_processSeqLedState()` needs `seq_isRunning()` to decide whether a
chase event should install or clear the layer. `seq_isRunning()` is
declared in `sequencer.h` (line 170). No circular dependency:
`sequencer.h` includes only `stm32f4xx.h`, `globals.h`, and
`PatternData.h` — none of which include `ledHandler.h`.

```c
/* after: */
#include "PatternData.h"
#include <string.h>

/* insert: */
#include "sequencer.h"
```

### Change 1A-2: Guard chase drain on transport state

**File**: `Core/Hardware/frontPanel/ledHandler.c`  
**Lines**: 1490–1493  
**Action**: MODIFY

Replace the unconditional chase dispatch with a transport-aware branch.
When `seq_isRunning()` returns nonzero, behaviour is unchanged: the chase
step is forwarded to `led_updateCurrentStep()`. When transport is
stopped, the drain calls `led_clearActive_step()` instead, which removes
the `LED_LAYER_CHASE` bit, restores the base state, and resets
`led_currentStepLed` to `0xFF`. This enforces the mandate: no chase
light when playback is not running.

```c
/* current (lines 1490–1493): */
    /* Chase light: move the temporary current-step LED if the viewed pattern
     * should show playback chase. */
    if (d & SEQ_LED_DIRTY_CHASE)
        led_updateCurrentStep(seq_ledState.chaseStep);

/* proposed: */
    /* Chase light: install or clear the chase layer based on transport state.
     * When transport is running, forward the step to the existing page-aware
     * renderer. When stopped, unconditionally remove any chase inversion —
     * there is no playback position to display. */
    if (d & SEQ_LED_DIRTY_CHASE) {
        if (seq_isRunning())
            led_updateCurrentStep(seq_ledState.chaseStep);
        else
            led_clearActive_step();
    }
```

---

## Fix 1B — Queue chase drain on transport stop

### Change 1B-1: Set `SEQ_LED_DIRTY_CHASE` in the stop branch

**File**: `Core/Sequencer/sequencer.c`  
**Line**: 1293 (after `midiParser_checkMtc();`, inside the `!isRunning` branch)  
**Action**: ADD

When transport stops, the CHASE layer from the last-played step is still
installed on a physical LED but will never be moved or cleared because
no further `SEQ_LED_DIRTY_CHASE` events are queued. This insert marks
the chase as dirty so the foreground drain (Fix 1A-2) reaches
`led_clearActive_step()` on its next pass.

The write must appear after `seq_running = 0u` (line 1281) so that when
the drain reads `seq_isRunning()`, it sees the stopped state.

```c
/* current (lines 1288–1293): */
		voiceControl_noteOff(0xFF);

		trigger_reset(0);
		trigger_allOff();

		midiParser_checkMtc();

/* proposed (insert after line 1293, before the closing brace): */
		voiceControl_noteOff(0xFF);

		trigger_reset(0);
		trigger_allOff();

		midiParser_checkMtc();

		seq_ledState.dirty |= SEQ_LED_DIRTY_CHASE;
```

---

## Fix 2 — Legacy bankset.bcg mask migration

### Change 2-1: Replace legacy single-key expansion with self-only defaults

**File**: `Core/Hardware/SD/storageTypes.c`  
**Lines**: 1230–1231  
**Action**: MODIFY

The legacy path runs when the parser encounters the old single key
`scene_mask_voice_edit=XXXX` (no `_NN` per-Scene suffix). Currently it
copies the old value to all 16 staging entries, which contaminates every
Scene's edit mask with whichever bits the old firmware wrote. The fix
replaces the copied value with the self-only default `(1u << scene_i)`,
matching the `bank_init()` contract for a fresh Bank.

```c
/* current (lines 1230–1231): */
            for (scene_i = 0u; scene_i < BANK_SCENE_SLOT_COUNT; scene_i++)
                state->scene_mask_voice_edit[scene_i] = value16;

/* proposed: */
            for (scene_i = 0u; scene_i < BANK_SCENE_SLOT_COUNT; scene_i++)
                state->scene_mask_voice_edit[scene_i] = (uint16_t)(1u << scene_i);
```

Line 1232 (`state->seen_scene_mask_voice_edit = 0xffffu;`) is unchanged:
all 16 entries are still written and should be marked as seen.

---

## Implementation order

| Step | Fix | File | Lines | Action | Depends on |
|------|-----|------|-------|--------|------------|
| 1 | 1A-1 | `ledHandler.c` | 47 | ADD `#include "sequencer.h"` | — |
| 2 | 1A-2 | `ledHandler.c` | 1490–1493 | MODIFY chase drain | Step 1 |
| 3 | 1B-1 | `sequencer.c` | after 1293 | ADD dirty bit on stop | — |
| 4 | 2-1 | `storageTypes.c` | 1230–1231 | MODIFY legacy expansion | — |

Steps 1–3 are the coordinated chase fix. Step 4 is independent and can
be applied in any order relative to 1–3.

---

## Paths resolved

| Defect path | Fix step | Mechanism |
|-------------|----------|-----------|
| Boot: spurious chase from `seq_realignActivePatternToMasterClock()` | 1A-2 | Drain sees `!seq_isRunning()`, clears instead of installing |
| Transport stop: chase layer persists from last-played step | 1B-1 + 1A-2 | Stop branch queues dirty; drain clears |
| PERF Scene change while stopped: `seq_selectActivePattern()` queues chase | 1A-2 | Drain sees `!seq_isRunning()`, clears instead of installing |
| Legacy bankset.bcg: Scene 0 in all masks | 2-1 | Self-only default replaces broadcast copy |

---

## Verification criteria

1. **Boot**: flash firmware, first boot. Track 0 step 0 should be lit
   (if set). Holding VOICE should NOT show Scene 0 unless it is in the
   active Scene's mask. PERF should show Scene 0 lit (if present with
   active steps).

2. **Transport stop**: start playback, let it run past step 0, stop.
   All 16 STEP/SEQ LEDs should show correct base states — no inverted
   LED at the stopped position.

3. **PERF Scene change while stopped**: with transport stopped, press a
   Scene button in PERF mode. Step LEDs should not show a chase
   inversion on step 0.

4. **Transport start**: start playback. Chase LED should advance
   normally across the step row. Stopping and restarting should leave
   no stuck LED.

5. **Legacy bankset.bcg**: load a Bank from an old bankset.bcg file.
   Hold VOICE on each Scene — only the active Scene's own bit (and any
   user-toggled bits) should appear in the mask.

---

## Work log

- 2026-09-26: Read `MEMORY.md`, the S071 defect assessment, the direct-reason
  note, and this implementation schedule. Confirmed the common root cause is
  a retained `LED_LAYER_CHASE` inversion while `seq_running == 0`; the legacy
  bankset broadcast is a separate migration defect.
- 2026-09-26: Applied Fix 1A. `led_processSeqLedState()` now uses
  `seq_isRunning()` to install chase only during playback and calls
  `led_clearActive_step()` for stopped-transport chase events. Added the
  `sequencer.h` dependency and synchronized the contract comments in
  `ledHandler.c` and `ledHandler.h`.
- 2026-09-26: Applied Fix 1B. `seq_setRunning(0)` now queues
  `SEQ_LED_DIRTY_CHASE` after publishing the stopped state and completing
  stop-side cleanup, with matching source/header comments.
- 2026-09-26: Applied Fix 2. Legacy `scene_mask_voice_edit=XXXX` migration now
  expands to `(1u << scene_i)` self-only defaults instead of copying the old
  single mask to all 16 Scenes. Updated both storageTypes comments to describe
  the corrected contract.
- 2026-09-26: Verification complete for this workspace: `make all -j2`
  passed with the changed objects rebuilt, producing `text=456644`,
  `data=416`, `bss=291900`; `make img` then packaged the resulting
  `build/LXRV2_lxr02.img`. Hardware verification criteria 1–5 remain pending.
