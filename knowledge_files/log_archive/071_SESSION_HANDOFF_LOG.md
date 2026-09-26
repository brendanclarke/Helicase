# Session 071 Handoff Log

**Project**: LXR-02 firmware port (STM32F765VIH6)
**Branch**: `dev-ph5-effects`
**Session dates**: 2026-09-25 / 2026-09-26
**Base commit**: `7341d3b` (Session 070 closeout)

---

## End of session

```
DATE: 2026-09-26
SESSION GOAL: Voice morph automation and modulation cleanup (Parts A/B/C),
              LED chase state defect fix, LFO target voice handler fix.
COMPLETED: All three parts implemented, two defects fixed, all 24 hardware
           tests PASS.
VERIFIED ON HARDWARE: Yes — tests T1-T24 all PASS.

CHANGES THIS SESSION:
- Core/Bank/BankData.c: per-Scene voice-edit mask array (A1-A3)
- Core/Bank/BankData.h: per-Scene setter/getter declarations (A3)
- Core/Bank/Scene/Autosave.h: AUTOSAVE_BANK_VOICE_EDIT_MASK_BYTES constant (A4)
- Core/Bank/Scene/Autosave.c: dirty width 2->32, live getter 13..44, bank apply loop (A5-A7)
- Core/Hardware/SD/storageTypes.h: bankset per-Scene array + uint16_t seen (A8)
- Core/Hardware/SD/storageTypes.c: per-Scene parser/writer, legacy fallback fix (A8, Fix 2)
- Core/Hardware/SD/filesystem.c: 4 load + 1 save per-Scene loops (A9)
- Core/Bank/Scene/Preset/presetManager.c: morph rebuild on Scene switch (A10),
  audio-out/FX-send step-override tables and APIs (C1-C8)
- Core/Bank/Scene/Preset/presetManager.h: step-override API declarations (C)
- Core/Bank/Scene/Preset/presetMorphEngine.c: direction enum, contribution struct,
  resolver, setter, init/clear, effectiveVoiceBase helper (B1-B3)
- Core/Bank/Scene/Preset/presetMorphEngine.h: PresetMorphLfoDirection enum,
  setter signature (B1, B3)
- Core/DSP/Instruments/InstrumentManager.c: polarity-to-direction encoding (B4),
  voice cell handler full reinstall (T12)
- Core/DSP/Instruments/InstrumentManager.h: updated contract comment (T12)
- Core/Menu/menu.c: live display effective values (C9), immediate underline (C10),
  presetMorphEngine.h include (C11)
- Core/Sequencer/sequencer.c: audio-out/FX-send step override integration,
  transport stop chase dirty (Fix 1B)
- Core/Sequencer/sequencer.h: seq_setRunning contract comment update (Fix 1B)
- Core/Hardware/frontPanel/ledHandler.c: drain-side chase guard (Fix 1A),
  sequencer.h include
- Core/Hardware/frontPanel/ledHandler.h: contract comment update (Fix 1A)

KNOWN ISSUES INTRODUCED: None.
KNOWN ISSUES RESOLVED:
- Q-C3 boot-state stale voice-edit mask bits (resolved by Part A per-Scene defaults)
- LED chase inversion at boot/stop/PERF-while-stopped (Fix 1A/1B)
- Legacy bankset.bcg mask migration broadcasting Scene 0 to all masks (Fix 2)
- Stale morph_lfo_contributions after LFO target voice cell change from "scn" (T12)

NEXT SESSION RECOMMENDED GOAL: Begin Phase 5 Effects development.
BLOCKERS: None. All S071 changes are hardware-accepted.

CRITICAL REMINDERS FOR NEXT SESSION:
- FX_SEND automation (targets 398..403) apply path is a no-op until Phase 5 FX bus
- Per-track step scale/shuffle have no sequencer playback effect (PATTERN_DYNAMIC_STACK.md §6.4)
- Phase 4.5 copy operations (pat_copyTrack, pat_copyPattern, pat_copyBar) remain queued
```

---

## Build metrics

| Metric | S070 closeout | S071 Parts A+B+C | S071 +LED fix | S071 +T12 fix (final) |
|--------|--------------|-------------------|---------------|----------------------|
| text | 455,804 | 456,620 | 456,644 | 456,748 |
| data | 416 | 416 | 416 | 416 |
| bss | 291,820 | 291,900 | 291,900 | 291,900 |
| image | 456,236 | ~457,052 | ~457,076 | ~457,180 |

### SRAM growth breakdown

| Source | Bytes | Owner |
|--------|------:|-------|
| BankData mask: `uint16_t[1]` -> `uint16_t[16]` | +30 | BankData.c |
| `op_bankset_state` staging: `{u8,u8,u16}` -> `{u16,u8,u16[16]}` | +32 | storageTypes.h |
| Audio-out step-override table: `{active,route}[6]` | +12 | presetManager.c |
| FX-send step-override table: `{active,amount}[6]` | +12 | presetManager.c |
| **Total** | **+86** | |

Parts B (contribution reinterpretation) and T12 (voice cell handler) add zero SRAM.

---

## Part A — Per-Scene voice-edit mask

### Problem

The voice-edit mask was a single global `uint16_t`, shared by all 16 Scenes.
Editing a Scene setting (morph, audio out, etc.) fanned out to whichever
Scenes the mask specified, regardless of which Scene was active when those
bits were toggled. Switching Scenes did not switch the mask context, so
fan-out decisions made for one Scene context contaminated another.

Additionally, on boot with old Autosave or bankset.bcg data, stale mask bits
from a prior session could appear in the VOICE-held LED display (Q-C3).

### Solution

Replace the scalar with a 16-entry `uint16_t` array, one per Scene. Each
Scene defaults to `(1u << i)` (self-only). All existing accessors index by
`bank_active_scene_slot` transparently. New `bank_setSceneMaskVoiceEditForScene()`
and `bank_sceneMaskVoiceEditForScene()` accessors support boot restore and
bankset load/save.

### Implementation details (24 changes, A1-A24)

**BankData (A1-A3):**
- `bank_scene_mask_voice_edit` changed from `uint16_t` to `uint16_t[BANK_SCENE_SLOT_COUNT]`
- `bank_init()` initializes each entry to `(1u << i)`
- All accessors (`bank_ensureActiveInVoiceEditMask`, `bank_setSceneMaskVoiceEdit`,
  `bank_sceneMaskVoiceEdit`, `bank_sceneInVoiceEditMask`, `bank_toggleSceneMaskVoiceEdit`)
  index by `bank_active_scene_slot`
- Per-Scene setter/getter pair added for boot restore and bankset load

**Autosave (A4-A7):**
- `AUTOSAVE_BANK_VOICE_EDIT_MASK_BYTES` = 32 (16 Scenes x 2 bytes)
- Dirty width changed from 2 to 32 bytes
- Live payload getter expanded from offsets 13..15 to 13..44, computing
  Scene index as `(offset - 13) / 2`
- Bank payload apply changed from single 2-byte read to 16-iteration loop

**bankset.bcg (A8):**
- `storage_bankset_t` expanded: `uint16_t scene_mask_voice_edit[BANK_SCENE_SLOT_COUNT]`,
  `uint16_t seen_scene_mask_voice_edit` (one bit per Scene)
- Parser reads legacy `scene_mask_voice_edit=XXXX` with self-only defaults
  (Fix 2, see LED defect section) and new `scene_mask_voice_edit_NN=XXXX`
- Writer emits 16 per-Scene lines
- Per-Scene prefix match uses `strncmp(key, "scene_mask_voice_edit", 21u) == 0 && key[21] == '_'`

**Filesystem (A9):**
- Four load sites (`filesystem.c:13907, 14090, 18726, 27385`) replaced with
  per-Scene loops using `bank_setSceneMaskVoiceEditForScene()`, gated on
  per-Scene `seen` bits
- One save site captures all 16 masks via `bank_sceneMaskVoiceEditForScene()`

**Scene switch morph rebuild (A10/A24):**
- `preset_applySceneSettings()` calls `presetMorph_rebuildScene(scene_index)`
  after `preset_syncSceneMorphMirrors()`, queuing all 6 slots for the bounded
  morph worker so DSP converges to the new Scene's morph amounts immediately

### Backward compatibility

- Old bankset.bcg with single `scene_mask_voice_edit` key: parser expands to
  self-only defaults per Scene (not the old broadcast behavior)
- Old Autosave records with 2-byte mask region: Scenes 1-15 keep `bank_init()`
  defaults; only Scene 0's mask is restored from old data
- New bankset.bcg files contain 16 per-Scene lines; old firmware ignores the
  `_NN` suffix keys and falls back to its default

---

## Part B — Base-independent LFO voice-morph contribution

### Problem

The LFO voice-morph adapter computed an absolute shaped amount using the
retained Scene morph amount as base, then stored it in the morph engine's
hidden contribution table. The resolver converted it back to a delta relative
to its current effective base. When step automation changed the base between
when the LFO sample was stored and when the morph worker resolved it, the
delta was wrong:

```
retained base used by InstrumentManager = 0
active step base used by morph engine   = 128
positive LFO at half travel             = absolute stored amount ~128

old resolver: 128 + (stored 128 - resolver base 128) = 128
expected:     128 + half of (255 - 128)              = ~192
```

### Solution

Store base-independent direction and normalized depth. Let the resolver
compute the delta from the current effective base at resolution time.

### Implementation details (8 changes, B1-B8)

**Contribution type (B1):**
```c
typedef enum {
    PRESET_MORPH_LFO_DIRECTION_NONE = 0,
    PRESET_MORPH_LFO_DIRECTION_MAIN,
    PRESET_MORPH_LFO_DIRECTION_MORPH
} PresetMorphLfoDirection;

typedef struct {
    uint8_t direction;
    uint8_t depth;
} preset_morph_lfo_contribution_t;
```

Same 2 bytes per entry, zero additional SRAM. NONE = inactive.

**Resolver (B2):**
```c
if (direction == MORPH)
    effective += ((int32_t)(255u - base) * depth + 127) / 255;
else if (direction == MAIN)
    effective -= ((int32_t)base * depth + 127) / 255;
```

Uses `presetMorph_effectiveVoiceBase()` for the base at resolution time.

**Setter (B3):** Signature changed from `(active, amount)` to
`(direction, depth)`. Bounds check on direction added. Clear stores
`{NONE, 0}`.

**Effective base helper:** New static `presetMorph_effectiveVoiceBase()`
consolidates step-override-or-retained base selection into one call site,
used by the resolver, pass snapshot, priority path, synchronous apply, and
the public effective-amount getter.

**InstrumentManager encoding (B4/B8):** Polarity-to-direction+depth mapping
without reading the morph base:
```
positive: signed_depth =  amount * source
negative: signed_depth = -amount * (1 - source)
bipolar:  signed_depth =  amount * (2*source - 1)

signed_depth > 0 -> MORPH, round(|signed_depth| * 255)
signed_depth < 0 -> MAIN,  round(|signed_depth| * 255)
signed_depth = 0 -> NONE,  0
```

Explicit `lfo_value_0_1` and `amount` clamping to [0, 1] before polarity math.
Post-quantization `if (depth == 0u) direction = NONE` check.

---

## Part C — Scene superpage live display

### Problem

The Scene superpage (VOICE/mix appended screen) read all values from retained
SceneData, showing the base value instead of the live effective value during
step automation playback. The PERF page showed live morph via
`parameter_values[]`, but the superpage did not.

Additionally, writing a held-step Scene-target automation entry did not
immediately show the underline presence marker; the progressive scan had to
complete a full 128-step sweep.

### Solution

Add step-override tables for audio-out and FX-send (voice morph already had
one from Session 070). Read effective values on the superpage. Set the
underline bit immediately on Scene-target automation write.

### Implementation details (11 changes, C1-C11)

**Step-override tables (C1-C8):**
- `preset_audioout_step_override[6]` and `preset_fxsend_step_override[6]`
  declared at file scope in presetManager.c
- Each is `struct { uint8_t active; uint8_t value; }` (2 bytes per voice)
- Set/clear/get API pairs with input clamping (audio-out to `MIXER_ROUTING_DAC2_R`,
  FX-send to 127)
- `preset_init()` clears both tables
- Sequencer FX_SEND case changed from `return 1u` to `break` (falls through
  to dirty-bit tracking)

**Live display (C9):**
- `menu_cellDisplayValue()` reads from `presetMorph_getEffectiveVoiceAmount()`
  for voice morph, `preset_getEffectiveAudioOut()` for audio out,
  `preset_getEffectiveFxSend()` for FX send
- `menu.c` includes `presetMorphEngine.h` for the effective-amount getter

**Immediate underline (C10):**
- After successful `patSvc_writeStepAutomation()` for a `MENU_CELL_SCENE_SETTING`,
  sets the corresponding `va_searchSceneMask` bit via `va_sceneSearchBitForCell()`
- Uses `else if` (a cell cannot be both INSTRUMENT and SCENE_SETTING)

---

## LED chase state defect

### Symptoms

Three incorrect LED states at first boot, all affecting LED_STEP1/LED_SEQ1:
1. Pattern view, track 0, step 0 set: LED unlit (should be lit)
2. VOICE held, Scene 0 not in active Scene's mask: LED illuminated (should be off)
3. PERF mode, Scene 0 present with active steps: LED unlit (should be lit)

### Root cause

A single spurious `LED_LAYER_CHASE` inversion on LED_STEP1. The CHASE layer
reconstructs the base state and toggles it, so whatever the correct base is,
the rendered output is its complement.

Three paths produce or leave a CHASE layer while `seq_running == 0`:
1. **Boot**: `seq_realignActivePatternToMasterClock()` unconditionally sets
   `SEQ_LED_DIRTY_CHASE` without testing `seq_running`
2. **Transport stop**: no chase-clearing event is queued; layer persists
3. **PERF Scene change while stopped**: `seq_selectActivePattern()` calls
   `seq_realignActivePatternToMasterClock()`, queuing chase for step 0

### Fix

**Fix 1A — Drain-side chase guard** (`ledHandler.c`):
```c
if (d & SEQ_LED_DIRTY_CHASE) {
    if (seq_isRunning())
        led_updateCurrentStep(seq_ledState.chaseStep);
    else
        led_clearActive_step();
}
```
Added `#include "sequencer.h"` for `seq_isRunning()`. No circular dependency.

**Fix 1B — Chase drain on transport stop** (`sequencer.c`):
```c
seq_ledState.dirty |= SEQ_LED_DIRTY_CHASE;
```
Added in the `!isRunning` branch of `seq_setRunning()`, after `seq_running = 0u`.

**Fix 2 — Legacy bankset.bcg mask migration** (`storageTypes.c`):
Legacy single-key `scene_mask_voice_edit=XXXX` expansion changed from copying
`value16` to all 16 entries to writing `(uint16_t)(1u << scene_i)` self-only
defaults.

### Mandate

When playback is not running, no CHASE light shall be visible. The drain-side
guard is a single policy that covers all three paths.

---

## T12 morph assignment defect

### Symptoms

Scene 7, LFO inadvertently driving voice 6 morph. Filter frequency on voice 6
inaudible (morph engine overwriting it every tick). Bug voided on reboot.

### Root cause

`INSTRUMENT_BIND_LFO_TARGET_VOICE` handler (`InstrumentManager.c:3059-3068`)
was store-only: it validated and stored the new voice cell value but did NOT
call `instrumentManager_installLfoModulationTarget()`. Changing the voice cell
from 7 ("scn") to a voice number updated the descriptor byte but left the old
Scene-target installation active, including any `morph_lfo_contributions`
entries.

### Evidence

Autosave trace analysis: 195,210 records, 3,944 `6vm` dirty marks post-boot.
Tick deltas within clusters were 4-11 (LFO-rate oscillation). All on-disk
instrument files showed no Scene-namespace LFO targets. The bug was purely
transient runtime state.

### Fix

Voice cell handler now performs full target reinstall mirroring the param
handler: reads sibling param cell via `instrumentManager_descriptorIndexForBinding()`,
computes target ID with `instrumentManager_lfoTargetIdFromToken()`, and calls
`instrumentManager_installLfoModulationTarget()`. The install path's
`restoreLfoSupplementalTarget()` clears stale morph, decimation, and
Scene-target contributions.

During Kit apply, voice cell (descriptor index 32 or 31) precedes its paired
param cell (33 or 32) in descriptor order. The voice handler's install is
superseded by the param handler's second install — redundant but harmless.

Zero additional SRAM.

---

## Hardware test results

All 24 tests from the S071 test plan passed on target hardware:

| Test | Description | Result |
|------|-------------|--------|
| T1 | Per-Scene mask isolation: boot fresh, Scene 0 shows only self | PASS |
| T2 | Toggle Scene 1 into Scene 0's mask; Scene 1 unaffected | PASS |
| T3 | Scene 0 morph 200, switch to Scene 1, verify independent | PASS |
| T4 | Scene 1 morph 50, switch back, Scene 0 still 200 | PASS |
| T5 | Scene 0 morph 50 retained across switch | PASS |
| T6 | Multi-Scene fan-out: Scene 0+2 mask, morph 180, verify | PASS |
| T7 | Scene settings fan-out: audio out, mask {0,3} | PASS |
| T8 | AutoSave persistence: power cycle, values retained | PASS |
| T9 | Morph rebuild on Scene switch during playback | PASS |
| T10 | Backward compat: old bankset.bcg, all Scenes receive mask | PASS |
| T11 | New bankset.bcg: 16 per-Scene lines written | PASS |
| T12 | LFO + step automation composition: alternating step values | PASS |
| T13 | Diagnostic: retained 0, step 128, LFO half -> ~192 | PASS |
| T14 | Stop transport: LFO continues around retained base | PASS |
| T15 | Positive polarity: base toward full-morph | PASS |
| T16 | Negative polarity: base toward main | PASS |
| T17 | Bipolar: travel on both sides | PASS |
| T18 | Amount zero: base only | PASS |
| T19 | Bases 0 and 255: correct one-sided headroom | PASS |
| T20 | No instrument fan-out regression | PASS |
| T21 | Superpage live automation display: morph changes with steps | PASS |
| T22 | Superpage underline for automated Scene targets | PASS |
| T23 | Held-step immediate underline for Scene targets | PASS |
| T24 | Voice-edit mask boot state: only active Scene LED lit | PASS |

---

## Open questions resolved

| Question | Resolution |
|----------|------------|
| Q-C1 | All three Scene settings (morph, audio out, FX send) get live effective-value display. +12 bytes SRAM each for audio-out and FX-send override tables. |
| Q-C3 | Part A per-Scene defaults are sufficient. No defensive present-mask intersection needed. |
| V1-V7 | All codebase verification items confirmed. See S071 plan §Verification. |

---

## Files changed (complete list)

| File | Changes |
|------|---------|
| `Core/Bank/BankData.c` | Per-Scene mask array, init, all accessors, per-Scene setter/getter |
| `Core/Bank/BankData.h` | Per-Scene setter/getter declarations |
| `Core/Bank/Scene/Autosave.h` | `AUTOSAVE_BANK_VOICE_EDIT_MASK_BYTES` constant |
| `Core/Bank/Scene/Autosave.c` | Dirty width, live getter expansion, bank apply loop |
| `Core/Hardware/SD/storageTypes.h` | bankset per-Scene array, `uint16_t` seen field, `BankData.h` include |
| `Core/Hardware/SD/storageTypes.c` | Per-Scene parser/writer, legacy fallback self-only defaults |
| `Core/Hardware/SD/filesystem.c` | 4 load + 1 save per-Scene loops |
| `Core/Bank/Scene/Preset/presetManager.c` | Morph rebuild on Scene switch, audio-out/FX-send step-override tables and APIs |
| `Core/Bank/Scene/Preset/presetManager.h` | Step-override API declarations |
| `Core/Bank/Scene/Preset/presetMorphEngine.c` | Direction enum, contribution struct, resolver, setter, init/clear, effectiveVoiceBase helper |
| `Core/Bank/Scene/Preset/presetMorphEngine.h` | `PresetMorphLfoDirection` enum, setter signature |
| `Core/DSP/Instruments/InstrumentManager.c` | B8 polarity-to-direction encoding, T12 voice cell handler fix |
| `Core/DSP/Instruments/InstrumentManager.h` | Updated contract comment |
| `Core/Menu/menu.c` | Live display effective values, immediate underline, `presetMorphEngine.h` include |
| `Core/Sequencer/sequencer.c` | Audio-out/FX-send step override integration, transport stop chase dirty |
| `Core/Sequencer/sequencer.h` | `seq_setRunning()` contract comment update |
| `Core/Hardware/frontPanel/ledHandler.c` | Drain-side chase guard, `sequencer.h` include |
| `Core/Hardware/frontPanel/ledHandler.h` | Contract comment update |

---

## Architectural decisions and invariants

### Per-Scene voice-edit mask invariant

Each Scene's mask defaults to self-only `(1u << i)`. The active Scene's bit
must always be set. When the active Scene changes, if the new Scene's bit is
not in the mask, the entire mask drops to just the new Scene's bit. This
prevents cross-Scene contamination.

### LFO contribution ownership

The `morph_lfo_contributions` table is the sole runtime authority for LFO
voice-morph modulation. It is:
- BSS-resident, zero-initialized on boot
- Cleared by `presetMorph_init()` (all directions to NONE)
- Written only by `presetMorph_setVoiceLfoModulation()` (from InstrumentManager
  adapter) and `presetMorph_clearLfoSource()` (from install/restore path)
- Read only by `presetMorph_resolveLfoAmount()` (from the bounded morph worker)

The InstrumentManager never reads the morph base for LFO voice-morph targets.
Direction and depth encode polarity and intensity independent of any base.

### Effective base selection

`presetMorph_effectiveVoiceBase()` is the single authority for choosing between
step-override and retained base. Used by: resolver, pass snapshot,
priority path, synchronous apply, and public effective-amount getter.

### Chase mandate

When playback is not running, no CHASE light shall be visible. Enforced at
the single drain point in `led_processSeqLedState()`.

### LFO target voice cell contract

Both `INSTRUMENT_BIND_LFO_TARGET_VOICE` and `INSTRUMENT_BIND_LFO_TARGET_VOICE_2`
handlers now perform a full target reinstall when the voice cell changes.
This mirrors the sibling param handler: read the paired cell, compute the
target ID, and call `instrumentManager_installLfoModulationTarget()`. The
install path clears stale contributions from the old target before the new
one takes effect.

---

## Deferred items carried forward

- FX_SEND automation (targets 398..403): wired for editing/storage, apply path
  is no-op until Phase 5 FX bus.
- Per-track step scale/shuffle: stored/edited/persisted, no sequencer playback
  effect. See `PATTERN_DYNAMIC_STACK.md` §6.4.
- Phase 4.5 copy operations (`pat_copyTrack`, `pat_copyPattern`, `pat_copyBar`)
  remain queued.
- Fix 3 (defensive autosave format guard for old-format mask detection):
  optional, not applied. Remains available if non-zero padding is found in
  production autosave files.
- Budget extraction: filesystem.c budget primitive may need extraction if
  non-filesystem consumers appear.
