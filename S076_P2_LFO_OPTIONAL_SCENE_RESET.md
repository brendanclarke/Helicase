# S076 Part 2 — LFO Optional Scene Reset

## 1. Problem Statement

When the active Scene changes, each instrument slot is eventually reset via
`instrumentManager_resetRuntimeSlot()` → `memset` + `lfo_init()`. This zeroes
the LFO phase regardless of user intent. The reset is deferred (bounded
worker or trigger-time force-apply), so the exact moment of phase loss depends
on envelope quieting and trigger timing, but every slot eventually loses its
running LFO phase.

**Requested behaviour:** the user should be able to choose, per LFO, whether
a Scene change resets the LFO phase. A new `scn` option in the existing
retrigger selector (`rtg`) controls this. When `scn` is set on the *incoming*
Scene's LFO, the phase resets to `phaseOffset`. When `scn` is NOT set, the
LFO inherits the phase from the outgoing Scene so it continues its cycle
uninterrupted.

## 2. Current State

### LFO struct (`Core/DSPAudio/lfo.h:64`)

```c
typedef struct LfoStruct {
    uint32_t    phase;          // full 32-bit accumulator
    uint32_t    phaseInc;
    uint8_t     waveform;
    uint8_t     retrigger;      // 0=off, 1..6=voice retrigger source
    uint32_t    phaseOffset;    // target phase on retrigger
    float       rnd;
    uint8_t     sync;
    float       freq;
    ModulationNode modTarget;
    ModulationNode modTarget2;
    uint8_t     polarity;
    float       modNodeValue;
} Lfo;
```

### Retrigger selector (`Core/Menu/MenuText.h:79`)

```c
static const char retriggerNames[][4] = {
    {7}, {"off"},{"v1"},{"v2"},{"v3"},{"v4"},{"v5"},{"v6"},
};
```

Values 0..6 stored in `lfo.retrigger`. The retrigger dispatcher at
`InstrumentManager.c:1510` compares `lfo->retrigger == (trigger_track + 1u)`.

### Scene-change slot reset (`InstrumentManager.c:1419`)

`instrumentManager_resetRuntimeSlot()` does `memset(&runtime_slots[slot], 0, ...)`
then calls the engine's `initVoice()` which calls `lfo_init()`. This
unconditionally zeroes `phase` to 0. The new Scene's descriptor values
(including `phaseOffset`, `retrigger`, `freq`, etc.) are then written by
`presetMorph_applyVoiceNow()` through `instrumentManager_writeRuntime()`.

### FxBuffer handoff (`Core/DSP/Effects/FxBuffer.h:153`)

The `fxbuf_handoff_t` struct (180 bytes in SRAM1) carries arena state between
Effect type activations. It is Effect/arena-specific: read/write offsets,
unit ownership, rate/channel metadata. It is NOT a general Scene transition
struct and should not be repurposed for LFO state.

### Phase offset scaling (pre-existing issue)

The descriptor row `lfo_offset` stores a 0..127 value as `TYPE_UINT32` into
`lfo.phaseOffset`. This value is written raw:
`*((uint32_t *)parameter.ptr) = (uint32_t)value;`
(InstrumentManager.c:1774). The phase accumulator runs 0..0xFFFFFFFF, so
a phaseOffset of 127 is 127/4294967295 ≈ 0.000003% of the cycle — effectively
zero. **This is a pre-existing bug and should be fixed in this session, but
it is a separate change not gated on the Scene reset feature.** See section 7.

## 3. Design

### 3.1 Retrigger selector expansion

Add `scn` as value 7 in the retrigger selector. The `lfo.retrigger` field
already stores 0..6 in a `uint8_t`; value 7 means "reset phase on Scene
change."

The `scn` value is **independent** of voice retrigger values 1..6. It is not
a compound: an LFO is either retriggered by a voice, or retriggered by a
Scene change, or not retriggered at all. This matches the existing
single-value selector model. If compound retrigger (voice AND Scene) is
wanted later, the retrigger field can be widened to a bitmask, but that is
out of scope.

**Decision needed:** should `scn` be exclusive with voice retrigger, or
should it be possible to have both voice retrigger AND Scene retrigger?

- **Option A (exclusive, recommended):** `scn` is value 7. The selector shows
  `off`, `v1`..`v6`, `scn`. Only one is active. Simple, no storage change.
- **Option B (compound):** a separate 1-bit flag `lfo.scene_retrigger`
  alongside the existing voice retrigger. Either or both can be active.
  Requires +1 byte per LFO in the runtime struct and a new descriptor row.

Recommendation: **Option A** for now. A user who wants both can set the LFO
retrigger to `v1` and rely on the sequencer pattern to provide a trigger on
the first step after a Scene change. Compound mode can be added later if
requested.

### 3.2 LFO phase handoff struct

The FxBuffer handoff is Effect-specific and should not be extended. Instead,
introduce a small per-slot LFO phase snapshot that captures the outgoing
Scene's running phase before the slot reset.

```c
typedef struct {
    uint32_t phase[INSTRUMENT_SLOT_COUNT];
    uint8_t  valid;
} lfo_scene_handoff_t;
```

This is 25 bytes of SRAM1 (6 × 4 + 1). It is static in InstrumentManager.c,
written once at the start of `preset_startDrumsetApply()` before any slot
reset, and read during the slot reset path when the incoming LFO does not
have `scn` retrigger set.

**Why per-slot, not per-LFO-pair:** each instrument slot has exactly one LFO
oscillator (with two destination pairs sharing that oscillator). The phase
is per oscillator, not per destination.

### 3.3 Scene-change phase restore

During `instrumentManager_resetRuntimeSlot()`, after `lfo_init()` zeroes the
phase, the restore is applied. But the incoming Scene's `retrigger` value
has not been written yet at that point — it arrives later via
`presetMorph_applyVoiceNow()` → `instrumentManager_writeRuntime()`. So the
restore decision cannot be made during `resetRuntimeSlot()`.

Instead, the restore happens **after** descriptor values are applied:

1. `preset_startDrumsetApply()` captures all 6 LFO phases into the handoff
   before calling `instrumentManager_clearAllRuntimeModulationTargets()`.
2. `instrumentManager_resetRuntimeSlot()` runs normally (`memset` + `init`).
3. `presetMorph_applyVoiceNow()` writes all descriptor values including
   `lfo.retrigger`, `lfo.phaseOffset`, `lfo.freq`, etc.
4. A new call, `instrumentManager_restoreLfoPhaseIfNeeded(slot)`, checks the
   incoming LFO's `retrigger` field:
   - If `retrigger == LFO_RETRIGGER_SCENE` (7): set `phase = phaseOffset`
     (the incoming Scene's configured start phase).
   - Otherwise: restore `phase` from the handoff snapshot (the outgoing
     Scene's running phase), so the LFO continues uninterrupted.
5. This call is made from `preset_resetAndApplyKitVoiceImage()` after
   `presetMorph_applyVoiceNow()`.

### 3.4 Retrigger dispatcher — exclude `scn` from voice triggers

The retrigger dispatcher at `InstrumentManager.c:1510` compares
`lfo->retrigger == (trigger_track + 1u)`. Since `scn` (value 7) is outside
the voice range 1..6, the comparison naturally fails — no voice trigger will
match value 7. No change needed to the dispatcher.

However, `trigger_track` 6 is the alternate/choke trigger for slot 6
(`trigger_track + 1 = 7`). This would collide with `LFO_RETRIGGER_SCENE = 7`.
The guard `if (trigger_track > INSTRUMENT_SLOT_COUNT) return;` at line 1508
accepts track 6 (6 > 6 is false). So track 6 with `retrigger == 7` would
cause a false retrigger match.

**Fix required:** change the retrigger comparison to exclude `scn`:

```c
if (lfo && lfo->retrigger != 0u &&
    lfo->retrigger != LFO_RETRIGGER_SCENE &&
    lfo->retrigger == (uint8_t)(trigger_track + 1u))
    lfo->phase = lfo->phaseOffset;
```

Or equivalently, guard with `lfo->retrigger <= INSTRUMENT_SLOT_COUNT` before
the comparison.

### 3.5 Boot and transport

At boot, all LFO phases start at 0 (from `memset`/`lfo_init`). The handoff
is invalid (`valid = 0`). The first `preset_startDrumsetApply()` captures
whatever is in the runtime (all zero at boot). If the incoming Scene has
`scn` set, the phase goes to `phaseOffset` — correct. If not, the phase goes
to the captured 0 — also correct (same as current behaviour).

Transport stop/start does not call `preset_startDrumsetApply()` (it calls
`seq_setStepIndexToStart()` which does automation restore, not slot reset).
LFO phase is currently unaffected by transport stop/start unless a voice
retrigger fires. This is unchanged.

## 4. Menu and Storage

### Descriptor row (all four instrument types)

`ROW_MENU("lfo_retrigger_voice", ...)` already uses `MENU_RETRIGGER` with
values 0..6. Adding `scn` as value 7:

**MenuText.h:79** — add `"scn"` and increment count:
```c
static const char retriggerNames[][4] = {
    {8}, {"off"},{"v1"},{"v2"},{"v3"},{"v4"},{"v5"},{"v6"},{"scn"},
};
```

No descriptor row change is needed — the existing `TYPE_UINT8` field can
store 0..7. The max value clamp in the descriptor row uses `MENU_RETRIGGER`
which reads the count from `retriggerNames[0][0]`. Changing the count from 7
to 8 automatically extends the selector range.

### `.kit` file storage

The retrigger field is stored in kit files through the instrument descriptor
system. Value 7 will be saved and loaded correctly by the existing
`instrumentManager_writeParameter()` / read path because `TYPE_UINT8`
handles any byte value. Older firmware loading a file with value 7 will
either clamp it to the old max (6, if clamped) or store it and have the
comparison `retrigger == 7` never match a trigger track — harmless in either
case.

## 5. Implementation Changes

| # | File | Action | What |
|---|------|--------|------|
| 1 | `Core/Menu/MenuText.h` | MODIFY line 79 | Add `"scn"` to `retriggerNames`, count 7→8 |
| 2 | `Core/DSPAudio/lfo.h` | ADD | `#define LFO_RETRIGGER_SCENE 7u` |
| 3 | `Core/DSP/Instruments/InstrumentManager.c` | ADD struct + functions | `lfo_scene_handoff_t` static, `instrumentManager_captureLfoPhases()`, `instrumentManager_restoreLfoPhaseIfNeeded(slot)` |
| 4 | `Core/DSP/Instruments/InstrumentManager.c:1510` | MODIFY | Exclude `LFO_RETRIGGER_SCENE` from voice retrigger comparison (track 6 collision) |
| 5 | `Core/DSP/Instruments/InstrumentManager.h` | ADD | Declare the two new functions |
| 6 | `Core/Bank/Scene/Preset/presetManager.c` | MODIFY `preset_startDrumsetApply()` ~line 1728 | Call `instrumentManager_captureLfoPhases()` before modulation teardown |
| 7 | `Core/Bank/Scene/Preset/presetManager.c` | MODIFY `preset_resetAndApplyKitVoiceImage()` ~line 1654 | Call `instrumentManager_restoreLfoPhaseIfNeeded(voice)` after `presetMorph_applyVoiceNow()` |

### New RAM: +25 bytes SRAM1

`lfo_scene_handoff_t`: 6 × 4 (phase) + 1 (valid) = 25 bytes. Static in
InstrumentManager.c. No DTCM or arena impact.

## 6. Detailed Changes

### Change 1 — MenuText.h retrigger names

```c
/* BEFORE */
static const char retriggerNames[][4] = {
    {7}, {"off"},{"v1"},{"v2"},{"v3"},{"v4"},{"v5"},{"v6"},
};

/* AFTER */
static const char retriggerNames[][4] = {
    {8}, {"off"},{"v1"},{"v2"},{"v3"},{"v4"},{"v5"},{"v6"},{"scn"},
};
```

### Change 2 — lfo.h define

After the existing `#define LFO_MAX_F` block:

```c
/*
 * LFO retrigger source: Scene change (S076 P2).
 *
 * What: value 7 in the lfo.retrigger field. When set, a Scene change resets
 * the LFO phase to phaseOffset using the incoming Scene's configured offset.
 * When NOT set, the LFO inherits the outgoing Scene's running phase so the
 * cycle continues uninterrupted. This value is outside the voice trigger
 * range 1..6 and must not match voice retrigger comparisons.
 * Affiliates: retriggerNames[] (MenuText.h), instrumentManager_retriggerRuntimeLfos(),
 * instrumentManager_restoreLfoPhaseIfNeeded().
 */
#define LFO_RETRIGGER_SCENE  7u
```

### Change 3 — InstrumentManager.c: handoff struct and functions

After the existing `slot6_track7_decay_step_*` statics:

```c
/*
 * LFO phase snapshot for Scene-change handoff (S076 P2).
 *
 * What: captures each slot's running LFO phase before the deferred Scene
 * worker resets runtime slots. The incoming Scene's LFO settings determine
 * whether the phase is restored (continue) or reset to phaseOffset (scn).
 * Why: the slot reset (memset + lfo_init) zeroes the phase unconditionally.
 * Without a snapshot, there is nothing to restore from.
 * Inputs: instrumentManager_captureLfoPhases() writes all 6 phases from the
 * current runtime. instrumentManager_restoreLfoPhaseIfNeeded(slot) reads the
 * captured phase for one slot after descriptor values have been applied.
 * Output: one uint32_t phase per slot, plus a valid flag.
 * Lifetime: static runtime state. Valid from capture until the next capture
 * or boot. At boot, valid = 0 (BSS zero), so the first Scene apply uses
 * lfo_init's default phase (0) — same as current behaviour.
 * RAM: 25 bytes SRAM1.
 * Affiliates: preset_startDrumsetApply() (capture site),
 * preset_resetAndApplyKitVoiceImage() (restore site),
 * instrumentManager_resetRuntimeSlot() (the reset that destroys the phase).
 */
static struct {
    uint32_t phase[INSTRUMENT_SLOT_COUNT];
    uint8_t  valid;
} lfo_scene_handoff;

void instrumentManager_captureLfoPhases(void)
{
    uint8_t slot;

    for (slot = 0u; slot < INSTRUMENT_SLOT_COUNT; slot++) {
        Lfo *lfo = instrumentManager_runtimeLfo(slot);
        lfo_scene_handoff.phase[slot] = lfo ? lfo->phase : 0u;
    }
    lfo_scene_handoff.valid = 1u;
}

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

### Change 4 — InstrumentManager.c: retrigger collision fix

```c
/* BEFORE (line ~1510) */
    for (slot = 0u; slot < INSTRUMENT_SLOT_COUNT; slot++) {
        Lfo *lfo = instrumentManager_runtimeLfo(slot);
        if (lfo && lfo->retrigger == (uint8_t)(trigger_track + 1u))
            lfo->phase = lfo->phaseOffset;
    }

/* AFTER */
    for (slot = 0u; slot < INSTRUMENT_SLOT_COUNT; slot++) {
        Lfo *lfo = instrumentManager_runtimeLfo(slot);
        if (lfo && lfo->retrigger != 0u &&
            lfo->retrigger <= INSTRUMENT_SLOT_COUNT &&
            lfo->retrigger == (uint8_t)(trigger_track + 1u))
            lfo->phase = lfo->phaseOffset;
    }
```

The `<= INSTRUMENT_SLOT_COUNT` guard (i.e. `<= 6`) ensures that
`LFO_RETRIGGER_SCENE` (7) and any future non-voice values never match a
voice trigger. The `!= 0u` check is for clarity (off = no retrigger).

### Change 5 — InstrumentManager.h declarations

After the existing `instrumentManager_clearSlot6Track7StepDecayOverride()`
declaration:

```c
/*
 * LFO phase Scene-change handoff (S076 P2).
 *
 * captureLfoPhases(): snapshots the running phase of every slot's LFO before
 * the deferred Scene worker starts. Caller: preset_startDrumsetApply().
 * restoreLfoPhaseIfNeeded(slot): after descriptor values are applied to the
 * incoming slot's LFO, decides: scn retrigger → phase = phaseOffset;
 * otherwise → phase = captured snapshot (continue). Caller:
 * preset_resetAndApplyKitVoiceImage().
 */
void instrumentManager_captureLfoPhases(void);
void instrumentManager_restoreLfoPhaseIfNeeded(uint8_t slot);
```

### Change 6 — presetManager.c: capture before teardown

In `preset_startDrumsetApply()`, before `instrumentManager_clearAllRuntimeModulationTargets()`:

```c
    /*
     * S076 P2: snapshot each slot's running LFO phase before the deferred
     * Scene worker resets runtime slots. The phase is restored or reset
     * per slot after descriptor values are applied, depending on the
     * incoming Scene's LFO retrigger setting.
     */
    instrumentManager_captureLfoPhases();
```

Placement: before `instrumentManager_clearAllRuntimeModulationTargets()` at
line 1728, because the mod target teardown does not modify LFO phase but
must complete before any slot reset begins.

### Change 7 — presetManager.c: restore after descriptor apply

In `preset_resetAndApplyKitVoiceImage()`, after `presetMorph_applyVoiceNow()`:

```c
    /*
     * S076 P2: restore or reset the LFO phase based on the incoming Scene's
     * retrigger setting. This must run after presetMorph_applyVoiceNow()
     * because the retrigger value is written to the runtime LFO by the
     * descriptor application path.
     */
    if (scene_index == scene_getActiveIndex())
        instrumentManager_restoreLfoPhaseIfNeeded(voice);
```

Active-Scene guard matches the existing `instrumentManager_resetRuntimeSlot()`
guard on line 1651.

## 7. Follow-Up: phaseOffset Scaling (Separate Change)

The `lfo_offset` descriptor row writes a 0..127 byte directly as a
`uint32_t` into `lfo.phaseOffset`. Since the phase accumulator runs
0..0xFFFFFFFF, the offset should be scaled:

```c
phaseOffset = (uint32_t)((value / 127.0f) * 0xFFFFFFFFu);
```

Or with integer math:
```c
phaseOffset = (uint32_t)(((uint64_t)value * 0xFFFFFFFFu) / 127u);
```

This is a separate fix. It affects all existing retrigger behaviour (voice
retrigger, `scn` retrigger, and manual preview retrigger). It should be
done in this session but as a separate change with its own verification.

The TYPE_UINT32 path in `instrumentManager_writeParameter()` does not scale.
This should use a new `IM_SPECIAL_LFO_OFFSET` tag analogous to the existing
`IM_SPECIAL_LFO_RATE`, so the descriptor row can be:

```c
ROW_SPECIAL("lfo_offset", "LFO", "Offset", "ofs", DTYPE_0B127, MOD_NONE,
            lfo.phaseOffset, TYPE_UINT32, IM_SPECIAL_LFO_OFFSET),
```

And the special handler scales the value appropriately.

## 8. Open Decisions

1. **Exclusive vs. compound retrigger** (section 3.1): recommendation is
   exclusive (Option A). Confirm or request compound mode.

2. **Phase offset scaling** (section 7): should this be done in this session
   as a companion change? It affects both existing voice retrigger and the
   new `scn` retrigger. Without it, the `scn` retrigger resets to
   effectively-zero phase regardless of the offset setting.

3. **Display text for `scn`**: the three-character abbreviation `"scn"` fits
   the existing format. Full text for the 8-character display column is
   "ScnChng" or "ScnRset" — confirm preference.

## 9. Verification

1. **`scn` retrigger**: set an LFO to `scn`. Change Scenes. Verify the LFO
   phase resets (visible as the modulated parameter snapping to its start
   value). Change to a Scene whose LFO is NOT `scn`; verify the modulated
   parameter continues smoothly.

2. **Voice retrigger unchanged**: set an LFO to `v1`. Trigger voice 1.
   Verify the LFO resets. Change Scenes. Verify the LFO phase continues
   (not reset by the Scene change).

3. **Track 7 collision**: set slot 6's LFO to `scn`. Trigger track 7.
   Verify the LFO does NOT retrigger (the fix excludes value 7 from voice
   comparisons).

4. **Boot**: power on. First Scene's LFOs should start at phase 0 (or
   phaseOffset if `scn` is set). This is the same as current behaviour.

5. **Phase offset scaling** (if done): set LFO offset to 64. Set retrigger
   to `v1`. Trigger. Verify the LFO starts at approximately 50% of its
   cycle. Repeat with `scn` and Scene change.

## 10. Notes

- The `fxbuf_handoff_t` struct is intentionally not used. It is an
  Effect/arena-specific mechanism with its own lifecycle
  (begin-exit / handoff-read). LFO phase is instrument-slot state, not
  arena state, and mixing the two lifetimes would create ordering hazards.

- The 25-byte handoff struct is static in InstrumentManager.c and is not
  serialized, not in AutoSave, and not in any wire format. It is pure
  runtime transition state.

- If the instrument type changes between Scenes (e.g. slot 3 is DRM in
  Scene A and SNR in Scene B), the phase is still captured and restored
  because every engine type has exactly one Lfo struct in the same position
  within its voice struct. A type change resets the entire engine via
  `memset` + init, but the restore writes `phase` after that reset.
