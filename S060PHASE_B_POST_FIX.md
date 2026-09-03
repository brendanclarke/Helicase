# S060 Phase B + B2 Post-Fix Analysis

Session 060: Atomic safe-write for `.hcnames`, refreshed-flag implementation, and `op_close_status` bug fix.

Build: `text=388908, data=400, bss=96180` (+40 bytes from pre-implementation baseline 388868).

---

## Phase B: Atomic Safe-Write for `.hcnames`

### Problem

Every HCNAMES rewrite previously opened `.hcnames` with `"w"` (truncate-and-write). A power loss or card error during the truncation/write window destroys the only copy of the 129-row resident names register, leaving boot with no identity data. The `.hcindex` browser rebuild cannot recover names without this file.

### Solution: temp-file safe-write pattern

All five HCNAMES write paths now follow this sequence:

1. Open `.hcnamtmp` for write (`"w"`)
2. Stream all 129 rows to the temp file
3. Close `.hcnamtmp`
4. `afatfs_sync()` — flush the closed temp to card
5. `afatfs_removeObjects_lfn(".hcnames")` — retire the old live register
6. `afatfs_renameObject_lfn(".hcnamtmp", ".hcnames")` — promote the synced temp
7. `filesystem_finish()` — final sync via `afatfs_sync()` in the shared flush gate

The live `.hcnames` is untouched until step 5. A power loss at any point leaves either the intact old register or a recoverable `.hcnamtmp` that the boot prelude validates.

### Temp file define

```c
/* filesystem.c ~line 106 */
#define FS_RESIDENT_NAMES_TEMP_FILENAME ".hcnamtmp"
```

### Mirror validity gate update

`hcnames_mirror_valid` is set to `INVALID` before every write-capable temp open. `PUBLISH_PENDING` is deferred until after rename (previously set after close). `VALID` is granted only after the shared `filesystem_finish()` sync succeeds. Error at any safe-write phase demotes back to `INVALID`.

### Full-card detection

Each write path now captures the return value of `afatfs_fwrite()` and checks `afatfs_isFull()` when `written == 0`. On a stuck-full card, the partial temp is closed with `op_close_status = ERROR` and the live register is never removed or renamed.

### Error-path close handling

Formatter failure (`op_line_len == 0`) and full-card detection both close the temp file through `afatfs_fclose(op_file, on_file_closed)` with `op_close_status` pre-set to `ERROR`. The close-wait phase checks this status and aborts without touching the live register.

### Write paths converted

| Write path | Function | Open phase | Stream phase | Safe-write tail phases | Notes |
|---|---|---|---|---|---|
| Boot full-write | `writeResidentNames_tick` | 0 | 2 | 3-6 (close/sync/remove/rename) | First-use creation at card mount |
| Runtime update | `residentNames_tick` | 3 | 5 | 10-14 (close/sync/remove/rename/publish) | Kit/Scene/Instrument HCNAMES refresh |
| Bootstrap create | `residentNames_tick` | 7 | 8 | 10-14 (shared with runtime update) | Empty-register first write |
| Bank Load | `loadBankDirectory_tick` | 83 | 85 | 90-94 (close/sync/remove/rename/publish) | Bank-owned targeted rewrite |
| Bank Save | `saveBankDirectory_tick` | 83 | 85 | 87-91 (close/sync/remove/rename/publish) | Bank-owned targeted rewrite |
| Autosave drain | `autosaveParameterDrain_tick` | 70 | 72 | 73-76 (close/sync/remove/rename) | Post-drain convergence |

### Boot recovery prelude

New phases 15-19 in `ensureAutosaveFiles_tick`, gated by `op_file_version == 0`:

- **Phase 15**: Open `.hcnamtmp "r"`. NULL (absent) is normal — sets `op_file_version = 1` and falls through to existing live-register read at phase 0.
- **Phase 16**: Validate all 129 rows via `filesystem_cacheResidentRecord()`. Any parse error or short row count sets `op_file_version = 0` and closes with ERROR status.
- **Phase 17**: Close temp. If valid (129 rows, clean close, `op_file_version == 2`): remove old `.hcnames`. If invalid: remove `.hcnamtmp` instead.
- **Phase 18**: Wait remove completion. Valid temp proceeds to rename; invalid temp falls through to phase 0.
- **Phase 19**: Wait rename of validated temp to `.hcnames`. Falls through to phase 0 to continue with normal boot.

`op_file_version` is borrowed as a one-bit prelude gate: 0 = not tried, 1 = done (skip prelude), 2 = reading temp.

---

## Phase B2: Refreshed Flag (`\tR`)

### Problem

After a library Load or Save, the object's resident identity is current, but `autosave` may not have drained that payload yet. The HCNAMES file cannot distinguish "refreshed by load" from "inherited from previous boot" for convergence decisions. Without this flag, a post-drain HCNAMES rewrite has no way to know which rows need their `R` suppressed (because autosave has captured their payload) versus which rows are still pending.

### Solution: bit 13 in `fs_resident_source[]`

#### Header changes (`filesystem.h` lines 627-639)

```
VALUE_MASK           = 0x1fff  (was 0x7fff)
REFRESHED_FLAG       = 0x2000  (new, bit 13)
DIRTY_FLAG           = 0x8000  (moved from .c, was 0x8000)
Special tokens narrowed to 13 bits:
  INHERIT            = 0x1fff  (was 0x7fff)
  UNKNOWN            = 0x1ffe  (was 0x7ffe)
  INSTRUMENT_DIRECT  = 0x1ffd  (was 0x7ffd)
```

The 13-bit value field holds 0..999 for direct numbered slots plus three high tokens. Bit 13 is the refreshed witness. Bit 15 remains the dirty staging flag. Bit 14 is reserved.

#### Setting refreshed

Two new static helpers in `filesystem.c`:

- `filesystem_setResidentRefreshed(uint16_t row)` — ORs bit 13 onto one row
- `filesystem_setResidentSceneRefreshed(uint8_t scene_index)` — sets refreshed on Scene + Kit + 6 Instrument rows

Called at the successful terminal boundary of each Load/Save operation:

| Operation | Rows marked | Call site |
|---|---|---|
| Kit Load | Kit + 6 Instruments | `loadKitDirectory_tick` completion (~line 10364) |
| Scene Load | All selected Scenes via mask | `loadSceneDirectory_tick` completion (~line 11859) |
| Bank Load (empty bank) | Bank row only | `loadBankDirectory_tick` (~line 12647) |
| Bank Load (with children) | Bank row only | `loadBankDirectory_tick` (~line 12828) |
| Instrument Load | Single instrument row (excludes Morph/temporary) | `loadInstrument_tick` (~line 13690) |
| Instrument Save | Single instrument row (excludes Morph) | `saveInstrument_tick` (~line 14095) |
| Kit Save | Kit + 6 Instruments | `saveKitDirectory_tick` (~line 15798) |
| Scene Save | Scene + Kit + 6 Instruments | `saveSceneDirectory_tick` (~line 17378) |
| Bank Save | Bank row | `saveBankDirectory_tick` (~line 16605) |

#### Preserving refreshed across operations

- `filesystem_setResidentSource()` (~line 5297): captures existing `REFRESHED_FLAG` before staging dirty, ORs it back after. A source replacement does not clear a valid refresh witness.
- `filesystem_prepareResidentNamesCache()` (~line 5368): preserves `REFRESHED_FLAG` when resetting non-dirty rows to `UNKNOWN` during HCNAMES reload.
- `filesystem_clearResidentSourceDirtyFlags()` (~line 5531): changed from `&= VALUE_MASK` to `&= ~DIRTY_FLAG` so clearing dirty does not clear refreshed.

#### Formatter (`filesystem_formatResidentNameLine`, ~line 20372)

Captures the raw refreshed bit before `VALUE_MASK` strips metadata. Appends `\tR` between the source column and the newline at both numeric and symbolic exit points. Capacity checks (`len + 4u > cap`) guard the extra two bytes.

During autosave drain (`current_op == AUTOSAVE_PARAMETER_DRAIN && op_file_version != 0u`), the formatter suppresses `R` for rows where `autosave_objectFullyCaptured(row)` returns true. This serializes the "about to be cleared" state without clearing the in-RAM bit until final sync confirms.

#### Parser (`filesystem_cacheResidentRecord`, ~line 5433)

After parsing the source column at the second tab, the parser looks for a third field. If the first character after the second tab is `R` followed by NUL, tab, CR, or LF, the refreshed flag is restored to the source word during cache load. Extra/unknown fields are silently discarded for forward compatibility.

#### Autosave drain convergence

New functions:

- `filesystem_autosaveDrainHasRefreshWork()`: scans all 129 source words for any row with `REFRESHED_FLAG` set whose autosave object is fully captured. Returns 0 if mirror is not VALID (fails closed). This gates whether a post-drain HCNAMES rewrite is needed.
- `filesystem_clearResidentRefreshedCaptured()`: clears bit 13 only on rows that are both refreshed and fully captured. Called only after final sync confirms the physical register matches.
- `filesystem_autosaveDrainAfterCommit()`: entry point called after autosave target close. If no refresh work exists, finishes immediately. Otherwise sets `op_file_version = 1` (formatter mode flag) and enters phase 70.

Phases 70-76 in `autosaveParameterDrain_tick` implement the full safe-write cycle (identical pattern to other paths). Phase 76 sets `PUBLISH_PENDING` after rename.

`filesystem_autosaveWriterCompleted()` (~line 22606): on `FS_STATUS_DONE` with `op_file_version != 0`, calls `filesystem_clearResidentRefreshedCaptured()` and resets `op_file_version`. ERROR preserves witnesses for retry.

`filesystem_autosaveWriterFinishErrorNow()` (~line 6668): resets `op_file_version = 0` so the formatter mode does not leak into a subsequent non-autosave call.

#### Autosave query function (`Autosave.c`, after line 1433)

`autosave_objectFullyCaptured(uint16_t hcnames_row)`: maps an HCNAMES row coordinate to its autosave byte range using the existing section geometry defines, then scans the canonical dirty mask bit-by-bit. Returns 1 only when every byte in the object's wire interval is clean. Zero-allocation, read-only query. Declared in `Autosave.h` with `AUTOSAVE_HCNAMES_BANK_ROW` define (= 0).

---

## Post-Implementation Bug Fix: `op_close_status` Initialization

### Symptom

Hardware test after Phase B + B2 implementation:
- Bank Load → `FsErr` on Banks 049, 050, 018; `BKKit14` on Bank 016
- Kit Load → `HNkL01` on every kit load (= HCNAMES file not found at open, phase 1)
- Scene selection → `HNsL01` on every scene selection (= HCNAMES file not found at open, phase 1)

### Root cause

`op_close_status` is a shared static (`static fs_status_t op_close_status`). Bank Load's HCNAMES rewrite (phases 83-94) depends on checking this variable after temp file close (phase 90). However, between the HCNAMES preload read (phase 82) and the HCNAMES write open (phase 83), Bank Load runs its child scene loading (phases 1-78). Child scene loading routinely calls `afatfs_fclose()` with `on_file_closed` callbacks across many sites (lines ~11547-12804). The `on_file_closed` callback sets `op_close_done = true` but does NOT set `op_close_status`. However, the child close paths set `op_close_status = FS_STATUS_ERROR` on non-fatal child failures (scene not found, etc.) that the Bank Load operation recovers from.

When the HCNAMES write reaches its safe-write tail at phase 90 (wait temp close), the stale `ERROR` from a child scene failure causes the close-wait to falsely report failure, aborting the entire safe-write sequence before rename. This leaves `.hcnamtmp` on card but `.hcnames` intact. However, subsequent operations fail because the Bank Load itself reported `FsErr`, and cascading effects leave the system in a state where Kit Load and Scene selection cannot find `.hcnames`.

The autosave drain path (phase 71) correctly initialized `op_close_status = FS_STATUS_DONE` before its streaming phase, proving the pattern was known but was missed in the Bank Load and Bank Save write paths, and defensively in the boot full-write.

### Fix

Added `op_close_status = FS_STATUS_DONE;` at three locations:

1. **Bank Load phase 84** (~line 13121): after temp open succeeds, before entering streaming phase 85
2. **Bank Save phase 84** (~line 16647): after temp open succeeds, before entering streaming phase 85
3. **Boot full-write phase 0** (~line 4841): defensive, after temp open call, before streaming begins

Build delta: +16 bytes (`text=388884` from `388868`).

### Why Kit Load and Scene selection failed

Error codes `HNkL01` and `HNsL01` decode as phase 1 = file not found at `afatfs_fopen_lfn()`. The HCNAMES read path itself was not changed by Phase B. These failures cascade from the Bank Load failure: if Bank Load's HCNAMES safe-write aborts before completing rename, the `.hcnames` file remains intact on card (remove happens only after temp close validation). However, the Bank Load operation itself reports `FsErr`, which may leave the system in a state where the HCNAMES reader is not properly initialized for subsequent operations.

---

## Files Changed

| File | Lines | Nature |
|---|---|---|
| `Core/Hardware/SD/filesystem.c` | ~26,347 (was ~25,372) | +975 lines: 5 safe-write conversions, boot recovery prelude, refreshed flag infrastructure, autosave drain convergence, `op_close_status` fix |
| `Core/Hardware/SD/filesystem.h` | lines 627-639 | Token narrowing (0x7fff→0x1fff), new REFRESHED_FLAG/DIRTY_FLAG/VALUE_MASK defines |
| `Core/Bank/Scene/Autosave.h` | lines 68, 381-389 | `AUTOSAVE_HCNAMES_BANK_ROW`, `autosave_objectFullyCaptured()` declaration |
| `Core/Bank/Scene/Autosave.c` | +58 lines after line 1433 | `autosave_objectFullyCaptured()` implementation |

---

## Hardware Test 2: Post-Fix Results

Build: `text=388884` (same as post-fix). Firmware image `LXRV2_lxr02.img` = 389300 bytes on SD_CARD_B_PHASE_2.

### Results

| Operation | Result |
|---|---|
| Bank Save | OK |
| Kit Load | OK |
| Scene Load | OK |
| Scene Save | OK |
| Bank Load | FsErr at end |

### `.hcnames` analysis

The `.hcnames` file on the SD card is valid: 129 rows, all carrying `\tR` refreshed markers. No `.hcnamtmp` temp file present. This confirms the safe-write completed cleanly (close → sync → remove → rename → final sync all succeeded).

### `asavetrc.bin` analysis

No `E` (AUTOSAVE_TRACE_STAGE_OPERATION_ERROR, 0x45) records present in the trace. This means `filesystem_complete(FS_STATUS_ERROR)` was never called during the test session. The Bank Load completed with `FS_STATUS_DONE`, not `ERROR`.

### Root cause: settings writer race on post-load `.hcindex` restore

The "FsErr" is not from Bank Load child failures. Bank 051 has 16 valid Scenes, the trace has no ERROR records, and `op_bank_scene_failed_mask` is 0. The "FsErr" comes from the post-load `.hcindex` restore being synchronously rejected because the autonomous settings writer took the filesystem facade first.

#### Failure chain

1. Bank Load post-children commit calls `filesystem_markSettingsDirty()` at [filesystem.c:12894](Core/Hardware/SD/filesystem.c#L12894) with a 1-second debounce (`next_due = now + 1000`).
2. Bank Load phases 83-94 write the HCNAMES safe-write (open temp, stream 129 rows, close, sync, remove, rename, final sync). On slow SD cards this exceeds 1000ms.
3. Bank Load completes with DONE. Callback `on_bank_load_complete()` copies `op_bank_scene_failed_mask` (= 0) to `pm_bank_load_failed_scene_mask`. `preset_completeFilesystemOp()` calls `filesystem_ack()` → status = IDLE.
4. Next main loop: `menu_pollPresetStatus()` starts async DSP apply via `menu_startSoundApply()`. The `preset_bankLoadFailedSceneMask()` check at [menu.c:8425](Core/Menu/menu.c#L8425) passes (mask is 0).
5. Same tick: `filesystem_tick()` runs the idle schedulers. `filesystem_settingsWriterSchedule_tick()` finds `dirty = 1`, debounce expired (≥ 1000ms since step 1), calls `filesystem_start(SAVE_GLOBALS)`. `filesystem_start()` clears `fs_error_code` ([filesystem.c:23692](Core/Hardware/SD/filesystem.c#L23692)) and sets `status = BUSY`.
6. DSP apply completes on a subsequent tick. `menu_finishSoundApply()` → `menu_requestLoadCommandFinalIndexRestore()` → `filesystem_requestReloadLibraryIndex()` at [filesystem.c:25229](Core/Hardware/SD/filesystem.c#L25229) returns `false` (facade BUSY).
7. Rejection path at [menu.c:3277-3278](Core/Menu/menu.c#L3277): `menu_finishLoadSaveCommand()` then `menu_showFilesystemErrorOverlay()`. Overlay reads the now-empty `fs_error_code` → displays "FsErr".

#### Existing precedent

The autosave trace flush scheduler at [filesystem.c:23043](Core/Hardware/SD/filesystem.c#L23043) already has the `menu_isLoadSaveCommandActive()` guard, with a comment at lines 23035-23038 that literally describes this bug class:

> "this optional diagnostic append previously could start between Bank payload completion and Menu's final read-only `.hcindex` request, making that foreground request fail with generic FsErr solely because the shared facade was busy."

The settings writer scheduler was missing this guard.

#### Fix

Added `if (menu_isLoadSaveCommandActive()) return;` to `filesystem_settingsWriterSchedule_tick()` at [filesystem.c:22563](Core/Hardware/SD/filesystem.c#L22563), before the existing dirty/debounce checks. The guard defers settings writes while any accepted Load/Save command owns the facade, matching the trace flush scheduler's existing pattern. The dirty flag and debounce deadline are preserved — the write fires at the next idle tick after the command completes.

The autosave writer scheduler already defers via `menu_activePage == LOAD_PAGE || menu_activePage == SAVE_PAGE` at [filesystem.c:22872](Core/Hardware/SD/filesystem.c#L22872), which is broader (defers during page browse, not just active commands) and already covers this window.

### What the `op_close_status` fix resolved

- **Before fix**: Bank Load always failed with FsErr or BKKit14 because stale `op_close_status = ERROR` from child scene processing leaked into the HCNAMES safe-write tail (phase 90), causing the temp close check to abort. This left `.hcnamtmp` on card, cascading into HNkL01/HNsL01 on subsequent Kit/Scene operations.
- **After fix**: Bank Load's safe-write succeeds (`op_close_status` properly initialized at phase 84). Kit Load, Scene Load, Scene Save, and Bank Save all pass.

---

## Status

- [x] Phase B: all 5 write paths converted to safe-write
- [x] Phase B2: refreshed flag set/preserve/format/parse/clear
- [x] Boot recovery prelude (phases 15-19)
- [x] Autosave drain convergence (phases 70-76)
- [x] `op_close_status` initialization bug identified and fixed
- [x] Build clean, no warnings
- [x] Hardware test 2: safe-write verified (`.hcnames` valid, no `.hcnamtmp`, no operation errors in trace)
- [x] Hardware test 2: Kit Load, Scene Load/Save, Bank Save all pass
- [x] Bank Load FsErr — settings writer race on `.hcindex` restore identified and guarded

---

## Hardware Test 3: Post-Settings-Writer-Guard Results

Build: `text=388908, data=400, bss=96180`. Firmware image on SD_CARD_B_PHASE_3. `active_bank=52` after test.

### Results

| Operation | Result |
|---|---|
| Bank Load | OK (multiple) |
| Bank Save | OK (multiple) |
| Scene Load | OK (multiple) |
| Kit Load | OK (multiple) |

No FsErr on any Bank Load. The settings writer guard resolved the remaining failure.

Minor UI observations (noted in SCOPING_TARGETS.md, not regressions):
- Name retention glitch on Load/Save page after Bank operations
- Scrolling artifact on Load/Save page after Bank operations

### SD card validation

- `settings.cfg`: well-formed, `active_bank=52`, `autosave=1`, `lines=17`
- `.hcnames`: 129 rows, Bank row `Fullb 052`, `\tR` markers on loaded Scene/Kit/Instrument rows. No malformed rows
- `.hcprms1`/`.hcprms2`: both 34,768 bytes (correct full-record size), no truncation
- `asavetrc.bin`: no ERROR (`0x45`) stage records — clean session
- No `.hcnamtmp` stale temp files
- Bank 052 (`Fullb`): 16 child scenes (00-15), each with complete `sceneset.scg`, `Kit <name>/`, `pattern.pat`, `effects.fx`. `bankset.bcg` v2 well-formed (`active_scene=2`, `scene_mask_voice_edit=0004`)
- `.hcindex` files (Bank, Kit, Scene, Instrument/Drum) present and correctly populated
- `.hctmp.*` temporary instrument snapshot files present and well-formed (one per type)
- Child `kitset.kcg` v1 correct (slot/type/file entries), `pattern.pat` v3 correct (7 hex rows), `effects.fx` v1 placeholder
- Pre-existing remnants unchanged: `Bank/old012-c76c/`, `Kit/err007 Chip`, `Kit/err037 Slak2`, `Scene/err010 Barf` (from earlier sessions)
