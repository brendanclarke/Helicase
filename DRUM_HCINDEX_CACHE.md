# Instrument `.hcindex` & General Purpose Cache Work Report

This document summarizes the architectural changes and bug fixes implemented during the transition from a flat `/Instrument/` directory to type-specific subdirectories backed by a boot index cache.

## 1. Architectural Transition: Instrument Subdirectories
To improve organization and loading speeds, the instrument filesystem was refactored:
- **Subdirectories**: Instruments are no longer grouped entirely into `/Instrument/`. They now reside in `/Instrument/Drum/`, `/Instrument/Snare/`, `/Instrument/Cymbal/`, and `/Instrument/HiHat/`.
- **Boot Index (`.hcindex`)**: A new mechanism generates an `.hcindex` file at boot time (and after saves), caching the 8-character display names.
- **Generalized cache**: One shared cache (`fs_list_cache_name[index]`) is reused by Instrument, Kit, root Scene, and root Bank. Instrument uses alphabetized rows up to the full cache; numbered libraries use slot-addressed 000..999 rows. FAT short aliases and longer source stems are operation-local only.
- **Menu Loading**: Nested Instrument Load and Save dispose the shared cache on exit and type changes, then use the selected type's asynchronous `.hcindex` request on entry/type selection. The UI remains locked until that cache is ready.
- **Kit/Scene/Bank Loading**: Top-level Kit, KitMrp, root Scene, and root Bank Load/Save dispose the same cache on entry/type changes, then reload `/Kit/.hcindex`, `/Scene/.hcindex`, or `/Bank/.hcindex`. Those indexes preserve blank rows and slot order; the displayed `NNN Name` key is reconstructed from the row number plus the cached name. Bank-local Scenes remain outside the root Bank index.

## 2. Bug Fix: In-Memory Cache Desynchronization
### The Issue
Following the architectural transition, newly saved Drum instruments were not loadable until a full system reboot. They appeared as blank lines (`"Empty   "`) in the load menu.

### Root Cause
1. When a user saved a Drum instrument, the `filesystem_saveInstrument_tick` state machine correctly:
   - Wrote the new `.drm` file to `/Instrument/Drum/`
   - Re-generated the `.hcindex` on the SD card
2. However, the in-memory function `filesystem_updateInstrumentCacheAfterSave` was **only updating the legacy RAM cache arrays** (`instrument_file_name`, `instrument_file_count`, etc.) and was bypassing the new general-purpose cache (`fs_list_cache_name`).
3. The Menu UI read the overall count from the updated legacy cache, permitting the user to scroll to the newly saved slot. But when retrieving the display string, it read from `fs_list_cache_name`, which was stale and uninitialized (resulting in blank text).
4. Attempting to load this blank string resulted in a malformed path that `afatfs_fopen_lfn` rejected.

### The Fix
Patched `filesystem_recordInstrumentFile` and `filesystem_updateInstrumentCacheAfterSave` in `filesystem.c`. Now, whenever a Drum instrument (`INSTRUMENT_TYPE_DRM`) is saved or removed:
- `fs_list_cache_name` is kept perfectly in sync with `instrument_file_name` memory operations (like `memcpy` shifts during insertion).
- `fs_list_cache_count` is updated directly in RAM.

This allows the UI to instantly display the newly saved Drum instrument without rebooting and generates the exact proper long filename (e.g. `Kick 01.drm`) for `afatfs_fopen_lfn` to open successfully.

## 3. Load Drum Investigation (2026-07-19)
### Confirmed failure path
The former Drum-only `menu_requestDrumIndexLoad()` path loaded Drum names into
`fs_list_cache_name` and `filesystem_instrumentCount(INSTRUMENT_TYPE_DRM)`
correctly reported `fs_list_cache_count`. However, at that point
`filesystem_requestLoadInstrument()` still checked
`browser_index` against `instrument_file_count[type]`. The index request clears
that legacy Drum count before reading `.hcindex`, so every Drum load request is
rejected before `filesystem_loadInstrument_tick()` can open the selected file.

The loader's successful-parse metadata copy also still read
`instrument_file_name[]` and `instrument_file_stem[]`, which are not populated by
the index-only path. The fix must use the general Drum cache for validation,
filename/display metadata, and retained source-stem metadata. This was the
transitional Drum-only state; section 4 records the completed all-type
conversion.

### Fix implemented
Added one cache-selection boundary in `filesystem.c`. Drum requests validated
against `fs_list_cache_count`; Drum file opens, staged display names, and
retained source stems used `fs_list_cache_name`. The cache count was also
cleared when a new index read or a full Instrument directory scan started.

### Verification
`make -j2` completes successfully. The resulting firmware compiles and links;
the remaining warnings are existing asyncfatfs/unused-function warnings. The
previous rejection condition is no longer possible for a valid Drum index entry:
the menu count and the filesystem request validator now read the same cache.

## 4. Generalized registry/index/cache implementation (2026-07-19)

The Drum-only rollout was completed for every registered Instrument type.

### Registry-owned storage folders

`instrument_registry_entry_t` now stores `storage_directory` immediately beside
the type's file `extension`. `InstrumentManager.c` defines the four current
rows as `Drum`, `Snare`, `Cymbal`, and `HiHat`, and
`instrumentManager_storageDirectory()` is the only cross-module accessor.
Filesystem code no longer carries a type-to-folder switch. Adding a new
instrument therefore requires one registry row, and the same metadata drives
scan, index creation, index loading, root Instrument Load, and Instrument Save.

### Boot and save index lifecycle

`filesystem_createBootIndexBlocking()` now runs the foreground-pumped index
writer once for every registry row. It enters `/Instrument/`, creates or opens
the registry-owned subdirectory, truncates that directory's `.hcindex`, and
writes the current typed cache as newline-separated display names. A completed
Instrument Save selects only its saved type and runs the same writer, so other
type indexes are not needlessly rewritten.

### Per-type index loading

`filesystem_requestLoadInstrumentIndex(type, callback)` replaces the old
Drum/boot-marker API. It loads `/Instrument/<registry directory>/.hcindex`
asynchronously and replaces only that type's general cache. Both nested Load
and Save call it on entry, while type navigation requests the newly selected
type. The VOICE-button and Pot-1 nested-entry paths share this request, so
neither entry route can expose a stale list. The index reader accepts both the
current display-only lines and the older `display,filename` form by consuming
the display field before the comma.

### Verification notes

`make -j2` completes successfully after the generalization. The remaining
compiler/linker messages are the existing asyncfatfs unused-function and
newlib syscall-stub warnings. `git diff --check` is clean. The generated
`build/LXRV2_lxr02.img` is a build artifact and should be restored before
handoff if the repository tracks it.

## 5. Legacy SRAM cache removal (2026-07-19)

The former `instrument_file_name`, `instrument_file_count`,
`instrument_file_open_name`, and `instrument_file_stem` arrays have been
removed. Full scans, `.hcindex` loads, post-save refresh, browser accessors,
and Instrument Load validation now use only one shared 1,000-entry cache. Boot
refresh scans and writes one type at a time, disposing the cache between types;
menu entry/type changes reload the selected `.hcindex`, and menu exit disposes
it. This recovers approximately 20 KB of permanent SRAM while preserving the
existing display-name-based filename construction and staged long-stem metadata
path.
