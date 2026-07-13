# Kit Save Filesystem Names Audit

Date: 2026-07-13

Scope: Phase 3.5-3.6 Kit Save filesystem naming, with emphasis on whether
saved Kit folders and saved instrument member files truthfully preserve and
display spaces and upper/lowercase distinctions.

Note: the Summary through Acceptance Matrix sections describe the pre-fix audit
findings and target plan. The Implementation Notes section records the Session
036 code changes made from this plan.

## Summary

The current implementation does not conform to the intended user-facing naming
contract for saved Kits.

The core problem is not only that asyncfatfs lacks long filename creation. The
more serious bug is that Kit Save updates firmware caches with names that are
not the names physically created on the card. This makes Kit Load show a
logical/display name that can differ from the actual FAT directory entry. That
is the exact "register lies to the user" failure and should be treated as a
blocking bug before further Scene/Bank save work builds on this path.

Current asyncfatfs creation path creates only one short 8.3 directory entry:

- `afatfs_createFile()` converts every create/open name through
  `fat_convertFilenameToFATStyle()` in
  `Core/Hardware/SD/asyncfatfs/asyncfatfs.c:2757`.
- `fat_convertFilenameToFATStyle()` uppercases every basename/extension
  character and emits only the 11-byte FAT short-name field in
  `Core/Hardware/SD/asyncfatfs/fat_standard.c:47`.
- New entries are written with `entry->filename = opState->filename` only, with
  no LFN entries and no NT lowercase flags, in
  `Core/Hardware/SD/asyncfatfs/asyncfatfs.c:2598`.

That means firmware-created names cannot preserve spaces, mixed case, or more
than 8.3 characters today. Any UI/cache that presents them as preserving those
properties is currently wrong.

## Confirmed Mismatches

### 1. Kit Save removes spaces before creating the folder

`filesystem_makeKitDirectorySaveName()` builds an 8.3 folder input from
`preset_currentName` in `Core/Hardware/SD/filesystem.c:1846`.

Observed behavior in code:

- The folder name begins with the numeric slot prefix.
- Spaces in the kit name are skipped entirely.
- Uppercase letters are converted to lowercase before passing the name to
  asyncfatfs.
- The generated name is capped to eight basename characters total, e.g.
  `001Saved`.

Then asyncfatfs stores that as a FAT short entry such as `001SAVED`.

This does not meet the preferred numbered folder form `NNN Name`, and it cannot
preserve spaces.

### 2. Kit Save cache is updated with a name that may not exist on disk

After writing the kit directory, phase 24 of
`filesystem_saveKitDirectory_tick()` updates the in-memory browser cache in
`Core/Hardware/SD/filesystem.c:2181`.

Observed behavior in code:

- `kit_slot_name[op_slot]` is filled from `preset_currentName`.
- `kit_slot_open_name[op_slot]` is filled from `op_save_kit_dir_name`.

That means the load browser can immediately display the user's logical name
even if the SD card only contains the uppercased/truncated 8.3 entry. On a fresh
scan, the firmware would derive display text from the short alias instead. This
creates two different truths depending on whether the browser is showing the
post-save cache or a card scan.

This is the highest-priority defect.

### 3. Saved instrument member files are intentionally lowercased before FAT
creation, then physically uppercased by asyncfatfs

`storage_makeSavedInstrumentFilename()` generates 8.3 instrument member
filenames from retained Scene stems in `Core/Hardware/SD/storageTypes.c:397`.
`storage_filenameChar()` lowercases all alphabetic characters and replaces
spaces/unsupported characters with `_`.

Then asyncfatfs uppercases the physical FAT short entry. So a retained stem like
`My Snare` becomes a logical generated input like `my_snare.snr`, while the
card contains `MY_SNARE.SNR`.

The Kit can still load because `kitset.kcg` references the same logical 8.3
input and asyncfatfs maps that to the same short entry. But it does not retain
case or spaces, and the saved kitset file records names that are not byte-for-
byte what a user sees in a filesystem browser.

### 4. SceneData preserves logical stems, not physical filenames

`scene_copyInstrumentSourceName()` preserves the first 16 printable stem
characters in `Core/Scene/SceneData.c:104`. This is useful future metadata, but
with the current writer it is not an on-card filename guarantee.

The save path currently treats this retained stem as input for generated 8.3
member filenames. Without LFN creation, this cannot preserve case/space
distinction and should be documented as "logical source stem" only.

### 5. Scanner can read/display LFNs, but writer cannot create them

The Kit scanner reconstructs LFN fragments and uses them as display names when
present. That is good for host-created folders like `001 Slak`.

However, because the writer creates only short entries, firmware-created folders
will not re-enter the system as LFNs after a rescan. The scan fallback will
derive display names from short aliases such as `001SAVED`, not from the
original logical name.

## Required Fix Direction

There are two acceptable end states. The project should choose one explicitly.

### Preferred: implement real asyncfatfs LFN creation

Add an asyncfatfs/filesystem primitive that can create a file or directory with
a requested long display name plus a generated unique short alias.

Minimum requirements:

- Create all required LFN directory entries before the owning SFN entry.
- Compute and store the LFN checksum against the generated SFN.
- Generate collision-safe SFNs using the `BASENA~N.EXT` convention.
- Preserve spaces and mixed case in the LFN UTF-16 entries.
- For names that are valid 8.3 but require lowercase preservation, either emit
  an LFN anyway or set the FAT NT lowercase flags correctly.
- Make open-by-name work for the new files. Existing filesystem code may still
  cache/open the SFN alias after creation, but scans must display the LFN.
- Keep the primitive asynchronous and filesystem-boundary-owned; callers should
  not write raw FAT entries locally.

With this path, Kit Save should create:

- `Kit/NNN <preset_currentName>/`
- `kitset.kcg`
- Instrument files whose visible names come from retained stems when possible.

`kitset.kcg` may reference the short alias or visible filename, but the choice
must be consistent with the loader. If asyncfatfs still opens by SFN, store and
use the generated SFN internally while exposing the LFN in UI.

### Temporary acceptable fallback: display only the physical short names

If LFN creation is too large for the immediate pass, make the firmware honest.

Minimum requirements:

- Rename the Kit Save helpers/comments to say they create short physical names,
  not preferred folder names.
- After save, update `kit_slot_name[]` from the same short-name parser used by a
  real rescan, not from `preset_currentName`.
- Prefer forcing a scan of `Kit/` after save completion instead of hand-editing
  the scan cache. This guarantees the UI reflects what the card actually
  contains.
- Make `op_save_kit_dir_name` match the physical short entry as closely as the
  local API can represent. If asyncfatfs stores uppercase and no NT lowercase
  flags, the cache should not display lowercase.
- For saved instruments, either record/display the physical 8.3 aliases or
  clearly keep logical stems separate from on-card names.

This fallback does not satisfy the desired user-facing naming behavior, but it
does remove the false register and makes future LFN work safer.

## Implementation Plan

1. Add a small FAT naming design note before coding.

   Define three names explicitly at the filesystem boundary:

   - `display_name`: user-facing LFN text, may contain spaces and mixed case.
   - `open_name`: asyncfatfs-openable short alias.
   - `physical_name`: what is actually written to the FAT directory entry.

   No save path should update UI caches with `display_name` unless that exact
   display name was written as an LFN or re-read from a scan.

2. Add a short-name truthfulness fix first.

   In `filesystem_saveKitDirectory_tick()` phase 24, replace the manual
   `preset_currentName` cache update with either:

   - a direct call through the same short-alias recording path used by scan, or
   - a follow-up `filesystem_requestScanKits()`-style scan operation before the
     save reports complete.

   Acceptance: save a kit named with spaces/lowercase, power-cycle or rescan,
   and the Load page must show the same name both before and after rescan.

3. Add or choose the real LFN primitive.

   Extend asyncfatfs with an operation such as:

   ```c
   bool afatfs_mkdir_long(const char *display_name,
                          char open_name_out[13],
                          afatfsFileCallback_t callback);
   bool afatfs_fopen_long(const char *display_name,
                          const char *mode,
                          char open_name_out[13],
                          afatfsFileCallback_t callback);
   ```

   Names are illustrative; the important part is that the primitive owns SFN
   generation, LFN entry allocation, checksum, collision handling, and returns
   the alias the current loader can open later.

4. Implement LFN entry allocation in asyncfatfs.

   Required internals:

   - Parse the requested path component into ASCII/UTF-16 LFN code units.
   - Reject or replace illegal FAT LFN characters consistently.
   - Generate a legal 8.3 SFN alias and test collisions against existing SFN
     entries.
   - Allocate a contiguous run of directory entries large enough for all LFN
     fragments plus the SFN entry.
   - Write LFN entries in correct ordinal/checksum form.
   - Write the SFN entry last so interrupted creation does not expose a
     complete-looking file without its LFN chain.

5. Update Kit Save to use LFN creation.

   `filesystem_makeKitDirectorySaveName()` should become a display-name builder
   for `NNN <name>`, not an 8.3 sanitizer. The create primitive should return
   the actual `open_name` for `kit_slot_open_name[]`.

   Acceptance:

   - Saving `001 My Kit` creates a visible folder with the space.
   - Rescanning displays `My Kit` exactly within the eight-character LCD field.
   - Loading opens the returned/cached SFN alias and succeeds.

6. Update instrument member save naming.

   Use retained `kit->instrument_stem[slot]` as the LFN stem, including spaces
   and case where valid. Keep duplicate handling, but apply it to visible names
   and aliases consistently.

   Acceptance:

   - A loaded stem with mixed case keeps mixed case in the visible file.
   - A loaded stem with a space keeps the space in the visible file.
   - `kitset.kcg` references names that the loader can open and that do not
     mislead a user browsing the card.

7. Preserve compatibility.

   Existing host-generated folders/files must continue to load:

   - Preferred `NNN Name`
   - Compatibility `NNN_Name`
   - Short aliases like `001SLA~1`
   - Existing saved short folders like `001SAVED`
   - Existing generated instrument short files

8. Add targeted tests or host harness coverage.

   At minimum, test pure helper behavior with host-side unit coverage or a small
   fake directory-entry harness:

   - SFN conversion uppercase/truncation is understood and isolated.
   - LFN checksum generation is stable.
   - LFN fragment ordering reconstructs correctly with the existing scanner.
   - Duplicate long names produce distinct aliases.
   - Post-save cache equals post-rescan cache for every tested name.

## Acceptance Matrix

| Case | Required result |
|---|---|
| Save Kit `My Kit` | Visible folder retains the space, or UI displays the exact physical short fallback. |
| Save Kit `aBcDeF` | Case is retained by LFN, or UI displays exact physical uppercase fallback. |
| Save Kit `Long Name` | No cache displays a name that was not physically created. |
| Save instrument `Snare A` | Visible file retains case/space with LFN, or kitset/UI use exact short fallback. |
| Save duplicate instrument stems | Files do not collide; kitset references loadable names. |
| Save then immediate Kit Load browse | Display matches a fresh scan. |
| Power cycle/rescan after save | Display remains identical to immediate post-save display. |

## Documentation Updates After Fix

Update these docs when the implementation lands:

- `MEMORY.md`
- `SCOPING_TARGETS.md`
- `knowledge_files/specification_reference/FILESYSTEM_SPEC.md`
- `knowledge_files/specification_reference/MODULE_INTERCHANGE_SPEC.md`

The docs should stop saying "firmware-created physical names are 8.3-safe" if
LFN creation lands. If only the fallback lands, docs should state plainly that
saved firmware names are short physical names and that the UI deliberately shows
the same short names until LFN creation exists.

## Implementation Notes

### 2026-07-13 pass

Implemented the preferred direction rather than the temporary display-only
fallback.

Asyncfatfs changes:

- Added system-wide public long-name create/open primitives:
  `afatfs_mkdir_lfn()` and `afatfs_fopen_lfn()`.
- Added public filename buffer constants:
  `AFATFS_SHORT_FILENAME_MAX` and `AFATFS_LONG_FILENAME_MAX`.
- Kept existing `afatfs_mkdir()` and `afatfs_fopen()` behavior unchanged for
  old 8.3 callers.
- Extended the create-file operation state with a bounded display component,
  generated short alias, returned alias pointer, LFN scan accumulator, and
  sector-local free-run tracking.
- Added VFAT LFN helper logic for:
  - ASCII display-name sanitization.
  - SFN alias generation with `~N` collision fallback.
  - LFN checksum generation.
  - Matching an existing SFN alias only when its preceding LFN chain matches
    the requested display component.
  - Writing LFN fragments followed by the owning SFN entry.
- The first LFN writer reserves a contiguous run inside one directory sector.
  This avoids assuming directory clusters are physically contiguous. If no
  sector has enough free entries, the existing directory-extension operation is
  used and retried through the normal async poll path.

Filesystem Kit Save changes:

- `filesystem_makeKitDirectorySaveName()` was replaced with an LFN display-name
  builder that emits the spec-preferred `NNN <name>` component.
- Kit Save now calls `afatfs_mkdir_lfn()` for the target Kit folder and stores
  the returned short alias in `kit_slot_open_name[]`.
- Instrument member saves now generate visible filenames preserving retained
  stem spaces/case, call `afatfs_fopen_lfn()`, and store returned short aliases
  in the existing `op_save_instrument_file[][]` array.
- Kit Save now writes the six instrument files before `kitset.kcg`, because the
  final aliases are only known after each LFN create/open resolves collisions.
  `kitset.kcg` is written last with the returned aliases.
- The post-save Kit browser cache is updated from the actual display component
  and returned alias, not from `preset_currentName`.

Storage helper changes:

- Added `storage_makeSavedInstrumentDisplayFilename()` beside the old 8.3
  helper. The old helper remains available for compatibility/fallback callers.
- The new display helper preserves spaces and mixed case, trims unsafe trailing
  spaces/dots, replaces FAT-forbidden characters with `_`, and appends the
  descriptor-owned extension.

Verification status:

- Normal firmware build could not be run in this environment because `make`,
  `gcc`, `clang`, and `arm-none-eabi-gcc` were not installed.
- A manual static pass was performed over the touched state machines. The
  highest-risk areas for hardware/real-build verification are:
  - LFN create on an empty directory sector.
  - LFN create when alias ordinal zero collides and `~1` is selected.
  - LFN create when the current directory must be extended.
  - Save same Kit twice and confirm the second save reuses the same LFN/SFN
    pair rather than creating another alias.
  - Immediate post-save Kit Load browse compared with a fresh rescan/power
    cycle.
