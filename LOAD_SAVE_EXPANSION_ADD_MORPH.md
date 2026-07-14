# Load/Save Expansion and Morph Plan

## Purpose

This document is the implementation plan for the next Save/Load expansion pass
after the asyncfatfs long-filename and case-preserving filesystem work.

It is grounded in the current code shape:

- `Core/Hardware/SD/asyncfatfs/` owns FAT/VFAT object creation, opening, and
  object iteration.
- `Core/Hardware/SD/filesystem.c` owns async storage state machines, scan
  caches, Kit/Instrument/Scene directory traversal, and save request capture.
- `Core/Hardware/SD/storageTypes.c/h` owns text schemas and descriptor-indexed
  Kit/Instrument/Scene serialization.
- `Core/Scene/SceneData.c/h` owns resident Scene, Kit, Instrument endpoint
  images, per-voice Morph amounts, and retained source names.
- `Core/Scene/Preset/presetManager.c/h` owns staged commit, runtime apply,
  Morph endpoint commit, and completion reporting to Menu.
- `Core/Menu/menu.c/h` owns Load/Save reachability, cursor movement, save-name
  editing, OK/OW display, and nested Instrument workflows.

This is not a replacement filesystem spec. It records the exact behavior we are
about to implement for overwrite saves, retained names, Kit/Instrument Morph
Load/Save, and the Load/Save UI.

## Filesystem Policy Confirmed

The asyncfatfs long-filename and case-preserving update is the active
foundation. Production code should use visible display components through the
LFN-aware APIs:

- root `Kit`
- root `Scene`
- root `Instrument`
- file `kitset.kcg`
- file `sceneset.scg`

asyncfatfs must not hide ordinary names beginning with `.`. Dot-prefixed files
and directories are real FAT objects. Only structural FAT records are hidden by
the object iterator:

- deleted entries
- VFAT LFN fragment entries
- volume labels
- structural `.` and `..`
- terminators

Product scanners apply product policy after receiving concrete objects. For
example, Kit scan accepts numbered directories, Instrument scan accepts files
whose visible name or fallback alias matches a registered instrument extension.

User-facing save and load matching is case-insensitive and case-preserving:

- a user-entered filename or directory component matches an existing object
  regardless of case;
- a matched save is an overwrite and must show `OW`;
- after overwrite, the visible object name retains the newly user-entered case;
- same-casefold collisions are treated as one user-facing object, not as
  distinct product objects;
- if a card already contains several physical objects whose names differ only by
  case, scans expose only the first one in filesystem/alphabetic order, with
  capital letters sorting before lowercase for the same character;
- later same-casefold duplicates are ignored by browsers and loaders;
- overwrite removes every same-casefold duplicate and leaves behind one object
  using the case arrangement the user entered for the save.

For example, if an externally edited card contains both `fiRstfile.snr` and
`firStfile.snr`, the scan exposes `fiRstfile.snr` because `R` sorts before `r`.
The later duplicate is not displayed or loaded. Saving over that name removes
both physical variants and writes exactly the newly entered case.

## First Prerequisite: LFN-Aware Rename/Replace in asyncfatfs

Before the Save/Load repair and Morph work, asyncfatfs needs a focused
filesystem primitive: rename one visible object component while preserving the
object's data clusters and children. This is mandatory before Morph work:
normal Kit Save and Kit Morph Save must use the same directory ensure/rename
path so an occupied `Kit/NNN <name>/` slot changes to the edited visible name
instead of preserving the old directory component.

The active overwrite strategy is:

- directory-shaped saves preserve the existing directory tree;
- if the selected directory already exists, rename the directory to the new
  visible component when the edited save name changed;
- then enter the directory and process only the expected children;
- for an expected subdirectory, create it if absent or rename the slot-number-
  matching old subdirectory if its visible component changed, then recurse into
  that known child;
- for an expected file, open it with the LFN-aware write path so it is replaced
  if present or created if absent;
- unrelated extra files and subdirectories are tolerated and left on card.

This is important for future Bank Save/Load because Bank is expected to contain
multiple levels of meaningful subdirectories. Rename/replace only mutates the
objects that the save state machine explicitly owns.

### Current Code Facts From The Dive

The rename/replace implementation must be designed around these current source
facts:

- `asyncfatfs.h` already exposes `afatfsObjectInfo_t`, and that struct carries
  `displayName`, `shortName`, `kind`, `attrib`, `ntReserved`, `sfnEntry`,
  `lfnFirstEntry`, and `lfnEntryCount`. This is the identity metadata a rename
  primitive needs to update a complete VFAT object name, not merely the final
  short-name entry.
- `afatfs_findNextObject()` is the correct scanner boundary. The current source
  already has `afatfs_isStructuralDotEntry()` and hides only FAT's synthetic
  `.` / `..` directory links. Preserve that behavior while adding duplicate
  case-fold suppression/removal.
- `afatfs_mkdir_lfn()` and `afatfs_opendir_lfn()` already provide component
  create/open behavior, but `mkdir_lfn()` opens an existing matching directory
  without changing its display name. Occupied slot saves therefore need an
  explicit rename step before entering the directory.
- `afatfs_fopen_lfn(..., "w", ...)` already provides the expected file replace
  behavior for normal save files: if the target exists it is opened and
  truncated; if it does not exist it is created. Keep file replacement on that
  existing path unless coding proves an LFN entry-run metadata refresh is needed.
- The long-name create path already has reusable machinery for display-name
  validation, short-alias generation, LFN fragment counts, free-run discovery,
  and writing an LFN run plus final SFN entry. Rename should reuse or factor
  this logic instead of inventing a parallel VFAT writer.
- `filesystem.c` already has one active operation slot through
  `filesystem_start()` / `filesystem_tick()`. Rename/replace should be sequenced
  as phases inside the existing save state machines rather than as a user-facing
  filesystem operation.

### Required Public asyncfatfs API

Add a component-based rename entry point to `asyncfatfs.h`.

Recommended shape:

```c
bool afatfs_renameObject_lfn(const char *oldDisplayName,
                             const char *newDisplayName,
                             afatfsMatchMode_t matchMode,
                             char openNameOut[AFATFS_SHORT_FILENAME_MAX],
                             afatfsCallback_t complete);
```

If matching by display text alone proves too ambiguous for numbered slot saves,
add a private helper that takes a freshly scanned `afatfsObjectInfo_t` instead:

```c
static bool afatfs_renameObjectByInfo(const afatfsObjectInfo_t *object,
                                      const char *newDisplayName,
                                      char openNameOut[AFATFS_SHORT_FILENAME_MAX],
                                      afatfsCallback_t complete);
```

Header comment to add beside `afatfs_renameObject_lfn()`:

```c
/*
 * Rename one object in the current directory by display component.
 *
 * What: Starts an asynchronous rename of one file or directory named by a
 * single visible component. The operation updates the object's VFAT LFN/SFN
 * directory entry run and returns the new asyncfatfs-openable short alias in
 * openNameOut when requested.
 *
 * Why: Slot saves must change `NNN OldName/` into `NNN NewName/` while
 * preserving the directory tree. The slot number remains the product identity;
 * the FAT directory entry is only the visible name and open alias.
 *
 * Inputs: oldDisplayName and newDisplayName are component names in the current
 * directory, not paths. User-facing callers use case-insensitive matching while
 * preserving newDisplayName's visible case. openNameOut may be NULL if the
 * caller does not need the new alias.
 *
 * Outputs/effects: the object's first cluster, file size, attributes, and child
 * data are preserved. Only the directory entry run naming the object changes.
 * The callback fires after the rename state machine has finished or failed.
 *
 * Affiliates/clients: filesystem.c Kit, Scene, Bank, and future Effect slot
 * save phases; asyncfatfs object scanning; FAT/VFAT name generation helpers;
 * File/Dir diagnostics if a rename test entry is added.
 */
```

### Required Rename Semantics

The rename primitive must preserve object contents and identity:

- never touch the object's first cluster chain;
- never traverse or remove child entries inside a directory;
- keep the source object's `attrib` and size metadata coherent;
- reject a target display name whose case-folded form already belongs to a
  different object in the same directory;
- treat renaming an object to the same display component as success and return
  its current open alias;
- use the same case-insensitive display comparison rules as
  `afatfs_fopen_lfn()` and `afatfs_findNextObject()`;
- flush/sync must still be handled by the outer save state machine before it
  reports success.

Long-name sizing is the important edge case. If the new display component needs
no more directory entries than the old LFN/SFN run, the code may rewrite the
existing run in place and mark any now-unused leading LFN entries deleted. If the
new name needs more entries, the safer implementation is to write a new complete
entry run that points to the same cluster chain, then retire only the old entry
run. That retires name metadata, not file or directory data.

### Required asyncfatfs Internal Changes

Add a filesystem-global rename operation state to `afatfs_t`, not to normal
`openFiles[]`, because rename owns directory scanning and may need to reserve or
rewrite raw directory entries while the current directory is active.

Implementation pieces to add or factor:

- structural dot helper for `afatfs_findNextObject()`;
- display-match helper shared with `fopen_lfn()`/`mkdir_lfn()` matching;
- LFN entry-count calculation helper for an arbitrary display component;
- short-alias generation that can ignore the source object while checking
  collisions for the new name;
- raw entry-run writer that can write LFN fragments plus an SFN entry from an
  existing object's metadata;
- raw entry-run retire helper that marks only old LFN/SFN name entries deleted;
- rename state machine phases for scan source, scan target collision, choose
  in-place vs move-run, write replacement entries, retire old entries if moved,
  and finish.

Comment block for the structural dot helper:

```c
/*
 * Identify only FAT's structural dot directory entries.
 *
 * What: Returns true for the synthetic `.` and `..` entries inside a directory,
 * and false for ordinary user objects whose names merely begin with a dot.
 *
 * Why: The current object iterator hides every short alias beginning with `.`.
 * That makes host-created files such as `.DS_Store` invisible even though the
 * confirmed filesystem policy says dot-prefixed objects are real objects.
 * Rename/replace and product scanners need the iterator to report concrete
 * objects and let product policy decide whether to use them.
 *
 * Affiliates/clients: afatfs_findNextObject(), File/Dir diagnostics,
 * Kit/Scene/Instrument library scans, and future save collision checks.
 */
```

Comment block for the raw entry-run writer:

```c
/*
 * Write a VFAT name entry run for an existing object identity.
 *
 * What: Emits zero or more LFN fragments followed by one SFN entry. The SFN
 * entry receives the new short alias and display-case flags, while preserving
 * the source object's attributes, first cluster, file size, and timestamps.
 *
 * Why: Renaming a directory must change the name without allocating a new
 * directory tree or touching children. Moving to a new entry run is required
 * when the new long name needs more LFN fragments than the old run provides.
 *
 * Affiliates/clients: afatfs_renameObject_lfn(), LFN create helpers,
 * afatfs_findNextObject(), and future directory-shaped save operations.
 */
```

Comment block for retiring an old entry run:

```c
/*
 * Retire only the old VFAT name entries after a successful moved rename.
 *
 * What: Marks the old LFN fragments and old SFN entry deleted after the new
 * entry run has been written. It does not free clusters or inspect children.
 *
 * Why: The old directory entry is no longer the authoritative name, but the
 * file or directory data still belongs to the newly written entry. This is not
 * object deletion; it is metadata replacement.
 *
 * Affiliates/clients: afatfs_renameObject_lfn(), FAT cache dirty marking, and
 * save state machines that need the returned open alias.
 */
```

### Required filesystem.c Integration

Add private save-phase helpers rather than public product operations unless a
File/Dir diagnostic rename test is useful.

Required behavior for directory-shaped saves:

1. enter the parent directory;
2. scan for the product-owned object by product identity, such as slot number
   `NNN` for Kit/Scene/Bank;
3. if absent, create the expected directory with `afatfs_mkdir_lfn()`;
4. if present and its display component differs from the requested component,
   call the asyncfatfs rename primitive;
5. store the returned open alias into the scan/save cache;
6. open the directory by alias and continue into owned children;
7. for every expected child directory, repeat the same ensure/rename/create
   logic;
8. for every expected child file, use `afatfs_fopen_lfn(..., "w", ...)` so the
   file is replaced or created;
9. ignore unrelated child objects.

For Kit Save specifically:

- `Kit/NNN <edited-kit-name>/` is ensured by slot number `NNN`;
- if an occupied slot's folder name differs, it is renamed before entry;
- six member Instrument files are replaced/created using retained Instrument
  names with the voice number in character 8;
- `kitset.kcg` is replaced/created after member aliases are known;
- stale extra files in the Kit folder are tolerated;
- retained Kit name updates only after normal save completes successfully.

For Scene and future Bank Save, use the same pattern recursively: ensure and
rename only the subdirectories the save format owns, replace/create the files the
format owns, and tolerate extra objects.

### Required Test Plan

Before wiring this into Kit/Scene/Bank save paths, test asyncfatfs rename with a
small diagnostic or controlled save path:

- rename a root LFN file and confirm contents survive;
- rename a root SFN-only file and confirm the returned alias opens it;
- rename an empty directory and confirm it can still be entered;
- rename a directory containing files and confirm children survive;
- rename a directory to a longer name that requires more LFN entries;
- rename a directory to a shorter name and confirm old LFN fragments do not
  remain visible;
- attempt rename to an existing different object and confirm failure;
- confirm dot-prefixed ordinary objects remain visible in scans;
- confirm sync/remount shows the new visible name and preserved contents.

### Safety Rules

- Never rename root.
- Never rename structural `.` or `..`.
- Never free cluster chains as part of directory rename.
- Never remove unrelated child files merely because a save did not rewrite them.
- Never create a duplicate slot directory when the numbered slot already exists.
- Never report save success until rename, replace/create of expected files,
  close, and sync have all completed.

## Save Overwrite Semantics

Any save that targets an existing object should update that object in place by
renaming expected directories and replacing expected files. Directory-shaped
saves do not clear the whole tree first.

For slot directories, the slot number is the identity:

- `NNN` is the slot ID;
- the text after `NNN ` is the user-entered save name;
- occupied slot save renames the existing directory when the edited name changed;
- expected child files are replaced or created;
- expected child directories are ensured and renamed recursively when necessary;
- unrelated stale files or subdirectories are tolerated.

For example, saving Kit slot `042` with edited name `Crunch  ` should make the
slot's visible directory component become:

```text
Kit/042 Crunch  /
```

The implementation should not preserve the old folder display name. It should
rename the old numbered object, then replace/create the expected files inside
that preserved directory tree.

For standalone file saves, overwrite uses the LFN-aware write path with
case-insensitive matching. The user is warned with `OW` before confirming when
the target name matches an existing file after case folding, and the file is
replaced if present or created if absent. The visible saved filename retains the
case entered by the user for the current save.
## Retained Internal Name Semantics

Names are metadata derived from file or directory names. They are not stored
inside Instrument or Kit data files as authoritative names.

After a successful normal save, the user-entered character-entry field becomes
the internal retained name for exactly the object being saved.

Rules:

- Kit Save updates the internal Kit name only.
- Scene Save updates the internal Scene name only.
- Instrument Save updates the internal retained name for that Instrument only.
- Effect Save will update the internal retained name for that Effect only.
- Bank Save will update the internal Bank name only.
- Container saves do not update retained names for sub-data inside the
  container.

Examples:

- Kit Save updates the Kit name but does not rename its instruments in RAM.
- Scene Save updates the Scene name but does not rename the embedded Kit,
  Instruments, Pattern, or future Effect data.
- Instrument Save updates only the selected voice's retained Instrument name.

Morph operations never update retained internal names.

Blank names are valid. The firmware should not substitute a fallback when the
user leaves all name characters blank. The retained internal name is blank, and
a numbered slot directory with a blank name is the three digits plus the
separator space, for example `042 `. The literal name `Empty` is also valid. The
UI may therefore show a real saved object named `Empty`; this is accepted.

This blank numbered-folder rule applies to root numbered libraries such as
`Kit/NNN <name>/` and future root `Scene/NNN <name>/`. It does not change the
Scene-internal embedded Kit directory rule: inside a Scene folder, the embedded
Kit directory is always named `Kit <kit-name>`. A blank embedded Kit name is
therefore `Kit `, not an absent directory and not a root numbered Kit slot.

## Current Name Storage Gap

Instrument source names already exist in `kit_t`:

- `instrument_display_name[slot][9]`
- `instrument_stem[slot][SCENE_INSTRUMENT_STEM_LEN + 1]`

They are initialized at boot to defaults like `inst_vo1` and are updated by
normal Instrument Load.

Kit and Scene need equivalent retained-name storage. Current code mostly uses
scan-cache names and `preset_currentName`, which is not enough for the new save
semantics because the save editor, selected slot display, and retained object
name must be separate concepts.

Add resident retained display/stem fields for:

- active Kit name;
- Scene name;
- later Bank name and Effect names when those objects are implemented.

The exact location should follow ownership:

- Kit name belongs with the resident `kit_t`.
- Scene name belongs with the resident `scene_t`.
- Instrument names remain per Kit instrument slot as they are today.

## Save UI Display and Cursor Semantics

The Save page must keep these concepts separate:

- selected save type;
- selected slot number when the operation is slot-based;
- slot occupancy display (`Empty` or existing object name);
- editable save name;
- OK/OW confirmation.

### Slot Selection

When scrolling a numbered save slot:

- show `Empty` when the slot has no object;
- show the existing object name when the slot is occupied;
- do not overwrite the edit-name buffer just because the slot display changed.

When the encoder click moves from slot selection to character entry:

- the character-entry field shows the current internal retained name for the
  object being saved;
- it does not keep showing `Empty` unless the internal retained name really is
  `Empty`.

For Kit Save, this means the editor is seeded from the retained Kit name. For
future Scene Save, it is seeded from the retained Scene name.

Operations directly on a library file rather than a numbered directory, such as
standalone Instrument Save, always show the internal name as the editable name.

### OK/OW

`OW` should persist whenever the pending operation will overwrite an existing
object. It should not flash only at the moment of confirmation.

Rules:

- occupied Kit slot: `OW`;
- occupied Scene slot: `OW`;
- occupied Bank slot: `OW`;
- standalone Instrument Save where the target filename already exists in
  `Instrument/`: `OW`;
- future standalone Effect Save where the target filename already exists in its
  library: `OW`;
- otherwise `ok`.

For slot saves, `OW` depends on slot occupancy, not whether the edited name
matches the old name.

For file saves, `OW` depends on case-insensitive target filename existence after
the extension is added.

## Kit Save Naming

### Kit Folder Name

The folder name is:

```text
NNN <edited-kit-name>
```

`NNN` is always the three-digit slot number. The text after the space is the
user-entered character field exactly after filesystem sanitization for FAT
legality. Blank/all-space names remain blank/all-space names, so the visible
folder component is the three digits followed by one separator space.

Occupied slot save:

1. find the existing numbered directory for `NNN`;
2. rename it to `NNN <edited-kit-name>` if the visible component differs;
3. enter the directory using the current asyncfatfs open alias;
4. replace or create all member instrument files;
5. replace or create `kitset.kcg` after member aliases are known;
6. update Kit scan cache and retained Kit name only after successful completion.

Extra files inside the Kit folder are tolerated. The authoritative current Kit
state is defined by the rewritten member files and `kitset.kcg`, not by the
absence of unrelated leftovers.
### Kit Member Instrument Filenames

Kit Save writes member instruments from internally retained per-voice
Instrument names. It does not use the Kit save-name editor for member
instruments.

To avoid collisions when two voices have the same retained Instrument name, Kit
Save always writes the voice number into character 8 of each member stem.

Rules:

- voice number is `1` through `6`;
- character 8 is the voice number;
- if the retained name is shorter than 7 characters, pad with spaces until
  character 8;
- if the retained name is longer than 7 characters, character 8 is overwritten
  with the voice number;
- extension still comes from the instrument type;
- this rule applies only to Kit member instrument files.

Examples:

```text
Kick + voice 3      -> "Kick   3.drm"
LongNameHere + v2  -> "LongNam2.drm"
Hat + voice 6       -> "Hat    6.hat"
```

Standalone Instrument Save does not apply this rule. The user explicitly names
that one file and gets `OW` if it will overwrite.

## Existing Normal Load Behavior To Preserve

Normal Kit Load:

- loads `Kit/NNN Name/`;
- parses `kitset.kcg`;
- loads six listed Instrument files;
- replaces the selected resident Kit only after validation;
- updates retained Kit name from the directory name;
- updates retained per-Instrument names from member filenames.

Normal Instrument Load:

- loads one file from root `Instrument/`;
- stages outside live SceneData;
- commits only after validation;
- replaces the destination slot;
- updates that slot's retained Instrument name from the file stem;
- clears/rebinds runtime modulation as current Preset code already does.

Normal Scene Load, when promoted:

- loads `Scene/NNN Name/`;
- updates retained Scene name from the directory name;
- loads contained data according to the Scene format;
- does not invent names for sub-data except where those sub-loaders already
  derive names from their own file or directory names.

## Morph Load Semantics

Morph Load uses normal file data as the source and copies source normal
endpoints into resident morph endpoints.

Morph Load does not update retained names.

### Kit Morph Load

User-visible operation label:

```text
KitMrp
```

Current code already has most of this behavior:

- filesystem stages the selected Kit directory;
- filesystem does not replace live SceneData for `FS_INTERNAL_OP_LOAD_KIT_MORPH`;
- Preset copies same-type source normal endpoints into resident morph endpoints;
- mismatched source/destination instrument types are no-change;
- routing, display names, instrument source names, and modulation bindings are
  preserved.

Promotion work:

- make `KitMrp` reachable in the Load type cycle;
- keep it unreachable on Save until Morph Save is implemented;
- verify no retained names are updated on completion;
- verify active-scene Morph worker refresh remains bounded.

### Instrument Morph Load

User-visible operation labels append `Mrp` to the instrument type:

```text
DrumMrp
SnarMrp
CymMrp
HiHatMrp
```

Exact labels should use the existing Instrument type display helper and fit the
eight-character LCD type field.

Current code already has most of this behavior:

- the normal Instrument loader stages the selected file;
- Preset copies source normal endpoint values into the destination slot's morph
  image only when the staged type matches the resident slot type;
- mismatches are rejected/no-change;
- the destination slot type, retained name, routing, and modulation bindings are
  preserved.

Promotion work:

- expose Instrument Morph Load from nested Instrument Load type selection;
- make type-row cursor behavior clear;
- keep Morph load tied to the destination slot's current type;
- verify no retained Instrument name update occurs.

## Morph Save Semantics

Morph Save writes a normal Kit or Instrument file/directory, but with a special
save view of endpoint images.

Morph Save does not update retained names.

### Core Rule

For each morphable descriptor:

- file `[params]` receives the interpolated value at the current per-voice Morph
  amount;
- file `[morph]` receives the current normal endpoint value.

For non-morphable descriptors:

- write the ordinary single endpoint exactly as normal save does;
- do not invent a `[morph]` value for routing, target selectors, or other
  non-morphable setup cells.

For Kit slot 6 generated track-7 decay:

- apply the same rule as descriptor endpoints:
  - normal saved value is the current interpolated value;
  - morph saved value is the current normal endpoint.

The current per-voice Morph amount is `scene->settings.voice_morph_amount[slot]`.
Do not use hidden LFO Morph overlays for file save. LFO Morph modulation is a
runtime layer, not retained file state.

### Kit Morph Save

User-visible operation label:

```text
KitMrp
```

Save path:

1. target slot and edited Kit name follow normal Kit Save UI and overwrite
   rules;
2. if the target exists, rename the directory when the edited name changed;
3. otherwise create `Kit/NNN <edited-kit-name>/`;
4. write or replace six member Instrument files using Kit member filename
   collision rules;
5. each member file uses Morph Save endpoint mapping;
6. write or replace `kitset.kcg` after member aliases are known;
7. tolerate stale extra files in the Kit folder;
8. do not update retained Kit name or retained Instrument names on completion.

Open point for implementation detail:

- The writer can either build a temporary `kit_t` save image or add a
  `storage_formatInstrumentLine()` variant/context flag that resolves values
  from a save-view accessor.
- Prefer the approach that keeps storageTypes owning descriptor section rules
  and keeps filesystem.c unaware of descriptor counts.

### Instrument Morph Save

User-visible operation labels append `Mrp` to the instrument type:

```text
DrumMrp
SnarMrp
CymMrp
HiHatMrp
```

Save path:

1. nested Instrument Save selects source voice and edited filename;
2. type row chooses normal Instrument Save vs Instrument Morph Save;
3. target filename is the edited standalone Instrument name plus type extension;
4. `OW` shows if that file already exists under case-insensitive comparison;
5. replace the file if it exists, or create it if it does not;
6. write one Instrument file using Morph Save endpoint mapping;
7. do not update the retained Instrument name on completion.

Standalone Instrument Save keeps normal behavior and updates the retained
Instrument name only for normal save, not Morph save.

## Instrument Save Cursor Model

Nested Instrument Save should match Kit Save's cursor model, except there is no
slot-number field.

Fields:

1. save type row;
2. character-entry filename field;
3. `ok`/`OW` selector.

Normal Instrument Save type row:

```text
Save:[Drum    ]
Save:[Snare   ]
Save:[Cymbal  ]
Save:[HiHat   ]
```

Morph Instrument Save type row:

```text
Save:[DrumMrp ]
Save:[SnarMrp ]
Save:[CymMrp  ]
Save:[HiHatMrp]
```

The exact visible strings should be adjusted to the existing eight-character
LCD constraints and instrument display labels.

Cursor behavior:

- without edit mode, encoder moves between type, name, and OK/OW;
- in type edit mode, encoder toggles normal vs Morph save for the selected
  resident type;
- in name edit mode, encoder edits the current character;
- in OK/OW edit click, post the selected save request.

## Menu Reachability Order

After rename/replace support is implemented and tested, restore reachability in
this order:

1. keep File/Dir diagnostics reachable;
2. normal Kit Load and Save with directory rename and file replace behavior;
3. normal Instrument Load and Save with file-existence `OW`;
4. KitMrp Load;
5. InstrumentMrp Load;
6. KitMrp Save;
7. InstrumentMrp Save;
8. Scene Load/Save after Scene-specific audit;
9. Settings/Samples promotion as separate retest work;
10. Bank and Effect when their resident object models exist.

Do not promote a type merely because the enum exists. `menu.c` should continue
to own a whitelist of reachable entries.
## Implementation Work Plan

### Phase 1: asyncfatfs Rename/Replace Foundation

- Verify/preserve `afatfs_findNextObject()` structural-dot behavior: only
  synthetic `.` and `..` entries are hidden; ordinary dot-prefixed objects
  remain visible.
- Add LFN-aware rename-object support that preserves data clusters and child
  directory contents.
- Factor or reuse LFN create helpers for entry-count calculation, short-alias
  generation, display comparison, and LFN/SFN entry-run writing.
- Add filesystem-level save helpers that ensure a directory by product identity:
  create if absent, rename if present under the old display component, then open
  by the current alias.
- Keep ordinary file replacement on `afatfs_fopen_lfn(..., "w", ...)` unless
  coding exposes an LFN metadata gap.
- Test rename with File/Dir diagnostics or a controlled save path before musical
  save paths depend on it.

Acceptance:

- rename one root file with LFN and preserve contents;
- rename one root SFN-only file and preserve contents;
- rename one empty directory;
- rename one directory containing files and confirm children survive;
- rename to a longer LFN that needs a larger entry run;
- rename to a shorter LFN without leaving visible stale name fragments;
- reject rename onto an existing different object;
- confirm unrelated root objects remain visible.

### Phase 2: Normal Save Identity, UI, Rename, and Replace

This combines the previous Phases 2-5. Do this as one implementation pass
because the pieces are coupled:

- retained Kit/Scene/Instrument names decide the Save editor seed;
- slot occupancy and filename existence decide persistent `OK`/`OW`;
- the same edited name must be used by the menu request, filesystem rename,
  file overwrite, cache refresh, and retained-name commit;
- normal Kit Save and normal Instrument Save are the production proofs that the
  asyncfatfs rename/remove foundation is correct.

Work:

- Add resident retained Kit and Scene display-name fields in SceneData, plus
  small copy/set helpers. Instrument retained name storage already exists and
  stays display+stem because Kit member filenames need the 16-character stem.
- Split Save UI slot display from Save editor contents. Slot scrolling shows
  `Empty` or the scanned slot name, while entering character edit seeds from
  the retained object name.
- Preserve blank names. A blank root Kit name saves as `NNN ` and a blank
  retained name remains blank; the literal text `Empty` is just another name.
- Make `OK`/`OW` persistent and identity-based. Numbered slots show `OW` when
  occupied. Root Instrument Save shows `OW` when the target `stem.ext` exists
  under case-insensitive comparison.
- Replace root Kit Save's occupied-folder open path with ensure-by-slot:
  scan/find the numbered directory, rename it to the edited component when
  needed, then enter it.
- Collapse same-casefold Kit member files before writing each member, then
  create one replacement file with the generated visible case.
- Collapse same-casefold root Instrument files before writing, then create one
  replacement file with the edited visible case.
- Update browser caches after successful saves so the in-RAM UI immediately
  mirrors the on-card case-insensitive overwrite result.
- Update retained Kit/Scene/Instrument names only after successful normal save.
  Morph load/save must not update retained names.

Acceptance:

- Save slot scroll shows `Empty` for absent slots, but entering the name field
  shows the retained Kit/Scene/Instrument name, not the slot display sentinel.
- Blank Kit save name creates or renames the root folder to `NNN `.
- Occupied Kit slot always shows `OW`, even when the edited name equals the
  existing slot display.
- Empty Kit slot shows `ok`.
- Occupied Kit save changes the folder name to the edited name while preserving
  child directory contents not owned by the save.
- Kit Save writes exactly one file for each expected member Instrument and one
  `kitset.kcg`; duplicate retained Instrument names become voice-numbered
  member filenames.
- Root Instrument Save shows `OW` for case-only matches such as `Kick.drm` vs
  `kick.drm`.
- Root Instrument overwrite leaves one visible target file whose case matches
  the newly entered display component.
- Reboot/rescan shows the saved Kit/Instrument under the new visible name.
- Successful normal saves update only the saved object's retained name.

### Phase 3: Morph Load and Morph Save

This rolls the previous Phase 6 and Phase 7 into one later Morph phase. Do not
expand or implement it until Phase 2 is complete and tested; Morph Save must use
the exact same directory ensure/rename and file replace helpers created for
normal saves.

Deferred work:

- Promote KitMrp and InstrumentMrp Load reachability.
- Preserve existing Preset same-type morph endpoint copy semantics.
- Add KitMrp and InstrumentMrp Save request paths.
- Add storageTypes save-view support for Morph Save endpoint mapping.
- Keep retained names unchanged for every Morph operation.

Acceptance will be expanded after the normal-save phase lands.

## Real Code Dive: asyncfatfs Rename/Overwrite Implementation

This section is based on the current source tree, not on the spec alone. The
source already contains partial rename infrastructure:

- `Core/Hardware/SD/asyncfatfs/asyncfatfs.h` already declares
  `afatfs_renameObject_lfn()`.
- `Core/Hardware/SD/asyncfatfs/asyncfatfs.c` already has
  `afatfsRenameObject_t`, `afatfs_renameObjectContinue()`, sector-local
  in-place/moved-run rename, and LFN/SFN entry-run rewrite helpers.
- `afatfs_findNextObject()` already hides only structural `.` / `..` entries
  through `afatfs_isStructuralDotEntry()`. Dot-prefixed user objects are already
  visible.
- `afatfs_funlink()` only deletes the owning SFN entry after truncation. It does
  not retire the preceding VFAT LFN fragments, so it is not sufficient for the
  new overwrite rule.
- Production callers in `filesystem.c` still pass `AFATFS_MATCH_CASE_SENSITIVE`
  to most LFN operations. The new product behavior requires
  `AFATFS_MATCH_CASE_INSENSITIVE` for user-facing production saves/loads while
  preserving the entered display case on write.

### Duplicate Case-Fold Policy In Code

The implementation must treat same-casefold variants as one product object:

- Scans expose only the first variant after product sorting.
- Product sorting is case-fold primary, then raw ASCII display order as the
  tiebreaker. That makes capital letters sort before lowercase for the same
  character, so `fiRstfile.snr` sorts before `firStfile.snr`.
- Later same-casefold variants are ignored by browsers/loaders.
- Overwrite removes all same-casefold file variants before writing the new file,
  leaving one visible object with the newly entered case.

### `fat_standard.h/c` Changes

Add one shared display comparison helper for product/browser ordering. Existing
`fat_compareDisplayName()` folds case but returns `0` for same-casefold
variants, so it cannot decide which duplicate variant should win display order.

New helper:

```c
int8_t fat_compareDisplayNameCasefoldThenCase(const char *a, const char *b);
```

Comment block to add beside the declaration in `fat_standard.h`:

```c
/*
 * Compare two FAT display components for product browser order.
 *
 * What: Sorts by ASCII case-folded text first, then by the original display
 * bytes when the folded text is identical. The raw-byte tiebreaker makes
 * uppercase letters sort before lowercase letters for the same character.
 *
 * Why: User-facing load/save is case-insensitive but case-preserving. If an
 * externally edited card contains `fiRstfile.snr` and `firStfile.snr`, both
 * names match the same product object. The browser must expose exactly one
 * deterministic winner, and the capital-letter-first tiebreaker chooses
 * `fiRstfile.snr`.
 *
 * Inputs: NUL-terminated ASCII display components returned by asyncfatfs object
 * iteration or built by storage save-name helpers. These are components, not
 * paths.
 *
 * Output: strcmp-style ordering. Zero means byte-identical display text, not
 * merely same-casefold text.
 *
 * Affiliates/clients: filesystem.c Instrument browser insertion, File/Dir
 * diagnostics if sorted duplicate hiding is added there, Kit/Scene duplicate
 * slot arbitration, and overwrite duplicate collection.
 */
```

Implementation comments to add in `fat_standard.c`:

```c
/*
 * First pass: compare case-folded bytes.
 *
 * This pass implements the universal case-insensitive product identity. If the
 * folded bytes differ, the names are different product objects and normal
 * alphabetical order decides their relative position.
 */
```

```c
/*
 * Second pass: compare original bytes only after folded equality.
 *
 * This does not make the product identity case-sensitive. It only chooses the
 * display winner when an externally edited card already contains duplicate
 * same-casefold names. ASCII uppercase letters sort before lowercase letters,
 * matching the required capital-first policy.
 */
```

No existing caller of `fat_compareDisplayName()` should be silently changed to
the new helper. `fat_compareDisplayName()` remains the open/match predicate;
the new helper is for sorting and duplicate-winner selection.

### `asyncfatfs.h` Changes

Revise the existing rename comment and add an overwrite/delete primitive for
same-casefold file cleanup. The current `afatfs_funlink()` requires an open file
handle and deletes only the SFN entry, so it cannot implement "remove every
same-casefold LFN object before writing."

New public primitive:

```c
typedef enum {
    AFATFS_REMOVE_FILES_ONLY = 0,
    AFATFS_REMOVE_EMPTY_DIRECTORIES,
} afatfsRemoveObjectMode_t;

bool afatfs_removeObjects_lfn(const char *displayName,
                              afatfsMatchMode_t matchMode,
                              afatfsRemoveObjectMode_t mode,
                              afatfsCallback_t complete);
```

Header comment for `afatfs_renameObject_lfn()`:

```c
/*
 * Rename one object in the current directory by display component.
 *
 * What: Starts an asynchronous rename of one file or directory named by a
 * single visible component. Matching follows matchMode; production callers use
 * case-insensitive matching so a case-only save can refresh visible casing. The
 * operation updates the complete VFAT LFN/SFN name entry run and returns the
 * new asyncfatfs-openable short alias in openNameOut when requested.
 *
 * Why: Numbered directory saves must change `NNN OldName/` into
 * `NNN NewName/` while preserving children. The slot number is the product
 * identity; the FAT directory entry run is only visible metadata plus the
 * short alias needed by existing open paths.
 *
 * Inputs: oldDisplayName and newDisplayName are current-directory components,
 * not paths. openNameOut may be NULL. complete fires after success or failure;
 * callers inspect openNameOut[0] or their outer filesystem state to decide
 * whether the rename succeeded.
 *
 * Outputs/effects: first cluster, file size, attributes, timestamps, and
 * directory children are preserved. Only the object name entry run changes.
 *
 * Affiliates/clients: filesystem.c Kit, KitMrp, Scene, Bank, and future Effect
 * directory-shaped save phases; asyncfatfs object scanning; LFN/SFN name
 * generation helpers.
 */
```

Header comment for `afatfs_removeObjects_lfn()`:

```c
/*
 * Remove all objects whose display name matches one component.
 *
 * What: Scans the current directory and removes every object whose visible
 * display component matches displayName under matchMode. File removal frees the
 * file's cluster chain and retires the full VFAT LFN/SFN entry run. Directory
 * removal is limited by mode and must never recursively delete children.
 *
 * Why: Product overwrite is case-insensitive and case-preserving. If an
 * external filesystem created `Kick.drm` and `kick.drm`, saving `KiCk.drm`
 * must remove both old physical variants before writing one new object with the
 * user's entered case. afatfs_funlink() cannot do this because it needs an open
 * handle and deletes only the SFN entry.
 *
 * Inputs: displayName is a single component in the current directory.
 * AFATFS_REMOVE_FILES_ONLY is used before Instrument and Kit member file
 * writes. AFATFS_REMOVE_EMPTY_DIRECTORIES is reserved for directory-shaped save
 * cleanup when recursive delete is not available.
 *
 * Outputs/effects: callbacks fire once the scan has reached the end or failed.
 * A successful no-op is allowed when no matching object exists. The operation
 * restarts its scan after each deletion because retiring entries mutates the
 * directory being scanned.
 *
 * Affiliates/clients: filesystem.c file overwrite preflight, duplicate
 * case-fold cleanup, future recursive directory deletion, FAT chain truncate
 * helpers, and VFAT entry-run retirement helpers.
 */
```

### `asyncfatfs.c` Changes

#### 1. Rename comments and matching

Update the existing rename implementation comments to describe
case-insensitive production matching and case-preserving output. In the current
source, rename collision matching already uses `op->matchMode`, so production
callers can switch to `AFATFS_MATCH_CASE_INSENSITIVE` once duplicate cleanup
exists.

Important in-place comments to add in `afatfs_renameObjectContinue()`:

```c
/*
 * Source scan: match the old display component under the caller's policy.
 *
 * Production Kit/Scene saves pass case-insensitive matching because an
 * occupied slot is the same product object even if the on-card component case
 * differs from the user-entered replacement. Exact diagnostics may still pass
 * case-sensitive matching when they need to probe raw VFAT behavior.
 */
```

```c
/*
 * Same-display fast path.
 *
 * Under case-insensitive matching, folded equality is not enough to skip work:
 * a case-only rename must still rewrite the LFN/SFN run so the visible card
 * name changes to newName. Only byte-identical display text can use this
 * success shortcut.
 */
```

That second comment points at a real code change: the current source uses
`fat_compareDisplayName(..., op->matchMode == AFATFS_MATCH_CASE_SENSITIVE) == 0`
for the same-name fast path. For production case-insensitive calls, this would
skip a case-only casing refresh. Change this fast path to byte-exact comparison,
for example:

```c
if (fat_compareDisplayName(op->source.displayName,
                           op->newName,
                           true) == 0) {
    ...
}
```

#### 2. Add reusable entry-run retirement helper

The rename code already has `afatfs_renameObjectRetireOldRun()`, but duplicate
overwrite and future delete need the same behavior without a rename operation.
Factor a generic helper:

```c
static afatfsOperationStatus_e afatfs_retireObjectNameRun(
        const afatfsObjectInfo_t *object);
```

Comment block:

```c
/*
 * Retire one object's complete VFAT name entry run.
 *
 * What: Marks the checksum-verified LFN fragments and the owning SFN entry as
 * deleted. It operates only on the directory entries that name the object.
 *
 * Why: Removing or moving a VFAT object must not leave orphan display fragments
 * visible to later scans. afatfs_funlink() currently marks only the SFN entry,
 * which is acceptable for old short-name files but not for case-preserving LFN
 * overwrite.
 *
 * Inputs: afatfsObjectInfo_t from afatfs_findNextObject(). Its lfnFirstEntry,
 * lfnEntryCount, and sfnEntry identify the entry run. The helper requires the
 * current sector-local run shape used by the existing LFN writer.
 *
 * Outputs/effects: directory cache sector is marked dirty after entries are
 * marked deleted. It does not free clusters and does not inspect directory
 * children.
 *
 * Affiliates/clients: afatfs_renameObjectRetireOldRun(),
 * afatfs_removeObjects_lfn(), future recursive delete, and save overwrite
 * preflight.
 */
```

#### 3. Add object removal state machine

Add a filesystem-global `afatfsRemoveObjects_t` beside `afatfsRenameObject_t`.
It should not consume an `openFiles[]` slot while scanning, but it may use a
private synthetic `afatfsFile_t` while freeing one file's cluster chain through
the existing truncate logic.

New state fields:

```c
typedef enum {
    AFATFS_REMOVE_OBJECTS_PHASE_INITIAL = 0,
    AFATFS_REMOVE_OBJECTS_PHASE_SCAN,
    AFATFS_REMOVE_OBJECTS_PHASE_LOAD_ENTRY,
    AFATFS_REMOVE_OBJECTS_PHASE_TRUNCATE_FILE,
    AFATFS_REMOVE_OBJECTS_PHASE_RETIRE_NAME_RUN,
    AFATFS_REMOVE_OBJECTS_PHASE_RESTART_SCAN,
    AFATFS_REMOVE_OBJECTS_PHASE_FINISH,
} afatfsRemoveObjectsPhase_e;

typedef struct afatfsRemoveObjects_t {
    uint8_t active;
    uint8_t succeeded;
    afatfsRemoveObjectsPhase_e phase;
    afatfsMatchMode_t matchMode;
    afatfsRemoveObjectMode_t mode;
    afatfsCallback_t callback;
    char displayName[AFATFS_LONG_FILENAME_MAX + 1u];
    afatfsObjectFinder_t finder;
    afatfsObjectInfo_t object;
    fatDirectoryEntry_t sourceEntry;
    afatfsFile_t syntheticFile;
} afatfsRemoveObjects_t;
```

Struct comment:

```c
/*
 * Global same-name object removal operation.
 *
 * What: Owns the asynchronous scan/delete loop used before case-insensitive
 * overwrite. It scans the current directory, removes one matching physical
 * object, restarts the scan, and repeats until no matches remain.
 *
 * Why: FAT directories can contain externally-created names that differ only by
 * case. Product overwrite must collapse those variants into one newly written
 * object. Restarting after each deletion avoids keeping a raw finder cursor
 * alive across mutations to the same directory sector.
 *
 * Inputs retained here: displayName and matchMode define the product identity;
 * mode restricts whether directories are ignored or only empty directories may
 * be retired. callback returns control to filesystem.c once all matching
 * objects are gone.
 *
 * Outputs/effects: file cluster chains are freed through the existing truncate
 * code path, and complete LFN/SFN name runs are retired through the shared
 * name-run helper. The syntheticFile never escapes asyncfatfs.
 *
 * Affiliates/clients: afatfs_poll(), afatfs_findNextObject(),
 * afatfs_ftruncateContinue(), afatfs_retireObjectNameRun(), filesystem.c
 * overwrite preflight.
 */
```

Important loop/variable comments inside the state machine:

```c
/*
 * Match under the requested policy, not raw byte equality.
 *
 * Production overwrite passes case-insensitive matching so every case variant
 * of the same user-facing filename is removed. Exact diagnostics can still pass
 * case-sensitive matching to test a single physical display component.
 */
```

```c
/*
 * Directory handling is intentionally conservative.
 *
 * Removing a non-empty directory requires recursive traversal, which is a
 * separate primitive. For this Morph/Kit pass, file overwrite removes duplicate
 * files completely; directory-shaped saves preserve the selected directory and
 * ignore later duplicate directories until recursive delete exists.
 */
```

```c
/*
 * Build a synthetic file handle from the source SFN entry.
 *
 * The truncate code already knows how to release a file cluster chain and mark
 * one SFN entry deleted. This synthetic handle supplies exactly the metadata it
 * expects: directoryEntryPos, firstCluster, cursorCluster, logicalSize,
 * physicalSize, type, attrib, and a truncate operation state. The handle is
 * private to the remove operation and is never returned to callers.
 */
```

```c
/*
 * Retire the LFN/SFN entry run after cluster release.
 *
 * Cluster release must happen first for files so no newly-created object can
 * reuse a directory entry that still points at live clusters. The shared run
 * helper then removes both LFN fragments and the already-deleted SFN entry from
 * the visible directory namespace.
 */
```

```c
/*
 * Restart the scan after every deletion.
 *
 * Directory sectors have just been mutated and the previous finder may point
 * into an entry run that no longer exists. Restarting is cheaper and safer than
 * trying to repair raw scan state in place, and overwrite cleanup is a bounded
 * foreground filesystem operation rather than an audio-thread path.
 */
```

Add `afatfs_removeObjectsContinue()` to `afatfs_poll()` beside
`afatfs_renameObjectContinue()`.

#### 4. Create/open case-preserving write behavior

Product callers should not rely on `afatfs_fopen_lfn(..., "w", insensitive)` to
both collapse duplicates and rewrite case. Instead:

1. call `afatfs_removeObjects_lfn(target, AFATFS_MATCH_CASE_INSENSITIVE,
   AFATFS_REMOVE_FILES_ONLY, cb)`;
2. then call `afatfs_fopen_lfn(target, "w", AFATFS_MATCH_CASE_INSENSITIVE, ...)`.

Still update comments in `afatfs_createFileContinue()` where they refer to
"current exact-case policy"; production now uses case-insensitive matching.

Comment replacement:

```c
/*
 * Alias collision is useful only when creation is allowed.
 *
 * A read-only LFN open cannot resolve a display-name miss by inventing a new
 * "~N" alias. Write/create callers may generate the next alias candidate after
 * confirming the visible display name did not match under their match policy.
 * Product overwrite removes same-casefold physical duplicates before it reaches
 * this create path, so a new object created here is the single surviving visible
 * variant.
 */
```

### `filesystem.c` Changes

Historical note: this subsection was written during the asyncfatfs foundation
planning pass. It records the original integration intent, but the exact
next-pass implementation plan is now the later section
`Real Code Dive: Combined Normal Save Integration (Phase 2)`. When details
conflict, follow the Phase 2 section.

#### 1. Add overwrite-preflight scratch

Add generic save scratch for remove/rename callbacks:

```c
static uint8_t op_remove_done = 0u;
static char op_save_kit_existing_display_name[AFATFS_LONG_FILENAME_MAX + 1u];
```

Callback comment:

```c
/*
 * Mark completion of an asyncfatfs object-removal preflight.
 *
 * What: Records that afatfs_removeObjects_lfn() finished. The operation itself
 * reports success/failure through the outer filesystem phase by whether the
 * following create/open succeeds.
 *
 * Why: File overwrite is now a two-step sequence: remove all same-casefold
 * physical variants, then create one object with the user's entered case. The
 * filesystem state machine needs a tiny callback latch between those steps.
 *
 * Affiliates/clients: Kit member file save, root Instrument save, future
 * Scene/effect/pattern standalone file saves.
 */
```

#### 2. Kit save directory ensure/rename

Replace phase 6 of `filesystem_saveKitDirectory_tick()`. Current code opens an
occupied slot by `kit_slot_open_name[op_slot]` and explicitly avoids renaming.
New behavior:

1. In `Kit/`, scan objects for the numbered slot `NNN`.
2. Choose the first matching slot directory by product ordering if duplicates
   exist. Later duplicate directories are ignored for now because non-empty
   recursive deletion is not part of this primitive.
3. If the chosen visible display component differs byte-for-byte from
   `op_save_kit_display_name`, call
   `afatfs_renameObject_lfn(chosenDisplay, op_save_kit_display_name,
   AFATFS_MATCH_CASE_INSENSITIVE, op_save_kit_dir_name, cb)`.
4. If absent, call `afatfs_mkdir_lfn(op_save_kit_display_name,
   AFATFS_MATCH_CASE_INSENSITIVE, op_save_kit_dir_name, cb)`.
5. Open/chdir using the returned alias.

Comment block for the slot scan helper:

```c
/*
 * Find the canonical directory object for one numbered Kit slot.
 *
 * What: Scans the current Kit/ directory for directory objects whose visible
 * name or compatibility short alias parses as the requested `NNN` slot.
 *
 * Why: Slot number is product identity. An occupied save must rename that
 * directory to the edited display component before rewriting child files,
 * instead of creating a duplicate or preserving the old visible name.
 *
 * Inputs: op_slot is the requested root Kit slot; afatfs current directory is
 * already Kit/. Outputs: op_save_kit_existing_display_name receives the visible
 * source component, and op_save_kit_dir_name receives its open alias.
 *
 * Duplicate policy: if external editing created several same-slot variants,
 * choose the one that sorts first by case-folded text with raw ASCII as a
 * tiebreaker. Later duplicate directories are hidden from product behavior
 * until recursive directory delete is implemented.
 *
 * Affiliates/clients: filesystem_saveKitDirectory_tick(), KitMrp save, Scene
 * embedded Kit save, asyncfatfs rename, storage_parseNumberedFolder().
 */
```

Comment inside duplicate choice comparison:

```c
/*
 * Keep the earliest display variant by product order.
 *
 * This implements the capital-first duplicate policy for externally-created
 * same-casefold names. It does not make the slot identity case-sensitive; it
 * only chooses which physical directory is the visible/editable representative.
 */
```

#### 3. Kit member file overwrite

Before phase 16 opens each member file, insert a remove phase:

```c
afatfs_removeObjects_lfn(op_save_instrument_display_file[op_instrument_slot],
                         AFATFS_MATCH_CASE_INSENSITIVE,
                         AFATFS_REMOVE_FILES_ONLY,
                         on_remove_complete);
```

Then call `afatfs_fopen_lfn(..., "w", AFATFS_MATCH_CASE_INSENSITIVE, ...)`.

Comment block beside the remove phase:

```c
/*
 * Collapse same-casefold member-file variants before writing.
 *
 * What: Deletes every physical file in the Kit directory whose display name
 * matches the target member filename under case-insensitive comparison.
 *
 * Why: `Kick.drm` and `kick.drm` can exist only after external filesystem
 * edits, but product save must treat them as one object. Removing all variants
 * before fopen_lfn("w") guarantees the saved Kit contains exactly one member
 * file with the case generated from retained Scene metadata.
 *
 * Inputs: op_save_instrument_display_file[op_instrument_slot] is the visible
 * target name; op_instrument_slot identifies which kit member is being written.
 *
 * Outputs/effects: duplicate files are removed from FAT and the following
 * fopen_lfn() creates the single authoritative file. Directories with the same
 * folded component are ignored by AFATFS_REMOVE_FILES_ONLY.
 *
 * Affiliates/clients: afatfs_removeObjects_lfn(), afatfs_fopen_lfn(),
 * storage_formatInstrumentLine(), kitset.kcg alias collection.
 */
```

#### 4. Root Instrument save overwrite

Add the same remove-before-write phase before
`filesystem_saveInstrument_tick()` opens `op_instrument_save_display_name`.
Switch production LFN calls in this writer from `AFATFS_MATCH_CASE_SENSITIVE` to
`AFATFS_MATCH_CASE_INSENSITIVE`.

Comment block:

```c
/*
 * Remove case-variant Instrument files before saving one root Instrument.
 *
 * What: In Instrument/, removes every file whose display component matches the
 * requested target under case-insensitive comparison.
 *
 * Why: The root Instrument pool is user-copyable from desktop filesystems. If a
 * card contains both `fiRstfile.snr` and `firStfile.snr`, the browser exposes
 * only the capital-first winner, and overwrite must collapse all physical
 * variants into the newly entered case.
 *
 * Inputs: op_instrument_save_display_name is the captured target filename from
 * Menu after extension/type construction. Output: the following fopen_lfn()
 * writes one replacement file and returns its short alias for cache update.
 *
 * Affiliates/clients: filesystem_updateInstrumentCacheAfterSave(),
 * afatfs_removeObjects_lfn(), afatfs_fopen_lfn(), nested Instrument Save UI.
 */
```

#### 5. Instrument browser duplicate suppression

Change `filesystem_compareInstrumentDisplayName()` to use
`fat_compareDisplayNameCasefoldThenCase()`, or replace its local logic with the
same two-pass comparison. Add a duplicate-fold check in
`filesystem_recordInstrumentFile()`:

- compute display stem;
- if an existing cached row has the same casefolded stem and same type, keep the
  row whose full visible filename sorts first by casefold+case;
- do not increment count for hidden duplicates.

Comment block:

```c
/*
 * Suppress same-casefold Instrument browser duplicates.
 *
 * What: Before inserting a scanned Instrument/ object, compare it against
 * existing cached rows for the same instrument type. If the display stem matches
 * case-insensitively, keep only the filename that sorts first by folded text and
 * raw ASCII case.
 *
 * Why: Externally edited FAT cards may contain names that differ only by case.
 * Product policy treats those as one object; later variants must not appear in
 * the Load UI even though asyncfatfs reports every physical FAT object.
 *
 * Inputs: display_name and open_name come from afatfs_findNextObject().
 * Outputs: the per-type cache either keeps its existing representative,
 * replaces it with an earlier-sorting variant, or inserts a new product object.
 *
 * Affiliates/clients: filesystem_requestScanInstruments(), nested Instrument
 * Load, root Instrument Save cache update, fat_compareDisplayNameCasefoldThenCase().
 */
```

#### 6. Kit/Scene scan duplicate handling and blank names

`storage_parseNumberedFolder()` currently rejects blank names after `NNN `.
Change it so `NNN ` is valid and copies an all-space display name. Then change
`filesystem_makeKitDirectoryDisplayName()` so it no longer substitutes `Kit`
when the edited name is blank.

This affects root numbered Kit folders only. Scene embedded Kits keep the
separate `Kit <kit-name>` directory convention.

Comment block for `storage_parseNumberedFolder()`:

```c
/*
 * Accept a blank numbered-folder display name.
 *
 * What: `NNN ` is a valid numbered folder component. The parsed display field
 * becomes eight spaces, matching the retained internal blank name.
 *
 * Why: Blank is a real user-entered name, not an empty slot sentinel. Slot
 * occupancy comes from the numeric prefix and validated folder contents, while
 * the UI string `Empty` is only the display for an absent slot.
 *
 * Inputs: FAT display component beginning with three digits and a space or
 * underscore. Outputs: slot receives 000..999 directly; display receives the
 * post-separator text padded to the fixed LCD width, or all spaces when blank.
 *
 * Affiliates/clients: filesystem_recordKitDirectory(),
 * filesystem_recordSceneDirectory(), Kit/Scene Save UI, retained-name storage.
 */
```

Comment block for `filesystem_makeKitDirectoryDisplayName()`:

```c
/*
 * Preserve blank Kit save names.
 *
 * What: Builds a root numbered Kit folder component: `NNN ` plus exactly the
 * sanitized edited Kit name. If every name character is blank, the component
 * ends after the separator space.
 *
 * Why: The internal retained Kit name can be blank. Falling back to `Kit` would
 * silently rename a real blank root Kit save and break the Save UI contract
 * that `Empty` means absent slot, not blank-named slot. This does not apply to
 * Scene embedded Kits, whose directory name is always `Kit <kit-name>`.
 *
 * Inputs: op_slot is the direct 000..999 library slot; preset_currentName is
 * the fixed-width editable retained Kit name. Output: LFN display component
 * passed to asyncfatfs rename/mkdir.
 *
 * Affiliates/clients: filesystem_saveKitDirectory_tick(), KitMrp Save, Kit
 * scan cache update, storage_parseNumberedFolder().
 */
```

#### 7. Cache update after overwrite

Update `filesystem_updateInstrumentCacheAfterSave()` to remove cache rows by
case-insensitive filename/stem match, not `strcmp()`/`strncmp()` only. Update
Kit save cache update to always store the new display name after successful
rename.

Comment block:

```c
/*
 * Refresh browser cache after case-insensitive overwrite.
 *
 * What: Removes every cached row whose filename or display stem matches the
 * saved target under case-insensitive comparison, then inserts the one returned
 * by the completed save.
 *
 * Why: The SD card has already collapsed same-casefold physical files into one
 * visible object. The in-RAM browser cache must mirror that immediately so the
 * next nested load cannot select a stale duplicate alias.
 *
 * Inputs: display_name is the target case just written; open_name is the short
 * alias returned by asyncfatfs. Outputs: per-type Instrument cache contains one
 * row for the saved object.
 *
 * Affiliates/clients: filesystem_saveInstrument_tick(),
 * filesystem_recordInstrumentFile(), nested Instrument Load.
 */
```

### `menu.c` Changes Tied To Overwrite

Historical note: this subsection is superseded by the later Phase 2 Menu plan,
which includes the exact Instrument target-existence helper and Save editor
seeding changes.

The current `menu_currentSaveWouldOverwrite()` returns `OW` for Kit/Scene only
when the occupied slot name differs from `preset_currentName`. Change it to:

- return `OW` for any occupied Kit slot;
- return `OW` for any occupied Scene slot;
- add nested Instrument Save `OW` after filesystem exposes a
  case-insensitive target-exists helper for `Instrument/<stem.ext>`;
- leave File/Dir diagnostics as their own behavior unless they are promoted to
  product semantics.

Comment block:

```c
/*
 * Compute persistent overwrite display from product identity.
 *
 * What: Returns nonzero whenever the pending Save action targets an existing
 * product object. For numbered saves, the slot number is the identity. For root
 * Instrument Save, the target filename plus type extension is matched
 * case-insensitively inside Instrument/.
 *
 * Why: `OW` is a warning that data will be replaced, not a warning that the
 * edited display name differs. Saving Kit slot 042 over Kit slot 042 overwrites
 * whether the name is unchanged, case-only changed, or completely different.
 *
 * Inputs: current Save page mode, selected save type, selected slot or nested
 * Instrument source, and current edit-name buffer. Output: nonzero paints `OW`;
 * zero paints `ok`.
 *
 * Affiliates/clients: menu_repaintGeneric(), filesystem slot caches,
 * future Instrument filename existence helper, Load/Save type whitelist.
 */
```

## Implementation Notes: asyncfatfs LFN Rename/Replace Pass

Status after the current code pass:

- Added `fat_compareDisplayNameCasefoldThenCase()` in `fat_standard.c/.h`.
  It gives the product browser/order rule requested for externally-created
  same-casefold duplicates: compare folded text first, then raw ASCII display
  bytes so uppercase variants sort before lowercase variants. This is a sorting
  and representative-selection helper, not an identity helper; same-casefold
  names still identify the same product object.
- Added `afatfsRemoveObjectMode_t` and `afatfs_removeObjects_lfn()` in
  `asyncfatfs.h`, with comments documenting the file-only production overwrite
  scope and the reserved empty-directory mode.
- Added a global async remove-by-display-name operation in `asyncfatfs.c`.
  It scans `afatfs.currentDirectory` with `afatfs_findNextObject()`, matches
  visible names under the caller's case policy, truncates matching files through
  the existing FAT-chain release path, retires the full VFAT LFN/SFN name run,
  then restarts scanning until no matching file remains. Non-matching objects
  yield after one object so the operation stays paced by `afatfs_poll()`.
- Added `afatfs_retireObjectNameRun()` as the shared helper for deleting every
  directory entry in a checksum-verified object name run. Rename now uses this
  helper when it moves an object to a new run; remove uses it after file cluster
  release.
- Hardened `afatfs_renameObject_lfn()` so byte-identical old/new display text
  is the only no-op. Case-only renames now rewrite the LFN/SFN run, which is
  required for case-preserving saves.
- Added `afatfs_removeObjectsContinue()` to `afatfs_poll()` beside the rename
  state machine.

Intentional limitations in this pass:

- Directory deletion remains disabled. `AFATFS_REMOVE_EMPTY_DIRECTORIES` is
  documented as reserved until an emptiness check or recursive delete primitive
  lands. This keeps duplicate non-empty Kit/Scene directories out of the file
  overwrite primitive and preserves the selected directory for rename-in-place.
- The current LFN retire/rename helpers still require the same sector-local LFN
  run shape as the existing LFN writer. Cross-sector LFN entry runs remain a
  future asyncfatfs expansion.
- filesystem.c is not yet wired to call `afatfs_removeObjects_lfn()` before
  save writes, nor to use the rename path for occupied numbered Kit folders.
  That integration is the next pass.

Verification notes:

- Source-level checks confirmed the remove path can safely reuse
  `afatfs_ftruncate()`: its internal zero seek completes atomically and does not
  replace the queued truncate operation.
- This shell does not have `make`, `gcc`, or `arm-none-eabi-gcc` on PATH, so no
  compiler/build pass was possible here. Run the normal `make`/`make img`
  checklist on a toolchain-equipped machine before hardware testing.

## Real Code Dive: Combined Normal Save Integration (Phase 2)

This section expands the combined normal-save phase that replaces old Phases
2-5. It is based on the current source tree after the asyncfatfs rename/remove
pass.

The pass is deliberately not Morph work. It creates the normal-save semantics
that Morph Save will later reuse.

### Current Source Facts

- `SceneData.h` already retains per-Instrument names in `kit_t`:
  `instrument_display_name[slot][9]` and
  `instrument_stem[slot][SCENE_INSTRUMENT_STEM_LEN + 1u]`.
- `kit_t` does not yet retain the Kit's own display name.
- `scene_t` does not yet retain the Scene's own display name.
- `preset_currentName[8]` is still the Save page edit buffer and is also used
  by older load-name behavior. It is not sufficient as resident object identity.
- `filesystem_saveKitDirectory_tick()` phase 6 currently opens an occupied Kit
  slot through `kit_slot_open_name[op_slot]` and intentionally does not rename
  the directory.
- `filesystem_makeKitDirectoryDisplayName()` currently substitutes `Kit` when
  the edited name is blank. This conflicts with the confirmed `NNN ` blank-name
  rule.
- Kit member file save and root Instrument save currently use
  `AFATFS_MATCH_CASE_SENSITIVE` and do not call `afatfs_removeObjects_lfn()`
  before writing.
- `filesystem_recordInstrumentFile()` inserts every scanned physical file. It
  does not suppress same-casefold duplicate stems.
- `filesystem_updateInstrumentCacheAfterSave()` removes cache rows by exact
  alias or exact fixed display stem; it does not collapse case variants.
- `menu_currentSaveWouldOverwrite()` returns `OW` for Kit/Scene only when the
  occupied slot name differs from `preset_currentName`; it returns no `OW` for
  root Instrument Save.
- `filesystem.h` does not expose a root Instrument target-existence helper, so
  Menu cannot compute persistent Instrument `OW` without new API.

### `SceneData.h` Changes

Add one shared display-name length for retained object names near the existing
stem length:

```c
#define SCENE_OBJECT_DISPLAY_NAME_LEN 8u
```

Keep `SCENE_INSTRUMENT_STEM_LEN` unchanged. Do not add a Kit stem unless a
future feature needs it; Kit and Scene normal saves only need the eight
character editor/display field today.

Add Kit retained display name before the existing per-Instrument retained names:

```c
    /*
     * Retained Kit display name.
     *
     * What: Eight printable characters plus NUL naming the resident Kit for
     * Save editor seeding. This is storage/UI metadata, not a DSP parameter and
     * not a filename alias.
     *
     * Why: Slot browsing displays on-card Kit/NNN names, while Save character
     * entry must start from the currently loaded or last-saved resident Kit
     * identity. Keeping that identity in SceneData prevents the Save UI from
     * mistaking an empty slot display for the Kit's internal name.
     *
     * Inputs: normal Kit Load and successful normal Kit Save. Morph Load/Save
     * must not write this field. Output clients: Menu Save editor seeding,
     * root Kit Save folder-name construction, and future Scene embedded Kit
     * naming.
     */
    char display_name[SCENE_OBJECT_DISPLAY_NAME_LEN + 1u];
```

Add Scene retained display name inside `scene_t`, before `settings`:

```c
    /*
     * Retained Scene display name.
     *
     * What: Eight printable characters plus NUL naming the resident Scene for
     * Save editor seeding and sceneset.scg output.
     *
     * Why: Root Scene slot display is on-card library state. The resident Scene
     * needs its own name so saving to an empty or differently named slot does
     * not derive identity from the slot browser sentinel.
     *
     * Inputs: normal Scene Load and successful normal Scene Save. Morph
     * operations must not write this field. Output clients: Save UI,
     * filesystem_requestSaveSceneDirectory(), and future Bank Scene lists.
     */
    char display_name[SCENE_OBJECT_DISPLAY_NAME_LEN + 1u];
```

Add public setter declarations after the existing `scene_selectActive()` block:

```c
void scene_setKitDisplayName(kit_t *kit, const char name[SCENE_OBJECT_DISPLAY_NAME_LEN]);
void scene_setResidentKitDisplayName(uint8_t scene_index,
                                     const char name[SCENE_OBJECT_DISPLAY_NAME_LEN]);
void scene_setSceneDisplayName(uint8_t scene_index,
                               const char name[SCENE_OBJECT_DISPLAY_NAME_LEN]);
const char *scene_kitDisplayName(uint8_t scene_index);
const char *scene_sceneDisplayName(uint8_t scene_index);
```

Header comment for `scene_setKitDisplayName()`:

```c
/*
 * Retain one Kit display name in Kit-owned storage.
 *
 * What: Copies exactly eight display cells into kit->display_name, sanitizing
 * non-printable bytes to spaces and appending NUL.
 *
 * Why: Kit name is resident data, separate from a root Kit library slot's
 * current folder name. Normal Kit Save updates this field only after the save
 * succeeds; Morph operations leave it alone.
 *
 * Inputs: caller-owned Kit pointer and fixed-width eight-character display
 * field. Outputs: kit->display_name when kit is non-NULL.
 *
 * Affiliates/clients: filesystem normal Kit Load/Save completion, Menu Save
 * editor seeding, Scene embedded Kit naming.
 */
```

Header comment for `scene_setSceneDisplayName()`:

```c
/*
 * Retain one resident Scene display name.
 *
 * What: Copies exactly eight display cells into the selected resident Scene,
 * sanitizing non-printable bytes to spaces and appending NUL.
 *
 * Why: Scene Save needs an internal name independent of the root Scene slot
 * currently highlighted by the browser. This also gives future Bank work a
 * Scene-local name field instead of overloading preset_currentName.
 *
 * Inputs: resident Scene index and fixed-width display field. Outputs:
 * scenes[index].display_name when the index is valid.
 *
 * Affiliates/clients: filesystem Scene Load/Save, Menu Save editor seeding,
 * future Bank Scene lists.
 */
```

### `SceneData.c` Changes

Add a private display-name copy helper near `scene_copyInstrumentSourceName()`:

```c
static void scene_copyDisplayName(char dst[SCENE_OBJECT_DISPLAY_NAME_LEN + 1u],
                                  const char name[SCENE_OBJECT_DISPLAY_NAME_LEN])
{
    uint8_t i;

    /*
     * Normalize a retained object display name.
     *
     * Inputs are fixed-width Save/Load display bytes, not NUL-terminated C
     * strings. Output is the same eight-cell field plus NUL for resident
     * SceneData storage. Blank/all-space names are preserved because blank is
     * a valid user name; the UI sentinel `Empty` is handled by filesystem/menu
     * slot occupancy logic, not by this helper.
     */
    if (!dst)
        return;
    for (i = 0u; i < SCENE_OBJECT_DISPLAY_NAME_LEN; i++) {
        char c = name ? name[i] : ' ';
        dst[i] = (c >= 0x20 && c <= 0x7e) ? c : ' ';
    }
    dst[SCENE_OBJECT_DISPLAY_NAME_LEN] = '\0';
}
```

Implement the new setters/accessors:

```c
void scene_setKitDisplayName(kit_t *kit,
                             const char name[SCENE_OBJECT_DISPLAY_NAME_LEN])
{
    if (!kit)
        return;
    scene_copyDisplayName(kit->display_name, name);
}

void scene_setResidentKitDisplayName(
        uint8_t scene_index,
        const char name[SCENE_OBJECT_DISPLAY_NAME_LEN])
{
    scene_t *scene = scene_get(scene_index);
    if (!scene)
        return;
    scene_setKitDisplayName(&scene->kit, name);
}

void scene_setSceneDisplayName(uint8_t scene_index,
                               const char name[SCENE_OBJECT_DISPLAY_NAME_LEN])
{
    scene_t *scene = scene_get(scene_index);
    if (!scene)
        return;
    scene_copyDisplayName(scene->display_name, name);
}

const char *scene_kitDisplayName(uint8_t scene_index)
{
    const scene_t *scene = scene_getConst(scene_index);
    return scene ? scene->kit.display_name : "        ";
}

const char *scene_sceneDisplayName(uint8_t scene_index)
{
    const scene_t *scene = scene_getConst(scene_index);
    return scene ? scene->display_name : "        ";
}
```

Initialization update: in the Scene/Kit initialization path, set default Kit and
Scene names to blank spaces, not `"Empty   "`. `Empty` is an absent-slot
display sentinel owned by filesystem/menu.

Comment beside initialization:

```c
/*
 * Initialize retained object names to blank, not Empty.
 *
 * Blank is a valid resident name and the Save editor must preserve it. The
 * visible word `Empty` belongs only to missing library slots reported by the
 * scan cache.
 */
```

### `storageTypes.c/h` Changes

Change `storage_parseNumberedFolder()` so `NNN ` is valid. The current
function must stop rejecting an empty post-separator display string.

Required behavior:

- three leading decimal digits are still mandatory;
- separator remains space or underscore;
- slot number maps directly to 0..999;
- display bytes after the separator are copied with `storage_copyDisplayName()`;
- when the component ends immediately after the separator, return success and
  output eight spaces.

Implementation comment:

```c
/*
 * Accept a blank numbered-folder display name.
 *
 * What: `NNN ` is a valid numbered folder component. The parsed display field
 * becomes eight spaces, matching a retained internal blank name.
 *
 * Why: Blank is a real user-entered name, not an empty-slot sentinel. Slot
 * occupancy comes from the numeric prefix and validated folder contents; the
 * UI string `Empty` is only the display for an absent slot.
 *
 * Inputs: FAT display component beginning with three digits and a space or
 * underscore. Outputs: slot receives 000..999 directly; display receives the
 * post-separator text padded to the fixed LCD width, or all spaces when blank.
 *
 * Affiliates/clients: filesystem_recordKitDirectory(),
 * filesystem_recordSceneDirectory(), Kit/Scene Save UI, retained-name storage.
 */
```

No header signature change is needed.

### `filesystem.h` Changes

Add a public helper for persistent Instrument Save `OW`:

```c
uint8_t filesystem_instrumentTargetExists(instrument_type_t type,
                                          const char *display_stem);
```

Header comment:

```c
/*
 * Query whether a root Instrument save target already exists.
 *
 * What: Builds the same visible `stem.ext` component that root Instrument Save
 * will write, then checks the current Instrument/ scan cache for a
 * case-insensitive match of the same instrument type.
 *
 * Why: Menu must render persistent `OW` before the user confirms Save.
 * Numbered slots can answer from occupancy caches, but root Instrument Save is
 * filename-based and needs the extension/type rule owned by filesystem.
 *
 * Inputs: resident instrument type and the eight-character Save editor stem.
 * Outputs: nonzero when confirming would overwrite at least one on-card
 * same-casefold Instrument file.
 *
 * Affiliates/clients: menu_currentSaveWouldOverwrite(), root Instrument Save,
 * Instrument browser duplicate suppression.
 */
```

### `filesystem.c` Includes and Scratch

Add:

```c
#include "fat_standard.h"
```

This is needed for `fat_compareDisplayName()` and
`fat_compareDisplayNameCasefoldThenCase()` in product cache sorting and
duplicate checks.

Replace the old `op_save_opened_existing_dir` meaning with explicit
ensure/rename scratch:

```c
static uint8_t op_remove_done = 0u;
static uint8_t op_rename_done = 0u;
static uint8_t op_save_found_existing_dir = 0u;
static char op_save_existing_display_name[AFATFS_LONG_FILENAME_MAX + 1u];
static char op_save_existing_open_name[AFATFS_SHORT_FILENAME_MAX];
```

Callback helpers:

```c
static void on_remove_complete(void)
{
    op_remove_done = 1u;
}

static void on_rename_complete(void)
{
    op_rename_done = 1u;
}
```

Callback comment:

```c
/*
 * Mark completion of asyncfatfs overwrite preflight work.
 *
 * What: Latches that afatfs_removeObjects_lfn() or afatfs_renameObject_lfn()
 * has called back. The following state-machine phase decides success by
 * opening the expected object with the returned alias/display name.
 *
 * Why: File overwrite and occupied-slot save are now multi-step operations:
 * collapse same-casefold file variants before writing, and rename an existing
 * numbered directory before entering it. The filesystem pump needs a tiny
 * callback bridge between asyncfatfs completion and the next state-machine
 * phase.
 *
 * Affiliates/clients: filesystem_saveKitDirectory_tick(),
 * filesystem_saveSceneDirectory_tick(), filesystem_saveInstrument_tick(),
 * future KitMrp/InstrumentMrp Save.
 */
```

### `filesystem.c` Kit/Scene Scan Duplicate Policy

Update `filesystem_recordKitDirectory()` and `filesystem_recordSceneDirectory()`
so duplicate numbered directories do not replace an earlier representative
arbitrarily.

New helper:

```c
static uint8_t filesystem_displayPrecedesCached(const char *candidate,
                                                const char *cached)
{
    return (uint8_t)(fat_compareDisplayNameCasefoldThenCase(candidate,
                                                            cached) < 0);
}
```

Use it after parsing a numbered folder:

```c
if (kit_slot_present[slot] &&
    !filesystem_displayPrecedesCached(display, kit_slot_name[slot])) {
    return;
}
```

Then write the cache entry as today. Scene uses the same rule with
`scene_slot_present/name/open_name`.

Comment:

```c
/*
 * Keep the earliest display variant by product order.
 *
 * Slot number is the product identity, so externally-created duplicate
 * directories for the same `NNN` slot must not appear as separate products.
 * The casefold-first/raw-case tiebreak chooses a deterministic representative
 * with capital letters before lowercase, while later duplicate directories are
 * ignored until recursive delete exists.
 */
```

### `filesystem.c` Instrument Browser Duplicate Suppression

Replace `filesystem_compareInstrumentDisplayName()` body with the shared helper
or make it a wrapper:

```c
static int8_t filesystem_compareInstrumentDisplayName(const char *a,
                                                       const char *b)
{
    return fat_compareDisplayNameCasefoldThenCase(a, b);
}
```

Add a same-product helper for type+stem:

```c
static uint8_t filesystem_instrumentCacheEntryMatches(
        instrument_type_t type,
        uint8_t index,
        const char *display_name)
{
    char display[STORAGE_KIT_DISPLAY_NAME_LEN + 1u];

    filesystem_copyInstrumentStemDisplay(display, display_name);
    return (uint8_t)(
        type < INSTRUMENT_TYPE_UNKNOWN &&
        index < instrument_file_count[type] &&
        fat_compareDisplayName(instrument_file_name[type][index],
                               display,
                               false) == 0);
}
```

Modify `filesystem_recordInstrumentFile()`:

1. classify type as today;
2. build `display` and `stem16`;
3. scan existing cache rows for same type and case-insensitive display stem;
4. if found, compare full visible filename by
   `fat_compareDisplayNameCasefoldThenCase(display_name, existing_display_name)`;
5. keep the earlier representative and discard the later one;
6. if the new one wins, replace that cache row's name/open/stem without
   incrementing `instrument_file_count[type]`;
7. otherwise insert sorted as today.

Add a temporary full-display cache only if needed. If not adding another cache
array, compare the eight-character stem because Instrument browser identity is
type + displayed stem; the overwrite path still removes full same-casefold
physical filenames before writing.

Comment:

```c
/*
 * Suppress same-casefold Instrument browser duplicates.
 *
 * What: Before inserting a scanned Instrument/ object, compare it against
 * existing cached rows for the same instrument type. If the display stem
 * matches case-insensitively, keep only the filename that sorts first by folded
 * text and raw ASCII case.
 *
 * Why: Externally edited FAT cards may contain names that differ only by case.
 * Product policy treats those as one object; later variants must not appear in
 * the Load UI even though asyncfatfs reports every physical FAT object.
 *
 * Inputs: display_name and open_name come from afatfs_findNextObject().
 * Outputs: the per-type cache either keeps its existing representative,
 * replaces it with an earlier-sorting variant, or inserts a new product object.
 *
 * Affiliates/clients: filesystem_requestScanInstruments(), nested Instrument
 * Load, root Instrument Save cache update,
 * fat_compareDisplayNameCasefoldThenCase().
 */
```

Implement `filesystem_instrumentTargetExists()` near the other Instrument cache
accessors:

```c
uint8_t filesystem_instrumentTargetExists(instrument_type_t type,
                                          const char *display_stem)
{
    char display_file[AFATFS_LONG_FILENAME_MAX + 1u];

    if (type >= INSTRUMENT_TYPE_UNKNOWN || !display_stem)
        return 0u;
    storage_makeSavedInstrumentDisplayFilename(display_file,
                                               sizeof(display_file),
                                               display_stem,
                                               type,
                                               0u,
                                               0u);
    for (uint8_t i = 0u; i < instrument_file_count[type]; i++) {
        if (filesystem_instrumentCacheEntryMatches(type, i, display_file))
            return 1u;
    }
    return 0u;
}
```

### `filesystem.c` Instrument Cache Update After Save

Update `filesystem_updateInstrumentCacheAfterSave()` so it removes every
same-casefold cached row for the saved target type/stem before inserting the
new one.

Replace the exact checks:

```c
strcmp(instrument_file_open_name[type][i], open_name) == 0 ||
strncmp(instrument_file_name[type][i], display, STORAGE_KIT_DISPLAY_NAME_LEN) == 0
```

with:

```c
fat_compareDisplayName(instrument_file_open_name[type][i],
                       open_name,
                       false) == 0 ||
fat_compareDisplayName(instrument_file_name[type][i],
                       display,
                       false) == 0
```

Comment:

```c
/*
 * Refresh browser cache after case-insensitive overwrite.
 *
 * What: Removes every cached row whose filename alias or display stem matches
 * the saved target under case-insensitive comparison, then inserts the one
 * returned by the completed save.
 *
 * Why: The SD card has already collapsed same-casefold physical files into one
 * visible object. The in-RAM browser cache must mirror that immediately so the
 * next nested load cannot select a stale duplicate alias.
 *
 * Inputs: display_name is the target case just written; open_name is the short
 * alias returned by asyncfatfs. Outputs: per-type Instrument cache contains one
 * row for the saved object.
 *
 * Affiliates/clients: filesystem_saveInstrument_tick(),
 * filesystem_recordInstrumentFile(), nested Instrument Load.
 */
```

### `filesystem.c` Kit Display Name Construction

Change `filesystem_makeKitDirectoryDisplayName()` so it never substitutes
`Kit` for a blank save name.

Remove:

```c
if (pos == 4u) {
    dst[pos++] = 'K';
    dst[pos++] = 'i';
    dst[pos++] = 't';
}
```

Keep the existing trim behavior by setting `pos = last_meaningful`. For an
all-space name, `pos` remains `4`, so the output is `NNN ` plus NUL.

Comment replacement:

```c
/*
 * Preserve blank Kit save names.
 *
 * What: Builds a root numbered Kit folder component: `NNN ` plus exactly the
 * sanitized edited Kit name. If every name character is blank, the component
 * ends after the separator space.
 *
 * Why: The internal retained Kit name can be blank. Falling back to `Kit`
 * would silently rename a real blank root Kit save and break the Save UI
 * contract that `Empty` means absent slot, not blank-named slot. This does not
 * apply to Scene embedded Kits, whose directory name is always
 * `Kit <kit-name>`.
 *
 * Inputs: op_slot is the direct 000..999 library slot; preset_currentName is
 * the fixed-width editable retained Kit name. Output: LFN display component
 * passed to asyncfatfs rename/mkdir.
 *
 * Affiliates/clients: filesystem_saveKitDirectory_tick(), KitMrp Save, Kit
 * scan cache update, storage_parseNumberedFolder().
 */
```

### `filesystem.c` Kit Member Filename Generation

Change `filesystem_prepareSavedInstrumentFilenames()` so every Kit member file
gets the voice number forced into character 8, not only duplicates.

Replace both-pass duplicate logic with a single pass:

```c
memset(op_save_instrument_file, 0, sizeof(op_save_instrument_file));
for (slot = 0u; slot < STORAGE_KIT_SLOT_COUNT; slot++) {
    storage_makeSavedInstrumentDisplayFilename(
        op_save_instrument_display_file[slot],
        sizeof(op_save_instrument_display_file[slot]),
        kit->instrument_stem[slot],
        kit->instruments[slot].type,
        (uint8_t)(slot + 1u),
        1u);
}
```

Comment:

```c
/*
 * Generate voice-numbered member Instrument filenames.
 *
 * What: Builds one visible filename for each Kit member using the retained
 * per-voice Instrument stem and always forcing the one-based voice number into
 * character 8 of the stem.
 *
 * Why: Kit Save owns six member files in one directory. Two voices may retain
 * the same Instrument name, and the save must still produce six distinct
 * authoritative member files without depending on asyncfatfs alias suffixes.
 *
 * Inputs: resident Kit source stems and slot types. Outputs: visible LFN file
 * components in op_save_instrument_display_file[] and cleared returned-alias
 * buffers in op_save_instrument_file[].
 *
 * Affiliates/clients: storage_makeSavedInstrumentDisplayFilename(),
 * filesystem_saveKitDirectory_tick(), kitset.kcg file references.
 */
```

### `filesystem.c` Ensure Numbered Directory Helper

Factor root Kit/Scene directory ensure behavior into small helpers before
rewriting save phases. If keeping separate Kit and Scene helpers is simpler,
share only the inner scan/compare logic.

Recommended helper shape for Kit:

```c
static uint8_t filesystem_findKitSlotDirectoryForSave(void);
static uint8_t filesystem_startEnsureKitSlotDirectory(void);
```

`filesystem_findKitSlotDirectoryForSave()` assumes asyncfatfs current directory
is `Kit/` and scans with `afatfs_findNextObject()`.

Outputs:

- `op_save_found_existing_dir`;
- `op_save_existing_display_name`;
- `op_save_existing_open_name`;
- `op_save_kit_dir_name` when an alias is already known.

Scan rule:

```c
if (object.kind == AFATFS_OBJECT_DIRECTORY &&
    storage_parseNumberedFolder(object.displayName, &slot, display) &&
    slot == op_slot) {
    choose by fat_compareDisplayNameCasefoldThenCase(object.displayName,
                                                     best_display) < 0;
}
```

Comment:

```c
/*
 * Find the canonical directory object for one numbered Kit slot.
 *
 * What: Scans the current Kit/ directory for directory objects whose visible
 * name or compatibility short alias parses as the requested `NNN` slot.
 *
 * Why: Slot number is product identity. An occupied save must rename that
 * directory to the edited display component before rewriting child files,
 * instead of creating a duplicate or preserving the old visible name.
 *
 * Inputs: op_slot is the requested root Kit slot; afatfs current directory is
 * already Kit/. Outputs: op_save_existing_display_name receives the visible
 * source component, and op_save_kit_dir_name receives its open alias.
 *
 * Duplicate policy: if external editing created several same-slot variants,
 * choose the one that sorts first by case-folded text with raw ASCII as a
 * tiebreaker. Later duplicate directories are hidden from product behavior
 * until recursive directory delete is implemented.
 *
 * Affiliates/clients: filesystem_saveKitDirectory_tick(), KitMrp save, Scene
 * embedded Kit save, asyncfatfs rename, storage_parseNumberedFolder().
 */
```

Helper flow:

1. if no existing directory: call
   `afatfs_mkdir_lfn(op_save_kit_display_name,
                     AFATFS_MATCH_CASE_INSENSITIVE,
                     op_save_kit_dir_name,
                     on_file_opened)`;
2. if existing display byte-equals target: open by `op_save_kit_dir_name`;
3. if existing display differs: call
   `afatfs_renameObject_lfn(op_save_existing_display_name,
                            op_save_kit_display_name,
                            AFATFS_MATCH_CASE_INSENSITIVE,
                            op_save_kit_dir_name,
                            on_rename_complete)`, wait for callback, then open
   by returned alias.

Byte equality check:

```c
fat_compareDisplayName(op_save_existing_display_name,
                       op_save_kit_display_name,
                       true) == 0
```

Use case-insensitive matching only for finding the source; use byte equality to
decide whether a case-only rename is needed.

### `filesystem_saveKitDirectory_tick()` Phase Rewrite

Replace phases 6-10 with ensure/rename phases. Suggested phase map:

```text
6  BEGIN slot directory scan
7  SCAN slot directory
8  START mkdir/rename/open selected directory
9  WAIT mkdir/open or rename
10 OPEN renamed directory by returned alias if needed
11 WAIT target kit folder
12 CHDIR target kit folder
13 CLOSE target kit handle
14 WAIT CLOSE target kit
15 START first member file remove preflight
```

Then renumber the old member/kitset phases or insert the new phases into the
existing gaps. The important sequence is:

1. after `Kit/` is current directory, find slot by `NNN`;
2. create or rename to `op_save_kit_display_name`;
3. open the resulting directory by alias;
4. chdir into it;
5. for each member file, remove same-casefold variants;
6. write member file with LFN `"w"` and case-insensitive matching;
7. write `kitset.kcg`;
8. return root;
9. update cache and retained name.

Member remove phase before existing member open:

```c
op_remove_done = 0u;
if (!afatfs_removeObjects_lfn(
        op_save_instrument_display_file[op_instrument_slot],
        AFATFS_MATCH_CASE_INSENSITIVE,
        AFATFS_REMOVE_FILES_ONLY,
        on_remove_complete)) {
    return;
}
```

Then:

```c
if (!op_remove_done)
    return;
...
afatfs_fopen_lfn(op_save_instrument_display_file[op_instrument_slot],
                 "w",
                 AFATFS_MATCH_CASE_INSENSITIVE,
                 op_save_instrument_file[op_instrument_slot],
                 on_file_opened)
```

Comment beside member remove:

```c
/*
 * Collapse same-casefold member-file variants before writing.
 *
 * What: Deletes every physical file in the Kit directory whose display name
 * matches the target member filename under case-insensitive comparison.
 *
 * Why: `Kick.drm` and `kick.drm` can exist only after external filesystem
 * edits, but product save must treat them as one object. Removing all variants
 * before fopen_lfn("w") guarantees the saved Kit contains exactly one member
 * file with the case generated from retained Scene metadata.
 *
 * Inputs: op_save_instrument_display_file[op_instrument_slot] is the visible
 * target name; op_instrument_slot identifies which Kit member is being written.
 *
 * Outputs/effects: duplicate files are removed from FAT and the following
 * fopen_lfn() creates the single authoritative file. Directories with the same
 * folded component are ignored by AFATFS_REMOVE_FILES_ONLY.
 *
 * Affiliates/clients: afatfs_removeObjects_lfn(), afatfs_fopen_lfn(),
 * storage_formatInstrumentLine(), kitset.kcg alias collection.
 */
```

Cache update in final phase:

- set `kit_slot_present[op_slot] = 1u`;
- parse `op_save_kit_display_name` with `storage_parseNumberedFolder()`;
- copy parsed display to `kit_slot_name[op_slot]` even for occupied saves;
- copy returned alias `op_save_kit_dir_name` to `kit_slot_open_name[op_slot]`;
- add `op_slot` to `kb_map` if absent;
- call `scene_setResidentKitDisplayName(scene_getActiveIndex(),
  preset_currentName)` only after every file closes and root chdir succeeds.

Comment:

```c
/*
 * Commit Kit save identity after all owned files have been rewritten.
 *
 * What: Updates the Kit scan cache and resident Kit display name from the
 * target directory actually written by this save.
 *
 * Why: An occupied slot may have been renamed, and same-casefold duplicates may
 * have been collapsed. The in-RAM browser and retained SceneData identity must
 * mirror the durable on-card result only after the save is complete.
 *
 * Inputs: op_save_kit_display_name and op_save_kit_dir_name from the
 * ensure/rename path. Outputs: kit_slot_* cache, kb_map, and resident Kit name.
 *
 * Affiliates/clients: menu_currentSaveWouldOverwrite(), future KitMrp Save,
 * boot/rescan behavior.
 */
```

### `filesystem_saveInstrument_tick()` Phase Rewrite

Switch root `Instrument/` creation/open to case-insensitive matching:

```c
afatfs_mkdir_lfn(STORAGE_ROOT_INSTRUMENT,
                 AFATFS_MATCH_CASE_INSENSITIVE,
                 op_root_open_name,
                 on_file_opened)
```

Insert remove-before-open phases after entering `Instrument/` and before the
current target file open.

Suggested phase map:

```text
6  REMOVE target casefold variants
7  WAIT REMOVE
8  OPEN target instrument file
9  WAIT target instrument file
10 WRITE complete instrument text
11 CLOSE target instrument file
12 WAIT CLOSE target instrument file
13 RETURN ROOT + UPDATE CACHE + RETAIN NAME
```

Remove call:

```c
op_remove_done = 0u;
if (!afatfs_removeObjects_lfn(op_instrument_save_display_name,
                              AFATFS_MATCH_CASE_INSENSITIVE,
                              AFATFS_REMOVE_FILES_ONLY,
                              on_remove_complete)) {
    return;
}
```

Open call:

```c
afatfs_fopen_lfn(op_instrument_save_display_name,
                 "w",
                 AFATFS_MATCH_CASE_INSENSITIVE,
                 op_instrument_save_open_name,
                 on_file_opened)
```

Comment:

```c
/*
 * Remove case-variant Instrument files before saving one root Instrument.
 *
 * What: In Instrument/, removes every file whose display component matches the
 * requested target under case-insensitive comparison.
 *
 * Why: The root Instrument pool is user-copyable from desktop filesystems. If
 * a card contains both `fiRstfile.snr` and `firStfile.snr`, the browser exposes
 * only the capital-first winner, and overwrite must collapse all physical
 * variants into the newly entered case.
 *
 * Inputs: op_instrument_save_display_name is the captured target filename from
 * Menu after extension/type construction. Output: the following fopen_lfn()
 * writes one replacement file and returns its short alias for cache update.
 *
 * Affiliates/clients: filesystem_updateInstrumentCacheAfterSave(),
 * afatfs_removeObjects_lfn(), afatfs_fopen_lfn(), nested Instrument Save UI.
 */
```

Final retained-name update:

```c
scene_setInstrumentSourceName(op_instrument_save_source_scene,
                              op_instrument_save_source_slot,
                              op_instrument_save_display_name);
```

Do this only after `filesystem_updateInstrumentCacheAfterSave()` and only for
normal Instrument Save. Morph Save will later skip it.

### `filesystem_requestSaveInstrument()` Capture

Keep request-time capture of source Scene/slot/type. Build
`op_instrument_save_display_name` exactly once from the Save editor stem and
the captured resident type using
`storage_makeSavedInstrumentDisplayFilename(... force_voice_suffix = 0u)`.

Comment:

```c
/*
 * Capture the root Instrument save target before asynchronous work starts.
 *
 * What: Converts the eight-character Save editor stem plus the resident slot's
 * current instrument type into the exact visible `stem.ext` component that
 * will be written in Instrument/.
 *
 * Why: Menu state can move while the filesystem operation is pending. The save
 * must serialize the source slot and filename accepted at OK click time, not a
 * later UI selection.
 *
 * Inputs: source Scene/slot and display stem. Outputs:
 * op_instrument_save_* scratch for filesystem_saveInstrument_tick().
 *
 * Affiliates/clients: menu nested Instrument Save, persistent OW helper,
 * storage_makeSavedInstrumentDisplayFilename().
 */
```

### Normal Load Retained-Name Updates

Normal Kit Load already copies a complete staged Kit into selected Scenes. Add
retained Kit display update in the normal-load success branch, not in KitMrp:

```c
scene_setResidentKitDisplayName(scene_index, kit_slot_name[op_slot]);
```

Do this for every selected destination Scene after validation succeeds.

Normal Scene Load should call:

```c
scene_setSceneDisplayName(scene_index, scene_slot_name[op_slot]);
```

when Scene Load is promoted. The storage field can land now even if Scene UI
stays gated.

Normal Instrument Load already updates retained Instrument name through Preset
using `filesystem_loadedInstrumentDisplayName()` and
`filesystem_loadedInstrumentStem()`. Leave Morph Instrument Load unchanged so it
does not call `scene_setInstrumentSourceName()`.

Comment:

```c
/*
 * Update retained Kit identity only for normal Kit Load.
 *
 * What: Copies the selected Kit folder display name into each destination
 * resident Kit after the entire directory validates and commits.
 *
 * Why: KitMrp uses the same file parser but copies endpoint values into morph
 * images only. Morph operations must not rename the resident Kit or its member
 * Instruments.
 *
 * Affiliates/clients: filesystem_loadKitDirectory_tick(), Preset KitMrp
 * completion, Save editor seeding.
 */
```

### `presetManager.c/h` Changes

Normal Kit Save completion should not rely on `preset_currentName` as the only
retained name. After filesystem success, Preset/Menu completion can continue to
clear status as today, but the actual retained Kit update should live in
`filesystem_saveKitDirectory_tick()` final phase because filesystem knows the
save completed and which Scene/slot was saved.

For Save editor seeding, add or use small Preset helpers that copy resident
names into `preset_currentName`:

```c
char *preset_prepareKitSaveName(uint8_t scene_index);
char *preset_prepareSceneSaveName(uint8_t scene_index);
char *preset_prepareInstrumentSaveName(uint8_t scene_index, uint8_t slot);
```

Implementation:

```c
memcpy(preset_currentName, scene_kitDisplayName(scene_index), 8u);
...
memcpy(preset_currentName, scene_sceneDisplayName(scene_index), 8u);
...
memcpy(preset_currentName,
       scene_getConst(scene_index)->kit.instrument_display_name[slot],
       8u);
```

If adding Preset wrappers feels unnecessary, Menu may call SceneData accessors
directly. Prefer Preset wrappers only if they keep current Save-page ownership
cleaner.

Comment:

```c
/*
 * Seed the Save editor from resident identity, not slot display.
 *
 * What: Copies the currently loaded object's retained name into
 * preset_currentName before character editing begins.
 *
 * Why: Slot scrolling is library browsing. The editable save name belongs to
 * the resident Kit, Scene, or Instrument and must not become `Empty` simply
 * because the highlighted target slot is absent.
 *
 * Affiliates/clients: menu_handleLoadSaveMenu(), SceneData retained-name
 * fields, filesystem save request capture.
 */
```

### `menu.c` Save UI Changes

Change `menu_currentSaveWouldOverwrite()`:

```c
if (menu_saveOptions.what == SAVE_TYPE_KIT)
    return filesystem_kitSlotExists(menu_currentPresetNr[SAVE_TYPE_KIT]);
if (menu_saveOptions.what == SAVE_TYPE_SCENE)
    return filesystem_sceneSlotExists(menu_currentPresetNr[SAVE_TYPE_SCENE]);
if (menu_instrumentLoadActive && menu_activePage == SAVE_PAGE)
    return filesystem_instrumentTargetExists(menu_instrumentLoadType,
                                             menu_instrumentSaveName);
return 0u;
```

If Instrument Save still routes through `preset_currentName` rather than
`menu_instrumentSaveName` at the exact call site, use that same source. The key
rule is: query the stem that will be captured by
`filesystem_requestSaveInstrument()`.

Comment:

```c
/*
 * Compute persistent overwrite display from product identity.
 *
 * What: Returns nonzero whenever the pending Save action targets an existing
 * product object. For numbered saves, the slot number is the identity. For
 * root Instrument Save, the target filename plus type extension is matched
 * case-insensitively inside Instrument/.
 *
 * Why: `OW` warns about replacement, not about whether the edited text differs
 * from the old display name. A save to an occupied slot overwrites even when
 * the name is unchanged, and a case-only Instrument filename match overwrites
 * on FAT.
 *
 * Affiliates/clients: menu_repaintLoadSavePage(), filesystem slot caches,
 * filesystem_instrumentTargetExists(), Save OK click handlers.
 */
```

Change slot-to-name transition in `menu_handleLoadSaveMenu()`:

- when entering Kit Save character edit, seed `preset_currentName` from
  retained Kit name, not `filesystem_kitSlotName(slot)`;
- when entering Scene Save character edit, seed from retained Scene name;
- when entering nested Instrument Save name edit, keep current behavior that
  seeds from the selected slot's retained Instrument display name.

The existing `menu_prepareInstrumentSaveName()` already reads
`kit->instrument_display_name[menu_instrumentLoadSlot]`; keep that behavior.

Comment:

```c
/*
 * Enter Save name editing with resident identity.
 *
 * What: Copies the saved object's retained name into the editable field when
 * the cursor moves from slot/type selection into character editing.
 *
 * Why: The slot row displays target occupancy, while the character row edits
 * the name that will be written. Empty target slots must not erase or replace
 * the resident Kit/Scene/Instrument name.
 *
 * Affiliates/clients: preset_currentName, menu_instrumentSaveName,
 * SceneData retained-name accessors, persistent OK/OW display.
 */
```

### `menu_loadSaveTypeIsRestored()`

Do not promote Morph in this pass. Keep current restored types as File, Dir,
and Kit unless normal Instrument Save is already reachable through nested
Instrument mode. If the normal-save pass exposes root Instrument Save from the
nested Instrument Save UI, that path is not a `SAVE_TYPE_*` promotion; it is a
destination-slot submode.

Morph type promotion remains Phase 3.

### Build/Test Checklist For Phase 2

Code-level checks:

```text
git diff --check
make
make img
```

Functional tests:

- Boot, scan Kit, and confirm empty slots display `Empty`.
- Save Kit to an empty slot with blank name; confirm folder is `NNN `.
- Save Kit to an occupied slot with a changed name; confirm folder is renamed
  and unrelated extra files inside remain.
- Save Kit with duplicate retained Instrument names; confirm all six member
  files are distinct by voice-numbered character 8.
- Put `Kick.drm` and `kick.drm` in a Kit directory externally; save the Kit and
  confirm only the generated member case remains.
- Save root Instrument to a new filename; `ok` is shown and retained
  Instrument name updates on success.
- Save root Instrument over a case-only existing filename; `OW` is shown and
  only the newly entered case remains on card.
- Load the saved root Instrument normally; retained Instrument name updates
  from the file stem.
- Run KitMrp/InstrumentMrp load smoke tests if still reachable through direct
  calls, and confirm retained names do not change.

### Phase 2 Implementation Pass Notes

Current pass status:

- Added resident Kit and Scene display-name storage in `SceneData`.
  - `SCENE_OBJECT_DISPLAY_NAME_LEN` defines the shared eight-cell object name.
  - `kit_t.display_name` stores the currently resident Kit name, independent of
    the highlighted root Kit slot.
  - `scene_t.display_name` stores the currently resident Scene name for future
    Scene Save/Load work.
  - `scene_setKitDisplayName()`, `scene_setResidentKitDisplayName()`,
    `scene_setSceneDisplayName()`, `scene_kitDisplayName()`, and
    `scene_sceneDisplayName()` centralize sanitizing/copying.
  - `scene_initAll()` initializes both retained object names to all blanks, not
    `Empty`.

- Updated storage/display helpers.
  - `storage_parseNumberedFolder()` now accepts `NNN ` with an all-blank display
    component so blank-named Kit/Scene slots are valid.
  - `storage_makeSavedInstrumentDisplayFilename()` now uses character 8 of the
    stem for the one-based Kit voice marker when `force_voice_suffix` is set,
    producing six deterministic member filenames without `_vN`.

- Updated filesystem cache identity and duplicate policy.
  - Kit and Scene scan caches now keep only the first same-slot duplicate by
    `fat_compareDisplayNameCasefoldThenCase()`, matching the capital-first
    product rule.
  - Instrument scan/cache duplicate suppression now uses the same casefold then
    raw-case sort.
  - Parsed fixed-width display buffers are explicitly NUL-terminated before
    duplicate sorting so the comparator never reads beyond the eight display
    cells.
  - `filesystem_instrumentTargetExists()` exposes the root Instrument overwrite
    query used by Menu for persistent `OW`.

- Reworked normal Kit Save.
  - Root `Kit` creation/open uses case-insensitive LFN matching.
  - The save scans `Kit/` for the target slot, chooses the canonical existing
    same-slot directory, renames it to the edited visible component when needed,
    or creates it when absent.
  - Each owned member Instrument file is preceded by
    `afatfs_removeObjects_lfn(..., AFATFS_MATCH_CASE_INSENSITIVE,
    AFATFS_REMOVE_FILES_ONLY, ...)` so external case variants collapse to one
    newly written file.
  - Final cache update writes the target folder display name and returned open
    alias, updates `kb_map`, and retains the resident Kit display name only
    after all file writes and the root return succeed.

- Reworked normal root Instrument Save.
  - Root `Instrument` creation/open uses case-insensitive LFN matching.
  - Save removes all casefold-equivalent file variants before opening the target
    with `afatfs_fopen_lfn(..., "w", AFATFS_MATCH_CASE_INSENSITIVE, ...)`.
  - The Instrument browser cache is updated after close/root return, then the
    resident Instrument source name is retained for future Save editor seeding.

- Updated normal Kit Load retained identity.
  - Normal Kit Load commits `kit_slot_name[op_slot]` into each destination
    resident Kit after the staged directory validates.
  - KitMrp Load remains excluded from resident-name updates.

- Updated Save UI behavior.
  - `menu_currentSaveWouldOverwrite()` now reports numbered Kit/Scene overwrite
    from slot occupancy, not display-name difference.
  - Nested root Instrument Save reports overwrite through
    `filesystem_instrumentTargetExists()` and renders `OW` in its early-return
    repaint path.
  - Entering Kit/Scene Save name editing reseeds `preset_currentName` from
    resident SceneData identity so empty target slots do not overwrite the
    editable name with `Empty` or a previous slot occupant.

Verification notes for this pass:

- `git diff --check -- <Phase 2 touched files>` passed after line-ending
  normalization.
- `git ls-files --eol` reports LF working-tree endings for every touched file,
  avoiding the earlier fake whole-file diff presentation.
- Local compile was not run because `make`, `gcc`, `clang`, and
  `arm-none-eabi-gcc` are not available in this environment.

## Remaining Decisions

Product semantics are settled for files, numbered blank folder names, retained
Kit/Scene name placement, and the asyncfatfs public rename/remove APIs.

Still open:

- **Duplicate non-empty directories:** decide whether ignored later duplicate
  numbered directories remain acceptable until recursive delete lands, or
  whether a duplicate same-slot directory should make normal save fail.
- **Diagnostics policy:** decide whether File/Dir diagnostic menus should remain
  exact-case tests or switch to production case-insensitive/case-preserving
  behavior.
- **Blank root Instrument names:** decide whether standalone Instrument blank
  names should remain `inst.<ext>`, become blank-stem extension files, or be
  disallowed in UI.
- **Morph Save implementation shape:** defer until Phase 3. Choose between a
  temporary save-view copy and a storageTypes descriptor accessor/context flag
  after normal save rename/replace helpers are proven.
## Build and Test Checklist

For each implementation phase:

```text
git diff --check
make
make img
```

Hardware tests after rename/replace foundation:

- File/Dir diagnostics still scan, load, save, and overwrite.
- Directory rename preserves child contents.
- File replacement through the LFN-aware write path matches existing objects
  case-insensitively and preserves the newly entered visible target casing.
- Case-only overwrite tests, such as saving `kick.drm` over `Kick.drm`, show
  `OW` and leave one visible object using the newly entered case.

Hardware tests after normal save repair:

- Save Kit to empty slot.
- Save Kit to occupied slot with a different edited name.
- Confirm the Kit folder is renamed to the edited name.
- Confirm current member files and `kitset.kcg` are rewritten.
- Confirm unrelated extra files in the Kit folder are tolerated by load/save.
- Confirm duplicate retained Instrument names become distinct member files via
  character-8 voice number.
- Save Instrument to new file.
- Save Instrument over existing file and confirm persistent `OW`.

Hardware tests after Morph load/save:

- KitMrp Load copies normal file endpoints into resident morph endpoints.
- InstrumentMrp Load copies normal file endpoints into the selected voice's morph
  endpoints.
- KitMrp Save writes interpolated values as normal endpoints and current normal
  values as morph endpoints.
- InstrumentMrp Save does the same for one selected voice.
- Normal saves update retained names; Morph saves do not.
