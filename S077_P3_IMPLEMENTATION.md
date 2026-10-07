# S077 P3 — Implementation Schedule

**Parent:** `S077_P3_BLANK_MENU_AFTER_SCN_LOAD.md` §5.
**Status (2026-10-07):** code implementation not yet applied; this document
is the complete change specification.

---

## Overview

Two functional changes and four comment corrections, all in `menu.c`,
`filesystem.c`, and `filesystem.h`. One new static function; one rewritten
function body; no new RAM; no ISR/DSP path; no file-format change.

---

## Change 1 — New callback: `menu_residentNameDeferredFlushComplete()`

**File:** `Core/Menu/menu.c`
**Location:** insert immediately after `menu_residentNameScratchFlushComplete()`
(after line 5642, before line 5644 `menu_endResidentNameScratchSession`)
**Action:** ADD new static function

### Code (full function)

```c
static void menu_residentNameDeferredFlushComplete(void)
{
    /*
     * What: completion callback for the background HCNAMES rewrite that runs
     * after leaving Load/Save with a nonzero dirty-Scene mask.
     *
     * Why this exists as a separate callback: the foreground callback
     * menu_residentNameScratchFlushComplete() was written for name-session
     * boundaries *inside* the Load page, where the next browser view needs
     * the write's result. It therefore raises and clears menu_storageBusy,
     * clears the browser name cache, posts browser requests, repaints, and
     * enters the next Kit/Instrument context. None of those actions apply
     * after leaving Load/Save: the destination page is already visible and
     * fully working, the browser cache is already disposed (menu_switchPage()
     * line 13134), and menu_storageBusy must never be raised on this path
     * because it would lock mode buttons, encoder, pots, runtime widgets,
     * and copy/clear for the ~3 s write duration (the root cause of
     * S077_P3_BLANK_MENU_AFTER_SCN_LOAD.md §3).
     *
     * Inputs: the filesystem facade status at the moment the HCNAMES write
     * completed (FS_STATUS_DONE on success, FS_STATUS_ERROR on failure).
     *
     * Outputs:
     *   - filesystem_ack() is called exactly once on both paths, returning
     *     the shared facade from DONE/ERROR to IDLE. This is mandatory:
     *     without it the facade stays at its terminal status and blocks
     *     AutoSave, the trace flush, and every other idle-only scheduler
     *     rung permanently. See the identical pattern in
     *     menu_residentNameScratchFlushComplete() (line 5610) and
     *     menu_showFilesystemErrorOverlay() (line 5069 ff.).
     *   - Success: menu_residentNameDirtySceneMask is cleared. The in-RAM
     *     name block and the card copy now agree. No cache clear, no browser
     *     request, no repaint — the visible page reads only SceneData,
     *     parameter_values, and the in-RAM name block, none of which depend
     *     on the card copy (§4 of the parent document).
     *   - Failure: the dirty mask is kept and
     *     menu_showFilesystemErrorOverlay() is called, which shows the FsErr
     *     overlay exactly as today. filesystem_tick() will retry on its next
     *     idle pass because the mask is still nonzero.
     *   - menu_storageBusy is not touched on either path. It was never set
     *     by the trigger (Change 2 below) and must not be set here.
     *
     * Common accessors: called only by filesystem_residentNames_tick() at
     * the end of the HCNAMES write state machine via the callback pointer
     * passed to filesystem_requestUpdateResidentKitNames().
     *
     * Affiliates: menu_triggerDeferredHcnamesFlush() (the trigger, Change 2),
     * menu_residentNameScratchFlushComplete() (the foreground sibling),
     * filesystem_requestUpdateResidentKitNames(),
     * filesystem_residentNames_tick(), filesystem_ack(),
     * menu_showFilesystemErrorOverlay().
     */
    uint8_t flush_ok = (uint8_t)(filesystem_status() == FS_STATUS_DONE);

    filesystem_ack();

    if (!flush_ok) {
        menu_showFilesystemErrorOverlay();
        return;
    }
    menu_residentNameDirtySceneMask = 0u;
}
```

### What this does not do (deliberately)

- Does not set or clear `menu_storageBusy`. The trigger never set it; the
  callback never touches it.
- Does not clear `menu_residentNameScratchValid` or
  `menu_residentNameScratchScene`. The exit path in `menu_switchPage()`
  (line 13331–13333) already did that before the write started.
- Does not call `filesystem_clearNameCache()`. The exit path already did
  that (line 13334). The Load/Save re-entry path has its own reload.
- Does not post browser requests or repaint. No Load/Save surface is open.
- Does not call `menu_traceInstrumentEntry()`. The deferred path is not an
  Instrument-entry lifecycle boundary; the foreground path's trace records
  are specific to name-session transitions on the Load page.

---

## Change 2 — Rewrite `menu_triggerDeferredHcnamesFlush()`

**File:** `Core/Menu/menu.c`
**Location:** lines 5695–5710
**Action:** MODIFY — replace the function body

### Current code (lines 5695–5710)

```c
void menu_triggerDeferredHcnamesFlush(void)
{
    /*
     * What: hand one deferred HCNAMES checkpoint to the filesystem facade.
     * Why: leaving Load/Save must repaint immediately; the dirty mask survives
     * until this idle scheduler rung can safely start the existing atomic
     * writer. Inputs: an idle facade and a nonzero Menu dirty mask. Outputs:
     * one accepted HCNAMES request, or retained dirty state on refusal.
     * Affiliates: menu_endResidentNameScratchSession(), filesystem_tick(),
     * and menu_residentNameScratchFlushComplete().
     */
    if (menu_residentNameDirtySceneMask == 0u ||
        menu_activePage == LOAD_PAGE || menu_activePage == SAVE_PAGE)
        return;
    (void)menu_endResidentNameScratchSession();
}
```

### Replacement code

```c
void menu_triggerDeferredHcnamesFlush(void)
{
    /*
     * What: hand one deferred HCNAMES rewrite to the filesystem facade as a
     * background write that does not lock the Menu.
     *
     * Why: after leaving Load/Save, the dirty-Scene mask records Kit and
     * Instrument identity rows whose in-RAM names have been updated by a
     * completed load or save but whose card copy in `/.hcnames` is stale.
     * The destination page must be fully working immediately — mode buttons,
     * encoder, pots, runtime widgets, and copy/clear — so this path must not
     * raise menu_storageBusy. The old implementation delegated to
     * menu_endResidentNameScratchSession(), which raised menu_storageBusy
     * and used the foreground callback menu_residentNameScratchFlushComplete().
     * That locked the entire Menu for the ~3 s write duration, causing the
     * blank-VOICE-page and encoder-lock symptoms described in
     * S077_P3_BLANK_MENU_AFTER_SCN_LOAD.md §3.
     *
     * Inputs: an idle filesystem facade (checked by filesystem_tick() before
     * calling this function, line 25621), copy/clear not suspended (same
     * gate), and a nonzero menu_residentNameDirtySceneMask. The page must
     * not be LOAD_PAGE or SAVE_PAGE (those sessions manage their own
     * foreground flushes). The name scratch was already invalidated and the
     * browser cache already cleared at Load/Save exit (menu_switchPage()
     * lines 13331–13334).
     *
     * Outputs:
     *   - Calls filesystem_requestUpdateResidentKitNames() directly with the
     *     dirty mask and the new background callback
     *     menu_residentNameDeferredFlushComplete().
     *   - Does NOT touch menu_storageBusy. The filesystem goes BUSY for the
     *     duration of the write, which prevents AutoSave and other facade
     *     users from starting, but the Menu itself remains fully responsive.
     *   - If the request is refused (filesystem reports busy despite the
     *     caller's check — a race that the existing retry tolerates), the
     *     mask stays set and filesystem_tick() retries on the next idle pass.
     *
     * Common accessors: called only from filesystem_tick() (line 25623)
     * through the menu_hasResidentNameDirtyMask() / page-check gate.
     *
     * Affiliates: menu_residentNameDeferredFlushComplete() (the callback),
     * menu_endResidentNameScratchSession() (the foreground sibling, unchanged,
     * still used by all in-session boundaries on the Load page),
     * filesystem_tick(), filesystem_requestUpdateResidentKitNames(),
     * filesystem_residentNames_tick().
     */
    if (menu_residentNameDirtySceneMask == 0u ||
        menu_activePage == LOAD_PAGE || menu_activePage == SAVE_PAGE)
        return;
    (void)filesystem_requestUpdateResidentKitNames(
        menu_residentNameDirtySceneMask,
        menu_residentNameDeferredFlushComplete);
}
```

### What changed

- The call to `menu_endResidentNameScratchSession()` is replaced by a
  direct call to `filesystem_requestUpdateResidentKitNames()` with the new
  background callback.
- `menu_storageBusy` is never set. The scratch invalidation and cache clear
  that `menu_endResidentNameScratchSession()` used to perform before posting
  the request are now handled by the `menu_switchPage()` exit block
  (lines 13331–13334), which already ran before `filesystem_tick()` calls
  this trigger.

---

## Change 3 — Comment correction: `filesystem.h` lines 903–920

**File:** `Core/Hardware/SD/filesystem.h`
**Location:** lines 903–920 (the "Resident Instrument name-register access"
comment block above `filesystem_requestLoadResidentInstrumentName`)
**Action:** MODIFY — correct the stale shared-cache reference

### Current text (line 916)

```
 * SRAM array is allocated: both operations temporarily reuse the single
 * `fs_list_cache_name[1000][9]` allocation normally occupied by `.hcindex`.
```

### Replacement text

```
 * SRAM array is allocated: both operations use the dedicated 1,449-byte
 * `hcnames_name_mirror` (not the shared `fs_list_cache_name` browser cache).
```

### Full replacement block (lines 903–920)

Replace:
```c
/*
 * Resident Instrument name-register access.
 *
 * What: the load request reads root `/.hcnames` into the existing generalized
 * name cache so Menu can copy one Scene/voice name on nested Instrument Load or
 * Save entry. The update request performs the same read, replaces only the
 * Instrument row(s) selected by scene_mask/instrument_slot from committed
 * resident state, and streams the variable-length file back before callback.
 *
 * Why the complete file is borrowed: `.hcnames` lines are trimmed, so changing
 * one name can change its byte length and cannot safely be overwritten at a
 * fixed byte offset. Unrelated rows are preserved from the file. No additional
 * SRAM array is allocated: both operations temporarily reuse the single
 * `fs_list_cache_name[1000][9]` allocation normally occupied by `.hcindex`.
 * A multi-Scene normal Instrument Load may set several scene_mask bits; a Save
 * passes one bit. All calls are asynchronous and return false when busy or when
 * coordinates are invalid.
 */
```

With:
```c
/*
 * Resident Instrument name-register access.
 *
 * What: the load request reads root `/.hcnames` into the dedicated 1,449-byte
 * `hcnames_name_mirror` so Menu can copy one Scene/voice name on nested
 * Instrument Load or Save entry. The update request performs the same read,
 * replaces only the Instrument row(s) selected by scene_mask/instrument_slot
 * from committed resident state, and streams the variable-length file back
 * before callback.
 *
 * Why the complete file is borrowed: `.hcnames` lines are trimmed, so changing
 * one name can change its byte length and cannot safely be overwritten at a
 * fixed byte offset. Unrelated rows are preserved from the file. No additional
 * SRAM array is allocated: both operations use the dedicated
 * `hcnames_name_mirror`, not the shared `fs_list_cache_name` browser cache.
 * A multi-Scene normal Instrument Load may set several scene_mask bits; a Save
 * passes one bit. All calls are asynchronous and return false when busy or when
 * coordinates are invalid.
 */
```

---

## Change 4 — Comment correction: `filesystem.h` lines 946–962

**File:** `Core/Hardware/SD/filesystem.h`
**Location:** lines 946–962 (the "Resident Kit name-register access" comment
block above `filesystem_requestLoadResidentKitName`)
**Action:** MODIFY — correct the stale shared-cache reference

### Current text (lines 949–951)

```
 * The load request mirrors Instrument menu entry: it borrows the generalized
 * cache for all 161 root HCNAMES rows so Menu can copy one resident Scene's Kit
 * name plus all six Instrument names before `/Kit/.hcindex` replaces that same
 * allocation.
```

### Replacement block (lines 946–962)

Replace:
```c
/*
 * Resident Kit name-register access.
 *
 * The load request mirrors Instrument menu entry: it borrows the generalized
 * cache for all 161 root HCNAMES rows so Menu can copy one resident Scene's Kit
 * name plus all six Instrument names before `/Kit/.hcindex` replaces that same
 * allocation. Menu retains those seven rows for the complete combined
 * Kit/Instrument session. Loads and saves only update the Menu scratch and an
 * accumulated dirty-Scene mask; they do not reopen HCNAMES. At session exit,
 * one update request replaces exactly the Kit row plus all six Instrument rows
 * for every bit in scene_mask from committed resident state, while preserving
 * every other logical row read from the variable-length file. A successful
 * request makes those seven rows
 * authoritative even when the source Scene's Bank-present bit is clear; this
 * prevents a valid Kit Save from serializing its new Kit name as a blank row.
 * Both requests are asynchronous, return false for busy/invalid input, and
 * allocate no additional persistent SRAM.
 */
```

With:
```c
/*
 * Resident Kit name-register access.
 *
 * The load request reads `/.hcnames` into the dedicated `hcnames_name_mirror`
 * so Menu can copy one resident Scene's Kit name plus all six Instrument names
 * before `/Kit/.hcindex` replaces the shared `fs_list_cache_name` browser
 * cache. Menu retains those seven rows for the complete combined
 * Kit/Instrument session. Loads and saves only update the Menu scratch and an
 * accumulated dirty-Scene mask; they do not reopen HCNAMES. At session exit,
 * one update request replaces exactly the Kit row plus all six Instrument rows
 * for every bit in scene_mask from committed resident state, while preserving
 * every other logical row read from the variable-length file. A successful
 * request makes those seven rows
 * authoritative even when the source Scene's Bank-present bit is clear; this
 * prevents a valid Kit Save from serializing its new Kit name as a blank row.
 * Both requests are asynchronous, return false for busy/invalid input, and
 * allocate no additional persistent SRAM.
 */
```

---

## Change 5 — Comment correction: `filesystem_requestUpdateResidentKitNames()`

**File:** `Core/Hardware/SD/filesystem.c`
**Location:** lines 30483–30497 (the comment block inside
`filesystem_requestUpdateResidentKitNames`)
**Action:** MODIFY — correct the stale shared-cache reference

### Current text (lines 30490–30497)

```
     * Output: `/.hcnames` is read into the existing generalized cache, exactly
     * one Kit row and six Instrument rows per selected Scene are replaced from
     * committed resident state, and the variable-length file is rewritten
     * through the normal close/flush gate. Every unrelated logical row is
     * preserved from the file. One exit request can therefore commit several
     * actions and several Scenes without retaining a 16-by-7 name array. The
     * request reuses the existing operation mask, line buffer, and general
     * cache, so it adds no persistent SRAM storage.
```

### Replacement text

```
     * Output: `/.hcnames` is read into the dedicated `hcnames_name_mirror`,
     * exactly one Kit row and six Instrument rows per selected Scene are
     * replaced from committed resident state, and the variable-length file is
     * rewritten through the normal close/flush gate. Every unrelated logical
     * row is preserved from the file. One exit request can therefore commit
     * several actions and several Scenes without retaining a 16-by-7 name
     * array. The request reuses the existing operation mask, line buffer, and
     * the dedicated mirror, so it adds no persistent SRAM storage.
```

---

## Change 6 — Comment correction: `filesystem_residentNames_tick()`

**File:** `Core/Hardware/SD/filesystem.c`
**Location:** lines 6601–6614 (the top-of-function comment block inside
`filesystem_residentNames_tick`)
**Action:** MODIFY — correct the stale "generalized cache" reference

### Current text (lines 6605–6607)

```
     * Load mode is used on Instrument or Kit menu entry: it fills the existing
     * generalized cache and lets Menu copy one selected row before requesting
     * the appropriate `.hcindex`.
```

### Replacement text

```
     * Load mode is used on Instrument or Kit menu entry: it fills the
     * dedicated `hcnames_name_mirror` and lets Menu copy one selected row
     * before requesting the appropriate `.hcindex`.
```

---

## Change 7 — Comment correction: `menu_switchPage()` exit block

**File:** `Core/Menu/menu.c`
**Location:** lines 13319–13329 (the `end_resident_name_session` comment
inside `menu_switchPage`)
**Action:** MODIFY — add the "must not lock" rationale

### Current comment (lines 13321–13329)

```c
        /*
         * Detach page repaint from HCNAMES persistence.
         *
         * What: discard only the browser/session view and retain the dirty
         * Scene mask. Why: the destination page must become visible on this
         * pass; filesystem_tick() will schedule the existing atomic HCNAMES
         * rewrite once the facade is idle. Inputs: the pre-switch dirty mask.
         * Outputs: no new buffer, no identity mutation, and no page-exit wait.
         * Affiliates: menu_triggerDeferredHcnamesFlush() and the next idle
         * filesystem scheduler rung.
         */
```

### Replacement comment

```c
        /*
         * Detach page repaint from HCNAMES persistence.
         *
         * What: discard only the browser/session view and retain the dirty
         * Scene mask. Why: the destination page must become visible on this
         * pass, and the deferred write must not lock the Menu. The ~3 s card
         * write that filesystem_tick() schedules once the facade is idle runs
         * as a background operation: it holds the filesystem (as AutoSave
         * does) but never raises menu_storageBusy, so mode buttons, encoder,
         * pots, runtime widgets, and copy/clear remain responsive throughout.
         * Inputs: the pre-switch dirty mask.
         * Outputs: no new buffer, no identity mutation, and no page-exit wait.
         * Affiliates: menu_triggerDeferredHcnamesFlush(),
         * menu_residentNameDeferredFlushComplete(), and the next idle
         * filesystem scheduler rung.
         */
```

---

## Summary table

| # | File | Lines | Action | What |
|---|------|-------|--------|------|
| 1 | `Core/Menu/menu.c` | after 5642 | ADD | New static `menu_residentNameDeferredFlushComplete()` |
| 2 | `Core/Menu/menu.c` | 5695–5710 | MODIFY | Rewrite `menu_triggerDeferredHcnamesFlush()` body |
| 3 | `Core/Hardware/SD/filesystem.h` | 903–920 | MODIFY | Correct Instrument comment: mirror, not shared cache |
| 4 | `Core/Hardware/SD/filesystem.h` | 946–962 | MODIFY | Correct Kit comment: mirror, not shared cache |
| 5 | `Core/Hardware/SD/filesystem.c` | 30483–30497 | MODIFY | Correct update-request comment: mirror, not cache |
| 6 | `Core/Hardware/SD/filesystem.c` | 6601–6614 | MODIFY | Correct tick comment: mirror, not generalized cache |
| 7 | `Core/Menu/menu.c` | 13319–13329 | MODIFY | Add "must not lock" rationale to exit block comment |

**No changes to:** `menu_endResidentNameScratchSession()`,
`menu_residentNameScratchFlushComplete()`, `menu_hasResidentNameDirtyMask()`,
`filesystem_tick()`, `filesystem_requestUpdateResidentKitNames()` (body),
`filesystem_residentNames_tick()` (body), `menu.h`, `buttonHandler.c`,
`copyClearSession.c`, or any other file.

**No new RAM.** No new header declarations (the new callback is static).
No ISR or DSP path. No file-format change. No change to the write's content
or duration. No change to in-session Load page foreground flushes.

**Build verification:** `make all && make img` must succeed with no warnings.
`python3 tools/link_budget.py arm-none-eabi-nm build/lxr02.elf` must report
the same BSS and data as the S076 close build; flash text will grow by the
new function body minus the removed `menu_endResidentNameScratchSession()`
delegation (net delta: a few tens of bytes).

---

## Implementation Log (2026-10-07)

### Applied — all seven planned changes

| # | File | Action | Result |
|---|------|--------|--------|
| 1 | `Core/Menu/menu.c` | ADD `menu_residentNameDeferredFlushComplete()` | Done. Inserted after `menu_residentNameScratchFlushComplete()` (now lines 5644–5705), immediately before `menu_endResidentNameScratchSession()`. Verbatim from the plan. |
| 2 | `Core/Menu/menu.c` | MODIFY `menu_triggerDeferredHcnamesFlush()` | Done (now lines 5758–5810). Body now calls `filesystem_requestUpdateResidentKitNames(menu_residentNameDirtySceneMask, menu_residentNameDeferredFlushComplete)`; it no longer calls `menu_endResidentNameScratchSession()` and never touches `menu_storageBusy`. |
| 3 | `Core/Hardware/SD/filesystem.h` | Instrument comment | Done (lines 903–921). Now names the dedicated `hcnames_name_mirror`, not `fs_list_cache_name`. |
| 4 | `Core/Hardware/SD/filesystem.h` | Kit comment | Done (lines 946–963). Same correction. |
| 5 | `Core/Hardware/SD/filesystem.c` | `filesystem_requestUpdateResidentKitNames()` comment | Done (lines 30490–30498). Now says the read goes into `hcnames_name_mirror`. |
| 6 | `Core/Hardware/SD/filesystem.c` | `filesystem_residentNames_tick()` comment | Done (lines 6605–6608). Now says it fills `hcnames_name_mirror`. |
| 7 | `Core/Menu/menu.c` | `menu_switchPage()` exit-block comment | Done (lines 13422–13435). Adds the "must not lock the Menu" rationale and names the new callback. |

### Applied — one extra comment-block edit under the project convention

The `.c`/`.h` comment-block convention requires the header declaration of a
public function to describe the same contract as its `.c` definition. Change 2
rewrote the public `menu_triggerDeferredHcnamesFlush()`, so its block in
`Core/Menu/menu.h` (lines 583–595) was updated too. The plan's summary said
`menu.h` was unchanged; that omission would have left the header naming the old
callback and the old (locking) semantics. The header now states the deferred
write holds the filesystem but must not raise `menu_storageBusy`, and names
`menu_residentNameDeferredFlushComplete()`. No declarations were added (the new
callback is `static`), so this is comment text only.

### Line-number note

The plan's line references were pre-edit positions. After Change 1 shifted
`menu.c` by +63 lines, Changes 2 and 7 landed at 5758 and 13422. In
`filesystem.c` the two comment blocks were at 30490 (not 30483) and 6605 (not
6601); the `sed` location text was correct, only the numbers had drifted.

### Build result — PASS

`make all && make img` completed with exit 0. No warnings from `menu.c` or
`filesystem.c` under `-Wall -Wextra`. The only diagnostics are pre-existing:
the `pat_addrPtr` packed-member warning (`PatternData.c:200`) and the
`libc_nano` `_close`/`_lseek`/`_read`/`_write` "not implemented" notes.

```
   text     data     bss      dec      hex   filename
 538640      420  427008   966068   ebdb4   build/lxr02.elf
Flash : 539,060 / 753,664 B used, headroom 214,604 B
ITCM  : 4,168 / 16,384 B
DTCM  : statics 4,472 B
FXBUF : 126,592 B at 0x20001180 (min 122,880, margin 3,712)
Written: build/LXRV2_lxr02.img (539060b) OK
```

**RAM:** no delta from P3. The new function has one 1-byte local and no static
or global storage, so `data` and `bss` are unchanged by this change. (The
`data`/`bss` totals above differ from the S076-close figures because S077
P1/P2 are also in this tree: P1 adds the expected +256 B BSS, plus alignment.)

**Flash:** text grows by the new function body minus the removed
`menu_endResidentNameScratchSession()` delegation. The exact single-change
delta was not isolated on hardware-style builds in this session; the plan's
"a few tens of bytes" estimate stands, and the image remains well inside the
736 KiB window with 214,604 B headroom.

### Status

Implementation complete and build-verified. **Hardware verification is
pending** — it is the user's to run against `S077_P3_BLANK_MENU_AFTER_SCN_LOAD.md`
§10. Changes are uncommitted; the user manages commits.

---

## Implementation Assessment (review, 2026-10-07)

**Verdict:** the two functional changes are correct and match the design in
`S077_P3_BLANK_MENU_AFTER_SCN_LOAD.md` §5. The background write no longer
raises `menu_storageBusy`. That removes both reported symptoms (blank VOICE
page, ~3 s encoder lock) and the pot, widget and copy/clear locks.

The review found **one new reachable defect** (A1): a spurious FsErr overlay
when the user re-enters Load/Save during the write and crosses a
name-session boundary. It is cosmetic and causes no data loss, but it will
show up in testing. A one-condition fix was proposed and has since been
applied (see "A1 fix — applied"). There are also minor comment and log
corrections (A2, A3).

### Verified

| Check | Result |
|---|---|
| Diff scope | `menu.c` (3 hunks), `menu.h` (comment), `filesystem.c` and `filesystem.h` (comments only). No other source touched. |
| `menu_triggerDeferredHcnamesFlush()` | Same gates as before (mask nonzero, not LOAD/SAVE; `filesystem_tick()` adds idle facade and `!cc_suspended`). Posts `filesystem_requestUpdateResidentKitNames(mask, menu_residentNameDeferredFlushComplete)`. Never touches `menu_storageBusy`. |
| Refusal while the filesystem is idle | `filesystem_start()` refuses an idle facade only during a copy/clear name-buffer loan. `ccSvc_busy()` includes `ccSvc_buf != 0`, so `copyClear_backgroundSuspended()` already stops the trigger for the whole loan. A silent retry on the next idle pass is correct. |
| Callback timing | `filesystem_complete()` sets the terminal status and calls the callback synchronously. No Menu retry can run between DONE and the callback, so `menu_deferSelectionRequest` retries always see the mask already cleared. |
| `menu_residentNameDeferredFlushComplete()` | Reads the result before `filesystem_ack()`. Acks exactly once. Success clears the mask; failure keeps it and shows the overlay (which acks again, a harmless no-op). Does not touch `menu_storageBusy`, the cache, browser requests or repaint. Matches the spec. |
| Mask clear on success | Exact: no new bits can arrive during the write, because every marking completion needs the filesystem. |
| Exit block | `menu_switchPage()` still invalidates the scratch and clears the browser cache before the write (now `menu.c:13436–13439`). |
| Load/Save re-entry (no boundary crossed) | Kit (`menu_requestKitEntryNames()` → `menu_requestResidentNameScratch()`), Scene (`menu_requestSceneEntryName()`) and Bank/Pattern (`menu_requestLibraryIndexLoad()`) all arm `menu_deferSelectionRequest` while the filesystem is busy and clear any `menu_storageBusy` they raised. The retry runs after the write. As designed. |
| Build | `make -W Core/Menu/menu.c -W Core/Hardware/SD/filesystem.c all` recompiled both files: `text 538,640 / data 420 / bss 427,008`, the same as the log. **`menu.c`: no warnings.** `menu_residentNameDeferredFlushComplete` is present (LTO-private); the trigger is inlined into `filesystem_tick()`. |
| RAM | No new statics or globals. One 1-byte local. |

### A1 — Spurious FsErr overlay on Load-page boundaries during the write (new, reachable)

**Mechanism.** These in-session boundaries call
`menu_endResidentNameScratchSession()` whenever
`menu_residentNameDirtySceneMask != 0u`:

| Site | Boundary |
|---|---|
| `menu.c:11184` | Top-row type change leaving Kit/KitMrp |
| `menu.c:11220` | Top-row type change leaving Scene |
| `menu.c:7759` | Pot-1 type selection to a non-Kit type (`menu_loadSaveEnterTop()`) |
| `menu.c:7835`, `7885` | Pot-1 entry into nested Instrument Load / Save |
| `menu.c:7522`, `7419`, `6812`, `6826`, `6863` | Nested Instrument voice press, exit, type steps |

During the background write the mask is still nonzero; it is cleared only
in the callback. So the helper:

1. sets `menu_storageBusy = 1`;
2. calls `filesystem_requestUpdateResidentKitNames()`, which is refused
   because the filesystem is busy;
3. records a FAILED trace, clears `menu_storageBusy` and **calls
   `menu_showFilesystemErrorOverlay()`**.

**Reachable sequence (typical):**

1. Scene Load.
2. PERF.
3. LOAD/SAVE within ~3 s. The page now opens; this was the purpose of the fix.
4. Turn the encoder on the type row (Scene → anything else), or use Pot-1 to
   pick another type.
5. **FsErr overlay.**

**Why it was not reachable before:** the old write held
`menu_storageBusy`, so the LOAD/SAVE press in step 3 was dropped. The bug
doc's §6.2 analysed browser entry (which defers cleanly) but missed these
boundary calls. That is a gap in the review analysis, not in the
implementation of the spec.

**Consequence.**

- The overlay is misleading: no card error occurred. It clears after the
  test-result timer and repaints on the Load page.
- There is no data loss. The helper returns 0, the caller continues on its
  normal path, the new browser request is refused and deferred, the write
  in flight finishes and clears the mask, and the retry reads the fresh
  `/.hcnames`.
- The in-flight write is already serializing exactly the bits the boundary
  wanted to flush. The in-RAM name block cannot change while the filesystem
  is busy, so the boundary's integrity purpose is met.

**Proposed fix (minimal, no RAM, one guard in one function).** In
`menu_endResidentNameScratchSession()`, after the two mask checks and
before `menu_storageBusy = 1u`:

- If `filesystem_status() == FS_STATUS_BUSY`, return 0 with no overlay,
  leaving the mask, scratch and `menu_storageBusy` unchanged.
- The DEV trace record may be kept, with the FAILED flag, so the event stays
  visible in the trace.

Every caller already handles a 0 return: today's refusal path returns 0
after the overlay, and the callers then continue into a request that defers
on a busy filesystem. So **behaviour changes only by removing the overlay
in the busy case**. A genuine refusal while the filesystem is idle (none is
expected; see "Refusal while the filesystem is idle" above) still shows it.

**Note (pre-existing, out of scope):** the same busy refusal happens today
when AutoSave holds the filesystem at one of these boundaries, and it has
the same side effect. The boundary flush is skipped and Scene bits are
carried forward in the mask. The comment at `menu.c:11198–11212` warns that
a later Kit Load could then overwrite the in-RAM name block before those
bits are written. The proposed guard does not change that exposure (it only
removes the overlay). With the deferred write, the carried bits are cleared
by its callback on success, so this case adds no new exposure.

**Test addition** (bug doc §10): Scene Load → PERF → LOAD/SAVE within ~1 s:

- turn the type row off Scene;
- Pot-1 to Bank;
- enter nested Instrument Load from Kit.

Expected after the A1 fix: no FsErr. Names fill in after the write.

### A2 — Comment references (minor)

- `menu_residentNameDeferredFlushComplete()` says "Change 2 below". That is
  this document's numbering and means nothing in source. Replace it with
  `menu_triggerDeferredHcnamesFlush()`.
- Hard line numbers in the new comments have already drifted:

  | Cited | Now |
  |---|---|
  | `menu_switchPage()` line 13334 | `13439` |
  | lines 13331–13334 | `13436–13439` |
  | `filesystem_tick()` lines 25621/25623 | `25623`/`25624` |
  | overlay "line 5069 ff." | function at `5055`, ack at `5099` |

  Prefer function names alone, which is the convention most of the
  surrounding comments follow.

### A3 — Implementation-log correction (minor)

- The log says "No warnings from `menu.c` or `filesystem.c`". `menu.c` is
  clean, but recompiling `filesystem.c` emits seven `-Wunused-function`
  warnings, for example `filesystem_bootLoggingSetKitDetail`,
  `filesystem_blockRename` and `filesystem_applyStaleGlobalsFallback`.
- `filesystem.c`'s P3 changes are comments only, so these come from code P3
  didn't touch. Read that log line as "no new warnings from P3".
- Flash delta for P3 alone is still not isolated, as the log states.

### A1 fix — applied (2026-10-07)

**File:** `Core/Menu/menu.c`, `menu_endResidentNameScratchSession()`.

**Change:** one guard inserted after the two mask checks and before
`menu_storageBusy = 1u`. If `filesystem_status() == FS_STATUS_BUSY`, the
function:

- records the existing DEV trace milestone with the FAILED flag
  (`AUTOSAVE_TRACE_INSTRUMENT_ENTRY_PHASE_HCNAMES_FLUSH`, 1);
- returns 0;
- leaves the dirty mask, the name scratch and `menu_storageBusy` untouched;
- shows **no** error overlay.

A comment block in the surrounding style states the what, why, inputs,
output and affiliates.

**Behaviour:**

- **Busy filesystem:** the only difference from before is that the
  misleading FsErr overlay is gone. Callers already continue on their 0
  path, and their next browser request defers on the busy filesystem and
  retries after the write.
- **Idle filesystem:** the update request is attempted exactly as before. A
  genuine refusal on that path still shows the overlay.
- **Successful start:** unchanged.

**Build:** `make all && make img` passed. `menu.c` has no warnings.

```
   text     data     bss      dec      hex   filename
 538696      420  427008   966124    ebdec   build/lxr02.elf
Written: build/LXRV2_lxr02.img (539116b) OK
```

- Text +56 B against the pre-A1 build (538,640). Data and BSS unchanged.
- No new RAM: the guard reads existing state only.

### Status

- **A1 fixed.**
- A2 (comment references) and A3 (log wording) are still open. Both are
  cosmetic and change no behaviour.
- Hardware verification is pending, against
  `S077_P3_BLANK_MENU_AFTER_SCN_LOAD.md` §10 plus the A1 test above:
  Scene Load → PERF → LOAD/SAVE within ~1 s, then type-row change off
  Scene, Pot-1 to Bank, and nested Instrument Load entry from Kit.
  Expected: no FsErr, and names fill in after the write.
- Changes are uncommitted; the user manages commits.
