# New asyncfatfs Menu Wiring And `.names` Resident-Identity Plan

Status: draft implementation plan.

Updated: 2026-07-18.

## Implementation Log

### 2026-07-18 — execution started

- Confirmed the pre-wiring tree builds successfully with
  `text=367752`, `data=408`, and `bss=364908` bytes.
- Confirmed the existing worktree already contains the completed asyncfatfs
  Phase 5/6 implementation and documentation; those changes are preserved.
- Confirmed the current Instrument path still depends on the four
  `instrument_file_*` scan arrays, numbered Kit/Scene/Bank paths still depend
  on their 1,000-slot arrays, and boot performs four blocking cache-population
  scans.
- Began Phase B with the rule that `.names` contains only the 129 resident
  source identities. Library scans will never populate it.
- Added `afatfs_openRoot()`, which allocates an independent FAT16/FAT32 root
  handle without changing the legacy global working directory. The ARM build
  passes after the API addition.
- Added `namesRegister.c/.h` and the Makefile entry. The implemented format uses
  alternating 32-byte CRC headers and two 129-record banks; update copying uses
  one 32-byte record buffer and rewrites every record generation/CRC before the
  inactive bank is synced and published.
- Added pre-audio `.names` validation/creation to boot. The first linked build
  with the register engine reports `text=371464`, `data=412`, `bss=365076`;
  compared with the execution baseline this is +3,712 flash text, +4 data, and
  +168 BSS before any old name cache is removed.
- Removed the 19,968-byte Instrument display/SFN/stem arrays. Instrument Load
  now acquires an explicit root handle, opens `/Instrument` parent-relative,
  resolves `(type, ordinal)` by repeated full-directory minimum selection, and
  opens the selected component through `afatfs_fopenChild()` without `chdir` or
  a cached alias. Same-folded case variants occupy one resolver ordinal.
- Corrected `.names` sanitization after implementation review: periods remain
  valid Kit/Scene/Bank name characters; only the Instrument-domain caller owns
  removal of a filename extension.
- The first cache-removal build passes with `text=371056`, `data=412`, and
  `bss=345388`. Relative to the original execution baseline, linked BSS is now
  19,520 bytes lower even after adding the 168-byte register engine and bounded
  resolver scratch. The remaining four one-byte Instrument counts are an
  interim Menu compatibility bridge and are not used to authorize Load.
- Temporarily disabled the synchronous cache-backed Instrument overwrite hint:
  it now returns false rather than claim stale `OW`. Phase C must replace this
  compatibility point with the planned generation-tagged real-tree probe.

## 1. Objective

This plan has two connected goals:

1. Remove every persistent SRAM name/list cache from the product storage and
   Menu paths.
2. Convert root Instrument Load and Save into the first testable vertical slice
   that uses the new parent-relative asyncfatfs APIs and the SD-root `.names`
   resident-identity register.

The Instrument slice must be completed and tested before Kit, Scene, and Bank
are migrated. It proves the one-name-at-a-time Menu flow, real-tree resolution,
resident-source-name updates, explicit directory ownership, and recovery-safe
Save behavior without requiring the entire Load/Save Menu to change at once.

## 2. Correct Meaning Of `.names`

`.names` is **not a filesystem index or browser cache**. It records only the
names of objects from which the current resident musical state was sourced.

It must never store:

- a root Kit, Scene, or Bank library slot number;
- an Instrument browser ordinal;
- a directory path;
- a parent cluster;
- an `afatfsObjectId_t`;
- a FAT SFN/open alias;
- an extension or Instrument type;
- an existence/presence bitmap for library objects;
- a count or sorted representation of files in a library directory.

All location and directory selection comes from current Menu request state:

- Kit, Scene, and Bank use the selected numeric slot and their known root
  parent;
- Bank-local Scenes use the selected two-digit Scene coordinate and selected
  Bank directory;
- Instruments use the selected Instrument type and current Menu ordinal;
- embedded Kit/Scene children use the component names read from their actual
  storage schemas and directory scans.

Every operation resolves those coordinates against the real FAT tree. `.names`
is used only to seed Save name editing and to display the source identity of
resident Scene/Kit/Instrument/Bank state.

## 3. Definition Of A Stored Name

The register stores names in the existing resident-name domains:

| Domain | Stored text | Capacity |
| --- | --- | ---: |
| Instrument | Filename stem with the extension removed. Retain up to `SCENE_INSTRUMENT_STEM_LEN` (currently 16) printable bytes. | 16 Scenes x 6 slots = 96 |
| Kit | Folder display component after removing a leading `NNN ` for a root Kit or `Kit ` for an embedded Scene Kit. | 16 |
| Scene | Folder display component after removing a leading root `NNN ` or Bank-local `NN `. | 16 |
| Bank | Folder display component after removing the leading `NNN `. | 1 |
| **Total** | Resident source-name cells only. | **129** |

The first eight printable characters of an Instrument stem are sufficient for
the current LCD. The longer 16-byte stem remains necessary when a later Kit Save
regenerates member filenames. The LCD representation must be derived from the
stored stem; `.names` must not store a duplicate eight-character Instrument
display cache.

### 3.1 Update interpretation

The operation rules are:

- normal Instrument Load or Save introduces at most one distinct Instrument
  source name. If the current Scene-mask fan-out writes the payload into more
  than one resident Scene, that same name is written to each affected resident
  Instrument cell in one snapshot update;
- Kit operations update the source identities associated with that Kit,
  including up to six child Instrument stems;
- Scene operations update the source identities associated with that Scene,
  including up to six child Instrument stems;
- Bank operations may replace the complete resident identity snapshot;
- Morph Load/Save operations do not change an object's sourced-from identity
  when they update or export only a Morph endpoint.

The statement that Kit or Scene can change up to six names is treated as the
six child Instrument source names. The enclosing Kit or Scene source-name cell
must also be updated when that enclosing resident object is loaded or saved;
otherwise the 16 Kit and 16 Scene cells could not remain authoritative. Code
and tests should call this out as one container-name write plus zero-to-six
child-name writes rather than hiding the distinction in a raw byte count.

The phrase "one Instrument name" therefore counts distinct source identity,
not the number of resident cells receiving it. The current multi-Scene edit
mask may remain functional, but every Scene/slot whose payload is committed must
receive that same stem in `.names`; no unselected resident cell may change.

## 4. Ownership And Layering

### 4.1 asyncfatfs remains generic

`Core/Hardware/SD/asyncfatfs/asyncfatfs.c` and `.h` continue to know only FAT
objects, explicit parent handles, file bytes, tree operations, and structured
result codes. They must not know what an Instrument, Kit, Scene, Bank, Menu
slot, or `.names` register cell means.

If implementation exposes a missing generic primitive, add only the minimal
parent-relative/object-identity API. Every `.c` and `.h` change must retain the
project requirement for detailed adjacent comments explaining what the change
does, why it exists, inputs, outputs, ownership/lifetime, important loops and
indices, arithmetic/offset calculations, and affiliated code.

The child APIs still need an initial root handle. Add a generic synchronous
root-handle acquisition API because materializing mounted root geometry needs
no SD I/O and must not mutate `currentDirectory`:

```c
afatfsDirHandle_t afatfs_openRoot(void);
```

It returns an ordinary caller-owned directory handle for either a FAT16 fixed
root or FAT32 root cluster. This is a generic asyncfatfs capability, not product
location storage. After this bootstrap, every Instrument child open is relative
to an explicit parent.

### 4.2 Product register module

Add:

- `Core/Hardware/SD/namesRegister.h`
- `Core/Hardware/SD/namesRegister.c`

and add the new C file to the project `Makefile` beside `filesystem.c` and
`storageTypes.c`.

`namesRegister` owns:

- the serialized `.names` format;
- conversion from `(domain, resident Scene, Instrument slot)` to a fixed file
  offset;
- validation, initialization, read, single-record update, and full Bank
  snapshot update;
- the one operation-local record/name buffer;
- generation/checksum handling and deterministic default names.

`filesystem.c` owns:

- opening root `/.names` through asyncfatfs;
- pumping register I/O as part of its existing serialized async operation
  facade;
- real-tree resolution of Menu coordinates;
- ordering payload durability, resident commit, and source-name updates.

Menu and Preset must not open `.names` or inspect its byte layout directly.

## 5. Proposed `.names` Format

Use a fixed-offset, versioned file so one resident name can be read without
loading any other name.

Recommended record addressing:

```text
Instrument record = scene_index * 6 + instrument_slot     (0..95)
Kit record        = 96 + scene_index                       (96..111)
Scene record      = 112 + scene_index                     (112..127)
Bank record       = 128                                   (128)
```

Use two 512-byte header slots and two complete record banks. Each bank contains
129 fixed 32-byte records (4,128 bytes) and 480 bytes of padding, for a
sector-aligned 4,608-byte bank. The complete file is therefore 10,240 bytes:

```text
sector 0             header slot A (512 bytes)
sector 1             header slot B (512 bytes)
sectors 2..10        record bank A (4,608 bytes)
sectors 11..19       record bank B (4,608 bytes)
```

SD cost and slower whole-bank updates are intentional tradeoffs for bounded
SRAM and atomic multi-name snapshots. Only one 32-byte record buffer is needed
while copying or updating a bank.

### 5.1 Header

The header should contain:

- magic such as `LXRNAMES`;
- format version;
- header and record sizes;
- record count (`129`);
- monotonically increasing committed sequence/generation;
- selected record-bank number;
- selected bank CRC32 and exact serialized byte count;
- compile-time domain counts and maximum name lengths;
- header CRC32;
- reserved bytes zeroed for future format extension.

On mount, validate both header slots and the bank each header selects. The
highest-sequence header whose own CRC and complete bank CRC validate is current.
A torn newer header or incomplete inactive bank is ignored in favor of the
older complete snapshot.

### 5.2 Record

Serialize fields by explicit byte offset rather than writing a compiler C
structure directly:

```text
byte 0      valid/state
byte 1      domain
byte 2      resident key within that domain
byte 3      source-name length
bytes 4-20  printable name bytes plus room for NUL (17 bytes)
bytes 21-23 reserved, written as zero
bytes 24-27 generation
bytes 28-31 record CRC32
```

For Kit, Scene, and Bank, length is at most eight. For Instrument it is at most
16. A record contains no extension, type, slot location, path, or FAT identity.
The resident key is only the fixed address of a RAM-equivalent source-name cell
(Scene index or Scene/voice index); it is never an on-card library coordinate.

### 5.3 Initialization and corruption policy

On mount:

1. Open the real root and then `.names` using `afatfs_fopenChild()`.
2. Validate both header slots and their selected banks as described above.
3. If no complete snapshot exists, create/truncate the file, initialize bank A
   to deterministic defaults one record at a time, sync it, write header A with
   sequence 1 pointing to bank A, then sync the header.
4. Validate each record independently when read. A bad individual record uses
   a deterministic fallback and triggers a fresh snapshot update without
   treating the musical library tree as a source for resident identity.

Do **not** rebuild `.names` by scanning `/Instrument`, `/Kit`, `/Scene`, or
`/Bank`. Those trees describe library objects, not which objects currently
inhabit resident Scene memory. At boot, normal Bank/Scene/Kit fallback loading
will overwrite the corresponding resident-source records as payloads are
successfully committed.

Recommended fallbacks are `inst_voN` for Instrument, `Kit`, `Scene NN`, and
`none` for Bank. They are identity-loss fallbacks only; they must never be used
to locate a library object.

### 5.4 Update durability

Every update, even a one-name Instrument update, uses copy-on-write:

1. choose the bank not referenced by the current highest valid header;
2. stream all 129 records from the active bank to the inactive bank one record
   at a time;
3. substitute the changed Instrument, Kit/Scene group, or full Bank records as
   their fixed offsets pass through the loop, and rewrite every copied record
   with the new generation and a new record CRC;
4. write until all bytes are accepted, treating a zero-byte write as WAIT;
5. compute record and complete-bank CRCs while streaming;
6. sync the complete inactive bank;
7. write the older/invalid header slot with the active header sequence plus one,
   the inactive bank number, and its CRC;
8. sync that one header sector before reporting the update durable.

A power loss before the header commit leaves the previous bank authoritative.
A power loss after the synced header commit selects the complete new bank.
Never overwrite the active bank or newest valid header first. The implementation
may be slow, but it must use one record buffer and must not add a 129-record SRAM
mirror.

## 6. Eliminate Library Name And Location Caches

The following current arrays must be deleted rather than repurposed:

- `kit_slot_present[1000]`;
- `kit_slot_name[1000][9]`;
- `kit_slot_open_name[1000][13]`;
- `scene_slot_present[1000]`;
- `scene_slot_name[1000][9]`;
- `scene_slot_open_name[1000][13]`;
- `bank_slot_present[1000]`;
- `bank_slot_name[1000][9]`;
- `bank_slot_open_name[1000][13]`;
- `instrument_file_count[4]` as a persistent scan result;
- `instrument_file_name[4][128][9]`;
- `instrument_file_open_name[4][128][13]`;
- `instrument_file_stem[4][128][17]`;
- `kb_map[1000]`, `kb_numKits`, and the compatibility writer hooks in
  `filesystem.c`;
- Bank child `op_bank_child_name[16][9]` and
  `op_bank_child_open_name[16][13]`;
- Dev File/Dir list-name arrays when Dev Mode is enabled as well as disabled.

`op_bank_child_present[16]` should become only the existing 16-bit child mask.
It is state, not a name/location cache. A count or current ordinal may likewise
remain while a Menu page is active, but it must be recomputed from the real
directory and must not retain object names or aliases.

### 6.1 What may remain in SRAM

The cache-removal rule does not prohibit bounded working text needed to perform
one operation. The following are valid when their lifetime is explicit:

- one current Menu name display/editor buffer;
- one currently resolved `afatfsObjectInfo_t` while its parent remains open;
- one current `.names` record buffer;
- one immutable request target component copied at request acceptance;
- one parser line or writer line;
- bounded delete/copy traversal names already required by asyncfatfs;
- flash-resident constant UI label tables.

These are transient operation state, not multi-object identity caches. Clear or
overwrite them when the operation/page ends, and never expose a borrowed pointer
whose backing buffer can be changed by a later callback.

### 6.2 Legacy Kit browser

Repository inspection shows `kitBrowser.c` owns the 2,000-byte `kb_map` and is
not the primary current Load/Save Menu path. Remove `kitBrowser.c/.h` and its
Makefile source entry if no external build target calls it. If it is still
required by a hidden product surface, rewrite it as a direct numeric slot
selector using the same one-slot resolver as Menu; do not retain the map.

### 6.3 Resident struct names

After `.names` has proven all four resident domains, remove:

- `scene_t.display_name[9]`;
- `kit_t.display_name[9]`;
- `kit_t.instrument_display_name[6][9]`;
- `kit_t.instrument_stem[6][17]`;
- `BankData.c`'s `bank_display_name[9]`.

Replace SceneData/BankData name getters and setters with asynchronous
register-facing Preset/filesystem requests. Do not make DSP or SceneData depend
on SD I/O; Menu asks for a resident name only when it needs to paint or seed an
editor.

`preset_currentName` must cease being persistent identity. Move/rename it as a
Menu-owned eight-character edit/display buffer, fill it from one `.names` read,
and copy it into immutable Save request scratch on confirmation.

### 6.4 Other name caches

- Remove `sample_name_cache` from SRAM. Sample identity originates in sample
  flash, not SD resident-source storage, so read one sample name from the flash
  metadata when displayed; do not put sample locations in `.names`.
- Replace Dev File/Dir list arrays with an on-demand Nth-object scan using one
  object candidate. Compile the entire diagnostic implementation out under
  `CONFIG_DEV_MODE=0`.
- Retain operation-local filename stacks only where recursive traversal needs
  them. They are bounded algorithm state and should be unioned when lifetimes
  are mutually exclusive.

## 7. Real-Tree Menu Resolution Without Caches

Add a product-level resolver state machine in `filesystem.c`. It receives a
Menu coordinate, opens the known parent, scans actual objects with
`afatfs_findFirstObject()` / `afatfs_findNextObject()`, and returns one current
component plus existence/type status.

### 7.1 Numbered Kit, Scene, and Bank slots

For a selected `000..999` slot:

1. Open the real root handle.
2. Open the known `Kit`, `Scene`, or `Bank` child with
   `afatfs_openDirChild()`.
3. Scan objects and parse each public display component with
   `storage_parseNumberedFolder()`.
4. Keep only one best matching candidate for the requested number, using the
   existing casefold-then-case duplicate policy.
5. Return the stripped display name for the LCD, but keep the complete
   callback-scoped object identity only for the operation that immediately
   consumes it.
6. Close every handle on success, empty slot, error, or cancellation.

Encoder movement changes the numeric value immediately. It increments a Menu
request generation and posts/coalesces one resolver request. Until completion,
show the new number and `Loading` rather than the previous slot's name. A stale
completion whose generation or coordinate no longer matches is discarded.

Existence, overwrite display, first-slot boot fallback, and actual Load must all
use a real scan. They must not add a presence bitmap as a shortcut.

### 7.2 Instrument type and ordinal

Instrument Menu location is `(instrument_type, ordinal)`. Since no sorted list
may be cached, resolve it with bounded repeated directory passes:

1. Scan `/Instrument` and count files matching the selected registered type.
2. To resolve ordinal `k`, repeatedly select the lexicographically lowest
   same-type object greater than the previously selected key until the kth
   object is reached. Keep only the previous key and current best candidate.
3. Apply the existing case-insensitive duplicate rule during selection so case
   variants occupy one Menu ordinal.
4. Derive the LCD name by removing the actual file extension from the selected
   `displayName`.
5. Coalesce rapid encoder movement and restart from the latest requested
   ordinal. Slow browsing is acceptable; stale or incorrect object selection
   is not.

A later performance optimization may use a different bounded selection
algorithm, but it may not create an SRAM name/alias array or put library
locations into `.names`.

### 7.3 Load-time re-resolution

The Menu's displayed resolution is not an authorization token. When OK or an
instant-load action starts, copy only the Menu coordinate into the request and
repeat the real-tree scan inside that operation. This protects against host
edits, a previous mutation, or an object disappearing between display and use.

## 8. Instrument Vertical Slice

This is the first implementation milestone the user can exercise from the
panel.

### 8.1 Public API changes

Replace cache-index contracts with Menu-coordinate contracts:

```c
bool filesystem_requestResolveInstrumentName(instrument_type_t type,
                                             uint16_t ordinal,
                                             uint32_t generation,
                                             fs_completion_cb_t cb);

bool filesystem_requestLoadInstrument(uint16_t destination_scene_mask,
                                      uint8_t destination_slot,
                                      instrument_type_t type,
                                      uint16_t ordinal,
                                      fs_completion_cb_t cb);
```

Provide accessors that copy, rather than expose indefinitely borrowed cache
pointers:

```c
uint16_t filesystem_resolvedInstrumentCount(void);
uint8_t filesystem_copyResolvedName(char *dst, uint8_t capacity,
                                    uint32_t *generation_out);
```

Keep Save input as an edited stem copied at request acceptance. Replace
`filesystem_instrumentTargetExists()` with an asynchronous real-tree target
probe; the returned result is tagged with the editor generation so `OW` cannot
describe an earlier spelling.

Update `presetManager.c/.h` and `menu.c` to use `uint16_t` ordinals even if the
current UI still saturates visible numbering at 999.

### 8.2 Instrument Load using explicit parents

Rewrite `filesystem_loadInstrument_tick()` so it never depends on current
directory, cached aliases, or `.names` for location:

1. Open a root directory handle.
2. Open `/Instrument` through `afatfs_openDirChild(root, "Instrument", ...)`.
3. Resolve the requested `(type, ordinal)` in that open parent using the real
   object iterator.
4. Copy the selected filename stem without extension into one operation-local
   17-byte buffer.
5. Open the selected file with
   `afatfs_fopenChild(instrument_parent, selected_display_component, "r",
   AFATFS_OPEN_EXISTING, AFATFS_MATCH_CASE_SENSITIVE, ...)`.
6. Parse into `op_staged_instrument` exactly as today. Zero-byte reads remain
   WAIT unless `afatfs_feof()` is also true.
7. Close the file, Instrument parent, and root in deterministic reverse order.
8. Preset commits the validated payload to every selected valid resident
   Scene/slot and applies runtime state in the existing safe order.
9. After the payload commit, write the one resolved stem to exactly those
   resident Instrument `.names` records as one snapshot update and sync it.
10. Only then report the complete Load result to Menu.

If the actual object disappears or changes during resolution/open, report a
structured stale/not-found error and leave both resident payload and `.names`
unchanged. Normal Instrument Morph Load retains the existing source-name record
because it does not replace the resident Instrument identity.

### 8.3 Instrument Save using Phase 6 tree replacement

Use the new recoverable tree operation instead of deleting/truncating one live
Instrument file in place. Slow copying is acceptable and avoids a power-loss
window that destroys the old file before the replacement is durable.

Save flow:

1. Copy the edited stem, source Scene/slot, source type, and Normal/Morph mode
   into immutable request state.
2. Open the real root parent and run `afatfs_recoverTreeReplace(root, ...)`
   before any Instrument browser or Save mutation. Handle `RECOVERY_REQUIRED`
   explicitly; do not guess which scratch tree is valid.
3. Resolve the current `/Instrument` directory from the real root. It may be
   absent for the first save.
4. Call `afatfs_beginTreeReplace(root, "Instrument", ...)` to obtain a
   guaranteed-new staging directory.
5. If a live `/Instrument` exists, scan its children one at a time. Copy every
   unknown file/directory and every non-target Instrument object into staging
   with `afatfs_copyObjectTree()`. Skip every file whose actual display
   component is the case-insensitive same-type target being overwritten.
6. Create the new target in the staging directory with
   `afatfs_fopenChild(staging, target_component, "w", AFATFS_CREATE_NEW,
   AFATFS_MATCH_CASE_SENSITIVE, ...)`.
7. Stream the current Instrument text using the existing line formatter. Close
   the target and all source children.
8. Close the old Instrument directory before promotion.
9. Call `afatfs_commitTreeReplace()`. Its stage sync, journal sequence,
   promotion, old-tree cleanup, and final sync are the Save durability boundary.
10. After commit succeeds, update the one resident Instrument `.names` record
    for a normal Save and sync it. Morph Save leaves the source identity alone.
11. Re-resolve the current Menu selection from the real tree for repaint; do
    not update a deleted Instrument cache.

If any staging copy or write fails, call `afatfs_abortTreeReplace()` and retain
the live Instrument directory. A failed `.names` update after a successful tree
commit is a register error, not proof that the Instrument file failed to save;
show a distinct repair warning and retry the fixed-record update. On reboot, a
normal product load can repopulate the resident identity.

This copy-all-children policy preserves host-created unknown objects and
subdirectories. It deliberately trades Save latency for bounded SRAM and
recoverable replacement.

### 8.4 Menu behavior for the test slice

Instrument Load:

- entering the nested Instrument page scans the selected type for a count;
- the retained resident source name comes from the appropriate `.names` cell;
- moving onto the pool row changes the ordinal immediately and asynchronously
  resolves the real tree;
- `Loading` is displayed until the current generation resolves;
- only the current resolved name is held for repaint;
- OK/instant selection posts a fresh coordinate-based load, not a filename;
- successful normal Load repaints from the updated `.names` cell.

Instrument Save:

- seed the editor by reading the selected resident Instrument `.names` cell;
- derive the extension only from the resident Instrument type;
- run an async real-tree target probe for `OW` as characters change;
- confirmation rechecks the target during tree reconstruction regardless of
  the displayed `OW` result;
- normal Save updates the source name after commit; Morph Save does not.

The Menu must remain responsive during repeated scans and tree copy. Encoder
events only update desired state/generation while filesystem work is busy.

## 9. Kit, Scene, And Bank Migration After Instrument Test

### 9.1 Kit

- Replace `filesystem_kitSlotExists/Name/firstKitSlot` with async direct-slot
  resolution against `/Kit`.
- Root Kit Load resolves `NNN ` from Menu, loads the actual directory, strips
  the folder prefix for the resident Kit record, and extracts each actual
  member filename stem into the six Instrument records.
- Kit Save reads the Kit and six Instrument resident names one at a time from
  `.names`, builds its staging tree, commits with `afatfs` replace, then updates
  the Kit group only after success.
- `KitMrp` continues to leave resident source identities unchanged.

### 9.2 Scene

- Replace Scene slot caches with async direct-slot resolution against `/Scene`.
- Root Scene and Bank-local Scene folders use their respective `NNN ` and `NN `
  stripping rules only after a real directory object is selected.
- Scene Load/Save updates the resident Scene name, embedded Kit name, and up to
  six member Instrument stems as one generation-controlled group.
- Scene payload staging remains separate from `.names`; do not remove
  `op_staged_scene` as part of this wiring project.

### 9.3 Bank

- Replace Bank slot caches with direct `/Bank` resolution.
- Bank preview scans retain only a 16-bit child-present mask. Child names and
  open aliases are resolved again when a child is actually loaded.
- Successful Bank Load/Save writes the Bank name and the complete set of
  resident Scene/Kit/Instrument source names as one `.names` snapshot
  generation.
- An empty Bank writes the Bank identity and invalid/default records for
  resident objects it does not supply according to the fallback policy.

## 10. Boot And Recovery Order

Replace the four blocking boot cache scans in `main.c` with a bounded boot
coordinator:

1. mount asyncfatfs;
2. open root and recover known replace transactions before using product trees;
3. validate/create `.names` without scanning library trees into it;
4. resolve the configured/default Bank slot directly;
5. if absent or empty, scan only for the lowest root Scene slot;
6. if absent, scan only for the lowest root Kit slot;
7. otherwise use resident defaults;
8. as each payload commits, update the relevant `.names` resident records;
9. start audio only after the chosen initial payload and required source-name
   updates finish or a visible fallback/error policy is selected.

Finding the lowest slot needs one scan and one integer best-slot candidate, not
a 1,000-entry presence array.

## 11. Development-Mode Gating

Under `CONFIG_DEV_MODE=0`, compile out:

- Dev File/Dir operation enum cases and dispatcher branches;
- all Dev finder, object, name, payload, timer, result, and Menu state;
- public Dev request implementations or replace them with constant false/empty
  stubs only if stable linking requires declarations.

Under `CONFIG_DEV_MODE=1`, Dev browsing uses the same one-object Nth resolver
and never reinstates the two 3,136-byte list arrays.

Both configurations must build. The off build must contain no `fs_test_*`,
`op_test_*`, or `menu_test*` SRAM symbols.

## 12. Implementation Phases

### Phase A — Lock baselines and contracts

- Record current ARM section sizes and exact cache symbols from
  `build/lxr02.elf`.
- Add tests/fixtures for name stripping, Instrument extension classification,
  casefold duplicate ordering, direct numbered-slot matching, and `.names`
  record offsets/CRC.
- Document the single-destination Instrument test rule and Normal versus Morph
  identity semantics in `FILESYSTEM_SPEC.md`.

Exit: format and ownership tests exist before any cache is removed.

### Phase B — Implement `.names` register engine

- Add `namesRegister.c/.h` and Makefile entry.
- Implement header/record serializers without packed-struct persistence.
- Implement validate/create, one-record read/write, group generation, full
  snapshot, deterministic fallback, and result codes.
- Integrate one async register job into the existing filesystem dispatcher.

Exit: a Dev-only diagnostic can read/write every resident coordinate across a
remount while SRAM contains only one record buffer.

### Phase C — Instrument resolver and Menu read path

- Implement the repeated real-tree Instrument count/Nth resolver.
- Convert nested Instrument Menu display to generation-tagged async resolution.
- Seed resident display from `.names`; display pool items only from the current
  real-tree result.
- Remove the four Instrument list/cache arrays and cache-maintenance helpers.

Exit: Instrument browsing works with no `instrument_file_*` symbols in the ELF.

### Phase D — Instrument Load

- Convert the API to `(scene, slot, type, ordinal)`.
- Use explicit root/Instrument parent handles and `afatfs_fopenChild()`.
- Preserve staging and parser validation.
- Commit one resident slot, apply runtime, then update one `.names` Instrument
  record before reporting completion.

Exit: normal and Morph Instrument Load pass real-card tests, stale browser
coordinates cannot load the wrong file, and no alias cache is used.

### Phase E — Instrument Save

- Add root replace recovery preflight.
- Build a replacement `/Instrument` tree one child at a time.
- Stream the requested file into staging and commit Phase 6 replacement.
- Update one `.names` record only for normal Save.
- Replace synchronous cache-based `OW` with a generation-tagged real scan.

Exit: Instrument Save/overwrite is testable from Menu, preserves unrelated
objects, survives injected interruption, and does not update a list cache.

### Phase F — Numbered Menu libraries

- Convert Kit, Scene, and Bank Menu rows to direct-slot resolvers.
- Convert boot first-slot selection.
- Remove all 69,000 bytes of root slot presence/name/open arrays.
- Remove or rewrite legacy `kitBrowser` and delete `kb_map`.

Exit: all numbered browsing and overwrite decisions scan the real selected
parent using only one candidate.

### Phase G — Resident identity cutover

- Migrate Kit, Scene, and Bank name reads/updates to `.names` groups.
- Remove name/stem fields from `scene_t`, `kit_t`, and BankData.
- Move `preset_currentName` to transient Menu editor ownership.
- Re-measure `scene_t` and update serializers/converters that used embedded
  name fields.

Exit: the only persistent resident musical source names are in `.names`.

### Phase H — Remaining caches and Dev state

- Remove Bank child name/open arrays.
- Remove sample-name SRAM duplication in favor of one flash metadata read.
- Replace Dev lists with the one-object resolver and compile-gate all Dev state.
- Search the complete ELF/source for remaining multi-entry name/alias arrays.

Exit: no persistent SRAM name cache remains.

## 13. Verification Matrix

### `.names`

- missing file creates a valid register without scanning libraries into it;
- bad magic/version/header CRC recreates defaults;
- bad record CRC affects only that resident name;
- Instrument, Kit, Scene, and Bank offsets never overlap;
- power loss during a group update never publishes a mixed complete generation;
- no record contains `NNN `, `NN `, `Kit `, an extension, slash, alias, cluster,
  object ID, or Menu/library coordinate.

### Instrument browsing

- empty `/Instrument` displays Empty without a cache;
- each type counts and resolves independently;
- ordinal ordering matches current casefold-then-case behavior;
- case-only duplicates occupy one ordinal;
- rapid encoder turns show only the latest generation;
- host rename/delete between display and OK produces not-found/stale behavior,
  never a different file load.

### Instrument Load

- each registered type parses and applies correctly;
- malformed and truncated files leave resident payload and `.names` unchanged;
- normal Load updates one distinct Instrument source stem in every resident
  cell whose payload was committed;
- Morph Load changes only Morph endpoints and leaves the source stem unchanged;
- active audio never observes staged/partial parameter data;
- every handle and finder is released on every failure phase.

### Instrument Save

- first Save creates `/Instrument` through replace staging;
- Save preserves unrelated known and unknown children;
- same-case and case-variant overwrite leaves one requested target;
- normal Save updates one `.names` stem only after tree commit;
- Morph Save writes projected data without changing the resident stem;
- card-full/copy/write failure aborts staging and leaves the live tree;
- power interruption at PREPARED, OLD_RENAMED, and PROMOTED recovers to a
  deterministic complete tree;
- completion waits for the replace sync and any required `.names` sync.

### Cache removal and memory

- `arm-none-eabi-nm` contains none of:
  `kit_slot_*`, `scene_slot_*`, `bank_slot_*`, `instrument_file_*`, `kb_map`,
  `kb_numKits`, `op_bank_child_name`, or `op_bank_child_open_name`;
- `CONFIG_DEV_MODE=0` contains no Dev cache/state symbols;
- `scenes` shrinks by the ARM-ABI amount measured after name-field removal;
- runtime stack high-water is measured during the slowest Instrument replace;
- build/map results are recorded in `MEMORY_AUDIT.md`.

## 14. Documentation During Implementation

Keep these documents current after each phase:

- `AFATFS_ADDITIONS_SUMMARY.md`: exact code changes, discovered bugs, results,
  ABI sizes, and tests run;
- `AFATFS_EXPANSION_PLAN.md`: only if a generic asyncfatfs primitive or Phase 6
  contract changes;
- `knowledge_files/specification_reference/ASYNCFATFS_REFERENCE.md`: final
  explicit-parent usage and any new generic API;
- `knowledge_files/specification_reference/FILESYSTEM_SPEC.md`: `.names`
  ownership, Menu coordinate resolution, and Instrument Save/Load sequence;
- `knowledge_files/specification_reference/MEMORY_AUDIT.md`: actual post-removal
  SRAM symbols and region totals;
- `MEMORY.md`: concise durable project-context summary after the implementation
  is verified.

No production `.c` or `.h` change is complete without detailed code-adjacent
comments at the function, important phase/loop, variable, offset/arithmetic,
input/output, ownership, and affiliate boundaries requested for this project.

## 15. Completion Definition

The work is complete when:

1. `.names` stores exactly the 129 resident source-name cells and no location
   information.
2. Instrument Load and Save are usable from the panel through current Menu
   coordinates, real-tree resolution, explicit-parent asyncfatfs calls, and
   `.names` updates.
3. Instrument Save uses recoverable `/Instrument` tree replacement and
   preserves unrelated directory contents.
4. All product library and resident name caches have been removed from SRAM;
   only bounded one-operation name buffers remain.
5. Dev-only name/state arrays are absent when Dev Mode is disabled and no
   multi-entry Dev name cache returns when it is enabled.
6. ARM builds, map checks, filesystem fault tests, and on-device Instrument
   load/save tests pass with the measured results recorded in the project
   specifications.
