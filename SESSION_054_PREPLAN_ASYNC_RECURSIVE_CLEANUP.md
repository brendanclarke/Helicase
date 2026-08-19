# Session 054 Pre-Plan — Async Recursive-Delete Cleanup

**Status:** planning only. No source change was made while writing this
document. It supersedes and preserves the necessary content of
`RECURSIVE_TREE_DELETE_REIMPLEMENT.md`, `KIT_PARSE_BOOTLOCK_RESOLVE.md`, and
`LOAD_SAVE_AUTO_ELEMENT_TEST_REPORT.md`, which the user will delete after this
document is confirmed. Everything needed to resume work — build status, root
causes found by re-reading the current source, and full prior test evidence
— is captured below.

## 0. Where things stand right now

- **The firmware builds clean.** `make -j2` on the current worktree succeeds:
  `text=379,660 data=396 bss=94,612`. The recursive-delete reimplementation
  is NOT mid-compile-failure; the "compile failures" language in the handoff
  request referred to the test/verification pass, not a broken build. The
  only warnings are the pre-existing `_write`/`_read`/`_lseek`/`_close`
  stub-not-implemented linker warnings, five known `-Wunused-function` dead
  helpers already called out as legacy in the recipe log
  (`filesystem_writeStreamChunk`, `filesystem_applyStaleGlobalsFallback`,
  `filesystem_staleMetaPrefixLen`, `filesystem_applyLegacy22Globals`,
  `filesystem_metaHasStoredGlobalsLen`), and the documented
  `asyncfatfs.c:1146` unused `eraseCount` parameter.
- Only `Core/Hardware/SD/asyncfatfs/asyncfatfs.c`, `asyncfatfs.h`, and
  `Core/Hardware/SD/filesystem.c` are uncommitted in the working tree
  (plus `MEMORY.md`). `presetManager.c/.h`, `storageTypes.c/.h`, and
  `menu.c` shown modified in the session's initial git-status snapshot were
  already committed by the time this pre-plan was written (`git diff` against
  `HEAD` for those five files is empty); only re-verify this if a
  `git status` at the start of Session 054 disagrees.
- So Session 054 is **not** a compile-fix session. It is a **functional-bug**
  session against the working build, driven by the card-fixture test report
  below plus two additional root causes found in this pass by reading the
  current `filesystem.c` against that report.

## 1. Root-caused, ready-to-fix bugs (read the current source, confirmed by line number)

### 1.1 Overwrite resolver reports error after a successful delete (`ScnS05`, likely also `KitS`/`BnkS`)

**Symptom (from the overwrite test, `SD_OVERWRITE_TEST`):** Scene 019 was
correctly deleted and replaced (`019 Organity` → `019 Rollin`, complete tree
with sceneset.scg/Kit/6 instruments/pattern.pat/effects.fx all present and
correct on the card), but the save still reported error `ScnS05`
(`FS_INTERNAL_OP_SAVE_SCENE` phase 5 = `filesystem_deleteSlotDirectory_tick()`
returning `FS_STATUS_ERROR`).

**Root cause — confirmed in `Core/Hardware/SD/filesystem.c`:**

`filesystem_deleteSlotDirectory_tick()` (~line 12533) has a diagnostic-only
timeout observer: if a phase doesn't advance for more than 50,000 polls, it
calls `filesystem_makeNamedErrorCode()` and sets
`op_delete_slot_timeout_observed = 1u` (~line 12550), with an explicit
comment: *"Observation is not cancellation: native delete has no abort API,
so retain ownership until its callback releases the handle."* — i.e. the
intent is diagnostic-only and must not turn a subsequently-successful delete
into a failure.

But the final completion check contradicts that comment
(`FS_DELETE_SLOT_DELETE_MATCH`, ~line 12691):

```c
case FS_DELETE_SLOT_DELETE_MATCH:
    if (!op_delete_tree_done)
        return FS_STATUS_BUSY;
    if (op_delete_tree_result != AFATFS_RESULT_OK) {
        op_delete_slot_phase = FS_DELETE_SLOT_ERROR;
        return FS_STATUS_ERROR;
    }
    if (op_delete_slot_timeout_observed) {      // <-- bug
        op_delete_slot_phase = FS_DELETE_SLOT_ERROR;
        return FS_STATUS_ERROR;
    }
    op_delete_slot_phase = FS_DELETE_SLOT_DONE;
    return FS_STATUS_DONE;
```

A nested Scene tree (embedded Kit dir + 6 instrument files + pattern.pat +
effects.fx, each entry retired at most one name-run sector batch or one FAT
cluster per poll, per the recipe's own bounded-yield design) can easily take
more than 50,000 foreground polls to delete — most polls return `BUSY`
waiting on SD hardware, not making structural progress. Once that counter
trips, `op_delete_slot_timeout_observed` latches `1` and is **never cleared**,
so even though the native delete finishes with `AFATFS_RESULT_OK` afterward,
the code above still converts it into `FS_STATUS_ERROR`. This exactly matches
the observed evidence: the delete-and-recreate completed correctly on the
card, but the save still reported an error.

Kit Save and Bank Save share the same `filesystem_deleteSlotDirectory_tick()`
worker (Kit at ~line 12775, Bank's child-Scene path shares Scene's save tick),
so this same spurious-error class is a live risk for Kit and Bank overwrite
too, not just Scene — it just hasn't been triggered/observed there yet.

**Fix direction:** the diagnostic timeout should still be recorded (named
error code, logging) but must not by itself flip a later `AFATFS_RESULT_OK`
into `FS_STATUS_ERROR`. Either drop the
`if (op_delete_slot_timeout_observed) { ... return FS_STATUS_ERROR; }` block
from `FS_DELETE_SLOT_DELETE_MATCH` entirely (let the native result be
authoritative, matching the "observation, not cancellation" comment), or keep
it error-only for the specific case where the timeout fired *before* the
delete accepted/started (i.e. distinguish "we gave up waiting on something
that never finished" from "it finished fine, we just also logged that it took
a long time"). Also re-examine whether 50,000 polls is actually a reasonable
diagnostic threshold for a full nested Scene delete versus a single Kit/file
delete — it may need a larger budget for the Scene case specifically, or a
budget expressed in structural units (objects/clusters freed) rather than raw
poll count, consistent with §6.4 of the retained recipe below (bounded
corruption termination is *already* budget-based inside `asyncfatfs.c`; this
50,000-poll counter in `filesystem.c` is a separate, cruder outer diagnostic
layer and is the one actually misbehaving).

**Retest:** re-run the Scene overwrite fixture from `SD_OVERWRITE_TEST`
(load Scene, edit, save to an occupied slot) and confirm no `ScnS05`/`KitS`/
`BnkS` error on a successful overwrite. Also deliberately construct a large
nested Scene (multiple instruments, big kit) to try to actually reach the
50,000-poll diagnostic path and confirm it now still completes successfully
rather than erroring.

### 1.2 HCNAMES source provenance is never staged on Save (Kit/Scene/Bank/Instrument — all four)

**Symptom (from `LOAD_SAVE_AUTO_ELEMENT_TEST_REPORT.md`), confirmed as the
report's headline defect:** after Save, the `.hcnames` row's *name* updates
correctly but its *source* token keeps the previously-loaded slot instead of
the just-saved slot. Clearest case: Scene saved to slot `031`, row still says
source `004` (the loaded slot). Also observed for Kit (source stayed `013`
after saving to a new slot) and Instrument (`docwird1 -` never became `@`
even though the pool file was written).

**Root cause — confirmed by comparing every Save completion path against
every Load completion path in `Core/Hardware/SD/filesystem.c`:**

Every Load completion path stages the row's *source* explicitly, via
`filesystem_setResidentSource(row, source)`, immediately alongside
`filesystem_setIdentityName()`/`filesystem_cacheResidentName()`:

- Kit Load: ~line 8344-8349 (`filesystem_setResidentSource(kitRow, op_slot)`
  then instrument rows to `FS_RESIDENT_SOURCE_INHERIT`).
- Scene Load: ~line 8978-8990 (`filesystem_setResidentSource(sceneRow,
  op_slot)` when `current_op == FS_INTERNAL_OP_LOAD_SCENE`).
- Bank Load (empty-Bank branch): ~line 10277
  (`filesystem_setResidentSource(0u, op_slot)`).
- Bank Load (normal branch): ~line 10426
  (`filesystem_setResidentSource(FS_IDENTITY_BANK_ROW, op_slot)`).
- Instrument Load (pool/direct load): ~line 11213-11217
  (`filesystem_setResidentSource(instrumentRow,
  FS_RESIDENT_SOURCE_INSTRUMENT_DIRECT)`).

`filesystem_setResidentSource()` (line 4266) stages the value with a dirty
flag that survives the subsequent read-old-register pass — the read helper
`filesystem_cacheResidentRecord()` (line 4387, see line 4432
`if ((fs_resident_source[row] & FS_RESIDENT_SOURCE_DIRTY_FLAG) == 0u)`)
explicitly only takes the *old file's* source value when the row was **not**
already staged dirty by the caller. This is a correct, deliberate mechanism
— it is just never invoked from Save.

**None of the four Save completion paths call `filesystem_setResidentSource`
at all** — they call only the name-side helper:

- Kit Save: `filesystem_setIdentityName(FS_IDENTITY_KIT_ROW, ...)` at
  `filesystem_saveKitDirectory_tick()` phase 21, ~line 12930-12932. No
  paired `filesystem_setResidentSource()` call before or after.
- Scene Save: `filesystem_prepareResidentNamesCache()` +
  `FS_INTERNAL_OP_UPDATE_HCNAMES_SCENE` handoff at
  `filesystem_saveSceneDirectory_tick()` phase 37, ~line 13897-13929. The
  shared updater it hands off to,
  `filesystem_cacheCurrentResidentSceneNames()` (~line 4557-4585), only calls
  `filesystem_cacheResidentName()` — never `filesystem_setResidentSource()`.
- Bank Save: `filesystem_cacheResidentName(0u, op_bank_display_name)` at
  `filesystem_saveBankDirectory_tick()` phase 45, ~line 13361. No paired
  `filesystem_setResidentSource(0u, op_slot)` call (contrast with Bank Load's
  symmetric ~line 10426 call the adjacent comment explicitly says Bank Save
  "must remain symmetric" with).
- Instrument Save (non-morph pool write):
  `filesystem_setIdentityName((uint8_t)(FS_IDENTITY_INSTRUMENT_ROW_0 +
  op_instrument_save_source_slot), display)` at ~line 11576-11579. No
  `filesystem_setResidentSource(..., FS_RESIDENT_SOURCE_INSTRUMENT_DIRECT)`
  call — contrast with Instrument Load's ~line 11213-11217, which sets that
  exact source token for the exact same row shape. (Root Instrument Save also
  hands off straight to `FS_INTERNAL_OP_CREATE_BOOT_INDEX`, never to
  `FS_INTERNAL_OP_UPDATE_HCNAMES_INSTRUMENT`, so today it may not even
  publish the identity-name change into `.hcnames` — verify this at the same
  time; the shared `filesystem_cacheCurrentResidentInstrumentNames()` helper
  at ~line 4470-4507 has the same source-less pattern as the Kit/Scene
  helpers if it does get reached via Menu's deferred flush.)

**Fix direction — mechanical, symmetric with the four Load paths above:**

1. Kit Save (~line 12930, before the phase-21 `filesystem_finish`/rebuild
   handoff): call `filesystem_setResidentSource(filesystem_residentKitRow(op_kit_save_source_scene), op_slot)`,
   plus `FS_RESIDENT_SOURCE_INHERIT` for its six instrument rows, mirroring
   Kit Load ~line 8344-8358.
2. Scene Save (~line 13897, before arming
   `FS_INTERNAL_OP_UPDATE_HCNAMES_SCENE`): call
   `filesystem_setResidentSource(filesystem_residentSceneRow(op_kit_save_source_scene), op_slot)`,
   mirroring Scene Load ~line 8978-8983. Its embedded Kit/Instrument rows
   should get `FS_RESIDENT_SOURCE_INHERIT` the same way Scene Load does.
3. Bank Save (~line 13361, alongside `filesystem_cacheResidentName(0u,
   op_bank_display_name)`): call
   `filesystem_setResidentSource(FS_IDENTITY_BANK_ROW, op_slot)`, mirroring
   Bank Load ~line 10426.
4. Instrument Save (~line 11576-11579): call
   `filesystem_setResidentSource(filesystem_residentInstrumentRow(op_instrument_save_source_scene, op_instrument_save_source_slot), FS_RESIDENT_SOURCE_INSTRUMENT_DIRECT)`,
   mirroring Instrument Load ~line 11213-11217. Also confirm/fix that root
   Instrument Save actually reaches an HCNAMES publish step (today it jumps
   straight to `FS_INTERNAL_OP_CREATE_BOOT_INDEX`); if it relies on Menu's
   deferred flush instead, the staged dirty source will still survive until
   that flush runs `filesystem_cacheCurrentResidentInstrumentNames()`, but
   that helper needs the same one-line addition as items 1-3, or the flush
   will still silently drop the staged source.

**Retest:** repeat the four `LOAD_SAVE_AUTO_ELEMENT_TEST_REPORT.md` element
fixtures (delete `/.hcnames`+`/.hcprms*`+`bootlog.bin`+`asavetrc.bin`, boot,
Play, load an element, edit, save to a new slot, power off, capture card) and
confirm each `.hcnames` row's source token now matches the *saved* slot (or
`@` for Instrument), not the loaded one. This is the same table format used
in the retained report (§4 below) — reuse it directly for the Session 054
retest writeup.

## 2. Not yet root-caused — needs Session 054 investigation before a fix

These were observed in testing but this pass did not find a confirmed root
cause in the source. Do not guess a fix; trace the actual code path first.

1. **Kit Save did not materialize a library Kit** (`SD_KIT`: no new/renamed
   directory appeared under `/Kit/`, yet the resident Kit name changed to
   `SoyEarsv`). `filesystem_saveKitDirectory_tick()` phase 8 (~line 12786)
   unconditionally calls `afatfs_mkdir_lfn()` once the delete-slot phase
   (§1.1) completes, so on paper a new directory should always appear. One
   plausible explanation worth checking first: Menu may stage the new display
   name into the identity cache (or `preset_currentName`) at Save-page entry
   time, independent of whether the filesystem operation actually ran/
   completed — i.e. the name change observed on the card might be a red
   herring from a *different, earlier* successful save in the same session,
   or the save request never actually started
   (`op_save_kit_dir_display_name[0] == '\0'` guard at
   `filesystem_saveKitDirectory_tick()` phase 0 would abort silently to the
   caller's normal error path — check whether Menu surfaces that). Trace
   `filesystem_requestSaveKitDirectory()`'s caller in `menu.c` and confirm
   `op_save_kit_dir_display_name` is populated correctly for a fresh slot
   before assuming a deeper bug.
2. **Kit Save menu shows zero Kits despite `/Kit/.hcindex` being populated**
   (`SD_OVERWRITE_TEST` item 2). The Kit library and its `.hcindex` are
   intact (39 kits present); the shared name cache was empty on entering Kit
   Save after prior Scene/HCNAMES operations retagged it. Needs a menu-path
   trace: confirm Kit Save entry reloads `/Kit/.hcindex` whenever
   `fs_list_cache_kind` is not already `FS_NAME_CACHE_KIT` (see
   `fs_list_cache_kind`/`FS_NAME_CACHE_*` usage in `filesystem.c`; menu.c
   itself has no direct cache-tag references, so the entry-reload decision
   lives in whichever `filesystem_request*Index`/`filesystem_requestKit*`
   call Kit Save's page-entry handler makes).
3. **Bank Save page-entry freeze** (`SD_BANK`). No new/temp/partial Bank
   directory appears; `.hcnames` row 0 still shows the loaded Bank; trace
   ends with a `W` AutoSave-writer-suppressed record (`canonical mask
   dirty=1`), consistent with a hang at/inside Bank Save page entry before
   any Bank payload work starts. Needs a menu-path investigation of Bank
   Save's page-entry handler plus the new direct delete/recreate preflight
   (`filesystem_saveBankDirectory_tick()` early phases), not the AutoSave
   layer itself.
4. **Mid-drain boot freeze, `.hcprms2` truncated at 32,768 bytes instead of
   34,768** (`SD_FREEZE`). Signature of a power loss/freeze exactly at a
   32 KiB boundary during an AutoSave A/B record drain. `.hcprms1` remained
   the full valid winner, so this is recoverable, but the drain itself froze.
   Needs a look at `Core/Bank/Scene/Autosave.c`'s copy/CRC drain phase and
   whether it has any 32 KiB (cluster-boundary-sized) chunking behavior that
   could stall. Not yet located precisely in this pass.
5. **Boot Bank Load still times out at 20 s** (`bootlog.bin = B012S09I` in
   both `SD_FREEZE2` and the stale carry-over in `SD_OVERWRITE_TEST`). This
   is explicitly *not* the same as the Kit-quarantine boot lock in §3 below
   — it is the Bank Load embedded-instrument budget. `BOOT_FILESYSTEM_TIMEOUT_MS`
   was already raised from 10 s to 20 s between test passes and still isn't
   enough; per the retained recipe's explicit non-goals, do **not** raise
   the timeout again as the fix — profile why the embedded-instrument load
   sequence during boot Bank Load is slow instead.
6. **Instrument overwrite correctness unconfirmed.** `Instrument/Drum` (and
   Snare/Cymbal/HiHat) showed no new filename between `SD_FREEZE2` and
   `SD_OVERWRITE_TEST` snapshots, which is expected for a true in-place
   overwrite (same filename) — a directory listing can't distinguish "content
   overwritten correctly" from "nothing happened." Needs a content-level diff
   of the target instrument file, not just a listing.

## 3. Deferred/still-open from the two other source documents

- **`KIT_PARSE_BOOTLOCK_RESOLVE.md` is planning-only and still unapplied.**
  It targets `bootlog.bin == KQ003KST`, the pre-audio boot lock in
  `filesystem_createLibraryIndexBlocking(FS_LIBRARY_INDEX_KIT)`'s
  `filesystem_quarantineKitLibraryBlocking()` full-content Kit validation
  during boot (one shared 10 s `KITQUAR` deadline covering every root Kit's
  `kitset.kcg` + six member opens). Fix: delete the boot-time quarantine gate
  in `filesystem.c` (`filesystem_bootLoggingArm("KITQUAR ")` /
  `filesystem_quarantineKitLibraryBlocking()` /
  `filesystem_bootLoggingOperationDone()` / its abort-index-generation
  return), then retire the now-dead helpers
  (`filesystem_quarantineKitLibraryBlocking`,
  `filesystem_validateCurrentKitBlocking`, `filesystem_makeQuarantineName`,
  `filesystem_bootLoggingSetKitDetail`, the `fs_kit_validation_result_t`/
  `fs_kit_quarantine_result_t` enums, the `#if 0` embedded-Kit quarantine
  block) if nothing outside the boot path still calls them. Full Kit content
  validation already exists at load time
  (`filesystem_loadKitDirectory_tick()` phases 11-15) and doesn't need
  duplicating. Explicitly out of scope: do not raise
  `BOOT_FILESYSTEM_TIMEOUT_MS`, do not add SD timing holds, do not fold this
  into the delete-tree work. This is an independent, self-contained, low-risk
  fix and is a good Session 054 candidate alongside §1.1/§1.2 since it's
  fully scoped already and just needs the deletion executed plus the Step 6-8
  build/static/hardware verification in that document.
- **`RECURSIVE_TREE_DELETE_REIMPLEMENT.md` follow-up (2026-08-19 addendum):**
  boot no longer stalls at the Kit-quarantine gate in the fallback-boot run
  that was captured, but `/.hcnames`, `/.hcprms1`, `/.hcprms2` were absent
  from the card, which correctly produced `HNsL01`/`HNkL01` ("register
  missing or unreadable") errors entering Load:[Scene]/Load:[Kit] — this is
  confirmed to be *only* the missing-file condition, not a regression in the
  HCNAMES open/read path (`afatfs_chdir(NULL)` →
  `afatfs_fopen_lfn(".hcnames", "r")` → `filesystem_readTextLine()` →
  `afatfs_fclose()` are all untouched by the delete-tree recipe; only the
  create/absence-proof path, `filesystem_hcnamesProbe_tick()`, was touched,
  and that's a different code path than LOAD). Action for Session 054:
  restore a valid `/.hcnames` to the card and re-confirm Load:[Scene]/
  Load:[Kit] clear the two errors, then exercise the HCNAMES write/update
  path once (any Save that commits the register) and watch for `HNPrb`,
  `HNDup`, `HNsU`, or `HNkU` to confirm `filesystem_hcnamesProbe_tick()`
  still correctly returns ABSENT and creates the file when appropriate. This
  is quick and should be done early, since a missing `/.hcnames` will
  otherwise make every §1.2 retest look like a false failure.
- The recursive-delete recipe's own acceptance matrix (crossing-boundary LFN
  runs, malformed LFN, FAT16 vs FAT32 root, broken `..`/cyclic layout,
  injected FAT/cache error, exhausted handle pool — §10 of the original
  document) has never been executed on hardware/card fixtures. Once §1.1 and
  §1.2 are fixed and retested, that low-level acceptance matrix is the next
  layer of verification before calling the delete-tree reimplementation
  fully closed, per the recipe's own scope statement that it "remains the
  hardware/card test plan."

## 4. Full prior test evidence (preserved from `LOAD_SAVE_AUTO_ELEMENT_TEST_REPORT.md`)

Scope: four element types were exercised with the same sequence: delete the
card's working files (`/.hcnames`, `/.hcprms*`, `bootlog.bin`, `asavetrc.bin`),
boot, press Play, load an element, edit some parameters, save the element to
a new slot, then power off and capture the card.

Snapshots: `SD_INST/` (Instrument), `SD_KIT/` (Kit), `SD_SCENE/` (Scene),
`SD_BANK/` (Bank — froze entering Bank Save), `SD_FREEZE/` (boot freeze after
the Scene test).

### HCNAMES provenance table

| Snapshot | Row | Name | Source | Expected |
|---|---|---|---|---|
| SD_INST | 0 | Full | 008 | 008 (Bank) |
| SD_KIT | 23 (Kit 06) | SoyEarsv | 013 | saved slot |
| SD_SCENE | 7 (Scene 06) | Moch tsv | 004 | 031 (saved) |
| SD_BANK | 0 | LoadTst | 012 | 012 (no save occurred) |
| SD_FREEZE | 0 | Full | 008 | 008 |

- **Scene** (clearest failure): `Scene/031 Moch tsv` created on disk
  (filesystem-level save succeeded); resident row 7 correctly shows saved
  name `Moch tsv` but source stayed `004` (loaded slot) instead of `031`.
  Root-caused in §1.2.
- **Kit**: resident row 23 shows `SoyEarsv` with source `013` (loaded kit).
  No `SoyEarsv` directory exists under `/Kit/` — two separate defects: source
  not updated (§1.2, root-caused) and directory not materialized (§2 item 1,
  not yet root-caused).
- **Instrument**: `Instrument/Drum/barfd2sv.drm` created (pool-file write
  worked); every Instrument row stayed `-` (inherit) including row 69
  (`docwird1 -`, the Scene-6/slot-0 row the trace shows the load committed
  to). Root-pool instrument should publish source `@` (direct). Root-caused
  in §1.2.
- **Bank**: row 0 is `LoadTst` source `012` — correct only because the save
  never ran (froze before reaching payload work, see §2 item 3).

### AutoSave/trace health (not defects — confirms the writer itself works)

A/B ping-pong functions correctly (winner flips A→B→A across generations);
`V`/`M`/`C`/`P`/`T` records present with incrementing generations; runtime
parameter edits (`D` records) captured/drained; re-dirty/merge behavior
visible (mask merge `dirty=1` then later capture).

`tools/verify_bank_autosave.py` still FAILs on every snapshot with the same
signature (`present_mask=0x0000` expected `0xffff`, `active_scene=0` expected
`6`, `voice_edit_mask=0x0000` expected `0x0040`, Scene 06 payloads missing) —
this is the **already-deferred** boot Bank Load capture defect (Bank Load
runs before `autosave_setMutationTrackingEnabled(1)`, so its Bank-section
markers are rejected). Recorded as deferred in `SCOPING_TARGETS.md`; do not
conflate with the source-provenance bug above.

### Follow-up pass — overwrite-function test (`SD_FREEZE2`, `SD_OVERWRITE_TEST`)

Build/config: `DEV_MODE_DIAGNOSTIC=0`, `DEV_MODE_LOGGING=1`,
`BOOT_FILESYSTEM_TIMEOUT_MS=20000`.

- **Boot timeout (`SD_FREEZE2`)**: `bootlog.bin = B012S09I` — boot Bank Load
  still timing out, now with a 20 s budget (was 10 s), reaching Scene 09
  before the deadline. Not the AutoSave/HCNAMES layer. `asavetrc.bin` shows
  normal boot Bank Load `I`/`L` marks with `TRACKING_ENABLED=0`, confirming
  the boot-capture gap is still present. See §2 item 5.
- **Second-boot overwrite pass (`SD_OVERWRITE_TEST`)**: boot succeeded on
  retry (`bootlog.bin` still stale `B012S09I` from the timeout above). Three
  defects:
  1. Kit Save menu showed zero Kits despite `/Kit/.hcindex` populated and 39
     kits physically present — see §2 item 2.
  2. Scene overwrite reported `ScnS05` even though the physical overwrite
     (`019 Organity` → `019 Rollin`, complete tree) succeeded — root-caused
     in §1.1.
  3. Instrument overwrite unconfirmed from directory listing alone (same
     filename before/after is expected for a correct in-place overwrite) —
     see §2 item 6.
- Cross-cutting: boot Bank Load still runs with `TRACKING_ENABLED=0` and
  `published=0` for every Scene/instrument (same deferred AutoSave Bank
  defect, unchanged). `settings.cfg` is `active_bank=12` in both snapshots,
  matching the loaded `LoadTst` Bank.

## 5. Recommended Session 054 order of operations

1. Restore `/.hcnames` to the test card and re-confirm Load:[Scene]/
   Load:[Kit] clear `HNsL01`/`HNkL01` (§3, quick, unblocks everything else).
2. Apply the §1.1 fix (delete-resolver spurious error) — small, isolated,
   directly explains a real observed defect. Rebuild, retest Scene overwrite,
   then Kit/Bank overwrite as a regression check since they share the same
   worker.
3. Apply the §1.2 fix (HCNAMES source staging on Save) across all four Save
   paths — mechanical, symmetric with existing Load-path code. Rebuild,
   re-run the four-element Load/Save/AutoSave fixture, confirm the
   provenance table now shows the saved slot everywhere.
4. Apply the `KIT_PARSE_BOOTLOCK_RESOLVE.md` boot-quarantine removal (§3) —
   independently scoped, already has a full implementation outline and its
   own build/static/hardware verification steps in that document.
5. Re-run the acceptance-matrix items from `RECURSIVE_TREE_DELETE_REIMPLEMENT.md`
   §10 that are still unexecuted, now that §1.1/§1.2 are fixed.
6. Only after 1-5: pick up the not-yet-root-caused items in §2 (Kit directory
   materialization, Kit Save menu cache reload, Bank Save entry freeze, boot
   mid-drain freeze, boot Bank Load 20 s timeout, Instrument overwrite content
   verification) — these need their own source tracing before a fix is
   proposed; do not guess at them under time pressure.
