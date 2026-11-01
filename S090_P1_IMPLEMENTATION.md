# S090 P1 — Load/Save Name Fetch Speed-Up While Playing: Implementation Schedule

**Plan:** `S090_P1_LOAD_SAVE_SPEEDUP.md` (§11 scope, §10 Q1–Q6 accepted).
**Baseline:** the working tree of 2026-10-31 on `dev-ph7-voice_expansion`
(HEAD `d944c87f` plus the uncommitted S089 files). Every line number below is
for that tree. Its image is text 683,512 / data 424 / bss 412,884,
`build/LXRV2_lxr02.img` payload 683,936 B (the §1.3 numbers).
**Status:** implemented 2026-10-31; static gates pass, hardware pending. See
§12 for the implementation record.
**Pre-verified:** every change below was applied to a copy of the working tree
in the session scratchpad and built with the firmware flags (`make all`),
logging on and off (§11). The code blocks in this document are generated
from those same verified edits; the numbers are measured, not estimated.
**One implementation (U20):** S1, S2, S4 and S5 are applied together. S3
and S6 need no code (§1 D1, D2). Nothing outside §11 of the plan is touched.

---

## 0. Decisions applied

| # | Decision (user) | Applied as |
|---|---|---|
| U4/U5/U9 | 129 B RAM snapshot replaces `.hctmp` | `pm_instrument_snapshot`, SRAM1 `.bss`, owner Preset (P-C7) |
| U6 | No wrong names, ever | reuse only through `hcnames_mirror_valid` VALID and the browser-cache tag with an idle facade; every browser reader is tag-checked (§1 D8) |
| U12 | Do not change shared-cache users unless the speedup needs it | AutoSave, Pattern, Bank, copy/clear and boot uses of both caches are unchanged; the only Menu-side cache changes are listed with the time they save (§1 D8) |
| U13–U15 | Withdrawn: 3a, 3b, 4a, 5e | not implemented |
| U17 | Type changes must not wait for a list | item 7 (S5) |
| U18 | Page display unchanged; blank means busy | names stay blank while a read runs; the one visible change is that the type label follows each step at once (§1 D11) |
| U19 | No added underrun risk | no change adds work to a main-loop pass or lengthens a filesystem step; item 1 and §7a withdrawn |
| U20 | One implementation, no side quests | this document; trace format, decoder and `AutosaveTrace.h` unchanged |
| §10 Q1 | Serve Menu `.hcnames` reads from a VALID mirror; accept that `fs_resident_source[]` is not reloaded on that path | S2 (F-12, F-31, F-32) |
| §10 Q2 | Replace item 5 with the existing gates | no op table, no build check, no 5c hooks, no 1 B flag |
| §10 Q3 | 6a-i yes (same-type index reuse in a session); 6a-ii no (cache still cleared on exit) | S4 (FH-2, F-34, M-04, M-05, M-06, M-11) |
| §10 Q4 | Cancel the stale index read | S5 (F-03, F-06..F-11, F-13..F-16, F-33) |
| §10 Q5 | `kit` restore keeps today's no-AutoSave-mark behaviour | `preset_startInstrumentApplyImage(..., 0u)` in P-C7 |
| §10 Q6 | Morph restore helper | `preset_restoreInstrumentMorphSnapshot()` + shared `preset_armInstrumentMorphCommit()` (P-C4, P-C7) |

## 1. Findings from the deep-dive

The plan holds. These findings correct or narrow its details; each is
reflected in the changes below.

**D1. S3 (6c boot seed) needs no code.** The last pre-audio storage
operation is `filesystem_ensureAutosaveFilesBlocking()` (`main.c` ≈1292,
when a Bank is resident and AutoSave is enabled). Its tick reads `.hcnames`
and sets `hcnames_mirror_valid = FS_HCNAMES_MIRROR_VALID` on close
(`filesystem.c` 7804), and nothing later in that operation
(`filesystem_ensureAutosaveFiles_tick()`, 7638–8629) changes the flag. The
first Load/Save entry after boot is therefore served by S2 with no card read.
If ensure did not run or failed, the first entry reads the card as today.
The approved boot read (U8) is not added.

**D2. S6 (3c prompt retry) needs no code.** The deferred dispatcher in
`menu_pollPresetStatus()` (≈17431) fires on the first Menu poll where Preset
is idle, `menu_storageBusy` is clear and the facade is not BUSY, and the main
loop polls Menu every pass (`main.c` 1530, then `filesystem_tick()` 1536).
The workers that may return before it (`menu_tickSoundApply()`,
`preset_tickDrumsetApply()`, `menu_tickInstrumentApply()`) are ordering
constraints: a retry may post a payload load, which must not overlap a
running apply. There is no slower cadence to fix.

**D3. The temporary dual-path V1 check is replaced by a static proof.** With
one implementation the temp path is deleted in the same change, so the
logging-only "run both restores" check of plan item 2 cannot exist. The RAM
restore is identical to the file restore by construction:
- **Same cells.** The temp file was written by
  `storage_formatInstrumentLineView()`: `[params]` for every descriptor with a
  `file_key`, `[morph]` only for Morphable ones (`storageTypes.c` 981–989).
  The loader reset the slot first (`instrumentManager_resetSlot()`), and the
  parser fills Normal from `[params]` and only Morphable Morph cells from
  `[morph]` (`storageTypes.c` ≈2397–2405). `preset_snapshotCellRestored()`
  applies exactly that rule. Every descriptor row is built by the `ROW*`
  macros with a key (`DrumParameters.c` 71–86 and the other three types), so
  `file_key` is never NULL.
- **Same values.** The parser's values are the serializer's values: one-byte
  cells, `self` LFO tokens mapped back to the same slot, values already in
  range. The snapshot holds the resident bytes themselves.
- **Same Scenes.** The file restore completed through
  `preset_startInstrumentApply()`, which targets `pm_kit_request_scene_mask`
  (else the one destination Scene). P-C7 uses the same expression.
- **Same AutoSave behaviour.** Mark argument 0 (Q5); the refreshed witness is
  not set (the file path skipped it for temp loads, F-20 context).
- **Morph restore.** Same type checks, same copy domain (Morphable), same tail
  (`autosave_markInstrumentMorphDirty()`, `bank_invalidateSdCleanScene()`,
  `presetMorph_requestVoice()`, Morph-only worker) through the shared
  `preset_armInstrumentMorphCommit()`.

Hardware checks H2/H3 confirm it on the device.

**D4. Simpler than the plan's API.**
- One capture function serves both sessions (the Morph restore reads only the
  Morph image); the plan listed two.
- The plan's Preset-owned "last apply came from the snapshot" flag is not
  needed: the restore is synchronous, and Menu's existing
  `menu_instrumentTempOperationPending` already tells
  `menu_finishInstrumentApplySession()` which apply finished.

**D5. Kept for the decoder, removed where untraced.**
- `FS_INTERNAL_OP_SAVE_INSTRUMENT_TEMP` stays as a retired value (F-01):
  `decode_devlogs.py` maps internal ops by declaration order.
- `AUTOSAVE_TRACE_INSTRUMENT_ENTRY_PHASE_TEMP_REQUEST/COMPLETE` stay defined
  in `AutosaveTrace.h`; they are simply no longer emitted.
- The three `PRESET_OP_*TEMP*` values are removed (P-H1, P-H2): completed-op
  values are never written to a trace.

**D6. `filesystem_makeInstrumentTemporaryFilename()` stays.** The plan said
to remove the `.hctmp` name builders, but
`filesystem_isReservedInstrumentTemporaryName()` needs it to keep stale
`.hctmp.<ext>` files out of the index. Only its other callers go.

**D7. The RAM restore must wait for the same idle point the file restore
had.** The file restore committed from Preset's completion switch, which runs
only after `menu_tickSoundApply()`, `preset_tickDrumsetApply()` (deferred
Scene-slot commits) and `menu_tickInstrumentApply()` report idle. The RAM
restore is called directly from the lower-row crossing, the idle retry at the
top of the poll and `menu_finishInstrumentApplySession()`, so M-09 checks
`menu_storageBusy`, `menu_instrumentApplyActive`, `menu_soundApplyActive`,
`preset_getStatus()` and `preset_applyWorkersIdle()` itself. A deferred
restore stays owed through `menu_instrumentKitRestorePending`, exactly as a
refused file request did.

**D8. 6a-i needs two leftover cache clears removed.** After any preview, a
VOICE change runs the LSR-01 dirty-name flush. Its completion
(`menu.c` 7804) and the following `.hcnames` request (8168) both called
`filesystem_clearNameCache()`. Since S058 Option 1C no `.hcnames` transaction
touches `fs_list_cache_name` (`filesystem_residentNames_tick()` uses it only
as copy/clear's borrowed scratch, 6950), so both clears only threw the index
away. Removing them (M-05, M-06) is what lets a same-type VOICE change skip
the 2.4–3 s index read. Safety: every browser reader checks the cache domain
and type (`filesystem_kitSlotName()`, `...sceneSlotName()`,
`...bankSlotName()`, `...patternSlotName()`, `filesystem_cachedInstrumentCount()`),
and the reuse test `filesystem_instrumentNameCacheLoaded()` also requires an
idle facade, because the tag is set when a request starts.

**D9. The Instrument index completion could post an unwanted preview.** It
posted `menu_instrumentLoadRequestSelection()` whenever the defer flag was
set. Before S090 only a Load-number turn could set it there. With item 7, a
refused request from a type step also sets it while the cursor is on the
type row or `kit`, and a pool load would then replace the voice. M-04 limits
the post to a pool cursor on the number row, the condition the dispatcher
already uses; other defers go to the dispatcher.

**D10. Item 7 scope as built.**
- **Admitted:** a plain turn of the type row in edit mode on the Load/Save
  page (top level) and on the nested Instrument Load type row, while the only
  storage work is a Menu `.hcindex` read (`filesystem_browseReadRunning()`).
- **Still locked, as today:** pots (Pot 1 also crosses voices and pages,
  whose entry paths are transaction-guarded), and every turn while a write,
  an `.hcnames` card read, a payload load, an apply or a rebuild runs. The
  plan's §5 item 7 point 3 said a turn during a write "still updates the
  label"; as built it stays locked, which keeps every write path unchanged.
- **Every completion that can be pending when a step is admitted is
  handled:** the two index reads have exactly one runtime callback each
  (`menu_instrumentIndexLoadComplete()`, `menu_libraryIndexLoadComplete()`);
  the post-command index restore runs with `menu_loadSaveCommandActive` set and
  is excluded. This closes the S082 freeze route recorded in
  `menu_handleLoadSaveKnobDelta()` (a completion running against a changed
  context, or an input owner lost): refusals no longer touch
  `menu_storageBusy` (M-11, M-12), a cancelled read clears the cache in
  filesystem before its callback, and both completions retire a stale result.

**D11. The one visible change.** Today, a type step that starts a read leaves
the previous type's frame on the LCD until the read completes, because
`menu_parseEncoder()` returns before its repaint while `menu_storageBusy` is
set. `menu_paintTypeStepWhileBusy()` repaints at once: new type label, blank
names (and a blank Kit name on top-level Kit Load). This is required for U17
and follows U18 (blank means busy).

**D12. Remaining cost, reported, not in scope.** A VOICE change after
previews still runs the LSR-01 dirty-name flush, which is a full `.hcnames`
rewrite. With S2 the read after it is free, and with D8 so is the index, but
the write remains. Measure it under plan §10 Q8 (target time).

**D13. Unrelated findings (reported only).**
- `op_instrument_load_index` (`filesystem.c` 1869) is written but never read.
  LTO keeps it in the baseline image and drops it after the patch (−2 B).
- The seven pre-existing `-Wunused-function` warnings in `filesystem.c`
  (plan §1.3) are unchanged.
- `MODULE_INTERCHANGE_SPEC.md` ≈242 gives the mirror as 1,305 B (the old
  145-row size); the code asserts 1,449 B.
- `filesystem_autosaveWriterFinishErrorNow()` and AutoSave recovery phase 38
  still clear the browser cache, left over from before Option 1C. Harmless (it
  costs one index read); left as is under U12.
- The orphaned comment above the temp prototypes in `presetManager.h`
  (documenting a non-existent `filesystem_instrumentLoadPreviewOriginal()`)
  is replaced, because it sits on the API P-H3 rewrites.

**D14. Measured cost (scratch build, §11).**

| | Baseline | Patched | Delta |
|---|---:|---:|---:|
| text | 683,512 | 683,616 | +104 |
| data | 424 | 420 | −4 |
| bss | 412,884 | 413,020 | +136 |
| image payload (`link_budget.py`) | 683,936 | 684,036 | **+100 B** (69,628 B headroom) |
| ITCM / DTCM statics | 15,240 / 4,372 | 15,240 / 4,372 | 0 / 0 |

- **Static RAM (.data + .bss): +132 B.** By symbol:
  `pm_instrument_snapshot` +129 (approved); `op_instrument_load_temporary`
  −1; `op_instrument_save_temporary` −1; `op_instrument_load_index` −2 (D13).
  That is +125, plus 7 B of linker alignment between sections.
- **Stack:** one 196 B `kit_instrument_slot_t` in
  `preset_restoreInstrumentSnapshot()`, foreground, during a restore only
  (approved).
- **Flash:** the expected decrease did not happen. Removing the temp paths
  saves less than the cancel phases, the browse-read API and the
  restore/commit helpers cost.
- **Logging off:** text 666,920, bss 396,184.

## 2. How to apply

- **One implementation.** All changes in §3–§7 go in together; S1, S2, S4 and
  S5 depend on each other only through the shared files.
- **Line numbers** are original working-tree lines. Apply each file from the
  bottom up (highest line first) so earlier numbers stay valid; the IDs are
  listed per file in ascending order.
- **Kinds:**
  - **MODIFY** replaces the listed lines with the block;
  - **REMOVE** deletes them;
  - **ADD** inserts the block before the listed line.
- **Each entry quotes** the first and last original line it touches, for a
  check before editing.
- **Then:** `make all && make img`, compare with §8, run the plan's §6.1 tests
  as amended in §8.

### 2.1 Change index

| Item | What | Changes |
|---|---|---|
| S1 | RAM `kit` snapshot replaces `.hctmp` (normal and Morph) | P-H1..P-H4, P-C1..P-C7, FH-3, F-01, F-02, F-04, F-05, F-17..F-30, ST-1, ST-2, M-01..M-03, M-07..M-10, M-15, M-18, M-22..M-29 |
| S2 | Menu `.hcnames` entry reads served from a VALID mirror | F-12, F-31, F-32 |
| S3 | Boot seed | none (D1) |
| S4 | Same-type Instrument index reuse in a session | FH-2, F-34, M-04, M-05, M-06, M-11 |
| S5 | Type steps never wait; stale index read cancelled | FH-1, F-03, F-06..F-11, F-13..F-16, F-33, M-04, M-11..M-14, M-16, M-17, M-19..M-21, M-30, M-31 |
| S6 | Prompt retry | none (D2) |

## 3. `Core/Bank/Scene/Preset/presetManager.h` and `presetManager.c`

S1 (Preset side). RAM: +129 B `.bss` (`pm_instrument_snapshot`), 196 B stack inside `preset_restoreInstrumentSnapshot()` during a restore.

### `Core/Bank/Scene/Preset/presetManager.h`

#### P-H1 — REMOVE lines 60–63

S1. Remove `PRESET_OP_INSTRUMENT_TEMP_SAVE` and `PRESET_OP_INSTRUMENT_MORPH_TEMP_SAVE` with their comments. Completed-op values are not traced (checked: no `PRESET_OP_` in `decode_devlogs.py`), so renumbering the later values is safe.

First original line (60): `` /* Hidden `.hctmp.<ext>` save that prepares Menu's reversible `kit` row. */ ``  
Last original line (63): `PRESET_OP_INSTRUMENT_MORPH_TEMP_SAVE,`

Removed text:

```c
    /* Hidden `.hctmp.<ext>` save that prepares Menu's reversible `kit` row. */
    PRESET_OP_INSTRUMENT_TEMP_SAVE,
    /* Hidden Morph-only `.hctmp` save/load for InstrumentMrp's reversible row. */
    PRESET_OP_INSTRUMENT_MORPH_TEMP_SAVE,
```

#### P-H2 — REMOVE line 68

S1. Remove `PRESET_OP_INSTRUMENT_MORPH_TEMP_LOAD`.

First original line (68): `PRESET_OP_INSTRUMENT_MORPH_TEMP_LOAD,`  
Last original line (68): `PRESET_OP_INSTRUMENT_MORPH_TEMP_LOAD,`

Removed text:

```c
    PRESET_OP_INSTRUMENT_MORPH_TEMP_LOAD,
```

#### P-H3 — MODIFY lines 333–366

S1. Replace the four temp prototypes (and the orphaned comment above them, which documents a `filesystem_instrumentLoadPreviewOriginal()` that no longer exists) with the snapshot API.

First original line (333): `/*`  
Last original line (366): `instrument_type_t type);`

Replacement:

```c
/*
 * Reversible `kit` row snapshot for nested Instrument Load (S090 P1).
 *
 * What: the entry voice of a nested Instrument Load session is held as one
 * 129-byte RAM image (registry type plus the Normal and Morph parameter
 * images) instead of the former hidden `.hctmp.<ext>` file. Capture is a
 * synchronous copy; a restore commits the image through the same Preset paths
 * the file restore used, so the resident result is the same and no card
 * access happens at entry, on a voice change, or on a `kit` restore.
 *
 * preset_captureInstrumentSnapshot(): copies the resident slot at
 * scene_index/slot into the snapshot. Used by both the normal and the Morph
 * (InstrumentMrp) session; the Morph restore reads only the Morph image.
 * Output: nonzero when the slot exists and holds a registered type; zero
 * leaves the previous snapshot untouched.
 *
 * preset_restoreInstrumentSnapshot(): rebuilds one kit_instrument_slot_t on
 * the stack (instrumentManager_resetSlot() defaults, then exactly the cells
 * the former temp file carried) and commits it through
 * preset_startInstrumentApplyImage() to the same destination Scene mask the
 * last Instrument load used, without the whole-Instrument AutoSave mark. When
 * the active Scene is touched the bounded runtime apply is armed; the caller
 * drives preset_tickInstrumentApply() until it returns 0.
 *
 * preset_restoreInstrumentMorphSnapshot(): copies the snapshot's Morphable
 * Morph cells into the resident slot when its type still matches, then runs
 * the shared InstrumentMrp commit tail (Morph AutoSave mark, card-clean bit,
 * Morph-only worker for the active Scene).
 *
 * Both restores refuse destination_scene >= SCENE_COUNT (Scene 17 is never a
 * destination, S082 P1 R1) and a type that differs from the captured type, and
 * then return 0 without changing any state.
 * Callers: Core/Menu/menu.c menu_prepareInstrumentLoadTemp() and
 * menu_restoreInstrumentLoadTemp() (the nested Instrument Load `kit` row).
 */
uint8_t preset_captureInstrumentSnapshot(uint8_t scene_index, uint8_t slot);
uint8_t preset_restoreInstrumentSnapshot(uint8_t destination_scene,
                                         uint8_t destination_slot,
                                         instrument_type_t type);
uint8_t preset_restoreInstrumentMorphSnapshot(uint8_t destination_scene,
                                              uint8_t destination_slot,
                                              instrument_type_t type);
```

#### P-H4 — MODIFY lines 602–607

S1. Comment only: `preset_startInstrumentApply()` no longer serves the `kit` restore.

First original line (602): `* Inputs: immutable request Scene/slot, filesystem's validated staging payload,`  
Last original line (607): `* restore is deliberately non-marking. Active Scene commits clear all outgoing`

Original:

```c
 * Inputs: immutable request Scene/slot, filesystem's validated staging payload,
 * and a root-pool versus hidden-temporary persistence decision that Menu reads
 * from filesystem's immutable completed-request flag. Output: inactive Scenes receive retained
 * state only. A root-pool commit marks its complete type/Normal/Morph payload
 * for AutoSave immediately at each retained Scene destination; hidden `kit`
 * restore is deliberately non-marking. Active Scene commits clear all outgoing
```

Replacement:

```c
 * Inputs: immutable request Scene/slot, filesystem's validated staging payload,
 * and the caller's AutoSave decision (Menu passes 1 for every root-pool load;
 * since S090 the reversible `kit` restore never reaches this function, it
 * commits through preset_restoreInstrumentSnapshot()). Output: inactive Scenes
 * receive retained state only. A root-pool commit marks its complete
 * type/Normal/Morph payload for AutoSave immediately at each retained Scene
 * destination. Active Scene commits clear all outgoing
```

### `Core/Bank/Scene/Preset/presetManager.c`

#### P-C1 — REMOVE lines 534–552

S1. Remove `on_instrument_temp_save_complete()` and `on_instrument_morph_temp_save_complete()`.

First original line (534): `static void on_instrument_temp_save_complete(void)`  
Last original line (552): `(blank)`

Removed text:

```c
static void on_instrument_temp_save_complete(void)
{
    /*
     * Report completion of the hidden normal Instrument Load source write.
     *
     * Input: filesystem has closed `.hctmp.<ext>` in one type directory.
     * Output: Menu may now load that type's `.hcindex` and permit the `kit`
     * row. No Scene/DSP/HCNAMES work occurs here. Affiliates: Menu entry
     * sequencing and filesystem_requestSaveInstrumentTemp().
     */
    preset_completeFilesystemOp(PRESET_OP_INSTRUMENT_TEMP_SAVE);
}

static void on_instrument_morph_temp_save_complete(void)
{
    /* Report completion of the hidden Morph-only InstrumentMrp baseline. */
    preset_completeFilesystemOp(PRESET_OP_INSTRUMENT_MORPH_TEMP_SAVE);
}
```

#### P-C2 — REMOVE lines 580–585

S1. Remove `on_instrument_morph_temp_load_complete()`.

First original line (580): `static void on_instrument_morph_temp_load_complete(void)`  
Last original line (585): `(blank)`

Removed text:

```c
static void on_instrument_morph_temp_load_complete(void)
{
    /* The staged hidden file is committed through the Morph-only path. */
    preset_completeFilesystemOp(PRESET_OP_INSTRUMENT_MORPH_TEMP_LOAD);
}
```

#### P-C3 — REMOVE lines 2396–2455

S1. Remove `preset_copyInstrumentMorphToMorphIfSameType()` and `preset_commitStagedInstrumentMorphToMorph()`: their only caller was the temp-origin branch removed in P-C4. The snapshot Morph restore (P-C7) has its own copy loop.

First original line (2396): `static uint8_t preset_copyInstrumentMorphToMorphIfSameType(`  
Last original line (2455): `(blank)`

Removed: 60 lines (the whole span above; see the tree for the text).

#### P-C4 — MODIFY lines 2576–2623

S1. Split `preset_startInstrumentMorphApply()`: its tail becomes `preset_armInstrumentMorphCommit()`, shared with the snapshot Morph restore; the `filesystem_loadedInstrumentWasMorphTemporary()` branch goes (a staged image is now always a pool file).

First original line (2576): `void preset_startInstrumentMorphApply(uint8_t scene_index, uint8_t slot)`  
Last original line (2623): `}`

Replacement:

```c
/*
 * Finish one InstrumentMrp-style endpoint commit (S090 P1: shared tail).
 *
 * What: when `copied` is nonzero, marks the destination's Morphable Morph
 * domain dirty for AutoSave, clears the destination Scene's card-clean bit
 * and, for the active Scene, requests that voice's Morph rebuild and arms the
 * bounded Morph-only apply worker. When `copied` is zero (type mismatch or no
 * Morphable cell) nothing changes and the worker stays inactive, so Menu's
 * apply tick finishes on its next poll.
 * Why: these steps were the tail of preset_startInstrumentMorphApply(). The
 * RAM `kit` Morph restore must run exactly the same steps after its own copy,
 * so the tail is shared rather than duplicated.
 * Inputs: destination Scene/slot and the result of the caller's copy.
 * Outputs: AutoSave Morph marks, Bank card-clean state, the Morph engine voice
 * request and the instrument_apply_* worker state.
 * Callers: preset_startInstrumentMorphApply() (pool InstrumentMrp load) and
 * preset_restoreInstrumentMorphSnapshot() (`kit` row restore).
 * Affiliates: autosave_markInstrumentMorphDirty(),
 * bank_invalidateSdCleanScene(), presetMorph_requestVoice(),
 * preset_tickInstrumentApply().
 */
static void preset_armInstrumentMorphCommit(uint8_t scene_index,
                                            uint8_t slot,
                                            uint8_t copied)
{
    if (!copied)
        return;
    /*
     * InstrumentMrp copied only compatible Morphable endpoint values into
     * the retained destination. Mark precisely that same descriptor domain
     * now: type, Normal endpoints, name, HCNAMES source, routing, and
     * runtime interpolation did not change. This remains outside the
     * active-Scene branch because inactive resident Scene data still needs
     * persistence even though it requires no immediate audible refresh.
     */
    autosave_markInstrumentMorphDirty(scene_index, slot);
    /*
     * Option 2: InstrumentMrp changed the resident Morph image. The
     * destination Scene's card-clean bit clears because its saved payload
     * no longer equals the on-card Bank child. Affiliate:
     * bank_invalidateSdCleanScene().
     */
    bank_invalidateSdCleanScene(scene_index);
    if (scene_index == scene_getActiveIndex()) {
        presetMorph_requestVoice(scene_index, slot);
        instrument_apply_active = 1u;
        instrument_apply_scene = scene_index;
        instrument_apply_phase = INSTRUMENT_APPLY_PHASE_MORPH_REBUILD;
        instrument_apply_rebind_source = 0u;
        instrument_apply_morph_only = 1u;
    }
}

void preset_startInstrumentMorphApply(uint8_t scene_index, uint8_t slot)
{
    /*
     * Commit a staged pool InstrumentMrp file and arm a morph-only refresh.
     *
     * InstrumentMrp preserves the destination slot identity. It copies no
     * display name, performs no routing apply, and does not clear/rebind
     * modulation targets. A type mismatch is a no-change operation; the cursor
     * remains inactive and Menu will simply unlock on the next poll.
     *
     * Since S090 the staged image is always a pool file: its normal endpoints
     * become the resident Morph endpoints. The reversible `kit` baseline no
     * longer passes through filesystem staging; it is restored from RAM by
     * preset_restoreInstrumentMorphSnapshot(), which shares the commit tail
     * preset_armInstrumentMorphCommit().
     */
    instrument_apply_active = 0u;
    preset_ensureMorphInitialized();
    preset_armInstrumentMorphCommit(
        scene_index, slot,
        preset_commitStagedInstrumentNormalToMorph(scene_index, slot));
}
```

#### P-C5 — MODIFY lines 2683–2689

S1. Comment only in `preset_startInstrumentApplyImage()`.

First original line (2683): `* targets or differ from a file load. Only a root-pool load enables the`  
Last original line (2689): `* and filesystem_instrumentLoadPreviewOriginal().`

Original:

```c
     * targets or differ from a file load. Only a root-pool load enables the
     * whole-Instrument AutoSave marker; temporary restore deliberately does
     * not manufacture a new retained-data mutation.
     *
     * Affiliates: preset_startInstrumentApply(),
     * preset_loadInstrumentTemp(), filesystem_loadedInstrumentSlot(),
     * and filesystem_instrumentLoadPreviewOriginal().
```

Replacement:

```c
     * targets or differ from a file load. Only a root-pool load enables the
     * whole-Instrument AutoSave marker; the snapshot `kit` restore deliberately
     * does not manufacture a new retained-data mutation.
     *
     * Affiliates: preset_startInstrumentApply(),
     * preset_restoreInstrumentSnapshot() (S090 P1, RAM `kit` restore),
     * filesystem_loadedInstrumentSlot() and preset_startInstrumentCopy().
```

#### P-C6 — MODIFY lines 2822–2824

S1. Comment only in `preset_startInstrumentApply()`.

First original line (2822): `* Inputs: callback-captured Scene/slot plus Preset's immutable request`  
Last original line (2824): `* decision. Output: the Instrument result commits through the same safe`

Original:

```c
     * Inputs: callback-captured Scene/slot plus Preset's immutable request
     * type/mask plus Menu's immutable root-pool versus temporary-restore
     * decision. Output: the Instrument result commits through the same safe
```

Replacement:

```c
     * Inputs: callback-captured Scene/slot plus Preset's immutable request
     * type/mask plus Menu's AutoSave decision (1 for every root-pool load since
     * S090). Output: the Instrument result commits through the same safe
```

#### P-C7 — MODIFY lines 2856–2980

S1. Replace the four temp functions with the 129 B snapshot, its cell rule, capture, normal restore and Morph restore.

First original line (2856): `uint8_t preset_saveInstrumentTemp(uint8_t source_scene, uint8_t source_slot)`  
Last original line (2980): `(blank)`

Replacement:

```c
/*
 * The reversible `kit` row snapshot (S090 P1; 129 B SRAM1 .bss, approved by
 * the user 2026-10-31, owner Preset).
 *
 * What: the entry voice of one nested Instrument Load session: its registry
 * type and its Normal and Morph parameter images, byte for byte.
 * Why: it replaces the hidden `.hctmp.<ext>` file. While audio plays the
 * facade gets one 16-byte bit-banged burst per main-loop pass, so that file's
 * create/write/close/sync took 9-10 s at every entry and voice change and the
 * restore paid a file read. morph_interpolation is not stored: the file path
 * never restored it either (the loader resets the staged slot and the apply
 * rebuilds interpolation through presetMorph_requestAll()).
 * Lifetime: meaningful while Menu's menu_instrumentTempValid is set; Menu owns
 * that validity and the original nine-byte name (menu_instrumentTempName).
 * Only one nested Instrument session exists at a time, so one image suffices;
 * a new capture simply overwrites it.
 * Writer: preset_captureInstrumentSnapshot(). Readers:
 * preset_restoreInstrumentSnapshot(), preset_restoreInstrumentMorphSnapshot().
 */
static struct {
    uint8_t type;                                            /* instrument_type_t */
    instrument_param_value_t normal[INSTRUMENT_PARAM_COUNT]; /* instrument_parameters */
    instrument_param_value_t morph[INSTRUMENT_PARAM_COUNT];  /* morph_instrument_parameters */
} pm_instrument_snapshot;
/* The approved allocation is exactly 129 B: one type byte plus two images of
 * INSTRUMENT_PARAM_COUNT one-byte cells; the struct has no padding. */
_Static_assert(sizeof(pm_instrument_snapshot) == 129u,
               "S090 P1: the approved kit snapshot is 129 B");

/*
 * Decide whether the former temp file carried one descriptor cell (S090 P1).
 *
 * What: true for a Normal cell when the descriptor has a file key, and for a
 * Morph cell when it has a file key and is Morphable.
 * Why: the `.hctmp` file was written by storage_formatInstrumentLineView(),
 * which emits [params] for every descriptor with a file key and [morph] only
 * for Morphable ones (storageTypes.c storage_descriptorWritableInSection()).
 * On restore the loader reset the slot first, so every other cell came back
 * as its instrumentManager_resetSlot() default. Copying exactly the cells the
 * file carried keeps the RAM restore identical to the file restore it
 * replaces, including for cells the file never held.
 * Inputs: one registry descriptor and the section (0 Normal, 1 Morph).
 * Output: nonzero when that cell is restored from the snapshot.
 * Callers: preset_restoreInstrumentSnapshot(),
 * preset_restoreInstrumentMorphSnapshot().
 */
static uint8_t preset_snapshotCellRestored(const ParamDescriptor *descriptor,
                                           uint8_t morph_section)
{
    if (!descriptor || !descriptor->file_key)
        return 0u;
    if (!morph_section)
        return 1u;
    return (uint8_t)((descriptor->flags & INSTRUMENT_PARAM_FLAG_MORPHABLE) != 0u);
}

uint8_t preset_captureInstrumentSnapshot(uint8_t scene_index, uint8_t slot)
{
    const kit_instrument_slot_t *source =
        scene_instrumentSlotConst(scene_index, slot);

    /*
     * Capture the entry voice for the `kit` row (S090 P1).
     *
     * What: copies the resident slot's type and both endpoint images into the
     * one RAM snapshot; nothing else changes and no filesystem request is made.
     * Why: it replaces preset_saveInstrumentTemp() /
     * preset_saveInstrumentMorphTemp(), whose `.hctmp` write blocked the
     * nested entry chain for 9-10 s while playing.
     * Inputs: the session's Scene and voice (Menu's menu_instrumentLoadScene /
     * menu_instrumentLoadSlot). scene_instrumentSlotConst() rejects an
     * invalid Scene or slot. Output: 1 when captured; 0 leaves the previous
     * snapshot unchanged and Menu keeps the `kit` row unavailable.
     * Caller: menu_prepareInstrumentLoadTemp() (normal and Morph sessions).
     */
    if (!source || source->type >= INSTRUMENT_TYPE_UNKNOWN)
        return 0u;
    pm_instrument_snapshot.type = (uint8_t)source->type;
    memcpy(pm_instrument_snapshot.normal,
           source->parameter_images.instrument_parameters,
           sizeof(pm_instrument_snapshot.normal));
    memcpy(pm_instrument_snapshot.morph,
           source->parameter_images.morph_instrument_parameters,
           sizeof(pm_instrument_snapshot.morph));
    return 1u;
}

uint8_t preset_restoreInstrumentSnapshot(uint8_t destination_scene,
                                         uint8_t destination_slot,
                                         instrument_type_t type)
{
    /*
     * One staged slot on the foreground stack (196 B, approved 2026-10-31).
     * preset_startInstrumentApplyImage() copies it into every destination
     * Scene before returning, so it does not need to outlive this call.
     */
    kit_instrument_slot_t staged;
    const instrument_registry_entry_t *entry;
    uint16_t destination_mask;
    uint8_t i;

    /*
     * Restore the entry voice from RAM through the normal Instrument commit
     * (S090 P1; replaces preset_loadInstrumentTemp() and its `.hctmp` read).
     *
     * Nothing loads into Scene 17 (S082 P1 R1, D36): a destination outside
     * the resident range is refused before any state changes, as the file
     * path did. A type that differs from the captured type is refused too,
     * so a snapshot can never be applied to a different instrument family.
     */
    if (destination_scene >= SCENE_COUNT ||
        destination_slot >= INSTRUMENT_SLOT_COUNT ||
        type >= INSTRUMENT_TYPE_UNKNOWN ||
        pm_instrument_snapshot.type != (uint8_t)type)
        return 0u;
    entry = instrumentManager_registryEntry(type);
    if (!entry)
        return 0u;
    /*
     * Rebuild the slot exactly as the file loader staged it: reset to the
     * type's defaults, then lay in only the cells the file carried (see
     * preset_snapshotCellRestored()). The loop bound is the type's own
     * descriptor count, the same domain the loader and serializer walk.
     */
    instrumentManager_resetSlot(&staged, type);
    for (i = 0u; i < entry->descriptor_count && i < INSTRUMENT_PARAM_COUNT; i++) {
        const ParamDescriptor *descriptor = &entry->descriptors[i];

        if (preset_snapshotCellRestored(descriptor, 0u))
            staged.parameter_images.instrument_parameters[i] =
                pm_instrument_snapshot.normal[i];
        if (preset_snapshotCellRestored(descriptor, 1u))
            staged.parameter_images.morph_instrument_parameters[i] =
                pm_instrument_snapshot.morph[i];
    }
    /*
     * Commit to the same Scenes the file restore committed to.
     *
     * The file restore completed through preset_startInstrumentApply(), which
     * targets pm_kit_request_scene_mask (the mask of the last accepted
     * Instrument load, i.e. the previews this restore undoes) and falls back
     * to the one destination Scene when that mask is empty. The same
     * expression is used here so the set of restored Scenes is unchanged.
     * The final 0 is the AutoSave decision: a restore returns to the session
     * baseline and must not create a whole-Instrument mutation mark (S090 F4).
     */
    destination_mask = pm_kit_request_scene_mask
        ? pm_kit_request_scene_mask
        : (uint16_t)(1u << destination_scene);
    preset_startInstrumentApplyImage(&staged, destination_mask,
                                     destination_slot, destination_slot,
                                     type, 0u);
    return 1u;
}

uint8_t preset_restoreInstrumentMorphSnapshot(uint8_t destination_scene,
                                              uint8_t destination_slot,
                                              instrument_type_t type)
{
    kit_instrument_slot_t *destination;
    const instrument_registry_entry_t *entry;
    uint8_t copied = 0u;
    uint8_t i;

    /*
     * Restore the entry Morph endpoints for the InstrumentMrp `kit` row
     * (S090 P1; replaces preset_loadInstrumentMorphTemp(), its `.hctmp` read
     * and the former staged Morph-to-Morph commit).
     *
     * Same refusals as the normal restore: no Scene 17 destination and no
     * restore into a type other than the captured one.
     */
    if (destination_scene >= SCENE_COUNT ||
        destination_slot >= INSTRUMENT_SLOT_COUNT ||
        type >= INSTRUMENT_TYPE_UNKNOWN ||
        pm_instrument_snapshot.type != (uint8_t)type)
        return 0u;
    destination = scene_instrumentSlot(destination_scene, destination_slot);
    entry = instrumentManager_registryEntry(type);
    if (!destination || !entry)
        return 0u;
    /*
     * Same entry sequence as preset_startInstrumentMorphApply(): stop any
     * previous worker, make sure the Morph engine is initialised, copy, then
     * run the shared commit tail.
     */
    instrument_apply_active = 0u;
    preset_ensureMorphInitialized();
    /*
     * Copy only Morphable Morph cells the former file carried, and only when
     * the resident slot still holds the captured type (a mismatch is a
     * complete no-change, as the staged Morph-to-Morph commit was).
     */
    if (destination->type == type) {
        for (i = 0u; i < entry->descriptor_count && i < INSTRUMENT_PARAM_COUNT; i++) {
            if (!preset_snapshotCellRestored(&entry->descriptors[i], 1u))
                continue;
            destination->parameter_images.morph_instrument_parameters[i] =
                pm_instrument_snapshot.morph[i];
            copied = 1u;
        }
    }
    preset_armInstrumentMorphCommit(destination_scene, destination_slot, copied);
    return 1u;
}
```

## 4. `Core/Hardware/SD/filesystem.h`

Public declarations for S4 and S5; removal of the temp API.

#### FH-1 — ADD before line 854

S5. Declare the browse-read API (running / cancel / was-cancelled).

Anchor (line 854, unchanged): `/*`

New block:

```c
/*
 * Menu `.hcindex` read state and disposal (S090 P1, item 7: type steps never
 * wait for a list).
 *
 * filesystem_browseReadRunning(): nonzero while the facade runs a typed
 * Instrument (FS_INTERNAL_OP_LOAD_INDEX) or numbered-library
 * (FS_INTERNAL_OP_LOAD_LIBRARY_INDEX) index read. Menu admits type steps only
 * then.
 * filesystem_cancelBrowseRead(): asks that read to stop at its next phase with
 * no callback outstanding; it closes its handle, returns to root, clears the
 * browser cache and finishes ERROR. A rebuild that the read handed off to is a
 * separate operation and is never cancelled. No-op when no index read runs.
 * filesystem_browseReadWasCancelled(): valid in the completion callback before
 * filesystem_ack(); nonzero when the read ended through that cancel.
 * Accessors: Menu's type-row handlers, the encoder gate, and
 * menu_instrumentIndexLoadComplete().
 */
uint8_t filesystem_browseReadRunning(void);
void filesystem_cancelBrowseRead(void);
uint8_t filesystem_browseReadWasCancelled(void);
```

#### FH-2 — ADD before line 1214

S4. Declare `filesystem_instrumentNameCacheLoaded()`.

Anchor (line 1214, unchanged): `/* Dispose the single shared Instrument/Kit/Scene/Bank/Pattern browser cache or its`

New block:

```c
/*
 * True when the shared cache holds a completed index of this Instrument type
 * and the facade is idle (S090 P1, 6a-i). Lets Menu reuse the index on a
 * same-type VOICE change or Morph toggle instead of reading it again.
 */
uint8_t filesystem_instrumentNameCacheLoaded(instrument_type_t type);
```

#### FH-3 — REMOVE lines 1331–1370

S1. Remove the temp-origin getters and the four temp request prototypes.

First original line (1331): `/*`  
Last original line (1370): `fs_completion_cb_t cb);`

Removed text:

```c
/*
 * Report whether the just-completed staged Instrument came from `.hctmp`.
 *
 * Inputs: none; valid only in the completion window after the successful
 * Instrument callback and before any later filesystem request starts. Output:
 * nonzero exactly for the hidden reversible `kit` source captured by
 * filesystem_requestLoadInstrumentTemp(). Why: Menu must distinguish a root
 * pool replacement, which changes retained Instrument owners and marks
 * AutoSave, from a temporary rollback without inferring it from the mutable
 * browser cursor or its separate UI-operation latch. Affiliate: Menu's
 * PRESET_OP_INSTRUMENT_LOAD completion handling.
 */
uint8_t filesystem_loadedInstrumentWasTemporary(void);
/* True only when the completed hidden Instrument load is the Morph-only
 * reversible `kit` baseline used by InstrumentMrp. */
uint8_t filesystem_loadedInstrumentWasMorphTemporary(void);
/*
 * Save/load the reversible normal Instrument Load `kit` source.
 *
 * Inputs: one Scene/voice and its typed family. Output: the normal Instrument
 * serializer/parser writes or reads the hidden `.hctmp.<ext>` inside that
 * family directory. The temporary operation never updates HCNAMES or the
 * typed `.hcindex`; Menu owns the one nine-byte original label and invalidates
 * it on voice/type/mode/exit boundaries. Affiliates: presetManager wrappers,
 * Menu's direct `kit` row, and filesystem_recordInstrumentFile() exclusion.
 */
bool filesystem_requestSaveInstrumentTemp(uint8_t source_scene,
                                          uint8_t source_slot,
                                          fs_completion_cb_t cb);
bool filesystem_requestSaveInstrumentMorphTemp(uint8_t source_scene,
                                               uint8_t source_slot,
                                               fs_completion_cb_t cb);
bool filesystem_requestLoadInstrumentTemp(uint8_t destination_scene,
                                          uint8_t destination_slot,
                                          instrument_type_t type,
                                          fs_completion_cb_t cb);
bool filesystem_requestLoadInstrumentMorphTemp(uint8_t destination_scene,
                                               uint8_t destination_slot,
                                               instrument_type_t type,
                                               fs_completion_cb_t cb);
```

## 5. `Core/Hardware/SD/filesystem.c`

S1 removal of the temp file path; S2 mirror-served `.hcnames` reads; S4 cache-hit accessor; S5 cancellable index reads. RAM: -2 B (two temp flags); the cancel bit reuses the op-scoped `op_bank_flags`.

#### F-01 — MODIFY lines 409–410

S1. Keep `FS_INTERNAL_OP_SAVE_INSTRUMENT_TEMP` as a retired value: the decoder maps internal ops by declaration order (`decode_devlogs.py` `FS_INTERNAL_OPS`).

First original line (409): `` /* Temporary `kit` row file; it never updates HCNAMES or `.hcindex`. */ ``  
Last original line (410): `FS_INTERNAL_OP_SAVE_INSTRUMENT_TEMP,`

Original:

```c
    /* Temporary `kit` row file; it never updates HCNAMES or `.hcindex`. */
    FS_INTERNAL_OP_SAVE_INSTRUMENT_TEMP,
```

Replacement:

```c
    /*
     * Retired in S090 P1 (the `.hctmp` `kit` file is now a RAM snapshot in
     * Preset). The value is kept, never started and never dispatched, so the
     * numbering of every later operation stays what
     * tools/diagnostic_tools/decode_devlogs.py FS_INTERNAL_OPS expects
     * (declaration order) and trace records need no decoder change.
     */
    FS_INTERNAL_OP_SAVE_INSTRUMENT_TEMP,
```

#### F-02 — MODIFY lines 1041–1042

S1. Comment only (staging union).

First original line (1041): `` /* One parsed Instrument candidate; its original `kit` source is now an ``  
Last original line (1042): `` * on-card `.hctmp.<ext>` file, so staging never owns a second image. */ ``

Original:

```c
    /* One parsed Instrument candidate; its original `kit` source is now an
     * on-card `.hctmp.<ext>` file, so staging never owns a second image. */
```

Replacement:

```c
    /* One parsed Instrument candidate; the reversible `kit` source is Preset's
     * RAM snapshot (S090 P1), so staging never owns a second image. */
```

#### F-03 — ADD before line 1455

S5. Name the op-scoped cancel bit for index reads (no RAM).

Anchor (line 1455, unchanged): `/*`

New block:

```c
/*
 * Cancel request for a running Menu `.hcindex` read (S090 P1, item 7).
 *
 * What: the same op-scoped bit as FS_BANK_FLAG_PREVIEW_CANCEL, named for its
 * second user. filesystem_cancelBrowseRead() sets it only while
 * FS_INTERNAL_OP_LOAD_INDEX or FS_INTERNAL_OP_LOAD_LIBRARY_INDEX runs; those
 * readers then stop at their next phase with no callback outstanding, close
 * any open handle, return to root, clear the browser cache and finish ERROR.
 * Why: a Load/Save type step disposes the list read for the type it left, so
 * only the type the user settles on is read. op_bank_flags is the per-
 * operation flag byte that filesystem_start() clears; index reads never use
 * any Bank bit, so sharing the bit costs no RAM and cannot leak into another
 * operation (a rebuild started from an index read is a new operation and
 * starts with the bit clear, so it can never be cancelled).
 * Accessors: filesystem_cancelBrowseRead(), filesystem_browseCancelRequested(),
 * filesystem_browseReadWasCancelled().
 */
#define FS_OP_FLAG_BROWSE_CANCEL     FS_BANK_FLAG_PREVIEW_CANCEL
```

#### F-04 — REMOVE lines 1863–1867

S1. Remove `FS_INSTRUMENT_LOAD_TEMP_*` and `op_instrument_load_temporary` (-1 B).

First original line (1863): `/* Hidden reversible-load origin: ordinary temp, or Morph-only temp snapshot. */`  
Last original line (1867): `static uint8_t op_instrument_load_temporary = 0u;`

Removed text:

```c
/* Hidden reversible-load origin: ordinary temp, or Morph-only temp snapshot. */
#define FS_INSTRUMENT_LOAD_TEMP_NONE  0u
#define FS_INSTRUMENT_LOAD_TEMP_NORMAL 1u
#define FS_INSTRUMENT_LOAD_TEMP_MORPH  2u
static uint8_t op_instrument_load_temporary = 0u;
```

#### F-05 — REMOVE lines 1906–1907

S1. Remove `op_instrument_save_temporary` (-1 B).

First original line (1906): `/* True only for the hidden reversible-load save; skip cache/name publication. */`  
Last original line (1907): `static uint8_t op_instrument_save_temporary = 0u;`

Removed text:

```c
/* True only for the hidden reversible-load save; skip cache/name publication. */
static uint8_t op_instrument_save_temporary = 0u;
```

#### F-06 — ADD before line 4857

S5. Add the two static cancel helpers ahead of the Instrument index reader.

Anchor (line 4857, unchanged): `/*`

New block:

```c
/*
 * Report whether the running index read has been asked to stop (S090 item 7).
 *
 * Inputs: FS_OP_FLAG_BROWSE_CANCEL in the op-scoped flags. Output: nonzero once
 * filesystem_cancelBrowseRead() has run for this operation. Callers: the
 * cancel checkpoints in filesystem_loadInstrumentIndex_tick() and
 * filesystem_loadLibraryIndex_tick().
 */
static uint8_t filesystem_browseCancelRequested(void)
{
    return (uint8_t)((op_bank_flags & FS_OP_FLAG_BROWSE_CANCEL) != 0u);
}

/*
 * End a cancelled index read (S090 item 7).
 *
 * What: clears the browser cache (its rows are partial and its tag names the
 * type that was left) and finishes the operation ERROR through the normal
 * completion, so the Menu callback runs exactly once.
 * Why: a cancelled read must never leave a domain tag that a reader or the
 * S090 cache hit could trust. ERROR (not DONE) skips the final flush, which is
 * correct for a read-only operation.
 * Preconditions: no file handle is open, no callback is outstanding and the
 * current directory is root (each caller reaches this only from such a phase).
 * Callers: the cancel phases of the two index-read state machines.
 * Affiliates: filesystem_browseReadWasCancelled(), Menu's
 * menu_instrumentIndexLoadComplete() / menu_libraryIndexLoadComplete().
 */
static void filesystem_finishCancelledBrowseRead(void)
{
    filesystem_clearNameCacheStorage();
    filesystem_finish(FS_STATUS_ERROR);
}
```

#### F-07 — MODIFY line 4900

S5. Instrument index read, cancel point at phase 1.

First original line (4900): `case 1: /* OPEN Instrument/ */`  
Last original line (4900): `case 1: /* OPEN Instrument/ */`

Original:

```c
    case 1: /* OPEN Instrument/ */
```

Replacement:

```c
    case 1: /* OPEN Instrument/ */
        /* S090 item 7 cancel point: root, no handle, nothing outstanding. */
        if (filesystem_browseCancelRequested()) {
            filesystem_finishCancelledBrowseRead();
            return;
        }
```

#### F-08 — MODIFY line 4947

S5. Instrument index read, cancel point at phase 6.

First original line (4947): `case 6: /* OPEN the registry-defined type directory */`  
Last original line (4947): `case 6: /* OPEN the registry-defined type directory */`

Original:

```c
    case 6: /* OPEN the registry-defined type directory */
```

Replacement:

```c
    case 6: /* OPEN the registry-defined type directory */
        /*
         * S090 item 7 cancel point: Instrument/ is the current directory and
         * its handle is closed; phase 25 returns to root and ends the read.
         */
        if (filesystem_browseCancelRequested()) {
            op_phase = 25u;
            return;
        }
```

#### F-09 — MODIFY line 5020

S5. Instrument index read, cancel point in phase 10 after the type-directory close.

First original line (5020): `op_kit_root_dir = NULL;`  
Last original line (5020): `op_kit_root_dir = NULL;`

Original:

```c
        op_kit_root_dir = NULL;
```

Replacement:

```c
        op_kit_root_dir = NULL;
        /*
         * S090 item 7 cancel point: the type directory is current and its
         * handle is closed; skip the `.hcindex` open and end through phase 25.
         */
        if (filesystem_browseCancelRequested()) {
            op_phase = 25u;
            return;
        }
```

#### F-10 — MODIFY line 5113

S5. Instrument index read, cancel point at the top of the row loop (phase 17).

First original line (5113): `{`  
Last original line (5113): `{`

Original:

```c
    {
```

Replacement:

```c
    {
        /*
         * S090 item 7 cancel point: `.hcindex` is open and no read is
         * outstanding between polls; close it through phases 26-27, then 25.
         */
        if (filesystem_browseCancelRequested()) {
            op_phase = 26u;
            return;
        }
```

#### F-11 — ADD before line 5240

S5. Instrument index read, new cancel phases 25-27.

Anchor (line 5240, unchanged): `default:`

New block:

```c
    case 25: /* S090 item 7: CANCELLED - RETURN ROOT + ERROR */
        /*
         * Entered only from a cancel point with no handle open. A fatal volume
         * cannot chdir; end at once, as phase 16 does, rather than wait.
         */
        if (!afatfs_chdir(NULL) &&
            !filesystem_instrumentIndexFATFatal())
            return;
        filesystem_finishCancelledBrowseRead();
        return;

    case 26: /* S090 item 7: CANCELLED - CLOSE the open `.hcindex` */
        op_close_done = false;
        if (afatfs_fclose(op_file, on_file_closed))
            op_phase = 27u;
        else if (filesystem_instrumentIndexFATFatal())
            filesystem_finishCancelledBrowseRead();
        return;

    case 27: /* S090 item 7: CANCELLED - WAIT CLOSE, then phase 25 */
        if (!op_close_done) {
            if (filesystem_instrumentIndexFATFatal())
                filesystem_finishCancelledBrowseRead();
            return;
        }
        op_file = NULL;
        op_phase = 25u;
        return;
```

#### F-12 — MODIFY lines 7073–7075

S2. `.hcnames` load from a VALID mirror finishes at phase 0.

First original line (7073): `case 0: /* RETURN ROOT + OPEN EXISTING REGISTER */`  
Last original line (7075): `return;`

Original:

```c
    case 0: /* RETURN ROOT + OPEN EXISTING REGISTER */
        if (!afatfs_chdir(NULL))
            return;
```

Replacement:

```c
    case 0: /* RETURN ROOT + OPEN EXISTING REGISTER */
        /*
         * Serve a Menu read from a VALID mirror (S090 P1, Q1).
         *
         * What: a load-mode operation whose request left the mirror VALID
         * finishes DONE here without opening `/.hcnames`; the completion
         * callback then copies its rows from hcnames_name_mirror exactly as
         * after a card read.
         * Why: the read took 2.7 s while playing (S090 trace). VALID is set
         * only after a complete read or after a writer's final sync, and every
         * writer marks the mirror INVALID before it touches a row, so a VALID
         * mirror equals the card. Menu also writes its own dirty rows before
         * this read (menu_endResidentNameScratchSession()).
         * Only filesystem_requestLoadResidentKitName() and
         * filesystem_requestLoadResidentSceneName() leave the mirror VALID
         * (they skip the reset when it is VALID); every other load or update
         * request resets it first, so it reaches the card read below unchanged.
         * Side effect (accepted, S090 §10 Q1): fs_resident_source[] is not
         * reloaded from the card on this path; it keeps its RAM state.
         * Affiliates: filesystem_prepareResidentNamesCache(),
         * menu_residentNameScratchLoaded(), menu_sceneResidentNameLoaded().
         */
        if (!update && hcnames_mirror_valid == FS_HCNAMES_MIRROR_VALID) {
            filesystem_finish(FS_STATUS_DONE);
            return;
        }
        if (!afatfs_chdir(NULL))
            return;
```

#### F-13 — MODIFY line 10428

S5. Library index read, cancel point at phase 1.

First original line (10428): `case 1: /* OPEN ROOT DIRECTORY */`  
Last original line (10428): `case 1: /* OPEN ROOT DIRECTORY */`

Original:

```c
    case 1: /* OPEN ROOT DIRECTORY */
```

Replacement:

```c
    case 1: /* OPEN ROOT DIRECTORY */
        /* S090 item 7 cancel point: root, no handle, nothing outstanding. */
        if (filesystem_browseCancelRequested()) {
            filesystem_finishCancelledBrowseRead();
            return;
        }
```

#### F-14 — MODIFY line 10465

S5. Library index read, cancel point in phase 5 after the root-directory close.

First original line (10465): `op_kit_root_dir = NULL;`  
Last original line (10465): `op_kit_root_dir = NULL;`

Original:

```c
        op_kit_root_dir = NULL;
```

Replacement:

```c
        op_kit_root_dir = NULL;
        /*
         * S090 item 7 cancel point: the library directory is current and its
         * handle is closed; skip the `.hcindex` open and end through phase 9.
         */
        if (filesystem_browseCancelRequested()) {
            op_phase = 9u;
            return;
        }
```

#### F-15 — MODIFY line 10490

S5. Library index read, cancel point at the top of the row loop (phase 7).

First original line (10490): `{`  
Last original line (10490): `{`

Original:

```c
    {
```

Replacement:

```c
    {
        /*
         * S090 item 7 cancel point: `.hcindex` is open and no read is
         * outstanding between polls; close it, then phase 10 and 9.
         */
        if (filesystem_browseCancelRequested()) {
            op_close_done = false;
            if (afatfs_fclose(op_file, on_file_closed))
                op_phase = 10u;
            return;
        }
```

#### F-16 — ADD before line 10532

S5. Library index read, new cancel phases 9-10.

Anchor (line 10532, unchanged): `default:`

New block:

```c
    case 9: /* S090 item 7: CANCELLED - RETURN ROOT + ERROR */
        if (!afatfs_chdir(NULL))
            return;
        filesystem_finishCancelledBrowseRead();
        return;

    case 10: /* S090 item 7: CANCELLED - WAIT CLOSE, then phase 9 */
        if (!op_close_done)
            return;
        op_file = NULL;
        op_phase = 9u;
        return;
```

#### F-17 — MODIFY lines 11180–11182

S1. Comment only: the reserved-name filter stays to hide stale `.hctmp` files.

First original line (11180): `* filesystem state changes. Affiliates: filesystem_makeInstrumentTemporaryFilename(),`  
Last original line (11182): `* filesystem_requestSave/LoadInstrumentTemp().`

Original:

```c
 * filesystem state changes. Affiliates: filesystem_makeInstrumentTemporaryFilename(),
 * filesystem_recordInstrumentFile(), the one-type Instrument scanner, and
 * filesystem_requestSave/LoadInstrumentTemp().
```

Replacement:

```c
 * filesystem state changes. Affiliates: filesystem_makeInstrumentTemporaryFilename(),
 * filesystem_recordInstrumentFile() and the one-type Instrument scanner.
 * S090 P1: firmware no longer writes this object (the `kit` source is a RAM
 * snapshot), but the test stays so `.hctmp.<ext>` files left on cards by older
 * firmware remain invisible; they are safe to delete.
```

#### F-18 — MODIFY lines 17097–17122

S1. Instrument load: drop the verbatim `.hctmp` open branch.

First original line (17097): `/*`  
Last original line (17122): `}`

Replacement:

```c
            /*
             * Derive the one immediate LFN open key after staging has claimed
             * the separate typed stage.
             *
             * A captured pool display stem plus type is formatted into its
             * `<stem>.<ext>` leaf. (S090 P1: the verbatim `.hctmp.<ext>` branch
             * is gone with the temp file.) Output: `lfn` is the exact selected
             * pool leaf for the following asyncfatfs open. Affiliate:
             * filesystem_requestLoadInstrument().
             */
            storage_makeSavedInstrumentDisplayFilename(
                lfn, sizeof(lfn), display_name,
                op_instrument_load_type, 0u, 0u);
```

#### F-19 — MODIFY lines 17182–17192

S1. Instrument load: the identity row is always published (the `if (!op_instrument_load_temporary)` becomes a plain block so its local stays scoped).

First original line (17182): `/*`  
Last original line (17192): `if (!op_instrument_load_temporary) {`

Original:

```c
                /*
                 * Publish a normal pool load name, never the hidden temp name.
                 *
                 * Inputs: validated selected filename and request-time hidden
                 * temporary flag. Output: ordinary pool loads update one
                 * identity row immediately; `.hctmp.<ext>` restores parameters
                 * only, leaving Menu's separate nine-byte `kit` label intact.
                 * Affiliates: filesystem_requestLoadInstrumentTemp(), Menu's
                 * session invalidation, and the exit-time HCNAMES writer.
                 */
                if (!op_instrument_load_temporary) {
```

Replacement:

```c
                /*
                 * Publish the pool load name.
                 *
                 * Inputs: validated selected filename. Output: the pool load
                 * updates one identity row immediately. (S090 P1: every load
                 * here is a pool load; the hidden temp restore that skipped
                 * this is gone.) Affiliates: Menu's session invalidation and the
                 * exit-time HCNAMES writer.
                 */
                {
```

#### F-20 — MODIFY lines 17231–17236

S1. Instrument load: the refreshed witness is always set on DONE.

First original line (17231): `if (op_close_status == FS_STATUS_DONE &&`  
Last original line (17236): `*/`

Original:

```c
        if (op_close_status == FS_STATUS_DONE &&
            !op_instrument_load_temporary) {
            /* The normal Instrument payload is now past its close boundary
             * and is about to be handed to the completion callback. Morph and
             * hidden temporary restores deliberately remain identity-neutral.
             */
```

Replacement:

```c
        if (op_close_status == FS_STATUS_DONE) {
            /* The normal Instrument payload is now past its close boundary
             * and is about to be handed to the completion callback. */
```

#### F-21 — REMOVE lines 17581–17593

S1. Instrument save phase 21: remove the temp early finish.

First original line (17581): `/*`  
Last original line (17593): `}`

Removed text:

```c
        /*
         * Finish a hidden `kit` snapshot without publishing library state.
         *
         * Inputs: `.hctmp.<ext>` has been written in the current type folder.
         * Output: normal filesystem completion only. Why: the temporary file
         * must not rename the live Instrument identity or alter `.hcindex`;
         * it is excluded from future scans and is read only by the direct
         * `kit` row. Affiliates: Menu entry sequencing and temp load request.
         */
        if (op_instrument_save_temporary) {
            filesystem_finish(FS_STATUS_DONE);
            return;
        }
```

#### F-22 — MODIFY lines 17756–17757

S1. Instrument save content CRC: drop the temp condition.

First original line (17756): `if (current_op == FS_INTERNAL_OP_SAVE_INSTRUMENT &&`  
Last original line (17757): `!op_instrument_save_temporary) {`

Original:

```c
    if (current_op == FS_INTERNAL_OP_SAVE_INSTRUMENT &&
        !op_instrument_save_temporary) {
```

Replacement:

```c
    if (current_op == FS_INTERNAL_OP_SAVE_INSTRUMENT) {
```

#### F-23 — REMOVE line 27965

S1. Dispatcher: the retired op is never ticked.

First original line (27965): `case FS_INTERNAL_OP_SAVE_INSTRUMENT_TEMP:`  
Last original line (27965): `case FS_INTERNAL_OP_SAVE_INSTRUMENT_TEMP:`

Removed text:

```c
    case FS_INTERNAL_OP_SAVE_INSTRUMENT_TEMP:
```

#### F-24 — REMOVE lines 28295–28297

S1. `filesystem_start()`: remove the two temp-flag resets.

First original line (28295): `/* A new request is ordinary until its dedicated temp request opts in. */`  
Last original line (28297): `op_instrument_save_temporary = 0u;`

Removed text:

```c
    /* A new request is ordinary until its dedicated temp request opts in. */
    op_instrument_load_temporary = 0u;
    op_instrument_save_temporary = 0u;
```

#### F-33 — ADD before line 32964

S5. Public browse-read API definitions.

Anchor (line 32964, unchanged): `(blank)`

New block:

```c

/*
 * Menu `.hcindex` read state and disposal (S090 P1, item 7; contract in
 * filesystem.h).
 *
 * filesystem_browseReadRunning(): the facade is BUSY with a Menu-requested
 * index read (typed Instrument or numbered library). Those two operations
 * are read-only until they hand off to a rebuild, which is a new operation.
 */
uint8_t filesystem_browseReadRunning(void)
{
    return (uint8_t)(status == FS_STATUS_BUSY &&
                     (current_op == FS_INTERNAL_OP_LOAD_INDEX ||
                      current_op == FS_INTERNAL_OP_LOAD_LIBRARY_INDEX));
}

/*
 * filesystem_cancelBrowseRead(): ask a running index read to stop at its next
 * phase with no callback outstanding. No-op when none runs, so it is safe on
 * every type step. Callers: Menu's type-row handlers.
 */
void filesystem_cancelBrowseRead(void)
{
    if (filesystem_browseReadRunning())
        op_bank_flags = (uint8_t)(op_bank_flags | FS_OP_FLAG_BROWSE_CANCEL);
}

/*
 * filesystem_browseReadWasCancelled(): in the completion window (before
 * filesystem_ack()), nonzero when the finished read had been asked to stop
 * and ended ERROR. A read that still finished DONE reports zero and is judged
 * by its caller's own staleness check. Callers: the Menu index completions.
 */
uint8_t filesystem_browseReadWasCancelled(void)
{
    return (uint8_t)(filesystem_browseCancelRequested() &&
                     status == FS_STATUS_ERROR);
}
```

#### F-31 — MODIFY lines 33097–33098

S2. `filesystem_requestLoadResidentKitName()`: keep a VALID mirror.

First original line (33097): `/* S082 review F9: reset the register only after the request is accepted. */`  
Last original line (33098): `filesystem_prepareResidentNamesCache();`

Original:

```c
    /* S082 review F9: reset the register only after the request is accepted. */
    filesystem_prepareResidentNamesCache();
```

Replacement:

```c
    /*
     * S082 review F9: reset the register only after the request is accepted.
     *
     * S090 P1 (Q1): a VALID mirror already equals the card, so it is not
     * reset; filesystem_residentNames_tick() phase 0 then finishes this read
     * from the mirror with no card access. An INVALID or PUBLISH_PENDING
     * mirror is reset and read from the card exactly as before.
     */
    if (hcnames_mirror_valid != FS_HCNAMES_MIRROR_VALID)
        filesystem_prepareResidentNamesCache();
```

#### F-32 — MODIFY lines 33172–33173

S2. `filesystem_requestLoadResidentSceneName()`: keep a VALID mirror.

First original line (33172): `/* S082 review F9: reset the register only after the request is accepted. */`  
Last original line (33173): `filesystem_prepareResidentNamesCache();`

Original:

```c
    /* S082 review F9: reset the register only after the request is accepted. */
    filesystem_prepareResidentNamesCache();
```

Replacement:

```c
    /*
     * S082 review F9: reset the register only after the request is accepted.
     * S090 P1 (Q1): a VALID mirror is kept and served by
     * filesystem_residentNames_tick() phase 0 (see
     * filesystem_requestLoadResidentKitName()).
     */
    if (hcnames_mirror_valid != FS_HCNAMES_MIRROR_VALID)
        filesystem_prepareResidentNamesCache();
```

#### F-34 — ADD before line 33380

S4. `filesystem_instrumentNameCacheLoaded()` definition.

Anchor (line 33380, unchanged): `(blank)`

New block:

```c

/*
 * Report whether the browser cache holds a completed index of one Instrument
 * type (S090 P1, 6a-i; contract in filesystem.h).
 *
 * What: true when the cache domain is INSTRUMENT, its type is `type`, and the
 * facade is not BUSY.
 * Why: the tag is set when a request starts (filesystem_requestLoadInstrumentIndex(),
 * filesystem_startInstrumentIndexRebuildScan()), so while any operation runs
 * the rows may be partial: an index read, a rebuild scan, or an Instrument
 * Save updating the cache. Refusing on any BUSY facade is the simple,
 * provable rule; a failed or cancelled read clears the tag (Menu completion /
 * filesystem_finishCancelledBrowseRead()), so an idle facade with this tag
 * means the last writer published the whole index.
 * Inputs: type. Output: nonzero when Menu may publish the cached browser
 * without a read. Callers: menu_requestInstrumentIndexLoad(),
 * menu_instrumentIndexLoadComplete().
 */
uint8_t filesystem_instrumentNameCacheLoaded(instrument_type_t type)
{
    return (uint8_t)(type < INSTRUMENT_TYPE_UNKNOWN &&
                     fs_list_cache_kind == FS_NAME_CACHE_INSTRUMENT &&
                     fs_list_cache_type == type &&
                     status != FS_STATUS_BUSY);
}
```

#### F-25 — MODIFY lines 33728–33732

S1. `filesystem_requestSaveInstrumentMode()`: accept only Save/Morph Save.

First original line (33728): `if (op != FS_INTERNAL_OP_SAVE_INSTRUMENT &&`  
Last original line (33732): `}`

Original:

```c
    if (op != FS_INTERNAL_OP_SAVE_INSTRUMENT &&
        op != FS_INTERNAL_OP_SAVE_INSTRUMENT_MORPH &&
        op != FS_INTERNAL_OP_SAVE_INSTRUMENT_TEMP) {
        return false;
    }
```

Replacement:

```c
    if (op != FS_INTERNAL_OP_SAVE_INSTRUMENT &&
        op != FS_INTERNAL_OP_SAVE_INSTRUMENT_MORPH) {
        return false;
    }
```

#### F-26 — MODIFY lines 33737–33746

S1. `filesystem_requestSaveInstrumentMode()`: always format the pool filename.

First original line (33737): `if (op == FS_INTERNAL_OP_SAVE_INSTRUMENT_TEMP) {`  
Last original line (33746): `}`

Original:

```c
    if (op == FS_INTERNAL_OP_SAVE_INSTRUMENT_TEMP) {
        filesystem_makeInstrumentTemporaryFilename(display, instrument->type);
    } else {
        storage_makeSavedInstrumentDisplayFilename(display,
                                                   sizeof(display),
                                                   display_name,
                                                   instrument->type,
                                                   (uint8_t)(source_slot + 1u),
                                                   0u);
    }
```

Replacement:

```c
    storage_makeSavedInstrumentDisplayFilename(display,
                                               sizeof(display),
                                               display_name,
                                               instrument->type,
                                               (uint8_t)(source_slot + 1u),
                                               0u);
```

#### F-27 — REMOVE lines 33783–33785

S1. `filesystem_requestSaveInstrumentMode()`: remove the temp flag.

First original line (33783): `/* This flag is consumed by save phase 21 to suppress cache/name changes. */`  
Last original line (33785): `(uint8_t)(op == FS_INTERNAL_OP_SAVE_INSTRUMENT_TEMP);`

Removed text:

```c
    /* This flag is consumed by save phase 21 to suppress cache/name changes. */
    op_instrument_save_temporary =
        (uint8_t)(op == FS_INTERNAL_OP_SAVE_INSTRUMENT_TEMP);
```

#### F-28 — MODIFY line 33790

S1. `filesystem_requestSaveInstrumentMode()`: the save-request trace record is now unconditional (the block keeps its indentation).

First original line (33790): `if (op != FS_INTERNAL_OP_SAVE_INSTRUMENT_TEMP) {`  
Last original line (33790): `if (op != FS_INTERNAL_OP_SAVE_INSTRUMENT_TEMP) {`

Original:

```c
    if (op != FS_INTERNAL_OP_SAVE_INSTRUMENT_TEMP) {
```

Replacement:

```c
    {
```

#### F-29 — REMOVE lines 33827–33920

S1. Remove `filesystem_requestSaveInstrumentTemp()`, `...SaveInstrumentMorphTemp()`, `...LoadInstrumentTemp()`, `...LoadInstrumentMorphTemp()`.

First original line (33827): `bool filesystem_requestSaveInstrumentTemp(uint8_t source_scene,`  
Last original line (33920): `(blank)`

Removed: 94 lines (the whole span above; see the tree for the text).

#### F-30 — REMOVE lines 33987–34011

S1. Remove `filesystem_loadedInstrumentWasTemporary()` and `filesystem_loadedInstrumentWasMorphTemporary()`.

First original line (33987): `uint8_t filesystem_loadedInstrumentWasTemporary(void)`  
Last original line (34011): `(blank)`

Removed text:

```c
uint8_t filesystem_loadedInstrumentWasTemporary(void)
{
    /*
     * Expose the filesystem-captured origin of the completed Instrument.
     *
     * Inputs: the existing request-local flag, which filesystem_start() resets
     * for every new operation and filesystem_requestLoadInstrumentTemp() alone
     * sets. Output: the exact normal-pool versus `.hctmp` classification remains
     * available beside the validated staging image until a subsequent request
     * reuses operation storage. Why: Menu's temporary save/restore sequencing
     * latch can remain set for UI reasons even when this completed file was a
     * normal root-pool load, so it is not safe AutoSave provenance. This getter
     * copies no payload and allocates no state. Affiliate: Menu completion.
     */
    return op_instrument_load_temporary;
}

uint8_t filesystem_loadedInstrumentWasMorphTemporary(void)
{
    /* The origin flag remains valid beside the staged Instrument until the
     * next filesystem request reuses operation storage. */
    return (uint8_t)(op_instrument_load_temporary ==
                     FS_INSTRUMENT_LOAD_TEMP_MORPH);
}
```

## 6. `Core/Hardware/SD/storageTypes.h` and `storageTypes.c`

S1: the writer projection only the temp file used.

### `Core/Hardware/SD/storageTypes.h`

#### ST-1 — MODIFY lines 518–520

S1. Remove `STORAGE_INSTRUMENT_SAVE_MORPH_SNAPSHOT` (last enumerator; no other value moves).

First original line (518): `STORAGE_INSTRUMENT_SAVE_MORPH,`  
Last original line (520): `STORAGE_INSTRUMENT_SAVE_MORPH_SNAPSHOT`

Original:

```c
    STORAGE_INSTRUMENT_SAVE_MORPH,
    /* Hidden `.hctmp` projection containing only the Morph endpoint image. */
    STORAGE_INSTRUMENT_SAVE_MORPH_SNAPSHOT
```

Replacement:

```c
    STORAGE_INSTRUMENT_SAVE_MORPH
    /* S090 P1: STORAGE_INSTRUMENT_SAVE_MORPH_SNAPSHOT (the hidden `.hctmp`
     * Morph projection) was removed with the temp file. */
```

### `Core/Hardware/SD/storageTypes.c`

#### ST-2 — REMOVE lines 1089–1148

S1. Remove the Morph-snapshot projection branch of `storage_formatInstrumentLineView()` (only the temp file used it).

First original line (1089): `if (view->mode == STORAGE_INSTRUMENT_SAVE_MORPH_SNAPSHOT) {`  
Last original line (1148): `(blank)`

Removed: 60 lines (the whole span above; see the tree for the text).

## 7. `Core/Menu/menu.c`

S1 snapshot capture/restore; S4 index reuse; S5 type steps over a running read.

#### M-01 — MODIFY lines 1806–1814

S1. Comment only: `menu_instrumentTempName`.

First original line (1806): `/*`  
Last original line (1814): `*/`

Original:

```c
/*
 * One Menu-owned identity for the hidden reversible `kit` file.
 *
 * Inputs: copied once from the existing HCNAMES identity row after Instrument
 * Load entry succeeds. Output: the `kit` row label remains stable while pool
 * loads update the normal identity row. Why: `.hctmp.<ext>` carries parameters
 * on SD, but must never become another name cache or HCNAMES row. Affiliates:
 * preset_save/loadInstrumentTemp() and all voice/type/exit invalidations.
 */
```

Replacement:

```c
/*
 * One Menu-owned identity for the reversible `kit` row.
 *
 * Inputs: copied once from the existing HCNAMES identity row when nested
 * Instrument Load entry captures its snapshot. Output: the `kit` row label
 * remains stable while pool loads update the normal identity row. Why: since
 * S090 the parameters live in Preset's RAM snapshot (pm_instrument_snapshot),
 * which holds no name; this field must never become another name cache or
 * HCNAMES row. Affiliates: preset_captureInstrumentSnapshot(),
 * preset_restoreInstrumentSnapshot() and all voice/type/exit invalidations.
 */
```

#### M-02 — MODIFY lines 1818–1831

S1. Comment only: `menu_instrumentTempOperationPending` (meaning narrowed to "a snapshot restore is applying").

First original line (1818): `/*`  
Last original line (1831): `*/`

Original:

```c
/*
 * One operation tag shared by the temporary save and its later temporary load.
 *
 * Inputs: Menu posts either the entry `.hctmp.<ext>` save or the direct `kit`
 * restore. Output: completion/application code can identify that accepted
 * operation without consulting the mutable encoder cursor. Why: Load-number
 * turns are deliberately accepted while a pool Instrument operation drains;
 * `menu_instrumentLoadSource` may therefore say `kit` when that *pool* load
 * completes. This latch sequences temporary save/restore UI work only; it
 * must not classify AutoSave provenance, which comes from filesystem's
 * immutable request flag. Affiliates: menu_prepareInstrumentLoadTemp(),
 * menu_restoreInstrumentLoadTemp(), menu_tickInstrumentApply(), and
 * PRESET_OP_INSTRUMENT_TEMP_SAVE / PRESET_OP_INSTRUMENT_LOAD handling.
 */
```

Replacement:

```c
/*
 * "A snapshot `kit` restore is applying" (S090 P1: meaning narrowed).
 *
 * Inputs: set by menu_restoreInstrumentLoadTemp() when it commits the RAM
 * snapshot and arms the bounded Instrument apply. Output:
 * menu_finishInstrumentApplySession() reads it once when that apply drains,
 * so it can tell the completed restore from a completed pool load without
 * consulting the mutable encoder cursor (Load-number turns stay accepted while
 * an apply drains, so `menu_instrumentLoadSource` may say `kit` when a *pool*
 * apply completes). Before S090 it also tagged the asynchronous `.hctmp`
 * save and load; both are gone. It never classifies AutoSave provenance.
 * Affiliates: menu_restoreInstrumentLoadTemp(), menu_tickInstrumentApply(),
 * menu_finishInstrumentApplySession(), menu_invalidateInstrumentLoadTemp().
 */
```

#### M-03 — MODIFY lines 1842–1859

S1. Comment only: `menu_instrumentKitRestorePending`.

First original line (1842): `* Inputs: set when a pool preview (normal or Morph) commits over the browsed`  
Last original line (1859): `* single call's outcome closes every one of those drop paths with one rule.`

Replacement:

```c
 * Inputs: set when a pool preview (normal or Morph) commits over the browsed
 * slot; cleared when a snapshot restore is committed for that slot (S090:
 * menu_restoreInstrumentLoadTemp() clears it as it commits), and at every
 * session boundary through menu_invalidateInstrumentLoadTemp(). Output: three
 * independent places consult it — the pool-to-`kit` crossing, the "already on
 * `kit`" repeat-turn branch, and the idle retry at the top of
 * menu_pollPresetStatus() — so the restore lands whether the user keeps
 * scrolling or stops dead on `kit`.
 *
 * Why it exists: the crossing handler latches menu_instrumentLoadSource to
 * MENU_INSTRUMENT_SOURCE_KIT *before* attempting the restore. If that attempt
 * cannot run — a pool preview the user just scrolled through is still
 * loading or applying, or another apply worker is active (S090: the RAM
 * restore waits for the same idle point the file restore's completion did) —
 * the UI is already "on `kit`" while the parameters were never restored, and
 * every subsequent detent hits the idempotent no-op branch. Playback made this
 * the common case, because a sounding voice lengthens the preview's apply and
 * widens the busy window the crossing lands in. Tracking owed-ness instead of a
 * single call's outcome closes every one of those drop paths with one rule.
```

#### M-04 — MODIFY lines 7592–7680

S4/S5. Split the Instrument index completion into `menu_instrumentIndexReady()` (shared with the cache hit) and the completion proper, which retires a cancelled or stale read quietly; the coalesced pool post is limited to a pool cursor on the number row.

First original line (7592): `static void menu_instrumentIndexLoadComplete(void)`  
Last original line (7680): `}`

Replacement:

```c
/*
 * Publish a ready typed Instrument index to the nested browser (S090 P1).
 *
 * What: clamps the remembered row of the loaded type, mirrors it into the
 * shown row for a pool cursor, releases the input gate and either posts the
 * one coalesced pool selection or repaints.
 * Why: two paths now reach a ready index: a completed `.hcindex` read and
 * the S090 cache hit in menu_requestInstrumentIndexLoad() (a same-type voice
 * change reuses the index already in the browser cache). Both must leave the
 * browser in exactly the same state, so they share this tail.
 * Inputs: menu_instrumentLoadType/Source, menu_saveOptions.state,
 * menu_deferSelectionRequest; the browser cache must hold the loaded type
 * (checked by both callers). Outputs: menu_instrumentLoadShownIndex,
 * menu_storageBusy (cleared, or re-raised by an accepted selection), LCD.
 * Callers: menu_instrumentIndexLoadComplete(), menu_requestInstrumentIndexLoad().
 * Affiliates: menu_instrumentLoadClampIndex(),
 * menu_instrumentLoadRequestSelection(), the deferred dispatcher in
 * menu_pollPresetStatus().
 */
static void menu_instrumentIndexReady(void)
{
    menu_instrumentLoadClampIndex();
    if (menu_instrumentLoadActive &&
        !menu_instrumentSaveMode &&
        menu_instrumentLoadSource == MENU_INSTRUMENT_SOURCE_POOL) {
        menu_instrumentLoadShownIndex =
            menu_instrumentLoadIndex[menu_instrumentLoadType];
    }
    menu_storageBusy = 0u;
    /*
     * Start only the newest Instrument number after the index is ready.
     *
     * Keeping this beside the index ensures the newest number is paired with
     * its freshly loaded `.hcindex` name before
     * menu_instrumentLoadRequestSelection() posts the payload. That helper
     * clears the defer flag and reasserts storage busy if Preset accepts the
     * coalesced request.
     *
     * S090 P1: the post is limited to a pool cursor on the number row, the
     * same condition the deferred dispatcher in menu_pollPresetStatus() uses.
     * Before S090 a defer could only have been raised here by a Load-number
     * turn (which always leaves that state), so this is unchanged for every
     * earlier path. Type steps are now accepted while an index read runs
     * (S090 item 7), and their refused requests also raise the defer flag
     * while the cursor rests on the type row or on `kit`; posting a pool load
     * then would replace the voice with a row the user never selected. Such a
     * defer is left for the dispatcher, which re-enters
     * menu_requestInstrumentEntryNames() and lands on the cache hit.
     */
    if (menu_deferSelectionRequest &&
        menu_instrumentLoadActive &&
        !menu_instrumentSaveMode &&
        menu_instrumentLoadSource == MENU_INSTRUMENT_SOURCE_POOL &&
        menu_saveOptions.state == SAVE_STATE_EDIT_PRESET_NR) {
        menu_instrumentLoadRequestSelection();
        if (menu_storageBusy)
            return;
    }
    menu_repaintAll();
}

static void menu_instrumentIndexLoadComplete(void)
{
    /*
     * Complete one typed Instrument index request, including transparent
     * rebuild.
     *
     * What: snapshots the terminal filesystem result before changing Menu
     * state, releases the direct filesystem facade, and publishes the
     * rebuilt/read cache only on success. Failure discards the unusable cache,
     * cancels deferred payload selection, releases Menu input, and opens the
     * existing filesystem error overlay.
     *
     * Why: this callback bypasses Preset's normal acknowledgement owner.
     * Leaving DONE/ERROR parked blocks the idle-only AutoSave and trace
     * schedulers, while the previous unconditional clamp/deferred path could
     * attempt an Instrument load from a partial or empty cache after
     * `.hcindex` failed.
     *
     * S090 P1 (item 7): a type step may now arrive while this read runs. Two
     * outcomes are therefore not the user's current request and are retired
     * quietly instead of being published or reported:
     * - the read was cancelled by that step (filesystem_cancelBrowseRead()),
     *   which ends it ERROR with the cache already cleared;
     * - the read finished DONE before the cancel took effect, but for a type
     *   that is no longer the selected one.
     * Both clear the cache, release input and leave one deferred retry, which
     * the dispatcher in menu_pollPresetStatus() serves for the type on screen.
     *
     * Inputs: the terminal filesystem result (read before filesystem_ack()),
     * the browse-read cancel witness, current nested Load/Save mode, selected
     * voice/type/source, and menu_deferSelectionRequest. Outputs: on
     * success, one safe typed browser and optional coalesced selection; on
     * failure, no payload request and one visible error with the filesystem
     * facade returned to IDLE; on a cancelled or stale read, a silent retry.
     * Affiliates: menu_requestInstrumentIndexLoad(), menu_instrumentIndexReady(),
     * menu_instrumentLoadRequestSelection(), menu_instrumentLoadClampIndex(),
     * filesystem_browseReadWasCancelled(),
     * filesystem_instrumentNameCacheLoaded().
     */
    uint8_t index_ok = (uint8_t)(filesystem_status() == FS_STATUS_DONE);
    /* Read the cancel witness while the terminal status is still visible:
     * filesystem_ack() below returns the facade to IDLE. */
    uint8_t cancelled = filesystem_browseReadWasCancelled();

    filesystem_ack();
    menu_traceInstrumentEntry(
        AUTOSAVE_TRACE_INSTRUMENT_ENTRY_PHASE_INDEX_COMPLETE,
        (uint8_t)!index_ok);
    if (menu_pendingPageSwitch != MENU_PENDING_PAGE_NONE) {
        filesystem_clearNameCache();
        menu_deferSelectionRequest = 0u;
        menu_storageBusy = 0u;
        return;
    }
    /*
     * A read cancelled by a later type step is not a failure (S090 item 7).
     *
     * Its own cancel path already cleared the cache; the type step that
     * cancelled it left the newer request deferred. Release input and keep
     * one retry armed; no overlay. menu_pendingPageSwitch above still wins.
     */
    if (cancelled) {
        filesystem_clearNameCache();
        menu_storageBusy = 0u;
        menu_deferSelectionRequest = 1u;
        return;
    }
    if (!index_ok) {
        filesystem_clearNameCache();
        menu_deferSelectionRequest = 0u;
        menu_storageBusy = 0u;
        menu_showFilesystemErrorOverlay();
        return;
    }
    /*
     * A completed read for a type the user has already stepped away from
     * (S090 item 7).
     *
     * The step's own cache clear may also have emptied it mid-read. Either
     * way the cache does not hold the selected type, so it is discarded and
     * the selected type is retried; publishing it would pair the shown type
     * with another type's names.
     */
    if (!filesystem_instrumentNameCacheLoaded(menu_instrumentLoadType)) {
        filesystem_clearNameCache();
        menu_storageBusy = 0u;
        menu_deferSelectionRequest = 1u;
        return;
    }
    menu_instrumentIndexReady();
}
```

#### M-29 — MODIFY lines 7692–7693

S1. Comment only (`menu_traceInstrumentEntry()`).

First original line (7692): `` * Why: HCNAMES, hidden `.hctmp`, and `.hcindex` are serialized but otherwise ``  
Last original line (7693): `` * indistinguishable to a user watching the blank `kit` label. Affiliates: ``

Original:

```c
     * Why: HCNAMES, hidden `.hctmp`, and `.hcindex` are serialized but otherwise
     * indistinguishable to a user watching the blank `kit` label. Affiliates:
```

Replacement:

```c
     * Why: HCNAMES and `.hcindex` are serialized but otherwise
     * indistinguishable to a user watching the blank `kit` label. Affiliates:
```

#### M-05 — MODIFY lines 7800–7805

S4. Flush completion keeps the browser cache (leftover clear from before S058 Option 1C).

First original line (7800): `menu_residentNameDirtySceneMask = 0u;`  
Last original line (7805): `menu_storageBusy = 0u;`

Original:

```c
    menu_residentNameDirtySceneMask = 0u;
    menu_residentNameScratchValid = 0u;
    menu_residentNameScratchScene =
        MENU_RESIDENT_NAME_SCRATCH_INVALID_SCENE;
    filesystem_clearNameCache();
    menu_storageBusy = 0u;
```

Replacement:

```c
    menu_residentNameDirtySceneMask = 0u;
    menu_residentNameScratchValid = 0u;
    menu_residentNameScratchScene =
        MENU_RESIDENT_NAME_SCRATCH_INVALID_SCENE;
    /*
     * Keep the browser cache across the flush (S090 P1, 6a-i).
     *
     * What: the `.hcindex` cache is no longer cleared here.
     * Why: this clear dates from before S058 Option 1C, when `.hcnames`
     * transactions borrowed the browser cache. They now use the dedicated
     * hcnames_name_mirror, so a flush cannot change any cached index row; the
     * cache tag still names the domain it holds, and every browser reader
     * checks that tag. Clearing here threw away the Instrument index on every
     * VOICE change after a preview (LSR-01 flushes dirty rows on that change)
     * and forced a 2.4-3 s index read for a type that was already loaded.
     * Affiliates: menu_requestInstrumentIndexLoad() cache hit,
     * filesystem_instrumentNameCacheLoaded(), filesystem_residentNames_tick().
     */
    menu_storageBusy = 0u;
```

#### M-06 — MODIFY lines 8166–8168

S4. `menu_requestResidentNameScratch()` keeps the browser cache (same reason).

First original line (8166): `menu_residentNameScratchScene = scene;`  
Last original line (8168): `filesystem_clearNameCache();`

Original:

```c
    menu_residentNameScratchScene = scene;
    menu_residentNameScratchValid = 0u;
    filesystem_clearNameCache();
```

Replacement:

```c
    menu_residentNameScratchScene = scene;
    menu_residentNameScratchValid = 0u;
    /*
     * S090 P1 (6a-i): the browser cache is kept across this `.hcnames` read.
     * The read fills only the dedicated hcnames_name_mirror (S058 Option 1C);
     * the clear that stood here is from the time the read borrowed the
     * browser cache. Keeping it lets the nested Instrument entry reuse a
     * same-type index after a VOICE change. Index rows do not depend on the
     * Scene, and every browser reader checks the cache domain, so a kept cache
     * can never show a wrong name; a clean Scene change still disposes it
     * through menu_endResidentNameScratchSession() above.
     */
```

#### M-07 — MODIFY lines 8248–8257

S1. Comment only: `menu_invalidateInstrumentLoadTemp()`.

First original line (8248): `/*`  
Last original line (8257): `*/`

Original:

```c
    /*
     * Dispose one Menu-visible temporary `kit` identity.
     *
     * Inputs: voice/type/mode/exit boundaries. Output: the nine-byte label and
     * validity/type state are blanked, so no later cursor move can load a
     * `.hctmp.<ext>` from a different session. The on-card file is deliberately
     * treated as dirty scratch and is overwritten on the next same-type entry;
     * it is excluded from `.hcindex` and never affects HCNAMES.
     * Affiliates: menu_prepareInstrumentLoadTemp() and all nested boundaries.
     */
```

Replacement:

```c
    /*
     * Dispose one Menu-visible `kit` identity.
     *
     * Inputs: voice/type/mode/exit boundaries. Output: the nine-byte label and
     * validity/type state are blanked, so no later cursor move can restore a
     * snapshot from a different session. The RAM snapshot itself is simply
     * left in place and overwritten by the next capture (S090 P1); stale
     * `.hctmp.<ext>` files from older firmware stay hidden by the scanner and
     * are safe to delete. Affiliates: menu_prepareInstrumentLoadTemp() and all
     * nested boundaries.
     */
```

#### M-08 — MODIFY lines 8296–8348

S1. `menu_prepareInstrumentLoadTemp()`: synchronous RAM capture.

First original line (8296): `static uint8_t menu_prepareInstrumentLoadTemp(void)`  
Last original line (8348): `}`

Replacement:

```c
static uint8_t menu_prepareInstrumentLoadTemp(void)
{
    /*
     * Capture the entry voice for the `kit` row (S090 P1: RAM, synchronous).
     *
     * What: copies the session voice into Preset's RAM snapshot and makes the
     * `kit` row valid at once; the normal Load also keeps the original name in
     * the nine-byte Menu field. Always returns 1, so the caller continues
     * straight to the typed `.hcindex` in the same pass.
     * Why: the former `.hctmp.<ext>` write was the longest phase of nested
     * entry while playing (9-10 s in the S090 trace) and also blocked every
     * VOICE change. A capture failure (only possible for an invalid slot or
     * an unregistered type) leaves the `kit` row unavailable but no longer
     * stops the browser from opening.
     * Inputs: completed seven-row name scratch plus the current Load
     * Scene/voice/type/mode. Outputs: menu_instrumentTempValid/Type/Name.
     * Save mode has no reversible row and skips the capture. A valid snapshot
     * from this session is kept (voice/type/mode boundaries invalidate it).
     * Affiliates: menu_requestInstrumentEntryNames(),
     * preset_captureInstrumentSnapshot(), menu_restoreInstrumentLoadTemp().
     */
    if (menu_instrumentSaveMode || menu_instrumentTempValid)
        return 1u;
    /*
     * InstrumentMrp displays the existing HCNAMES identity cell, so only the
     * normal Load keeps its own copy of the original name for the `kit` row.
     */
    if (!menu_instrumentLoadMorphMode) {
        memcpy(menu_instrumentTempName, menu_instrumentSaveName,
               MENU_INSTRUMENT_SAVE_NAME_LEN);
        menu_instrumentTempName[MENU_INSTRUMENT_SAVE_NAME_LEN] = '\0';
    }
    menu_instrumentTempType = menu_instrumentLoadType;
    if (!preset_captureInstrumentSnapshot(menu_instrumentLoadScene,
                                          menu_instrumentLoadSlot)) {
        menu_invalidateInstrumentLoadTemp();
        return 1u;
    }
    menu_instrumentTempValid = 1u;
    return 1u;
}
```

#### M-09 — MODIFY lines 8350–8404

S1. `menu_restoreInstrumentLoadTemp()`: RAM restore committed in the call, gated on the same idle point the file restore's completion had.

First original line (8350): `static void menu_restoreInstrumentLoadTemp(void)`  
Last original line (8404): `}`

Replacement:

```c
static void menu_restoreInstrumentLoadTemp(void)
{
    /*
     * Restore the session snapshot after the cursor reaches `kit`
     * (S090 P1: from RAM, committed in this call).
     *
     * What: commits Preset's RAM snapshot to the browsed voice and arms the
     * same bounded Instrument apply the file restore's completion armed. The
     * normal Load first makes the original visible name authoritative again
     * in the seven-name scratch (the row is marked dirty for the session's
     * exit write); InstrumentMrp restores only its Morphable Morph cells.
     *
     * Why the guards: the file restore committed only from Preset's
     * completion switch, which runs once every apply worker is idle and no
     * Preset operation is in flight. A RAM restore is called directly (lower
     * row crossing, the idle retry at the top of menu_pollPresetStatus(), and
     * menu_finishInstrumentApplySession()), so it checks that same idle point
     * itself: no Menu input owner, no Instrument or whole-Sound apply, no
     * Preset operation, no Scene/Instrument apply worker. If it cannot run
     * now, menu_instrumentKitRestorePending (set by every committed preview)
     * keeps it owed and the idle retry runs it later. A snapshot from another
     * type is refused, as the type-matched file name used to be.
     *
     * Inputs: menu_instrumentTempValid/Type/Name, the session
     * Scene/voice/type/mode, menu_kitLoadSceneMask. Outputs: SceneData slot(s)
     * through Preset, menu_instrumentSaveName, the dirty name scratch,
     * menu_instrumentTempOperationPending (read once by
     * menu_finishInstrumentApplySession()), menu_storageBusy for the apply.
     * Affiliates: preset_restoreInstrumentSnapshot(),
     * preset_restoreInstrumentMorphSnapshot(), menu_tickInstrumentApply(),
     * menu_invalidateInstrumentLoadTemp(), lower-row decrement handling.
     */
    uint8_t accepted;

    if (menu_storageBusy ||
        menu_instrumentApplyActive ||
        menu_soundApplyActive ||
        preset_getStatus() != PRESET_IDLE ||
        !preset_applyWorkersIdle() ||
        !menu_instrumentTempValid ||
        menu_instrumentTempType >= INSTRUMENT_TYPE_UNKNOWN ||
        menu_instrumentTempType != menu_instrumentLoadType)
        return;
    accepted = menu_instrumentLoadMorphMode
        ? preset_restoreInstrumentMorphSnapshot(menu_instrumentLoadScene,
                                                menu_instrumentLoadSlot,
                                                menu_instrumentTempType)
        : preset_restoreInstrumentSnapshot(menu_instrumentLoadScene,
                                           menu_instrumentLoadSlot,
                                           menu_instrumentTempType);
    if (!accepted)
        return;
    /*
     * The restore is committed: the owed state is settled, and the shared tag
     * tells menu_finishInstrumentApplySession() that the apply now starting
     * belongs to this restore (it clears the tag and posts nothing more).
     */
    menu_instrumentKitRestorePending = 0u;
    menu_instrumentTempOperationPending = 1u;
    if (!menu_instrumentLoadMorphMode) {
        /*
         * The original visible identity becomes authoritative again before the
         * bounded runtime apply starts (as the file restore's completion did):
         * one Instrument HCNAMES row is marked dirty for the session exit.
         */
        memcpy(menu_instrumentSaveName, menu_instrumentTempName,
               MENU_INSTRUMENT_SAVE_NAME_LEN);
        menu_instrumentSaveName[MENU_INSTRUMENT_SAVE_NAME_LEN] = '\0';
        menu_refreshResidentNameScratchInstrument(menu_kitLoadSceneMask,
                                                  menu_instrumentLoadSlot);
        /*
         * The committed image can change one slot's layout. Drop stale members
         * from every directional VOICE mask before the next edit (S072 Step 10
         * F5), as menu_startInstrumentApply() does for a pool load.
         */
        bank_revalidateVoiceEditMasks();
    }
    /*
     * Hold input through the bounded apply, exactly as
     * menu_startInstrumentApply() / menu_startInstrumentMorphApply() do;
     * menu_tickInstrumentApply() releases it when Preset's worker drains.
     */
    menu_instrumentApplyActive = 1u;
    menu_instrumentApplySlot = menu_instrumentLoadSlot;
    menu_storageBusy = 1u;
    menu_refreshLoadSceneLeds();
}
```

#### M-10 — MODIFY lines 8507–8512

S1. Comment only in `menu_requestInstrumentEntryNames()`.

First original line (8507): `/*`  
Last original line (8512): `*/`

Original:

```c
    /*
     * Normal Instrument Load must finish the durable `kit` snapshot before
     * `.hcindex` browsing begins. This serializes the SD operations and keeps
     * the one temporary filename out of the browser cache. Save/Morph paths
     * bypass it because neither offers the reversible parameter row.
     */
```

Replacement:

```c
    /*
     * Normal Instrument Load captures its `kit` snapshot before `.hcindex`
     * browsing begins (S090 P1: a RAM copy, so the index request follows in
     * the same pass). Save/Morph paths bypass the name copy because neither
     * offers the reversible name row.
     */
```

#### M-11 — MODIFY lines 8518–8553

S4/S5. `menu_requestInstrumentIndexLoad()`: cache hit, then non-destructive refusal while the facade is busy.

First original line (8518): `static void menu_requestInstrumentIndexLoad(instrument_type_t type)`  
Last original line (8553): `}`

Replacement:

```c
static void menu_requestInstrumentIndexLoad(instrument_type_t type)
{
    /*
     * Request the selected type's own `.hcindex` whenever nested Instrument
     * Load or Save enters/selects a type. Inputs: a registry type captured by
     * Menu. Output: the UI remains locked until the foreground SD state
     * machine has loaded that type's general-purpose name cache. A rejected
     * request is deferred through the existing selection retry path, which
     * handles a still-busy filesystem without inventing a second queue.
     */
    /*
     * Reuse an index that is already loaded (S090 P1, 6a-i).
     *
     * What: when the browser cache already holds a completed index of this
     * type, the browser is published at once with no card access.
     * Why: a VOICE change between two voices of the same type, a Morph
     * toggle, or a return to the same type re-requested the same file
     * (2.4-3 s while playing). Kit has reused its index the same way since
     * S054 ("Branch B" in menu_requestKitEntryNames()).
     * Safety: filesystem_instrumentNameCacheLoaded() is true only for the
     * INSTRUMENT domain of this exact type while the facade is idle, so a read
     * or rebuild still in flight is never reused; every writer of the cache
     * either republishes it whole or clears its tag.
     */
    if (filesystem_instrumentNameCacheLoaded(type)) {
        menu_instrumentIndexReady();
        return;
    }
    /*
     * Refuse non-destructively while the facade is busy (S090 P1, item 7).
     *
     * What: arm the one deferred retry and leave the cache, the generation
     * snapshot and menu_storageBusy exactly as they are.
     * Why: a type step is now accepted while an older index read runs. That
     * read owns menu_storageBusy until its completion; the former refusal
     * path below cleared the flag and the cache under it, which reopened
     * every input while the read was still in flight (the S082 first-boot
     * freeze route recorded in menu_handleLoadSaveKnobDelta()). The same
     * pattern as menu_requestResidentNameScratch() (S054).
     */
    if (filesystem_status() == FS_STATUS_BUSY) {
        menu_traceInstrumentEntry(
            AUTOSAVE_TRACE_INSTRUMENT_ENTRY_PHASE_INDEX_REQUEST, 1u);
        menu_deferSelectionRequest = 1u;
        return;
    }
    filesystem_clearNameCache();
    menu_storageBusy = 1u;
    menu_selectionGenerationSnapshot = menu_selectionGeneration;
    if (!filesystem_requestLoadInstrumentIndex(
            type, menu_instrumentIndexLoadComplete)) {
        menu_traceInstrumentEntry(
            AUTOSAVE_TRACE_INSTRUMENT_ENTRY_PHASE_INDEX_REQUEST, 1u);
        /*
         * Release the input gate that the refused request never earned.
         *
         * menu_storageBusy was raised above in anticipation of an accepted
         * request. When the request is refused, leaving it raised is a
         * self-inflicted deadlock: the deferred retry armed on the next line
         * is dispatched only while `!menu_storageBusy`, so the one recovery
         * path this function relies on can never fire, and nothing else clears
         * the flag. The result is a permanently locked nested Instrument menu
         * with no error overlay and no trace. Clearing it here restores the
         * intended "wait, then retry once idle" behaviour. Since S090 this is
         * reached only with an idle facade (an invalid type or directory).
         */
        menu_storageBusy = 0u;
        menu_deferSelectionRequest = 1u;
    } else {
        menu_traceInstrumentEntry(
            AUTOSAVE_TRACE_INSTRUMENT_ENTRY_PHASE_INDEX_REQUEST, 0u);
    }
}
```

#### M-12 — MODIFY lines 8909–8910

S5. `menu_requestLibraryIndexLoad()`: non-destructive refusal while the facade is busy.

First original line (8909): `menu_selectionGenerationSnapshot = menu_selectionGeneration;`  
Last original line (8910): `filesystem_clearNameCache();`

Original:

```c
    menu_selectionGenerationSnapshot = menu_selectionGeneration;
    filesystem_clearNameCache();
```

Replacement:

```c
    /*
     * Refuse non-destructively while the facade is busy (S090 P1, item 7).
     *
     * What: arm the deferred retry before touching the generation snapshot,
     * the cache or menu_storageBusy.
     * Why: a type step may now arrive while an older index read runs. Keeping
     * the snapshot unchanged lets that read's completion recognise itself as
     * stale (menu_libraryIndexLoadComplete()'s coordinate check), so it clears
     * the cache, releases input and keeps the retry, instead of being taken
     * for the current request.
     */
    if (filesystem_status() == FS_STATUS_BUSY) {
        menu_deferSelectionRequest = 1u;
        return;
    }
    menu_selectionGenerationSnapshot = menu_selectionGeneration;
    filesystem_clearNameCache();
```

#### M-13 — MODIFY line 9310

S5. `menu_instrumentLoadStepType()`: remember the type before the step.

First original line (9310): `int8_t direction = (inc < 0) ? -1 : 1;`  
Last original line (9310): `int8_t direction = (inc < 0) ? -1 : 1;`

Original:

```c
    int8_t direction = (inc < 0) ? -1 : 1;
```

Replacement:

```c
    int8_t direction = (inc < 0) ? -1 : 1;
    /* S090 item 7: the type before this step, to tell a real type change
     * (which disposes a running index read) from a Morph toggle. */
    instrument_type_t previous_type = menu_instrumentLoadType;
```

#### M-14 — MODIFY line 9410

S5. `menu_instrumentLoadStepType()`: cancel the running read on a real type change.

First original line (9410): `menu_selectionGeneration++;`  
Last original line (9410): `menu_selectionGeneration++;`

Original:

```c
            menu_selectionGeneration++;
```

Replacement:

```c
            menu_selectionGeneration++;
            /*
             * Dispose the index read for the type just left (S090 item 7).
             *
             * What: a step to another type asks a running `.hcindex` read to
             * stop at its next safe phase (it finishes ERROR with the cache
             * cleared, and its completion retires it quietly).
             * Why: only the type the user settles on needs its list; reading
             * each type passed over cost a full read per step. A step that
             * lands on the same type (Morph toggle) keeps the read, because
             * its result is still the one needed.
             * Affiliates: filesystem_cancelBrowseRead(),
             * menu_instrumentIndexLoadComplete().
             */
            if (menu_instrumentLoadType != previous_type)
                filesystem_cancelBrowseRead();
```

#### M-15 — MODIFY line 11356

S1. Comment only (pot gate).

First original line (11356): `` * load, HCNAMES read, `.hctmp` save, Instrument apply or Bank preview ``  
Last original line (11356): `` * load, HCNAMES read, `.hctmp` save, Instrument apply or Bank preview ``

Original:

```c
     * load, HCNAMES read, `.hctmp` save, Instrument apply or Bank preview
```

Replacement:

```c
     * load, HCNAMES read, `.hctmp` save (removed in S090), Instrument apply or Bank preview
```

#### M-16 — ADD before line 15333

S5. Add `menu_paintTypeStepWhileBusy()`.

Anchor (line 15333, unchanged): `/* -----------------------------------------------------------------------`

New block:

```c
/*
 * Paint a Load/Save type step while storage is busy (S090 P1, item 7).
 *
 * What: when menu_storageBusy is set after a type step, repaint the whole
 * page now: the new type label appears and its names are blank. For a
 * top-level Kit/KitMrp Load the name buffer is blanked first, because that
 * page shows preset_currentName rather than a cache row.
 * Why: the encoder path repaints only when storage is idle
 * (menu_parseEncoder() returns before its repaint while busy), so a type step
 * that started a read, or that was accepted while an older read runs, left
 * the previous type's frame on screen until the read completed. The user must
 * see each step at once. Blank names mean busy (user rule, S090 U18): the
 * other types' names are blank already, because their slot readers check the
 * cache domain and the step clears the cache.
 * Inputs: menu_storageBusy, page, nested mode, menu_saveOptions.what.
 * Outputs: preset_currentName (top-level Kit Load only) and the LCD frame.
 * Callers: menu_handleLoadSaveMenu() type-row branches (top-level and nested
 * Instrument Load). Affiliates: menu_repaintAll(),
 * menu_libraryIndexLoadComplete() (which fills the name when the read lands).
 */
static void menu_paintTypeStepWhileBusy(void)
{
    if (!menu_storageBusy)
        return;
    if (menu_activePage == LOAD_PAGE &&
        !menu_instrumentLoadActive &&
        (menu_saveOptions.what == SAVE_TYPE_KIT ||
         menu_saveOptions.what == SAVE_TYPE_KIT_MORPH))
        memset(preset_currentName, ' ', 8u);
    menu_repaintAll();
}
```

#### M-17 — MODIFY lines 15451–15452

S5. Nested Instrument type row: paint the step.

First original line (15451): `if (inc != 0)`  
Last original line (15452): `menu_instrumentLoadStepType(inc);`

Original:

```c
                if (inc != 0)
                    menu_instrumentLoadStepType(inc);
```

Replacement:

```c
                if (inc != 0) {
                    menu_instrumentLoadStepType(inc);
                    /* S090 item 7: show the step at once while busy. */
                    menu_paintTypeStepWhileBusy();
                }
```

#### M-18 — MODIFY lines 15520–15532

S1. Comment only (lower-row `kit` crossing).

First original line (15520): `* crossing posts the hidden-file restore; subsequent`  
Last original line (15532): `* renderer in menu_repaintLoadSavePage().`

Original:

```c
                         * crossing posts the hidden-file restore; subsequent
                         * decrements while `kit` is already selected alter
                         * neither filesystem nor Preset state. Why: Load-number
                         * turns remain intentionally accepted while an earlier
                         * request drains. Reposting `.hctmp.<ext>` from every
                         * detent of a large decrement could repeatedly contend
                         * with that request and leave a stale restore eligible
                         * after the user re-entered Instrument Load. The
                         * existing identity row remains its original HCNAMES
                         * stem, so no name copy or HCNAMES write is needed.
                         * Affiliates: menu_restoreInstrumentLoadTemp(), the
                         * hidden `.hctmp.<ext>` parser request, and the `kit`
                         * renderer in menu_repaintLoadSavePage().
```

Replacement:

```c
                         * crossing commits the snapshot restore; subsequent
                         * decrements while `kit` is already selected alter
                         * neither SceneData nor Preset state. Why: Load-number
                         * turns remain intentionally accepted while an earlier
                         * request drains. Re-running the restore from every
                         * detent of a large decrement would re-apply the voice
                         * on every detent and could leave a stale restore
                         * eligible after the user re-entered Instrument Load.
                         * The existing identity row remains its original
                         * HCNAMES stem, so no name copy or HCNAMES write is
                         * needed. Affiliates: menu_restoreInstrumentLoadTemp(),
                         * preset_restoreInstrumentSnapshot() (S090 P1), and the
                         * `kit` renderer in menu_repaintLoadSavePage().
```

#### M-19 — MODIFY lines 15845–15846

S5. Top-level type row: cancel the running read on a type change.

First original line (15845): `if (menu_saveOptions.what != previous_type)`  
Last original line (15846): `menu_selectionGeneration++;`

Original:

```c
                if (menu_saveOptions.what != previous_type)
                    menu_selectionGeneration++;
```

Replacement:

```c
                if (menu_saveOptions.what != previous_type) {
                    menu_selectionGeneration++;
                    /*
                     * Dispose the index read for the type just left
                     * (S090 item 7): it stops at its next safe phase and its
                     * completion retires it quietly; only the type the user
                     * settles on is read. Affiliates:
                     * filesystem_cancelBrowseRead(),
                     * menu_libraryIndexLoadComplete().
                     */
                    filesystem_cancelBrowseRead();
                }
```

#### M-30 — MODIFY lines 15858–15859

S5. Top-level type row, Kit-family flush branch: paint the step.

First original line (15858): `menu_refreshLoadSceneLeds();`  
Last original line (15859): `break;`

Original:

```c
                    menu_refreshLoadSceneLeds();
                    break;
```

Replacement:

```c
                    menu_refreshLoadSceneLeds();
                    /* S090 item 7: the flush owns storage; show the step. */
                    menu_paintTypeStepWhileBusy();
                    break;
```

#### M-31 — MODIFY lines 15894–15895

S5. Top-level type row, Scene flush branch: paint the step.

First original line (15894): `menu_refreshLoadSceneLeds();`  
Last original line (15895): `break;`

Original:

```c
                    menu_refreshLoadSceneLeds();
                    break;
```

Replacement:

```c
                    menu_refreshLoadSceneLeds();
                    /* S090 item 7: the flush owns storage; show the step. */
                    menu_paintTypeStepWhileBusy();
                    break;
```

#### M-20 — MODIFY lines 15902–15905

S5. Top-level type row: paint the step.

First original line (15902): `menu_resetLoadSaveSceneSelection();`  
Last original line (15905): `break;`

Original:

```c
            menu_resetLoadSaveSceneSelection();
            menu_requestCurrentLoadSaveSelection(0);
            menu_refreshLoadSceneLeds();
            break;
```

Replacement:

```c
            menu_resetLoadSaveSceneSelection();
            menu_requestCurrentLoadSaveSelection(0);
            menu_refreshLoadSceneLeds();
            /* S090 item 7: show the step at once while busy. */
            menu_paintTypeStepWhileBusy();
            break;
```

#### M-21 — MODIFY lines 16094–16112

S5. Encoder gate: admit type steps over a Menu index read.

First original line (16094): `menu_saveOptions.what == SAVE_TYPE_KIT_MORPH))));`  
Last original line (16112): `}`

Replacement:

```c
               menu_saveOptions.what == SAVE_TYPE_KIT_MORPH))));
        /*
         * Type steps never wait for a list (S090 P1, item 7; user U17).
         *
         * What: a plain turn of the type row in edit mode, on the Load or Save
         * page (top level) or the nested Instrument Load type row, is admitted
         * while the only storage work is a Menu `.hcindex` read.
         * Why: each read takes 2.4-3 s while playing, and the gate refused
         * every step until it finished, so reaching a type three steps away
         * cost three reads. The step installs the new type at once; the type
         * handlers cancel the running read and their refused requests leave
         * one deferred retry for the type the user settles on.
         * Guards (all must hold):
         * - filesystem_browseReadRunning(): the facade runs a Menu index read
         *   (Instrument or numbered library), which is cancellable and whose
         *   completion handlers retire a stale result. Writes, `.hcnames`
         *   reads, payload loads, previews and rebuilds keep the full lock;
         * - Preset idle, no Instrument apply and no Load/Save command: nothing
         *   else owns the coordinates this step changes;
         * - copy/clear does not own the encoder;
         * - nested Instrument Save is excluded: its type row only toggles
         *   Morph and never needs a read.
         * Pots (menu_handleLoadSaveKnobDelta()) keep their lock: Pot 1 also
         * crosses voices and pages, whose entry paths are transaction-guarded.
         * Affiliates: menu_handleLoadSaveMenu() type branches,
         * menu_paintTypeStepWhileBusy(), filesystem_cancelBrowseRead(),
         * menu_instrumentIndexLoadComplete(), menu_libraryIndexLoadComplete().
         */
        uint8_t allow_type_step = (uint8_t)(
            inc != 0 &&
            button == lastEncoderButton &&
            (menu_activePage == LOAD_PAGE || menu_activePage == SAVE_PAGE) &&
            editModeActive &&
            menu_saveOptions.state == SAVE_STATE_EDIT_TYPE &&
            !(menu_instrumentLoadActive && menu_instrumentSaveMode) &&
            !menu_loadSaveCommandActive &&
            !menu_instrumentApplyActive &&
            preset_getStatus() == PRESET_IDLE &&
            !copyClear_ownsEncoder() &&
            filesystem_browseReadRunning());

        /*
         * Storage exclusivity blocks every control except Load-number turns
         * and, since S090, type steps over an index read (above).
         *
         * Kit and nested Instrument payloads can take long enough that a hard
         * encoder lock makes sparse libraries painful to browse. A plain turn
         * on the already-selected number field may therefore update only the
         * Menu-owned desired number while the immutable older request drains.
         * Clicks, Scene/voice changes, Save editing, and all non-Load pages
         * remain blocked. The handler leaves one existing defer flag. The
         * resident typed/Kit index normally stays active, so the latest number
         * and name repaint immediately; only entry/exit waits for shared-cache
         * restoration.
         */
        if (!allow_load_number_scroll && !allow_type_step) {
            lastEncoderButton = button;
            return;
        }
```

#### M-22 — MODIFY lines 17318–17332

S1. Comment only (idle `kit` retry in `menu_pollPresetStatus()`).

First original line (17318): `` * Output: the `.hctmp.<ext>` restore is re-posted; the owed flag is left ``  
Last original line (17332): `* function's own morph branch. See S054_INST_RESTORE_FIX.md.`

Original:

```c
     * Output: the `.hctmp.<ext>` restore is re-posted; the owed flag is left
     * alone here and cleared only by the completion that actually restores the
     * slot, so a request rejected again this pass is simply retried on the
     * next one. Why: the user may stop turning the moment they reach `kit`, so
     * the restore cannot depend on another detent arriving to carry it. This
     * check deliberately sits ahead of every tick function below because those
     * return early as soon as any one of them does work — in particular
     * preset_tickDrumsetApply(), whose Scene-switch quiet-gate can stay active
     * for seconds while a pattern keeps re-triggering the browsed voice, which
     * is exactly the condition under which this bug was reported. Placing the
     * retry here makes it unstarvable. menu_restoreInstrumentLoadTemp() is
     * itself a no-op unless the snapshot is valid and its type still matches,
     * so an unusable snapshot cannot spin. Morph mode is included: the same
     * owed-state rule drives preset_loadInstrumentMorphTemp() through that
     * function's own morph branch. See S054_INST_RESTORE_FIX.md.
```

Replacement:

```c
     * Output: the snapshot restore is retried; the owed flag is left alone
     * here and cleared only when a restore is actually committed, so a restore
     * that still cannot run this pass is simply retried on the next one. Why:
     * the user may stop turning the moment they reach `kit`, so the restore
     * cannot depend on another detent arriving to carry it. This check
     * deliberately sits ahead of every tick function below because those
     * return early as soon as any one of them does work — in particular
     * preset_tickDrumsetApply(), whose Scene-switch quiet-gate can stay active
     * for seconds while a pattern keeps re-triggering the browsed voice, which
     * is exactly the condition under which this bug was reported. Placing the
     * retry here makes it unstarvable; since S090 the restore itself waits for
     * idle apply workers (preset_applyWorkersIdle()), so committing it here
     * cannot overlap one. menu_restoreInstrumentLoadTemp() is a no-op unless
     * the snapshot is valid and its type still matches, so an unusable
     * snapshot cannot spin. Morph mode is included through that function's
     * Morph branch. See S054_INST_RESTORE_FIX.md.
```

#### M-23 — MODIFY line 17460

S1/S5. Comment only (deferred dispatcher).

First original line (17460): `` * Load/Save is open (HCNAMES seven-row read, `.hctmp` snapshot, ``  
Last original line (17460): `` * Load/Save is open (HCNAMES seven-row read, `.hctmp` snapshot, ``

Original:

```c
                 * Load/Save is open (HCNAMES seven-row read, `.hctmp` snapshot,
```

Replacement:

```c
                 * Load/Save is open (HCNAMES seven-row read, typed index,
```

#### M-24 — MODIFY line 17461

S1/S5. Comment only (deferred dispatcher).

First original line (17461): `* typed index) resumes through menu_requestInstrumentEntryNames(),`  
Last original line (17461): `* typed index) resumes through menu_requestInstrumentEntryNames(),`

Original:

```c
                 * typed index) resumes through menu_requestInstrumentEntryNames(),
```

Replacement:

```c
                 * a cancelled or stale read) resumes through menu_requestInstrumentEntryNames(),
```

#### M-25 — REMOVE lines 17513–17544

S1. Remove the temp branches of the Preset failure path (only temp ops used them; a failed pool load never set either flag).

First original line (17513): `if (preset_getCompletedOp() == PRESET_OP_INSTRUMENT_TEMP_SAVE ||`  
Last original line (17544): `}`

Removed text:

```c
        if (preset_getCompletedOp() == PRESET_OP_INSTRUMENT_TEMP_SAVE ||
            preset_getCompletedOp() == PRESET_OP_INSTRUMENT_MORPH_TEMP_SAVE) {
            /* A failed baseline write must not leave the shared pending tag
             * blocking the next nested Instrument entry. */
            menu_invalidateInstrumentLoadTemp();
        } else if (preset_getCompletedOp() == PRESET_OP_INSTRUMENT_LOAD ||
                   preset_getCompletedOp() ==
                       PRESET_OP_INSTRUMENT_MORPH_TEMP_LOAD) {
            /* Keep a valid baseline available for a retry, but release the
             * accepted restore tag after a failed hidden-file read. */
            menu_instrumentTempOperationPending = 0u;
            /*
             * Bound the owed-restore retry at a genuine terminal failure.
             *
             * Input: a hidden `.hctmp.<ext>` read that was accepted and then
             * failed — proven for the normal op by the filesystem's immutable
             * temporary-origin flag, and implicit for the Morph temp op. Output:
             * the owed state is released so the idle retry in
             * menu_pollPresetStatus() stops re-posting a read that just failed;
             * the user sees the ERR overlay and can scroll off and back onto
             * `kit` to try again. A *failed pool* load is deliberately excluded:
             * it never replaced the slot, so whatever was owed before it stays
             * owed. This branch is the failure counterpart of the owed-state
             * updates in the PRESET_OP_INSTRUMENT_*_LOAD success cases, which
             * this early return would otherwise skip entirely.
             */
            if (preset_getCompletedOp() ==
                    PRESET_OP_INSTRUMENT_MORPH_TEMP_LOAD ||
                filesystem_loadedInstrumentWasTemporary()) {
                menu_instrumentKitRestorePending = 0u;
            }
        }
```

#### M-26 — REMOVE lines 17844–17892

S1. Remove the `PRESET_OP_INSTRUMENT_TEMP_SAVE` and `PRESET_OP_INSTRUMENT_MORPH_TEMP_SAVE` cases.

First original line (17844): `case PRESET_OP_INSTRUMENT_TEMP_SAVE:`  
Last original line (17892): `(blank)`

Removed: 49 lines (the whole span above; see the tree for the text).

#### M-27 — MODIFY lines 17893–17968

S1. `PRESET_OP_INSTRUMENT_LOAD`: every completion is a pool load (AutoSave mark 1, owed restore on success; the temp-name block moves to M-09).

First original line (17893): `case PRESET_OP_INSTRUMENT_LOAD:`  
Last original line (17968): `}`

Replacement:

```c
    case PRESET_OP_INSTRUMENT_LOAD:
    {
        uint8_t selection_stale = (uint8_t)(
            menu_selectionGenerationSnapshot != menu_selectionGeneration);

        if (selection_stale && menu_activePage == LOAD_PAGE &&
            menu_instrumentLoadActive && !menu_instrumentSaveMode) {
            /* The loaded payload is committed, but its coordinate is no longer
             * the displayed one; let the latest desired row win next. */
            menu_deferSelectionRequest = 1u;
            menu_deferSelectionLoadKit = 1u;
        }

        /*
         * Track whether the resident slot still matches the `kit` snapshot.
         *
         * Since S090 every completion here is a root-pool load: the `kit`
         * restore is committed from RAM by menu_restoreInstrumentLoadTemp()
         * and never passes through this completion. A successful pool load
         * therefore always makes a restore owed; a failed one never reaches
         * this case (the early failure return above), so whatever was owed
         * before it stays owed. Affiliate: the owed-state declaration.
         */
        if (preset_getCompletedOk())
            menu_instrumentKitRestorePending = 1u;

        /*
         * Single Instrument load completion.
         *
         * Inputs: Preset request slot retained when the Instrument/ file load
         * was posted. Output: only that slot is applied through Preset's
         * one-slot cursor. This must not call menu_startSoundApply(), because
         * that path is intentionally a whole-Kit/Scene apply operation. A
         * root-pool load replaces retained Instrument data, so its committed
         * payload is marked for AutoSave (the third argument, 1).
         */
        menu_startInstrumentApply(preset_getRequestScene(),
                                  preset_getRequestSlot(),
                                  1u);
        menu_refreshLoadSceneLeds();
        break;
    }
```

#### M-28 — REMOVE lines 17998–18010

S1. Remove the `PRESET_OP_INSTRUMENT_MORPH_TEMP_LOAD` case.

First original line (17998): `case PRESET_OP_INSTRUMENT_MORPH_TEMP_LOAD:`  
Last original line (18010): `(blank)`

Removed text:

```c
    case PRESET_OP_INSTRUMENT_MORPH_TEMP_LOAD:
        /* The hidden snapshot is already a Morph-only request; preserve the
         * name/type/Normal image and run the ordinary bounded Morph worker. */
        /* The owed Morph restore has now reached its terminal completion.
         * Cleared on failure as well as success for the same reason as the
         * normal path: a failed terminal completion must not be retried
         * forever, and the user already receives the ERR overlay. */
        menu_instrumentKitRestorePending = 0u;
        menu_startInstrumentMorphApply(preset_getRequestScene(),
                                       preset_getRequestSlot());
        menu_refreshLoadSceneLeds();
        break;
```

## 8. Verification

### 8.1 Static gates (after applying)

1. `make all && make img` succeeds.
2. **No new warnings.** The warning set equals the baseline's, including the
   seven pre-existing `-Wunused-function` in `filesystem.c` (plan §1.3).
3. **`link_budget.py`:** payload 684,036 B (+100 B), ITCM 15,240 B, DTCM
   statics 4,372 B, FX margin 3,808 B. Any other number means the tree
   differs from this schedule.
4. **`arm-none-eabi-nm -S`:**
   - `pm_instrument_snapshot` is `0x81` (129 B);
   - `op_instrument_load_temporary` and `op_instrument_save_temporary` are
     absent;
   - no `*InstrumentTemp*` or `*WasTemporary*` symbol remains except
     `filesystem_isReservedInstrumentTemporaryName` (kept, D6).
5. **Logging off** (`DEV_MODE_LOGGING 0`, then back to 1): builds with no
   warning that the logging-on build lacks; text 666,920, bss 396,184.

### 8.2 Hardware (logging ON, PLAY running unless noted)

The plan's §6.1 tables apply with these amendments:
- **Name correctness:** compare against a fresh card read (a forced read, or
  a computer listing); the shadow check is withdrawn (U14).
- **H16, H17, H19:** withdrawn with 3a, 3b, 4a.
- **H18:** pass is "no glitch, no underrun" (U19). Include an Instrument Save
  and a Kit Save while playing.

Tests for this implementation (subset of plan §6.1 plus H22–H26):

| # | Test | Pass |
|---|---|---|
| H1 | Instrument entry, each type | no `.hctmp` file touched (card listing unchanged); names appear after one index read |
| H2 | Preview rows `000…`, then back to `kit` | entry voice restored exactly (normal Load, every type) |
| H3 | Morph entry, preview, `kit` | only Morphable Morph cells restored; Normal image, name and type unchanged |
| H4 | `kit` before any preview | no change audible |
| H5 | Fast number scroll in every phase | number first, name next, LEDs last; number never lags |
| H6 | Instrument Save, InstrumentMrp Save | unchanged |
| H7 | VOICE change between two Drum voices, no preview | no SD access; names immediate |
| H7b | Preview on one Drum voice, then VOICE to another Drum voice | only the LSR-01 name flush (a write); no `.hcnames` read; no index read (D8, D12) |
| H8 | Drum → Snare → Drum (one detent each, waiting) | one index read per type change |
| H10 | Boot → PLAY → Load/Save | no `.hcnames` card read (D1); one index read |
| H11 | Exit, return, nothing changed | no `.hcnames` read; one index read (6a-ii dropped) |
| H12 | Exit, MIDI PC Kit load, return | correct names |
| H13 | Exit with dirty names (deferred flush), return | correct names after the flush |
| H14 | Change active Scene, enter | correct names for the new Scene |
| H15 | Copy/clear during and after Load/Save | loan works; names correct |
| H18 | Everything above, listening | no glitch; no underrun |
| H20 | Scene/Bank/Pattern Load (audio-suspended path) | unchanged; fast drain still owns it |
| H21 | Stopped transport: H1, H7, H10, H11 | no regression |
| H22 | AutoSave rewrite and a Pattern drain between entries, then enter | correct names, served from the mirror |
| H23 | Step the top-level type several types quickly, Load and Save | the label follows every step; names blank, then the settled type's list after one read |
| H24 | Nested type Drum → Snare → Cymbal → HiHat quickly | same as H23 |
| H25 | Step type while a write runs (e.g. right after leaving Kit with dirty names) | the turn is ignored as today; after the write, one read for the shown type |
| H26 | Step back to a type whose read was cancelled | fresh read; complete, correct list |
| H27 | Turn Pot 1 while a list loads | ignored, as today |

## 9. Documentation at close-out (plan §9, narrowed by U13–U20)

- **`FILESYSTEM_SPEC.md`:**
  - Instrument entry without `.hctmp`; the RAM `kit` restore (≈145, 228,
    307, 425, 912, 2066, 2780);
  - Menu `.hcnames` entry reads served from a VALID mirror (≈2474) and the
    scope limit (a card edited on a computer is seen after a power cycle);
  - same-type Instrument index reuse in a session;
  - §Binding Mandates: the type-step rule beside the slot rule (a type step
    disposes the running index read; only the settled type is read; blank
    names mean busy);
  - stale `.hctmp.<ext>` files are safe to delete.
- **`STORAGE_SRAM_MANIFEST.md`:** `pm_instrument_snapshot` 129 B (Preset); the
  removed 1 B temp flags.
- **`COPYCLEAR_UTILITIES.md` §15:** only where it describes the `.hctmp` entry
  phases.
- **`MEMORY.md`:**
  - replace the S088 status line;
  - add the reuse rule (names in RAM only through `hcnames_mirror_valid` VALID
    or the browser-cache tag with an idle facade; shared-cache users unchanged
    unless the speedup needs it);
  - add "the snapshot owns `kit` restore";
  - add to Failed Approaches: caching every library index in RAM / loading
    indexes at boot (S090).
- **Session log:** `knowledge_files/log_archive/090_SESSION_HANDOFF_LOG.md`,
  with every detail of the plan and this schedule (both are deleted at close).
- **Not changed (U20):** `DEV_MODES.md`, `AutosaveTrace.h`,
  `decode_devlogs.py`, `MODULE_INTERCHANGE_SPEC.md` (no boot-order change, D1).

## 10. Out of scope (do not touch)

- item 1 idle pump and §7a burst size (U19); §7b/§7c (not approved);
- items 3a, 3b, 4a, 4b, 4c, 5 (replaced by Q2), 5e;
- 6a-ii (cache kept across exit) and the original 6b scratch retention;
- the LSR-01 flush on VOICE change (D12);
- AutoSave, Pattern, Bank, copy/clear and boot uses of `hcnames_name_mirror`
  and `fs_list_cache_name`;
- page layout, labels, LEDs (U18), apart from D11;
- the D13 findings;
- the uncommitted Oscillator/DrumVoice/S089 files.

## 11. Pre-verification record (session scratchpad, 2026-10-31)

- **Tree:** a copy of the working tree (`rsync`, without `.git`, the two
  `Helicase-dev-*` reference trees, `knowledge_files`, `SD_CARD*`, `tools`,
  `build`; `link_budget.py` and `build_lxrv2_img.py` copied in).
- **Baseline build of that copy:** text 683,512, data 424, bss 412,884;
  payload 683,936 B. This equals plan §1.3, so the copy matches the tree.
- **Patch:** the 81 changes of §3–§7 were applied by a script that addresses
  the original line numbers bottom-up per file and asserts the first and last
  original line of every change before editing. All assertions held; no two
  changes overlap.
- **Patched build, from a fresh copy:** `make all` exit 0; 14 distinct
  warnings, the same set as the baseline; numbers as in §1 D14.
- **Logging-off build:** exit 0; no additional warnings; text 666,920, data
  420, bss 396,184.
- **Not run:** hardware (§8.2, the user's check). There is no host test for
  Menu/filesystem sequencing in `tools/dsp_test`, and adding one is out of
  scope (U20).

---

## 12. Implementation progress (2026-10-31)

**Step 1 — baseline check.** Working tree at `7f8b5d07`, clean apart from the
two S090 documents. All 81 entries parsed; every first/last quoted line, ADD
anchor and original code block validated against the tree before any edit. No
entry overlapped another.

**Step 2 — apply.** All 81 changes applied bottom-up per file (highest
original line first), so the scheduled line numbers stayed valid:

| File | Entries |
|---|---:|
| `Core/Bank/Scene/Preset/presetManager.h` | 4 |
| `Core/Bank/Scene/Preset/presetManager.c` | 7 |
| `Core/Hardware/SD/filesystem.h` | 3 |
| `Core/Hardware/SD/filesystem.c` | 34 |
| `Core/Hardware/SD/storageTypes.h` | 1 |
| `Core/Hardware/SD/storageTypes.c` | 1 |
| `Core/Menu/menu.c` | 31 |

`git diff --check` is clean. No `config.h` change remains; `DEV_MODE_LOGGING`
is back to 1.

**Step 3 — build and static gates.** `make all && make img` exit 0. Measured
numbers equal §1 D14 exactly:

| | Baseline | Implemented |
|---|---:|---:|
| text | 683,512 | 683,616 |
| data | 424 | 420 |
| bss | 412,884 | 413,020 |
| image payload | 683,936 | 684,036 |
| flash headroom | 69,728 | 69,628 |
| ITCM | 15,240 | 15,240 |
| DTCM statics | 4,372 | 4,372 |
| FXBUF margin | 3,808 | 3,808 |

- `nm`: `pm_instrument_snapshot` is `0x81` (129 B);
  `op_instrument_load_temporary`, `op_instrument_save_temporary`,
  `*InstrumentTemp*` and `*WasTemporary*` are absent except the intentionally
  kept `filesystem_isReservedInstrumentTemporaryName`.
- Warning set equals the baseline (20 lines, including the seven documented
  pre-existing `-Wunused-function` warnings in `filesystem.c`).
- Logging off: text 666,920, data 420, bss 396,184; no additional warning.
  Logging was restored and the final image rebuilt to the row above.

**Step 4 — documentation.** Close-out notes were applied to
`FILESYSTEM_SPEC.md` (RAM `kit` snapshot, VALID-mirror entry reads, same-type
index reuse, type-step binding rule, legacy `.hctmp` files),
`STORAGE_SRAM_MANIFEST.md` (S090 allocation and numbers), `MEMORY.md` (status,
reuse gates, snapshot ownership, type-step rule, rejected index pool) and
`knowledge_files/log_archive/090_SESSION_HANDOFF_LOG.md`.

**Step 5 — pending user hardware check.** H1-H27 of §8.2 remain. Nothing in
this implementation was exercised on the device here; the logging toggle and
build gates are host-side only.

---

## 13. Post-implementation assessment (2026-10-31)

### 13.1 Verdict

**The implementation conforms to the schedule.** It builds to the scheduled
numbers, and the image in the tree is the image of this source. One cosmetic
difference was found, and its cause is a defect in this schedule's rendering,
not in the implementation (A1). No defect introduced by S090 was found. One
pre-existing gap is reachable on one more path because of M-06 (A2), and one
arithmetic slip is in the SRAM manifest (A3). Hardware H1–H27 (§8.2) remain
the user's check.

### 13.2 What was checked

**Conformance against the verified code.** The seven implemented files were
compared with the scratch tree the schedule was generated from (§11):

| File | Result |
|---|---|
| `presetManager.h`, `presetManager.c` | byte-identical |
| `filesystem.h` | byte-identical |
| `storageTypes.h`, `storageTypes.c` | byte-identical |
| `filesystem.c` | identical except 3 missing blank lines (A1) |
| `menu.c` | identical except 1 missing blank line (A1) |

The schedule body (§0–§11) was also regenerated from the `7f8b5d07` sources
and is identical to the file in the tree apart from the status line and §12.
So the baseline the schedule assumed is the committed tree.

**Independent build** (a copy of the current tree in the session scratchpad;
`build/` in the project was not touched):
- `make all && make img`: exit 0.
- `link_budget.py`: payload 684,036 B, headroom 69,628 B, ITCM 15,240 B,
  DTCM statics 4,372 B, FX margin 3,808 B. Equals §1 D14 and §12.
- `arm-none-eabi-size`: text 683,616, data 420, bss 413,020.
- **`build/LXRV2_lxr02.img` in the project is byte-identical** to the image
  built from the current source (684,052 B file), so the image on hand is this
  implementation.
- Warnings: the same 14 distinct warnings as the baseline (the 20 lines
  quoted in §12 include the libc stub and LTO notes).
- `nm`: `pm_instrument_snapshot` `0x81`; both temp flags absent; the only
  temp-named symbol is the kept `filesystem_isReservedInstrumentTemporaryName`.
- Logging off: exit 0, no additional warning, text 666,920 / data 420 /
  bss 396,184.

**Behaviour review of the as-built code** (paths traced in the tree):
- **Admitted type turn.** From the gate to `menu_handleLoadSaveMenu()`, every
  intervening branch needs `btnClicked`, which is 0 for an admitted turn, so
  the turn reaches only the type-row case. Inside the handler, every branch
  before the `editModeActive` switch is also `btnClicked`-gated.
- **Requests made by an admitted step all refuse without side effects while
  the read runs:**
  - `menu_requestResidentNameScratch()`, `menu_requestSceneEntryName()` and
    `menu_flushForeignNameRows()` defer when the facade is busy;
  - `menu_requestLibraryIndexLoad()` (M-12) and
    `menu_requestInstrumentIndexLoad()` (M-11) defer before touching the
    snapshot, the cache or `menu_storageBusy`;
  - Kit Branch A restores `menu_storageBusy` to its prior owner (S082 F3);
  - `preset_loadName()` (the Samples type) calls `filesystem_ack()`, which is
    a no-op while BUSY.
- **Pending completions.**
  - A cancelled Instrument read returns quietly (M-04).
  - A DONE Instrument read for a type the user has left fails
    `filesystem_instrumentNameCacheLoaded()` and is retried.
  - A library read takes `menu_libraryIndexLoadComplete()`'s stale path when
    its generation snapshot no longer matches. When Kit Branch A's refusal has
    already moved the snapshot, the completion still only repaints: the step
    cleared the cache, and every slot reader is tag-checked, so no wrong name
    can appear.
  - The cancel bit is reset by `filesystem_start()`, so an index read that
    hands off to a rebuild produces a normal (never cancelled) completion.
- **RAM restore gating (D7).** Three cases were traced:
  - The crossing arrives while a pool load is in flight: the restore is
    refused, and the pool completion sets `menu_instrumentKitRestorePending`.
  - `menu_finishInstrumentApplySession()` runs the restore once Preset is
    idle; it returns `menu_storageBusy` = 1, so the restore's apply ticks next.
  - A Morph type mismatch returns 1 with nothing copied; the apply tick
    finishes at once.
- **S4 reuse.** Every writer that can leave an INSTRUMENT tag was checked:
  - the index read and rebuild scan publish the whole index before going idle;
  - a failed read is cleared by Menu, a cancelled one by filesystem;
  - Instrument Save (`filesystem_updateInstrumentCacheAfterSave()`) clears
    first if the tag names another type, then rebuilds that type's index
    before completing.

### 13.3 Findings

**A1 — four blank separator lines missing (cosmetic; schedule defect).**
- **Where:** `menu.c` between `menu_paintTypeStepWhileBusy()` and the
  `menu_handleLoadSaveMenu` header (M-16). In `filesystem.c`: between
  `filesystem_finishCancelledBrowseRead()` and the Instrument index reader's
  comment (F-06); between Instrument phase 27 and `default:` (F-11); between
  library phase 10 and `default:` (F-16).
- **Cause:** the generator that rendered §3–§7 strips trailing newlines from
  each code block, which dropped the final blank line of these four ADD
  blocks. The pre-verified edits had it. The implementer applied the document
  as written.
- **Effect:** none on the build (whitespace only); it breaks the file's
  blank-line convention between functions and between cases.
- **Fix if wanted:** insert one empty line at each of the four places. No
  rebuild is needed for correctness.

**A2 — a failed library index read leaves its domain tag (pre-existing; one
more path since M-06; low).**
- **What:** `filesystem_requestReloadLibraryIndex()` tags the cache when the
  request starts (`filesystem_prepareLibraryNameCache()`).
  `menu_libraryIndexLoadComplete()` does not clear it when `index_ok` is 0.
  After a failed Kit/Scene/Bank/Pattern read, the tag survives with empty
  rows, and the page shows `Empty` for every slot until the cache is cleared
  (type change, exit).
- **Before S090:** this was already visible right after the failure, and on
  any Kit Branch B re-entry while the name scratch stayed valid.
- **What M-06 adds:** one more route to Kit Branch B without a clear. A Kit
  page source-Scene change with dirty names runs the flush; the flush
  completion (M-05) and the re-read of `.hcnames` (M-06) no longer clear;
  Branch B then trusts the failed cache.
- **Conditions:** a card read failure on the Kit index earlier in the same
  Load/Save session. Not reachable on a healthy card.
- **Narrow fix (not applied; user decision):** in
  `menu_libraryIndexLoadComplete()`, call `filesystem_clearNameCache()` when
  `index_ok` is 0, mirroring `menu_instrumentIndexLoadComplete()`. This
  touches only Menu's own handling of a failed read, and it closes the gap on
  all four library types (U6).

**A3 — arithmetic slip in `STORAGE_SRAM_MANIFEST.md` (documentation).**
- **Error:** the S090 entry says "129 + 2 removed = +127, plus 5 B of linker
  alignment". It lists three removed variables (1 + 1 + 2 B), so the symbol
  sum is +129 − 4 = **+125**, and the alignment is **7 B** (§1 D14). The
  +132 B total is right.
- **Wording:** "`op_instrument_load_index` … became unreferenced" should read
  "was already write-only; LTO now drops it".

**Observation (no action).** A Kit ↔ KitMrp type step clears the cache and
rereads the shared Kit index, as before S090 (the type-row clear runs on any
type change). Item 7 inherits this: such a step during a Kit read cancels it
and starts it again.

### 13.4 Open

- Hardware matrix §8.2 (H1–H27), with H7b and H23–H26 as the new behaviour.
  H18 must show no underrun.
- §1 D12: the LSR-01 flush cost on a VOICE change after previews, to be
  weighed against the target time (plan §10 Q8).
- A1 (optional whitespace), A2 (decide on the one-line fix), A3 (doc
  correction).

### 13.5 Fixes applied (2026-10-31, user: "make the narrow, direct fixes for A1-A3")

- **A1:** the four blank separator lines were restored. `filesystem.c` is now
  byte-identical to the pre-verified code (§11); `menu.c` differs from it only
  by the A2 block.
- **A2:** `menu_libraryIndexLoadComplete()` (`menu.c` after the stale
  `!coordinate_current` return) now calls `filesystem_clearNameCache()` when
  the current read ended ERROR, with a comment block giving the reason. There
  is no overlay and no other behaviour change; a cancelled read (already
  cleared by filesystem) clears again harmlessly.
- **A3:** the `STORAGE_SRAM_MANIFEST.md` S090 entry now reads "+129 − 1 − 1 −
  2 = +125, plus 7 B of linker alignment" and "`op_instrument_load_index` …
  was already write-only and LTO now drops it".
- **Build** (in the project tree, for the user's hardware test): `make all &&
  make img` exit 0; no new warning; `git diff --check` clean.

  | | §1 D14 | After A2 |
  |---|---:|---:|
  | text | 683,616 | 683,648 (+32) |
  | data / bss | 420 / 413,020 | 420 / 413,020 |
  | image payload | 684,036 | 684,068 (headroom 69,596 B) |
  | `build/LXRV2_lxr02.img` | 684,052 B | 684,084 B |

  The §8.1 gate 3 numbers move by these +32 B.
- **Hardware for A2** (only reachable after a card read error on a library
  index): after such a failure the page shows blank names instead of
  `Empty`, and the next entry or selection reads the index again.
