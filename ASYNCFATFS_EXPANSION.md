# asyncfatfs Long Filename + Case Expansion Plan

## Scope

This is an initial plan for reworking `Core/Hardware/SD/asyncfatfs/` so it
becomes a real mixed-case, long-filename-capable FAT layer for both files and
directories. It is based on the current source tree, not on a desired API or on
the higher-level Kit/Scene save code.

The goal is to make asyncfatfs itself own the FAT/VFAT filename rules:

- preserve mixed case for short 8.3 names via FAT `ntReserved` bits;
- create, scan, match, and delete VFAT long filename chains;
- support long names for both files and directories through one internal create
  path;
- return enough metadata to callers that existing loaders/savers no longer need
  to reconstruct LFN behavior manually;
- keep all operations asynchronous and compatible with the existing cache,
  open-file, and `afatfs_poll()` model.

## Repo Recheck After Scene-Entry Reset

The repository was reset back to the branch that already contains the Scene
Load/Save work and the earlier partial asyncfatfs filename work. This changes
the starting snapshot for the plan, but not the target architecture.

Current context differences from the first draft of this document:

- `menu.h` currently exposes `SAVE_TYPE_KIT`, `SAVE_TYPE_KIT_MORPH`,
  `SAVE_TYPE_SCENE`, `SAVE_TYPE_GLO`, and `SAVE_TYPE_SAMPLES`. These entries
  are still considered stale for this expansion and should be replaced or made
  unreachable while the generic `File` / `Dir` test menus are active.
- `filesystem.h/c` currently include `FS_FILE_SCENE`,
  `filesystem_requestLoadSceneForScenes()`,
  `filesystem_requestSaveSceneDirectory()`, `filesystem_requestScanScenes()`,
  `FS_INTERNAL_OP_LOAD_SCENE`, `FS_INTERNAL_OP_SAVE_SCENE`, and
  `FS_INTERNAL_OP_SCAN_SCENES`. These are useful history, but the File/Dir test
  facade should be separate and should not depend on Scene-specific caches.
- `filesystem.c` already has `FS_INTERNAL_OP_FLUSH_FINISH` and a deferred
  completion drain. The sync-boundary stage should therefore audit and finish
  that mechanism, then expose a clean asyncfatfs-level `afatfs_sync()` contract
  rather than treating flush completion as wholly absent.
- `asyncfatfs.h` already exposes `AFATFS_SHORT_FILENAME_MAX`,
  `AFATFS_LONG_FILENAME_MAX`, `afatfs_fopen_lfn()`, and `afatfs_mkdir_lfn()`.
  The current signatures return short aliases but do not accept a case-sensitive
  match mode and do not return full object metadata.
- `asyncfatfs.c` already contains partial LFN creation/scanning state inside
  `afatfsCreateFile_t`, including `longName`, `scanLongName`,
  `scanLongNameChecksum`, free-run tracking, alias generation, and
  directory-initialization handoff. This should be treated as a prototype to
  refactor or replace with the object-scanner design below, because it is still
  create/open-local rather than a general directory-object layer.
- `fat_standard.c/h` already contain `fat_calculateFilenameCaseFlags()` and
  `fat_applyFilenameCaseFlags()`. The case-preservation stage should audit
  these helpers instead of re-adding them from scratch. LFN structs, constants,
  checksum helpers, fragment encode/decode, and compare policy are still not
  centralized in `fat_standard`.
- `filesystem.c` still has local LFN scan helpers for Kit, Scene, Instrument,
  and sample scans. These should eventually collapse onto
  `afatfs_findNextObject()` once asyncfatfs owns object enumeration.

Net effect: the implementation plan remains valid, but several tasks become
"promote and harden the existing partial code" rather than brand-new additions.
The test-menu directive is unchanged: the only expected working Load/Save
surface during this expansion is `Load:[File]`, `Load:[Dir]`, `Save:[File]`,
and `Save:[Dir]`.

## FAT Reality Check

FAT short names are case-insensitive lookup keys. The on-disk 8.3 entry stores
uppercase bytes in `filename[11]` and uses `ntReserved` flags to say whether the
base and/or extension should be displayed in lowercase.

VFAT long names preserve mixed-case UTF-16 display text in one or more directory
entries immediately before the owning short 8.3 entry. The short entry remains
the physical identity for cluster, size, attributes, timestamps, and deletion.

Therefore "full case sensitive support" has two separate meanings:

- **case preservation:** asyncfatfs must round-trip and display names using the
  exact case stored in short-name case bits or LFN entries;
- **case-sensitive firmware matching:** new LFN APIs can compare long display
  names byte-for-byte so `Foo.txt` and `foo.txt` are different to firmware if
  both exist on a card.

Host operating systems commonly treat FAT names case-insensitively even when
case is preserved. The firmware can avoid creating ambiguous same-folded names,
or it can allow them for its own use and accept that desktop behavior may be
surprising. This needs a policy decision before implementation.

## Current asyncfatfs State

### Public API

`asyncfatfs.h` currently exposes:

- `afatfs_fopen(filename, mode, complete)`
- `afatfs_fopen_lfn(displayName, mode, openNameOut, complete)`
- `afatfs_mkdir(filename, complete)`
- `afatfs_mkdir_lfn(displayName, openNameOut, complete)`
- `afatfs_chdir(dirHandle)`
- `afatfs_findFirst/findNext/findLast`
- basic read/write/seek/close/truncate/unlink/flush lifecycle calls

Names are single path components only. Paths are not parsed. The partial LFN
APIs can create/open by display name and return a generated short alias, but
they do not accept a case-sensitive match policy, do not return full object
metadata, and do not expose an LFN-aware directory iterator. The compatibility
API remains 8.3-oriented.

### Name Conversion

`fat_standard.c` currently has only:

- `fat_convertFilenameToFATStyle()`
- `fat_convertFATStyleToFilename()`
- `fat_calculateFilenameCaseFlags()`
- `fat_applyFilenameCaseFlags()`

`fat_convertFilenameToFATStyle()` uppercases every byte with `toupper()`. It
does not compute `ntReserved`, validate 8.3 legality, handle 0xE5 first-byte
escaping, or distinguish "valid short name" from "requires LFN".

`fat_calculateFilenameCaseFlags()` and `fat_applyFilenameCaseFlags()` now cover
the all-lowercase short-name case-bit path. Mixed-case names still require LFN
entries, and VFAT checksum/fragment helpers are still private to asyncfatfs or
filesystem local code instead of living in `fat_standard`.

### Directory Scanning

`afatfs_findNext()` returns one raw `fatDirectoryEntry_t *` at a time. It does
not filter volume labels, dot entries, deleted entries, or LFN entries. Callers
must inspect raw FAT entries themselves.

This is workable for 8.3-only code, but it is the wrong boundary for LFN. VFAT
identity is a chain of long entries plus a following owning SFN entry. Returning
raw entries makes every caller responsible for maintaining the LFN chain state
and checksum validation.

### Create/Open State Machine

`afatfs_createFileContinue()` currently scans the current directory for a raw
11-byte `filename` match. If no match is found and create mode is set, it calls
`afatfs_allocateDirectoryEntry()` and writes one SFN entry.

Important current limitations:

- the create operation still treats generated `filename[11]` as the physical
  identity and keeps LFN display matching local to create/open;
- free-space tracking exists for the current LFN create path, but there is no
  reusable directory-entry-run allocator exposed to delete/rename/object scan;
- existing LFN reconstruction exists inside the create scanner, but there is no
  general object iterator that higher layers can use;
- LFN matching currently opens or creates through `fopen_lfn()`/`mkdir_lfn()`
  without a caller-selected case-sensitive policy;
- deletion/truncate deletes only the SFN entry and does not know about preceding
  LFN entries;
- directory creation has a handoff toward `afatfs_extendSubdirectory()`, but
  that behavior should be made a formal create phase and retested through the
  generic File/Dir menus rather than trusted as a Kit-save side effect.

### Directory Extension

`afatfs_extendSubdirectory()` already exists and can allocate a cluster,
zero-fill it, and create `.` / `..` entries for the first sector of a new
subdirectory. The current tree has a directory-initialization handoff for newly
created directories, but the expansion should audit it as part of the unified
object create state machine and prove it with `Save:[Dir]`.

### Open-File Constraints

`AFATFS_MAX_OPEN_FILES` is currently `3`. The current Kit/Scene save machinery
needs to enter directories and create files while holding callbacks and state.
Any LFN expansion must respect this limit or raise it deliberately with RAM
accounting.

## Required Design Decisions

1. **Lookup policy:** should LFN APIs be case-sensitive, case-insensitive, or
   selectable per call?

   Recommendation: add flags and default the compatibility API to
   case-insensitive SFN behavior, while the new LFN API can request
   case-sensitive matching.

2. **Ambiguous names:** should firmware allow two names that differ only by
   case, such as `KitSet.kcg` and `kitset.kcg`?

   Recommendation: reject same-casefold collisions for user-facing save paths
   unless there is a strong reason to allow them. This keeps cards sane on macOS
   and Windows.

3. **Path parsing:** should asyncfatfs support only one component at a time, or
   should it accept paths like `Kit/060 Smpty/kitset.kcg`?

   Recommendation: keep the first implementation component-based. Add correct
   `openLong/mkdirLong/findObject` primitives first. Path parsing can be a later
   wrapper around the same primitives.

4. **Character set:** VFAT LFN entries are UTF-16. The firmware currently uses
   printable ASCII names.

   Recommendation: implement ASCII/UTF-16LE subset first, reject or replace
   unsupported input bytes explicitly, and document that non-ASCII host-created
   names can be scanned but may be reported with substitution until a real
   Unicode policy exists.

## New Public API Shape

Keep existing calls as compatibility wrappers:

```c
bool afatfs_fopen(const char *filename,
                  const char *mode,
                  afatfsFileCallback_t complete);

bool afatfs_mkdir(const char *filename,
                  afatfsFileCallback_t complete);
```

Add a richer object-open API:

```c
typedef enum {
    AFATFS_NAME_MATCH_COMPAT_SFN = 0,
    AFATFS_NAME_MATCH_LFN_CASE_INSENSITIVE,
    AFATFS_NAME_MATCH_LFN_CASE_SENSITIVE,
} afatfsNameMatchMode_t;

typedef enum {
    AFATFS_OBJECT_FILE,
    AFATFS_OBJECT_DIRECTORY,
} afatfsObjectKind_t;

typedef struct {
    afatfsObjectKind_t kind;
    afatfsNameMatchMode_t matchMode;
    uint8_t create;
    uint8_t truncateExisting;
    uint8_t failIfExists;
    char shortNameOut[13];
    char displayNameOut[AFATFS_LONG_FILENAME_MAX + 1];
} afatfsOpenOptions_t;

bool afatfs_openObject(const char *displayName,
                       const char *mode,
                       afatfsOpenOptions_t *options,
                       afatfsFileCallback_t complete);
```

This is only a sketch. The actual header can be smaller, but the important
point is that object kind, match policy, creation behavior, returned SFN alias,
and returned display name must all be first-class asyncfatfs concepts.

Add an LFN-aware directory iterator:

```c
typedef struct {
    char displayName[AFATFS_LONG_FILENAME_MAX + 1];
    char shortName[13];
    uint8_t attrib;
    uint8_t hasLongName;
    uint8_t ntReserved;
    afatfsDirEntryPointer_t sfnEntry;
    afatfsDirEntryPointer_t lfnFirstEntry;
    uint8_t lfnEntryCount;
} afatfsDirectoryObject_t;

afatfsOperationStatus_e afatfs_findNextObject(
    afatfsFilePtr_t directory,
    afatfsFinder_t *finder,
    afatfsDirectoryObject_t *object);
```

`afatfs_findNext()` can remain for legacy raw scans, but new filesystem code
should use `afatfs_findNextObject()`.

## Internal Data Structures Required

### FAT LFN Entry Type

Add a packed VFAT LFN structure to `fat_standard.h`:

```c
typedef struct fatLongDirectoryEntry_t {
    uint8_t sequenceNumber;
    uint16_t name1[5];
    uint8_t attrib;
    uint8_t type;
    uint8_t checksum;
    uint16_t name2[6];
    uint16_t firstClusterLow;
    uint16_t name3[2];
} __attribute__((packed)) fatLongDirectoryEntry_t;
```

Add constants:

- `FAT_FILE_ATTRIBUTE_LONG_NAME`
- `FAT_LFN_SEQUENCE_LAST`
- `FAT_LFN_CHARS_PER_ENTRY`
- `FAT_NTRES_LOWERCASE_BASE`
- `FAT_NTRES_LOWERCASE_EXT`

### FAT Name Helpers

Add helpers in `fat_standard.c/h`:

- `fat_isLongDirectoryEntry(entry)`
- `fat_lfnChecksum(sfn[11])`
- `fat_calculateShortNameCaseFlags(name)`
- `fat_applyShortNameCaseFlags(name, ntReserved)`
- `fat_isLegalShortName(name)`
- `fat_makeShortNameCandidate(displayName, ordinal, out11, outPrintable)`
- `fat_writeLongDirectoryEntry(entry, displayName, ordinal, checksum)`
- `fat_readLongDirectoryEntryFragment(entry, buffer, capacity)`
- `fat_caseFoldAsciiChar()` and `fat_compareDisplayName()`

These belong in `fat_standard` because they are FAT format rules, not
Kit/Scene storage policy.

### Create/Open State

Expand `afatfsCreateFile_t` from:

```c
uint8_t phase;
uint8_t filename[FAT_FILENAME_LENGTH];
```

to include:

- requested display name;
- generated SFN candidate;
- returned short alias buffer pointer;
- returned display name buffer pointer;
- object kind;
- match mode;
- LFN entry count;
- alias ordinal for `~1`, `~2`, etc.;
- scan-time LFN reconstruction buffer;
- scan-time LFN checksum;
- free-run start pointer;
- free-run length;
- first LFN entry pointer for deletion/replacement;
- existing-object metadata.

The create state machine should no longer consider `filename[11]` to be the
whole identity.

### Directory Object Scanner

Implement one internal scanner that reads raw directory entries and emits only
real objects:

1. Skip deleted entries.
2. Accumulate LFN entries in reverse order.
3. Validate sequence numbers and checksum against the following SFN entry.
4. Convert UTF-16LE fragments to firmware display text.
5. Convert SFN fallback names using `ntReserved`.
6. Return metadata for the SFN entry and the preceding LFN run.

This scanner should be used by:

- `afatfs_findNextObject()`;
- `afatfs_createFileContinue()`;
- future delete/rename logic;
- any higher-level scan that wants names.

## Create/Open State Machine Rework

### Phase 1: Parse Request

Before scanning:

- sanitize/validate the requested display name;
- determine whether a plain SFN entry is enough;
- generate the first legal SFN candidate;
- compute `ntReserved` case flags for short-name-only objects;
- compute LFN entry count for long-name objects.

Inputs: display component, object kind, open mode/options.
Outputs: create state with normalized request, generated SFN candidate, and
required directory entry count.

### Phase 2: Scan Current Directory as Objects

Use the new object scanner rather than raw `strncmp()`.

For each object:

- compare LFN display name according to the requested match mode;
- also compare SFN alias for compatibility opens;
- reject object-kind mismatches unless the caller explicitly permits them;
- track alias collisions against generated SFN candidates;
- track a contiguous free/deleted/terminator entry run large enough for the
  requested LFN chain plus SFN.

Important loop comment needed in code: the scanner must continue through LFN
entries and only make identity decisions at the owning SFN entry.

### Phase 3: Existing Object Open

When an existing object matches:

- load the SFN entry into `afatfsFile_t`;
- copy actual display name and short name to output buffers if requested;
- if opened for write-only with create/truncate semantics, truncate via the
  existing truncate path;
- if opened for append, seek to end as current code does;
- do not rewrite LFN metadata unless the caller explicitly requests rename.

### Phase 4: New Object Create

When no object matches and create is allowed:

- ensure a free run large enough for `lfnEntryCount + 1`;
- if necessary, extend the current directory and resume scanning;
- write all LFN entries first, then write the owning SFN entry;
- set SFN `ntReserved` correctly;
- mark the directory sector dirty;
- set `file->directoryEntryPos` to the SFN entry, not the first LFN entry;
- retain LFN metadata in the create state for possible rollback/error cleanup.

### Phase 5: Directory Initialization

If the object kind is directory and the object was newly created:

- do not call the public callback after writing only the SFN entry;
- hand the file handle to `afatfs_extendSubdirectory()` with the current
  directory as parent;
- allocate the first cluster;
- write first-cluster fields back to the SFN entry;
- zero-fill the whole cluster;
- create `.` and `..`;
- only then invoke the callback with the directory handle.

This must be a formal phase of the create state machine, not an incidental
helper hidden in Kit Save. New directories are not valid until this completes.

### Phase 6: Failure Cleanup

If creation partially wrote LFN entries or an SFN entry and then fails:

- mark any created entries deleted where possible;
- clear the file handle operation;
- invoke callback with `NULL`;
- leave `afatfs.lastError` or a new per-operation error code set for diagnosis.

Current code clears only `file->type` on failure. The expanded implementation
needs stricter cleanup because LFN creation can dirty multiple entries.

## Delete / Unlink Requirements

`afatfs_funlink()` currently marks only the SFN entry deleted after truncation.
With LFN support it must also delete the preceding LFN entries that belong to
that SFN entry.

Required work:

- store `lfnFirstEntry` and `lfnEntryCount` when opening an object;
- teach unlink to mark the full LFN run deleted after truncating clusters;
- ensure deletion handles objects opened by SFN-only compatibility paths, where
  LFN metadata may not have been scanned unless open uses the object scanner.

Recommendation: all opens should eventually use the object scanner internally,
even compatibility `afatfs_fopen()`, so delete always has metadata.

## Rename / Replace Policy

Full long-name support eventually needs `rename` or "replace object by display
name" support. Kit/Scene save wants overwrite semantics, and trying to fake
that with `mkdir()` caused duplicate-name failure modes.

Add later, after create/open/delete are correct:

```c
bool afatfs_renameObject(afatfsFilePtr_t object,
                         const char *newDisplayName,
                         afatfsCallback_t complete);
```

Rename is harder than create because the LFN entry count can change. The safe
implementation is:

- if new LFN count fits in the existing LFN run, rewrite in place;
- otherwise allocate a new directory entry run, copy SFN metadata, write new LFN
  chain, then mark old entries deleted.

Do not make Kit Save depend on rename in the first pass.

## Flush and Completion Boundary

`afatfs_flush()` currently attempts cache writeback. For removable-media save
semantics, callers need a completion boundary that means:

- no dirty cache entries remain;
- no sector write is in flight;
- all file handles involved in the operation are closed or stable.

Add a public helper or strengthen the existing contract:

```c
bool afatfs_sync(void);
```

`afatfs_sync()` can call/extend `afatfs_flush()` and explicitly wait until
`cacheFlushInProgress == false`. Higher-level save completion should not report
done until this succeeds.

## Compatibility Strategy

Do not break existing loaders/savers during asyncfatfs expansion.

1. Keep `afatfs_fopen()` and `afatfs_mkdir()` signatures.
2. Implement them as wrappers around the new object API.
3. Preserve legacy behavior for 8.3 names:
   - uppercase raw SFN lookup remains accepted;
   - read opens still find existing short entries;
   - write opens still create/truncate as before.
4. Add new APIs separately:
   - `afatfs_fopen_lfn()` or `afatfs_openObject()`;
   - `afatfs_mkdir_lfn()` or directory object option;
   - `afatfs_findNextObject()`.
5. Move higher-level Kit/Scene code to the new APIs only after asyncfatfs has
   focused tests or hardware probes proving create/open/delete/flush.

## Implementation Stages

### Stage 1: Short-Name Case Preservation

Files:

- `Core/Hardware/SD/asyncfatfs/fat_standard.h`
- `Core/Hardware/SD/asyncfatfs/fat_standard.c`
- `Core/Hardware/SD/asyncfatfs/asyncfatfs.c`

Changes:

- add `FAT_NTRES_LOWERCASE_BASE` and `FAT_NTRES_LOWERCASE_EXT`;
- compute case flags before uppercasing SFN keys;
- write `entry->ntReserved` on new short entries;
- apply case flags when converting SFN entries to printable names;
- update scans to use printable SFN conversion instead of raw uppercase.

Exit test:

- create `kitset.kcg`, remount on desktop, confirm it displays lowercase;
- open the same file using `KITSET.KCG`, `kitset.kcg`, and mixed-case spellings.

### Stage 2: LFN Parser and Object Iterator

Files:

- `fat_standard.h/c`
- `asyncfatfs.h/c`

Changes:

- define LFN entry struct and constants;
- implement checksum and fragment decode;
- add `afatfsDirectoryObject_t`;
- add `afatfs_findNextObject()`;
- keep `afatfs_findNext()` unchanged for raw legacy users.

Exit test:

- scan a directory containing host-created `060 Smpty`, `Slkty1.drm`, and a
  short-only `KITSET.KCG`;
- confirm iterator returns display names, short aliases, attributes, and LFN
  metadata correctly.

### Stage 3: LFN Creation for Files

Files:

- `asyncfatfs.c`
- `asyncfatfs.h`
- `fat_standard.c/h`

Changes:

- expand `afatfsCreateFile_t`;
- add free-run tracking for multiple entries;
- generate SFN aliases with collision handling;
- write VFAT LFN entries plus final SFN entry;
- add `afatfs_fopen_lfn()` or `afatfs_openObject()`.

Exit test:

- create `Slkty1.drm` and `kitset.kcg`;
- create two long names with same first alias candidate and prove `~1`, `~2`
  collision handling;
- remount on desktop and verify names and file contents.

### Stage 4: LFN Creation for Directories

Files:

- `asyncfatfs.c`
- `asyncfatfs.h`

Changes:

- add object kind to create state;
- add `afatfs_mkdir_lfn()` or object API directory option;
- route newly-created directories through `afatfs_extendSubdirectory()` before
  callback success;
- guarantee `chdir()` on a callback directory handle is safe.

Exit test:

- create `Kit/060 Smpty/`;
- `chdir()` into it immediately in the callback path;
- create `kitset.kcg` inside it;
- close/sync/remount and confirm folder and file exist.

### Stage 5: LFN-Aware Delete

Files:

- `asyncfatfs.c`
- `asyncfatfs.h`

Changes:

- carry LFN run metadata on opened file handles or a sidecar structure;
- update `afatfs_funlink()` to delete both LFN entries and SFN entry;
- ensure truncation still frees cluster chains before directory entries are
  marked deleted.

Exit test:

- create a long-named file, delete it, remount, confirm no orphan LFN entries
  appear in desktop repair tools or later scans.

### Stage 6: Sync Boundary

Files:

- `asyncfatfs.c`
- `asyncfatfs.h`
- higher-level filesystem wrapper later

Changes:

- define `afatfs_sync()` or strengthen `afatfs_flush()`;
- document that save completion must wait for dirty entries and in-flight
  writes;
- verify SD driver callbacks happen after card busy release.

Exit test:

- create folder/file, call sync until true, power off immediately, remount and
  confirm persistence.

### Stage 7: Higher-Level Migration

Only after Stages 1-6 are verified:

- migrate Kit Save to `mkdir_lfn/openObject`;
- migrate Scene Save;
- migrate Instrument Save;
- implement overwrite policy using object open/delete/replace primitives;
- reintroduce Morph Save.

## Test Harness Recommendation

The current failure loop is too slow if every bug requires flashing hardware and
mounting a card. Add a tiny test harness around asyncfatfs with a block-device
shim that writes to a local image file.

Minimum useful tests:

- SFN lowercase case-bit round trip;
- LFN checksum encode/decode;
- LFN scan with deleted entries and broken chains;
- create LFN file;
- create LFN directory and immediately `chdir`;
- delete LFN file/directory entry chain;
- sync boundary after create/write/close;
- alias collision generation.

If a full block-device shim is too much, add temporary firmware diagnostic
operations that create fixed test objects and report the failing asyncfatfs
phase through a small status accessor. But the long-term answer should be
host-side image tests.

## Main Risks

- **Directory entry runs crossing sectors:** VFAT LFN chains should be stored
  contiguously in directory entries. The implementation should either support
  runs across sectors/clusters correctly or deliberately require a same-sector
  run and extend/continue until one exists.
- **Open-file handle pressure:** LFN create/delete needs more metadata, and
  higher-level save paths may hold directory handles while opening child files.
  `AFATFS_MAX_OPEN_FILES` may need an explicit RAM-cost review.
- **Partial creation cleanup:** multi-entry creates can leave orphan LFN entries
  if failure paths are not designed up front.
- **Case ambiguity on desktop OSes:** firmware case-sensitive lookup can create
  names that desktop tools treat as aliases or conflicts.
- **Unicode:** ASCII-only LFN is enough for current Kit/Scene names, but the
  implementation must not corrupt non-ASCII host-created entries.
- **FAT16 root directory:** FAT16 root cannot be extended. Any multi-entry LFN
  creation in FAT16 root must fail cleanly when no contiguous free run exists.

## Recommended Next Step

Do not touch Kit Save first. Implement Stage 1 and Stage 2 in asyncfatfs only,
then add one narrow hardware or image-level proof:

1. scan and display a host-created long directory name;
2. create one lowercase 8.3 file and verify case preservation;
3. create one long-name file and verify desktop visibility.

Only after those pass should directory creation and Kit Save be rebuilt on top
of the new primitives.

## Detailed Code-Dive Expansion: Case-Sensitive File/Dir Test Menus

The current Load/Save menu entries are considered stale for this expansion.
They are allowed to break or be removed while asyncfatfs is rebuilt. The only
Load/Save entries that should be treated as working acceptance targets during
this phase are:

- `Load:[File    ]`
- `Load:[Dir     ]`
- `Save:[File    ]`
- `Save:[Dir     ]`

Kit, Settings, Samples, Scene, Morph, Pattern, ALL, Performance, and any older
legacy save/load behavior should be moved out of the UI path or left
unreachable until the new asyncfatfs primitives pass these tests.

### User-Facing Test Behavior

`Load:[File    ]`

- Browse all files in the current test root directory.
- Do not show directories.
- Preserve exact case in the displayed filename.
- The lower row shows the first eight display characters of the filename. If
  the full filename including extension fits inside eight characters, include
  the extension.
- Show `ok` at the right.
- When `ok` is selected and clicked, open the selected file case-sensitively and
  read the first four bytes.
- Display those bytes for two seconds as hex, split across both LCD rows. The
  exact LCD geometry should fit 16 columns; a workable layout is:
  - row 1: `0x32    0xF3    `
  - row 2: `0x91    0x08    `

`Load:[Dir     ]`

- Browse all directories in the current test root directory.
- Do not show files.
- Preserve exact case in the displayed directory name.
- The lower row shows the first eight display characters of the directory name.
- Show `ok` at the right.
- When `ok` is selected and clicked, enter the selected directory and inspect
  the first alphanumerically sorted object inside it.
- If the first sorted object is a file, read and display its first four bytes
  using the same two-row hex format as `Load:[File]`.
- If the first sorted object is a directory, display `Dir: <name>` for two
  seconds, where `<name>` is the first eight characters of the subdirectory name
  with exact case preserved.

`Save:[File    ]`

- Edit a filename including extension using the normal encoder name mechanism,
  expanded beyond the existing eight-character preset name model.
- Show `ok` at the right.
- When `ok` is selected and clicked, create or overwrite exactly that file name
  case-sensitively.
- Write four random bytes to the file.
- Sync the file and directory metadata before reporting completion.
- Display the saved four bytes for two seconds using the same two-row hex
  format.

`Save:[Dir     ]`

- Edit a directory name, including a period/extension if the user enters one,
  using the same expanded encoder filename mechanism.
- Show `ok` at the right.
- When `ok` is selected and clicked, create exactly that directory name
  case-sensitively.
- Enter the directory.
- Save four random bytes to a file inside that directory using the same name as
  the directory.
- Sync the file and directory metadata before reporting completion.
- Display the saved four bytes for two seconds using the same two-row hex
  format.

## Required asyncfatfs Code Changes

### `fat_standard.h`: Add FAT/VFAT filename constants and structs

Add:

- `FAT_FILE_ATTRIBUTE_LONG_NAME`
- `FAT_NTRES_LOWERCASE_BASE`
- `FAT_NTRES_LOWERCASE_EXT`
- `FAT_LFN_SEQUENCE_LAST`
- `FAT_LFN_CHARS_PER_ENTRY`
- `FAT_SHORT_NAME_PRINTABLE_MAX`
- `FAT_LFN_MAX_CHARS` or an asyncfatfs-level equivalent
- packed `fatLongDirectoryEntry_t`

Comment text to place near the constants:

```c
/*
 * FAT short names and VFAT long names are separate layers.
 *
 * filename[11] in a normal directory entry is the case-insensitive physical
 * lookup key. ntReserved carries lowercase-display bits for short 8.3 names.
 * VFAT long-name entries use attribute 0x0f and store the exact display text
 * as UTF-16 fragments immediately before the owning short entry. asyncfatfs
 * uses both layers: short names remain the cluster/size owner, while LFN
 * entries provide exact-case user-visible identity for the new object APIs.
 */
```

Comment text for `fatLongDirectoryEntry_t`:

```c
/*
 * On-disk VFAT long filename entry.
 *
 * Each entry stores up to thirteen UTF-16LE code units. The entries appear in
 * reverse order immediately before the owning SFN entry, and every fragment
 * repeats the checksum of that SFN. firstClusterLow must remain zero for LFN
 * entries. The struct is packed because it overlays one 32-byte FAT directory
 * slot exactly.
 */
```

### `fat_standard.c/h`: Add short-name case helpers

Add:

- `uint8_t fat_calculateFilenameCaseFlags(const char *filename);`
- `void fat_applyFilenameCaseFlags(char *filename, uint8_t ntReserved);`
- `uint8_t fat_filenameHasLowercaseBase(...)` as private helper if useful;
- `uint8_t fat_filenameHasLowercaseExt(...)` as private helper if useful.

Comment text:

```c
/*
 * Compute FAT ntReserved case-display bits before the name is uppercased.
 *
 * Inputs: a caller's printable 8.3 filename component. Output: base/ext
 * lowercase flags only when the corresponding component is entirely lowercase
 * according to FAT's short-name display convention. Mixed-case short names
 * require VFAT LFN entries; they cannot be represented exactly with only
 * ntReserved.
 */
```

Important logic comment:

```c
/*
 * FAT short-name case flags are all-or-nothing per component. A base name such
 * as "kitset" can display lowercase through ntReserved; "KitSet" cannot, so
 * callers that require exact mixed case must request LFN creation instead of
 * relying on the short entry.
 */
```

### `fat_standard.c/h`: Add VFAT checksum and fragment helpers

Add:

- `uint8_t fat_lfnChecksum(const uint8_t sfn[FAT_FILENAME_LENGTH]);`
- `uint8_t fat_isLongDirectoryEntry(const fatDirectoryEntry_t *entry);`
- `void fat_lfnClearNameBuffer(char *dst, uint16_t cap);`
- `uint8_t fat_lfnAppendFragment(...)` or equivalent;
- `void fat_lfnWriteFragment(...)`;
- `int fat_compareDisplayName(const char *a, const char *b, uint8_t caseSensitive);`

Comment text for checksum:

```c
/*
 * Calculate the VFAT checksum for one owning short filename.
 *
 * Every LFN fragment stores this checksum. During scans the checksum proves
 * that the accumulated long-name chain belongs to the following SFN entry and
 * not to a stale/deleted fragment sequence. During creation the same checksum
 * links all newly-written fragments to the generated short alias.
 */
```

Comment text for fragment decode:

```c
/*
 * Decode one VFAT UTF-16LE fragment into an ASCII firmware display buffer.
 *
 * The first implementation intentionally supports the printable ASCII subset
 * used by firmware-generated test names. Unsupported non-ASCII code units are
 * replaced with '_' for display but must still cause exact-match failure for
 * case-sensitive opens, so host-created Unicode names are not accidentally
 * overwritten by an ASCII request.
 */
```

### `asyncfatfs.h`: Add explicit LFN/object API

Add public constants and types:

```c
#define AFATFS_LONG_FILENAME_MAX 80
#define AFATFS_SHORT_FILENAME_MAX 13

typedef enum {
    AFATFS_OBJECT_FILE,
    AFATFS_OBJECT_DIRECTORY,
} afatfsObjectKind_t;

typedef enum {
    AFATFS_MATCH_CASE_SENSITIVE,
    AFATFS_MATCH_CASE_INSENSITIVE,
} afatfsMatchMode_t;

typedef struct {
    char displayName[AFATFS_LONG_FILENAME_MAX + 1u];
    char shortName[AFATFS_SHORT_FILENAME_MAX];
    uint8_t attrib;
    uint8_t hasLongName;
    uint8_t ntReserved;
    afatfsObjectKind_t kind;
    afatfsDirEntryPointer_t sfnEntry;
    afatfsDirEntryPointer_t lfnFirstEntry;
    uint8_t lfnEntryCount;
} afatfsObjectInfo_t;
```

Add APIs:

```c
bool afatfs_fopen_lfn(const char *displayName,
                      const char *mode,
                      afatfsMatchMode_t matchMode,
                      char shortNameOut[AFATFS_SHORT_FILENAME_MAX],
                      afatfsFileCallback_t complete);

bool afatfs_mkdir_lfn(const char *displayName,
                      afatfsMatchMode_t matchMode,
                      char shortNameOut[AFATFS_SHORT_FILENAME_MAX],
                      afatfsFileCallback_t complete);

afatfsOperationStatus_e afatfs_findNextObject(
    afatfsFilePtr_t directory,
    afatfsFinder_t *finder,
    afatfsObjectInfo_t *object);

bool afatfs_sync(void);
```

Comment text for the new API block:

```c
/*
 * Case-sensitive long-name object API.
 *
 * These calls treat the user-visible display component as the requested object
 * identity. VFAT LFN entries preserve exact case for mixed-case and long names;
 * short 8.3 entries use ntReserved when that can preserve the exact display
 * case. matchMode controls lookup comparison, not storage: storage always
 * preserves the exact representable case, while AFATFS_MATCH_CASE_SENSITIVE
 * refuses to open a name whose spelling differs by case.
 */
```

Comment text for `afatfs_findNextObject()`:

```c
/*
 * Iterate logical directory objects rather than raw 32-byte FAT entries.
 *
 * The iterator accumulates and validates any preceding VFAT LFN fragments,
 * then returns one object when the owning SFN entry is reached. Deleted,
 * volume-label, orphaned LFN, and malformed chains are skipped. Callers that
 * need user-visible names should use this API instead of afatfs_findNext().
 */
```

### `asyncfatfs.c`: Expand create/open state

Replace the narrow `afatfsCreateFile_t` state with fields for:

- callback;
- create/open phase;
- requested display name;
- generated SFN `filename[11]`;
- printable short alias;
- short-name case flags;
- object kind;
- match mode;
- LFN entry count;
- alias ordinal;
- returned short-name pointer;
- current scan reconstructed LFN buffer;
- scan LFN validity/checksum/entry count;
- free-run start and length;
- owning SFN pointer for existing match;
- first LFN pointer and count for existing match.

Comment text:

```c
/*
 * Create/open operation state for both SFN and VFAT LFN objects.
 *
 * filename[11] is only the generated physical alias. The requested display
 * component, match mode, object kind, and LFN run metadata are kept separately
 * because long filenames are identified by the validated LFN chain plus owning
 * SFN entry. The same state drives files and directories so mkdir_lfn() cannot
 * drift away from fopen_lfn().
 */
```

### `asyncfatfs.c`: Add object scanner

Add a private helper used by both `afatfs_findNextObject()` and
`afatfs_createFileContinue()`:

```c
static afatfsOperationStatus_e afatfs_scanNextObject(
    afatfsFilePtr_t directory,
    afatfsFinder_t *finder,
    afatfsObjectInfo_t *object,
    afatfsObjectScanState_t *scan);
```

Comment text:

```c
/*
 * Advance through raw directory entries until one logical object is available.
 *
 * The loop deliberately postpones name decisions until the owning SFN entry.
 * LFN entries have no cluster, size, or attributes of their own; they are only
 * valid if their sequence numbers and checksum match the immediately following
 * SFN. This helper is the single place that understands VFAT chain assembly so
 * higher layers do not accidentally compare an orphaned fragment as a file.
 */
```

Important loop comment:

```c
/*
 * Deleted entries contribute to free-run tracking but invalidate any pending
 * LFN chain. A valid long-name chain must be physically contiguous and must end
 * at the next non-LFN SFN entry.
 */
```

### `asyncfatfs.c`: Rework free-run allocation

Current `afatfs_allocateDirectoryEntry()` returns one free entry. Add an
allocator that can reserve `N` contiguous entries:

```c
static afatfsOperationStatus_e afatfs_allocateDirectoryEntryRun(
    afatfsFilePtr_t directory,
    uint8_t count,
    afatfsDirEntryPointer_t *first,
    fatDirectoryEntry_t **firstEntry);
```

Comment text:

```c
/*
 * Reserve a contiguous run of directory entries for VFAT object creation.
 *
 * LFN creation needs one slot per long-name fragment plus one final SFN slot.
 * The run must stay contiguous because host FAT readers associate the LFN
 * fragments with the immediately following SFN entry. If the current directory
 * has no suitable run, this helper may extend the directory and resume scanning
 * asynchronously.
 */
```

Important FAT16-root comment:

```c
/*
 * FAT16 root directories have fixed size and cannot be extended. If no
 * contiguous run is available there, LFN creation must fail cleanly instead of
 * falling through to subdirectory extension.
 */
```

### `asyncfatfs.c`: Rework create phases

Replace `AFATFS_CREATEFILE_PHASE_CREATE_NEW_FILE` with separate phases:

- parse/prepare request;
- scan objects;
- allocate entry run;
- write LFN entries;
- write SFN entry;
- initialize newly-created directory;
- existing object truncate/append/seek;
- success;
- cleanup failure.

Comment text for write-LFN phase:

```c
/*
 * Write VFAT fragments before the owning SFN entry.
 *
 * Each fragment stores thirteen UTF-16 code units from the display name and
 * the checksum of the generated short alias. The last logical fragment is
 * written first on disk with FAT_LFN_SEQUENCE_LAST set, matching the VFAT
 * reverse-order convention. The following SFN entry is the authoritative owner
 * for cluster, size, timestamps, and attributes.
 */
```

Comment text for write-SFN phase:

```c
/*
 * Write the owning short entry after all optional LFN fragments.
 *
 * The SFN entry is what FAT uses for cluster allocation and file size. For
 * short-only names, ntReserved preserves lowercase base/extension display. For
 * LFN names, ntReserved still describes the generated alias returned to callers
 * but the LFN chain is the exact display identity.
 */
```

Comment text for directory-initialization phase:

```c
/*
 * Newly-created directories are not valid when only their parent entry exists.
 *
 * Before mkdir_lfn()/mkdir() can call back successfully, the directory must own
 * a first cluster, the parent SFN entry must contain that cluster, the cluster
 * must be zero-filled, and "." / ".." entries must be written. File creation
 * can lazily allocate clusters on first write; directory creation cannot.
 */
```

### `asyncfatfs.c`: Delete full object entry chain

Extend `afatfsFile_t` or add a sidecar field so opened objects remember:

- `lfnFirstEntry`;
- `lfnEntryCount`;
- `hasLongName`;
- exact display name if useful.

`afatfs_funlink()` should mark the LFN run deleted after truncating the cluster
chain and before/with deleting the SFN entry.

Comment text:

```c
/*
 * Delete the whole logical object, not only the owning SFN entry.
 *
 * VFAT readers ignore orphaned LFN chains only by convention; leaving them on
 * disk makes later scans ambiguous and can cause stale display names to attach
 * to the next created short entry. Unlink therefore marks every validated LFN
 * fragment plus the final SFN entry with FAT_DELETED_FILE_MARKER.
 */
```

### `asyncfatfs.c/h`: Add `afatfs_sync()`

Add `afatfs_sync()` or strengthen `afatfs_flush()` so success means:

- no dirty cache entries;
- no sector write in progress;
- no callback still pending from the SD driver.

Comment text:

```c
/*
 * Removable-media persistence boundary.
 *
 * afatfs_sync() returns true only after dirty cache entries have been written
 * and any in-flight sector write has completed. Save test operations use this
 * before reporting success so a user can power off or move the SD card after
 * the two-second result screen without losing the newly-created directory or
 * file entry.
 */
```

## Required Filesystem Facade Changes

The test menus should not call asyncfatfs directly from `menu.c`. Add a small
test-file facade in `Core/Hardware/SD/filesystem.c/h` so the UI stays on the
same single-operation `filesystem_tick()` model.

### `filesystem.h`: Add test API and result accessors

Add:

```c
typedef enum {
    FS_TEST_OBJECT_FILE,
    FS_TEST_OBJECT_DIR,
} fs_test_object_type_t;

typedef struct {
    char display_name[AFATFS_LONG_FILENAME_MAX + 1u];
    uint8_t is_directory;
} fs_test_browser_entry_t;

bool filesystem_requestScanTestObjects(fs_test_object_type_t type,
                                       fs_completion_cb_t cb);
uint8_t filesystem_testObjectCount(void);
const char *filesystem_testObjectName(uint8_t index);
uint8_t filesystem_testObjectIsDirectory(uint8_t index);

bool filesystem_requestLoadTestFile(uint8_t index, fs_completion_cb_t cb);
bool filesystem_requestLoadTestDir(uint8_t index, fs_completion_cb_t cb);
bool filesystem_requestSaveTestFile(const char *display_name,
                                    fs_completion_cb_t cb);
bool filesystem_requestSaveTestDir(const char *display_name,
                                   fs_completion_cb_t cb);

const uint8_t *filesystem_testResultBytes(void);
const char *filesystem_testResultName(void);
uint8_t filesystem_testResultIsDirectory(void);
```

Comment text:

```c
/*
 * Temporary asyncfatfs verification facade.
 *
 * These requests intentionally replace the legacy Load/Save surface while
 * long-name support is being built. They expose only generic file/directory
 * operations needed to prove case-sensitive LFN read/write behavior on real SD
 * cards. The API owns scanning, sorted browser entries, four-byte read/write
 * results, and sync-before-complete so Menu does not include asyncfatfs.h.
 */
```

### `filesystem.c`: Add test operation enum values

Add:

- `FS_INTERNAL_OP_SCAN_TEST_FILES`
- `FS_INTERNAL_OP_SCAN_TEST_DIRS`
- `FS_INTERNAL_OP_LOAD_TEST_FILE`
- `FS_INTERNAL_OP_LOAD_TEST_DIR`
- `FS_INTERNAL_OP_SAVE_TEST_FILE`
- `FS_INTERNAL_OP_SAVE_TEST_DIR`

Comment text:

```c
/*
 * Generic file/dir verification operations.
 *
 * These are deliberately separate from Kit/Pattern/Globals operations. They
 * exercise asyncfatfs object semantics directly: exact-case browsing, opening,
 * directory entry, overwrite, child-file creation, and sync. Once these pass,
 * the musical save formats can be rebuilt on top of the same primitives.
 */
```

### `filesystem.c`: Add test browser cache

Add fixed-size cache, for example:

- `FS_TEST_BROWSER_MAX 64`
- `fs_test_browser_entry_t fs_test_browser[FS_TEST_BROWSER_MAX]`
- `uint8_t fs_test_browser_count`
- `uint8_t fs_test_selected_index`
- `uint8_t fs_test_result_bytes[4]`
- `char fs_test_result_name[AFATFS_LONG_FILENAME_MAX + 1]`
- `uint8_t fs_test_result_is_directory`
- `char fs_test_request_name[AFATFS_LONG_FILENAME_MAX + 1]`

Comment text:

```c
/*
 * Test browser/result cache.
 *
 * The Load:[File]/Load:[Dir] menus need a stable list while the encoder
 * scrolls, and the two-second result screen needs stable bytes/name after the
 * filesystem operation completes. The cache is intentionally small and root
 * scoped; it is diagnostic infrastructure, not the final Kit browser.
 */
```

### `filesystem.c`: Scan test root

Implement scan against the current root directory first. Do not scan
recursively.

Phases:

1. `chdir(NULL)`
2. open `"."` or use current root handle for scanning
3. `afatfs_findFirst`
4. loop `afatfs_findNextObject`
5. filter files or directories
6. insert into cache sorted alphanumerically with exact display case retained
7. finish success

Comment text for sort insertion:

```c
/*
 * Insert browser entries in deterministic alphanumeric order.
 *
 * Load:[Dir] depends on this same ordering when it enters a directory and
 * chooses the first child object. The comparison uses the display name returned
 * by asyncfatfs so exact case is shown to the user; the ordering itself may use
 * ASCII case-sensitive order unless a later UI decision chooses natural
 * case-folded order.
 */
```

### `filesystem.c`: Load test file

Phases:

1. validate selected index is a file;
2. `chdir(NULL)`;
3. `afatfs_fopen_lfn(name, "r", AFATFS_MATCH_CASE_SENSITIVE, ...)`;
4. read up to four bytes;
5. close;
6. zero-fill or mark missing bytes if shorter than four;
7. finish success/error.

Comment text:

```c
/*
 * Case-sensitive file read probe.
 *
 * The selected display name came from afatfs_findNextObject(), so reopening it
 * with AFATFS_MATCH_CASE_SENSITIVE proves that exact-case lookup works in the
 * normal open path, not only during scanning. The four-byte payload is small
 * enough to show directly on the LCD and to verify on a desktop hex viewer.
 */
```

### `filesystem.c`: Load test directory

Phases:

1. validate selected index is a directory;
2. `chdir(NULL)`;
3. `afatfs_mkdir_lfn(name, AFATFS_MATCH_CASE_SENSITIVE, ...)` or an
   `openObject` directory read mode that does not create;
4. `chdir()` into the directory;
5. scan child objects with `afatfs_findNextObject()`;
6. choose first alphanumeric child;
7. if child is directory, copy name to result and set `result_is_directory`;
8. if child is file, open case-sensitively and read first four bytes;
9. close child, return root, close directory handle as needed;
10. finish.

Comment text:

```c
/*
 * Case-sensitive directory entry probe.
 *
 * This operation proves three asyncfatfs behaviors together: opening a
 * directory by exact display name, chdir() into the returned handle, and
 * enumerating child objects with exact-case names. If the first sorted child is
 * a file it also proves child file open/read from inside an LFN directory.
 */
```

### `filesystem.c`: Save test file

Phases:

1. copy request name at request time;
2. generate four bytes from `GetRngValue()`;
3. `chdir(NULL)`;
4. `afatfs_fopen_lfn(name, "w", AFATFS_MATCH_CASE_SENSITIVE, ...)`;
5. write four bytes;
6. close file;
7. call `afatfs_sync()` until true;
8. finish success.

Comment text:

```c
/*
 * Case-sensitive file write/overwrite probe.
 *
 * The filename is copied before the operation starts so later encoder edits
 * cannot retarget an in-flight save. Opening with "w" is expected to create or
 * truncate exactly the requested display name, not a case-different sibling.
 * Completion is delayed until afatfs_sync() proves the bytes and directory
 * metadata are card-visible.
 */
```

Random-byte comment:

```c
/*
 * Four random bytes are both payload and test receipt.
 *
 * They give the user an immediate LCD value to compare against the file on a
 * desktop mount. GetRngValue() returns 16 bits, so two reads supply the four
 * bytes without adding another random source to the filesystem layer.
 */
```

### `filesystem.c`: Save test directory

Phases:

1. copy request directory/file name at request time;
2. generate four bytes;
3. `chdir(NULL)`;
4. `afatfs_mkdir_lfn(name, AFATFS_MATCH_CASE_SENSITIVE, ...)`;
5. `chdir()` into the returned directory;
6. create/overwrite child file with the same display name using
   `afatfs_fopen_lfn(name, "w", AFATFS_MATCH_CASE_SENSITIVE, ...)`;
7. write four bytes;
8. close file;
9. return to root;
10. close directory handle if still owned separately;
11. call `afatfs_sync()` until true;
12. finish success.

Comment text:

```c
/*
 * Directory creation plus same-name child-file write probe.
 *
 * This is the acceptance test for newly-created LFN directories. The directory
 * callback must not fire until the directory has a first cluster and valid "."
 * / ".." entries, because the next phase immediately chdir()s into it and
 * creates a child file. Using the same display name for the child file stresses
 * object-kind separation: a directory and file may share display text only in
 * different parent directories.
 */
```

## Required Preset Layer Changes

The generic test operations can bypass most musical Preset behavior, but the
existing Menu completion path expects `preset_getStatus()` to transition through
`PRESET_LOAD_IN_PROGRESS` and `PRESET_UPDATE_READY`.

Add new completed operations in `presetManager.h/c`:

- `PRESET_OP_TEST_FILE_LOAD`
- `PRESET_OP_TEST_DIR_LOAD`
- `PRESET_OP_TEST_FILE_SAVE`
- `PRESET_OP_TEST_DIR_SAVE`

Add request functions:

```c
uint8_t preset_loadTestFile(uint8_t index);
uint8_t preset_loadTestDir(uint8_t index);
void preset_saveTestFile(const char *display_name);
void preset_saveTestDir(const char *display_name);
```

Comment text:

```c
/*
 * Bridge Menu's temporary File/Dir test entries onto filesystem requests.
 *
 * These requests do not apply musical state. They exist only so Menu can reuse
 * the established async completion gate and storage-busy input lock while
 * filesystem.c exercises asyncfatfs. On completion Menu reads the result bytes
 * or result name directly from filesystem accessors.
 */
```

Alternative: Menu can call `filesystem_request...` directly and maintain its
own busy/completion state. That avoids adding diagnostic operations to Preset,
but it forks the existing async UI pattern. Recommendation: use Preset for
completion routing unless the codebase is about to delete the Preset load/save
bridge entirely.

## Required Menu Changes

### `menu.h`: Replace Load/Save enum entries

Change:

```c
enum loadSaveEnum {
    SAVE_TYPE_KIT = 0,
    SAVE_TYPE_GLO,
    SAVE_TYPE_SAMPLES,
    NUM_SAVE_TYPES
};
```

to:

```c
enum loadSaveEnum {
    SAVE_TYPE_FILE = 0,
    SAVE_TYPE_DIR,
    NUM_SAVE_TYPES
};
```

Comment text:

```c
/*
 * Temporary Load/Save object types for asyncfatfs validation.
 *
 * The legacy musical save entries are intentionally removed from the active UI
 * while the filesystem layer is rebuilt. File and Dir are generic test targets
 * that prove exact-case LFN scan/open/create/write behavior before Kit/Scene
 * save code is reintroduced.
 */
```

### `menu.c`: Add filename edit buffer

`preset_currentName[8]` is too small because Save:[File] and Save:[Dir] must
edit names including extension. Add:

- `MENU_TEST_NAME_MAX` equal to `AFATFS_LONG_FILENAME_MAX` or a smaller LCD
  practical limit;
- `static char menu_testFilename[MENU_TEST_NAME_MAX + 1u];`
- `static uint8_t menu_testNameCursor;`
- `static uint8_t menu_testBrowserIndex[NUM_SAVE_TYPES];`
- `static uint8_t menu_testResultActive;`
- `static uint16_t menu_testResultStart;`

Comment text:

```c
/*
 * Temporary File/Dir test-name editor state.
 *
 * The old preset_currentName field is exactly eight characters and cannot
 * represent filenames with extensions or long directory names. This buffer is
 * copied into filesystem requests at OK time, while menu_testNameCursor owns
 * the LCD-visible edit position. It is diagnostic UI state only; musical preset
 * names must not depend on it.
 */
```

### `menu.c`: Extend save state machine for long filename editing

The current `SAVE_STATE_EDIT_NAME1..SAVE_STATE_EDIT_NAME8` enum only supports
eight positions. Options:

1. Add a separate File/Dir edit substate with `menu_testNameCursor`, keeping the
   existing enum.
2. Replace name states with one `SAVE_STATE_EDIT_NAME` plus cursor.

Recommendation for lowest blast radius: use option 1. When `menu_saveOptions.what`
is `SAVE_TYPE_FILE` or `SAVE_TYPE_DIR`, interpret `SAVE_STATE_EDIT_PRESET_NR`
as browse index on Load and as name edit cursor on Save.

Comment text near encoder handling:

```c
/*
 * File/Dir test save uses a cursor into menu_testFilename, not the old
 * eight-character preset name states.
 *
 * The legacy name states are tied to preset_currentName and will be removed
 * with the stale musical save UI. Keeping the diagnostic filename cursor
 * separate lets the encoder edit extensions and longer names while preserving
 * the existing top-row/type and right-side OK navigation pattern.
 */
```

### `menu.c`: Paint new Load/Save type row

In `menu_repaintLoadSavePage()` map:

- `SAVE_TYPE_FILE` -> `File    `
- `SAVE_TYPE_DIR` -> `Dir     `

Comment text:

```c
/*
 * Paint only the temporary asyncfatfs test types.
 *
 * File and Dir intentionally replace Kit/Settings/Samples in this phase. Their
 * labels describe generic filesystem objects, because the purpose of this UI is
 * to verify exact-case object behavior before any musical file format is put
 * back on top.
 */
```

### `menu.c`: Paint Load:[File] and Load:[Dir] lower row

For Load page:

- display current browser index or omit numeric index if space is better used
  by the name;
- copy first eight characters from `filesystem_testObjectName(index)`;
- exact case preserved;
- show `ok` at columns 14-15;
- arrow at column 13 when `SAVE_STATE_OK`.

Comment text:

```c
/*
 * Load File/Dir displays the asyncfatfs object name exactly as scanned.
 *
 * The lower row intentionally truncates to eight LCD characters without
 * uppercasing. This makes case preservation visible during browsing and keeps
 * the test focused on the name returned by afatfs_findNextObject().
 */
```

### `menu.c`: Paint Save:[File] and Save:[Dir] lower row

For Save page:

- show an eight-character window around `menu_testFilename`;
- support extension characters and long names;
- show `ok` at columns 14-15;
- cursor should underline the active character when editing;
- navigation should allow moving from type -> filename -> ok.

Comment text:

```c
/*
 * Save File/Dir paints a sliding window over the requested display name.
 *
 * Unlike preset_currentName, this buffer may include '.', lowercase letters,
 * and more than eight characters. The LCD shows only the current eight-character
 * window, while OK submits the full NUL-terminated name to filesystem.c.
 */
```

### `menu.c`: Add two-second result screen

Add a small transient result mode in `menu_pollPresetStatus()` or a new helper
called before normal repaint:

- when test op completes, set `menu_testResultActive = 1`;
- copy result bytes/name from filesystem accessors;
- record `menu_testResultStart = time_sysTick`;
- paint result screen immediately;
- while active, ignore load/save repaint and keep screen for 2000 ms;
- after timeout, clear flag, reset storage busy, repaint Load/Save page.

Comment text:

```c
/*
 * Two-second File/Dir test result overlay.
 *
 * The overlay is a receipt for the SD operation, not a modal menu page. It
 * freezes the LCD long enough for the user to compare bytes against the SD card
 * on a desktop, then returns to the same Load/Save test entry. Input remains
 * storage-busy locked while the overlay is active.
 */
```

Hex formatting helper comment:

```c
/*
 * Format four bytes as two fixed-width LCD rows.
 *
 * Each byte is shown as 0xHH followed by spaces, two bytes per row. Fixed
 * placement avoids dynamic string length changes and makes repeated tests easy
 * to compare visually.
 */
```

Directory-result comment:

```c
/*
 * Directory child result path.
 *
 * Load:[Dir] may discover that the first sorted child is itself a directory.
 * In that case there are no bytes to display; showing "Dir: " plus the exact
 * child name proves that directory enumeration preserved case and object kind.
 */
```

### `menu.c`: Encoder/click behavior

For Load:

- type edit increments between File and Dir;
- browse edit increments through `filesystem_testObjectCount()`;
- entering a type triggers `filesystem_requestScanTestObjects()`;
- selecting OK triggers `preset_loadTestFile(index)` or
  `preset_loadTestDir(index)`.

For Save:

- type edit increments between File and Dir;
- filename edit changes current character;
- navigation moves through type, filename cursor positions, and OK;
- selecting OK triggers `preset_saveTestFile(menu_testFilename)` or
  `preset_saveTestDir(menu_testFilename)`.

Comment text:

```c
/*
 * Load/Save File/Dir input map.
 *
 * The stale musical operations are intentionally not reachable. Encoder turns
 * either switch between File and Dir, browse the scanned object cache, edit the
 * requested save name, or move to OK. The OK click posts exactly one filesystem
 * test request through Preset so the existing storage-busy gate blocks further
 * input until completion.
 */
```

## Acceptance Checklist

1. Boot with SD card inserted.
2. `Save:[File]` as `test.bin`, confirm the LCD bytes.
3. Power off immediately after the two-second result clears, mount SD on Mac,
   confirm `test.bin` exists with the same four bytes.
4. `Save:[File]` as `Test.bin`, confirm it creates/overwrites only the exact
   case-sensitive name according to the chosen collision policy.
5. `Load:[File]` browse names and confirm case is displayed exactly.
6. `Load:[File]` open `test.bin` and confirm displayed bytes match the file.
7. `Save:[Dir]` as `Probe.dir`, confirm `Probe.dir/Probe.dir` exists and bytes
   match LCD.
8. `Load:[Dir]` browse `Probe.dir`, select OK, confirm it displays the child
   file bytes.
9. Add a subdirectory that sorts first inside `Probe.dir`, then `Load:[Dir]`
   should show `Dir: <name>` with exact case.
10. Create host-side mixed-case LFN files and directories, rescan, and confirm
    browsing preserves exact case.

## Implementation Order for This Test Phase

1. asyncfatfs Stage 1: SFN case preservation.
2. asyncfatfs Stage 2: LFN object scanner.
3. filesystem facade: scan test files/dirs and expose sorted object caches.
4. menu replacement: show only File/Dir test entries.
5. asyncfatfs Stage 3: LFN file create/open.
6. filesystem facade: Save/Load File tests.
7. asyncfatfs Stage 4: LFN directory create/open.
8. filesystem facade: Save/Load Dir tests.
9. sync completion boundary.
10. repeated hardware tests with immediate power-off after success overlay.

Do not reintroduce Kit Save, Scene Save, Morph Save, or musical Load/Save menu
items until all ten acceptance checks pass.

## Implementation Work Log

### 2026-07-14 Pass 1: asyncfatfs + File/Dir Test Surface

Implemented the first full expansion pass against the reset Scene-era repo.
This pass intentionally prioritizes the generic File/Dir tests over old
musical save/load behavior.

Changed `Core/Hardware/SD/asyncfatfs/fat_standard.h/.c`:

- Added shared VFAT helpers:
  - `fat_isLongDirectoryEntry()`
  - `fat_lfnChecksum()`
  - `fat_lfnCharAllowed()`
  - `fat_lfnSanitizeChar()`
  - `fat_compareDisplayName()`
- Added constants for `FAT_FILE_ATTRIBUTE_LFN`,
  `FAT_LFN_LAST_LONG_ENTRY`, and `FAT_LFN_CHARS_PER_ENTRY`.
- Adjacent source comments explain why these helpers belong at the FAT grammar
  boundary instead of in Kit/Scene/filesystem-local scanners.

Changed `Core/Hardware/SD/asyncfatfs/asyncfatfs.h/.c`:

- Added `afatfsMatchMode_t` with case-sensitive and compatibility matching.
- Expanded `afatfs_fopen_lfn()` and `afatfs_mkdir_lfn()` to accept an explicit
  match mode.
- Added `afatfs_opendir_lfn()` so Load:[Dir] can open existing directories
  without accidentally creating them.
- Added `afatfsObjectFinder_t`, `afatfsObjectInfo_t`,
  `afatfs_findFirstObject()`, `afatfs_findNextObject()`, and
  `afatfs_findLastObject()`.
- Added `afatfs_sync()` as a named persistence boundary over the existing
  strict flush logic.
- Kept the older raw `afatfs_findNext()` API intact for stale callers.
- Existing Kit/Scene LFN save call sites were updated to pass
  `AFATFS_MATCH_CASE_SENSITIVE` so the new API is explicit everywhere.

Important implementation note: object enumeration is now the place that hides
raw LFN fragments, deleted records, volume labels, and dot entries. It returns
the exact display name from a checksum-validated LFN chain, or the case-applied
short alias when no valid LFN exists. This prevents the File/Dir tests from
recreating local VFAT chain scanners.

Changed `Core/Hardware/SD/filesystem.h/.c`:

- Added the generic File/Dir test facade:
  - `filesystem_requestScanTestFiles()`
  - `filesystem_requestScanTestDirs()`
  - `filesystem_testFileCount()`
  - `filesystem_testDirCount()`
  - `filesystem_testFileName()`
  - `filesystem_testDirName()`
  - `filesystem_requestLoadTestFile()`
  - `filesystem_requestLoadTestDir()`
  - `filesystem_requestSaveTestFile()`
  - `filesystem_requestSaveTestDir()`
  - `filesystem_testResultKind()`
  - `filesystem_testResultBytes()`
  - `filesystem_testResultName()`
- Added filesystem internal ops for test scan/load/save file/dir.
- Added root-level exact-case sorted caches for files and directories.
- Added four-byte random save payload generation via `GetRngValue()`.
- Added incremental four-byte read/write loops so test operations remain async.
- Load:[Dir] scans the selected directory, chooses the first exact-case sorted
  child, and either reads bytes from the child file or reports
  `FS_TEST_RESULT_DIRECTORY` with the child directory name.

Important implementation note: request functions validate/copy the test name
before calling `filesystem_start()`, so an invalid empty/slash-only name cannot
leave the filesystem facade stuck busy.

Changed `Core/Scene/Preset/presetManager.h/.c`:

- Added test completion ops:
  - `PRESET_OP_TEST_SCAN`
  - `PRESET_OP_TEST_FILE_LOAD`
  - `PRESET_OP_TEST_DIR_LOAD`
  - `PRESET_OP_TEST_FILE_SAVE`
  - `PRESET_OP_TEST_DIR_SAVE`
- Added wrappers:
  - `preset_scanTestFiles()`
  - `preset_scanTestDirs()`
  - `preset_loadTestFile()`
  - `preset_loadTestDir()`
  - `preset_saveTestFile()`
  - `preset_saveTestDir()`
- These wrappers deliberately do not touch SceneData, DSP, kit buffers, or
  musical state. They only preserve the existing async Preset/Menu completion
  route.

Changed `Core/Menu/menu.h/.c`:

- Added active `SAVE_TYPE_FILE` and `SAVE_TYPE_DIR` before the stale musical
  entries.
- Left stale Kit/Scene/Settings/Samples enum values in place so old code can
  compile, but Menu type cycling is now bounded to File/Dir only.
- Load:[File] scans root files and browses the sorted exact-case cache.
- Load:[Dir] scans root directories and browses the sorted exact-case cache.
- Save:[File] edits the first eight characters of a bounded long-name buffer
  and writes four random bytes to that exact root filename.
- Save:[Dir] edits the same bounded name, creates/opens that exact directory,
  writes four random bytes to a same-name child file, and returns the bytes.
- Test load/save completions show a nonblocking two-second overlay:
  - byte results on both rows;
  - directory-child results as `Dir:` plus the first eight name characters.
- `menu_resetSaveParameters()` now resets stale musical entries to File and
  lands on browser index for Load or first name character for Save.

Known temporary UI constraint: the underlying filesystem/asyncfatfs supports
`FS_TEST_NAME_MAX`/`AFATFS_LONG_FILENAME_MAX` components, but this first menu
editor only exposes the first eight characters. Widening the front-panel editor
can now be done above the filesystem boundary.

### Verification

- `git diff --check` completed cleanly.
- `make` completed and linked `build/lxr02.elf`.
- Remaining warnings after this pass:
  - pre-existing `asyncfatfs.c` unused `eraseCount` parameter;
  - usual nano/newlib syscall linker warnings for `_close`, `_lseek`, `_read`,
    and `_write`;
  - usual LTO serial compilation note.

### Hardware Test Focus

Run the acceptance checklist above before reintroducing Kit/Scene/Morph save
menus. The first hardware tests should especially confirm:

- Save:[File] creates a visible exact-case root file with the displayed bytes.
- Save:[Dir] creates a visible exact-case root directory and same-name child
  file with the displayed bytes.
- Load:[File] and Load:[Dir] preserve mixed case in browsing.
- Load:[Dir] correctly distinguishes first-child file bytes from first-child
  directory labels.

## 2026-07-14 Hardware Result Investigation: SFN Opens and Duplicate Payloads

### Reported Results

Tested firmware against an SD card snapshot now copied into `SD_CARD_EXACT/`.

Observed on hardware:

- `Load:[File]` on `GLO.CFG`: no result overlay; cursor returned to the browser
  slot field.
- `Load:[File]` on `P000.ALL`: no result overlay; cursor returned to the
  browser slot field.
- `Load:[Dir]` on `samples`: no result overlay.
- `Load:[Dir]` on `Scene`: displayed `Dir: 001 Slak` on both rows.
- `Load:[Dir]` on `Instrume` / `Instrument`: no result overlay.
- `Save:[File]` for `0fut.bin`: displayed `0x7c 0x56` on both rows.
- `Save:[Dir]` for `0dirnbin`: displayed `0x25 0x50` on both rows.
- `Load:[Dir]` on `0dirnbin`: displayed the saved `0x25 0x50` pair on both
  rows.
- `Load:[File]` on `0fut.bin`: displayed the saved `0x7c 0x56` pair on both
  rows.

### SD Snapshot Evidence

Root-level `SD_CARD_EXACT/` contains both legacy/host-created short-name files
and firmware-created test objects:

- `GLO.CFG`, 23 bytes, first bytes:
  `86 04 00 00 00 00 00 04 00 03 02 00 ...`
- `P000.ALL`, 50946 bytes, first bytes:
  `46 72 73 74 41 6c 6c 20 02 86 00 00 ...`
- `samples/01_cut.wav`, first bytes:
  `52 49 46 46 ...` (`RIFF`)
- `Instrument/1shtsnc1.cym`, first bytes:
  `66 6f 72 6d 61 74 3d 68 ...` (`format=h...`)
- `Scene/001 Slak/` exists and sorts as the first child of `Scene/`.
- `0fut.bin` exists and contains exactly four bytes:
  `7c 56 7c 56`.
- `0dirnbin/0dirnbin` exists and contains exactly four bytes:
  `25 50 25 50`.

The important split is: internally-created LFN test objects can be reopened,
while existing SFN-only objects cannot be opened through the new LFN API even
though the object scanner can list them.

### Root Cause 1: LFN Open Does Not Fall Back to SFN Display Names

The scanner side is mostly right:

- `afatfs_findNextObject()` reconstructs a checksum-valid LFN display name when
  one exists.
- If no valid LFN chain belongs to an SFN entry, it falls back to
  `fat_convertFATStyleToFilename()` plus `fat_applyFilenameCaseFlags()`.
- That is why Load:[File] can browse `GLO.CFG` and `P000.ALL`, and Load:[Dir]
  can browse `samples` and `Instrument`.

The open side is not aligned with the scanner:

```c
} else if (opState->longNameEnabled) {
    uint8_t displayMatches = 0u;

    if (opState->scanLongNameValid &&
        opState->scanLongNameChecksum ==
            afatfs_lfnChecksum((const uint8_t *)entry->filename) &&
        fat_compareDisplayName(opState->scanLongName,
                               opState->longName,
                               opState->matchMode ==
                                   AFATFS_MATCH_CASE_SENSITIVE) == 0) {
        displayMatches = 1u;
    }
    if (displayMatches) {
        ...
    }
    if (strncmp(entry->filename, (char*) opState->filename,
                FAT_FILENAME_LENGTH) != 0) {
        ...
        break;
    }
    opState->aliasOrdinal++;
    ...
}
```

This logic only treats a checksum-validated LFN chain as a display-name match.
For a plain short-name object like `GLO.CFG`, `P000.ALL`, `samples`, or
`1shtsnc1.cym`, `scanLongNameValid` is false. When the generated alias matches
the SFN entry, the code treats that as an alias collision, increments
`aliasOrdinal`, and eventually fails the read/open. It never asks whether the
case-applied short display name equals the requested display component.

That explains each failed legacy/host-created case:

- `GLO.CFG` and `P000.ALL` are root SFN files. Load:[File] lists them but
  `afatfs_fopen_lfn(..., "r", AFATFS_MATCH_CASE_SENSITIVE, ...)` cannot open
  them.
- `samples` is a root SFN directory. Load:[Dir] cannot enter it.
- `Instrument` may be LFN at the root, but its first sorted child,
  `1shtsnc1.cym`, is an SFN file. Load:[Dir] can select the directory, but
  fails when it tries to open the first child file by display name.
- `Scene` works because the first sorted child is `001 Slak`, a directory. The
  current Load:[Dir] behavior reports a first-child directory name without
  opening it, so it avoids the SFN file-open bug.

### Fix Plan 1: Make LFN Open Match Scanner Semantics

Change `asyncfatfs.c` in the `opState->longNameEnabled` branch of
`afatfs_createFileContinue()`.

Add a small local helper or inline block that derives a normal entry's short
display component:

```c
char shortDisplay[AFATFS_SHORT_FILENAME_MAX];
afatfs_copyShortAliasText((const uint8_t *)entry->filename,
                          entry->ntReserved,
                          shortDisplay);
```

Then set `displayMatches` when either:

1. a checksum-valid LFN chain belongs to this SFN and the LFN display text
   matches under `opState->matchMode`; or
2. no checksum-valid LFN chain belongs to this SFN and the case-applied SFN
   display text matches under `opState->matchMode`.

Comment text to land beside the change:

```c
/*
 * Match LFN requests against the same display component returned by the object
 * iterator.
 *
 * A FAT object may have a checksum-valid VFAT long-name chain, or it may be a
 * plain short-name entry whose display spelling comes from filename[] plus
 * ntReserved case bits. Load:[File] and Load:[Dir] browse both forms through
 * afatfs_findNextObject(), so open/create must resolve the same display string
 * here. Without the SFN-display fallback, existing card files such as GLO.CFG,
 * P000.ALL, samples/, and Instrument/*.drm list correctly but fail when OK is
 * selected because the generated alias is mistaken for an unrelated collision.
 */
```

Important policy detail:

- If a valid LFN chain exists for the SFN entry, prefer the LFN display name and
  do not also treat the short alias as the object's display name for
  case-sensitive File/Dir tests. This keeps object browsing and object opening
  one-to-one.
- If no valid LFN chain exists, use the SFN display name exactly as
  `afatfs_findNextObject()` does.
- Type validation remains unchanged: a file request must not resolve a
  directory, and a directory request must not resolve a file.
- `openNameOut` should still receive the actual SFN alias through
  `afatfs_copyShortAliasText()`.

Also adjust the alias-collision path:

- If the requested display name does not match and the generated alias collides
  with an existing SFN:
  - in create/write modes, continue the current `~N` alias retry behavior;
  - in read-only mode, fail instead of wasting scans on new aliases that cannot
    be created.

Comment text for that branch:

```c
/*
 * Alias collision is only useful when creation is allowed.
 *
 * Read-only LFN opens cannot resolve by inventing a new "~N" alias; if the
 * display component did not match this entry, the colliding SFN proves that the
 * requested object is absent under the current exact-case policy.
 */
```

Acceptance after this fix:

- Load:[File] on `GLO.CFG` should display `0x86 0x04` on the top row and
  `0x00 0x00` on the bottom row.
- Load:[File] on `P000.ALL` should display `0x46 0x72` on the top row and
  `0x73 0x74` on the bottom row.
- Load:[Dir] on `samples` should open `01_cut.wav` and display `0x52 0x49`
  on the top row and `0x46 0x46` on the bottom row.
- Load:[Dir] on `Instrument` should open `1shtsnc1.cym` and display
  `0x66 0x6F` on the top row and `0x72 0x6D` on the bottom row.

### Root Cause 2: Test Payload Reads the Same RNG Halfword Twice

`filesystem_makeTestBytes()` currently does this:

```c
int16_t a = GetRngValue();
int16_t b = GetRngValue();
op_test_bytes[0] = (uint8_t)(a & 0xff);
op_test_bytes[1] = (uint8_t)(((uint16_t)a >> 8) & 0xffu);
op_test_bytes[2] = (uint8_t)(b & 0xff);
op_test_bytes[3] = (uint8_t)(((uint16_t)b >> 8) & 0xffu);
```

`GetRngValue()` directly reads `RNG_DR` and does not wait for a fresh DRDY edge.
The two immediate reads in one foreground pass can return the same 16-bit
register contents. The copied SD files prove this happened:

- `0fut.bin`: `7c 56 7c 56`
- `0dirnbin/0dirnbin`: `25 50 25 50`

So the duplicate overlay is not only an LCD problem; the file payload itself is
duplicated.

### Fix Plan 2: Generate Four Distinct Test Bytes Without Blocking

Do not block the storage state machine waiting on RNG hardware. Instead, make
the test payload generator produce a 32-bit test word from one hardware RNG
sample plus a cheap evolving software mixer.

Proposed `filesystem.c` change:

- Add a static `uint32_t fs_test_payload_counter`.
- In `filesystem_makeTestBytes()`:
  - read `uint16_t rng = (uint16_t)GetRngValue()`;
  - mix it with `time_sysTick`, `fs_test_payload_counter`, and a fixed odd
    constant;
  - run a tiny xorshift32 or LCG step;
  - split the resulting 32-bit value into four bytes.

Comment text to land beside the generator:

```c
/*
 * Build a four-byte diagnostic payload without assuming back-to-back RNG_DR
 * reads are fresh.
 *
 * GetRngValue() reads the hardware RNG data register directly and may return
 * the same 16-bit word when called twice in the same foreground pass. The File
 * and Dir save tests need four visibly independent bytes so the LCD result can
 * prove byte order and file persistence. Seed a tiny software mixer from one
 * hardware sample, the system tick, and an incrementing counter; this is a test
 * payload, not cryptographic randomness.
 */
```

Expected result after this fix:

- Save:[File] and Save:[Dir] should usually show four non-repeating bytes,
  formatted as two bytes on the top row and two bytes on the bottom row.
- The mounted SD files should contain exactly those four bytes in the same
  order.

### Root Cause 3: Test Failures Are Silent

When a filesystem operation fails, `preset_completeFilesystemOp()` records
`PRESET_OP_NONE` instead of the requested operation. Menu then falls through the
default path and simply repaints the browser. That is why the failed legacy file
and directory loads appeared as "nothing shown" rather than a useful error.

### Fix Plan 3: Add a Test Error Overlay

Keep old musical operation error behavior unchanged for now, but add a test
completion path that preserves the test operation identity and exposes success
vs error to Menu.

Possible implementation:

- Add `static volatile uint8_t pm_completed_ok`.
- Add `uint8_t preset_getCompletedOk(void)` in `presetManager.h/.c`.
- In the generic `preset_completeFilesystemOp()`, set `pm_completed_ok` based
  on whether `filesystem_status() == FS_STATUS_DONE`.
- For old operations, Menu can continue using the existing completed-op switch.
- For `PRESET_OP_TEST_*`, Menu should show the byte/Dir overlay only when
  `preset_getCompletedOk()` is nonzero.
- On failure, Menu should show a two-second `ERR` overlay for File/Dir tests.

Comment text for Preset:

```c
/*
 * Preserve completion success for temporary File/Dir diagnostics.
 *
 * The old menu often treats failed load/save as "no completed op", but the
 * asyncfatfs expansion tests must distinguish a real zero-byte result from a
 * failed open/read. This flag lets Menu show an explicit ERR overlay while
 * leaving the existing operation enum and musical completion paths intact.
 */
```

Comment text for Menu:

```c
/*
 * Show File/Dir test failures explicitly.
 *
 * Silent fallback to the browser hides the exact failure mode this temporary
 * storage surface is meant to reveal. A two-second ERR overlay makes failed
 * SFN/LFN opens visible without blocking the main loop.
 */
```

### Root Cause 4: Result Display Text Is Ambiguous

The requirement is now clearer from hardware testing: the result screen should
show all four bytes, not duplicate the same line. The current source already
formats two bytes per row in `menu_showTestResult()`, but the comments and the
previous plan wording still say "byte results on both rows," which reads as
intentional duplication.

### Fix Plan 4: Tighten Display Contract and Comments

Make the display contract explicit:

- top row: bytes 0 and 1;
- bottom row: bytes 2 and 3;
- each byte rendered as `0xNN`;
- no intentional duplication.

Comment text:

```c
/*
 * Display all four diagnostic bytes exactly once.
 *
 * The top row shows bytes 0 and 1; the bottom row shows bytes 2 and 3. This
 * layout fits two 0xNN tokens per 16-cell row and makes byte-order/persistence
 * errors visible. Repeating the top row would hide short reads and duplicated
 * payload generation.
 */
```

### Fix Order

1. Fix asyncfatfs LFN open/create matching so SFN-only display names can open.
2. Add test completion success/error reporting and Menu `ERR` overlay.
3. Fix `filesystem_makeTestBytes()` so it generates four useful bytes.
4. Rebuild and repeat the exact SD-card tests above.

Do not reintroduce Kit/Scene/Morph save work until:

- `GLO.CFG`, `P000.ALL`, `samples`, and `Instrument` all produce visible
  results;
- firmware-created `0fut.bin` and `0dirnbin/0dirnbin` still reopen correctly;
- saved test files contain four bytes, not two repeated bytes;
- failed opens, if any remain, show `ERR` rather than silently returning to the
  browser.

## 2026-07-14 Fix Implementation Notes

Implemented the four fixes from the hardware-result investigation.

### Fix 1: SFN Display Fallback in LFN Opens

Changed `Core/Hardware/SD/asyncfatfs/asyncfatfs.c` inside
`afatfs_createFileContinue()` for `opState->longNameEnabled`.

What changed:

- The matcher now checks a checksum-valid LFN chain first.
- If no valid LFN chain owns the SFN entry, it derives the short display name
  with `afatfs_copyShortAliasText(entry->filename, entry->ntReserved, ...)`.
- It compares that SFN display name against `opState->longName` using the
  requested `AFATFS_MATCH_CASE_*` policy.
- Type validation still happens after a display match, so a file request cannot
  open a directory and a directory request cannot open a file.
- Read-only alias collisions now fail immediately instead of generating `~N`
  aliases that cannot be created.

Why this must exist:

- `afatfs_findNextObject()` lists both LFN-backed objects and SFN-only objects.
  Open/create must resolve the same display spelling that the browser returns.
- Existing SD cards contain many SFN-only objects such as `GLO.CFG`,
  `P000.ALL`, `samples`, and `Instrument/*.drm`.
- Without this fallback, those objects browse correctly but fail on OK.

Adjacent code comments were added in the matcher and alias-collision branch.

### Fix 2: Explicit File/Dir Test Success and ERR Overlay

Changed `Core/Scene/Preset/presetManager.h/.c`:

- Added `preset_getCompletedOk()`.
- Added `pm_completed_ok`.
- `preset_completeFilesystemOp()` now records whether the filesystem finished
  with `FS_STATUS_DONE`.
- Old musical operations keep their historical failure behavior.
- `PRESET_OP_TEST_*` operations preserve their op identity even on failure so
  Menu can display a diagnostic result.

Changed `Core/Menu/menu.c`:

- Added `menu_testResultError`.
- `menu_showTestResult()` now displays centered `ERR` on both rows for failed
  File/Dir test operations.
- `PRESET_OP_TEST_SCAN` also shows `ERR` if the scan itself fails.
- Load/save test completions snapshot byte/Dir results only when
  `preset_getCompletedOk()` is true.

Why this must exist:

- The previous failure path looked identical to a normal browser repaint.
- The File/Dir surface exists to expose asyncfatfs behavior, so failed opens
  must be visible.

### Fix 3: Four Useful Test Bytes

Changed `Core/Hardware/SD/filesystem.c`:

- Added `fs_test_payload_counter`.
- Replaced two immediate `GetRngValue()` reads with one hardware RNG sample
  mixed with `time_sysTick`, the counter, and a fixed odd constant.
- Added a small xorshift32 diffusion step.
- Split the resulting 32-bit diagnostic value into bytes 0..3.

Why this must exist:

- `GetRngValue()` reads `RNG_DR` directly and may return the same register word
  when called twice in the same foreground pass.
- The hardware snapshot proved the old files contained repeated halfwords:
  `7c 56 7c 56` and `25 50 25 50`.

This is still only a diagnostic payload, not cryptographic randomness.

### Fix 4: Display Contract Clarified

Changed comments in `Core/Menu/menu.c` around `menu_showTestResult()`:

- Successful byte results now explicitly mean:
  - top row: bytes 0 and 1;
  - bottom row: bytes 2 and 3;
  - format: `0xNN`.
- Directory-child results still show `Dir:` plus the first eight characters.
- Error results show `ERR`.

### Retest Expectations

Retest with `SD_CARD_EXACT/` copied back to the SD card:

- `Load:[File]` on `GLO.CFG` should display:
  - row 1: `0x86    0x04`
  - row 2: `0x00    0x00`
- `Load:[File]` on `P000.ALL` should display:
  - row 1: `0x46    0x72`
  - row 2: `0x73    0x74`
- `Load:[Dir]` on `samples` should display:
  - row 1: `0x52    0x49`
  - row 2: `0x46    0x46`
- `Load:[Dir]` on `Instrument` should display:
  - row 1: `0x66    0x6F`
  - row 2: `0x72    0x6D`
- `Save:[File]` and `Save:[Dir]` should write four bytes that are not simply
  the same 16-bit pair repeated.
- Any remaining failed test op should show `ERR`.

## 2026-07-14 Hardware Retest: File/Dir Diagnostics

User retest results against an updated `SD_CARD_EXACT/` snapshot:

- `Load:[File] GLO.CFG` displayed `86 04 00 00`, matching the file.
- `Load:[File] P001.ALL` displayed `31 61 6C 6C`, matching the file.
- `Load:[File] P013.PRF` displayed `54 73 70 74`, matching the copied
  `SD_CARD_EXACT/P013.PRF` after correcting the original transcribed byte.
- `Save:[File] 1est.bin` displayed and wrote `31 C5 FC FA`.
- `Save:[Dir] 1dstkbin` displayed and wrote `36 51 81 1D` to
  `1dstkbin/1dstkbin`.
- `Load:[Dir] Kit old` was reported as `00 00 00 01`. The copied snapshot
  contains `SD_CARD_EXACT/Kit/.DS_Store` with exactly those first four bytes.
  This is correct filesystem behavior: ordinary names beginning with `.` are
  valid FAT objects and must not be hidden by asyncfatfs or by the diagnostic
  menus. The directory-load test should open whichever concrete child sorts
  first by display name, including dot-prefixed host files.
- `Load:[Dir] samples` was reported as `00 05 16 07`, but the copied
  `samples/01_cut.wav` starts `52 49 46 46`, and no visible copied file in the
  snapshot starts `00 05 16 07`. This remains the only unexplained retest item.

Correction after policy review:

- Do not add a dotfile visibility filter. The filesystem and these diagnostics
  must remain exact: dot-prefixed files/directories are ordinary names and must
  be listed, sorted, opened, overwritten, and read when selected.
- Comments in `Core/Hardware/SD/filesystem.c` and
  `Core/Hardware/SD/filesystem.h` now state that only structural FAT records are
  skipped. Ordinary names beginning with `.` remain eligible.

Remaining verification target:

- Rebuild and retest `Load:[Dir] samples` if needed. If `01_cut.wav` remains
  the first sorted concrete child on the actual card, it should display
  `52 49 46 46`; if another hidden/dot-prefixed concrete file sorts first on
  the actual FAT volume, the diagnostic should correctly read that file instead.
