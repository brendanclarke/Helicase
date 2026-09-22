# S070 Phase 2 — Load/Save Revision Pass

Session: S070
Branch: `dev-ph5-effects`
Baseline: commit `65e73c6` (Phase 1 complete)
Source: `S070_SYSTEMS_GENERAL_CHECK_AND_REVIEW_PLAN.md` Phase 2,
`AUTOSAVE_TEST_CASES_LOAD_SAVE_REVISIONS.md` (deleted in doc cleanup, recovered
from prior commit). Authoritative specs: `FILESYSTEM_SPEC.md`,
`AUTOSAVE.md`, `MODULE_INTERCHANGE_SPEC.md`.

---

## Scope

Four user-visible items under one selection-coordinate architecture:

1. **LSR-01**: checkpoint dirty Kit/Instrument HCNAMES state before browser
   cache/domain handoff, asynchronously and without changing Morph identity.
2. **LSR-02**: detach page repaint/exit from HCNAMES persistence so the old
   Load screen cannot remain visible while storage drains.
3. **LSR-03**: standardize blank as "not ready" and `Empty` as "current index
   proved absent." Disable commit until the displayed coordinate is resolved.
4. **LSR-04**: make every browser publish selection immediately, request name
   and optional preview asynchronously, tag results with a generation, and
   discard stale callbacks.

These four items are architecturally coupled: they share a single
selection-coordinate model and should be implemented together, not piecemeal.

---

## Current Architecture Summary

### HCNAMES Memory Layout

| Buffer | Variable | Size | Location |
|--------|----------|------|----------|
| Name mirror | `hcnames_name_mirror[145][9]` | 1,305 B | `filesystem.c:1039` (SRAM1 .bss) |
| Source/provenance register | `fs_resident_source[145]` | 290 B | `filesystem.c:1020` (SRAM1 .bss) |
| Validity gate | `hcnames_mirror_valid` | 1 B | `filesystem.c:1050` |
| Active identity block | `fs_identity_name[8][9]` | 72 B | `filesystem.c:1068` |
| Browser name cache | `fs_list_cache_name[1000][9]` | 9,000 B | `filesystem.c:1012` |

The 1,305-byte name mirror and the 290-byte source register are the dedicated
HCNAMES SRAM (Session 058 Option 1C). They are completely separate from the
9,000-byte browser cache. There is no second copy, rollback buffer, snapshot
mechanism, or undo log for HCNAMES.

### HCNAMES Publication Flow

| Operation | HCNAMES write timing |
|-----------|---------------------|
| Kit Load/Save | **Deferred** to family/page exit via `menu_endResidentNameScratchSession()` |
| Instrument Load/Save | **Deferred** to family/page exit (same path) |
| Scene Load | **Immediate** via `filesystem_cacheCurrentResidentSceneNames()` + Scene-op safe-write |
| Scene Save | **Immediate** (same Scene-op path) |
| Bank Load | **Immediate** per-child overlay, single rewrite at end |
| Bank Save | **Immediate** (full-register rewrite) |
| AutoSave convergence | Post-drain: clears `R` flag for fully-captured rows |

The Kit/Instrument deferral is the root of the LSR-01 problem: dirty identity
accumulates in `menu_residentNameDirtySceneMask` (a `uint16_t` in `menu.c`)
until exit calls `filesystem_requestUpdateResidentKitNames()` for one atomic
HCNAMES rewrite. A type switch or power loss before that exit loses the
identity update.

### AutoSave / Load/Save Exclusion Gates

Six layers prevent concurrent access:

1. **Page-level**: scalar/Pattern drain schedulers check
   `menu_activePage == LOAD_PAGE || SAVE_PAGE` (filesystem.c:24320).
2. **Command-level**: `menu_isLoadSaveCommandActive()` gates settings writer
   and Pattern drains (filesystem.c:23817, 24068, 24429).
3. **Setup suppression**: `filesystem_ensureAutosaveFiles_tick()` defers on
   Load/Save page (filesystem.c:24272).
4. **Shared facade**: `filesystem_start()` rejects on `BUSY`; single-owner.
5. **Facade acknowledgement**: `filesystem_ack()` protocol returns facade to
   `IDLE` before background schedulers can acquire it.
6. **Budget gate**: `filesystem_backgroundBudgetAvailable()` limits all
   background work (scalar/Pattern drain, Pattern repair).

An already-admitted AutoSave transaction runs to its safe close boundary — it
is not cancelled. The page rule is a deferment, not a discard.

### Page-Exit Expedite (Session 056)

When `fs_autosave_page_suppressed` is set during Load/Save browsing, the first
scheduler tick after page exit clears it and resets the AutoSave deadline to
`now + 250ms` (vs. the normal 5-second interval). This eliminates wasted
debounce time. The expedite covers AutoSave scalar/Pattern drains only — it
does not cover HCNAMES writes (those are handled by the deferred-session
exit path via `menu_residentNameScratchFlushComplete()`).

### Browser Selection Model

The browser uses a single-owner 9,000-byte name cache with domain tagging
(`fs_list_cache_kind`). Key flow:

1. Encoder movement updates `menu_currentPresetNr[what]`.
2. `menu_requestCurrentLoadSaveSelection()` publishes the name **synchronously**
   from the resident `.hcindex` cache if loaded, or sets blank (spaces) if not.
3. Kit on LOAD_PAGE: immediate payload load via `preset_loadKitForScenes()`.
4. Scene/Bank: name-only on scroll, payload on explicit OK.
5. Deferred retry: `menu_deferSelectionRequest` coalesces to latest position.

There is **no explicit generation tag** on the browser cache. Staleness is
managed by domain identity (`fs_list_cache_kind`) and `menu_storageBusy`
input gating.

### Blank vs. Empty Display States

| State | Meaning | When displayed |
|-------|---------|----------------|
| Blank (spaces) | "Not yet resolved" | Cache not loaded, HCNAMES owns cache, domain switching |
| `Empty   ` | "Index proved absence" | `.hcindex` loaded, slot has no directory |

The `filesystem_*SlotName()` accessors return `"Empty   "` only when
`!filesystem_*SlotExists(slot)` after a valid cache load. Blank is the default
during transitions.

---

## LSR-01: HCNAMES Checkpoint Before Cache/Domain Handoff

### Problem Statement

After a Kit or Instrument Load commit, the name/source/refreshed state is
updated in SRAM but the durable `.hcnames` rewrite is deferred to family/page
exit. If the user changes browser type (Kit → Instrument, or switches
Instrument type) the `.hcindex` cache domain changes, but the dirty HCNAMES
rows have not yet been persisted. A power loss or hard fault at this point
loses the identity update entirely.

### What the Code Does Today

- `menu_refreshResidentNameScratchKit(scene_mask)` (`menu.c:4546`): ORs into
  `menu_residentNameDirtySceneMask`. Does NOT write HCNAMES.
- `menu_refreshResidentNameScratchInstrument(scene_mask, slot)` (`menu.c:4569`):
  Same accumulation. Does NOT write HCNAMES.
- `menu_endResidentNameScratchSession()` (`menu.c:4656`): Called at family/page
  exit. If dirty mask nonzero, calls
  `filesystem_requestUpdateResidentKitNames(mask, callback)` for one atomic
  safe-write.
- The 72-byte active identity block (`fs_identity_name`) is updated at commit
  time, so the RAM state is always current. Only the on-card `.hcnames` lags.

### Key Design Decisions Required

**Q1: Separate checkpoint buffer or mark-and-flush?**

The plan asks: "Clarify whether the checkpoint is a separate buffer or a
mark-and-flush of the existing one."

**Finding:** A separate buffer is not needed. The 1,305-byte HCNAMES name
mirror and 290-byte source register already hold the committed state. The
problem is not that the data is unavailable — it's that the write is
deferred too long. The fix is to trigger the safe-write earlier (at browser
domain transition), not to add a snapshot.

A mark-and-flush approach using the existing `menu_residentNameDirtySceneMask`
is architecturally correct: the mask already tracks exactly which Scenes need
their Kit + 6 Instrument rows rewritten. The flush should fire at domain
transitions (type switch, cache handoff) rather than only at page exit.

**Recommendation:** Mark-and-flush. No new buffer allocation needed.

**Q2: Should the flush block the UI?**

No. The spec requires "without blocking page repaint." The flush request
should be queued to the facade; if the facade is busy, the dirty mask persists
until the next idle opportunity. `menu_storageBusy` gates further cache-
destructive operations until the flush completes. The existing
`menu_residentNameScratchFlushComplete()` callback is the right completion
path.

**Q3: Morph identity isolation.**

Morph Load/Save must never turn Morph endpoint data into a Normal Instrument
identity. The current code already handles this: `menu_refreshResidentNameScratchInstrument()`
is called only from the Normal Instrument commit path, not from
`InstrumentMrp` commit. Verify this remains true after any refactoring.

### Risk Assessment

| Risk | Severity | Mitigation |
|------|----------|------------|
| Flush request arrives while facade is busy with a Load/Save command | Medium | Retain dirty mask, retry on next idle. Already the pattern for the exit path. |
| Scene-to-Kit family type boundary during HCNAMES flush | Low | Existing `menu_switchPage()` already handles flush-then-re-enter. Verify the domain transition case follows the same ordering. |
| Dirty mask lost on power failure between commit and flush | Medium | Inherent in the current architecture. The `R` refreshed flag in the source register provides a recovery witness: boot reader will narrow-load any `R`-marked row from the library. |
| Multiple flushes coalesce incorrectly | Low | Use the same one-dirty-mask / one-rewrite model. Each flush replaces all dirty rows; additional commits that land during a flush re-dirty the mask for the next cycle. |

---

## LSR-02: Detach Page Repaint/Exit from HCNAMES Persistence

### Problem Statement

On leaving Kit/Instrument Load, the old menu screen remains painted while
HCNAMES persistence runs. The user sees a visible "exit hang" because the page
switch is deferred by `menu_storageBusy` while the HCNAMES write completes.

### What the Code Does Today

- `menu_endResidentNameScratchSession()` (`menu.c:4656`) sets
  `menu_storageBusy = 1u` and starts the HCNAMES write.
- `menu_residentNameScratchFlushComplete()` (`menu.c:4597`) calls
  `filesystem_ack()`, clears dirty/session state, and then either re-enters
  the next browser context or calls `menu_repaintAll()`.
- `menu_processPendingPageSwitch()` (`menu.c:10047`) holds the page switch
  until `!menu_storageBusy`.
- The LCD repaint cycle is blocked by the busy flag.

### Key Design Decisions Required

**Q4: Complete the write before releasing, or defer to next idle cycle?**

The plan asks: "If the user exits Load/Save while an HCNAMES write is in
flight, the facade must either complete the write before releasing or defer it
to the next idle cycle."

**Finding:** The Session 056 page-exit expedite handles AutoSave drain
deferral but does **not** cover HCNAMES writes. The HCNAMES write is driven by
`menu_endResidentNameScratchSession()`, not by the AutoSave scheduler.

The correct fix is to **detach the page switch from the HCNAMES write**:

1. Tear down the browser and paint the destination page on the normal UI
   refresh cadence (immediately release `menu_storageBusy` for the page
   transition).
2. Queue the HCNAMES write independently. It runs when the facade is next
   idle.
3. The dirty mask persists until the write succeeds. If the user re-enters
   the Kit/Instrument browser before the write completes, the pending flush
   joins the new session naturally.

**Recommendation:** Defer-to-idle. This is safer than trying to complete an
in-progress write at an arbitrary point; the `R` refreshed flag provides boot
recovery if power is lost before the deferred write lands.

**Q5: Interaction with AutoSave page-exit expedite.**

Once the page switch happens immediately (before the HCNAMES write), the
AutoSave page-exit expedite fires earlier: the writer sees `menu_activePage !=
LOAD_PAGE` and begins its 250ms countdown. The HCNAMES write and the AutoSave
drain could both want the facade. The facade's single-owner `BUSY` rejection
prevents a race, but ordering matters:

- If the HCNAMES write wins the facade first, the AutoSave drain waits for
  it to complete + acknowledge, then runs on the next tick.
- If the AutoSave drain wins first, the HCNAMES write waits and retries.

Both orderings are safe as long as the HCNAMES write is not abandoned. The
dirty mask provides the persistence guarantee.

### Risk Assessment

| Risk | Severity | Mitigation |
|------|----------|------------|
| Page switch races with in-flight HCNAMES write | Medium | The facade is single-owner. The write either finishes and is acknowledged, or is not yet started. A pending-but-not-started write becomes a deferred request. |
| Deferred write stalls indefinitely because AutoSave keeps winning the facade | Low | AutoSave drains have bounded duration. The HCNAMES write is a small file (145 rows ≈ 2 KB). Priority: the HCNAMES write should be scheduled before the next AutoSave drain when both are pending. |
| User re-enters Kit/Instrument before deferred write completes | Medium | The pending dirty mask is carried forward. The re-entry reads HCNAMES from the mirror (RAM), not from the card. The write completes alongside or before the next exit. |
| Power loss between page exit and deferred HCNAMES write | Medium | The `R` refreshed flag in the source register persists on the card from the previous HCNAMES write. Boot reader uses Case 2 (narrow library load) for `R`-marked rows, recovering the correct identity. Only the name text may be stale; the source/provenance (the structurally important field) was staged at commit time with the `FS_RESIDENT_SOURCE_DIRTY_FLAG` protection. |

---

## LSR-03: Blank / Empty Coordinate Discipline

### Problem Statement

Some browsers (notably Bank) temporarily display `Empty` for occupied slots
until the scroll list refreshes. This conflates "the index cannot answer yet"
and "the index proves absence."

### What the Code Does Today

All four `filesystem_*SlotName()` accessors (`filesystem.c:30638-30747`)
return `"Empty   "` when either:
- The slot is out of range or the cache count is zero, OR
- The cache row is blank/null.

This does not distinguish "cache not loaded" from "cache loaded, slot absent."

The blank state (8 spaces) is used when `menu_storageBusy` and the cache
domain doesn't match (`menu.c:4274`), but this path is not consistently
applied across all browsers.

### Key Design Decisions Required

**Q6: Should OK/Load/Save be disabled or silently refused?**

The plan says "disable commit until the displayed coordinate is resolved."

**Recommendation:** Disable. The encoder can scroll freely, but the OK button
should be non-responsive (no facade request, no error overlay) while the
coordinate is unresolved. This is simpler than refusing and showing an error.
The existing `menu_storageBusy` flag already blocks input in many paths; the
additional check is whether the name cache domain matches and the slot has been
positively resolved.

**Q7: What about the Save page?**

On the Save page, the slot name seeds the editor but does not gate the save
operation (the user types a new name). An unresolved slot should show blank and
the editor should not auto-populate from `Empty`. The existing code at
`menu.c:9509-9512` already seeds from the resident identity, not from the
browser slot — verify this remains correct.

### Risk Assessment

| Risk | Severity | Mitigation |
|------|----------|------------|
| User presses OK during the blank window and nothing happens | Low | Expected behavior. The blank state is brief (index load is ~50-100ms for typical libraries). No visible error needed. |
| Rapid scrolling keeps the coordinate perpetually unresolved | Low | The deferred-selection retry (`menu_deferSelectionRequest`) coalesces to the latest position. Only the final position needs resolution. |
| Instrument type switch during blank window | Low | Type switch disposes the cache domain and triggers a new index load. The coordinate resets to blank naturally. |

---

## LSR-04: Asynchronous Selection Pipeline with Generation Tagging

### Problem Statement

Bank Load scrolls more slowly between occupied slots than empty ones because
occupied slots trigger a child-Scene scan that asserts `menu_storageBusy`. The
user cannot scroll past an occupied slot without waiting for the preview to
complete.

### What the Code Does Today

- `menu_requestBankLoadPreview()` (`menu.c`): absent slots finish locally but
  occupied slots trigger a child-Scene scan, gating input.
- Kit on LOAD_PAGE: scroll triggers an immediate payload load
  (`preset_loadKitForScenes()`), which also gates input.
- Scene: scroll shows name only from cache (no payload), OK triggers load.
- There is **no generation tag** on the browser cache or on async callbacks.
  Staleness is managed by domain identity and `menu_storageBusy` blocking.

### Key Design Decisions Required

**Q8: What constitutes a "generation" and where does it live?**

**Recommendation:** A single `uint8_t` wrapping counter in `menu.c`, incremented
on every encoder movement or type switch. Every async filesystem request
(index load, preview scan, name lookup) captures the generation at request
time. The completion callback compares its captured generation against the
current value; a mismatch discards the result silently.

This is simpler than tagging every cache row — only the request/callback
boundary needs the generation.

**Q9: Stale callback after user commits.**

The plan asks: "explicit handling for the case where the user has already
committed a selection before the name arrives — the displayed name must not
retroactively change after commit."

**Finding:** This is a real risk. If the user presses OK while a stale name
callback is still in flight, the callback could overwrite the committed
selection's display name. The fix: commit must freeze the displayed name.
After OK is accepted, any pending name callback for a prior generation is
discarded as stale. The committed name is set at OK time from the identity
store (the filesystem's committed source of truth), not from the async
callback.

**Recommendation:** On OK, capture the committed name from the HCNAMES
identity block or the active `.hcindex` row and set it as the display name.
Subsequent callbacks for any generation ≤ the commit generation are discarded.

**Q10: Async loads for all four live browser types. (RESOLVED)**

All four "live" browser types — Kit, Instrument, KitMrp, InstrumentMrp —
must load asynchronously from the menu scroll:

1. **Scroll publishes immediately**: slot number and name are updated on the
   display without waiting for the payload load.
2. **Load starts instantly**: the payload load begins on the facade as soon
   as the selection changes, but does **not** set `menu_storageBusy` for
   input gating. The menu encoder remains responsive.
3. **Dispose on supersession**: if the user scrolls to another slot before
   the in-flight load completes, the completed result is disposed (generation
   tag mismatch in callback). The facade runs the load to its safe close
   boundary — it cannot be cancelled mid-stream — but the result is reverted
   instead of applied.
4. **Latest wins, queue depth 1**: at most one in-flight load plus one
   deferred selection exist. `menu_deferSelectionRequest` coalesces to the
   latest encoder position, so rapid scrolling produces at most one stale
   completion before the final slot's load starts.

**Top-slot restore for all four live types**: the top slot (above slot `000`
for Kit, the `kit` row for Instrument) always restores the original settings
as they were on entry to the browser. This already works for Kit and
Instrument (Normal) via the existing entry-snapshot / `.hctmp` mechanism.
KitMrp and InstrumentMrp must gain the same restore-on-top-slot behavior
for consistency.

**Exit ordering constraint**: if the user exits the page (or switches browser
type) while a load is in flight, the in-flight load must complete first (the
facade cannot be abandoned mid-stream), then HCNAMES checkpoint (LSR-01),
then the exit completes. The completed load result is disposed (not applied)
because the user is leaving. The exit path sees a brief wait while the
in-flight load finishes, but this is bounded by the single-facade completion
time, not by user interaction.

### Risk Assessment

| Risk | Severity | Mitigation |
|------|----------|------------|
| Stale callback after commit overwrites display name | High | Freeze display name at OK time. Discard callbacks for prior generations. |
| Generation counter wraps and a very old callback matches | Negligible | `uint8_t` wraps every 256 scrolls. The facade is single-owner and callbacks complete in <500ms. A 256-scroll queue is not physically possible. |
| Async Bank preview returns for wrong slot after rapid scrolling | Medium | Generation tag comparison in callback. Stale results are silently dropped. Latest-selection-wins is already the deferred-retry policy. |
| Multiple async requests in flight simultaneously | Low | The facade is single-owner; only one filesystem request runs at a time. `menu_deferSelectionRequest` coalesces to the latest position. At most one stale and one current request can exist. |
| Rapid Kit/Instrument loads cause DSP glitches during fast scroll | Medium | Each load applies through the normal Preset commit path with descriptor image write. A disposed load reverts to the entry snapshot. The DSP sees at most one transient state per facade cycle. |
| KitMrp/InstrumentMrp top-slot restore not yet implemented | Medium | Requires entry-snapshot caching for Morph endpoints. Kit/Instrument Normal already have this. Implementation must follow the same pattern: cache on entry, restore on top-slot select. |
| Exit waits for in-flight load before HCNAMES checkpoint | Low | The wait is bounded by one facade operation (~50-200ms for a Kit load). This is much shorter than the current visible "exit hang" from HCNAMES persistence blocking the page switch. |

---

## Cross-Cutting Risks and Architecture Questions

### R1: HCNAMES Publication Race with AutoSave Convergence

**Risk:** After an AutoSave drain captures all dirty payload bytes for an
object, `filesystem_autosaveDrainAfterCommit()` triggers an HCNAMES convergence
write that clears the `R` flag for fully-captured rows. If a Load/Save commit
has just set `R` on those rows and the HCNAMES name/source was staged but not
yet written (deferred to exit), the convergence write could:

1. See the new `R` flag.
2. Check `autosave_objectFullyCaptured()` — returns false because the new
   commit just dirtied the mask.
3. Leave `R` set (correct behavior).

**Assessment:** The existing design is safe for this case. The convergence
write only clears `R` when the object's entire wire interval is clean. A fresh
Load/Save commit dirties the entire object, so the convergence check will
correctly leave `R` set. The risk is theoretical, not practical.

However, if LSR-02 causes the page exit to happen *before* the HCNAMES write
(deferred-to-idle model), and the AutoSave convergence write runs first, the
convergence write will use the name mirror contents that were updated at commit
time. This is correct — the mirror is always current. The deferred HCNAMES
write will then also succeed, writing the same (current) mirror state.
Harmless duplicate write, not a race.

**Status:** SAFE under both current and proposed architectures. No action
needed.

### R2: Bank Identity Agreement After Refactoring

**Invariant:** After any Load/Save, `settings.cfg` HCNAMES row 0, and HCPR
must all agree on the active Bank identity.

**Finding:** Bank Load/Save call `bank_setRestoreBankSlot()` and
`filesystem_markSettingsDirty()` together (`menu.c:3041`,
`filesystem.c:2754`). The settings scheduler owns asynchronous `active_bank`
serialization. HCNAMES row 0 is updated by the Bank Load/Save HCNAMES writer.
HCPR captures the Bank identity through the normal scalar dirty mask.

**Risk:** The LSR refactoring changes Kit/Instrument HCNAMES publication timing
but does **not** change Bank/Scene HCNAMES publication, which is already
immediate. The Bank identity invariant is not affected by the LSR items.

**Status:** SAFE. No action needed. Add a defensive assertion or trace
check at the Load/Save completion boundary if desired.

### R3: Corrupt/Partial Pattern Inside Scene Loads

**Invariant:** Any component failure of a Scene (settings, Kit, Pattern, or
Effect in the future) invalidates the entire Scene — it must load empty. This
guards against the user overwriting valid resident data with a partially
loaded Scene. A Scene with a corrupt embedded Pattern is not a usable Scene.

**Finding:** The Scene Load state machine uses `op_load_invalid_layer`
(`filesystem.c:1351-1360`) to classify content failures. `FS_LOAD_INVALID_KIT`
and `FS_LOAD_INVALID_SCENE` both lead to the quarantine-eligible decision at
phase 62 (`filesystem.c:13044`). However, no `FS_LOAD_INVALID_PATTERN` layer
exists in the current enum; Pattern is not classified as a failure layer.

The success terminal at phase 61 (`filesystem.c:13001`) publishes the
validated Pattern and Scene payload and marks each selected Scene's hierarchy
refreshed. A Pattern validation failure must **never** reach this terminal.
If it does, the Scene would be partially committed (settings + Kit valid,
Pattern invalid) and the user could overwrite valid library data with it.

**Action required:** Verify and enforce the invariant:

1. Confirm that a failed PAT4 validation (version, size, CRC32C) during
   Scene Load sets `op_close_status = FS_STATUS_ERROR` and routes to the
   Scene-empty path, not to the success terminal.
2. Confirm that the HCNAMES Pattern row is not published with `@` provenance
   for a Pattern that failed validation.
3. If the current code does not enforce this (i.e., Pattern failure is
   silently tolerated and the Scene commits anyway), add
   `FS_LOAD_INVALID_PATTERN` to the `fs_load_invalid_layer_t` enum and route
   Pattern validation failure through the same invalidation path as Kit and
   Scene failures.
4. The same invariant applies to Effect when it gains live parameters in
   Phase 5. A failed `effects.fx` must invalidate the Scene.

**Status:** VERIFICATION NEEDED. The Scene Load state machine is 70+ phases.
A trace walk or hardware test with a corrupted `.pat` file inside a Scene
directory is required to confirm the failure path. If the invariant is not
currently enforced for Pattern, this is a defect to fix in Phase 2.

### R4: Load/Save Exclusion During Active AutoSave Transaction

**Finding:** The six-layer gate system described above prevents new AutoSave
drains from starting during Load/Save. An already-in-progress drain continues
to its safe close boundary and does not interfere.

**Risk specific to LSR:** If the deferred HCNAMES write (LSR-02) runs via the
idle scheduler, it uses the same facade as the AutoSave drain. The facade's
single-owner rejection prevents both from running simultaneously. The
question is priority: should a pending HCNAMES write preempt the AutoSave
scheduler?

**Recommendation:** Give HCNAMES writes a scheduling priority higher than
AutoSave drains but lower than foreground Load/Save commands. This ensures
the user's deferred HCNAMES update lands before the next AutoSave drain runs.
Implementation: check for a pending HCNAMES dirty mask before evaluating the
AutoSave writer's due tick.

**Status:** DESIGN DECISION NEEDED. See Unresolved Questions below.

### R5: Page-Exit vs. Persistence Race (Facade Occupancy)

**Finding:** The facade is single-occupancy. At page exit, these consumers may
contend:

1. The deferred HCNAMES write (dirty Kit/Instrument rows)
2. The AutoSave page-exit expedite (250ms deadline after page exit)
3. Any pending foreground request from the destination page

The facade's `BUSY` rejection prevents true races. The question is whether
the deferred HCNAMES write can be lost by never winning the facade.

**Assessment:** The HCNAMES write is a small operation (~2 KB, one open/write/
close/sync/remove/rename cycle). AutoSave drains are larger and take multiple
ticks. If the HCNAMES write is scheduled first (see R4 recommendation), it
will complete before the AutoSave drain starts. If it's not scheduled first,
the worst case is a one-drain delay (AutoSave drain takes ~500ms to 2s). The
dirty mask persists, so the HCNAMES write will succeed on the next attempt.

**Status:** LOW RISK with the scheduling recommendation from R4.

---

## Resolved Design Decisions

### UQ-1: HCNAMES Checkpoint Trigger Points (LSR-01) — RESOLVED

**Decision:** Option A with dirty-mask guard. Trigger the mark-and-flush at
every browser domain transition (Kit → Instrument type, Instrument type →
type, Kit/Instrument → Scene/Bank), but only if
`menu_residentNameDirtySceneMask != 0`. This preserves zero-write behavior
for clean browsing sessions while ensuring dirty rows are persisted before
cache handoff.

**Additional constraint:** If a live load (Kit, Instrument, KitMrp,
InstrumentMrp) is in flight when the user initiates a domain transition or
page exit, the in-flight load must run to its safe close boundary first. The
completed result is disposed (generation mismatch), then the HCNAMES
checkpoint fires, then the transition/exit proceeds. The ordering is always:
**complete in-flight load → HCNAMES checkpoint → exit/transition**.

### UQ-2: HCNAMES Write Scheduling Priority (LSR-02 / R4) — RESOLVED

**Decision:** Option A. New scheduler rung in `filesystem_tick()` positioned
between foreground commands and AutoSave. Checked every tick when
`menu_residentNameDirtySceneMask != 0` and the facade is idle. This gives
HCNAMES writes scheduling priority over AutoSave drains without coupling
the two systems.

### UQ-3: Async Loads for All Four Live Types (LSR-04) — RESOLVED

**Decision:** All four live browser types — Kit, Instrument, KitMrp,
InstrumentMrp — load asynchronously from the menu scroll. Loads start
instantly on selection, run on the facade without gating the menu encoder,
and are disposed on generation mismatch. See Q10 above for the full
contract.

**Top-slot restore**: the top slot always restores original settings as
they were on entry. Currently implemented for Kit and Instrument (Normal).
KitMrp and InstrumentMrp must gain the same entry-snapshot / restore-on-
top-slot behavior. The Morph entry snapshot must cover Morph endpoint cells
only — it must not overwrite Normal image, HCNAMES identity/source, or
routing.

### UQ-4: Generation Tag Scope (LSR-04) — RESOLVED

**Decision:** Option A. One global `uint8_t` for all browsers, incremented
on any scroll or type switch. Simplicity over marginal savings.

### UQ-5: Scene Component Failure Invariant (R3) — RESOLVED

**Invariant confirmed:** Any component failure of a Scene (settings, Kit,
Pattern, or Effect in the future) invalidates the entire Scene — it must
load empty. This is a data protection invariant: a partially loaded Scene
must never be available for the user to overwrite valid library data with.

**Action:** Verify and enforce this invariant for Pattern (and later Effect).
If the current Scene Load state machine tolerates a corrupt PAT4 and commits
the Scene without it, this is a defect. Add `FS_LOAD_INVALID_PATTERN` to
the layer enum and route Pattern validation failure through the same empty-
Scene path as Kit and Scene-settings failures. See R3 above.

### UQ-6: Test Cases Document — RESOLVED

**Decision:** Do not restore `AUTOSAVE_TEST_CASES_LOAD_SAVE_REVISIONS.md`.
The document is retired. Important test items from it should be folded into
Phase 4 of `S070_SYSTEMS_GENERAL_CHECK_AND_REVIEW_PLAN.md`, specifically:

- AS-BOOT, AS-WRITE, AS-ENABLE matrices → fold into §4.1 or §4.2
- ID-COHERENCE, NAME-PUBLISH, SETTINGS matrices → fold into §4.2 or §4.4
- LSR test clusters → fold into §4.4 (regression validation for Phase 2)
- LS-DATA, LS-SAVE, LS-DELETE, LS-STATE, LS-NAME items → fold into §4.2

---

## Remaining Open Items

1. ~~**R3 — Scene Pattern invalidation path**~~: **VERIFIED SAFE** by code
   trace (see Appendix A) and **CONFIRMED on hardware** (see Appendix A
   Hardware Validation Result). Test fixture `SD_CARD_FULLBAD_TEST/Bank/
   000 FullBad`: child 03 (corrupt PAT4) correctly invalidated, all 15
   valid children loaded successfully.

2. **KitMrp / InstrumentMrp entry snapshot**: the existing Kit/Instrument
   Normal entry-snapshot mechanism (`.hctmp` for Instrument, entry cache for
   Kit) needs to be extended to cover Morph endpoint cells. Verify that the
   existing `.hctmp` infrastructure can be reused or whether a separate Morph
   snapshot is needed.

---

## Execution Strategy

Implementation order:

1. **Add trace infrastructure** (pre-behavior): top-level Load/Save request/
   refusal and HCNAMES publication latency observations. No behavior changes.

2. **Hardware-validate R3** (Scene component failure invariant): load
   `000 FullBad` Bank on hardware, confirm child 03 fails and the remaining
   children load successfully. Code trace verified the invariant holds
   (Appendix A); this step confirms observable behavior.

3. **Implement the selection-coordinate model** (LSR-03 + LSR-04 together):
   generation counter, blank/Empty discipline, disable commit while
   unresolved, async loads for all four live types (Kit, Instrument, KitMrp,
   InstrumentMrp), Bank async preview, top-slot restore for KitMrp and
   InstrumentMrp.

4. **Implement HCNAMES checkpoint** (LSR-01): mark-and-flush at domain
   transitions when dirty mask is nonzero. Enforce the in-flight load →
   HCNAMES → exit ordering. Verify Morph identity isolation.

5. **Detach page exit from HCNAMES persistence** (LSR-02): immediate page
   switch, deferred HCNAMES write via new scheduler rung in
   `filesystem_tick()`.

6. **Fold test items** from the deleted `AUTOSAVE_TEST_CASES_LOAD_SAVE_
   REVISIONS.md` into Phase 4 of the S070 plan.

7. **Run test matrix**: Phase 4 validation covering the LSR behavior changes,
   HCNAMES publication across browser-family changes, async load dispose/
   restore, and the AutoSave interaction matrix.

Steps 3-5 are architecturally coupled and should be implemented in one pass.
Step 1 is a prerequisite for validating steps 3-5. Step 2 is independent and
can be done first or in parallel.

---

## Files Likely Affected

| File | Changes |
|------|---------|
| `Core/Menu/menu.c` | Selection generation counter, async load dispatch/dispose, top-slot entry snapshot for KitMrp/InstrumentMrp, flush trigger points, page-exit detachment (in-flight completion → HCNAMES → exit ordering), blank/Empty discipline, OK disable logic |
| `Core/Hardware/SD/filesystem.c` | Deferred HCNAMES scheduler rung in `filesystem_tick()`, slot-name accessor Empty/blank split, async Bank preview |
| `Core/Hardware/SD/filesystem.h` | New public APIs for deferred HCNAMES check, generation-tagged callbacks |
| `Core/Hardware/SD/storageTypes.h` | Possible new types for selection coordinate |
| `Core/Bank/Scene/Preset/presetManager.c` | Async Kit/Instrument load callbacks, entry snapshot for Morph variants, dispose/revert path for stale completions |

**RAM cost:** One `uint8_t` generation counter in `menu.c`. The deferred
HCNAMES scheduler reuses the existing `menu_residentNameDirtySceneMask` and
`filesystem_requestUpdateResidentKitNames()` infrastructure. KitMrp/
InstrumentMrp entry snapshots may require caching Morph endpoint cells — size
depends on whether the existing `.hctmp` mechanism can be reused (zero
additional SRAM) or a RAM snapshot is needed (up to 72 bytes per voice ×
number of Morph-affected slots). This will be itemized during implementation.

---

## Appendix A: R3 Code Trace — Scene Pattern Failure Path (S070)

### Objective

Walk the Scene Load state machine (`filesystem_loadSceneDirectory_tick()`,
`filesystem.c:11822`) to confirm that a corrupt or truncated `.pat` file
inside a Bank child Scene never reaches the success terminal at phase 61.

### Trace Path

The Bank Load state machine (`filesystem_loadBankDirectory_tick()`,
`filesystem.c:13269`) delegates to the Scene Load for each selected child
(line 13277: `filesystem_loadSceneDirectory_tick()` when
`op_bank_payload_active` is set at phase 18, line 13974).

**Phase 9** (filesystem.c:11982) — SCAN child names. Discovers the first
`.pat`, `.fx`, and `Kit *` children by LFN-aware object iteration. Stores
`.pat` filename in `op_scene_pattern_open_name`. If multiple `.pat` files
exist, rejects immediately with `FS_LOAD_INVALID_SCENE` → phase 10.

**Phase 11** (filesystem.c:12056) — If no `.pat` was found
(`op_scene_pattern_open_name[0] == '\0'`), sets `FS_LOAD_INVALID_SCENE`
→ phase 62. Scene invalidated.

**Phases 12–32** — Parse `sceneset.scg`, open and validate embedded Kit
directory and all Instruments.

**Phase 33** (filesystem.c:12524) — `filesystem_commitSceneStage()`. Commits
validated sceneset + Kit to resident SRAM. Calls `pat_initScene()` for every
selected Scene in the mask (filesystem.c:16271), which zeroes the Pattern
region (addresses → `PAT_ADDR_SENTINEL`, pool → 0, bitmap → free). This is
the non-atomic boundary: settings/Kit are committed before Pattern I/O.

**Phase 44** (filesystem.c:12680) — OPEN the `.pat` file by
`op_scene_pattern_open_name`. If open fails → phase 52 with
`FS_LOAD_INVALID_SCENE`.

**Phase 45** (filesystem.c:12692) — WAIT open. If `op_file` is NULL, sets
`FS_LOAD_INVALID_SCENE` → phase 52. On success, finds the first selected
Scene in the mask, calls `pat_initScene()` again (redundant for the first
Scene, needed if multiple Scenes share one source), initializes CRC
accumulator.

**Phase 46** (filesystem.c:12725) — READ and validate fixed v4 header via
`filesystem_patternHeaderValid()`. If invalid → `FS_LOAD_INVALID_SCENE` →
phase 52. If premature EOF → same path.

**Phase 47** (filesystem.c:12759) — READ header extension. If region is NULL
or premature EOF → `FS_LOAD_INVALID_SCENE` → phase 52.

**Phases 48–50** (filesystem.c:12807) — READ address array, bitmap, pool
**directly into resident Pattern SRAM** via `pat_sceneRegionMut()`. If
region is NULL or premature EOF → `FS_LOAD_INVALID_SCENE` → phase 52.

**Phase 51** (filesystem.c:12864) — VALIDATE CRC32C. If mismatch →
`FS_LOAD_INVALID_SCENE` → phase 52. On success, fan out: `memcpy()` the
first selected Scene's region to all other selected Scenes, set
`op_close_status = FS_STATUS_DONE` → phase 52.

**Phase 52** (filesystem.c:12902) — CLOSE `.pat` file. Calls
`filesystem_patternServiceFinish()` to reopen the Pattern service gate.
Proceeds to phase 53.

**Phase 53** (filesystem.c:12913) — WAIT close. Checks
`op_close_status`:
- `!= FS_STATUS_DONE` → **phase 62** (error terminal). Phase 61 skipped.
- `== FS_STATUS_DONE` → phase 56 (effects).

**Phase 61** (filesystem.c:13001) — Success terminal. Publishes validated
Pattern and Scene, calls `filesystem_setResidentSceneRefreshed()` for each
selected Scene, sets `op_bank_loaded_scene = 1u`. **Only reachable through
phase 60 (successful Effect close) → phase 61.**

**Phase 62** (filesystem.c:13044) — Error/quarantine decision. For Bank
children (`current_op == FS_INTERNAL_OP_LOAD_BANK`), quarantine rename is
skipped (filesystem.c:13068) → phase 72.

**Phase 72** (filesystem.c:13170) — RETURN to Bank loader. For a failed
child (`op_close_status != FS_STATUS_DONE`), restores CWD to root
(filesystem.c:13196) → Bank phase 20.

**Bank Phase 20** (filesystem.c:13979) — Clears the child's bit from
`op_bank_scene_load_mask` (line 13988), sets the bit in
`op_bank_scene_failed_mask` (line 13991). Advances cursor to next child.
The failed Scene is excluded from the Bank's successfully-loaded set.

### Findings

1. **The invariant holds.** A Pattern validation failure at any stage (header
   invalid, CRC mismatch, truncated file, region NULL) sets
   `op_close_status = FS_STATUS_ERROR` and `op_load_invalid_layer =
   FS_LOAD_INVALID_SCENE`, jumps to phase 52, which closes the file and
   proceeds to phase 53. Phase 53 sees the error and routes to phase 62,
   **skipping phase 61 entirely**. The failed Scene never receives
   `filesystem_setResidentSceneRefreshed()` or `op_bank_loaded_scene = 1u`.

2. **No `FS_LOAD_INVALID_PATTERN` is needed.** Every Pattern failure already
   uses `FS_LOAD_INVALID_SCENE`, which is the correct severity. The entire
   Scene is invalidated, not just the Pattern component. Adding a Pattern-
   specific layer would only matter for quarantine granularity (renaming just
   the `.pat` vs. the whole Scene directory), which is not the desired
   behavior.

3. **SRAM side effect after phase 33.** The non-Pattern payload (settings +
   Kit) is committed to resident SRAM at phase 33 before Pattern I/O starts.
   `pat_initScene()` zeroes the Pattern region at the same point. If the
   Pattern load fails after phase 48 (i.e., after streaming began but before
   CRC validation at phase 51), the Pattern region contains partially written
   data. However:
   - For Bank Loads: the failed child is removed from the presence mask at
     Bank phase 20. The orphaned SRAM data is unreachable.
   - For root Scene Loads: the load is reported as failed and the Scene
     directory is quarantine-renamed. The SRAM contains stale data but no
     publication path was reached.
   - In both cases, the sequencer/menu never treats this Scene as
     successfully loaded.

4. **HCNAMES is not published for the failed Pattern.** Phase 61's
   `filesystem_setResidentSceneRefreshed()` is the only path that publishes
   `R` and `@` provenance for the Pattern row. Since it's never reached on
   failure, no corrupt Pattern identity enters the HCNAMES register.

### Status

**R3 is VERIFIED SAFE.** No code change needed for the Pattern failure
invariant. The `FS_LOAD_INVALID_PATTERN` enum value listed in earlier
sections of this document can be removed from the Files Likely Affected
table — it is not required.

### Hardware Validation Test

A test fixture has been prepared to confirm the trace on hardware:

- **Bank:** `SD_CARD/Bank/000 FullBad` — copy of `001 Full` with one
  corrupt child.
- **Corrupt child:** `03 Pop` — `pattern.pat` truncated to 10 bytes
  (original 323 bytes). `sceneset.scg`, `effects.fx`, and `Kit Pop/` are
  intact.
- **Expected behavior:** Bank Load of `000 FullBad` should:
  1. Successfully load children 00–02, 04–15.
  2. Fail child 03 (Pattern validation error after sceneset/Kit pass).
  3. Clear bit 3 from the presence mask; set bit 3 in the failed mask.
  4. Report `BKKit 20` error code for the failed child.
  5. Not publish HCNAMES for child 03.
  6. Boot trace should show the `P` detail marker for child 03 followed by
     an error indicator, then continuation to child 04.
- **Observation points:** boot log (`bootlog.bin`), autosave trace
  (`asavetrc.bin`), HCNAMES mirror after load, presence mask via
  `bank_scenePresentMask()`.

### Hardware Validation Result (S070)

**Test fixture:** `SD_CARD_FULLBAD_TEST/Bank/000 FullBad` — 16 children with
valid PAT4 binary `.pat` files (10,656 bytes each), except `03 Pop` whose
`pattern.pat` is truncated to 10 bytes. All `sceneset.scg` (242–249 bytes),
`effects.fx` (50 bytes), and `Kit */` directories (7 instruments each) are
intact across all 16 children.

**Observed behavior:**

1. **Child 03 Pop correctly failed.** Scene empty in PERF — no Kit, no
   Pattern, not selectable. HCNAMES Scene row 3 is blank with no provenance;
   Pattern row 3 has `?` (failed) provenance. No `.pat03b` AutoSave file
   created. This matches the code trace: PAT4 header validation fails at
   phase 46, routes through phase 52 → 53 → 62 → 72 → Bank phase 20, which
   clears bit 3 from `op_bank_scene_load_mask` and sets it in
   `op_bank_scene_failed_mask`.

2. **All other children (00–02, 04–15) loaded successfully.** HCNAMES has
   correct Kit and Pattern names for all 15 valid scenes. Scenes with
   populated patterns (steps set) display normally in PERF. Scenes 09 and
   11–15 are valid but have no steps set in their source patterns — they load
   correctly but appear empty in the sequencer, which is expected.

3. **AutoSave capture timing.** AutoSave pattern files (`.pat00b` through
   `.pat06b`, excluding `.pat03b`) were created for scenes 0–6 only at the
   time of observation. Scenes 07–15 had not yet been captured — their
   HCNAMES rows carry the `R` (refreshed) flag, indicating names were read
   from the library but AutoSave convergence had not yet cleared them. This
   is normal: the AutoSave drain is budget-limited and captures scenes
   incrementally.

4. **HCNAMES provenance.** Pattern rows for scenes 04–06 show `@` (loaded
   from library file) provenance; scenes 00–02 show `-` (captured by
   AutoSave drain). Scene 03 shows `?` (failed). This split confirms the
   expected lifecycle: AutoSave captured the first few scenes before
   observation, while the later scenes still carried their library-load
   provenance.

**Conclusion:** R3 is **CONFIRMED on hardware**. The Scene Load state machine
correctly invalidates a child with a corrupt PAT4 Pattern file while loading
all sibling children successfully. The failed child is excluded from the
presence mask, its HCNAMES rows are not published with `@` provenance, and no
AutoSave data is captured for it. No code change is needed.
