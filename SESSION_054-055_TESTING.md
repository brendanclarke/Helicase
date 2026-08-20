### 0. Prerequisite

Restore a valid `/.hcnames` to the test card first (carried over from
`RECURSIVE_TREE_DELETE_REIMPLEMENT.md`'s still-open follow-up). Confirm
Load:[Scene] and Load:[Kit] no longer report `HNsL01`/`HNkL01`. Every fixture
below assumes a working register; skipping this makes several of them
false-fail for an unrelated reason.

### 1. Bug #1 retest — Scene overwrite no longer reports a spurious error

---
Should be done now
---

1. Load a Scene into the active resident slot, edit a parameter, Save to an
   **occupied** root Scene slot (the exact `SD_OVERWRITE_TEST` sequence).
   Confirm the save completes with no `ScnS05` (or `KitS`/`BnkS` equivalent)
   error, and that the target directory was actually replaced on the card
   (old tree gone, new tree present with `sceneset.scg`, embedded `Kit
   <name>/`, `pattern.pat`, `effects.fx`).
   ---
   should be ok now
   ---

2. Decode `asavetrc.bin`. Find the Scene Save's `'X'` record(s), if any
   (`flags & 0x07 == 0` = delete-slot site). Confirm: if present, `value`
   bits 0-7 (the `fs_delete_slot_phase_t` value at the moment of the stall)
   and bit 3 of `flags` (`AUTOSAVE_TRACE_PHASE_STALL_FLAG_IN_NATIVE_DELETE`)
   tell you whether the stall was during the parent scan or inside native
   delete — and that the save still completed successfully regardless.
   ---
   should be ok pending trace check
   ---
3. **Force the stall path deliberately**: build a large nested Scene (several
   instruments, a big embedded Kit) specifically to try to cross 50,000 polls
   during its own delete. Confirm the overwrite still succeeds and a `'X'`
   record appears. This is the direct proof the fix works, not just that the
   common case got lucky and never hit the stall counter at all.
---
this is dumb, Scene size is a known limit, the stress test is Bank
---

4. Regression-check Kit and Bank overwrite the same way (both share
   `filesystem_deleteSlotDirectory_tick()`).
   ---
   bank test pending for Bug 5
   ---

### 2. Bug #2 retest — HCNAMES provenance, all four Save paths

Repeat the `LOAD_SAVE_AUTO_ELEMENT_TEST_REPORT.md` four-element sequence
(delete `/.hcnames`+`/.hcprms*`+`bootlog.bin`+`asavetrc.bin`, boot, Play, load
an element, edit, save to a **new** slot, power off, capture card) once per
element type, and for each:

- Read the saved row directly from `/.hcnames` and confirm its source token
  is the **saved** slot (Kit/Scene/Bank: a 3-digit numbered slot; Instrument:
  `@`), not the loaded one.
- Decode `asavetrc.bin` and find that Save's `'O'` records. Confirm, in
  order: one `REQUEST` (checkpoint bits `[4:2]==0`) with the correct slot in
  `value` bits 0-9; for Kit/Scene one `DELETE_RESULT` (`==1`) and one
  `CREATE_RESULT` (`==2`) both with `FLAG_FAILED` (`flags` bit 7) clear; one
  `SOURCE_STAGED` (`==3`); one `FINISH` (`==4`). Element type is `flags` bits
  0-1 (`0`=Kit `1`=Scene `2`=Bank `3`=Instrument).
- For Instrument specifically, also confirm the row's **name** changed to the
  saved stem, not just its source — this is the deeper defect §2.1/§2.5
  found (the old code published neither).

### 3. Kit-materialization evidence

Save a Kit to a **new** (previously empty) slot. Confirm a `/Kit/NNN Name/`
directory actually appears. Decode `asavetrc.bin` and read the Kit `'O'`
`CREATE_RESULT` record's `FLAG_FAILED` bit:

- Clear + directory present → confirms the fix; no further action.
- Clear + directory absent → the trace itself is misleading (an
  `afatfs_mkdir_lfn()` success that doesn't durably persist); this is a new,
  narrower bug in `asyncfatfs.c`'s create/flush path, not the Save
  state-machine logic this plan touched.
- Set → `afatfs_mkdir_lfn()` itself refused; read the `filesystem_deleteSlotDirectory_tick()`
  and preceding-phase evidence to see what state the parent scan left
  things in.

### 4. Kit Save menu empty-cache evidence

Reproduce the exact `SD_OVERWRITE_TEST` sequence: Scene overwrite, then
immediately enter Kit Save in the same power-on session without rebooting.
Confirm whether the Kit list is empty or populated. Decode `asavetrc.bin` and
find the Kit `'O'` `REQUEST` records with a nonzero value in bits 16-17 (the
Menu branch tag, distinct from the normal slot/CRC use of that field at this
one call site — see §3.5's note on this deliberate field reuse):

- `1` → branch A: resident-name scratch was invalid/scene-mismatched, went
  through `menu_requestResidentNameScratch()` (not traced further by this
  plan — a genuinely different code path).
- `2` → branch B: `filesystem_libraryNameCacheLoaded(FS_LIBRARY_INDEX_KIT)`
  returned true (cache already tagged Kit), so no reload was requested. If
  the Kit list is empty *and* this branch fired, the bug is that this
  function is returning a stale "true" despite `fs_list_cache_kind` actually
  needing a refresh — inspect `fs_list_cache_kind`'s last writer before this
  point.
- `3` (low bit set) → branch C: cache was correctly recognized as stale and
  `filesystem_requestLoadKitIndex()` was called and accepted. If the Kit list
  is still empty after this, the bug is downstream inside the reload itself
  (`filesystem_requestReloadLibraryIndex()`/`filesystem_requestScanKits()`),
  not in the entry-decision logic this plan instrumented.
- `3` (low bit clear) → branch C but the reload request was **rejected**
  (`filesystem_requestLoadKitIndex()` returned false) — read
  `menu_deferSelectionRequest`'s handling from there.

### 5. Bank Save entry freeze evidence

Attempt Bank Save the same way it froze before. Decode `asavetrc.bin`
(pulling the card mid-hang if the freeze recurs and a clean shutdown isn't
possible) and check, in order:

- Is there a Bank `'O'` `REQUEST` record at all? If not, the hang is
  upstream of `preset_saveBank()` even being called — in Menu's page-entry/
  gating state (`menu_storageBusy`/`menu_loadSaveCommandActive`), not
  anywhere this plan instrumented. That is the next session's lead.
- If `REQUEST` is present, is there a `'X'` `PHASE_STALL` record with
  `flags == AUTOSAVE_TRACE_PHASE_STALL_SITE_BANK_ENTRY (1)`? Its `value` bits
  0-7 are the exact `op_phase` Bank Save was stuck at (0 = initial HCNAMES
  open; 80/81 = HCNAMES read; 82 = close; 83-86 = final register rewrite; a
  value ≥ 8 with no payload ever appearing means it stalled after delegating
  to the Scene payload writer, which is not itself instrumented by this
  session's changes).
- If `REQUEST` is present but no `'X'` record ever appears even after a long
  wait, either the hang resolved faster than 20,000 polls (and something
  else entirely is the real symptom on this run), or it's a genuine hard
  lockup that no cooperative check could ever catch — record whichever, both
  are useful negative results.

### 6. Boot Bank Load timing evidence

Boot with a full multi-Scene Bank selected (enough Scenes/instruments to
plausibly approach the 20 s budget). Whether or not it actually times out,
decode `asavetrc.bin` and extract every `'N'`
(`AUTOSAVE_TRACE_STAGE_INSTRUMENT_ENTRY`) record with `flags ==
AUTOSAVE_TRACE_INSTRUMENT_ENTRY_PHASE_REQUEST (1)` emitted during this boot.
For each, `value` bits 0-3 are the Bank-local Scene child index and bits 4-6
the instrument voice slot; `tick` is the 16-bit millisecond-resolution
timestamp (wraps at 65,536 ms — watch for a wrap if the boot run is long).
Compute the delta between consecutive records to get per-instrument load
duration, and look for whether time is spread evenly (an inherent
volume-of-I/O finding, not a bug — the fix would be reducing what boot loads,
not chasing a stall) or concentrated in one outlier step (a real stall worth
tracing further next session).

### 7. Instrument overwrite content verification

Save an Instrument to a **new** slot; decode `asavetrc.bin` and record that
Save's `'O'` `CREATE_RESULT` `value` bits 16-31 (the CRC16 fingerprint — note
finding 1 above: this is the *unfinished* running CRC32C, not the
`~crc32c`-complemented form used elsewhere in this file, so only compare it
against another fingerprint produced the same way, never against a value
computed via `autosave_recordCrcFinish()`). Then overwrite that same slot
with different content and repeat. Confirm the fingerprint changed. There is
currently no equivalent fingerprint emission on the Load path (only Save
emits `'O'` records), so this pass can only prove "Save A produced different
bytes than Save B," not "the Load path reads back what Save wrote" — for the
latter, cross-check the two saved files' bytes by hand for this first pass,
and consider adding a matching Load-side fingerprint in a later session if
this remains a recurring question.

### 8. Regression pass

Re-run the AutoSave health checks from `LOAD_SAVE_AUTO_ELEMENT_TEST_REPORT.md`
(`tools/verify_bank_autosave.py`, the A/V/M/C/P/T sequence) to confirm none of
this session's changes disturbed the already-working AutoSave writer —
particularly the drain's new stall-and-fail path (§3.3/finding 3): confirm a
**normal, healthy** Save/AutoSave cycle never emits a drain `'X'` record at
all. A `'X'` record with `flags ==
AUTOSAVE_TRACE_PHASE_STALL_SITE_DRAIN (2)` on an otherwise-ordinary save is
itself a finding, not expected noise.
