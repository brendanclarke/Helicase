# AutoSave Specification

## Authority and scope

This is the authoritative reference for the implemented Helicase AutoSave
format, ownership, boot restore, mutation tracking, and background writer
through Session 061. Historical plans and session logs explain how the
implementation was reached, but they do not override this document.

Related authority is deliberately separate:

- `FILESYSTEM_SPEC.md` owns the non-AutoSave product filesystem layout and
  `settings.cfg` schema. Its AutoSave boundary deliberately points here; the
  rejected pre-Session-045 per-file dot-backer design is not a current spec;
- `DEV_MODES.md` owns development-mode selection and diagnostic file output;
- `ASYNCFATFS_REFERENCE.md` owns low-level AsyncFATFS contracts;
- `SRAM_MANIFEST.md` owns the binding memory-reservation policy and the current
  Session 061 linked allocation/capture snapshot;
- `AUTOSAVE_TEST_CASES_LOAD_SAVE_REVISIONS.md` owns the deferred interaction
  and regression matrix for AutoSave, HCNAMES, `settings.cfg`, and Load/Save.

AutoSave currently persists the active resident Bank's implemented scalar
state into two hidden root records. It does not modify root `Bank/`, `Scene/`,
`Kit/`, or `Instrument/` library objects and does not replace explicit Load or
Save operations.

Implemented through the Session 048 AutoSave baseline, Session 056 page-exit
expedite and AsyncFATFS file-size fix, Session 060 writer/HCNAMES/source work,
and the Session 061 boot reader:

- persistent `settings.cfg` AutoSave on/off preference;
- boot/runtime creation and validation of `/.hcprms1` and `/.hcprms2`;
- a continuation-cycle winner cache that skips dual-record CRC validation
  and the on-card mask re-read when the writer itself committed the last
  target and the card has not been removed (Session 060 Phase A);
- scalar dirty hooks for Scene, Kit, Instrument normal, and morphable
  Instrument Morph values, plus the format's implemented Bank fields;
- two-byte little-endian HCNAMES source fields for every Scene, Kit, and
  Instrument sub-object, with source-byte routing and dirty marking;
- successful whole-object publication for root Instrument Load, normal Kit
  Load, root Scene Load, and selective Bank Load/Save, with a complete
  committed-hierarchy HCNAMES boundary;
- one canonical mutation mask, bounded dirty scanning and value capture, A/B
  transformed copy, CRC32C, commit-last runtime publication, retry, and
  continuation scheduling;
- a post-drain HCNAMES convergence step that clears a per-row "refreshed"
  witness once that row's autosave object is fully captured, and safe-
  rewrites `/.hcnames` through the same temp-file pattern as the A/B
  records (Session 060 Phase B/B2; see "HCNAMES atomic safe-write and the
  refreshed flag" below);
- an AutoSave lifecycle trace when `DEV_MODE_LOGGING` is enabled;
- boot validation of the A/B pair and restoration of a Bank-matching winner;
- per-row Case 1 payload application, Case 2 narrow library reload, and Case 3
  all-or-nothing Scene invalidation, including deferred dirty replay and
  non-blocking notices;
- an all-129-rows-refreshed HCNAMES-authoritative boot path for a newer
  Load/Save session that the page guard prevented HCPR from capturing;
- best-effort boot loading of `pattern.pat` from each accepted Scene's resolved
  library source, because Pattern data is not yet part of HCPR.

Not implemented and not to be inferred from the reader/writer:

- Pattern persistence in the hidden records;
- live Effect persistence (`AUTOSAVE_EFFECT_PARAM_COUNT` is zero);
- crash-recoverable promotion into explicit Bank library files;
- a second resident Bank, background staging Bank, or general object journal.

## Ownership

- `Core/Bank/Scene/Autosave.c/.h` owns the binary format, live-byte projection,
  CRC32C helpers, one canonical dirty mask, typed dirty-marker API, HCNAMES-row
  source-field projection, and the boot-only inverse payload-to-resident apply
  functions. It owns no file handle or scheduler.
- Retained owners mark their own changes: `BankData`, `SceneData`, and Preset's
  descriptor-aware Instrument path call typed marker functions only after the
  retained value changes. `on_scene_load_complete()` owns the root Scene
  whole-object marker after the terminal Scene/Pattern/Effect/HCNAMES result,
  so no partial Scene commit can be published as a successful load.
- `Core/Hardware/SD/filesystem.c` is the sole AsyncFATFS owner. It owns pair
  setup, validation, winner selection, both boot readers, HCNAMES recovery,
  narrow library loads, bounded capture, transformed copying, publication,
  scheduling, and error rollback.
- `settings.cfg` and `filesystem_setAutosaveEnabled()` own policy. A trace or
  diagnostic must never change that policy or dirty state.

## On-card v1 record contract

The two singleton names are:

- `/.hcprms1`
- `/.hcprms2`

Each file is exactly 34,768 bytes:

| Region | Offset | Bytes | Meaning |
| --- | ---: | ---: | --- |
| Header | 0 | 64 | Magic, version, commit, generation, CRC32C, probe |
| Mutation mask | 64 | 3,856 | One bit for every payload byte |
| Bank payload | 3,920 | 128 | Restore slot, name, masks, active Scene |
| Scene payloads | 4,048 | 30,720 | Sixteen fixed 1,920-byte Scene regions |

The payload is 30,848 bytes, so the 3,856-byte mask covers it exactly. Mask bit
N describes payload-relative byte N. It never describes a header byte.

Each Scene region reserves:

- eight name bytes;
- two HCNAMES source bytes immediately after the name;
- 118 Scene-parameter bytes, currently 40 live;
- 512 Effect bytes, currently no live parameters;
- 1,280 Kit bytes containing eight name bytes, a two-byte HCNAMES source
  field, 118 parameter/reserve bytes, and six fixed 192-byte Instrument
  records.

Instrument records retain a three-byte type token, eight identity bytes, a
two-byte HCNAMES source field, 72 descriptor-indexed normal cells, 72
descriptor-indexed Morph cells, and reserved padding. A Morph cell is live
only when its descriptor is Morphable. C structs are never copied as the wire
format. The Scene and Kit source fields occupy relative bytes 8..9; the
Instrument source field occupies relative bytes 11..12, with normal cells
starting at 13. The source field is absorbed from previously reserved
parameter/tail bytes, so every containing section remains the same size.

The Bank `scene_present_mask` occupies payload bytes 10..11 (absolute record
offsets 3930..3931) and is the effective resident Scene availability union.
Bank Load preserves existing resident bits and ORs in its effective
selected-child mask; an equal-value completion explicitly re-marks those two
bytes so a successful load refreshes the hidden record even when the
change-aware BankData setter is a no-op. The logging-only `B` trace stage
witnesses the resident mask at Bank Load commit and at the drain's first-byte
capture; it uses the existing eight-byte trace ring and adds no production RAM.


`Save:[Bank]` preserves the resident present mask by OR-ing in its selected
child subset. A partial save therefore cannot shrink the live Bank solely
because unsaved resident Scenes were outside the save mask. This Session 057
fix is a prerequisite for correct whole-Bank AutoSave publication.

Header requirements:

- magic `HCPR`;
- format version 1;
- valid commit byte `0xa5`;
- wrapping 32-bit generation;
- Castagnoli CRC32C over the complete record while treating stored CRC bytes
  12..15 as zero;
- one-byte probe counter as a writer witness. Generation, not probe, selects
  the newer record. Equal valid generations deterministically select A.

Phase C is the documented exception to the general reserved-cell rule: its
source fields use previously reserved bytes without a version bump because the
record, mask, payload, section boundaries, and validation size are unchanged.
The first complete drain rewrites all present resident payload scopes in the
new internal layout, upgrading old-format parameter positions in place. Any
future change to offsets, widths, ordering, or interpretation outside this
explicit Phase C migration still requires a format-version decision and an
explicit migration/rejection policy.

## Boot restore and policy lifecycle

`settings.cfg` is loaded before initial Bank selection and before hidden-file
setup. Its normalized `autosave=0|1` value is supplied to
`filesystem_setAutosaveEnabled()`. The active Bank from settings is the root
identity against which an HCPR candidate or authoritative HCNAMES row is
checked.

With AutoSave off:

- boot does not validate or read the hidden records and does not run the
  HCNAMES-authoritative special path;
- no hidden-file ensure, recovery, or drain may start;
- mutation tracking is disabled;
- pending canonical dirty state is discarded immediately when safe, or at the
  first safe completion boundary if a transform was already running;
- disabling must never abort an owned AsyncFATFS operation or alter CRC-covered
  bytes midway through a copy.

With AutoSave on, boot stage 10b calls
`filesystem_validateAutosaveWinnerBlocking()` after settings and indexes are
ready but before canonical Bank Load. Both candidates are streamed through the
same size/header/commit/CRC32C validation used by the writer. A valid candidate
whose Bank slot matches settings is preferred over a valid nonmatching
candidate; among candidates with equal match status, wrapping generation
selects the newer, with Record A retaining an equal-generation tie. The stage-11
decision is:

1. If `filesystem_hasBootWinner()` reports a valid winner matching the active
   settings Bank, call `filesystem_autosaveBootReaderBlocking()`.
2. If no matching winner exists or that reader declines, call
   `filesystem_bootHcnamesAuthoritativeLoad()`. This path succeeds only for the
   special all-refreshed register state described below.
3. Otherwise run the pre-existing canonical Bank/Scene/Kit fallback ladder.
   A canonical Bank Load with AutoSave on sets the boot-latch Bank flag so its
   complete live image is marked after tracking is enabled.

A boot-reader deadline/fail-fast expiry follows the boot timeout path; it is
not converted into Case 3 mass invalidation. An empty winner Bank and an empty
HCNAMES-authoritative Bank both decline so the existing empty-Bank fallback
semantics remain intact.

### Matching-winner reader

The reader first validates `.hcnamtmp`, then `.hcnames`. A valid temp is adopted
through the normal safe-write path. If neither register is usable, a valid
HCPR winner may regenerate HCNAMES atomically from its identity/source/type
fields, with all rows marked refreshed. A true read/I/O failure remains a
failure; invalid content does not authorize arbitrary creation unless the
validated-winner regeneration rule applies.

The reader applies the winner Bank section, then evaluates the eight identity
rows for every present Scene in this order: Scene, Kit, Instruments 0..5.

- **Case 1 — row has no `R`:** the HCPR object is caught up. Apply its payload
  through `autosave_applyScenePayload()`, `autosave_applyKitPayload()`, or
  `autosave_applyInstrumentPayload()`. Compare the payload's embedded source
  with the HCNAMES source; a mismatch emits Q/`0x01`, and the payload source is
  copied into the resident register as defense-in-depth.
- **Case 2 — row has `R`, source resolves, narrow load succeeds:** load exactly
  that level from the library. A Scene narrow load changes Scene parameters
  only; a Kit narrow load reads the kitset and all six typed member files; an
  Instrument narrow load reads only that typed member. None may cascade source
  mutations into an independent child row. A successful row emits Q/`0x04`.
- **Case 3 — row has `R` but cannot be resolved/loaded, or a Case-1 Instrument
  type token is invalid:** empty the entire Scene, stop evaluating that Scene,
  rewrite all eight rows to `?|R`, and emit Q/`0x02`. Never leave a partial
  Scene available to later Save.

Source inheritance is Instrument -> Kit -> Scene -> Bank. A numeric source is
invalid only when written directly on the Instrument row itself; a numeric
slot inherited from a parent is a valid library source. Implementations must
use the resolver's `resolved_row` to distinguish those cases.

After row evaluation, every accepted Scene receives a best-effort
`pattern.pat` load from its resolved Scene source. HCPR v1 does not store
PatternSet. A missing/corrupt Pattern leaves the initialized empty PatternSet
but does not empty otherwise valid scalar Scene state. Effects remain zero/live
placeholder state because their format live count is zero.

### HCNAMES-authoritative reader

This special path covers the durable state left by loading a Bank, then loading
or saving children without leaving Load/Save: the page guard can suppress the
HCPR writer while HCNAMES already records every committed source. It may run
only when:

1. HCNAMES row 0 is a direct Bank slot equal to `settings.cfg`'s active Bank;
2. every one of the 129 data rows has `R`.

It never uses or regenerates from HCPR. It loads the Bank container from the
Bank tree, narrow-loads all eight rows of each present Scene with the same
source resolver and all-or-nothing Scene rule, then best-effort loads Patterns.
Any failed gate or hard failure declines to canonical fallback. Do not weaken
the two gates: partial refreshed state is handled by the matching-winner reader,
not by treating HCNAMES alone as a general parameter snapshot.

### Instrument-type scratch lifetime

Both readers parse or seed all 96 Instrument types before the first narrow
load. Those tokens must survive until the last Scene because payload staging is
destructive. The implementation uses the 96-byte view of the existing
144-byte `op_bank_child_scratch` union; asynchronous canonical Bank Load uses
the mutually exclusive 16x9 child-display view only after the reader returns
and `filesystem_start()` clears it. Do not move the types back into
`fs_stage_workspace`, take only a per-Scene snapshot, or borrow the disposable
9,000-byte list/index cache needed by canonical fallback.

### Deferred dirty replay and notices

The readers run with mutation tracking off. `fs_boot_latch` therefore retains a
Bank-fallback flag plus Case-2 and Case-3 Scene masks. After
`filesystem_ensureAutosaveFilesBlocking()` successfully creates/validates the
pair and enables tracking, replay marks the whole Bank for fallback or
HCNAMES-authoritative restore and marks every Case-2/3 Scene without Pattern.
Case-2 bits clear after replay; Bank and Case-3 fields remain until Menu's
read-and-clear notice accessors consume them.

Menu displays these as sequential, non-blocking approximately two-second
post-audio overlays. Notice presentation must not add SD work to boot or delay
audio startup.

With AutoSave on and a resident Bank:

- setup ensures both hidden records exist after the initial restore;
- mutation tracking starts only after setup and its flush succeed;
- runtime re-enable marks the complete currently gettable resident Bank dirty
  so changes made while tracking was off are not missed;
- setup failure leaves tracking and the writer disabled until an explicit
  lifecycle transition permits another setup attempt.

No resident Bank means no AutoSave file activity.

## Dirty marking rules

There is exactly one persistent 3,856-byte canonical dirty mask. Producers OR
bits into it atomically; they do not enqueue events and do not own files.

Use only the typed API:

- `autosave_markBankFieldDirty()`;
- `autosave_markSceneParameterDirty()`;
- `autosave_markKitParameterDirty()`;
- `autosave_markInstrumentNormalParameterDirty()`;
- `autosave_markInstrumentMorphParameterDirty()`;
- `autosave_markSourceDirty()` for one HCNAMES-addressed Scene, Kit, or
  Instrument source field;
- future Effect marker functions only after Effect ownership exists.

Whole-object helpers mark currently gettable cells but do not copy data.
Successful root Instrument Load marks that slot's three type bytes, two source
bytes, all owned Normal endpoints, and all owned Morphable Morph endpoints
immediately after the retained commit. Successful normal Kit Load marks the
Kit and six Instrument payloads. Root Scene Load marks the committed Scene
scope only after its complete filesystem/HCNAMES transaction. Bank Load/Save
marks the effective committed child scope; runtime re-enable and canonical or
HCNAMES-authoritative boot fallback mark the complete currently gettable Bank.

Successful InstrumentMrp Load marks only the destination's Morphable Morph
endpoints. Hidden temporary `kit` restore, failed loads, and HCNAMES names
remain excluded; source bytes are included only when the committed operation
changes or owns that provenance. The reversible InstrumentMrp `kit` restore
uses a Morph-only hidden snapshot and likewise marks only restored Morphable
Morph endpoint cells. `autosave_markSceneWithPatternDirty()` is presently the
non-Pattern alias and must not be described as Pattern persistence. Copy/paste
and less common Load/Save transitions remain explicit regression targets in
`AUTOSAVE_TEST_CASES_LOAD_SAVE_REVISIONS.md`; do not infer coverage from a
nearby marker.

The ordering rule is binding: update the retained owner first, then mark the
matching typed coordinate. Never calculate wire offsets in Menu, DSP, MIDI, or
another producer. Never mark runtime-only DSP overlays as retained state.

Successful Bank Load and Bank Save also mark the existing settings writer
dirty immediately after committing the restore slot. The debounced writer then
serializes `active_bank` from `bank_restoreBankSlot()`; the mark performs no
filesystem I/O and does not create a second settings writer.

## Background writer

New dirty work receives a five-second debounce. Repeated changes coalesce into
the same bits; they do not start one file operation per edit. Load and Save
pages suppress new background starts, and the single filesystem facade gives
foreground work priority. An already active transaction runs to its safe
close/flush boundary.

When the page guard suppresses the writer, `fs_autosave_page_suppressed` is
set. On the first scheduler tick after the user leaves the Load/Save page,
the flag clears and the writer deadline resets to
`now + AUTOSAVE_WRITER_CONTINUATION_INTERVAL_MS` (250 ms). This eliminates
wasted debounce time between page exit and the first drain admission. The flag
is unconditional — it fires whether or not the user loaded/saved something;
if the mask is clean, the scheduler's existing dirty guard disarms the writer
before any drain starts. The flag is also cleared in both card-failure reset
paths. (Session 056, pending hardware verification.)

The page rule is a deferment, not a discard. After a direct foreground
filesystem callback consumes a terminal result, it must acknowledge that
`DONE`/`ERROR` result before it releases its UI owner; otherwise the facade is
not `IDLE` and neither the trace append nor this writer can acquire it. The
final read-only root Scene/Bank index callback follows this rule after it has
captured its success byte. It does not alter the page guard, writer debounce,
or mutation mask.

**This is a general rule, not a one-off, and it has already been missed more
than once.** Every Menu-side terminal path — success or failure — must call
`filesystem_ack()` before releasing its UI owner. Sessions 050 and 051 each
fixed one missing call at one specific completion site. Session 055 found and
fixed the largest remaining gap: `menu_showFilesystemErrorOverlay()` is the
*shared* terminal path for nearly every failed Menu filesystem operation
(nested Instrument entry, top-level Kit/Scene/Bank entry, index reloads, and
more), and it never acknowledged the facade at all. One failed read from any
of those callers parked `status` at `FS_STATUS_ERROR` permanently — silently
killing both this writer and the trace flush for the rest of the session,
since foreground requests keep working even while the facade is stuck
(`filesystem_start()` rejects only on `BUSY`, not `ERROR`), which is what made
the earlier failures so hard to notice. See
`knowledge_files/log_archive/055_SESSION_HANDOFF_LOG.md` for the full
investigation. When adding any new Menu-side filesystem completion path,
audit it against this rule before assuming the writer/trace will keep
running.

One transaction:

1. validates both candidates and chooses the newest valid record matching the
   current Bank identity;
2. imports the winner's on-card mutation mask once for interrupted-work
   recovery;
3. examines at most `AUTOSAVE_MASK_BITS_PER_TICK` mask positions per service
   pass;
4. atomically takes and snapshots at most
   `AUTOSAVE_PARAMETER_GETS_PER_WRITE` live values into the dedicated patch
   cache;
5. opens the winner read-only;
6. removes every case-folded physical variant of only the inactive target;
7. creates one inactive target and streams the winner through a transformed
   copy, substituting captured values and the updated mask;
8. closes and syncs the invalid copy;
9. writes and syncs the CRC;
10. writes the valid commit byte last, closes, and syncs;
11. acknowledges captured bits only after durable completion.

If work remains after success, the next complete transaction is eligible after
250 ms. Clean completion disarms the writer until a later mutation. Errors
restore every captured offset to the canonical mask and retry after the normal
five-second interval. An empty merged mask completes read-only and must not
advance generation, probe, or target contents.

**Continuation-cycle winner cache (Session 060 Phase A).** Steps 1 and 2 above
are skipped on a continuation cycle when a four-static winner cache
(`fs_autosave_winner_cached` plus the cached index/generation/probe) is
populated from the previous transaction's own commit. The writer just wrote
that target and knows its exact identity; re-validating it 250 ms later via
full CRC streaming cost roughly 544 ticks and four file open/close cycles —
the dominant per-cycle expense — for no information gain while the card
remains inserted (SD removal while powered is not part of the product
contract). The cache is populated only when the completed drain used the
normal copy-forward path (`have_winner` true); a no-valid-record recovery
that rebuilt both records from HCNAMES baseline data leaves the cache clear
so the next cycle re-validates fully. It is invalidated at every lifecycle
boundary that could change on-card state or Bank identity: card
failure/remount, boot ensure, AutoSave OFF (both the policy-setter and
scheduler paths, plus a completion-callback OFF-during-active-transaction
path found during implementation), writer error, and Bank-session loss. The
cache is consumed (cleared) the moment a drain reads it, so an error on a
cached-path drain always falls through to full validation on retry, never a
second use of a stale cached identity. Measured effect: steady-state drain
time dropped from about 3.1 s to about 2.2 s per cycle (Section
"Autosave drain timing" cross-reference in `S060PHASE_A_POST_FIXES.md`); the
~2.0-2.2 s file-write phase itself is unchanged and dominates the remaining
time.

The live-Bank match in step 1 is implemented by
`autosave_streamValidationMatchesBank()`. A mismatch no longer forces
regeneration: when a valid winner exists but its Bank identity differs from
the current resident Bank (a legitimate Bank-session transition), the writer
marks the entire Bank payload dirty via `autosave_markResidentBankDirty()`
and proceeds to the transformed copy-forward path (phase 50), overwriting the
mismatched winner's content with current live state in one drain cycle. The
regeneration path (phase 30) is reserved for genuinely invalid records where
no winner exists. Bank slot and name remain mutable payload fields, not
immutable identity.

## CRC scheduling: implemented bounded contract

Every CRC traversal is now governed by
`AUTOSAVE_CRC_BYTES_PER_TICK == 128`: candidate validation, transformed-copy
CRC, initial-record generation, and neither-valid recovery each retain their
cursor/accumulator between filesystem passes and consume no more than the
shared byte budget in one tick. The creation selector is retained separately
before its former scratch field is reused as the CRC accumulator, so a missing
A cannot be opened accidentally as B. This needs no record-sized buffer, blind
delay, or new permanent CRC store.

Do not add a blind one-millisecond interval or sleep between unbounded work.
That failed experiment slowed Bank operations dramatically and merely delayed
the audio glitches; it was rolled back. The controlled reimplementation order
is recorded in `SETTINGS_BANK_LOAD_REIMPLEMENT.md`.

## HCNAMES atomic safe-write and the refreshed flag (Session 060 Phase B/B2)

`/.hcnames` is authoritative in `FILESYSTEM_SPEC.md`, but its safe-write
mechanics and its interaction with AutoSave's dirty mask are specified here
because the writer that clears the flag is the AutoSave drain itself.

The current physical schema is one exact
`#types<TAB>drm<TAB>snr<TAB>cym<TAB>hat` header plus 129 data rows.
Bank/Scene/Kit rows are `name<TAB>source[<TAB>R]`; Instrument rows are
`name<TAB>source<TAB>type[<TAB>R]`, where type is mandatory. The complete
parser/source grammar belongs to `FILESYSTEM_SPEC.md`.

**Atomic safe-write.** Every HCNAMES rewrite — boot full-write, runtime
targeted update, Bank Load, Bank Save, and the drain post-commit convergence
below — now follows the same temp-file pattern already used for
`settings.cfg` and the `.hcprms` pair: open `.hcnamtmp`
(`FS_RESIDENT_NAMES_TEMP_FILENAME`), stream the `#types` header line plus all
129 rows, close, `afatfs_sync()` to make the temp durable, remove the old live
`.hcnames`, rename the temp into place, then take the final flush-gate sync.
The live file is untouched until the remove step; a power loss at any point
leaves either the intact old register or a recoverable `.hcnamtmp` that a boot
recovery prelude in `filesystem_ensureAutosaveFiles_tick()` validates (a
current `#types` header plus 129 parseable rows) and either promotes or
discards before any code path opens `.hcnames` for read.
`hcnames_mirror_valid` is demoted to `INVALID` before every write-capable
open, set to `PUBLISH_PENDING` only after the rename succeeds (not after
close — the file is not authoritative until renamed), and promoted to `VALID`
only by the shared final sync.

**Refreshed flag.** Bit 13 of the existing `fs_resident_source[]` register
(`FS_RESIDENT_SOURCE_REFRESHED_FLAG`, `0x2000`) marks a row whose object was
just loaded or saved from the library and whose autosave record has not yet
fully re-captured it. It is serialized as an optional third `.hcnames` column:
`name<TAB>source<TAB>R\n` for Bank/Scene/Kit rows (0..32). Instrument rows
(33..128) carry a mandatory type column between source and the witness, so
`R` is their optional fourth column: `name<TAB>source<TAB>type<TAB>R\n`;
see `FILESYSTEM_SPEC.md` for the row grammar. Absence of the `R` suffix means
not-refreshed
(backward compatible with pre-Phase-B2 files). Adding this bit required
narrowing `FS_RESIDENT_SOURCE_VALUE_MASK` from `0x7fff` to `0x1fff` and moving
the three special source tokens into 13 bits: `FS_RESIDENT_SOURCE_INHERIT =
0x1fff`, `_UNKNOWN = 0x1ffe`, `_INSTRUMENT_DIRECT = 0x1ffd`. Numbered library
slots (0..999) are unaffected. Any code computing or comparing a source token
must use these post-Phase-B2 13-bit values, not the older 15-bit ones that
appear in pre-Session-060 planning documents.

Every load/save completion that replaces a Scene/Kit/Instrument's resident
identity sets the refreshed flag on that object's row(s) via
`filesystem_setResidentRefreshed()` / `filesystem_setResidentSceneRefreshed()`,
immediately alongside the existing HCNAMES source staging and the Phase C
`autosave_markSourceDirty()` call. `autosave_objectFullyCaptured(hcnames_row)`
(`Autosave.c`) is the cleanliness query: it maps one HCNAMES row (Bank / Scene
1..16 / Kit 17..32 / Instrument 33..128) to its complete wire interval in the
canonical dirty mask and returns true only when every byte in that interval is
clean. After every drain completion boundary (both the clean-mask exit and the
post-commit exit), `filesystem_autosaveDrainAfterCommit()` calls
`filesystem_autosaveDrainHasRefreshWork()` to check whether any refreshed row
is now fully captured; if so it invalidates the mirror, safe-rewrites
`.hcnames` (suppressing the `R` suffix for rows that will become clean once
this write commits), and only after the write's own final sync succeeds does
`filesystem_clearResidentRefreshedCaptured()` actually clear bit 13 for those
rows. An error preserves the refreshed witness for retry on the next drain.

Session 061 made the publication boundary explicit: every successful Load or
Save that commits an object hierarchy must publish the HCNAMES name, source,
and refreshed witness for every Scene/Kit/Instrument object it committed. A
root Scene operation therefore carries the Scene, Kit, and six Instrument
identities together; Scene Save seeds the child identity store from its source
rows, and Bank Save child preparation stages the Kit identity. Do not make the
boot reader compensate for stale names. Already-persisted `?|R` rows from an
older failed reader require an explicit reload rather than source guessing.

This is why the immediate (not deferred) load/save marking approach works
without a separate re-dirty request mask: `autosave_markPayloadOffsetDirty()`
already uses IRQ-safe atomic bit-OR, so a mark that lands mid-scan is either
captured in the current drain cycle or survives cleanly into the next one, and
`objectFullyCaptured()` keeps the refreshed flag set until every byte is
actually clean either way. Session 060 Phase D audited this against the
original parent-plan design (a deferred `uint16_t` per-Scene re-dirty mask)
and found the immediate approach strictly better — lower latency, zero extra
SRAM, no new ordering dependency — so the deferred mask was never implemented.
See `S060PHASE_D_RE_DIRTY.md` for the full call-site audit.

Name bytes (the 8-byte name field in each Scene/Kit/Instrument autosave
record header) are deliberately excluded from source/refreshed re-dirtying:
`autosave_getLivePayloadByte()` has no name getter, compound markers skip the
name byte range by design, and the boot readers use `.hcnames` for live
identity, not autosave record names. Regeneration may use the winner's embedded
names only when HCNAMES itself is absent/corrupt and the winner is valid. Do
not add a name getter or dirty name bytes merely to make debug/baseline fields
look current.

## Power-loss behavior

During a normal A/B update, the inactive target remains invalid until payload
and CRC are durable. The valid commit byte is published last. Power loss can
therefore leave:

- the previous winner valid and the target invalid;
- both records valid, with generation selecting the newer;
- an incomplete target that fails size/header/CRC/commit validation.

Power loss while writing the runtime target's CRC does not invalidate the
previous winner. Initial creation is different: an interrupted newly-created
file can be short and invalid, and if both files were absent there may not yet
be a valid peer. AutoSave does not promise recovery if both records are
externally deleted, corrupted, or made ambiguous by duplicate directory
entries.

## Singleton and duplicate-file rules

FAT display names are case-insensitive. A failed open callback is not proof of
absence, and AsyncFATFS append/write modes include CREATE. These rules caused
repeated same-display-name failures and are mandatory for future work:

- First creation must follow a complete, successfully closed root scan proving
  zero case-folded matches.
- Existing boot setup records are never opened for write.
- Runtime replacement keeps the selected winner, removes all case-folded file
  variants of only the inactive target, waits for removal completion, and then
  creates one canonical target.
- Scan, finder, close, open, or type errors remain errors. They must not fall
  through to creation.
- Never choose one of multiple matches silently. Preserve evidence unless a
  separately specified recovery transaction authorizes removal.
- Never add another hidden record name as a workaround for a failed lookup.

The hidden A/B paths implement their own scan/create discipline. Development
log files do not yet have equivalent duplicate-safe singleton handling; that
limitation belongs to `DEV_MODES.md` and must not be mistaken for an AutoSave
format rule.

## Resolved: 32,768-byte `.hcprms` file-size truncation

Earlier hardware captures produced exactly 32,768-byte `.hcprms` files whose
prefix appeared to be a valid initial record. The root cause was identified
and fixed in Session 056: `afatfs_fseekAtomic()` did not call
`afatfs_fileUpdateFilesize()`, so `logicalSize` stayed at 0 for newly created
files. The only size persisted to the FAT directory entry was `physicalSize`
(cluster-rounded to 32,768 = one 32 KB cluster) from
`AFATFS_SAVE_DIRECTORY_NORMAL` during cluster allocation. See
`ASYNCFATFS_REFERENCE.md` "Seek and file-size tracking" for the fix.

Post-fix hardware testing confirmed all four `.hcprms` files across two boots
are the correct 34,768 bytes.

The Session 047 logging-only 64-byte `ASENSURE` boot-deadline diagnostic
capsule remains in place for any future lower-layer failures.
`DEV_MODES.md` owns its exact 72-byte bootlog envelope.

## Public API guide

The public boundary is split deliberately. Retained owners use `Autosave.h`;
boot, policy, and SD orchestration use `filesystem.h`.

### Retained-state and format API (`Autosave.h`)

| API family | Use | Constraint |
|---|---|---|
| `autosave_mark*ParameterDirty()` / `autosave_markSourceDirty()` | Mark one retained scalar/source after its owner commits the value | Producer does no file I/O and never computes a raw wire offset |
| `autosave_markWholeInstrumentDirty()`, `autosave_markKitDirty()`, `autosave_markSceneWithoutPatternDirty()`, `autosave_markResidentBankDirty()` | Mark a completed object/region | Marks only currently implemented live bytes; names and Pattern remain excluded |
| `autosave_mask*()` helpers | Atomic take/merge/restore and writer progress | Filesystem consumes the one canonical mask; no second request mask |
| `autosave_getLivePayloadByte()` | Serialize one live payload coordinate | Writer-side projection only |
| validation/CRC/format helpers | Stream-validate and construct HCPR v1 | Exact geometry and commit-last rules remain binding |
| `autosave_applyBankPayload()`, `autosave_applyScenePayload()`, `autosave_applyKitPayload()`, `autosave_applyInstrumentPayload()` | Apply validated HCPR bytes at boot | Tracking must be off; Instrument apply can reject unknown three-byte type text |
| `autosave_extractPayloadSource()` | Read the two-byte source from a validated section | Used for Case-1 defense-in-depth comparison |

### Filesystem AutoSave API (`filesystem.h`)

| API | Use and lifecycle |
|---|---|
| `filesystem_setAutosaveEnabled()` / `filesystem_autosaveEnabled()` | Apply/query normalized policy without unsafe synchronous abort |
| `filesystem_ensureAutosaveFilesBlocking()` | Boot/runtime setup of the pair; enables tracking only after durable success and replays the boot latch |
| `filesystem_validateAutosaveWinnerBlocking()` / `filesystem_hasBootWinner()` | Stage-10b streaming validation and the stage-11 Bank-match gate |
| `filesystem_autosaveBootReaderBlocking()` | Restore a validated Bank-matching winner with per-row Cases 1/2/3 |
| `filesystem_regenerateHcnamesFromWinnerBlocking()` | Recover missing/invalid HCNAMES from a validated winner; internal boot orchestration is the normal caller |
| `filesystem_bootHcnamesAuthoritativeLoad()` | Restore the all-129-rows-refreshed, settings-Bank-matching special state without using HCPR |
| `filesystem_setBootLatchBankFallback()` | Defer whole-Bank dirty publication until tracking becomes live; `main.c` only |
| `filesystem_bootReaderNoticeSceneMask()` / `filesystem_bootReaderNoticeBankFallback()` | Menu read-and-clear access to one-shot post-boot notices |
| `filesystem_autosaveTraceFlushBlocking()` | Bench-only durable trace boundary before deliberate power removal |

All boot reader functions are blocking only in the pre-audio boot window and
internally pump AsyncFATFS. Runtime Load/Save must continue to use the
asynchronous request/status/ack facade.

## Extending AutoSave

For each new retained scalar:

1. identify the owning Bank/Scene/Kit/Instrument/Effect domain;
2. append or explicitly version its format identifier and live-count contract;
   Phase C source fields are the one documented reserved-space migration;
3. add the live-byte getter mapping;
4. call the typed marker from every retained setter after mutation;
5. include it in the appropriate whole-object marker;
6. keep descriptor Morph eligibility consistent between getter and marker;
7. add compile-time geometry assertions;
8. test mutation, writer error rollback, restart, AutoSave off, and power
   interruption at payload/CRC/commit boundaries appropriate to the change.

Pattern or Effect support is a feature extension, not a scalar addition. It
requires confirmed retained ownership, an explicit wire schema/version plan,
bounded snapshot/read behavior, dirty hooks, recovery semantics, and SRAM
approval for any new allocation.

Do not add a second writer, scheduler, mask, record-sized SRAM image, or
filesystem handle. Do not borrow the 9,000-byte name cache for the dedicated
4,608-byte patch cache without a separately reviewed ownership transition.

## Validation status and diagnostics

Hardware validation is accepted for scalar Scene, Kit, Instrument, MIDI
channel/note, and the root Scene publication boundary. No user-changeable Bank
scalar exists for an extra direct UI test. Pattern and live Effect remain
excluded exactly as specified above.

Session 061 hardware-accepted the HCNAMES-authoritative reader with
`SD_CARD_READER_9`, produced from a Bank 001 Load followed by root Scene 008
`Rollin` loads into resident Scenes 0, 1, 14, and 15 without leaving Load/Save.
The final image SHA-256 is
`5732e821d256f521e48814d2cf255c895b1fbb7fdfa9f006b43f5ae293fb8c62`.
Its trace contains 128 Case-2 success rows and one summary, raw summary
`0x0000ffff` (Case 2 `0xffff`, Case 3 `0`), and no `E` or `X`. The post-boot
card contains valid generations 9 and 10; generation 10 is the winner. All 129
HCNAMES refreshed witnesses agree with the corresponding HCPR dirty-object
state. This closes the original cross-Scene type-lifetime failure.

One focused acceptance test remains: reboot the Reader 9 state and capture it
to exercise the mixed matching-winner Case-1/Case-2 path. The full later
interaction/failure matrix is in
`AUTOSAVE_TEST_CASES_LOAD_SAVE_REVISIONS.md`; it is not a claim that already
accepted writer cases are unverified.

With file logging enabled, AutoSave emits bounded lifecycle transitions through
the RAM-only `AutosaveTrace` producer. The trace must not alter AutoSave
policy, dirty state, scheduling, or writer results. AutoSave does not own the
development flag, destination filename, record envelope, or persistence
policy; those details are authoritative only in `DEV_MODES.md`.

Whole-Instrument marking additionally emits one bounded diagnostic outcome
record after each request. It records map eligibility, the mutation-tracking
gate, and expected versus accepted dirty-byte counts; it does not change the
mask, scheduling, retained record, or loader result. This terminal summary is
necessary because the individual dirty-byte records for one Instrument can
wrap the fixed trace ring. Its exact flags and packing are owned by
`AutosaveTrace.h` and `DEV_MODES.md`.

An `ASENSURE` boot timeout additionally freezes a logging-only diagnostic
capsule before boot recovery destroys the active filesystem state. It observes
creation only and neither changes record validity nor retries, truncates,
repairs, or accepts either hidden record. Its exact `/bootlog.bin` envelope is
owned by `DEV_MODES.md`; this specification deliberately does not duplicate
the diagnostic wire layout.

The reader emits `AUTOSAVE_TRACE_STAGE_BOOT_READER` (`Q`): flags `0x01` mean a
Case-1 embedded-source mismatch, `0x02` a Case-3 Scene invalidation, `0x04` a
Case-2 narrow-load success, and `0x80` the final summary. A row value packs
Scene in bits 0..3, HCNAMES row in bits 8..15, and embedded/resolved source in
bits 16..31 when applicable. A summary packs Case-2 Scenes in bits 0..15 and
Case-3 Scenes in bits 16..31. Both readers flush the Q batch before runtime
dirty replay can wrap the trace ring. `DEV_MODES.md` owns the complete trace
file envelope and decoder behavior.

`tools/decode_devlogs.py` decodes the eight-byte boot token and conditional
72-byte `ASENSURE` capsule; it also decodes `/asavetrc.bin`. It is not an
AutoSave record inspector and it must not modify fixtures. Validate AutoSave
captures without
editing the source files: check exact record sizes, header/commit fields,
CRC32C, generation selection, dirty masks, and trace records independently. A
broader human-readable development-log converter is deferred until AutoSave
behavior and every logging format are complete.
