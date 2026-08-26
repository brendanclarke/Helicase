# settings.cfg Safe-Write — Implementation Plan

Status: **implemented — build verified 2026-08-26.** Companion to
`S057_AUTOSAVE_WRITER_WRAP.md` §3b, which first flagged that every boot
re-serializes `settings.cfg` via a direct in-place truncate with no backup —
unlike AutoSave's `.hcprms1/2`, a power loss mid-write can leave it empty or
torn, silently reverting `active_bank` (and everything else) to firmware
defaults with no error surfaced. This document is the agreed fix design.

---

## 1. Why this is one fix, not two

`filesystem_markSettingsDirty()` is called from both Bank Load/Save
(`filesystem.c:10645, 10800, 14042`) and ordinary Global Menu edits, but
both feed the same debounced scheduler
(`filesystem_settingsWriterSchedule_tick()`), which starts the same single
operation, `FS_INTERNAL_OP_SAVE_GLOBALS`, which runs the same
`filesystem_saveGlobals_tick()` state machine. There is no separate
"autowriter" — fixing `filesystem_saveGlobals_tick()`'s write mechanism once
makes both triggers safe. **Bank Load's own code does not change at all.**

---

## 2. Design: temp file + validated promote

Replace the current single `afatfs_fopen(STORAGE_SETTINGS_FILENAME, "w", ...)`
truncate-in-place with:

1. Write the complete new content to a **temp file** (new-file create, not a
   truncate of anything meaningful — see §4).
2. Close it, **then explicitly sync** (see §5 — this is the load-bearing
   step).
3. Only after sync confirms the temp file is durably on the card: remove
   the existing `settings.cfg` (if present), then rename the temp file to
   `settings.cfg`.
4. At boot, before the existing settings load runs at all: check for a
   leftover temp file. If one exists, it proves a promotion was
   interrupted; validate and promote it, or discard it, before proceeding
   to the existing (unmodified) load path.

No new low-level AsyncFATFS code is needed — every primitive this reuses is
already implemented and already hardware-exercised elsewhere:

- `afatfs_fopen(name, "w", cb)` / `filesystem_writeTextLine()` — the same
  open-and-stream mechanism the current writer already uses, just pointed
  at a different filename.
- `afatfs_sync()` (`asyncfatfs.c:1144-1156`) — same call AutoSave's writer
  already uses three times per transaction.
- `afatfs_removeObjects_lfn(name, AFATFS_REMOVE_FILES_ONLY, cb)` — the same
  "remove every case-folded match of this name, tolerate zero matches"
  primitive AutoSave already uses to clear its inactive `.hcprms` target,
  and Instrument/Kit saves already use before rewriting member files.
- `afatfs_renameObject_lfn(old, new, matchMode, aliasOut, cb)` — the same
  primitive `filesystem_blockRename()` uses for Kit quarantine. Confirmed
  constraint: it fails with `AFATFS_RESULT_ALREADY_EXISTS` if the target
  name is already occupied (`asyncfatfs.c:4790`) — this is *why* step 3
  removes the old file before renaming, not instead of it.

---

## 3. Naming

**Plainly visible, no dot-prefix.** `.hcnames`/`.hcprms1/2` are opaque
binary wire-format files nobody's meant to read directly; `settings.cfg` is
plain text specifically meant to be human-inspectable, and a leftover temp
file after an interrupted write is exactly the evidence someone debugging a
bad boot should be able to find sitting in plain sight, not hidden.
Proposed name: `settings.tmp` — clearly paired with `settings.cfg` by name
alone. (Exact string TBD at implementation time; any plainly-visible,
obviously-paired name works.)

No separate cleanup step is needed on the success path: rename doesn't
copy, it relabels the existing object in place
(`ASYNCFATFS_REFERENCE.md:348-349` — "preserves the object's first cluster,
file size, attributes, timestamps"). Once the rename to `settings.cfg`
completes, `settings.tmp` simply no longer exists as a name. Only a
*rejected* (invalid or orphaned) temp file needs an explicit remove, during
boot recovery.

---

## 4. Why opening the temp file with "w" is safe (unlike today's scheme)

The current bug is dangerous specifically because `settings.cfg`'s *live,
trusted* content is what gets truncated in place. The temp file carries no
such risk: nothing depends on preserving whatever (if anything) was
previously sitting at that name. If a stale temp file exists from an
earlier interrupted attempt in the same session, truncating and
overwriting it is exactly the right behavior — it's about to be fully
rewritten from scratch either way.

---

## 5. Sync placement — the load-bearing correction to the original proposal

Initial design review raised: could a still-incomplete temp file ever get
promoted? Answer worked out in discussion: **no, provided sync is placed
correctly**, and that placement is the one piece that must not be skipped.

`afatfs_fclose()` completing means the write was accepted into the cache
and the close-time directory entry save was *queued* — not that it's
physically on the card. `afatfs_sync()`'s own doc comment calls this out
directly: "public persistence boundary for callers that have finished a
write flow... waits for dirty sectors and in-flight SD writes." AutoSave's
writer already treats close and sync as two separate required steps at
every one of its own irreversible junctures (`AUTOSAVE.md`: "closes **and
syncs** the invalid copy," "writes **and syncs** the CRC," "...closes,
**and syncs**"). The current `filesystem_saveGlobals_tick()` has *zero*
sync calls anywhere — a separate, smaller pre-existing gap this fix also
closes.

**Binding rule for implementation:** `afatfs_sync()` must be called after
closing the temp file and its completion must be awaited **before** the
old-file removal step starts. With that ordering:

- Crash before sync completes → removal of the old `settings.cfg` is never
  reached; it's untouched. Any temp file left behind was never confirmed
  durable and boot recovery must not trust its mere existence as proof of
  completeness (see §7).
- Crash after sync completes → the temp file is guaranteed fully durable
  (all data clusters and its final directory entry) before removal/rename
  ever starts. There is no window where a genuinely partial temp file is
  sitting there available to be promoted.

This is why the terminator line (§6) is not load-bearing for correctness —
sync is what actually closes the gap. The terminator is being kept anyway,
per instruction, for its own separate reason.

---

## 6. Terminator line — kept for future format-expansion, not for this fix

Requested explicitly: keep a terminator/count line even though sync alone
already makes promotion-of-a-partial-file impossible. Rationale: it gives
any future settings-format expansion a cheap, self-checking completeness
signal without needing a format-version bump or touching the sync-based
safety guarantee this fix already provides.

Current `filesystem_nextSettingsLine()` (`filesystem.c:12689-12768`) emits
17 lines (`case 0u`..`case 16u`: `format`, `version`, then 15 keyed
fields ending in `autosave`), `default:` returns 0 to signal end-of-content.

Add one new final line, **`case 17u`**, emitted last:

```
lines=17
```

— where `17` is the count of preceding key lines (not counting itself).
Whoever adds a new settings key in the future increments both the new
key's own case and this declared count; no other format change needed.

Validation rule for any consumer that wants a completeness check (used by
boot recovery in §7, not by the normal load path in §8): the file is
complete only if every line parses without error **and** the last line
encountered before EOF is a `lines=N` line whose `N` equals the number of
successfully parsed lines that preceded it. A clean-but-early EOF (crash
mid-content, but happens to land right after a syntactically valid line)
is now distinguishable from genuine completion, because the required
terminator would simply be absent.

---

## 7. Boot-time recovery — new prelude before the existing load

Add a new pass **before** `filesystem_loadGlobals_tick()`'s existing phase
0, which is otherwise completely unmodified:

**Pass A (new) — temp-file discovery and resolution:**

1. Attempt to open the temp file for read.
2. **Not found** → nothing to do; proceed directly to Pass B (existing load
   path, untouched).
3. **Found** → read and validate it using the existing
   `filesystem_parseSettingsLine()` (same function, no duplicated parser),
   plus the terminator check from §6:
   - **Valid** (every line parses, terminator present and count matches) →
     promote: remove the existing `settings.cfg` if present (tolerate "not
     found" as success, per the same singleton-removal discipline already
     established for `.hcprms`/`.hcnames` in `AUTOSAVE.md`; treat any real
     scan/open/type error as a hard error, not a silent pass), then rename
     the temp file to `settings.cfg`.
   - **Invalid** (any parse error, or terminator missing/mismatched, or a
     read/open error partway through) → discard: remove the temp file only.
     The existing `settings.cfg`, if any, was never touched and remains
     authoritative.
4. Either way, fall through to **Pass B**.

**Pass B (unchanged) — the existing `filesystem_loadGlobals_tick()`**, now
guaranteed to see a single, consistent `settings.cfg` (or none, on genuine
first boot) regardless of what happened on the previous boot's write.

This reuses one parser for both the recovery-validation pass and the real
load pass — no second implementation of "what does a valid settings.cfg
look like."

---

## 8. What does not change

- `filesystem_markSettingsDirty()`, the debounce scheduler
  (`filesystem_settingsWriterSchedule_tick()`), and every caller (Bank
  Load, Bank Save, Global Menu edits) — untouched. They only ever mark
  dirty or start `FS_INTERNAL_OP_SAVE_GLOBALS`; none of them know or care
  how the write itself is implemented.
- `filesystem_nextSettingsLine()`'s existing 17 field cases — untouched,
  only gains one new terminator case.
- The existing revision-check discipline at close
  (`op_settings_change_revision` captured at open, compared at finish to
  decide whether `fs_settings_dirty` clears or a newer mutation arrived
  mid-write and must re-arm) — must be preserved unchanged across the new
  phases; it doesn't interact with the promote mechanics at all.
- P1/P2 from `S057_AUTOSAVE_WRITER_WRAP.md` §3a/§3b (Bank Save present-mask
  union, redundant-boot-write accept/gate decision) — independent items,
  not entangled with this fix.

---

## 9. New phases (informal — exact enum values are an implementation detail)

**`filesystem_saveGlobals_tick()`**, appended after the existing
open/write/close sequence (now targeting the temp filename instead of
`settings.cfg` directly):

```
... existing OPEN / WAIT_OPEN / WRITE_LINES / CLOSE / WAIT_CLOSE (temp file) ...
SYNC        -> afatfs_sync()
WAIT_SYNC   -> block until sync reports complete
REMOVE_OLD  -> afatfs_removeObjects_lfn(settings.cfg, FILES_ONLY, cb)
WAIT_REMOVE
RENAME      -> afatfs_renameObject_lfn(temp -> settings.cfg, ...)
WAIT_RENAME
FINISH      -> filesystem_finish(...), same revision-check as today
```

**`filesystem_loadGlobals_tick()`**, new prelude before existing phase 0:

```
CHECK_TEMP        -> afatfs_fopen(temp, "r", cb)
WAIT_CHECK_TEMP
  not found  -> fall through to existing phase 0 (unchanged)
  found      -> READ_AND_VALIDATE_TEMP (reuses filesystem_parseSettingsLine()
                 + §6 terminator check)
    valid    -> REMOVE_OLD_SETTINGS -> RENAME_TEMP -> existing phase 0
    invalid  -> REMOVE_TEMP -> existing phase 0
```

---

## 10. Test plan (for the implementation session)

- Normal case: setting changed, debounce fires, `settings.cfg` updates,
  temp file does not persist afterward.
- Power-cut simulation (or actual hardware pull) at each new phase
  boundary: mid temp-write, right after temp close (before sync),
  right after sync (before remove-old), mid remove-old, mid rename. Confirm
  boot recovery reaches the correct outcome in every case (old file intact,
  or new file promoted — never neither, never a torn file surviving as
  `settings.cfg`).
- Confirm a temp file deliberately truncated short (simulating a crash
  mid-write that *did* reach sync somehow, or a hand-edited fixture) is
  rejected by the terminator check and discarded, leaving the existing
  `settings.cfg` in place.
- Confirm first-boot (`settings.cfg` doesn't exist, no temp file) is
  unaffected — falls straight through Pass A to the existing defaults path.
- Confirm the Bank Load fallback case (boot requests a Bank that doesn't
  exist, falls back, `settings.cfg` must reflect the actual loaded Bank)
  still works unchanged, since `filesystem_markSettingsDirty()` itself is
  untouched.

---

## 11. Full Implementation — Code Changes

Status: **all 6 changes applied, build verified clean 2026-08-26.** Every
change site is listed below with the exact code added or replaced, its
source-level location at the time this section was written, and the comment
text that accompanies it. The changes are organized file-by-file, then in the
order they appear in each file. No new files were created.

### RAM allocation notice (per project policy)

This implementation adds **4 bytes** of static `.bss` in normal SRAM1,
lifetime `filesystem_loadGlobals_tick()` only. These are recovery-validation
scratch bytes declared alongside the existing settings-scheduler variables
in `filesystem.c`:

| Variable | Type | Purpose |
|----------|------|---------|
| `op_settings_recovery_valid` | `uint8_t` | 1 while every line parsed OK; 0 on any error. After the close phase, doubles as the promote/discard selector (1 = promote). |
| `op_settings_recovery_line_count` | `uint8_t` | Running count of all lines consumed by the reader. |
| `op_settings_recovery_terminator_n` | `uint8_t` | The `N` extracted from the last `lines=N` line. |
| `op_settings_recovery_terminator_seen` | `uint8_t` | 1 when the most recently processed line was a `lines=N` terminator; 0 otherwise. Must be 1 at EOF for the file to be declared complete. |

No other RAM allocation (globals, statics, linker sections, DMA buffers, or
stack changes) is introduced.

---

### 11.1  `Core/Hardware/SD/storageTypes.h` — temp filename constant

**Location:** after line 50 (`#define STORAGE_SETTINGS_FILENAME "settings.cfg"`).

**Add:**

```c
/*
 * Temp-file target for the safe settings writer. The safe-write flow
 * serializes new settings content here first, syncs it durable, then
 * promotes it to STORAGE_SETTINGS_FILENAME via remove-old + rename.
 * Plainly visible (no dot-prefix) so a leftover after an interrupted
 * write is discoverable by anyone inspecting the card root.
 */
#define STORAGE_SETTINGS_TEMP_FILENAME "settings.tmp"
```

**Why this must exist:** Every other constant in this section names a product
file target. The temp filename must be a shared constant, not a local string,
because it is referenced by both the save state machine (which creates and
writes it) and the load state machine (which discovers, validates, and either
promotes or discards it at boot).

---

### 11.2  `filesystem.c` — recovery-validation state variables

**Location:** after line 1543 (`static uint8_t op_settings_write_active = 0u;`),
within the settings-scheduler variable block.

**Add:**

```c
/*
 * Boot recovery-validation scratch for the settings temp-file prelude.
 *
 * What: tracks validity, line count, and terminator state while the boot
 * recovery prelude reads and validates a leftover settings.tmp. After the
 * temp file is closed, op_settings_recovery_valid doubles as the
 * promote/discard path selector: 1 = promote (remove old settings.cfg then
 * rename temp), 0 = discard (remove the invalid temp file).
 *
 * Inputs: the text lines read from settings.tmp by filesystem_readTextLine().
 * Outputs: one promote/discard decision at the end of the read pass.
 * Affiliates: filesystem_loadGlobals_tick()'s recovery prelude,
 * filesystem_parseSettingsLine(), and the §6 terminator-count check.
 *
 * Lifetime: meaningful only during FS_INTERNAL_OP_LOAD_GLOBALS phases 0-10;
 * not referenced by any other operation. 4 bytes total, normal SRAM .bss.
 */
static uint8_t op_settings_recovery_valid = 0u;
static uint8_t op_settings_recovery_line_count = 0u;
static uint8_t op_settings_recovery_terminator_n = 0u;
static uint8_t op_settings_recovery_terminator_seen = 0u;
```

---

### 11.3  `filesystem.c` — terminator-line detection helper

**Location:** immediately before `filesystem_parseSettingsLine()` (currently
at line 2131), grouped with the other settings text helpers
(`filesystem_trimSettingsText`, `filesystem_parseSettingsU16`,
`filesystem_settingsParamForKey`).

**Add:**

```c
/*
 * Detect a settings terminator line and extract its declared count.
 *
 * What: returns 1 if the NUL-terminated line matches "lines=N" (where N is a
 * valid unsigned decimal integer that fits in a uint8_t), storing N in *n_out.
 * Returns 0 for all other lines, leaving *n_out unmodified.
 *
 * Why: the boot recovery prelude must distinguish a genuine terminator from an
 * ordinary data line, and extract N for the completeness cross-check, without
 * embedding format knowledge in the state machine itself. The normal load path
 * (Pass B) never calls this; it accepts "lines" as an unknown key via
 * filesystem_parseSettingsLine()'s existing unknown-key pass-through.
 *
 * Inputs: one NUL-terminated text line from filesystem_readTextLine() (newline
 * already stripped). Outputs: *n_out receives N on match; return value signals
 * whether the match succeeded. Affiliates: filesystem_loadGlobals_tick()
 * recovery phase 2 and the §6 terminator design.
 */
static uint8_t filesystem_isSettingsTerminatorLine(const char *line,
                                                    uint8_t *n_out)
{
    uint16_t parsed;

    if (strncmp(line, "lines=", 6u) != 0)
        return 0u;
    if (!filesystem_parseSettingsU16(line + 6u, &parsed))
        return 0u;
    if (parsed > 255u)
        return 0u;
    *n_out = (uint8_t)parsed;
    return 1u;
}
```

---

### 11.4  `filesystem.c` — terminator case in `filesystem_nextSettingsLine()`

**Location:** `filesystem_nextSettingsLine()`, currently at line 12764
(between `case 16u:` for "autosave" and `default: return 0u;`).

**Add new case 17u before `default:`:**

```c
    case 17u:
        /*
         * Self-checking terminator for future format expansion.
         *
         * What: emits "lines=17\n" as the final line, where 17 is the count
         * of preceding data lines (cases 0u..16u). Why: gives any future
         * consumer a cheap completeness signal that distinguishes a genuine
         * end-of-file from a clean-but-early EOF after a crash. Adding a new
         * settings key in the future requires incrementing both the key's own
         * case and this declared count. The boot recovery prelude validates
         * this count; the normal load path ignores it as an unknown key.
         * Affiliates: §6 of S057_SETTINGS_WRITE_SAFE.md.
         */
        return filesystem_formatAssignmentU16Line(dst, cap, "lines", 17u);
```

**Why this must exist:** See §6. The terminator is not load-bearing for the
safe-write correctness (sync is), but it provides a cheap format-expansion
guard and allows the recovery prelude to distinguish a truncated file from a
complete one without requiring the sync gate's state to survive a reboot.

---

### 11.5  `filesystem.c` — `filesystem_saveGlobals_tick()` rewrite

**Location:** lines 16048–16106 (the entire function plus its header comment).

**Replace the entire block** (header comment and function body) with the
expanded safe-write state machine. The new phases are:

```
Phase 0:  OPEN           — create/truncate settings.tmp
Phase 1:  WAIT_OPEN      — wait for on_file_opened callback
Phase 2:  WRITE_LINES    — stream all 18 lines (17 data + terminator)
Phase 3:  CLOSE          — close temp file
Phase 4:  WAIT_CLOSE     — wait for on_file_closed callback
Phase 5:  SYNC_TEMP      — pump afatfs_sync() until temp is durable
Phase 6:  REMOVE_OLD     — remove settings.cfg (tolerate not-found)
Phase 7:  WAIT_REMOVE    — wait for on_remove_complete callback
Phase 8:  RENAME         — rename settings.tmp → settings.cfg
Phase 9:  WAIT_RENAME    — wait for on_rename_complete callback
Phase 10: FINISH         — filesystem_finish(FS_STATUS_DONE)
```

**Exact replacement code:**

```c
/* -----------------------------------------------------------------------
** SAVE SETTINGS state machine — safe-write via temp + sync + promote.
**
** Phases: 0=open temp, 1=wait_open, 2=write lines, 3=close temp,
**         4=wait_close, 5=sync, 6=remove old, 7=wait_remove,
**         8=rename, 9=wait_rename, 10=finish.
**
** What: writes settings to a temp file, syncs it durable, then atomically
** promotes it to settings.cfg via remove-old + rename. A power loss at any
** point either leaves the previous settings.cfg untouched (crash before
** sync) or leaves a durable, complete temp file that boot recovery will
** find and promote (crash between sync and rename completion).
**
** Inputs: live parameter_values[] and BankData, captured revision from
** the debounced scheduler. Outputs: one durable settings.cfg after the
** final flush. Affiliates: filesystem_settingsWriterSchedule_tick(),
** filesystem_loadGlobals_tick()'s recovery prelude, S057 design §2/§5.
** ----------------------------------------------------------------------- */
static void filesystem_saveGlobals_tick(void)
{
    switch (op_phase) {
    case 0: /* OPEN temp file */
        /*
         * Snapshot the settings revision before the first line is emitted.
         * Input: live dirty revision. Output: the final flush may clear dirty
         * only if no later Global/provenance event advanced it. Why: line-by-
         * line serialization is not an atomic SRAM snapshot. Affiliates:
         * filesystem_complete() and the background settings scheduler.
         */
        op_settings_change_revision = fs_settings_change_revision;
        op_settings_write_active = 1u;
        op_file_ready = false;
        op_file = NULL;
        /*
         * Target the temp file, not settings.cfg. Truncating settings.tmp is
         * safe regardless of whether a stale copy exists: nothing depends on
         * preserving temp-file content. This is the key difference from the
         * former in-place truncate of the live settings.cfg. See §4.
         */
        if (!afatfs_fopen(STORAGE_SETTINGS_TEMP_FILENAME, "w", on_file_opened))
            return;
        op_phase = 1;
        return;

    case 1: /* WAIT_OPEN */
        if (!op_file_ready) return;
        if (op_file == NULL) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_write_line_index = 0u;
        op_write_line_len = 0u;
        op_write_line_offset = 0u;
        op_phase = 2;
        return;

    case 2: /* WRITE LINES (including terminator) */
        if (filesystem_writeTextLine(filesystem_nextSettingsLine, NULL))
            return;
        op_phase = 3;
        return;

    case 3: /* CLOSE temp file */
        op_close_done = false;
        if (afatfs_fclose(op_file, on_file_closed))
            op_phase = 4;
        return;

    case 4: /* WAIT_CLOSE */
        if (!op_close_done) return;
        op_phase = 5;
        return;

    case 5: /* SYNC — make temp file durable before touching old settings.cfg */
        /*
         * Persistence boundary between the temp file and the irreversible
         * remove/rename that follows. afatfs_sync() returns true only when
         * every dirty cache sector and every in-flight SD write has completed.
         * Without this sync, a crash after close but before the cache drains
         * could leave a partially-written temp file that the recovery prelude
         * would then validate and incorrectly promote. See §5.
         */
        if (!afatfs_sync())
            return;
        op_phase = 6;
        return;

    case 6: /* REMOVE_OLD — remove existing settings.cfg before rename */
        /*
         * afatfs_renameObject_lfn() fails with AFATFS_RESULT_ALREADY_EXISTS if
         * the target name is occupied. The old settings.cfg must be removed
         * first. Tolerate not-found (first boot, or a previous boot's recovery
         * already promoted): afatfs_removeObjects_lfn() reports OK when zero
         * matching objects exist. Case-insensitive match ensures any case
         * variant is retired. See §2 step 3.
         */
        op_remove_done = 0u;
        op_remove_result = AFATFS_RESULT_OK;
        if (!afatfs_removeObjects_lfn(STORAGE_SETTINGS_FILENAME,
                                      AFATFS_MATCH_CASE_INSENSITIVE,
                                      AFATFS_REMOVE_FILES_ONLY,
                                      on_remove_complete))
            return;
        op_phase = 7;
        return;

    case 7: /* WAIT_REMOVE */
        if (!op_remove_done) return;
        if (op_remove_result != AFATFS_RESULT_OK) {
            /*
             * Cannot proceed to rename if the old file is still present.
             * The temp file is durable; boot recovery will find and promote
             * it on next power cycle. Error out so the scheduler retries or
             * the next boot recovers.
             */
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_phase = 8;
        return;

    case 8: /* RENAME temp → settings.cfg */
        /*
         * Promote the synced temp file to the live settings filename. Reuses
         * the shared op_repair_rename_open_name buffer for the returned 8.3
         * alias (not used further, but the API requires a non-NULL buffer).
         * Case-insensitive match on the source name handles any case variant
         * of the temp file.
         */
        op_rename_done = 0u;
        op_rename_result = AFATFS_RESULT_OK;
        memset(op_repair_rename_open_name, 0,
               sizeof(op_repair_rename_open_name));
        if (!afatfs_renameObject_lfn(STORAGE_SETTINGS_TEMP_FILENAME,
                                     STORAGE_SETTINGS_FILENAME,
                                     AFATFS_MATCH_CASE_INSENSITIVE,
                                     op_repair_rename_open_name,
                                     on_rename_complete))
            return;
        op_phase = 9;
        return;

    case 9: /* WAIT_RENAME */
        if (!op_rename_done) return;
        if (op_rename_result != AFATFS_RESULT_OK) {
            /*
             * Rename failure after old-file removal means settings.cfg is
             * absent but settings.tmp is durable. Boot recovery will find
             * and promote it. Error out to surface the condition.
             */
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_phase = 10;
        return;

    case 10: /* FINISH — standard flush + revision acknowledgment */
        /*
         * filesystem_finish(FS_STATUS_DONE) defers to the shared
         * filesystem_flushFinish_tick() which pumps afatfs_sync() until the
         * rename's directory-entry changes are durable, then calls
         * filesystem_complete() where the revision acknowledgment clears
         * fs_settings_dirty if no newer mutation arrived during the write.
         */
        filesystem_finish(FS_STATUS_DONE);
        return;

    default:
        filesystem_finish(FS_STATUS_ERROR);
        return;
    }
}
```

**What changed from the original function and why:**

| Old | New | Why |
|-----|-----|-----|
| Phase 0 opens `STORAGE_SETTINGS_FILENAME` ("w") | Phase 0 opens `STORAGE_SETTINGS_TEMP_FILENAME` ("w") | Temp file truncation is safe; live-file truncation was the original bug. §4. |
| No explicit sync | Phase 5: `afatfs_sync()` | Persistence boundary before the irreversible remove/rename. §5. The old code relied only on `filesystem_finish()`'s final flush, which ran AFTER close — no sync preceded the (now-removed) in-place truncate because there was nothing to sync before. |
| Phase 4 called `filesystem_finish(FS_STATUS_DONE)` directly | Phases 6-9: remove old + rename | The promote step that makes the temp file the new live file. §2 step 3. |
| — | Phase 10: `filesystem_finish(FS_STATUS_DONE)` | Same final flush + revision ack as before, now after rename instead of after close. |
| 5 phases (0–4) | 11 phases (0–10) | Six new phases for the sync/remove/rename promotion. |

---

### 11.6  `filesystem.c` — `filesystem_loadGlobals_tick()` rewrite

**Location:** lines 15968–16046 (the entire function plus its header comment).

**Replace the entire block** with the recovery prelude (Pass A) followed by
the existing load (Pass B, renumbered). The new phases are:

```
--- Pass A: Recovery prelude ---
Phase 0:  OPEN_TEMP           — try to open settings.tmp for read
Phase 1:  WAIT_OPEN_TEMP      — not found: → 10. Found: init, → 2
Phase 2:  READ_VALIDATE_TEMP  — read/validate loop
Phase 3:  CLOSE_TEMP          — close temp file handle
Phase 4:  WAIT_CLOSE_TEMP     — valid: → 5 (promote). Invalid: → 5 (discard)
Phase 5:  REMOVE_FILE         — promote: remove settings.cfg. Discard: remove settings.tmp
Phase 6:  WAIT_REMOVE         — promote OK: → 7. Otherwise: → 10
Phase 7:  RENAME              — rename settings.tmp → settings.cfg
Phase 8:  WAIT_RENAME         — → 10

--- Pass B: Existing load (unchanged logic, renumbered) ---
Phase 10: DEFAULTS + OPEN     (was phase 0)
Phase 11: WAIT_OPEN           (was phase 1)
Phase 12: READ_LINES          (was phase 2)
Phase 13: CLOSE               (was phase 3)
Phase 14: WAIT_CLOSE          (was phase 4)
```

**Exact replacement code:**

```c
/* -----------------------------------------------------------------------
** LOAD SETTINGS state machine — with boot recovery prelude.
**
** Pass A (phases 0-8): boot recovery prelude. Checks for a leftover
**   settings.tmp from an interrupted safe-write. If found: validates it
**   with the existing parser + terminator check; promotes (valid) or
**   discards (invalid). Either way, falls through to Pass B.
** Pass B (phases 10-14): the existing settings.cfg load, unmodified in
**   logic, only renumbered from the former phases 0-4.
**
** What: ensures that a power loss during settings persistence never leaves
** the system with both files missing and no authoritative settings state.
** Affiliates: filesystem_saveGlobals_tick()'s safe-write phases,
** filesystem_parseSettingsLine(), filesystem_isSettingsTerminatorLine(),
** S057 design §7.
** ----------------------------------------------------------------------- */
static void filesystem_loadGlobals_tick(void)
{
    uint8_t line_ready;
    uint8_t eof;
    storage_status_t st;

    switch (op_phase) {

    /* =================================================================
     * Pass A — Recovery prelude: settings.tmp discovery and resolution.
     * ================================================================= */

    case 0: /* OPEN_TEMP — try to open settings.tmp for read */
        /*
         * Attempt to open the temp file before any settings defaults are
         * applied. A leftover settings.tmp proves a safe-write promotion was
         * interrupted on the previous boot; its presence gates the validation
         * and promote/discard decision that follows. The open uses "r" so
         * afatfs reports not-found as a NULL callback, not an error. See §7.
         */
        op_file_ready = false;
        op_file = NULL;
        if (!afatfs_fopen(STORAGE_SETTINGS_TEMP_FILENAME, "r", on_file_opened))
            return;
        op_phase = 1;
        return;

    case 1: /* WAIT_OPEN_TEMP */
        if (!op_file_ready) return;
        if (op_file == NULL) {
            /*
             * No temp file: the common case. No interrupted write to resolve.
             * Skip the entire recovery prelude and enter the existing load at
             * Pass B (phase 10).
             */
            op_phase = 10;
            return;
        }
        /*
         * Temp file found: initialize recovery-validation state and begin
         * reading. line_count increments for every line consumed; the
         * terminator detector records N and a seen-flag so the EOF check can
         * verify that the final line was "lines=N" with N == line_count - 1.
         */
        op_settings_recovery_valid = 1u;
        op_settings_recovery_line_count = 0u;
        op_settings_recovery_terminator_n = 0u;
        op_settings_recovery_terminator_seen = 0u;
        op_line_len = 0u;
        op_phase = 2;
        return;

    case 2: /* READ_VALIDATE_TEMP — read and validate each line */
        st = filesystem_readTextLine(op_file, op_line_buf, &op_line_len,
                                     sizeof(op_line_buf), &line_ready, &eof);
        if (st == STORAGE_STATUS_WAIT)
            return;
        if (st != STORAGE_STATUS_OK) {
            /* Read error (e.g. line too long): mark invalid and close. */
            op_settings_recovery_valid = 0u;
            op_phase = 3;
            return;
        }
        if (line_ready) {
            op_settings_recovery_line_count++;
            /*
             * Check for the terminator line before passing to the parser.
             * The parser silently accepts "lines=N" as an unknown key, which
             * is correct for Pass B's normal load, but the recovery prelude
             * also needs to extract and track N for the §6 completeness check.
             * The seen-flag is set on terminator lines and cleared on
             * non-terminator lines, so at EOF it is 1 only if the very last
             * line was a terminator.
             */
            if (filesystem_isSettingsTerminatorLine(op_line_buf,
                    &op_settings_recovery_terminator_n)) {
                op_settings_recovery_terminator_seen = 1u;
            } else {
                op_settings_recovery_terminator_seen = 0u;
            }
            /*
             * Validate syntax via the existing parser. This also applies
             * values to parameter_values[]/BankData, but Pass B will call
             * filesystem_resetSettingsToDefaults() (phase 10) before its own
             * read pass, so these are harmlessly overwritten.
             */
            if (filesystem_parseSettingsLine(op_line_buf) != FS_STATUS_DONE)
                op_settings_recovery_valid = 0u;
            return;
        }
        if (eof) {
            /*
             * §6 completeness check: valid only if every line parsed without
             * error AND the final line was a "lines=N" terminator whose N
             * equals the number of preceding data lines. N == line_count - 1
             * because the terminator itself is counted in line_count but does
             * not count toward the "17 preceding key lines" it declares.
             */
            if (!op_settings_recovery_valid ||
                !op_settings_recovery_terminator_seen ||
                op_settings_recovery_terminator_n !=
                    (uint8_t)(op_settings_recovery_line_count - 1u)) {
                op_settings_recovery_valid = 0u;
            }
            op_phase = 3;
        }
        return;

    case 3: /* CLOSE_TEMP */
        op_close_done = false;
        if (afatfs_fclose(op_file, on_file_closed))
            op_phase = 4;
        return;

    case 4: /* WAIT_CLOSE_TEMP — decide promote or discard */
        if (!op_close_done) return;
        /*
         * op_settings_recovery_valid now serves double duty as the path
         * selector: 1 = promote (valid temp file, remove old settings.cfg
         * then rename), 0 = discard (invalid temp file, remove settings.tmp).
         * The existing load (Pass B) follows either path.
         */
        op_phase = 5;
        return;

    case 5: /* REMOVE_FILE — remove the appropriate target */
        /*
         * Promote path: remove the old settings.cfg before renaming the
         * validated temp file into its place. Tolerate not-found (genuine
         * first boot, or a prior recovery already promoted successfully).
         *
         * Discard path: remove the invalid settings.tmp. The existing
         * settings.cfg (if any) was never touched and remains authoritative.
         *
         * Both paths use the same shared on_remove_complete callback and the
         * same case-insensitive singleton-removal discipline established for
         * AutoSave's .hcprms target (AUTOSAVE.md). See §7.
         */
        op_remove_done = 0u;
        op_remove_result = AFATFS_RESULT_OK;
        if (!afatfs_removeObjects_lfn(
                op_settings_recovery_valid
                    ? STORAGE_SETTINGS_FILENAME
                    : STORAGE_SETTINGS_TEMP_FILENAME,
                AFATFS_MATCH_CASE_INSENSITIVE,
                AFATFS_REMOVE_FILES_ONLY,
                on_remove_complete))
            return;
        op_phase = 6;
        return;

    case 6: /* WAIT_REMOVE */
        if (!op_remove_done) return;
        if (op_settings_recovery_valid &&
            op_remove_result == AFATFS_RESULT_OK) {
            /*
             * Promote path: old settings.cfg removed (or was already absent).
             * Proceed to rename settings.tmp → settings.cfg.
             */
            op_phase = 7;
            return;
        }
        /*
         * Discard path, or promote-path remove error. In the discard case,
         * the existing settings.cfg is untouched. In the remove-error case,
         * settings.cfg is still present so the existing load will read it
         * (the temp file remains as well; next boot retries recovery). Either
         * way, skip rename and enter the existing load at Pass B.
         */
        op_phase = 10;
        return;

    case 7: /* RENAME — promote settings.tmp → settings.cfg */
        /*
         * Reuses the shared op_repair_rename_open_name buffer for the
         * returned 8.3 alias (required by the API but not used further).
         * Only reached when the promote path's remove succeeded, so the
         * target name settings.cfg is guaranteed unoccupied.
         */
        op_rename_done = 0u;
        op_rename_result = AFATFS_RESULT_OK;
        memset(op_repair_rename_open_name, 0,
               sizeof(op_repair_rename_open_name));
        if (!afatfs_renameObject_lfn(STORAGE_SETTINGS_TEMP_FILENAME,
                                     STORAGE_SETTINGS_FILENAME,
                                     AFATFS_MATCH_CASE_INSENSITIVE,
                                     op_repair_rename_open_name,
                                     on_rename_complete))
            return;
        op_phase = 8;
        return;

    case 8: /* WAIT_RENAME */
        if (!op_rename_done) return;
        /*
         * Rename success or failure, proceed to the existing load. On
         * success: settings.cfg is the promoted temp file; Pass B re-reads
         * and applies it. On failure (extremely unlikely): settings.cfg was
         * already removed so Pass B sees no file and uses defaults;
         * settings.tmp remains durable for next boot's recovery.
         */
        op_phase = 10;
        return;

    /* =================================================================
     * Pass B — Existing settings.cfg load (unchanged logic, renumbered).
     * ================================================================= */

    case 10: /* DEFAULTS + OPEN */
        op_file_ready = false;
        op_file = NULL;
        fs_stale_warning_pending = FS_STALE_WARNING_NONE;
        filesystem_resetSettingsToDefaults();
        op_close_status = FS_STATUS_DONE;
        op_line_len = 0u;
        if (!afatfs_fopen(STORAGE_SETTINGS_FILENAME, "r", on_file_opened))
            return;
        op_phase = 11;
        return;

    case 11: /* WAIT_OPEN */
        if (!op_file_ready) return;
        if (op_file == NULL) {
            /*
             * No settings file is valid first-boot/card state.
             *
             * Output: defaults from phase 10 remain live. No former raw
             * globals fallback is attempted because that filename is retired
             * and should not be recognized by current firmware.
             */
            filesystem_finish(FS_STATUS_DONE);
            return;
        }
        op_phase = 12;
        return;

    case 12: /* READ LINES */
        st = filesystem_readTextLine(op_file, op_line_buf, &op_line_len,
                                     sizeof(op_line_buf), &line_ready, &eof);
        if (st == STORAGE_STATUS_WAIT)
            return;
        if (st != STORAGE_STATUS_OK) {
            op_close_status = FS_STATUS_ERROR;
            op_phase = 13;
            return;
        }
        if (line_ready) {
            op_close_status = filesystem_parseSettingsLine(op_line_buf);
            if (op_close_status != FS_STATUS_DONE)
                op_phase = 13;
            return;
        }
        if (eof) {
            filesystem_sanitizeLoadedGlobals();
            op_close_status = FS_STATUS_DONE;
            op_phase = 13;
        }
        return;

    case 13: /* CLOSE */
        op_close_done = false;
        if (afatfs_fclose(op_file, on_file_closed))
            op_phase = 14;
        return;

    case 14: /* WAIT_CLOSE */
        if (!op_close_done) return;
        /*
         * Data is in parameter_values[] and BankData. Global runtime apply
         * happens in Menu when it sees the Preset completion, matching the old
         * deferred apply boundary.
         */
        filesystem_finish(op_close_status);
        return;

    default:
        filesystem_finish(FS_STATUS_ERROR);
        return;
    }
}
```

**What changed from the original function and why:**

| Aspect | Old | New | Why |
|--------|-----|-----|-----|
| Phase count | 5 (0–4) | 15 (0–8, 10–14) | Recovery prelude adds 9 phases; existing load renumbered from 0→10. |
| First action at boot | Defaults + open settings.cfg | Open settings.tmp for read | Must discover and resolve any leftover temp file before reading settings.cfg. §7. |
| Validation | — | Phase 2: reads temp file, checks each line with `filesystem_parseSettingsLine()` + terminator detection | Reuses the one existing parser. No duplicated "what does valid settings.cfg look like" logic. §7 step 3. |
| Promote/discard | — | Phases 5-8: remove + optional rename | Promotes valid temp files; discards invalid ones. Either way, Pass B sees a clean state. §7 steps 3-4. |
| Existing load logic | Phases 0–4 | Phases 10–14 (identical logic) | Unchanged in logic; only renumbered to make room for the prelude. §8. |

---

### 11.7  `filesystem.c` — save header comment update

**Location:** the block comment immediately above `filesystem_saveGlobals_tick()`
(currently lines 16048–16052).

**Replace:**

The old header comment reads:
```
** SAVE SETTINGS state machine
**
** Phases: 0=open, 1=wait_open, 2=write lines, 3=close, 4=wait_close
```

The new header is integrated into the replacement code in §11.5 above. No
separate change needed — the replacement covers it.

---

### 11.8  `filesystem.c` — load header comment update

**Location:** the block comment that preceded `filesystem_loadGlobals_tick()`
(if any informal phase summary existed above the original function).

Same as §11.7 — the replacement code in §11.6 includes the new header.

---

### 11.9  Summary of what does NOT change

These sites were reviewed and confirmed unchanged:

| Site | Why unchanged |
|------|---------------|
| `filesystem_markSettingsDirty()` (line 19300) | Only marks dirty + restarts debounce. No write-mechanism knowledge. §8. |
| `filesystem_settingsWriterSchedule_tick()` (line 19610) | Only starts `FS_INTERNAL_OP_SAVE_GLOBALS`. §8. |
| `filesystem_settingsWriterCompleted()` (line 19590) | Only re-arms dirty on error. §8. |
| `filesystem_complete()` (line 3243) | Revision-check logic is operation-agnostic; new phases don't interact with it. §8. |
| `filesystem_finish()` (line 3309) | Standard flush gate; works identically for the new final phase. §8. |
| `filesystem_flushFinish_tick()` (line 3506) | Standard sync pump; unchanged. §8. |
| `filesystem_parseSettingsLine()` (line 2131) | Accepts `lines=N` as an unknown key (no error). §6. |
| `filesystem_start()` (line 20533) | Sets `op_phase = 0` for both LOAD_GLOBALS and SAVE_GLOBALS. §8. |
| `filesystem_tick()` dispatch (line 20400) | `case FS_INTERNAL_OP_LOAD_GLOBALS` / `case FS_INTERNAL_OP_SAVE_GLOBALS` — still dispatch to the same two tick functions. §8. |
| `on_file_opened()`, `on_file_closed()`, `on_remove_complete()`, `on_rename_complete()` callbacks | Shared across all operations; already used by the save/load globals machinery's new phases exactly as other operations use them. |
| `op_repair_rename_open_name` (line 1121) | Reused as the rename alias buffer; only one operation runs at a time, so there is no ownership conflict with `FS_INTERNAL_OP_REPAIR_NAMES`. |
| `op_remove_done`, `op_remove_result` (line 563) | Shared async-remove latch; only one operation runs at a time. |
| `op_rename_done`, `op_rename_result` (line 1092) | Shared async-rename latch; only one operation runs at a time. |
| `filesystem.h` | No public API changes. Save/load globals are internal operations. |
| All callers of `filesystem_markSettingsDirty()` | Bank Load, Bank Save, Global Menu edits — unchanged. §1, §8. |
| `config.h` / `SETTINGS_AUTOWRITE_DEBOUNCE_MS` | Debounce timing unchanged. |

---

### 11.10  Implementation order

1. **§11.1** — add `STORAGE_SETTINGS_TEMP_FILENAME` to `storageTypes.h`.
2. **§11.2** — add recovery-validation state variables to `filesystem.c`.
3. **§11.3** — add `filesystem_isSettingsTerminatorLine()` helper.
4. **§11.4** — add `case 17u` terminator to `filesystem_nextSettingsLine()`.
5. **§11.5** — replace `filesystem_saveGlobals_tick()` with the safe-write version.
6. **§11.6** — replace `filesystem_loadGlobals_tick()` with the recovery-prelude version.
7. **Build** — `make clean && make && make img`.
8. **Test** — per §10.

---

## 12. Implementation Log

**2026-08-26 — all code changes applied and build verified.**

Changes applied in order per §11.10:

| Step | Section | File | Result |
|------|---------|------|--------|
| 1 | §11.1 | `storageTypes.h` | `STORAGE_SETTINGS_TEMP_FILENAME` added after `STORAGE_SETTINGS_FILENAME`. |
| 2 | §11.2 | `filesystem.c` | 4 recovery-validation statics added after `op_settings_write_active`. 4 bytes .bss. |
| 3 | §11.3 | `filesystem.c` | `filesystem_isSettingsTerminatorLine()` added before `filesystem_parseSettingsLine()`. |
| 4 | §11.4 | `filesystem.c` | `case 17u` terminator added to `filesystem_nextSettingsLine()` before `default:`. |
| 5 | §11.5 | `filesystem.c` | `filesystem_saveGlobals_tick()` replaced: 5 phases → 11 phases (temp+sync+promote). |
| 6 | §11.6 | `filesystem.c` | `filesystem_loadGlobals_tick()` replaced: 5 phases → 15 phases (recovery prelude + renumbered load). |

Build: `make clean && make && make img` — zero errors, zero new warnings.
Image: `build/LXRV2_lxr02.img` (382516 bytes).

RAM delta: +4 bytes `.bss` (recovery scratch), as declared in §11.2.
No changes to `filesystem.h` or any other file.

---

## 13. Recovery-Promotion Hardware Test — 2026-08-26

**Test shim:** temporary 3-line short-circuit at `filesystem_saveGlobals_tick()`
phase 6 (REMOVE_OLD) that called `filesystem_finish(FS_STATUS_DONE)` and
returned, skipping remove/rename so settings.tmp remained on the card alongside
settings.cfg. Shim was reverted after boot 1.

**Procedure and results:**

| Boot | Firmware | Card before boot | Card after boot | Result |
|------|----------|-----------------|-----------------|--------|
| 1 | Test shim (phase 6 short-circuit) | `settings.cfg` only | `settings.cfg` + `settings.tmp` (both 277 bytes, identical content) | Save phases 0-5 ran: temp file created, written, synced durable. Phase 6 shim prevented promote. Both files present. |
| 2 | Clean (shim reverted) | `settings.tmp` only (`settings.cfg` deleted manually) | `settings.cfg` only (`settings.tmp` consumed by rename) | Recovery prelude found settings.tmp, validated it (18 lines, terminator `lines=17` matched 17 preceding data lines), promoted via remove+rename. Pass B loaded the promoted file normally. |

**Content verification — all three files byte-identical:**

```
format=helicase.settings
version=1
active_bank=15
bpm=125
ext_sync=4
quantisation=2
midi_chan_global=1
midi_filt_tx=0
midi_filt_rx=0
midi_routing=0
screensaver_on_off=1
bar_reset_mode=1
prescaler_clock_in=0
prescaler_clock_out1=0
follow=0
osc_wave_interp=1
autosave=1
lines=17
```

**Confirmed:**
- Terminator line (`lines=17`) emitted correctly by `case 17u`.
- Recovery prelude validation passed (terminator present, N=17 == line_count-1).
- Promote path executed: remove old (tolerated not-found), rename succeeded.
- Post-promote load read the promoted file and applied all settings correctly.
- No settings.tmp remained after the promote — rename consumed it.

**Status: PASS.** Test shim reverted, clean firmware is the current build.
