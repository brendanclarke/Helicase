# asyncfatfs Missing Operations Post-Mortem

Date: 2026-07-17

## Why This Exists

The 16-Scene Bank work exposed that the current `asyncfatfs` interface is too low-level for the product storage model now being asked of it. The firmware wants to treat Kits, Scenes, Instruments, and Banks as named object trees. The filesystem layer mostly offers single-component open/create calls, short-alias reopen handles, low-level iterators, and a few recently-added LFN helpers. That gap forced product code to build tree replacement, duplicate cleanup, and publish/rollback behavior out of many fragile foreground state-machine phases.

That is the wrong boundary. Bank Save/Load should not have to know whether a child was opened through a VFAT display name, an 8.3 short alias, a cached alias, or a synthetic temp name. It should be able to say: replace this directory tree with this complete new tree, then publish it atomically enough for the firmware and card reader to agree.

## Current Failure Symptoms

- `SD_CARD/Bank/000 SlakBad3/` contained stale Instrument files beside newly saved Instrument files. This showed that Bank overwrite was merging into an existing tree instead of replacing it.
- `SD_CARD/Bank/000 SlakBad4/` contained two embedded Kit directories in one Bank child Scene: the intended loaded `Kit Forest` plus the previous `Kit Slak`. This showed that even temp-based publishing could reopen a stale temp directory because `mkdir_lfn()` is create-or-open.
- `SD_CARD/Bank/000 Slak?/` is visible on the host filesystem, but the product path does not reliably load names containing `?`. The current firmware LFN allowed-character set does not include `?`, so asyncfatfs sanitizes it to `_` for firmware-created names. The UI/user expectation and filesystem policy are not aligned.
- `SD_CARD/Bank/000 Slok/` is structurally plausible on disk but loading hit `ERR BnkL14`. Bank Load phase 14 is in the path that reopens a selected Bank child directory. That points again at path-component identity: the object found during scan is not reliably the same object later opened for payload load.
- On boot after a failed or partial Bank load, instrument parameters can be incorrect because higher-level code may partially mutate resident state, cache state, or Bank metadata before the full object tree has been proven and committed.

## Root Causes

### 1. No First-Class Object Identity

`afatfs_findNextObject()` returns useful data: `displayName`, `shortName`, and physical directory entry pointers. But most public operations still take a component string in the current directory. Product code therefore has to choose between:

- visible LFN display names, which are what the user sees;
- printable 8.3 aliases, which are what older open paths accept;
- cached aliases from previous scans, which can become stale after rename/delete/create;
- physical entry pointers, which are exposed but not accepted by most mutating APIs.

That is the source of many failures. We scan one object, later reopen by a string, and hope the same physical object is selected again. With duplicate visible names, stale aliases, case variants, host-created names, or sanitized names, that hope is not strong enough.

Missing capability: an opaque `afatfsObjectId`/handle that can be passed to open, delete, rename, stat, and copy/move operations without re-resolving by display text.

### 2. Create-Or-Open Is Unsafe For Replacement Writes

`afatfs_mkdir_lfn()` creates a directory if absent, but opens it if present. That is fine for “ensure this folder exists”; it is dangerous for “create a brand-new staging tree.” The temp Bank publish approach failed because a stale `tmp...` folder could be reopened and merged into.

Missing capability: separate create modes:

- `CREATE_NEW`: fail if the component already exists.
- `OPEN_EXISTING`: fail if absent.
- `CREATE_OR_OPEN`: current behavior, but only when explicitly requested.
- `REPLACE_FILE`: remove/truncate exactly one file object and create a clean replacement.

### 3. No Recursive Directory Delete Owned By asyncfatfs

Directory deletion is currently split between filesystem.c and asyncfatfs. The high-level code scans, opens children, descends, removes files, then asks asyncfatfs to remove an empty directory. This requires many state phases, stack limits, short-name side channels, and careful current-directory restoration. It also repeatedly re-resolves names.

Missing capability: `afatfs_deleteTree(object_or_path, flags, callback)` implemented inside asyncfatfs, using physical object identity once the root is resolved. It should delete all children, free cluster chains, retire the whole VFAT LFN/SFN entry runs, and report a precise failure code.

### 4. No Atomic Directory Replace / Transactional Publish

Bank Save wants this operation:

1. Write a complete replacement tree.
2. Verify all files closed/flushed.
3. Move old tree out of the live namespace.
4. Move replacement tree into the live namespace.
5. Leave the card in a recoverable state after power loss at any step.

Today that is hand-coded in filesystem.c using temp names, old names, rename callbacks, and cache updates. It is still vulnerable because name selection and collision checking are separate from create.

Missing capability: `afatfs_replaceTree(target, builder/temp, flags, callback)` or lower-level primitives that make it reliable:

- create unique temp directory with guaranteed non-collision;
- rename/move by object identity;
- optional backup/displaced-object handling;
- cleanup/resume policy for interrupted temp/old folders;
- final sync boundary before success.

### 5. No Copy Or Move Tree Operations

Feature work now needs Bank, Scene, Kit, and Instrument data to move between library folders and Bank-local folders. Without copy/move operations, product code must parse a Scene into runtime structs and reserialize it, even when the user intent is “copy this saved object tree.”

That is wrong for file fidelity. It can rewrite names, reorder children, drop unknown future files, or expose bugs in parsers that should not be involved in a simple filesystem copy.

Missing capability:

- `afatfs_copyFile(source_object, dest_parent, dest_name, flags, callback)`
- `afatfs_copyTree(source_object, dest_parent, dest_name, flags, callback)`
- `afatfs_moveObject(source_object, dest_parent, dest_name, flags, callback)`

These should preserve file bytes and directory structure unless the caller explicitly asks to transform content.

### 6. LFN Character Policy Is Not A Single Contract

`fat_lfnCharAllowed()` currently excludes `?`. `fat_lfnSanitizeChar()` converts unsupported characters to `_`. Host-created folders can contain characters the firmware writer will never create. The menu/user-facing character set can drift from asyncfatfs' accepted write set.

That creates ambiguous behavior:

- A host folder named `000 Slak?` may scan as `000 Slak?`.
- A firmware-created attempt to write the same text may sanitize to `000 Slak_`.
- A later open by display text may compare sanitized text against unsanitized scanned text or vice versa depending on which path performed the conversion.

Missing capability:

- one exported name-policy module used by menu, storage formatting, scanning, create, rename, and compare;
- an explicit result when a requested name cannot be represented, instead of silent replacement;
- tests for every allowed UI character through create, scan, open, rename, delete, and host round-trip.

### 7. Incomplete Error Reporting

Errors such as `BnkL14` identify a product-level state-machine phase, not the filesystem reason. Was the child missing? Did display-name comparison fail? Did the short alias collide? Was the directory entry non-sector-local? Was the FAT chain invalid? Did currentDirectory point somewhere unexpected?

Missing capability: asyncfatfs should expose structured operation results:

- operation kind;
- resolved source display name and short alias;
- failure reason enum;
- whether source was absent, target existed, target collision, invalid name, unsupported LFN run, directory not empty, cache/FAT IO failure, or path depth overflow;
- optional physical entry pointer for diagnostics.

Product code should not need to encode this as `BnkL14`.

### 8. Current Directory Is Global Mutable State

Most operations act in `afatfs.currentDirectory`. Higher-level state machines constantly `chdir()` into root, Bank, child Scene, embedded Kit, then climb back. If any async phase fails or if a helper assumes a different current directory, the next operation may run in the wrong parent.

Missing capability:

- path/object operations that take an explicit parent directory handle or object id;
- directory handles that can be opened and used without changing global current directory;
- clear ownership rules for handles during async scans and mutations.

## What asyncfatfs Should Support

### Object Model

Add a stable object descriptor:

```c
typedef struct {
    afatfsObjectKind_t kind;
    char displayName[AFATFS_LONG_FILENAME_MAX + 1u];
    char shortName[AFATFS_SHORT_FILENAME_MAX];
    afatfsDirEntryPointer_t lfnFirstEntry;
    uint8_t lfnEntryCount;
    afatfsDirEntryPointer_t sfnEntry;
    uint32_t firstCluster;
    uint32_t logicalSize;
    uint8_t attrib;
} afatfsObjectId_t;
```

Then allow all mutating operations to accept this identity directly. String lookup should be only one way to acquire an object id, not the identity itself.

### Path / Parent API

Support component paths or explicit parent handles:

- open child by display component under parent handle;
- create child under parent handle;
- scan parent handle;
- rename/move object to parent handle;
- delete object by object id.

This removes almost all `chdir()` choreography from product code.

### Create Modes

Every create API should choose an explicit policy:

- fail if exists;
- open if exists;
- replace existing file;
- replace existing empty directory;
- replace recursive directory tree;
- generate unique temp name under parent.

`mkdir_lfn()` should not be the primitive used for staging replacement trees unless it can be told `CREATE_NEW`.

### Recursive Tree Delete

Implement this in asyncfatfs:

```c
bool afatfs_deleteTree(const afatfsObjectId_t *root,
                       afatfsDeleteFlags_t flags,
                       afatfsResultCallback_t cb);
```

It should own traversal, cluster freeing, LFN/SFN retirement, dot-entry handling, and restart-after-mutation behavior. It should not depend on product code reopening children by short aliases.

### Tree Copy / Move

Implement file-preserving copy and move:

```c
bool afatfs_copyObjectTree(const afatfsObjectId_t *src,
                           afatfsDirHandle_t *dst_parent,
                           const char *dst_display_name,
                           afatfsCopyFlags_t flags,
                           afatfsResultCallback_t cb);

bool afatfs_moveObject(const afatfsObjectId_t *src,
                       afatfsDirHandle_t *dst_parent,
                       const char *dst_display_name,
                       afatfsMoveFlags_t flags,
                       afatfsResultCallback_t cb);
```

This would let Scene/Bank copy operations preserve exact bytes instead of load/parse/reserialize.

### Transactional Replace

For product saves, provide a transaction helper:

```c
bool afatfs_beginTreeReplace(parent, target_name, options, tx_out);
bool afatfs_txCreateFile(tx, path, bytes...);
bool afatfs_txCreateDir(tx, path);
bool afatfs_commitTreeReplace(tx, callback);
bool afatfs_abortTreeReplace(tx, callback);
```

The implementation can still be FAT-realistic rather than truly atomic, but it should own:

- guaranteed-unique temp naming;
- collision detection;
- old tree displacement;
- temp promotion;
- flush/sync before success;
- recovery/cleanup semantics for interrupted transactions.

### Name Policy

Make a single module authoritative for:

- UI allowed characters;
- firmware-created LFN allowed characters;
- host-created scan substitution;
- compare/case-fold policy;
- sanitize vs reject behavior;
- trailing-space/trailing-period policy.

If `?` is supported in the menu, asyncfatfs must support it or the menu must reject it. Silent conversion is not acceptable for product object names.

### Diagnostics

Every async operation should return a structured result:

```c
typedef enum {
    AFATFS_RESULT_OK,
    AFATFS_RESULT_NOT_FOUND,
    AFATFS_RESULT_ALREADY_EXISTS,
    AFATFS_RESULT_INVALID_NAME,
    AFATFS_RESULT_UNSUPPORTED_NAME,
    AFATFS_RESULT_NOT_EMPTY,
    AFATFS_RESULT_NOT_DIRECTORY,
    AFATFS_RESULT_NOT_FILE,
    AFATFS_RESULT_IO_ERROR,
    AFATFS_RESULT_CORRUPT_LFN_RUN,
    AFATFS_RESULT_UNSUPPORTED_LAYOUT,
} afatfsResultCode_t;
```

Product code should be able to report `Bank child 01 open failed: not found after scan` rather than `ERR BnkL14`.

## Minimum Test Matrix Before Resuming Bank Work

1. Create, scan, open, rename, delete directories with every allowed UI character.
2. Specifically test `?`; either support it end-to-end or reject it at the UI/name-policy layer.
3. Create duplicate case variants on host, then ensure firmware delete/replace produces one object.
4. Create stale `tmp...` and `old...` folders, then ensure tree replace never opens them accidentally.
5. Save a Bank with Scene children `00`, `01`, `02`; verify load sees all children.
6. Copy a Scene tree into a Bank child without parsing it; byte-compare source/destination files.
7. Replace a Bank tree where the old tree contains extra unknown folders/files; verify they do not survive.
8. Power-loss simulation after temp create, after old displacement, and after temp promotion.
9. Corrupt or orphan LFN fragment tests; scanner should not return misleading display names.
10. Error-path tests that assert structured asyncfatfs result codes, not only product phase numbers.

## Practical Recommendation

Stop adding Bank/Scene save behavior on top of the current component-string API. The next filesystem step should be an asyncfatfs rework centered on object identity, create modes, recursive tree operations, tree copy/move, and transactional directory replacement. Once those exist, Bank Save should shrink to product intent:

1. Build or copy the desired Bank tree into a transaction.
2. Commit replace for `Bank/NNN Name`.
3. Update runtime Bank metadata only after commit succeeds.

That boundary is what was missing. Without it, every product save feature becomes another attempt to manually reconstruct filesystem semantics from short aliases, display names, and global `chdir()` state.
