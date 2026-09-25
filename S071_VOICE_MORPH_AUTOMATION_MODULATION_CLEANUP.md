# Voice Morph Automation and Modulation Cleanup

Session: S071 · Branch: `dev-ph5-effects`.

Consolidates remaining work from `S070_PH4_MORPH_PER_SCENE_IMPLEMENTATION.md`
and `S071_LFO_VOICE_MORPH.md`. Items already implemented (the Q1 runtime
overlay, step-automation dirty bitmap, morph step override, slot-6 decay
override, audio-out runtime apply, and effective-base selection at every morph
engine site) are not repeated here.

Two product features remain:

**A — Per-Scene voice-edit mask.** The voice-edit mask controls which Scenes
receive a VOICE-page or Scene-settings edit. It is currently a single global
value. Morph, audio out, FX send, fader, decimation, and instrument edits all
fan out through it. Making the mask per-Scene lets the user set different
morph values (and other Scene settings) for different Scenes, then recall them
on Scene switch. Scene switch also needs to queue a morph rebuild so the DSP
converges to the new Scene's morph immediately.

**B — Base-independent LFO voice-morph contribution.** The LFO voice-morph
adapter currently computes an absolute shaped amount and stores it in the
morph engine's hidden contribution table. The resolver converts it back to a
delta relative to the current base. When step automation changes the base
between when the LFO sample was stored and when the morph worker resolves it,
the delta is wrong. The fix is to store a base-independent direction and
normalized depth, and let the resolver compute the delta from the current
effective base at resolution time.

---

## Part A — Per-Scene Voice-Edit Mask

### A1 — `BankData.c`: per-Scene mask storage

**Where**: `Core/Bank/BankData.c`, line 8 and `bank_init()` (line 135).

**Current**:

```c
static uint16_t bank_scene_mask_voice_edit;
```

Init: `bank_scene_mask_voice_edit = 1u;`

**New**:

```c
static uint16_t bank_scene_mask_voice_edit[BANK_SCENE_SLOT_COUNT];
```

Init:

```c
{
    uint8_t i;
    for (i = 0u; i < BANK_SCENE_SLOT_COUNT; i++)
        bank_scene_mask_voice_edit[i] = (uint16_t)(1u << i);
}
```

Each Scene defaults to only itself. The user opts into multi-Scene fan-out
by toggling Scene bits with VOICE + SEQ.

### A2 — `BankData.c`: update all mask accessors

**Where**: `bank_ensureActiveInVoiceEditMask()` (line 67),
`bank_setSceneMaskVoiceEdit()` (line 290),
`bank_sceneMaskVoiceEdit()` (line 311),
`bank_sceneInVoiceEditMask()` (line 326),
`bank_toggleSceneMaskVoiceEdit()` (line 332).

Replace every occurrence of the scalar `bank_scene_mask_voice_edit` with
`bank_scene_mask_voice_edit[bank_active_scene_slot]`. The existing API
signatures are unchanged; callers continue to get/set the active Scene's
mask transparently.

### A3 — `BankData.c` / `BankData.h`: per-Scene setter and getter

Add for boot load (Autosave restore and bankset.bcg parse):

```c
void bank_setSceneMaskVoiceEditForScene(uint8_t scene_index, uint16_t mask);
uint16_t bank_sceneMaskVoiceEditForScene(uint8_t scene_index);
```

The setter normalizes, enforces the active-bit invariant when the index
matches the active Scene, and marks `AUTOSAVE_BANK_FIELD_VOICE_EDIT_MASK`
dirty on change. The getter returns the indexed entry with bounds check.

### A4 — `Autosave.h`: add width constant

**Where**: `Core/Bank/Scene/Autosave.h`, lines 157–158.

`AUTOSAVE_BANK_VOICE_EDIT_MASK_OFFSET` stays at `AUTOSAVE_BANK_OFFSET + 13u`.
Add:

```c
#define AUTOSAVE_BANK_VOICE_EDIT_MASK_BYTES \
    (BANK_SCENE_SLOT_COUNT * 2u)
```

`AUTOSAVE_BANK_SECTION_BYTES` stays at 128. The per-Scene masks (32 bytes)
occupy bytes 13–44, well within the allocation.

### A5 — `Autosave.c`: dirty marking width

**Where**: `autosave_markBankFieldDirty()` (line 1472).

Change `AUTOSAVE_BANK_FIELD_VOICE_EDIT_MASK` width from `2u` to
`AUTOSAVE_BANK_VOICE_EDIT_MASK_BYTES`.

### A6 — `Autosave.c`: live payload getter

**Where**: `autosave_getLivePayloadByte()` (lines 968–973).

Expand the offset range from `13..15` to
`13..(13 + AUTOSAVE_BANK_VOICE_EDIT_MASK_BYTES)`. Compute the Scene index as
`(payload_offset - 13) / 2` and the byte position as
`(payload_offset - 13) % 2`. Read from
`bank_sceneMaskVoiceEditForScene(scene)`.

### A7 — `Autosave.c`: bank payload apply

**Where**: `autosave_applyBankPayload()` (lines 1196–1198).

Replace the single 2-byte read + `bank_setSceneMaskVoiceEdit()` with a
16-iteration loop reading 2 bytes each and calling
`bank_setSceneMaskVoiceEditForScene(scene, mask)`.

### A8 — `storageTypes.h` / `storageTypes.c`: bankset.bcg format

**Where**: `Core/Hardware/SD/storageTypes.h` (lines 239–241) and
`Core/Hardware/SD/storageTypes.c` (lines 1186–1217).

Replace `uint16_t scene_mask_voice_edit` with
`uint16_t scene_mask_voice_edit[BANK_SCENE_SLOT_COUNT]`. Replace
`uint8_t seen_scene_mask_voice_edit` with `uint16_t` (one bit per Scene).

Parser reads both:
- Legacy `scene_mask_voice_edit=XXXX`: sets all 16 entries (backward compat).
- Per-Scene `scene_mask_voice_edit_NN=XXXX`: sets the indexed entry.

Writer emits 16 per-Scene lines.

### A9 — `filesystem.c`: bankset load/save

**Where**: lines 13907, 14090, 18726, 27385 (load) and 29569 (save).

Load: replace single `bank_setSceneMaskVoiceEdit()` with per-Scene loop
using `bank_setSceneMaskVoiceEditForScene()`, gated on the per-Scene
`seen` bits.

Save: capture all 16 masks via `bank_sceneMaskVoiceEditForScene()`.

### A10 — `presetManager.c`: morph rebuild on Scene switch

**Where**: `preset_applySceneSettings()` (line 1139), after
`preset_syncSceneMorphMirrors(scene)`.

Add `presetMorph_rebuildScene(scene_index)` so the morph worker converges
to the new Scene's per-voice morph amounts within the normal foreground
budget, closing the timing gap between mirror sync and deferred slot commit.

---

## Part B — Base-Independent LFO Voice-Morph Contribution

### B1 — `presetMorphEngine.c`: change contribution representation

**Where**: `Core/Bank/Scene/Preset/presetMorphEngine.c`, lines 22–29.

**Current**:

```c
typedef struct {
    uint8_t active;
    uint8_t amount;
} preset_morph_lfo_contribution_t;
```

**New**:

```c
typedef struct {
    uint8_t direction;
    uint8_t depth;
} preset_morph_lfo_contribution_t;
```

Declare the direction type in `presetMorphEngine.h`:

```c
typedef enum {
    PRESET_MORPH_LFO_DIRECTION_NONE = 0,
    PRESET_MORPH_LFO_DIRECTION_MAIN,
    PRESET_MORPH_LFO_DIRECTION_MORPH
} PresetMorphLfoDirection;
```

`direction == NONE` is inactive. Same 2 bytes per entry, zero additional
RAM. Init and clear paths write `{NONE, 0}` where they currently write
`{0, 0}`.

### B2 — `presetMorphEngine.c`: rewrite LFO resolver

**Where**: `presetMorph_resolveLfoAmount()` (lines 140–182).

The resolver currently converts stored absolute amounts to deltas:

```c
effective += (int32_t)contribution->amount - base;
```

Replace with direction-aware delta computation using the current effective
base:

```c
if (contribution->direction == PRESET_MORPH_LFO_DIRECTION_MORPH)
    effective += ((int32_t)(255u - base) * contribution->depth + 127) / 255;
else if (contribution->direction == PRESET_MORPH_LFO_DIRECTION_MAIN)
    effective -= ((int32_t)base * contribution->depth + 127) / 255;
```

This uses the base at resolution time, not at LFO dispatch time. Step
automation changes are reflected immediately without waiting for a new LFO
sample.

### B3 — `presetMorphEngine.c` / `presetMorphEngine.h`: change setter contract

**Where**: `presetMorph_setVoiceLfoModulation()` (lines 605–634) and its
declaration in `presetMorphEngine.h` (line 72).

**Current signature**:

```c
void presetMorph_setVoiceLfoModulation(uint8_t scene_index,
    uint8_t target_slot, uint8_t source_slot, uint8_t target_pair,
    uint8_t active, uint8_t amount);
```

**New signature**:

```c
void presetMorph_setVoiceLfoModulation(uint8_t scene_index,
    uint8_t target_slot, uint8_t source_slot, uint8_t target_pair,
    PresetMorphLfoDirection direction, uint8_t depth);
```

The body stores `direction` and `depth` instead of `active` and `amount`.
`presetMorph_clearLfoSource()` stores `{NONE, 0}` where it currently stores
`{0, 0}` — no other change needed there.

### B4 — `InstrumentManager.c`: encode polarity as direction + depth

**Where**: `instrumentManager_updateLfoSceneDestination()` voice-morph case
(lines 2323–2337).

**Current**: reads effective base, calls `modNode_shapeRangeU16()` to produce
an absolute shaped amount, stores via `presetMorph_setVoiceLfoModulation()`
with `active=1` and `amount=shaped`.

**New**: compute direction and normalized depth from the raw LFO source
value, polarity, and amount — without reading the morph base. The base is
no longer needed at this site because the resolver applies it.

Conceptually, with normalized source `s` in `[0, 1]` and amount `a` in
`[0, 1]`:

```text
positive:  signed_depth =  a * s
negative:  signed_depth = -a * (1 - s)
bipolar:   signed_depth =  a * (2*s - 1)

signed_depth > 0 → MORPH,  round(|signed_depth| * 255)
signed_depth < 0 → MAIN,   round(|signed_depth| * 255)
signed_depth = 0 → NONE, 0
```

Remove the `base` read and `modNode_shapeRangeU16()` call for voice-morph
targets. Decimation and track-7 decay adapters continue using their current
processing unchanged.

---

## Files changed

| File | Part | Changes |
|---|---|---|
| `Core/Bank/BankData.c` | A | Array storage, init, update all accessors, per-Scene setter/getter (A1–A3) |
| `Core/Bank/BankData.h` | A | Declare per-Scene setter/getter (A3) |
| `Core/Bank/Scene/Autosave.h` | A | Width constant (A4) |
| `Core/Bank/Scene/Autosave.c` | A | Dirty width, live getter, bank apply (A5–A7) |
| `Core/Hardware/SD/storageTypes.h` | A | Array + bitfield in bankset state (A8) |
| `Core/Hardware/SD/storageTypes.c` | A | Parse/write per-Scene keys with legacy fallback (A8) |
| `Core/Hardware/SD/filesystem.c` | A | Per-Scene loop at 4 load + 1 save sites (A9) |
| `Core/Bank/Scene/Preset/presetManager.c` | A | `presetMorph_rebuildScene()` in `preset_applySceneSettings()` (A10) |
| `Core/Bank/Scene/Preset/presetMorphEngine.c` | B | Contribution type, resolver, setter (B1–B3) |
| `Core/Bank/Scene/Preset/presetMorphEngine.h` | B | Direction enum, setter signature (B1, B3) |
| `Core/DSP/Instruments/InstrumentManager.c` | B | Polarity encoding without base read (B4) |

## What does not change

- **menu.c**: all fan-out code already calls `bank_sceneMaskVoiceEdit()`.
  The getter returns the active Scene's mask, so fan-out narrows
  automatically.
- **SceneData**: `scene_settings_t::morph_amount` and
  `voice_morph_amount[6]` are already per-Scene.
- **Autosave file size**: `AUTOSAVE_BANK_SECTION_BYTES` stays at 128 (45 of
  128 bytes used after Part A).
- **MIDI**: `preset_morph()` and `preset_morphVoice()` write only to the
  active Scene.
- **LFO contribution table size**: same 144 bytes (2 bytes per entry ×
  6 voices × 6 sources × 2 pairs). Representation changes from
  `active + absolute` to `direction + depth` with zero RAM growth.
- **Sequencer**: Q1 runtime overlay already implemented. No sequencer
  changes in this plan.

## Backward compatibility

### bankset.bcg

Old files with one `scene_mask_voice_edit=XXXX` line: the parser applies the
value to all 16 Scene entries. New files contain 16
`scene_mask_voice_edit_NN=XXXX` lines. Old firmware loading a new file
ignores the per-Scene keys and falls back to its default.

### Autosave record

The voice-edit-mask region grows from 2 to 32 bytes. An old-format record
restores 2 bytes into Scene 0's mask; Scenes 1–15 keep their
`bank_init()` defaults.

### LFO contribution

The representation change is internal to `presetMorphEngine.c`. No
persistent format, SceneData field, or public API semantics change beyond
the setter signature. All existing callers (InstrumentManager and
`presetMorph_clearLfoSource()`) are updated in this plan.

## Implementation order

1. A1–A3 (BankData per-Scene storage, accessors, per-Scene setter/getter)
2. A4–A7 (Autosave format expansion)
3. A8–A9 (bankset.bcg text format and filesystem load/save)
4. A10 (morph rebuild on Scene switch)
5. B1–B3 (contribution type, resolver, setter)
6. B4 (InstrumentManager polarity encoding)

Build after step 6. Parts A and B are independent and can be implemented
in either order. Within Part A, steps 1–3 give correctness; step 4
improves timing. Within Part B, steps 5–6 must land together.

## Test plan

### Per-Scene mask isolation

1. Boot fresh. Select Scene 0. Verify SEQ LEDs show only Scene 0 lit when
   VOICE MODE is held.
2. Toggle Scene 1 into Scene 0's mask. Switch to Scene 1. Verify Scene 1's
   mask shows only Scene 1 (unaffected by Scene 0's toggle).

### Per-Scene morph retention

3. Scene 0, mask = self only. Set `1vm` to 200.
4. Switch to Scene 1. Verify `1vm` is Scene 1's own value.
5. Set Scene 1's `1vm` to 50. Switch back to Scene 0. Verify `1vm` = 200.

### Multi-Scene fan-out when intended

6. Scene 0, add Scene 2 to mask. Set `1vm` to 180. Switch to Scene 2.
   Verify `1vm` = 180. Switch to Scene 1. Verify Scene 1 was unaffected.

### Scene settings fan-out

7. Scene 0 with mask = {0, 3}. Edit audio out for voice 1. Switch to
   Scene 3: routing applied. Switch to Scene 1: original routing.

### AutoSave persistence

8. After steps 3–5, wait 30 s. Power cycle. Verify Scene 0 `1vm` = 200,
   Scene 1 `1vm` = 50, masks as set.

### Morph rebuild on Scene switch

9. Transport running, Scene 0 morph at 200. Switch to Scene 1 (morph 50).
   Verify audible change within one bar.

### Backward compatibility

10. Load old bankset.bcg (single `scene_mask_voice_edit`). Verify all
    Scenes receive that mask.
11. Save Bank with new firmware. Verify 16 per-Scene lines in bankset.bcg.

### LFO + step automation composition

12. Program alternating voice-morph step values (0, 128, 224) on one voice.
    Route a slow positive LFO to that voice's morph. Play. Verify each step
    changes the LFO's starting point: from step value 128, the LFO moves
    toward 255; from 0, it sweeps from 0 toward 255.
13. Diagnostic case: retained base 0, step override 128, LFO positive at
    half depth. Must produce approximately 192 (not 128).
14. Stop transport. Verify LFO continues around retained base 0 (step
    overlay cleared).

### LFO polarity

15. Positive: base toward full-morph endpoint.
16. Negative: base toward main endpoint.
17. Bipolar: travel on both sides of base.
18. Amount zero: base only.
19. Bases 0 and 255: correct one-sided headroom, no wrap.

### No instrument fan-out regression

20. Scene 0, add Scene 2 to mask. Edit a VOICE-page instrument parameter.
    Switch to Scene 2: same value applied. Verify morph was NOT copied.
