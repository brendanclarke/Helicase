# Session 054 Plan — Fix Root-Caused Bugs #1/#2 and Instrument Diagnostic Evidence

**Status:** Parts 1–3 implemented; Part 4 build complete, card fixtures pending
hardware access. Source changes are recorded below. Every claim below was
re-verified by reading the current working
tree (`Core/Hardware/SD/filesystem.c`, `Core/Hardware/SD/asyncfatfs/*`,
`Core/Bank/Scene/AutosaveTrace.*`, `Core/Bank/Scene/Autosave.c`,
`Core/Menu/menu.c`, `Core/Bank/Scene/Preset/presetManager.c`) as of this
session, not from `SESSION_054_PREPLAN_ASYNC_RECURSIVE_CLEANUP.md`, specs, or
memory. Where this pass changed or sharpened a prior conclusion, that is
called out explicitly. Line numbers are current-file line numbers at time of
writing; re-anchor by function/variable name if they drift before this plan
is executed.

Every code-change block below is written so its "What / Why / Inputs /
Outputs / Affiliates" text can be copied verbatim as the adjacent comment for
that change, per this project's commenting convention (see any existing
function in `filesystem.c` for the house style this matches).

---

## Part 1 — Fix root-caused bug #1: delete-resolver spurious error

### 1.1 What re-verification changed

The Session 053 pre-plan identified one bad gate, in
`FS_DELETE_SLOT_DELETE_MATCH`. Re-reading `filesystem_deleteSlotDirectory_tick()`
in full this session found a **second, identical bad gate** one case earlier,
in `FS_DELETE_SLOT_WAIT_CLOSE_SCAN`, that the pre-plan missed. Both must be
fixed together or the second one will still convert a legitimately slow
*scan* (before any delete even starts) into a spurious overwrite failure.

### 1.2 Exact defect

`op_delete_slot_timeout_observed` (`filesystem.c:620`) is set by the
diagnostic-only stall counter at `filesystem.c:12539-12550`:

```c
op_delete_slot_timeout_ticks++;
if (op_delete_slot_timeout_ticks > 50000 &&
    !op_delete_slot_timeout_observed) {
    uint8_t subphase = afatfs_getDeleteTreePhase();
    if (op_delete_slot_phase == FS_DELETE_SLOT_DELETE_MATCH && subphase != 0xFF) {
        filesystem_makeNamedErrorCode("TDel", subphase);
    } else {
        filesystem_makeNamedErrorCode("TOut", (uint8_t)op_delete_slot_phase);
    }
    /* Observation is not cancellation: native delete has no abort API,
     * so retain ownership until its callback releases the handle. */
    op_delete_slot_timeout_observed = 1u;
    if (op_delete_slot_phase == FS_DELETE_SLOT_DELETE_MATCH ||
        op_delete_slot_phase == FS_DELETE_SLOT_WAIT_SCAN ||
        op_delete_slot_phase == FS_DELETE_SLOT_WAIT_CLOSE_SCAN)
        return FS_STATUS_BUSY;
    ...
}
```

For the three phases explicitly named there
(`DELETE_MATCH`/`WAIT_SCAN`/`WAIT_CLOSE_SCAN`), the intent documented right
above — *"Observation is not cancellation"* — is that setting
`op_delete_slot_timeout_observed = 1u` is a **pure diagnostic latch**: it must
not by itself change the eventual pass/fail outcome. Two later completion
checks violate that intent by treating the latch as a failure condition on
its own, independent of whether the underlying native operation actually
succeeded:

**Site A — `FS_DELETE_SLOT_WAIT_CLOSE_SCAN` completion, `filesystem.c:12659-12672`:**

```c
case FS_DELETE_SLOT_WAIT_CLOSE_SCAN:
    if (!op_close_done)
        return FS_STATUS_BUSY;
    op_delete_slot_dir = NULL;
    if (op_delete_slot_scan_error ||
        op_delete_slot_match_count > 1u ||
        op_delete_slot_timeout_observed) {          /* <-- bug */
        op_delete_slot_phase = FS_DELETE_SLOT_ERROR;
        return FS_STATUS_ERROR;
    }
    if (op_delete_slot_match_count == 0u) {
        op_delete_slot_phase = FS_DELETE_SLOT_DONE;
        return FS_STATUS_DONE;
    }
    ...
```

If the child-enumeration scan of a large directory (many Kit/Scene/Bank
children, or a card with many stray entries) alone takes long enough to trip
the 50,000-tick diagnostic counter, the scan can still finish correctly
(`op_delete_slot_scan_error` clear, `match_count` 0 or 1) and this gate still
forces `FS_STATUS_ERROR` — before any delete has even been attempted.

**Site B — `FS_DELETE_SLOT_DELETE_MATCH` completion, `filesystem.c:12691-12703`:**

```c
case FS_DELETE_SLOT_DELETE_MATCH:
    if (!op_delete_tree_done)
        return FS_STATUS_BUSY;
    if (op_delete_tree_result != AFATFS_RESULT_OK) {
        op_delete_slot_phase = FS_DELETE_SLOT_ERROR;
        return FS_STATUS_ERROR;
    }
    if (op_delete_slot_timeout_observed) {           /* <-- bug */
        op_delete_slot_phase = FS_DELETE_SLOT_ERROR;
        return FS_STATUS_ERROR;
    }
    op_delete_slot_phase = FS_DELETE_SLOT_DONE;
    return FS_STATUS_DONE;
```

This is the one that produced the observed `ScnS05` defect: a nested Scene
tree (embedded Kit dir + 6 instrument files + pattern.pat + effects.fx, each
retired at most one name-run sector batch or one FAT cluster per poll, per
`RECURSIVE_TREE_DELETE_REIMPLEMENT.md`'s own bounded-yield design) can
legitimately exceed 50,000 foreground polls — most polls return `BUSY`
waiting on SD hardware, not making structural progress. Once the counter
trips, `op_delete_slot_timeout_observed` latches `1` and is **never cleared**,
so `afatfs_deleteTree()` finishing afterward with `AFATFS_RESULT_OK` is still
converted to `FS_STATUS_ERROR` here. On the card, the delete-and-recreate had
already completed correctly (`019 Organity` → `019 Rollin`, complete tree);
the save only *reported* failure.

### 1.3 Fix

Remove the timeout-observed condition from both completion gates. The native
result (`op_delete_tree_result`) and the scan's own error/duplicate latches
(`op_delete_slot_scan_error`, `op_delete_slot_match_count`) are already
sufficient and authoritative; `op_delete_slot_timeout_observed` should
influence only diagnostics (§3 adds a durable diagnostic record in its place,
so the signal is not lost, just no longer treated as a verdict).

**Site A — `filesystem.c:12663-12668`, remove the third OR term:**

```c
    if (op_delete_slot_scan_error ||
        op_delete_slot_match_count > 1u) {
        op_delete_slot_phase = FS_DELETE_SLOT_ERROR;
        return FS_STATUS_ERROR;
    }
```

**Site B — `filesystem.c:12698-12701`, delete the block entirely:**

```c
    case FS_DELETE_SLOT_DELETE_MATCH:
        if (!op_delete_tree_done)
            return FS_STATUS_BUSY;
        if (op_delete_tree_result != AFATFS_RESULT_OK) {
            op_delete_slot_phase = FS_DELETE_SLOT_ERROR;
            return FS_STATUS_ERROR;
        }
        op_delete_slot_phase = FS_DELETE_SLOT_DONE;
        return FS_STATUS_DONE;
```

**In-place doc comment** (replace the existing one-line comment at
`filesystem.c:621-628`, which currently documents only the completion latch,
with this, covering the corrected contract of the whole timeout mechanism):

```c
/*
 * Completion latch for asyncfatfs' maintained recursive deleter.
 *
 * What: op_delete_tree_done/op_delete_tree_result record whether/how the one
 * foreground-pumped afatfs_deleteTree() call for this slot finished.
 * op_delete_slot_timeout_observed is a separate, purely diagnostic latch: it
 * records that the 50,000-poll stall counter fired at some point during this
 * slot's scan or delete, but it must never by itself turn a subsequent
 * AFATFS_RESULT_OK/clean-scan outcome into FS_STATUS_ERROR. "Observation is
 * not cancellation" (see the stall-counter comment below): native delete has
 * no abort API, so a slow-but-legitimate nested-tree delete must still be
 * allowed to finish and report its own true result. The two completion gates
 * that read this latch (FS_DELETE_SLOT_WAIT_CLOSE_SCAN and
 * FS_DELETE_SLOT_DELETE_MATCH) therefore key their pass/fail decision only on
 * op_delete_slot_scan_error / op_delete_slot_match_count / op_delete_tree_result.
 * A fired stall observation is still visible: it durably records a
 * AUTOSAVE_TRACE_STAGE_PHASE_STALL trace record (see filesystem_pollPhaseStall()
 * below) so real-world delete duration can be measured and the 50,000 budget
 * re-tuned from field evidence, without ever failing a save that actually
 * succeeded. Inputs: afatfs_deleteTree()'s callback for the object captured by
 * the slot scanner. Output: the slot-delete state machine learns whether the
 * one native delete finished and how. Affiliates: filesystem_deleteSlotDirectory_tick(),
 * filesystem_pollPhaseStall(), autosaveTrace_record().
 */
```

### 1.4 Diagnostic hook at the same site (folds into §3's shared helper)

Refactor the bespoke `op_delete_slot_timeout_ticks`/`op_delete_slot_last_phase`
pair (`filesystem.c:12530-12531`, `12535-12539`) to use the new shared
`filesystem_pollPhaseStall()` helper introduced in §3.1, and change the stall
callback to *also* emit a trace record instead of only writing the on-screen
`fs_error_code`. See §3.2 for the exact new call site text — it replaces
`filesystem.c:12539-12560`'s stall-detection block.

### 1.5 Retest

1. Rebuild, then repeat the `SD_OVERWRITE_TEST` Scene-overwrite fixture (load
   Scene, edit, save to an occupied slot). Confirm no `ScnS05` on a
   successful overwrite.
2. Deliberately build a large nested Scene (several instruments, a big Kit)
   to try to actually cross the 50,000-poll mark and confirm the operation
   now still completes successfully rather than erroring — and confirm a
   `AUTOSAVE_TRACE_STAGE_PHASE_STALL` record appears in `asavetrc.bin` for
   that run (proof the diagnostic still fires; it just no longer fails the
   save).
3. Repeat for Kit and Bank overwrite as a regression check, since both share
   `filesystem_deleteSlotDirectory_tick()`.

---

## Part 2 — Fix root-caused bug #2: HCNAMES source provenance not staged on Save

### 2.1 What re-verification changed

The pre-plan characterized this as one uniform gap ("all four Save paths
never call `filesystem_setResidentSource()`") and proposed a uniform
one-line-per-site fix. Re-reading the full path from each Save completion
point through to the actual `/.hcnames` file write shows the four Save paths
are **not architecturally uniform**, and the fix must match each one's real
wiring:

| Save path | How its HCNAMES write actually reaches disk today | Fix shape |
|---|---|---|
| Scene | Self-transitions directly into `FS_INTERNAL_OP_UPDATE_HCNAMES_SCENE` in the same tick function (`filesystem.c:13927-13928`) | Stage source, no other wiring needed |
| Bank | Self-contained direct read-then-rewrite of all 129 rows inside `filesystem_saveBankDirectory_tick()` itself (`filesystem.c:12987-13459`), never touches the shared `FS_INTERNAL_OP_UPDATE_HCNAMES_*` machinery at all | Stage source, no other wiring needed |
| Kit | Publishes a name into the small `fs_identity_name` LCD cache only (`filesystem.c:12930-12932`); the *durable* `/.hcnames` write happens later, only if Menu's session-exit flush (`menu_endResidentNameScratchSession()` → `filesystem_requestUpdateResidentKitNames()`, `menu.c:3441-3478`) runs with a nonzero dirty-Scene mask. This flush **is** wired up and known-working for Kit (Kit Load uses the identical staging pattern and Session 051 confirmed correct Kit HCNAMES publication on hardware). | Stage source at the same point Kit already stages its name; the existing working flush will pick it up |
| Instrument | Publishes a name into `fs_identity_name` only (`filesystem.c:11576-11579`). **No code path ever triggers `FS_INTERNAL_OP_UPDATE_HCNAMES_INSTRUMENT` for a completed root Instrument Save.** `filesystem_requestUpdateResidentInstrumentNames()` (`filesystem.c:20728`, declared `filesystem.h:627`) has **zero callers anywhere in the repository** — confirmed by a full-repository grep. This is a structural gap, not just a missing staging call: today, a root Instrument Save's name *and* source both only ever live in the ephemeral `fs_identity_name` cache and are never durably written to `/.hcnames` by any reachable code path. | Stage source **and** add the missing publish trigger |

This directly explains the test-report evidence more precisely than the
pre-plan did: Instrument row 69 stayed `docwird1 -` not just because the
*source* wasn't staged, but because **neither the name nor the source ever
reached a register write** — `docwird1` was left over from whatever the
register already had, not written by the observed Instrument Save at all.

### 2.2 Fix — Kit Save (`filesystem.c:12917-12941`, `filesystem_saveKitDirectory_tick()` case 21)

Current code:

```c
    case 21:
        if (!afatfs_chdir(NULL))
            return;
        if (op_kit_save_mode == STORAGE_INSTRUMENT_SAVE_NORMAL)
            filesystem_setIdentityName(FS_IDENTITY_KIT_ROW,
                                       preset_currentName);
        /*
         * Defer successful completion until the boot-equivalent Kit rebuild
         * chain has finished. ...
         */
        op_library_index_rebuild_kind = FS_NAME_CACHE_KIT;
        op_library_index_rebuild_pending = 1u;
        filesystem_finish(FS_STATUS_DONE);
        return;
```

Add source staging immediately after the existing identity-name update, only
for a normal (non-morph-only) save, mirroring Kit Load's identical staging
at `filesystem.c:8344-8358`:

```c
    case 21:
        if (!afatfs_chdir(NULL))
            return;
        if (op_kit_save_mode == STORAGE_INSTRUMENT_SAVE_NORMAL) {
            uint8_t instrument_slot;

            filesystem_setIdentityName(FS_IDENTITY_KIT_ROW,
                                       preset_currentName);
            /*
             * Stage this Kit Save's row/source pair for the next durable
             * HCNAMES rewrite.
             *
             * What: marks the saved Kit row as directly sourced from the just
             * written op_slot, and its six member Instrument rows as
             * inheriting from that Kit, using the same dirty-flag staging
             * mechanism Kit Load already uses at the equivalent point.
             *
             * Why: filesystem_saveKitDirectory_tick() only ever updates the
             * ephemeral fs_identity_name LCD cache directly; the durable
             * /.hcnames rewrite happens later and separately, whenever Menu's
             * session-exit flush (menu_endResidentNameScratchSession() ->
             * filesystem_requestUpdateResidentKitNames()) next runs with this
             * Scene's bit set in its dirty mask. That flush already existed
             * and already reads the same fs_resident_source[] dirty-staging
             * array Kit Load stages into, so staging here — instead of only
             * updating the LCD cache — is the minimal change that makes Save
             * symmetric with Load. Without this, filesystem_cacheResidentRecord()
             * (filesystem.c:4387) accepts the old on-card source for this row
             * because no caller ever marked it dirty, which is exactly the
             * "Kit saved to a new slot, HCNAMES source stayed at the loaded
             * slot" defect from LOAD_SAVE_AUTO_ELEMENT_TEST_REPORT.md.
             *
             * Inputs: op_slot (the just-created/overwritten root Kit
             * directory's numbered slot) and op_kit_save_source_scene (the
             * resident Scene whose Kit was saved). Outputs: two dirty-flagged
             * fs_resident_source[] cells; no file I/O here.
             *
             * Affiliates: filesystem_setResidentSource(), Kit Load's
             * equivalent staging (filesystem.c:8344-8358),
             * menu_endResidentNameScratchSession(),
             * filesystem_cacheCurrentResidentKitNames().
             */
            (void)filesystem_setResidentSource(
                filesystem_residentKitRow(op_kit_save_source_scene),
                op_slot);
            for (instrument_slot = 0u;
                 instrument_slot < STORAGE_KIT_SLOT_COUNT;
                 instrument_slot++) {
                (void)filesystem_setResidentSource(
                    filesystem_residentInstrumentRow(
                        op_kit_save_source_scene, instrument_slot),
                    FS_RESIDENT_SOURCE_INHERIT);
            }
        }
        /*
         * Defer successful completion until the boot-equivalent Kit rebuild
         * chain has finished. ...
         */
        op_library_index_rebuild_kind = FS_NAME_CACHE_KIT;
        op_library_index_rebuild_pending = 1u;
        filesystem_finish(FS_STATUS_DONE);
        return;
```

(`filesystem_residentKitRow`/`filesystem_residentInstrumentRow`/
`filesystem_setResidentSource` are all already `static`/exported in this same
file — no header change needed for this site.)

### 2.3 Fix — Scene Save (`filesystem.c:13894-13929`, `filesystem_saveSceneDirectory_tick()` case 37, non-Bank-child branch)

Current code (the `else` branch reached only for a direct root Scene Save,
not a Bank-child payload):

```c
            op_scene_load_scene_mask =
                (uint16_t)(1u << op_kit_save_source_scene);
            op_library_index_rebuild_kind = FS_NAME_CACHE_SCENE;
            op_library_index_rebuild_pending = 1u;
            filesystem_prepareResidentNamesCache();
            ...
            filesystem_bootLoggingArm("HCNAMES ");
            current_op = FS_INTERNAL_OP_UPDATE_HCNAMES_SCENE;
            op_phase = 0u;
            return;
```

Add source staging before `filesystem_prepareResidentNamesCache()` (staging
must happen before the register read that
`FS_INTERNAL_OP_UPDATE_HCNAMES_SCENE` performs, so the dirty flag protects it
— matching the same ordering Scene Load uses at `filesystem.c:8978-8990`):

```c
            op_scene_load_scene_mask =
                (uint16_t)(1u << op_kit_save_source_scene);
            op_library_index_rebuild_kind = FS_NAME_CACHE_SCENE;
            op_library_index_rebuild_pending = 1u;
            /*
             * Stage this Scene Save's row/source pair before the immediately
             * following HCNAMES read-merge-rewrite hand-off.
             *
             * What: marks the saved Scene row as directly sourced from
             * op_slot, and its embedded Kit and six Instrument rows as
             * inheriting from that Scene, mirroring Scene Load's identical
             * staging at filesystem.c:8978-8990.
             *
             * Why: unlike Kit Save, Scene Save hands off to
             * FS_INTERNAL_OP_UPDATE_HCNAMES_SCENE in the very next phase of
             * this same tick, with no intervening Menu-driven flush — so this
             * is the only chance to mark the row dirty before
             * filesystem_cacheResidentRecord() reads the old on-card value
             * for it. Without this, a Scene saved to a new slot keeps
             * reporting its previously-loaded slot as source (the clearest
             * failure in LOAD_SAVE_AUTO_ELEMENT_TEST_REPORT.md: Scene saved
             * to 031, row still said source 004).
             *
             * Inputs: op_slot (the just-created/overwritten root Scene
             * directory's numbered slot) and op_kit_save_source_scene (the
             * resident Scene just saved). Outputs: dirty-flagged
             * fs_resident_source[] cells for the Scene row, its Kit row, and
             * its six Instrument rows; no file I/O here.
             *
             * Affiliates: filesystem_setResidentSource(), Scene Load's
             * equivalent staging (filesystem.c:8978-8990),
             * filesystem_cacheCurrentResidentSceneNames(),
             * FS_INTERNAL_OP_UPDATE_HCNAMES_SCENE.
             */
            (void)filesystem_setResidentSource(
                filesystem_residentSceneRow(op_kit_save_source_scene),
                op_slot);
            (void)filesystem_setResidentSource(
                filesystem_residentKitRow(op_kit_save_source_scene),
                FS_RESIDENT_SOURCE_INHERIT);
            {
                uint8_t instrument_slot;
                for (instrument_slot = 0u;
                     instrument_slot < STORAGE_KIT_SLOT_COUNT;
                     instrument_slot++) {
                    (void)filesystem_setResidentSource(
                        filesystem_residentInstrumentRow(
                            op_kit_save_source_scene, instrument_slot),
                        FS_RESIDENT_SOURCE_INHERIT);
                }
            }
            filesystem_prepareResidentNamesCache();
            ...
```

### 2.4 Fix — Bank Save (`filesystem.c:13355-13362`, `filesystem_saveBankDirectory_tick()` case 45)

Current code:

```c
        /*
         * The newly-created final root folder is the authoritative Bank identity.
         * Update only row zero in the already-read register; selected Scene,
         * Kit, and Instrument rows were copied from HCNAMES into the just
         * written Bank tree and remain unchanged in resident memory.
         */
        filesystem_cacheResidentName(0u, op_bank_display_name);
        op_phase = 83u;
        return;
```

Add source staging beside the existing name update. This site is safe and
directly effective: `filesystem.c:13415-13420` (case 85, confirmed by
re-reading it this session) writes row 0 straight from
`fs_resident_source[0]`/`fs_list_cache_name[0]` a few phases later with no
intervening full-register re-read, and `filesystem.c:13448` clears the dirty
flag only after that write closes — exactly the ordering
`filesystem_setResidentSource()` was designed for.

```c
        /*
         * The newly-created final root folder is the authoritative Bank identity.
         * Update only row zero in the already-read register; selected Scene,
         * Kit, and Instrument rows were copied from HCNAMES into the just
         * written Bank tree and remain unchanged in resident memory.
         */
        filesystem_cacheResidentName(0u, op_bank_display_name);
        /*
         * Stage row zero's source before phase 85 streams the merged register
         * back to /.hcnames.
         *
         * What: marks the Bank row as directly sourced from op_slot, mirroring
         * Bank Load's identical staging at filesystem.c:10426
         * (filesystem_setResidentSource(FS_IDENTITY_BANK_ROW, op_slot)) —
         * the adjacent comment on that Load-side call already states Bank
         * Save "must remain symmetric" with it, which this closes.
         *
         * Why: without this, filesystem_cacheResidentRecord() (already
         * invoked earlier in this same operation at case 81/13032) retained
         * whatever source the last-read /.hcnames had for row 0, so a Bank
         * Save reports its previously-loaded slot as source. Case 85
         * (filesystem.c:13412-13434) writes row 0 straight from
         * fs_resident_source[0], and case 86 (filesystem.c:13448) clears the
         * dirty flag only once that write is durable, so staging here is
         * both necessary and sufficient.
         *
         * Inputs: op_slot (the just-created final root Bank directory's
         * numbered slot). Outputs: one dirty-flagged fs_resident_source[0]
         * cell; no file I/O here.
         *
         * Affiliates: filesystem_setResidentSource(), Bank Load's equivalent
         * staging (filesystem.c:10426), case 85's register writer.
         */
        (void)filesystem_setResidentSource(FS_IDENTITY_BANK_ROW, op_slot);
        op_phase = 83u;
        return;
```

### 2.5 Fix — Instrument Save (`filesystem.c:11541-11582`, `filesystem_saveInstrument_tick()` case 21)

This is the deeper fix. Current code (`!morph_save` branch):

```c
        if (!morph_save) {
            char display[STORAGE_KIT_DISPLAY_NAME_LEN + 1u];

            filesystem_copyInstrumentStemDisplay(
                display, op_instrument_save_display_name);
            filesystem_setIdentityName(
                (uint8_t)(FS_IDENTITY_INSTRUMENT_ROW_0 +
                          op_instrument_save_source_slot),
                display);
        }
        /*
         * Refresh the saved type's registry-owned `.hcindex` before publishing
         * Save completion. ...
         */
        op_instrument_index_type = op_instrument_save_type;
        ...
        filesystem_bootLoggingArm("INSINDEX");
        current_op = FS_INTERNAL_OP_CREATE_BOOT_INDEX;
        op_phase = 0u;
        return;
```

Root Instrument Save today only ever updates `fs_identity_name` and then
hands off to `FS_INTERNAL_OP_CREATE_BOOT_INDEX`, which rebuilds only the
Instrument *type* library index (`.hcindex`) — a completely separate
artifact from `/.hcnames`. Nothing ever requests
`FS_INTERNAL_OP_UPDATE_HCNAMES_INSTRUMENT` afterward (confirmed zero
callers of the public `filesystem_requestUpdateResidentInstrumentNames()`
wrapper, repository-wide). The correct, minimal fix mirrors what Scene Save
already does (§2.3): stage the row, then **hand off directly** into
`FS_INTERNAL_OP_UPDATE_HCNAMES_INSTRUMENT` in place, rather than going to
`FS_INTERNAL_OP_CREATE_BOOT_INDEX` and silently losing the HCNAMES
publication. The `.hcindex` rebuild must still happen — chain it via the
same `op_library_index_rebuild_kind`/`op_library_index_rebuild_pending`
deferred-completion mechanism Kit Save already uses (`filesystem.c:12935-12938`),
instead of a direct `current_op` jump, so both artifacts get updated and
only one final callback fires:

```c
        if (!morph_save) {
            char display[STORAGE_KIT_DISPLAY_NAME_LEN + 1u];

            filesystem_copyInstrumentStemDisplay(
                display, op_instrument_save_display_name);
            filesystem_setIdentityName(
                (uint8_t)(FS_IDENTITY_INSTRUMENT_ROW_0 +
                          op_instrument_save_source_slot),
                display);
            /*
             * Stage this Instrument Save's row/source pair and hand off to
             * the durable HCNAMES publish, instead of only updating the
             * ephemeral fs_identity_name LCD cache.
             *
             * What: marks the resident Instrument row for
             * (op_instrument_save_source_scene, op_instrument_save_source_slot)
             * as directly sourced (FS_RESIDENT_SOURCE_INSTRUMENT_DIRECT — the
             * '@' token), mirroring Instrument Load's identical staging at
             * filesystem.c:11213-11217, then transitions current_op the same
             * way Scene Save does (filesystem.c:13927-13928) instead of
             * jumping straight to FS_INTERNAL_OP_CREATE_BOOT_INDEX.
             *
             * Why: filesystem_requestUpdateResidentInstrumentNames()
             * (filesystem.c:20728) is the only other path that reaches
             * FS_INTERNAL_OP_UPDATE_HCNAMES_INSTRUMENT, and it has zero
             * callers anywhere in this repository (confirmed by repository
             * grep, 2026-08-19) — Menu never invokes it, so nothing has ever
             * durably published a root Instrument Save's name or source to
             * /.hcnames through any reachable path; both silently lived only
             * in fs_identity_name until the next unrelated Bank/Scene/Kit
             * register rewrite happened to carry the still-dirty-flagged
             * value along as a side effect, or (more often, per test
             * evidence) never did. This matches
             * LOAD_SAVE_AUTO_ELEMENT_TEST_REPORT.md exactly: row 69 stayed
             * `docwird1 -` — neither the new name nor '@' ever reached the
             * register.
             *
             * Inputs: op_instrument_save_source_scene/_source_slot (the
             * resident voice just saved). Outputs: one dirty-flagged
             * fs_resident_source[] cell; current_op/op_phase transition to
             * the shared HCNAMES updater in place of this operation's normal
             * completion.
             *
             * Affiliates: filesystem_setResidentSource(), Instrument Load's
             * equivalent staging (filesystem.c:11213-11217),
             * filesystem_cacheCurrentResidentInstrumentNames(),
             * FS_INTERNAL_OP_UPDATE_HCNAMES_INSTRUMENT,
             * FS_INTERNAL_OP_CREATE_BOOT_INDEX (now reached via the deferred
             * op_library_index_rebuild_pending path instead of directly).
             */
            (void)filesystem_setResidentSource(
                filesystem_residentInstrumentRow(
                    op_instrument_save_source_scene,
                    op_instrument_save_source_slot),
                FS_RESIDENT_SOURCE_INSTRUMENT_DIRECT);
            op_kit_load_scene_mask =
                (uint16_t)(1u << op_instrument_save_source_scene);
            op_slot = op_instrument_save_source_slot;
            op_library_index_rebuild_kind = FS_NAME_CACHE_INSTRUMENT;
            op_library_index_rebuild_pending = 1u;
            filesystem_bootLoggingArm("HCNAMES ");
            current_op = FS_INTERNAL_OP_UPDATE_HCNAMES_INSTRUMENT;
            op_phase = 0u;
            return;
        }
        /*
         * Refresh the saved type's registry-owned `.hcindex` before publishing
         * Save completion. ...
         */
        op_instrument_index_type = op_instrument_save_type;
        ...
        filesystem_bootLoggingArm("INSINDEX");
        current_op = FS_INTERNAL_OP_CREATE_BOOT_INDEX;
        op_phase = 0u;
        return;
```

**This site needs verification before being taken as final**, more than
§2.2-2.4: confirm during implementation that (a) `op_kit_load_scene_mask` is
the correct scratch variable `filesystem_cacheCurrentResidentInstrumentNames()`
reads to know which Scene/slot changed (it reads `op_kit_load_scene_mask`
and `op_slot` per `filesystem.c:4484-4506` — re-check that function's exact
field reads against what's set here, since it was written for the Load
call shape and this is now being driven from Save), and (b) that
`FS_INTERNAL_OP_CREATE_BOOT_INDEX`'s own entry phase 0 doesn't require
scratch fields this hand-off doesn't set. If (a) doesn't line up exactly,
the safer alternative is a **new** small dedicated tick phase that stages the
row then explicitly calls the two operations in sequence via the existing
`op_library_index_rebuild_pending` deferred-completion mechanism, rather than
reusing `op_kit_load_scene_mask` — prefer correctness over matching Scene
Save's exact shape.

### 2.6 Retest

Repeat the four `LOAD_SAVE_AUTO_ELEMENT_TEST_REPORT.md` element fixtures
(delete `/.hcnames`+`/.hcprms*`+`bootlog.bin`+`asavetrc.bin`, boot, Play, load
an element, edit, save to a new slot, power off, capture card) and confirm:

- Kit: saved row's source becomes the saved slot, not the loaded one.
- Scene: saved row's source becomes the saved slot.
- Bank: row 0's source becomes the saved slot.
- Instrument: the row gains both the saved stem name **and** `@` — this is a
  stronger check than the pre-plan's ("source becomes `@`"), since §2.5
  found the name was silently dropped too.

---

## Part 3 — Diagnostic hooks for the remaining not-yet-root-caused defects

All hooks below use the existing `autosaveTrace_record()` ring
(`Core/Bank/Scene/AutosaveTrace.h/.c`) — the "previously established logging
system" already used at runtime for exactly this purpose (its own header
says it "exists only to show which autosave lifecycle boundary was actually
reached during a bench run"; it is unconditionally `#if DEV_MODE_LOGGING`-gated
to no-op stubs in production, is IRQ-safe, drains to `asavetrc.bin`, and adds
**zero new SRAM** regardless of how many stage codes are defined — its ring
is a fixed `AUTOSAVE_TRACE_RECORD_COUNT * 8` bytes set once in `config.h`).
No new persistent storage is introduced by this section; only two new stage
codes and a handful of new call sites.

The boot-only watchdog+capsule mechanism (`fs_boot_logging_*`,
`fs_hcprms_boot_capsule`, `filesystem_writeBootFailureLogBlocking()`) was
deliberately **not** reused or extended for the runtime items below: its
recovery path calls `filesystem_tick()` in a **blocking** spin loop
(`filesystem.c:18770-18785`) and is documented as boot-only — reusing it at
runtime would violate this project's hard rule that runtime SD work must stay
asynchronous (`MEMORY.md` "General Process Reminders"). It remains correct
and untouched for boot-time use (§3.5 adds to it read-only, via the existing
per-substep code setter, not the blocking recovery path).

### 3.1 New shared helper: `filesystem_pollPhaseStall()`

Add near the top of `filesystem.c`'s utility section (a good location is
immediately before `filesystem_deleteSlotDirectory_tick()`, since that is
its first caller and currently owns the only existing instance of this
pattern).

```c
/*
 * Detect a cooperative state-machine phase that stops advancing.
 *
 * What: a small edge-triggered stall counter. A caller owns one uint8_t
 * "last observed phase" cell and one uint32_t "ticks since that phase was
 * last seen changing" cell (both plain statics, no new SRAM class — this
 * generalizes the pattern already hand-written once for delete-slot at
 * op_delete_slot_timeout_ticks/op_delete_slot_last_phase). Each call compares
 * the caller's current phase number against the last-seen value: a change
 * resets the counter to zero; no change increments it. The function returns
 * 1 exactly once, on the poll where the counter first exceeds
 * threshold_ticks (an edge, not a level) — so a caller's own one-shot latch
 * (if it needs a persistent "already reported" flag,  like
 * op_delete_slot_timeout_observed) is set only once per genuine stall, not
 * once per poll after the first.
 *
 * Why: this project's state machines are foreground-pumped and must never
 * block, so "the operation looks stuck" can only ever be detected
 * cooperatively — by noticing a phase number hasn't changed across many
 * polls, exactly as the pre-existing delete-slot counter already did. Three
 * more sites in this session's plan need the identical pattern (Bank Save
 * entry, the runtime AutoSave parameter drain, and delete-slot's own
 * refactor); duplicating the counter/reset logic a fourth time was judged
 * worse than one shared, documented helper. This function performs no
 * logging itself — callers decide what to do with a returned 1 (write an
 * on-screen error code, emit a trace record, force the operation to fail),
 * because the right response differs by site (delete-slot must keep waiting
 * for the native callback; the runtime drain should abort the write).
 *
 * Inputs: phase (the caller's current state-machine phase number, any
 * caller-defined small integer); last_phase/stall_ticks (the caller-owned
 * persistent cells described above); threshold_ticks (the poll-count budget
 * for this call site — sites differ, so this is not a shared constant).
 * Output: 1 on the single poll where the stall first crosses the threshold,
 * 0 on every other poll (including every poll once the phase next changes,
 * which implicitly resets and re-arms detection for the new phase).
 *
 * Affiliates: filesystem_deleteSlotDirectory_tick() (§1.4),
 * filesystem_saveBankDirectory_tick() (§3.3), and
 * filesystem_autosaveParameterDrain_tick() (§3.4).
 */
static uint8_t filesystem_pollPhaseStall(uint8_t phase,
                                         uint8_t *last_phase,
                                         uint32_t *stall_ticks,
                                         uint32_t threshold_ticks)
{
    if (phase != *last_phase) {
        *last_phase = phase;
        *stall_ticks = 0u;
        return 0u;
    }
    (*stall_ticks)++;
    return (uint8_t)(*stall_ticks == threshold_ticks + 1u);
}
```

### 3.2 New trace stage: `AUTOSAVE_TRACE_STAGE_PHASE_STALL`

Add to `Core/Bank/Scene/AutosaveTrace.h`, in the `autosave_trace_stage_t`
enum, alongside the existing single-letter stages (`'X'` is unused today):

```c
    /*
     * X: a foreground-pumped state machine's phase counter crossed its
     * cooperative stall threshold (filesystem_pollPhaseStall()). This is a
     * pure observation — the owning state machine may still finish
     * successfully afterward — not a failure record. flags identifies which
     * call site fired; value32 carries site-specific coordinates so a single
     * eight-byte record is still self-describing without a separate decoder
     * table per site.
     */
    AUTOSAVE_TRACE_STAGE_PHASE_STALL = 'X',
```

And the flags/value32 layout documentation, alongside the existing per-stage
`#define` blocks:

```c
/*
 * X (PHASE_STALL) flags: bits 0..2 select the call site
 * (0 = delete-slot resolver, 1 = Bank Save entry, 2 = runtime AutoSave
 * parameter drain). Bits 3..7 are site-specific single-bit flags, currently
 * used only by site 0 (bit 3 set when the stall was observed inside the
 * native afatfs_deleteTree() delete itself, i.e. FS_DELETE_SLOT_DELETE_MATCH,
 * rather than during the parent scan).
 */
#define AUTOSAVE_TRACE_PHASE_STALL_SITE_MASK        0x07u
#define AUTOSAVE_TRACE_PHASE_STALL_SITE_DELETE_SLOT 0u
#define AUTOSAVE_TRACE_PHASE_STALL_SITE_BANK_ENTRY  1u
#define AUTOSAVE_TRACE_PHASE_STALL_SITE_DRAIN       2u
#define AUTOSAVE_TRACE_PHASE_STALL_FLAG_IN_NATIVE_DELETE (1u << 3u)

/*
 * X value32 layout, common to every site: bits 0..7 the caller's own phase
 * number at the moment of the stall (fits every site: delete-slot's
 * fs_delete_slot_phase_t, Bank Save's op_phase, and the drain's op_phase are
 * all well under 256); bits 8..17 the numbered slot involved (0..999,
 * FS_RESIDENT_NAMES_ROW_COUNT-sized headroom not needed here); bits 18..31
 * a site-specific fourteen-bit extra field:
 *   - site 0 (delete-slot): afatfs_getDeleteTreePhase()'s native subphase
 *     (0..255, using bits 18..25; bits 26..31 unused/zero) when the stall was
 *     observed inside FS_DELETE_SLOT_DELETE_MATCH, else zero.
 *   - site 1 (Bank Save entry): unused/zero (the phase number alone is
 *     sufficient at this site; bits 18..31 reserved for future use).
 *   - site 2 (drain): op_autosave_writer.stream_offset >> 4 (bits 18..31,
 *     i.e. the write progress into the 34,768-byte record in 16-byte units,
 *     giving byte-level resolution up to 262,128 which comfortably covers
 *     AUTOSAVE_RECORD_BYTES).
 */
#define AUTOSAVE_TRACE_PHASE_STALL_PHASE_SHIFT  0u
#define AUTOSAVE_TRACE_PHASE_STALL_SLOT_SHIFT   8u
#define AUTOSAVE_TRACE_PHASE_STALL_EXTRA_SHIFT  18u
```

### 3.3 Wire the shared helper into delete-slot (bug #1's own site), Bank Save entry, and the runtime drain

**Delete-slot refactor** (`filesystem.c:12530-12560`, replaces the existing
bespoke counter):

```c
static uint8_t  op_delete_slot_last_phase = 0u;
static uint32_t op_delete_slot_stall_ticks = 0u;

static fs_status_t filesystem_deleteSlotDirectory_tick(void)
{
    if (filesystem_pollPhaseStall((uint8_t)op_delete_slot_phase,
                                  &op_delete_slot_last_phase,
                                  &op_delete_slot_stall_ticks,
                                  50000u) &&
        !op_delete_slot_timeout_observed) {
        uint8_t subphase = afatfs_getDeleteTreePhase();
        uint8_t flags = AUTOSAVE_TRACE_PHASE_STALL_SITE_DELETE_SLOT;
        uint32_t value = (uint32_t)op_delete_slot_phase <<
                          AUTOSAVE_TRACE_PHASE_STALL_PHASE_SHIFT;
        value |= (uint32_t)op_delete_slot_number <<
                 AUTOSAVE_TRACE_PHASE_STALL_SLOT_SHIFT;
        if (op_delete_slot_phase == FS_DELETE_SLOT_DELETE_MATCH &&
            subphase != 0xFFu) {
            flags |= AUTOSAVE_TRACE_PHASE_STALL_FLAG_IN_NATIVE_DELETE;
            value |= (uint32_t)subphase <<
                     AUTOSAVE_TRACE_PHASE_STALL_EXTRA_SHIFT;
            filesystem_makeNamedErrorCode("TDel", subphase);
        } else {
            filesystem_makeNamedErrorCode("TOut", (uint8_t)op_delete_slot_phase);
        }
        autosaveTrace_record(AUTOSAVE_TRACE_STAGE_PHASE_STALL, flags, value);
        /* Observation is not cancellation: native delete has no abort API,
         * so retain ownership until its callback releases the handle. See
         * the completion-gate comment above op_delete_tree_done for why this
         * latch below no longer gates FS_STATUS_ERROR by itself. */
        op_delete_slot_timeout_observed = 1u;
        if (op_delete_slot_phase == FS_DELETE_SLOT_DELETE_MATCH ||
            op_delete_slot_phase == FS_DELETE_SLOT_WAIT_SCAN ||
            op_delete_slot_phase == FS_DELETE_SLOT_WAIT_CLOSE_SCAN)
            return FS_STATUS_BUSY;
        op_delete_slot_scan_error = 1u;
        if (op_delete_slot_dir)
            op_delete_slot_phase = FS_DELETE_SLOT_CLOSE_SCAN;
        else
            op_delete_slot_phase = FS_DELETE_SLOT_ERROR;
    }
    ...
```

Delete the now-superseded `op_delete_slot_timeout_ticks`,
`op_delete_slot_last_phase` (old bare `uint8_t`, replaced by the two new
cells above), and the old inline `if (op_delete_slot_phase != ...)`
reset block at `filesystem.c:12535-12539` — `filesystem_pollPhaseStall()`
now owns that reset logic.

**Bank Save entry** (`filesystem_saveBankDirectory_tick()`,
`filesystem.c:12968`, first line inside the `switch`):

```c
static uint8_t  op_bank_save_entry_last_phase = 0u;
static uint32_t op_bank_save_entry_stall_ticks = 0u;

static void filesystem_saveBankDirectory_tick(void)
{
    if (op_bank_payload_active) {
        filesystem_saveSceneDirectory_tick();
        return;
    }
    /*
     * Diagnostic-only stall observer for Bank Save's own phases (0, 80..86).
     *
     * What: records one AUTOSAVE_TRACE_STAGE_PHASE_STALL entry the first time
     * this operation's own op_phase stops advancing for more than 20,000
     * polls. Does not change op_phase, does not fail the operation, does not
     * touch op_bank_payload_active — purely observational, following the same
     * "observation is not cancellation" contract as the delete-slot stall
     * counter this pattern was generalized from (§3.1).
     *
     * Why: LOAD_SAVE_AUTO_ELEMENT_TEST_REPORT.md's SD_BANK snapshot recorded
     * a hang entering Bank Save with no forensic evidence at all — no new/
     * temp Bank directory, /.hcnames row 0 unchanged, and the AutoSave trace
     * showed only the ordinary Load/Save-page writer-suppression record. This
     * is the first Bank-Save-specific instrumentation added since that
     * report; its only job is to prove which of Bank Save's own phases
     * (0 = initial HCNAMES-register open; 80/81 = HCNAMES read; 82 = close;
     * 8..45 = inherited Scene-payload writer once op_bank_payload_active
     * flips true; 83..86 = final register rewrite) the next reproduction
     * actually stalls in, since today nothing distinguishes "never even
     * requested from Menu" from "stuck inside filesystem_saveBankDirectory_tick()
     * itself" from "stuck inside the shared Scene payload writer it delegates
     * to." Pair with the menu.c-side instrumentation in §3.6 bracketing
     * preset_saveBank(), which narrows further whether Menu ever actually
     * reached the request call.
     *
     * Inputs: op_phase. Outputs: at most one trace record per stalled phase
     * (the helper is edge-triggered, so it does not repeat every poll).
     *
     * Affiliates: filesystem_pollPhaseStall(), menu.c's preset_saveBank()
     * call site (menu.c:6946), filesystem_saveSceneDirectory_tick() (the
     * shared payload writer this delegates to once op_bank_payload_active is
     * set, which is not separately instrumented here — a stall after
     * delegation surfaces as a Scene Save stall if that path also needs this
     * treatment later).
     */
    if (filesystem_pollPhaseStall((uint8_t)op_phase,
                                  &op_bank_save_entry_last_phase,
                                  &op_bank_save_entry_stall_ticks,
                                  20000u)) {
        uint32_t value = (uint32_t)op_phase <<
                          AUTOSAVE_TRACE_PHASE_STALL_PHASE_SHIFT;
        value |= (uint32_t)op_slot << AUTOSAVE_TRACE_PHASE_STALL_SLOT_SHIFT;
        autosaveTrace_record(AUTOSAVE_TRACE_STAGE_PHASE_STALL,
                             AUTOSAVE_TRACE_PHASE_STALL_SITE_BANK_ENTRY,
                             value);
    }
    ...
```

(20,000 rather than 50,000: Bank Save's own phases before delegating to the
Scene payload writer are all single small reads/opens/closes, not a
multi-object recursive delete, so a much shorter budget is appropriate and
will surface a real stall sooner without false-triggering on ordinary SD
latency. Tune from field evidence after the first retest, same as bug #1's
50,000.)

**Runtime AutoSave parameter drain** (`filesystem_autosaveParameterDrain_tick()`,
`filesystem.c:5387`, first line inside the `switch`):

```c
static uint8_t  op_autosave_drain_last_phase = 0u;
static uint32_t op_autosave_drain_stall_ticks = 0u;

static void filesystem_autosaveParameterDrain_tick(void)
{
    /*
     * Diagnostic-only stall observer, then a real (non-blocking) bailout.
     *
     * What: records one AUTOSAVE_TRACE_STAGE_PHASE_STALL entry the first time
     * this operation's op_phase stops advancing for more than 30,000 polls,
     * then — unlike every other stall site in this plan — also forces the
     * operation to fail via the existing filesystem_autosaveWriterFinishError()
     * close-down path instead of continuing to wait forever.
     *
     * Why: this is the one site in this plan tied to a genuine reported
     * freeze rather than only a spurious-error report. LOAD_SAVE_AUTO_ELEMENT_TEST_REPORT.md's
     * SD_FREEZE snapshot showed .hcprms2 truncated at exactly 32,768 bytes
     * (of 34,768) with no bootlog.bin and no asavetrc.bin produced at all —
     * meaning either a true hard lockup (which no cooperative check anywhere
     * could ever detect or help with — its silence on retest is itself the
     * diagnostic result for that case) or, as the fully-silent trace/bootlog
     * output also matches, a genuine soft stall in a runtime drain that
     * today has **no deadline at all**: unlike the boot-only ASENSURE
     * creation path (filesystem_ensureAutosaveFiles_tick(), which already
     * has full fs_boot_logging_*/fs_hcprms_boot_capsule coverage), this
     * runtime drain's copy/CRC/commit phases (10..21, 51..67 below) have
     * never had any deadline or forensic capture — confirmed by grep: no
     * hcprmsCapsule/fs_boot_logging call appears anywhere in this function.
     * A soft stall here would hold the filesystem facade FS_STATUS_BUSY
     * forever with no existing recourse; this both reports it and recovers
     * it. 32,768 = 32 * 1024 is a suspicious round boundary — plausibly a FAT
     * cluster-size edge in the delete-tree-reimplementation's changed
     * allocation-hint search (see the "cluster allocator now searches both
     * sides of its allocation hint" comment at filesystem.c:5268) rather than
     * an AutoSave-specific bug; this hook is what will confirm or rule that
     * out on the next reproduction, by showing whether stream_offset stalls
     * exactly at a cluster boundary during afatfs_fwrite() specifically.
     *
     * Inputs: op_phase, op_autosave_writer.stream_offset. Outputs: at most
     * one trace record per stalled phase, and (new) a forced ERROR completion
     * through the existing, already-safe finish-error path — never a direct
     * afatfs_destroy()/remount like the boot-only recovery path uses, and
     * never a blocking wait.
     *
     * Affiliates: filesystem_pollPhaseStall(), filesystem_autosaveWriterFinishError(),
     * autosave_transformDrainChunk() (Autosave.c), the boot-only
     * fs_hcprms_boot_capsule mechanism this deliberately does not reuse (see
     * §3 preamble for why).
     */
    if (filesystem_pollPhaseStall((uint8_t)op_phase,
                                  &op_autosave_drain_last_phase,
                                  &op_autosave_drain_stall_ticks,
                                  30000u)) {
        uint32_t value = (uint32_t)op_phase <<
                          AUTOSAVE_TRACE_PHASE_STALL_PHASE_SHIFT;
        value |= (uint32_t)(op_autosave_writer.stream_offset >> 4) <<
                 AUTOSAVE_TRACE_PHASE_STALL_EXTRA_SHIFT;
        autosaveTrace_record(AUTOSAVE_TRACE_STAGE_PHASE_STALL,
                             AUTOSAVE_TRACE_PHASE_STALL_SITE_DRAIN,
                             value);
        filesystem_autosaveWriterFinishError();
        return;
    }
    switch (op_phase) {
    ...
```

**Caution on this last one:** unlike the other two hooks (pure observation),
this one changes runtime behavior — a drain that previously hung forever now
fails after ~30,000 polls. Confirm during implementation that
`filesystem_autosaveWriterFinishError()` (`filesystem.c:5342-5360`) is safe
to call from every phase this switch can be in when the stall check runs
(it already branches on whether `op_file`/`op_autosave_writer.target_file`
are live, so it should be, but verify against phases 51-56, the mask-read
phases, specifically, since those were not the primary phases read closely
in this session's pass).

### 3.4 New trace stage: `AUTOSAVE_TRACE_STAGE_SAVE_LIFECYCLE` — Kit-materialization and Instrument-overwrite evidence

Add to `AutosaveTrace.h`, alongside the others (`'O'` is unused):

```c
    /*
     * O: one checkpoint in a Kit/Scene/Bank/Instrument Save's lifecycle. Added
     * to answer two open questions from LOAD_SAVE_AUTO_ELEMENT_TEST_REPORT.md
     * that this session could not root-cause from source alone: whether Kit
     * Save's mkdir is actually reached/succeeding when a saved Kit fails to
     * appear under /Kit/, and whether an Instrument overwrite's written bytes
     * actually changed (a directory listing cannot tell, since a correct
     * in-place overwrite keeps the same filename). Doubles as direct
     * before/after evidence for the Part 2 HCNAMES-provenance fix: the
     * SOURCE_STAGED checkpoint fires exactly where §2.2-2.5 added the new
     * filesystem_setResidentSource() calls.
     */
    AUTOSAVE_TRACE_STAGE_SAVE_LIFECYCLE = 'O',
```

```c
/*
 * O flags: bits 0..1 element type (0 Kit, 1 Scene, 2 Bank, 3 Instrument);
 * bits 2..4 checkpoint (0 REQUEST, 1 DELETE_RESULT, 2 CREATE_RESULT,
 * 3 SOURCE_STAGED, 4 FINISH); bit 7 FAILED (checkpoint-specific meaning: at
 * DELETE_RESULT/CREATE_RESULT/FINISH it means that step reported an error; at
 * REQUEST/SOURCE_STAGED it is always clear).
 */
#define AUTOSAVE_TRACE_SAVE_LIFECYCLE_TYPE_MASK   0x03u
#define AUTOSAVE_TRACE_SAVE_LIFECYCLE_TYPE_KIT        0u
#define AUTOSAVE_TRACE_SAVE_LIFECYCLE_TYPE_SCENE       1u
#define AUTOSAVE_TRACE_SAVE_LIFECYCLE_TYPE_BANK        2u
#define AUTOSAVE_TRACE_SAVE_LIFECYCLE_TYPE_INSTRUMENT  3u
#define AUTOSAVE_TRACE_SAVE_LIFECYCLE_CHECKPOINT_SHIFT 2u
#define AUTOSAVE_TRACE_SAVE_LIFECYCLE_CHECKPOINT_REQUEST       0u
#define AUTOSAVE_TRACE_SAVE_LIFECYCLE_CHECKPOINT_DELETE_RESULT 1u
#define AUTOSAVE_TRACE_SAVE_LIFECYCLE_CHECKPOINT_CREATE_RESULT 2u
#define AUTOSAVE_TRACE_SAVE_LIFECYCLE_CHECKPOINT_SOURCE_STAGED 3u
#define AUTOSAVE_TRACE_SAVE_LIFECYCLE_CHECKPOINT_FINISH        4u
#define AUTOSAVE_TRACE_SAVE_LIFECYCLE_FLAG_FAILED  (1u << 7u)

/*
 * O value32 layout: bits 0..9 the numbered slot (0..999); bits 10..15
 * unused/zero; bits 16..31 checkpoint-specific: at CREATE_RESULT, a compact
 * CRC32C-derived sixteen-bit fingerprint of the bytes just written (top 16
 * bits of autosave_crc32cUpdate()'s running value over the write buffer —
 * Autosave.c's existing table-free CRC32C, reused as a generic byte
 * fingerprint here, not for its AutoSave-specific record meaning) so an
 * Instrument overwrite's *content* can be compared against a later reload's
 * fingerprint without a manual hex diff; at every other checkpoint, zero.
 */
#define AUTOSAVE_TRACE_SAVE_LIFECYCLE_SLOT_SHIFT   0u
#define AUTOSAVE_TRACE_SAVE_LIFECYCLE_CRC16_SHIFT  16u
```

Call sites (four Save tick functions, one or two lines each):

- **Kit Save request** — `filesystem_requestSaveKitDirectory()`,
  `filesystem.c:20297-20318`, at the end just before `return true;`:
  `autosaveTrace_record(AUTOSAVE_TRACE_STAGE_SAVE_LIFECYCLE, (AUTOSAVE_TRACE_SAVE_LIFECYCLE_CHECKPOINT_REQUEST << AUTOSAVE_TRACE_SAVE_LIFECYCLE_CHECKPOINT_SHIFT) | AUTOSAVE_TRACE_SAVE_LIFECYCLE_TYPE_KIT, (uint32_t)slot << AUTOSAVE_TRACE_SAVE_LIFECYCLE_SLOT_SHIFT);`
- **Kit Save delete result** — `filesystem_saveKitDirectory_tick()` case 5,
  `filesystem.c:12774-12783`, right after `delete_status` is known (both the
  BUSY-return-early path stays silent; log only once BUSY clears):
  add before the existing `if (delete_status == FS_STATUS_ERROR)` check, one
  record with `CHECKPOINT_DELETE_RESULT` and `FLAG_FAILED` set iff
  `delete_status == FS_STATUS_ERROR`.
- **Kit Save mkdir result** — case 9, `filesystem.c:12798-12804`, right after
  `if (!op_file_ready) return;`: one record with `CHECKPOINT_CREATE_RESULT`,
  `FLAG_FAILED` set iff `!op_file`. **This is the direct evidence for "Kit
  Save did not materialize a directory"**: if this record shows
  `FLAG_FAILED` clear on the next repro, mkdir genuinely succeeded and the
  missing-directory report was a stale/misread card snapshot; if it shows
  `FLAG_FAILED` set, `afatfs_mkdir_lfn()` itself is refusing, which is a new,
  narrower bug to chase in `asyncfatfs.c` next session.
- **Kit Save source staged** — immediately after the new
  `filesystem_setResidentSource()` calls added in §2.2: one record with
  `CHECKPOINT_SOURCE_STAGED`.
- **Kit Save finish** — case 21, right before `filesystem_finish(FS_STATUS_DONE)`
  (`filesystem.c:12940`): one record with `CHECKPOINT_FINISH`.
- **Scene Save**: identical four checkpoints at the equivalent
  `filesystem_requestSaveSceneDirectory()`, the delete-status check in
  `filesystem_saveSceneDirectory_tick()` case 5 (`filesystem.c:13551-13560`,
  same shape as Kit's case 5), the Scene mkdir-result wait at case 9
  (`filesystem.c:13575-13581`), and the `SOURCE_STAGED`/self-transition point
  added in §2.3.
- **Bank Save**: `CHECKPOINT_REQUEST` at `filesystem_requestSaveBank()`
  (`filesystem.c:20552`+); `CHECKPOINT_SOURCE_STAGED` at the new call added
  in §2.4; `CHECKPOINT_FINISH` at case 86 right before
  `filesystem_finish(FS_STATUS_DONE)` (`filesystem.c:13458`). Bank Save's
  delete/create checkpoints are Scene Save's (it delegates), already covered.
- **Instrument Save**: `CHECKPOINT_REQUEST` at
  `filesystem_requestSaveInstrumentMode()` (`filesystem.c:21391`+, after
  `filesystem_start()` succeeds). The `CREATE_RESULT` record needs a CRC16
  content fingerprint spanning the whole write, but the write itself
  (case 18, `filesystem.c:11513-11527`) streams through
  `filesystem_writeTextLine()` one buffer at a time across multiple
  foreground polls — there is no single tick where the complete write buffer
  is available. Use the new `op_instrument_save_content_crc` accumulator
  (§3.8): reset it to the CRC32C initial value at case 17 entry
  (`filesystem.c:11501-11506`, beside the existing
  `op_write_line_index`/`op_write_line_len`/`op_write_line_offset` resets),
  call `autosave_crc32cUpdate()` once per byte actually written inside case
  18 (mirroring how `Autosave.c`'s own CRC callers accumulate across
  bounded per-tick intervals), and emit the `CREATE_RESULT` record — with
  `FLAG_FAILED` set iff `!op_file` was seen at case 17, and the accumulator's
  top 16 bits in `value32` bits 16-31 — once case 19 is reached (i.e. after
  the write is known complete, not mid-write). This is the direct evidence
  for "Instrument overwrite content unconfirmed": compare this fingerprint
  against a fingerprint computed the same way from a subsequent Load of the
  same slot. `CHECKPOINT_SOURCE_STAGED` at the new self-transition added in
  §2.5. `CHECKPOINT_FINISH` is naturally covered by the
  `UPDATE_HCNAMES_INSTRUMENT` handoff added in §2.5 rather than a separate
  record here, since that operation's own completion is now the real finish.

### 3.5 Kit Save menu-entry empty-cache diagnostic (`Core/Menu/menu.c:3791-3831`, `menu_requestKitEntryNames()`)

Re-reading this function this session found it is **not obviously buggy**:
`filesystem_libraryNameCacheLoaded(FS_LIBRARY_INDEX_KIT)`
(`filesystem.c:21008-21022`) correctly checks the real cache-domain tag
(`fs_list_cache_kind == FS_NAME_CACHE_KIT`), and when that's false — which it
should be after an intervening Scene Save retags the cache — the function
does correctly fall through to `filesystem_clearNameCache()` +
`filesystem_requestLoadKitIndex()` at `menu.c:3824-3830`. On paper this
should already self-correct. The defect must therefore be in one of two
places this session did not have time to trace to ground truth:

1. The earlier guard at `menu.c:3803-3807`
   (`!menu_residentNameScratchValid || menu_residentNameScratchScene != menu_loadSaveSourceScene`)
   may be taking the *other* branch (`menu_requestResidentNameScratch()`)
   under the exact sequence the test used, and that path was not traced this
   session.
2. `filesystem_requestLoadKitIndex()` → `filesystem_requestReloadLibraryIndex()`
   may itself no-op under some stale precondition not yet examined.

Rather than guess, instrument the decision points directly:

```c
static void menu_requestKitEntryNames(void)
{
    ...
    if (!menu_residentNameScratchValid ||
        menu_residentNameScratchScene != menu_loadSaveSourceScene) {
        /*
         * Diagnostic: record which re-entry branch this session takes.
         *
         * What: one trace record per Kit Save/Load menu entry, capturing
         * whether the resident-name scratch was considered valid/matching.
         * Why: LOAD_SAVE_AUTO_ELEMENT_TEST_REPORT.md's "Kit Save menu shows
         * zero Kits" defect could not be root-caused from source alone this
         * session — filesystem_libraryNameCacheLoaded()'s own tag check looks
         * correct, so the fault is somewhere in this function's branch choice
         * or in filesystem_requestLoadKitIndex()'s own preconditions, neither
         * confirmed yet. This narrows it to one bit of live evidence: which
         * branch actually ran. Inputs: menu_residentNameScratchValid,
         * menu_residentNameScratchScene, menu_loadSaveSourceScene. Outputs:
         * one trace record, no state change. Affiliates:
         * menu_requestResidentNameScratch(), filesystem_libraryNameCacheLoaded().
         */
        autosaveTrace_record(AUTOSAVE_TRACE_STAGE_SAVE_LIFECYCLE,
                             (AUTOSAVE_TRACE_SAVE_LIFECYCLE_CHECKPOINT_REQUEST <<
                              AUTOSAVE_TRACE_SAVE_LIFECYCLE_CHECKPOINT_SHIFT) |
                             AUTOSAVE_TRACE_SAVE_LIFECYCLE_TYPE_KIT,
                             0x00010000u /* branch A: scratch invalid/mismatched */);
        (void)menu_requestResidentNameScratch(menu_loadSaveSourceScene);
        return;
    }
    ...
    if (filesystem_libraryNameCacheLoaded(FS_LIBRARY_INDEX_KIT)) {
        ...
        autosaveTrace_record(AUTOSAVE_TRACE_STAGE_SAVE_LIFECYCLE,
                             (AUTOSAVE_TRACE_SAVE_LIFECYCLE_CHECKPOINT_REQUEST <<
                              AUTOSAVE_TRACE_SAVE_LIFECYCLE_CHECKPOINT_SHIFT) |
                             AUTOSAVE_TRACE_SAVE_LIFECYCLE_TYPE_KIT,
                             0x00020000u /* branch B: cache already tagged Kit, no reload */);
        menu_storageBusy = 0u;
        menu_repaintAll();
        return;
    }
    filesystem_clearNameCache();
    menu_storageBusy = 1u;
    {
        uint8_t accepted = filesystem_requestLoadKitIndex(menu_libraryIndexLoadComplete);
        autosaveTrace_record(AUTOSAVE_TRACE_STAGE_SAVE_LIFECYCLE,
                             (AUTOSAVE_TRACE_SAVE_LIFECYCLE_CHECKPOINT_REQUEST <<
                              AUTOSAVE_TRACE_SAVE_LIFECYCLE_CHECKPOINT_SHIFT) |
                             AUTOSAVE_TRACE_SAVE_LIFECYCLE_TYPE_KIT,
                             0x00030000u | (accepted ? 1u : 0u) /* branch C: reload requested */);
        if (!accepted) {
            menu_storageBusy = 0u;
            menu_deferSelectionRequest = 1u;
            menu_repaintAll();
        }
    }
}
```

(The `0x0001/0x0002/0x0003_0000` literals are placeholder branch tags in
`value32`'s bits 16-31, distinct from the checkpoint's normal
`CRC16_SHIFT` meaning at this call site — document this local reuse
explicitly in the comment when implementing, since `AUTOSAVE_TRACE_SAVE_LIFECYCLE`
is being borrowed here for a Menu-side decision trace rather than a
filesystem Save-lifecycle checkpoint. If this reads as an awkward fit once
written, prefer defining a fifth, Menu-specific stage letter instead of
overloading `'O'`'s value32 contract two different ways — decide during
implementation, not blocked on this plan.)

Once this fires on a real repro of the empty-Kit-list defect, the three
`0x000N_0000` tags directly show which of the three branches ran, which
turns "needs investigation" into a one-line diagnosis.

### 3.6 Bank Save entry — bracket the Menu-side call (`Core/Menu/menu.c:6946-6950`)

```c
                case SAVE_TYPE_BANK:
                    autosaveTrace_record(AUTOSAVE_TRACE_STAGE_SAVE_LIFECYCLE,
                        (AUTOSAVE_TRACE_SAVE_LIFECYCLE_CHECKPOINT_REQUEST <<
                         AUTOSAVE_TRACE_SAVE_LIFECYCLE_CHECKPOINT_SHIFT) |
                        AUTOSAVE_TRACE_SAVE_LIFECYCLE_TYPE_BANK,
                        (uint32_t)menu_currentPresetNr[SAVE_TYPE_BANK] <<
                        AUTOSAVE_TRACE_SAVE_LIFECYCLE_SLOT_SHIFT);
                    if (preset_saveBank(
                            menu_currentPresetNr[SAVE_TYPE_BANK],
                            menu_kitLoadSceneMask))
                        commandAccepted = 1u;
                    break;
```

Together with §3.3's Bank Save entry stall observer, this brackets the
freeze: if the trace file from the next repro has the `REQUEST` record but
no subsequent `PHASE_STALL` record and no `FINISH`, the hang is inside
`filesystem_saveBankDirectory_tick()`'s own phases below its first stall
check (i.e. it's advancing phases too fast/normally for the 20,000-tick
budget to have fired yet, but still never completing — unlikely but
possible, and would itself be useful evidence); if the `REQUEST` record is
present with a subsequent `PHASE_STALL`, the exact stuck phase is known
directly; if the `REQUEST` record itself never appears in the trace at all,
the hang is before `preset_saveBank()` is even called (upstream Menu
state/gating, out of this instrumentation's reach and a distinct next-session
lead — e.g. `menu_storageBusy`/`menu_loadSaveCommandActive` gating not yet
traced this session).

### 3.7 Boot Bank Load embedded-instrument timing (`filesystem.c:9023-9065`, `filesystem_loadSceneDirectory_tick()` case 27, and the Bank child-cursor advance)

The existing boot-logging substep marker
(`filesystem_bootLoggingSetBankSceneDetail('I')` at `filesystem.c:9038`,
confirmed present and firing today — it is what already produces the `...I`
suffix in a captured `bootlog.bin` token like `B012S09I`) only ever retains
the *last* substep reached before a timeout; it cannot show how long each
prior instrument/Scene took. Add a timestamped breadcrumb beside it instead
of replacing it (the existing on-timeout code is still useful for the
worst case where the trace ring itself doesn't get a chance to flush):

```c
    case 27: /* PREPARE/OPEN next embedded instrument */
        if (op_instrument_slot >= STORAGE_KIT_SLOT_COUNT) {
            op_phase = 33;
            return;
        }
        storage_instrumentStateInit(&op_instrument_state, ...);
        instrumentManager_resetSlot(...);
        op_line_len = 0u;
        if (filesystem_bankPayloadDetailActive()) {
            filesystem_bootLoggingSetBankSceneDetail('I');
            /*
             * Timestamped breadcrumb for the boot Bank Load embedded-
             * instrument sequence, alongside the existing single retained
             * substep code.
             *
             * What: one trace record per embedded instrument file this Bank
             * child is about to open, carrying the current Bank-local Scene
             * child cursor and instrument slot.
             *
             * Why: LOAD_SAVE_AUTO_ELEMENT_TEST_REPORT.md's follow-up pass
             * confirmed boot Bank Load still exhausts a 20-second budget
             * (bootlog.bin = B012S09I, i.e. still inside Scene 9's Instrument
             * stage) but the existing bootlog token only ever retains the
             * final substep reached, not a timing history, so there is no way
             * to tell whether the 20 seconds is spread evenly across many
             * Scenes/instruments (inherent I/O volume — a card/volume
             * question, not a bug) or concentrated in one slow step (a real
             * stall worth chasing). asavetrc.bin is confirmed to already
             * capture boot-time records even through a 20-second-timeout run
             * (SD_FREEZE2's asavetrc.bin already showed boot Bank Load I/L
             * marks), so this ring is a safe choice here. This must remain
             * unconditional (not gated on a "logging build" special case
             * beyond the ring's own #if DEV_MODE_LOGGING stub) so it costs
             * nothing extra in non-logging builds, matching every other call
             * site in this plan.
             *
             * Inputs: op_bank_child_cursor, op_instrument_slot. Outputs: one
             * eight-byte trace record; no state change.
             *
             * Affiliates: filesystem_bootLoggingSetBankSceneDetail() (the
             * existing single-retained-code marker this sits beside),
             * autosaveTrace_record()'s tick16 field (the timestamp this
             * instrumentation actually needs — decode consecutive records'
             * tick16 deltas to get per-instrument duration).
             */
            autosaveTrace_record(
                AUTOSAVE_TRACE_STAGE_INSTRUMENT_ENTRY,
                AUTOSAVE_TRACE_INSTRUMENT_ENTRY_PHASE_REQUEST,
                ((uint32_t)op_bank_child_cursor <<
                 AUTOSAVE_TRACE_INSTRUMENT_ENTRY_SCENE_SHIFT) |
                ((uint32_t)op_instrument_slot <<
                 AUTOSAVE_TRACE_INSTRUMENT_ENTRY_SLOT_SHIFT));
        }
        ...
```

This reuses the existing `AUTOSAVE_TRACE_STAGE_INSTRUMENT_ENTRY` ('N') stage
rather than adding a third new one — its documented value32 layout (Scene in
bits 0-3, slot in bits 4-6, type in bits 8-15) already fits this exactly, and
its `PHASE_REQUEST` phase constant is already a generic "about to start"
marker. No `.h` change needed for this hook, only the new `.c` call site.

### 3.8 RAM/behavior accounting for this section

- Two new `autosave_trace_stage_t` enum values (`'X'`, `'O'`) plus their
  `#define` flag/shift constants: zero bytes (compile-time only, the enum's
  underlying storage is the same `uint8_t` record field regardless of how
  many named values it has).
- Six new plain statics for the three stall-observer call sites
  (`op_delete_slot_last_phase`/`_stall_ticks` — already existed in a
  different form, net zero; `op_bank_save_entry_last_phase`/`_stall_ticks`,
  `op_autosave_drain_last_phase`/`_stall_ticks` — new, 10 bytes total,
  normal SRAM1, unconditional — same class of unconditional small diagnostic
  state the pre-existing delete-slot counter already used, not gated behind
  `DEV_MODE_LOGGING`).
- One new plain static for §3.4's Instrument-overwrite CRC16 fingerprint:
  `static uint32_t op_instrument_save_content_crc;` (4 bytes, normal SRAM1,
  unconditional). §3.4's write-path text was written assuming this
  accumulator without declaring or sizing it — `filesystem_writeTextLine()`
  streams the instrument text line-by-line across multiple foreground polls
  (case 18 is not a single-tick, single-buffer write), so the running
  `autosave_crc32cUpdate()` value must survive across those polls in its own
  persistent cell; it cannot be a local. Reset it to the CRC32C initial value
  at case 17 entry (mirroring how `op_write_line_len`/`op_write_line_offset`
  are reset at the same point), accumulate one call per byte written inside
  case 18, and read the top 16 bits of the final value into the
  `CREATE_RESULT` record once case 19 is reached. This total is now folded
  into the 14-byte figure below.
- Total new unconditional normal-SRAM1 bytes for this section: **14 bytes**
  (10 from the three stall observers + 4 from the Instrument CRC
  accumulator). Flag this 14-byte total explicitly to the user per
  `MEMORY.md`'s RAM Allocation Approval Policy before implementing, even
  though it is far below anything that policy has previously required
  discussion for.
- No new file, no new persistent record class, no change to
  `AUTOSAVE_TRACE_RECORD_COUNT`/ring sizing.
- One behavior change beyond pure logging: §3.3's runtime-drain hook can now
  make a previously-infinite operation fail after ~30,000 polls. This is a
  deliberate, minimal safety net for the freeze investigation, not a
  cosmetic addition — call it out to the user as a real (if narrowly scoped)
  behavior change when implementing, distinct from every other hook in this
  section.

---

## Part 4 — Build/rebuild and retest checklist for next session

1. Implement Part 1 (both sites), rebuild, retest per §1.5.
2. Implement Part 2 (all four sites, with §2.5's Instrument fix verified
   against `filesystem_cacheCurrentResidentInstrumentNames()`'s actual field
   reads before trusting the exact hand-off shape shown), rebuild, retest
   per §2.6.
3. Implement Part 3 (shared helper, two new stages, all call sites), rebuild,
   confirm `make -j2` stays warning-clean apart from the pre-existing known
   warnings, and confirm linked `text`/`data`/`bss` deltas match the ~10-byte
   unconditional-static estimate in §3.8 (plus ordinary code-size growth from
   the new call sites; no new `#if DEV_MODE_LOGGING`-gated allocation beyond
   what already existed).
4. Card fixtures, in this order (each layered on the last so evidence
   accumulates rather than requiring six separate full-card resets):
   - Restore `/.hcnames` first (carried over from
     `RECURSIVE_TREE_DELETE_REIMPLEMENT.md`'s still-open follow-up) so
     Load:[Scene]/Load:[Kit] don't false-fail on a missing register before
     any of this session's fixtures run.
   - Scene overwrite (bug #1 retest, §1.5).
   - Four-element Load/Save/AutoSave fixture (bug #2 retest, §2.6) — pull
     `asavetrc.bin` afterward and confirm `SOURCE_STAGED`/`FINISH` `'O'`
     records appear for each Save with `FLAG_FAILED` clear.
   - Kit Save to a new slot specifically, checked against a `/Kit/` listing
     immediately after — cross-reference against the `'O'`
     `CREATE_RESULT` record's `FLAG_FAILED` bit (§3.4).
   - Re-enter Kit Save immediately after a Scene Save in the same power-on
     session (the exact `SD_OVERWRITE_TEST` sequence) — pull the trace and
     read off which of the three `menu_requestKitEntryNames()` branches
     fired (§3.5).
   - Attempt Bank Save entry the same way it froze before — pull the trace
     for the `REQUEST`/`PHASE_STALL` pair (§3.6) and, if it still hangs,
     whatever state can be captured before power-cycling.
   - A full-Bank boot (many Scenes/instruments) timed against the 20 s
     budget — pull `asavetrc.bin` and compute per-instrument `tick16` deltas
     from the new `'N'` records (§3.7).
   - Instrument overwrite to the same slot — pull the `CREATE_RESULT`
     record's CRC16 fingerprint from the Save, then a Load of that same slot
     and compare (needs a matching fingerprint emission added to the Load
     path too if one doesn't already exist there — check before assuming
     this comparison is possible as described, or fingerprint the Save
     record only and cross-check by hand against the actual file bytes for
     this first pass).

---

## Implementation notes — 2026-08-19

- Part 1 is implemented in `filesystem_deleteSlotDirectory_tick()`. Both the
  `FS_DELETE_SLOT_WAIT_CLOSE_SCAN` and `FS_DELETE_SLOT_DELETE_MATCH` verdict
  gates now use only their authoritative scan/native-delete results; the
  timeout latch is diagnostic only. The old delete counter now uses the shared
  `filesystem_pollPhaseStall()` helper and emits the new `'X'` trace record.
- Part 2 is implemented for Kit, Scene, Bank, and normal root Instrument Save.
  Each path stages its direct/inherited HCNAMES source cells at the point that
  precedes its existing register rewrite. Instrument Save now explicitly hands
  off to `FS_INTERNAL_OP_UPDATE_HCNAMES_INSTRUMENT` and then performs a
  single-type physical Instrument rescan before rewriting that type's
  `.hcindex`; this rescan is required because HCNAMES temporarily borrows the
  one shared name cache. It adds no SRAM.
- Part 3 is implemented with `'X'` phase-stall records, `'O'` Save-lifecycle
  records, the Menu Kit-branch/Bank-request witnesses, Bank-load embedded
  Instrument `'N'` breadcrumbs, and a raw-byte CRC32C API for the streamed
  Instrument fingerprint. The runtime AutoSave drain is the only intentional
  behavior change: after roughly 30,000 unchanged polls it enters the existing
  asynchronous writer error-close path.
- The approved unconditional SRAM increase is exactly 14 bytes in normal
  SRAM1: 5 bytes for Bank Save's phase observer, 5 bytes for the drain phase
  observer, and 4 bytes for `op_instrument_save_content_crc`. Delete-slot's
  replacement counter is net-zero because its prior 4-byte timeout counter was
  replaced by a 4-byte stall counter. The linked image reports `bss = 94,624`
  bytes after the change (same total as the preceding build; the new cells fit
  existing layout slack), with `data = 400` bytes. No other SRAM expansion was
  noticed: no trace-ring resize, payload buffer, cache, union, or new persistent
  record storage was added. The added CRC helper has no retained state.
- `make -j2` completes successfully. The output retains the pre-existing
  unused-function warnings in `filesystem.c` and the standard `nano.specs`
  `_close`/`_read`/`_write`/`_lseek` linker warnings. The built image is
  `build/lxr02.elf` / `build/lxr02.bin`, with `text = 381,404`, `data = 400`,
  and `bss = 94,624`.
- Physical SD-card fixtures in Part 4 were not run in this environment. The
  remaining validation is the ordered card checklist above, especially the
  successful-overwrite/no-`ScnS05` proof, the four Save provenance rows, the
  `'X'`/`'O'` trace records, and the Instrument same-slot fingerprint check.

---

## Review — verified independently against the committed diff, 2026-08-19

Re-checked by reading the actual diff (`git diff -- Core/`) line by line
against Parts 1-3 above, not by trusting the implementation-notes summary.
`make -j2` was re-run clean in this pass too (`text=381,404 data=400
bss=94,624`, no new warnings beyond the pre-existing unused-function/nano.specs
set) — the implementer's build numbers check out exactly.

**Confirmed correct, no changes needed:**

- Part 1: both gates (`FS_DELETE_SLOT_WAIT_CLOSE_SCAN` at what is now
  `filesystem.c:12931-12934` and `FS_DELETE_SLOT_DELETE_MATCH`'s removed
  block) match the plan exactly; the timeout latch is fully demoted to
  diagnostic-only.
- Part 2, Kit/Scene/Bank: all three staging sites land exactly where planned,
  before their respective register read/rewrite, with the correct row helpers
  and `FS_RESIDENT_SOURCE_INHERIT`/direct-slot values.
- Part 2, Instrument: independently traced
  `op_kit_load_scene_mask`/`op_slot` through to
  `filesystem_cacheCurrentResidentInstrumentNames()` (`filesystem.c:4547-4568`)
  and confirmed the fields line up exactly with what the new hand-off sets —
  this was the one part of the plan flagged as needing verification before
  trusting it, and it checks out. Also confirmed the `else` dispatch at both
  `filesystem.c:4811-4816` and `4919-4924` already routed
  `FS_INTERNAL_OP_UPDATE_HCNAMES_INSTRUMENT` to
  `filesystem_cacheCurrentResidentInstrumentNames()` before this session —
  that wiring was dormant dead code, not new, and the Instrument Save fix is
  what makes it reachable for the first time.
- The `op_library_index_rebuild_pending` deferred-completion field
  (`filesystem.c:3160`) is consulted generically by the shared completion
  path regardless of which `current_op` calls `filesystem_finish()`, so
  staging it before the `current_op` hand-off to
  `FS_INTERNAL_OP_UPDATE_HCNAMES_INSTRUMENT` correctly chains into the
  Instrument `.hcindex` rebuild once that operation itself finishes. This is
  sound, and is a real design decision the plan didn't fully work out (§2.5
  said only "chain it via the same mechanism") — the implementer noticed
  Instrument's index rebuild is *type-filtered*, not the numbered-folder scan
  Kit/Scene/Bank use, and added `filesystem_startInstrumentIndexRebuildScan()`
  plus the two `FS_NAME_CACHE_INSTRUMENT` branches in
  `filesystem_startLibraryIndexRebuild()`/`filesystem_libraryIndexRebuildScanComplete()`
  to bridge that gap correctly, reusing the pre-existing
  `op_instrument_scan_one_type`/`op_instrument_scan_registry_index` fields the
  same way an existing single-type scan requester already did. This is a
  legitimate, correctly-scoped addition beyond the plan's literal text, not a
  deviation to be concerned about.
- Part 3: `filesystem_pollPhaseStall()` matches the plan exactly, including
  the edge-triggered `== threshold + 1` return. All three call sites
  (delete-slot, Bank Save entry, AutoSave drain) pass the correctly-typed
  phase value (`op_delete_slot_phase` explicitly cast to `uint8_t`; `op_phase`
  passed bare, which is already declared `uint8_t` at `filesystem.c:337` — no
  narrowing risk). The CRC accumulation loop in `filesystem_writeTextLine()`
  hashes exactly the newly-written byte range using the pre-update
  `op_write_line_offset`, correctly handling a line split across multiple
  polls. The Menu-side Kit-entry and Bank-request witnesses land at the exact
  branch points described in §3.5/§3.6.

**Minor findings — none block testing, but worth fixing when next touching
these files:**

1. **Vestigial no-op in the Instrument `CREATE_RESULT` CRC packing**
   (`filesystem.c:11692`, inside case 19):
   `((op_instrument_save_content_crc ^ 0u) & 0xffff0000u) | ...`. `^ 0u` is a
   no-op — this reads like a leftover from an earlier draft that was going to
   apply `autosave_recordCrcFinish()`'s `~crc32c` complement (matching the
   convention every other CRC32C caller in this file uses) and never did. It
   does not break anything: the fingerprint is still deterministic and usable
   for a same-firmware overwrite-vs-reload comparison exactly as designed,
   since nothing else in this plan computes the "finished" (complemented)
   form to compare against. But it's worth either deleting the dead `^ 0u` or
   deciding deliberately whether this fingerprint should go through
   `autosave_recordCrcFinish()` for consistency with the rest of the file's
   CRC32C convention, and saying so in the comment either way.
2. **Redundant assignment** — `op_instrument_index_type = op_instrument_save_type;`
   is set twice for a normal (non-morph) save: once inside the new
   `if (!morph_save)` block (`filesystem.c:11767`) and again unconditionally
   right after the block (`filesystem.c:11793`, pre-existing line, kept for
   the morph path). Harmless — same value both times — but the inner
   assignment is now dead weight; either delete it or add a one-line comment
   noting it's intentionally kept for a future path where the two could
   diverge.
3. **Stall-observer state is never reset at operation start**, for the two
   *new* observers only (`op_bank_save_entry_last_phase`/`_stall_ticks` and
   `op_autosave_drain_last_phase`/`_stall_ticks`) — delete-slot's own
   `op_delete_slot_timeout_observed` latch (not the phase/tick pair) is
   explicitly cleared in `filesystem_deleteSlotDirectoryStart()`, but none of
   the three phase/tick pairs, old or new, are. In practice
   `filesystem_pollPhaseStall()`'s change-detection self-corrects within one
   poll as soon as the new operation's phase diverges from whatever stale
   value was left by the *previous, unrelated* run of the same operation type
   — which is true almost always, since real operations move through phases
   within a handful of polls. The narrow risk is two back-to-back operations
   of the same type both legitimately lingering at the *same* early phase
   number for SD-latency reasons: the second run's stall count would start
   from the first run's leftover count rather than zero, shrinking its
   effective threshold. For the two purely-diagnostic sites (Bank Save entry,
   delete-slot) that only costs an occasionally-early trace record. For the
   **AutoSave drain**, this is the one site where a stall now forces a real
   `FS_STATUS_ERROR` completion (§3.3), so a shrunk effective threshold there
   means a legitimate slow-but-healthy drain could fail somewhat earlier than
   30,000 polls on a second consecutive run. This is low-severity (it still
   takes two consecutive genuinely-slow-at-the-same-phase runs to matter at
   all, and the failure mode is "reports ERROR a bit sooner," not silent data
   loss), so it does not need to block testing — but watch for it explicitly
   in the AutoSave-drain fixture below, and if it shows up, the fix is a
   one-line reset (e.g. `op_autosave_drain_last_phase = 0xffu;`, a phase value
   that can never occur, forced at `filesystem_autosaveParameterDrain_tick()`
   case 0 and `filesystem_saveBankDirectory_tick()` case 0) rather than
   anything structural.
4. **Formatting nit only** — in `menu_requestKitEntryNames()`'s branch-C
   change (`menu.c:3847-3866`), the three statements inside
   `if (!accepted) { ... }` are not indented one level deeper than the `if`
   itself. Cosmetic; `git diff --check` reports no whitespace error and it
   compiles and runs correctly, but it doesn't match the rest of the file's
   indentation and is worth a pass with the normal formatter.
5. **`tools/decode_devlogs.py` does not know about the new stages yet.**
   `STAGE_ENUM`/`STAGE_PRODUCER` (`tools/decode_devlogs.py:94-114`) have no
   `"X"`/`"O"` entries, so every new record decodes today as `unknown stage
   0x58`/`0x4f` with no flags/value breakdown — the raw `stage`/`flags`/
   `tick`/`value` bytes are still printed (the decoder's fallback path
   preserves them), so testing is not blocked, but reading results will mean
   hand-applying the bit layouts from `AutosaveTrace.h` (reproduced in the
   testing plan below) instead of getting a decoded line. This file was out
   of scope for the original plan (which was explicitly `.c`/`.h` only), but
   it is the tool `MEMORY.md` designates for card analysis, so it should be
   updated before or during the next testing pass — flagged here rather than
   done silently, since it's a Python change outside this plan's stated
   scope.

None of these five are correctness bugs in the firmware behavior itself —
items 1-2 are cosmetic, item 3 is a real but low-severity edge case worth
watching for in testing rather than pre-emptively fixing, item 4 is
whitespace, and item 5 is a tooling gap, not a code defect.

**All five fixed, 2026-08-19, rebuilt clean:**

1. Removed the dead `^ 0u` in the Instrument `CREATE_RESULT` CRC packing
   (`filesystem.c`, case 19) and documented explicitly that this is the raw
   running accumulator, never the `autosave_recordCrcFinish()`-complemented
   form, so it must only be compared against another value produced the same
   way.
2. Removed the now-dead inner `op_instrument_index_type = op_instrument_save_type;`
   assignment inside the `if (!morph_save)` block; the pre-existing
   unconditional assignment right after the block still covers both paths.
3. Both new stall observers are now rearmed to an unreachable sentinel phase
   (`0xffu`) at their operation's single admission point —
   `filesystem_requestSaveBank()` for the Bank Save entry observer, and the
   `filesystem_start(FS_INTERNAL_OP_AUTOSAVE_PARAMETER_DRAIN, ...)` call site
   for the drain observer — so a stale count from an unrelated prior request
   of the same operation type can no longer carry forward.
4. Fixed the `menu_requestKitEntryNames()` branch-C indentation to match the
   rest of the file.
5. `tools/decode_devlogs.py` now decodes `'X'`/`'O'` records: `STAGE_ENUM`/
   `STAGE_PRODUCER` entries, and `elif` branches that unpack site/phase/slot
   for `'X'` and type/checkpoint/slot/FAILED (plus the Instrument CRC16 or
   the Kit-entry Menu-branch-tag caveat) for `'O'`. Verified against three
   synthetic records by hand before touching the card.

Rebuilt clean after all five: `text=381,436 data=400 bss=94,624` (bss
unchanged from before these fixes — none of them added storage, only
corrected existing code), same pre-existing warning set, no new ones. The
Part 5 testing plan above is unchanged by these fixes; it was already written
against the corrected behavior.

---

## Part 5 — Testing plan

This assumes Part 4's ordering and folds in what changed once real code (not
a plan) existed to check against: the exact trace fields to read, and the
`decode_devlogs.py` gap from finding 5 above. Read `asavetrc.bin` after every
fixture below with `tools/decode_devlogs.py <path-to-asavetrc.bin>` (single-file
mode, decodes to stdout) even though `'X'`/`'O'` records currently print as
"unknown stage" — the raw `stage=`/`flags=0x..`/`tick=`/`value=0x........`
fields on each line are enough to hand-decode with the tables below, and are
exactly what several steps ask you to read off.

### 0. Prerequisite

Restore a valid `/.hcnames` to the test card first (carried over from
`RECURSIVE_TREE_DELETE_REIMPLEMENT.md`'s still-open follow-up). Confirm
Load:[Scene] and Load:[Kit] no longer report `HNsL01`/`HNkL01`. Every fixture
below assumes a working register; skipping this makes several of them
false-fail for an unrelated reason.

### 1. Bug #1 retest — Scene overwrite no longer reports a spurious error

1. Load a Scene into the active resident slot, edit a parameter, Save to an
   **occupied** root Scene slot (the exact `SD_OVERWRITE_TEST` sequence).
   Confirm the save completes with no `ScnS05` (or `KitS`/`BnkS` equivalent)
   error, and that the target directory was actually replaced on the card
   (old tree gone, new tree present with `sceneset.scg`, embedded `Kit
   <name>/`, `pattern.pat`, `effects.fx`).
2. Decode `asavetrc.bin`. Find the Scene Save's `'X'` record(s), if any
   (`flags & 0x07 == 0` = delete-slot site). Confirm: if present, `value`
   bits 0-7 (the `fs_delete_slot_phase_t` value at the moment of the stall)
   and bit 3 of `flags` (`AUTOSAVE_TRACE_PHASE_STALL_FLAG_IN_NATIVE_DELETE`)
   tell you whether the stall was during the parent scan or inside native
   delete — and that the save still completed successfully regardless.
3. **Force the stall path deliberately**: build a large nested Scene (several
   instruments, a big embedded Kit) specifically to try to cross 50,000 polls
   during its own delete. Confirm the overwrite still succeeds and a `'X'`
   record appears. This is the direct proof the fix works, not just that the
   common case got lucky and never hit the stall counter at all.
4. Regression-check Kit and Bank overwrite the same way (both share
   `filesystem_deleteSlotDirectory_tick()`).

### 2. Bug #2 retest — HCNAMES provenance, all four Save paths

Repeat the `LOAD_SAVE_AUTO_ELEMENT_TEST_REPORT.md` four-element sequence
(delete `/.hcnames`+`/.hcprms*`+`bootlog.bin`+`asavetrc.bin`, boot, Play, load
an element, edit, save to a **new** slot, power off, capture card) once per
element type, and for each:

- Read the saved row directly from `/.hcnames` and confirm its source token
  is the **saved** slot (Kit/Scene/Bank: a 3-digit numbered slot; Instrument:
  `@`), not the loaded one.
- Decode `asavetrc.bin` and find that Save's `'O'` records. Confirm, in
  order: one `REQUEST` (checkpoint bits `[4:2]==0`) with the correct slot in
  `value` bits 0-9; for Kit/Scene one `DELETE_RESULT` (`==1`) and one
  `CREATE_RESULT` (`==2`) both with `FLAG_FAILED` (`flags` bit 7) clear; one
  `SOURCE_STAGED` (`==3`); one `FINISH` (`==4`). Element type is `flags` bits
  0-1 (`0`=Kit `1`=Scene `2`=Bank `3`=Instrument).
- For Instrument specifically, also confirm the row's **name** changed to the
  saved stem, not just its source — this is the deeper defect §2.1/§2.5
  found (the old code published neither).

### 3. Kit-materialization evidence

Save a Kit to a **new** (previously empty) slot. Confirm a `/Kit/NNN Name/`
directory actually appears. Decode `asavetrc.bin` and read the Kit `'O'`
`CREATE_RESULT` record's `FLAG_FAILED` bit:

- Clear + directory present → confirms the fix; no further action.
- Clear + directory absent → the trace itself is misleading (an
  `afatfs_mkdir_lfn()` success that doesn't durably persist); this is a new,
  narrower bug in `asyncfatfs.c`'s create/flush path, not the Save
  state-machine logic this plan touched.
- Set → `afatfs_mkdir_lfn()` itself refused; read the `filesystem_deleteSlotDirectory_tick()`
  and preceding-phase evidence to see what state the parent scan left
  things in.

### 4. Kit Save menu empty-cache evidence

Reproduce the exact `SD_OVERWRITE_TEST` sequence: Scene overwrite, then
immediately enter Kit Save in the same power-on session without rebooting.
Confirm whether the Kit list is empty or populated. Decode `asavetrc.bin` and
find the Kit `'O'` `REQUEST` records with a nonzero value in bits 16-17 (the
Menu branch tag, distinct from the normal slot/CRC use of that field at this
one call site — see §3.5's note on this deliberate field reuse):

- `1` → branch A: resident-name scratch was invalid/scene-mismatched, went
  through `menu_requestResidentNameScratch()` (not traced further by this
  plan — a genuinely different code path).
- `2` → branch B: `filesystem_libraryNameCacheLoaded(FS_LIBRARY_INDEX_KIT)`
  returned true (cache already tagged Kit), so no reload was requested. If
  the Kit list is empty *and* this branch fired, the bug is that this
  function is returning a stale "true" despite `fs_list_cache_kind` actually
  needing a refresh — inspect `fs_list_cache_kind`'s last writer before this
  point.
- `3` (low bit set) → branch C: cache was correctly recognized as stale and
  `filesystem_requestLoadKitIndex()` was called and accepted. If the Kit list
  is still empty after this, the bug is downstream inside the reload itself
  (`filesystem_requestReloadLibraryIndex()`/`filesystem_requestScanKits()`),
  not in the entry-decision logic this plan instrumented.
- `3` (low bit clear) → branch C but the reload request was **rejected**
  (`filesystem_requestLoadKitIndex()` returned false) — read
  `menu_deferSelectionRequest`'s handling from there.

### 5. Bank Save entry freeze evidence

Attempt Bank Save the same way it froze before. Decode `asavetrc.bin`
(pulling the card mid-hang if the freeze recurs and a clean shutdown isn't
possible) and check, in order:

- Is there a Bank `'O'` `REQUEST` record at all? If not, the hang is
  upstream of `preset_saveBank()` even being called — in Menu's page-entry/
  gating state (`menu_storageBusy`/`menu_loadSaveCommandActive`), not
  anywhere this plan instrumented. That is the next session's lead.
- If `REQUEST` is present, is there a `'X'` `PHASE_STALL` record with
  `flags == AUTOSAVE_TRACE_PHASE_STALL_SITE_BANK_ENTRY (1)`? Its `value` bits
  0-7 are the exact `op_phase` Bank Save was stuck at (0 = initial HCNAMES
  open; 80/81 = HCNAMES read; 82 = close; 83-86 = final register rewrite; a
  value ≥ 8 with no payload ever appearing means it stalled after delegating
  to the Scene payload writer, which is not itself instrumented by this
  session's changes).
- If `REQUEST` is present but no `'X'` record ever appears even after a long
  wait, either the hang resolved faster than 20,000 polls (and something
  else entirely is the real symptom on this run), or it's a genuine hard
  lockup that no cooperative check could ever catch — record whichever, both
  are useful negative results.

### 6. Boot Bank Load timing evidence

Boot with a full multi-Scene Bank selected (enough Scenes/instruments to
plausibly approach the 20 s budget). Whether or not it actually times out,
decode `asavetrc.bin` and extract every `'N'`
(`AUTOSAVE_TRACE_STAGE_INSTRUMENT_ENTRY`) record with `flags ==
AUTOSAVE_TRACE_INSTRUMENT_ENTRY_PHASE_REQUEST (1)` emitted during this boot.
For each, `value` bits 0-3 are the Bank-local Scene child index and bits 4-6
the instrument voice slot; `tick` is the 16-bit millisecond-resolution
timestamp (wraps at 65,536 ms — watch for a wrap if the boot run is long).
Compute the delta between consecutive records to get per-instrument load
duration, and look for whether time is spread evenly (an inherent
volume-of-I/O finding, not a bug — the fix would be reducing what boot loads,
not chasing a stall) or concentrated in one outlier step (a real stall worth
tracing further next session).

### 7. Instrument overwrite content verification

Save an Instrument to a **new** slot; decode `asavetrc.bin` and record that
Save's `'O'` `CREATE_RESULT` `value` bits 16-31 (the CRC16 fingerprint — note
finding 1 above: this is the *unfinished* running CRC32C, not the
`~crc32c`-complemented form used elsewhere in this file, so only compare it
against another fingerprint produced the same way, never against a value
computed via `autosave_recordCrcFinish()`). Then overwrite that same slot
with different content and repeat. Confirm the fingerprint changed. There is
currently no equivalent fingerprint emission on the Load path (only Save
emits `'O'` records), so this pass can only prove "Save A produced different
bytes than Save B," not "the Load path reads back what Save wrote" — for the
latter, cross-check the two saved files' bytes by hand for this first pass,
and consider adding a matching Load-side fingerprint in a later session if
this remains a recurring question.

### 8. Regression pass

Re-run the AutoSave health checks from `LOAD_SAVE_AUTO_ELEMENT_TEST_REPORT.md`
(`tools/verify_bank_autosave.py`, the A/V/M/C/P/T sequence) to confirm none of
this session's changes disturbed the already-working AutoSave writer —
particularly the drain's new stall-and-fail path (§3.3/finding 3): confirm a
**normal, healthy** Save/AutoSave cycle never emits a drain `'X'` record at
all. A `'X'` record with `flags ==
AUTOSAVE_TRACE_PHASE_STALL_SITE_DRAIN (2)` on an otherwise-ordinary save is
itself a finding, not expected noise.
