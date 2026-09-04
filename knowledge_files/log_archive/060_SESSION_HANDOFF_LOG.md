# Session 060 — Handoff Log

## 1. Scope and source documents

Session 060 had two independent threads of work:

1. **AutoSave writer-side preparation for a future boot reader** (Phases
   A/B/B2/C/D), scoped from `S060_AUTOSAVE_UPDATE_READER_PREP.md`. The reader
   itself is still deferred; this session only builds the writer-side
   machinery a reader will need: a faster continuation-cycle drain, a
   crash-safe `.hcnames`, a per-row "refreshed" witness distinguishing
   "autosave is authoritative" from "reload from library," and per-sub-object
   HCNAMES source fields inside the autosave record.
2. **Boot Instrument `.hcindex` fixup** (`S060_HCINDEX_FIXUP.md`), a
   separately triggered investigation after the user observed that only
   `Instrument/Drum/.hcindex` was ever generated at boot across five SD card
   captures.

Working documents for both threads (`S060_AUTOSAVE_UPDATE_READER_PREP.md`,
`S060PHASE_A_WRITER_SPEEDUP.md`, `S060PHASE_A_POST_FIXES.md`,
`S060PHASE_B_B2_HCNAMES.md`, `S060PHASE_B_POST_FIX.md`,
`S060PHASE_C_AUTOSAVE_SOURCE.md`, `S060PHASE_D_RE_DIRTY.md`,
`S060_HCINDEX_FIXUP.md`) are disposable session artifacts; the user intends
to delete them once this handoff and the specification updates below carry
forward everything durable. Do not treat their line-number references as
current — several predate later edits in the same session.

Branch: `dev-ph3-autosave-pre-overscope-apply`. Commits `3ff43e4`..`eca4271`
cover Phases A through D pre-implementation planning and application. The
Instrument `.hcindex` fix (both the repair-step guard and the asyncfatfs-level
AppleDouble filter) was applied after the last commit and remains uncommitted
at session close, alongside the specification-reference updates in this
closeout pass.

---

## 2. Phase A — AutoSave writer continuation-cycle speedup

### 2.1 Baseline cost

One drain transaction (`filesystem_autosaveParameterDrain_tick()`) spent
about 965 bounded ticks per cycle, dominated by ~544 ticks of dual-record
CRC32C validation (phases 0-5, reading both `.hcprms1`/`.hcprms2` in full)
plus four file open/close cycles — work that is entirely redundant on a
continuation cycle (250 ms after the writer's own prior commit) because the
writer already knows which record it just wrote and SD card removal while
the device is powered is not part of the product contract.

### 2.2 Winner cache design

Four external statics (`fs_autosave_winner_cached`,
`fs_autosave_cached_winner_index`, `fs_autosave_cached_winner_generation`,
`fs_autosave_cached_winner_probe`, 7 bytes total) persist across the
`fs_stage_workspace` union's per-cycle zeroing. `filesystem_autosaveWriterCompleted()`
populates the cache after a successful drain that used the normal
copy-forward path (`op_autosave_writer.have_winner` true) and left dirty
bits remaining; drain phase 0 consumes the cache (restoring winner
identity, jumping to phase 56, and clearing the flag) whenever it is set.

A no-valid-record recovery (phases 30-39, rebuilding both records from
HCNAMES baseline data with no selected winner) deliberately does **not**
populate the cache — this guard was added during implementation review, not
in the original recipe, because without it the completion callback would
cache a synthetic `{index=1, gen=1, probe=1}` tuple that was never actually
validated.

The cache is invalidated at nine lifecycle sites (recipe specified eight;
implementation review found one more — an OFF-during-active-transaction
path inside `filesystem_autosaveWriterCompleted()` itself): card
failure/remount, boot ensure, AutoSave OFF (policy setter, scheduler
fall-through, and the completion-callback path), writer error, and
Bank-session loss.

### 2.3 CRC/commit reopen merge

Phases 57-61 (close CRC handle, sync, reopen target, seek, wait) were
removed. After the CRC write (previously phase 21) the same open handle
seeks directly to the commit-byte offset (phase 62) instead of closing and
reopening — both offsets (5 and 12) live in the record's first 512-byte
sector, so AsyncFATFS was already caching them together, and the two
syncs collapsed into the one final sync in `filesystem_finish()`. The
commit-last contract is unaffected: neither byte is durable until that
single final sync.

### 2.4 Build and initial verification

First clean build: `text=385244 data=404 bss=96184` (image
`build/LXRV2_lxr02.img`, 385,664 bytes). Post-implementation review
confirmed every recipe change landed as specified plus the two documented
improvements (extra invalidation site, recovery guard), and traced the
completion callback's read of `op_autosave_writer.winner_index/generation`
through `filesystem_finish()` -> `filesystem_flushFinish_tick()` ->
`filesystem_complete()` to confirm the stage workspace is never reused
between phase 22 and the callback.

### 2.5 Boot failure: 4-column legacy `.hcnames`

First hardware boot on `SD_CARD_PHASE_A` failed to load the Bank. Root
cause: the card's root `/.hcnames` was written by an even older firmware
version using a 4-column row format (`Full\t050\t-\t0\n`), and
`filesystem_cacheResidentRecord()` rejected any row with more than one tab
as malformed — fatal because Bank Load reads root `/.hcnames` as a preload.
Not a Phase A regression; a pre-existing format-compatibility gap exposed
by testing against an older card. Fixed by isolating the source token at
the second tab boundary instead of rejecting the whole row; extra columns
after the source token are now silently discarded, so both the current
2-column and the older 4-column format parse. Build: `text=385276`
(+32 bytes).

### 2.6 Post-fix defects: chdir and SFN/LFN migration

Two further bugs surfaced from the same boot symptom
(`ERR HNkL01`/`HNsL01` on Kit/Scene load, no `.hcprms` created):

- **Bug 1 (confirmed, fixed):** Bank Load phase 83 opened `.hcnames` for
  write without first returning CWD to root, so the file was created inside
  `/Bank/NNN/` instead of `/`. Fixed with one `if (!afatfs_chdir(NULL))
  return;` before the open, matching the pattern every other HCNAMES opener
  already used.
- **Bug 2 (confirmed, resolved as a side effect of Bug 1's fix, not
  independently fixed):** the pre-existing root `.hcnames` was created by
  old firmware via the short-name API, producing an SFN-only entry with
  alias `        HCN` (display `.HCN`) and no LFN chain. New firmware opens
  exclusively via `afatfs_fopen_lfn()`, which generates alias `HCNAMES    `
  (display `HCNAMES`) — the two never match, so every open of the
  pre-existing file returned NULL. Once Bug 1's chdir fix let Bank Load
  create a *new* root `.hcnames` with a proper LFN chain during boot
  (before `ensureAutosaveFiles` or any Kit/Scene load menu could run), the
  mismatch stopped mattering. The old SFN-only entry remains an orphan in
  the root directory — harmless, unmatched by any open/probe path, and
  left for a future card reformat or cleanup pass.

`SD_CARD_PHASE_A_3` verification (post-fix, pre-test card with `.hcprms1/2`
and `.hcnames` deliberately deleted, then Kit/Bank/Scene Load and Bank
Save exercised): **zero `OPERATION_ERROR` records across 7,047 trace
records** (vs. 9 errors in 711 records pre-fix). `.hcprms1/2` present at
34,768 bytes with real payload; `.hcnames` 1,344 bytes, 129 rows, 2-column
format; 13 complete autosave drain cycles (`ADMITTED -> VALIDATED ->
CAPTURED -> PUBLISHED -> TERMINAL`).

### 2.7 Measured speedup

Comparing the Phase A 3 trace (13 cycles) against a pre-Phase-A baseline
trace (6 cycles): steady-state continuation cycles (cached winner) dropped
from ~3,075 ms to ~2,218 ms per drain — about 28% faster. The 24-byte
winner cache eliminates the ~960 ms validate phase entirely (0 ms on cached
cycles); the ~2.0-2.2 s file-write phase is unchanged and dominates the
remaining time. Cold-validation cycles (first/last of a session) remain
~3.5 s, comparable to baseline, since they cannot use the cache.

---

## 3. Phase B — `.hcnames` atomic safe-write

### 3.1 Problem

All five HCNAMES write paths (boot full-write, runtime targeted update,
bootstrap create, Bank Load, Bank Save) opened `.hcnames` with `"w"` mode —
truncate-and-write. A power loss or card error mid-write destroys the only
copy of the 129-row resident-identity register, and the `.hcindex` browser
rebuild cannot recover names without it.

### 3.2 Pattern

Every write path now follows the same sequence already proven for
`settings.cfg` (Session 057) and `.hcprms1/2`:

1. Open `.hcnamtmp` (`FS_RESIDENT_NAMES_TEMP_FILENAME`) for write.
2. Stream all 129 rows.
3. Close.
4. `afatfs_sync()` — make the temp durable.
5. `afatfs_removeObjects_lfn(".hcnames")` — retire the old live register
   (tolerates absent, e.g. first boot).
6. `afatfs_renameObject_lfn(".hcnamtmp", ".hcnames")` — promote.
7. `filesystem_finish()` — final sync through the shared flush gate.

The live `.hcnames` is untouched until step 5, so a power loss anywhere in
this sequence leaves either the intact old register or a recoverable
`.hcnamtmp`. `hcnames_mirror_valid` is demoted to `INVALID` before every
write-capable open, promoted to `PUBLISH_PENDING` only after rename
succeeds (not after close — the file is not authoritative until renamed),
and promoted to `VALID` only by the shared final sync; any error at any
safe-write phase demotes it back to `INVALID`.

Each write path also now captures `afatfs_fwrite()`'s return value and
checks `afatfs_isFull()` on a zero-byte write; a stuck-full card closes the
partial temp with `op_close_status = ERROR` and never touches the live
register.

### 3.3 Boot recovery prelude

New phases 15-19 in `filesystem_ensureAutosaveFiles_tick()`, gated by a
borrowed `op_file_version` one-bit flag (0 = not tried, 1 = done/skip, 2 =
reading temp): open `.hcnamtmp` read-only (absent is normal, falls through);
validate all 129 rows via the existing `filesystem_cacheResidentRecord()`
parser; close; if valid (129 rows, clean parse) remove the old `.hcnames`
and rename the temp into place; if invalid, remove the temp instead; either
way fall through to the normal `.hcnames` read. This mirrors the
`settings.tmp` recovery prelude exactly and runs before any code path opens
`.hcnames` for read.

### 3.4 Write paths converted

| Path | Function | Tail phases |
|---|---|---|
| Boot full-write | `writeResidentNames_tick` | 3-6 |
| Runtime update | `residentNames_tick` | 10-14 |
| Bootstrap create | `residentNames_tick` | 10-14 (shared) |
| Bank Load | `loadBankDirectory_tick` | 90-94 |
| Bank Save | `saveBankDirectory_tick` | 87-91 |
| Autosave drain (new, Phase B2) | `autosaveParameterDrain_tick` | 73-76 |

---

## 4. Phase B2 — HCNAMES refreshed flag

### 4.1 Purpose

The reader (Phase E, still deferred) must distinguish "autosave fully
represents this object's current state" from "this object was loaded/saved
since the last complete autosave capture, so the record may still be
mid-catch-up." The refreshed flag is that witness: set at load/save
completion, cleared by the autosave drain only after that object's byte
range in the canonical dirty mask is fully clean.

### 4.2 Bit allocation and token narrowing

Chosen bit: 13 of `fs_resident_source[]` (`FS_RESIDENT_SOURCE_REFRESHED_FLAG
= 0x2000`). Bit 15 was already the dirty-staging flag
(`FS_RESIDENT_SOURCE_DIRTY_FLAG = 0x8000`). Because the pre-existing special
source tokens (`INHERIT`, `UNKNOWN`, `INSTRUMENT_DIRECT`) occupied
`0x7ffd..0x7fff` — overlapping bit 14/15 — `FS_RESIDENT_SOURCE_VALUE_MASK`
was narrowed from `0x7fff` to `0x1fff` and the three tokens moved into the
resulting 13-bit space: `INHERIT = 0x1fff`, `UNKNOWN = 0x1ffe`,
`INSTRUMENT_DIRECT = 0x1ffd`. Numbered library slots (0..999) fit
comfortably below that. **Any code touching source tokens must use these
post-Phase-B2 13-bit values** — several pre-Session-060 planning documents
still reference the old 15-bit values and are wrong on this point.

### 4.3 Row format

`.hcnames` rows extend from `name\tsource\n` to `name\tsource[\tR]\n`. The
optional `R` suffix is the serialized refreshed witness; its absence is
backward compatible with every pre-Session-060 file. The formatter
(`filesystem_formatResidentNameLine()`) captures the raw bit before masking
and appends `\tR` at both its numeric and symbolic exit points. The parser
(`filesystem_cacheResidentRecord()`) already truncated at a second tab for
forward-compat reasons; it now also inspects the byte after that tab for
`R`.

### 4.4 Set/clear lifecycle

Two helpers set the flag: `filesystem_setResidentRefreshed(row)` (single
row) and `filesystem_setResidentSceneRefreshed(scene_index)` (Scene + Kit +
6 Instruments). Every Load/Save completion family calls one of these
alongside its existing HCNAMES source staging: Kit Load, Scene Load, Bank
Load (Bank row only — Bank has no autosave source field), Instrument Load,
Instrument Save, Kit Save, Scene Save, Bank Save.

Two functions preserve the flag across unrelated writes:
`filesystem_setResidentSource()` captures and re-ORs it around a source
replacement; `filesystem_clearResidentSourceDirtyFlags()` was changed from
`&= VALUE_MASK` (which would have also stripped bit 13) to `&=
~DIRTY_FLAG` so clearing the dirty-staging bit no longer clobbers a live
refresh witness.

Clearing runs only after a successful post-drain HCNAMES publication:
`filesystem_autosaveDrainHasRefreshWork()` scans all 129 rows for any that
are both refreshed and (`autosave_objectFullyCaptured(row)`, new in
`Autosave.c`) fully clean in the canonical mask; if any exist,
`filesystem_autosaveDrainAfterCommit()` invalidates the mirror and enters
phases 70-76 (the same five-step safe-write tail as Phase B, with the
formatter suppressing `R` for rows about to become clean); only after that
write's own final sync succeeds does the terminal callback call
`filesystem_clearResidentRefreshedCaptured()` to actually clear bit 13 on
those rows. An error leaves the witness set for retry on the next drain.
Two call sites feed this convergence step: after mask merge with no dirty
bits (a load set refreshed but nothing needs capturing) and after the
normal post-commit copy-forward.

### 4.5 Post-implementation bug: stale `op_close_status`

Hardware test 2 (`SD_CARD_B_PHASE_2`) found Bank Load failing with `FsErr`
or `BKKit14`, and every subsequent Kit Load/Scene selection failing with
`HNkL01`/`HNsL01` (file-not-found at open). Root cause: `op_close_status`
is a shared operation-scoped static. Bank Load's child Scene-loading
sub-phases (recovering from per-child failures such as "scene not found")
set it to `FS_STATUS_ERROR` — correctly, for their own recoverable purpose
— but nothing reinitialized it before the HCNAMES safe-write tail ran much
later in the same operation (phase 90's close-wait). The stale `ERROR`
falsely aborted the safe-write before rename, leaving `.hcnamtmp` on card
but `.hcnames` intact; the cascading `FsErr` on Bank Load itself then broke
the system state for later operations. Fixed with three explicit
`op_close_status = FS_STATUS_DONE;` insertions, right after each write
path's temp-file open succeeds and before its streaming phase: Bank Load
phase 84, Bank Save phase 84, and (defensively) boot full-write phase 0.
Build delta: +16 bytes.

Hardware test 2 (post-fix) then passed for Bank Save, Kit Load, Scene
Load, and Scene Save, but Bank Load still failed with `FsErr`.

### 4.6 Second post-implementation bug: settings-writer facade race

Trace analysis (`asavetrc.bin`, no `E`/`OPERATION_ERROR` records at all,
`op_bank_scene_failed_mask == 0`) proved the second `FsErr` was not a Bank
Load child failure. Root cause: `filesystem_markSettingsDirty()`
(called after Bank Load commit, 1-second debounce) could win the facade
race against Menu's post-load `.hcindex` restore. Sequence: Bank Load
completes DONE, `filesystem_ack()` returns the facade to IDLE; on a later
tick the settings writer's debounce has already expired (HCNAMES
safe-write took long enough on a slow card) and it starts `SAVE_GLOBALS`,
taking the facade to `BUSY`; DSP apply then finishes and Menu's
`filesystem_requestReloadLibraryIndex()` is rejected as `BUSY`, and the
generic error overlay reads an already-cleared `fs_error_code` and displays
`FsErr`.

This is the same defect class the autosave trace-flush scheduler was
already guarded against (comment at `filesystem.c:23035-23038` literally
describes it). The settings writer scheduler was simply missing the same
guard. Fixed with one line —
`if (menu_isLoadSaveCommandActive()) return;` — added to
`filesystem_settingsWriterSchedule_tick()` before its dirty/debounce
checks, matching the trace-flush scheduler's existing pattern.

Hardware test 3 (`SD_CARD_B_PHASE_3`) then passed cleanly: multiple Bank
Load, Bank Save, Scene Load, and Kit Load operations, no `FsErr`, `.hcnames`
129 rows with correct `\tR` markers on loaded rows, `.hcprms1/2` both
correct 34,768-byte size, zero `OPERATION_ERROR` trace records, no stale
`.hcnamtmp`. Two minor cosmetic observations (name-retention glitch and a
scrolling artifact on the Load/Save page after Bank operations) were noted
and carried to `SCOPING_TARGETS.md`, not fixed this session.

Build after both fixes: `text=388908 data=400 bss=96180`.

---

## 5. Phase C — Autosave record source fields

### 5.1 Goal

Every autosave sub-object (Scene, Kit, Instrument) gains a 2-byte source
field recording its originating library slot, so a future reader can cross-
check the record's contents against `.hcnames`.

### 5.2 Zero-growth geometry

Rather than growing the record, the source field is absorbed from each
sub-object's existing reserved parameter/padding space:

| Sub-object | Old layout | New layout | Size |
|---|---|---|---|
| Scene header | name(8) + params(120) = 128 | name(8) + source(2) + params(118) = 128 | unchanged |
| Kit header | name(8) + params(120) = 128 | name(8) + source(2) + params(118) = 128 | unchanged |
| Instrument | type(3)+name(8)+normal(72)+morph(72)+pad(37) = 192 | type(3)+name(8)+source(2)+normal(72)+morph(72)+pad(35) = 192 | unchanged |

Every section boundary, the mask (3,856 B), the payload (30,848 B), and the
record (34,768 B) are completely unchanged. Four new `_Static_assert`s
(Scene/Kit source-to-parameter contiguity, Instrument source-to-normal
contiguity, Kit parameter-to-instruments contiguity) protect the geometry
at compile time. Source offsets: Scene +8, Kit +8 (relative), Instrument
+11 (the 3-byte type prefix pushes it past +8).

### 5.3 Wire compatibility

Old-format records (pre-Phase-C) pass CRC and size validation unchanged
under the new firmware — only the *interpretation* of bytes within each
sub-object header differs. The existing `autosave_markResidentBankDirty()`
path (fired on any Bank-identity mismatch or first boot) marks every
present byte dirty, including the new source-field positions, so the first
complete drain after upgrading a card silently rewrites every sub-object
into the new layout. No format-version bump, no explicit migration code.

### 5.4 Getter/marker/dispatch

`autosave_getSourceByte(hcnames_row, byte_index, *value)` (new,
`Autosave.c`) delegates to the existing public `filesystem_residentSource()`
accessor, which already masks off bits 13-15 (refreshed/dirty/reserved).
`autosave_markSourceDirty(hcnames_row)` (new) is the row-addressed
complement, converting an HCNAMES row into the matching 2-byte payload
offset and marking both bytes through the same
`autosave_markPayloadOffsetDirty()` funnel every other marker uses. Three
new dispatch branches in `autosave_getLivePayloadByte()` route Scene+8..9,
Kit+8..9, and Instrument+11..12 to the getter. Three compound markers
(`markWholeInstrumentDirty`, `markKitDirty`, `markSceneWithoutPatternDirty`)
were extended to include their object's source bytes. Three Save
completion families in `filesystem.c` (Instrument, Kit, Scene) now pair
every `filesystem_setResidentSource()` call with the matching
`autosave_markSourceDirty()` call; Bank Save needs none (the Bank section
has no source field — `markSourceDirty()` early-exits for row 0). Load
completions need no new pairing because their existing compound markers
already cover source bytes after this change.

Name bytes (the 8-byte name field in each header) are **not** re-dirtied or
given a getter — see §7.4 below.

### 5.5 SRAM and build

Zero persistent SRAM cost (mask/payload/record sizes unchanged; getter/
marker use only automatic stack, ~20 bytes across call frames). Clean
`make clean && make`: `text=389036 data=400 bss=96192`. `make img`:
389,436-byte payload, 389,452-byte packaged image.

### 5.6 Hardware verification

Booted with Phase C firmware, performed a Bank Load (all 16 scenes) and an
individual Scene Load (slot 6, `FilMod`), let multiple drain cycles
complete, pulled the card. Both `.hcprms` records passed CRC at the
unchanged 34,768-byte size (generation 16 the current winner). Source
fields read correctly at their new offsets: Bank-loaded scenes carry
`0x0000` (their bank slot) on Scene/Kit/all-6-Instrument rows; the
individually-loaded Scene 6 carries `0x0003` (its library slot) on its own
Scene row while its Kit and Instrument rows remain `0x1fff` (INHERIT) since
they were loaded transitively as part of the Scene, not independently.
Instrument type bytes at offset 0..2 (`drm`/`snr`/`cym`/`hat`) read
correctly, confirming the 3-byte type prefix and the +11 source offset
don't overlap.

---

## 6. Phase D — Re-dirty mechanism audit

### 6.1 What the parent plan specified

The original plan (`S060_AUTOSAVE_UPDATE_READER_PREP.md` §10) called for a
deferred `uint16_t` per-Scene re-dirty request mask in the writer
workspace: load/save completions would set request bits, and drain entry
would OR them into the canonical mask before scanning.

### 6.2 What was actually needed: nothing

A full code-site audit (every load/save completion, every compound marker,
`autosave_markPayloadOffsetDirty()`'s atomicity, the Phase B2 refreshed-flag
lifecycle) found that Phases B2 and C's *immediate* marking — compound
markers firing synchronously from load completion callbacks, source markers
firing synchronously from save completion callbacks — already satisfies
every Phase D requirement, and does so with lower latency than the deferred
design: `autosave_markPayloadOffsetDirty()` uses IRQ-safe atomic bit-OR
(`autosave_maskByteOr()`), so a mark landing mid-drain-scan is captured
either in the current cycle (if the scanner hasn't reached that byte yet)
or the next one (if it has) — strictly better than a deferred mask that
always adds one full cycle of latency regardless. `objectFullyCaptured()`
keeps the refreshed flag pinned until every relevant byte is actually
clean either way, so there is no correctness gap from skipping the
deferred mask. This session therefore made **zero source changes** for
Phase D — only verification and documentation.

### 6.3 Name-byte re-dirtying: deliberately not implemented

The parent plan also specified re-dirtying "name and source" bits (10 bytes
per object) on Save. Phase C implements source marking but the audit
confirmed name-byte marking would be a no-op: `autosave_getLivePayloadByte()`
has no name-byte dispatch (falls through to `return 0`), the compound
markers deliberately start past the name field by design comment, and the
future reader is specified to use `.hcnames` for identity — not autosave
record names — so a stale name byte in the autosave record has no
consequence. Adding a name getter would also require a new public accessor
into the private `hcnames_name_mirror[]`, reversing the current one-way
`filesystem.c -> Autosave.h` dependency for no benefit. This decision is
recorded in `SCOPING_TARGETS.md` specifically so a future session doesn't
"fix" the staleness by adding that getter.

### 6.4 Remaining work

Four end-to-end hardware lifecycle tests (Load->drain->refreshed-cleared->
HCNAMES-rewritten; Save->drain->refreshed-cleared; load during an active
drain scan; multiple overlapping loads) are specified in
`S060PHASE_D_RE_DIRTY.md` §5 but were not run as dedicated fixtures this
session — only incidentally exercised through the Phase B/C hardware
passes (whose card copies show `\tR`-marked rows and post-drain
`.hcnamtmp` activity consistent with the convergence machinery running, but
are not a targeted Phase D fixture).

`make clean && make` re-verification (re-run against the committed source
at closeout, no code changed): `text=389036 data=400 bss=96192`, identical
to the Phase C build.

---

## 7. Boot Instrument `.hcindex` fix (separate investigation)

### 7.1 Symptom

Across five SD card captures, only `Instrument/Drum/.hcindex` was ever
present at boot; `Snare/`, `Cymbal/`, and `HiHat/` never got their index
regenerated even though all four directories existed with real content.
Kit/Scene/Bank root indexes were unaffected.

### 7.2 Architecture and why the failure was silent

`filesystem_createBootIndexBlocking()` iterates all four registry types in
order (Drum, Snare, Cymbal, HiHat), doing one scan + one index-write per
type, and bails out of the entire loop on the first type that fails either
step. `main.c:727` calls it as `(void)filesystem_createBootIndexBlocking();`
— the failure signal was completely discarded, so a second-type failure
silently stopped every remaining type with zero observable evidence.

### 7.3 Candidate elimination

Six candidate mechanisms were assessed at the source level before the
actual cause was found, per the user's explicit direction to identify a
concrete failing system rather than add speculative instrumentation:

1. **Shared-variable carryover between iterations** — mostly eliminated;
   `filesystem_start()` resets ~20 operation-local fields every call, and
   both scan/index-write completion paths null their remaining shared
   pointer.
2. **CWD state mismatch** — mostly eliminated by direct asyncfatfs analysis;
   `afatfs_chdir(NULL)` always builds a fresh root handle from
   `afatfs.rootDirectoryCluster`, and the Drum write cycle never touches
   root or `/Instrument/` directory sectors.
3. **Case-sensitivity divergence** (scan uses `CASE_SENSITIVE` for
   `/Instrument/`, index-write uses `CASE_INSENSITIVE`) — real
   inconsistency, worth normalizing, but cannot explain why iteration 1
   succeeds and iteration 2 fails; ruled a red herring in isolation.
4. **`filesystem_start()` precondition failure** — eliminated; its only
   guard is `status == BUSY`, and the blocking wrapper always acks first.
5. **Boot logging sticky timeout** — eliminated per explicit user
   correction ("boot takes nowhere near 20 seconds").
6. **macOS AppleDouble files misclassified as instruments** — **confirmed
   root cause**, after the user's own on-card evidence (a captured
   `inst.drm` file, 4096 bytes of macOS resource-fork metadata, regenerated
   every boot) pointed the investigation at it directly.

### 7.4 Root cause

macOS creates hidden `._<name>.<ext>` AppleDouble resource-fork files on
FAT volumes whenever a user copies real files onto the card from a Mac —
one shadow file per real file, sharing its extension. These pass the boot
repair step's type classification (`instrumentManager_filenameMatchesType()`
is a bare suffix check) but their display name starts with `.`, so
`filesystem_copyInstrumentStemDisplay()` (which stops at the first `.`)
produces an empty stem. The repair step
(`filesystem_repairBuildCandidate()`) had no usable-stem guard — unlike the
scan step, which already had one — so it fell back to the default stem
`inst`, tried to rename the first `._` file to `inst.<ext>`, produced a
bogus 4096-byte "instrument" of macOS metadata, then collided on the
second `._` file trying to rename to the same target, aborting the entire
instrument repair pass before any type's scan or index-write could run.
Confirmed on-card: 97 real `.drm` files, 97 `._*.drm` shadow files, and one
`inst.drm` (4096 bytes, `com.apple.quarantine` + resource-fork content) in
`Instrument/Drum/`.

### 7.5 Fix — two layers

Initial fix (repair-step guard only) was reported by the user as
insufficient ("ok, that didn't work"), because the real damage happens the
moment any `._` file reaches the classification/stem logic at all — a
narrower guard closer to the symptom doesn't stop every future consumer
from hitting the same class of bug. The user then asked explicitly for the
filter to move to the lowest feasible layer, system-wide:

1. **Primary fix — `afatfs_findNextObject()` (`asyncfatfs.c:2972-3004`).**
   After all three display-name resolution paths (verified LFN,
   malformed-LFN fallback, bare SFN) produce a final name, and before the
   function's terminal return, any object whose resolved display name
   begins with `._` is skipped — finder state reset, loop continues to the
   next raw entry — using the identical pattern already used for
   structural dot entries, deleted entries, LFN fragments, and volume
   labels. This makes AppleDouble files invisible to every directory
   consumer in the system automatically: repair, scan, index, save, load,
   and any future enumerator. No product-owned file collides with the `._`
   prefix (`.hcindex`/`.hcnames`/`.hcprms1`/`.hcprms2`/`.hcnamtmp` all use
   `.hc`; `.hctmp.<ext>` uses `.ht`).
2. **Defense-in-depth — `filesystem_repairBuildCandidate()`
   (`filesystem.c:9660`).** An unusable-stem guard (`filesystem_instrumentStemIsUsable()`)
   was added to the repair step, matching what the scan step already had.
   Now redundant with the asyncfatfs-level filter for the AppleDouble case
   specifically, but retained because it protects against any other future
   source of an unusable stem, not only this one.

### 7.6 Deliberately not applied this session

Two independent hygiene items from the same investigation were identified
but left unapplied, since neither was the actual root cause and the D1
filter already fixes the real failure:

- Normalizing the scan tick's `/Instrument/` open from
  `AFATFS_MATCH_CASE_SENSITIVE` to `AFATFS_MATCH_CASE_INSENSITIVE`
  (`filesystem.c:19636`) to match every other caller.
- Checking `filesystem_createBootIndexBlocking()`'s return value at
  `main.c:727` so a future boot-index failure is observable instead of
  silent.

Both are recorded as open items in `SCOPING_TARGETS.md` and
`S060_HCINDEX_FIXUP.md`'s summary table.

### 7.7 Test procedure (not yet run on hardware)

`S060_HCINDEX_FIXUP.md` specifies: boot with the fix applied, pull the
card, verify all four typed `.hcindex` files exist and match a physical
directory scan; save one instrument of each type and verify only that
type's index updates; delete one type's index and verify the existing Load
Menu recovery path (unchanged, `filesystem_beginInstrumentIndexRecovery()`)
regenerates it. This has not been exercised on hardware as of session
close — the fix is source-verified only.

---

## 8. Files changed

### Production source (uncommitted at session close)

| File | Change |
|---|---|
| `Core/Hardware/SD/filesystem.c` | Phases A/B/B2/C (committed across `3ff43e4`..`eca4271`) plus the Instrument-repair unusable-stem guard (`filesystem_repairBuildCandidate()`, ~line 9660) and settle barrier (`filesystem_createBootIndexBlocking()`), applied and uncommitted at close |
| `Core/Hardware/SD/asyncfatfs/asyncfatfs.c` | `afatfs_findNextObject()` AppleDouble `._` filter (~line 2972-3004), uncommitted at close |
| `Core/Bank/Scene/Autosave.c/.h` | Phase C source-field geometry, getter, marker, dispatch (committed) |
| `Core/Hardware/SD/filesystem.h` | Phase B2 token narrowing and new flag/mask defines (committed) |

### Documentation (this closeout pass)

| File | Change |
|---|---|
| `knowledge_files/specification_reference/AUTOSAVE.md` | Winner cache, HCNAMES safe-write/refreshed-flag section, source-field summary |
| `knowledge_files/specification_reference/FILESYSTEM_SPEC.md` | HCNAMES safe-write/refreshed-row-format, boot Instrument `.hcindex` fix, AutoSave boundary status |
| `knowledge_files/specification_reference/ASYNCFATFS_REFERENCE.md` | AppleDouble filtering section under Object Iteration |
| `SCOPING_TARGETS.md` | Session 060 resolved-defects/remaining-refactor-targets subsection |
| `knowledge_files/log_archive/000_SESSION_INDEX.md` | Quick-reference row and verbose summary for Session 060 |
| `knowledge_files/log_archive/060_SESSION_HANDOFF_LOG.md` | This file |

### Session working documents (disposable, user intends to delete)

`S060_AUTOSAVE_UPDATE_READER_PREP.md`, `S060PHASE_A_WRITER_SPEEDUP.md`,
`S060PHASE_A_POST_FIXES.md`, `S060PHASE_B_B2_HCNAMES.md`,
`S060PHASE_B_POST_FIX.md`, `S060PHASE_C_AUTOSAVE_SOURCE.md`,
`S060PHASE_D_RE_DIRTY.md`, `S060_HCINDEX_FIXUP.md`. Every durable decision,
rationale, and outcome from these documents has been folded into the
specification-reference updates and this log.

---

## 9. Explicit non-changes and deferred ideas

- **The AutoSave boot reader itself (Phase E) is not implemented.** Every
  Session 060 phase is writer-side preparation. Boot continues to use the
  existing Case 3 path (load from `settings.cfg`/library) regardless of
  AutoSave state until Phase E lands in a future session.
- **No AsyncFATFS driver changes for the AutoSave phases.** Every primitive
  Phases A/B/B2/C use (`fopen_lfn`, `removeObjects_lfn`, `renameObject_lfn`,
  `sync`) already existed. The one asyncfatfs change this session
  (`afatfs_findNextObject()`'s AppleDouble filter) belongs to the unrelated
  `.hcindex` investigation, not the AutoSave work.
- **No record-format version bump.** Phase C's source fields are absorbed
  into existing reserved space; the record stays version 1 at 34,768 bytes.
- **Deferred re-dirty mask never implemented** — see §6.2; the immediate-
  marking design already in place is strictly better.
- **Name-byte re-dirtying never implemented** — deliberate, see §6.3.
- **Case-sensitivity normalization and boot-index return-value check** —
  identified during the `.hcindex` investigation, deliberately deferred,
  see §7.6.
- **Bank Save present-mask subset-assignment bug (Session 052 P1)** — a
  pre-existing, unrelated deferred item; not touched this session.
- **Phase D hardware verification tests** — specified, not run; see §6.4.
- **Boot Instrument `.hcindex` fix hardware verification** — not run; see
  §7.7.

---

## 10. Closeout state

Build state at the point Phase D was verified (before the `.hcindex` fix's
source edits, which do not change any geometry/SRAM contract):
`text=389036 data=400 bss=96192`, identical across the Phase C and Phase D
verification passes since Phase D added no code.

Two hardware-untested items remain outstanding into the next session: the
four Phase D lifecycle tests (§6.4) and the boot Instrument `.hcindex` fix
test procedure (§7.7). Both are specified in enough detail (exact steps,
expected file states, expected trace signatures) to run without
re-deriving anything from this log.

Two minor UI cosmetic issues (Load/Save page name-retention glitch and
scrolling artifact after Bank operations, observed during Hardware Test 3)
are carried forward in `SCOPING_TARGETS.md`, not fixed this session.
