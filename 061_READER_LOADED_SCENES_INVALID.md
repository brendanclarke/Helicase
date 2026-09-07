# 061 — Boot Reader Invalidates the Four /Scene/-Sourced Scenes

Status: diagnosis confirmed; Option A adopted as the sole fix (Option B
rejected). The implementation schedule (§10) has been fully landed in
`Core/Hardware/SD/filesystem.c` and `filesystem.h` with build verification;
see the implementation log at the end of this file. Hardware fixtures 1-7
(§14) remain outstanding; this document stays the working record until they
pass, then converts to the session handoff per §12.3. Phase 2 (the
HCNAMES-authoritative boot load for the bank-loaded-then-powered-off case)
was implemented in this pass per §16 (code and docs uncommitted,
build-verified; implementation log in §17), hardware fixtures 16.8 pending.

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
| Scene Load — refreshed witness | `filesystem_loadSceneDirectory_tick()` case 61 (old tree `12572`, new tree `12678`) | `filesystem_setResidentSceneRefreshed()` loop over `op_scene_load_scene_mask` at the successful terminal boundary, after Pattern/Effect complete | Correct — pre-existing (Session 060 Phase C); no new code needed, see §15.2 |
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

### C3 — Scene Load refreshed witness — resolved without a code change

Status: NOT IMPLEMENTED AS LITERALLY SCHEDULED — the premise was wrong.
The shared Scene loader already marks every destination Scene plus its Kit
and six Instruments refreshed at its successful terminal boundary
(`filesystem_loadSceneDirectory_tick()` case 61, new tree line 12678;
Session 060 Phase C), after Pattern/Effect complete, ungated by
`current_op` so it covers root Scene Load and Bank-delegated child loads
alike. Adding the scheduled second `R` staging inside the commit block
would set the witness before Pattern/Effect I/O completes, contradicting
the deliberate design recorded in the case-61 comment. See §15.2 for the
evidence chain.

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

## 15. Implementation log — Session 061 (2026-09-06)

### 15.1 What landed

All code changes below are in `Core/Hardware/SD/filesystem.c` plus the
descriptive header comments in `Core/Hardware/SD/filesystem.h`. Every
inserted block carries its explanatory comment at the site.

| Site | Change |
|------|--------|
| `filesystem.c` after `filesystem_cacheCurrentResidentSceneNames()` | C1: new static `filesystem_cacheCurrentResidentSceneChildNames()` with the §10 C1 comment verbatim. No forward declaration was added beside the prototypes near the old `filesystem.c:1414`: in the current tree the helper's definition (~line 6004) precedes both dispatch uses (~6226, ~6462), so a prototype would be redundant noise against the file's own convention (prototypes exist only where a definition lags its uses). |
| phase 3 dispatch, `filesystem_residentNames_tick()` (~6225) | C2: SCENE branch now calls `filesystem_cacheCurrentResidentSceneNames()` then `filesystem_cacheCurrentResidentSceneChildNames()`; §10 C2 comment attached above the dispatch chain. |
| phase 7 bootstrap dispatch, `filesystem_residentNames_tick()` (~6460) | C2: same double call so a first register created by a Scene action carries child names; short pointer comment (full rationale lives at the phase-3 site). |
| `filesystem_requestSaveSceneDirectory()` (27883-27913) | C4: identity store seeded with `FS_IDENTITY_KIT_ROW` plus the six Instrument cells from the source Scene's register mirror, immediately after the Scene-row identity capture and before `filesystem_makeSceneEmbeddedKitDir()`; §10 C4 comment verbatim. |
| `filesystem_prepareBankSceneSaveSource()` (~15490) | C5: `FS_IDENTITY_KIT_ROW` staged from the child's own register row before the `filesystem_makeSceneEmbeddedKitDir()` call; §10 C5 comment verbatim. |
| Scene Load text-pattern commit phase (case 53, `filesystem_loadSceneDirectory_tick()`) | §11.2 "Unregistered Scene child" comment attached verbatim. |
| Scene Save pattern phase (case 29, `filesystem_saveSceneDirectory_tick()`) | §11.2 comment attached verbatim. |
| Scene Save effect phase (case 33, `filesystem_saveSceneDirectory_tick()`) | §11.2 comment attached verbatim. |
| `filesystem.h` resident-Scene accessor block | Updated: Scene-op updates additionally overlay the committed Kit + six Instrument rows from the identity store (Session 061); read-only loads unchanged. |
| `filesystem.h` `filesystem_requestSaveSceneDirectory()` block | Updated: request seeds Kit/Instrument identities from the source Scene's register rows before writing. |
| `filesystem.h` `filesystem_requestLoadSceneForScenes()` block | Updated: post-commit targeted HCNAMES update publishes the complete committed hierarchy with the refreshed witness staged at the terminal boundary. |
| `filesystem.c` public `filesystem_requestUpdateResidentSceneNames()` comment | Updated to the complete-hierarchy overlay semantics (identity cells must be seeded by Scene-action callers). |
| `FILESYSTEM_SPEC.md` | §8 invariant added verbatim (with the marked-children breakdown, three-part "mark", dirty-flag staging note, and Effect/Pattern future note) in the "Root resident-name register" section. |
| `S061_AUTOSAVE_READER.md` §7 | §12.2 sentence added: Scene-op HCNAMES update now publishes the complete committed hierarchy; effect/pattern rows remain future work. |

Build: clean `filesystem.o` recompile, full `make`, and `make img` all
succeed. `build/LXRV2_lxr02.img` written (405,488 bytes). No new warnings
from the inserted code (remaining filesystem.c warnings are the same
pre-existing unused-function set). No RAM change (see §13).

### 15.2 C3 resolved without a code change — the Scene Load refreshed
witness already exists

§9's "Scene Load — refreshed witness: none" row and C3's premise are
out of date relative to the current tree: the shared Scene loader already
marks the complete committed hierarchy refreshed at its successful terminal
boundary, `filesystem_loadSceneDirectory_tick()` case 61 (~line 12560,
commit `bf77021`, Session 060 Phase C):

```c
/* Mark each selected Scene plus its Kit and six Instruments refreshed only
 * at this successful terminal boundary; an earlier staging commit could
 * otherwise leave an R witness after a later Pattern/Effect failure. */
for (...) if (mask bit) filesystem_setResidentSceneRefreshed(scene_index);
```

That loop is not gated by `current_op`: it runs for root Scene Load
(`FS_INTERNAL_OP_LOAD_SCENE`) exactly as for Bank-delegated child loads,
covering every destination in `op_scene_load_scene_mask`. The dirty
sources staged at the commit block (~11794-11815) protect those words
through the deferred register re-read, so the staged `R` survives into the
published file. The §2 card state itself corroborates this: the four
library-sourced Scene rows carried `013 R`, which only the Scene Load's own
R staging can have produced on a register that was otherwise converged
before that session's guard-held writer stall.

C3 as literally scheduled (a second `R` staging inside the commit block
before `op_close_status = FS_STATUS_DONE;`) was therefore NOT added: it
would set the witness before Pattern/Effect I/O completes, directly
contradicting the deliberate design recorded in the case-61 comment
(an early `R` after a later Pattern/Effect failure). C3's intent is already
met at the correct boundary. The §9 inventory row and §10 C3 heading should
be read as corrected by this note.

### 15.3 Why the fixture still fails without C1/C2 (what actually changed)

The register writes of the four destination Scenes were already internally
correct in sources and `R`; the durable defect was exclusively the **name
cells**: Scene Load/Save staged the new Kit name and member stems into the
identity store but the Scene-op register overlay republished only Scene-row
names. C1/C2 close that publication gap; C4 makes Scene Save correct even
without a prior Kit/Instrument traversal; C5 keeps the identity store
coherent across Bank Save's per-child preparation. C6 sites verified
unchanged. §11.1 (Bank Save child rows preserved) and §11.3 (no sticky-state
code) are unchanged by the landing.

### 15.4 Residual notes

- The public `filesystem_requestUpdateResidentSceneNames()` has no current
  callers; internal Scene Load/Save dispatch sites are the only
  `FS_INTERNAL_OP_UPDATE_HCNAMES_SCENE` users today. Its overlay now also
  publishes child rows from whatever identity cells are staged, so any
  future caller must seed the identity store like the Scene action paths do
  (comment updated accordingly in both files).
- Hardware fixtures 1-7 in §14 are the remaining work; fixture 1 is the
  `SD_CARD_READER_3` card + `build/LXRV2_lxr02.img`, expecting no "empty"
  notices, `case3=0x0000`, and Kit rows 17/18/31/32 reading `SoyEared - R`
  afterwards.

## 16. Phase 2 — HCNAMES-authoritative boot load (bank loaded, powered off
before menu exit)

Status: implemented in this pass (C7/C8a/C8b/C9 landed, build-verified,
uncommitted; implementation log in §17). Evidence: `SD_CARD_READER_4/`
(post-session, pre-boot) and `SD_CARD_READER_5/` (post-boot). Hardware
fixtures in §16.8 remain outstanding. Supersedes the earlier §16 draft:
the general "fallback reconciliation" framing is withdrawn, and the C11
decision is settled by the unbreakable rule in 16.4.

### 16.1 Symptom

Booting `SD_CARD_READER_4` loads Bank 001 wholesale and ignores the four
Scenes loaded from `Scene/008 Rollin` into resident slots 0, 1, 14, 15.
The register was correct; the load path discarded it.

### 16.2 Evidence

- `SD_CARD_READER_4/.hcnames` is correct: Bank row `Full 001 R`; Scene rows
  0, 1, 14, 15 `Rollin 008 R`; Kit rows 17/18/31/32 `Rollin - R`; the six
  Instrument rows `rollind1..rollinh1 - <type> R`. Every row in the file
  carries `R`.
- The `.hcprms` winner (gen 8) is stale: Bank slot 2, name `LoadTst`,
  present mask `0xdbef`. `settings.cfg` says `active_bank=1`. The winner's
  Bank slot disagrees with settings.cfg, so `filesystem_hasBootWinner()`
  (`filesystem.c:25219`) is false, the boot reader is skipped (no `Q`
  records in either trace), and `main.c:887` runs the canonical
  `preset_loadBank(1)`.
- Why stale: the session began with the root records deleted; the
  Load/Save page guard suppressed every drain during the menu work (`W`
  record), and power-off beat the five-second debounce. The register, in
  contrast, is rewritten synchronously by every completed action — it is
  the freshest durable truth.
- `SD_CARD_READER_5` proves an ordering constraint for the fix: after the
  canonical Bank Load, `.hcnames` itself has been rewritten to plain Bank
  identity (the `008` rows are gone). The authoritative path must therefore
  *replace* the canonical load, not run after it.

### 16.3 The special case and its two checks

This is not a general fallback. It is the specific, real state produced by
"load a Bank in the menu, load more library items into it, power off
before exiting the menu": every `.hcnames` row is REFRESHED and the Bank
row names the freshly loaded Bank. In that state `.hcnames` alone fully
describes the Bank and every sub-child's library source, so the load can
proceed from `.hcnames` without consulting the payload at all.

The boot uses this path when **both** checks hold:

1. **Bank agreement:** the `.hcnames` Bank row carries a direct numeric
   slot (`< 1000`) equal to the boot Bank selected by `settings.cfg`
   (`bank_restoreBankSlot()` after settings parse).
2. **All refreshed:** every one of the 129 register rows carries the
   `R` witness (`FS_RESIDENT_SOURCE_REFRESHED_FLAG`).

If either check fails, the boot behaves exactly as today (winner reader
when the winner matches; otherwise the canonical wholesale Bank Load).
No reconciliation of any kind runs in those other paths.

### 16.4 The unbreakable rule (C11 settled)

**If any child of a Scene is unresolvable, do not load the Scene — create
it empty.** This applies to every row of the authoritative path: an
unresolvable source (`UNKNOWN` after resolution, the corrupt-direct-numeric
Instrument guard, or a narrow-load I/O/parse failure) fires
`filesystem_bootReaderEmptyScene()` (`filesystem.c:27248`), stops that
Scene's remaining rows, sets its bit in `case3_scene_mask`, and queues the
existing "AutoSave Sc NN empty" notice. Never assemble or keep a partially
loaded Scene: the user could later Save it and overwrite good library data
with a mixture. (The earlier §16 draft's Option 1 — keep Bank content +
converge — is withdrawn; it violates this rule.)

### 16.5 Design

Stage 11 ordering becomes:

1. Valid winner whose Bank matches settings.cfg →
   `filesystem_autosaveBootReaderBlocking()` (unchanged).
2. Otherwise → **new** `filesystem_bootHcnamesAuthoritativeLoad()`: parse
   `.hcnames`, run the two checks from 16.3. If they hold, construct the
   entire resident state from `.hcnames` (16.5a-c below) and return 1. If
   they fail, or any hard I/O failure occurs before completion, return 0.
3. Only when step 2 returns 0 → canonical `preset_loadBank()` (unchanged,
   including its bank-fallback latch).

The authoritative path (16.5a-c), which replaces the canonical load:

a. **Narrow Bank load (new).** Enter `Bank/NNN <name>/` from the register
   Bank row, scan its children with the existing blocking finder
   (`filesystem_blockFindNextObject()`, `filesystem.c:21446`, used by the
   quarantine scan at `filesystem.c:21823`) and
   `storage_parseBankSceneFolder()` to build the 00..15 present mask, parse
   `bankset.bcg` through the existing `storage_bankset` parser (fields:
   `active_scene`, `scene_mask_voice_edit`), then commit BankData
   (`bank_setRestoreBankSlot`, `bank_setDisplayName`,
   `bank_setScenePresentMask`, `bank_setActiveSceneSlot`,
   `bank_setSceneMaskVoiceEdit`, `bank_setHasResidentBank(1)`) plus the
   reader's active-Scene tail (`scene_selectActive`,
   `seq_alignActivePatternToScene`, `menu_setShownPattern`).

b. **Per-Scene resolution (all rows are `R` by check 2).** For every
   present Scene, evaluate the eight rows in Scene → Kit → six Instruments
   order: resolve with `filesystem_resolveResidentSource()`
   (`filesystem.c:5517`) plus the shared guard (C7); a row whose
   resolution lands on the Bank row narrow-loads from `Bank/NNN/NN name/`;
   any other direct source narrow-loads from its Scene/Kit/Instrument
   library container via the existing single-level loaders
   (`filesystem.c:26841`, `26910`, `26957`). Unresolvable → the
   unbreakable rule (16.4). Because there is no canonical load in this
   path, Bank-inherited rows are narrow-loaded too — nothing is skipped
   for being "already loaded".

c. **Pattern, register publication, trace, latch.** For every present
   Scene not emptied by 16.4, call
   `filesystem_bootReaderLoadPattern()` (`filesystem.c:27034`) — it
   resolves the Scene row itself, so Bank-inherited Scenes get the Bank's
   `pattern.pat` and library-sourced Scenes get the library folder's.
   Publish the register only when any Scene was emptied (reuse
   `filesystem_publishHcnamesRegisterBlocking()`, `filesystem.c:26548`);
   successfully resolved rows keep their existing correct register cells.
   Emit per-row `Q` `0x04`/`0x02` records and the existing `0x80` summary
   (case2 bits 0..15, case3 bits 16..31) — no new trace flags. Set the
   existing `bank_fallback` boot latch so the replay marks the constructed
   Bank dirty and the first drain captures it.

Failure semantics: any hard failure (register unreadable, Bank folder
unenterable, bankset unparseable) returns 0 *before* committing partial
Scene resolution results that the canonical fallback would not overwrite
cleanly; the canonical Bank Load then reloads every present Scene
wholesale, so any earlier narrow writes are overwritten by design.

### 16.6 Change list

#### C7 — Shared row-resolution guard

File: `Core/Hardware/SD/filesystem.c`. Operation: ADD static
`filesystem_bootReaderResolveResidentRow(row, *resolved_row)` near
`filesystem_resolveResidentSource()` usage; MODIFY
`filesystem_bootReaderEvaluateScene()` (`filesystem.c:27309`) to call it
instead of its inline guard; the new authoritative resolver (C8b) calls it
too, keeping the Phase-5b Instrument-row semantics single-sourced.

```c
/*
 * Resolve one resident row for boot recovery with the shared guard.
 *
 * What: runs filesystem_resolveResidentSource() and applies the
 * instrument-row numeric-source guard: a numeric token sitting directly
 * on an Instrument row (resolved_row == row) is corrupt and becomes
 * UNKNOWN, while a numeric slot inherited from a Bank/Scene/Kit parent
 * row remains valid. Why: the winner reader's per-Scene evaluation and
 * the new HCNAMES-authoritative loader must reject the same malformed
 * register the same way; duplicating the guard risks the two paths
 * diverging again (Session 061 Phase 5b). Inputs: one HCNAMES row.
 * Outputs: the resolved source and, via *resolved_row, the row that
 * supplied the direct source. Accessors:
 * filesystem_resolveResidentSource(), filesystem_residentSource().
 * Affiliates: filesystem_bootReaderEvaluateScene() and
 * filesystem_bootHcnamesAuthoritativeLoad().
 */
```

#### C8a — Blocking narrow Bank loader (new static)

File: `Core/Hardware/SD/filesystem.c`. Operation: ADD static
`filesystem_bootNarrowLoadBank(uint16_t bank_slot)` beside the other
narrow loaders (after `filesystem_bootReaderEnterSceneFolder()`,
`filesystem.c:26640`).

```c
/*
 * Load one Bank container blocking, from its register identity.
 *
 * What: enters Bank/NNN <name>/ using the register Bank-row display name,
 * scans the 00..15 child directories with the blocking finder to build
 * the Scene-present mask, parses bankset.bcg through the shared
 * storage_bankset parser, and commits BankData: restore slot, display
 * name, present mask, active Scene, voice-edit mask, and the
 * has-resident-bank bit. Why: the HCNAMES-authoritative boot path must
 * construct the Bank without the .hcprms payload and without the
 * asynchronous Bank Load state machine (which would also rewrite the
 * register); the boot reader's own step-2 payload apply is not usable
 * here. Inputs: bank_slot, the parsed register mirror (Bank row name),
 * and op_bankset_state scratch. Outputs: committed BankData and a
 * returned present mask or zero on failure; no .hcnames writes.
 * Accessors: filesystem_blockChdir(),
 * filesystem_bootReaderEnterDirectory(), filesystem_makeNumberedDir(),
 * filesystem_blockOpenDirLfn(), afatfs_findFirstObject(),
 * filesystem_blockFindNextObject(), storage_parseBankSceneFolder(),
 * storage_banksetInit/ParseLine/Finalize(), bank_set* accessors.
 * Affiliates: filesystem_quarantineScenesInParentBlocking() (the same
 * blocking-scan pattern, filesystem.c:21823), the runtime Bank Load
 * bankset phases (filesystem.c:13305-13376), and
 * filesystem_bootHcnamesAuthoritativeLoad().
 */
static uint8_t filesystem_bootNarrowLoadBank(uint16_t bank_slot);
```

Skeleton: enter `Bank/`, `makeNumberedDir(NNN, register Bank name)`,
enter; findFirstObject + blockFindNextObject loop; for each directory
`storage_parseBankSceneFolder(displayName, &child_slot, display)` that
returns a valid 0..15 slot, set the present-mask bit; open `bankset.bcg`
and stream lines through `storage_banksetParseLine()`/`Finalize()` (same
`filesystem_bootReadLineBlocking()` pattern as
`filesystem_bootReaderParseKitset()`); commit BankData; `bank_setHasResidentBank(1u)`;
`scene_selectActive(bank_activeSceneSlot())`;
`seq_alignActivePatternToScene(...)`; `menu_setShownPattern(...)` (the same
tail as `filesystem_autosaveBootReaderBlocking()` step 2,
`filesystem.c:27489`).

#### C8b — HCNAMES-authoritative boot load (new public function)

File: `Core/Hardware/SD/filesystem.c` (beside
`filesystem_autosaveBootReaderBlocking()`, `filesystem.c:27489`) and
`filesystem.h`. Operation: ADD.

```c
/*
 * Boot load driven entirely by .hcnames when it is authoritative.
 *
 * What: parses .hcnames (temp-file prelude first, then the register),
 * then requires the two special-case checks — the register Bank row is a
 * direct numeric slot equal to bank_restoreBankSlot() (the settings.cfg
 * boot Bank), and all 129 rows carry the refreshed witness. When both
 * hold, the register is authoritative: this function constructs the
 * whole resident state from it — the Bank container via
 * filesystem_bootNarrowLoadBank(), then every present Scene's eight rows
 * via resolve-plus-narrow-load, Bank-inherited rows from the Bank tree
 * and direct rows from their Scene/Kit/Instrument libraries, then
 * pattern.pat per non-emptied Scene. Any unresolvable child of a Scene
 * applies the unbreakable rule: the Scene is not loaded, it is created
 * empty and noticed. Returns 1 on a completed authoritative load and 0
 * when either check fails or a hard failure occurs, in which case the
 * caller falls through to the canonical Bank Load unchanged.
 *
 * Why: after a menu Bank Load plus further Load/Save actions, a power
 * off before menu exit leaves every register row REFRESHED while the
 * autosave payload is still the pre-session capture (the page guard
 * holds the writer). The register is the freshest durable truth, so the
 * boot must not discard it: every correctly defined sub-child source —
 * Scene, Kit, and every single Instrument — must be respected, and a
 * partially loaded Scene must never be assembled for a later Save
 * (Session 061 Phase 2, SD_CARD_READER_4/5).
 *
 * Inputs: mounted card, parsed settings.cfg (bank_restoreBankSlot),
 * .hcnames, and the boot latch. Outputs: committed BankData and per-row
 * resident SceneData; case2/case3 latch masks, Q trace records, notice
 * state; register publication only for emptied Scenes. Uses the existing
 * register mirror, identity/type scratch, staged kit/instrument
 * workspace, op_bankset_state, and boot latch — no new RAM.
 *
 * Accessors: filesystem_bootReaderParseRegisterFile(),
 * filesystem_bootReaderResolveResidentRow(),
 * filesystem_bootNarrowLoadBank(),
 * filesystem_bootReaderNarrowLoadScene(),
 * filesystem_bootReaderNarrowLoadKit(),
 * filesystem_bootReaderNarrowLoadInstrument(),
 * filesystem_bootReaderLoadPattern(),
 * filesystem_bootReaderEmptyScene(),
 * filesystem_publishHcnamesRegisterBlocking(),
 * filesystem_setBootLatchBankFallback(),
 * filesystem_autosaveTrace_record(). Affiliates: main.c stage 11,
 * filesystem_autosaveBootReaderBlocking() (the winner-matching sibling),
 * 061_READER_LOADED_SCENES_INVALID.md §16.3-16.4.
 */
uint8_t filesystem_bootHcnamesAuthoritativeLoad(void);
```

Skeleton:

```c
uint8_t filesystem_bootHcnamesAuthoritativeLoad(void)
{
    /* 1. parse register (temp prelude, then .hcnames) */
    /* 2. check 1: register Bank row direct slot == bank_restoreBankSlot() */
    /*    check 2: every row 0..128 has FS_RESIDENT_SOURCE_REFRESHED_FLAG */
    /*    either fails -> return 0 (caller runs the canonical Bank Load) */
    /* 3. filesystem_bootNarrowLoadBank(bank_slot) -> 0 on failure */
    /* 4. for each present scene: rows scene/kit/6 instruments in order
     *      resolve via C7; resolved_row==bank -> narrow-load from bank tree
     *      other direct -> narrow-load that row (Q 0x04, case2 mask)
     *      unresolvable/failed -> unbreakable rule: EmptyScene, Q 0x02,
     *      case3 mask, stop this scene's rows, rows_changed = 1
     * 5. for each present scene not emptied: bootReaderLoadPattern
     * 6. if rows_changed: publishHcnamesRegisterBlocking
     * 7. setBootLatchBankFallback(); Q summary 0x80 (case2|case3<<16)
     * 8. return 1
     */
}
```

#### C9 — main.c wiring

File: `main.c`. Operation: MODIFY stage 11: after the existing
`filesystem_hasBootWinner()`/reader block, attempt the authoritative path
when the reader did not restore (`boot_restored_winner == 0`); only when
it also returns 0 does the existing canonical `preset_loadBank()` branch
run. Timeout handling identical to the reader's
(`filesystem_bootLoggingTimedOut()` → `boot_filesystem_timeout`).

```c
/*
 * HCNAMES-authoritative boot load between reader and canonical fallback.
 *
 * What: when the winner reader could not restore (no valid winner, Bank
 * mismatch, or reader decline), tries the special-case load driven
 * entirely by .hcnames — valid only when the register Bank row equals
 * the settings.cfg boot Bank and every register row is refreshed. Why:
 * that is exactly the state left by "load Bank in menu, power off before
 * menu exit", and in it the register fully names every library source;
 * proceeding from it respects all Scene/Kit/Instrument overrides that a
 * canonical wholesale Bank Load would discard. Inputs:
 * boot_restored_winner. Outputs: either the authoritative load
 * completes and the canonical ladder is skipped, or the ladder runs
 * unchanged. Affiliates:
 * filesystem_bootHcnamesAuthoritativeLoad(),
 * filesystem_autosaveBootReaderBlocking(), main.c stage 11/12.
 */
if (!boot_restored_winner && filesystem_autosaveEnabled()) {
    boot_restored_winner = filesystem_bootHcnamesAuthoritativeLoad();
    if (filesystem_bootLoggingTimedOut())
        goto boot_filesystem_timeout;
}
```

#### C10 — Trace reuse (no new flags)

The authoritative path reuses the existing `Q` records: per-row `0x02`
(unresolvable) and `0x04` (narrow-load), summary `0x80` with case2 bits
0..15 and case3 bits 16..31. No decoder or `AutosaveTrace.h` change; the
earlier draft's `0x08` flag is dropped.

#### C11 — The unbreakable rule (no open decision)

Implemented inside C8b per 16.4: any unresolvable child → Scene not
loaded, created empty, noticed. The earlier draft's "keep Bank content +
converge" option is withdrawn and must not be implemented.

#### C12 — Documentation

- `FILESYSTEM_SPEC.md`: add the Phase-2 boot decision (16.3 checks,
  authoritative resolution, unbreakable rule) beside the Session 061
  invariant added earlier.
- `S061_AUTOSAVE_READER.md` §4: note the new middle path between the
  winner reader and the canonical fallback, and that it never consults
  the payload.
- `MEMORY.md`: volatile note while uncommitted.

### 16.7 RAM and scope

No new RAM: C8a/C8b reuse `op_bankset_state`, the register mirror, the
identity/type scratch, the staged kit/instrument workspace, the boot
latch, and stack-local directory-name buffers identical to the existing
narrow loaders. No new statics.

Out of scope, recorded:

- The winner-matching reader path is unchanged.
- The no-Bank Scene/Kit fallback ladder is unchanged (the special case
  is Bank-scoped).
- The flashed image on the card was 405,720 bytes vs 405,504 for
  `build/LXRV2_lxr02.img`: confirm the next test image is built from the
  reviewed tree.
- `SD_CARD_READER_5`'s missing `.hcprms1` remains an observed artifact
  of copying during/after the drain's A/B publish cycle, not a defect
  addressed here.

### 16.8 Hardware verification plan (Phase 2)

1. Boot `SD_CARD_READER_4` with the new image: Bank 001 with scenes 0, 1,
   14, 15 playing Rollin (008) content and scenes 2..13 the Bank-tree
   content; summary `Q` `0x80` with `case2=0xffff`, `case3=0x0000`;
   `.hcnames` still carries the `008 R` rows afterwards (no wholesale
   rewrite).
2. Check-failure fixture: same card but clear the `R` on a few rows (or
   edit the register Bank row to a different slot): boot must fall back
   to the plain canonical Bank Load, with no authoritative resolution.
3. Unbreakable-rule fixture: `SD_CARD_READER_4` content with
   `Scene/008 Rollin` deleted: scenes 0, 1, 14, 15 must come up empty
   with the "AutoSave Sc NN empty" notices — never partially loaded —
   while scenes 2..13 load from the Bank.
4. No-winner fixture: delete `.hcprms1`/`.hcprms2` from a card whose
   register satisfies both checks: the authoritative path still loads
   correctly (it never consults the payload).
5. Regression: `SD_CARD_READER_3` (matching winner → reader path) and
   `SD_CARD_READER_5` (already-converged Bank → plain fallback) boot
   unchanged.

## 17. Phase 2 implementation log — Session 061 continuation (2026-09-06)

Status while working: tree carries the Phase-1 landing (uncommitted) plus
unrelated in-progress user splash/branding edits (`Makefile`, `lcd.c`,
`menu.c`, `main.c` splash strings, untracked `Core/Menu/SplashAnimation.*`,
untracked `SD_CARD_READER_4/5/` evidence cards). All Phase-2 edits below
avoid those files except main.c stage 11 (C9), which is a separate region
from the splash edits.

### 17.1 Reconciliation of the §16 change list against the current tree

All §16 line anchors were re-derived after the Phase-1 shift (+~92 lines in
`filesystem.c`). Current anchors: `filesystem_resolveResidentSource()` 5517;
blocking helpers 21084-21490; `publishHcnamesRegisterBlocking()` 26548;
`bootReadLineBlocking()` 26579; `EnterDirectory()` 26611;
`EnterSceneFolder()` 26640; `EnterKitFolder()` 26687; `ParseKitset()` 26731;
`NarrowLoadScene()` 26841; `NarrowLoadKit()` 26910;
`NarrowLoadInstrument()` 26957; `LoadPattern()` 27034;
`ApplyRowType()` 27103; `ParseRegisterFile()` 27165;
`SeedInstrumentTypes()` 27214; `EmptyScene()` 27248;
`EvaluateScene()` 27309; `autosaveBootReaderBlocking()` 27489.
`EvaluateScene()`'s inline Phase-5b guard is at 27396-27403.

Notes taken while implementing:
- C7 needs no forward declaration if defined just before `EvaluateScene()`
  (first use 27396; second use inside C8b, which is defined later).
- `EvaluateScene()` reference behavior for per-row `Q` records: 0x04 with
  `scene | row<<8 | resolved<<16` and 0x02 with `scene | row<<8`; summary
  0x80 packs case2 in bits 0..15 and case3 in bits 16..31.
- `EmptyScene()` resets the 8 rows of the Scene to UNKNOWN|R and keeps the
  mirror names for display; `rows_changed` must be set so C8b publishes.
- C8a mirrors the runtime Bank container commit (Bank Load phases 6-17 and
  the 13751-13785 commit): open the selected Bank handle, chdir in, scan
  children with `afatfs_findFirstObject` +
  `filesystem_blockFindNextObject`, parse `bankset.bcg` via
  `storage_bankset*`, then commit with `bank_setDisplayName` /
  `bank_setScenePresentMask` / `bank_selectActiveSceneForEditMask` /
  `bank_setSceneMaskVoiceEdit` / `bank_setRestoreBankSlot` /
  `bank_setHasResidentBank(1)` and the reader's active tail
  (`scene_selectActive`, `seq_alignActivePatternToScene`,
  `menu_setShownPattern`). Active-scene normalization mirrors runtime phase
  17: clamp to 0..15, then first present child when the bankset active is
  absent from the present mask (empty bank -> 0).
- C8a returns the discovered present mask, 0 on hard failure. C8b treats a
  zero return (failure) and a committed-but-empty Bank (mask 0) identically:
  decline with return 0 so the canonical `preset_loadBank()` ladder (and its
  empty-Bank fallback semantics) runs unchanged - same decision the winner
  reader makes for an empty-Bank record.
- C8b register read mirrors winner-reader step 1 exactly (`.hcnamtmp`
  prelude, then `.hcnames`, `bootReaderParseRegisterFile()`); there is no
  winner regeneration fallback because this path never consults .hcprms.
- C9 inserts between the winner-reader block and the canonical branch in
  main.c stage 11, using the plan's snippet verbatim; the existing branch
  conditions already key off `boot_restored_winner`.

### 17.2 C7 - shared row-resolution guard (landed)

Added `filesystem_bootReaderResolveResidentRow()` immediately before
`filesystem_bootReaderEvaluateScene()` and replaced the inline guard with a
call. Comment attached per §16.6 C7. `EvaluateScene()` behavior unchanged.

### 17.3 C8a - blocking narrow Bank loader (landed)

Added static `filesystem_bootNarrowLoadBank(uint16_t bank_slot)` after
`filesystem_bootReaderEnterKitFolder()`, with the §16.6 C8a comment. See
§17.1 notes for the commit mirror.

### 17.4 C8b - HCNAMES-authoritative boot load (landed)

Added public `filesystem_bootHcnamesAuthoritativeLoad()` directly after
`filesystem_autosaveBootReaderBlocking()` with the §16.6 C8b comment and the
declaration (plus the same comment) in `filesystem.h`.

### 17.5 C9 - main.c wiring (landed)

Stage 11 now attempts the authoritative path between the winner reader and
the canonical ladder, per §16.6 C9; timeout handling matches the reader.

### 17.6 Build and remaining work

Full `make` and `make img` pass: `text=406,892`, `data=404`,
`bss=96,212` — no new RAM (C7/C8a/C8b reuse the register mirror,
`op_bankset_state`, the type/kit/instrument scratch, the boot latch,
and stack-local name buffers, per §16.7). No new compiler warnings from
the added code. `build/LXRV2_lxr02.img` regenerated (407,312 bytes).

Final code sites (post-landing numbering):

- C7 `filesystem_bootReaderResolveResidentRow()` at `filesystem.c:27458`;
  called from `filesystem_bootReaderEvaluateScene()` (`filesystem.c:27582`)
  and from C8b (`filesystem.c:27868`).
- C8a `filesystem_bootNarrowLoadBank()` at `filesystem.c:26746` (beside the
  folder-entry helpers, after `filesystem_bootReaderEnterKitFolder()`).
- C8b `filesystem_bootHcnamesAuthoritativeLoad()` at `filesystem.c:27788`
(immediately after `filesystem_autosaveBootReaderBlocking()`, with the
§16.6 comment above it; its narrow-Bank call is at 27842) and declared
at `filesystem.h:376` with the matching comment block.
- C9 stage-11 hook at `main.c:893` (comment at 876-892), between the
  winner-reader block and the canonical `preset_loadBank()` branch, using
  the §16.6 snippet verbatim including the timeout check.
- C10 no new trace flags: C8b reuses `Q` 0x04/0x02 per-row records and
  the 0x80 summary with case2 in bits 0..15 / case3 in bits 16..31; the
  boot-logging label is `HCAUTH  ` when DEV_MODE_LOGGING.
- C11 implemented inside C8b step 4 (unbreakable rule).
- C12 docs: `FILESYSTEM_SPEC.md` Phase-2 paragraph; `S061_AUTOSAVE_READER.md`
  §4 middle-path note; `MEMORY.md` quick-start Phase-2 paragraph.

Deviations from the §16 text, all deliberate and noted here:

- §16.6 C8a's skeleton declares `static uint8_t filesystem_bootNarrowLoadBank(`
  `uint16_t bank_slot)`; a 16-bit present mask cannot be returned through
  `uint8_t`, so the landing returns `uint16_t` (comment records "present
  mask or zero on failure").
- C8b treats a committed-but-empty Bank (present mask 0) as a decline,
  exactly like the winner reader treats an empty-Bank record, so the
  canonical ladder keeps its empty-Bank and no-Bank Scene/Kit fallback
  semantics (§16.7's "no-Bank ladder unchanged" requirement).
- C8a commits the Bank container only after the bankset parse succeeds,
  and leaves root CWD before returning, so a decline hands the canonical
  loader a clean starting context.
- C8b parses the register with the same `.hcnamtmp`-prelude-then-`.hcnames`
  sequence as the winner reader, but has no regeneration fallback (it
  never consults `.hcprms`); an unreadable register simply declines.

Remaining work: hardware fixtures §16.8 items 1-5 (fixture 1 is
`SD_CARD_READER_4` + the new image: Bank 001 with scenes 0/1/14/15
playing Rollin 008 content, summary `Q` 0x80 `case2=0xffff`,
`case3=0x0000`, and `.hcnames` still carrying the `008 R` rows).
## 18. Phase 2 implementation review — judgement (2026-09-06)

Verdict: **implemented correctly per §16; approved for the §16.8 hardware
fixtures.** Review evidence below; four observations are recorded in
18.4 and do not block testing.

### 18.1 What landed and was verified

| Plan item | Status | Verification |
|-----------|--------|--------------|
| C7 shared resolution guard | In | `filesystem_bootReaderResolveResidentRow()` wraps `filesystem_resolveResidentSource()` plus the Phase-5b instrument guard; `filesystem_bootReaderEvaluateScene()` now calls it, so both boot consumers share one guard. |
| C8a narrow Bank loader | In | `filesystem_bootNarrowLoadBank()` enters `Bank/NNN <name>/` from the register, builds the 00..15 present mask with the existing blocking finder + `storage_parseBankSceneFolder()`, parses `bankset.bcg` via the shared parser, commits BankData, and runs the reader's active-Scene tail. Empty Bank (mask 0) returns 0 so the canonical ladder keeps its empty-Bank semantics. |
| C8b authoritative load | In | `filesystem_bootHcnamesAuthoritativeLoad()` parses the register (temp prelude first), enforces both §16.3 checks, constructs the Bank, resolves all eight rows per present Scene (Case 2/3 only — no payload anywhere), applies the unbreakable rule via `filesystem_bootReaderEmptyScene()`, loads patterns for non-emptied Scenes, publishes only on emptied Scenes, sets the bank-fallback latch, and emits per-row `Q` `0x04`/`0x02` plus the `0x80` summary. |
| C9 main.c wiring | In | Placed at `main.c:893-898` exactly between the winner-reader block and the canonical `preset_loadBank` branch, gated on `!boot_restored_winner && filesystem_autosaveEnabled()`, with the same boot-deadline timeout handling. `boot_restored_winner` is initialized at `main.c:767`; `filesystem_bootLoggingEnd()` still runs once at `main.c:1124`. |
| C10 trace reuse | In | No new flags; decoder unchanged. |
| C11 unbreakable rule | In | Any unresolvable row (UNKNOWN, guard-rejected, or narrow-load failure) empties the Scene, sets `case3_scene_mask`, stops the Scene's remaining rows, and is published. |
| C12 docs | In | `filesystem.h`, `FILESYSTEM_SPEC.md`, `S061_AUTOSAVE_READER.md`, `MEMORY.md` updated. |

### 18.2 Simulation against the failing fixture

`SD_CARD_READER_4` was re-checked programmatically against the
implemented gates:

- Check 1: register Bank row `Full 001 R` — direct slot 1 equals
  `active_bank=1` → passes.
- Check 2: all 129 data rows carry `R` → passes.
- Bank: `Bank/001 Full` exists with `bankset.bcg` (active 6, voice mask
  0x0040) → narrow Bank load succeeds, present mask 0xffff.
- Scenes 0, 1, 14, 15: Scene rows `008` → `Scene/008 Rollin` exists with
  `Kit Rollin` and the six member files; Kit/Instrument rows inherit up to
  the Scene row and resolve to the same folder → all narrow loads
  succeed.
- Scenes 2..13: rows inherit to the Bank row → narrow loads run against
  `Bank/001 Full/NN <name>/`, whose kit names match the register →
  succeed.

Expected boot result on this fixture: Bank 001 with scenes 0, 1, 14, 15
playing Rollin and scenes 2..13 the Bank-tree content; per-row `Q` `0x04`
for all 128 sub-child rows; summary `Q` `0x80` value `0xffff0000`
(`case2=0xffff`, `case3=0x0000`); `.hcnames` afterwards still carries the
`008 R` rows (the path does not rewrite them).

### 18.3 Build and RAM

Forced recompile of `filesystem.c` and `main.c`: no new warnings (only
the pre-existing unused-function set). `make img` succeeds. Sizes:
`text=406,892` (+1,808 vs Phase 1), `data=404`, `bss=96,212` — **bss
unchanged, no new RAM**, per §16.7. `bank_selectActiveSceneForEditMask()`
and `bank_setActiveSceneSlot()` write the same
`bank_active_scene_slot` and both enforce the edit-mask invariant, so the
narrow Bank loader's commit is equivalent to the payload apply's.

### 18.4 Observations (non-blocking, recorded for the test session)

1. **Pattern stays best-effort.** The unbreakable rule is enforced for
   the eight register rows. A missing `pattern.pat` still leaves a zeroed
   PatternSet (non-fatal, reader-parity). If the rule is intended to also
   empty a Scene whose pattern file is missing, that is a follow-up
   decision — not changed here.
2. **Unresolvable rows are sticky-empty by design.** After the rule
   fires, the rows become `? R` with retained names, so subsequent boots
   re-attempt, fail the bank-branch name lookup, and re-empty with a
   notice until the user reloads the slots. This is the intended
   consequence of "never partially load" — the Bank's own content is
   deliberately not substituted.
3. **Decline paths leave CWD inside `Bank/NNN/`.** The narrow Bank
   loader returns without `chdir(NULL)` on some failures; this is safe
   because the canonical fallback ladder starts with `chdir(NULL)`.
   Verified, no change needed.
4. **Unrelated worktree changes are present in this build.** The
   worktree also carries splash-screen edits (`lcd.c`, `SplashAnimation.c`
   in `Makefile`, `boot_show_splash()` text, the commented-out
   `splashAnimation_play()`), which are included in the rebuilt image
   (407,312 bytes). This review covers the Phase-2 change only; confirm
   the splash edits are intended before hardware runs.


