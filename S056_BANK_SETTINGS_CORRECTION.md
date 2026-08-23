# Session 056 — Bank Trace Visibility + settings.cfg Correction

Follow-up to `S056_BANK_TESTS.md`, item "Inconclusive, needs a retest." Two
related problems, investigated together because the fix for both is "stop
treating Bank Load/Save completion as done before its durable side effects
are durable":

- **Part A** — why an interactive Bank Load leaves no trace evidence at all.
- **Part B** — why `settings.cfg`'s `active_bank` can still be stale after
  the menu has already unlocked, and how to make that impossible.

Implementation is now in progress. The source trace confirmed the proposal's
public API correction (`filesystem_requestSave(FS_FILE_SETTINGS, ...)`) and
the chained-operation scratch hazard; the implementation log at the end of
this document records each landed change and verification result.

---

## Part A — Why Bank Load produces no trace record

### What Scene already does

`Core/Bank/Scene/Preset/presetManager.c:439` (`on_scene_load_complete()`) —
the completion callback for a root Scene Load — opens with an unconditional
trace record, *before* touching any other state:

```c
autosaveTrace_record(AUTOSAVE_TRACE_STAGE_SCENE_LOAD_COMPLETE,
                     (uint8_t)(filesystem_status() == FS_STATUS_DONE
                                   ? AUTOSAVE_TRACE_SCENE_LOAD_COMPLETE_FLAG_STATUS_DONE
                                   : 0u),
                     (uint32_t)pm_kit_request_scene_mask);
```

The doc comment above it states the design intent directly: *"a missing R
record proves the callback was not reached, R with bit 0 clear proves a
terminal error entered it, and R with value zero proves the request arrived
without a destination."* This `'R'` stage exists specifically so a Scene
Load's outcome is always visible in `asavetrc.bin`, independent of whatever
else does or doesn't happen downstream (AutoSave debounce, dirty marking,
flush timing).

### What Bank Load has instead

`on_bank_load_complete()` (`presetManager.c:509`) — the exact structural
counterpart, registered as the callback for `filesystem_requestLoadBank()`
in `preset_loadBank()` — has **no equivalent record**. It goes straight into
`filesystem_status() == FS_STATUS_DONE` branching and per-Scene dirty
marking, then calls `preset_completeFilesystemOp(PRESET_OP_BANK_LOAD)`.
`on_bank_save_complete()` (`presetManager.c:545`) is even thinner — it's a
one-line passthrough to `preset_completeFilesystemOp(PRESET_OP_BANK_SAVE)`
with no trace at all.

This confirms the suspicion driving this investigation: **Bank never got the
same treatment Scene got.** It isn't a coincidence that this session's card
showed zero trace evidence for the interactive Bank Load — there was never
any code path that would have produced one at the Preset layer, and the one
piece of Bank-specific tracing that *does* exist deep in `filesystem.c`
(the `'B'` Bank-present-mask witness, `AUTOSAVE_TRACE_STAGE_BANK_PRESENT`,
fired from the metadata-commit phase around `filesystem.c:10780`) is subject
to the same SRAM-ring-then-idle-flush pipeline as everything else — so it
can be silently sitting unflushed if the card is pulled soon enough, with no
way to distinguish that from "never ran."

Both boot and interactive Bank Loads share this exact same code
(`main.c:813` and `menu.c:7209` both call `preset_loadBank()` →
`filesystem_requestLoadBank()` → the one `FS_INTERNAL_OP_LOAD_BANK` state
machine → `on_bank_load_complete()`), so this isn't a "boot path vs.
interactive path" divergence — it's simply that **no Bank Load, boot or
interactive, has ever had an unconditional completion witness**, and the
project's Bank testing to date has apparently always used the power-cycle
method (confirmed in `S056_BANK_TESTS.md`: all 12 `'B'` records in the
entire trace file are boot-time, none interactive), so this gap has never
been exercised or noticed before.

### Proposed fix

Add one new stage, mirroring `'R'`'s design exactly:

```c
AUTOSAVE_TRACE_STAGE_BANK_OP_COMPLETE = 'K',   /* unused letter */
```

Emitted as the *first* line of both `on_bank_load_complete()` and
`on_bank_save_complete()`:

```c
autosaveTrace_record(AUTOSAVE_TRACE_STAGE_BANK_OP_COMPLETE,
    (uint8_t)((filesystem_status() == FS_STATUS_DONE ? 0x01u : 0u) |
              (is_save ? 0x02u : 0u)),          /* bit0 DONE, bit1 kind */
    ((uint32_t)pm_request_slot));               /* bank slot, 0..999 */
```

- `flags` bit 0: DONE observed (same convention as `'R'`).
- `flags` bit 1: kind — 0 = Load, 1 = Save (same two-value packing style
  already used by `'L'`'s kind bits).
- `value32`: the Bank slot (`pm_request_slot`, already captured by
  `preset_loadBank()`/`preset_saveBank()` before the request is posted) —
  enough to identify *which* Bank Load/Save this witness belongs to without
  adding new retained state.

This costs one 8-byte record per Bank Load/Save, guarantees `asavetrc.bin`
always shows whether the operation's own callback was reached and whether
it observed success — the same guarantee Scene Load already has — and
requires no new persistent storage. It should be added regardless of Part B,
since it's useful independent of the settings-write fix and is a small,
self-contained, low-risk change.

**Note**: this only fixes *visibility*. It does not, by itself, guarantee
the record reaches disk before the card is pulled — that still depends on
the periodic 500 ms trace flush getting an idle tick. Part B's change
(making the menu wait for a real filesystem operation to finish before
unlocking) gives the trace flush the same idle window it needs, as a side
effect — see the closing note in Part B.

---

## Part B — settings.cfg must not depend on a debounce for Bank Load/Save

### Current behavior

`filesystem_markSettingsDirty()` (`filesystem.c:19297`) only sets a dirty
flag and a debounce deadline:

```c
fs_settings_change_revision++;
fs_settings_dirty = 1u;
fs_settings_next_due_tick = (uint16_t)(time_sysTick + SETTINGS_AUTOWRITE_DEBOUNCE_MS);
```

It's called at all three Bank-identity commit points
(`filesystem.c:10644` empty Bank Load, `:10799` non-empty Bank Load,
`:14041` Bank Save), each time with the same comment: *"the existing
debounced settings writer re-serializes `active_bank` later; this call
opens no file."* The actual write only happens later, from
`filesystem_settingsWriterSchedule_tick()` (`filesystem.c:19607`), gated on:

```c
if (!fs_settings_runtime_ready || !fs_settings_dirty ||
    afatfs_getFilesystemState() != AFATFS_FILESYSTEM_STATE_READY)
    return;
now = time_sysTick;
if ((uint16_t)(now - fs_settings_next_due_tick) >= 0x8000u)
    return;
(void)filesystem_start(FS_INTERNAL_OP_SAVE_GLOBALS, FS_FILE_SETTINGS, 0u,
                       filesystem_settingsWriterCompleted);
```

— 1 second of quiet (`SETTINGS_AUTOWRITE_DEBOUNCE_MS`) plus an idle facade.
Meanwhile, Menu's own unlock point,
`menu_finishLoadSaveCommand()` (`menu.c:207`), runs from the
`PRESET_OP_BANK_LOAD` handler (`menu.c:8134`) as soon as
`filesystem_status()` on the *Bank Load itself* reads DONE and the
sound-apply cursor drains — it has no dependency on the settings writer at
all. So there is a real window, by design, where the menu says the
operation is finished and returns control to the user (who is now free to
navigate away, power off, or pull the card) while `settings.cfg` on disk
still names the *previous* Bank.

This matches this session's card exactly: HCNAMES (synchronous, inside the
Bank Load's own commit) was fully correct; `settings.cfg` (debounced,
outside it) was not.

### What "should never depend on a debounce" means concretely

The debounce exists to *coalesce rapid changes* (per its own doc comment:
"multiple changes inside one second should coalesce") — that's the right
behavior for something like scalar Menu edits that can fire many times per
second. A Bank Load or Bank Save is a single, deliberate, already-debounced-
by-the-user action (the OK/OW press). It doesn't need coalescing; it needs
its one settings write to happen before the operation is allowed to be
"done." The fix is not to remove the debounce mechanism (it's still correct
for its original callers) — it's to make Bank Load/Save's own completion
*not* declare victory until a real settings write has actually completed,
bypassing the debounce for this one caller.

### Proposed fix

Both `on_bank_load_complete()` and `on_bank_save_complete()`
(`presetManager.c:509`, `:545`) currently end by calling
`preset_completeFilesystemOp(PRESET_OP_BANK_LOAD / _SAVE)` — the call that
sets `pm_status = PRESET_UPDATE_READY`, which is what lets Menu's poll loop
reach `menu_finishLoadSaveCommand()` and unlock. Change both to defer that
final call until an explicit, immediate settings flush has itself
completed:

```c
static void on_bank_settings_flush_complete(void)
{
    /*
     * Finish the Bank op that requested this synchronous settings write,
     * regardless of the write's own outcome — a failed settings.cfg write
     * must not hang Menu. filesystem_handleSettingsWriteResult() re-arms the
     * normal debounced retry on failure so the value still converges later.
     */
    filesystem_handleSettingsWriteResult(status);   /* new tiny export, see below */
    preset_completeFilesystemOp(pm_pending_bank_op); /* PRESET_OP_BANK_LOAD or _SAVE */
}

/* at the tail of on_bank_load_complete() / on_bank_save_complete(), replacing
   the direct preset_completeFilesystemOp(...) call: */
pm_pending_bank_op = PRESET_OP_BANK_LOAD; /* or _SAVE */
if (!filesystem_start(FS_INTERNAL_OP_SAVE_GLOBALS, FS_FILE_SETTINGS, 0u,
                      on_bank_settings_flush_complete)) {
    /* Facade unexpectedly refused — degrade to the existing debounced path
       rather than risk a hang; this preserves today's behavior as a
       fallback instead of introducing a new one. */
    preset_completeFilesystemOp(pm_pending_bank_op);
}
```

`filesystem_handleSettingsWriteResult()` is a small new export that factors
the existing retry-on-error body out of `filesystem_settingsWriterCompleted()`
(`filesystem.c:19589`) so both the periodic scheduler and this new
synchronous caller share one retry policy instead of duplicating it:

```c
void filesystem_handleSettingsWriteResult(fs_status_t result)
{
    if (result != FS_STATUS_DONE) {
        fs_settings_dirty = 1u;
        fs_settings_next_due_tick = (uint16_t)(
            time_sysTick + SETTINGS_AUTOWRITE_DEBOUNCE_MS);
    }
    filesystem_ack();
}
```

`filesystem_settingsWriterCompleted()` becomes a one-line wrapper around it.

### Why this is safe

- `filesystem_start()` requires the facade to be idle. At the point
  `on_bank_load_complete()`/`on_bank_save_complete()` run, the Bank
  operation has just reached its own terminal callback — the facade is idle
  by construction (this is the same precondition the existing debounced
  scheduler already relies on).
- The explicit "refuse → finish anyway" fallback means this change cannot
  introduce a new hang mode. This project has already paid for one
  hang-class bug in this exact area (Session 055: `menu_requestInstrumentIndexLoad()`/
  `menu_requestLibraryIndexLoad()` raised `menu_storageBusy` expecting
  acceptance and self-deadlocked on refusal) — the fallback branch above is
  there specifically so this fix doesn't reintroduce that class of bug.
  `menu_loadSaveCommandActive` stays owned and `...` stays displayed for the
  short extra time the settings write takes (well under the existing 1 s
  debounce window in the common case, since there's no coalescing to wait
  for anymore); on failure the operation still completes and the normal
  debounced retry converges the value later, exactly as today.
- This only changes Bank Load/Save's own completion sequencing. It does not
  touch the periodic debounced writer, which remains exactly as-is for every
  other `filesystem_markSettingsDirty()` caller.
- Side effect worth noting: because this keeps the facade "busy" for a few
  more ticks after the Bank commit, it also gives the periodic trace flush
  (`FS_INTERNAL_OP_AUTOSAVE_TRACE_FLUSH`, 500 ms interval, also idle-gated) a
  guaranteed additional idle window before Menu ever reports the operation
  finished — which independently improves the odds that Part A's new `'K'`
  record (and any `'B'`/dirty-mark records from the same Bank Load) actually
  reach `asavetrc.bin` before a user who immediately pulls the card can beat
  it there. It does not *guarantee* the trace flush runs (that's still a
  separate, unforced operation), but it removes one of the two known-stale
  paths from this session's report using one mechanism.

### What this does not fix

The AutoSave *record* itself (`.hcprms1`/`.hcprms2`, the 34 KB parameter
snapshot) still commits on its own 5-second debounce
(`AUTOSAVE_WRITER_INTERVAL_MS`), independent of Menu's lock. That's correct
to leave alone — it's a much larger write than settings.cfg, genuinely
benefits from coalescing multiple rapid Scene/Kit edits into one commit, and
forcing it synchronous on every Load/Save would make ordinary Kit/Instrument
editing sluggish. `settings.cfg` is a 17-line file recording exactly one
thing Bank Load/Save changes (`active_bank`, the boot-restore selection) —
its cost/benefit for going synchronous is completely different, which is
why this proposal is scoped to `settings.cfg` only, per the original ask.

---

## Summary of proposed changes

| File | Change |
|---|---|
| `Core/Bank/Scene/AutosaveTrace.h` | New stage `AUTOSAVE_TRACE_STAGE_BANK_OP_COMPLETE = 'K'` |
| `Core/Bank/Scene/Preset/presetManager.c` | Unconditional `'K'` record at the top of `on_bank_load_complete()` / `on_bank_save_complete()`; both now chain an explicit `FS_INTERNAL_OP_SAVE_GLOBALS` write before calling `preset_completeFilesystemOp()`, with a same-tick fallback if the write is refused |
| `Core/Hardware/SD/filesystem.c` | New small export `filesystem_handleSettingsWriteResult()`, factored out of `filesystem_settingsWriterCompleted()`, reused by the new synchronous caller |
| `Core/Hardware/SD/filesystem.h` | Declare the new export |
| `knowledge_files/specification_reference/DEV_MODES.md` | Document `'K'` alongside the existing `'B'`/`'R'` entries |

Neither change alters the AutoSave record's own debounce, the trace ring
size, or anything about Kit/Scene/Instrument Load/Save — scope is
deliberately limited to Bank Load/Save's own completion sequencing, per this
session's ask.

---

# Implementation Plan

Expanded from the proposal above into an exact, file-by-file change list.
**No code has been changed — this is planning only.** Every code block below
is written the way it should land in the source, including the comment
block that should sit next to it, so it can be copied in directly rather
than re-derived at implementation time.

Two corrections surfaced by this deeper pass, neither visible from the
proposal alone:

1. **`filesystem_start()` is `static`** (file-local to `filesystem.c`,
   declared at `filesystem.c:20524`). `presetManager.c` cannot call it
   directly, so the sketch in the proposal above (`filesystem_start(FS_INTERNAL_OP_SAVE_GLOBALS, ...)`
   called from `presetManager.c`) will not compile as written. The public
   entry point that reaches the same operation is
   `filesystem_requestSave(FS_FILE_SETTINGS, 0u, cb)`
   (`filesystem.c:21427`, declared `filesystem.h:548`) — it validates the
   `FS_FILE_SETTINGS` descriptor (`supports_save` is `1` at `filesystem.c:329`)
   and then calls the same `filesystem_start(FS_INTERNAL_OP_SAVE_GLOBALS, ...)`
   internally. Change 7/8 below use this instead.
2. **`on_bank_load_complete()`'s caller-visible "was a Scene actually
   loaded" bit lives in `op_`-prefixed filesystem.c scratch that a chained
   operation will clear before Menu ever reads it.** `preset_completedBankLoadedScene()`
   (`presetManager.c:2274`, the sole caller is `menu.c:8154`) returns
   `filesystem_lastBankLoadLoadedScene()`, which returns the raw
   `op_bank_loaded_scene` field (`filesystem.c:22849`). That field is one of
   the many `op_*` fields `filesystem_start()` unconditionally resets to 0 at
   the top of every new operation (`filesystem.c:20625`, inside the same
   reset block documented at `filesystem.c:20593-20606`). Today this is
   harmless because nothing starts a new operation between
   `on_bank_load_complete()` finishing and Menu reading this bit. Once this
   plan makes `on_bank_load_complete()` chain a settings-write operation
   *before* Menu can see completion, that chained `filesystem_start()` would
   silently zero `op_bank_loaded_scene` — so a Bank Load that legitimately
   loaded a Scene child would read back as "empty" by the time Menu asks,
   and Menu would incorrectly run the empty-Bank fallback ladder
   (`menu.c:8156`, `preset_loadFirstAvailableSceneOrKit()`) instead of the
   normal sound-apply path. Change 5/7 below add a presetManager-owned
   snapshot taken *before* the chain, specifically to prevent this
   regression. This is a correctness hazard the original proposal did not
   anticipate; it only exists because of the fix, so it has to ship in the
   same change, not as a follow-up.

Everything else in this plan matches the proposal's original design intent
exactly, just pinned to precise insertion points.

## Change list

| # | File | Function / location | Kind |
|---|---|---|---|
| 1 | `Core/Bank/Scene/AutosaveTrace.h` | `autosave_trace_stage_t` enum | add stage `'K'` |
| 2 | `Core/Bank/Scene/AutosaveTrace.h` | flag-constant block near `'R'`'s | add 2 flag `#define`s |
| 3 | `Core/Hardware/SD/filesystem.h` | near `filesystem_markSettingsDirty()` decl | add 1 export declaration |
| 4 | `Core/Hardware/SD/filesystem.c` | `filesystem_settingsWriterCompleted()` | split into new export + thin wrapper |
| 5 | `Core/Bank/Scene/Preset/presetManager.c` | static state block (~line 88) | add 2 new statics |
| 6 | `Core/Bank/Scene/Preset/presetManager.c` | `preset_completeFilesystemOp()` | split into new variant + thin wrapper |
| 7 | `Core/Bank/Scene/Preset/presetManager.c` | `on_bank_load_complete()` | rewrite |
| 8 | `Core/Bank/Scene/Preset/presetManager.c` | `on_bank_save_complete()` | rewrite |
| 9 | `Core/Bank/Scene/Preset/presetManager.c` | new function, placed before Change 7/8 | add |
| 10 | `Core/Bank/Scene/Preset/presetManager.c` | `preset_completedBankLoadedScene()` | change backing source |
| 11 | `knowledge_files/specification_reference/DEV_MODES.md` | `/asavetrc.bin` stage list | document `'K'` |

Suggested implementation order: 1, 2, 3, 4 (filesystem.c/.h first, since
nothing in presetManager.c can compile against the new export until it
exists) → 5, 6, 9 (new state and helpers before anything calls them) → 10
→ 7, 8 (the two callbacks, last, since they're the only things that call
everything added above) → 11 (docs, anytime, but easiest once the shape is
final).

---

### Change 1 — `Core/Bank/Scene/AutosaveTrace.h`: new stage `'K'`

Insert immediately after the existing `AUTOSAVE_TRACE_STAGE_BANK_PRESENT = 'B',`
line (`AutosaveTrace.h:144`), inside the `autosave_trace_stage_t` enum, before
its closing `} autosave_trace_stage_t;`:

```c
    /*
     * K: unconditional Bank Load/Save completion witness, mirroring R
     * (AUTOSAVE_TRACE_STAGE_SCENE_LOAD_COMPLETE) for Bank instead of root
     * Scene. Bank Load and Bank Save previously had no callback-entry trace
     * at all -- the only Bank-specific record was 'B', emitted from deep
     * inside filesystem.c's commit phase, which proves what the commit did
     * but not whether Preset's own completion callback (on_bank_load_complete()/
     * on_bank_save_complete() in presetManager.c) was ever reached or what it
     * observed. A missing K record proves that callback was not reached; K
     * with bit 0 clear proves it was reached but observed a terminal error;
     * K with bit 0 set proves success. Emitted as the first action in both
     * callbacks, before any ack or further work, so its presence/flags are
     * never contingent on anything the callback does afterward. Session 056
     * added this after an interactive Bank Load produced zero trace evidence
     * of any kind -- see S056_BANK_TESTS.md and this file's own investigation
     * above for the card evidence that motivated it.
     */
    AUTOSAVE_TRACE_STAGE_BANK_OP_COMPLETE = 'K',
```

**Why here specifically**: stage letters in this enum are grouped loosely by
subsystem rather than strict chronological addition order (`'Y'` then `'B'`
sit together at the tail as the two most recent additions before this one).
Placing `'K'` immediately after `'B'` keeps every Bank-related stage
adjacent for a future reader scanning the enum.

**Inputs**: none (this is a type definition).
**Output**: one new valid value for `autosave_trace_stage_t`; no behavior
change until something calls `autosaveTrace_record()` with it (Change 7/8).
**Affiliates**: `AUTOSAVE_TRACE_STAGE_SCENE_LOAD_COMPLETE` (`'R'`, the direct
precedent), `AUTOSAVE_TRACE_STAGE_BANK_PRESENT` (`'B'`, the existing
Bank-specific stage this complements), `presetManager.c`'s
`on_bank_load_complete()` / `on_bank_save_complete()` (Change 7/8, the sole
producers), `tools/decode_devlogs.py` and `tools/devlog_unpack.py` (need a
matching lookup-table entry — see the note at the end of this plan).

---

### Change 2 — `Core/Bank/Scene/AutosaveTrace.h`: flag constants for `'K'`

Insert immediately after the existing block:

```c
#define AUTOSAVE_TRACE_SCENE_LOAD_COMPLETE_FLAG_STATUS_DONE (1u << 0u)
```

(`AutosaveTrace.h:322`), i.e. between that line and the `F flags` comment
block that follows it:

```c
/*
 * K flags: bit 0 means the Bank Load/Save completion callback observed
 * FS_STATUS_DONE (same convention and same bit position as R's status flag,
 * intentionally, so a reader who already knows R's layout does not have to
 * relearn it for K). Bit 1 selects which callback produced this record: 0
 * means on_bank_load_complete() (a Load), 1 means on_bank_save_complete()
 * (a Save) -- the same two-way kind-bit convention already used by L
 * (AUTOSAVE_TRACE_LOAD_MARK_KIND_KIT / _SCENE). The K value is the target
 * Bank library slot (0..999, from pm_request_slot, captured by
 * preset_loadBank()/preset_saveBank() before the request was even posted),
 * letting this callback-entry witness be matched against later B/L/D
 * records for the same slot even when a trace file captures more than one
 * Bank operation.
 */
#define AUTOSAVE_TRACE_BANK_OP_COMPLETE_FLAG_STATUS_DONE (1u << 0u)
#define AUTOSAVE_TRACE_BANK_OP_COMPLETE_FLAG_KIND_SAVE   (1u << 1u)
```

**Inputs/Output/Affiliates**: same as Change 1 — pure constants, no behavior
until Change 7/8 use them.

---

### Change 3 — `Core/Hardware/SD/filesystem.h`: declare the new export

Insert immediately after `void filesystem_enableRuntimeSettingsWrites(void);`
(`filesystem.h:408`):

```c
/*
 * Apply a completed settings.cfg write's outcome without acknowledging it.
 *
 * Inputs: the terminal fs_status_t observed for one FS_INTERNAL_OP_SAVE_GLOBALS
 * operation, from either producer -- the periodic debounced scheduler
 * (filesystem_settingsWriterSchedule_tick()) or a caller that needs the write
 * to happen immediately instead of waiting out the debounce (Bank Load/Save
 * completion, see presetManager.c's on_bank_settings_flush_complete()).
 * Output: on anything other than FS_STATUS_DONE, re-arms the normal dirty
 * flag and debounce deadline so the value still converges on the next idle
 * pass; on DONE, does nothing further. Deliberately does not call
 * filesystem_ack() -- every caller owns acknowledging its own terminal
 * status once it has read whatever else it needs from filesystem_status(),
 * exactly as every other filesystem terminal path in this codebase does.
 * Affiliates: filesystem_settingsWriterCompleted() (the existing debounced
 * path, now a thin wrapper around this), on_bank_settings_flush_complete()
 * (the new synchronous path), and SETTINGS_AUTOWRITE_DEBOUNCE_MS.
 */
void filesystem_handleSettingsWriteResult(fs_status_t result);
```

**Why exported at all**: `fs_settings_dirty` and `fs_settings_next_due_tick`
are private statics in `filesystem.c` (`filesystem.c:1541` region). The new
synchronous caller in `presetManager.c` needs the exact same retry policy on
failure that the debounced scheduler already has, without duplicating it or
reaching into filesystem.c's private state. This export is the minimal
surface that allows that sharing.

---

### Change 4 — `Core/Hardware/SD/filesystem.c`: split `filesystem_settingsWriterCompleted()`

Current code (`filesystem.c:19589-19605`):

```c
    if (status != FS_STATUS_DONE) {
        fs_settings_dirty = 1u;
        fs_settings_next_due_tick = (uint16_t)(
            time_sysTick + SETTINGS_AUTOWRITE_DEBOUNCE_MS);
    }
    filesystem_ack();
}
```

Replace the whole `filesystem_settingsWriterCompleted()` function with:

```c
void filesystem_handleSettingsWriteResult(fs_status_t result)
{
    /*
     * Re-arm the debounced settings writer on anything but a clean commit.
     *
     * Inputs: the terminal status of one just-finished FS_INTERNAL_OP_SAVE_GLOBALS
     * operation. Output: DONE does nothing (the write is durable, nothing is
     * owed); anything else marks the value dirty again and restarts the
     * normal SETTINGS_AUTOWRITE_DEBOUNCE_MS deadline, so a transient SD
     * failure still converges later through the ordinary idle scheduler
     * instead of being silently dropped. Deliberately does not touch
     * filesystem_ack() -- see the declaration comment in filesystem.h. Why a
     * separate exported function instead of leaving this logic private:
     * on_bank_settings_flush_complete() in presetManager.c (Session 056)
     * needs this exact retry policy for its own synchronous, non-debounced
     * settings write, without either duplicating it or being given direct
     * access to fs_settings_dirty/fs_settings_next_due_tick.
     */
    if (result != FS_STATUS_DONE) {
        fs_settings_dirty = 1u;
        fs_settings_next_due_tick = (uint16_t)(
            time_sysTick + SETTINGS_AUTOWRITE_DEBOUNCE_MS);
    }
}

static void filesystem_settingsWriterCompleted(void)
{
    /*
     * Complete an invisible settings.cfg write without involving Preset/Menu.
     *
     * This is the debounced scheduler's own completion callback
     * (filesystem_settingsWriterSchedule_tick() is its sole caller). It owns
     * acknowledging this operation's terminal status because, unlike the
     * synchronous Bank Load/Save path, nothing else is waiting on this
     * result -- there is no menu.c state to advance and no chained follow-up
     * operation. Inputs: terminal SAVE_GLOBALS status. Outputs: the shared
     * retry policy in filesystem_handleSettingsWriteResult() runs first,
     * then this operation's terminal status is acknowledged so a future
     * unrelated caller is never refused by a stale unacked DONE/ERROR.
     */
    filesystem_handleSettingsWriteResult(status);
    filesystem_ack();
}
```

**Inputs**: `status` (the module-level filesystem facade status, read
implicitly by both functions, same as today).
**Output**: identical externally-observable behavior for the existing
debounced path (same dirty/deadline re-arm on failure, same final ack) —
this is a pure refactor for that caller. The only new thing is that the
retry-policy half is now independently callable.
**Affiliates**: `filesystem_settingsWriterSchedule_tick()` (unchanged
caller), `SETTINGS_AUTOWRITE_DEBOUNCE_MS` (`config.h:282`),
`on_bank_settings_flush_complete()` (Change 9, the new caller).

---

### Change 5 — `Core/Bank/Scene/Preset/presetManager.c`: two new statics

Insert immediately after `pm_kit_request_scene_mask` (`presetManager.c:88`):

```c
/*
 * Which Bank operation is waiting on the chained synchronous settings.cfg
 * write started by on_bank_load_complete()/on_bank_save_complete().
 *
 * Inputs: set to PRESET_OP_BANK_LOAD or PRESET_OP_BANK_SAVE immediately
 * before filesystem_requestSave(FS_FILE_SETTINGS, ...) is posted. Output:
 * on_bank_settings_flush_complete() reads this to know which op identity to
 * report through preset_completeFilesystemOpWithResult() once the chained
 * write finishes -- Menu's completion dispatch (menu.c's PRESET_OP_BANK_LOAD
 * / PRESET_OP_BANK_SAVE cases) must see the Bank operation's own identity,
 * never the incidental settings write's. Why a dedicated field instead of
 * reusing pm_completed_op: pm_completed_op is Menu-facing output, written
 * only once the whole chained sequence is done; this is Preset-internal
 * input describing work still in flight. Affiliate: on_bank_settings_flush_complete().
 */
static volatile preset_op_type_t pm_pending_bank_op = PRESET_OP_NONE;
/*
 * Snapshot of filesystem_lastBankLoadLoadedScene(), taken at the top of
 * on_bank_load_complete() before anything acks the Bank Load's own terminal
 * status or starts another operation.
 *
 * Inputs: op_bank_loaded_scene (filesystem.c), read once per Bank Load
 * completion through the existing public accessor. Output: preset_completedBankLoadedScene()
 * (menu.c's sole caller, at menu.c:8154) returns this snapshot instead of
 * reading the live filesystem.c field. Why this exists: Session 056 added a
 * chained filesystem_requestSave(FS_FILE_SETTINGS, ...) call inside
 * on_bank_load_complete() (Change 7). Starting any new operation resets
 * op_bank_loaded_scene to 0 as part of filesystem_start()'s generic
 * per-operation scratch reset (filesystem.c:20625), which would otherwise
 * make a real, non-empty Bank Load look empty to Menu by the time Menu asks
 * -- Menu reads this bit only after pm_status reaches PRESET_UPDATE_READY,
 * which now happens only after the chained write completes. Capturing it
 * here, before the chain starts, removes the dependency entirely regardless
 * of what any later chained operation does to filesystem.c's scratch.
 * Affiliates: filesystem_lastBankLoadLoadedScene(), preset_completedBankLoadedScene(),
 * menu.c:8154.
 */
static volatile uint8_t pm_pending_bank_loaded_scene = 0u;
```

**Why `volatile`**: matches every other `pm_*` field in this block
(`pm_status`, `pm_completed_op`, `pm_request_slot`, ...) — they're all
written from one asynchronous callback context and read from Menu's poll,
and the existing code already qualifies all of them this way.

---

### Change 6 — `Core/Bank/Scene/Preset/presetManager.c`: split `preset_completeFilesystemOp()`

Current code (`presetManager.c:219-247`):

```c
static void preset_completeFilesystemOp(preset_op_type_t completed_op)
{
    fs_status_t fs_status = filesystem_status();
    uint8_t completed_ok = (uint8_t)(fs_status == FS_STATUS_DONE);
    uint8_t is_test_op = (uint8_t)(completed_op == PRESET_OP_TEST_SCAN ||
                                   completed_op == PRESET_OP_TEST_FILE_LOAD ||
                                   completed_op == PRESET_OP_TEST_DIR_LOAD ||
                                   completed_op == PRESET_OP_TEST_FILE_SAVE ||
                                   completed_op == PRESET_OP_TEST_DIR_SAVE);

    filesystem_ack();
    pm_completed_ok = completed_ok;
    if (completed_ok || is_test_op || completed_op != PRESET_OP_NONE) {
        pm_completed_op = completed_op;
        pm_status = PRESET_UPDATE_READY;
    } else {
        pm_completed_op = PRESET_OP_NONE;
        pm_status = PRESET_UPDATE_READY;
    }
}
```

Replace with:

```c
static void preset_completeFilesystemOpWithResult(preset_op_type_t completed_op,
                                                   uint8_t completed_ok)
{
    /*
     * Publish one operation's outcome to Menu with an explicitly supplied
     * result, instead of deriving it from the live filesystem facade.
     *
     * Inputs: the operation identity Menu should see, and whether it should
     * be treated as successful. Output: acknowledges whatever operation is
     * currently terminal on the filesystem facade, then publishes
     * pm_completed_ok/pm_completed_op/pm_status for Menu's poll loop to pick
     * up. Why a supplied result instead of always reading filesystem_status():
     * a caller may be acknowledging a *different* operation than the one it
     * is reporting -- on_bank_settings_flush_complete() (Session 056) is
     * finishing the Bank Load/Save that was captured earlier, not the
     * incidental settings.cfg write whose terminal status happens to be live
     * on the facade at the moment this runs. Reading filesystem_status()
     * here in that case would silently report the wrong operation's result.
     * preset_completeFilesystemOp() below is the original, unchanged-behavior
     * entry point for every other caller, which still derives completed_ok
     * from the live facade exactly as before.
     */
    uint8_t is_test_op = (uint8_t)(completed_op == PRESET_OP_TEST_SCAN ||
                                   completed_op == PRESET_OP_TEST_FILE_LOAD ||
                                   completed_op == PRESET_OP_TEST_DIR_LOAD ||
                                   completed_op == PRESET_OP_TEST_FILE_SAVE ||
                                   completed_op == PRESET_OP_TEST_DIR_SAVE);

    filesystem_ack();
    pm_completed_ok = completed_ok;
    if (completed_ok || is_test_op || completed_op != PRESET_OP_NONE) {
        pm_completed_op = completed_op;
        pm_status = PRESET_UPDATE_READY;
    } else {
        pm_completed_op = PRESET_OP_NONE;
        pm_status = PRESET_UPDATE_READY;
    }
}

static void preset_completeFilesystemOp(preset_op_type_t completed_op)
{
    /*
     * Publish one operation's outcome to Menu, reading success from the
     * live filesystem facade. This is the original, unmodified behavior of
     * this function, now expressed as the common case of
     * preset_completeFilesystemOpWithResult(): every pre-existing caller
     * (on_kit_load_complete(), on_scene_load_complete(),
     * on_scene_save_complete(), on_instrument_*_complete(), the Bank Load/Save
     * failure paths, and the rest) keeps its exact prior behavior.
     */
    preset_completeFilesystemOpWithResult(
        completed_op, (uint8_t)(filesystem_status() == FS_STATUS_DONE));
}
```

**Inputs/Output**: for every existing caller, byte-for-byte identical
behavior — `preset_completeFilesystemOp()` still derives `completed_ok` from
`filesystem_status()` internally, just one call deeper. This is a
behavior-preserving refactor for the ~15 existing call sites
(`on_kit_load_complete()`, `on_scene_load_complete()`,
`on_scene_save_complete()`, the Instrument family, `on_globals_save_complete()`,
`on_pattern_save_complete()`, `on_all_save_complete()`,
`on_performance_save_complete()`, and both Bank failure paths).
**Affiliates**: every `on_*_complete()` callback in this file;
`on_bank_settings_flush_complete()` (Change 9) is the one new caller of the
`WithResult` variant directly.

---

### Change 7 — `Core/Bank/Scene/Preset/presetManager.c`: rewrite `on_bank_load_complete()`

Current code (`presetManager.c:509-543`):

```c
static void on_bank_load_complete(void)
{
    uint16_t completed_scene_mask;
    uint8_t scene_index;

    /* [existing doc comment, ~514-532] */
    if (filesystem_status() == FS_STATUS_DONE) {
        completed_scene_mask = filesystem_lastBankLoadSceneMask();
        for (scene_index = 0u;
             scene_index < SCENE_COUNT && scene_index < 16u;
             scene_index++) {
            if ((completed_scene_mask & (uint16_t)(1u << scene_index)) != 0u)
                autosave_markSceneWithoutPatternDirty(scene_index);
        }
    }
    preset_completeFilesystemOp(PRESET_OP_BANK_LOAD);
}
```

Replace with:

```c
static void on_bank_load_complete(void)
{
    uint16_t completed_scene_mask;
    uint8_t scene_index;
    uint8_t load_done = (uint8_t)(filesystem_status() == FS_STATUS_DONE);

    /*
     * Unconditional Bank Load completion witness -- the Bank counterpart of
     * on_scene_load_complete()'s 'R' record, added for the same reason: a
     * missing K record proves this callback was never reached; K's DONE bit
     * proves whether filesystem.c reported success. Must run first, before
     * any ack, so it reflects exactly what this callback observed on entry
     * and can never be skipped by an early return added later. value32 is
     * the target Bank slot (pm_request_slot), captured by preset_loadBank()
     * before the request was even posted, so this record identifies which
     * Bank Load it belongs to without adding new retained state.
     */
    autosaveTrace_record(AUTOSAVE_TRACE_STAGE_BANK_OP_COMPLETE,
        (uint8_t)(load_done ? AUTOSAVE_TRACE_BANK_OP_COMPLETE_FLAG_STATUS_DONE
                            : 0u),
        (uint32_t)pm_request_slot);

    /*
     * Snapshot the "did this load actually place a Scene" bit before it can
     * be reset out from under Menu. See pm_pending_bank_loaded_scene's own
     * declaration comment (Change 5) for the full rationale: this must
     * happen before the settings-write chain below starts any new
     * operation, and unconditionally (both success and failure), since
     * nothing downstream should have to prove which branch this callback
     * took before trusting the snapshot.
     */
    pm_pending_bank_loaded_scene = filesystem_lastBankLoadLoadedScene();

    /*
     * Complete one root Bank load.
     *
     * Filesystem has validated bankset.bcg and either loaded a Bank-local
     * Scene child or reported a valid empty Bank through
     * filesystem_lastBankLoadLoadedScene(). Menu/boot decide whether to run
     * the fallback chain after reading that bit.
     *
     * Inputs: FS_STATUS_DONE and filesystem_lastBankLoadSceneMask() before the
     * completion helper acknowledges operation scratch. Output: each set child
     * bit receives the existing non-Pattern Scene dirty scope; a valid empty
     * Bank yields no Scene marks. Why: the original request may name absent
     * children, while bank_scenePresentMask() also contains retained unselected
     * Scenes; only the completed effective-child mask identifies this Bank
     * Load's payload. BankData has already marked changed Bank fields through
     * its own setters. Affiliates: filesystem_lastBankLoadSceneMask(), Bank
     * phase-20 metadata commit, autosave_markSceneWithoutPatternDirty(), and
     * preset_completeFilesystemOp().
     */
    if (load_done) {
        completed_scene_mask = filesystem_lastBankLoadSceneMask();
        for (scene_index = 0u;
             scene_index < SCENE_COUNT && scene_index < 16u;
             scene_index++) {
            if ((completed_scene_mask & (uint16_t)(1u << scene_index)) != 0u)
                autosave_markSceneWithoutPatternDirty(scene_index);
        }

        /*
         * Flush settings.cfg synchronously before Menu can see this Load as
         * finished, instead of leaving it to the 1-second debounced writer.
         *
         * Inputs: a successful Bank Load, which already called
         * bank_setRestoreBankSlot()/bank_setDisplayName() and
         * filesystem_markSettingsDirty() deep inside filesystem.c's commit
         * phase (filesystem.c:10799 for a non-empty Load, :10644 for an
         * empty one) -- but only *armed* the debounce, never wrote the file.
         * Output: filesystem_ack() closes out this Bank Load's own terminal
         * status first (every filesystem terminal path this callback reads
         * must be acknowledged, per the existing project convention --
         * see 055_SESSION_HANDOFF_LOG.md); pm_pending_bank_op records which
         * Menu-facing operation identity to report once the chained write
         * finishes; filesystem_requestSave(FS_FILE_SETTINGS, ...) then starts
         * the same FS_INTERNAL_OP_SAVE_GLOBALS operation the debounced path
         * uses, just immediately instead of waiting out the debounce. This
         * keeps menu_loadSaveCommandActive/'...' owned by Menu for the short
         * extra time the write takes, so a card pulled the instant Menu
         * shows the operation finished can no longer catch active_bank
         * stale on disk. Why filesystem_requestSave() and not
         * filesystem_start(): filesystem_start() is private to filesystem.c;
         * filesystem_requestSave(FS_FILE_SETTINGS, ...) is the public entry
         * point that reaches the identical FS_INTERNAL_OP_SAVE_GLOBALS
         * operation. On refusal (the facade unexpectedly busy -- should not
         * happen here since this callback runs only once the Bank Load's own
         * operation has just gone terminal, but is handled rather than
         * assumed impossible), finish the Bank Load immediately instead of
         * risking a hang: this preserves today's stale-until-debounce
         * behavior as an explicit fallback rather than introducing a new
         * stuck-Menu failure mode. Affiliates: filesystem_requestSave(),
         * on_bank_settings_flush_complete(), filesystem_handleSettingsWriteResult(),
         * and the Session 055 rule that every filesystem terminal path must
         * be acknowledged.
         */
        pm_pending_bank_op = PRESET_OP_BANK_LOAD;
        filesystem_ack();
        if (!filesystem_requestSave(FS_FILE_SETTINGS, 0u,
                                    on_bank_settings_flush_complete)) {
            preset_completeFilesystemOpWithResult(PRESET_OP_BANK_LOAD, 1u);
        }
        return;
    }
    preset_completeFilesystemOp(PRESET_OP_BANK_LOAD);
}
```

**Inputs**: `filesystem_status()`, `filesystem_lastBankLoadLoadedScene()`,
`filesystem_lastBankLoadSceneMask()`, `pm_request_slot` — all read in the
same order and at the same point relative to `filesystem_ack()` as before
(before it, never after), which matters because several of them read
`op_*` scratch that a later `filesystem_start()` would clear.
**Output**: on success, Menu does not see this operation as complete until
`settings.cfg` has actually been (re)written; on failure, behavior is
byte-for-byte unchanged from today. `asavetrc.bin` gains one `'K'` record
per Bank Load, success or failure.
**Affiliates**: `preset_loadBank()` (`presetManager.c:2211`, the request-time
setter of `pm_request_slot`), `menu.c:7209` and `main.c:813` (both callers of
`preset_loadBank()`), `menu.c:8134`'s `case PRESET_OP_BANK_LOAD:` (the
eventual Menu-side consumer of `pm_completed_op`/`pm_pending_bank_loaded_scene`
via `preset_completedBankLoadedScene()`).

---

### Change 8 — `Core/Bank/Scene/Preset/presetManager.c`: rewrite `on_bank_save_complete()`

Current code (`presetManager.c:545-555`):

```c
static void on_bank_save_complete(void)
{
    /*
     * Complete one root Bank save.
     *
     * The save writes bankset.bcg plus the selected Bank-local Scene children.
     * It does not imply runtime DSP changes, so Menu can share the ordinary
     * Save completion cleanup used by Scene Save.
     */
    preset_completeFilesystemOp(PRESET_OP_BANK_SAVE);
}
```

Replace with:

```c
static void on_bank_save_complete(void)
{
    uint8_t save_done = (uint8_t)(filesystem_status() == FS_STATUS_DONE);

    /*
     * Unconditional Bank Save completion witness. See on_bank_load_complete()'s
     * matching comment (Change 7) for the full rationale -- identical
     * reasoning, mirrored for Save. AUTOSAVE_TRACE_BANK_OP_COMPLETE_FLAG_KIND_SAVE
     * distinguishes this record from a Load record sharing the same stage
     * letter.
     */
    autosaveTrace_record(AUTOSAVE_TRACE_STAGE_BANK_OP_COMPLETE,
        (uint8_t)((save_done ? AUTOSAVE_TRACE_BANK_OP_COMPLETE_FLAG_STATUS_DONE
                             : 0u) |
                  AUTOSAVE_TRACE_BANK_OP_COMPLETE_FLAG_KIND_SAVE),
        (uint32_t)pm_request_slot);

    /*
     * Complete one root Bank save.
     *
     * The save writes bankset.bcg plus the selected Bank-local Scene children.
     * It does not imply runtime DSP changes, so Menu can share the ordinary
     * Save completion cleanup used by Scene Save.
     *
     * On success, chain the same synchronous settings.cfg flush Bank Load
     * uses (Change 7) -- Bank Save changes the same active_bank boot-restore
     * authority Bank Load does (filesystem.c:14041's filesystem_markSettingsDirty()
     * call, right next to bank_setRestoreBankSlot()/bank_setDisplayName()),
     * so the two paths must stay symmetric here too, not just in
     * filesystem.c. Bank Save has no empty/non-empty distinction and no
     * op_bank_loaded_scene-equivalent bit that menu.c reads afterward, so
     * unlike Change 7 there is no snapshot to capture here -- only the
     * settings-write chain applies.
     */
    if (save_done) {
        pm_pending_bank_op = PRESET_OP_BANK_SAVE;
        filesystem_ack();
        if (!filesystem_requestSave(FS_FILE_SETTINGS, 0u,
                                    on_bank_settings_flush_complete)) {
            preset_completeFilesystemOpWithResult(PRESET_OP_BANK_SAVE, 1u);
        }
        return;
    }
    preset_completeFilesystemOp(PRESET_OP_BANK_SAVE);
}
```

**Verification note for implementation time**: `menu.c`'s
`case PRESET_OP_BANK_SAVE:` handler (`menu.c:8549`, shared with
`PRESET_OP_SCENE_SAVE`) calls `menu_refreshSavedLibraryName(preset_getCompletedOp())`
on success. This plan has *not* traced whether that function reads any
`op_*`-prefixed filesystem.c scratch that the chained settings write would
clear (as opposed to resident `BankData`/`fs_list_cache_name` state, which
is untouched by `filesystem_start()`'s reset). Before landing Change 8,
confirm `menu_refreshSavedLibraryName()` only reads state that survives an
intervening `filesystem_start()` call; if it doesn't, it needs the same
snapshot treatment as Change 5/7.

**Inputs/Output/Affiliates**: same shape as Change 7, scoped to Save;
`preset_saveBank()` (`presetManager.c:2235`) is the request-time setter of
`pm_request_slot` for this path.

---

### Change 9 — `Core/Bank/Scene/Preset/presetManager.c`: new function `on_bank_settings_flush_complete()`

Add this **before** `on_bank_load_complete()` (i.e. before its first use —
placing it directly after the Change 6 pair, around `presetManager.c:248`,
keeps every helper this file's Bank callbacks depend on grouped together
and avoids needing a forward declaration):

```c
static void on_bank_settings_flush_complete(void)
{
    /*
     * Finish the Bank Load/Save that chained this synchronous settings.cfg
     * write, once the write itself reaches a terminal state.
     *
     * Inputs: filesystem_status() now reflects the FS_INTERNAL_OP_SAVE_GLOBALS
     * write's own outcome (DONE or ERROR), not the Bank operation's --
     * pm_pending_bank_op (set by on_bank_load_complete()/on_bank_save_complete()
     * immediately before this write was posted) is what identifies which
     * Bank operation is actually finishing here. Output:
     * filesystem_handleSettingsWriteResult() re-arms the normal debounced
     * retry if the settings write itself failed, so the value still
     * converges later even though this path bypassed the debounce; a failed
     * settings.cfg write never blocks or fails the Bank operation it belongs
     * to -- preset_completeFilesystemOpWithResult() is always told success
     * (1u) here because this function only runs on the branch where the
     * underlying Bank Load/Save already succeeded (on_bank_load_complete()/
     * on_bank_save_complete() only reach the code that starts this chain
     * after confirming filesystem_status() == FS_STATUS_DONE for their own
     * operation). Why not derive completed_ok from filesystem_status() here
     * like the ordinary preset_completeFilesystemOp() does: that would
     * report the settings write's outcome as if it were the Bank
     * operation's own, which is wrong -- a flaky SD write of a 17-line file
     * must not make Menu show an ERR overlay for a Bank Load that actually
     * loaded correctly. Affiliates: filesystem_handleSettingsWriteResult(),
     * preset_completeFilesystemOpWithResult(), pm_pending_bank_op.
     */
    filesystem_handleSettingsWriteResult(filesystem_status());
    preset_completeFilesystemOpWithResult(pm_pending_bank_op, 1u);
}
```

**Inputs**: `filesystem_status()` (the settings write's own terminal
status), `pm_pending_bank_op` (Change 5).
**Output**: exactly one `filesystem_ack()` (inside
`preset_completeFilesystemOpWithResult()`), acknowledging the settings
write's terminal status; `pm_status`/`pm_completed_op`/`pm_completed_ok`
published for Menu, reporting the *Bank* operation's identity and success.
**Affiliates**: registered as the `cb` argument to both
`filesystem_requestSave()` calls in Change 7 and Change 8; nothing else
calls this function.

---

### Change 10 — `Core/Bank/Scene/Preset/presetManager.c`: `preset_completedBankLoadedScene()`

Current code (`presetManager.c:2274-2277`):

```c
uint8_t preset_completedBankLoadedScene(void)
{
    return filesystem_lastBankLoadLoadedScene();
}
```

Replace with:

```c
uint8_t preset_completedBankLoadedScene(void)
{
    /*
     * Report whether the most recently completed Bank Load placed a Scene,
     * from Preset's own snapshot rather than filesystem.c's live scratch.
     *
     * Inputs: pm_pending_bank_loaded_scene, captured by
     * on_bank_load_complete() before it acknowledges the Load's own terminal
     * status or starts the chained settings.cfg write (Change 7). Output:
     * the same boolean this function always returned, just sourced
     * differently. Why the change: filesystem_lastBankLoadLoadedScene()
     * reads op_bank_loaded_scene, which filesystem_start() resets to 0 at
     * the top of every new operation (filesystem.c:20625) -- including the
     * settings-write operation on_bank_load_complete() now chains before
     * this function's sole caller (menu.c:8154) ever runs. Reading the live
     * field here would make a real, non-empty Bank Load intermittently look
     * empty to Menu. Affiliates: pm_pending_bank_loaded_scene,
     * on_bank_load_complete(), menu.c:8154.
     */
    return pm_pending_bank_loaded_scene;
}
```

**Inputs/Output**: identical return value in every case that mattered
before this change (the function is only ever consulted immediately after a
Bank Load completes, and the snapshot is refreshed on every single Bank
Load completion, success or failure) — the only behavior difference is that
it now survives the chained settings write instead of being vulnerable to
it.
**Affiliates**: `menu.c:8154` (sole caller), `on_bank_load_complete()`
(Change 7, sole writer of the snapshot).

---

### Change 11 — `knowledge_files/specification_reference/DEV_MODES.md`: document `'K'`

Insert immediately after the existing `B` paragraph (`DEV_MODES.md:285-289`,
the "Session 052 Bank present-mask witness" paragraph), before the "2026-08-16
root-Scene hardware fixture" paragraph:

```markdown
`K` is the unconditional Bank Load/Save completion witness, added Session
056 as the direct Bank counterpart of `R`: flags bit 0 reports whether the
callback observed `FS_STATUS_DONE`; bit 1 selects Load (0) versus Save (1).
The value is the target Bank library slot. A missing `K` record for an
attempted Bank Load/Save proves `on_bank_load_complete()`/
`on_bank_save_complete()` (`presetManager.c`) was never reached; unlike `B`,
which is emitted deep inside `filesystem.c`'s own commit phase and proves
what the commit did, `K` proves whether Preset's completion callback ran at
all and what it observed. Every Bank Load/Save is also expected to chain one
immediate `FS_INTERNAL_OP_SAVE_GLOBALS` write before Menu unlocks (Session
056; see `S056_BANK_SETTINGS_CORRECTION.md`), so a successful `K` should
normally be followed by one full `S`/`A`/`V`/(`M`/`C`/`P` or the recovery
path)/`T` sequence for that settings write before Menu's busy indicator
clears.
```

Also update `tools/decode_devlogs.py` and `tools/devlog_unpack.py`'s stage
lookup tables to add `'K'` (and its two flag meanings) alongside the
existing entries for `'B'`/`'R'`, so a trace captured after this change
decodes instead of falling back to an "unknown stage" line. Both scripts
import their lookup tables from a shared source per their own docstrings
(`devlog_unpack.py` explicitly "imports decode_devlogs's lookup tables
directly") — confirm the exact table location at implementation time and
add one entry there rather than in both files separately.

---

## Testing / verification checklist (for whoever implements this)

- Confirm `filesystem_desc(FS_FILE_SETTINGS)->supports_save` really is the
  `1` it appears to be at `filesystem.c:329` (struct field order was
  inferred from `filesystem_requestLoad()`'s `supports_load` check, not
  independently confirmed against the `fs_file_desc_t` typedef itself).
- Confirm the `menu_refreshSavedLibraryName()` question flagged in Change 8.
- Hardware-retest the exact scenario from `S056_BANK_TESTS.md`: Bank Load
  while playback is running, then pull the card immediately (no deliberate
  wait this time, since that's no longer supposed to matter) and confirm
  `settings.cfg`'s `active_bank` and both AutoSave records already reflect
  the new Bank, and `asavetrc.bin` contains a `'K'` record plus a full
  settings-write lifecycle sequence.
- Confirm a *failed* settings write (e.g. temporarily force
  `FS_INTERNAL_OP_SAVE_GLOBALS` to fail) still lets Menu unlock promptly
  rather than hanging, and that `active_bank` converges once the normal
  debounced retry succeeds later.
- Confirm the empty-Bank Load branch (`filesystem.c:10644`) is covered by
  this same fix — it funnels into the same `on_bank_load_complete()`
  callback, so no separate change should be needed, but this was reasoned
  from code reading, not exercised on hardware.

---

## Implementation log

### 2026-08-22 — source implementation landed

- Added trace stage `K` in `AutosaveTrace.h`. Bit 0 is the callback's observed
  `FS_STATUS_DONE`; bit 1 selects Save versus Load; the value is the requested
  Bank slot. `presetManager.c` emits it as the first action in both Bank
  completion callbacks.
- Added `filesystem_handleSettingsWriteResult(fs_status_t)` as the shared
  retry-policy helper. The existing debounced settings callback still owns its
  own `filesystem_ack()`, while the Bank bridge calls the helper and lets
  Preset acknowledge the chained terminal operation.
- Bank Load/Save success now acknowledges the Bank operation, starts the public
  `filesystem_requestSave(FS_FILE_SETTINGS, 0u, ...)` path immediately, and
  delays `PRESET_UPDATE_READY` until that settings write reaches its final
  filesystem completion. A refused request falls back to immediate Bank
  completion so Menu cannot self-deadlock; a failed settings write re-arms the
  normal debounce and does not falsely fail the already-successful Bank.
- Preset snapshots `filesystem_lastBankLoadLoadedScene()` before starting the
  settings operation, because `filesystem_start()` clears
  `op_bank_loaded_scene`. The snapshot and pending Bank operation identity use
  exactly 2 bytes of ordinary SRAM1 `.bss`, owned by Preset for the current
  Bank completion.
- Confirmed `FS_FILE_SETTINGS` is numbered=0, supports_load=1, and
  supports_save=1; confirmed `menu_refreshSavedLibraryName()` reads the
  durable Bank name cache/index and does not depend on reset Bank-load scratch.
  Added `K` decoding to the shared human-readable decoder and the compact
  decoder through its existing import path.
- Verification status: `git diff --check` passed; `make clean && make` passed
  with the linked image reporting `text=381572`, `data=400`, `bss=94744`; the
  `make img` packaging check passed; and synthetic Load/Save `K` records were
  decoded correctly by both decoder entry points. Hardware retest remains
  pending.
