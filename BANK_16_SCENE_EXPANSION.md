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

---

## Implementation Notes — 2026-07-17

### Current code changes landed

- `SCENE_COUNT` is now `16u`; the resident `scenes[]` allocation builds and links.
- BankData now owns:
  - `restore_bank_slot` as a bounded `uint16_t` 0..999 value.
  - `scene_mask_present` for resident Scene availability.
  - `scene_mask_voice_edit` as the requested name for VOICE-mode Scene fan-out.
  - The invariant that the active Scene is always included in `scene_mask_voice_edit`.
- `bankset.bcg` now writes `version=2` and `scene_mask_voice_edit=0xNNNN`.
- Root settings moved from the retired raw globals file to keyed `settings.cfg`.
  - The loader opens only `settings.cfg`; there is no `glo.cfg` fallback.
  - Persisted settings are the agreed global-menu allowlist plus `active_bank`.
  - Morph, voice morph, and decimation are excluded from settings and remain Scene data.
- Bank Load now scans Bank-local `SS Name` children, intersects the requested mask with actual children, and loads selected children one at a time through the existing Scene payload loader.
- Bank Save now writes `bankset.bcg` once, then iterates selected Scene mask bits and saves each resident Scene into its matching Bank-local child folder.
- Boot restore now tries `bank_restoreBankSlot()` from settings/default Bank 000 and passes `0xffff` so Bank Load can load all present child Scenes.
- PERF SEQ presses switch the active Scene; VOICE-held SEQ presses toggle `scene_mask_voice_edit`.
- VOICE-page instrument/Scene-setting commits and PERF Morph/voice-morph/decimation now fan out through `scene_mask_voice_edit`.
- Instrument Load can commit one staged Instrument file to a Scene mask; Instrument Save remains single-source.
- Load/Save Scene LEDs now use context-specific masks:
  - Load Kit/KitMrp/Scene/Instrument targets any physical Scene slot and uses a short SEQ-row flash for the toggled Scene.
  - Save Kit/KitMrp/Scene/Instrument sources only resident present Scenes and stays single-source.
  - Bank Save defaults to resident present Scenes.
  - Bank Load requests a read-only preview scan of the highlighted Bank slot's `00..15` child Scene folders, then lights only the child Scenes present in that Bank; the active Scene is the only persistent blink.
- MODE VOICE held now has a dedicated LED refresh for `scene_mask_voice_edit` instead of reusing PERF Scene LEDs.
- Sequencer trigger safety now guards the 16-channel MIDI note-on cache and moves Pattern random-next sentinels above the 0..15 Scene index range.
- SD_CARD fixture migration:
  - Removed `SD_CARD/GLO.CFG`.
  - Added `SD_CARD/settings.cfg`.
  - Updated existing `SD_CARD/Bank/*/bankset.bcg` files to v2 with `scene_mask_voice_edit=0x0001`.

### Retest Repair Notes — 2026-07-17

- Fixed initial Load:[Bank] entry preview: `menu_requestCurrentLoadSaveSelection()` no longer returns early for explicit-OK Scene/Bank browsing, so Bank entry starts the read-only child Scene scan for the currently highlighted slot before the user scrolls.
- Fixed MODE VOICE held SEQ leakage: `buttonHandler_voiceSceneMaskHoldActive` follows foreground event order instead of the ISR-updated physical held flag, so SEQ presses made during the VOICE hold are always consumed by `menu_voiceHeldSceneButtonPressed()`. `buttonHandler_voiceSceneSeqPressedMask` now retains those consumed buttons until their matching release edges, preventing the normal VOICE release handler from toggling Pattern steps after an overlay press.
- Restored VOICE-mode SEQ row after releasing MODE VOICE by repainting the current voice/viewed Pattern track and SELECT subpage LED after clearing the temporary Scene edit-mask overlay.
- Changed PERF SEQ-row semantics to match the requested Scene status display:
  - Steady LEDs now indicate resident Scenes whose Pattern contains at least one active step.
  - The active resident Scene blinks even when its Pattern is empty.
  - The current-step chaselight is suppressed in PERF because the SEQ row is not a step display there.
  - PERF entry refresh happens after `menu_switchPage()` clears LEDs, so the row is fresh immediately on entry.
- Kept PERF VOICE-button mute behavior and voice LED ownership intact: `buttonHandler_showMuteLEDs()` still runs for PERF, while the new Scene-status repaint only owns the SEQ row.
- PERF Scene switching now selects the Scene and its matching Pattern together:
  - `menu_setShownPattern()` stores valid 0..15 Pattern/Scene indices instead of pinning the UI to Pattern 0.
  - New `seq_selectActivePattern()` immediately aligns `seq_activePattern`, clears pending pattern-change state, resets scheduler positions from the selected Pattern, notifies LED/menu follow state, sends program change, and releases sounding notes.
  - Pattern edits remain active-Scene only; `scene_mask_voice_edit` is used only by parameter fan-out paths.
- Scene setting apply now reapplies per-slot audio routing for the active Scene, so Scene changes restore `audio out` along with morph/decimation and kit voice runtime parameters.
- Successful Kit, Scene, and Instrument loads now OR their captured destination Scene mask into BankData's resident present mask. This makes newly populated Scenes switchable in PERF and toggleable through MODE VOICE immediately after load completion; morph-only loads do not mark empty Scenes present.
- Removed active Pattern repeat/next behavior. `seq_setNextPattern()` is now a no-op compatibility hook, the sequencer boundary resolver stays on `seq_activePattern`, late quantized recording always writes the active Scene/Pattern, the old Pattern Settings repeat/next row is hidden, and PatternData repeat/next setters/getters return neutral compatibility values. Any future repeat/advance feature must switch at the Scene level so Pattern data and Scene parameters move together.
- Scene switching no longer restarts transport. `seq_selectActivePattern()` now preserves `seq_elapsedPpqTicks`/master step clock and calls `seq_realignActivePatternToMasterClock()` after changing the active Pattern. That realign path now derives each track's event count from tick zero instead of advancing from stale per-track counters, so switching Scenes recalculates all track step positions against the existing master position even when the new Scene has different track lengths, scales, shuffle, or rotation.
- Added deferred per-instrument Scene apply for smoother PERF switching:
  - Scene selection and Pattern selection still happen immediately and preserve the current master playback position.
  - Immediate Scene apply now updates only Scene-wide mirrors/decimation; per-instrument runtime state waits.
  - Instrument parameters, the runtime instrument type, LFO slot/target supplemental cells, audio out, and future per-instrument FX/fader affiliates commit per slot only after the old amp envelope is below the quiet threshold or immediately before the new Scene pattern triggers that slot.
  - InstrumentManager now owns a runtime type shadow so a ringing slot keeps rendering through the instrument type whose parameters are actually loaded until the deferred slot commit occurs.
- Fixed Bank Save child iteration after hardware retest:
  - `ERR BnkS11` was Bank Save phase `0x11`, where the writer reopens the just-created Bank directory after one Bank-local Scene payload returns to root. The code was passing the captured 8.3 open alias to `afatfs_opendir_lfn()`, which matches display names; it now uses `afatfs_opendir()` for that short alias.
  - Bank-local child Scene saves now delete any existing `SS *` child folder inside the selected Bank before writing the replacement. This mirrors root Scene Save cleanup and prevents stale embedded Kit/Instrument files from surviving when a Scene's kit name or instrument stems change.
  - Follow-up duplicate-folder regression: the first child cleanup reused `storage_parseNumberedFolder()`, which only recognizes root `NNN Name` folders. Bank children are `NN Name`, so `01 Slak2` could be missed and a later save could create a second visible Bank child on FAT. The delete matcher now has an explicit Bank-local mode using `storage_parseBankSceneFolder()` and deletes every matching `SS *` child before recreating one clean folder.
  - Follow-up freeze/corrupt-save hardening from `SD_CARD/Bank/000 Slak_bad/`: recursive directory delete now carries the exact 8.3 open alias beside each visible name. Slot cleanup passes the matched child directory's `shortName` into the recursive deleter, and the final empty-directory removal uses the new `afatfs_removeObject()` exact-alias path instead of re-resolving by LFN. This matters once a card already contains duplicate visible `SS Name` children: open, recurse, and retire now operate on the same physical directory entry instead of potentially picking a sibling with the same display name.
- Tightened deferred Scene-switch smoothing:
  - The amp-envelope quiet threshold is now `0.0001f` instead of `0.001f`, so quiet-time slot commits wait deeper into the envelope tail.
  - The Morph worker now supports `presetMorph_prioritizeVoice()`: a triggered pending Scene slot can jump ahead of the current bounded Morph sweep, save the interrupted cursor, and resume from that cursor after the priority slot finishes.
  - The trigger-time Scene switch path still synchronously rebuilds the slot from retained endpoints before dispatching the note, so it is not using old interpolation values; priority mainly prevents unrelated queued Morph work from consuming foreground ticks first.
