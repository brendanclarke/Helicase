# Bank 16-Scene Expansion — Implementation Plan (Rev 4)

Raise the resident Bank from the current one-Scene bridge to a full 16-Scene workspace, wire all three SEQ-button Scene interaction surfaces (PERF, VOICE-held, load/save), add `voice_edit_mask` to BankData, and migrate `FS_FILE_GLOBALS` from `glo.cfg` to `settings.cfg`.

---

## Background

### Current PatternSet — what is actually in the code

`PatternSet` (in `PatternData.h`) is the **bridge** pattern model. Each Scene carries one `PatternSet`. The old "8 fixed pattern slots" design is gone from the code. The `PatternSet` per Scene is the bridge in place *now*, and is what 16× `scene_t` actually allocates.

### Future pattern model — NOT this session's work

The dynamic-stack pool model (Phase 4 in `SCOPING_TARGETS.md`) replaces `PatternSet` with a static address array plus a 16,383-byte dynamic event pool per scene. This is a separate architectural change. This session uses the existing `PatternSet` bridge as-is.

### Actual `scene_t` SRAM math

STM32F765: DTCM 128 KB + SRAM1 368 KB. Total RAM = 496 KB. 
16 × `scene_t` ≈ 186 KB. 
Measuring with `arm-none-eabi-size` after bumping `SCENE_COUNT` is the correct gate before wiring anything else.

### `glo.cfg` status

`glo.cfg` is fully wired as `FS_FILE_GLOBALS` in `filesystem.c`. It is deprecated in favor of `settings.cfg`. The migration involves replacing `glo.cfg` with a keyed-text `settings.cfg` load/save path and retiring the binary raw read/write machines.

---

## Resolved Open Questions

1. **`settings.cfg` schema format**: Resolved to **keyed text** (e.g. `bpm=120\n`), matching other config files (`bankset.bcg`, `sceneset.scg`, `kitset.kcg`). 
2. **`settings.cfg` field scope**: Includes **all global and PERF parameters** for now. This spans the entire range from `PAR_BEGINNING_OF_GLOBALS` (`PAR_BPM`) all the way up through `PAR_VOICE_DECIMATION_ALL`.
3. **PERF-held SEQ LED trigger**: The front panel arrangement is VOICE, PERF, STEP, LOAD/SAVE (Left to Right). In code, this maps to `BUT_MODE2` (index 30).
4. **VOICE-held detection for SEQ toggle**: The physical VOICE button maps to `BUT_MODE1` (index 31).
5. **Bank Load Scene toggle default**: Defaults to `0xFFFF` (all 16 scenes toggled for load). Active scene blinks and CAN be toggled off by the user.
6. **Scene switch apply scope in PERF mode**: `preset_startDrumsetApply(scene_index)` will be used to stagger the DSP kit application over successive foreground passes to prevent audio dropouts.

---

## Proposed Changes

### Component A — Storage expansion (`SceneData.h`, `SceneData.c`)

#### [MODIFY] [SceneData.h](file:///c:/Users/brendan.clarke/proj/Helicase/Core/Bank/Scene/SceneData.h)
```c
/* Change SCENE_COUNT to 16 to allocate the full resident Bank workspace. */
#define SCENE_COUNT 16u

/* Define the reserved index for a future background staging Scene.
 * This prevents hardcoding '16' in later loader features.
 */
#define SCENE_STAGING_INDEX SCENE_COUNT
```
- No changes to `SceneData.c` are necessary because loops rely on `SCENE_COUNT` directly. `scene_initAll()` automatically initializes all 16 scenes.

> [!IMPORTANT]
> Confirm `arm-none-eabi-size` shows ≤ ~280 KB SRAM used after this change before touching any other component.

---

### Component B — BankData voice-edit mask (`BankData.h`, `BankData.c`)

#### [MODIFY] [BankData.h](file:///c:/Users/brendan.clarke/proj/Helicase/Core/Bank/BankData.h)
Add API:
```c
/* 
 * 16-bit mask: bit N set -> Scene N is in the VOICE-mode edit set. 
 * Menu/parameter edits will apply to all scenes present in this mask.
 */
void     bank_setVoiceEditMask(uint16_t mask);
uint16_t bank_getVoiceEditMask(void);
void     bank_toggleVoiceEditScene(uint8_t scene_index);
```

#### [MODIFY] [BankData.c](file:///c:/Users/brendan.clarke/proj/Helicase/Core/Bank/BankData.c)
Implement the above. 
- State variable: `static uint16_t bank_voice_edit_mask = 1u;`
- Default initial mask is `(1u << bank_active_scene_slot)`. 
- When the active scene changes, the mask is **not** automatically reset. 

---

### Component C — `settings.cfg` migration

#### [MODIFY] [filesystem.h](file:///c:/Users/brendan.clarke/proj/Helicase/Core/Hardware/SD/filesystem.h)
- Repurpose `FS_FILE_GLOBALS` as `FS_FILE_SETTINGS`. The caller API doesn't change, just the internal handling.

#### [MODIFY] [filesystem.c](file:///c:/Users/brendan.clarke/proj/Helicase/Core/Hardware/SD/filesystem.c)

- **`fs_file_descs[]`**: Change `FS_FILE_GLOBALS` entry `literal_name` from `"glo.cfg"` to `"settings.cfg"`.
- **Save (`filesystem_saveGlobals_tick`)**:
  - Open `settings.cfg` for write at root.
  - Write fixed header: `format=helicase.settings\nversion=1\n`
  - Write `active_bank=%d\n` (getting value from `bank_getActiveBank()`).
  - Loop through or manually write all parameters mapped from `parameter_values[PAR_BPM]` through `parameter_values[PAR_VOICE_DECIMATION_ALL]`. Examples: `bpm=%d\n`, `ext_sync=%d\n`, `voice1_morph=%d\n`. Use `storage_formatParameter()` helpers if applicable.
- **Load (`filesystem_loadGlobals_tick`)**:
  - Attempt to open `settings.cfg`. If it does not exist, fallback to opening `glo.cfg` and reading binary (backward compatibility).
  - If `settings.cfg` is opened, parse line by line using `storage_parseLine()`.
  - Check keys and update `bank_setActiveSceneSlot()` (for bank) and `parameter_values[]` respectively. 
  - Unknown keys are silently ignored.

---

### Component D — `bankset.bcg` v2 persistence for `voice_edit_mask`

#### [MODIFY] [filesystem.c](file:///c:/Users/brendan.clarke/proj/Helicase/Core/Hardware/SD/filesystem.c)
- **Bank Load parser**: Add string matching for `voice_edit_mask=`. Parse value as a 16-bit hex or int, and call `bank_setVoiceEditMask()`. If field is missing, default to `(1u << bank_active_scene_slot)`.
- **Bank Save writer**: Emit `voice_edit_mask=%u\n` (using `bank_getVoiceEditMask()`) as a new field directly after `active_scene=`.

---

### Component E — PERF-mode Scene switching (new menu API + button wiring)

#### [MODIFY] [menu.h](file:///c:/Users/brendan.clarke/proj/Helicase/Core/Menu/menu.h) & [menu.c](file:///c:/Users/brendan.clarke/proj/Helicase/Core/Menu/menu.c)

Add:
```c
/*
 * Handle SEQ button press in PERF mode — switch the active Scene.
 *
 * Input: zero-based scene_index 0..15. Ignored for index >= SCENE_COUNT.
 * Effects: updates SceneData active index, applies the new Scene's kit to
 * DSP via Preset, refreshes PERF SEQ LEDs and LCD. 
 */
void menu_perfModeSceneButtonPressed(uint8_t scene_index) {
    if (scene_index >= SCENE_COUNT) return;
    
    scene_selectActive(scene_index);
    bank_setActiveSceneSlot(scene_index);
    
    /* Request staggered apply of the new scene to prevent DSP drops */
    preset_startDrumsetApply(scene_index);
    
    menu_refreshPerfSceneLeds();
    menu_repaintAll();
}

/*
 * Repaint the 16 SEQ LEDs for PERF mode.
 *
 * For each Scene: lit if pat_sceneHasActiveSteps(). Active Scene LED:
 * blinks when sequencer is running, static lit when stopped.
 */
void menu_refreshPerfSceneLeds(void) {
    for (uint8_t i = 0; i < SCENE_COUNT; i++) {
        led_setValue(pat_sceneHasActiveSteps(i), LED_SEQ1 + i);
    }
    led_setBlinkLed(LED_SEQ1 + scene_getActiveIndex(), seq_isRunning());
}
```

#### [MODIFY] [buttonHandler.c](file:///c:/Users/brendan.clarke/proj/Helicase/Core/Hardware/frontPanel/buttonHandler.c)

- **In `buttonHandler_seqButtonPressed()`**: Add before existing dispatch:
```c
if (bh_state.selectButtonMode == SELECT_MODE_PERF) {
    menu_perfModeSceneButtonPressed(seqButtonPressed);
    return;
}
```
- **On PERF mode entry (`handleModeButtons()` → `SELECT_MODE_PERF`)**: Call `menu_refreshPerfSceneLeds()` after initializing LEDs.
- **PERF-button held from any mode**: Inside the SHIFT-press handler (e.g. `buttonHandler_processEvents()`), check `btn_held[BUT_MODE2]` (PERF button). If held, call `menu_refreshPerfSceneLeds()` instead of the normal mute LED path.

---

### Component F — VOICE-held Scene edit-mask toggles

#### [MODIFY] [menu.h](file:///c:/Users/brendan.clarke/proj/Helicase/Core/Menu/menu.h) & [menu.c](file:///c:/Users/brendan.clarke/proj/Helicase/Core/Menu/menu.c)

Add:
```c
/* Checks kit type compatibility. Returns 1 if all 6 slot types match. */
static uint8_t scene_kitTypesMatch(uint8_t a, uint8_t b) {
    const scene_t *sa = scene_getConst(a);
    const scene_t *sb = scene_getConst(b);
    if (!sa || !sb) return 0u;
    for (uint8_t slot = 0u; slot < INSTRUMENT_SLOT_COUNT; slot++) {
        if (sa->kit.instruments[slot].type != sb->kit.instruments[slot].type)
            return 0u;
    }
    return 1u;
}

/*
 * Handle SEQ button press while VOICE button is held.
 *
 * Toggles Scene N in/out of the voice-edit mask.
 * - Active Scene cannot be toggled OFF.
 * - A Scene with different instrument types cannot be toggled ON.
 */
void menu_voiceHeldSceneButtonPressed(uint8_t scene_index) {
    uint16_t mask = bank_getVoiceEditMask();
    uint8_t active = scene_getActiveIndex();
    
    if (scene_index == active) return; /* Cannot toggle active off */
    
    if (!(mask & (1u << scene_index))) {
        /* Turning ON: verify all 6 slot types match */
        if (!scene_kitTypesMatch(active, scene_index)) return;
    }
    
    bank_toggleVoiceEditScene(scene_index);
    menu_refreshVoiceHeldSceneLeds();
}

/*
 * Repaint 16 SEQ LEDs for VOICE-held mode.
 * Lit = bit set in bank_getVoiceEditMask(). Active Scene LED blinks.
 */
void menu_refreshVoiceHeldSceneLeds(void) {
    uint16_t mask = bank_getVoiceEditMask();
    for (uint8_t i = 0; i < SCENE_COUNT; i++) {
        led_setValue((mask & (1u << i)) ? 1 : 0, LED_SEQ1 + i);
    }
    led_setBlinkLed(LED_SEQ1 + scene_getActiveIndex(), 1);
}
```

#### [MODIFY] [buttonHandler.c](file:///c:/Users/brendan.clarke/proj/Helicase/Core/Hardware/frontPanel/buttonHandler.c)

In `buttonHandler_seqButtonPressed()`, after the PERF gate, add:
```c
/* BUT_MODE1 represents the physical VOICE mode button */
if (btn_held[BUT_MODE1] && bh_state.selectButtonMode == SELECT_MODE_VOICE) {
    menu_voiceHeldSceneButtonPressed(seqButtonPressed);
    return;
}
```

---

### Component G — Load/Save Scene toggle expansion

#### [MODIFY] [menu.c](file:///c:/Users/brendan.clarke/proj/Helicase/Core/Menu/menu.c)

**`menu_refreshLoadSceneLeds()` — mode-specific default masks**:

Update the existing function so it respects the following defaults when the UI mask resets:
- **Load → Kit/Scene**: Mask defaults to `0x0000`, active scene LED blinks and is implicitly included.
- **Load/Save → Bank**: Mask defaults to `0xFFFF`, active scene LED blinks.
- **Save → Kit/Scene/Instrument**: Active scene blinks, toggles disabled.

Modify `menu_loadSceneButtonPressed()` logic:
- For **Bank Load**: Allow toggling the active scene OFF (LED goes dark), but resume blink if toggled ON. 
- For **Save non-Bank**: Return `1u` immediately to consume the button press without altering the mask (user cannot toggle).
- The mask state (`menu_kitLoadSceneMask`) must reset to the context default each time `menu_saveOptions.what` changes, instead of maintaining previous toggle state.

---

## Implementation Sequencing

1. **A** — bump `SCENE_COUNT`, check link size.
2. **B** — add `voice_edit_mask` to BankData.
3. **C** — migrate `glo.cfg` → `settings.cfg`.
4. **D** — extend `bankset.bcg` write for `voice_edit_mask`.
5. **E** — wire PERF SEQ Scene switching.
6. **F** — wire VOICE-held Scene toggle.
7. **G** — update Load/Save mask defaults.
