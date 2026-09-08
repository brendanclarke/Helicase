# S061 — HCNAMES Invalid Fix 2: Preserve All 96 Types in Existing Boot Scratch

Status: diagnosis confirmed from `SD_CARD_READER_4` as the input fixture and
`SD_CARD_READER_8` as the post-boot capture made with the current working-tree
image. Source implementation is complete and build-verified; hardware
acceptance remains pending. The files are not invalid. The HCNAMES-authoritative
boot path was passing corrupted in-RAM Instrument types to otherwise valid
files. The current per-Scene six-type snapshot was too late for Scenes 1..15.

## 1. Verdict

The precise defect is the lifetime of
`fs_stage_workspace.boot_reader_type[96]`.

`filesystem_bootReaderParseRegisterFile()` correctly parses all 96 mandatory
Instrument type tokens from `/.hcnames`, but stores them in a member of
`fs_stage_workspace`, a union also used as `scene_stage`, `kit_stage`, and
`instrument_stage`. Every Case-2 narrow load writes one of those other union
members. The first Scene therefore destroys the saved HCNAMES types for every
later Scene.

The attempted fix currently in `filesystem.c` snapshots only six types at the
start of each Scene. That protects those six types from loads performed later
within the same Scene, but it does not protect the other 90 types from loads
performed by earlier Scenes. Scene 0 snapshots valid data before the first
narrow load and succeeds. Scene 1 snapshots bytes 6..11 from an already
overwritten staging object, and Scenes 2..15 do the same at later offsets.

The targeted fix is to place all 96 parsed types in storage outside the staging
union before the first narrow load and use that immutable image for the entire
traversal. No new RAM is required: give the existing 144-byte
`op_bank_child_display` operation scratch a second, mutually exclusive union
view for the 96 boot-reader types. Apply that view in both boot-reader entry
paths.

## 2. The input fixture is valid

`SD_CARD_READER_4` is the intended state produced by:

1. load Bank 001 `Full`;
2. without exiting the Load/Save menu, load root Scene 008 `Rollin` into
   resident Scenes 0, 1, 14, and 15;
3. power off before the guarded AutoSave writer can publish.

Its durable state is internally coherent:

- `settings.cfg` has `active_bank=1` and `autosave=1`;
- `/.hcnames` has the valid `#types drm snr cym hat` header, exactly 129 data
  rows, and `R` on every row;
- HCNAMES Bank row 0 is `Full 001 R`;
- Scene rows 1, 2, 15, and 16 are `Rollin 008 R`;
- their Kit rows are `Rollin - R`;
- their Instrument rows are `rollind1`, `rollind2`, `rollind3`, `rollins1`,
  `rollinc1`, and `rollinh1`, with types `drm/drm/drm/snr/cym/hat`, source `-`,
  and `R`;
- `Scene/008 Rollin/Kit Rollin/kitset.kcg` declares those same six types and
  filenames, and all six files exist;
- `.hcprms1` is generation 7 and `.hcprms2` is generation 8. The newer record
  still describes Bank 002 `LoadTst`, as expected when the Load/Save page guard
  prevented publication.

Consequently the matching-winner reader is not selected. The two HCNAMES
authority checks pass (row 0 agrees with settings Bank 001, and all rows have
`R`), so `filesystem_bootHcnamesAuthoritativeLoad()` is the path that must
reconstruct the resident Bank.

The `Scene/008 Rollin` and `Bank/001 Full` trees in `SD_CARD_READER_4` and
`SD_CARD_READER_8` compare byte-for-byte. In particular, all seven files in
`Scene/008 Rollin/Kit Rollin` are identical between the pre-boot and post-boot
copies. There is no malformed Scene, Kit, or Instrument file to repair.

## 3. Decisive hardware evidence

`SD_CARD_READER_8/LXRV2_lxr02.img` is SHA-256-identical to the current
`build/LXRV2_lxr02.img`. That image contains both the per-Scene type snapshot
attempt and the synchronous root-`chdir` change.

The `SD_CARD_READER_8/asavetrc.bin` `Q` records show:

- Scene 0: Scene row 1, Kit row 17, and all six Instrument rows 33..38 complete
  Case 2 successfully from source 008;
- Scene 1: Scene row 2 and Kit row 18 complete successfully from the same
  source 008, then Instrument row 39 immediately becomes Case 3;
- the Scene-1 Kit success and Instrument failure both have tick 1424. The
  Instrument failure therefore occurs before a meaningful file transaction;
- the final summary is raw value `0xfffeffff`: Case-2 mask `0xffff`, Case-3
  mask `0xfffe`. Scene 0 survives; every later Scene is emptied;
- post-boot HCNAMES agrees exactly: Scene 0 retains `008/-/-`, while Scenes
  1..15 have all eight sources rewritten to `?` by
  `filesystem_bootReaderEmptyScene()`.

Scene 0 and Scene 1 resolve to the same physical `Scene/008 Rollin` tree.
Scene 0 successfully loading all six files proves that Scene 1's first-file
failure is not a bad file, path, name, type column, or kitset declaration.

The improvement from `SD_CARD_READER_7` is also diagnostic. Before the
six-type local snapshot, Scene 0 failed partway through its own Instrument
rows. With the snapshot, all of Scene 0 succeeds, but the first Instrument of
Scene 1 fails. This is the exact boundary predicted by an intra-Scene-only
snapshot of a cross-Scene lifetime.

## 4. Exact failure chain in the current source

1. At `filesystem.c:27295-27297`, each parsed Instrument type is written to
   `fs_stage_workspace.boot_reader_type[scene * 6 + slot]`.
2. That array is a member of the 2,048-byte staging union at
   `filesystem.c:952-977`.
3. At `filesystem.c:27893-27899`, the authoritative loader copies only the
   current Scene's six entries immediately before evaluating that Scene.
4. Scene 0 gets a valid copy because the union still contains the freshly
   parsed 96-byte type image.
5. Scene 0's narrow Scene load writes `fs_stage_workspace.scene_stage`; its
   narrow Kit load clears and fills `op_staged_kit`; each narrow Instrument
   load writes `op_staged_instrument`. All are aliases of
   `boot_reader_type`.
6. When the outer loop reaches Scene 1, bytes 6..11 of
   `boot_reader_type` are no longer parsed type tokens. They are bytes from the
   last staged payload. Some corrupted bytes can accidentally equal enum values
   0..3, explaining the varying number of apparent Instrument successes in
   later Scenes; the first out-of-range or wrong-but-valid value stops that
   Scene.
7. `filesystem_bootReaderNarrowLoadInstrument()` rejects an out-of-range type
   at its initial guard, or rejects a wrong-but-valid type against
   `op_kitset.instrument_type[instrument_slot]`. It returns zero.
8. The caller converts that false local failure into Case 3, invokes the
   all-or-nothing rule, empties the Scene, and changes its eight source rows to
   `UNKNOWN|REFRESHED` (`? R`).

The same latent defect exists in the matching-winner path. Its
`filesystem_bootReaderEvaluateScene()` currently takes the same too-late
six-byte snapshot at `filesystem.c:27518-27522`; after Scene 0 performs a
Case-2 load, later Scenes can read overwritten types in exactly the same way.

## 5. Precise targeted fix

Replace the standalone declaration of the existing 144-byte Bank-child display
cache with an explicitly shared operation-scratch union:

```c
#define FS_BOOT_READER_INSTRUMENT_TYPE_COUNT \
    (AUTOSAVE_SCENE_COUNT * AUTOSAVE_INSTRUMENTS_PER_KIT)

typedef union {
    char bank_child_display[STORAGE_BANK_SCENE_MAX_SLOTS]
                           [STORAGE_SCENE_DISPLAY_NAME_LEN + 1u];
    uint8_t boot_reader_type[FS_BOOT_READER_INSTRUMENT_TYPE_COUNT];
} filesystem_bank_child_scratch_t;

static filesystem_bank_child_scratch_t op_bank_child_scratch;
```

The two views have non-overlapping lifetimes:

- `boot_reader_type[96]` is owned by the Stage-11 boot reader from HCNAMES
  parsing/regeneration through the last Scene row evaluation;
- `bank_child_display[16][9]` is owned by a normal asynchronous Bank Load from
  its Bank-child scan through consumption of the cached child names.

These lifetimes cannot overlap. The boot readers use blocking narrow loaders,
and `filesystem_bootNarrowLoadBank()` uses its own local `display` buffer rather
than `op_bank_child_display`. If a boot reader declines, the canonical Bank
Load starts a new filesystem operation; `filesystem_start()` clears the Bank
child scratch before the Bank scan repopulates it. The root Bank existence
check is unaffected because it uses the separate shared library-name cache.

Remove `boot_reader_type[]` from `filesystem_stage_workspace_t`. Change
`filesystem_bootReaderApplyRowType()` and
`filesystem_bootReaderSeedInstrumentTypes()` to write directly to
`op_bank_child_scratch.boot_reader_type`. The complete parsed type image then
never occupies the destructive payload-staging union and does not require a
second copy.

### 5.1 Matching-winner path

In `filesystem_autosaveBootReaderBlocking()`:

1. let a successful register parse populate the new scratch view directly;
2. after winner-based HCNAMES regeneration, let
   `filesystem_bootReaderSeedInstrumentTypes()` populate that same view from
   the regenerated resident Scene types;
3. remove `filesystem_bootReaderEvaluateScene()`'s six-byte local array and
   copy loop;
4. select each Case-2 Instrument type directly from the immutable scratch:

```c
instrument_type_t type = (instrument_type_t)
    op_bank_child_scratch.boot_reader_type[
        (uint16_t)scene_index * AUTOSAVE_INSTRUMENTS_PER_KIT + slot];
```

No function signature change or full-image `memcpy()` is necessary.

### 5.2 HCNAMES-authoritative path

In `filesystem_bootHcnamesAuthoritativeLoad()`:

1. let the successful register parse populate the new scratch view directly;
2. remove the six-byte array and copy loop from inside the per-Scene loop;
3. select each Instrument type only from the immutable full image:

```c
instrument_type_t type = (instrument_type_t)
    op_bank_child_scratch.boot_reader_type[
        (uint16_t)scene_index * AUTOSAVE_INSTRUMENTS_PER_KIT + slot];
```

### 5.3 Existing Bank Load references, reset, and assertions

Change the existing runtime Bank Load users of `op_bank_child_display` to the
`op_bank_child_scratch.bank_child_display` member. In generic request setup,
clear the whole union:

```c
memset(&op_bank_child_scratch, 0, sizeof(op_bank_child_scratch));
```

Retain the existing 144-byte SRAM accounting and add compile-time checks for
both views:

```c
_Static_assert(
    sizeof(op_bank_child_scratch.boot_reader_type) == 96u,
    "boot reader must retain exactly 16 x 6 Instrument types");
_Static_assert(
    sizeof(op_bank_child_scratch) == 144u,
    "shared Bank-child/boot-reader scratch must remain 144 bytes");
```

Update the aggregate Option-1 SRAM assertion and SRAM manifest to name the
shared scratch while retaining the same 144-byte total. The final 48 bytes of
the union are simply unused while the boot-reader type view owns it.

### 5.4 Comments and invariant

Update the scratch declaration and the two reader comments to state the real
lifetime invariant:

> All 96 HCNAMES Instrument types live in the Bank-child operation scratch,
> outside the payload-staging union, from register parsing through the last
> Scene row evaluation. A per-Scene copy from payload staging is insufficient
> because loading an earlier Scene destroys the still-needed entries of later
> Scenes. Normal Bank Load may reuse the scratch only after the boot reader
> returns; `filesystem_start()` resets it before the Bank-child scan.

No public API signature or file-format change is needed.  The existing public
header comments receive the lifetime-guarantee clarification scheduled in
§9.3; the private scratch type and all storage access remain in
`filesystem.c`.

## 6. Scope exclusions

Do not fix this by weakening
`filesystem_bootReaderNarrowLoadInstrument()`'s type checks. Those checks
prevent a descriptor image from being interpreted as the wrong Instrument
type and are correct when given the real HCNAMES token.

Do not derive an Instrument row's type from the just-loaded parent kitset or
from the resident slot after the Kit load. An independently sourced
Instrument row may legitimately override its parent's type; the mandatory
HCNAMES type token is the authority for that row.

Do not add another HCNAMES rewrite, directory scan, fallback Bank load, or
Case-3 exception. HCNAMES names/sources/types are correct before boot, and P1
is acting correctly after it receives a false `load_ok == 0` result.

Do not borrow `fs_list_cache_name`. Stage 10 deliberately reloads the root Bank
index into that cache, and `main.c` calls `filesystem_bankSlotExists()` after a
reader decline but before canonical Bank Load starts. Repurposing or disposing
that cache during the reader would make a valid fallback Bank appear absent or
would require an unnecessary index reload. `op_bank_child_scratch` has the
required non-overlapping lifetime without disturbing that decision.

The current `afatfs_chdir(NULL)` synchronization change is not the cure for
this failure: it is already present in the byte-identical Reader-8 image, yet
Scenes 1..15 still fail at the predicted type-lifetime boundary. Retain or
revert that lower-layer change only on its own evidence; it is outside this
targeted fix.

## 7. RAM impact and ownership boundary

The reader needs exactly 96 stable bytes: 16 resident Scenes x 6 Instruments x
one `uint8_t` type token. Names and source/refreshed metadata already have
separate durable storage and do not belong in this image.

The fix adds **zero bytes** to `.bss`, `.data`, DTCM, heap, or the main stack.
It overlays those 96 bytes on the existing, already-accounted 144-byte
`op_bank_child_display` allocation and removes the current six-byte per-Scene
automatic arrays. The resulting source-level stack use therefore decreases by
six bytes in each affected reader frame rather than increasing by 90 bytes.

Owner and lifetime are explicit:

- boot-reader view: pre-audio Stage 11, from HCNAMES parse/regeneration through
  completion of all Scene row evaluations;
- Bank-child display view: a canonical/runtime Bank Load, from its child scan
  through the delegated Scene opens;
- transition: the reader returns before canonical fallback starts, and every
  new filesystem request clears the shared operation scratch.

No new-RAM acknowledgement is required because the retained 144-byte
allocation and its peak remain unchanged. The ownership/comment/assertion
changes must nevertheless be included so a future edit cannot make the two
views live concurrently.

## 8. Acceptance test

Re-run a fresh copy of `SD_CARD_READER_4`; do not use the already-mutated
post-boot Reader-7/8 HCNAMES files.

Required result:

- no `AutoSave Sc NN empty` notice;
- resident Scenes 0, 1, 14, and 15 load and play root Scene 008 `Rollin`;
- resident Scenes 2..13 load and play their Bank-001 child content;
- every per-row boot `Q` record is flags `0x04`; there are no flags `0x02`
  records;
- the summary has Case-2 mask `0xffff` and Case-3 mask `0x0000`.

The raw summary value must be **`0x0000ffff`**, because the implementation
packs `case2_scene_mask` into bits 0..15 and `case3_scene_mask` into bits
16..31. The `0xffff0000` expected value written in
`061_READER_LOADED_SCENES_INVALID.md` sections 18.2 and 19 has the halves
reversed and should not be used as the test oracle.

After the runtime drain, `R` flags may legitimately clear, but the four Rollin
Scene rows must remain source `008`, their Kit rows source `-`, and their 24
Instrument rows source `-`; no row may become `?`.

Also run one matching-winner mixed Case-1/Case-2 fixture. It must prove the
same stable 96-byte scratch view is used by
`filesystem_autosaveBootReaderBlocking()`, not only by the
HCNAMES-authoritative path exercised by `SD_CARD_READER_4`.

## 9. Full implementation schedule (deep-dive baseline: `ff767e8`)

This section is the implementation checklist.  All line references are to the
clean current tree at `ff767e8`; use the named declaration/function anchors if
an earlier edit shifts a later line.  No new allocation, file-format field,
public API, state-machine phase, or `main.c` call order is authorized by this
schedule.

### 9.1 `Core/Hardware/SD/filesystem.c` — private storage and invariants

1. **Lines 952-977, `filesystem_stage_workspace_t`: remove only the
   `boot_reader_type[96]` union member and its now-false comment.**

   The typed stage must return to being exclusively a destructive payload
   workspace (`kit_stage`, `instrument_stage`, `scene_stage`, writer, and
   regeneration state).  The removal is necessary because each Case-2 narrow
   loader legitimately writes that union, whereas the HCNAMES types must remain
   readable until the final Scene has been evaluated.  There are no inputs or
   outputs at this declaration; its effect is to make accidental aliasing
   impossible at the storage-ownership boundary.  Common stage accessors are
   `op_staged_kit`, `op_staged_instrument`, and
   `filesystem_initSceneStage()`.  Affiliates are all three narrow loaders and
   the two boot-reader traversal functions in items 6-7 below.

   Adjacent replacement comment text for the remaining stage declaration:

   ```c
   /*
    * The payload stage is destructive parser/commit storage only.  It must not
    * retain boot-reader HCNAMES metadata: Case-2 Scene, Kit, and Instrument
    * loads overwrite these union views before later rows are evaluated.
    * Durable per-row boot metadata belongs to a separate operation scratch.
    */
   ```

2. **Lines 1235-1255, replace `op_bank_child_display` with the explicitly
   shared `filesystem_bank_child_scratch_t` union and add the count macro next
   to it.**

   Define `FS_BOOT_READER_INSTRUMENT_TYPE_COUNT` as
   `AUTOSAVE_SCENE_COUNT * AUTOSAVE_INSTRUMENTS_PER_KIT`; then declare:

   ```c
   typedef union {
       char bank_child_display[STORAGE_BANK_SCENE_MAX_SLOTS]
                              [STORAGE_SCENE_DISPLAY_NAME_LEN + 1u];
       uint8_t boot_reader_type[FS_BOOT_READER_INSTRUMENT_TYPE_COUNT];
   } filesystem_bank_child_scratch_t;

   static filesystem_bank_child_scratch_t op_bank_child_scratch;
   ```

   This preserves the existing 144-byte `.bss` object while giving its first
   96 bytes a stable boot-reader view.  Its input is mutually exclusive owner
   activity, not data passed through an API: the boot reader fills all 96
   entries from parsed HCNAMES rows (or winner-reconstructed `SceneData`), and
   normal Bank Load fills all 16 display cells during its child scan.  Its
   outputs are respectively a type selected by `[scene * 6 + slot]` and a
   Bank-child name selected by `[child_slot]`.  The relevant common accessors
   are `filesystem_bootReaderApplyRowType()`,
   `filesystem_bootReaderSeedInstrumentTypes()`,
   `filesystem_bootReaderEvaluateScene()`,
   `filesystem_bootHcnamesAuthoritativeLoad()`,
   `storage_parseBankSceneFolder()`, and
   `filesystem_displayPrecedesCached()`.  Affiliates are the runtime Bank
   scan/child-dispatch phases (items 4-5) and both blocking boot readers
   (items 6-7).

   Adjacent declaration comment text:

   ```c
   /*
    * One 144-byte operation scratch with strictly non-overlapping views.
    * `boot_reader_type` is owned from complete HCNAMES parsing (or winner
    * regeneration) through the final Stage-11 Scene evaluation; its 96
    * entries must not share payload-stage storage because an earlier Case-2
    * load destroys types still needed by later Scenes.  `bank_child_display`
    * is owned only by asynchronous Bank Load from its child scan through the
    * delegated child opens.  A reader returns before canonical Bank Load can
    * begin, and filesystem_start() clears this whole object for each request.
    */
   ```

3. **Lines 10617-10625, replace the old single-view assertion and retain the
   aggregate SRAM assertion through `sizeof(op_bank_child_scratch)`.**

   Add one assertion for the 96-byte type image and one for the unchanged
   144-byte union; change the Option-1 aggregate calculation to reference the
   union object.  These checks take no runtime input or output.  They prevent
   an enum/count change from silently shortening the boot image, and prevent a
   later added union member from consuming RAM reserved for Pattern data.  The
   common compile-time inputs are `AUTOSAVE_SCENE_COUNT`,
   `AUTOSAVE_INSTRUMENTS_PER_KIT`, `STORAGE_BANK_SCENE_MAX_SLOTS`, and
   `STORAGE_SCENE_DISPLAY_NAME_LEN`; the output is a build failure on a broken
   contract.  Affiliates are the RAM policy in `MEMORY.md`, the primary-owner
   table in `SRAM_MANIFEST.md`, and the two runtime views in item 2.

   Adjacent assertion comment text:

   ```c
   /* Keep the shared operation scratch within the already-approved Option-1A
    * 144-byte reservation: 16 x 6 HCNAMES type bytes fit without growing the
    * normal-SRAM1 peak, while Bank Load still retains 16 x 9 display cells. */
   ```

4. **Lines 13415-13445 and 13879-13895, redirect all normal Bank Load display
   accesses to `op_bank_child_scratch.bank_child_display`.**

   In phase 15, test/copy the lexical-winning child name through the display
   member.  In phase 27, copy the selected member into `op_scene_display_name`.
   This is a mechanical member qualification; the scan's duplicate-winner rule,
   selection mask, folder parsing, and child-loading behavior do not change.
   Inputs are the Bank directory objects and `op_bank_child_cursor`; outputs
   remain `op_bank_child_present_mask` plus one selected Scene display name.
   The common parser/accessor pair is `storage_parseBankSceneFolder()` and
   `filesystem_displayPrecedesCached()`; the consumer is
   `filesystem_loadSceneDirectory_tick()`.  Affiliates are
   `filesystem_requestLoadBank()` and `filesystem_start()`.

   Adjacent phase-15/phase-27 comment text:

   ```c
   /* Use the Bank-child view only during the asynchronous Bank Load lifetime.
    * It is the same 144-byte object the boot reader uses earlier for types,
    * but no blocking Stage-11 reader is active while these phases scan or
    * consume child names. */
   ```

5. **Line 25003, `filesystem_start()`: clear the complete union with
   `memset(&op_bank_child_scratch, 0, sizeof(op_bank_child_scratch))`.**

   This replaces the display-array reset and makes request admission the
   explicit handoff from a completed boot reader (or prior Bank Load) to a new
   normal operation.  The input is every newly accepted asynchronous request;
   the output is a zeroed scratch before the Bank scan can populate names.  It
   does not reset the scratch during delegated Bank children, so names remain
   available until phase 27 consumes each one.  The common owner is
   `filesystem_start()`; its immediate Bank Load affiliate is
   `filesystem_requestLoadBank()`, while blocking readers never call this
   reset inside their traversal.  Preserve the surrounding rule that generic
   setup must not clear `fs_stage_workspace` or `fs_list_cache_name`.

   Adjacent reset comment text:

   ```c
   /* Reset the shared Bank-child/boot-reader scratch only at a new async
    * request boundary.  This releases a finished reader's immutable type
    * image before Bank Load writes names, without clearing payload stage or
    * the library cache whose lifetimes are independently owned. */
   ```

### 9.2 `Core/Hardware/SD/filesystem.c` — type producers and boot consumers

6. **Lines 27240-27298, `filesystem_bootReaderApplyRowType()`; lines
   27354-27382, `filesystem_bootReaderSeedInstrumentTypes()`; and lines
   27301-27315, the register-parser comment: redirect both producers to the
   shared type view and correct their lifetime documentation.**

   `filesystem_bootReaderApplyRowType()` continues to parse a mandatory third
   HCNAMES Instrument field and writes the decoded token to
   `op_bank_child_scratch.boot_reader_type[scene * 6 + slot]`.  The regeneration
   producer continues to copy `scene->kit.instruments[slot].type` to that same
   coordinate for all 16 Scenes.  Their inputs remain `(row, line)` and
   `SceneData`, respectively; their output is the full immutable 96-entry image
   required by subsequent Case-2 row loads.  Row-coordinate helpers/constants
   are `FS_RESIDENT_NAMES_INSTRUMENT_BASE`, `STORAGE_KIT_SLOT_COUNT`,
   `AUTOSAVE_SCENE_COUNT`, and `AUTOSAVE_INSTRUMENTS_PER_KIT`; type conversion
   remains `storage_instrumentTypeFromText()`.  Affiliates are
   `filesystem_bootReaderParseRegisterFile()`,
   `filesystem_regenerateHcnamesFromWinnerBlocking()`, and both readers below.
   Do not alter HCNAMES parsing, header validation, formatter, or the type
   token itself.

   Adjacent producer comment text:

   ```c
   /* Store every parsed/regenerated HCNAMES type in the non-stage shared
    * scratch.  The index is the fixed resident Scene/slot coordinate and must
    * remain valid through all later Case-2 loads, including loads for other
    * Scenes that overwrite fs_stage_workspace. */
   ```

7. **Lines 27495-27621, `filesystem_bootReaderEvaluateScene()`: remove the
   six-byte `instrument_types` local and its copy loop, then read the Case-2
   Instrument type directly from the full shared image.**

   Preserve the 1,920-byte `scene_section` automatic buffer, row order, Case
   1 behavior, source resolution, narrow-loader call signature, Case-3 P1
   handling, and trace packing.  For `index >= 2`, calculate `slot = index -
   2` exactly as today and pass `(instrument_type_t)` from the shared image at
   `scene_index * AUTOSAVE_INSTRUMENTS_PER_KIT + slot`.  Inputs are the
   destination Scene coordinate, winner record, parsed/reconstructed 96 types,
   and row provenance; outputs remain committed payload/Case masks and the
   `rows_changed` result.  Common accessors are
   `filesystem_residentInstrumentRow()`,
   `filesystem_bootReaderResolveResidentRow()`, and
   `filesystem_bootReaderNarrowLoadInstrument()`.  Affiliates are
   `autosave_applyInstrumentPayload()`, `filesystem_bootReaderEmptyScene()`,
   and the type producers in item 6.

   Adjacent traversal comment text:

   ```c
   /* Case-2 Instrument selection reads the complete immutable HCNAMES type
    * image, not a per-Scene stack copy.  Earlier narrow loaders may overwrite
    * fs_stage_workspace, but cannot affect this separate scratch; later
    * Scenes therefore receive their original row types as well. */
   ```

8. **Lines 27655-27688 and 27705-27764,
   `filesystem_autosaveBootReaderBlocking()`: amend the orchestrator comment
   to name the borrowed shared scratch, but do not change its control flow.**

   The register parse already fills all 96 entries; the winner-regeneration
   branch already calls the seeder.  This item is a comment-only correction
   adjacent to those two existing producer paths, documenting that the image
   survives from Step 1 through the Step-3 loop.  Inputs/outputs stay as stated
   in the public contract: a matching winner plus HCNAMES produce resident
   Bank/Scene state, boot-latch masks, and optional HCNAMES publication.
   Common accessors are `filesystem_bootReaderParseRegisterFile()`,
   `filesystem_bootReaderSeedInstrumentTypes()`, and
   `filesystem_bootReaderEvaluateScene()`; affiliates are main.c Stage 11 and
   the HCNAMES-authoritative sibling.  The comment must explicitly say that the
   144-byte object is pre-existing and borrowed, rather than claiming the
   reader has no relevant static scratch.

9. **Lines 27876-27932, `filesystem_bootHcnamesAuthoritativeLoad()`: remove
   the per-Scene six-byte local/copy loop and select Case-2 Instrument types
   directly from `op_bank_child_scratch.boot_reader_type`.  Amend the function
   comment at lines 27797-27812 to state the same full-traversal invariant.**

   This is the path exercised by `SD_CARD_READER_4`; it must use precisely the
   same index expression as item 7.  Inputs are the 129 parsed refreshed rows,
   current boot Bank slot, `bankset.bcg` presence mask, and the full type image;
   outputs are constructed Bank/Scene state, Case-2/Case-3 latches, notices,
   and only the established publication for emptied Scenes.  Common accessors
   are `filesystem_bootNarrowLoadBank()`,
   `filesystem_residentInstrumentRow()`,
   `filesystem_bootReaderResolveResidentRow()`, and
   `filesystem_bootReaderNarrowLoadInstrument()`.  Affiliates are main.c
   Stage 11, the matching-winner reader, and P1's
   `filesystem_bootReaderEmptyScene()`.  Do not derive the type from
   `op_kitset`, `SceneData` after the Kit load, or an Instrument file: a direct
   child source may validly differ from its parent Kit.

   Adjacent authoritative-loop comment text:

   ```c
   /* All 96 type entries were captured before filesystem_bootNarrowLoadBank()
    * and any narrow child load.  Read this Scene/slot directly from the
    * shared boot-reader view so Scene 0 cannot corrupt the type provenance
    * needed for Scenes 1..15. */
   ```

### 9.3 `Core/Hardware/SD/filesystem.h` — contract comment only

10. **Lines 329-375, public comments for
    `filesystem_autosaveBootReaderBlocking()` and
    `filesystem_bootHcnamesAuthoritativeLoad()`: add the common 16-by-6
    HCNAMES-type preservation guarantee; change neither declaration nor
    signature.**

    This header change records the externally meaningful guarantee shared by
    both Stage-11 readers: before a Case-2 narrow loader can alter staged
    payload state, all mandatory Instrument-row types are retained for the
    entire traversal.  Inputs are the parsed register (or winner-regenerated
    row image) and row coordinates; output is correct type-directed narrow
    Instrument selection for every present Scene.  The caller-facing accessors
    remain the two existing public functions; internal affiliates are the
    producer/parser functions in item 6 and the consumers in items 7 and 9.
    Do not expose `filesystem_bank_child_scratch_t`, add a header macro, or
    give callers access to either scratch view: allocation and lifetime remain
    private to `filesystem.c`.

    Adjacent header comment text:

    ```c
    /* Before any Case-2 narrow load, the reader preserves the complete
     * 16-by-6 HCNAMES Instrument-type image for the whole traversal.  Later
     * payload staging may not change the type used to resolve any remaining
     * Instrument row.  This is an internal zero-growth lifetime guarantee;
     * the public API and on-card format are unchanged. */
    ```

### 9.4 Authoritative documentation and explicit non-changes

11. **`knowledge_files/specification_reference/FILESYSTEM_SPEC.md:746-753`: amend
    the Bank Load scratch rule.**  Rename the implementation object to
    `op_bank_child_scratch.bank_child_display[16][9]`, retain the single-scan
    O(n) behavior, and add that its alternative 96-byte type view is private
    to a completed-before-Bank-Load Stage-11 reader.  Inputs/outputs for Bank
    Load remain directory objects -> presence/name cells; the new statement
    documents the non-overlap with HCNAMES rows -> type cells.  Affiliates are
    `filesystem_start()`, the phase-15/27 code, and the two boot readers.

12. **`knowledge_files/specification_reference/SRAM_MANIFEST.md:79` and
    `165-171`: replace the old owner name with `op_bank_child_scratch` and
    describe both views while retaining 144 B and the 1,311-B Option-1 total.**
    This is an accounting correction, not an allocation request: input is the
    static union definition; output is an unambiguous owner/lifetime record for
    the RAM policy and future linked-size audit.  Affiliate references are the
    `_Static_assert`s in item 3 and the filesystem-spec entry in item 11.

13. **`MEMORY.md:20-50`, only after source build verification and hardware
    acceptance: add the concise confirmed Session-061 outcome and measured
    linked sizes.**  Record zero persistent growth, the shared 144-byte owner,
    the completed fixtures, and any remaining hardware work.  Do not edit the
    historical Session-058 handoff or session index: those correctly describe
    what the object was at that time and are archival, not current authority.

14. **Deliberate non-changes, checked against the current tree:**

    - `main.c:852-898` remains unchanged: Stage 11 already orders matching
      winner reader, authoritative HCNAMES reader, then canonical Bank ladder.
    - `storageTypes.c/.h`, HCNAMES header/parser/formatter
      (`filesystem.c:5624-5692`, `5694-5805`, `21490-21674`), and the on-card
      `#types`/third-column schema remain unchanged; they already validate and
      serialize the required tokens.
    - `filesystem_bootNarrowLoadBank()` (`filesystem.c:26746-26869`) remains
      unchanged: it uses only a local `display[9]`, proving it cannot overlap
      the new union view during Stage 11.
    - No AutoSave wire offsets, trace record layout, HCNAMES rewrite policy,
      P1 Case-3 rule, type-validation guard, cache/stage ownership, or public
      API is changed.

### 9.5 Implementation order and verification gate

Implement in this order: item 1; item 2; item 3; items 4-5; item 6; items
7-9; item 10; then items 11-12.  Before building, use `rg` to prove no
`op_bank_child_display`, `fs_stage_workspace.boot_reader_type`,
`instrument_types[AUTOSAVE_INSTRUMENTS_PER_KIT]`, or stale union-lifetime
comment remains outside intentional historical documents.  Build with `make &&
make img`; inspect `arm-none-eabi-size build/lxr02.elf` and require unchanged
`.data`/`.bss` versus the pre-edit linked image (text may move).  The compile
assertions must pass without changing the 1,311-B reservation.

Then run §8's fresh `SD_CARD_READER_4` fixture and preserve a new post-boot
card capture.  Verify Q records, summary `0x0000ffff`, preserved sources, and
absence of the empty notices.  Finally run the matching-winner mixed Case-1/
Case-2 fixture.  Only after both hardware paths pass may item 13 update
`MEMORY.md` and this plan's status from diagnosis/schedule to implemented and
verified.

## 10. Implementation log

### 2026-09-07 — source implementation and documentation pass

- Confirmed the clean working tree matched the scheduled defect: all parsed
  Instrument types were still stored in `fs_stage_workspace`, and both readers
  still made a six-byte per-Scene snapshot.
- Removed `boot_reader_type[]` from the destructive payload-staging union and
  overlaid its 96-byte image on the existing 144-byte Bank-child scratch as
  `op_bank_child_scratch.boot_reader_type[]`. The Bank Load display users now
  use `op_bank_child_scratch.bank_child_display[]`, and `filesystem_start()`
  clears the complete union at each new asynchronous request boundary.
- Redirected both type producers and both Stage-11 traversal paths to the
  stable full image. Added compile-time checks for the 96-byte type view, the
  unchanged 144-byte union, and the unchanged 1,311-byte Option-1 accounting.
- Added adjacent lifetime/ownership comments in the changed `.c` code and the
  public `.h` contracts. Updated `FILESYSTEM_SPEC.md` and `SRAM_MANIFEST.md`
  to describe the shared owner without changing the file format or API.
- `make && make img` passed. The linked image reports
  `text=407,060`, `data=404`, `bss=96,212`; `.data`/`.bss` are unchanged from
  the pre-edit Session-061 image. Static audit found no stale old symbol or
  staging-union type access in active `.c`/`.h` source, and `git diff --check`
  passed.
- Hardware fixtures are not available in this workspace, so the fresh
  `SD_CARD_READER_4` authoritative-path fixture and mixed matching-winner
  fixture remain pending before this document can be marked hardware-verified.

## 11. Independent post-implementation assessment

### 2026-09-07 — source review verdict

**No blocking source-level finding.** The implementation matches the targeted
zero-growth fix and closes the demonstrated cross-Scene aliasing path. No
further code change is indicated before hardware acceptance.

The dataflow is now correct:

- the staging union no longer contains `boot_reader_type[]`;
- the HCNAMES parser and winner-regeneration seeder are the only semantic
  producers of the `op_bank_child_scratch.boot_reader_type[]` image;
- the matching-winner and HCNAMES-authoritative traversals read each Case-2
  Instrument type from that complete 96-byte image;
- both six-byte per-Scene arrays and their too-late copy loops are gone; and
- active C/header source contains no stale
  `fs_stage_workspace.boot_reader_type`, local
  `instrument_types[AUTOSAVE_INSTRUMENTS_PER_KIT]`, or standalone
  `op_bank_child_display` access.

The shared-scratch lifetime is also sound. The Stage-11 narrow Bank loader uses
its own local display cell, and the Scene/Kit/Instrument/pattern loaders use the
blocking filesystem helpers rather than `filesystem_start()`. They therefore
cannot reset or claim the Bank-child view during row evaluation. The
asynchronous starts reachable from the readers occur on safe sides of the
lifetime: winner regeneration starts before
`filesystem_bootReaderSeedInstrumentTypes()` repopulates all 96 entries, while
HCNAMES publication and trace flushing start only after every Scene row has
been evaluated.

Failure handoff remains valid. A partial temp/live HCNAMES parse is never
consumed: a successful replacement parse overwrites all 96 mandatory
Instrument rows, successful regeneration starts with a scratch reset and then
seeds all entries, and total failure returns without evaluating a row. After a
reader decline, `main.c` performs `filesystem_bankSlotExists()` against the
separate library-name cache. An accepted canonical Bank request then enters
`filesystem_start()`, which clears the complete union before Bank Load scans or
consumes `bank_child_display[][]`. Stale boot types therefore cannot affect
fallback, and the Bank index needed to select fallback was not borrowed.

Independent build and linked-size verification against clean `HEAD` baseline
`3756b78` produced:

| Image | text | data | bss |
|---|---:|---:|---:|
| baseline | 407,100 B | 404 B | 96,212 B |
| implementation | 407,060 B | 404 B | 96,212 B |
| change | -40 B | 0 B | 0 B |

The linked symbols remain exactly 2,048 bytes for `fs_stage_workspace` and 144
bytes for the renamed `op_bank_child_scratch`. LTO disassembly also confirms
that removing the local six-byte arrays reduced the compiler's local stack
reservation by eight aligned bytes in each affected frame:
`filesystem_bootReaderEvaluateScene()` is 1,996 -> 1,988 bytes and
`filesystem_bootHcnamesAuthoritativeLoad()` is 60 -> 52 bytes.

`make -j2`, `make img`, and `git diff --check` pass. The generated
`build/LXRV2_lxr02.img` is 407,480 bytes with SHA-256
`5732e821d256f521e48814d2cf255c895b1fbb7fdfa9f006b43f5ae293fb8c62`.
The build retains unrelated existing unused-function and bare-metal syscall
stub warnings; none names or originates in the changed scratch accesses.

The remaining gate is hardware evidence, not another source edit. Run a fresh
copy of `SD_CARD_READER_4` and require the authoritative-path results in
section 8, especially no Case-3 `Q` record and raw summary `0x0000ffff`. Then
run the mixed matching-winner Case-1/Case-2 fixture. Until both pass, this fix
is source-reviewed and build-verified, but not hardware-verified. The captured
`SD_CARD_READER_4` directory is present in the workspace; what is unavailable
here is execution of the rebuilt image on the target hardware, rather than the
input data itself.

## 12. `SD_CARD_READER_9` post-boot hardware assessment

### 2026-09-08 — authoritative-path verdict: pass

`SD_CARD_READER_9` is the post-boot capture made by running the fixed image on
the `SD_CARD_READER_4` input. It passes the section 8 acceptance criteria for
the HCNAMES-authoritative path and directly demonstrates that the
cross-Scene Instrument-type corruption is fixed.

The fixture provenance is sound:

- `SD_CARD_READER_9/LXRV2_lxr02.img` is byte-identical to the reviewed build
  image. Both have SHA-256
  `5732e821d256f521e48814d2cf255c895b1fbb7fdfa9f006b43f5ae293fb8c62`.
- The complete `Bank`, `Scene`, `Kit`, and `Instrument` library trees are
  byte-identical between Readers 4 and 9. `settings.cfg` is also
  byte-identical and still selects `active_bank=1` with `autosave=1`.
- There is one live `.hcnames`, both `.hcprms` records, and no residual
  `/.hcnamtmp` transaction file.

The boot trace is conclusive. Record `#015346` validates the old generation-8
winner in `.hcprms2`, after which records `#015347..#015474` contain exactly
128 successful Case-2 row completions: eight rows for every one of the 16
Scenes. All have flags `0x04`; there is no Case-3 (`0x02`) row. The four
overlaid resident Scenes 0, 1, 14, and 15 resolve their Scene, Kit, and six
Instrument rows through effective source 8. The remaining Bank children
resolve through effective source 1. Summary record `#015475` is exactly:

```text
case2_scene_mask=0xffff, case3_scene_mask=0x0000
raw value=0x0000ffff
```

There is no `E` operation-error or `X` phase-stall record. The trace's `Q`
total is 129, exactly the 128 row results plus the one summary. Consequently,
all 16 Scenes were accepted and none was emptied by P1. This is the precise
hardware result that the earlier six-byte snapshots failed to produce.

The resulting HCNAMES register is also correct:

- it has the required `#types` header and exactly 129 data rows;
- it contains no unknown (`?`) source;
- resident Scenes 0, 1, 14, and 15 are all named `Rollin` with direct source
  `008`; their Kits and Instruments retain inherited source `-` and the six
  required types `drm, drm, drm, snr, cym, hat`; and
- compared with Reader 4, identities and sources were preserved. The only
  changes are the expected removal of `R` witnesses as autosave objects became
  fully captured.

Both updated autosave records are exact-size, committed, CRC-valid format-v1
records with valid reserved bytes:

| Record | Generation | Probe | CRC32C | Dirty bits | Bank | Present | Active / VOICE |
|---|---:|---:|---:|---:|---|---:|---|
| `.hcprms1` | 9 | 8 | `0x2449f3fb` | 6,671 | `001 Full` | `0xffff` | Scene 6 / `0x0040` |
| `.hcprms2` | 10 | 9 | `0x12caa3b9` | 5,138 | `001 Full` | `0xffff` | Scene 0 / `0x0001` |

Generation 10 in `.hcprms2` is the unambiguous current winner. Its coherent
Scene-0 active and VOICE masks describe a later live selection than Bank
001's saved Scene-6/`0x0040` startup values retained in generation 9; this is
consistent with selecting the first recovered Rollin Scene while checking the
result. If no such post-boot selection was made, that UI-state change is a
separate fact to investigate, but it is not evidence of a wrong Bank or failed
Scene recovery.

Most importantly, the generation-10 file-carried dirty mask and the physical
HCNAMES register agree for every one of the 129 object rows: there are **zero
`R`-versus-object-mask mismatches**. Every autosave source/type mismatch is
confined to an object that is still dirty and still carries `R`; there is no
mismatch in any clean object. That is the required power-loss invariant: a
later matching-winner boot may trust clean Case-1 objects and must Case-2
reload the still-refreshed objects from their authoritative HCNAMES sources.

The capture was taken during normal continuation convergence, not after the
entire Bank became clean. HCNAMES has 46 clean rows and 83 rows still carrying
`R`. The clean frontier includes the Bank, complete Scenes 0..4 and their
children, plus Instruments 0..4 of Scene 5. In particular:

- autosave Scenes 0 and 1 are fully clean; both carry Scene source 8,
  inherited Kit/Instrument sources, and the correct six Instrument types;
  after excluding the deliberately stale embedded name bytes, their complete
  1,920-byte autosave sections are identical, as expected from loading the
  same `Scene/008 Rollin` source;
- Scenes 14 and 15 still carry `R`, so their older record payload/source bytes
  are correctly not authoritative yet; their HCNAMES rows retain direct
  source 8 and will force Case 2 if power is lost before later drains capture
  them; and
- stale embedded Scene/Kit/Instrument names in `.hcprms` are expected by the
  documented format. Those name bytes are deliberately not dirtied or
  refreshed; HCNAMES is the boot identity authority.

The trace file ends with the generation-9 terminal record, while the card also
contains the later valid generation-10 commit and the HCNAMES convergence
state matching its mask. This only means the later lifecycle trace records
had not themselves reached the low-priority trace file before capture; the
generation-10 commit byte and whole-record CRC prove that the autosave
publication completed.

`tools/verify_bank_autosave.py SD_CARD_READER_9 1` is not a valid pass/fail
oracle for this mixed-source fixture: by design it requires all resident
identities and sampled payloads to equal the unchanged `Bank/001 Full` library
tree. It therefore reports the four intentional `Rollin` overlays (and the
later active-Scene selection) as failures, even though it correctly selects
`.hcprms2` and reports `present_mask=0xffff`. The row-aware HCNAMES/source/type
and dirty-mask checks above are the applicable validation.

Fixture hashes for later comparison are:

```text
.hcnames   7d54c7f4a4a1cdf6d362ad4cfb019fc298007186a18f3ec7dea38c1ecd23ed02
.hcprms1   5d641b94252acb184d4965d5b087336c69dd21e3ac492ff799ecda432c8b4ea2
.hcprms2   cc83be68bf8024d64833ef0b917ac92db2a713b3645cf934ebf46b0a6ea14639
asavetrc   3d45f11ac6ad9ae7be2784ae8c4a1cb4880bebb64dec989ba458c2b939fb0696
```

The fix can therefore be marked **hardware-verified for the original
HCNAMES-authoritative failure**. The overall section 9.5 gate is not yet fully
closed: boot Reader 9 once more and capture the result to exercise the
matching-winner mixed Case-1/Case-2 path. Reader 9 is a particularly strong
fixture for that test because its HCNAMES `R` rows and generation-10 dirty mask
are already in exact agreement.
