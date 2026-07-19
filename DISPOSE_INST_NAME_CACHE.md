# Instrument Browser: Eliminate Root-Directory Name Cache from SRAM

> Status: completed. The former per-type `instrument_file_*` browser arrays
> described below have been removed. This document remains as the design and
> verification record for that migration; the retained Instrument browser state
> is one shared `fs_list_cache_name[index]` and `fs_list_cache_count`, tagged
> with the currently loaded type. The earlier proposal to eliminate all boot
> scans was superseded: boot now scans and writes one type at a time so this
> single cache can still regenerate every `.hcindex`.

## Background and Motivation (historical)

The root Instrument browser currently keeps three static arrays in `filesystem.c`:

| Array | Size |
|---|---|
| `instrument_file_name[4][128][9]` — display names | **4,608 bytes** |
| `instrument_file_open_name[4][128][13]` — 8.3 open aliases | **6,656 bytes** |
| `instrument_file_stem[4][128][17]` — long stems | **8,704 bytes** |
| **Total** | **~20 KB** |

All three arrays are populated once by `filesystem_requestScanInstruments()` at
boot and then consulted on every encoder tick while the Instrument Load browser
is open. The entire 20 KB block lives permanently in SRAM even though only one
name and one open alias are needed at any given moment.

This plan replaces the three arrays with three targeted on-demand operations,
all answered with small scratch buffers:

| New operation | Fires when | Output |
|---|---|---|
| `FS_INTERNAL_OP_COUNT_INSTRUMENT_TYPE` | Browser entry or type change | Count for one type into `instrument_file_count[type]` |
| `FS_INTERNAL_OP_FETCH_INSTRUMENT_ENTRY` | Every encoder tick (both Normal and Morph Load) | Display name + 8.3 alias for slot N of type T |
| `FS_INTERNAL_OP_CHECK_INSTRUMENT_EXISTS` | Every Save name-editor character change | Yes/no existence flag for OW indicator |

## Confirmed Answers to Previous Open Questions

**Q1 — Morph Load browsing.** Morph Load browses the same `Instrument/` pool
in exactly the same way as Normal Load; the only behavioural difference is which
Scene/slot fields are updated on commit. The on-demand name-fetch mechanism
therefore applies equally to both. Neither Normal nor Morph *Save* needs the
cache, only the live OW existence check (Q3).

**Q2 — Count scan timing.** No Instrument scan of any kind fires at boot.
Counts are populated lazily — one type at a time — when the user enters the
Instrument browser or changes the active type. The existing
`filesystem_requestScanInstruments()` boot call is removed entirely.

**Q3 — Overwrite check.** The `filesystem_instrumentTargetExists()` synchronous
walk of `instrument_file_name[]` is replaced by a new async
`FS_INTERNAL_OP_CHECK_INSTRUMENT_EXISTS` operation. It is in scope for this
session; `instrument_file_name[]` is not deleted until this replacement is
complete.

---

## Goal for This Session

Replace the Instrument Load/Save browser so it:

1. Shows the slot **number** immediately when the encoder moves (no delay).
2. Issues a lightweight SD name-fetch for the newly selected position.
3. Shows the **name** only after the fetch completes and the encoder position
   has not moved since the request was issued.
4. If the encoder has moved on, **discards** the stale result and fires a fresh
   request from the new position.
5. Requires **no scan cache** to display names for Normal or Morph Load browsing.
6. Requires **no scan cache** to display the OW/OK indicator in Instrument Save.
7. **Eliminates the boot Instrument scan entirely.**
8. Frees **~20 KB** of SRAM when the three arrays are deleted in Phase 4.

---

## Proposed Changes

### Phase 0 — Audit the existing scan/load FSM

Before writing code, read:

- `filesystem_scanInstruments_tick()` — the full scan walk (will be replaced by the count-only FSM).
- `filesystem_loadInstrument_tick()` (phases 0–11) — the load FSM.
- `filesystem_requestScanInstruments()` — removed from boot path.
- `filesystem_instrumentTargetExists()` — the synchronous overwrite check to be replaced.

Confirm that `afatfs_findNextObject()` / `afatfs_findFirstObject()` are the
only mechanism to walk a directory and retrieve a display name plus short alias
in one call.

---

### Phase 1 — Three new filesystem operations

#### 1A. `FS_INTERNAL_OP_COUNT_INSTRUMENT_TYPE` — lazy per-type count

**Purpose:** Walk `Instrument/` counting only files matching one
`instrument_type_t` extension. Stores the result into `instrument_file_count[type]`.
No names, no aliases, no stems.

**New request scratch:**

```c
/*
 * Scratch for lazy per-type Instrument count.
 *
 * op_count_instrument_type — extension filter for the count walk.
 * Stores result into instrument_file_count[op_count_instrument_type].
 */
static instrument_type_t op_count_instrument_type;
```

**New public API:**

```c
/*
 * Count root Instrument/ files of one type, storing the result lazily.
 *
 * Inputs: instrument type and completion callback. Output: on completion,
 * filesystem_instrumentCount(type) returns the updated count. Fires when
 * the Instrument browser is entered or the active type is changed, never
 * at boot.
 *
 * Returns false if the filesystem is busy.
 */
bool filesystem_requestCountInstrumentType(instrument_type_t type,
                                           fs_completion_cb_t cb);
```

**FSM `filesystem_countInstrumentType_tick()` phases:**

| Phase | Action |
|---|---|
| 0 | `instrument_file_count[op_count_instrument_type] = 0`; `afatfs_chdir(NULL)` |
| 1 | `afatfs_opendir_lfn(STORAGE_ROOT_INSTRUMENT, ...)` |
| 2 | Wait open; NULL handle → finish DONE (empty, count stays 0) |
| 3 | `afatfs_chdir(dir)` + `afatfs_findFirstObject()` |
| 4 | `afatfs_findNextObject()` loop: for each object, `storage_classifyInstrumentDisplayFile()` to check extension match; if match, increment `instrument_file_count[type]` (cap at `FS_INSTRUMENT_MAX_PER_TYPE`); at end-of-dir, advance to close |
| 5 | `afatfs_findLastObject()` + close dir |
| 6 | `afatfs_chdir(NULL)` |
| 7 | `filesystem_finish(FS_STATUS_DONE)` |

> [!NOTE]
> This operation is identical in structure to the old scan FSM minus all the
> `memcpy` lines that wrote into `instrument_file_name`, `instrument_file_open_name`,
> and `instrument_file_stem`. The only write is a single `uint8_t` counter.

---

#### 1B. `FS_INTERNAL_OP_FETCH_INSTRUMENT_ENTRY` — on-demand name + alias

**Purpose:** Walk `Instrument/` for one type, skip to the Nth matching file,
return its display name and 8.3 alias. Used by every encoder tick in both
Normal Load and Morph Load browsing.

**New scratch fields (25 bytes total):**

```c
/*
 * Scratch for on-demand Instrument entry fetch.
 *
 * op_fetch_instrument_type  - extension filter.
 * op_fetch_instrument_slot  - zero-based sorted slot to locate.
 * op_fetch_instrument_seq   - generation counter echoed back to Menu so
 *                             stale results are discarded rather than shown.
 * op_fetch_instrument_display_name - 9-byte result (8 chars + NUL).
 * op_fetch_instrument_open_name    - 13-byte result (SFN alias + NUL).
 */
static instrument_type_t op_fetch_instrument_type;
static uint8_t           op_fetch_instrument_slot;
static uint8_t           op_fetch_instrument_seq;
static char op_fetch_instrument_display_name[STORAGE_KIT_DISPLAY_NAME_LEN + 1u];
static char op_fetch_instrument_open_name[STORAGE_KIT_FILENAME_MAX];
```

**New public API:**

```c
/*
 * Fetch the display name and open alias for one Instrument pool slot.
 *
 * Inputs: instrument type, zero-based sorted slot index within that type,
 * opaque generation sequence number, and completion callback. On completion,
 * filesystem_fetchedInstrumentDisplayName() and
 * filesystem_fetchedInstrumentOpenName() hold the result, and
 * filesystem_fetchedInstrumentSeq() returns the echoed sequence so Menu can
 * discard stale results without blocking.
 *
 * Returns false if the filesystem is busy.
 */
bool filesystem_requestFetchInstrumentEntry(instrument_type_t  type,
                                            uint8_t            sorted_slot,
                                            uint8_t            seq,
                                            fs_completion_cb_t cb);

const char *filesystem_fetchedInstrumentDisplayName(void);
const char *filesystem_fetchedInstrumentOpenName(void);
uint8_t     filesystem_fetchedInstrumentSeq(void);
```

**FSM `filesystem_fetchInstrumentEntry_tick()` phases:**

| Phase | Action |
|---|---|
| 0 | `afatfs_chdir(NULL)` |
| 1 | `afatfs_opendir_lfn(STORAGE_ROOT_INSTRUMENT, ...)` |
| 2 | Wait open; NULL → finish ERROR |
| 3 | `afatfs_chdir(dir)` + `afatfs_findFirstObject()`; init match counter to 0 |
| 4 | `afatfs_findNextObject()` loop: for each object, check extension; if match and counter == `op_fetch_instrument_slot`, copy display name + alias to scratch, break to phase 6; else increment counter; end-of-dir → phase 5 |
| 5 | Slot not found (end of dir) → zero scratch; finish ERROR |
| 6 | `afatfs_findLastObject()` + close dir |
| 7 | `afatfs_chdir(NULL)` |
| 8 | `filesystem_finish(FS_STATUS_DONE)` |

---

#### 1C. `FS_INTERNAL_OP_CHECK_INSTRUMENT_EXISTS` — async OW check

**Purpose:** Walk `Instrument/` looking for a case-insensitive display filename
match of a given stem + type extension. Returns a boolean. Used by the
Instrument Save OW indicator in place of the synchronous
`filesystem_instrumentTargetExists()`.

**New scratch fields (small):**

```c
/*
 * Scratch for async Instrument overwrite check.
 *
 * op_check_instrument_type  - extension filter.
 * op_check_instrument_seq   - generation counter echoed back; Save name-editor
 *                             may change faster than the walk completes.
 * op_check_instrument_stem  - eight-character display stem to match.
 * op_check_instrument_found - result: nonzero means a same-name file exists.
 */
static instrument_type_t op_check_instrument_type;
static uint8_t           op_check_instrument_seq;
static char op_check_instrument_stem[STORAGE_KIT_DISPLAY_NAME_LEN + 1u];
static uint8_t           op_check_instrument_found;
```

**New public API:**

```c
/*
 * Async check whether an Instrument/ file with the given display stem and
 * type already exists (case-insensitive).
 *
 * Inputs: instrument type, eight-character display stem (the Save name editor
 * buffer), generation sequence for stale-check, and completion callback.
 * Outputs: on completion, filesystem_checkedInstrumentExists() returns the
 * result and filesystem_checkedInstrumentSeq() returns the echoed sequence.
 *
 * Returns false if the filesystem is busy.
 */
bool filesystem_requestCheckInstrumentExists(instrument_type_t  type,
                                             const char        *display_stem,
                                             uint8_t            seq,
                                             fs_completion_cb_t cb);

uint8_t filesystem_checkedInstrumentExists(void);
uint8_t filesystem_checkedInstrumentSeq(void);
```

**FSM `filesystem_checkInstrumentExists_tick()` phases:**

| Phase | Action |
|---|---|
| 0 | `op_check_instrument_found = 0`; `afatfs_chdir(NULL)` |
| 1 | `afatfs_opendir_lfn(STORAGE_ROOT_INSTRUMENT, ...)` |
| 2 | Wait open; NULL → finish DONE (not found) |
| 3 | `afatfs_chdir(dir)` + `afatfs_findFirstObject()` |
| 4 | `afatfs_findNextObject()` loop: for each object matching the extension, build its display stem and do a case-insensitive compare against `op_check_instrument_stem`; on match set `op_check_instrument_found = 1`, break to phase 6; end-of-dir → phase 6 with found = 0 |
| 5 | *(merged into phase 4 loop)* |
| 6 | `afatfs_findLastObject()` + close dir |
| 7 | `afatfs_chdir(NULL)` |
| 8 | `filesystem_finish(FS_STATUS_DONE)` |

**Retire `filesystem_instrumentTargetExists()`:** Once the new async check is
wired into Menu, `filesystem_instrumentTargetExists()` (the synchronous walker
of `instrument_file_name[]`) is deleted.

---

### Phase 2 — Remove the boot Instrument scan

- Remove `filesystem_requestScanInstruments()` call from the boot sequence in
  `main.c` (or wherever it is currently called synchronously at startup).
- Remove `filesystem_requestScanInstruments()` from `filesystem.h` public API.
- Remove `filesystem_scanInstruments_tick()` FSM from `filesystem.c`.
- The `FS_INTERNAL_OP_SCAN_INSTRUMENTS` enum value is deleted.
- `instrument_file_count[INSTRUMENT_TYPE_UNKNOWN]` remains but is initialised
  to all-zeros at startup; it is now populated only by the lazy count operation
  (Phase 1A).

> [!WARNING]
> The boot-time `instrument_file_count[]` population is gone. Any code path
> that calls `filesystem_instrumentCount()` before the user has entered the
> Instrument browser for a given type will see 0. Audit all call sites before
> deleting the boot scan. The two sites in `menu.c` are both inside
> `menu_instrumentLoadActive` guards, so they are safe.

---

### Phase 3 — Menu-side on-demand state machine

#### 3a. New Menu state variables

```c
/*
 * On-demand Instrument name fetch state.
 *
 * menu_instNameSeq     - generation counter, incremented on every encoder
 *                        move that changes the displayed position. Wraps.
 * menu_instNamePending - nonzero while a name fetch is in flight.
 * menu_instNameBuf     - result from the last fetch whose sequence matched.
 *                        Eight printable chars + NUL.
 * menu_instNameBufType/Slot - type/slot the cached result belongs to, so a
 *                        stale result is not shown after a type change.
 * menu_instNameOpenAlias - 8.3 open alias paired with menu_instNameBuf;
 *                        used by the load-on-confirm path in Phase 4.
 */
static uint8_t           menu_instNameSeq     = 0u;
static uint8_t           menu_instNamePending = 0u;
static char              menu_instNameBuf[STORAGE_KIT_DISPLAY_NAME_LEN + 1u];
static instrument_type_t menu_instNameBufType = INSTRUMENT_TYPE_UNKNOWN;
static uint8_t           menu_instNameBufSlot = 0xFFu;
static char              menu_instNameOpenAlias[STORAGE_KIT_FILENAME_MAX];

/*
 * On-demand Instrument overwrite check state.
 *
 * menu_instOwSeq      - generation counter, incremented on every save-name
 *                       character change.
 * menu_instOwPending  - nonzero while the existence check is in flight.
 * menu_instOwFound    - last confirmed existence result (0 = OK, 1 = OW).
 */
static uint8_t menu_instOwSeq     = 0u;
static uint8_t menu_instOwPending = 0u;
static uint8_t menu_instOwFound   = 0u;
```

#### 3b. New helper: `menu_instrumentLoadRequestName()`

Called on every encoder tick that changes the pool position, and on browser
entry. Applies equally to Normal Load and Morph Load.

```c
static void menu_instrumentLoadRequestName(void)
{
    /*
     * Fire an on-demand Instrument name fetch for the current display position.
     *
     * Inputs: selected type and shown index. Output: increments the generation
     * counter, erases the stale name and open alias, and posts a non-blocking
     * fetch request. If the filesystem is busy the request is skipped; the
     * encoder tries again on the next tick. This never blocks.
     *
     * Applies to both Normal Load and Morph Load browsing; the fetch is
     * identical in both cases.
     */
    uint8_t next_seq        = (uint8_t)(menu_instNameSeq + 1u);
    menu_instNameSeq        = next_seq;
    menu_instNamePending    = 0u;
    menu_instNameBuf[0]     = '\0';
    menu_instNameOpenAlias[0] = '\0';

    if (filesystem_requestFetchInstrumentEntry(
            menu_instrumentLoadType,
            menu_instrumentLoadShownIndex,
            next_seq,
            menu_instrumentLoadNameCallback)) {
        menu_instNamePending = 1u;
    }
}
```

#### 3c. New helper: `menu_instrumentLoadRequestCount()`

Called on browser entry and on type change. Fires a count-only walk for the
newly selected type.

```c
static void menu_instrumentLoadRequestCount(void)
{
    /*
     * Fire a lazy count scan for the currently selected Instrument type.
     *
     * Inputs: menu_instrumentLoadType. Output: asynchronous update of
     * filesystem_instrumentCount(type); Menu repaints when the callback fires.
     * If the filesystem is busy the count remains 0 for this type until the
     * next type-change or browser re-entry.
     *
     * This replaces the boot-time filesystem_requestScanInstruments() call.
     * Only the count for the active type is fetched; other types are counted
     * only if the user navigates to them.
     */
    instrument_file_count[menu_instrumentLoadType] = 0u; /* optimistic reset */
    filesystem_requestCountInstrumentType(menu_instrumentLoadType,
                                          menu_instrumentLoadCountCallback);
}
```

#### 3d. Completion callbacks

```c
static void menu_instrumentLoadNameCallback(void)
{
    /*
     * Consume a completed Instrument name fetch.
     *
     * If the echoed sequence matches the current generation counter, copy the
     * display name and open alias into Menu's scratch and repaint. If it does
     * not match (encoder moved on while the walk was in flight), discard
     * silently. A new fetch is already in flight or will fire on the next tick.
     *
     * Context: main-loop filesystem_tick(), not an ISR.
     */
    uint8_t seq = filesystem_fetchedInstrumentSeq();
    menu_instNamePending = 0u;
    if (!menu_instrumentLoadActive || seq != menu_instNameSeq)
        return;   /* stale or exited browser — discard */
    memcpy(menu_instNameBuf,
           filesystem_fetchedInstrumentDisplayName(),
           STORAGE_KIT_DISPLAY_NAME_LEN + 1u);
    memcpy(menu_instNameOpenAlias,
           filesystem_fetchedInstrumentOpenName(),
           STORAGE_KIT_FILENAME_MAX);
    menu_instNameBufType = menu_instrumentLoadShownType;
    menu_instNameBufSlot = menu_instrumentLoadShownIndex;
    menu_repaintAll();
}

static void menu_instrumentLoadCountCallback(void)
{
    /*
     * Consume a completed per-type count.
     *
     * filesystem_instrumentCount(type) is now valid. Clamp the shown index
     * and repaint so the number field reflects the new upper bound.
     *
     * Context: main-loop filesystem_tick(), not an ISR.
     */
    if (!menu_instrumentLoadActive)
        return;
    menu_instrumentLoadClampIndex();   /* existing helper */
    /* Re-seed the name fetch in case the count reduced the shown index. */
    menu_instrumentLoadRequestName();
    menu_repaintAll();
}

static void menu_instrumentOwCheckCallback(void)
{
    /*
     * Consume a completed Instrument existence check.
     *
     * If the echoed sequence matches, update the OW flag and repaint so the
     * Save bottom-right affordance shows OK or OW immediately. Stale results
     * from previous editor states are discarded.
     *
     * Context: main-loop filesystem_tick(), not an ISR.
     */
    uint8_t seq = filesystem_checkedInstrumentSeq();
    menu_instOwPending = 0u;
    if (!menu_instrumentLoadActive || !menu_instrumentSaveMode ||
        seq != menu_instOwSeq)
        return;   /* stale or exited Save mode */
    menu_instOwFound = filesystem_checkedInstrumentExists();
    menu_repaintAll();
}
```

#### 3e. Encoder path changes

**Browser entry** (`menu_loadSaveEnterInstrumentLoad()`): after existing
clamp/index initialisation, add:

```c
menu_instNameBuf[0]       = '\0';
menu_instNameOpenAlias[0] = '\0';
menu_instNameBufType      = INSTRUMENT_TYPE_UNKNOWN;
menu_instNameBufSlot      = 0xFFu;
menu_instrumentLoadRequestCount();   /* lazy count for entry type */
menu_instrumentLoadRequestName();    /* optimistic first name fetch */
```

**Type change** (`menu_instrumentLoadStepType()`, after updating
`menu_instrumentLoadType`): add:

```c
menu_instNameBuf[0]       = '\0';
menu_instNameOpenAlias[0] = '\0';
menu_instNameBufType      = INSTRUMENT_TYPE_UNKNOWN;
menu_instNameBufSlot      = 0xFFu;
menu_instrumentLoadRequestCount();   /* count new type before bounding */
/* name fetch will re-fire from count callback after clamp */
```

**Encoder move in pool mode** (both Normal and Morph Load, the existing block
in `menu_handleLoadSaveMenu` starting at line 5363): replace
`menu_instrumentLoadRequestSelection()` with `menu_instrumentLoadRequestName()`.

```
Before (current):
    menu_instrumentLoadRequestSelection();   // immediate full file load

After:
    menu_instrumentLoadRequestName();        // cheap name-only fetch
    /* Full load deferred to encoder-click (Phase 4). */
```

**Save name editor change** (whenever a character is edited in
`menu_instrumentSaveName[]`): add a call to fire the existence check:

```c
static void menu_instrumentSaveRequestOwCheck(void)
{
    /*
     * Fire an async existence check for the current Save editor stem.
     *
     * Inputs: resident instrument type and the current eight-character
     * editor buffer. Output: increments generation counter, resets the
     * found flag to 0 (optimistic OK), and posts a non-blocking check.
     * If the filesystem is busy the OW indicator remains 0 (OK) until
     * the next character change triggers a retry.
     */
    const kit_instrument_slot_t *slot =
        scene_instrumentSlotConst(menu_instrumentLoadScene,
                                  menu_instrumentLoadSlot);
    instrument_type_t type = slot ? slot->type : INSTRUMENT_TYPE_DRM;
    uint8_t next_seq = (uint8_t)(menu_instOwSeq + 1u);
    menu_instOwSeq     = next_seq;
    menu_instOwFound   = 0u;
    menu_instOwPending = 0u;

    if (filesystem_requestCheckInstrumentExists(
            type, menu_instrumentSaveName, next_seq,
            menu_instrumentOwCheckCallback)) {
        menu_instOwPending = 1u;
    }
}
```

Call `menu_instrumentSaveRequestOwCheck()` at Save-mode entry
(`menu_loadSaveEnterInstrumentSave()`) and after every character edit in
`menu_handleLoadSaveMenu`.

#### 3f. Repaint changes

**Pool-source name display** (around line 4711 of `menu_repaintLoadSavePage()`):

```c
/* Before */
memcpy(&editDisplayBuffer[1][5],
       count ? filesystem_instrumentName(menu_instrumentLoadShownType, index)
             : "Empty   ",
       8u);

/* After */
const char *shown_name;
if (count == 0u) {
    shown_name = "Empty   ";
} else if (menu_instNameBuf[0] != '\0' &&
           menu_instNameBufType == menu_instrumentLoadShownType &&
           menu_instNameBufSlot == menu_instrumentLoadShownIndex) {
    shown_name = menu_instNameBuf;
} else {
    shown_name = "        ";   /* eight spaces: name fetch in progress */
}
memcpy(&editDisplayBuffer[1][5], shown_name, 8u);
```

The number field (columns 1–3) is unchanged — it renders from
`menu_instrumentLoadShownIndex` immediately with no SD access.

**Save OW indicator** (replaces the `menu_currentSaveWouldOverwrite()` call in
the nested Instrument Save painter, around line 4639):

```c
/* Before */
if (menu_currentSaveWouldOverwrite())
    memcpy(&editDisplayBuffer[1][14], "OW", 2u);
else
    memcpy(&editDisplayBuffer[1][14], menuText_ok, 2u);

/* After (inside menu_instrumentSaveMode repaint block) */
if (menu_instOwFound)
    memcpy(&editDisplayBuffer[1][14], "OW", 2u);
else
    memcpy(&editDisplayBuffer[1][14], menuText_ok, 2u);
```

`menu_currentSaveWouldOverwrite()` still handles all other Save types; only the
nested Instrument Save painter switches to the async flag.

---

### Phase 4 — Instrument Load transaction: confirm on click, load by alias

#### 4a. `filesystem_requestLoadInstrumentByAlias()` — new entry point

```c
/*
 * Load one root Instrument/ file by pre-fetched open alias.
 *
 * Cache-free path: the caller has the 8.3 alias and display name from a
 * preceding filesystem_requestFetchInstrumentEntry() call and passes them
 * here, bypassing any scan-cache lookup.
 *
 * Inputs: destination Scene/slot, type, pre-fetched 8.3 open alias,
 * display name (for post-commit kit slot labelling), and completion callback.
 * Output: same async parse/staging as filesystem_requestLoadInstrument().
 */
bool filesystem_requestLoadInstrumentByAlias(uint8_t            destination_scene,
                                             uint8_t            destination_slot,
                                             instrument_type_t  type,
                                             const char        *open_alias,
                                             const char        *display_name,
                                             fs_completion_cb_t cb);
```

Inside the load FSM, phase 6 is bifurcated: when called via the alias entry
point, `afatfs_fopen()` uses `open_alias` directly rather than reading
`instrument_file_open_name[type][index]`. The display name and stem at
phase-8 commit are similarly taken from the request scratch rather than from
the deleted arrays.

#### 4b. Menu: load fires on encoder click, not on every tick

`menu_instrumentLoadRequestSelection()` is updated to use the alias path and is
called only from the click/OK handler — not from the encoder-move branch.

```c
static void menu_instrumentLoadRequestSelection(void)
{
    /*
     * Trigger the actual Instrument load on encoder-click confirmation.
     *
     * Guard: the open alias must already be resolved for the current position.
     * If the name fetch has not yet completed (blank name on LCD), the click
     * is silently ignored — the user sees the blank and waits a moment.
     *
     * This function is no longer called from the encoder-move path; it fires
     * only from the explicit confirmation action.
     */
    if (menu_instNameBufType != menu_instrumentLoadType ||
        menu_instNameBufSlot != menu_instrumentLoadShownIndex ||
        menu_instNameOpenAlias[0] == '\0')
        return;   /* alias not ready — silently skip */

    menu_instrumentLoadIndex[menu_instrumentLoadType] =
        menu_instrumentLoadShownIndex;  /* keep index consistent */

    if ((menu_instrumentLoadMorphMode &&
         preset_loadInstrumentMorphByAlias(
             menu_instrumentLoadScene,
             menu_instrumentLoadSlot,
             menu_instrumentLoadType,
             menu_instNameOpenAlias,
             menu_instNameBuf)) ||
        (!menu_instrumentLoadMorphMode &&
         preset_loadInstrumentByAlias(
             menu_instrumentLoadScene,
             menu_instrumentLoadSlot,
             menu_instrumentLoadType,
             menu_instNameOpenAlias,
             menu_instNameBuf,
             menu_kitLoadSceneMask))) {
        menu_storageBusy = 1u;
    }
}
```

> [!IMPORTANT]
> `preset_loadInstrumentByAlias()` and `preset_loadInstrumentMorphByAlias()`
> are thin wrappers that call `filesystem_requestLoadInstrumentByAlias()` with
> the appropriate Scene/slot coordinates — identical in structure to the
> existing `preset_loadInstrument()` / `preset_loadInstrumentMorph()` except
> they pass the alias rather than a cache index.

---

### Phase 5 — Delete the three large cache arrays

Once Phases 1–4 are complete and all call sites are migrated:

1. Delete `instrument_file_name[4][128][9]` — **4,608 bytes freed**.
2. Delete `instrument_file_open_name[4][128][13]` — **6,656 bytes freed**.
3. Delete `instrument_file_stem[4][128][17]` — **8,704 bytes freed**.
4. Delete `filesystem_requestScanInstruments()`, `filesystem_scanInstruments_tick()`, and `FS_INTERNAL_OP_SCAN_INSTRUMENTS`.
5. Delete `filesystem_instrumentTargetExists()` (synchronous OW check).
6. Delete `filesystem_instrumentName()` and `filesystem_instrumentDisplayIndex()`.
7. Remove all `memset` clearing of the three arrays.
8. Remove `filesystem_instrumentName()` / `filesystem_instrumentDisplayIndex()` call sites from `menu.c`.

**Total freed: ~20 KB** of SRAM.

---

## Verification Plan

### Manual on-device tests

1. **Normal Load browse:** Enter Load → VOICE press → Instrument browser opens. Spin encoder fast: number updates instantly; name blanks then settles once SD fetch completes for the resting position.
2. **Morph Load browse:** Enter the `...Mrp` row (Morph Load). Same behaviour as Normal: pool position numbered immediately, name fetched on-demand.
3. **Type change:** Change type with the top-row encoder. Number resets to 1, name blanks, count fetch fires, name fetch fires after count completes and index is clamped.
4. **Confirm load:** Click when name is visible → instrument loads (DSP applies, Scene slot updated). Click while name still blank → silently skipped (alias not ready guard).
5. **Instrument Save OW:** Enter Save mode from Instrument browser. Edit the name character by character: OK changes to OW within a fraction of a second whenever a file with that name exists; returns to OK when the stem no longer matches. Both Normal and Morph Save show the correct indicator.
6. **Boot time:** Instrument scan no longer fires at boot. Boot is faster; `filesystem_instrumentCount()` returns 0 for all types until the browser is opened.
7. **Kit Load, Scene Load, Bank Load:** Unaffected — separate scan/load paths.

### Build verification

```bash
make && make img
arm-none-eabi-size build/LXRV2_lxr02.elf   # before and after Phase 5
```

- No new compiler warnings.
- `.bss` shrinks by approximately 20 KB after Phase 5.

### Regression checks

- Instrument Load commits correctly to Scene/slot (parameter apply, display name update, stem update).
- Instrument Morph Load commits to morph endpoint only (no slot type change, no display name update — confirmed existing behaviour).
- Instrument Save writes correct file; kit slot stem in `kit_t.instrument_display_name` is unchanged by Save.
- OW indicator shows for exact case-insensitive stem + extension match, clears when name is edited away.
- `menu_storageBusy` is set/cleared correctly throughout all new async operations.

---

## Files to Modify

### [`filesystem.c`](file:///Users/bc/Helicase/Core/Hardware/SD/filesystem.c)

- Add `FS_INTERNAL_OP_COUNT_INSTRUMENT_TYPE`, `FS_INTERNAL_OP_FETCH_INSTRUMENT_ENTRY`, `FS_INTERNAL_OP_CHECK_INSTRUMENT_EXISTS` to the internal op enum.
- Add new scratch fields (~30 bytes total across the three operations).
- Add three new FSM tick functions.
- Add three new public request functions and accessor functions.
- Add `filesystem_requestLoadInstrumentByAlias()` with bifurcated phase 6 in the load FSM.
- *(Phase 5)* Delete `instrument_file_name`, `instrument_file_open_name`, `instrument_file_stem`.
- *(Phase 5)* Delete `filesystem_requestScanInstruments()`, `filesystem_scanInstruments_tick()`, `filesystem_instrumentTargetExists()`, `filesystem_instrumentName()`, `filesystem_instrumentDisplayIndex()`.

### [`filesystem.h`](file:///Users/bc/Helicase/Core/Hardware/SD/filesystem.h)

- Declare all new public functions.
- *(Phase 5)* Remove deleted function declarations.

### [`main.c`](file:///Users/bc/Helicase/main.c) *(or wherever the boot scan is called)*

- *(Phase 2)* Remove `filesystem_requestScanInstruments()` call from the boot sequence.

### [`Core/Bank/Scene/Preset/presetManager.c`](file:///Users/bc/Helicase/Core/Bank/Scene/Preset/presetManager.c)

- Add `preset_loadInstrumentByAlias()` and `preset_loadInstrumentMorphByAlias()` thin wrappers.

### [`menu.c`](file:///Users/bc/Helicase/Core/Menu/menu.c)

- Add new static state variables (~50 bytes: name buf, alias buf, type/slot tags, two generation counters, two pending flags, one found flag).
- Add `menu_instrumentLoadRequestName()`, `menu_instrumentLoadRequestCount()`, `menu_instrumentSaveRequestOwCheck()`.
- Add `menu_instrumentLoadNameCallback()`, `menu_instrumentLoadCountCallback()`, `menu_instrumentOwCheckCallback()`.
- Replace immediate-load encoder behaviour with name-fetch-then-confirm flow.
- Fire count + name fetch on browser entry and type change.
- Fire OW check on Save entry and every name character change.
- Update repaint block: pool name uses `menu_instNameBuf`; Save OW uses `menu_instOwFound`.
- *(Phase 5)* Remove `filesystem_instrumentName()` / `filesystem_instrumentDisplayIndex()` call sites.

---

## Implementation Order

1. **Phase 1** — Three new filesystem FSMs and their public API.
2. **Phase 2** — Remove boot scan from `main.c`; remove `filesystem_requestScanInstruments()`.
3. **Phase 3** — Menu state, callbacks, encoder path, repaint, OW check wiring.
4. Build, flash, smoke-test the browser on hardware (name + OW indicator).
5. **Phase 4** — `filesystem_requestLoadInstrumentByAlias()` + preset wrappers + Menu confirmation hook.
6. Test load transaction end-to-end (Normal and Morph).
7. **Phase 5** — Delete three arrays + all dead API; final build + SRAM size verification.
