# Load-path repair plan

## Purpose

Repair the current Instrument and Kit Load paths without restoring the retired
SRAM filename/location caches. The fixes must keep `.names` as a resident
source-name register only: it is not a library index and must never be used to
locate an Instrument file or Kit directory.

The final loader memory rule is stricter than the earlier repair work: it may
retain only one current, eight-character display name. It may retain request
coordinates such as type, numeric slot, ordinal, destination, and dirty scope,
but it must not retain a directory listing, a multi-object name list, full
source stems, FAT aliases, or paths in SRAM.

This plan is based on the current worktree and
[`DOT_NAMES_REPAIR_WORK.md`](DOT_NAMES_REPAIR_WORK.md), not on the older
implementation plan alone.

## Execution status

- Instrument resolver, physical-order single-object Instrument Load, action
  gating, typed Kit-directory opening, parent-relative Kit payload reads, and
  callback-ordered cleanup: implemented and build-verified.
- Multi-domain deferred `.names` reconciliation and removal of the legacy
  Kit/Scene/Bank slot-name/alias arrays: intentionally still pending. The
  current firmware keeps those arrays for the existing Menu browser contract;
  removing them safely requires the separate asynchronous Menu coordinate/UI
  migration described in Phase 6 and must not be hidden inside this load-path
  repair.
- Real-card SD validation of the diagnostic matrix below: pending hardware
  test.

## Confirmed failures in the current code

### Instrument browser name is always blank

`filesystem_requestResolveInstrumentName()` stores `type`, `ordinal`, and the
generation **before** it calls `filesystem_start()`.

`filesystem_start()` then resets `op_instrument_load_type` to
`INSTRUMENT_TYPE_UNKNOWN` and clears the staged display buffer. The resolver
subsequently scans `/Instrument` for files of type `INSTRUMENT_TYPE_UNKNOWN`,
finds no candidates, and completes without publishing a matching generation.
`filesystem_instrumentName()` therefore returns eight spaces.

This is a request-lifetime ordering error, not an LCD padding problem.

There are two associated problems to correct while touching this path:

- The resolver shares `op_instrument_load_type` and the staged display field
  with the actual payload loader. A background name request must not overwrite
  or be interpreted as payload-load state.
- Resolver phase 6 queues `afatfs_fclose()` and immediately calls
  `filesystem_finish()`, without waiting for the close callback. The next
  filesystem request can then begin with a handle whose close has not been
  observed by the facade.

### Every Kit Load returns `ERR KDir07`

`ERR KDir07` is emitted only by `filesystem_loadKitDirectory_tick()` after:

1. the Kit browser scan marked the requested numeric slot present;
2. the loader opened and entered root `Kit/`; and
3. phase 6 requested the selected slot directory and phase 7 received a
   `NULL` handle.

The selected directory is currently opened with:

```c
afatfs_fopen(kit_slot_open_name[op_slot], "r", on_file_opened)
```

`afatfs_fopen()` opens an archive/file object. The selected object is a FAT
directory, so asyncfatfs' typed open policy rejects it and invokes the callback
with `NULL`. That is the direct cause of `KDir07`.

Changing that one call to a directory-aware API is necessary, but it is not the
whole repair. The current Kit loader still enters directories through mutable
global current-directory state and relies on `kit_slot_open_name`, a retained
location alias. The intended system must resolve the requested numbered Kit
directory from the real `Kit/` tree for the operation being performed.

## Invariants for the repair

- Menu owns the selected library coordinate: kind, numeric slot/ordinal, and
  load destination.
- The filesystem resolves that coordinate against the live FAT tree for each
  load. It must not infer a directory from `.names`.
- `.names` changes only after a payload has successfully committed to resident
  state. A failed parse, missing object, or cancelled request changes neither
  resident source names nor `.names`.
- A directory is always opened using a directory-aware asyncfatfs API;
  payload files are always opened using a file-aware API.
- Every accepted async open/close has exactly one callback-observed terminal
  state before the next handle-dependent phase begins.
- The only filesystem-facade name scratch is one printable eight-character
  current-object field plus its NUL terminator. It is flushed before every new
  name lookup, source-object open, or `.names` update. Instrument names are
  eight characters too; there is no 16-character Instrument stem cache.
- An Instrument name result is valid only for the exact `(type, ordinal,
  generation)` request that produced it. A previous payload-load stem must
  never be displayed as a browser-row name.
- No new resident arrays of library names, FAT aliases, paths, directory
  handles, object identities, or deferred name records may be introduced.
- A loader must never have a source object and `/.names` open concurrently.
  Source parsing completes and all source handles close before the deferred
  resident-name reconciliation opens `/.names`.
- A successful load creates an identity-refresh barrier. Menu may retain the
  coordinate needed to reconstruct that identity, but it must reject another
  Load or Save until the name has been refreshed from the live tree and the
  required `.names` update has completed.

## Phase 1 — Add narrow load diagnostics before restructuring

Add operation-local diagnostics that explain *which* typed open or real-tree
resolution failed. Keep the existing short display error format, but use
distinct locations for:

- root acquisition failure;
- `/Kit` open failure;
- requested numbered Kit directory absent;
- requested object found but not a directory;
- `kitset.kcg` missing/open failure;
- Kit member file missing/open failure;
- `/Instrument` open failure;
- Instrument ordinal absent;
- selected Instrument file open failure;
- parser/finalizer failure; and
- close or cleanup failure.

Do not overload `KDir07` for all possible Kit failures. It currently proves
only that the phase-6 request produced a null handle.

Expose enough diagnostic context for panel/hardware testing to distinguish a
bad slot from a typed-open rejection. The plan does not require verbose
filenames in error RAM or in the UI; a compact phase/reason code is sufficient.

## Phase 2 — Repair Instrument background name resolution

### 2.1 Separate resolver request state from payload-load state

Introduce dedicated resolver-only fields:

- requested instrument type;
- requested browser ordinal;
- request generation;
- completed generation;
- the one shared eight-character current-object display result; and
- an explicit result state (`pending`, `resolved`, `not found`, `error`).

Do not reuse `op_instrument_load_type`, `op_instrument_load_index`, or
`op_staged_instrument_display_name` for background UI resolution. The actual
payload loader does not keep a second name field: after a successful parse it
retains coordinates only and lets the identity-refresh barrier resolve the
eight-character name again after source handles are closed.

### 2.2 Capture fields after operation initialization

In `filesystem_requestResolveInstrumentName()`:

1. reject invalid type/ordinal and a busy filesystem;
2. call `filesystem_start()`;
3. only after it succeeds, capture the resolver request fields and clear the
   resolver result; and
4. start the resolver state machine.

This ordering avoids `filesystem_start()` erasing the request type. Apply the
same audit rule to every request API: fields reset by `filesystem_start()` must
be assigned only after the start succeeds.

### 2.3 Use the same real-tree ordering as actual Instrument Load

Keep a single-pass, cacheless ordinal behavior:

1. acquire an explicit root handle;
2. open `/Instrument` as a child directory of root;
3. scan one real FAT object at a time;
4. filter by Instrument extension/type;
5. count matching objects in the iterator's stable physical directory order;
6. when the requested ordinal is reached, copy its extension-free name into
   the single eight-character current-object field and stop scanning; and
7. discard the asyncfatfs iterator object after the open/close transition.

The browser's ordinal must use this same physical iterator order in the actual
Instrument loader. This is the deliberate trade-off required by the one-name
rule: globally sorting a list without retaining either a list or at least two
comparison keys is incompatible with the stated SRAM limit. The displayed name
is always the first eight printable characters of the extension-free filename;
characters after eight never enter facade SRAM.

### 2.4 Close handles correctly

Replace the resolver's current immediate-finish close path with explicit
`CLOSE_DIRECTORY` and `WAIT_CLOSE_DIRECTORY` phases. Clear the local handle
only after the close callback. Close the explicit root handle as well if the
resolver adopts the explicit-parent path.

Every early failure must take the same ordered cleanup route. Do not issue
`filesystem_finish()` while an accepted close operation is still pending.

### 2.5 Correct browser display behavior

Change `filesystem_instrumentName()` to return the one resolver display only when
all of these match:

- resolver state is `resolved`;
- requested type equals the caller's type;
- requested ordinal equals the caller's index; and
- completed generation equals the request generation.

Remove the fallback that returns a payload-load display name merely because
`browser_index == op_instrument_load_index`; it can show an old loaded file on
an unrelated browser row. Before resolution completes, return a space-padded
pending field, never a null-filled field and never stale text. Clear the one
name field before starting every resolver request so it cannot briefly expose
the preceding selected object.

### 2.6 Menu handoff

Retain the repaired interaction model: encoder movement changes selection and
queues one background resolver request; OK posts the actual payload load.

On type, source, row, page, or nested-Load exit changes, invalidate the visible
resolver generation, clear the one name field, and queue a fresh real-tree
lookup from the retained type and slot/ordinal. In the completion callback,
repaint only if the current menu state still refers to the completed request.

The Load/Save action gate remains closed while that lookup or a post-load
identity refresh is pending. The actual load then re-resolves the selected file
from the tree and does not depend on the UI-name resolver having completed
first. This is intentionally slower than a list cache, but preserves the SRAM
contract and prevents an action from using an unrefreshed identity.

## Phase 3 — Replace Kit Load's file-open path with real-tree directory resolution

### 3.1 Stop opening a Kit directory as a file

As the immediate correctness fix, remove the selected-directory
`afatfs_fopen()` call from Kit Load. A directory must be opened through
`afatfs_opendir`, `afatfs_opendir_lfn`, or preferably
`afatfs_openDirChild`.

This alone should eliminate the deterministic type mismatch behind `KDir07`,
but do not stop at that minimal change.

### 3.2 Make normal Kit Load parent-relative

Rewrite `filesystem_loadKitDirectory_tick()` around explicit handles:

1. acquire `root = afatfs_openRoot()`;
2. open `Kit` as an explicit child directory of root;
3. scan that live `Kit` directory for the requested three-digit numeric slot
   using `storage_parseNumberedFolder()`;
4. when the matching object is encountered, pass its exact display component
   directly into `afatfs_openDirChild(kit_parent, ...)`; do not copy its LFN or
   SFN alias into facade scratch;
5. receive the selected directory through a directory result callback;
6. open `kitset.kcg` with `afatfs_fopenChild(selected_kit_dir, ...)`;
7. open each `file=` Kit member with `afatfs_fopenChild(selected_kit_dir, ...)`;
8. close the member file, selected Kit directory, `Kit` directory, and root in
   reverse ownership order, waiting for every callback; and
9. commit the staged Kit only after all six members validate.

No `afatfs_chdir()` should be required for normal Kit Load after this phase.
This prevents a global current-directory transition from changing where a later
open resolves and makes the load compatible with asyncfatfs parent retention.

### 3.3 Resolve the numbered folder during the load

The authoritative locator is the numeric slot requested by Menu, not
`kit_slot_open_name` and not the eight-character browser label. While scanning
the real `Kit/` directory:

- accept only directory objects whose full display name parses as the selected
  slot;
- if multiple host-created folders claim the same slot, select the first one in
  physical iterator order rather than retaining candidate names for sorting;
- pass the current iterator object's component straight to the directory open,
  then discard it; and
- derive the eight-character Kit display field only while it is the current
  object. Flush it before any later source-object or `.names` operation.

This keeps on-card location resolution accurate even if a host computer has
changed short aliases, capitalization, or long folder names since the last
scan.

### 3.4 Treat Kit and Kit Morph consistently

`FS_INTERNAL_OP_LOAD_KIT` and `FS_INTERNAL_OP_LOAD_KIT_MORPH` share this
directory parser. Both must use the same root/Kit/slot/member open sequence.
Their only difference remains the final commit behavior:

- normal Kit Load commits the validated staged Kit to selected Scenes, marks
  coordinate-only identity refresh work, and defers resident Kit/Instrument
  `.names` updates until the user exits that Load type;
- Kit Morph Load exposes the staged data to Preset for morph projection and
  must not rename normal resident identities or update `.names`.

## Phase 4 — Verify and harden actual Instrument Load

The actual Instrument loader already begins the right way: explicit root,
explicit `/Instrument` child, real-tree ordinal selection, and parent-relative
file open. Audit and repair it in parallel with the browser resolver so it has
the same one-name and handle-lifetime rules.

Required checks:

- `filesystem_requestLoadInstrument()` must keep assigning its destination,
  type, and index after successful `filesystem_start()`.
- Invalid type, missing `/Instrument`, ordinal exhaustion, non-file object,
  open failure, parse failure, and close failure must have distinct terminal
  diagnostics and must leave live Scene data untouched.
- The selected object must be opened immediately by the exact display component
  in the current asyncfatfs iterator object, not by a copied long name, a
  truncated display stem, or a cached short alias.
- Successful parsing must retain only a coordinate-only identity-dirty
  descriptor for the post-load `.names` update. It must not retain a complete
  source stem or a second display name. A failed load must not publish a new
  source identity.
- Explicit root and `/Instrument` handles must be closed in all success and
  error paths before `filesystem_finish()` is called.

If the existing loader still fails after the resolver ordering repair, capture
the first structured child-open result before changing behavior. Do not add
fallback aliases or reintroduce a filename cache to mask a parent-lifetime or
typed-open failure.

## Phase 5 — Deferred `.names` reconciliation and single-name action gate

After every successful normal Load, set an `identity_refresh_required` state
containing only the coordinates needed to reconstruct the selected source:
load kind, source numeric slot/ordinal/type, destination slot or Scene mask,
and normal-versus-morph mode. This state contains no names, paths, aliases, or
object handles.

When the user exits the active Load type, perform the deferred reconciliation:

1. reject any new Load or Save while the barrier is active;
2. verify all payload source handles have closed;
3. flush the one eight-character current-object field;
4. resolve the source again from the real tree using the saved coordinates;
5. copy the required current name into that one field, one object at a time;
6. close the source object before opening `/.names`;
7. open `/.names`, update one associated resident record, close it, and flush
   the one name field; and
8. repeat real-tree resolution only when another record is required, then
   release the Load/Save action gate only after the final `.names` write is
   durable.

For a Kit, Scene, or Bank group update, iterate the relevant resident
coordinates one at a time. Reopen and parse the source tree as needed instead
of staging six or sixteen names. Slow reconciliation is acceptable; bounded
SRAM is the priority. Morph loads carry no identity-dirty descriptor and do not
update `.names`.

If the user changes the active Load type before a payload is loaded, use the
same barrier in its simpler refresh-only form: flush the one name field,
re-resolve the newly selected `(type, slot/ordinal)`, and keep Load/Save
actions disabled until that one name is current.

## Phase 6 — Remove the remaining location/name cache dependency

The current root Kit/Scene/Bank arrays retain `present`, eight-character
labels, and short aliases for up to 1,000 slots per domain. For Kit alone this
is approximately 23 KB before alignment (`1000 × (1 + 9 + 13)`). It conflicts
with the project decision that slot lists do not retain names and that location
comes from the live tree.

After the repaired load paths pass hardware tests:

1. make Menu increment a numeric slot without requiring a cached label;
2. resolve the one displayed name asynchronously from the selected root tree;
3. make load operations resolve the selected numeric slot again as described
   above; and
4. remove `kit_slot_name`, `kit_slot_open_name`, and equivalent Scene/Bank
   library-name and alias caches once their callers have been migrated.

Keep only a numeric type/slot/ordinal coordinate, one eight-character current
name field, bounded asyncfatfs iterator state for the object being examined,
and `.names` records for the 129 resident source identities. `.names` does not
replace directory scanning for a library slot.

Reduce any `.names` Instrument record and all facade name interfaces to eight
printable characters plus a terminator. Migration of an existing register is
performed through the same copy-on-write format validation path: read one old
record, keep its first eight printable characters, write the new record, and
never allocate a full old/new name table.

This cache-removal phase is deliberately after functional load repair so it
does not obscure the current `KDir07` root cause.

## Required code documentation

For every C or H change made under this plan, add the required change-adjacent
comment blocks. Comments must document what changed, why it exists, inputs,
outputs, ownership/lifetime, and affiliated state machines or callers. Add
separate comments around important scan loops, generation comparisons, slot
math, retained handles, and cleanup transitions; a function-header comment is
not enough.

In particular, document:

- why a directory uses `openDirChild` rather than `fopen`;
- why selected folder identity is consumed directly from the current iterator
  object and not cached;
- why resolver request fields are assigned after `filesystem_start()`;
- why generation matching prevents stale LCD names; and
- why the action gate waits for deferred identity refresh;
- why a source object is closed before `/.names` is opened; and
- which handle owns each close phase.

## Verification matrix

Run these tests on a real SD card as well as a build.

| Test | Expected result |
| --- | --- |
| Scroll an Instrument pool with several files of one type | The correct padded, first-eight-character name appears after background resolution; encoder stays responsive. |
| Scroll quickly, change type, then leave the page | No stale name appears after generation/page change; no handle leak or busy state remains. |
| Press OK before a name resolves | Actual Instrument Load resolves the selected real-tree ordinal itself and completes or returns a specific error. |
| Load two Instrument files with distinct parameter values | Only the selected Scene/voice receives the parsed staged data; source name changes only after commit. |
| Load a valid `Kit/NNN Long Name/` folder | No `KDir07`; `kitset.kcg` and all members open through the selected directory. |
| Load a Kit whose folder has an LFN and generated `~n` alias | The real full folder name is selected by numeric slot; alias changes do not affect load. |
| Load slot 000 and a high numbered slot | Both use exact numeric-slot matching; 000 is not treated as empty. |
| Missing Kit slot / wrong-kind object / missing kitset / missing member | Load fails with the corresponding specific diagnostic; resident Kit and `.names` stay unchanged. |
| Kit Load to multiple Scene targets | All requested targets change only after the complete Kit validates. |
| Kit Morph Load | Morph projection works but does not rename resident normal Kit/Instrument identities. |
| Save a Kit, rescan/reboot, then load it | The saved directory name is found from the actual tree and the Kit loads successfully. |
| Power interruption or I/O failure during a load | No partially parsed Kit or Instrument is committed; `.names` retains the prior valid source names. |
| Load succeeds, then user exits its Load type | Actions stay gated until source is re-resolved, source handles close, and the one-at-a-time `.names` reconciliation is durable. |
| Change Load type without loading | The one name field is cleared and refreshed from the new type/slot before Load or Save can be accepted. |

Build with `make -j10` after each coherent phase. Record hardware results,
diagnostic codes, and any change to SRAM/BSS in this document and in the
relevant asyncfatfs implementation notes.

## Completion criteria

The repair is complete only when:

- Instrument pool names appear as eight-character fields for the selected
  `(type, ordinal)` and no stale loaded name can be displayed;
- Instrument Load completes successfully from the real `/Instrument` tree;
- valid Kit Load no longer returns `ERR KDir07` and loads its full directory
  payload;
- Kit Save followed by rescan/reboot/load works with LFN directories;
- all error exits close their accepted handles and preserve resident payload
  and `.names`;
- no source object and `/.names` handle are open concurrently; and
- the implementation has not reintroduced a list, multi-name staging area,
  long-name/stem cache, path, or short-alias cache as a workaround.

## Implementation work log

### 2026-07-18 — resolver and typed-open slice

- Established a clean forced ARM build before the slice: `text=374176`,
  `data=416`, `bss=342992`; the existing warning set was limited to known
  unused legacy helpers/variables and toolchain/library warnings.
- Added dedicated Instrument resolver type state and moved resolver request
  capture after `filesystem_start()`. This prevents the initializer from
  erasing the requested type and producing a permanently blank browser name.
- Replaced resolver candidate/previous long-name scratch with a physical-order
  one-object scan and one eight-character display result. The resolver now
  waits for its directory close callback before restoring root and completing.
- Changed actual Instrument selection to open the current iterator object
  directly, with a retry phase that cannot skip an ordinal when asyncfatfs is
  temporarily busy. The facade no longer copies a candidate list or a long
  alias for this load.
- Changed Kit Load's selected-folder open from file-typed `afatfs_fopen()` to
  directory-typed `afatfs_openDirChild()`. This addresses the confirmed
  `ERR KDir07` type mismatch; the later parent-relative metadata/member opens
  are recorded in the next log entry, while cache removal remains pending.
- Added a Menu name-refresh action gate so an OK Load/Save action cannot overlap
  a pending one-name resolver request.
- The forced ARM build after these changes completed successfully at
  `text=373808`, `data=416`, `bss=342616`. Hardware SD/load validation is still
  required before marking the load path complete.

### 2026-07-18 — parent-relative Kit Load and alias-compatible reopen

- Replaced Kit Load's mutable-current-directory sequence with retained parent
  handles: root → `Kit/` → selected Kit directory. The selected directory is
  now opened with `afatfs_openDirChild()`, so a file cannot be accepted as a
  Kit folder and the historical `KDir07` path is no longer the normal result
  for a valid directory.
- Opened `kitset.kcg` and each member Instrument with
  `afatfs_fopenChild()` relative to the selected Kit directory. Every metadata
  or member-open failure now enters the existing close path before finishing;
  no parser is called on a failed callback handle.
- Added ordered close phases for selected Kit, `Kit/`, and root handles. This
  prevents a retained parent from being released before its child and makes
  the final `filesystem_finish()` occur only after all callbacks have fired.
- Extended asyncfatfs LFN matching to accept the physical SFN alias as a
  fallback when a valid LFN is present. The live object iterator exposes both
  spellings, and the Kit scan currently retains the alias as a transitional
  reopen key; without this fallback an LFN directory listed correctly but
  could still fail when selected.
- Added a null-parent cleanup branch to root Instrument Load. If opening
  `/Instrument` fails, the loader now skips the impossible directory close
  callback and releases only the explicit root handle instead of hanging.
- The final incremental ARM build after the null-parent/lease reset and
  result-specific diagnostic cleanup completed successfully at `text=374288`,
  `data=416`, `bss=342608`. Remaining
  work is the planned removal of the
  multi-slot Kit/Scene/Bank caches and deferred `.names` reconciliation; those
  are intentionally not claimed complete by this load-repair slice.

### 2026-07-18 — hardware-reported retry slice

- Kit Load no longer trusts `kit_slot_open_name[slot]` for the selected folder.
  It scans the retained `Kit/` parent for the requested numeric prefix, keeps
  only the current iterator object, copies one eight-character display name,
  and then opens that exact live display component with the typed directory
  API. This directly targets the remaining KDir07 report, including cards that
  expose only an SFN-style display component.
- Nested Instrument Load now queues a resolver refresh when entering the page,
  after every type change, and after every encoder ordinal change. The LCD
  field is blanked while that request is pending, so a prior `[  1]` name
  cannot remain visible for a later coordinate.
- Instrument payload open now retries the same live object through its SFN
  alias after a failed display/LFN open, without retaining a list or path.
- The new build completes successfully at `text=374848`, `data=416`,
  `bss=342616`. The fixes still require SD-card retest of Kit Load, Instrument
  Load, and rapid encoder movement; firmware-only compilation cannot prove the
  card's exact LFN/SFN directory layout.
