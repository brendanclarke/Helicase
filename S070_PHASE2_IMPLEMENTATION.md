# S070 Phase 2 — Load/Save Revision Implementation Schedule

Session: S070
Branch: `dev-ph5-effects`
Source: `S070_PHASE2_LOAD_SAVE_REVISION.md` (all design decisions resolved)

---

## Overview

This document is the line-by-line code implementation schedule for the four
architecturally coupled LSR items. Implementation follows the order specified
in the plan's Execution Strategy (§ Execution Strategy, steps 3–5):

1. **Step 3** — Selection-coordinate model (LSR-03 + LSR-04)
2. **Step 4** — HCNAMES checkpoint (LSR-01)
3. **Step 5** — Detach page exit from HCNAMES persistence (LSR-02)

Steps 1–2 of the Execution Strategy (trace infrastructure and R3 hardware
validation) are already complete. Step 6 (test-item fold) is a documentation
task, not a code change.

Each change entry specifies: file, line range, operation (ADD/MODIFY/REMOVE),
what it does, why it exists, inputs/outputs, common accessors, and affiliate
code.

## Implementation Notes — 2026-09-22

### Audit decisions before code changes

- The repository already implements the Morph entry snapshot through the
  existing `.hctmp` path: `menu_prepareInstrumentLoadTemp()` dispatches the
  Morph-specific temporary save, and the corresponding Morph temporary-load
  completion restores only Morph endpoint data. No additional Morph snapshot
  buffer or Normal-identity update is required.
- The two new deferred-HCNAMES scheduler accessors will live in `menu.h`,
  alongside the other Menu-owned public accessors. `filesystem.c` already
  includes `menu.h`; this keeps ownership with Menu while still giving the
  filesystem scheduler a narrow boundary.
- Live Kit/Instrument payload requests currently set `menu_storageBusy`, and
  `menu_switchPage()` uses that flag before clearing the shared cache. After
  removing the input lock, page exit must also test `preset_getStatus()` so an
  in-flight Preset operation reaches its terminal boundary before cache/domain
  teardown.
- Existing normal Instrument and Morph completion paths already distinguish
  identity mutation: only normal Instrument completion calls
  `menu_refreshResidentNameScratchInstrument()`. Morph completion routes only
  through the Morph apply worker.

Implementation is beginning with the selection-generation and blank/Empty
changes, followed by the HCNAMES scheduler and page-exit detachment. Each
source change will retain an adjacent contract comment in both the `.c` and
`.h` boundary where a public API is introduced.

### Code pass 1 — landed before build

- Added the two-byte selection generation/snapshot state and capture points for
  top-level, Bank-preview, typed-Instrument, HCNAMES, test-scan, and direct
  Scene/index requests. Numeric Kit/Instrument scrolling increments the live
  generation; type changes, page exits, and accepted OK commits invalidate
  older callbacks.
- Added stale-coordinate handling to the direct library-index and Bank-preview
  callbacks, including facade acknowledgement and latest-selection retry. The
  library index remains a domain-wide result, so a stale coordinate causes the
  cache to be discarded and re-resolved rather than publishing the old row.
- Removed the Menu input lock from accepted live Kit/KitMrp/Instrument payload
  selection requests. `menu_parseEncoder()` still admits only numeric live-load
  turns while Preset is active, preventing cache/type teardown or OK from
  racing an in-flight payload.
- Added the blank/`Empty` split to all four numbered-library name accessors and
  disabled Load-page OK while the displayed coordinate is all-space.
- Added `menu_hasResidentNameDirtyMask()` and
  `menu_triggerDeferredHcnamesFlush()` in `menu.c`/`menu.h`, plus the higher-
  priority idle scheduler rung in `filesystem.c`. Load/Save page exit now
  tears down the browser immediately and retains the dirty mask for that rung;
  pending page exits no longer re-enter a browser when the HCNAMES completion
  callback returns.
- Added the Preset-status page-exit guard and skipped a new `kit` restore when
  an exit is already pending, preserving the required in-flight-load → HCNAMES
  checkpoint → page-transition ordering. Existing Morph temp snapshot and
  identity isolation paths remain unchanged.

### Code pass 2 — callback/exit hardening

- Captured the generation again at direct Kit/Scene index follow-on requests,
  so a completed HCNAMES entry cannot reuse an older request tag after the
  user scrolls during entry.
- Prevented pending physical exits from re-entering Kit, Scene, or Instrument
  browser indexes in direct completion callbacks. Terminal HCNAMES errors now
  release Menu's busy flag while retaining the dirty mask for retry.
- Added the same safe release to failed HCNAMES entry reads and removed the
  redundant Bank-preview stale-result comment; these are source-only hygiene
  changes with no additional RAM.

### Verification — source/build/image

- `git diff --check`: PASS.
- `make all`: PASS. The explicit `all` target was used because this Makefile's
  `-include $(OBJS:.o=.d)` appears before `all`, so an unqualified `make`
  selects the first dependency target (`build/main.o`) instead of the firmware
  image. The complete link reports `text=451,532`, `data=416`, `bss=291,756`.
- `make img`: PASS. `build/LXRV2_lxr02.img` was generated successfully at
  451,964 bytes.
- No hardware validation has been run in this workspace. The remaining checks
  are the Load/Save interaction matrix: rapid live Kit/Instrument scrolling,
  stale Bank preview discard, blank-versus-`Empty` OK behavior, HCNAMES
  checkpoint timing across Kit/Instrument/type/page transitions, and re-entry
  before the deferred HCNAMES write completes.

---

## RAM Budget

| Item | Size | Region | Owner |
|------|------|--------|-------|
| `menu_selectionGeneration` + request snapshot | 2 bytes | normal SRAM1 `.bss` | `menu.c` |

Total new retained allocation: **2 bytes**. No other new globals, buffers,
or retained state. The deferred HCNAMES scheduler reuses the existing
`menu_residentNameDirtySceneMask` (2 bytes, already allocated) and the
existing `filesystem_requestUpdateResidentKitNames()` infrastructure. The
KitMrp/InstrumentMrp entry-snapshot mechanism (`.hctmp`) is already
implemented; its Morph extension is deferred to a later session per the plan's
Remaining Open Items §2.

---

## File Change Index

| File | Changes |
|------|---------|
| `Core/Menu/menu.c` | Generation counter, async load dispatch/dispose, blank/Empty discipline, OK disable, flush trigger at domain transitions, page-exit detachment |
| `Core/Hardware/SD/filesystem.c` | Deferred HCNAMES scheduler rung, slot-name blank/Empty split |
| `Core/Menu/menu.h` | Public bridge for deferred HCNAMES dirty-mask scheduling |
| `Core/Hardware/SD/filesystem.h` | Blank/`Empty` slot-name contract for numbered caches |

---

## 1. Selection-Coordinate Model (LSR-03 + LSR-04)

### 1.1 ADD: Generation counter declaration — `menu.c:119`

**Operation:** ADD one line after `menu_deferSelectionRequest` (line 118).

```c
static uint8_t menu_selectionGeneration = 0u;
```

**What:** A single wrapping `uint8_t` counter incremented on every encoder
scroll movement and every browser type switch within the Load/Save page.
Every async filesystem request (index load, Bank preview scan, name lookup,
Kit/Instrument payload load) captures this value at request time. The
completion callback compares its captured generation against the current
value; a mismatch silently discards the result. The live counter and one
shared request snapshot are two retained bytes; no per-request buffer exists.

**Why:** Without a generation tag, a stale callback (e.g., a Bank preview
returning for slot 003 after the user scrolled to slot 007) overwrites the
current display. The facade's single-owner model prevents concurrent
filesystem requests, but the completion callback for the *previous* request
can still arrive after the user has scrolled past. The generation counter
makes stale detection O(1) with no additional per-request state beyond the
file-scope snapshot variable. The snapshot is valid because the filesystem
facade admits only one asynchronous owner at a time.

**Inputs:** None (initialised to zero).
**Outputs:** Incremented by scroll and type-switch sites; captured and
compared by async callbacks.
**Accessors:** `menu_requestCurrentLoadSaveSelection()`,
`menu_requestBankLoadPreview()`, all Kit/Instrument/Scene/Bank/Pattern
async completion callbacks.
**Affiliates:** `menu_deferSelectionRequest` (line 118),
`menu_storageBusy` (line 89).

---

### 1.2 ADD: Generation snapshot variable — `menu.c:120`

**Operation:** ADD one line after the generation counter.

```c
static uint8_t menu_selectionGenerationSnapshot = 0u;
```

**What:** Captures `menu_selectionGeneration` at the moment a filesystem
request is posted. Completion callbacks compare this snapshot against the
live counter. If they differ, the result is stale and is discarded.

**Why:** C closures are not available; the snapshot variable serves as the
callback's captured generation. Only one async request is in flight at a
time (facade single-owner), so one snapshot variable is sufficient.

**Inputs:** Written at request time.
**Outputs:** Read at callback time.
**Affiliates:** `menu_selectionGeneration` (§1.1).

---

### 1.3 MODIFY: Increment generation on encoder scroll — `menu.c:9440`

**Operation:** MODIFY the `if (inc != 0)` block at line 9440 in
`menu_handleLoadSaveMenu()`, SAVE_STATE_EDIT_PRESET_NR case.

**Before:** The block immediately calls `menu_requestCurrentLoadSaveSelection()`.
**After:** Insert `menu_selectionGeneration++;` as the first statement inside
the `if (inc != 0)` block.

```c
            if (inc != 0) {
                menu_selectionGeneration++;
                if (menu_activePage == LOAD_PAGE) {
                    menu_requestCurrentLoadSaveSelection(1);
                } else {
                    menu_requestCurrentLoadSaveSelection(0);
                }
            }
```

**What:** Advances the generation counter on every encoder detent that
changes the selected slot number on the Load/Save preset-number row.

**Why:** Each scroll position is a distinct selection coordinate. Callbacks
from earlier positions must not overwrite results for the current position.
The increment is placed before the request so the request captures the
*new* generation.

**Inputs:** `inc != 0` (encoder moved).
**Outputs:** `menu_selectionGeneration` incremented by one.
**Affiliates:** `menu_currentPresetNr[]` update at line 9439,
`menu_requestCurrentLoadSaveSelection()` at line 9445.

---

### 1.4 MODIFY: Increment generation on type switch — `menu.c:9338`

**Operation:** MODIFY the type-change block in `menu_handleLoadSaveMenu()`,
SAVE_STATE_EDIT_TYPE case (line 9338). Insert `menu_selectionGeneration++;`
immediately after the `menu_saveOptions.what` assignment confirms a real
type change.

**Location:** Inside the `if (menu_saveOptions.what != previous_type && ...)` block,
after the existing `menu_endResidentNameScratchSession()` call sequence
(around line 9380), just before `menu_requestCurrentLoadSaveSelection()`.

```c
                menu_selectionGeneration++;
```

**What:** Advances the generation counter when the user changes browser type
(Kit → Scene, Scene → Bank, etc.) on the Load/Save page.

**Why:** A type switch invalidates all pending callbacks from the previous
browser domain. The cache domain changes, and any in-flight name or preview
result belongs to the old domain.

**Inputs:** `menu_saveOptions.what != previous_type`.
**Outputs:** `menu_selectionGeneration` incremented.
**Affiliates:** The type-switch cleanup sequence at lines 9338–9388,
`menu_endResidentNameScratchSession()` at line 9341/9377.

---

### 1.5 MODIFY: Capture generation at each accepted request site — `menu.c`

**Operation:** MODIFY the accepted-request branches rather than capturing at
the top of `menu_requestCurrentLoadSaveSelection()`. A top-level helper can
refuse a request, publish a synchronous name, or enter a direct HCNAMES/index
chain, so the snapshot is assigned only after the actual Preset/direct request
is accepted. The direct Kit/Scene follow-on index requests also capture their
generation immediately before posting.

```c
    menu_selectionGenerationSnapshot = menu_selectionGeneration;
```

**What:** Captures the current generation at the moment an asynchronous
selection request is accepted. Refused/deferred paths do not overwrite the
snapshot belonging to the in-flight request.

**Why:** The snapshot must be taken at acceptance time, not at callback time,
because the user may scroll further between dispatch and completion. Capturing
only after acceptance also prevents a refused retry from changing the tag for
another request already in flight.

**Inputs:** `menu_selectionGeneration` (live counter).
**Outputs:** `menu_selectionGenerationSnapshot` (captured value for callbacks).
**Affiliates:** `menu_requestTestScan()`, the accepted Kit/KitMrp/name-load
branches, `menu_requestLibraryIndexLoad()`, the direct Kit/Scene follow-on
index callbacks, `menu_requestBankLoadPreview()`, and the typed Instrument
payload/index helpers.

---

### 1.6 MODIFY: Capture generation in Bank preview request — `menu.c:5433`

**Operation:** MODIFY `menu_requestBankLoadPreview()` (line 5418). Add one
line at the top of the function body, before `menu_bankLoadPreviewSlot = slot;`
(line 5433):

```c
    menu_selectionGenerationSnapshot = menu_selectionGeneration;
```

**What:** Captures the current generation for the Bank child-Scene preview
scan. The completion callback (`menu_bankLoadPreviewComplete()`) checks this
snapshot before publishing the preview mask and repainting SEQ LEDs.

**Why:** If the user scrolls to a different Bank slot before the preview
returns, the old slot's child mask must not be published.

**Inputs:** `menu_selectionGeneration`.
**Outputs:** `menu_selectionGenerationSnapshot`.
**Affiliates:** `menu_bankLoadPreviewComplete()` (line ~5157).

---

### 1.7 MODIFY: Check generation in Bank preview completion — near `menu.c:5157`

**Operation:** MODIFY `menu_bankLoadPreviewComplete()`. Add a generation
check at the top, before publishing the preview mask:

```c
    if (menu_selectionGenerationSnapshot != menu_selectionGeneration) {
        filesystem_ack();
        menu_storageBusy = 0u;
        return;
    }
```

**What:** Discards a stale Bank preview result. The filesystem facade is
acknowledged and storage is released so the next request can proceed.

**Why:** A Bank preview scan takes multiple ticks. If the user scrolled past
this slot, publishing its child mask would show incorrect SEQ LEDs.

**Inputs:** Captured snapshot vs. live generation counter, filesystem status.
**Outputs:** Silent discard on mismatch; normal publication on match.
**Affiliates:** `menu_requestBankLoadPreview()` (§1.6),
`menu_refreshLoadSceneLeds()`.

---

### 1.8 MODIFY: Check generation in library index load completion

**Operation:** MODIFY `menu_libraryIndexLoadComplete()` (near line 4457).
Add a generation check at the top, after the filesystem status check:

```c
    if (menu_selectionGenerationSnapshot != menu_selectionGeneration) {
        filesystem_ack();
        menu_storageBusy = 0u;
        if (menu_deferSelectionRequest)
            menu_requestCurrentLoadSaveSelection(menu_deferSelectionLoadKit);
        return;
    }
```

**What:** Discards a stale library index load. If a deferred selection is
pending (the user scrolled while the index was loading), the deferred
request is re-posted with the current generation.

**Why:** A `.hcindex` load for the previous browser type or slot must not
overwrite the display.

**Inputs:** Snapshot vs. live counter, deferred-selection flag.
**Outputs:** Silent discard or deferred retry on mismatch.
**Affiliates:** `menu_requestCurrentLoadSaveSelection()` (§1.5).

---

### 1.9 MODIFY: Check generation in Kit Load completion — `menu.c:~10284`

**Operation:** MODIFY the Kit Load completion path in
`menu_pollPresetStatus()` where `menu_refreshResidentNameScratchKit()` is
called (line 10284). After the refresh, before the final repaint/release,
add a generation check:

```c
    if (menu_selectionGenerationSnapshot != menu_selectionGeneration) {
        /* Stale Kit load completed. The payload was applied to DSP/resident
         * state by Preset's commit path, but the user has moved on. The name
         * and slot displayed come from the current selection, not this
         * completion's captured slot. Release storage and let the deferred
         * selection proceed. */
        menu_storageBusy = 0u;
        if (menu_deferSelectionRequest)
            menu_requestCurrentLoadSaveSelection(1);
        return;
    }
```

**What:** After a Kit Load completes, if the user has already scrolled to a
different slot, the completion releases storage without repainting the old
slot's name. The DSP commit has already happened (Preset's immutable
request), which is correct — the latest-selection-wins policy means the
next Kit Load will overwrite this one. The deferred selection, if pending,
starts the load for the current slot.

**Why (LSR-04):** Kit Load on the LOAD_PAGE currently sets
`menu_storageBusy = 1u` and gates input until completion. With async loads,
the completion must handle the case where the user has already moved on.
This is the "dispose on supersession" contract from the plan's Q10.

**Inputs:** Snapshot vs. live counter, `menu_deferSelectionRequest`.
**Outputs:** Silent release on mismatch; normal repaint on match.
**Affiliates:** `preset_loadKitForScenes()` (line 4382),
`menu_refreshResidentNameScratchKit()` (line 4546).

---

### 1.10 MODIFY: Do not gate input on Kit Load scroll — `menu.c:4390`

**Operation:** MODIFY the Kit Load accepted block in
`menu_requestCurrentLoadSaveSelection()` (line 4382–4395). Remove the
`menu_storageBusy = 1u;` assignment at line 4390. Replace with:

```c
            /* Kit load starts on the facade but does NOT gate the menu
             * encoder. The user may continue scrolling; a superseded load is
             * disposed on generation mismatch in its completion callback.
             * Storage remains un-busy so the encoder's next detent can
             * post a new selection. */
```

Similarly modify the KitMrp accepted block at line 4406: remove
`menu_storageBusy = 1u;`.

**What:** Removes the input gate from Kit and KitMrp Load-page scroll.
The facade is still single-owner (the next load request will be deferred
via `menu_deferSelectionRequest` if the facade is busy), but the encoder
remains responsive.

**Why (LSR-04):** The plan requires "Scroll publishes immediately" and
"Load starts instantly... without gating the menu encoder." The existing
`menu_storageBusy` gate made occupied Kit slots visibly slower to scroll
past because the user had to wait for the full payload load + apply.

**Inputs:** `preset_loadKitForScenes()` returns accepted (true).
**Outputs:** `menu_storageBusy` is NOT set; `menu_deferSelectionRequest`
is set to 1 in the else branch (refused/busy).
**Affiliates:** `menu_deferSelectionRequest` (line 118),
the deferred-retry poll in `menu_pollPresetStatus()`.

---

### 1.11 MODIFY: Do not gate input on Instrument Load scroll — `menu.c:5517`

**Operation:** MODIFY `menu_instrumentLoadRequestSelection()` (line 5483).
Same pattern as §1.10: remove the `menu_storageBusy = 1u;` inside the
accepted branch (line ~5520). The Instrument load starts on the facade
but the encoder stays responsive.

**What:** Removes the input gate from Instrument Load-page scroll.
**Why (LSR-04):** Same rationale as Kit. Normal and Morph Instrument loads
are "live" browser types that must load asynchronously.
**Affiliates:** `preset_loadInstrumentForScenes()`,
`preset_loadInstrumentMorph()`.

---

### 1.12 MODIFY: Freeze display name on OK commit — `menu.c:9320`

**Operation:** MODIFY the OK acceptance block in `menu_handleLoadSaveMenu()`
(line 9320). After `if (commandAccepted)`, before
`menu_beginLoadSaveCommand()`, add:

```c
            if (commandAccepted) {
                /* Freeze the displayed name at commit time. Any pending async
                 * callback for a prior generation is discarded by the
                 * generation check. The committed name comes from the identity
                 * store (HCNAMES/cache), not from the async callback. */
                menu_selectionGeneration++;
                menu_beginLoadSaveCommand();
            }
```

**What:** Increments the generation counter at OK time so every pending
async callback from a prior scroll position is stale. The committed
selection's display name was already published synchronously from the
`.hcindex` cache by `menu_requestCurrentLoadSaveSelection()` before OK
was pressed. No retroactive name change can occur after commit.

**Why (LSR-04, Q9):** The plan identifies this as a high-severity risk:
"If the user presses OK while a stale name callback is still in flight,
the callback could overwrite the committed selection's display name."
Incrementing the generation makes every pre-commit callback stale by
construction.

**Inputs:** `commandAccepted` flag from the dispatch switch.
**Outputs:** `menu_selectionGeneration` incremented; command begins.
**Affiliates:** `menu_beginLoadSaveCommand()` (line 217).

---

### 1.13 MODIFY: Blank/Empty discipline in slot-name accessors — `filesystem.c:30650`

**Operation:** MODIFY all four `filesystem_*SlotName()` functions
(lines 30650, 30677, 30713, 30740). Change the return for
"slot exists but cache row is blank/null" to return blank (spaces) instead
of `"Empty   "`. The `"Empty   "` return is reserved for "slot does not
exist after a valid cache load."

**Current code (example — `filesystem_kitSlotName`, line 30650):**
```c
const char *filesystem_kitSlotName(uint16_t zero_based_slot)
{
    const char *name;
    if (!filesystem_kitSlotExists(zero_based_slot))
        return "Empty   ";
    name = filesystem_cachedLibraryName(FS_NAME_CACHE_KIT, zero_based_slot);
    return name ? name : "Empty   ";
}
```

**After:**
```c
const char *filesystem_kitSlotName(uint16_t zero_based_slot)
{
    const char *name;
    if (!filesystem_kitSlotExists(zero_based_slot))
        return "Empty   ";
    name = filesystem_cachedLibraryName(FS_NAME_CACHE_KIT, zero_based_slot);
    return name ? name : "        ";
}
```

Apply the same change to `filesystem_sceneSlotName()` (line 30677),
`filesystem_bankSlotName()` (line 30713), and
`filesystem_patternSlotName()` (line 30740).

**What:** When the cache is loaded and a slot exists, but the cached row
is blank or the library accessor returns NULL, the function now returns
8 spaces (blank = "not yet resolved") instead of `"Empty   "`.

**Why (LSR-03):** The plan standardizes blank as "not ready" and `Empty`
as "current index proved absence." The old code conflated the two. After
this change:
- Blank (8 spaces) = "the cache/HCNAMES cannot answer yet" (domain
  switching, cache not loaded, mid-transition).
- `"Empty   "` = "the `.hcindex` loaded successfully and this slot has
  no directory."

**Inputs:** `zero_based_slot`, `fs_list_cache_kind`, cache contents.
**Outputs:** `"Empty   "` only when slot provably absent; blank when
unresolved.
**Affiliates:** `menu_requestCurrentLoadSaveSelection()` (uses the name
for LCD display), `filesystem_librarySlotExists()`,
`filesystem_cachedLibraryName()`.

---

### 1.14 MODIFY: Disable OK while coordinate is unresolved — `menu.c:9219`

**Operation:** MODIFY the OK button dispatch block in
`menu_handleLoadSaveMenu()` (line 9219). Add a guard that refuses the OK
press when the current displayed name is blank (unresolved coordinate).

Insert before the existing `if (btnClicked)` block:

```c
    /* LSR-03: disable commit while the displayed coordinate is unresolved.
     * A blank name (8 spaces) means the index has not yet loaded for this
     * slot. The encoder can scroll freely, but OK is non-responsive. */
    if (btnClicked && menu_saveOptions.what < SAVE_TYPE_GLO) {
        uint8_t is_blank = 1u;
        uint8_t i;
        for (i = 0u; i < 8u; i++) {
            if (preset_currentName[i] != ' ') {
                is_blank = 0u;
                break;
            }
        }
        if (is_blank && menu_activePage == LOAD_PAGE)
            btnClicked = 0u;
    }
```

**What:** Suppresses the OK button on the Load page when the current
selection's name is all spaces (blank = unresolved). The Save page is
excluded because the name editor seeds from the resident identity, not
from the browser slot (per plan Q7).

**Why (LSR-03):** "Disable commit until the displayed coordinate is
resolved." The blank state is brief (`.hcindex` load is ~50-100ms), so
the user sees a momentary non-response rather than an error overlay.

**Inputs:** `preset_currentName[8]`, `menu_saveOptions.what`,
`menu_activePage`.
**Outputs:** `btnClicked` set to 0 (suppressed) if unresolved.
**Affiliates:** The OK dispatch block at line 9219,
`preset_currentName[]`.

---

## 2. HCNAMES Checkpoint at Domain Transitions (LSR-01)

### 2.1 MODIFY: Flush dirty mask at type-switch boundary — `menu.c:9338`

**Operation:** MODIFY the type-change boundary in
`menu_handleLoadSaveMenu()`, SAVE_STATE_EDIT_TYPE case. This code already
calls `menu_endResidentNameScratchSession()` at lines 9341 and 9377 for
Kit/KitMrp and Scene dirty-mask flush. The change is to **verify** that
every transition path that changes `menu_saveOptions.what` away from a
Kit/KitMrp/Instrument browser type triggers the flush.

**Specifically:** The existing code at line 9338 already checks
`menu_saveOptions.what != previous_type` and calls
`menu_endResidentNameScratchSession()` for Kit-family exits (lines 9341,
9377). The `menu_residentNameDirtySceneMask != 0u` check at line 9376
correctly fires the one HCNAMES rewrite.

**Verification needed, no code change expected:** Confirm that the paths
at lines 9341 and 9377 cover all domain transitions:
- Kit → Scene: covered (line 9341, leaves Kit family).
- Kit → Bank: covered (same path).
- Kit → Instrument (nested): covered (line 9341).
- Instrument → Kit (unnested): covered (line 9377, Scene dirty mask).
- Any type → the same type: no-op (line 9338 guard).

**If a gap is found:** Add a call to
`menu_endResidentNameScratchSession()` with a dirty-mask guard at the
specific transition. The existing function handles the flush correctly;
only the trigger point is at issue.

**What:** Ensures that Kit/Instrument HCNAMES dirty rows are persisted
before the browser cache domain changes, even if the user never exits the
Load/Save page.

**Why (LSR-01):** "The fix is to trigger the safe-write earlier (at browser
domain transition), not to add a snapshot." The dirty mask accumulates
identity updates in SRAM but the durable `.hcnames` rewrite is deferred.
A domain transition without flushing risks losing the identity update.

**Inputs:** `menu_residentNameDirtySceneMask`, `menu_saveOptions.what`,
`previous_type`.
**Outputs:** One HCNAMES rewrite started if the mask is nonzero.
**Affiliates:** `menu_endResidentNameScratchSession()` (line 4656),
`filesystem_requestUpdateResidentKitNames()` (filesystem.h:942),
`menu_residentNameScratchFlushComplete()` (line 4597).

---

### 2.2 VERIFY: Morph identity isolation — `menu.c:4569`

**Operation:** VERIFY (no code change expected).

Confirm that `menu_refreshResidentNameScratchInstrument()` (line 4569) is
called only from the Normal Instrument commit path, never from the
InstrumentMrp commit path. The InstrumentMrp commit must NOT turn Morph
endpoint data into a Normal Instrument identity in HCNAMES.

**How to verify:** Search for all callers of
`menu_refreshResidentNameScratchInstrument()` (lines 4812, 10591, 10743).
Confirm each caller is gated by `!menu_instrumentLoadMorphMode` or an
equivalent condition that excludes Morph-only commits.

**Why (LSR-01, Q3):** "Morph Load/Save must never turn Morph endpoint data
into a Normal Instrument identity."

---

### 2.3 MODIFY: Exit ordering — complete in-flight load before HCNAMES — `menu.c:10984`

**Operation:** MODIFY `menu_switchPage()` (line 10978). In the
`menu_storageBusy` guard at line 10984, add handling for the case where
an in-flight async Kit/Instrument load (from §1.10/§1.11's un-gated scroll)
is running when the user exits:

The existing code at line 10984 already retains the exit in
`menu_pendingPageSwitch`. With §1.10, the in-flight load is NOT gated by
`menu_storageBusy`, so the page switch will proceed immediately. The
in-flight load's completion callback will find a generation mismatch
(because `menu_selectionGeneration` was incremented by the type-switch at
§1.4 or by the page transition) and dispose the result.

**However:** If the load was accepted and the facade is busy, the page
switch must wait for the facade to complete (the facade cannot be abandoned
mid-stream). The `menu_pendingPageSwitch` mechanism already handles this:
the mode-button press is retained, and `menu_processPendingPageSwitch()`
fires it when `menu_storageBusy == 0` and `preset_getStatus() == PRESET_IDLE`.

**Change needed:** In the Kit/KitMrp completion callback (§1.9), when a
generation mismatch is detected, check whether a page switch is pending.
If so, trigger the HCNAMES checkpoint before releasing storage:

```c
    if (menu_selectionGenerationSnapshot != menu_selectionGeneration) {
        /* Stale load completed. If the user is exiting, the HCNAMES
         * checkpoint must fire before the page switch. The dirty mask
         * persists until the next idle opportunity. */
        if (menu_pendingPageSwitch != MENU_PENDING_PAGE_NONE &&
            menu_residentNameDirtySceneMask != 0u) {
            /* The existing endResidentNameScratchSession() will start the
             * HCNAMES write if dirty. Its completion callback releases
             * storageBusy and processPendingPageSwitch() fires the exit. */
            (void)menu_endResidentNameScratchSession();
            return;
        }
        menu_storageBusy = 0u;
        if (menu_deferSelectionRequest)
            menu_requestCurrentLoadSaveSelection(1);
        return;
    }
```

**What:** Ensures the ordering: complete in-flight load → HCNAMES checkpoint
→ exit. The in-flight load has already completed (this is its callback).
If the dirty mask is nonzero and the user is exiting, the HCNAMES write
starts now. The page switch fires after that write completes.

**Why (LSR-01, UQ-1):** "The ordering is always: complete in-flight load →
HCNAMES checkpoint → exit/transition."

**Inputs:** Generation mismatch, `menu_pendingPageSwitch`,
`menu_residentNameDirtySceneMask`.
**Outputs:** HCNAMES write started if dirty + exiting; otherwise silent
release.
**Affiliates:** `menu_endResidentNameScratchSession()` (line 4656),
`menu_processPendingPageSwitch()` (line 11238).

---

## 3. Detach Page Exit from HCNAMES Persistence (LSR-02)

### 3.1 ADD: Deferred HCNAMES dirty-mask check API — `filesystem.h`

**Operation:** ADD one public function declaration after the existing
`filesystem_requestUpdateResidentKitNames()` block (after line 943):

```c
/*
 * What: check whether Menu has accumulated a nonzero HCNAMES dirty mask
 * that needs a deferred write. Called by filesystem_tick()'s new HCNAMES
 * scheduler rung to decide whether to start a deferred write when the
 * facade is idle.
 *
 * Why (LSR-02): the deferred HCNAMES write runs as a new scheduler rung
 * in filesystem_tick(), positioned between foreground commands and the
 * AutoSave writer. This query lets filesystem.c inspect Menu's dirty mask
 * without reaching into Menu's statics.
 *
 * Inputs: none.
 * Outputs: nonzero if a deferred HCNAMES write is needed.
 * Affiliates: menu_residentNameDirtySceneMask (menu.c:1266),
 * filesystem_tick() HCNAMES scheduler rung.
 */
uint8_t menu_hasResidentNameDirtyMask(void);
```

---

### 3.2 ADD: Deferred HCNAMES dirty-mask check implementation — `menu.c`

**Operation:** ADD after `menu_endResidentNameScratchSession()` (after
line 4693):

```c
uint8_t menu_hasResidentNameDirtyMask(void)
{
    /*
     * What: returns nonzero when accumulated Kit/Instrument HCNAMES changes
     * await their deferred safe-write. The caller is filesystem_tick()'s
     * HCNAMES scheduler rung, which starts the write when the facade is idle.
     *
     * Why: Menu owns the dirty mask, but the deferred write scheduler lives
     * in filesystem_tick(). This accessor crosses the boundary without
     * exposing Menu internals.
     *
     * Inputs: none.
     * Outputs: nonzero if menu_residentNameDirtySceneMask != 0.
     * Affiliates: menu_residentNameDirtySceneMask (line 1266),
     * menu_endResidentNameScratchSession() (line 4656).
     */
    return (uint8_t)(menu_residentNameDirtySceneMask != 0u);
}
```

---

### 3.3 ADD: Deferred HCNAMES flush trigger function — `menu.c`

**Operation:** ADD after `menu_hasResidentNameDirtyMask()` (§3.2):

```c
void menu_triggerDeferredHcnamesFlush(void)
{
    /*
     * What: starts the deferred HCNAMES write if the dirty mask is nonzero
     * and no name session is actively using the cache. Called from
     * filesystem_tick()'s HCNAMES scheduler rung when the facade is idle.
     *
     * Why (LSR-02): the page exit no longer blocks on the HCNAMES write.
     * Instead, the dirty mask persists and this function picks it up on the
     * next idle tick. The existing menu_endResidentNameScratchSession()
     * handles the actual write request, mask clearing, and callback.
     *
     * Inputs: idle facade (caller guarantees), nonzero dirty mask.
     * Outputs: one filesystem_requestUpdateResidentKitNames() call if dirty.
     * Affiliates: menu_endResidentNameScratchSession() (line 4656),
     * filesystem_tick() HCNAMES scheduler rung (§3.4).
     */
    if (menu_residentNameDirtySceneMask == 0u)
        return;
    if (menu_activePage == LOAD_PAGE || menu_activePage == SAVE_PAGE)
        return;
    (void)menu_endResidentNameScratchSession();
}
```

---

### 3.4 ADD: Deferred HCNAMES flush trigger declaration — `filesystem.h`

**Operation:** ADD adjacent to the `menu_hasResidentNameDirtyMask()`
declaration (§3.1):

```c
/*
 * What: starts a deferred HCNAMES write from filesystem_tick()'s idle
 * scheduler. Implemented in menu.c.
 *
 * Why (LSR-02): page exit releases the UI immediately; this function
 * picks up the dirty mask on the next idle facade tick.
 *
 * Inputs: idle facade (guaranteed by the caller's status check).
 * Outputs: may start one HCNAMES write.
 * Affiliates: filesystem_tick() HCNAMES scheduler rung.
 */
void menu_triggerDeferredHcnamesFlush(void);
```

Note: These two declarations (`menu_hasResidentNameDirtyMask` and
`menu_triggerDeferredHcnamesFlush`) may alternatively be placed in
`menu.h` if the project convention puts Menu public APIs there. Currently
`filesystem.c` already includes `menu.h` and calls several Menu accessors
(`menu_activePage`, `menu_isLoadSaveCommandActive()`), so placing them in
`menu.h` is architecturally consistent.

---

### 3.5 ADD: HCNAMES scheduler rung in `filesystem_tick()` — `filesystem.c:25099`

**Operation:** ADD a new scheduler rung in `filesystem_tick()` (line 25012)
between the pattern trace flush (line 25089) and the budget refill
(line 25099). Insert:

```c
    /*
     * What: deferred Kit/Instrument HCNAMES write runs at higher priority
     * than the AutoSave writer. Checked every tick when the facade is idle
     * and Menu has accumulated dirty HCNAMES rows from a Load/Save commit
     * whose page exit did not block on the write.
     *
     * Why (LSR-02, UQ-2): the user's deferred HCNAMES update must land
     * before the next AutoSave drain, but after settings persistence and
     * diagnostic trace. This position gives HCNAMES writes scheduling
     * priority over AutoSave without coupling the two systems.
     *
     * Inputs: idle facade (status == FS_STATUS_IDLE), Menu's accumulated
     * dirty mask via menu_hasResidentNameDirtyMask().
     * Outputs: may start one HCNAMES write; if started, the facade
     * transitions to BUSY and subsequent scheduler rungs are skipped.
     * Affiliates: menu_triggerDeferredHcnamesFlush() (menu.c),
     * menu_residentNameDirtySceneMask (menu.c:1266),
     * filesystem_autosaveWriterSchedule_tick() (line 24194).
     */
    if (status == FS_STATUS_IDLE && menu_hasResidentNameDirtyMask())
        menu_triggerDeferredHcnamesFlush();
```

**Location:** After line 25089 (`filesystem_patternTraceFlushSchedule_tick()`)
and before line 25099 (`filesystem_backgroundBudgetRefill()`).

**What:** A new scheduler rung that fires the deferred HCNAMES write before
the AutoSave writer gets a chance. This ensures dirty Kit/Instrument identity
rows are persisted promptly after the user exits Load/Save, without blocking
the page transition.

**Why (LSR-02, UQ-2):** "New scheduler rung in filesystem_tick() positioned
between foreground commands and AutoSave. Checked every tick when
menu_residentNameDirtySceneMask != 0 and the facade is idle."

**Inputs:** `status == FS_STATUS_IDLE`, `menu_hasResidentNameDirtyMask()`.
**Outputs:** If the HCNAMES write starts, `status` becomes `FS_STATUS_BUSY`
and subsequent idle-gated schedulers (AutoSave, Pattern drain) are naturally
skipped because their `status == FS_STATUS_IDLE` check fails.
**Affiliates:** `filesystem_settingsWriterSchedule_tick()` (line 25067),
`filesystem_autosaveWriterSchedule_tick()` (line 25111).

---

### 3.6 MODIFY: Page exit does not block on HCNAMES write — `menu.c:10978`

**Operation:** MODIFY `menu_switchPage()` (line 10978). The existing logic
at lines 11033–11047 sets `end_resident_name_session = 1` when leaving
Load/Save with a dirty mask, and line 11199 calls
`menu_endResidentNameScratchSession()`. This call currently blocks the page
switch via `menu_storageBusy` while the HCNAMES write runs.

**Change:** Replace the call at line 11199 with a version that does NOT
set `menu_storageBusy`:

```c
    if (end_resident_name_session) {
        /* LSR-02: do not block the page switch on the HCNAMES write.
         * The dirty mask persists and the deferred scheduler rung in
         * filesystem_tick() picks it up on the next idle tick. Clear the
         * scratch session state so the new page can proceed. */
        menu_residentNameScratchValid = 0u;
        menu_residentNameScratchScene =
            MENU_RESIDENT_NAME_SCRATCH_INVALID_SCENE;
        filesystem_clearNameCache();
    }
```

This replaces the existing `(void)menu_endResidentNameScratchSession();`
at line 11199. The key difference: the scratch session is cleared (so the
new page can use the cache), but the dirty mask is NOT cleared and no
HCNAMES write is started. The deferred scheduler (§3.5) will pick up the
mask and start the write when the facade is idle.

**What:** Detaches the page switch from HCNAMES persistence. The page
transition happens immediately. The HCNAMES write runs in the background
via the deferred scheduler.

**Why (LSR-02):** "Detach the page switch from the HCNAMES write: tear
down the browser and paint the destination page on the normal UI refresh
cadence. Queue the HCNAMES write independently."

**Inputs:** `end_resident_name_session` flag, `menu_residentNameDirtySceneMask`.
**Outputs:** Scratch state cleared, dirty mask retained for deferred write.
**Affiliates:** `menu_endResidentNameScratchSession()` (line 4656),
the deferred scheduler rung (§3.5), `menu_processPendingPageSwitch()`
(line 11238).

---

### 3.7 MODIFY: `menu_residentNameScratchFlushComplete()` handles
deferred context — `menu.c:4597`

**Operation:** MODIFY `menu_residentNameScratchFlushComplete()` (line 4597).
Add handling for the case where the flush was triggered by the deferred
scheduler (§3.5) rather than by an in-session exit. When the user has
already left Load/Save by the time the flush completes, the function must
not attempt to re-enter a Kit/Instrument browser context:

The existing code at lines 4642-4651 already checks
`menu_activePage == LOAD_PAGE || menu_activePage == SAVE_PAGE` before
re-entering the browser. When the flush is deferred, `menu_activePage`
will be the new page (not Load/Save), so these checks correctly fall
through to the `menu_repaintAll()` at line 4653. No change needed
to the existing logic.

**Verification only:** Confirm that the existing flow correctly handles:
1. Deferred flush completes while the user is on a VOICE page → no browser
   re-entry, `menu_repaintAll()` fires (correct).
2. Deferred flush completes while the user re-entered Load/Save → the
   existing branches at lines 4643-4650 handle re-entry (correct).
3. Deferred flush fails → `menu_showFilesystemErrorOverlay()` at line 4630
   fires (correct; the dirty mask was already cleared at line 4635 by the
   successful-path check, so on failure the mask persists for retry).

**Status:** VERIFY ONLY, no code change expected.

---

## 4. Interaction Verification Checklist

These are not code changes but architectural invariants that must be
verified during implementation and testing:

### 4.1 AutoSave page-exit expedite interaction (LSR-02, Q5)

With the page switch happening immediately (before the HCNAMES write), the
AutoSave page-exit expedite fires earlier. The facade's `BUSY` rejection
prevents a race, but verify:
- If the HCNAMES write wins the facade first (via the scheduler rung's
  higher priority), the AutoSave drain waits. ✓ by construction (§3.5).
- If the AutoSave drain wins first (HCNAMES dirty mask was zero at the
  rung, then became dirty from a later commit), the HCNAMES write retries
  on the next idle tick. ✓ by construction (dirty mask persists).

### 4.2 Power-loss recovery (LSR-02, R5)

If power is lost between page exit and the deferred HCNAMES write, the
`R` refreshed flag in `fs_resident_source[]` survives from the previous
HCNAMES write. The boot reader uses Case 2 (narrow library load) for
`R`-marked rows, recovering the correct identity. The name text may be
stale but the source/provenance was staged at commit time with the
`FS_RESIDENT_SOURCE_DIRTY_FLAG` protection.

### 4.3 Re-entry before deferred write completes (LSR-02)

If the user re-enters Kit/Instrument Load before the deferred HCNAMES
write completes, the pending dirty mask is carried forward. The re-entry
reads HCNAMES from the mirror (RAM), not from the card. The write
completes alongside or before the next exit. The deferred scheduler's
`menu_activePage == LOAD_PAGE || SAVE_PAGE` check (§3.3) prevents the
deferred trigger from firing while the user is browsing, matching the
existing page-suppression contract.

### 4.4 Bank identity invariant (R2)

Bank Load/Save call `bank_setRestoreBankSlot()` and
`filesystem_markSettingsDirty()` together. Bank/Scene HCNAMES publication
is already immediate (not deferred). The LSR refactoring changes only
Kit/Instrument HCNAMES timing. The Bank identity invariant is not affected.

### 4.5 Scene Pattern failure invariant (R3)

VERIFIED SAFE by code trace and hardware (Appendix A of the plan). No code
change needed.

---

## 5. Summary of All Changes

| # | File | Line | Op | Description |
|---|------|------|----|-------------|
| 1.1 | menu.c | 119 | ADD | `menu_selectionGeneration` counter |
| 1.2 | menu.c | 120 | ADD | `menu_selectionGenerationSnapshot` variable |
| 1.3 | menu.c | 9440 | MOD | Increment generation on encoder scroll |
| 1.4 | menu.c | ~9380 | MOD | Increment generation on type switch |
| 1.5 | menu.c | request sites | MOD | Capture snapshot only after async request acceptance |
| 1.6 | menu.c | 5433 | MOD | Capture snapshot at Bank preview request time |
| 1.7 | menu.c | ~5157 | MOD | Check generation in Bank preview completion |
| 1.8 | menu.c | ~4457 | MOD | Check generation in library index completion |
| 1.9 | menu.c | ~10284 | MOD | Check generation in Kit Load completion |
| 1.10 | menu.c | 4390 | MOD | Remove input gate on Kit/KitMrp Load scroll |
| 1.11 | menu.c | ~5520 | MOD | Remove input gate on Instrument Load scroll |
| 1.12 | menu.c | 9320 | MOD | Freeze name + increment gen on OK commit |
| 1.13 | filesystem.c | 30650+ | MOD | Blank/Empty split in 4 slot-name accessors |
| 1.14 | menu.c | 9219 | MOD | Disable OK while name is unresolved (blank) |
| 2.1 | menu.c | 9338 | VERIFY | Flush dirty mask at all type-switch boundaries |
| 2.2 | menu.c | 4569 | VERIFY | Morph identity isolation |
| 2.3 | menu.c | ~10284 | MOD | Exit ordering: in-flight → HCNAMES → exit |
| 3.1 | menu.h | ~449 | ADD | `menu_hasResidentNameDirtyMask()` declaration |
| 3.2 | menu.c | ~4694 | ADD | `menu_hasResidentNameDirtyMask()` implementation |
| 3.3 | menu.c | ~4694 | ADD | `menu_triggerDeferredHcnamesFlush()` implementation |
| 3.4 | menu.h | ~449 | ADD | `menu_triggerDeferredHcnamesFlush()` declaration |
| 3.5 | filesystem.c | 25089 | ADD | HCNAMES scheduler rung in `filesystem_tick()` |
| 3.6 | menu.c | 11199 | MOD | Page exit does not block on HCNAMES write |
| 3.7 | menu.c | 4597 | VERIFY | Deferred flush completion handles non-LS page |

**Total new code:** ~80 lines of implementation, ~50 lines of contract
comments.
**Total new RAM:** 2 bytes (`menu_selectionGeneration` and its request
snapshot).
**Files touched:** 4 source files (`menu.c`, `menu.h`, `filesystem.c`,
`filesystem.h`), plus this implementation log and the generated firmware image.

---

## 6. Deferred Items (Not This Session)

Per the plan's Remaining Open Items §2:

- **Additional Morph snapshot projection.** The current `.hctmp` path already
  provides the required Morph entry snapshot and restores only Morph endpoint
  data, so no new RAM or identity path was needed here. Any future change to
  broaden that projection remains a separate feature.

- **Test-item fold into Phase 4.** Documentation task, not a code change.
