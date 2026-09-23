# S070 Phase 2 — LSR-01 Supplement: Instrument Entry/Exit HCNAMES Flush

Session: S070
Branch: `dev-ph5-effects`
Source: Hardware test `SD_CARD_LSR01`, Appendix C test item 14 (LSR-01).

---

## Defect

## Implementation Notes — 2026-09-23

### Pre-change audit

- The four listed boundaries are all in `Core/Menu/menu.c`; no filesystem
  ownership or new storage is required. The existing
  `menu_endResidentNameScratchSession()` remains the single HCNAMES writer,
  and its existing dirty Scene mask is the checkpoint state.
- The free-scroll LSR-04 path can leave `menu_storageBusy == 0` while Preset
  still owns a Kit or Instrument request. The shared
  `menu_loadInstrumentTransactionBusy()` helper is therefore being extended
  to include `preset_getStatus() != PRESET_IDLE`, so both ButtonHandler entry
  and Menu exit observe the same safe boundary.
- The flush boundary increments the existing selection generation before the
  cache/domain transition. No new generation, callback buffer, or retained
  name storage is introduced.

Implementation will proceed in two passes: first the shared transaction guard
and the four entry/exit checkpoint sites, then source/build verification and
final notes in this document. Every new public-header contract will receive an
adjacent block comment in `.h`, with the implementation rationale adjacent in
`.c`.

After a Kit Load on Load:[Kit], dirty HCNAMES rows persist across an
**encoder** type switch (Kit → Scene, Kit → Bank, etc.) but are **lost**
across a **voice-button** entry into nested Instrument Load followed by
power loss.

The encoder type-switch path fires `menu_endResidentNameScratchSession()`
at the domain transition boundary (confirmed working). Four related
paths bypass this flush:

| Path | Function | Line | Has flush? |
|------|----------|------|------------|
| VOICE button into Instrument | `menu_loadInstrumentVoicePressed()` | 6300 | **No** |
| Pot-1 into Instrument Load | `menu_loadSaveEnterInstrumentLoad()` | 6627 | **No** |
| Pot-1 into Instrument Save | `menu_loadSaveEnterInstrumentSave()` | 6662 | **No** |
| Instrument exit back to Kit | `menu_loadInstrumentExit()` | 6273 | **No** |

For comparison, these paths DO flush:

| Path | Function | Line | Has flush? |
|------|----------|------|------------|
| Encoder type switch | `menu_handleLoadSaveMenu()` EDIT_TYPE | ~9515 | Yes |
| Instrument type step | `menu_instrumentLoadStepType()` | 5752 | Yes |
| Pot-1 to non-Kit top-level | `menu_loadSaveEnterTop()` | 6603 | Yes |
| Page exit | `menu_switchPage()` | 11413 | Yes (LSR-02) |

The deferred scheduler (LSR-02) picks up the dirty mask after page exit,
but if the user stays in Load/Save and power is lost between any of these
unflushed transitions, the HCNAMES write never starts.

---

## Root Cause

`menu_loadInstrumentVoicePressed()` enters nested Instrument Load as a
side effect of a ButtonHandler voice-button press. It is not routed through
`menu_handleLoadSaveMenu()`'s SAVE_STATE_EDIT_TYPE case and therefore
does not execute the type-switch flush path. The same applies to the
Pot-1 entry helpers.

`menu_loadInstrumentExit()` exits nested Instrument back to Kit without
flushing because the original design treated Kit and Instrument as one
shared HCNAMES session with a single exit boundary (page exit). That
assumption was valid before LSR-02 made page exit deferred — now, every
intermediate transition within Load/Save is a potential power-loss window
where dirty rows must already be on the card.

Additionally, `menu_loadInstrumentTransactionBusy()` (line 6383) checks
only `menu_instrumentLoadActive && menu_storageBusy`. With LSR-04's
free-scroll Instrument loads, the facade can be busy via Preset without
`menu_storageBusy` being set. This allows the voice-button entry and
the Instrument exit to proceed while an Instrument load is in-flight.

---

## Affected Scenarios

1. **Kit Load → VOICE button → power loss.** Kit dirty mask never flushed.
   (Primary defect, confirmed on hardware via `SD_CARD_LSR01`.)

2. **Instrument Load → VOICE button (different voice) → power loss.**
   Instrument dirty mask from the first voice never flushed before
   switching to the second voice's context. The voice switch at line 6341
   re-enters with a new voice but skips the flush.

3. **Instrument Load → exit to Kit → power loss.** Instrument dirty mask
   accumulated during browsing never flushed. `menu_loadInstrumentExit()`
   at line 6273 returns to Kit without checking the dirty mask.

4. **Kit Load → Pot-1 to Instrument → power loss.** Same as scenario 1 but
   via Pot-1 instead of VOICE button.

---

## Fix

Add the generation-increment + dirty-mask flush pattern at all four
boundaries. Extend the busy guards to cover in-flight free-scroll loads
via `preset_getStatus() != PRESET_IDLE`.

---

## Changes

### 1. MODIFY: Voice-button busy guard — `menu.c:6316`

**Operation:** MODIFY the busy guard in `menu_loadInstrumentVoicePressed()`
at line 6316. Extend it to cover in-flight free-scroll Kit loads.

**Before:**
```c
    if (menu_storageBusy && !menu_instrumentLoadActive &&
        (menu_saveOptions.what == SAVE_TYPE_KIT ||
         menu_saveOptions.what == SAVE_TYPE_KIT_MORPH)) {
```

**After:**
```c
    if ((menu_storageBusy || preset_getStatus() != PRESET_IDLE) &&
        !menu_instrumentLoadActive &&
        (menu_saveOptions.what == SAVE_TYPE_KIT ||
         menu_saveOptions.what == SAVE_TYPE_KIT_MORPH)) {
```

**What:** Extends the busy guard to detect an in-flight free-scroll Kit
load that does not set `menu_storageBusy`. With LSR-04's async Kit loads,
the facade can be busy (Preset owns a Kit load request) without Menu
knowing it via `menu_storageBusy`.

**Why:** Without this, the voice-button entry can race with a Kit load
that is still in-flight. The in-flight load's completion callback would
find a generation mismatch and dispose, but the entry sequence could
replace the shared cache before the load completes.

**Inputs:** `preset_getStatus()`, `menu_storageBusy`,
`menu_instrumentLoadActive`, `menu_saveOptions.what`.
**Outputs:** Returns 1 (consumed) to defer the voice press until the
in-flight load completes.
**Affiliates:** The same guard pattern in `menu_parseEncoder` (line 9751),
`menu_switchPage` (line 11194), and
`menu_instrumentLoadRequestSelection` (line 5618).

---

### 2. ADD: HCNAMES flush before voice-button Instrument entry — `menu.c:6339`

**Operation:** ADD after the `menu_loadInstrumentTransactionBusy()` guard
(line 6337) and before the entry setup sequence (line 6341), inside
`menu_loadInstrumentVoicePressed()`.

```c
    /* LSR-01: entering or switching voices is a coordinate boundary. Flush
     * any dirty Kit/Instrument HCNAMES rows before the shared cache switches
     * to HCNAMES → Instrument .hcindex. If the flush starts, its completion
     * re-enters the browser; the voice entry returns consumed. */
    menu_selectionGeneration++;
    if (menu_residentNameDirtySceneMask != 0u &&
        menu_endResidentNameScratchSession())
        return 1u;
```

**What:** Increments the generation counter (this is a coordinate boundary)
and flushes the dirty HCNAMES mask if nonzero. If the flush starts
(facade was idle, request accepted), the function returns 1 (consumed).

This covers both first entry from Kit (scenario 1) and voice-to-voice
switches within nested Instrument mode (scenario 2), because the function
handles both cases — `menu_instrumentLoadActive` may be either 0 or 1
when this code runs.

**Why (LSR-01):** Every voice-button press that reaches this point is a
coordinate boundary. Dirty rows from the previous context (Kit or a
different Instrument voice) must be persisted before the cache domain
changes.

**Inputs:** `menu_residentNameDirtySceneMask`,
`menu_selectionGeneration`.
**Outputs:** If dirty mask nonzero and flush accepted: returns 1, flush
starts. If dirty mask zero or flush refused (facade busy): falls through
to the existing entry sequence.
**Affiliates:** `menu_endResidentNameScratchSession()` (line 4693),
`menu_residentNameScratchFlushComplete()` (line 4636),
`menu_instrumentLoadStepType()` (line 5752).

**Interaction with flush completion:** When the flush completes and
`menu_residentNameScratchFlushComplete()` runs at line 4671:

- The Appendix ordering now installs `menu_instrumentLoadActive == 1` and
  the selected voice/type before starting the flush, including first entry
  from Kit. Completion therefore calls `menu_requestInstrumentEntryNames()`
  for the selected destination context directly.

The HCNAMES write remains brief (~50–100ms), and the single voice press is
consumed without requiring a second press.

---

### 3. ADD: HCNAMES flush before Pot-1 Instrument Load entry — `menu.c:6633`

**Operation:** ADD before the existing `menu_invalidateInstrumentLoadTemp()`
call at line 6634, inside `menu_loadSaveEnterInstrumentLoad()`.

```c
    /* LSR-01: Pot-1 into nested Instrument is the same domain transition as a
     * voice-button entry. Flush dirty HCNAMES before replacing the cache. */
    menu_selectionGeneration++;
    if (menu_residentNameDirtySceneMask != 0u &&
        menu_endResidentNameScratchSession())
        return;
```

**What:** Same pattern as §2 but for the Pot-1 entry path. The function is
`void`, so the return is bare.

**Why (LSR-01):** Pot-1 can navigate directly from a top-level Kit/KitMrp
row into a nested Instrument row without passing through the encoder
type-switch handler. The same cache domain transition occurs.

**Inputs/Outputs:** Same as §2.
**Affiliates:** `menu_loadSaveEnterTop()` (line 6603) — the Pot-1 top-level
path already has the flush. This adds coverage for the Pot-1 nested path.

**Interaction with flush completion:** Appendix A2 installs the destination
Instrument context before starting the flush. Completion therefore resumes
`menu_requestInstrumentEntryNames()` for the selected Pot-1 voice/type; the
logical position does not need a second detent.

---

### 4. ADD: HCNAMES flush before Pot-1 Instrument Save entry — `menu.c:6665`

**Operation:** ADD before the existing `menu_invalidateInstrumentLoadTemp()`
call at line 6666, inside `menu_loadSaveEnterInstrumentSave()`.

```c
    /* LSR-01: Pot-1 into nested Instrument Save is a domain transition. */
    menu_selectionGeneration++;
    if (menu_residentNameDirtySceneMask != 0u &&
        menu_endResidentNameScratchSession())
        return;
```

**What:** Same pattern as §3 but for the Save-page Pot-1 entry path.

**Why (LSR-01):** The Save-page Pot-1 path is less likely to hit this (the
user would have to Load a Kit on Load page, switch to Save via Pot-1, then
navigate to an Instrument Save row), but the domain transition is the same
and the flush should be architecturally consistent.

**Inputs/Outputs:** Same as §2.
**Affiliates:** `menu_loadSaveEnterInstrumentLoad()` (§3),
`menu_loadInstrumentVoicePressed()` (§2).

---

### 5. MODIFY: Instrument exit busy guard — `menu.c:6285`

**Operation:** MODIFY `menu_loadInstrumentExit()` at line 6285. Extend the
busy guard to cover in-flight free-scroll Instrument loads.

**Before:**
```c
    if (menu_loadInstrumentTransactionBusy())
        return;
```

**After:**
```c
    if (menu_loadInstrumentTransactionBusy() ||
        preset_getStatus() != PRESET_IDLE)
        return;
```

**What:** Prevents exiting nested Instrument back to Kit while an
Instrument load is in-flight (free-scroll, no `menu_storageBusy`). The
existing `menu_loadInstrumentTransactionBusy()` only catches loads that
set `menu_storageBusy` — which free-scroll loads do not. Without this
guard, the exit could proceed while Preset is still applying an Instrument
payload, which would clear `menu_instrumentLoadActive` before the
completion callback runs.

**Why:** The Instrument load completion in `menu_pollPresetStatus()` at
line 10738 checks `menu_instrumentLoadActive` to set the deferred
selection flag. If the exit cleared this flag before the completion ran,
the completion's staleness handling would be wrong. More importantly, the
exit calls `menu_requestKitEntryNames()` which replaces the cache domain,
and a concurrent Instrument apply would set `menu_storageBusy` mid-exit.

**Inputs:** `preset_getStatus()`.
**Outputs:** Returns early (exit deferred) if Preset is not idle.
**Affiliates:** `menu_loadInstrumentTransactionBusy()` (line 6383),
the same guard in `menu_switchPage` (line 11194).

---

### 6. ADD: HCNAMES flush at Instrument exit to Kit — `menu.c:6287`

**Operation:** ADD after the busy guard (§5) and before
`menu_invalidateInstrumentLoadTemp()` at line 6290, inside
`menu_loadInstrumentExit()`.

```c
    /* LSR-01: exiting nested Instrument back to Kit is a checkpoint boundary.
     * Any dirty Instrument HCNAMES rows must be persisted before the cache
     * switches back to /Kit/.hcindex. If the flush starts, its completion
     * re-enters Kit automatically; we return early to defer the exit cleanup
     * until after the write. */
    menu_selectionGeneration++;
    if (menu_residentNameDirtySceneMask != 0u &&
        menu_endResidentNameScratchSession())
        return;
```

**What:** Increments the generation counter and flushes the dirty HCNAMES
mask before exiting from Instrument back to Kit. Appendix A4 refines this
ordering: the function clears `menu_instrumentLoadActive` and the outgoing
Instrument context before starting the flush, so completion sees Kit
ownership and calls `menu_requestKitEntryNames()` directly.

**Why (LSR-01, scenario 3):** If the user loaded an Instrument (dirtying
HCNAMES) and then exits to Kit, the dirty rows must be on the card before
the cache switches to Kit `.hcindex`. Without this, a power loss between
the exit and the eventual page exit loses the Instrument identity. The
shared-session design ("Kit and Instrument share one exit boundary") was
valid before LSR-02 made page exit deferred — now the intermediate
transition is the checkpoint boundary.

**Inputs:** `menu_residentNameDirtySceneMask`,
`menu_selectionGeneration`.
**Outputs:** If dirty mask nonzero and flush accepted: returns early,
flush starts. If mask zero: falls through to the existing exit sequence.
**Affiliates:** `menu_endResidentNameScratchSession()` (line 4693),
`menu_residentNameScratchFlushComplete()` (line 4636).

**Interaction with flush completion:** Appendix A4 clears the Instrument
context before starting the flush. Completion therefore sees Kit ownership
and calls `menu_requestKitEntryNames()` directly; the mode-button exit is
completed without a second press.

---

## Summary

| # | File | Line | Op | Description |
|---|------|------|----|-------------|
| 1 | menu.c | 6316 | MOD | Extend voice-button busy guard for free-scroll Kit loads |
| 2 | menu.c | 6339 | ADD | HCNAMES flush + gen increment before voice-button entry |
| 3 | menu.c | 6633 | ADD | HCNAMES flush + gen increment before Pot-1 Instrument Load |
| 4 | menu.c | 6665 | ADD | HCNAMES flush + gen increment before Pot-1 Instrument Save |
| 5 | menu.c | 6285 | MOD | Extend Instrument exit busy guard for free-scroll loads |
| 6 | menu.c | 6287 | ADD | HCNAMES flush + gen increment at Instrument exit to Kit |
| 7 | menu.c/.h | 6383/359 | MOD | Include active Preset status in nested transaction guard |

**Total new code:** ~24 lines of implementation, ~18 lines of comments.
**Total new RAM:** 0 bytes.
**Files touched:** 2 source files (`menu.c`, `menu.h`), plus this supplement
and the generated firmware image.

### Implementation Notes — 2026-09-23 completion

- Extended `menu_loadInstrumentTransactionBusy()` in `menu.c` and its public
  contract in `menu.h` so ButtonHandler observes both `menu_storageBusy` and
  an active Preset operation. This closes the LSR-04 free-scroll race without
  adding a second transaction flag or changing the approved RAM budget.
- Added the requested generation increment and dirty-mask checkpoint before
  VOICE-button Instrument entry/voice switching, Pot-1 Instrument Load entry,
  Pot-1 Instrument Save entry, and nested Instrument exit back to Kit.
- The checkpoint returns before cache or nested-session teardown when the
  existing HCNAMES writer accepts the request. The existing completion path
  then restores the appropriate browser context; page exit remains governed
  by the S070 Phase 2 deferred scheduler.
- Added defensive Pot-1 entry guards so a free-scroll Preset payload cannot be
  displaced by logical-position navigation while the facade is owned.
- No Morph identity or `.hctmp` path was changed; this supplement only closes
  the missing Kit/Instrument HCNAMES boundary coverage.

---

## Verification

### Source/build verification — 2026-09-23

- `git diff --check`: PASS.
- `make all`: PASS. Link metrics: `text=451,756`, `data=416`,
  `bss=291,756`.
- `make img`: PASS. `build/LXRV2_lxr02.img` generated at 452,188 bytes.
- New retained RAM: 0 bytes. Hardware scenarios below remain pending.

### Scenario 1 — Kit Load → VOICE button → power cycle (primary defect)

1. Load a Kit on Load:[Kit] (dirties HCNAMES).
2. Press a VOICE button to enter nested Instrument Load.
3. Power cycle.
4. On reboot, confirm the loaded Kit's name appears correctly in HCNAMES.

### Scenario 2 — Instrument Load → different VOICE → power cycle

1. Enter Instrument Load via VOICE button.
2. Load an Instrument on voice 1 (dirties HCNAMES).
3. Press a different VOICE button (voice 2).
4. Power cycle.
5. On reboot, confirm voice 1's loaded Instrument name persists in HCNAMES.

### Scenario 3 — Instrument Load → exit to Kit → power cycle

1. Enter Instrument Load via VOICE button.
2. Load an Instrument (dirties HCNAMES).
3. Press the mode button to exit back to Kit Load.
4. Power cycle.
5. On reboot, confirm the loaded Instrument's name persists in HCNAMES.

### Scenario 4 — Kit Load → Pot-1 to Instrument → power cycle

1. Load a Kit on Load:[Kit] (dirties HCNAMES).
2. Turn Pot-1 to an Instrument Load row.
3. Power cycle.
4. On reboot, confirm the loaded Kit's name persists.

### Clean paths (no delay expected)

- Voice-button entry with no dirty mask → proceeds immediately.
- Instrument exit with no dirty mask → proceeds immediately.
- Voice-button entry while a Kit/Instrument load is in-flight → consumed
  and deferred until the load completes (no hang).

---

## Appendix — Flush Completion Must Reach Destination Context

### Implementation Notes — Appendix pass, 2026-09-23

- Confirmed the completion callback dispatches from live Menu state rather
  than a retained transition object: `menu_instrumentLoadActive` selects
  Instrument re-entry, while Kit/KitMrp `menu_saveOptions.what` selects the
  top-level Kit path.
- The Appendix fix will therefore install the complete destination context in
  each caller before starting the existing HCNAMES write. This uses only
  existing Menu state and keeps the flush callback/API unchanged.
- The outgoing Instrument temporary snapshot is cleared before the new
  context is installed; this is RAM-only and independent of HCNAMES card I/O.

### Problem

The §2/§3/§4/§6 changes flush dirty HCNAMES rows before the destination
state is set up. When the flush starts, the function returns early. The
flush completion callback (`menu_residentNameScratchFlushComplete`, line
4671) dispatches based on the current `menu_instrumentLoadActive` state —
which is still the *old* context. The user must press the button a second
time to reach the intended destination.

This affects all four sites:

| Site | Returns before | Completion sees | Dispatches to |
|------|---------------|-----------------|---------------|
| §2 VOICE entry (from Kit) | `instrumentLoadActive = 1u` | `instrumentLoadActive == 0` | Kit re-entry (wrong) |
| §2 VOICE switch (voice→voice) | new voice setup | old voice context | old Instrument re-entry (wrong) |
| §3 Pot-1 Instrument Load | `instrumentLoadActive = 1u` | `instrumentLoadActive == 0` | Kit re-entry (wrong) |
| §4 Pot-1 Instrument Save | `instrumentLoadActive = 1u` | `instrumentLoadActive == 0` | Kit re-entry (wrong) |
| §6 exit to Kit | `instrumentLoadActive = 0u` | `instrumentLoadActive == 1` | Instrument re-entry (wrong) |

### Reference

`menu_instrumentLoadStepType()` (line 5750) does this correctly: it sets
the destination state (`menu_instrumentLoadMorphMode`, type, etc.) BEFORE
calling `menu_endResidentNameScratchSession()`. The completion callback
sees `menu_instrumentLoadActive == 1` and the new morph/type state, and
dispatches to `menu_requestInstrumentEntryNames()` with the right context.

### Fix

At each site, move the full destination state setup BEFORE the
`menu_endResidentNameScratchSession()` call. The state setup is pure RAM
(no filesystem I/O), so it is safe to execute before the flush starts.
The flush writes the dirty mask's HCNAMES rows to the card; it does not
read or depend on Menu's Instrument/Kit browser state.

The generation increment stays where it is (before the state setup).
`menu_invalidateInstrumentLoadTemp()` also stays early — it clears the
temp snapshot for the outgoing context, which must happen before the new
context is set up.

---

### A1. MODIFY: Voice-button entry — move state setup before flush — `menu.c:6359`

**Operation:** MODIFY `menu_loadInstrumentVoicePressed()`. Move the entire
destination state block (lines 6378–6412, from `menu_invalidateInstrumentLoadTemp()`
through `menu_instrumentLoadClampIndex()` plus voice LED setup) to BEFORE
the `menu_endResidentNameScratchSession()` call. The flush return keeps its
`return 1u` but now returns after the destination is already installed.

**After restructure:**
```c
    menu_selectionGeneration++;
    /* Set up the destination voice/type/mode context first, so that when
     * the flush completion callback dispatches at line 4671, it sees the
     * Instrument context and calls menu_requestInstrumentEntryNames()
     * with the correct voice, type, and mode already installed. */
    menu_invalidateInstrumentLoadTemp();
    menu_instrumentLoadActive = 1u;
    menu_instrumentSaveMode = (uint8_t)(menu_activePage == SAVE_PAGE);
    menu_instrumentLoadSlot = voiceNr;
    menu_instrumentLoadScene = scene_getActiveIndex();
    menu_loadSaveSourceScene = menu_instrumentLoadScene;
    menu_kitLoadSceneMask = (uint16_t)(1u << menu_instrumentLoadScene);
    menu_instrumentLoadSource = MENU_INSTRUMENT_SOURCE_KIT;
    menu_saveOptions.what = SAVE_TYPE_KIT;
    menu_saveOptions.state = SAVE_STATE_EDIT_TYPE;
    editModeActive = 1u;
    slot = scene_instrumentSlotConst(menu_instrumentLoadScene, voiceNr);
    menu_instrumentLoadType = slot ? slot->type : INSTRUMENT_TYPE_DRM;
    if (!instrumentManager_typeSelectableForSceneSlot(
            menu_instrumentLoadScene, voiceNr, menu_instrumentLoadType)) {
        menu_instrumentLoadType = INSTRUMENT_TYPE_DRM;
    }
    menu_instrumentLoadBaseType = menu_instrumentLoadType;
    menu_instrumentLoadMorphMode = 0u;
    menu_invalidateInstrumentLoadTemp();
    menu_instrumentLoadClampIndex();
    menu_setActiveVoice(voiceNr);
    menu_refreshLoadSceneLeds();
    if (menu_residentNameDirtySceneMask != 0u &&
        menu_endResidentNameScratchSession())
        return 1u;
    /* No flush needed or facade refused; proceed directly to entry names. */
    menu_requestInstrumentEntryNames();
    if (!menu_storageBusy)
        menu_repaintAll();
    return 1u;
```

**What:** The full destination state (voice, type, mode, Scene, LEDs) is
installed before the flush. The flush's completion callback at line 4673
sees `menu_instrumentLoadActive == 1u` and calls
`menu_requestInstrumentEntryNames()`, which enters the Instrument browser
for the voice/type the user selected — no second button press needed.

**Why:** The original §2 fix returned before setting any destination state,
forcing the completion callback to dispatch to the old context. This
matches the pattern established by `menu_instrumentLoadStepType()` (line
5750), where the morph/type state is set before the flush.

**Interaction:** `menu_invalidateInstrumentLoadTemp()` runs twice — once
for the outgoing context (before state setup) and once after the type is
resolved (line 6404 in the original code). Both are idempotent RAM clears.
The voice LED and Scene LED updates are visual-only and safe to run before
the flush.

---

### A2. MODIFY: Pot-1 Instrument Load — move state setup before flush — `menu.c:6685`

**Operation:** MODIFY `menu_loadSaveEnterInstrumentLoad()`. Same pattern
as A1: move the state setup (lines 6691–6709) to before the flush call.

**After restructure:**
```c
    menu_selectionGeneration++;
    menu_invalidateInstrumentLoadTemp();
    menu_activePage = LOAD_PAGE;
    menu_instrumentLoadActive = 1u;
    menu_instrumentSaveMode = 0u;
    menu_instrumentLoadSlot = voice;
    menu_instrumentLoadScene = scene_getActiveIndex();
    menu_loadSaveSourceScene = menu_instrumentLoadScene;
    menu_kitLoadSceneMask = (uint16_t)(1u << menu_instrumentLoadScene);
    menu_instrumentLoadSource = MENU_INSTRUMENT_SOURCE_KIT;
    menu_saveOptions.what = SAVE_TYPE_KIT;
    menu_saveOptions.state = SAVE_STATE_EDIT_TYPE;
    editModeActive = 1u;
    menu_instrumentLoadRefreshBaseType(0u);
    if (menu_instrumentLoadOptionAt(voice, option, &type, &morph)) {
        menu_instrumentLoadType = type;
        menu_instrumentLoadMorphMode = morph;
    }
    menu_invalidateInstrumentLoadTemp();
    menu_instrumentLoadClampIndex();
    menu_setActiveVoice(voice);
    menu_loadSaveSetInstrumentVoiceLed(voice);
    menu_refreshLoadSceneLeds();
    if (menu_residentNameDirtySceneMask != 0u &&
        menu_endResidentNameScratchSession())
        return;
    menu_requestInstrumentEntryNames();
```

**What/Why:** Same as A1. The completion callback sees
`menu_instrumentLoadActive == 1` and the correct voice/type, dispatches to
the Instrument browser directly.

---

### A3. MODIFY: Pot-1 Instrument Save — move state setup before flush — `menu.c:6733`

**Operation:** MODIFY `menu_loadSaveEnterInstrumentSave()`. Same pattern.

**After restructure:**
```c
    menu_selectionGeneration++;
    menu_invalidateInstrumentLoadTemp();
    menu_activePage = SAVE_PAGE;
    menu_instrumentLoadActive = 1u;
    menu_instrumentSaveMode = 1u;
    menu_instrumentLoadSlot = voice;
    menu_instrumentLoadScene = scene_getActiveIndex();
    menu_loadSaveSourceScene = menu_instrumentLoadScene;
    menu_kitLoadSceneMask = (uint16_t)(1u << menu_instrumentLoadScene);
    menu_instrumentLoadSource = MENU_INSTRUMENT_SOURCE_KIT;
    menu_saveOptions.what = SAVE_TYPE_KIT;
    menu_saveOptions.state = SAVE_STATE_EDIT_TYPE;
    editModeActive = 1u;
    menu_instrumentLoadRefreshBaseType(0u);
    menu_instrumentLoadMorphMode = morph ? 1u : 0u;
    menu_setActiveVoice(voice);
    menu_loadSaveSetInstrumentVoiceLed(voice);
    menu_refreshLoadSceneLeds();
    if (menu_residentNameDirtySceneMask != 0u &&
        menu_endResidentNameScratchSession())
        return;
    /* Pot-1 Save follows the same session entry as VOICE-button entry. */
    menu_requestInstrumentEntryNames();
```

---

### A4. MODIFY: Instrument exit to Kit — move teardown before flush — `menu.c:6301`

**Operation:** MODIFY `menu_loadInstrumentExit()`. Move the exit teardown
(`menu_invalidateInstrumentLoadTemp()`, `menu_instrumentLoadActive = 0u`,
`menu_instrumentSaveMode = 0u`, `menu_loadSaveClearInstrumentVoiceBlinks()`)
to BEFORE the flush call.

**After restructure:**
```c
    menu_selectionGeneration++;
    /* Tear down the Instrument context first so the flush completion
     * callback sees menu_instrumentLoadActive == 0 and dispatches to
     * menu_requestKitEntryNames() instead of re-entering Instrument. */
    menu_invalidateInstrumentLoadTemp();
    menu_instrumentLoadActive = 0u;
    menu_instrumentSaveMode = 0u;
    menu_loadSaveClearInstrumentVoiceBlinks();
    menu_refreshLoadSceneLeds();
    if (menu_residentNameDirtySceneMask != 0u &&
        menu_endResidentNameScratchSession())
        return;
    /* No flush needed or facade refused; proceed directly to Kit names. */
    menu_requestKitEntryNames();
    if (!menu_storageBusy)
        menu_repaintAll();
```

**What:** The exit teardown runs before the flush. When the flush completes,
`menu_residentNameScratchFlushComplete()` at line 4675 sees
`menu_instrumentLoadActive == 0` and `menu_saveOptions.what == SAVE_TYPE_KIT`,
so it calls `menu_requestKitEntryNames()`. The Kit browser opens
immediately after the ~100ms write — no second mode-button press.

**Why:** The outgoing `.hcindex` cache is invalidated by the flush
completion at line 4668 (`filesystem_clearNameCache()`), not by Menu's
state teardown. `menu_invalidateInstrumentLoadTemp()` only clears RAM
flags (the temp name buffer, temp type, temp valid flag). Moving it before
the flush is safe because the flush writes HCNAMES (identity rows), not
the temp snapshot.

---

### Summary of Appendix Changes

| # | Site | Change |
|---|------|--------|
| A1 | VOICE button entry | State setup before flush |
| A2 | Pot-1 Instrument Load | State setup before flush |
| A3 | Pot-1 Instrument Save | State setup before flush |
| A4 | Instrument exit to Kit | Teardown before flush |

**Principle:** The flush completion callback's dispatch (line 4671) must
see the *destination* context, not the *source* context. Every flush site
must install its destination state before calling
`menu_endResidentNameScratchSession()`, matching the pattern already
established by `menu_instrumentLoadStepType()`.

**No new RAM, no new API, no change to the flush completion callback
itself.** The fix is purely ordering within the caller.

### Appendix implementation notes — 2026-09-23 completion

- Implemented A1–A4 in `menu.c`. VOICE and Pot-1 Instrument entry now install
  the selected page, voice, type, mode, LEDs, and scene mask before the flush;
  Instrument exit clears the outgoing nested context before the flush.
- The existing `menu_residentNameScratchFlushComplete()` now observes the
  destination context naturally: Instrument entry resumes typed Instrument
  names, while Instrument exit resumes the Kit browser. No callback API,
  transition object, or new RAM was added.
- `make all`, `make img`, and `git diff --check` pass after the Appendix pass.
  Hardware validation of the four Appendix scenarios remains pending.
