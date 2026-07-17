# Bank Save Failure Trace: Duplicate Folders and State Machine Freezes

This document details the exact sequence of events that causes the save menu to freeze and produces corrupted (`_bad`) folders when overwriting a Bank slot (e.g., `000 Slak`) that contains multiple Scene children.

## Background Context
In the `16-Scene` expansion, a Bank directory (e.g., `Bank/000 Slak/`) contains up to 16 Bank-local child Scene directories named with a 2-digit prefix (e.g., `01 Slak2  `).

When `filesystem_saveBankDirectory_tick()` runs an overwrite save, it iterates through the active scenes. For each scene, it must delete the existing child directory before writing the replacement to prevent stale instrument or kit files from surviving.

## Failure Mechanism 1: The Duplicate Directory Regression

### 1. Cleanup Matcher Failure
During phase 19 of the Bank save loop, the system delegates to `filesystem_deleteBankSceneSlotDirectoriesStart()` to clean up the existing scene. This uses the `filesystem_deleteKitSlotDirectories_tick()` state machine.

In the `FS_DELETE_SLOT_SCAN_NEXT` phase, the code calls `filesystem_directoryObjectMatchesSlot()` to see if the scanned FAT object matches the target slot.
However, if this matching uses `storage_parseNumberedFolder()` instead of `storage_parseBankSceneFolder()`, it expects a 3-digit folder (like `"000 Slak"`). Since Bank-local scene folders use a 2-digit format (`"00 Slak  "`), the matcher silently fails to identify the existing scene folder. The old directory is **left on the disk**.

### 2. FAT32 LFN Duplication
Bank save proceeds to phase 20 (scene save phase 8) and calls `afatfs_mkdir_lfn("01 Slak2  ")`.
Because the old directory was not deleted, the FAT32 file system creates a **second** physical directory entry with the exact same Long File Name (LFN) of `"01 Slak2  "`. 
FAT32 resolves the conflict silently by generating a different 8.3 short alias (e.g., `01SLAK~2` instead of the original `01SLAK~1`).

The SD card now contains duplicate visible folders with the same display name.

## Failure Mechanism 2: The Deletion Freeze

### 3. LFN Ambiguity During Recursion
On a subsequent overwrite attempt, the recursive deletion state machine (`filesystem_deleteTree_tick`) is eventually tasked with removing the directory tree. 
It opens the target directory using its LFN (`"01 Slak2  "`). Because there are duplicate directories, `afatfs_opendir_lfn()` resolves to the *first* one it finds (e.g., `01SLAK~1`).

The state machine recursively enters `~1`, deletes all its files and subdirectories, and climbs back up to the parent directory (`Bank/000 Slak/`).

### 4. The Infinite Loop
Back in the parent directory, the state machine attempts to delete the newly emptied directory by calling:
```c
afatfs_removeObjects_lfn("01 Slak2  ", ...)
```
Once again, the FAT driver resolves the LFN. However, it may resolve it to the **other** duplicate directory (`01SLAK~2`), which is NOT empty (it contains the files from the other duplicate). 

Because `01SLAK~2` is not empty, `afatfs_removeObjects_lfn()` fails. The state machine expects the empty directory removal to succeed, and this failure causes the `deleteTree` state machine to enter a continuous loop or fail abruptly, **freezing the save menu**.

## Failure Mechanism 3: Corrupted FAT Entries (`_bad` folders)
When the file system operations freeze mid-transaction, or when duplicate LFNs are manipulated simultaneously by a recursive deleter that targets the wrong physical entry, the directory tree becomes cross-linked or orphaned. 

When the SD card is read by an OS (or analyzed during testing), these broken FAT entries manifest as a `_bad` directory, representing a severely corrupted filesystem state that cannot be safely loaded.

## Required Fixes (As Documented in Implementation Notes)

To resolve these freezes, the filesystem must strictly bypass LFN ambiguity when operating on concrete slots:
1. **Explicit Child Parsing:** `filesystem_directoryObjectMatchesSlot()` must branch to use `storage_parseBankSceneFolder()` for Bank-local children, allowing the 2-digit matcher to find and delete the old folder before it can duplicate.
2. **Exact-Alias Deletion:** The recursive deleter (`filesystem_deleteTree_tick`) must copy the exact 8.3 open alias (`shortName`) from `afatfsObjectInfo_t` when it scans the folder. When it returns to the parent to remove the empty directory, it must use a new `afatfs_removeObject()` function that targets the exact 8.3 alias instead of resolving by LFN.
3. **Exact-Alias Reopening:** When Bank Save phase 16 re-enters the Bank folder after a child save completes, it must use `afatfs_opendir()` with the 8.3 alias instead of `afatfs_opendir_lfn()`, preventing `ERR BnkS11`.
