# DRAFT — Missing Library Folder Freeze (known bug, S093)

Status: **open, not fixed.** The user will decide later how the system should
handle a type with no library folder or no instrument files. This draft records
the report, what the code does, what has been ruled out, and what is still open.
No code was changed for this bug.

## 1. Report (user, S093, after the hcnames header fix)

- Firmware: the S093 Alkaline build (five instrument types) with the
  `filesystem_validateHcnamesHeader()` prefix fix. It boots and AutoSave
  restores.
- Steps: Load → Instrument (voice 2) → turn the type encoder past `Snare`.
  The next type is presumably `Alkaline`.
- Result: **hard lock** when the selection reaches that type.
- Card: restored from a commit (`SD_CARD_RESTORED/`). It has **no
  `/Instrument/Alkaline/` folder**, so it has no Alkaline `.hcindex` and no
  `.alk` files. The repository card image `SD_CARD/` does carry
  `Instrument/Alkaline/init.alk`.

Why Alkaline follows Snare on voice 2: with the default kit, V5 Cymbal and V6
HiHat already fill the two Advanced places, so
`menu_instrumentTypeSelectableForMask()` skips Cymbal and HiHat for voice 2.
After Drum, DrumMrp and Snare, the next selectable registry row is Alkaline.

**Not yet known** (ask on the next device run):
- What the screen shows when it locks.
- Whether audio keeps running. Audio continuing means a main-loop or
  storage wait; audio stopping means a HardFault or an ISR lock.
- Whether the S090 runtime watchdog (12 s) fires and shows a `W…` code.
- After a power cycle, whether `/Instrument/Alkaline/` and its `.hcindex`
  exist on the card. This shows how far runtime recovery got.
- The card's trace files (`asavetrc.bin`; the instrument-entry trace phases
  `AUTOSAVE_TRACE_INSTRUMENT_ENTRY_PHASE_*`) from the frozen run.

## 2. What the code does on that step (traced, S093 working tree)

Line numbers are from the tree at commit `ac3241e0` plus the uncommitted S093
P5 changes.

1. **Type step.** `menu_instrumentLoadStepType()` (`menu.c` l. 9695)
   selects the next selectable registry row. For a type that is not the base
   type it sets `menu_instrumentLoadSource = MENU_INSTRUMENT_SOURCE_CHANGE`
   and calls `filesystem_cancelBrowseRead()` (type changed), then
   `menu_instrumentLoadClampIndex()`, then
   `menu_requestInstrumentIndexLoad(type)`.
2. **Index request.** `menu_requestInstrumentIndexLoad()`:
   - if the typed cache is loaded, it calls `menu_instrumentIndexReady()`;
   - else, if the filesystem is busy, it defers;
   - else it sets `menu_storageBusy = 1` and calls
     `filesystem_requestLoadInstrumentIndex()` (`filesystem.c` l. 34706). That
     tags the cache `(FS_NAME_CACHE_INSTRUMENT, type)` and starts
     `FS_INTERNAL_OP_LOAD_INDEX`.

   **The UI is input-locked from here until the completion callback runs.**
3. **`filesystem_loadInstrumentIndex_tick()`** (l. 4991):
   - phases 0–5 enter `/Instrument/`;
   - phase 6 opens the registry directory (`Alkaline`);
   - phase 7: the directory is missing → phase 24 → return to root →
     `filesystem_beginInstrumentIndexRecovery()` (l. 4475);
   - (when the folder exists) phase 17 reads `.hcindex`. **A zero-row index
     goes to phase 19, "corrupt/empty index for recovery"**
     (`fs_list_cache_count == 0u ? 19u : 18u`), and then to recovery as well.
     So a type with an empty folder runs a rebuild on every visit.
4. **Recovery.** `filesystem_beginInstrumentIndexRecovery()`:
   - moves the menu's callback into `op_library_index_rebuild_callback`;
   - sets `op_library_index_rebuild_kind = FS_NAME_CACHE_INSTRUMENT` and the
     status to IDLE;
   - starts `filesystem_startInstrumentIndexRebuildScan()` (l. 4425). That
     resolves the registry index from `op_instrument_index_type`, sets
     `op_instrument_scan_one_type = 1`, and starts
     `FS_INTERNAL_OP_SCAN_INSTRUMENTS`.
5. **Scan.** `filesystem_scanInstruments_tick()` (l. 23517). At phase 7 the
   subdirectory is missing, and with `scan_one_type` set it goes to phase 12
   (DONE, empty cache). The callback
   `filesystem_libraryIndexRebuildScanComplete()` (l. 10781) then starts
   `FS_INTERNAL_OP_CREATE_BOOT_INDEX`, with
   `filesystem_libraryIndexRebuildWriteComplete()` as its completion.
6. **Writer.** `filesystem_createBootIndex_tick()` (l. 4640):
   - phases 0–4 re-enter `/Instrument/` and select the registry row;
   - phase 5 opens the type directory;
   - phase 6: missing, and the rebuild kind is INSTRUMENT, so **runtime
     mkdir** (`afatfs_mkdir_lfn(entry->storage_directory, …)`, one-shot
     `op_create_dir_retry`); the callback returns the new directory handle;
   - phase 7 `afatfs_chdir()` into it; 8 close; 9 `fopen(".hcindex", "w")`;
     10–12 write 0 rows, close, return; 14 finish.
7. **Completion.** `filesystem_completeLibraryIndexRebuild()` (l. 4353) sets
   the status and calls the menu's `menu_instrumentIndexLoadComplete()`. With
   DONE, and the cache still tagged `(INSTRUMENT, Alkaline)`, it calls
   `menu_instrumentIndexReady()`: it clears `menu_storageBusy` and repaints.
   The `chg` row copies blanks for row 000 of an empty type
   (`filesystem_copyInstrumentDisplayName()` handles NULL).

## 3. Ruled out (read and checked)

- **`afatfs_mkdir_lfn()` with no free file handle.** It calls the callback
  with NULL synchronously; the next tick sees `op_file == NULL` with the
  retry flag set and finishes with an error. No hang.
- **Writer clearing the cache tag.** `filesystem_createBootIndex_tick()`
  does not call `filesystem_clearNameCacheStorage()`, so after recovery
  `filesystem_instrumentNameCacheLoaded()` is true and the menu does not
  re-defer.
- **Registry index for the one-type scan.** It is resolved from the
  requested type (l. 4433–4448), not inherited from the boot loop.
- **Menu handling of an empty type.**
  - `menu_instrumentLoadClampIndex()` (l. 9486) sets the index to 0 for a
    zero count.
  - The Load row display tolerates a zero count (l. 13500–13512); the `chg`
    row blanks the name.
  - `menu_instrumentLoadIndex[INSTRUMENT_TYPE_UNKNOWN]` has 5 entries;
    Alkaline is index 4.
  - The label copy (`menu_instrumentLoadCopyTypeLabel()`) is bounded for an
    8-character label.
- **The deferred-request path** (`menu.c` l. 18080). With a `chg` source it
  calls `menu_requestInstrumentEntryNames()`, which clears
  `menu_deferSelectionRequest` and re-requests the typed index. It would
  loop only if the index completed without the cache counting as loaded,
  which step 7 above does not do.

## 4. Open suspects (not proven)

1. **Runtime mkdir → chdir has probably never run on hardware.** Every
   earlier type always had a folder on the card. In phase 7 the writer spins
   on `if (!afatfs_chdir(op_kit_root_dir)) return;` with **no fatal check
   and no timeout**. `afatfs_chdir()` (`asyncfatfs.c` l. 6299) returns false
   while `afatfs.currentDirectory` or the handle is busy
   (`afatfs_fileIsBusy()`). If the handle returned by the mkdir callback stays
   busy, for example on a pending directory-entry write or cluster
   allocation, the operation never advances: the UI stays input-locked and
   audio keeps running. The other `afatfs_chdir()` waits in this writer
   (phases 0 and 3) have the same shape.
2. **Phase 17's "empty index = corrupt" rule.** Any type whose folder exists
   with no files rebuilds its index on every visit. That is not a hang by
   itself, but a write on every browse step: slow and wearing. Combined with
   a slow write (suspect 1) it would look like a lock on every visit, not
   only the first.
3. **Something outside storage** (HardFault) remains possible until the
   device observations in §1 separate it. Audio stopping would point here.

## 5. What exists today, and a workaround

- The repository card image `SD_CARD/` ships `Instrument/Alkaline/init.alk`
  (S093 P4 §4.6), so a card copied from it has the folder and a file and does
  not take the missing-folder path at all.
- A card restored from an older commit lacks the folder. **Workaround until
  this is fixed:** copy `SD_CARD/Instrument/Alkaline/` onto the card.
- Boot treats a missing type folder as empty: the repair and the index stage
  both finish DONE (`filesystem.c` l. 10535–10538, 4752–4766). Only runtime
  index recovery and Instrument Save create the folder.

## 6. Design questions for the user (the later decision)

- Should boot create a missing registry type folder (and its empty
  `.hcindex`) once, so that runtime browsing never takes the mkdir path?
- Is an empty type (folder present, no files) a valid index, so that phase 17
  accepts zero rows instead of rebuilding every visit?
- What should Load show for a type with no files: an empty `chg` row as
  now, a message, or skipping the type in the list?
- Should every storage wait loop (`afatfs_chdir()`, close and open waits)
  carry the fatal check and a bounded retry, so that a stuck handle ends
  with an error code instead of a silent lock?

## 7. Files and anchors

| Site | Lines |
|---|---|
| `menu.c` `menu_instrumentLoadStepType()` | 9695 |
| `menu.c` `menu_requestInstrumentIndexLoad()` / `menu_instrumentIndexLoadComplete()` / `menu_instrumentIndexReady()` | search by name |
| `menu.c` deferred-request retry | 18080 |
| `filesystem.c` `filesystem_requestLoadInstrumentIndex()` | 34706 |
| `filesystem.c` `filesystem_loadInstrumentIndex_tick()` (phases 7→24, 17→19) | 4991 |
| `filesystem.c` `filesystem_beginInstrumentIndexRecovery()` | 4475 |
| `filesystem.c` `filesystem_startInstrumentIndexRebuildScan()` | 4425 |
| `filesystem.c` `filesystem_scanInstruments_tick()` | 23517 |
| `filesystem.c` `filesystem_libraryIndexRebuildScanComplete()` | 10781 |
| `filesystem.c` `filesystem_createBootIndex_tick()` (runtime mkdir, phase 6/7) | 4640 |
| `filesystem.c` `filesystem_completeLibraryIndexRebuild()` | 4353 |
| `asyncfatfs.c` `afatfs_mkdir_lfn()` / `afatfs_chdir()` | 6215 / 6299 |
