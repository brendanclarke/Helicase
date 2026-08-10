# HCNAMES Sources Extension — Design and Implementation Plan

## Status and purpose

Implementation is complete pending target validation. This document remains
the change log and acceptance plan; it does not by itself claim target
validation.

### Implementation notes — 2026-08-10

- Added the approved 129-by-`uint16_t` (258-byte) filesystem-owned source
  register, with a high-bit pending-publication flag so a source selected by a
  successful load survives the asynchronous reread of the old HCNAMES file.
- Extended HCNAMES parsing and serialization to `name<TAB>source`; legacy
  name-only rows parse as `UNKNOWN`, malformed new source tokens fail closed,
  and all writers now emit a source token even for blank names.
- Removed the 32-byte `SceneData` source array/API and the corresponding
  `settings.cfg` read/write fields.  Legacy `scene_source_NN` settings keys
  are accepted and ignored during migration.
- Completed source staging at successful root Instrument, Kit, Scene, and Bank
  load boundaries.  A root Instrument `@` source is assigned at its actual
  payload commit, not in the shared name publisher, so an Instrument Save
  retains rather than overwrites provenance. The existing HCNAMES close/sync
  gate clears staged source flags only after durability.
- Added `filesystem_resolveResidentSource()`. It follows the fixed
  Instrument -> Kit -> Scene -> Bank chain in one place and deliberately does
  no file I/O; missing-direct-target retry and global boot fallback remain the
  later AutoSave reader's responsibility.
- Kept malformed-source error handling inside each reader's existing async
  close path, so a bad HCNAMES record reports an error only after its open file
  is closed.
- Updated the filesystem/interchange/SRAM reference documents for the paired
  register and removed stale Preset/settings provenance comments. The SRAM
  manifest records the approved 258-byte allocation but is not a newly linked
  size report.
- Source-level review is complete for the changed writer/read paths. Hardware
  validation remains required. No build was run because the requested toolchain
  is unavailable in this environment.

The desired ownership model is:

- `/.hcnames` is the card-resident identity-and-provenance register for the
  active resident Bank, Scenes, Kits, and Instruments.
- Bank/Scene/Kit/Instrument payload files remain the normal cold-boot
  baseline.
- AutoSave is a later overlay.  Its clean name group may supersede the
  baseline name at boot; a dirty name group must fall back through the
  HCNAMES provenance chain.
- No name string is added to `scene_t`, `kit_t`, a DSP object, or a Menu-local
  cache.  The existing nine active identity rows remain the runtime name
  interface.

This is not a claim that AutoSave boot restore exists today.  The current
writer is write-only; the extension prepares the authoritative fallback model
that the later reader needs.

## Chosen representation

Keep one root singleton: `/.hcnames`.  Do **not** add a separately mutable
`/.hcsources` sidecar.  A sidecar would introduce a second open/close/sync
contract, pair-loss cases, and a question of which file wins after a power
loss.  The established HCNAMES preserve/overlay/rewrite transaction already
offers one coherent publication boundary.

Retain the existing 129 logical row coordinates and write one extended text
record per row:

```text
<trimmed-name><TAB><source-token>\n
```

The tab is mandatory in the new format.  The name may be empty, so the token
is always parsed from the field after the tab.  The current one-field line is
accepted only as a legacy input; every successful HCNAMES rewrite emits the
extended format.

Source tokens are compact text encodings of a `uint16_t` cache value:

| Token | Cached value | Meaning |
| --- | ---: | --- |
| `-` | `0x7fff` | Inherit from the enclosing level. |
| `?` | `0x7ffe` | Unknown: no asserted source; boot must use the documented fallback path. |
| `000`–`999` | `0`–`999` | A direct root object slot.  The row class determines Bank, Scene, or Kit namespace. |
| `@` | `0x7ffd` | Direct root Instrument source.  The same HCNAMES row supplies its stem; the resolved Instrument type supplies its extension. |

The logical row class is already fixed by the existing row map, so a separate
type byte is not needed.  `@` cannot use an unstable Instrument browser index:
the source key is the durable row stem plus the Instrument type that was
committed by the successful load.

The upward source chain is binding:

```text
Instrument row  -> Kit row -> Scene row -> Bank row -> normal boot fallback
```

An `inherit` token follows the next arrow.  A direct token ends the chain.
`unknown` does not invent a source; it invokes the normal boot fallback after
the resolver has exhausted any known enclosing source.  A missing/malformed
direct target is treated like an unresolved source and walks upward before the
global fallback is used.

## Memory approval and ownership

The user approved a 258-byte source cache: 129 `uint16_t` values.

Add it in `Core/Hardware/SD/filesystem.c` beside `fs_list_cache_name`:

```c
static uint16_t fs_resident_source[FS_RESIDENT_NAMES_ROW_COUNT]; /* 258 B */
```

Its owner is `filesystem.c`; its lifetime is static because HCNAMES source
resolution is needed after the shared 9,000-byte name cache is repurposed for
an index.  It replaces the current `SceneData.c` `scene_sources[16]` array
(32 B).  The net permanent SRAM increase is 226 B.  Record both the exact
258-B allocation and the 32-B removal in `SRAM_MANIFEST.md` before linking.

Do not put sources in `scene_t`, `kit_t`, `BankData`, Menu scratch, or the
AutoSave mask.  They are filesystem provenance, not playable state.

## Existing code affected

### 1. HCNAMES constants, cache, and public accessors

Files: `Core/Hardware/SD/filesystem.c`, `filesystem.h`, and the filesystem
sections of `MODULE_INTERCHANGE_SPEC.md` / `FILESYSTEM_SPEC.md`.

- Keep `FS_RESIDENT_NAMES_ROW_COUNT == 129`; rows remain Bank, 16 Scenes,
  16 Kits, then 96 Instruments.  The source is a field of each logical row,
  not another 129 physical cache rows.
- Add source constants for `INHERIT`, `UNKNOWN`, and `INSTRUMENT_DIRECT`, plus
  `filesystem_residentSource(row)`, `filesystem_resolveResidentSource()`, and
  a controlled setter.  These APIs make
  row ownership explicit and prevent Menu/Preset code from constructing raw
  source values.
- Extend `filesystem_prepareResidentNamesCache()` to clear both the existing
  name cache and all 129 source values to `UNKNOWN`.  This prevents source
  reuse after a library-index cache transition.
- Extend `filesystem_cacheResidentName()` into a paired parser/update helper
  such as `filesystem_cacheResidentRecord(row, line)`.  It must parse a
  legacy name-only line as `UNKNOWN`, reject malformed new tokens, and never
  let a bad source silently become `inherit`.
- Extend `filesystem_cachedResidentName()` with a source companion accessor.
  All targeted overlay helpers must copy/update the pair together.

Why: current HCNAMES helpers preserve only eight name cells.  Updating only a
name or only its source would create a fallback record that refers to a
different object than the displayed identity.

### 2. HCNAMES serialization and migration

Files: `Core/Hardware/SD/filesystem.c` functions around
`filesystem_formatResidentNameLine()`, `filesystem_nextResidentNameLine()`,
`filesystem_writeResidentNames_tick()`, and `filesystem_residentNames_tick()`.

- Replace the name-only formatter with a record formatter that emits the
  trimmed name, one tab, the canonical source token, and a newline.
- Preserve blank names: a blank row with inherited source serializes as
  `\t-\n`, not as an omitted row.  Row number is the identity coordinate.
- During all read paths—targeted Instrument/Kit/Scene updates, Bank
  preserve/overlay, boot recovery, and the AutoSave initial/recovery HCNAMES
  borrow—parse all 129 paired records into the name and source caches.
- Legacy files have no tab.  Load their name and assign `UNKNOWN`; do not
  guess that a row belongs to the current Bank.  The next completed load
  writes explicit provenance.
- A missing HCNAMES file remains subject to the existing case-folded absence
  proof.  On a proven absence, execute the ordinary Bank-000 / existing
  fallback selection first, initialize the resulting rows/sources, then write
  the new extended register.  A NULL open, duplicate singleton, scan/open/
  close/FAT error must never authorize regeneration.
- A source parse failure is a HCNAMES content error, not an instruction to
  create a new file or erase old rows.  Retain the current failure-transparent
  policy.

Why: one file and one durable rewrite avoids a name/source split-brain after
interrupted or failed metadata publication, while legacy parsing makes the
upgrade deterministic.

### 3. Source propagation at successful public boundaries

Files: `Core/Bank/Scene/Preset/presetManager.c`,
`Core/Hardware/SD/filesystem.c`, and any narrow HCNAMES request APIs.

At a successful public completion, update both the affected identity row(s)
and source row(s) before the HCNAMES write is allowed to complete:

| Operation | Rows updated | Source update |
| --- | --- | --- |
| Root Instrument Load | one Instrument row per selected destination Scene | set row to `@`; the published selected stem is the direct source key. |
| Root Kit Load | one Kit and six Instrument rows per selected Scene | Kit gets direct root Kit slot; all six Instrument rows become `inherit`. |
| Root Scene Load | Scene plus its Kit/Instrument subtree | Scene gets direct root Scene slot; Kit and all Instrument children become `inherit`. |
| Bank Load | Bank row and exactly each successfully loaded selected child block | Bank gets direct Bank slot; selected Scene subtrees become `inherit` unless the Bank-local child has an explicitly represented direct source.  Unselected rows remain byte-for-byte preserved. |
| Save / rename | only identity rows actually renamed | retain source unless the Save operation deliberately changes resident provenance; saving a copy does not change where the resident object was loaded from. |
| Morph projection / temporary `kit` preview | no permanent source change | these are parameter projections, not source replacement. |

For a failed, cancelled, staged-only, preview-only, or temporary-load action,
make no identity or source mutation.

Why: provenance is meaningful only once the corresponding live payload and
identity have committed.  Clearing child overrides on a parent replacement
prevents a stale Instrument source from surviving a replacement Kit.

### 4. Remove settings-owned Scene provenance

Files: `Core/Bank/Scene/SceneData.c/.h`, `presetManager.c`,
`Core/Hardware/SD/filesystem.c`, `FILESYSTEM_SPEC.md`, `AUTOSAVE.md`, and
`MEMORY.md`.

- Remove `scene_sources[16]`, `scene_resetSources()`, source encodings, and
  their public accessors from `SceneData`.
- Remove `scene_source_00` through `scene_source_15` parsing and serialization
  from `settings.cfg`; reduce the schema accordingly.
- Remove only the provenance calls in Preset completion.  Do not change the
  accepted one-second settings writer policy, AutoSave preference, or its
  revision/retry machinery.
- Replace those calls with HCNAMES source-pair updates owned by the same
  successful load completion that already owns the name update.
- Version/document the settings schema transition.  Older files may contain
  now-ignored `scene_source_NN` keys; they should not cause failure merely by
  being present.  New writes omit them.

Why: keeping both settings provenance and HCNAMES provenance would preserve
two independently writable answers to one fallback question.

### 5. AutoSave identity support and future boot reader

Files: `Core/Bank/Scene/Autosave.c/.h`, `Core/Hardware/SD/filesystem.c`, and
`AUTOSAVE.md`.

- Add named mapping helpers from each AutoSave Bank/Scene/Kit/Instrument name
  payload interval to its HCNAMES logical row.  The mappings must be shared by
  initial-record formatting, live capture, whole-object markers, and the
  eventual reader.
- Add `autosave_markNameGroupDirty(row/scope)` that marks all eight bytes in
  one short critical section.  The group helper replaces eight unrelated
  scalar calls so a caller cannot accidentally publish a partial name.
- Extend `autosave_getLivePayloadByte()` to read a dirty name byte from the
  HCNAMES identity register/cache accessor.  It must not add text to SceneData
  or call filesystem I/O.
- Initial/recovery formatting continues to snapshot HCNAMES names, but must
  use the paired-record parser and a documented completeness state.  A
  CRC-valid initial record with zero parameter payload is structurally valid
  but is not an authoritative complete resident-Bank snapshot.
- Design the future reader to apply a complete name group only when all eight
  associated on-card mask bits are clear.  If any is set, resolve the HCNAMES
  source chain and retain/load that fallback result for the whole group.
- Do not use Bank slot/name equality to reject a CRC-valid AutoSave record.
  Slot/name are payload values and restore inputs, not selection identity.

Why: AutoSave must be able to preserve a loaded identity across reboot, but it
must never manufacture a partially written name from an interrupted payload.

### 6. Durable ordering

The required load completion order is:

```text
validated staged payload
  -> resident payload commit / runtime apply preparation
  -> identity + source pair update in HCNAMES cache
  -> durable HCNAMES rewrite
  -> public completion callback
  -> AutoSave whole-object/name-group dirty marking
```

The AutoSave marker is after the HCNAMES rewrite succeeds.  If HCNAMES cannot
be made durable, the Load operation reports failure and no AutoSave name can
outlive its fallback provenance.

## Implementation sequence and verification

1. Document the new file grammar, token mapping, legacy behavior, resolver,
   and SRAM change.  Obtain any required confirmation before changing RAM.
2. Add paired HCNAMES parser/formatter and 258-B source cache without changing
   any load behavior.  Build both logging configurations.
3. Hardware-test an existing legacy HCNAMES file: names remain readable,
   sources become `UNKNOWN`, and one explicit Load rewrites only the affected
   rows in extended format.
4. Implement source propagation one operation family at a time: Instrument,
   Kit, Scene, then selective Bank.  Verify each preserves unselected rows and
   clears only the defined child overrides.
5. Remove settings Scene provenance and verify settings still preserves
   AutoSave on/off with its existing debounce behavior.
6. Add AutoSave name-group mapping/getter/marker support, but do not add the
   whole-object load hook until `INSTRUMENT_LOAD_AUTOSAVE.md`'s isolated plan
   is ready.
7. Design and separately implement the AutoSave boot reader/completeness
   contract.  Do not infer reader correctness from writer success alone.

Required fixtures include: missing HCNAMES; legacy HCNAMES; malformed source
token; deleted direct source; Instrument override over inherited Kit; Kit
replacement clearing Instrument overrides; partial Bank load preserving
unselected name/source pairs; AutoSave name mask clean; and each of the eight
name bits individually dirty.  Each fixture must prove the exact resolved
name and source path after a cold boot.

## Detailed file-by-file code delta

This section is the implementation checklist.  It names the existing symbols
that must change and explicitly calls out paths that must be reviewed but do
not need a new allocation or a new asynchronous operation.

### `Core/Hardware/SD/filesystem.c`

#### A. Define source values and allocate the approved cache

Near `FS_RESIDENT_NAMES_ROW_COUNT` add:

```c
#define FS_RESIDENT_SOURCE_INHERIT           UINT16_MAX
#define FS_RESIDENT_SOURCE_UNKNOWN           (UINT16_MAX - 1u)
#define FS_RESIDENT_SOURCE_INSTRUMENT_DIRECT (UINT16_MAX - 2u)
#define FS_RESIDENT_SOURCE_DIRECT_SLOT_LIMIT 1000u
```

Then, beside `fs_list_cache_name`, add:

```c
static uint16_t fs_resident_source[FS_RESIDENT_NAMES_ROW_COUNT];
```

Add a static assertion that it is exactly 258 bytes.  Initialize/clear every
entry to `FS_RESIDENT_SOURCE_UNKNOWN` in `filesystem_prepareResidentNamesCache`
and in the global filesystem reset path.

What it does: gives every logical HCNAMES row a durable in-RAM source while
the name cache is temporarily repurposed for a `.hcindex`.

Why it exists: `fs_list_cache_name` contains only 9-byte display cells and is
deliberately discarded after Menu entry.  The source must remain queryable by
the later boot restore/resolver without reintroducing source fields in musical
objects.

#### B. Replace name-only parsing with record parsing

Replace the internal role of `filesystem_cacheResidentName(row, line)` with
three helpers:

```c
static uint8_t filesystem_parseResidentSourceToken(const char *text,
                                                    uint16_t *source_out);
static uint8_t filesystem_parseResidentRecord(const char *line,
                                               char name_out[9],
                                               uint16_t *source_out,
                                               uint8_t *was_legacy_out);
static uint8_t filesystem_cacheResidentRecord(uint16_t row,
                                              const char *line);
```

`filesystem_parseResidentRecord` must:

1. locate the first tab;
2. when no tab exists, normalize the whole legacy line as the name and return
   `UNKNOWN` plus `was_legacy_out = 1`;
3. when a tab exists, normalize the text before it exactly as the current
   display-name parser does and strictly parse the token after it;
4. reject a second tab, non-printable characters, out-of-range numeric slots,
   trailing junk, or an `@` token outside an Instrument row; and
5. leave the destination cache unchanged on error.

`filesystem_cacheResidentRecord` writes both
`fs_list_cache_name[row]` and `fs_resident_source[row]` only after the entire
record passes validation.

What it does: converts the current line reader into a paired
identity/provenance reader while retaining the same 129-row streaming loop.

Why it exists: accepting a malformed source as an empty/inherited source would
silently redirect boot fallback.  The source must fail closed just as malformed
payload content does.

#### C. Add source-aware formatting

Replace `filesystem_formatResidentNameLine()` with:

```c
static uint8_t filesystem_formatResidentSourceToken(char *dst, uint16_t cap,
                                                     uint16_t source,
                                                     uint16_t row);
static uint8_t filesystem_formatResidentRecordLine(char *dst, uint16_t cap,
                                                   const char *name,
                                                   uint8_t present,
                                                   uint16_t source,
                                                   uint16_t row);
```

The formatter must always emit `name`, tab, canonical token, newline.  It
must serialize an empty name as `\t<token>\n`; it must never omit an empty
logical row because row number is part of the file contract.  Numeric slots
are zero-padded to three digits.  `@` is rejected for non-Instrument rows even
if an invalid caller reaches the formatter.

Update both writers to call it:

- `filesystem_residentNames_tick()` phase 5, which rewrites a preserved/overlaid
  cache after a normal update; and
- `filesystem_writeResidentNames_tick()` phase 2, which emits the first-card
  bootstrap register.

Replace `filesystem_nextResidentNameLine()` with a source-aware equivalent.
For bootstrap, Bank gets the selected root Bank slot when known; rows that
cannot be proven from the fallback chain receive `UNKNOWN`, not a guessed
parent source.

What it does: makes every new HCNAMES write self-contained and deterministic.

Why it exists: a reader that understands source records cannot recover a
missing source from a line that was never written, and writing paired rows
together is the only way to preserve atomic name/source publication.

#### D. Change the existing HCNAMES state machines, not their ownership

In `filesystem_residentNames_tick()`:

- Phase 2 must invoke `filesystem_cacheResidentRecord()` rather than the
  old name-only cache helper.  On parse error, close the source file and finish
  `FS_STATUS_ERROR`; do not continue to phase 3 and rewrite the file.
- Phase 3 must choose a source-aware overlay helper alongside the existing
  Instrument/Kit/Scene name overlay helper.
- Phase 5 must obtain both the cached name and cached source for
  `op_item_offset` and stream the extended record.
- Phase 7's proven-absent bootstrap must initialize every untouched cache name
  blank and every untouched source `UNKNOWN` before it overlays the completed
  action.  It must preserve the existing no-create-on-NULL semantics.
- Phases 8–9 retain exactly one read retry after a scan finds one folded match;
  the extension must not turn a parse error into a second retry or creation.

In `filesystem_writeResidentNames_tick()`:

- Keep the existing folded singleton proof in phase 0 and the close/flush
  completion gate in phase 3.
- Replace only the row serializer in phase 2.  It must write source tokens for
  every row, including blank ones.

In the AutoSave initial/recovery ensure state machine:

- Its HCNAMES streaming phases (current boot creation and invalid-pair recovery
  paths) must parse extended records through the same helper; no private
  second parser is permitted.
- Pass only the resulting 129 normalized name rows to the existing initial
  record formatter until the later AutoSave reader/source integration is
  implemented.  Sources are not currently part of the v1 AutoSave wire image.

What it does: retains the proven asynchronous facade/close/error behavior and
changes only record content handling.

Why it exists: HCNAMES is touched by normal menu updates, Bank Load/Save,
boot initial creation, and AutoSave recovery.  Updating only the menu reader
would make those other paths either corrupt the new format or interpret the
token as part of a display name.

#### E. Make every overlay update a name/source pair

Change the following existing internal helpers to update
`fs_resident_source[row]` as well as `fs_list_cache_name[row]`:

- `filesystem_cacheCurrentResidentInstrumentNames()`;
- `filesystem_cacheCurrentResidentKitNames()`;
- `filesystem_cacheCurrentResidentSceneNames()`; and
- `filesystem_cacheLoadedBankChildNames()` (the selected-child Bank overlay).

Add small internal source setters rather than embedding raw source literals in
each loop, for example:

```c
static void filesystem_cacheResidentSource(uint16_t row, uint16_t source);
static void filesystem_cacheSetInstrumentDirect(uint16_t row);
static void filesystem_cacheSetKitDirect(uint16_t row, uint16_t kit_slot);
static void filesystem_cacheSetSceneDirect(uint16_t row, uint16_t scene_slot);
static void filesystem_cacheSetChildInherited(uint8_t scene_index);
```

Use the captured request fields already held by each operation (`op_slot`,
`op_scene_load_scene_mask`, `op_kit_load_scene_mask`, and the saved Bank slot),
not Menu's current cursor.  Extend operation scratch only if a particular
completion does not already retain its accepted root slot.  Do not use a
browser index as an Instrument source.

What it does: establishes provenance at the same public boundary that now
establishes the name.

Why it exists: name and source can both change on a root load.  A later Menu
cursor move must never retarget a delayed HCNAMES or AutoSave publication.

#### F. Implement one resolver, but do not invoke it from normal menu paths

Add an internal resolver with a request object suitable for later boot use:

```c
typedef struct {
    uint16_t row;
    uint16_t resolved_source;
    uint16_t resolved_from_row;
} fs_resident_source_resolution_t;

static uint8_t filesystem_resolveResidentSource(
    uint16_t row, fs_resident_source_resolution_t *out);
```

It walks Instrument -> Kit -> Scene -> Bank, checks row bounds and source
token validity, and reports either a direct source, `UNKNOWN`, or unresolved.
It performs no I/O.  A later boot loader owns opening the returned object and
handles missing/malformed storage by walking the remaining ancestor chain.

What it does: centralizes hierarchy semantics.

Why it exists: duplicating “if empty then parent” logic in AutoSave, Preset,
and filesystem state machines would eventually produce different fallbacks for
the same name.

#### G. Add only narrow public source APIs

Expose getters/setters that operate on logical HCNAMES rows, not on SceneData
or Menu strings:

```c
uint16_t filesystem_residentSource(uint16_t row);
uint8_t filesystem_setResidentSource(uint16_t row, uint16_t source);
```

The setter validates row-class/token compatibility.  It changes RAM/register
state only; the existing request update functions publish it to card.  Do not
provide a public API that opens `/.hcnames` synchronously or accepts arbitrary
filename text.

What it does: lets the successful filesystem/Preset boundary declare
provenance while keeping parsing and physical persistence private.

Why it exists: source writes must use the same preserve/overlay/rewrite flow
as names; a casual direct file API would bypass singleton and error rules.

### `Core/Hardware/SD/filesystem.h`

- Add the three source constants (or a public enum) and documents stating
  their allowed row classes.
- Add declarations for the narrow row-based source getter/setter and, if
  needed by the future reader, a read-only resolver result API.
- Update comments for `filesystem_writeResidentNamesBlocking()`: it now writes
  129 extended identity/source records, not newline-delimited names only.
- Update comments for the three load/update families:
  `filesystem_requestUpdateResidentInstrumentNames`,
  `filesystem_requestUpdateResidentKitNames`, and
  `filesystem_requestUpdateResidentSceneNames`.  Each now preserves/replaces
  paired records and its source-propagation rule must be stated.
- Add an explicit source argument only where the request lacks a retained
  source decision.  Prefer operation-local setters made immediately before the
  existing update request where the source is already obvious; avoid widening
  public request signatures simply to move a two-byte token around.
- Update the AutoSave ensure comment: its HCNAMES read now accepts the
  extended grammar, but it does not yet make source values part of the v1
  hidden record.

What it does: makes source ownership visible at compile-time API boundaries.

Why it exists: callers otherwise see a function named “update names” and can
unintentionally create a name-only update that leaves stale provenance.

### `Core/Bank/Scene/SceneData.c` and `SceneData.h`

Remove, in one mechanical patch:

- the `static uint16_t scene_sources[SCENE_COUNT]` allocation and its 32-byte
  static assertion;
- `scene_resetSources()`;
- `scene_setSourceEncoded()`;
- `scene_setSourceLibrarySlot()`;
- `scene_setSourceBankSlot()`;
- `scene_sourceValue()`; and
- `SCENE_SOURCE_*` constants and all comments claiming provenance is a
  SceneData owner.

Remove the `scene_resetSources()` call from SceneData initialization.

What it does: makes filesystem HCNAMES the only retained provenance owner.

Why it exists: leaving this shadow array in place would retain two durable
answers after HCNAMES changes but before settings finishes writing.

### `Core/Bank/Scene/Preset/presetManager.c`

Remove `preset_setSceneSourcesFromMask()` and every use of it.  The affected
current callbacks are `on_scene_load_complete`, `on_scene_save_complete`, and
`on_bank_load_complete`; inspect the Kit and normal Instrument success paths
as well because those currently rely on HCNAMES publication without a general
source field.

For each callback:

- Remove only the SceneData provenance assignment and
  `filesystem_markSettingsDirty()` that existed solely to persist provenance.
- Retain `preset_markRequestedScenesPresentOnSuccessfulLoad()`, normal status
  completion, DSP apply ordering, and actual `settings.cfg` policy changes.
- Before starting/finishing the existing HCNAMES update, set source tokens
  using accepted request coordinates.  Root Scene Load sets selected Scene
  rows direct; root Scene Save does not change source unless product policy
  explicitly defines saving as a source change; Bank Load updates only its
  actual loaded-child mask; normal Instrument Load sets `@` on committed
  destination rows; Kit Load sets the Kit direct and child Instruments
  inherited.

If the existing callback runs *after* the HCNAMES rewrite, move only the
source staging into the filesystem operation before its rewrite rather than
reordering DSP apply or acknowledging a successful operation prematurely.

What it does: transfers source assignment to the actual successful operation
boundary.

Why it exists: Preset knows request semantics, but filesystem owns the one
durable metadata write.  Neither layer may publish a source after HCNAMES has
already closed.

### `Core/Bank/Scene/Preset/presetManager.h`

No new persistent storage or general public source API is required.  Update
only stale comments that say Scene provenance lives in settings/SceneData.
If a callback needs a source token that the current request accessors cannot
provide, add one read-only accessor for the already captured request slot/mask
instead of exporting Menu state.

What it does: preserves the current async request interface.

Why it exists: source provenance is metadata publication, not a new Menu
feature or a reason to make request state writable outside Preset.

### `Core/Bank/Scene/Autosave.c` and `Autosave.h`

This file is changed in two deliberately separated passes.

**Pass A, required by the HCNAMES extension:**

- Replace hard-coded/duplicated HCNAMES row arithmetic in initial-record
  formatting with named conversion helpers shared with later capture.
- Continue passing normalized HCNAMES names into
  `autosave_initialRecordByte()` / CRC / format functions.  Do not add source
  bytes to the shipped v1 record geometry in this pass.
- Update comments that call names “identity validation” and current Bank
  name/slot matching.  The settled future contract is that they are payload,
  not record-rejection identity.

**Pass B, implemented only with the Instrument-load plan:**

- Add RAM-only live getters for HCNAMES-derived Bank/Scene/Kit/Instrument name
  cells;
- add atomic eight-bit group markers; and
- add the first normal Instrument completion hook.

What it does: makes source/identity code share one row mapping without
combining the architecture migration with whole-object AutoSave publication.

Why it exists: the old failed Phase-2 branch mixed ownership and hooks.  The
source migration must be independently reviewable and hardware-tested first.

### `Core/Menu/menu.c` and `Core/Menu/menu.h`

No new Menu cache, source storage, or source-resolution logic is allowed.
Review and update comments at every HCNAMES session boundary to say that a
dirty row is an identity/source pair.  In particular, the combined Kit/
Instrument session's deferred exit update must preserve/set both values.

If Menu directly changes an identity row for a Save-name editor, it must not
guess or overwrite the corresponding source.  The source remains unchanged
for a save/rename unless the successful operation has an explicit direct-load
provenance rule.  Menu continues to call the existing async update request.

What it does: keeps the UI from becoming a second provenance owner.

Why it exists: Menu lifetime is transient and its selection may change while
an asynchronous filesystem operation is active.

### `main.c`

Do not add a second boot fallback implementation here.  Keep the existing
Bank-or-fallback ladder and its call to the normal HCNAMES bootstrap/update
path.  Replace any old `scene_resetSources()` initialization if present with
no operation.

The later AutoSave reader may call a filesystem-owned resolver only after the
baseline Bank/fallback load has made HCNAMES available.  `main.c` must not
parse HCNAMES or decide source inheritance itself.

What it does: preserves one source-of-truth fallback implementation.

Why it exists: two independently evolving boot ladders would make a missing
HCNAMES file behave differently during ordinary boot and AutoSave restore.

### `Core/Hardware/SD/filesystem.c` settings code

Remove the exact settings-schema pieces:

- `filesystem_settingsSceneSourceIndex()`;
- recognition/parsing of `scene_source_00` through `scene_source_15`;
- calls to `scene_setSourceEncoded()` during settings load; and
- writer lines 17–32 that emit `scene_source_NN` via `scene_sourceValue()`.

Adjust the settings line count/schema validation and its comments.  For
backward compatibility, a v1 settings file may contain the old keys; parse
and ignore them only during the migration release, then omit them on the next
successful settings write.  Do not fail a user’s existing settings file just
because it contains retired provenance data.

What it does: removes the old on-card provenance authority.

Why it exists: an HCNAMES source and a settings source could disagree after a
partial Bank load, precisely where the new hierarchy is meant to be reliable.

### Documentation and verification files

Update together after code is proven:

- `knowledge_files/specification_reference/FILESYSTEM_SPEC.md`: new HCNAMES
  grammar, source tokens, resolver, row propagation, missing/legacy behavior;
- `knowledge_files/specification_reference/AUTOSAVE.md`: atomic name groups,
  current writer limitation, future reader fallback semantics, and completion
  distinction;
- `knowledge_files/specification_reference/MODULE_INTERCHANGE_SPEC.md`: new
  filesystem source APIs and deletion of SceneData source APIs;
- `knowledge_files/specification_reference/SRAM_MANIFEST.md`: +258 B source
  cache, -32 B SceneData source array, net +226 B, owner/lifetime;
- `knowledge_files/specification_reference/DEV_MODES.md` only if a new test
  trace event is separately approved (none is part of this plan);
- `MEMORY.md`: remove settings provenance claims and record the new authority;
  and
- a new session handoff with exact hardware fixtures and compatibility result.

No Makefile or linker-script change is expected.  No new task, stack buffer,
DMA buffer, cache, or AsyncFATFS handle allocation is permitted.
