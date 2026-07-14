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

### Phase 2: Retained Names and Save UI Split

- Add retained Kit name storage.
- Add retained Scene name storage now, while leaving Scene Load/Save UI gated
  until the Scene-specific pass.
- Split save slot display from save edit-name state in Menu.
- Seed editor from retained object name when entering character entry.
- Leave blank names untouched; for numbered slots, all-blank user text produces
  the visible component `NNN `.
- Allow real `Empty` names.

Acceptance:

- Save slot scroll shows `Empty` for absent slots.
- Clicking into the name field shows retained Kit/Instrument name, not `Empty`,
  unless that retained name is literally `Empty`.
- Saving updates only the saved object's retained name.
- Morph load/save does not update retained names.

### Phase 3: Persistent OK/OW

- Change slot overwrite predicate to occupied-slot based.
- Add Instrument library filename existence helper with case-insensitive
  matching and case-preserving returned display data.
- Use filename plus extension for Instrument `OW`.
- Keep `OW` rendered continuously while the current request would overwrite.

Acceptance:

- occupied Kit slot always shows `OW`;
- empty Kit slot shows `ok`;
- standalone Instrument Save defaults to current retained name and shows `OW`
  when the matching file exists under case-insensitive comparison;
- editing the Instrument name to a non-existing target changes `OW` to `ok`.

### Phase 4: Normal Kit Save Rename/Replace

- Replace the current occupied-folder preserve-name path with rename-if-needed
  before entering the slot directory.
- Generate member Instrument filenames using retained names and voice number at
  character 8.
- Write or replace all member files.
- Write or replace `kitset.kcg`.
- Update Kit scan cache from the actual current display/alias pair.
- Update retained Kit name only after successful normal save.

Acceptance:

- occupied Kit save changes the folder name to the edited name;
- expected member files and `kitset.kcg` reflect the current Kit;
- duplicate Instrument names inside a Kit produce unique voice-numbered member
  files;
- unrelated extra files in the Kit folder do not break load or save;
- power-cycle/reboot scan sees the saved Kit under the new name.

### Phase 5: Normal Instrument Save Replace

- Detect case-insensitive target filename existence for `OW`.
- Use the LFN-aware write path to replace an existing file or create a missing
  file.
- Preserve standalone Instrument filename exactly after sanitization and type
  extension.
- Update retained Instrument name only after successful normal save.

Acceptance:

- saving a new Instrument file shows `ok`;
- saving over an existing target, including a case-only match, shows `OW`;
- overwrite leaves one visible target file whose case matches the newly entered
  display component;
- reloading the saved file updates that slot's retained name from the filename.

### Phase 6: Morph Load Promotion

- Add KitMrp to Load type whitelist.
- Add InstrumentMrp nested Load selection.
- Preserve existing Preset same-type endpoint-copy semantics.
- Verify no retained names change.

Acceptance:

- KitMrp copies matching source normal endpoints into resident morph endpoints;
- InstrumentMrp copies matching source normal endpoints into the selected
  resident slot's morph endpoints;
- mismatched types are no-change;
- current Morph amounts are reapplied through the bounded Morph worker.

### Phase 7: Morph Save

- Add filesystem/Preset/Menu request paths for Kit Morph Save.
- Add filesystem/Preset/Menu request paths for Instrument Morph Save.
- Add storageTypes save-view support for Morph Save endpoint mapping.
- Use the same directory rename and file replace behavior as normal save.
- Do not update retained names on Morph Save.

Acceptance:

- KitMrp Save writes file `[params]` as current interpolated values and
  `[morph]` as prior normal endpoints;
- InstrumentMrp Save does the same for one selected voice;
- normal save behavior remains unchanged;
- Morph Save does not rename Kit, Scene, or Instrument retained names;
- loading the saved file normally produces the interpolated values as normal
  endpoints;
- loading the saved file as Morph produces the interpolated values into morph
  endpoints, consistent with Morph Load rules.

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

### Scope Decisions Still Needed

1. **Duplicate non-empty directories:** file overwrite can remove all
   same-casefold variants now. Removing duplicate non-empty directories requires
   recursive delete, which is explicitly not part of the current asyncfatfs
   primitive. For Kit/Scene slot directories, the proposed behavior is: choose
   the capital-first product representative, rename that directory, rewrite its
   expected children, and hide later duplicate directories from browsers. Decide
   whether this is acceptable until recursive directory delete lands, or whether
   duplicate non-empty directories should make save fail.
2. **Diagnostics policy:** File/Dir diagnostic menus currently use
   case-sensitive LFN calls to probe raw behavior. Decide whether diagnostics
   should remain exact-case tests or switch to production
   case-insensitive/case-preserving behavior.
3. **Blank root Instrument names:** Kit/Scene blank names are now decided. Root
   Instrument Save currently funnels blank stems through
   `storage_makeSavedInstrumentDisplayFilename()`, which falls back to `inst`.
   Decide whether standalone Instrument blank names should become `.drm/.snr`
   style filenames, remain `inst.<ext>`, or be disallowed in the UI.
## Open Implementation Questions

Product semantics are settled for files and numbered blank folder names. The
remaining product-scope decisions are listed in the real-code dive above.

The remaining questions are implementation choices to answer while coding:

- Should the asyncfatfs rename API be public display-name based only, or should
  filesystem.c use a private object-info helper after scanning a numbered slot?
- For longer target names that need a larger LFN entry run, should rename always
  move to a new entry run, or use in-place rewrite when the old run is large
  enough and move only when necessary?
- Is the cleaner Morph Save implementation a temporary `kit_t`/slot copy or a
  storageTypes save-view accessor?
- Where exactly should retained Kit and Scene names live in `SceneData.h` so
  future Bank work can multiply them cleanly?
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
