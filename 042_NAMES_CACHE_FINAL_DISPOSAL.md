# 042 — Final Name, Key, Cache, and Staging Disposal Plan

## Status and scope

This is an implementation plan only.  It makes no firmware change by itself.

## Implementation progress — 2026-07-24

Implemented in the current worktree:

- `kit_t` no longer contains a Kit display name, six Instrument display names,
  or six retained filename stems.  `SceneData` now holds playable data only.
- The 9,000-byte list cache is again independent and is used only for
  `.hcindex`/`.hcnames` rows. A separate aligned 2,048-byte typed workspace
  stages Kit, non-Pattern Scene, and Instrument payloads; it is deliberately
  never aliased with the index cache after hardware testing showed that alias
  caused Kit Load browser failures.
- The former Menu seven-name scratch plus separate Scene scratch are replaced
  by filesystem's one 81-byte Bank/Scene/Kit/six-Instrument identity block.
- Kitset `file=` values remain parser-local only; successful Kit/Scene/
  Instrument operations publish their names through the identity block for the
  targeted HCNAMES writer.
- Scene settings plus its embedded Kit stage in the union. They are validated
  and committed before Pattern I/O. Pattern data then streams into final Scene
  SRAM, with the first selected destination mirrored to any additional selected
  destinations only after the direct read completes. This is intentionally
  non-atomic for Pattern data.
- The six retained Kit-member filename components were replaced by one
  49-byte on-demand formatting buffer.

Validation completed after these changes:

```text
make -j1
text 379256, data 400, bss 266880, dec 646536
```

Remaining work from this plan: replace the Bank loader's two 16-entry child
name/open-key arrays with one-child-at-a-time rescans, and collapse the other
persistent directory-component buffers down to the documented one/two
transient formatter arguments. Those are intentionally not claimed complete
until the Bank state machine preserves its exact mask-selective behavior under
hardware testing.

## Implementation progress — 2026-07-24 (Instrument preview and SRAM audit)

Implemented in this worktree:

- The physical identity block no longer duplicates the Bank name. Logical
  `FS_IDENTITY_BANK_ROW` aliases `BankData`'s existing 9-byte Bank name;
  filesystem stores only Scene, Kit, and six Instrument rows (72 bytes). The
  exact active-name total is therefore the specified 81 bytes: 9-byte BankData
  row plus 72-byte filesystem rows.
- The independent 2,048-byte typed staging workspace now has two concurrent
  Instrument-slot views: `original` and parser `candidate`. A normal
  Instrument Load session captures the selected voice once. While its type,
  mode, Scene, and voice remain unchanged, decrementing from pool row `000`
  returns to `kit` and applies the original parameters through the normal
  bounded DSP lifecycle. It adds no allocation; both images fit inside the
  existing 2,048-byte workspace.
- During preview, the existing Instrument identity row remains the original
  `.hcnames` stem. A selected pool name is derived from active `.hcindex` only
  when preview is invalidated or exited, then placed in that same row for the
  existing exit-time HCNAMES write. No original-name or filename-key copy is
  retained in the preview.
- A public display helper strips `.drm`/`.snr`/etc. from cached Instrument
  filenames before Menu or HCNAMES identity use. Root Instrument Load and Save
  completion use the same stem-only rule.

Linked SRAM evidence after this change (`make -j1`):

```text
fs_list_cache_name       9,000 B  (one index/HCNAMES cache)
fs_stage_workspace       2,048 B  (one aligned non-Pattern payload stage)
BankData Bank name           9 B
fs_identity_name            72 B  (Scene + Kit + six Instruments)
active-name total            81 B
```

The linked image reports `bss = 266,880` bytes. This is not an increase over
the immediately prior 2,048-byte-stage build (`266,896` bytes); linker layout
and dead-code elimination reduced the total by 16 bytes.

### Current complete name/key audit

The following is intentionally not an additional resident musical name cache:

| Item | Linked size | Lifetime / reason |
|---|---:|---|
| `op_filename_component` | 49 B | One-at-a-time derived Kit/Scene Instrument filename component. |
| `op_instrument_save_display_name` | 49 B | Immutable async root-Instrument request key until close. |
| `op_*_display_name` / `op_*_open_name` | 9–49 B each | Asyncfatfs operation arguments that must survive foreground-pumped phases. |
| `op_line_buf` / `op_lfn_name` | 160 B / 80 B | Streaming parser and FAT iterator scratch. |
| `staging_buf` | 512 B | Existing bulk-write stream buffer, unrelated to payload staging/names. |

The following strict-final-target exceptions remain and are not claimed as
compliant; they require a separate Bank/test state-machine refactor:

| Item | Linked size | Current reason |
|---|---:|---|
| `op_bank_child_name[16][9]` | 144 B | Bank loader retains all discovered child Scene names. |
| `op_bank_child_open_name[16][13]` | 208 B | Matching Bank child reopen aliases. |
| `fs_test_file_name` / `fs_test_dir_name` | 3,120 B each | File/Dir asyncfatfs diagnostic-menu result lists. |

These exceptions were not introduced or enlarged by the Instrument preview
change. The strict final disposal objective is therefore still incomplete;
they must be removed/reworked rather than treated as part of the permitted
9,000-byte index cache.

The objective is to make `.hcnames` the authoritative source for all Bank,
Scene, Kit, and Instrument names; eliminate retained file keys and duplicate
name storage; and use the existing 9,000-byte list cache as the only
load-validation workspace.  Pattern loading is intentionally excluded from
the atomic staging guarantee: it will be redesigned separately.

The plan must not add another cache, another 16-Scene name array, or a
second payload staging allocation.

> Historical implementation-detail sections below record the original
> 9,000-byte shared-workspace proposal. They are superseded by the correction
> above: production code now uses an independent 9,000-byte index/HCNAMES
> cache and a separate 2,048-byte typed non-Pattern stage. Retain the older
> text only as a trace of the rejected aliasing design, not as current code.

## Final SRAM contract

### Names retained outside the index cache

One physical BankData row plus one filesystem operation/menu block contain
exactly one copy of each simultaneously useful name:

| Row | Count | Bytes per row | Total |
|---|---:|---:|---:|
| Bank | 1 | 9 | 9 |
| Scene | 1 | 9 | 9 |
| Kit | 1 | 9 | 9 |
| Instrument | 6 | 9 | 54 |
| **Identity strings** | **9** | | **81** |

The ninth byte of every row is the terminator. BankData owns the Bank row;
filesystem owns the remaining eight rows. A compact valid-row bitmask, the
active source/destination slot, and operation phase are control values, not
additional names. No Scene object, Kit object, menu session, or pending
filesystem operation retains another Bank/Scene/Kit/Instrument display string.

### Independent index cache and payload stage

`fs_list_cache_name[1000][9]` remains exactly 9,000 bytes and contains only
`.hcnames` or one `.hcindex` domain. `fs_stage_workspace` is a separate,
aligned 2,048-byte union for non-Pattern payload validation. The separation is
mandatory: Instrument/Kit scrolling must keep its active index available while
an asynchronous candidate parses.

```c
/* Concrete member type names are to follow the final parser API. */
typedef union {
    uint8_t raw[FS_STAGE_CACHE_BYTES];
    kit_t kit_stage;
    struct {
        kit_instrument_slot_t original;
        kit_instrument_slot_t candidate;
    } instrument_preview_stage;
    struct {
        scene_settings_t settings;
        kit_t kit;
    } scene_stage;
} filesystem_shared_workspace_t;
```

The payload stage is never used by two parsers at once. Its Instrument member
does keep the entry snapshot plus one parser candidate concurrently:

1. `.hcnames` read/update uses the independent 9,000-byte name cache.
2. `.hcindex` traversal uses that same independent name cache.
3. Kit, Instrument, or non-Pattern Scene validation uses the typed stage
   member.
4. Once validation completes, the index cache remains available for the
   required name transaction and later `.hcindex` restoration.

The menu treats an active parser/apply as busy. Ordinary Instrument number
turns may retain the typed index and coalesce safely because the stage no
longer aliases it.

### Other SRAM that remains, explicitly

These items are not copies of names or retained file keys.  They remain for
their specific streaming/API purposes:

| Item | Current size | Why it remains |
|---|---:|---|
| Shared workspace | 9,000 B | The single `.hcindex` / `.hcnames` / staging union described above. |
| Five asyncfatfs handle slots | +656 B | Independently accepted filesystem concurrency capacity. |
| `op_line_buf` | 160 B | One streamed text-record parser buffer; avoids whole-file buffering. |
| `op_lfn_name` | 80 B | FAT directory iterator input/result while decoding an LFN entry. |
| Filename formatting buffer | 49 B | One transient component built from a name, slot, and extension. |
| Rename source/destination buffers | 2 × 49 B only while renaming | `afatfs_renameObject_lfn()` requires both call arguments to remain valid in the same asynchronous request. |
| Scalars | implementation-sized | Handles, byte counters, parser state, masks, slots, and status values. |
| Resident audio state | implementation-sized | Actual Scene, Kit, Instrument, and Pattern parameter data; it is not name/key caching. |

The 49-byte formatting buffer is acceptable transient call storage.  The
implementation must collapse the current persistent directory/member buffers
to this one buffer, except for the two live rename arguments.

## Canonical name and key rule

There are no long Instrument names.  Every Instrument leaf is eight or fewer
characters, a dot, and a three-character extension.  Therefore no legacy
stem preservation or conversion path is required.

For every filesystem operation, derive the object key immediately before the
associated open/create/remove request:

```text
directory location = Bank / Scene / Kit / Instrument slot coordinates
leaf component     = authoritative eight-cell .hcnames row + '.' + extension
```

The Instrument extension is selected from the Instrument type.  The caller
may pass a slot coordinate and an identity-block row, but must not pass or
retain a separately allocated filename/stem.  A duplicate leaf in the same
directory remains a normal filesystem naming error; it is not solved by an
SRAM key cache.

## Exact structural removals

### `Core/Bank/Scene/SceneData.h` and `SceneData.c`

Remove from `kit_t`:

```c
char display_name[SCENE_OBJECT_DISPLAY_NAME_LEN + 1u];
char instrument_display_name[INSTRUMENT_SLOT_COUNT][9];
char instrument_stem[INSTRUMENT_SLOT_COUNT][SCENE_INSTRUMENT_STEM_LEN + 1u];
```

Remove their supporting APIs and implementations, including
`scene_setKitDisplayName()`, `scene_getKitDisplayName()`, and
`scene_setInstrumentFileName()` if their only remaining job is to retain one
of those fields.  Update callers to read/write the corresponding identity row
or `.hcnames` cache row at the operation boundary.

This disposes 165 bytes per resident Scene (2,640 bytes across 16 Scenes):
one Kit display name, six Instrument display names, and six Instrument stems.

Required adjacent replacement comment in `SceneData.h`:

```c
/*
 * Deliberately no Bank, Scene, Kit, Instrument, filename, or file-stem
 * metadata is stored in scene_t or kit_t.
 *
 * Why: .hcnames is the authoritative name register and filesystem keys are
 * derived when an SD operation begins.  Keeping names here would create up to
 * sixteen stale duplicate copies and would make selective Bank Load capable
 * of overwriting names for unselected resident Scenes.
 *
 * Inputs/outputs: Scene/Kit data structures contain audio parameters only.
 * Name clients must use filesystem identity-row accessors instead.
 *
 * Affiliates: filesystem.c name-cache helpers, menu.c identity session, and
 * the Bank/Scene/Kit/Instrument load and save state machines.
 */
```

### `Core/Menu/menu.c`

Replace both of these independent menu allocations:

```c
menu_residentNameScratch[7][9]
menu_sceneResidentNameScratch[9]
```

and their separate coordinates/validity ownership with a single
`menu_filesystemIdentity[9][9]` block shared by Bank, Scene, Kit, and
Instrument menus.  Define named row constants for Bank, Scene, Kit, and the
six Instrument rows.  The valid-row bitmask replaces the two independent
"scratch valid" flags.

Delete paths that refill the Kit/Instrument scratch from `scene->kit`.
Instead, `menu_requestKitEntryNames()` and Scene entry helpers copy needed
rows from the just-read `.hcnames` workspace.  On menu exit, use the identity
rows as the input to the targeted `.hcnames` writer, then invalidate the
identity block.  Menu rendering reads the row directly; it does not fetch
from Scene SRAM.

Required adjacent replacement comment in `menu.c`:

```c
/*
 * The only menu-resident identity strings: Bank, Scene, Kit, and six
 * Instrument rows.  Each row is an eight-cell display name plus NUL.
 *
 * Why: .hcnames is authoritative, but list traversal overwrites the shared
 * filesystem workspace with .hcindex.  Copying only the active operation's
 * nine identities permits fast traversal without retaining names in every
 * resident Scene or allocating a second 1000-row cache.
 *
 * Inputs: rows copied from the root .hcnames workspace at menu entry or from
 * a completed load/save result.  Outputs: LCD names and targeted .hcnames
 * updates at menu exit.  Invalid rows render blank.
 *
 * Affiliates: filesystem_requestLoadResidentNames(),
 * filesystem_requestUpdateResidentNames(), menu load/save completion, and
 * .hcindex list ownership.
 */
```

### `Core/Hardware/SD/filesystem.c`

Replace:

```c
static char fs_list_cache_name[...][9];
static kit_t op_staged_kit;
static kit_instrument_slot_t op_staged_instrument;
static scene_t op_staged_scene;
```

with the aligned `filesystem_shared_workspace_t` and explicit workspace-owner
state.  All cache accessors use `workspace.names`; Kit and Instrument parsers
use the appropriate typed union member; Scene parsing uses only
`workspace.scene_stage.settings` and `workspace.scene_stage.kit`.

Do **not** put `PatternSet` in the union.  `scene_t` must no longer be a
staging member.

Required adjacent replacement comment in `filesystem.c`:

```c
/*
 * One mutually exclusive filesystem workspace.  It is a 9,000-byte name
 * cache while browsing .hcindex or .hcnames, and a typed validation image
 * only while a Kit, Instrument, or non-Pattern Scene payload is loading.
 *
 * Why: separate op_staged_* objects duplicate the largest cache allocation
 * even though browsing and payload validation cannot legally overlap.
 *
 * Inputs: the active state machine claims an owner before using a union
 * member.  Outputs: validated data is committed to final SRAM, after which
 * the owner releases the workspace for name/index operations.
 *
 * Affiliates: filesystem_clearNameCache(), HCNAMES read/write states,
 * list-index states, and all Kit/Instrument/Scene load states.
 */
```

Add compile-time checks adjacent to the union declaration:

```c
_Static_assert(sizeof(filesystem_shared_workspace_t) <= 9000u,
               "filesystem workspace exceeds the established cache budget");
_Static_assert(_Alignof(filesystem_shared_workspace_t) >= _Alignof(kit_t),
               "filesystem workspace must align Kit staging");
```

Use the project constants rather than literal values in the final source.

## Exact load-state changes

### Kit Load

1. Claim the workspace as `KIT_STAGE`.
2. Initialize and parse the Kit into `workspace.kit_stage`.
3. Validate the staged Kit, including each Instrument payload.
4. Copy the validated Kit parameter data to every requested destination.
5. Release stage ownership.
6. Claim the workspace as `HCNAMES`, update only the affected Kit/Instrument
   rows, write the root register, then reload the required `.hcindex`.

Names are copied to the identity block before list traversal replaces the
workspace.  They are never copied into `kit_t`.

Required state-transition comment:

```c
/*
 * Claim the shared workspace for Kit validation before parsing any payload.
 * The staged Kit is not visible to resident Scenes until the parser and
 * validator succeed, preserving atomic Kit parameter replacement.
 *
 * Inputs: open Kit directory and requested Scene mask.  Output: committed
 * audio parameters on success; unchanged resident Kits on validation error.
 * The workspace is released before the subsequent HCNAMES update.
 *
 * Affiliates: Kit parser, Instrument member parser, targeted HCNAMES writer,
 * and menu list-cache restoration.
 */
```

### Instrument Load

1. Claim the workspace as `INSTRUMENT_STAGE`.
2. Parse and validate the selected Instrument into its union member.
3. Commit only the selected Instrument parameter slot.
4. Release the workspace, update only that Instrument `.hcnames` row, and
   restore the active `.hcindex`.

The type-derived extension and identity row construct the leaf name at open
time; `op_staged_instrument_display_name` and
`op_staged_instrument_stem` are removed.

### Scene Load

1. Claim the workspace as `SCENE_STAGE`.
2. Read and validate Scene general parameters into
   `workspace.scene_stage.settings`.
3. Read and validate the Scene-resident Kit into
   `workspace.scene_stage.kit`, including its Instrument payloads.
4. Validate all non-Pattern Scene requirements.
5. Commit the validated Scene general parameters and Kit to the destination
   Scene SRAM.
6. Open/read the Pattern after that commit and write it directly into the
   destination Scene's Pattern member.
7. Release the workspace, update the relevant `.hcnames` rows, and restore
   `.hcindex`.

Pattern is intentionally direct-write and non-atomic.  If Pattern parsing
fails after step 5, the general Scene and Kit remain committed while Pattern
may be partially changed.  The error result must clearly identify this phase;
the later Pattern redesign owns its transactional policy.

Required Pattern-phase comment:

```c
/*
 * Pattern data is read directly into the final destination Scene only after
 * its general settings and embedded Kit have been staged, validated, and
 * committed.
 *
 * Why: Pattern writes are deliberately non-atomic until the dedicated Pattern
 * data redesign.  Excluding PatternSet from the shared workspace keeps the
 * existing 9,000-byte cache budget and avoids a second large staging buffer.
 *
 * Inputs: validated destination Scene and opened Pattern file.  Output: the
 * destination Pattern is updated incrementally.  A read/parse failure reports
 * a Pattern-phase error and does not roll back the already committed settings
 * or Kit.
 *
 * Affiliates: Scene load state machine, Pattern parser, error display hook,
 * and the later Pattern-format redesign.
 */
```

## Bank Load and Save changes

Bank operations use the workspace cache, never a separate `16 x name` or
`16 x open-name` collection.

### Selective Bank Load

1. Preserve the requested Scene mask exactly.  A zero/partial request never
   falls back to "all present children."  Unselected resident Scenes and their
   `.hcnames` rows remain untouched.
2. Process one selected child at a time.  Rescan/derive its directory name
   from the relevant `.hcindex`/directory entry as needed; do not retain
   `op_bank_child_name` or `op_bank_child_open_name` arrays.
3. Stage and commit each selected child using the Scene sequence above.
4. Once staging is no longer active, read `.hcnames` into `workspace.names`,
   overlay only the selected Bank/Scene/Kit/Instrument rows, and write it.
5. Restore the Bank `.hcindex` cache and update its slot assignment if needed.

### Bank Save

1. Claim `workspace.names` for `.hcnames` and obtain the current authoritative
   Bank, Scene, Kit, and Instrument rows.
2. Save one selected child at a time, deriving every directory/file component
   on demand from those rows, slot coordinates, and Instrument type extension.
3. Write/update the Bank directory `.hcindex`.
4. Update the root `.hcnames` Bank row and any rows actually changed by the
   save, then restore the Bank `.hcindex` cache.

Required Bank-state comment:

```c
/*
 * Bank child names are resolved one child at a time instead of being retained
 * in per-Bank arrays.
 *
 * Why: a Bank operation already owns the 1000-row shared workspace, so
 * retaining 16 duplicate display/open names adds stale SRAM state without
 * avoiding a required filesystem operation.  It also makes it easier to
 * accidentally update HCNAMES rows outside the user-selected load mask.
 *
 * Inputs: requested Bank slot, exact Scene mask, and current directory/index
 * entry.  Outputs: one child path for the immediate operation only; no child
 * name survives into the next child.
 *
 * Affiliates: selective Bank Load mask handling, HCNAMES overlay writer,
 * directory scan helpers, and Bank .hcindex restoration.
 */
```

## Filename-buffer disposal

Replace the persistent operation arrays below with a formatter that writes one
component into the single transient 49-byte buffer immediately before use:

```text
op_save_kit_member_display_file[6][49]
op_save_kit_dir_display_name[49]
op_save_scene_kit_display_name[49]
op_instrument_save_display_name[49]
op_save_bank_dir_display_name[49]
op_save_bank_tmp_display_name[49]
op_save_bank_old_display_name[49]
op_scene_child_display_name[9]
op_scene_display_name[9]
op_bank_display_name[9]
op_staged_instrument_display_name[9]
op_staged_instrument_stem[17]
op_bank_child_name[16][9]
op_bank_child_open_name[16][13]
```

The final Bank replacement/rename sequence is the sole exception: retain two
49-byte transient buffers for the source and destination arguments of
`afatfs_renameObject_lfn()`.  Temporary/old directory tokens are generated
from the Bank slot plus operation nonce immediately before that call.

Required formatter comment:

```c
/*
 * Format one filesystem component immediately before its asynchronous request
 * from an authoritative HCNAMES identity row, slot coordinate, and extension.
 *
 * Why: filenames are derived keys, not resident metadata.  Retaining arrays
 * of preformatted member or directory names duplicates .hcnames and can go
 * stale after a selective load/save.
 *
 * Inputs: destination buffer, its capacity, identity row, slot/type context,
 * and required extension.  Output: one NUL-terminated component valid for the
 * immediately following filesystem call.  Rename is the only caller needing
 * two buffers simultaneously.
 *
 * Affiliates: Kit/Scene/Bank save states, Instrument open/save states, and
 * afatfs_*_lfn APIs.
 */
```

## Header/API changes

`filesystem.h` must expose only operations that exchange fixed-width name rows
or identity row selectors.  APIs that currently accept a retained
`display_name` solely so the callee can save it for later must be changed to
either:

- consume the supplied eight-cell row during request setup and copy it into
  the one identity block; or
- derive it internally from `.hcnames` under the active workspace ownership.

Do not add API parameters for filenames or stems.  Add workspace owner and
identity-row documentation to the relevant declarations.

Required header comment:

```c
/*
 * Starts an operation using the active fixed-width identity rows; it neither
 * allocates nor retains a filename key.  The filesystem derives each leaf
 * component from .hcnames, slot context, and the fixed extension when that
 * component is opened or written.
 *
 * Inputs: operation coordinates/mask and completion callback.  Output: false
 * if the shared workspace or filesystem is busy; otherwise completion reports
 * the final operation status.  Name rows outside the requested mask are not
 * modified.
 *
 * Affiliates: menu identity session, shared-workspace owner state, HCNAMES
 * targeted writer, and asynchronous FAT operation phases.
 */
```

## Implementation order and verification

1. Introduce the workspace union, owner transitions, assertions, and
   instrumentation without changing behavior.
2. Move Kit and Instrument staging to the union and remove the dedicated
   `op_staged_kit`/`op_staged_instrument` objects.
3. Split Scene staging from Pattern loading and remove `op_staged_scene`.
4. Introduce the nine-row identity block and migrate menu/HCNAMES users.
5. Remove name/key fields from `kit_t`; change all call sites to derive names
   and filenames on demand.
6. Remove Bank child arrays and persistent filename arrays, retaining only the
   two rename arguments where required.
7. Update `042_SESSION_LOG.md`, build, inspect the link map/`nm` sizes, and
   run the tests below.

Required checks:

- `_Static_assert` proves the shared workspace remains within 9,000 bytes.
- A malformed Kit/Instrument leaves target parameters unchanged.
- A malformed pre-Pattern Scene leaves target Scene settings/Kit unchanged.
- A malformed Pattern reports the Pattern phase after settings/Kit commit;
  this known non-atomic result is tested and documented.
- Kit Load/Save updates its Kit plus six Instrument `.hcnames` rows.
- Scene Load/Save updates only the necessary Scene/Kit/Instrument rows.
- Selective Bank Load changes only masked Scenes and their corresponding
  `.hcnames` rows; an empty mask does not become an all-present mask.
- Menu scrolling remains responsive and renders blank until its needed
  `.hcnames` row is fetched.
- `arm-none-eabi-nm --print-size --size-sort` confirms removal of the three
  dedicated staging objects, per-Scene name/stem fields, Bank child arrays,
  and prebuilt filename arrays.
- Source comments beside every changed C/H declaration and state transition
  use the approved What/Why/Inputs/Outputs/Affiliates content above.

## 2026-07-24 regression correction — shared workspace handoff

- The boot index scan fills the workspace `names` member, then starts the
  chained `.hcindex` writer through `filesystem_start()`. Generic request
  setup was clearing the complete union at that handoff, so the writer had
  1,000 empty rows as input. A second generic clear through the Instrument
  stage alias also overlapped and erased the first cache rows.
- Both generic clears are removed. Only the owner that explicitly claims the
  workspace may initialize its own member: the cache transition clears names,
  and the Kit, Instrument, and Scene request paths clear their typed stage.
  This preserves the scan result until its index writer finishes without
  increasing SRAM. Rebuild is complete; a hardware boot must regenerate the
  already-blank `.hcindex` files.

## 2026-07-24 regression correction — selected-key capture before staging

- Hardware `KitL00` traced to a second union-lifetime error: Kit/Scene/
  Instrument requests validated an index row, then initialized their staging
  member, which aliases and destroys that same index before loader phase zero
  re-opened the selected directory/file. Kit therefore failed at phase `00`
  before any FAT payload open.
- Each request now copies only its selected key into already-existing operation
  scratch before claiming the typed union member. Kit and Scene use the existing
  nine-byte display scratch; Instrument uses its existing filename scratch.
  Loader phases now use that request-stable key for all later opens and the
  successful HCNAMES identity update. This is not a new cache or SRAM object.
- Bank Load no longer initializes a Scene stage while its asynchronous Bank
  name-repair preflight still needs the Bank cache. Its delegated child loader
  initializes the Scene stage at the existing later boundary. This preserves
  the Bank repair key without changing its mask-selective behavior.

## 2026-07-24 implementation correction — independent name and payload storage

- The shared-union design is retired. It caused the remaining Load-scroll
  errors because an accepted typed payload could still invalidate the active
  browser index. `fs_list_cache_name` is restored as a dedicated, exact
  1,000-by-9-byte (9,000-byte) `.hcindex`/`.hcnames` array; no Kit, Scene, or
  Instrument payload aliases it.
- A separate aligned 2,048-byte `fs_stage_workspace` now stages exactly one
  Kit, Instrument, or non-Pattern Scene (Scene settings plus embedded Kit).
  It proves capacity for 512 byte-valued parameter cells across the six voices,
  each of the three retained images (main, morph, interpolation), current
  Scene/Kit metadata, and 384 bytes reserved for a future Effect stage. Pattern
  remains excluded and continues to stream directly to final Scene SRAM after
  the non-Pattern commit.
- Compile-time assertions enforce the exact 9,000-byte name cache, the
  512-cell cross-voice parameter limit, the three-image capacity calculation,
  the Effect reserve, stage size, and Kit alignment. `nm` confirms 9,000 bytes
  for `fs_list_cache_name`, 2,048 bytes for `fs_stage_workspace`, 81 bytes for
  the identity block, and the pre-existing 512-byte stream buffer. The linked
  BSS is 266,896 bytes: precisely 2,048 bytes above the previous build.

## 2026-07-24 completed disposal — Bank children, diagnostics, and delete stacks

- Removed the three Bank-local 16-entry arrays: 144 B of child display names,
  208 B of child aliases, and 16 B of child flags. The retained 16-bit child
  mask is occupancy/mask state only. Bank Load performs its initial parent scan
  for that mask, then rescans the selected Bank parent for every selected Scene
  child. It keeps the lexical winner in existing one-name operation scratch,
  derives the `SS Name` component with `storage_formatBankSceneDir()`, and only
  then claims the normal one-Scene stage. Mask-selective payload and HCNAMES
  behavior is unchanged.
- Retired the generic File/Dir diagnostics and removed their two 64 x 49 B
  list caches (6,240 B). Menu no longer cycles to those types even in dev mode.
  Compatibility API stubs intentionally start no transaction and retain no
  names, aliases, or result cache.
- Retired the firmware-owned recursive delete implementation and its 558 B
  name/alias stacks. Slot replacement continues to call asyncfatfs'
  `afatfs_deleteTree()` for the concrete scanned object; only its completion
  result latch remains, which is operation status rather than a name/key cache.
- `make -j1` passes. BSS is now 259,352 B, down 7,528 B from 266,880 B before
  this disposal. Source comments adjacent to the changed C/H state and APIs
  describe the inputs, outputs, reason, and affiliates.
