# S076 Part 2 — LFO Scene Reset + Phase Offset Scaling — Implementation Schedule

Reference: `S076_P2_LFO_OPTIONAL_SCENE_RESET.md` (root directory).

This document specifies every code change required to implement:
- The `scn` LFO retrigger option (Scene-change phase reset/continue)
- The LFO phase handoff struct and capture/restore logic
- The track-6 retrigger collision fix
- The `phaseOffset` scaling fix (`IM_SPECIAL_LFO_OFFSET`)

Each change is cited by file, line, and add/modify/remove. Every change carries
a comment block suitable for insertion alongside the code.

13 changes across 9 files. New RAM: +25 bytes SRAM1.

---

## Change 1 — Add `LFO_RETRIGGER_SCENE` define

**File:** `Core/DSPAudio/lfo.h`
**Location:** After the `#define LFO_MAX_F` / `#define LFO_SR` block (after line 62)
**Action:** ADD define

```c
/*
 * LFO retrigger source: Scene change (S076 P2).
 *
 * What:       value 7 in the lfo.retrigger field. When set, a Scene change
 *             resets the LFO phase to phaseOffset using the incoming Scene's
 *             configured offset. When NOT set, the LFO inherits the outgoing
 *             Scene's running phase so the cycle continues uninterrupted.
 * Why:        gives the user per-LFO control over whether a Scene change
 *             interrupts the LFO cycle. This value is outside the voice
 *             trigger range 1..6 and must not match voice retrigger
 *             comparisons.
 * Inputs:     stored as uint8_t in lfo.retrigger via the MENU_RETRIGGER
 *             selector (retriggerNames[] index 7 = "scn").
 * Outputs:    instrumentManager_restoreLfoPhaseIfNeeded() tests this value
 *             to decide reset vs. continue; instrumentManager_retriggerRuntimeLfos()
 *             excludes it from voice trigger matching.
 * Affiliates: retriggerNames[] (MenuText.h),
 *             instrumentManager_retriggerRuntimeLfos() (InstrumentManager.c),
 *             instrumentManager_restoreLfoPhaseIfNeeded() (InstrumentManager.c).
 */
#define LFO_RETRIGGER_SCENE  7u
```

---

## Change 2 — Expand `retriggerNames` menu text

**File:** `Core/Menu/MenuText.h`
**Location:** Line 79–81, replace `retriggerNames` array
**Action:** MODIFY

```c
/* BEFORE (line 79–80) */
static const char retriggerNames[][4] = {
    {7}, {"off"},{"v1"},{"v2"},{"v3"},{"v4"},{"v5"},{"v6"},
};

/* AFTER */
static const char retriggerNames[][4] = {
    {8}, {"off"},{"v1"},{"v2"},{"v3"},{"v4"},{"v5"},{"v6"},{"scn"},
};
```

The first byte is the count of selectable entries. Incrementing it from 7 to 8
extends the MENU_RETRIGGER selector to include the new `"scn"` entry at
index 7, which maps directly to `LFO_RETRIGGER_SCENE`. No descriptor row
change is needed — the existing `TYPE_UINT8` field stores 0..7, and the menu
engine reads the count from `retriggerNames[0][0]`.

---

## Change 3 — Add `IM_SPECIAL_LFO_OFFSET` to the special writer enum

**File:** `Core/DSP/Instruments/InstrumentManager.h`
**Location:** Inside `instrument_special_writer_t` enum, after `IM_SPECIAL_LFO_RATE` (line 143), before `IM_SPECIAL_WRITER_COUNT` (line 144)
**Action:** ADD enum value

```c
/* BEFORE (lines 143–144) */
    IM_SPECIAL_LFO_RATE,
    IM_SPECIAL_WRITER_COUNT

/* AFTER */
    IM_SPECIAL_LFO_RATE,
    IM_SPECIAL_LFO_OFFSET,
    IM_SPECIAL_WRITER_COUNT
```

This inserts `IM_SPECIAL_LFO_OFFSET` as value 19. `IM_SPECIAL_WRITER_COUNT`
becomes 20. The static assert on line 153 verifies `WRITER_COUNT <= 32`
(5-bit mask `0x1Fu`); 20 ≤ 32 passes. The new tag routes `lfo_offset`
descriptor writes through the special handler for proper phase-accumulator
scaling instead of the raw `TYPE_UINT32` path.

---

## Change 4 — Declare LFO phase handoff functions

**File:** `Core/DSP/Instruments/InstrumentManager.h`
**Location:** After the `instrumentManager_clearSlot6Track7StepDecayOverride()` declaration (after line 501)
**Action:** ADD declarations

```c
/*
 * LFO phase Scene-change handoff (S076 P2).
 *
 * What:       captureLfoPhases() snapshots the running phase of every slot's
 *             LFO before the deferred Scene worker starts.
 *             restoreLfoPhaseIfNeeded(slot) checks the incoming LFO's
 *             retrigger field after descriptor values are applied and decides:
 *             scn retrigger → phase = phaseOffset (reset to start);
 *             otherwise → phase = captured snapshot (continue cycle).
 * Why:        instrumentManager_resetRuntimeSlot() unconditionally zeroes the
 *             LFO phase via memset + lfo_init. Without capture/restore, the
 *             running phase is always lost on Scene change.
 * Inputs:     captureLfoPhases() reads from the current runtime LFOs.
 *             restoreLfoPhaseIfNeeded(slot) reads the handoff struct and the
 *             incoming LFO's retrigger field (already written by descriptor
 *             apply).
 * Outputs:    captureLfoPhases() fills the static handoff struct (25 bytes).
 *             restoreLfoPhaseIfNeeded(slot) writes lfo->phase.
 * Callers:    preset_startDrumsetApply() (capture),
 *             preset_resetAndApplyKitVoiceImage() (restore).
 * Affiliates: instrumentManager_resetRuntimeSlot() (the reset that destroys
 *             the phase), presetMorph_applyVoiceNow() (descriptor apply that
 *             writes retrigger before restore is called).
 */
void instrumentManager_captureLfoPhases(void);
void instrumentManager_restoreLfoPhaseIfNeeded(uint8_t slot);
```

---

## Change 5 — Add LFO phase handoff struct (static)

**File:** `Core/DSP/Instruments/InstrumentManager.c`
**Location:** After the `instrumentManager_clearSlot6Track7StepDecayOverride()` function (after line 2566)
**Action:** ADD static struct

```c
/*
 * LFO phase snapshot for Scene-change handoff (S076 P2).
 *
 * What:       captures each slot's running LFO phase before the deferred
 *             Scene worker resets runtime slots. The incoming Scene's LFO
 *             settings determine whether the phase is restored (continue)
 *             or reset to phaseOffset (scn).
 * Why:        the slot reset (memset + lfo_init) zeroes the phase
 *             unconditionally. Without a snapshot, there is nothing to
 *             restore from.
 * Inputs:     instrumentManager_captureLfoPhases() writes all 6 phases from
 *             the current runtime. instrumentManager_restoreLfoPhaseIfNeeded()
 *             reads the captured phase for one slot after descriptor values
 *             have been applied.
 * Output:     one uint32_t phase per slot, plus a valid flag.
 * Lifetime:   static runtime state. Valid from capture until the next capture
 *             or boot. At boot, valid = 0 (BSS zero), so the first Scene
 *             apply uses lfo_init's default phase (0) — same as current
 *             behaviour.
 * RAM:        25 bytes SRAM1.
 * Affiliates: preset_startDrumsetApply() (capture site),
 *             preset_resetAndApplyKitVoiceImage() (restore site),
 *             instrumentManager_resetRuntimeSlot() (the reset that destroys
 *             the phase).
 */
static struct {
    uint32_t phase[INSTRUMENT_SLOT_COUNT];
    uint8_t  valid;
} lfo_scene_handoff;
```

---

## Change 6 — Add `instrumentManager_captureLfoPhases()` function

**File:** `Core/DSP/Instruments/InstrumentManager.c`
**Location:** Immediately after the `lfo_scene_handoff` struct (Change 5)
**Action:** ADD function

```c
/*
 * Snapshot every slot's running LFO phase for Scene-change handoff (S076 P2).
 *
 * What:       iterates all INSTRUMENT_SLOT_COUNT slots and reads the current
 *             runtime LFO phase into the static handoff struct.
 * Why:        must run before the deferred Scene worker calls
 *             instrumentManager_resetRuntimeSlot(), which zeroes the phase
 *             via memset + lfo_init. After capture, the handoff is marked
 *             valid so restoreLfoPhaseIfNeeded() can use it.
 * Inputs:     current runtime LFO state for each slot (may be NULL if slot
 *             type is NONE — captured as 0).
 * Outputs:    lfo_scene_handoff.phase[] filled, lfo_scene_handoff.valid = 1.
 * Callers:    preset_startDrumsetApply() — once per Scene-change worker,
 *             before instrumentManager_clearAllRuntimeModulationTargets().
 * Affiliates: instrumentManager_runtimeLfo() (the per-slot LFO accessor),
 *             instrumentManager_restoreLfoPhaseIfNeeded() (the consumer).
 */
void instrumentManager_captureLfoPhases(void)
{
    uint8_t slot;

    for (slot = 0u; slot < INSTRUMENT_SLOT_COUNT; slot++) {
        Lfo *lfo = instrumentManager_runtimeLfo(slot);
        lfo_scene_handoff.phase[slot] = lfo ? lfo->phase : 0u;
    }
    lfo_scene_handoff.valid = 1u;
}
```

---

## Change 7 — Add `instrumentManager_restoreLfoPhaseIfNeeded()` function

**File:** `Core/DSP/Instruments/InstrumentManager.c`
**Location:** Immediately after `instrumentManager_captureLfoPhases()` (Change 6)
**Action:** ADD function

```c
/*
 * Restore or reset one slot's LFO phase after Scene descriptor apply (S076 P2).
 *
 * What:       reads the incoming LFO's retrigger field (already written by
 *             presetMorph_applyVoiceNow → descriptor path) and decides:
 *             - retrigger == LFO_RETRIGGER_SCENE: phase = phaseOffset
 *               (the incoming Scene requests a fresh start at its offset).
 *             - otherwise: phase = handoff snapshot (the outgoing Scene's
 *               running phase, so the LFO continues its cycle).
 * Why:        instrumentManager_resetRuntimeSlot() zeroed the phase via
 *             memset + lfo_init. The incoming Scene's retrigger value was
 *             not yet written at that point, so the decision is deferred
 *             until after descriptor apply.
 * Inputs:     zero-based slot (0..INSTRUMENT_SLOT_COUNT-1), the static
 *             handoff struct, and the incoming LFO's retrigger field.
 * Outputs:    lfo->phase is set. Out-of-range slot, invalid handoff, or
 *             NULL LFO is a no-op.
 * Callers:    preset_resetAndApplyKitVoiceImage() — after
 *             presetMorph_applyVoiceNow() and before the slot is considered
 *             live.
 * Affiliates: instrumentManager_captureLfoPhases() (the writer),
 *             LFO_RETRIGGER_SCENE (lfo.h), instrumentManager_runtimeLfo().
 */
void instrumentManager_restoreLfoPhaseIfNeeded(uint8_t slot)
{
    Lfo *lfo;

    if (slot >= INSTRUMENT_SLOT_COUNT || !lfo_scene_handoff.valid)
        return;
    lfo = instrumentManager_runtimeLfo(slot);
    if (!lfo)
        return;
    if (lfo->retrigger == LFO_RETRIGGER_SCENE)
        lfo->phase = lfo->phaseOffset;
    else
        lfo->phase = lfo_scene_handoff.phase[slot];
}
```

---

## Change 8 — Fix track-6 retrigger collision

**File:** `Core/DSP/Instruments/InstrumentManager.c`
**Location:** Inside `instrumentManager_retriggerRuntimeLfos()`, lines 1510–1514
**Action:** MODIFY the loop body

```c
/* BEFORE (lines 1510–1514) */
    for (slot = 0u; slot < INSTRUMENT_SLOT_COUNT; slot++) {
        Lfo *lfo = instrumentManager_runtimeLfo(slot);
        if (lfo && lfo->retrigger == (uint8_t)(trigger_track + 1u))
            lfo->phase = lfo->phaseOffset;
    }

/* AFTER */
    for (slot = 0u; slot < INSTRUMENT_SLOT_COUNT; slot++) {
        Lfo *lfo = instrumentManager_runtimeLfo(slot);
        /*
         * Guard voice retrigger range to exclude LFO_RETRIGGER_SCENE (S076 P2).
         *
         * What:       only matches retrigger values 1..INSTRUMENT_SLOT_COUNT
         *             (i.e. 1..6, the voice trigger sources). Values 0 (off)
         *             and 7+ (scn, future) are excluded.
         * Why:        trigger_track 6 is the slot-6 alternate/choke trigger.
         *             trigger_track + 1 == 7 == LFO_RETRIGGER_SCENE. Without
         *             this guard, an LFO with retrigger = scn would falsely
         *             retrigger on every slot-6 alternate trigger.
         * Inputs:     lfo->retrigger (uint8_t), trigger_track (uint8_t).
         * Outputs:    phase reset only for voice retrigger matches.
         * Affiliates: LFO_RETRIGGER_SCENE (lfo.h), lfo_retrigger() (lfo.c).
         */
        if (lfo && lfo->retrigger != 0u &&
            lfo->retrigger <= INSTRUMENT_SLOT_COUNT &&
            lfo->retrigger == (uint8_t)(trigger_track + 1u))
            lfo->phase = lfo->phaseOffset;
    }
```

---

## Change 9 — Add `IM_SPECIAL_LFO_OFFSET` handler in special writer switch

**File:** `Core/DSP/Instruments/InstrumentManager.c`
**Location:** Inside `instrumentManager_applySpecialWriter()`, after the `IM_SPECIAL_LFO_RATE` case (after line 3098), before `case IM_SPECIAL_NONE:`
**Action:** ADD case

```c
    case IM_SPECIAL_LFO_OFFSET: {
        /*
         * Scale 0..127 descriptor byte to full 32-bit phase range (S076 P2).
         *
         * What:       converts the seven-bit offset value into a uint32_t
         *             phaseOffset spanning 0..0xFFFFFFFF, matching the LFO
         *             phase accumulator's full range.
         * Why:        the raw TYPE_UINT32 path writes the byte value directly
         *             (e.g. 127 → 127/4294967295 ≈ 0% of cycle). With
         *             scaling, value 127 maps to 0xFFFFFFFF (≈100% of cycle),
         *             value 64 maps to ≈50%, and value 0 maps to 0.
         * Inputs:     byteValue (0..127) from descriptor/automation write.
         * Outputs:    lfo->phaseOffset set to the scaled uint32_t value.
         * Callers:    instrumentManager_writeRuntime() via the special tag
         *             dispatch. Descriptor rows: lfo_offset in Drum, Snare,
         *             Cymbal, HiHat parameter tables.
         * Affiliates: IM_SPECIAL_LFO_RATE (the analogous rate handler),
         *             lfo.phaseOffset (the target field),
         *             instrumentManager_retriggerRuntimeLfos() and
         *             instrumentManager_restoreLfoPhaseIfNeeded() (consumers
         *             of the scaled phaseOffset).
         */
        Lfo *lfo = instrumentManager_runtimeLfo(slot);
        if (!lfo) return 0u;
        lfo->phaseOffset =
            (uint32_t)(((uint64_t)byteValue * 0xFFFFFFFFu) / 127u);
        return 1u; }
```

---

## Change 10 — Add `lfo_offset` classifier mapping

**File:** `Core/DSP/Instruments/InstrumentManager.c`
**Location:** Inside `instrumentManager_classifySpecialKey()`, after the `"lfo_rate"` entry (line 3166), before `return IM_SPECIAL_NONE;` (line 3167)
**Action:** ADD classifier entry

```c
    /*
     * S076 P2: lfo_offset uses the IM_SPECIAL_LFO_OFFSET handler so
     * the 0..127 byte is scaled to full 32-bit phase range instead of
     * being written raw by the TYPE_UINT32 path.
     */
    if (strcmp(key, "lfo_offset") == 0) return IM_SPECIAL_LFO_OFFSET;
```

The function after modification (lines 3166–3168):

```c
    if (strcmp(key, "lfo_rate") == 0) return IM_SPECIAL_LFO_RATE;
    if (strcmp(key, "lfo_offset") == 0) return IM_SPECIAL_LFO_OFFSET;
    return IM_SPECIAL_NONE;
}
```

---

## Change 11 — Convert `lfo_offset` descriptor rows to `ROW_SPECIAL`

Four files, one change each. Each row changes from `ROW(...)` to
`ROW_SPECIAL(...)` with the `IM_SPECIAL_LFO_OFFSET` tag. The `TYPE_UINT32`
storage type is preserved — the special handler writes the scaled value to
`lfo.phaseOffset` as `uint32_t`.

### Change 11a — DrumParameters.c

**File:** `Core/DSP/Instruments/Drum/DrumParameters.c`
**Location:** Line 223
**Action:** MODIFY

```c
/* BEFORE (line 223) */
    ROW("lfo_offset", "LFO", "Offset", "ofs", DTYPE_0B127, MOD_NONE, lfo.phaseOffset, TYPE_UINT32),

/* AFTER */
    ROW_SPECIAL("lfo_offset", "LFO", "Offset", "ofs", DTYPE_0B127, MOD_NONE, lfo.phaseOffset, TYPE_UINT32, IM_SPECIAL_LFO_OFFSET),
```

### Change 11b — SnareParameters.c

**File:** `Core/DSP/Instruments/Snare/SnareParameters.c`
**Location:** Line 220
**Action:** MODIFY

```c
/* BEFORE (line 220) */
    ROW("lfo_offset", "LFO", "Offset", "ofs", DTYPE_0B127, MOD_NONE, lfo.phaseOffset, TYPE_UINT32),

/* AFTER */
    ROW_SPECIAL("lfo_offset", "LFO", "Offset", "ofs", DTYPE_0B127, MOD_NONE, lfo.phaseOffset, TYPE_UINT32, IM_SPECIAL_LFO_OFFSET),
```

### Change 11c — CymbalParameters.c

**File:** `Core/DSP/Instruments/Cymbal/CymbalParameters.c`
**Location:** Line 222
**Action:** MODIFY

```c
/* BEFORE (line 222) */
    ROW("lfo_offset", "LFO", "Offset", "ofs", DTYPE_0B127, MOD_NONE, lfo.phaseOffset, TYPE_UINT32),

/* AFTER */
    ROW_SPECIAL("lfo_offset", "LFO", "Offset", "ofs", DTYPE_0B127, MOD_NONE, lfo.phaseOffset, TYPE_UINT32, IM_SPECIAL_LFO_OFFSET),
```

### Change 11d — HiHatParameters.c

**File:** `Core/DSP/Instruments/HiHat/HiHatParameters.c`
**Location:** Line 225
**Action:** MODIFY

```c
/* BEFORE (line 225) */
    ROW("lfo_offset", "LFO", "Offset", "ofs", DTYPE_0B127, MOD_NONE, lfo.phaseOffset, TYPE_UINT32),

/* AFTER */
    ROW_SPECIAL("lfo_offset", "LFO", "Offset", "ofs", DTYPE_0B127, MOD_NONE, lfo.phaseOffset, TYPE_UINT32, IM_SPECIAL_LFO_OFFSET),
```

---

## Change 12 — Capture LFO phases before Scene worker teardown

**File:** `Core/Bank/Scene/Preset/presetManager.c`
**Location:** Inside `preset_startDrumsetApply()`, before `instrumentManager_clearAllRuntimeModulationTargets();` (line 1728)
**Action:** ADD call

```c
    /*
     * S076 P2: snapshot each slot's running LFO phase before the deferred
     * Scene worker resets runtime slots (S076 P2).
     *
     * What:       calls instrumentManager_captureLfoPhases() to read the
     *             current phase of each slot's LFO into the static handoff
     *             struct.
     * Why:        instrumentManager_resetRuntimeSlot() will zero the phase
     *             via memset + lfo_init. The snapshot preserves the running
     *             phase for slots whose incoming LFO does not have the scn
     *             retrigger set.
     * Inputs:     current runtime LFO phases (six uint32_t values).
     * Outputs:    lfo_scene_handoff populated and marked valid.
     * Placement:  before instrumentManager_clearAllRuntimeModulationTargets()
     *             because the mod target teardown does not modify LFO phase
     *             but must complete before any slot reset begins. The capture
     *             runs once per Scene worker, not per slot.
     * Affiliates: instrumentManager_restoreLfoPhaseIfNeeded() (the consumer,
     *             called per slot in preset_resetAndApplyKitVoiceImage()),
     *             instrumentManager_resetRuntimeSlot() (the reset that
     *             destroys the phase).
     */
    instrumentManager_captureLfoPhases();
```

The function body after modification (lines 1717–1740):

```c
void preset_startDrumsetApply(void)
{
    /* ... existing comment block ... */
    instrumentManager_captureLfoPhases();
    instrumentManager_clearAllRuntimeModulationTargets();
    preset_ensureMorphInitialized();
    preset_applySceneSettings(scene_getActiveIndex());
    /* The deferred Scene worker uses the same immediate Effect activation. */
    effects_activateScene(scene_getActiveIndex());
    drumset_apply_scene = scene_getActiveIndex();
    drumset_apply_pending_mask =
        (uint16_t)((1u << INSTRUMENT_SLOT_COUNT) - 1u);
    drumset_apply_active = 1u;
    drumset_apply_voice = 0u;
    /* A new Scene worker must not inherit non-progress from its predecessor. */
    drumset_apply_stall_ticks = 0u;
}
```

---

## Change 13 — Restore LFO phase after descriptor apply

**File:** `Core/Bank/Scene/Preset/presetManager.c`
**Location:** Inside `preset_resetAndApplyKitVoiceImage()`, after `presetMorph_applyVoiceNow(scene_index, voice);` (line 1654)
**Action:** ADD call

```c
    /*
     * S076 P2: restore or reset the LFO phase based on the incoming Scene's
     * retrigger setting (S076 P2).
     *
     * What:       calls instrumentManager_restoreLfoPhaseIfNeeded(voice) which
     *             checks the incoming LFO's retrigger field:
     *             - LFO_RETRIGGER_SCENE: phase = phaseOffset (reset to the
     *               incoming Scene's configured start offset).
     *             - Otherwise: phase = captured snapshot from the outgoing
     *               Scene (continue the LFO cycle uninterrupted).
     * Why:        instrumentManager_resetRuntimeSlot() (called earlier in this
     *             function or already complete) zeroed the phase. The
     *             retrigger value was not available at reset time — it was
     *             written by presetMorph_applyVoiceNow() in the line above.
     *             This is the earliest point where both the retrigger setting
     *             and the scaled phaseOffset are in the runtime LFO.
     * Inputs:     zero-based voice slot; the incoming LFO's retrigger and
     *             phaseOffset fields (already written by descriptor apply);
     *             the static handoff struct (written by captureLfoPhases).
     * Outputs:    lfo->phase set to either phaseOffset or captured phase.
     * Guard:      active-Scene check matches the existing
     *             instrumentManager_resetRuntimeSlot() guard on line 1651.
     * Affiliates: instrumentManager_captureLfoPhases() (the snapshot writer,
     *             called in preset_startDrumsetApply()),
     *             LFO_RETRIGGER_SCENE (lfo.h).
     */
    if (scene_index == scene_getActiveIndex())
        instrumentManager_restoreLfoPhaseIfNeeded(voice);
```

The function body after modification (lines 1634–1656):

```c
static void preset_resetAndApplyKitVoiceImage(uint8_t scene_index,
                                              uint8_t voice)
{
    if (!scene_instrumentSlotConst(scene_index, voice) ||
        voice >= INSTRUMENT_SLOT_COUNT) {
        return;
    }

    /* ... existing comment block ... */
    if (scene_index == scene_getActiveIndex())
        instrumentManager_resetRuntimeSlot(voice);
    (void)preset_applyKitAudioRouting(scene_index, voice);
    presetMorph_applyVoiceNow(scene_index, voice);
    if (scene_index == scene_getActiveIndex())
        instrumentManager_restoreLfoPhaseIfNeeded(voice);
}
```

---

## Summary of all changes

| # | File | Line | Action | What |
|---|------|------|--------|------|
| 1 | `Core/DSPAudio/lfo.h` | after 62 | ADD | `#define LFO_RETRIGGER_SCENE 7u` |
| 2 | `Core/Menu/MenuText.h` | 79–80 | MODIFY | `retriggerNames` count 7→8, add `"scn"` |
| 3 | `Core/DSP/Instruments/InstrumentManager.h` | 143–144 | MODIFY | Add `IM_SPECIAL_LFO_OFFSET` to enum (value 19) |
| 4 | `Core/DSP/Instruments/InstrumentManager.h` | after 501 | ADD | Declare `captureLfoPhases()` and `restoreLfoPhaseIfNeeded()` |
| 5 | `Core/DSP/Instruments/InstrumentManager.c` | after 2566 | ADD | `lfo_scene_handoff` static struct (25 bytes) |
| 6 | `Core/DSP/Instruments/InstrumentManager.c` | after 2566 | ADD | `instrumentManager_captureLfoPhases()` function |
| 7 | `Core/DSP/Instruments/InstrumentManager.c` | after 2566 | ADD | `instrumentManager_restoreLfoPhaseIfNeeded()` function |
| 8 | `Core/DSP/Instruments/InstrumentManager.c` | 1510–1514 | MODIFY | Retrigger collision fix: guard `<= INSTRUMENT_SLOT_COUNT` |
| 9 | `Core/DSP/Instruments/InstrumentManager.c` | after 3098 | ADD | `IM_SPECIAL_LFO_OFFSET` case in special writer switch |
| 10 | `Core/DSP/Instruments/InstrumentManager.c` | 3166–3167 | MODIFY | Add `"lfo_offset"` → `IM_SPECIAL_LFO_OFFSET` classifier mapping |
| 11a | `Core/DSP/Instruments/Drum/DrumParameters.c` | 223 | MODIFY | `lfo_offset` → `ROW_SPECIAL` with `IM_SPECIAL_LFO_OFFSET` |
| 11b | `Core/DSP/Instruments/Snare/SnareParameters.c` | 220 | MODIFY | `lfo_offset` → `ROW_SPECIAL` with `IM_SPECIAL_LFO_OFFSET` |
| 11c | `Core/DSP/Instruments/Cymbal/CymbalParameters.c` | 222 | MODIFY | `lfo_offset` → `ROW_SPECIAL` with `IM_SPECIAL_LFO_OFFSET` |
| 11d | `Core/DSP/Instruments/HiHat/HiHatParameters.c` | 225 | MODIFY | `lfo_offset` → `ROW_SPECIAL` with `IM_SPECIAL_LFO_OFFSET` |
| 12 | `Core/Bank/Scene/Preset/presetManager.c` | before 1728 | ADD | `instrumentManager_captureLfoPhases()` call |
| 13 | `Core/Bank/Scene/Preset/presetManager.c` | after 1654 | ADD | `instrumentManager_restoreLfoPhaseIfNeeded(voice)` call |

---

## New RAM

| Item | Size | Region |
|------|------|--------|
| `lfo_scene_handoff` | 25 bytes (6 × uint32_t + 1 × uint8_t) | SRAM1 (BSS) |

No DTCM, arena, or stack impact. The `IM_SPECIAL_LFO_OFFSET` enum value and
classifier string are compile-time/flash. The `#define` is compile-time.

---

## Implementation order

Recommended order to minimise intermediate build errors:

1. Changes 1, 2, 3 (defines and enum — no references yet, compiles clean)
2. Changes 5, 6, 7 (struct and functions — reference Change 1 define, declare
   in step 3 not yet visible outside TU, compiles within InstrumentManager.c)
3. Change 4 (declarations — makes Changes 6/7 visible to presetManager.c)
4. Change 8 (retrigger collision fix — references Change 1 define, compiles)
5. Changes 9, 10 (special writer + classifier — references Change 3 enum)
6. Changes 11a–11d (descriptor rows — reference Change 3 enum tag)
7. Changes 12, 13 (call sites — reference Change 4 declarations)

All 13 changes must be in place before a clean build.

---

## Verification

### Build check

After all changes, build the project and compare the text/data/bss sizes. The
expected change:

- **text:** slight increase (new functions, switch case, classifier entry,
  `#define` is zero-cost)
- **data:** unchanged (no new initialized globals)
- **bss:** +25 bytes (the `lfo_scene_handoff` struct)

### Diagnostic self-check

The `instrumentManager_specialTagSelfCheck()` boot diagnostic (line 3182)
walks all descriptor rows and compares their compiled special tag against the
classifier result. After Changes 9, 10, and 11a–d, the `lfo_offset` rows in
all four instrument types will carry `IM_SPECIAL_LFO_OFFSET`, and the
classifier will return the same tag for key `"lfo_offset"`. The self-check
should pass (mismatch count = 0).

### Functional tests (hardware)

1. **Phase offset scaling:** set LFO offset to 64 (≈50%), retrigger to `v1`,
   trigger voice 1. The LFO should visibly start at the midpoint of its cycle.
   Value 127 should start near the end. Value 0 should start at the beginning.

2. **Scene reset (`scn`):** set Scene A's LFO retrigger to `off`, Scene B's
   to `scn` with offset 0. Switch A→B: LFO resets to 0. Switch B→A: LFO
   continues from wherever it was (inherited phase).

3. **Scene continue:** set both Scenes' LFO retrigger to `off`. Switch
   between them: LFO should continue uninterrupted (the phase is captured
   and restored).

4. **Voice retrigger + Scene change:** set Scene A's LFO retrigger to `v1`.
   Trigger voice 1 — LFO resets. Switch to Scene B (retrigger `off`) — LFO
   continues. Switch back to A — LFO resets to phaseOffset because A has
   `scn`... wait, A has `v1`, not `scn`. Correction: with `v1` set, the
   LFO continues on Scene change (captured phase restored) and resets on
   voice 1 trigger. Correct.

5. **Track-6 collision:** set an LFO's retrigger to `scn`. Trigger the
   slot-6 alternate (track 7). The LFO should NOT retrigger — the collision
   guard prevents it.

6. **Boot behaviour:** power on. First Scene apply should produce default
   phase = 0 for all LFOs (handoff not yet valid, all phases init to 0).
   Same as current behaviour.

---

## Implementation Log

*(To be filled in as changes are applied.)*
