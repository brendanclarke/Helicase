# Session 063 — Pattern Load Failures: Precise Cause and Fix

**Date:** 2026-09-12  
**Status:** diagnosis complete; targeted implementation specification  
**Scope:** root `/Pattern/` Load only

## Finding

The hardware run exposed two independent integration defects. Neither defect
is in the PAT4 reader, CRC handling, async FAT state machine, or HCNAMES
read/merge/temp/rename transaction.

1. **The successful Pattern Load completion path releases
   `menu_storageBusy` but never releases `menu_loadSaveCommandActive`.** This
   leaves the accepted OK command permanently owning the `...` presentation.
2. **The Pattern Load request drops the SEQ-button destination mask and the
   filesystem explicitly substitutes `scene_getActiveIndex()`.** The PAT4
   reader therefore writes the playing Scene even when the user selected a
   different resident Scene.

The exact required behavior is:

> Load the selected root Pattern into every Scene in the immutable Load-page
> SEQ mask. Replace only each selected Scene's `pat_scene_region_t`. Do not
> replace or apply its Kit, Effect, or general Scene settings, and do not change
> which Scene is active or viewed. Keep the accepted command active through the
> final Pattern index restore, then terminate it through the common Load/Save
> completion helper.

This document supersedes the Test 3 conclusion in
`S063_PAT_FILE_TESTING.md` that Pattern Load is hanging in the HCNAMES state
machine.

---

## Evidence from `SD_CARD_PAT_TEST_3`

The card state is the expected output of a **completed** Pattern payload plus
HCNAMES publication, not an HCNAMES hang:

- `/.hcnames` is 1,860 bytes and has all 146 physical lines: one `#types`
  header plus 145 data rows.
- There is no `/.hcnamtmp` left on the card.
- Physical lines 3 through 10 identify resident Scenes 0 through 7 as Scene
  source `073`, matching the eight Scene loads in the test setup.
- Pattern data row `129 + scene_index` is physical line `row + 2`. Physical
  line 135 is therefore resident Scene 4's Pattern row. It contains:

  ```text
  Barf	001	R
  ```

- The other loaded resident Pattern rows, physical lines 131 through 134 and
  136 through 138, remain `Barf\t-\tR`.

The source makes this result deterministic: current Pattern Load sets
`op_pattern_scene = scene_getActiveIndex()`, and the HCNAMES overlay writes
only the row derived from `op_pattern_scene`. The fixture therefore identifies
resident Scene 4 as the playing Scene at Pattern Load time and proves that the
selected Scene mask never reached the operation.

The payload is not a corrupt or aliased file. All three relevant files are
valid-sized 10,656-byte PAT4 images, and their SHA-256 values are:

```text
Pattern/000 Barf.pat       ab8cbf45bf5f2f6f521938284e419bd507ca78ca566e851840f359c2ae100c93
Pattern/001 Barf.pat       72aefb4d42b4cbf4702ae9ffbea2f72a982f7ce2ada1aeba700b3a36c311c6f6
Scene/073 Barf/Barf.pat    ab8cbf45bf5f2f6f521938284e419bd507ca78ca566e851840f359c2ae100c93
```

Thus slot 001 really differs from the Pattern embedded in Scene 073, which
explains the audible change when it was written into the active Scene.

There is also direct control-flow evidence that the original Pattern
filesystem callback reached Menu:

- `menu_switchPage()` refuses an immediate page change while
  `menu_storageBusy` is set.
- The successful `PRESET_OP_PATTERN_LOAD` branch is the path that clears that
  flag in this transaction.
- The test could leave for Voice mode and later re-enter Load/Save.

What remained stuck was the separate command-owner flag, not the filesystem
operation.

---

## Failure 1 — persistent `...`

### Exact cause

`Core/Menu/menu.c` has a paired command lifecycle:

- `menu_beginLoadSaveCommand()` (currently lines 216–237) sets both
  `menu_loadSaveCommandActive = 1` and `menu_storageBusy = 1`.
- `menu_paintLoadSaveConfirmation()` (line 280 onward) renders `...` whenever
  `menu_loadSaveCommandActive` is nonzero.
- `menu_finishLoadSaveCommand()` (lines 240–266) is the sole normal terminal
  helper. It ends no-playback storage ownership, clears both flags, resets the
  Load/Save cursor, and repaints.

The Pattern-specific success branch currently does this at lines 8635–8640:

```c
case PRESET_OP_PATTERN_LOAD:
    pat_applyPatternSettingsToMenu(menu_getViewedPattern());
    menu_storageBusy = 0;
    menu_resetSaveParameters();
    menu_repaintAll();
    break;
```

`menu_resetSaveParameters()` does not clear
`menu_loadSaveCommandActive`. Every later Load/Save repaint consequently sees
the old accepted command and paints `...`. `menu_handleLoadSaveMenu()` also
returns immediately while that flag remains set, so the page is not merely
painted incorrectly; its controls remain command-gated.

The direct `menu_storageBusy = 0` explains why the user can leave the page even
though re-entry still shows `...`. It also bypasses
`menu_endNoPlaybackStorage()` and leaves the background settings/trace
schedulers suppressed because those schedulers reserve the filesystem while
`menu_isLoadSaveCommandActive()` is true.

The filesystem and Preset completion chain itself is sound:

```text
filesystem_loadPattern_tick()
  -> filesystem_startPatternHcnamesUpdate()
  -> filesystem_residentNames_tick()
  -> filesystem_finish(DONE)
  -> on_pattern_load_complete()
  -> preset_completeFilesystemOp(PRESET_OP_PATTERN_LOAD)
  -> Menu PRESET_OP_PATTERN_LOAD branch
```

### Precise fix

Route Pattern Load through the same final read-only root-index and command
termination contract already used by Scene/Bank Load. The existing
`menu_requestLoadCommandFinalIndexRestore()` already maps
`SAVE_TYPE_PATTERN` to `FS_LIBRARY_INDEX_PATTERN`; the Pattern completion
branch simply fails to call it.

Replace the current branch with this structure:

```c
case PRESET_OP_PATTERN_LOAD:
    pat_applyPatternSettingsToMenu(menu_getViewedPattern());
    pat_applyTrackSettingsToMenu(menu_getViewedPattern(),
                                 menu_getActiveVoice());
    if (!menu_requestLoadCommandFinalIndexRestore()) {
        if (menu_loadSaveCommandActive)
            menu_finishLoadSaveCommand();
        else {
            menu_storageBusy = 0u;
            menu_resetSaveParameters();
        }
    }
    break;
```

The behavior is then:

1. The completed PAT4 and HCNAMES operation reaches Menu.
2. Pattern- and current-track edit fields are projected from the currently
   viewed Scene. This includes the v4 track length, scale, and shuffle fields;
   the current branch refreshes only the two pattern-level fields.
3. The unchanged `/Pattern/.hcindex` is reloaded while the accepted command
   still owns `...` and the filesystem scheduling gate.
4. `menu_loadCommandFinalIndexComplete()` snapshots the result, calls
   `filesystem_ack()`, and calls `menu_finishLoadSaveCommand()` on both success
   and error. On an index error it then shows the normal filesystem overlay.

The fallback is important even if Pattern Load later gains a non-Load-page
caller: no accepted command may terminate by manually clearing only one of the
two ownership flags. `menu_resetSaveParameters()` already repaints, so the old
extra `menu_repaintAll()` is unnecessary.

Do **not** add another callback inside
`filesystem_startPatternHcnamesUpdate()`, and do not bypass HCNAMES. The
current `filesystem_prepareResidentNamesCache()` call should remain; it
prepares the dedicated HCNAMES mirror and does not invalidate the shared
Pattern library-name cache.

---

## Failure 2 — Pattern loaded into the playing Scene

### Exact cause

The panel selection itself is working.

- `menu_loadSaveSelectableSceneMask()` admits Pattern because Pattern is below
  `SAVE_TYPE_GLO`.
- `menu_loadSceneButtonPressed()` toggles the selected bits in
  `menu_kitLoadSceneMask` and prevents the mask from becoming zero.
- Scene Load passes that mask to `preset_loadSceneForScenes()`.

Pattern Load breaks the chain at `Core/Menu/menu.c` lines 7583–7586:

```c
case SAVE_TYPE_PATTERN:
    if (preset_loadPattern(menu_currentPresetNr[SAVE_TYPE_PATTERN]))
        commandAccepted = 1u;
    break;
```

`preset_loadPattern()` has no destination argument and calls the generic
`filesystem_requestLoad(FS_FILE_PATTERN, ...)`. The Pattern arm of that generic
request then explicitly does this in `Core/Hardware/SD/filesystem.c` line
27512:

```c
op_pattern_scene = scene_getActiveIndex();
```

The root reader initializes and streams directly into
`pat_sceneRegionMut(op_pattern_scene)`, and
`filesystem_cacheCurrentResidentPatternName()` publishes only
`filesystem_residentPatternRow(op_pattern_scene)`. The active Scene is
therefore both the data destination and the only HCNAMES destination.

This was not an accidental late regression in the reader. Phase B.5.1 of
`S063_PATTERN_FILE_PHASE_A_B.md` explicitly specified “stream-read into
`pat_regions[active_scene]`.” That implementation-plan sentence contradicted
the authoritative requirement in `S063_DYNAMIC_PATTERN_FILE_AUTOSAVE.md`
section B.5, which says Pattern Load reads into the **target Scene** and does
not touch Kit, Effect, or Scene settings. The faulty active-Scene contract must
be removed at the request boundary, not patched after the read.

### Precise fix

Carry one immutable destination mask from Menu through Preset into the
filesystem, exactly as root Scene Load does.

#### 1. Menu

Change Pattern Load dispatch to pass the already-selected mask:

```c
case SAVE_TYPE_PATTERN:
    if (preset_loadPatternForScenes(
            menu_currentPresetNr[SAVE_TYPE_PATTERN],
            menu_kitLoadSceneMask)) {
        commandAccepted = 1u;
    }
    break;
```

Update the comments on `menu_loadSaveSelectableSceneMask()` and
`menu_loadSceneButtonPressed()` to name Pattern among the mask-targeted Load
types. No new UI state is needed.

#### 2. Preset

Replace the UI-facing entry point with:

```c
uint8_t preset_loadPatternForScenes(uint16_t presetNr,
                                    uint16_t scene_mask);
```

It must capture `scene_mask` in the existing request-stable
`pm_kit_request_scene_mask` before posting the filesystem request, then call
the new mask-aware filesystem function. The field name is historical, but its
existing role is already the immutable destination mask for Kit, Scene, Bank,
and Instrument operations; reusing it adds no retained RAM.

On successful Pattern completion, call
`preset_markRequestedScenesPresentOnSuccessfulLoad()` before
`preset_completeFilesystemOp(PRESET_OP_PATTERN_LOAD)`. This preserves the
existing rule that loading a musical object into an empty physical Scene makes
that Scene resident/present. It is a no-op for the already-present Scenes in
the reported test.

Do not call an AutoSave Pattern marker in S063. Pattern payload AutoSave is
explicitly deferred to S064.

#### 3. Filesystem request boundary

Add the explicit API beside the existing Scene/Kit mask-aware APIs:

```c
bool filesystem_requestLoadPatternForScenes(uint16_t slot,
                                            uint16_t scene_mask,
                                            fs_completion_cb_t cb);
```

Its request-time work must be, in this order:

1. Filter `scene_mask` to `SCENE_COUNT` and the 16 physical Scene bits, using
   the same bounded loop as `filesystem_requestLoadSceneForScenes()`.
2. Reject a zero valid mask, an out-of-range Pattern slot, or a missing Pattern
   index row.
3. Start `FS_INTERNAL_OP_LOAD_PATTERN`.
4. Store the normalized mask in the existing
   `op_scene_load_scene_mask`.
5. Set `op_pattern_scene` to the first set bit in that normalized mask. This is
   the one live Pattern region used as the no-extra-RAM stream target.
6. Capture the Pattern display name, source slot, and LFN exactly as the
   current generic Pattern request does.

Keep `filesystem_requestLoad(FS_FILE_PATTERN, ...)` as a compatibility wrapper
if non-UI callers may use it, but make it delegate to the new API with the
single active-Scene bit. The Load-page path must not use that compatibility
wrapper.

#### 4. PAT4 commit

The reader must not copy anything to the other selected Scenes until the fixed
header, extension, all three payload sections, and CRC have validated.

At successful CRC validation, use the same commit pattern already implemented
by Scene Load phase 51:

```c
const pat_scene_region_t *source = pat_sceneRegion(op_pattern_scene);

for (scene_index = 0u;
     scene_index < SCENE_COUNT && scene_index < 16u;
     scene_index++) {
    if ((op_scene_load_scene_mask & (uint16_t)(1u << scene_index)) == 0u)
        continue;

    if (scene_index != op_pattern_scene) {
        pat_scene_region_t *target = pat_sceneRegionMut(scene_index);
        if (target) {
            memcpy(target, source, sizeof(*target));
            bank_invalidateSdCleanScene(scene_index);
        }
    }
}
```

Only `pat_scene_region_t` is initialized, streamed, and copied. In particular,
this path must not call `filesystem_commitSceneStage()`, copy `scene_t`, copy
`kit_t`, parse/apply Effects, call `menu_startSoundApply()`, or change
`scene_getActiveIndex()`, `seq_activePattern`, or `menu_shownPattern`.

The direct first target is intentionally the same no-extra-RAM, non-atomic
strategy already used by Scene Load's Pattern phase. If a read fails after the
first target has been initialized or partially written, retain the current
root Pattern failure rule: reset that first target to an empty valid Pattern,
leave the not-yet-copied destinations unchanged, and publish no Pattern
HCNAMES rows. Because this is still a real mutation, invalidate the first
target's Bank SD-clean bit immediately beside the existing first-target
`pat_initScene(op_pattern_scene)` call. The success fan-out above then
invalidates only additional targets as each copy commits.

#### 5. HCNAMES publication

Change `filesystem_cacheCurrentResidentPatternName()` from one
`op_pattern_scene` row to a bounded iteration over
`op_scene_load_scene_mask`. For every selected Scene, and only every selected
Scene, publish:

- the loaded Pattern display name;
- the direct Pattern library source slot (`001` in this test); and
- the refreshed witness.

Do not alter the Bank, Scene, Kit, or Instrument rows. This produces the same
name/source fan-out as the Pattern-region fan-out and keeps every resident row
paired with the bytes actually committed to that Scene.

That helper is shared by root Pattern Save. Because `filesystem_start()`
clears `op_scene_load_scene_mask`, the Pattern Save request must set the mask
to the one bit for its existing `op_pattern_scene` source before entering the
shared HCNAMES transaction. This preserves current single-row Pattern Save
publication while Pattern Load gains multi-row publication; otherwise changing
the helper to iterate only the mask would silently regress Pattern Save.

---

## Required end-to-end control flow after the fix

```text
Load-page SEQ mask
  -> preset_loadPatternForScenes(slot, immutable_mask)
  -> filesystem_requestLoadPatternForScenes(slot, immutable_mask)
  -> choose first selected Pattern region as stream target
  -> read and CRC-validate PAT4
  -> copy only pat_scene_region_t to other selected Scenes
  -> update only selected Pattern HCNAMES rows
  -> Preset marks successful targets resident/present
  -> Menu projects viewed Pattern + track fields
  -> read-only /Pattern/.hcindex restore
  -> menu_loadCommandFinalIndexComplete()
  -> filesystem_ack()
  -> menu_finishLoadSaveCommand()
```

If the active Scene bit is absent, playback must not change. If the active
Scene bit is present, its Pattern may change because the user explicitly
selected it. In neither case may Pattern Load change the active Scene number or
any Kit, Effect, or general Scene data.

---

## Changes that are specifically not fixes

- Do not skip the HCNAMES transaction. The test card proves it finishes and
  publishes the row selected by the current faulty destination.
- Do not inject or replace the original completion callback during the
  HCNAMES transition. The current callback reaches Preset and Menu.
- Do not merely clear `menu_loadSaveCommandActive` by direct assignment. Use
  the terminal helper so storage busy, no-playback ownership, cursor reset, and
  scheduler release stay paired.
- Do not load a Scene and restore its Kit/Effect afterwards. That creates a
  larger non-atomic mutation and violates the pattern-only contract.
- Do not add a second full-Pattern staging buffer. Reuse the first selected
  resident Pattern as the stream target and the already-proven Scene Load
  success fan-out.
- Do not add Pattern AutoSave persistence in this correction; that remains
  S064 scope.

---

## Hardware acceptance test

Use a fresh card state or reload Scene 073 into resident Scenes 0–7 before the
test so the polluted Scene 4 Pattern row from `SD_CARD_PAT_TEST_3` is cleared.

### A. Non-active single destination

1. Make resident Scene 4 active and playing.
2. Enter Load → Pattern and select only resident Scene 0 with the SEQ row.
3. Load Pattern slot 001 and confirm OK.
4. While the operation runs, `...` may remain until the final Pattern index
   callback. It must then return to the bracketed Pattern type row.
5. Leave for Voice mode and re-enter Load/Save. `...` must not reappear, and
   encoder/OK input must work.
6. The playing Scene 4 Pattern must remain the Scene 073 Pattern.
7. Switch to resident Scene 0. Only then should Pattern 001 be heard.
8. Kit, Effect, and general Scene settings in Scenes 0 and 4 must be unchanged.
9. In `/.hcnames`, resident Scene 0's Pattern row (physical line 131) must be
   `Barf\t001\tR`; resident Scene 4's Pattern row (physical line 135) must
   remain `Barf\t-\tR`.

### B. Multi-destination and active opt-in

1. With Scene 4 playing, select only Scenes 0 and 7 and load slot 001. Only
   Scenes 0 and 7 and their two Pattern HCNAMES rows may change.
2. Repeat with Scenes 0 and 4 selected. The playing Pattern may now change,
   because the active Scene was explicitly included; no other resident Scene
   may change.

### C. Terminal ownership

After each success or error, verify:

- `menu_loadSaveCommandActive == 0`;
- `menu_storageBusy == 0`;
- the filesystem facade returns to `FS_STATUS_IDLE` after acknowledgement;
- no `/.hcnamtmp` remains;
- `/Pattern/.hcindex` can be browsed immediately; and
- settings, AutoSave, and diagnostic trace scheduling resume normally.

These checks distinguish the corrected command lifecycle from the present
state, where the disk work completes but Menu keeps the accepted command latch
forever.
