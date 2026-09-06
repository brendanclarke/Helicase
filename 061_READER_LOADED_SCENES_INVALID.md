# 061 — Boot Reader Invalidates the Four /Scene/-Sourced Scenes

Status: diagnosis confirmed; Option A adopted as the sole fix (Option B
rejected); full implementation schedule below. No code has been changed.

## 1. Symptom and expected result

Booting the current firmware (`build/LXRV2_lxr02.img`, byte-identical to
`SD_CARD_READER_3/LXRV2_lxr02.img`) with the contents of `SD_CARD_READER_3/`
invalidates the scenes and loads them empty ("AutoSave Sc NN empty"
notices).

Expected: Bank 001 "Full" should load with four scenes sourced from the
root `/Scene/` library — slots 0, 1, 14, 15 all sourced from
`Scene/013 SoyEared` — and the remaining twelve scenes sourced from the
Bank's own child folders.

What actually fails: exactly those four library-sourced scenes are
invalidated at their Kit row and emptied under P1 (all-or-nothing
per-Scene trust). The twelve Bank-inherited scenes resolve and load
correctly. Details and evidence below.

## 2. Card state (SD_CARD_READER_3)

- `settings.cfg`: `active_bank=1`.
- `.hcprms1` generation 13, `.hcprms2` generation 14; the valid winner is
  `.hcprms2` gen 14 with bank slot `001`, `present_mask=0xffff`,
  `active_scene=6`. Its Scene payload is the older pre-session Bank
  content (Barf/Slak/Hard/…). Because the winner's Bank slot matches
  `active_bank`, main.c stage 11 runs
  `filesystem_autosaveBootReaderBlocking()` instead of the canonical
  `preset_loadBank()` (`filesystem_hasBootWinner()` at
  `Core/Hardware/SD/filesystem.c:25058`).
- `.hcnames` (header plus 129 rows, every row carries `R`):
  - Bank row: `Full 001 R`.
  - Scene rows 0, 1, 14, 15: `SoyEared 013 R`.
  - Scene rows 2..13: `- R`, names matching the Bank tree.
  - Kit rows: all `- R`; the names are the Bank tree's kit names —
    row 17 `Barf`, row 18 `LoadTst`, …, row 31 `808cb`, row 32 `Pop`.
  - Instrument rows: all `- <type> R`.
- The session that produced this card ended while the Load/Save page guard
  held the autosave writer (`AUTOSAVE_TRACE_STAGE_WRITER_SUPPRESSED` `W`
  records in `asavetrc.bin`), so `.hcprms` still holds the pre-session
  payload and every `.hcnames` row is still REFRESHED.
- This copy predates the failing boot: the register still shows `013 R`
  scene rows and the records are still gens 13/14. A failing boot would
  have rewritten the four scenes' rows to `? R` (see §6) and the drain
  would have published gens 15/16. The only `Q` summary in the current
  trace (`case2=0x0000 case3=0x0000`) is from an earlier Case-1 boot in
  the same session, not from the failing boot.

## 3. Precise failure chain

1. Every row is REFRESHED, so per-Scene evaluation takes the Case 2/3
   path for every row (`filesystem_bootReaderEvaluateScene()`,
   `Core/Hardware/SD/filesystem.c:27148`).
2. For scenes 0, 1, 14, 15 the Scene row resolves to source slot 13 with
   `resolved_row` = the Scene row itself. The Case-2 narrow Scene load
   succeeds: `Scene/013 SoyEared/sceneset.scg` exists and parses.
3. The Kit row (`- R`) resolves upward: Kit row → Scene row → source 013,
   `resolved_row` = Scene row (`filesystem_resolveResidentSource()`,
   `filesystem.c:5517`). The narrow Kit loader
   (`filesystem_bootReaderNarrowLoadKit()`, `filesystem.c:26749`) calls
   `filesystem_bootReaderEnterKitFolder()` (`filesystem.c:26526`). Its
   embedded branch builds the physical folder name from the Kit row's
   display-name mirror:

   ```c
   filesystem_makeSceneEmbeddedKitDir(
       dir_name, sizeof(dir_name), hcnames_name_mirror[kit_row]);
   return filesystem_bootReaderEnterDirectory(dir_name);
   ```
   (`filesystem.c:26551`)

   The stale Kit-row names produce paths that do not exist in the library
   Scene:

   | Scene | Kit row | Stale row name | Path the reader builds (inside `Scene/013 SoyEared/`) | Real folder |
   |-------|---------|----------------|------------------------------------------------------|-------------|
   | 0     | 17      | `Barf`         | `Kit Barf`                                           | `Kit SoyEared` |
   | 1     | 18      | `LoadTst`      | `Kit LoadTst`                                        | `Kit SoyEared` |
   | 14    | 31      | `808cb`        | `Kit 808cb`                                          | `Kit SoyEared` |
   | 15    | 32      | `Pop`          | `Kit Pop`                                            | `Kit SoyEared` |

   `filesystem_bootReaderEnterDirectory()` finds no case-insensitive
   match, so `filesystem_bootReaderEnterKitFolder()` returns 0 and the
   narrow Kit load fails.
4. `load_ok == 0` fires Case 3 (`filesystem.c:27275`): P1 — the whole
   Scene is invalidated and emptied by
   `filesystem_bootReaderEmptyScene()` (`filesystem.c:27087`), its eight
   rows are set to `UNKNOWN|REFRESHED`, the scene's bit is set in
   `case3_scene_mask`, and the remaining rows of that Scene stop being
   evaluated. Menu's notice sequencer then shows "AutoSave Sc NN empty"
   (`Core/Menu/menu.c:427` and `menu.c:8214`).
5. The Kit row is evaluated before the six Instrument rows, so the
   Instrument rows are never reached for these scenes.

## 4. Why only the four /Scene/-sourced scenes fail

Scenes 2..13 have Kit rows whose display names match the Bank tree, and
their Kit rows resolve through the Scene row (`-`) to the Bank row 001
(`resolved_row = 0`). The embedded branch then enters
`Bank/001 Full/NN <name>/` and builds `Kit <kit-row name>` — e.g.
`Kit RedSnap` inside `02 RedSnap` — which exists. All of their narrow
loads (Scene, Kit, six Instruments, Pattern) succeed.

So the observed "all scenes invalidated" symptom is the four
library-sourced scenes; the twelve Bank-inherited scenes load normally.
If hardware shows literally all sixteen empty, that would be an
additional factor beyond this analysis — but the card data and code path
above show exactly four.

## 5. Origin of the stale Kit-row name (writer-side gap)

The root Scene Load does capture the real embedded Kit folder name:

- `filesystem.c:11488-11494` stores the text after `"Kit "` from the
  Scene folder scan into `op_scene_child_display_name`.
- `filesystem.c:11783-11784` stages it into the identity store
  (`filesystem_setIdentityName(FS_IDENTITY_KIT_ROW, …)`), and the source
  rows are cascaded at `filesystem.c:11794-11815` (Scene row = `op_slot`,
  Kit and Instrument rows = INHERIT).

But the Scene action's HCNAMES update
(`FS_INTERNAL_OP_UPDATE_HCNAMES_SCENE`, dispatched at
`filesystem.c:6158`/`6373`) calls
`filesystem_cacheCurrentResidentSceneNames()` (`filesystem.c:5975`),
which refreshes only Scene-row names. The Kit row keeps its previous
name from the old register image ("Barf"), while only its source is
staged to INHERIT. The captured Kit name is never written into the Kit
row.

Contrast: the Kit action's update calls
`filesystem_cacheCurrentResidentKitNames()` (`filesystem.c:5927`), which
does refresh the Kit row plus six Instrument rows from the identity
store. The Scene path has no equivalent overlay.

The result is a durable register that is internally inconsistent: the
Scene row says the data lives in `Scene/013 SoyEared`, while its Kit row
still names `Barf`, a kit that exists only inside
`Bank/001 Full/00 Barf`. `kitset.kcg` carries no name (object identity
comes from directory names), and the kit name is not derivable from the
scene name (for example `Scene/065 MochMod/Kit Moch to`), so the reader
has no other durable source to correct the stale mirror.

## 6. Consequence: the failure is sticky across boots

`filesystem_bootReaderEmptyScene()` retains the display names and sets
sources to `?` + `R`. On the next boot the Scene row (`?`) resolves via
the Bank row to slot 001, and the Bank branch of
`filesystem_bootReaderEnterSceneFolder()` builds the child directory from
the retained scene name: `00 SoyEared` — which does not exist (the Bank
child is `00 Barf`). The same four scenes therefore fail again on every
subsequent boot until the user reloads content into those slots or the
firmware is fixed. The drained `.hcprms` record also persists the emptied
four scenes, but the `R` flags keep every row on the Case 2/3 path, so
the stale payload is never trusted for them.

Note: no new code is scheduled against the sticky state itself. It is a
consequence of Case 3, not a cause; once the writer-side fix below stops
Case 3 from firing, the state can no longer be produced. A card that is
already in the sticky state still needs its four slots reloaded (or the
register corrected) once, because Option B — reader-side folder
discovery that would self-heal it — is rejected (§8).

## 7. Trace evidence and expected failing-boot signature

`SD_CARD_READER_3/asavetrc.bin` does not contain the failing boot (see
§2). Predicted signature for hardware confirmation of the next boot:

- Per-row `Q` records with flags `0x02` (Case 3) for the four Kit rows:
  values `0x00001100` (Scene 0, row 17), `0x00001201` (Scene 1, row 18),
  `0x00001f0e` (Scene 14, row 31), `0x0000200f` (Scene 15, row 32).
- Summary `Q` record with flags `0x80`: value `0xf003ffff`
  (`case2=0xffff`, `case3=0xf003`).
- Post-boot `.hcnames`: the four scenes' eight rows become `? R` with
  retained names; `.hcprms` advances to gens 15/16 with those four
  scenes zeroed.

Ruled out as causes:

- The Phase 5b Instrument-row source guard (`filesystem.c:27239`) — not
  triggered here (`resolved_row != instrument row`), and evaluation stops
  at the Kit row before Instrument rows anyway.
- A stale winner payload — real but irrelevant: every row is REFRESHED,
  so payload bytes are never applied; the Bank section's present mask is
  `0xffff`, so the reader proceeds.
- A failed `.hcnames` parse — the register parses cleanly (valid header,
  129 valid rows).

## 8. Decision

**Option A is the fix. Option B is rejected and must not be implemented.**

Option B (reader scans the resolved Scene folder for the one `"Kit "`
child instead of trusting the Kit-row name) would mask genuine register
inconsistencies instead of eliminating them: the writer would go on
producing stale names, Menu would keep displaying them, and the first
real corruption would surface somewhere else, later. The durable register
must be made correct at the moment of every Load/Save; the reader stays
strict and keeps failing loudly when the register lies. This is the same
reasoning as P1 and the existing create-capable absence-proof policy.

### The invariant this plan implements

Every committed **Load or Save must mark, in `/.hcnames`, the object it
committed and every child it committed**, for every destination:

- **Bank Load/Save** — the Bank row and, for every child Scene, the Scene
  row, its Kit row, and all six Instrument rows. (Verified correct;
  reference model, §9.)
- **Scene Load/Save** — the Scene row, its Kit row, and all six
  Instrument rows of every destination Scene. Its Effect and Pattern
  children have no `.hcnames` rows today; they are noted for future
  implementation (§11.2).
- **Kit Load/Save** — the Kit row and all six Instrument rows of every
  destination Scene. (Verified correct, §9.)
- **Instrument Load/Save** — its own Instrument row. (Verified correct,
  §9.)

**"Mark" means three things at once:**

1. the **name cell** carries the committed object's true display name
   (the name the physical directory/file was just read from or written
   under);
2. the **source cell** carries the committed provenance (direct numbered
   slot, `-` INHERIT, or `@` INSTRUMENT_DIRECT);
3. the **refreshed witness** (`R`) is set on every marked row so the
   autosave writer treats the freshly committed payload as unproven
   until the drain captures it (Phase B2 convergence).

All three are staged before the one deferred HCNAMES rewrite so the
old register image cannot overwrite them (`FS_RESIDENT_SOURCE_DIRTY_FLAG`
protection in `filesystem_cacheResidentRecord()`), and they are published
through the single shared register writer.

## 9. Complete site inventory (evidence from the current tree)

| Operation | Site | What it marks today | Verdict |
|-----------|------|---------------------|---------|
| Bank Load — per child | `filesystem.c:12572`, `filesystem.c:12588` | Scene+Kit+6 Instrument names/sources (`cacheCurrentBankSceneNameBlock`), R via `setResidentSceneRefreshed` | Correct — reference |
| Bank Load — Bank row | `filesystem.c:13412`, `filesystem.c:13588` | Bank row source + R | Correct |
| Bank Save — Bank row | `filesystem.c:17507-17529` | row 0 name/source/R; child rows preserved as-is | Correct (see §11.1 finding) |
| Bank Save — children | `filesystem.c:15369` (`prepareBankSceneSaveSource`), delegated Scene writer | tree written from register names; no re-staging (skips `filesystem.c:18261`) | Correct for register-consistent trees; finding noted §11.1 |
| Kit Load | `filesystem.c:11024-11034` (identity), `11044-11057` (sources), `11073-11084` (R) | Kit + 6 Instrument rows per destination | Correct |
| Kit Save | `filesystem.c:16603-16604` (kit identity), `16610-16633` (sources), `16640-16651` (R) | Kit + 6 Instrument rows | Correct; Instrument names rely on Menu traversal contract (`menu.c:3840-3846`) |
| Kit action register overlay | `filesystem.c:5927` (`cacheCurrentResidentKitNames`), dispatched `6157`/`6372` | Kit + 6 Instrument name cells from identity store | Correct — the model for C1 |
| Scene Load — identity/source | `filesystem.c:11488-11494` (capture), `11783-11791` (identity), `11794-11815` (sources) | Scene row name/source + Kit/Instrument sources | Names staged but never published; **gap 1** |
| Scene Load — refreshed witness | none | no `R` is set anywhere in the Scene Load path (`setResidentSceneRefreshed` exists only at `12572` and `18347`) | **gap 2** |
| Scene Save — staging | `filesystem.c:18303-18349` | Scene/Kit/Instrument sources + R; Kit/Instrument name cells not refreshed | **gap 1** (same as Load) |
| Scene Save — request capture | `filesystem.c:27659-27724` | `op_save_scene_kit_display_name` built from identity store, which may be stale when Save:[Scene] is entered without a Kit/Instrument traversal | **gap 3** |
| Scene action register overlay | `filesystem.c:5975` (`cacheCurrentResidentSceneNames`), dispatched `6158`/`6373` | Scene-row name cells only | **gap 1** (dispatch site for C2) |
| Instrument Load | `filesystem.c:14477-14526` | own row identity/source/R | Correct |
| Instrument Save | `filesystem.c:14894-14927` | own row identity/source/R | Correct |
| Menu identity seeding | `menu.c:3840-3846` (Kit/Instrument entry), `menu.c:4336-4343` (Scene entry), `menu.c:8882-8887` (Kit Save completion) | identity store population contract | Correct, keep |

## 10. Implementation schedule

Scope: `Core/Hardware/SD/filesystem.c` only, plus documentation. No
header-file API change is required: every new helper is file-static, and
every other change reuses existing statics and accessors. The comment
blocks below are written to be pasted verbatim as the adjacent comment
text at each site.

### C1 — New static helper `filesystem_cacheCurrentResidentSceneChildNames()`

File: `Core/Hardware/SD/filesystem.c`. Operation: ADD, immediately after
`filesystem_cacheCurrentResidentSceneNames()` (currently ends around
`filesystem.c:5999`). Also ADD the forward declaration beside the
existing resident-name static prototypes near `filesystem.c:1414` (where
`filesystem_setResidentRefreshed()` and
`filesystem_setResidentSceneRefreshed()` are declared), since the helper
is needed at `filesystem.c:6156`/`6371`, which precede its definition.

Comment to attach:

```c
/*
 * Replace the Kit-and-Instruments block of every Scene a Scene action
 * committed.
 *
 * What: for every destination Scene in op_scene_load_scene_mask, overlays
 * that Scene's Kit row and its six Instrument rows in the borrowed
 * HCNAMES cache from the operation-scoped identity store. This is the
 * Scene-action counterpart of filesystem_cacheCurrentResidentKitNames():
 * the same seven name cells, selected by the Scene-operation mask instead
 * of the Kit-operation mask.
 *
 * Why: a root Scene Load/Save replaces the entire embedded hierarchy, so
 * the durable register must name the new Kit and member Instruments
 * exactly as the physical "Kit <name>" folder and member files were read
 * or written. The existing Scene update refreshes only Scene-row names,
 * which leaves stale Kit-row names behind and makes the boot reader's
 * Case-2 narrow Kit path construct a folder that does not exist (Session
 * 061, 061_READER_LOADED_SCENES_INVALID.md). Bank, Scene, Kit, and
 * unselected Instrument rows remain copies of the file that was read.
 *
 * Inputs: op_scene_load_scene_mask (request-stable destination mask,
 * single-bit for Scene Save, arbitrary for multi-destination Scene Load)
 * and the identity store (FS_IDENTITY_KIT_ROW,
 * FS_IDENTITY_INSTRUMENT_ROW_0..5) staged by the Scene action's commit
 * path before this update ran. Outputs: hcnames_name_mirror[] cells for
 * rows 17..32 and 33..128 of every selected Scene; no file I/O and no
 * source-cell or refreshed-witness change (sources and R were staged at
 * commit).
 *
 * Accessors: filesystem_residentKitRow(),
 * filesystem_residentInstrumentRow(), filesystem_identityName(),
 * filesystem_cacheResidentName(). Affiliates:
 * filesystem_cacheCurrentResidentSceneNames() (Scene-row names),
 * filesystem_cacheCurrentResidentKitNames() (the Kit-op model),
 * filesystem_residentNames_tick() phases 3 and 7,
 * filesystem_requestSaveSceneDirectory(),
 * filesystem_prepareBankSceneSaveSource(), and the Scene Load commit
 * block near filesystem.c:11783.
 */
```

Implementation shape (mirrors `filesystem_cacheCurrentResidentKitNames()`
at `filesystem.c:5927`, swapping `op_kit_load_scene_mask` for
`op_scene_load_scene_mask`):

```c
static void filesystem_cacheCurrentResidentSceneChildNames(void)
{
    uint8_t scene_index;

    for (scene_index = 0u;
         scene_index < STORAGE_BANK_SCENE_MAX_SLOTS;
         scene_index++) {
        uint16_t row;
        uint8_t slot;
        if ((op_scene_load_scene_mask &
             (uint16_t)(1u << scene_index)) == 0u) {
            continue;
        }
        row = filesystem_residentKitRow(scene_index);
        if (row < FS_RESIDENT_NAMES_ROW_COUNT) {
            filesystem_cacheResidentName(
                row, filesystem_identityName(FS_IDENTITY_KIT_ROW));
        }
        for (slot = 0u; slot < STORAGE_KIT_SLOT_COUNT; slot++) {
            row = filesystem_residentInstrumentRow(scene_index, slot);
            if (row >= FS_RESIDENT_NAMES_ROW_COUNT)
                continue;
            filesystem_cacheResidentName(
                row, filesystem_identityName((uint8_t)(
                    FS_IDENTITY_INSTRUMENT_ROW_0 + slot)));
        }
    }
}
```

### C2 — Dispatch C1 from both HCNAMES update phases

File: `Core/Hardware/SD/filesystem.c`. Operation: MODIFY the two
dispatch sites inside `filesystem_residentNames_tick()`:
`filesystem.c:6156-6161` (phase 3, existing-register update) and
`filesystem.c:6371-6376` (phase 7, absent-register bootstrap). In both,
the `FS_INTERNAL_OP_UPDATE_HCNAMES_SCENE` branch currently calls only
`filesystem_cacheCurrentResidentSceneNames()`; it must now call
`filesystem_cacheCurrentResidentSceneChildNames()` immediately after.

Comment to attach (once, at the phase-3 site; the phase-7 site gets the
same one-line pointer):

```c
/*
 * Scene actions now publish the complete committed hierarchy.
 *
 * What: the Scene-op register overlay runs both existing helpers in
 * order — Scene-row names first, then the committed Kit and Instrument
 * rows from the identity store. Why: a Scene Load/Save replaces the
 * whole embedded hierarchy, and the reader's Case-2 Kit/Instrument
 * resolution derives physical folder names from these rows; publishing
 * only the Scene row left the previous Bank-embedded Kit names behind
 * and invalidated library-sourced Scenes at boot (Session 061). Inputs:
 * the fully read old register image plus the commit-staged identity
 * store. Outputs: the complete per-Scene name block in the borrowed
 * cache before the temp-file writer streams it. Affiliates:
 * filesystem_cacheCurrentResidentSceneNames(),
 * filesystem_cacheCurrentResidentSceneChildNames(), the Scene Load/Save
 * commit paths, and the boot reader's narrow Kit/Instrument loaders.
 */
```

The phase-7 bootstrap branch must make the same double call so a first
register created by a Scene action also carries child names.

### C3 — Scene Load commit: set the refreshed witness on every destination

File: `Core/Hardware/SD/filesystem.c`. Operation: ADD inside the
Scene Load commit block, immediately after the source cascade that ends
at `filesystem.c:11815` and before `op_close_status = FS_STATUS_DONE;`.
A second loop over the selected destinations (or a fold into the
existing source-staging loop) must call
`filesystem_setResidentSceneRefreshed(scene_index)` for every Scene in
`op_scene_load_scene_mask`, exactly as Scene Save does at
`filesystem.c:18347` and Bank Load does at `filesystem.c:12572`.

Comment to attach:

```c
/*
 * Scene Load must mark the complete committed hierarchy refreshed.
 *
 * What: sets the refreshed witness on the Scene, Kit, and six Instrument
 * rows of every destination Scene after the payload committed. Why: the
 * witness tells the autosave boot reader that these rows have data the
 * writer has not proven captured yet, forcing the Case-2/3 evaluation
 * that reloads from the just-established source instead of trusting the
 * stale winner payload (Case 1). Scene Save and Bank Load already set
 * this witness; Scene Load previously set none, so a Scene loaded onto a
 * converged register could be silently restored from old autosave bytes
 * on the next boot. Inputs: op_scene_load_scene_mask and the staged
 * sources from the preceding block. Outputs: bit 13 set on the selected
 * scenes' eight rows; no file I/O (the deferred HCNAMES rewrite
 * publishes it). Accessors: filesystem_setResidentSceneRefreshed().
 * Affiliates: filesystem_setResidentRefreshed() (filesystem.c:5857),
 * the Scene Save equivalent at filesystem.c:18347, the Bank Load
 * equivalent at filesystem.c:12572, and the Phase B2 convergence
 * pipeline that later clears the witness.
 */
```

### C4 — Scene Save request: seed the identity store from the source Scene's register rows

File: `Core/Hardware/SD/filesystem.c`. Operation: MODIFY
`filesystem_requestSaveSceneDirectory()` at `filesystem.c:27659`,
immediately before the `filesystem_makeSceneEmbeddedKitDir()` call at
`filesystem.c:27720-27724` (after `filesystem_setIdentityName(
FS_IDENTITY_SCENE_ROW, display_name)` at `filesystem.c:27720`).

Comment to attach:

```c
/*
 * Capture the source Scene's Kit and Instrument names before writing.
 *
 * What: seeds FS_IDENTITY_KIT_ROW and the six Instrument identity cells
 * from the source Scene's own register rows. Why: Scene Save writes the
 * embedded "Kit <name>" folder from the Kit identity and derives every
 * member filename from the Instrument identities
 * (filesystem_memberFilename()), then the post-save HCNAMES update
 * republishes those same identity cells. Save:[Scene] must therefore be
 * correct even when the user never traversed the Kit/Instrument menu,
 * whose entry read would otherwise be the only place these identities
 * were seeded. Inputs: source_scene (request-stable) and the current
 * register mirror. Outputs: the identity store names the exact Kit and
 * member stems this save is about to write; no new storage.
 * Accessors: filesystem_residentKitName(),
 * filesystem_residentInstrumentName(), filesystem_setIdentityName().
 * Affiliates: filesystem_makeSceneEmbeddedKitDir(),
 * filesystem_memberFilename(),
 * filesystem_cacheCurrentResidentSceneChildNames(),
 * filesystem_prepareBankSceneSaveSource(), and Menu's entry-time
 * identity seeding (menu.c:3840-3846).
 */
```

Shape:

```c
filesystem_setIdentityName(
    FS_IDENTITY_KIT_ROW,
    filesystem_residentKitName(source_scene));
for (uint8_t voice = 0u; voice < STORAGE_KIT_SLOT_COUNT; voice++) {
    filesystem_setIdentityName(
        (uint8_t)(FS_IDENTITY_INSTRUMENT_ROW_0 + voice),
        filesystem_residentInstrumentName(source_scene, voice));
}
```

### C5 — Bank Save per-child preparation: stage the Kit identity too

File: `Core/Hardware/SD/filesystem.c`. Operation: MODIFY
`filesystem_prepareBankSceneSaveSource()` at `filesystem.c:15369`, next
to the existing Instrument-identity staging at
`filesystem.c:15402-15406` (which currently runs after the
`filesystem_makeSceneEmbeddedKitDir()` call).

Comment to attach:

```c
/*
 * Stage the child's Kit identity from its own register row.
 *
 * What: copies the child Scene's Kit-row name into FS_IDENTITY_KIT_ROW
 * beside the existing six Instrument-identity captures. Why: the
 * prepare helper already rebuilds the embedded "Kit <name>" child and
 * the six member stems from the same register rows it was handed; the
 * identity store must name the same hierarchy so any later Scene-op
 * register overlay (C1/C2) or member-filename derivation agrees with
 * what this child wrote. Without it the Kit identity can remain stale
 * after a Bank Save while the Instrument identities are fresh.
 * Inputs: scene_index and the register mirror. Outputs: the identity
 * store's Kit cell; no file I/O. Accessors:
 * filesystem_cachedResidentName(), filesystem_residentKitRow(),
 * filesystem_setIdentityName(). Affiliates:
 * filesystem_makeSceneEmbeddedKitDir(), filesystem_memberFilename(),
 * filesystem_saveSceneDirectory_tick() phase 8,
 * filesystem_cacheCurrentResidentSceneChildNames().
 */
```

Shape (placed before the `filesystem_makeSceneEmbeddedKitDir()` call in
the helper):

```c
filesystem_setIdentityName(
    FS_IDENTITY_KIT_ROW,
    filesystem_cachedResidentName(
        filesystem_residentKitRow(scene_index)));
```

### C6 — Verified sites: no code change (close the enumeration)

These sites already satisfy the invariant and are listed so the schedule
is complete; each keeps its existing comments:

- Kit Load identity/source/R staging, `filesystem.c:11024-11084`.
- Kit Save identity/source/R staging, `filesystem.c:16603-16651`.
- Bank Load per-child block and Bank-row staging,
  `filesystem.c:12572`, `12588`, `13412`, `13588`.
- Instrument Load/Save single-row staging,
  `filesystem.c:14477-14526` and `14894-14927`.
- Menu identity seeding and Kit-Save reaffirmation,
  `menu.c:3840-3846`, `menu.c:4336-4343`, `menu.c:8882-8887`.

## 11. Explicit non-changes and scoping findings

### 11.1 Bank Save child rows are preserved, not re-staged

Bank Save writes every child from the register's own names
(`filesystem_prepareBankSceneSaveSource()`), so the register stays
name-consistent, and its per-child writer deliberately skips the
Scene-op HCNAMES staging branch (`filesystem.c:18261`). Children's
sources/R therefore remain whatever they already were (INHERIT+R after
the preceding Bank Load). One residual nuance: a child whose Scene row
carried a direct library source (e.g. `013`) keeps it after a Bank Save
even though the Bank now also holds a copy. The data is identical and
the source remains resolvable, so no boot failure results; per the
session decision ("Bank seems to do this correctly already") this is
accepted and no Bank Save change is scheduled. If stricter provenance
is later wanted, it is a separate, Bank-Save-scoped change to stage all
written children to INHERIT+R in the Bank writer's own phases —
deliberately out of scope here.

### 11.2 Effect and Pattern: future-implementation notes

Scene Load and Scene Save already move `pattern.pat` and `effects.fx`
physically (Scene Load pattern parse at `filesystem.c:12408`, effect
placeholder phases 56-60; Scene Save pattern write phases 29-31 and
effect placeholder phases 33-36). But neither child has an `.hcnames`
identity row: Pattern storage is explicitly "not the final dynamic
Pattern format" and Effect is a validation-only placeholder (`scene_t`
carries no effect field, 0 live params). Therefore the invariant can
only note them for the future, exactly as required:

Comment to attach at the Scene Load pattern phase (`filesystem.c:12408`)
and at the Scene Save pattern/effect phases (`filesystem.c:18114`,
`filesystem.c:18130`):

```c
/*
 * Unregistered Scene child (future HCNAMES row).
 *
 * What: pattern.pat / effects.fx are committed by this Scene action but
 * have no /.hcnames identity row today (Pattern format is not final;
 * Effect is a validation-only placeholder with zero live parameters).
 * Why: the Session 061 invariant requires every Load/Save to mark all
 * committed children; these two children cannot be marked until they
 * gain durable identity rows. When they do, they must join the Scene
 * action's marked-children block (filesystem_cacheCurrentResidentScene
 * ChildNames(), the refreshed-witness staging, and the boot reader's
 * per-Scene evaluation) in the same change that introduces their rows.
 * Affiliates: 061_READER_LOADED_SCENES_INVALID.md §11.2,
 * filesystem_cacheCurrentResidentSceneChildNames(), and the boot
 * reader's Case-2 narrow loaders.
 */
```

### 11.3 The sticky state (§6) gets no code

Per §8, Option B is rejected; nothing in this schedule self-heals a card
that is already in the Case-3 sticky state. The verification plan
reloads the four slots once on the fixture.

## 12. Documentation updates (part of the schedule)

1. `knowledge_files/specification_reference/FILESYSTEM_SPEC.md` — in the
   "Root resident-name register" section, add the invariant verbatim
   from §8 (every Load/Save marks all committed children; the three-part
   definition of "mark"; the effect/pattern future note).
2. `S061_AUTOSAVE_READER.md` §6/§7 — add one sentence: the Scene-op
   HCNAMES update now publishes the complete committed hierarchy
   (Scene + Kit + six Instruments), so the reader's Case-2 Kit path can
   trust the Kit-row name; effect/pattern rows remain future work.
3. This document stays the implementation schedule until the changes
   land; convert it to a session handoff entry afterwards.

## 13. RAM allocation statement

No new RAM. C1 reuses the 129 x 9-byte HCNAMES mirror, the 63-byte
identity store, and the existing op-scratch; C2-C5 add no state. This
complies with the RAM Allocation Approval Policy without any new
reservation.

## 14. Hardware verification plan

1. **Reported fixture.** Boot the `SD_CARD_READER_3` contents with the
   fixed image: no "empty" notices; scenes 0, 1, 14, 15 play SoyEared
   content and scenes 2..13 play their Bank-tree content; summary `Q`
   shows `case3=0x0000` and the register afterwards shows kit rows
   17/18/31/32 as `SoyEared - R`.
2. **Converged-register Scene Load.** After a clean drain (no `R`
   anywhere), load a root Scene into one Bank slot from the menu, power
   off in the menu, capture the card: the Scene row must carry the
   library slot + `R`, the Kit row must carry the library scene's real
   kit name + `- R`, and the six Instrument rows the library kit's
   member stems + `- R`. Boot the capture: the slot restores from the
   library (Case 2), not from the stale payload.
3. **Scene Save.** Save a resident Scene to a root slot, then boot: the
   register rows match the physical `Kit <name>` folder and member
   stems just written, all with `- R`; the boot reader Case-2 loads it.
   Repeat the save without any prior Kit/Instrument menu traversal
   (exercises C4).
4. **Kit Load/Save regression.** Multi-destination Kit Load and a Kit
   Save; confirm Kit + six Instrument rows still refresh exactly as
   before (C6 sites untouched).
5. **Bank Save regression.** Save the Bank after fixture 2; confirm the
   child rows remain consistent with the written tree and the Bank row
   updates (C5 has no behavioral change to the Bank writer).
6. **Sticky card recovery.** On a card already in the §6 sticky state,
   reload the four slots once, then power-cycle to confirm the failure
   does not return.
7. **Trace checks.** Verify the expected boot `Q` records on fixture 1,
   and confirm the post-boot drain eventually clears the `R` witnesses
   through the existing Phase B2 convergence pipeline.
