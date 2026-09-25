# Per-Scene Voice-Edit Mask and Morph Recall

Session: S070 · Branch: `dev-ph5-effects`.

## 1. Problem

Morph (`PAR_MORPH`) and per-voice Morph (`PAR_VOICE1_MORPH`..`PAR_VOICE6_MORPH`)
are stored per-Scene in `scene_settings_t::morph_amount` and
`voice_morph_amount[6]`. PERF-page edits fan out through
`bank_sceneMaskVoiceEdit()` to every Scene in the voice-edit mask. This
fan-out is the correct mechanism — it lets the user control which Scenes
receive an edit — but the mask is a single global value. When the default mask
includes all Scenes, a morph edit on Scene 0 also writes to Scenes 1–15.
Switching Scenes then recalls the same value, defeating per-Scene retention.

The same fan-out applies to all Scene settings: audio out, FX send, fader,
decimation, and morph. All should use the voice mask, and the mask should be
per-Scene.

Secondary issue: `preset_applySceneSettings()` syncs PERF mirrors but does
not queue a morph rebuild. The deferred Scene-switch worker eventually
rebuilds each slot via `presetMorph_applyVoiceNow()`, but there is a timing
gap between the mirror sync (display updates) and the DSP interpolation
rebuild.

## 2. Root cause

`BankData.c` stores a single `static uint16_t bank_scene_mask_voice_edit`.
Every caller — `menu_cellCommitValue()`, the PAR_MORPH / PAR_VOICEn_MORPH
cases, PAR_VOICE_DECIMATION_ALL, and the `MENU_CELL_SCENE_SETTING` switch —
reads this one mask via `bank_sceneMaskVoiceEdit()`. Because the mask is
global, it does not change when the user switches Scenes. The user has no way
to set different fan-out sets for different Scenes.

The mask is initialized to `1u` (Scene 0 only) at boot, but is overwritten
by bankset.bcg load and Autosave restore. The
`bank_ensureActiveInVoiceEditMask()` invariant guarantees the active Scene is
always in the mask but never narrows it.

## 3. Fix principle

1. **Per-Scene mask**: replace the single `bank_scene_mask_voice_edit` with
   an array `bank_scene_mask_voice_edit[BANK_SCENE_SLOT_COUNT]`. Each Scene
   owns its own fan-out set. `bank_sceneMaskVoiceEdit()` returns the active
   Scene's mask. The active-Scene invariant applies to the active Scene's
   mask entry.

2. **Default**: each Scene's mask defaults to `1u << scene_index` (just
   itself). The user opts into multi-Scene fan-out by adding other Scenes to
   the mask via VOICE + SEQ toggle.

3. **Morph rebuild on Scene switch**: `preset_applySceneSettings()` queues
   `presetMorph_rebuildScene()` after syncing mirrors, closing the DSP gap.

4. **No menu.c changes**: all existing fan-out code in `menu.c` already calls
   `bank_sceneMaskVoiceEdit()`. Because the getter now returns the active
   Scene's mask, the fan-out automatically narrows to that Scene's set.

5. **Serialization**: the Autosave binary format expands the bank section's
   voice-edit-mask field from 2 bytes to 32 bytes (16 × uint16_t). The
   bankset.bcg text format adds per-Scene keys (`scene_mask_voice_edit_NN`).
   Old bankset.bcg files with a single `scene_mask_voice_edit` key are read
   as a legacy default applied to all Scenes.

## 4. Changes

### Change 1 — `BankData.c`: per-Scene mask storage

**Where**: `Core/Bank/BankData.c`, static declaration (line 8) and
`bank_init()` (line 135).

**Current** (line 8):

```c
static uint16_t bank_scene_mask_voice_edit;
```

**Current** `bank_init()` (line 135):

```c
bank_scene_mask_voice_edit = 1u;
```

**New** (line 8):

```c
static uint16_t bank_scene_mask_voice_edit[BANK_SCENE_SLOT_COUNT];
```

**New** `bank_init()`:

```c
/*
 * Each Scene's voice-edit mask defaults to only that Scene.
 *
 * Inputs: none. Output: bank_scene_mask_voice_edit[i] = 1u << i. The
 * user opts into multi-Scene fan-out by toggling Scene bits with VOICE +
 * SEQ. Bank Load and Autosave restore overwrite these defaults. Why:
 * the per-Scene default prevents morph and other Scene-settings edits
 * from silently propagating to all Scenes when no bankset has been
 * loaded.
 */
{
    uint8_t i;
    for (i = 0u; i < BANK_SCENE_SLOT_COUNT; i++)
        bank_scene_mask_voice_edit[i] = (uint16_t)(1u << i);
}
```

### Change 2 — `BankData.c`: update all mask accessors to use active Scene's entry

**Where**: `Core/Bank/BankData.c`, functions
`bank_ensureActiveInVoiceEditMask()` (line 67),
`bank_setSceneMaskVoiceEdit()` (line 290),
`bank_sceneMaskVoiceEdit()` (line 311),
`bank_sceneInVoiceEditMask()` (line 326),
`bank_toggleSceneMaskVoiceEdit()` (line 332).

Each function currently reads/writes the scalar `bank_scene_mask_voice_edit`.
Replace every occurrence with `bank_scene_mask_voice_edit[bank_active_scene_slot]`.

**Example** — `bank_ensureActiveInVoiceEditMask()` (lines 67–89):

```c
static uint8_t bank_ensureActiveInVoiceEditMask(void)
{
    uint16_t active_bit = bank_sceneBit(bank_active_scene_slot);
    uint16_t *mask = &bank_scene_mask_voice_edit[bank_active_scene_slot];
    uint16_t previous_mask = *mask;

    /*
     * Enforce the core Scene-edit invariant on the active Scene's mask.
     *
     * Inputs: retained active Scene and its per-Scene mask entry. Output:
     * invariant-safe mask; if repair changes its value, the caller marks
     * the Autosave field. Why: the active Scene must always be in its own
     * fan-out set so runtime display, automation, and DSP apply treat it
     * as canonical.
     */
    if (active_bit == 0u)
        active_bit = 1u;
    *mask = bank_normalizeSceneMask(*mask);
    if ((*mask & active_bit) == 0u)
        *mask = active_bit;
    return (uint8_t)(*mask != previous_mask);
}
```

**Example** — `bank_sceneMaskVoiceEdit()`:

```c
uint16_t bank_sceneMaskVoiceEdit(void)
{
    if (bank_ensureActiveInVoiceEditMask())
        autosave_markBankFieldDirty(AUTOSAVE_BANK_FIELD_VOICE_EDIT_MASK);
    return bank_scene_mask_voice_edit[bank_active_scene_slot];
}
```

The remaining functions (`bank_setSceneMaskVoiceEdit`,
`bank_sceneInVoiceEditMask`, `bank_toggleSceneMaskVoiceEdit`) follow the same
pattern: substitute `bank_scene_mask_voice_edit` →
`bank_scene_mask_voice_edit[bank_active_scene_slot]`.

### Change 3 — `BankData.c` / `BankData.h`: add per-Scene setter for boot load

**Where**: new function in `Core/Bank/BankData.c`, declared in
`Core/Bank/BankData.h`.

**New function**:

```c
void bank_setSceneMaskVoiceEditForScene(uint8_t scene_index, uint16_t mask)
{
    /*
     * Set one Scene's voice-edit mask by index.
     *
     * Inputs: scene_index (0..15), raw mask. Output: the indexed mask
     * entry is normalized and invariant-checked; if this Scene is also
     * the active Scene, the active-bit invariant is enforced. Why: boot
     * load (Autosave restore and bankset.bcg parse) must populate each
     * Scene's mask independently before the active Scene is known or
     * while iterating all Scenes. The dirty mark covers the entire
     * per-Scene mask array.
     */
    uint16_t previous;

    if (scene_index >= BANK_SCENE_SLOT_COUNT)
        return;
    previous = bank_scene_mask_voice_edit[scene_index];
    bank_scene_mask_voice_edit[scene_index] = bank_normalizeSceneMask(mask);
    if (scene_index == bank_active_scene_slot)
        (void)bank_ensureActiveInVoiceEditMask();
    if (bank_scene_mask_voice_edit[scene_index] != previous)
        autosave_markBankFieldDirty(AUTOSAVE_BANK_FIELD_VOICE_EDIT_MASK);
}
```

**New declaration** in `BankData.h`:

```c
void bank_setSceneMaskVoiceEditForScene(uint8_t scene_index, uint16_t mask);
```

### Change 4 — `Autosave.h`: update offset comments (no constant change)

**Where**: `Core/Bank/Scene/Autosave.h`, lines 157–158.

The offset `AUTOSAVE_BANK_VOICE_EDIT_MASK_OFFSET` stays at
`AUTOSAVE_BANK_OFFSET + 13u`. The width grows from 2 to 32 bytes (16 Scenes
× 2 bytes each). The bank section size stays at 128. Update the comment:

```c
/*
 * Per-Scene VOICE-edit masks: 16 little-endian uint16_t values (32 bytes).
 * Scene N's mask occupies bytes 13 + 2*N .. 13 + 2*N + 1.
 */
#define AUTOSAVE_BANK_VOICE_EDIT_MASK_OFFSET \
    (AUTOSAVE_BANK_OFFSET + 13u)
#define AUTOSAVE_BANK_VOICE_EDIT_MASK_BYTES \
    (BANK_SCENE_SLOT_COUNT * 2u)
```

No other constants need to change. `AUTOSAVE_BANK_SECTION_BYTES` remains 128.
The mask array occupies bytes 13–44, well within the 128-byte allocation.

### Change 5 — `Autosave.c`: update dirty marking width

**Where**: `Core/Bank/Scene/Autosave.c`, inside `autosave_markBankFieldDirty()`
(line 1472).

**Current** (lines 1472–1475):

```c
    case AUTOSAVE_BANK_FIELD_VOICE_EDIT_MASK:
        payload_offset = (uint16_t)(AUTOSAVE_BANK_VOICE_EDIT_MASK_OFFSET -
                                    AUTOSAVE_PAYLOAD_OFFSET);
        width = 2u;
        break;
```

**New**:

```c
    case AUTOSAVE_BANK_FIELD_VOICE_EDIT_MASK:
        payload_offset = (uint16_t)(AUTOSAVE_BANK_VOICE_EDIT_MASK_OFFSET -
                                    AUTOSAVE_PAYLOAD_OFFSET);
        width = AUTOSAVE_BANK_VOICE_EDIT_MASK_BYTES;
        break;
```

### Change 6 — `Autosave.c`: update live payload getter for per-Scene masks

**Where**: `Core/Bank/Scene/Autosave.c`, inside
`autosave_getLivePayloadByte()` (lines 968–973).

**Current** (lines 968–973):

```c
        if (payload_offset >= 13u && payload_offset < 15u) {
            bank_value = bank_sceneMaskVoiceEdit();
            *value = autosave_u16Byte(
                bank_value, (uint8_t)(payload_offset - 13u));
            return 1u;
        }
```

**New**:

```c
        /*
         * Per-Scene voice-edit masks: 32 bytes at payload offsets 13..44.
         *
         * Inputs: payload_offset. Output: the correct byte of the
         * indexed Scene's mask via bank_sceneMaskVoiceEditForScene().
         * Byte layout: Scene N occupies offsets 13+2N (low) and
         * 13+2N+1 (high).
         */
        if (payload_offset >= 13u && payload_offset < 13u + AUTOSAVE_BANK_VOICE_EDIT_MASK_BYTES) {
            uint8_t mask_offset = (uint8_t)(payload_offset - 13u);
            uint8_t mask_scene = (uint8_t)(mask_offset / 2u);
            bank_value = bank_sceneMaskVoiceEditForScene(mask_scene);
            *value = autosave_u16Byte(bank_value, (uint8_t)(mask_offset % 2u));
            return 1u;
        }
```

This requires a new getter `bank_sceneMaskVoiceEditForScene(scene_index)` in
BankData (see Change 3 addendum below).

**Addendum to Change 3** — add a per-Scene getter in `BankData.c`/`BankData.h`:

```c
uint16_t bank_sceneMaskVoiceEditForScene(uint8_t scene_index)
{
    if (scene_index >= BANK_SCENE_SLOT_COUNT)
        return 0u;
    return bank_scene_mask_voice_edit[scene_index];
}
```

Declared in `BankData.h`:

```c
uint16_t bank_sceneMaskVoiceEditForScene(uint8_t scene_index);
```

### Change 7 — `Autosave.c`: update bank payload apply for per-Scene masks

**Where**: `Core/Bank/Scene/Autosave.c`, inside `autosave_applyBankPayload()`
(lines 1196–1198).

**Current** (lines 1196–1198):

```c
    bank_value = (uint16_t)(bank_section[13u] |
                            ((uint16_t)bank_section[14u] << 8u));
    bank_setSceneMaskVoiceEdit(bank_value);
```

**New**:

```c
    /*
     * Restore each Scene's per-Scene voice-edit mask.
     *
     * Inputs: bank_section bytes 13..44 — 16 little-endian uint16_t
     * values. Output: bank_setSceneMaskVoiceEditForScene() is called
     * for each Scene. Why: Autosave applies the per-Scene mask array
     * individually so each Scene recovers its own fan-out set.
     */
    {
        uint8_t scene;
        for (scene = 0u; scene < BANK_SCENE_SLOT_COUNT; scene++) {
            uint8_t base = (uint8_t)(13u + scene * 2u);
            bank_value = (uint16_t)(bank_section[base] |
                                    ((uint16_t)bank_section[base + 1u] << 8u));
            bank_setSceneMaskVoiceEditForScene(scene, bank_value);
        }
    }
```

### Change 8 — `storageTypes.h` / `storageTypes.c`: per-Scene mask in bankset.bcg

**Where**: `Core/Hardware/SD/storageTypes.h` (lines 239–241) and
`Core/Hardware/SD/storageTypes.c` (lines 1186–1217).

**storageTypes.h** — replace single mask with array:

**Current**:

```c
    uint8_t seen_scene_mask_voice_edit;
    /* ... */
    uint16_t scene_mask_voice_edit;
```

**New**:

```c
    uint16_t seen_scene_mask_voice_edit;
    /* ... */
    uint16_t scene_mask_voice_edit[BANK_SCENE_SLOT_COUNT];
```

The `seen` field becomes a 16-bit bitfield (one bit per Scene).

**storageTypes.c — parser** (line 1186):

**Current**: reads `scene_mask_voice_edit=XXXX`.

**New**: reads both formats:

- Legacy: `scene_mask_voice_edit=XXXX` — sets all 16 entries to the same
  value for backward compatibility.
- Per-Scene: `scene_mask_voice_edit_NN=XXXX` (NN = 00..15) — sets the
  indexed entry.

```c
    else if (storage_streq(key, "scene_mask_voice_edit")) {
        /* Legacy: single mask applied to all Scenes. */
        uint8_t i;
        for (i = 0u; i < BANK_SCENE_SLOT_COUNT; i++)
            state->scene_mask_voice_edit[i] = value16;
        state->seen_scene_mask_voice_edit = 0xFFFFu;
    }
    else if (storage_strStartsWith(key, "scene_mask_voice_edit_")) {
        uint8_t idx = storage_parseU8(&key[22]);
        if (idx < BANK_SCENE_SLOT_COUNT) {
            state->scene_mask_voice_edit[idx] = value16;
            state->seen_scene_mask_voice_edit |= (uint16_t)(1u << idx);
        }
    }
```

**storageTypes.c — writer** (line 1213):

**Current**: emits one `scene_mask_voice_edit=XXXX` line.

**New**: emits 16 `scene_mask_voice_edit_NN=XXXX` lines, one per Scene.
This is done across multiple calls to the line writer or by emitting each
Scene's mask in a loop within the existing bankset writer callback.

### Change 9 — `filesystem.c`: update all bankset load/save paths

**Where**: `Core/Hardware/SD/filesystem.c`, lines 13907, 14090, 18726, 27385
(load paths) and line 29569 (save path).

**Load paths** — replace single setter with per-Scene loop:

**Current** (e.g., line 13907):

```c
bank_setSceneMaskVoiceEdit(op_bankset_state.scene_mask_voice_edit);
```

**New**:

```c
{
    uint8_t i;
    for (i = 0u; i < BANK_SCENE_SLOT_COUNT; i++) {
        if (op_bankset_state.seen_scene_mask_voice_edit & (uint16_t)(1u << i))
            bank_setSceneMaskVoiceEditForScene(i, op_bankset_state.scene_mask_voice_edit[i]);
    }
}
```

Apply the same pattern at all four load sites (lines 13907, 14090, 18726,
27385).

**Save path** — replace single getter with per-Scene capture:

**Current** (lines 29569–29573):

```c
op_bankset_state.scene_mask_voice_edit = bank_sceneMaskVoiceEdit();
op_bankset_state.seen_scene_mask_voice_edit = 1u;
```

**New**:

```c
{
    uint8_t i;
    for (i = 0u; i < BANK_SCENE_SLOT_COUNT; i++)
        op_bankset_state.scene_mask_voice_edit[i] =
            bank_sceneMaskVoiceEditForScene(i);
    op_bankset_state.seen_scene_mask_voice_edit = 0xFFFFu;
}
```

### Change 10 — `presetManager.c`: queue morph rebuild on Scene switch

**Where**: `Core/Bank/Scene/Preset/presetManager.c`, inside
`preset_applySceneSettings()` (lines 1120–1124).

**Current** (lines 1120–1124):

```c
    preset_ensureMorphInitialized();
    preset_syncSceneMorphMirrors(scene);
    parameter_values[PAR_VOICE_DECIMATION_ALL] =
        scene->settings.voice_decimation_all;
    preset_applyVoiceDecimationAllRuntime(scene->settings.voice_decimation_all);
```

**New** (add `presetMorph_rebuildScene` after mirror sync):

```c
    preset_ensureMorphInitialized();
    preset_syncSceneMorphMirrors(scene);
    /*
     * Queue a bounded morph rebuild from the new Scene's retained amounts.
     *
     * Inputs: active Scene index. Output: the morph worker is queued for
     * all six slots so the runtime interpolation images converge to the
     * selected Scene's per-voice Morph values within the normal foreground
     * budget. The deferred Scene-switch worker also calls
     * presetMorph_applyVoiceNow() per slot, but that is gated on envelope
     * quiet and may lag; this queue bridges the gap so the DSP begins
     * reflecting the new Scene's Morph as soon as the worker ticks.
     */
    presetMorph_rebuildScene(scene_index);
    parameter_values[PAR_VOICE_DECIMATION_ALL] =
        scene->settings.voice_decimation_all;
    preset_applyVoiceDecimationAllRuntime(scene->settings.voice_decimation_all);
```

## 5. Files changed

| File | Changes |
|---|---|
| `Core/Bank/BankData.c` | Array storage, init per-Scene defaults, update all accessors (Changes 1–2), add per-Scene setter/getter (Change 3) |
| `Core/Bank/BankData.h` | Declare `bank_setSceneMaskVoiceEditForScene()` and `bank_sceneMaskVoiceEditForScene()` (Change 3) |
| `Core/Bank/Scene/Autosave.h` | Add `AUTOSAVE_BANK_VOICE_EDIT_MASK_BYTES` constant, update comment (Change 4) |
| `Core/Bank/Scene/Autosave.c` | Dirty marking width 2→32 (Change 5), live getter per-Scene (Change 6), bank payload apply loop (Change 7) |
| `Core/Hardware/SD/storageTypes.h` | Array + bitfield in `op_bankset_state_t` (Change 8) |
| `Core/Hardware/SD/storageTypes.c` | Parse/write per-Scene keys with legacy fallback (Change 8) |
| `Core/Hardware/SD/filesystem.c` | Per-Scene loop at all 4 load sites and 1 save site (Change 9) |
| `Core/Bank/Scene/Preset/presetManager.c` | `presetMorph_rebuildScene()` in `preset_applySceneSettings()` (Change 10) |

## 6. What does not change

- **menu.c**: all fan-out code already calls `bank_sceneMaskVoiceEdit()`. The
  getter now returns the active Scene's mask, so fan-out automatically
  narrows. No code changes needed in any menu commit path (PAR_MORPH,
  PAR_VOICEn_MORPH, PAR_VOICE_DECIMATION_ALL, MENU_CELL_SCENE_SETTING,
  MENU_CELL_INSTRUMENT, MENU_CELL_KIT_SETTING).

- **Storage schema**: `scene_settings_t::morph_amount` and
  `voice_morph_amount[6]` remain per-Scene. No SceneData change.

- **AutoSave file size**: `AUTOSAVE_BANK_SECTION_BYTES` stays at 128. The
  per-Scene masks (32 bytes) fit within the existing allocation (45 of 128
  bytes used after change, up from 15).

- **MIDI**: `preset_morph()` and `preset_morphVoice()` write only to the
  active Scene. No MIDI change.

- **LFO**: `presetMorph_setVoiceLfoModulation()` is a hidden contribution
  layer that does not touch SceneData or the voice mask.

- **sceneset.scg**: morph values are serialized per-Scene in the Scene
  settings file. No format change.

- **Boot**: `preset_sendDrumsetParameters()` calls `preset_applySceneSettings()`
  and drains `presetMorph_tick()`. The added `presetMorph_rebuildScene()` call
  generates worker items absorbed by the existing drain loop.

## 7. Backward compatibility

### bankset.bcg files

Old bankset.bcg files contain a single `scene_mask_voice_edit=XXXX` line.
The parser reads this as a legacy key and applies the value to all 16 Scene
entries. The writer always emits the new per-Scene format
(`scene_mask_voice_edit_NN=XXXX`). Old firmware loading a new bankset.bcg
ignores the per-Scene keys and falls back to its default mask.

### Autosave record

The Autosave binary format grows the voice-edit-mask region from 2 to 32
bytes. A boot with an old-format record (only 2 bytes at offsets 13–14)
restores those 2 bytes into Scene 0's mask; Scenes 1–15 retain their
`bank_init()` defaults (`1u << scene_index`). A new-format record written
after the upgrade populates all 32 bytes.

## 8. Implementation order

1. Changes 1–3 (BankData per-Scene storage, accessors, per-Scene setter/getter)
2. Changes 4–7 (Autosave format expansion)
3. Changes 8–9 (bankset.bcg text format and filesystem load/save)
4. Change 10 (morph rebuild on Scene switch)

Build after step 4. Steps 1–3 give correctness; step 4 improves timing.

## 9. Test plan

### Per-Scene mask isolation

1. Boot fresh. Select Scene 0. Verify SEQ LEDs show only Scene 0 lit when
   VOICE MODE is held (default mask = just self).
2. Toggle Scene 1 into Scene 0's mask (VOICE + SEQ). Verify LED 1 lights.
3. Switch to Scene 1. Verify SEQ LEDs show only Scene 1 lit (Scene 1's own
   mask was not affected by Scene 0's toggle).
4. Toggle Scene 2 into Scene 1's mask. Switch back to Scene 0. Verify Scene
   0's mask still shows Scene 0 + Scene 1 (not Scene 2).

### Per-Scene morph retention

5. Select Scene 0 (mask = Scene 0 only). Set `1vm` to 200.
6. Switch to Scene 1. Verify `1vm` shows Scene 1's own value (not 200).
7. Set Scene 1's `1vm` to 50.
8. Switch back to Scene 0. Verify `1vm` = 200.
9. Repeat for global Morph (`mrp`).

### Multi-Scene fan-out when intended

10. Select Scene 0. Add Scene 2 to Scene 0's mask.
11. Set `1vm` to 180. Switch to Scene 2. Verify `1vm` = 180 (fan-out worked).
12. Verify Scene 1 was NOT affected (its mask was independent).

### Scene settings fan-out

13. Select Scene 0 with mask = {Scene 0, Scene 3}.
14. Edit audio out routing for voice 1. Switch to Scene 3. Verify same
    routing was applied.
15. Switch to Scene 1. Verify Scene 1 has its original audio routing.

### AutoSave retention across power cycle

16. After steps 5–9, wait 30 s for AutoSave. Power off. Reboot.
17. Select Scene 0. Verify `1vm` = 200 and mask = {Scene 0} (or as set).
18. Select Scene 1. Verify `1vm` = 50 and mask = {Scene 1}.

### Runtime morph on Scene switch

19. Start transport on Scene 0 with Morph at 200 (audible timbral blend).
20. Switch to Scene 1 (Morph 50). Verify audible change within one bar.
21. Switch back to Scene 0. Verify audible return to 200 blend.

### Backward compatibility

22. Load a bankset.bcg file saved by old firmware (single
    `scene_mask_voice_edit` line). Verify all Scenes receive that mask value.
23. Save a Bank with new firmware. Open bankset.bcg. Verify 16
    `scene_mask_voice_edit_NN` lines are present.

### No regression in instrument fan-out

24. Select Scene 0. Add Scene 2 to Scene 0's mask.
25. Edit a VOICE-page instrument parameter (e.g. voice 1 filter cutoff).
26. Switch to Scene 2. Verify the same filter cutoff value was applied.
